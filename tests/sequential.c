/* sequential.c - threads are created and joined one after another, so they
 * never overlap and cannot contend for SMT resources. */
#include <stdio.h>
#include <pthread.h>

#define N 1000000

double a[N];
double b[N];
pthread_t t1;
pthread_t t2;

void *first(void *arg)
{
    int i;
    for (i = 0; i < N; i++) { a[i] = b[i] + 1.0; }
    return NULL;
}

void *second(void *arg)
{
    int i;
    for (i = 0; i < N; i++) { b[i] = a[i] * 2.0; }
    return NULL;
}

int main(void)
{
    pthread_create(&t1, NULL, first, NULL);
    pthread_join(t1, NULL);
    pthread_create(&t2, NULL, second, NULL);
    pthread_join(t2, NULL);
    printf("%f %f\n", a[3], b[3]);
    return 0;
}
