/* contention.c - SMT contention graph and thread-to-core mapping optimizer.
 *
 * Pair score (0..100) for threads A and B that may run concurrently:
 *
 *   score = 100 * overlap * lockf *
 *           ( W_EXEC  * exec    execution-unit overlap (int / fp / div)
 *           + W_LDST  * ldst    both are load/store-port heavy
 *           + W_CACHE * cache   combined footprint vs. shared L1D
 *           + W_BW    * bw  )   both pull data from beyond L2
 *
 * Every component uses min(demandA, demandB): contention needs two parties.
 * The mapper then pairs threads onto SMT sibling slots so that the sum of
 * pair scores is minimal (minimum-weight perfect matching).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ht.h"
#include "common.h"

static double clamp01(double x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static double fmin2(double a, double b) { return a < b ? a : b; }
static double fmax2(double a, double b) { return a > b ? a : b; }

const char *severity(double s)
{
    if (s >= SEV_HIGH) return "high";
    if (s >= SEV_MED) return "medium";
    return "low";
}

void fmt_bytes(double b, char *out, size_t n)
{
    if (b >= 1048576.0) snprintf(out, n, "%.1f MiB", b / 1048576.0);
    else if (b >= 1024.0) snprintf(out, n, "%.1f KiB", b / 1024.0);
    else snprintf(out, n, "%.0f B", b);
}

static void add_conf(PairInfo *P, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void add_conf(PairInfo *P, const char *fmt, ...)
{
    va_list ap;
    if (P->nconf >= MAX_CONF) return;
    va_start(ap, fmt);
    vsnprintf(P->conf[P->nconf++], sizeof P->conf[0], fmt, ap);
    va_end(ap);
}

/* ================================================================== */
/* Pairwise contention                                                 */
/* ================================================================== */

static void score_pair(HTContext *ctx, int i, int j)
{
    const Profile *A = &ctx->threads[i].prof, *B = &ctx->threads[j].prof;
    PairInfo *P = &ctx->pair[i][j];
    double wA = A->total_ops, wB = B->total_ops, ratio = 0;
    double l1 = (double)ctx->topo.l1d;
    double uni, press, cdA, cdB, raw;
    int a, b, k, nnotes = 0;
    char notes[MAX_CONF][128];
    char b1[32], b2[32];

    memset(P, 0, sizeof *P);
    P->overlap = threads_overlap(ctx, i, j);

    if (wA > 0 && wB > 0) ratio = fmin2(wA, wB) / fmax2(wA, wB);
    /* mean fraction of the two lifetimes that is overlapped */
    P->ofac = P->overlap ? (1.0 + ratio) / 2.0 : 0.0;

    /* shared locks -> critical sections are serialized, less true overlap */
    for (a = 0; a < A->nlocks; a++)
        for (b = 0; b < B->nlocks; b++)
            if (!strcmp(A->locks[a], B->locks[b])) {
                P->nshared_locks++;
                if (nnotes < MAX_CONF)
                    snprintf(notes[nnotes++], sizeof notes[0],
                             "both threads lock mutex '%s' (critical sections serialize)", A->locks[a]);
            }
    P->lockf = 1.0 - 0.15 * (P->nshared_locks > 2 ? 2 : P->nshared_locks);

    P->exec = clamp01(0.4 * fmin2(A->n_int, B->n_int) +
                      0.8 * fmin2(A->n_fp, B->n_fp) +
                      2.0 * fmin2(A->n_heavy, B->n_heavy));
    P->ldst = fmin2(A->n_ldst, B->n_ldst);

    /* union of footprints: arrays used by both threads are counted once */
    uni = A->ws_bytes + B->ws_bytes;
    for (a = 0; a < A->narrays; a++)
        for (b = 0; b < B->narrays; b++)
            if (A->arrays[a].sym == B->arrays[b].sym) {
                const ArrayUse *x = &A->arrays[a], *y = &B->arrays[b];
                uni -= fmin2(x->footprint, y->footprint);
                if (nnotes < MAX_CONF)
                    snprintf(notes[nnotes++], sizeof notes[0], "shared data '%s' (%s)", x->sym->name,
                             (x->writes > 0 || y->writes > 0) ? "at least one writer" : "read-only");
            }
    if (uni < 64.0) uni = 64.0;
    press = clamp01((log2(uni / l1) + 1.0) / 3.0);      /* 0 below L1/2 .. 1 above 4*L1 */
    cdA = clamp01(A->ws_bytes / (l1 / 4.0));
    cdB = clamp01(B->ws_bytes / (l1 / 4.0));
    P->cache = fmin2(cdA, cdB) * press;
    P->bw = fmin2(A->n_bw, B->n_bw);

    raw = W_EXEC * P->exec + W_LDST * P->ldst + W_CACHE * P->cache + W_BW * P->bw;
    P->score = 100.0 * raw * P->ofac * P->lockf;

    if (!P->overlap) {
        add_conf(P, "threads never run concurrently (create/join order) - no contention");
    } else {
        fmt_bytes(uni, b1, sizeof b1);
        fmt_bytes(l1, b2, sizeof b2);
        if (P->bw > 0.30)
            add_conf(P, "memory bandwidth: both threads stream data from beyond the L2 cache");
        if (P->cache > 0.30)
            add_conf(P, "L1D capacity: combined footprint %s vs shared L1D %s", b1, b2);
        if (P->exec > 0.25) {
            double ci = 0.4 * fmin2(A->n_int, B->n_int), cf = 0.8 * fmin2(A->n_fp, B->n_fp),
                   ch = 2.0 * fmin2(A->n_heavy, B->n_heavy);
            add_conf(P, "execution units: both threads demand %s",
                     (ch >= cf && ch >= ci) ? "divide/sqrt/math units"
                     : (cf >= ci) ? "floating-point units" : "integer ALUs");
        }
        if (P->ldst > 0.30)
            add_conf(P, "load/store ports: both threads are memory-instruction heavy");
    }
    for (k = 0; k < nnotes; k++) add_conf(P, "%s", notes[k]);
}

