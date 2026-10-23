#ifndef MY_RWLOCK_H
#define MY_RWLOCK_H

#include <pthread.h>
#include <stddef.h>

/* Nonrecursive, writer-preference lock. No upgrade, cancellation or ownership
 * tracking. Destroy only after all users have finished. Returns error codes,
 * like pthreads, rather than setting errno. */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t readers;
    pthread_cond_t writers;
    size_t active_readers;
    size_t waiting_readers;
    size_t waiting_writers;
    int active_writer;
} my_rwlock_t;

int my_rwlock_init(my_rwlock_t *lock);
int my_rwlock_rdlock(my_rwlock_t *lock);
int my_rwlock_wrlock(my_rwlock_t *lock);
int my_rwlock_unlock(my_rwlock_t *lock);
int my_rwlock_destroy(my_rwlock_t *lock);

#endif
