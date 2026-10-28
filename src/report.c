/* report.c - program.htmeta.json writer (tiny hand-written JSON emitter)
 *            and the human-readable console summary. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ht.h"
#include "opt.h"
#include "common.h"

/* ================================================================== */
/* Minimal JSON writer                                                 */
/* ================================================================== */

typedef struct { FILE *f; int depth; int cnt[32]; } JW;

static void jw_indent(JW *w)
{
    int i;
    for (i = 0; i < w->depth * 2; i++) fputc(' ', w->f);
}

static void jw_pre(JW *w, const char *key)
{
    if (w->cnt[w->depth]++) fputc(',', w->f);
    fputc('\n', w->f);
    jw_indent(w);
    if (key) fprintf(w->f, "\"%s\": ", key);
}

static void jw_escape(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        switch (*s) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\n': fputs("\\n", f); break;
        case '\t': fputs("\\t", f); break;
        default:
            if ((unsigned char)*s < 0x20) fprintf(f, "\\u%04x", *s);
            else fputc(*s, f);
        }
    }
    fputc('"', f);
}

static void jw_open(JW *w, const char *key, char ch)
{
    jw_pre(w, key);
    fputc(ch, w->f);
    w->depth++;
    w->cnt[w->depth] = 0;
}

static void jw_close(JW *w, char ch)
{
    int had = w->cnt[w->depth];
    w->depth--;
    if (had) { fputc('\n', w->f); jw_indent(w); }
    fputc(ch, w->f);
}

static void jw_obj(JW *w, const char *k)  { jw_open(w, k, '{'); }
static void jw_arr(JW *w, const char *k)  { jw_open(w, k, '['); }
static void jw_end_obj(JW *w)             { jw_close(w, '}'); }
static void jw_end_arr(JW *w)             { jw_close(w, ']'); }
static void jw_str(JW *w, const char *k, const char *v) { jw_pre(w, k); jw_escape(w->f, v); }
static void jw_int(JW *w, const char *k, long v)        { jw_pre(w, k); fprintf(w->f, "%ld", v); }
static void jw_num(JW *w, const char *k, double v)      { jw_pre(w, k); fprintf(w->f, "%.4f", v); }
static void jw_bool(JW *w, const char *k, int v)        { jw_pre(w, k); fputs(v ? "true" : "false", w->f); }

static void jw_intarr(JW *w, const char *k, const int *v, int n)
{
    int i;
    jw_pre(w, k);
    fputc('[', w->f);
    for (i = 0; i < n; i++) fprintf(w->f, "%s%d", i ? ", " : "", v[i]);
    fputc(']', w->f);
}

/* ================================================================== */
/* JSON report                                                         */
/* ================================================================== */

static void write_profile(JW *w, const Profile *p)
{
    int i;
    jw_obj(w, "profile");
    jw_num(w, "int_ops", p->int_ops);
    jw_num(w, "fp_ops", p->fp_ops);
    jw_num(w, "heavy_ops", p->heavy_ops);
    jw_num(w, "loads", p->loads);
    jw_num(w, "stores", p->stores);
    jw_num(w, "branches", p->branches);
    jw_num(w, "io_ops", p->io_ops);
    jw_num(w, "lock_ops", p->lock_ops);
    jw_num(w, "total_ops", p->total_ops);
    jw_num(w, "estimated_dram_lines", p->dram_lines);
    jw_num(w, "working_set_bytes", p->ws_bytes);
    jw_int(w, "assumed_trip_counts", p->assumed_trips);
    jw_obj(w, "access_patterns");
    jw_num(w, "sequential", p->seq_acc);
    jw_num(w, "strided", p->strided_acc);
    jw_num(w, "irregular", p->irregular_acc);
    jw_end_obj(w);
    jw_obj(w, "normalized_demand");
    jw_num(w, "integer", p->n_int);
    jw_num(w, "floating_point", p->n_fp);
    jw_num(w, "heavy_math", p->n_heavy);
    jw_num(w, "load_store", p->n_ldst);
    jw_num(w, "memory_bandwidth", p->n_bw);
    jw_end_obj(w);
    jw_end_obj(w);

    jw_arr(w, "arrays");
    for (i = 0; i < p->narrays; i++) {
        const ArrayUse *a = &p->arrays[i];
        jw_obj(w, NULL);
        jw_str(w, "name", a->sym->name);
        jw_str(w, "scope", a->sym->is_global ? "global" : "local");
        jw_int(w, "bytes", a->sym->bytes);
        jw_num(w, "reads", a->reads);
        jw_num(w, "writes", a->writes);
        jw_num(w, "footprint_bytes", a->footprint);
        jw_end_obj(w);
    }
    jw_end_arr(w);

    jw_arr(w, "shared_scalars_read");
    for (i = 0; i < p->ngr; i++) { jw_pre(w, NULL); jw_escape(w->f, p->gread[i]); }
    jw_end_arr(w);
    jw_arr(w, "shared_scalars_written");
    for (i = 0; i < p->ngw; i++) { jw_pre(w, NULL); jw_escape(w->f, p->gwritten[i]); }
    jw_end_arr(w);
    jw_arr(w, "locks");
    for (i = 0; i < p->nlocks; i++) { jw_pre(w, NULL); jw_escape(w->f, p->locks[i]); }
    jw_end_arr(w);
}

