/* unroll_licm.c - the multiplier alpha*beta + sqrt(alpha) is the same on every iteration
 * (loop-invariant code motion), and the inner loop is long enough to unroll. */
#include <stdio.h>
#include <math.h>

#define N 3000003

double a[N];
double b[N];
double c[N];
double alpha = 1.5;
double beta = 0.25;

int main(void)
{
    int i;
    int r;
    for (i = 0; i < N; i++) {
        a[i] = i * 0.5;
        b[i] = (N - i) * 0.25;
    }
    for (r = 0; r < 8; r++) {
        for (i = 0; i < N; i++) {
            c[i] = a[i] * (alpha * beta + sqrt(alpha)) + b[i];
        }
    }
    printf("%f\n", c[7]);
    return 0;
}
