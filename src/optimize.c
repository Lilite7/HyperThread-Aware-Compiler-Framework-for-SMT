/* optimize.c - the optimization pipeline driver.
 *
 *  for each round:
 *    1. analyse the current program (threads, cost model)
 *    2. find opportunities with every registered transformation; each one carries
 *       a legality verdict from the dependence analysis (or a refusal reason)
 *    3. for every legal opportunity (external advice first):
 *         apply it to a COPY, re-run the cost model,
 *         differential-test the copy against the current program,
 *         benchmark the copy against the current program
 *    4. apply the best candidate that passed to the program and go round again
 *
 *  A candidate is rejected the moment any stage fails.  The cost model only
 *  *prioritises*; when a benchmark is available it decides.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opt.h"
#include "verify.h"
#include "advisor.h"
#include "json.h"
#include "astutil.h"
#include "common.h"

#define MAX_CANDS     96
#define DRAM_LINE_COST 8.0      /* one line from memory ~ 8 simple operations (heuristic) */
#define MIN_BENCH_S   0.02      /* below this a timing run says nothing                    */

/* ================================================================== */
/* Model statistics                                                    */
/* ================================================================== */

void stats_of(const HTContext *ctx, const Profile *mp, Stats *s)
{
    int i;
    memset(s, 0, sizeof *s);
    for (i = 0; i < ctx->nthreads; i++) {
        const Profile *p = &ctx->threads[i].prof;
        s->ops += p->total_ops;
        s->loads += p->loads;
        s->stores += p->stores;
        s->dram += p->dram_lines;
        s->strided += p->strided_acc;
        s->irregular += p->irregular_acc;
    }
    if (mp) {
        s->ops += mp->total_ops;
        s->loads += mp->loads;
        s->stores += mp->stores;
        s->dram += mp->dram_lines;
        s->strided += mp->strided_acc;
        s->irregular += mp->irregular_acc;
    }
    s->contention = ctx->m_opt.total;
    s->cost = s->ops + DRAM_LINE_COST * s->dram;
}

/* ================================================================== */
/* Records                                                             */
/* ================================================================== */

static void fill_ai(OptRecord *r, const Proposal *p, int rank)
{
    snprintf(r->ai_explanation, sizeof r->ai_explanation, "%s", p->explanation);
    snprintf(r->ai_benefit, sizeof r->ai_benefit, "%s", p->benefit);
    snprintf(r->ai_preconditions, sizeof r->ai_preconditions, "%s", p->preconditions);
    snprintf(r->ai_risks, sizeof r->ai_risks, "%s", p->risks);
    r->ai_rank = rank;
}

static OptRecord *new_rec(OptResult *res, int round, const Cand *c, const Proposal *props)
{
    OptRecord *r;
    if (res->nrec >= res->cap) {
        res->cap = res->cap ? res->cap * 2 : 32;
        res->rec = realloc(res->rec, (size_t)res->cap * sizeof *res->rec);
    }
    r = &res->rec[res->nrec++];
    memset(r, 0, sizeof *r);
    r->round = round;
    snprintf(r->transform, sizeof r->transform, "%s", g_transforms[c->tr].name);
    snprintf(r->func, sizeof r->func, "%s", c->func);
    r->line = c->line;
    snprintf(r->region, sizeof r->region, "%s", c->region);
    snprintf(r->source, sizeof r->source, "%s", c->source ? "model" : "compiler");
    if (c->source && c->proposal >= 0 && props) fill_ai(r, &props[c->proposal], c->proposal + 1);
    snprintf(r->desc, sizeof r->desc, "%s", c->desc);
    r->external = c->external;
    snprintf(r->safety, sizeof r->safety, "%s",
             c->safety == SAFE_NO ? "refused" : c->safety == SAFE_FP ? "fp-reassoc" : "exact");
    snprintf(r->evidence, sizeof r->evidence, "%s", c->evidence);
    snprintf(r->status, sizeof r->status, "rejected");
    return r;
}

