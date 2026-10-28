/* threads_shared.c - two instances of the same worker update the same global element.
 * Promotion would hide the other thread's updates, so it must be refused; the loop
 * with a private array is fine. */
#include <stdio.h>
#include <pthread.h>

#define N 400000

double shared[4];
double mine[2];
pthread_t t[2];

void *worker(void *arg)
{
    int i;
    for (i = 0; i < N; i++) {
        shared[0] += i * 0.5;
    }
    return NULL;
}

int main(void)
{
    int i;
    for (i = 0; i < 2; i++) {
        pthread_create(&t[i], NULL, worker, NULL);
    }
    for (i = 0; i < 2; i++) {
        pthread_join(t[i], NULL);
    }
    printf("done\n");
    return 0;
}
