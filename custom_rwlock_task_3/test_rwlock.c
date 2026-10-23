#include "common.h"
#include "list.h"
#include "lock_backend.h"
#include <errno.h>
#include <sched.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
    exit(EXIT_FAILURE); } } while (0)
#define OK(x) check_pthread((x), #x)

typedef struct {
    my_rwlock_t lock;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int reader_entered, release_reader;
    int writer_entered, release_writer;
    int late_entered, sequence;
} ordered_test;

static void *holding_reader(void *p) {
    ordered_test *s = p;
    OK(my_rwlock_rdlock(&s->lock));
    OK(pthread_mutex_lock(&s->mutex));
    s->reader_entered = 1;
    OK(pthread_cond_broadcast(&s->cond));
    while (!s->release_reader) OK(pthread_cond_wait(&s->cond, &s->mutex));
    OK(pthread_mutex_unlock(&s->mutex));
    OK(my_rwlock_unlock(&s->lock));
    return NULL;
}

static void *ordered_writer(void *p) {
    ordered_test *s = p;
    OK(my_rwlock_wrlock(&s->lock));
    OK(pthread_mutex_lock(&s->mutex));
    REQUIRE(s->sequence == 0);
    s->sequence = 1;
    s->writer_entered = 1;
    OK(pthread_cond_broadcast(&s->cond));
    while (!s->release_writer) OK(pthread_cond_wait(&s->cond, &s->mutex));
    OK(pthread_mutex_unlock(&s->mutex));
    OK(my_rwlock_unlock(&s->lock));
    return NULL;
}

static void *late_reader(void *p) {
    ordered_test *s = p;
    OK(my_rwlock_rdlock(&s->lock));
    OK(pthread_mutex_lock(&s->mutex));
    REQUIRE(s->sequence == 1);
    s->sequence = 2;
    s->late_entered = 1;
    OK(pthread_cond_broadcast(&s->cond));
    OK(pthread_mutex_unlock(&s->mutex));
    OK(my_rwlock_unlock(&s->lock));
    return NULL;
}

/* White-box observation under the implementation's mutex ensures a waiter has
 * actually registered, not merely that its thread has started. No sleeps are
 * used as evidence of blocking. Timeout is only a test-failure safeguard. */
static void await_waiters(my_rwlock_t *lock, size_t readers, size_t writers) {
    double deadline = monotonic_seconds() + 10;
    for (;;) {
        OK(pthread_mutex_lock(&lock->mutex));
        int ready = lock->waiting_readers == readers &&
                    lock->waiting_writers == writers;
        OK(pthread_mutex_unlock(&lock->mutex));
        if (ready) return;
        REQUIRE(monotonic_seconds() < deadline);
        sched_yield();
    }
}

static void test_ordering(void) {
    ordered_test s = {0};
    pthread_t reader, writer, late;
    OK(my_rwlock_init(&s.lock));
    OK(pthread_mutex_init(&s.mutex, NULL));
    OK(pthread_cond_init(&s.cond, NULL));
    OK(my_rwlock_rdlock(&s.lock));
    REQUIRE(my_rwlock_destroy(&s.lock) == EBUSY);
    OK(pthread_create(&reader, NULL, holding_reader, &s));
    OK(pthread_mutex_lock(&s.mutex));
    while (!s.reader_entered) OK(pthread_cond_wait(&s.cond, &s.mutex));
    OK(pthread_mutex_unlock(&s.mutex));
    OK(pthread_mutex_lock(&s.lock.mutex));
    REQUIRE(s.lock.active_readers == 2); /* Actual overlapping read owners. */
    OK(pthread_mutex_unlock(&s.lock.mutex));

    OK(pthread_create(&writer, NULL, ordered_writer, &s));
    await_waiters(&s.lock, 0, 1);
    OK(my_rwlock_unlock(&s.lock)); /* One reader remains: writer must wait. */
    OK(pthread_create(&late, NULL, late_reader, &s));
    await_waiters(&s.lock, 1, 1);
    OK(pthread_mutex_lock(&s.lock.mutex));
    REQUIRE(s.lock.active_readers == 1 && !s.lock.active_writer);
    /* Wakeups without a predicate change must not admit either waiter. */
    OK(pthread_cond_broadcast(&s.lock.readers));
    OK(pthread_cond_broadcast(&s.lock.writers));
    OK(pthread_mutex_unlock(&s.lock.mutex));

    OK(pthread_mutex_lock(&s.mutex));
    REQUIRE(!s.writer_entered && !s.late_entered);
    s.release_reader = 1;
    OK(pthread_cond_broadcast(&s.cond));
    while (!s.writer_entered) OK(pthread_cond_wait(&s.cond, &s.mutex));
    REQUIRE(!s.late_entered);
    s.release_writer = 1;
    OK(pthread_cond_broadcast(&s.cond));
    OK(pthread_mutex_unlock(&s.mutex));
    OK(pthread_join(reader, NULL));
    OK(pthread_join(writer, NULL));
    OK(pthread_join(late, NULL));
    REQUIRE(s.sequence == 2 && s.late_entered);
    REQUIRE(my_rwlock_unlock(&s.lock) == EPERM);
    OK(my_rwlock_destroy(&s.lock));
    OK(pthread_cond_destroy(&s.cond));
    OK(pthread_mutex_destroy(&s.mutex));
    puts("PASS overlapping readers, last-reader wait, writer priority, wakeups");
}