static void fail(OptRecord *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void fail(OptRecord *r, const char *fmt, ...)
{
    va_list ap;
    snprintf(r->status, sizeof r->status, "rejected");
    va_start(ap, fmt);
    vsnprintf(r->reason, sizeof r->reason, fmt, ap);
    va_end(ap);
}

/* ---- sets of keys already handled ---- */
typedef struct { char k[160]; } KeyStr;
typedef struct { KeyStr *v; int n, cap; } KeySet;

static int ks_has(const KeySet *s, const char *k)
{
    int i;
    for (i = 0; i < s->n; i++) if (!strcmp(s->v[i].k, k)) return 1;
    return 0;
}

static void ks_add(KeySet *s, const char *k)
{
    if (s->n >= s->cap) { s->cap = s->cap ? s->cap * 2 : 32; s->v = realloc(s->v, (size_t)s->cap * sizeof *s->v); }
    snprintf(s->v[s->n++].k, sizeof s->v[0].k, "%s", k);
}

/* ================================================================== */
/* Outside advice (a person or an AI): "transform function line"       */
/* ================================================================== */

typedef struct { char tr[24]; char func[48]; int line; int used; } Sugg;

static int read_suggestions(const char *path, Sugg *v, int max)
{
    FILE *f = fopen(path, "r");
    char buf[256];
    int n = 0;
    if (!f) return -1;
    while (fgets(buf, sizeof buf, f) && n < max) {
        char *p = buf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == 0) continue;
        if (sscanf(p, "%23s %47s %d", v[n].tr, v[n].func, &v[n].line) == 3) { v[n].used = 0; n++; }
    }
    fclose(f);
    return n;
}

void write_advisor_prompt(const char *path, const char *source_text, const HTContext *ctx,
                          const Cand *cands, int ncands)
{
    FILE *f = fopen(path, "w");
    int i;
    (void)cands; (void)ncands;
    if (!f) { perror(path); return; }
    fputs(
"You are advising a compiler's loop-optimization pass for a small C subset\n"
"(int/float/double, 1-D and 2-D global arrays, for/while/if, pthreads).\n\n"
"The compiler can apply these transformations, each only when it can prove it is safe:\n"
"  interchange  swap two perfectly nested loops so the inner loop walks memory sequentially\n"
"  fusion       merge two adjacent loops that cover the same range and share arrays\n"
"  promotion    keep an array element that is the same on every iteration in a register\n\n"
"Look at the program and the profile below and name the places where one of these\n"
"would help.  The compiler will check every suggestion itself; a suggestion that is\n"
"unsafe or useless is simply rejected, so do not worry about being certain.\n\n"
"REPLY FORMAT - one suggestion per line, nothing else:\n"
"  <transformation> <function name> <line number of the first loop involved>\n"
"For example:\n"
"  interchange col_walk 46\n\n"
"----- per-thread profile estimated by the compiler -----\n", f);
    for (i = 0; i < ctx->nthreads; i++) {
        const Profile *p = &ctx->threads[i].prof;
        fprintf(f, "  %-24s %-34s ops=%.3g  memory-demand=%.2f  strided accesses=%.0f\n",
                ctx->threads[i].name, p->klass, p->total_ops, p->n_bw, p->strided_acc);
    }
    fputs("\n----- program -----\n", f);
    {
        int line = 1;
        const char *s = source_text;
        fprintf(f, "%4d  ", line);
        for (; *s; s++) {
            fputc(*s, f);
            if (*s == '\n' && s[1]) fprintf(f, "%4d  ", ++line);
        }
        if (s > source_text && s[-1] != '\n') fputc('\n', f);
    }
    fclose(f);
}

/* ================================================================== */
/* Evaluation of one candidate                                         */
/* ================================================================== */

typedef struct {
    const OptOptions *o;
    Node *cur;
    HTContext *ctx;             /* analysis of cur           */
    Stats before;
    Bench B;
    int gcc_ok, baseline_ok;
    char base_inst[300], base_plain[300];
    char last_plain[300];
} Ev;

static Node *main_of(Node *program)
{
    Node *f;
    for (f = program->a; f; f = f->next)
        if (f->kind == N_FUNC && !strcmp(f->text, "main")) return f;
    return NULL;
}

static Node *func_named(Node *program, const char *name)
{
    Node *f;
    for (f = program->a; f; f = f->next)
        if (f->kind == N_FUNC && !strcmp(f->text, name)) return f;
    return NULL;
}

static int analyse(HTContext *ctx, Node *prog, const OptOptions *o, Stats *st)
{
    Profile mp;
    Node *m;
    g_errors = 0;
    if (!run_analysis(ctx, prog, &o->topo, o->spread)) return 0;
    m = main_of(prog);
    if (m) profile_function(ctx, m, &mp);
    else memset(&mp, 0, sizeof mp);
    stats_of(ctx, &mp, st);
    return 1;
}

