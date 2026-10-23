#include "common.h"
#include "list.h"
#include "lock_backend.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

enum { READ, INSERT, DELETE };
typedef struct { int key; unsigned char kind; } operation;
typedef struct {
    int custom;
    size_t threads, initial, ops;
    double read, insert;
    uint64_t seed;
    int key_range;
} config;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    size_t ready;
    int go, abort;
} start_gate;
typedef struct {
    lock_backend *lock;
    list_node **head;
    const operation *operations;
    size_t begin, end;
    start_gate *gate;
    size_t counts[3], success[3];
    double finished;
    int error;
} worker_args;

static void usage(FILE *stream) {
    fprintf(stream,
        "Usage: rwlock_bench [--lock custom|pthread] [--threads N]\n"
        "  [--initial N] [--ops N] [--read F] [--insert F]\n"
        "  [--seed N] [--key-range N]\n"
        "Defaults: custom, 1 thread, 1000 keys, 100000 ops, read=0.9,\n"
        "insert=0.05, seed=1, key-range=max(2*initial,1).\n"
        "Fractions are in [0,1]; deletion fraction is 1-read-insert.\n"
        "Output: one CSV data row, no header (see README.md).\n");
}

static int parse_uint(const char *s, uint64_t *value) {
    if (!*s) return 0;
    for (const char *p = s; *p; p++) if (*p < '0' || *p > '9') return 0;
    errno = 0;
    char *end;
    unsigned long long n = strtoull(s, &end, 10);
    if (errno || *end || n > UINT64_MAX) return 0;
    *value = (uint64_t)n;
    return 1;
}

static int parse_fraction(const char *s, double *value) {
    errno = 0;
    char *end;
    double v = strtod(s, &end);
    if (end == s || *end || errno || !isfinite(v) || v < 0 || v > 1) return 0;
    *value = v;
    return 1;
}

static int parse_config(int argc, char **argv, config *c) {
    const char *names[] = {"--lock", "--threads", "--initial", "--ops",
                          "--read", "--insert", "--seed", "--key-range"};
    unsigned seen = 0;
    uint64_t threads = 1, initial = 1000, ops = 100000, range = 0;
    *c = (config){.custom = 1, .read = 0.9, .insert = 0.05, .seed = 1};
    for (int i = 1; i < argc; i += 2) {
        int option = -1;
        for (int j = 0; j < 8; j++) if (!strcmp(argv[i], names[j])) option = j;
        if (option < 0 || i + 1 == argc || (seen & (1u << option))) return 0;
        seen |= 1u << option;
        const char *v = argv[i + 1];
        switch (option) {
        case 0:
            if (strcmp(v, "custom") && strcmp(v, "pthread")) return 0;
            c->custom = !strcmp(v, "custom"); break;
        case 1: if (!parse_uint(v, &threads)) return 0; break;
        case 2: if (!parse_uint(v, &initial)) return 0; break;
        case 3: if (!parse_uint(v, &ops)) return 0; break;
        case 4: if (!parse_fraction(v, &c->read)) return 0; break;
        case 5: if (!parse_fraction(v, &c->insert)) return 0; break;
        case 6: if (!parse_uint(v, &c->seed)) return 0; break;
        case 7: if (!parse_uint(v, &range)) return 0; break;
        }
    }
    if (!(seen & (1u << 7))) {
        if (initial > INT_MAX / 2) return 0;
        range = initial ? 2 * initial : 1;
    }
    if (!threads || threads > SIZE_MAX / sizeof(pthread_t) ||
        threads > SIZE_MAX / sizeof(worker_args) || !ops ||
        ops > SIZE_MAX / sizeof(operation) || !range || range > INT_MAX ||
        initial > range || c->read + c->insert > 1.0) return 0;
    c->threads = (size_t)threads;
    c->initial = (size_t)initial;
    c->ops = (size_t)ops;
    c->key_range = (int)range;
    return 1;
}

/* SplitMix64: fixed-width arithmetic gives the same workload on Windows/Linux.
 * State is used only during single-threaded preparation. Not cryptographic. */
