#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<string.h>
#include<pthread.h>

const double t_step = 3600.0; //= 1 hour (in seconds)
const double G = 6.67430e-11; // m^3 / (kg * s^2)
int thread_count;
int global_stop = 0;

typedef struct {
    double m; // mass
    double x, y; // position
    double vx, vy; // velosities
} Body; 

typedef struct {
    double fx, fy; 
} Force;

typedef struct {
    Body *bodies;
    Force *main_force;
    Force *forces;
    pthread_barrier_t *start, *forces_done, *bodies_start, *done;
    int n;
    int id;
    int thread_count;
    double h_time;
    int *global_stop;
    int error;
} ThreadArgs;

void *routine_forces(void* arg){
    ThreadArgs *args = arg;
    while(1){
        pthread_barrier_wait(args->start);
        if(*args->global_stop){
            break;
        }
        args->error = 0;
        memset(args->forces, 0, args->n * sizeof(*args->forces)); // update forces to zero
        for (int i = args->id; i<args->n && !args->error; i+=args->thread_count){
            for (int j = i+1; j<args->n; ++j){ // count force for each pair only once
                double dx = args->bodies[j].x - args->bodies[i].x;
                double dy = args->bodies[j].y - args->bodies[i].y;
                double r = sqrt(dx*dx + dy*dy);
                if (r == 0){
                        fprintf(stderr, "Division by zero");
                        args->error = 1;
                        break; // leave pair loop, but still reach barriers
                    }
                double force_x, force_y;
                // Global gravitation law (with 3rd Newton's law)
                force_x = G * args->bodies[i].m * args->bodies[j].m * dx / (r*r*r);  
                force_y = G * args->bodies[i].m * args->bodies[j].m * dy / (r*r*r);
                args->forces[i].fx += force_x;
                args->forces[i].fy += force_y;
                args->forces[j].fx -= force_x;
                args->forces[j].fy -= force_y;  
            }
        }
        pthread_barrier_wait(args->forces_done); 
        pthread_barrier_wait(args->bodies_start);
        if(*args->global_stop){
            break;
        }
        // Euler's step (update x,y,vx,vy)
        for (int i = args->id; i<args->n; i+=args->thread_count){
            args->bodies[i].x += args->bodies[i].vx * args->h_time;
            args->bodies[i].y += args->bodies[i].vy * args->h_time;
          
            args->bodies[i].vx += args->main_force[i].fx * args->h_time / args->bodies[i].m;
            args->bodies[i].vy += args->main_force[i].fy * args->h_time / args->bodies[i].m; 
        }
        pthread_barrier_wait(args->done);        
    }
    return NULL;
}

