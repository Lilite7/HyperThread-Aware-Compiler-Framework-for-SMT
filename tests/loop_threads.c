/* loop_threads.c - threads created in 'for' loops with constant bounds.
 * 4 instances of a streaming worker + 2 instances of a math-heavy worker. */
#include <stdio.h>
#include <pthread.h>
#include <math.h>

#define NS 4
#define NC 2
#define N  500000

double src[N];
double dst[N];
double acc[2];

pthread_t streamers[NS];
pthread_t crunchers[NC];

void *streamer(void *arg)
{
    int i;
    for (i = 0; i < N; i++) {
        dst[i] = src[i] * 1.5 + dst[i];
    }
    return NULL;
}

void *cruncher(void *arg)
{
    double s = 1.0;
    int i;
    for (i = 1; i < 2000000; i++) {
        s += sqrt(i) / (s + 1.0);
    }
    printf("cruncher %f\n", s);
    return NULL;
}

int main(void)
{
    int i;
    for (i = 0; i < N; i++) {
        src[i] = 1.0;
    }
    for (i = 0; i < NS; i++) {
        pthread_create(&streamers[i], NULL, streamer, NULL);
    }
    for (i = 0; i < NC; i++) {
        pthread_create(&crunchers[i], NULL, cruncher, NULL);
    }
    for (i = 0; i < NS; i++) {
        pthread_join(streamers[i], NULL);
    }
    for (i = 0; i < NC; i++) {
        pthread_join(crunchers[i], NULL);
    }
    printf("done %f\n", dst[7]);
    return 0;
}
