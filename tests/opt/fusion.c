/* fusion.c - the first pair of loops can be fused (loop 2 reads a[i] right after loop 1
 * wrote it).  The second pair cannot: loop 2 reads x[i+1], which loop 1 only writes
 * one iteration later after fusion. */
#include <stdio.h>

#define N 6000000

double a[N];
double b[N];
double c[N];
double x[N];
double y[N];

int main(void)
{
    int i;
    for (i = 0; i < N; i++) {
        b[i] = i * 0.5;
    }
    for (i = 0; i < N; i++) {
        a[i] = b[i] * 2.0;
    }
    for (i = 0; i < N; i++) {
        c[i] = a[i] + 1.0;
    }
    for (i = 0; i < N - 1; i++) {
        x[i] = b[i] * 3.0;
    }
    for (i = 0; i < N - 1; i++) {
        y[i] = x[i + 1] + 1.0;
    }
    printf("%f %f\n", c[5], y[5]);
    return 0;
}