static uint64_t random_next(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static uint64_t random_bounded(uint64_t *state, uint64_t bound) {
    uint64_t threshold = (UINT64_C(0) - bound) % bound;
    uint64_t value;
    do { value = random_next(state); } while (value < threshold);
    return value % bound;
}

static uint64_t prepare_operations(operation *ops, const config *c) {
    /* Integer rounding may leave fewer than two additional deletions. */
    size_t reads = (size_t)((long double)c->read * c->ops);
    size_t inserts = (size_t)((long double)c->insert * c->ops);
    if (inserts > c->ops - reads) inserts = c->ops - reads;
    uint64_t state = c->seed;
    for (size_t i = 0; i < c->ops; i++) {
        ops[i].kind = i < reads ? READ : i < reads + inserts ? INSERT : DELETE;
        ops[i].key = (int)random_bounded(&state, (uint64_t)c->key_range);
    }
    for (size_t i = c->ops; i > 1; i--) {
        size_t j = (size_t)random_bounded(&state, i);
        operation tmp = ops[i - 1];
        ops[i - 1] = ops[j];
        ops[j] = tmp;
    }
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < c->ops; i++) {
        hash = (hash ^ (unsigned)ops[i].key) * UINT64_C(1099511628211);
        hash = (hash ^ ops[i].kind) * UINT64_C(1099511628211);
    }
    return hash;
}

static int prepare_list(list_node **head, const config *c) {
    list_node **tail = head;
    for (size_t i = 0; i < c->initial; i++) {
        list_node *node = malloc(sizeof(*node));
        if (!node) return ENOMEM;
        /* Uniformly spaced distinct keys, independent of lock/thread count. */
        node->key = (int)((uint64_t)i * (uint64_t)c->key_range / c->initial);
        node->next = NULL;
        *tail = node;
        tail = &node->next;
    }
    return 0;
}

