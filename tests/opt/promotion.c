/* promotion.c - classic matrix multiply: c[i][j] is the same element for every k.
 * Keeping it in a register removes a load and a store from the inner loop.
 * The second loop nest touches c[i][j+1] as well, so promotion must be refused there. */
#include <stdio.h>

#define N 450

double a[N][N];
double b[N][N];
double c[N][N];
double d[N][N];

int main(void)
{
    int i;
    int j;
    int k;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            a[i][j] = (i + j) * 0.01;
            b[i][j] = (i - j) * 0.02;
        }
    }
    for (i = 0; i < N; i++) {
        for (j = 0; j < N - 1; j++) {
            for (k = 0; k < N; k++) {
                c[i][j] += a[i][k] * b[k][j];
            }
        }
    }
    for (i = 0; i < N; i++) {
        for (j = 0; j < N - 1; j++) {
            for (k = 0; k < 64; k++) {
                d[i][j] += d[i][j + 1] * 0.5 + a[i][k];
            }
        }
    }
    printf("%f %f\n", c[3][4], d[3][4]);
    return 0;
}
