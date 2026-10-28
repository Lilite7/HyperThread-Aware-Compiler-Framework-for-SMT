/* verify.c - differential testing and benchmarking of program variants.
 *
 *   build      emit C (plain or instrumented), compile with the C compiler
 *   differ     run original and variant on the program's own data and on
 *              pseudo-random data; compare a fingerprint of all global memory
 *              and the printed output
 *   bench      interleaved timing runs, median wall time
 *
 * WHAT THIS ESTABLISHES (and what it does not):  a passing differential test is
 * evidence that the two programs agree on those inputs, not a proof that they agree
 * on every input.  The proof of legality comes from the dependence analysis in
 * depend.c; this is the independent second line of defence that catches mistakes in
 * that analysis or in the rewriting.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include "opt.h"
#include "verify.h"

static void cc_flags_verify(const char *cc, const char *src, const char *bin, char **argv)
{
    int i = 0;
    argv[i++] = (char *)cc;
    argv[i++] = "-O2";
    argv[i++] = "-ffp-contract=off";          /* no fused multiply-add differences between variants */
    argv[i++] = "-pthread";
    argv[i++] = "-w";
    argv[i++] = "-o";
    argv[i++] = (char *)bin;
    argv[i++] = (char *)src;
    argv[i++] = "-lm";
    argv[i] = NULL;
}

int vb_have_cc(const char *cc)
{
    char *argv[3];
    RunInfo r;
    argv[0] = (char *)cc; argv[1] = "--version"; argv[2] = NULL;
    r = run_proc(argv, NULL, NULL, 20, 0, NULL);
    return r.started && r.exit_code == 0;
}

int vb_build(Bench *B, const Node *program, int instrument, char *bin, size_t bn, char *err, size_t en)
{
    char src[300], errf[300];
    char *argv[16];
    RunInfo r;
    int id = ++B->counter;

    snprintf(src, sizeof src, "%s/v%d.c", B->dir, id);
    snprintf(bin, bn, "%s/v%d.bin", B->dir, id);
    snprintf(errf, sizeof errf, "%s/v%d.cc.txt", B->dir, id);
    if (!codegen_emit_plain(src, program, instrument)) {
        snprintf(err, en, "could not write %s", src);
        return 0;
    }
    cc_flags_verify(B->o->cc, src, bin, argv);
    r = run_proc(argv, NULL, errf, 120, 0, NULL);
    if (!r.started || r.exit_code != 0) {
        FILE *f = fopen(errf, "r");
        size_t k = 0;
        err[0] = 0;
        if (f) { k = fread(err, 1, en - 1, f); err[k] = 0; fclose(f); }
        if (!k) snprintf(err, en, "the C compiler failed");
        return 0;
    }
    return 1;
}

/* ---------- comparison ---------- */

static char **read_lines(const char *path, int *n)
{
    FILE *f = fopen(path, "r");
    char **v = NULL, buf[4096];
    int cap = 0;
    *n = 0;
    if (!f) return NULL;
    while (fgets(buf, sizeof buf, f)) {
        size_t l = strlen(buf);
        while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r')) buf[--l] = 0;
        if (*n >= cap) { cap = cap ? cap * 2 : 64; v = realloc(v, cap * sizeof *v); }
        v[(*n)++] = strdup(buf);
    }
    fclose(f);
    return v;
}

