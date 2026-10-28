/* ht.h - data structures and interfaces of the HT-Compass analysis pipeline
 *
 *   Thread Discovery -> Static Resource Profiler -> Concurrency Analyzer
 *   -> Contention Graph -> Mapping Optimizer -> Codegen / JSON report
 */
#ifndef HT_H
#define HT_H

#include "ast.h"
#include "sema.h"

#define MAX_THREADS 64
#define MAX_SITES   32
#define MAX_ARRAYS  24
#define MAX_NAMES   32
#define MAX_CORES   256
#define MAX_CONF    8
#define EV_INF      1000000

/* ------------------------------------------------------------------ */
/* Tunable constants of the contention model (all documented in README) */
/* ------------------------------------------------------------------ */
#define DEFAULT_TRIPS 64      /* assumed trip count of loops with unknown bounds */
#define BW_REF        4.0     /* bytes of DRAM traffic per operation = saturated */
#define W_EXEC        0.30    /* weights of the four contention components      */
#define W_LDST        0.15
#define W_CACHE       0.25
#define W_BW          0.30
#define SEV_HIGH      35.0    /* pair score >= SEV_HIGH  -> "high"              */
#define SEV_MED       15.0    /* pair score >= SEV_MED   -> "medium"            */

/* ------------------------------------------------------------------ */
/* Topology                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    int ncores;                          /* physical (or virtual) cores   */
    int ncpus;                           /* logical CPUs                  */
    int nsib[MAX_CORES];                 /* siblings per core (1 or 2)    */
    int cpus[MAX_CORES][2];              /* logical CPU ids per core      */
    long l1d, l2;                        /* bytes                         */
    char source[32];                     /* sysfs | user | assumed        */
    char cache_source[16];               /* sysfs | default | user        */
    int smt_detected;
} Topology;

void topo_detect(Topology *t, const char *spec, long l1_kb, long l2_kb);

/* ------------------------------------------------------------------ */
/* Loops                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    int canonical;                       /* var, step recognised          */
    char var[64];
    long start;  int start_known;
    long step;
    long bound;  int bound_known;
    long trips;  int trips_known;
} LoopInfo;

int loop_info(const Node *for_node, LoopInfo *li);
int const_eval(const Node *e, long *out);
int mentions(const Node *e, const char *name);
int has_array_ref(const Node *e);
int has_identifier(const Node *e);
int linear_coeff(const Node *e, const char *var, long *coeff);

/* ------------------------------------------------------------------ */
/* Profiles                                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    Symbol *sym;
    double reads, writes;
    double lines;                        /* cache lines touched if every access missed */
    double seq, strided, irregular;      /* access counts by pattern      */
    double footprint;                    /* bytes competing for L1D       */
    double dram_lines;                   /* estimated lines from DRAM     */
} ArrayUse;

typedef struct {
    double int_ops, fp_ops, heavy_ops;   /* ALU, FP add/mul, div/sqrt/...  */
    double loads, stores, branches;
    double io_ops, lock_ops;
    double total_ops;
    double seq_acc, strided_acc, irregular_acc;
    double dram_lines, ws_bytes;
    int narrays;
    ArrayUse arrays[MAX_ARRAYS];
    int ngr, ngw;                        /* shared scalar globals read/written */
    const char *gread[MAX_NAMES], *gwritten[MAX_NAMES];
    int nlocks;
    const char *locks[8];
    double n_int, n_fp, n_heavy, n_ldst, n_bw;   /* normalised demand, 0..1 */
    int assumed_trips;                   /* loops whose trip count was assumed */
    char klass[40];
} Profile;

/* ------------------------------------------------------------------ */
/* Threads                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    int id;
    Node *call;                          /* the pthread_create call       */
    Node *func;                          /* worker function (N_FUNC)      */
    int line;
    const char *handle;                  /* pthread_t variable name       */
    const char *in_func;                 /* function containing the call  */
    int in_loop;
    Node *loop_node;
    LoopInfo loop;
    int instances;
    int first_thread;
    int ev;                              /* creation event number         */
    int ev_end;                          /* join event (or -1)            */
    int serial;                          /* joined in the same loop body  */
} ThreadSite;

typedef struct {
    int id;
    int site, instance;
    char name[56];
    Node *func;
    int line;
    int ev_start, ev_end;
    Profile prof;
} Thread;

/* ------------------------------------------------------------------ */
/* Contention                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    int overlap;                         /* may run concurrently?         */
    double ofac;                         /* overlap factor 0..1           */
    double exec, ldst, cache, bw;        /* components 0..1               */
    double lockf;                        /* lock serialization factor     */
    double score;                        /* 0..100                        */
    int nshared_locks;
    int nconf;
    char conf[MAX_CONF][128];
} PairInfo;

typedef struct {
    int ngroups;
    int g[MAX_THREADS / 2 + 2][2];       /* thread ids, -1 = idle slot    */
    int core[MAX_THREADS];               /* physical core index           */
    int cpu[MAX_THREADS];                /* logical CPU id                */
    double total;                        /* sum of pair scores sharing a core */
    char algo[24];
} Mapping;

struct OptResult;

typedef struct {
    Topology topo;
    int nsites;
    ThreadSite sites[MAX_SITES];
    int nthreads;
    Thread threads[MAX_THREADS];
    PairInfo pair[MAX_THREADS][MAX_THREADS];
    Mapping m_opt, m_naive, m_worst;
    int spread;                          /* policy: one thread per core   */
    int assumed_trips_total;
    struct OptResult *opt;               /* optimizer report (optional)   */
} HTContext;

/* thread discovery + concurrency (analysis.c) */
int  run_analysis(HTContext *ctx, Node *program, const Topology *topo, int spread);
void discover_threads(HTContext *ctx, Node *program);
int  threads_overlap(const HTContext *ctx, int a, int b);

/* static resource profiler (profiler.c) */
void profile_threads(HTContext *ctx);
void profile_function(HTContext *ctx, Node *func, Profile *out);

/* contention graph + mapping (contention.c) */
void build_contention(HTContext *ctx);
void build_mappings(HTContext *ctx, int spread);
const char *severity(double score);

/* output (codegen.c, report.c) */
int  codegen_emit(const char *path, HTContext *ctx, const Node *program,
                  const Mapping *map, const char *strategy);
int  write_json(const char *path, HTContext *ctx, const Mapping *chosen,
                const char *strategy);
void print_summary(HTContext *ctx, const Mapping *chosen, const char *strategy);
void fmt_bytes(double b, char *out, size_t n);

#endif
