// EXPECT: attribute argument must be NULL
#include <pthread.h>
pthread_t t;
void *w(void *arg) { return NULL; }
int main(void)
{
    pthread_create(&t, 1, w, NULL);
    return 0;
}