/* returns 1 if the candidate passed every stage; *score is higher for better */
static int evaluate(Ev *E, const Cand *c, OptRecord *r, double *score)
{
    const OptOptions *o = E->o;
    Node *trial = ast_clone(E->cur);
    HTContext *ctx2 = calloc(1, sizeof *ctx2);
    Stats after;
    int passed = 0;
    char bin[300], err[400];

    r->has_model = 1;
    r->before = E->before;
    g_quiet = 1;
    g_errors = 0; sema_run(trial);
    if (g_errors) { fail(r, "internal: the working copy failed semantic analysis"); goto done; }
    if (!transform_apply(trial, NULL, c)) { fail(r, "could not locate the opportunity on the working copy"); goto done; }
    g_errors = 0; sema_run(trial);
    if (g_errors) { fail(r, "the rewritten program failed semantic analysis"); goto done; }
    if (!analyse(ctx2, trial, o, &after)) { fail(r, "the rewritten program failed thread analysis"); goto done; }
    if (ctx2->nthreads != E->ctx->nthreads) { fail(r, "the transformation changed the thread structure"); goto done; }
    r->after = after;
    explain_candidate(c, &E->before, &after, r->explanation, sizeof r->explanation);

    /* ---- stage: differential test ---- */
    if (o->verify) {
        int v;
        if (!E->gcc_ok) {
            fail(r, "no C compiler available for the differential test (use --no-verify to rely on the static proof alone)");
            goto done;
        }
        if (!E->baseline_ok) {
            fail(r, "the current program could not be built and run cleanly, so equivalence cannot be tested");
            goto done;
        }
        if (!vb_build(&E->B, trial, 1, bin, sizeof bin, err, sizeof err)) {
            fail(r, "the rewritten program does not compile: %.200s", err);
            goto done;
        }
        v = vb_differential(&E->B, E->base_inst, bin, c->safety == SAFE_FP, &r->vruns, &r->vpass, r->vnote, sizeof r->vnote);
        r->verified = (v == 1);
        if (v == 0) { fail(r, "DIFFERENTIAL TEST FAILED: %s", r->vnote); goto done; }
        if (v < 0)  { fail(r, "could not verify: %s", r->vnote); goto done; }
        snprintf(r->vnote, sizeof r->vnote, "%d run(s) agreed (%s)", r->vpass,
                 c->safety == SAFE_FP ? "sums compared with a tolerance" : "memory and output identical");
    }

    /* ---- stage: benchmark ---- */
    E->last_plain[0] = 0;
    if (o->bench && E->gcc_ok && E->baseline_ok) {
        BenchResult br;
        if (vb_build(&E->B, trial, 0, bin, sizeof bin, err, sizeof err) &&
            vb_bench(&E->B, E->base_plain, bin, o->bench_runs, &br)) {
            r->benched = 1;
            r->bench_runs = br.runs;
            r->base_s = br.min_a;
            r->cand_s = br.min_b;
            r->speedup = br.min_b > 0 ? br.min_a / br.min_b : 0;
            r->speedup_med = br.med_b > 0 ? br.med_a / br.med_b : 0;
            snprintf(E->last_plain, sizeof E->last_plain, "%s", bin);
        }
    }

    /* ---- decision ---- */
    if (r->benched && r->base_s >= MIN_BENCH_S) {
        snprintf(r->basis, sizeof r->basis, "benchmark");
        if (r->speedup < o->min_speedup || r->speedup_med < 1.0 + (o->min_speedup - 1.0) / 2.0) {
            fail(r, "measured %.2fx best-of-%d (median %.2fx, %.3f s to %.3f s); %.2fx is needed",
                 r->speedup, r->bench_runs, r->speedup_med, r->base_s, r->cand_s, o->min_speedup);
            goto done;
        }
        *score = r->speedup;
    } else {
        /* no usable timing: the cost model alone must predict a clear (3%) gain */
        int timing_wanted = o->bench && E->gcc_ok;
        int strong = after.cost < E->before.cost * (timing_wanted ? 0.97 : 0.995) &&
                     after.contention <= E->before.contention + 1.0;
        snprintf(r->basis, sizeof r->basis, "model");
        if (!strong) {
            fail(r, "the cost model predicts %s (cost %.0f to %.0f, contention %.1f to %.1f)%s",
                 after.cost < E->before.cost ? "too small a gain" : "no gain",
                 E->before.cost, after.cost, E->before.contention, after.contention,
                 r->benched ? "; the program runs too briefly to time reliably" : "");
            goto done;
        }
        *score = E->before.cost / (after.cost > 1 ? after.cost : 1);
    }
    snprintf(r->status, sizeof r->status, "passed");
    passed = 1;
done:
    g_quiet = 0;
    free(ctx2);
    return passed;
}

/* ================================================================== */
/* Driver                                                              */
/* ================================================================== */

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (b) { n = (long)fread(b, 1, (size_t)n, f); b[n] = 0; }
    fclose(f);
    return b;
}


/* ================================================================== */
/* The local language model                                            */
/* ================================================================== */

typedef struct { char key[128]; int prop; } LlmKey;

static char *slurp(const char *path);

static const char *region_func(const LoopRegion *rg, int nr, const char *id, int *line)
{
    int i;
    for (i = 0; i < nr; i++) if (!strcmp(rg[i].id, id)) { *line = rg[i].line; return rg[i].func; }
    *line = 0;
    return "";
}

