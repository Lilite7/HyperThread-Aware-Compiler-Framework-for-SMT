/* mixed.c - two memory-streaming threads and two FP-compute threads.
 * A good SMT mapping pairs each streaming thread with a compute thread. */
#include <stdio.h>
#include <pthread.h>

#define N      2000000
#define REPS   10
#define ITERS  30000000

double a[N];
double b[N];
double c[N];
double x[N];
double y[N];
double z[N];

pthread_t t[4];

void *stream_a(void *arg)
{
    int r;
    int i;
    for (r = 0; r < REPS; r++) {
        for (i = 0; i < N; i++) {
            a[i] = b[i] * 3.0 + c[i];
        }
    }
    return NULL;
}

void *stream_b(void *arg)
{
    int r;
    int i;
    for (r = 0; r < REPS; r++) {
        for (i = 0; i < N; i++) {
            x[i] = y[i] * 2.0 + z[i];
        }
    }
    return NULL;
}

void *fp_compute_1(void *arg)
{
    double s = 1.0;
    int i;
    for (i = 0; i < ITERS; i++) {
        s = s * 1.0000001 + 0.5;
    }
    printf("fp_compute_1 = %f\n", s);
    return NULL;
}

void *fp_compute_2(void *arg)
{
    double s = 2.0;
    int i;
    for (i = 0; i < ITERS; i++) {
        s = s * 0.9999999 + 0.25;
    }
    printf("fp_compute_2 = %f\n", s);
    return NULL;
}

int main(void)
{
    int i;
    for (i = 0; i < N; i++) {
        b[i] = 1.0;
        c[i] = 2.0;
        y[i] = 3.0;
        z[i] = 4.0;
    }
    pthread_create(&t[0], NULL, stream_a, NULL);
    pthread_create(&t[1], NULL, stream_b, NULL);
    pthread_create(&t[2], NULL, fp_compute_1, NULL);
    pthread_create(&t[3], NULL, fp_compute_2, NULL);
    pthread_join(t[0], NULL);
    pthread_join(t[1], NULL);
    pthread_join(t[2], NULL);
    pthread_join(t[3], NULL);
    printf("done a[5]=%f x[5]=%f\n", a[5], x[5]);
    return 0;
}
