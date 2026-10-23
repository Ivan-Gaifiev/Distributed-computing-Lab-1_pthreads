#ifndef COMMON_H
#define COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline void check_pthread(int rc, const char *operation) {
    if (rc) {
        fprintf(stderr, "%s: %s (%d)\n", operation, strerror(rc), rc);
        /* Unexpected primitive failure: continuing would invalidate ownership
         * or synchronization. pthread_create is handled separately by main. */
        exit(EXIT_FAILURE);
    }
}

static inline double monotonic_seconds(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
#endif