static void write_mapping(JW *w, const char *key, HTContext *ctx, const Mapping *m)
{
    int g, t;
    jw_obj(w, key);
    jw_str(w, "algorithm", m->algo);
    jw_num(w, "predicted_total_contention", m->total);
    jw_arr(w, "cores");
    for (g = 0; g < m->ngroups; g++) {
        int th[2], cpus[2], n = 0, core = m->core[m->g[g][0]];
        double sc = 0;
        for (t = 0; t < 2; t++)
            if (m->g[g][t] >= 0) { th[n] = m->g[g][t]; cpus[n] = m->cpu[th[n]]; n++; }
        if (n == 2) sc = ctx->pair[th[0]][th[1]].score;
        jw_obj(w, NULL);
        jw_int(w, "core", core);
        jw_intarr(w, "cpus", cpus, n);
        jw_intarr(w, "threads", th, n);
        jw_num(w, "pair_score", sc);
        jw_str(w, "advice", n < 2 ? "thread runs alone on this core"
                         : sc >= SEV_HIGH ? "high contention: prefer separate cores if available"
                         : sc >= SEV_MED  ? "moderate contention: SMT co-location acceptable"
                                          : "low contention: SMT co-location is beneficial");
        jw_end_obj(w);
    }
    jw_end_arr(w);
    jw_end_obj(w);
}


/* ---------- optimization section ---------- */

static void write_stats(JW *w, const char *key, const Stats *st)
{
    jw_obj(w, key);
    jw_num(w, "operations", st->ops);
    jw_num(w, "loads", st->loads);
    jw_num(w, "stores", st->stores);
    jw_num(w, "estimated_dram_lines", st->dram);
    jw_num(w, "strided_accesses", st->strided);
    jw_num(w, "irregular_accesses", st->irregular);
    jw_num(w, "predicted_contention", st->contention);
    jw_num(w, "model_cost", st->cost);
    jw_end_obj(w);
}


static void write_ai(JW *w, const AiInfo *a)
{
    int i;
    jw_obj(w, "ai");
    jw_str(w, "mode", a->mode == 1 ? "local-model" : a->mode == 2 ? "offline-rules" : a->mode == 3 ? "reply-file (test hook, not real inference)" : "none");
    jw_bool(w, "available", a->available);
    jw_bool(w, "failed", a->failed);
    if (a->error[0]) jw_str(w, "error", a->error);
    if (a->mode == 1 || a->mode == 3) {
        jw_str(w, "model", a->model);
        jw_str(w, "endpoint", a->endpoint);
        if (a->version[0]) jw_str(w, "ollama_version", a->version);
        if (a->note[0]) jw_str(w, "service_note", a->note);
        jw_arr(w, "installed_models");
        for (i = 0; i < a->ninstalled; i++) { jw_pre(w, NULL); jw_escape(w->f, a->installed_models[i]); }
        jw_end_arr(w);
        jw_obj(w, "request");
        jw_int(w, "prompt_characters", a->prompt_chars);
        jw_end_obj(w);
        jw_obj(w, "response");
        jw_int(w, "characters", a->response_chars);
        jw_num(w, "seconds", a->seconds);
        jw_int(w, "prompt_tokens", a->prompt_tokens);
        jw_int(w, "generated_tokens", a->eval_tokens);
        jw_bool(w, "cut_off_at_token_limit", a->truncated);
        jw_str(w, "excerpt", a->excerpt);
        jw_end_obj(w);
        jw_obj(w, "proposals");
        jw_int(w, "total", a->nproposals);
        jw_int(w, "supported", a->nsupported);
        jw_int(w, "matched_a_site", a->nmatched);
        jw_int(w, "unsupported", a->nunsupported);
        jw_int(w, "invalid", a->ninvalid);
        jw_int(w, "duplicate", a->nduplicate);
        jw_end_obj(w);
        jw_arr(w, "validation_notes");
        for (i = 0; i < a->nnotes; i++) { jw_pre(w, NULL); jw_escape(w->f, a->notes[i]); }
        jw_end_arr(w);
    }
    jw_end_obj(w);
}

