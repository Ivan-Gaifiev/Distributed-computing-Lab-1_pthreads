#include "my_rwlock.h"
#include <errno.h>

/* Called with mutex held. Wakeups grant no ownership: the waiter must recheck
 * its predicate, since wakeups may be spurious or another thread may win. */
static int wake_waiters(my_rwlock_t *lock) {
    if (lock->active_writer) return 0;
    if (lock->waiting_writers) {
        if (!lock->active_readers)
            return pthread_cond_signal(&lock->writers);
    } else if (lock->waiting_readers) {
        return pthread_cond_broadcast(&lock->readers);
    }
    return 0;
}

int my_rwlock_init(my_rwlock_t *lock) {
    int rc = pthread_mutex_init(&lock->mutex, NULL);
    if (rc) return rc;
    rc = pthread_cond_init(&lock->readers, NULL);
    if (rc) {
        pthread_mutex_destroy(&lock->mutex);
        return rc;
    }
    rc = pthread_cond_init(&lock->writers, NULL);
    if (rc) {
        pthread_cond_destroy(&lock->readers);
        pthread_mutex_destroy(&lock->mutex);
        return rc;
    }
    lock->active_readers = 0;
    lock->waiting_readers = 0;
    lock->waiting_writers = 0;
    lock->active_writer = 0;
    return 0;
}

int my_rwlock_rdlock(my_rwlock_t *lock) {
    int rc = pthread_mutex_lock(&lock->mutex);
    if (rc) return rc;
    lock->waiting_readers++;
    while (lock->active_writer || lock->waiting_writers) {
        rc = pthread_cond_wait(&lock->readers, &lock->mutex);
        if (rc) break;
    }
    lock->waiting_readers--;
    if (!rc) lock->active_readers++;
    int unlock_rc = pthread_mutex_unlock(&lock->mutex);
    return rc ? rc : unlock_rc;
}

int my_rwlock_wrlock(my_rwlock_t *lock) {
    int rc = pthread_mutex_lock(&lock->mutex);
    if (rc) return rc;
    lock->waiting_writers++;
    while (lock->active_writer || lock->active_readers) {
        rc = pthread_cond_wait(&lock->writers, &lock->mutex);
        if (rc) break;
    }
    lock->waiting_writers--;
    if (!rc) lock->active_writer = 1;
    else (void)wake_waiters(lock);
    int unlock_rc = pthread_mutex_unlock(&lock->mutex);
    return rc ? rc : unlock_rc;
}

int my_rwlock_unlock(my_rwlock_t *lock) {
    int rc = pthread_mutex_lock(&lock->mutex);
    if (rc) return rc;
    if (lock->active_writer) lock->active_writer = 0;
    else if (lock->active_readers) lock->active_readers--;
    else rc = EPERM;
    if (!rc) rc = wake_waiters(lock);
    int unlock_rc = pthread_mutex_unlock(&lock->mutex);
    return rc ? rc : unlock_rc;
}

int my_rwlock_destroy(my_rwlock_t *lock) {
    int rc = pthread_mutex_lock(&lock->mutex);
    if (rc) return rc;
    int busy = lock->active_writer || lock->active_readers ||
               lock->waiting_readers || lock->waiting_writers;
    rc = pthread_mutex_unlock(&lock->mutex);
    if (rc) return rc;
    if (busy) return EBUSY;
    rc = pthread_cond_destroy(&lock->readers);
    if (rc) return rc;
    rc = pthread_cond_destroy(&lock->writers);
    if (rc) return rc;
    return pthread_mutex_destroy(&lock->mutex);
}
