#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <pthread.h>

#ifdef _WIN32
    #include <windows.h>
    static double get_time_sec(void) {
        LARGE_INTEGER freq, cnt;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&cnt);
        return (double)cnt.QuadPart / (double)freq.QuadPart;
    }
#else
    #include <time.h>
    static double get_time_sec(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
    }
#endif

// https://ru.wikihow.com/вручную-начертить-множество-Мандельброта?ysclid=mvjb5joreo188840716

#define WIDTH 1600
#define HEIGHT 1200
#define X_MIN (-2.2)
#define X_MAX ( 1.0)
#define Y_MIN (-1.2)
#define Y_MAX ( 1.2)

static uint8_t* image = NULL;
static int g_max_iter = 1000;

typedef struct {
    int thread_id;
    int num_threads;
} ThreadArg;

static void* render_worker(void* arg) {
    ThreadArg* ta = (ThreadArg*)arg;
    int tid = ta->thread_id;
    int n = ta->num_threads;
    int rows_per_thread = (HEIGHT + n - 1) / n;
    int y_start = tid * rows_per_thread;
    int y_end = y_start + rows_per_thread;
    if (y_end > HEIGHT) {
        y_end = HEIGHT;
    }
    const double dx = (X_MAX - X_MIN) / (WIDTH  - 1);
    const double dy = (Y_MAX - Y_MIN) / (HEIGHT - 1);
    const int max_iter = g_max_iter;
    for (int py = y_start; py < y_end; py++) {
        double y = Y_MAX - py * dy;
        for (int px = 0; px < WIDTH; px++) {
            double x = X_MIN + px * dx;
            double zr = 0.0, zi = 0.0;
            double zr2 = 0.0, zi2 = 0.0;
            int iter = 0;
            while (iter < max_iter && (zr2 + zi2) <= 4.0) {
                zi  = 2.0 * zr * zi + y;
                zr  = zr2 - zi2 + x;
                zr2 = zr * zr;
                zi2 = zi * zi;
                iter++;
            }
            uint8_t r, g, b;
            if (iter >= max_iter) {
                r = g = b = 0;
            } 
            else {
                double smooth = iter + 1.0 - log(log(sqrt(zr2 + zi2))) / log(2.0);
                double t = smooth * 0.05;
                r = (uint8_t)(255.0 * (0.5 + 0.5 * sin(t)));
                g = (uint8_t)(255.0 * (0.5 + 0.5 * sin(t + 2.094)));
                b = (uint8_t)(255.0 * (0.5 + 0.5 * sin(t + 4.188)));
            }
            int idx = 3 * (py * WIDTH + px);
            image[idx + 0] = r;
            image[idx + 1] = g;
            image[idx + 2] = b;
        }
    }
    return NULL;
}

static int write_bmp(const char* filename, const uint8_t* rgb, int w, int h) {
    int row_size  = (w * 3 + 3) & ~3;
    int data_size = row_size * h;
    int file_size = 54 + data_size;
    FILE* fp = fopen(filename, "wb");
    uint8_t fh[14] = {0};
    fh[0] = 'B'; 
    fh[1] = 'M';
    *(uint32_t*)(fh + 2)  = (uint32_t)file_size;
    *(uint32_t*)(fh + 10) = 54;
    fwrite(fh, 1, 14, fp);
    uint8_t dh[40] = {0};
    *(uint32_t*)(dh + 0)  = 40;
    *(int32_t* )(dh + 4)  = w;
    *(int32_t* )(dh + 8)  = h;
    *(uint16_t*)(dh + 12) = 1;
    *(uint16_t*)(dh + 14) = 24;
    *(uint32_t*)(dh + 20) = (uint32_t)data_size;
    fwrite(dh, 1, 40, fp);
    uint8_t* row = (uint8_t*)calloc(row_size, 1);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            row[3*x + 0] = rgb[3*(y*w + x) + 2];
            row[3*x + 1] = rgb[3*(y*w + x) + 1];
            row[3*x + 2] = rgb[3*(y*w + x) + 0];
        }
        fwrite(row, 1, row_size, fp);
    }
    free(row);
    fclose(fp);
    return 0;
}

int main(int argc, char* argv[]) {
    int nthreads = 4;
    const char* out = "mandelbrot.bmp";
    if (argc >= 2) nthreads = atoi(argv[1]);
    if (argc >= 3) out = argv[2];
    if (argc >= 4) g_max_iter = atoi(argv[3]);
    if (nthreads < 1) nthreads = 1;
    if (nthreads > 64) nthreads = 64;
    if (g_max_iter < 1) g_max_iter = 1;
    printf("Rendering %dx%d, max_iter=%d, threads=%d\n", WIDTH, HEIGHT, g_max_iter, nthreads);
    image = (uint8_t*)malloc((size_t)WIDTH * HEIGHT * 3);
    pthread_t* threads = (pthread_t*)malloc(nthreads * sizeof(pthread_t));
    ThreadArg* args = (ThreadArg*)malloc(nthreads * sizeof(ThreadArg));
    double t0 = get_time_sec();
    for (int i = 0; i < nthreads; i++) {
        args[i].thread_id = i;
        args[i].num_threads = nthreads;
        pthread_create(&threads[i], NULL, render_worker, &args[i]);
    }
    for (int i = 0; i < nthreads; i++) {
        pthread_join(threads[i], NULL);
    }
    double t1 = get_time_sec();
    printf("Render time: %.3f s\n", t1 - t0);
    if (write_bmp(out, image, WIDTH, HEIGHT) != 0) {
        free(image); free(threads); free(args);
        return 1;
    }
    free(image);
    free(threads);
    free(args);
    return 0;
}