static OptRecord *ai_rec(OptResult *res, int round, const Proposal *p, int rank, const LoopRegion *rg, int nr)
{
    OptRecord *r;
    const char *fn;
    int line;
    if (res->nrec >= res->cap) {
        res->cap = res->cap ? res->cap * 2 : 32;
        res->rec = realloc(res->rec, (size_t)res->cap * sizeof *res->rec);
    }
    r = &res->rec[res->nrec++];
    memset(r, 0, sizeof *r);
    r->round = round;
    snprintf(r->transform, sizeof r->transform, "%.23s", p->canonical);
    fn = region_func(rg, nr, p->region, &line);
    snprintf(r->func, sizeof r->func, "%s", fn);
    r->line = line;
    snprintf(r->region, sizeof r->region, "%s", p->region);
    snprintf(r->source, sizeof r->source, "model");
    r->external = 1;
    snprintf(r->safety, sizeof r->safety, "-");
    snprintf(r->desc, sizeof r->desc, "%.60s proposed for %.60s", p->canonical, p->region[0] ? p->region : "(no region)");
    fill_ai(r, p, rank);
    return r;
}

/* Ask the model, validate its reply, and attach proposals to the compiler's own candidates.
 * Returns 0 only if the model could not be used at all (nothing is faked in that case). */
