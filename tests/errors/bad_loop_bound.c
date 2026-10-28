// EXPECT: needs a 'for' loop with constant bounds
#include <pthread.h>
pthread_t t[8];
void *w(void *arg) { return NULL; }
int main(void)
{
    int i;
    int n = 5;
    for (i = 0; i < n; i++) {
        pthread_create(&t[i], NULL, w, NULL);
    }
    return 0;
}