int main(int argc, char** argv){

    // check correct cmd launch
    if (argc != 4) {
        fprintf(stderr, "Correct usage: %s input.txt t_end thread_count\n", argv[0]);
        return 1;
    }
    // try to open file
    FILE *input = fopen(argv[1], "r");
    if (input == NULL){
        perror("Cannot open input");
        return 1;
    }
    // read t_end parameter
    char *end;
    double t_end = strtod(argv[2], &end);
    if (end == argv[2] || *end != '\0' || !isfinite(t_end) || t_end < 0){
        fprintf(stderr, "Invalide end time\n");
        fclose(input);
        return 1;
    }
    // read first line of the file
    int n;
    if (fscanf(input, "%d", &n) != 1 || n <= 0){
        fprintf(stderr, "Invalid number of bodies\n");
        fclose(input);
        return 1;
    }
    // allocate memory for n bodies
    Body *bodies = malloc(n * sizeof(*bodies));
    if (bodies == NULL) {
        fprintf(stderr, "Cannot allocate memory\n");
        fclose(input);
        return 1;
    }
    // read n bodies from the file and write them to the array
    for (int i=0; i<n; ++i){
        int counter = fscanf(input, "%lf %lf %lf %*lf %lf %lf %*lf",
            &bodies[i].m,
            &bodies[i].x,
            &bodies[i].y,
            &bodies[i].vx,
            &bodies[i].vy
        );
        if (counter != 5){
            fprintf(stderr, "Invalid body %d\n", i+1);
            free(bodies);
            fclose(input);
            return 1;
        }
    }
    // create main array of forces for each body
    Force *main_forces = calloc(n, sizeof(*main_forces)); // all forces equal zero
    if (main_forces == NULL) {
        fprintf(stderr, "Cannot allocate forces\n");
        free(bodies);
        fclose(input);
        return 1;
    }
    // initialising memory for threads
    long thread;
    pthread_t* thread_handles;
    thread_count = strtol(argv[3], NULL, 10);
    if (thread_count <= 0){
        fprintf(stderr, "Invalid thread count\n");
        free(main_forces);
        free(bodies);
        fclose(input);
        return 1;
    }
    thread_handles = malloc(thread_count * sizeof(pthread_t));
    if (thread_handles == NULL){
        fprintf(stderr, "Cannot allocate thread handles\n");
        free(main_forces);
        free(bodies);
        fclose(input);
        return 1;
    }

    // Prepare barriers
    pthread_barrier_t start, forces_done, bodies_start, done;
    if (pthread_barrier_init(&start, NULL, thread_count + 1) != 0 ||
        pthread_barrier_init(&forces_done, NULL, thread_count + 1) != 0 ||
        pthread_barrier_init(&bodies_start, NULL, thread_count + 1) != 0 ||
        pthread_barrier_init(&done, NULL, thread_count + 1) != 0){
        fprintf(stderr, "Cannot initialize barriers\n");
        exit(EXIT_FAILURE);
    }


    // Prepare arguments for threads
    ThreadArgs * args = calloc(thread_count, sizeof(*args));
    if (args == NULL){
        fprintf(stderr, "Cannot allocate arguments\n");
        exit(EXIT_FAILURE);
    }
    for(int i=0; i<thread_count; ++i){
        args[i].bodies = bodies; // pointer to main array for bodies
        args[i].main_force = main_forces;
        args[i].forces = calloc(n, sizeof(*args[i].forces)); // create private forces
        if (args[i].forces == NULL){
            fprintf(stderr, "Cannot allocate private forces\n");
            exit(EXIT_FAILURE);
        }
        args[i].id = i;
        args[i].n = n;
        args[i].thread_count = thread_count;
        args[i].start = &start;
        args[i].forces_done = &forces_done;
        args[i].bodies_start = &bodies_start;
        args[i].done = &done;
        args[i].h_time = 0;
        args[i].global_stop = &global_stop;
        args[i].error = 0;
    }
    // create threads
    for (thread = 0; thread < thread_count; thread++){
        if (pthread_create(&thread_handles[thread], NULL, routine_forces, &args[thread]) != 0){
            fprintf(stderr, "Cannot create thread\n");
            exit(EXIT_FAILURE); // terminate process: barrier participants are missing
        }
    }
    // Prepare output file for writing coordinates
    double t_curr = 0.0;
    FILE * output = fopen("output_coordinates.csv", "w");
    if (output == NULL){
        perror("Error opening output file");
        return 1;
    }
    // starting coordinates
    fprintf(output, "%lf", t_curr);
    for(int i = 0; i < n; ++i)
        fprintf(output, ";%lf;%lf", bodies[i].x, bodies[i].y);
    fprintf(output, "\n");
    
    // Main loop with calculation coordinates of bodies on each step
    while(t_curr < t_end){
        double h = fmin(t_step, t_end - t_curr);
        for(int i =0; i<thread_count; ++i){
            args[i].h_time = h;
        }
        // recalculate current forces
        pthread_barrier_wait(&start);
        pthread_barrier_wait(&forces_done);
        // check workers' errors
        for (int i=0; i<thread_count; ++i){
            if (args[i].error == 1)
                global_stop = 1;
        }
        if (!global_stop){
        memset(main_forces, 0, n * sizeof(*main_forces)); // main_forces to zeros
        // summing private forces to main array
        for(int i=0; i<n; i++){
            for(int j=0; j<thread_count; ++j){
                main_forces[i].fx += args[j].forces[i].fx;
                main_forces[i].fy += args[j].forces[i].fy;
            }
        }
        }
        pthread_barrier_wait(&bodies_start);
        if (global_stop)
            break; // workers exit after bodies_start; do not wait for done
        pthread_barrier_wait(&done);

        t_curr += h; // next time step

        // write new coordinates to output file
        fprintf(output, "%lf", t_curr);
        for(int i = 0; i < n; ++i)
            fprintf(output, ";%lf;%lf", bodies[i].x, bodies[i].y);
        fprintf(output, "\n");
    }

    // Stop command
    if (!global_stop){
        global_stop = 1;
        pthread_barrier_wait(&start);
    }
    for (thread = 0; thread < thread_count; thread++){
        pthread_join(thread_handles[thread], NULL);
    }

    pthread_barrier_destroy(&start);
    pthread_barrier_destroy(&done);
    pthread_barrier_destroy(&forces_done);
    pthread_barrier_destroy(&bodies_start);

    free(thread_handles);
    int failed = 0;
    for (int i = 0; i < thread_count; ++i){
        if (args[i].error) failed = 1;
        free(args[i].forces);
    }
    free(args);
    free(main_forces);
    free(bodies);
    fclose(input);
    if (fclose(output) != 0){
        perror("Error writing output");
        failed = 1;
    }
    return failed;
}