static int ai_consult(const OptOptions *o, OptResult *res, Node *cur, const HTContext *ctx, int round,
                      Cand *cands, int ncand, Proposal *props, int *nprops,
                      LoopRegion *regions, int *nreg, LlmKey *lk, int *nlk)
{
    AiInfo *ai = &res->ai;
    LlmConfig cfg = o->llm;
    LlmStatus st;
    char note[300] = "", model[96] = "", perr[300] = "";
    JBuf sys, usr;
    LlmReply rep;
    char *src;
    int i, p, n;

    *nprops = 0;
    ai->mode = cfg.reply_file ? 3 : 1;
    snprintf(ai->endpoint, sizeof ai->endpoint, "%s:%d", cfg.host, cfg.port);

    if (!cfg.reply_file) {
        memset(&st, 0, sizeof st);
        if (!llm_ensure(&cfg, &st, note, sizeof note)) {
            ai->failed = 1;
            snprintf(ai->error, sizeof ai->error, "The local language model is not available: %.280s",
                     st.error[0] ? st.error : "the Ollama service does not answer");
            snprintf(ai->note, sizeof ai->note, "%s", note);
            return 0;
        }
        snprintf(ai->version, sizeof ai->version, "%s", st.version);
        ai->ninstalled = st.nmodels > 8 ? 8 : st.nmodels;
        for (i = 0; i < ai->ninstalled; i++) snprintf(ai->installed_models[i], sizeof ai->installed_models[i], "%s", st.models[i]);
        snprintf(ai->note, sizeof ai->note, "%s", note);
        if (!llm_pick_model(&cfg, &st, model, sizeof model, perr, sizeof perr)) {
            ai->failed = 1;
            snprintf(ai->error, sizeof ai->error, "%s", perr);
            return 0;
        }
    } else {
        snprintf(model, sizeof model, "(reply file)");
    }
    ai->available = 1;
    snprintf(ai->model, sizeof ai->model, "%s", model);

    *nreg = advisor_regions(cur, regions, MAX_REGIONS);
    src = slurp(g_filename);
    jb_init(&sys); jb_init(&usr);
    advisor_build_prompt(src ? src : "", cur, ctx, regions, *nreg, &sys, &usr);
    free(src);
    ai->prompt_chars = (int)(sys.len + usr.len);
    if (o->llm_prompt_file) {                           /* transparency: the exact text the model receives */
        FILE *pf = fopen(o->llm_prompt_file, "w");
        if (pf) { fprintf(pf, "=== SYSTEM ===\n%s\n=== USER ===\n%s", sys.p ? sys.p : "", usr.p ? usr.p : ""); fclose(pf); }
    }

    if (!llm_chat(&cfg, model, sys.p, usr.p, &rep)) {
        jb_free(&sys); jb_free(&usr);
        ai->failed = 1;
        snprintf(ai->error, sizeof ai->error, "The local language model did not answer: %.250s%s", rep.error,
                 rep.timed_out ? " (raise --llm-timeout, use a smaller model, or run with --offline-optimize)" : "");
        llm_reply_free(&rep);
        return 0;
    }
    jb_free(&sys); jb_free(&usr);
    ai->seconds = rep.seconds;
    ai->prompt_tokens = rep.prompt_tokens;
    ai->eval_tokens = rep.eval_tokens;
    ai->truncated = rep.truncated;
    ai->response_chars = (int)strlen(rep.content);
    {
        size_t k, m = 0;
        for (k = 0; rep.content[k] && m < sizeof ai->excerpt - 1; k++)
            ai->excerpt[m++] = ((unsigned char)rep.content[k] < 0x20 && rep.content[k] != '\n') ? ' ' : rep.content[k];
        ai->excerpt[m] = 0;
    }

    n = advisor_parse(rep.content, regions, *nreg, props, MAX_PROPOSALS, perr, sizeof perr, ai->notes, &ai->nnotes, 6);
    llm_reply_free(&rep);
    if (n < 0) {
        if (ai->nnotes < 6)
            snprintf(ai->notes[ai->nnotes++], 200, "the model's reply could not be used: %.80s%s", perr,
                     ai->truncated ? " (the reply was cut off at the token limit)" : "");
        n = 0;
    }
    *nprops = n;
    ai->nproposals = n;

    /* attach supported proposals to the compiler's own candidates, by region */
    for (p = 0; p < n; p++) {
        Proposal *pr = &props[p];
        int matched = 0;
        if (pr->status == PS_UNSUPPORTED) ai->nunsupported++;
        else if (pr->status == PS_INVALID) ai->ninvalid++;
        else if (pr->status == PS_DUPLICATE) ai->nduplicate++;
        if (pr->status != PS_OK) continue;
        ai->nsupported++;
        for (i = 0; i < ncand; i++) {
            if (cands[i].tr != pr->tr || strcmp(cands[i].region, pr->region) != 0) continue;
            if (cands[i].source) continue;                                  /* already claimed by an earlier proposal */
            cands[i].external = 1;
            cands[i].source = 1;
            cands[i].proposal = p;
            if (g_transforms[pr->tr].param_name) {
                int v = pr->param, okv = (v == 2 || v == 4 || v == 8 || v == 16);
                if (v && !okv && ai->nnotes < 6)
                    snprintf(ai->notes[ai->nnotes++], 200, "proposal %d: parameter %d is not allowed; using %d", p + 1, v, g_transforms[pr->tr].param_default);
                cands[i].param = okv ? v : g_transforms[pr->tr].param_default;
            }
            if (*nlk < 64) { snprintf(lk[*nlk].key, sizeof lk[0].key, "%s", cands[i].key); lk[(*nlk)++].prop = p; }
            matched++;
        }
        ai->nmatched += matched > 0;
        if (!matched) {
            OptRecord *r = ai_rec(res, round, pr, p + 1, regions, *nreg);
            snprintf(r->status, sizeof r->status, "rejected");
            snprintf(r->reason, sizeof r->reason,
                     "the compiler found no %.30s opportunity at %.60s (the loop is not a supported pattern: it needs constant bounds, affine subscripts and no calls)",
                     pr->canonical, pr->region);
        }
    }
    /* proposals that cannot be attempted at all */
    for (p = 0; p < n; p++) {
        Proposal *pr = &props[p];
        OptRecord *r;
        if (pr->status == PS_OK) continue;
        r = ai_rec(res, round, pr, p + 1, regions, *nreg);
        snprintf(r->status, sizeof r->status, "%s", pr->status == PS_UNSUPPORTED ? "unsupported"
                                                  : pr->status == PS_INVALID ? "invalid" : "rejected");
        snprintf(r->reason, sizeof r->reason, "%s", pr->note);
    }
    return 1;
}

static void relabel_from_keys(Cand *cands, int ncand, const LlmKey *lk, int nlk)
{
    int i, k;
    for (i = 0; i < ncand; i++)
        for (k = 0; k < nlk; k++)
            if (!strcmp(cands[i].key, lk[k].key)) { cands[i].external = 1; cands[i].source = 1; cands[i].proposal = lk[k].prop; }
}

