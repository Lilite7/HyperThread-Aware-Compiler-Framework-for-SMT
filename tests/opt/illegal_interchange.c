/* illegal_interchange.c - a[i][j] depends on a[i-1][j+1] (distance (1,-1)).
 * Swapping the loops would run the dependence backwards: must be refused. */
#include <stdio.h>

#define N 2500

double a[N][N];

int main(void)
{
    int i;
    int j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            a[i][j] = i * 0.25 + j;
        }
    }
    for (j = 0; j < N - 1; j++) {
        for (i = 1; i < N; i++) {
            a[i][j] = a[i - 1][j + 1] + 1.0;
        }
    }
    printf("%f\n", a[N - 1][3]);
    return 0;
}
