/* nothing.c - no loop here offers a transformation. */
#include <stdio.h>

#define N 3

int v[N];

int main(void)
{
    int i;
    for (i = 0; i < N; i++) {
        v[i] = i * 3 % 7;
    }
    printf("%d\n", v[1]);
    return 0;
}