Node *optimize_program(Node *program, const OptOptions *o, OptResult *res)
{
    Node *cur = ast_clone(program);
    Ev E;
    KeySet done_keys = {0}, noted = {0};
    Sugg sug[64];
    int nsug = 0, round, i;
    char tmp[256] = "";
    Proposal props[MAX_PROPOSALS];
    LoopRegion regions[MAX_REGIONS];
    LlmKey lkeys[64];
    int nprops = 0, nreg = 0, nlk = 0;

    memset(res, 0, sizeof *res);
    res->o_plan = o->plan; res->o_fp = o->fp_ok; res->o_verify = o->verify; res->o_bench = o->bench;
    res->o_runs = o->bench_runs; res->o_seeds = o->seeds; res->o_min_speedup = o->min_speedup;
    res->ai.mode = o->use_llm ? (o->llm.reply_file ? 3 : 1) : 2;
    memset(&E, 0, sizeof E);
    E.o = o;
    E.cur = cur;
    E.ctx = calloc(1, sizeof *E.ctx);

    if (o->suggest_file) {
        nsug = read_suggestions(o->suggest_file, sug, 64);
        if (nsug < 0) { snprintf(res->note, sizeof res->note, "could not read advice file %s", o->suggest_file); nsug = 0; }
        res->nsuggest = nsug;
    }

    if (!o->plan && (o->verify || o->bench)) {
        E.gcc_ok = vb_have_cc(o->cc);
        res->gcc_ok = E.gcc_ok;
        if (E.gcc_ok && make_tempdir(tmp, sizeof tmp)) {
            snprintf(E.B.dir, sizeof E.B.dir, "%s", tmp);
            E.B.o = o;
        } else {
            E.gcc_ok = 0;
        }
    }

    for (round = 1; round <= (o->plan ? 1 : o->rounds); round++) {
        Cand cands[MAX_CANDS];
        int ncand = 0, order[MAX_CANDS], nord = 0, evaluated = 0;
        int best = -1, best_rec = -1;
        int np = 0, pidx[MAX_CANDS], prec[MAX_CANDS];
        double pscore[MAX_CANDS];
        char pbin[MAX_CANDS][300];
        int t, pass;

        g_quiet = 1;
        g_errors = 0; sema_run(cur);
        if (g_errors) { g_quiet = 0; snprintf(res->note, sizeof res->note, "the working program failed semantic analysis"); break; }
        if (!analyse(E.ctx, cur, o, &E.before)) { g_quiet = 0; snprintf(res->note, sizeof res->note, "analysis failed"); break; }
        g_quiet = 0;
        if (round == 1) res->first = E.before;

        for (t = 0; t < g_ntransforms; t++)
            ncand += transform_find(t, cur, E.ctx, cands + ncand, MAX_CANDS - ncand);

        if (o->use_llm && round == 1) {
            if (!ai_consult(o, res, cur, E.ctx, round, cands, ncand, props, &nprops, regions, &nreg, lkeys, &nlk)) {
                if (E.B.dir[0]) remove_tree(E.B.dir);
                free(E.ctx);
                return program;                              /* the model could not be used: stop, say so, fake nothing */
            }
        } else if (o->use_llm) {
            relabel_from_keys(cands, ncand, lkeys, nlk);
        }

        if (round == 1 && nsug) {                         /* match outside advice */
            int s;
            for (s = 0; s < nsug; s++)
                for (i = 0; i < ncand; i++)
                    if (!strcmp(sug[s].tr, g_transforms[cands[i].tr].name) && !strcmp(sug[s].func, cands[i].func) &&
                        sug[s].line == cands[i].line) {
                        cands[i].external = 1;
                        sug[s].used = 1;
                    }
            for (s = 0; s < nsug; s++) {
                if (sug[s].used) { res->nsuggest_matched++; continue; }
                {
                    Cand fake;
                    OptRecord *r;
                    int tr = -1;
                    for (t = 0; t < g_ntransforms; t++) if (!strcmp(sug[s].tr, g_transforms[t].name)) tr = t;
                    memset(&fake, 0, sizeof fake);
                    fake.tr = tr < 0 ? 0 : tr;
                    snprintf(fake.func, sizeof fake.func, "%s", sug[s].func);
                    fake.line = sug[s].line;
                    snprintf(fake.desc, sizeof fake.desc, "advice: %.23s in %.47s at line %d", sug[s].tr, sug[s].func, sug[s].line);
                    fake.external = 1;
                    r = new_rec(res, round, &fake, props);
                    snprintf(r->transform, sizeof r->transform, "%.23s", sug[s].tr);
                    snprintf(r->safety, sizeof r->safety, "-");
                    fail(r, tr < 0 ? "unknown transformation name"
                                   : "the compiler found no such opportunity at that place (not a supported loop pattern, or not present)");
                }
            }
        }

        if (round == 1 && o->prompt_file) {
            char *text = slurp(g_filename);
            write_advisor_prompt(o->prompt_file, text ? text : "", E.ctx, cands, ncand);
            free(text);
        }

        /* evaluation order: advice first, then those expected to help */
        for (i = 0; i < ncand; i++) if (cands[i].external) order[nord++] = i;
        {   /* the model's own ranking decides the order among its proposals */
            int a, b;
            for (a = 1; a < nord; a++) {
                int cur_i = order[a];
                int pa = (cands[cur_i].proposal >= 0) ? props[cands[cur_i].proposal].priority : 1000;
                for (b = a - 1; b >= 0; b--) {
                    int pb = (cands[order[b]].proposal >= 0) ? props[cands[order[b]].proposal].priority : 1000;
                    if (pb <= pa) break;
                    order[b + 1] = order[b];
                }
                order[b + 1] = cur_i;
            }
        }
        for (i = 0; i < ncand; i++) if (!cands[i].external && cands[i].promising) order[nord++] = i;
        for (i = 0; i < ncand; i++) if (!cands[i].external && !cands[i].promising) order[nord++] = i;

        /* baseline builds for this round */
        E.baseline_ok = 0;
        if (!o->plan && E.gcc_ok) {
            char e1[300];
            int ok1 = vb_build(&E.B, cur, 1, E.base_inst, sizeof E.base_inst, e1, sizeof e1);
            int ok2 = vb_build(&E.B, cur, 0, E.base_plain, sizeof E.base_plain, e1, sizeof e1);
            if (ok1 && ok2) {
                char *av[2];
                RunInfo ri;
                av[0] = E.base_plain; av[1] = NULL;
                ri = run_proc(av, NULL, NULL, o->timeout, o->mem_mb, NULL);
                E.baseline_ok = !ri.timed_out && ri.exit_code == 0;
            }
            if (!E.baseline_ok && round == 1)
                snprintf(res->note, sizeof res->note,
                         "the program does not build and run cleanly with %s, so candidates cannot be tested", o->cc);
        }
        res->baseline_ok = E.baseline_ok || o->plan;

        for (pass = 0; pass < nord; pass++) {
            Cand *c = &cands[order[pass]];
            OptRecord *r;
            double score = 0;
            char tag[200];

            if (ks_has(&done_keys, c->key)) continue;            /* evaluated and rejected earlier */
            if (c->safety == SAFE_NO && getenv("HT_TEST_FORCE_UNSAFE")) {
                /* TEST HOOK (never set in normal use): pretend the proof succeeded, so tests can
                 * check that the differential test still catches an illegal transformation. */
                c->safety = SAFE_EXACT;
                c->promising = 1;
                snprintf(c->evidence, sizeof c->evidence, "FORCED by HT_TEST_FORCE_UNSAFE");
            }
            if (c->safety == SAFE_NO) {
                snprintf(tag, sizeof tag, "R:%s", c->key);
                if (ks_has(&noted, tag)) continue;
                ks_add(&noted, tag);
                r = new_rec(res, round, c, props);
                explain_candidate(c, NULL, NULL, r->explanation, sizeof r->explanation);
                fail(r, "not provably safe: %s", c->why);
                continue;
            }
            if (c->safety == SAFE_FP && !o->fp_ok) {
                snprintf(tag, sizeof tag, "F:%s", c->key);
                if (ks_has(&noted, tag)) continue;
                ks_add(&noted, tag);
                r = new_rec(res, round, c, props);
                explain_candidate(c, NULL, NULL, r->explanation, sizeof r->explanation);
                fail(r, "legal, but it reorders a floating-point sum and so changes rounding; allow it with --fp-reassoc");
                continue;
            }
            if (o->use_llm && o->llm_only && !c->source) {
                snprintf(tag, sizeof tag, "O:%s", c->key);
                if (ks_has(&noted, tag)) continue;
                ks_add(&noted, tag);
                r = new_rec(res, round, c, props);
                snprintf(r->status, sizeof r->status, "skipped");
                snprintf(r->reason, sizeof r->reason, "found by the compiler but not proposed by the model (--llm-only)");
                continue;
            }
            if (!c->promising && !c->external) {
                snprintf(tag, sizeof tag, "S:%s", c->key);
                if (ks_has(&noted, tag)) continue;
                ks_add(&noted, tag);
                r = new_rec(res, round, c, props);
                snprintf(r->status, sizeof r->status, "skipped");
                snprintf(r->reason, sizeof r->reason, "legal, but the locality analysis expects no gain");
                continue;
            }
            if (o->plan) {
                snprintf(tag, sizeof tag, "P:%s", c->key);
                if (ks_has(&noted, tag)) continue;
                ks_add(&noted, tag);
                r = new_rec(res, round, c, props);
                explain_candidate(c, NULL, NULL, r->explanation, sizeof r->explanation);
                snprintf(r->status, sizeof r->status, "found");
                snprintf(r->reason, sizeof r->reason, "legal; not evaluated (planning only)");
                continue;
            }
            if (evaluated >= o->max_cands) {
                snprintf(tag, sizeof tag, "B:%d:%s", round, c->key);
                if (ks_has(&noted, tag)) continue;
                ks_add(&noted, tag);
                r = new_rec(res, round, c, props);
                snprintf(r->status, sizeof r->status, "skipped");
                snprintf(r->reason, sizeof r->reason, "evaluation budget of %d candidates per round was used up", o->max_cands);
                continue;
            }
            evaluated++;
            r = new_rec(res, round, c, props);
            snprintf(r->evidence, sizeof r->evidence, "%s", c->evidence);
            if (evaluate(&E, c, r, &score)) {
                pidx[np] = order[pass];
                prec[np] = res->nrec - 1;
                pscore[np] = score;
                snprintf(pbin[np], sizeof pbin[np], "%s", E.last_plain);
                np++;
            } else {
                ks_add(&done_keys, c->key);
            }
        }

        /* Choose the winner.  A benchmark winner is the maximum of several noisy numbers, so it is
         * measured again (twice as long) before it is allowed to change the program. */
        while (np > 0 && best < 0) {
            int k, bk = 0;
            OptRecord *wr;
            for (k = 1; k < np; k++) if (pscore[k] > pscore[bk]) bk = k;
            wr = &res->rec[prec[bk]];
            if (!strcmp(wr->basis, "benchmark") && pbin[bk][0]) {
                BenchResult br;
                if (vb_bench(&E.B, E.base_plain, pbin[bk], o->bench_runs * 2, &br)) {
                    double sp = br.min_b > 0 ? br.min_a / br.min_b : 0, sm = br.med_b > 0 ? br.med_a / br.med_b : 0;
                    if (sp < o->min_speedup || sm < 1.0 + (o->min_speedup - 1.0) / 2.0) {
                        fail(wr, "did not confirm on re-measurement (%.2fx best-of-%d, median %.2fx; %.2fx is needed)",
                             sp, br.runs, sm, o->min_speedup);
                        ks_add(&done_keys, cands[pidx[bk]].key);
                        pscore[bk] = pscore[np - 1]; pidx[bk] = pidx[np - 1]; prec[bk] = prec[np - 1];
                        memcpy(pbin[bk], pbin[np - 1], sizeof pbin[bk]);
                        np--;
                        continue;
                    }
                    wr->base_s = br.min_a; wr->cand_s = br.min_b;
                    wr->speedup = sp; wr->speedup_med = sm; wr->bench_runs = br.runs;
                }
            }
            best = pidx[bk];
            best_rec = prec[bk];
        }

        if (o->plan || best < 0) break;

        /* apply the winner to the real working program */
        {
            Cand *c = &cands[best];
            OptRecord *r = &res->rec[best_rec];
            Node *fb;
            g_quiet = 1;
            g_errors = 0; sema_run(cur);
            fb = func_named(cur, c->func);
            r->fn_before = fb ? function_text(fb) : NULL;
            if (!transform_apply(cur, NULL, c)) {
                g_quiet = 0;
                fail(r, "could not apply the winning candidate");
                break;
            }
            g_errors = 0; sema_run(cur);
            fb = func_named(cur, c->func);
            r->fn_after = fb ? function_text(fb) : NULL;
            g_quiet = 0;
            snprintf(r->status, sizeof r->status, "applied");
            res->applied++;
            ks_add(&done_keys, c->key);
        }
    }

    /* final statistics and end-to-end timing */
    g_quiet = 1;
    g_errors = 0; sema_run(cur);
    if (!g_errors && analyse(E.ctx, cur, o, &res->last)) { /* ok */ }
    else res->last = res->first;
    g_quiet = 0;
    if (res->applied > 0 && o->bench && E.gcc_ok && E.baseline_ok) {
        char b0[300], b1[300], e[300];
        BenchResult br;
        if (vb_build(&E.B, program, 0, b0, sizeof b0, e, sizeof e) &&
            vb_build(&E.B, cur, 0, b1, sizeof b1, e, sizeof e) &&
            vb_bench(&E.B, b0, b1, o->bench_runs * 2, &br)) {
            res->orig_s = br.min_a;
            res->final_s = br.min_b;
            if (br.min_a >= MIN_BENCH_S && br.min_b >= br.min_a) {
                /* the individual wins did not add up: do not ship changes that are not an overall gain */
                int k;
                for (k = 0; k < res->nrec; k++)
                    if (!strcmp(res->rec[k].status, "applied")) {
                        snprintf(res->rec[k].status, sizeof res->rec[k].status, "reverted");
                        snprintf(res->rec[k].reason, sizeof res->rec[k].reason,
                                 "the end-to-end measurement showed no overall gain (%.3f s to %.3f s)", br.min_a, br.min_b);
                    }
                snprintf(res->note, sizeof res->note,
                         "all changes were reverted: the final end-to-end timing was not faster (%.3f s to %.3f s)",
                         br.min_a, br.min_b);
                res->reverted = 1;
                res->applied = 0;
            }
        }
    }
    if (E.B.dir[0]) remove_tree(E.B.dir);
    free(E.ctx);
    free(done_keys.v);
    free(noted.v);
    return res->applied > 0 ? cur : program;
}
