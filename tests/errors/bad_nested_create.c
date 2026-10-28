// EXPECT: inside nested loops is not supported
#include <pthread.h>
pthread_t t[16];
void *w(void *arg) { return NULL; }
int main(void)
{
    int i;
    int j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            pthread_create(&t[i], NULL, w, NULL);
        }
    }
    return 0;
}