static void free_lines(char **v, int n)
{
    int i;
    for (i = 0; i < n; i++) free(v[i]);
    free(v);
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int close_enough(const char *a, const char *b)
{
    double x, y, tol;
    if (!strcmp(a, b)) return 1;
    x = strtod(a, NULL); y = strtod(b, NULL);
    if (isnan(x) && isnan(y)) return 1;
    tol = 1e-7 * fmax(1.0, fmax(fabs(x), fabs(y)));
    return fabs(x - y) <= tol;
}

/* compare state dumps: exact fingerprint, or tolerance on sums when fp rounding may differ */
static int compare_dumps(const char *pa, const char *pb, int fp_mode, char *note, size_t nn)
{
    int na, nb, i, ok = 1;
    char **la = read_lines(pa, &na), **lb = read_lines(pb, &nb);
    if (!la || !lb || na != nb) {
        snprintf(note, nn, "state dumps are missing or differ in length");
        ok = 0;
    } else {
        for (i = 0; i < na && ok; i++) {
            if (!strcmp(la[i], lb[i])) continue;
            if (fp_mode) {
                char na_[64], nb_[64], ha[32], hb[32], s1[64], q1[64], m1[64], s2[64], q2[64], m2[64];
                long n1, n2;
                if (sscanf(la[i], "G %63s %ld %31s %63s %63s %63s", na_, &n1, ha, s1, q1, m1) == 6 &&
                    sscanf(lb[i], "G %63s %ld %31s %63s %63s %63s", nb_, &n2, hb, s2, q2, m2) == 6 &&
                    !strcmp(na_, nb_) && n1 == n2 &&
                    (!strcmp(s1, "-") ? !strcmp(s2, "-") && !strcmp(ha, hb)
                                      : close_enough(s1, s2) && close_enough(q1, q2) && close_enough(m1, m2)))
                    continue;
            }
            snprintf(note, nn, "global memory differs: %.80s  vs  %.80s", la[i], lb[i]);
            ok = 0;
        }
    }
    if (la) free_lines(la, na);
    if (lb) free_lines(lb, nb);
    return ok;
}

static int compare_outputs(const char *pa, const char *pb, char *note, size_t nn)
{
    int na, nb, i, ok = 1;
    char **la = read_lines(pa, &na), **lb = read_lines(pb, &nb);
    if (!la) { la = malloc(sizeof *la); na = 0; }
    if (!lb) { lb = malloc(sizeof *lb); nb = 0; }
    if (na != nb) ok = 0;
    else {
        qsort(la, na, sizeof *la, cmp_str);           /* threads may interleave their output */
        qsort(lb, nb, sizeof *lb, cmp_str);
        for (i = 0; i < na && ok; i++) if (strcmp(la[i], lb[i])) ok = 0;
    }
    if (!ok) snprintf(note, nn, "printed output differs");
    free_lines(la, na);
    free_lines(lb, nb);
    return ok;
}

int vb_differential(Bench *B, const char *bin_a, const char *bin_b, int fp_mode,
                    int *runs, int *passed, char *note, size_t nn)
{
    int s, nseeds = B->o->seeds;
    char da[300], db[300], oa[300], ob[300];
    char envd1[340], envd2[340], envs[64];
    note[0] = 0;
    *runs = *passed = 0;
    snprintf(da, sizeof da, "%s/a.dump", B->dir);
    snprintf(db, sizeof db, "%s/b.dump", B->dir);
    snprintf(oa, sizeof oa, "%s/a.out.txt", B->dir);
    snprintf(ob, sizeof ob, "%s/b.out.txt", B->dir);

    for (s = 0; s <= nseeds; s++) {                    /* s == 0: the program's own initial data */
        const char *env1[3], *env2[3];
        char *av[2], *bv[2];
        RunInfo ra, rb;
        char why[200] = "";
        int k = 0;

        snprintf(envd1, sizeof envd1, "HT_DUMP=%s", da);
        snprintf(envd2, sizeof envd2, "HT_DUMP=%s", db);
        env1[k] = envd1; env2[k] = envd2; k++;
        if (s > 0) {
            snprintf(envs, sizeof envs, "HT_SEED=%d", s);
            env1[k] = envs; env2[k] = envs; k++;
        }
        env1[k] = NULL; env2[k] = NULL;
        av[0] = (char *)bin_a; av[1] = NULL;
        bv[0] = (char *)bin_b; bv[1] = NULL;

        unlink(da); unlink(db);
        ra = run_proc(av, oa, NULL, B->o->timeout, B->o->mem_mb, env1);
        if (ra.timed_out || ra.exit_code != 0) {
            if (s == 0) {
                if (ra.timed_out)
                    snprintf(note, nn, "the original program did not finish within %.0f s", B->o->timeout);
                else
                    snprintf(note, nn, "the original program exited abnormally (code %d)", ra.exit_code);
                return -1;                              /* cannot verify anything */
            }
            continue;                                   /* random data broke the original: skip this seed */
        }
        rb = run_proc(bv, ob, NULL, B->o->timeout, B->o->mem_mb, env2);
        (*runs)++;
        if (rb.timed_out || rb.exit_code != 0) {
            snprintf(note, nn, "the transformed program %s on %s",
                     rb.timed_out ? "timed out" : "failed", s ? "random data" : "the original data");
            return 0;
        }
        if (!compare_dumps(da, db, fp_mode, why, sizeof why)) {
            snprintf(note, nn, "%s (%s)", why, s ? "random data" : "original data");
            return 0;
        }
        if (!fp_mode && !compare_outputs(oa, ob, why, sizeof why)) {
            snprintf(note, nn, "%s (%s)", why, s ? "random data" : "original data");
            return 0;
        }
        (*passed)++;
    }
    return *passed > 0 ? 1 : -1;
}

/* ---------- benchmark ---------- */

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Interleaved timing of two binaries.  Order alternates (A B, B A, ...) so neither enjoys a
 * systematically warmer cache; short programs get more runs.  Returns best-of-N and median. */
int vb_bench(Bench *B, const char *bin_a, const char *bin_b, int runs, BenchResult *out)
{
    double va[40], vb[40], t0;
    int i;
    char *av[2], *bv[2];
    RunInfo r;
    if (runs < 1) runs = 1;
    av[0] = (char *)bin_a; av[1] = NULL;
    bv[0] = (char *)bin_b; bv[1] = NULL;
    r = run_proc(av, NULL, NULL, B->o->timeout, B->o->mem_mb, NULL);        /* warm-up */
    if (r.timed_out || r.exit_code != 0) return 0;
    t0 = r.secs;
    r = run_proc(bv, NULL, NULL, B->o->timeout, B->o->mem_mb, NULL);
    if (r.timed_out || r.exit_code != 0) return 0;
    if (t0 < 0.3 && runs < 9) runs = 9;                /* short runs are noisy: measure more */
    if (t0 < 0.1 && runs < 15) runs = 15;
    if (runs > 39) runs = 39;
    for (i = 0; i < runs; i++) {
        int a_first = (i % 2 == 0);
        RunInfo ra, rb;
        if (a_first) { ra = run_proc(av, NULL, NULL, B->o->timeout, B->o->mem_mb, NULL);
                       rb = run_proc(bv, NULL, NULL, B->o->timeout, B->o->mem_mb, NULL); }
        else         { rb = run_proc(bv, NULL, NULL, B->o->timeout, B->o->mem_mb, NULL);
                       ra = run_proc(av, NULL, NULL, B->o->timeout, B->o->mem_mb, NULL); }
        if (ra.timed_out || ra.exit_code != 0 || rb.timed_out || rb.exit_code != 0) return 0;
        va[i] = ra.secs;
        vb[i] = rb.secs;
    }
    qsort(va, runs, sizeof *va, cmp_d);
    qsort(vb, runs, sizeof *vb, cmp_d);
    out->runs = runs;
    out->min_a = va[0];
    out->min_b = vb[0];
    out->med_a = (runs & 1) ? va[runs / 2] : (va[runs / 2 - 1] + va[runs / 2]) / 2.0;
    out->med_b = (runs & 1) ? vb[runs / 2] : (vb[runs / 2 - 1] + vb[runs / 2]) / 2.0;
    return 1;
}