static void write_opt(JW *w, const OptResult *o)
{
    int i;
    jw_obj(w, "optimization");
    jw_bool(w, "plan_only", o->o_plan);
    jw_obj(w, "settings");
    jw_bool(w, "fp_reassociation_allowed", o->o_fp);
    jw_bool(w, "differential_test", o->o_verify);
    jw_bool(w, "benchmark_selection", o->o_bench);
    jw_num(w, "min_speedup", o->o_min_speedup);
    jw_int(w, "benchmark_runs", o->o_runs);
    jw_int(w, "random_data_seeds", o->o_seeds);
    jw_end_obj(w);
    write_ai(w, &o->ai);
    jw_bool(w, "c_compiler_available", o->gcc_ok);
    jw_bool(w, "baseline_runs_cleanly", o->baseline_ok);
    jw_int(w, "applied", o->applied);
    write_stats(w, "model_before", &o->first);
    write_stats(w, "model_after", &o->last);
    if (o->orig_s > 0 && o->final_s > 0) {
        jw_obj(w, "end_to_end_timing");
        jw_num(w, "original_seconds", o->orig_s);
        jw_num(w, "optimized_seconds", o->final_s);
        jw_num(w, "speedup", o->orig_s / o->final_s);
        jw_end_obj(w);
    }
    if (o->nsuggest) {
        jw_obj(w, "advice");
        jw_int(w, "suggestions_given", o->nsuggest);
        jw_int(w, "matched_an_opportunity", o->nsuggest_matched);
        jw_end_obj(w);
    }
    if (o->note[0]) jw_str(w, "note", o->note);
    jw_arr(w, "candidates");
    for (i = 0; i < o->nrec; i++) {
        const OptRecord *r = &o->rec[i];
        jw_obj(w, NULL);
        jw_int(w, "round", r->round);
        jw_str(w, "transformation", r->transform);
        jw_str(w, "function", r->func);
        jw_int(w, "line", r->line);
        jw_str(w, "description", r->desc);
        if (r->region[0]) jw_str(w, "region", r->region);
        jw_str(w, "source", r->source[0] ? r->source : "compiler");
        if (r->ai_rank > 0) {
            jw_obj(w, "model_proposal");
            jw_int(w, "rank", r->ai_rank);
            jw_str(w, "explanation", r->ai_explanation);
            jw_str(w, "expected_benefit", r->ai_benefit);
            jw_str(w, "preconditions", r->ai_preconditions);
            jw_str(w, "risks", r->ai_risks);
            jw_end_obj(w);
        }
        if (r->explanation[0]) jw_str(w, "explanation", r->explanation);
        jw_str(w, "safety", r->safety);
        jw_str(w, "status", r->status);
        if (r->reason[0]) jw_str(w, "reason", r->reason);
        if (r->evidence[0]) jw_str(w, "legality_evidence", r->evidence);
        jw_bool(w, "suggested_by_advice", r->external);
        if (r->basis[0]) jw_str(w, "decided_by", r->basis);
        if (r->has_model) {
            jw_obj(w, "model");
            write_stats(w, "before", &r->before);
            write_stats(w, "after", &r->after);
            jw_end_obj(w);
        }
        if (r->vruns || r->vnote[0]) {
            jw_obj(w, "differential_test");
            jw_bool(w, "passed", r->verified);
            jw_int(w, "runs", r->vruns);
            jw_int(w, "agreed", r->vpass);
            jw_str(w, "note", r->vnote);
            jw_end_obj(w);
        }
        if (r->benched) {
            jw_obj(w, "benchmark");
            jw_num(w, "baseline_seconds", r->base_s);
            jw_num(w, "candidate_seconds", r->cand_s);
            jw_num(w, "speedup", r->speedup);
            jw_num(w, "speedup_median", r->speedup_med);
            jw_int(w, "runs", r->bench_runs);
            jw_end_obj(w);
        }
        if (r->fn_before && r->fn_after) {
            jw_obj(w, "code");
            jw_str(w, "before", r->fn_before);
            jw_str(w, "after", r->fn_after);
            jw_end_obj(w);
        }
        jw_end_obj(w);
    }
    jw_end_arr(w);
    jw_end_obj(w);
}

