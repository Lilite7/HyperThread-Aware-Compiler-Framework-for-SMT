// EXPECT: must have signature
#include <pthread.h>
pthread_t t;
int w(void) { return 0; }
int main(void)
{
    pthread_create(&t, NULL, w, NULL);
    return 0;
}