void build_contention(HTContext *ctx)
{
    int i, j;
    for (i = 0; i < ctx->nthreads; i++)
        for (j = i + 1; j < ctx->nthreads; j++) {
            score_pair(ctx, i, j);
            ctx->pair[j][i] = ctx->pair[i][j];
        }
}

/* ================================================================== */
/* Mapping                                                             */
/* ================================================================== */

typedef struct { int i, j; double w; } Edge;

static int cmp_asc(const void *a, const void *b)
{
    double d = ((const Edge *)a)->w - ((const Edge *)b)->w;
    return d < 0 ? -1 : (d > 0 ? 1 : 0);
}

static int cmp_desc(const void *a, const void *b) { return cmp_asc(b, a); }

/* Exact minimum (or maximum) weight perfect matching by DP over bitmasks.
 * n must be even and <= 20.  C[i][j] == INFINITY forbids a pair. */
static void match_exact(int n, double C[][2 * MAX_THREADS], int maximize, int out[][2], int *npairs)
{
    size_t S = (size_t)1 << n, mask;
    double *dp = malloc(S * sizeof(double));
    signed char *pi = malloc(S), *pj = malloc(S);
    int i, j, np = 0;

    for (mask = 0; mask < S; mask++) dp[mask] = INFINITY;
    dp[0] = 0;
    for (mask = 0; mask < S; mask++) {
        if (dp[mask] == INFINITY) continue;
        for (i = 0; i < n && (mask >> i & 1); i++) ;
        if (i >= n) continue;
        for (j = i + 1; j < n; j++) {
            size_t nm;
            double v;
            if ((mask >> j & 1) || isinf(C[i][j])) continue;
            nm = mask | ((size_t)1 << i) | ((size_t)1 << j);
            v = dp[mask] + (maximize ? -C[i][j] : C[i][j]);
            if (v < dp[nm]) { dp[nm] = v; pi[nm] = (signed char)i; pj[nm] = (signed char)j; }
        }
    }
    mask = S - 1;
    while (mask && dp[mask] != INFINITY) {
        i = pi[mask]; j = pj[mask];
        out[np][0] = i; out[np][1] = j; np++;
        mask &= ~(((size_t)1 << i) | ((size_t)1 << j));
    }
    *npairs = np;
    free(dp); free(pi); free(pj);
}

/* Heuristic for large thread counts: take the p cheapest (or costliest)
 * disjoint pairs. */
static void match_greedy(int N, int p, int maximize, int out[][2], int *npairs)
{
    static Edge e[MAX_THREADS * MAX_THREADS];
    int ne = 0, i, j, np = 0;
    char used[MAX_THREADS] = {0};
    extern HTContext *g_map_ctx;

    for (i = 0; i < N; i++)
        for (j = i + 1; j < N; j++) { e[ne].i = i; e[ne].j = j; e[ne].w = g_map_ctx->pair[i][j].score; ne++; }
    qsort(e, ne, sizeof(Edge), maximize ? cmp_desc : cmp_asc);
    for (i = 0; i < ne && np < p; i++)
        if (!used[e[i].i] && !used[e[i].j]) {
            used[e[i].i] = used[e[i].j] = 1;
            out[np][0] = e[i].i; out[np][1] = e[i].j; np++;
        }
    *npairs = np;
}