int write_json(const char *path, HTContext *ctx, const Mapping *chosen, const char *strategy)
{
    JW w;
    int i, j, c;
    const Topology *T = &ctx->topo;
    FILE *f = fopen(path, "w");

    if (!f) { perror(path); return 0; }
    memset(&w, 0, sizeof w);
    w.f = f;
    fputc('{', f);
    w.depth = 1;

    jw_str(&w, "tool", "HT-Compass");
    jw_str(&w, "version", HT_VERSION);
    jw_str(&w, "input_file", g_filename);

    jw_obj(&w, "topology");
    jw_str(&w, "source", T->source);
    jw_bool(&w, "smt_detected", T->smt_detected);
    jw_int(&w, "cores", T->ncores);
    jw_int(&w, "logical_cpus", T->ncpus);
    jw_int(&w, "l1d_bytes", T->l1d);
    jw_int(&w, "l2_bytes", T->l2);
    jw_str(&w, "cache_source", T->cache_source);
    jw_arr(&w, "core_siblings");
    for (c = 0; c < T->ncores; c++) {
        jw_pre(&w, NULL);
        fprintf(f, "[%d", T->cpus[c][0]);
        if (T->nsib[c] > 1) fprintf(f, ", %d", T->cpus[c][1]);
        fputc(']', f);
    }
    jw_end_arr(&w);
    jw_end_obj(&w);

    jw_obj(&w, "program");
    jw_int(&w, "thread_sites", ctx->nsites);
    jw_int(&w, "threads", ctx->nthreads);
    jw_int(&w, "assumed_trip_counts", ctx->assumed_trips_total);
    jw_end_obj(&w);

    jw_obj(&w, "model");
    jw_obj(&w, "weights");
    jw_num(&w, "execution_units", W_EXEC);
    jw_num(&w, "load_store", W_LDST);
    jw_num(&w, "cache", W_CACHE);
    jw_num(&w, "memory_bandwidth", W_BW);
    jw_end_obj(&w);
    jw_obj(&w, "severity_thresholds");
    jw_num(&w, "high", SEV_HIGH);
    jw_num(&w, "medium", SEV_MED);
    jw_end_obj(&w);
    jw_int(&w, "default_trip_count", DEFAULT_TRIPS);
    jw_num(&w, "bandwidth_reference_bytes_per_op", BW_REF);
    jw_end_obj(&w);

    jw_arr(&w, "threads");
    for (i = 0; i < ctx->nthreads; i++) {
        const Thread *t = &ctx->threads[i];
        const ThreadSite *s = &ctx->sites[t->site];
        jw_obj(&w, NULL);
        jw_int(&w, "id", t->id);
        jw_str(&w, "name", t->name);
        jw_str(&w, "worker", t->func->text);
        jw_int(&w, "create_line", t->line);
        jw_str(&w, "handle", s->handle);
        jw_str(&w, "created_in", s->in_func);
        jw_str(&w, "class", t->prof.klass);
        jw_int(&w, "start_event", t->ev_start);
        jw_int(&w, "end_event", t->ev_end >= EV_INF ? -1 : t->ev_end);
        jw_int(&w, "assigned_core", chosen->core[i]);
        jw_int(&w, "assigned_cpu", chosen->cpu[i]);
        write_profile(&w, &t->prof);
        jw_end_obj(&w);
    }
    jw_end_arr(&w);

    jw_arr(&w, "contention_pairs");
    for (i = 0; i < ctx->nthreads; i++)
        for (j = i + 1; j < ctx->nthreads; j++) {
            const PairInfo *P = &ctx->pair[i][j];
            jw_obj(&w, NULL);
            jw_int(&w, "a", i);
            jw_int(&w, "b", j);
            jw_num(&w, "score", P->score);
            jw_str(&w, "severity", P->overlap ? severity(P->score) : "none");
            jw_bool(&w, "may_run_concurrently", P->overlap);
            jw_obj(&w, "components");
            jw_num(&w, "execution_units", P->exec);
            jw_num(&w, "load_store", P->ldst);
            jw_num(&w, "cache", P->cache);
            jw_num(&w, "memory_bandwidth", P->bw);
            jw_num(&w, "overlap_factor", P->ofac);
            jw_num(&w, "lock_factor", P->lockf);
            jw_end_obj(&w);
            jw_arr(&w, "conflicts");
            for (c = 0; c < P->nconf; c++) { jw_pre(&w, NULL); jw_escape(f, P->conf[c]); }
            jw_end_arr(&w);
            jw_end_obj(&w);
        }
    jw_end_arr(&w);

    jw_arr(&w, "high_contention_pairs");
    for (i = 0; i < ctx->nthreads; i++)
        for (j = i + 1; j < ctx->nthreads; j++)
            if (ctx->pair[i][j].overlap && ctx->pair[i][j].score >= SEV_HIGH) {
                int ij[2] = { i, j };
                jw_intarr(&w, NULL, ij, 2);
            }
    jw_end_arr(&w);

    jw_obj(&w, "mapping");
    jw_str(&w, "strategy_emitted", strategy);
    jw_str(&w, "policy", ctx->spread ? "spread" : "pack");
    jw_num(&w, "naive_total", ctx->m_naive.total);
    jw_num(&w, "optimized_total", ctx->m_opt.total);
    jw_num(&w, "worst_total", ctx->m_worst.total);
    jw_num(&w, "predicted_reduction_vs_naive_percent",
           ctx->m_naive.total > 0 ? 100.0 * (ctx->m_naive.total - ctx->m_opt.total) / ctx->m_naive.total : 0.0);
    write_mapping(&w, "emitted", ctx, chosen);
    write_mapping(&w, "optimized", ctx, &ctx->m_opt);
    write_mapping(&w, "naive", ctx, &ctx->m_naive);
    jw_end_obj(&w);

    if (ctx->opt) write_opt(&w, ctx->opt);

    jw_arr(&w, "warnings");
    for (i = 0; i < g_nwarnings; i++) { jw_pre(&w, NULL); jw_escape(f, g_warnings[i]); }
    jw_end_arr(&w);

    w.depth = 0;
    fputs("\n}\n", f);
    fclose(f);
    return 1;
}

