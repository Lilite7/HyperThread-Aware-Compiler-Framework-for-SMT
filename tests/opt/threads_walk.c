/* threads_walk.c - each worker walks its own pair of matrices column by column.
 * The optimizer should interchange the loops inside the workers; the thread
 * mapping and pinning are then computed for the optimized program. */
#include <stdio.h>
#include <pthread.h>

#define N 1800

double a1[N][N];
double b1[N][N];
double a2[N][N];
double b2[N][N];
pthread_t t[2];

void *w1(void *arg)
{
    int i;
    int j;
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            b1[i][j] = a1[i][j] + 1.0;
        }
    }
    return NULL;
}

void *w2(void *arg)
{
    int i;
    int j;
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            b2[i][j] = a2[i][j] * 2.0 + 3.0;
        }
    }
    return NULL;
}

int main(void)
{
    pthread_create(&t[0], NULL, w1, NULL);
    pthread_create(&t[1], NULL, w2, NULL);
    pthread_join(t[0], NULL);
    pthread_join(t[1], NULL);
    printf("%f %f\n", b1[2][3], b2[4][5]);
    return 0;
}
