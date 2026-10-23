#include "lock_backend.h"

static int custom_read(void *p) { return my_rwlock_rdlock(p); }
static int custom_write(void *p) { return my_rwlock_wrlock(p); }
static int custom_unlock(void *p) { return my_rwlock_unlock(p); }
static int custom_destroy(void *p) { return my_rwlock_destroy(p); }
static int native_read(void *p) { return pthread_rwlock_rdlock(p); }
static int native_write(void *p) { return pthread_rwlock_wrlock(p); }
static int native_unlock(void *p) { return pthread_rwlock_unlock(p); }
static int native_destroy(void *p) { return pthread_rwlock_destroy(p); }

int backend_init(lock_backend *lock, int custom) {
    if (custom) {
        lock->read = custom_read;
        lock->write = custom_write;
        lock->unlock = custom_unlock;
        lock->destroy = custom_destroy;
        return my_rwlock_init(&lock->storage.custom);
    }
    lock->read = native_read;
    lock->write = native_write;
    lock->unlock = native_unlock;
    lock->destroy = native_destroy;
    return pthread_rwlock_init(&lock->storage.native, NULL);
}