/* ================================================================== */
/* Console summary                                                     */
/* ================================================================== */


static void print_opt_summary(const OptResult *o)
{
    int i, shown = 0;
    if (o->ai.mode == 1 || o->ai.mode == 3) {
        const AiInfo *a = &o->ai;
        if (a->mode == 3) printf("AI: TEST REPLY read from a file (not a real model, not real inference)\n");
        else printf("AI: local model '%s' via Ollama %s (%s)\n", a->model, a->version[0] ? a->version : "?", a->endpoint);
        printf("    asked with %d characters; the model answered with %d characters in %.1f s (%d tokens)%s\n",
               a->prompt_chars, a->response_chars, a->seconds, a->eval_tokens, a->truncated ? ", cut off at the token limit" : "");
        printf("    proposals: %d total, %d supported (%d matched a place in the program), %d unsupported, %d invalid, %d duplicate\n",
               a->nproposals, a->nsupported, a->nmatched, a->nunsupported, a->ninvalid, a->nduplicate);
        { int k; for (k = 0; k < a->nnotes; k++) printf("    note: %s\n", a->notes[k]); }
    } else if (o->ai.mode == 2) {
        printf("AI: not used (offline mode: the compiler's own rules found the opportunities)\n");
    }
    printf("Optimization: %d change(s) applied", o->applied);
    if (o->o_plan) printf("  (planning only: nothing was built or run)");
    printf("\n");
    if (o->note[0]) printf("  note: %s\n", o->note);
    for (i = 0; i < o->nrec; i++) {
        const OptRecord *r = &o->rec[i];
        if (!strcmp(r->status, "skipped") && shown > 12) continue;
        printf("  %-11s %-11s %s%s\n", r->status, r->transform, r->desc, !strcmp(r->source, "model") ? "   [model]" : "");
        if (!strcmp(r->status, "applied") || !strcmp(r->status, "passed")) {
            if (r->benched && !strcmp(r->basis, "benchmark"))
                printf("           measured %.2fx best-of-%d (%.3f s to %.3f s, median %.2fx); %s\n", r->speedup, r->bench_runs, r->base_s, r->cand_s, r->speedup_med,
                       r->vnote[0] ? r->vnote : "");
            else
                printf("           decided by the cost model (%s); %s\n",
                       r->benched ? "program too brief to time" : "no timing available",
                       r->vnote[0] ? r->vnote : "not differentially tested");
            if (r->has_model)
                printf("           model: DRAM lines %.0f to %.0f, operations %.0f to %.0f, contention %.1f to %.1f\n",
                       r->before.dram, r->after.dram, r->before.ops, r->after.ops,
                       r->before.contention, r->after.contention);
        } else if (r->reason[0]) {
            printf("           %s\n", r->reason);
        }
        shown++;
    }
    if (o->orig_s > 0 && o->final_s > 0)
        printf("  end to end: %.3f s to %.3f s  (%.2fx)\n", o->orig_s, o->final_s, o->orig_s / o->final_s);
    printf("\n");
}

