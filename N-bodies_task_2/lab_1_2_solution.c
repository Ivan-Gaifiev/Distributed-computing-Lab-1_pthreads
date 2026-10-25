#include<stdio.h>
#include<stdlib.h>
#include<math.h>
#include<string.h>
#include<pthread.h>

const double t_step = 3600.0; //in seconds
const double G = 6.67430e-11; //in Newtons
int thread_count;


typedef struct {
    double m; // mass
    double x, y; // position
    double vx, vy; // velosities
} Body; 

typedef struct {
    double fx, fy; 
} Force;

typedef struct {
    const Body *bodies;
    Force *forces;
    int n;
    int id;
    int thread_count;
    int error;
} ThreadArgs;

void *routine_forces(void* arg){
    ThreadArgs *args = arg;
    for (int i = args->id; i<args->n; i+=args->thread_count){
        for (int j = i+1; j<args->n; ++j){ // count force for each pair only once
            double dx = args->bodies[j].x - args->bodies[i].x;
            double dy = args->bodies[j].y - args->bodies[i].y;
            double r = sqrt(dx*dx + dy*dy);
            if (r == 0){
                    fprintf(stderr, "Division by zero");
                    args->error = 1;
                    return NULL;
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
    // initialising memory for threads
    long thread;
    pthread_t* thread_handles;
    thread_count = strtol(argv[3], NULL, 10);
    thread_handles = malloc(thread_count * sizeof(pthread_t));

    /*  */
    for (thread = 0; thread < thread_count; thread++){
        pthread_create(&thread_handles[thread], NULL, routine_forces, (void*) ?);
    }

    for (thread = 0; thread < thread_count; thread++){
        pthread_join(thread_handles[thread], ?);
    }

    for (thread = 0; thread < thread_count; thread++){
        free(?[thread]);
        pthread_cond_destroy(&cond[thread]);
    }

    free();
    free(cond);
    free(thread_handles);



    Force *forces = calloc(n, sizeof(*forces)); // all forces equal zero
    if (forces == NULL) {
        fprintf(stderr, "Cannot allocate forces\n");
        free(bodies);
        fclose(input);
        return 1;
    }

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


    while(t_curr < t_end){
        double h = fmin(t_step, t_end - t_curr);
        
        memset(forces, 0, n * sizeof(*forces)); // update forces to zero

        // recalculate current forces
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) { // count force for each pair only once
                double dx = bodies[j].x - bodies[i].x;
                double dy = bodies[j].y - bodies[i].y;
                double r = sqrt(dx*dx + dy*dy);
                if (r == 0){
                    fprintf(stderr, "Division by zero");
                    free(bodies);
                    free(forces);
                    fclose(input);
                    return 1;}
                double force_x, force_y;
                // Global gravitation law (with 3rd Newton's law)
                force_x = G * bodies[i].m * bodies[j].m * dx / (r*r*r);  
                force_y = G * bodies[i].m * bodies[j].m * dy / (r*r*r);
                forces[i].fx += force_x;
                forces[i].fy += force_y;
                forces[j].fx -= force_x;
                forces[j].fy -= force_y;  
            }
        }

        // Euler's steps (update x,y,vx,vy)
        for (int i=0; i<n; ++i){
            bodies[i].x += bodies[i].vx * h;
            bodies[i].y += bodies[i].vy * h;

            bodies[i].vx += forces[i].fx * h / bodies[i].m;
            bodies[i].vy += forces[i].fy * h / bodies[i].m; 
        }
        
        t_curr += h;

        // write new coordinates to output file
        if (output == NULL){
            perror("Error opening output file");
            return 1;
        }
        fprintf(output, "%lf", t_curr);
        for(int i = 0; i < n; ++i)
            fprintf(output, ";%lf;%lf", bodies[i].x, bodies[i].y);
        fprintf(output, "\n");

    }

    free(forces);
    free(bodies);
    fclose(input);
    fclose(output);
    return 0;

}