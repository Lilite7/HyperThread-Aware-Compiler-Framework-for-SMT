/* mutex.c - two workers update a shared counter under the same mutex;
 * a third worker is independent. */
#include <stdio.h>
#include <pthread.h>

#define ROUNDS 200000

int counter = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_t th[3];

void *inc_worker(void *arg)
{
    int i;
    for (i = 0; i < ROUNDS; i++) {
        pthread_mutex_lock(&lock);
        counter += 1;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

void *dec_worker(void *arg)
{
    int i;
    for (i = 0; i < ROUNDS; i++) {
        pthread_mutex_lock(&lock);
        counter -= 1;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

void *independent(void *arg)
{
    double s = 0.0;
    int i;
    for (i = 0; i < ROUNDS; i++) { s += i * 0.5; }
    printf("independent %f\n", s);
    return NULL;
}

int main(void)
{
    pthread_create(&th[0], NULL, inc_worker, NULL);
    pthread_create(&th[1], NULL, dec_worker, NULL);
    pthread_create(&th[2], NULL, independent, NULL);
    pthread_join(th[0], NULL);
    pthread_join(th[1], NULL);
    pthread_join(th[2], NULL);
    printf("counter = %d\n", counter);
    return 0;
}
