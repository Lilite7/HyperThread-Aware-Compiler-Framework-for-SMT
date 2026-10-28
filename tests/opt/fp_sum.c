/* fp_sum.c - a column-order sum of doubles.  Interchange is dependence-legal but would
 * change the order of the floating-point additions, so it needs --fp-reassoc. */
#include <stdio.h>

#define N 3500

double m[N][N];
double total;

int main(void)
{
    int i;
    int j;
    double s = 0.0;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            m[i][j] = (i % 7) * 0.1 + (j % 5) * 0.01;
        }
    }
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            s += m[i][j];
        }
    }
    total = s;
    printf("%f\n", total);
    return 0;
}