static void *worker(void *arg) {
    worker_args *a = arg;
    start_gate *gate = a->gate;
    check_pthread(pthread_mutex_lock(&gate->mutex), "start lock");
    gate->ready++;
    check_pthread(pthread_cond_broadcast(&gate->cond), "ready broadcast");
    while (!gate->go && !gate->abort)
        check_pthread(pthread_cond_wait(&gate->cond, &gate->mutex), "start wait");
    int aborted = gate->abort;
    check_pthread(pthread_mutex_unlock(&gate->mutex), "start unlock");
    if (aborted) return NULL;

    size_t counts[3] = {0}, success[3] = {0};
    for (size_t i = a->begin; i < a->end; i++) {
        const operation *op = &a->operations[i];
        void *storage = &a->lock->storage;
        check_pthread(op->kind == READ ? a->lock->read(storage) :
                      a->lock->write(storage), "rwlock acquire");
        int result = op->kind == READ ? list_member(*a->head, op->key) :
                     op->kind == INSERT ? list_insert(a->head, op->key) :
                     list_delete(a->head, op->key);
        check_pthread(a->lock->unlock(storage), "rwlock release");
        if (result < 0) { a->error = ENOMEM; break; }
        counts[op->kind]++;
        success[op->kind] += (size_t)result;
    }
    /* Timestamp before aggregation, join, validation or freeing the list. */
    a->finished = monotonic_seconds();
    memcpy(a->counts, counts, sizeof(counts));
    memcpy(a->success, success, sizeof(success));
    return NULL;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--help")) { usage(stdout); return 0; }
    config c;
    if (!parse_config(argc, argv, &c)) {
        fprintf(stderr, "Invalid arguments.\n");
        usage(stderr);
        return EXIT_FAILURE;
    }
    operation *operations = malloc(c.ops * sizeof(*operations));
    pthread_t *threads = calloc(c.threads, sizeof(*threads));
    worker_args *args = calloc(c.threads, sizeof(*args));
    list_node *head = NULL;
    if (!operations || !threads || !args || prepare_list(&head, &c)) {
        fprintf(stderr, "Not enough memory for workload.\n");
        list_free(&head); free(operations); free(threads); free(args);
        return EXIT_FAILURE;
    }
    uint64_t workload_id = prepare_operations(operations, &c);
    lock_backend lock;
    check_pthread(backend_init(&lock, c.custom), "rwlock init");
    start_gate gate = {.ready = 0, .go = 0, .abort = 0};
    check_pthread(pthread_mutex_init(&gate.mutex, NULL), "start mutex init");
    check_pthread(pthread_cond_init(&gate.cond, NULL), "start cond init");
    size_t created = 0, begin = 0;
    int error = 0;
    for (; created < c.threads; created++) {
        size_t count = c.ops / c.threads + (created < c.ops % c.threads);
        args[created] = (worker_args){.lock = &lock, .head = &head,
            .operations = operations, .begin = begin, .end = begin + count,
            .gate = &gate};
        begin += count;
        /* Fault injection is enabled only in a separate test executable. */
#ifdef LAB3_TEST_CREATE_FAIL_AFTER
        if (created >= LAB3_TEST_CREATE_FAIL_AFTER) error = EAGAIN;
        else
#endif
        error = pthread_create(&threads[created], NULL, worker, &args[created]);
        if (error) {
            fprintf(stderr, "pthread_create after %zu threads: %s\n",
                    created, strerror(error));
            break;
        }
    }
    double start = 0;
    check_pthread(pthread_mutex_lock(&gate.mutex), "main start lock");
    if (error) gate.abort = 1;
    else {
        while (gate.ready < c.threads)
            check_pthread(pthread_cond_wait(&gate.cond, &gate.mutex), "ready wait");
        start = monotonic_seconds();
        gate.go = 1;
    }
    check_pthread(pthread_cond_broadcast(&gate.cond), "start broadcast");
    check_pthread(pthread_mutex_unlock(&gate.mutex), "main start unlock");
    for (size_t i = 0; i < created; i++)
        check_pthread(pthread_join(threads[i], NULL), "join");

    size_t counts[3] = {0}, success[3] = {0}, length = 0;
    double finish = start;
    for (size_t i = 0; i < created; i++) {
        if (args[i].error) error = args[i].error;
        if (args[i].finished > finish) finish = args[i].finished;
        for (int j = 0; j < 3; j++) {
            counts[j] += args[i].counts[j];
            success[j] += args[i].success[j];
        }
    }
    if (!error && (!list_validate(head, &length) ||
        counts[0] + counts[1] + counts[2] != c.ops ||
        c.initial + success[INSERT] < success[DELETE] ||
        length != c.initial + success[INSERT] - success[DELETE])) {
        fprintf(stderr, "List or operation-count invariant failed.\n");
        error = EINVAL;
    }
    double elapsed = finish - start;
    if (!error && elapsed <= 0) {
        fprintf(stderr, "Nonpositive measurement interval.\n");
        error = EINVAL;
    }
    check_pthread(pthread_cond_destroy(&gate.cond), "start cond destroy");
    check_pthread(pthread_mutex_destroy(&gate.mutex), "start mutex destroy");
    check_pthread(lock.destroy(&lock.storage), "rwlock destroy");
    list_free(&head);
    free(operations); free(threads); free(args);
    if (error) {
        fprintf(stderr, "Run failed: %s\n", strerror(error));
        return EXIT_FAILURE;
    }
    printf("%s,%zu,%zu,%zu,%.17g,%.17g,%" PRIu64 ",%d,"
           "%zu,%zu,%zu,%zu,%zu,%zu,%zu,%016" PRIx64 ",%.9f,%.3f\n",
           c.custom ? "custom" : "pthread", c.threads, c.initial, c.ops,
           c.read, c.insert, c.seed, c.key_range, counts[READ], counts[INSERT],
           counts[DELETE], success[READ], success[INSERT], success[DELETE],
           length, workload_id, elapsed, c.ops / elapsed);
    return EXIT_SUCCESS;
}
