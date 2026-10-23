#ifndef LOCK_BACKEND_H
#define LOCK_BACKEND_H
#include "my_rwlock.h"

typedef struct {
    union { my_rwlock_t custom; pthread_rwlock_t native; } storage;
    int (*read)(void *);
    int (*write)(void *);
    int (*unlock)(void *);
    int (*destroy)(void *);
} lock_backend;

int backend_init(lock_backend *lock, int custom);
#endif