void print_summary(HTContext *ctx, const Mapping *chosen, const char *strategy)
{
    int i, j, g, t;
    char b1[32], b2[32];
    const Topology *T = &ctx->topo;

    fmt_bytes((double)T->l1d, b1, sizeof b1);
    fmt_bytes((double)T->l2, b2, sizeof b2);
    printf("HT-Compass %s - %s\n", HT_VERSION, g_filename);
    printf("Topology : %d core(s), %d logical CPU(s) [%s]; L1D %s, L2 %s\n",
           T->ncores, T->ncpus, T->source, b1, b2);
    printf("Threads  : %d (from %d creation site(s))\n\n", ctx->nthreads, ctx->nsites);
    if (ctx->opt) print_opt_summary(ctx->opt);

    if (ctx->nthreads == 0) {
        printf("No pthread_create calls found; nothing to optimize.\n");
        return;
    }

    printf("  %-3s %-22s %-34s %7s %6s %9s\n", "id", "thread", "class", "ops", "bw", "footprint");
    for (i = 0; i < ctx->nthreads; i++) {
        const Profile *p = &ctx->threads[i].prof;
        char fp[32];
        fmt_bytes(p->ws_bytes, fp, sizeof fp);
        printf("  T%-2d %-22s %-34s %7.2g %6.2f %9s\n", i, ctx->threads[i].name, p->klass,
               p->total_ops, p->n_bw, fp);
    }

    printf("\nContention scores (0-100, higher = worse when sharing a core):\n");
    for (i = 0; i < ctx->nthreads; i++)
        for (j = i + 1; j < ctx->nthreads; j++) {
            const PairInfo *P = &ctx->pair[i][j];
            printf("  T%d-T%d  %5.1f  %-6s", i, j, P->score, P->overlap ? severity(P->score) : "none");
            if (P->nconf) printf("  %s", P->conf[0]);
            printf("\n");
        }

    printf("\nMapping (%s, %s):\n", strategy, chosen->algo);
    for (g = 0; g < chosen->ngroups; g++) {
        printf("  core %d (CPU", chosen->core[chosen->g[g][0]]);
        for (t = 0; t < 2; t++)
            if (chosen->g[g][t] >= 0) printf(" %d", chosen->cpu[chosen->g[g][t]]);
        printf("): ");
        for (t = 0; t < 2; t++)
            if (chosen->g[g][t] >= 0)
                printf("%sT%d %s", t ? " + " : "", chosen->g[g][t], ctx->threads[chosen->g[g][t]].name);
        printf("\n");
    }
    printf("\nPredicted total contention: naive %.1f -> optimized %.1f", ctx->m_naive.total, ctx->m_opt.total);
    if (ctx->m_naive.total > 0)
        printf("  (%.0f%% lower)", 100.0 * (ctx->m_naive.total - ctx->m_opt.total) / ctx->m_naive.total);
    printf("   [worst case %.1f]\n", ctx->m_worst.total);
}