static void test_list(void) {
    list_node *head = NULL;
    size_t length;
    REQUIRE(!list_member(head, 10));
    REQUIRE(!list_delete(&head, 10));
    REQUIRE(list_insert(&head, 20) == 1);
    REQUIRE(list_insert(&head, 10) == 1);
    REQUIRE(list_insert(&head, 30) == 1);
    REQUIRE(list_insert(&head, 20) == 0);
    REQUIRE(list_member(head, 10) && list_member(head, 20) && list_member(head, 30));
    REQUIRE(list_validate(head, &length) && length == 3);
    head->next->key = 10;
    REQUIRE(!list_validate(head, &length));
    head->next->key = 20;
    head->next->next->next = head;
    REQUIRE(!list_validate(head, &length));
    head->next->next->next = NULL;
    REQUIRE(list_delete(&head, 20));
    REQUIRE(list_delete(&head, 10));
    REQUIRE(list_delete(&head, 30));
    REQUIRE(!list_delete(&head, 30));
    REQUIRE(list_validate(head, &length) && length == 0);
    list_free(&head);
    REQUIRE(!head);
    puts("PASS sequential list and invariant checker");
}

enum { NTHREADS = 8, ITERATIONS = 2000 };
typedef struct {
    lock_backend lock;
    pthread_barrier_t barrier;
    pthread_mutex_t monitor;
    int readers, writers, value;
    list_node *head;
} stress_shared;
typedef struct { stress_shared *shared; int rank; } stress_arg;

static void *stress_worker(void *p) {
    stress_arg *a = p;
    stress_shared *s = a->shared;
    int rc = pthread_barrier_wait(&s->barrier);
    REQUIRE(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    for (int i = 0; i < ITERATIONS; i++) {
        int write = ((i + a->rank) % 4 == 0);
        OK(write ? s->lock.write(&s->lock.storage) : s->lock.read(&s->lock.storage));
        OK(pthread_mutex_lock(&s->monitor));
        REQUIRE(!s->writers);
        if (write) { REQUIRE(!s->readers); s->writers++; }
        else s->readers++;
        OK(pthread_mutex_unlock(&s->monitor));
        int before = s->value;
        if (write) {
            s->value++;
            REQUIRE(list_insert(&s->head, a->rank) == 1);
        } else (void)list_member(s->head, a->rank);
        sched_yield();
        if (write) REQUIRE(list_delete(&s->head, a->rank) == 1);
        else REQUIRE(s->value == before);
        OK(pthread_mutex_lock(&s->monitor));
        if (write) { REQUIRE(s->writers == 1 && !s->readers); s->writers--; }
        else { REQUIRE(!s->writers); s->readers--; }
        OK(pthread_mutex_unlock(&s->monitor));
        OK(s->lock.unlock(&s->lock.storage));
    }
    return NULL;
}

static void test_stress(int custom) {
    stress_shared s = {0};
    pthread_t threads[NTHREADS];
    stress_arg args[NTHREADS];
    OK(backend_init(&s.lock, custom));
    OK(pthread_mutex_init(&s.monitor, NULL));
    OK(pthread_barrier_init(&s.barrier, NULL, NTHREADS));
    for (int i = 0; i < NTHREADS; i++) {
        args[i] = (stress_arg){&s, i};
        OK(pthread_create(&threads[i], NULL, stress_worker, &args[i]));
    }
    for (int i = 0; i < NTHREADS; i++) OK(pthread_join(threads[i], NULL));
    REQUIRE(s.value == NTHREADS * ITERATIONS / 4);
    REQUIRE(!s.readers && !s.writers && !s.head);
    size_t length;
    REQUIRE(list_validate(s.head, &length) && length == 0);
    OK(s.lock.destroy(&s.lock.storage));
    OK(pthread_barrier_destroy(&s.barrier));
    OK(pthread_mutex_destroy(&s.monitor));
    printf("PASS %s concurrent exclusion, stable reads, list operations\n",
           custom ? "custom" : "pthread");
}

int main(void) {
    test_list();
    test_ordering();
    test_stress(1);
    test_stress(0);
    puts("All C tests passed.");
    return 0;
}
