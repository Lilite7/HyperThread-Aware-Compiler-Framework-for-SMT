/* opt.h - the optimization pipeline
 *
 *   discover opportunities  ->  prove legality  ->  apply to a copy
 *   ->  re-run the cost model  ->  differential test  ->  benchmark  ->  select
 *
 * The compiler decides.  Nothing here trusts an external suggestion: advice from
 * a person or an AI only changes the *order* in which opportunities are examined.
 */
#ifndef HT_OPT_H
#define HT_OPT_H

#include "ht.h"
#include "llm.h"

typedef struct {
    int enabled;
    int plan;               /* discover and judge legality only; build and run nothing */
    int fp_ok;              /* allow transformations that reassociate floating point  */
    int verify;             /* differential testing with gcc (default on)             */
    int bench;              /* benchmark-based selection (default on)                 */
    int bench_runs;         /* interleaved timing runs per program                    */
    int rounds;             /* greedy rounds                                          */
    int max_cands;          /* candidates evaluated per round                         */
    int seeds;              /* random-data seeds used by the differential test        */
    double min_speedup;     /* measured speedup needed to accept                      */
    double timeout;         /* seconds allowed for one run of a program               */
    long   mem_mb;          /* address-space limit for programs under test            */
    const char *cc;         /* C compiler used for test builds                        */
    const char *suggest_file;   /* advice file: "transform function line" per line    */
    const char *prompt_file;    /* write an advisor prompt here                       */
    /* --- local language model --- */
    int  use_llm;               /* consult the local model (the default for --optimize)    */
    int  llm_only;              /* evaluate only what the model proposed                   */
    int  llm_show_status;       /* --llm-status                                            */
    LlmConfig llm;
    const char *llm_prompt_file;    /* write the exact prompt sent to the model here              */
    Topology topo;
    int spread;
} OptOptions;

typedef enum { SAFE_NO = 0, SAFE_EXACT, SAFE_FP } Safety;

typedef struct {
    int tr;                 /* index into g_transforms                               */
    char func[48];
    int line;               /* line of the first loop involved                       */
    char key[128];          /* stable identity of the opportunity                    */
    char desc[200];
    int external;           /* named by the model or in the advice file              */
    int source;             /* 0 = found by the compiler, 1 = proposed by the model  */
    char region[64];        /* stable region id of the first loop: "func/L3"          */
    int param;              /* unroll factor / tile size (0 = default)                */
    int proposal;           /* index of the model proposal that named it, or -1       */
    int promising;          /* the locality heuristic expects a gain                 */
    Safety safety;
    char why[260];          /* why it was refused (safety == SAFE_NO)                */
    char evidence[320];     /* what was proved (otherwise)                           */
} Cand;

typedef struct Transformation {
    const char *name;       /* "interchange"                                          */
    const char *title;
    const char *aliases;    /* other names a model may use, space separated           */
    const char *region_doc; /* what the "region" of a proposal means for this one     */
    const char *param_name; /* NULL, or the proposal parameter ("factor")             */
    int param_default;
    /* One scan serves three roles (see transform_* below):
     *   check == 0  find the places where the pattern occurs   (finder)
     *   check == 1  also decide whether the change is safe     (precondition checker)
     * n1/n2, when given, receive the AST nodes of each place.                        */
    int (*scan)(Node *program, const HTContext *ctx, int check, Cand *out, int max, Node **n1, Node **n2);
    int (*apply)(Node *program, const HTContext *ctx, const Cand *c);          /* applier */
} Transformation;

extern const Transformation g_transforms[];
extern const int g_ntransforms;

int  transform_find_sites(int tr, Node *program, Cand *out, int max);                  /* finder             */
int  transform_find(int tr, Node *program, const HTContext *ctx, Cand *out, int max);  /* finder + checker   */
int  transform_check(int tr, Node *program, const HTContext *ctx, const Cand *site, Cand *verdict);
int  transform_apply(Node *program, const HTContext *ctx, const Cand *c);              /* applier            */
int  transform_by_name(const char *canonical);                                          /* -1 if unsupported  */
int  loop_ordinal(Node *func, Node *loop);          /* 1-based position among the function's loops */
void loop_region_id(Node *func, Node *loop, char *out, size_t n);

/* ---------- model statistics ---------- */
typedef struct {
    double ops, loads, stores, dram, strided, irregular, contention, cost;
} Stats;

/* ---------- report records ---------- */
typedef struct {
    int round;
    char transform[24], func[48];
    int line;
    char region[64];
    char source[12];        /* compiler | model                                      */
    char desc[200], explanation[800];
    char ai_explanation[420], ai_benefit[220], ai_preconditions[320], ai_risks[320];
    int ai_rank;
    int external;
    char safety[16];        /* exact | fp-reassoc | refused                          */
    char status[16];        /* applied | passed | rejected | skipped | unsupported | invalid | found */
    char reason[360];
    char evidence[320];
    int has_model;
    Stats before, after;
    int verified, vruns, vpass;
    char vnote[240];
    int benched;
    double base_s, cand_s, speedup, speedup_med;
    int bench_runs;
    char basis[16];         /* benchmark | model                                     */
    char *fn_before, *fn_after;
} OptRecord;

typedef struct {
    int mode;               /* 0 none, 1 local model, 2 offline rules, 3 reply file (test hook) */
    int available;          /* the service answered and a model was selected                    */
    int failed;             /* the model could not be used: no analysis happened                */
    char error[400];
    char model[96];
    char endpoint[80];
    char version[32];
    char note[300];         /* e.g. "started ollama serve"                                       */
    int  prompt_chars;
    int  response_chars;
    double seconds;
    int  prompt_tokens, eval_tokens;
    int  truncated;
    char excerpt[2048];     /* the start of the raw reply, for transparency                      */
    int  nproposals, nsupported, nunsupported, ninvalid, nduplicate, nmatched;
    char notes[6][200];     /* problems found while validating the reply                         */
    int  nnotes;
    int  installed;
    char installed_models[8][96];
    int  ninstalled;
} AiInfo;

typedef struct OptResult {
    AiInfo ai;
    int nrec, cap;
    OptRecord *rec;
    int applied;
    int baseline_ok;
    int gcc_ok;
    double orig_s, final_s;
    int reverted;
    int timed;
    Stats first, last;
    char note[300];
    int nsuggest, nsuggest_matched;
    int o_plan, o_fp, o_verify, o_bench, o_runs, o_seeds;
    double o_min_speedup;
} OptResult;

/* Optimise `program` (never modifies it); returns the optimised copy, or
 * `program` itself if nothing was applied. */
Node *optimize_program(Node *program, const OptOptions *o, OptResult *res);

/* ---------- helpers shared between the modules ---------- */
void stats_of(const HTContext *ctx, const Profile *main_prof, Stats *s);
void explain_candidate(const Cand *c, const Stats *before, const Stats *after,
                       char *out, size_t n);
void write_advisor_prompt(const char *path, const char *source_text, const HTContext *ctx,
                          const Cand *cands, int ncands);
char *function_text(const Node *func);        /* unparsed source of one function     */

/* codegen.c: plain / instrumented emission for verification and benchmarking */
int codegen_emit_plain(const char *path, const Node *program, int instrument);

/* runner.c */
typedef struct { int started, exit_code, timed_out, signaled; double secs; } RunInfo;
RunInfo run_proc(char *const argv[], const char *out_path, const char *err_path,
                 double timeout, long mem_mb, const char *const *env);
int  make_tempdir(char *path, size_t n);
void remove_tree(const char *path);

#endif
