/* patterns.c - access-pattern showcase: sequential, strided (column walk of a
 * 2-D array), and pointer chasing (data-dependent index). */
#include <stdio.h>
#include <pthread.h>

#define R 1024
#define C 1024
#define M 1000000

double grid[R][C];
int nxt[M];
double total[4];
pthread_t th[3];

void *row_walk(void *arg)
{
    int i;
    int j;
    double s = 0.0;
    for (i = 0; i < R; i++) {
        for (j = 0; j < C; j++) {
            s += grid[i][j];
        }
    }
    total[0] = s;
    return NULL;
}

void *col_walk(void *arg)
{
    int i;
    int j;
    double s = 0.0;
    for (j = 0; j < C; j++) {
        for (i = 0; i < R; i++) {
            s += grid[i][j];
        }
    }
    total[1] = s;
    return NULL;
}

void *chase(void *arg)
{
    int i;
    int x = 0;
    for (i = 0; i < 3000000; i++) {
        x = nxt[x];
    }
    printf("chase ended at %d\n", x);
    return NULL;
}

int main(void)
{
    int i;
    for (i = 0; i < M; i++) {
        nxt[i] = (i * 17 + 13) % M;
    }
    pthread_create(&th[0], NULL, row_walk, NULL);
    pthread_create(&th[1], NULL, col_walk, NULL);
    pthread_create(&th[2], NULL, chase, NULL);
    pthread_join(th[0], NULL);
    pthread_join(th[1], NULL);
    pthread_join(th[2], NULL);
    printf("%f %f\n", total[0], total[1]);
    return 0;
}
