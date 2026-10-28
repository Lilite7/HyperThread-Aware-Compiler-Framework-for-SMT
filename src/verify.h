/* verify.h - differential testing and benchmarking */
#ifndef HT_VERIFY_H
#define HT_VERIFY_H

#include "opt.h"

typedef struct {
    char dir[256];
    const OptOptions *o;
    int counter;
} Bench;

int vb_have_cc(const char *cc);

/* emit + compile; returns 1 on success, binary path in bin, compiler output in err otherwise */
int vb_build(Bench *B, const Node *program, int instrument, char *bin, size_t bn, char *err, size_t en);

/* 1 = identical on every run, 0 = a difference was found (see note),
 * -1 = the original does not run cleanly so nothing could be verified */
int vb_differential(Bench *B, const char *bin_a, const char *bin_b, int fp_mode,
                    int *runs, int *passed, char *note, size_t nn);

typedef struct { int runs; double min_a, min_b, med_a, med_b; } BenchResult;

/* interleaved timing (alternating order); 0 if a run failed */
int vb_bench(Bench *B, const char *bin_a, const char *bin_b, int runs, BenchResult *out);

#endif