HTContext *g_map_ctx;

/* Place groups on physical cores and compute the resulting total score. */
static void place(HTContext *ctx, Mapping *m)
{
    int g, s, t, k, u;
    const Topology *T = &ctx->topo;

    m->total = 0;
    for (t = 0; t < ctx->nthreads; t++) { m->core[t] = -1; m->cpu[t] = -1; }
    for (g = 0; g < m->ngroups; g++) {
        int core = g % T->ncores;
        for (s = 0; s < 2; s++) {
            t = m->g[g][s];
            if (t < 0) continue;
            m->core[t] = core;
            m->cpu[t] = (s < T->nsib[core]) ? T->cpus[core][s] : T->cpus[core][0];
        }
    }
    for (k = 0; k < ctx->nthreads; k++)
        for (u = k + 1; u < ctx->nthreads; u++)
            if (m->core[k] == m->core[u])
                m->total += ctx->pair[k][u].score;
}

static int cmp_group(const void *a, const void *b)
{
    return ((const int *)a)[0] - ((const int *)b)[0];
}

/* kind: 0 = optimized (min-weight matching), 1 = naive (creation order),
 *       2 = worst (max-weight matching).
 * policy pack   : form floor(N/2) SMT pairs (maximise SMT usage)
 * policy spread : form only the N-K pairs forced by having K physical cores */
static void make_mapping(HTContext *ctx, Mapping *m, int kind, int spread)
{
    int N = ctx->nthreads, K = ctx->topo.ncores;
    int p, D, n2, i, j, np = 0, ng = 0;
    static double C[2 * MAX_THREADS][2 * MAX_THREADS];
    int pairs[2 * MAX_THREADS][2];
    char paired[MAX_THREADS] = {0};
    int groups[MAX_THREADS][2];

    memset(m, 0, sizeof *m);
    if (N == 0) { strcpy(m->algo, "none"); return; }

    p = spread ? (N > K ? N - K : 0) : N / 2;
    if (p > N / 2) p = N / 2;
    D = N - 2 * p;                      /* threads that run alone on a core */
    n2 = N + D;                         /* real nodes + one dummy per single */
    g_map_ctx = ctx;

    if (p == 0) {
        strcpy(m->algo, "no-sharing");
    } else if (kind == 1) {
        for (i = 0; i < p; i++) { pairs[np][0] = 2 * i; pairs[np][1] = 2 * i + 1; np++; }
        strcpy(m->algo, "creation-order");
    } else if (n2 <= 20) {
        for (i = 0; i < n2; i++)
            for (j = 0; j < n2; j++) {
                if (i == j) C[i][j] = 0;
                else if (i < N && j < N) C[i][j] = ctx->pair[i][j].score;
                else if (i >= N && j >= N) C[i][j] = INFINITY;     /* dummy-dummy forbidden */
                else C[i][j] = 0.0;                                /* real-dummy: runs alone */
            }
        {
            int raw[2 * MAX_THREADS][2], nraw = 0;
            match_exact(n2, C, kind == 2, raw, &nraw);
            for (i = 0; i < nraw; i++)
                if (raw[i][0] < N && raw[i][1] < N) {
                    pairs[np][0] = raw[i][0]; pairs[np][1] = raw[i][1]; np++;
                }
        }
        strcpy(m->algo, "exact-dp");
    } else {
        match_greedy(N, p, kind == 2, pairs, &np);
        strcpy(m->algo, "greedy");
    }

    for (i = 0; i < np; i++) {
        int a = pairs[i][0], b = pairs[i][1];
        if (a > b) { int t = a; a = b; b = t; }
        groups[ng][0] = a; groups[ng][1] = b; ng++;
        paired[a] = paired[b] = 1;
    }
    for (i = 0; i < N; i++)
        if (!paired[i]) { groups[ng][0] = i; groups[ng][1] = -1; ng++; }
    qsort(groups, ng, sizeof groups[0], cmp_group);
    for (i = 0; i < ng; i++) { m->g[i][0] = groups[i][0]; m->g[i][1] = groups[i][1]; }
    m->ngroups = ng;
    place(ctx, m);
}

void build_mappings(HTContext *ctx, int spread)
{
    ctx->spread = spread;
    make_mapping(ctx, &ctx->m_opt, 0, spread);
    make_mapping(ctx, &ctx->m_naive, 1, spread);
    make_mapping(ctx, &ctx->m_worst, 2, spread);
}
