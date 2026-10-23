#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    double get_time_sec(void) {
        LARGE_INTEGER freq, cnt;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&cnt);
        return (double)cnt.QuadPart / (double)freq.QuadPart;
    }
#else
    double get_time_sec(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
    }
#endif

typedef struct {
    double real;
    double imag;
} Complex;

pthread_mutex_t mutex;
int  points_found = 0;
int  target_points = 0;
int  keep_running = 1;
Complex* results = NULL;

unsigned int xorshift32(unsigned int *state) {
    unsigned int x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// https://ru.wikipedia.org/wiki/Множество_Мандельброта?ysclid=mvjbfcqpd384103307

int is_in_mandelbrot(double real, double imag) {
    double z_real = 0.0, z_imag = 0.0;
    const int max_iter = 1000;
    for (int i = 0; i < max_iter; i++) {
        double zr2 = z_real * z_real;
        double zi2 = z_imag * z_imag;
        if (zr2 + zi2 > 4.0) {
            return 0;
        }
        double nr = zr2 - zi2 + real;
        double ni = 2.0 * z_real * z_imag + imag;
        z_real = nr;
        z_imag = ni;
    }
    return 1;
}

void* worker(void* arg) {
    unsigned int *seed_ptr = (unsigned int*)arg;
    while (1) {
        pthread_mutex_lock(&mutex);
        if (!keep_running || points_found >= target_points) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        pthread_mutex_unlock(&mutex);
        double real = -2.0 + 3.0 * ((double)xorshift32(seed_ptr) / UINT_MAX);
        double imag = -1.5 + 3.0 * ((double)xorshift32(seed_ptr) / UINT_MAX);
        if (is_in_mandelbrot(real, imag)) {
            pthread_mutex_lock(&mutex);
            if (points_found < target_points) {
                results[points_found].real = real;
                results[points_found].imag = imag;
                points_found++;
                if (points_found >= target_points) {
                    keep_running = 0;
                }
            }
            pthread_mutex_unlock(&mutex);
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    int nthreads  = atoi(argv[1]);
    target_points = atoi(argv[2]);
    points_found = 0;
    keep_running = 1;
    results = (Complex*)malloc(target_points * sizeof(Complex));
    pthread_mutex_init(&mutex, NULL);
    pthread_t* threads = (pthread_t*)malloc(nthreads * sizeof(pthread_t));
    unsigned int* seeds = (unsigned int*)malloc(nthreads * sizeof(unsigned int));
    for (int i = 0; i < nthreads; i++) {
        seeds[i] = (unsigned int)(time(NULL) + i * 1000 + 1);
    }
    double t_start = get_time_sec();
    for (int i = 0; i < nthreads; i++) {
        pthread_create(&threads[i], NULL, worker, &seeds[i]);
    }
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }
    double t_end = get_time_sec();
    long long elapsed_us = (long long)((t_end - t_start) * 1000000.0);
    FILE* fp = fopen("result.csv", "w");
    fprintf(fp, "real,imag\n");
    for (int i = 0; i < points_found; i++)
        fprintf(fp, "%.6f,%.6f\n", results[i].real, results[i].imag);
    fclose(fp);
    printf("%d %lld\n", nthreads, elapsed_us);
    free(results);
    free(threads);
    free(seeds);
    pthread_mutex_destroy(&mutex);
    return 0;
}