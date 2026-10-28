/* transpose.c - the second nest walks both matrices column by column.
 * Interchange is legal (no loop-carried dependence) and exact. */
#include <stdio.h>

#define N 3000

double a[N][N];
double b[N][N];

int main(void)
{
    int i;
    int j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            a[i][j] = i + j * 0.5;
        }
    }
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            b[i][j] = a[i][j] * 2.0;
        }
    }
    printf("%f\n", b[3][4]);
    return 0;
}
