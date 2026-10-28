/* codegen.c - emits program_gen.c.
 *
 * The AST is unparsed back to C.  Differences from the input program:
 *   1. a header comment and a compile-time table  __ht_cpu_map[thread id]
 *   2. a helper  __ht_create()  that pins a new thread with a pthread
 *      attribute (pthread_attr_setaffinity_np) before it starts
 *   3. every pthread_create(&t, NULL, f, a)  becomes
 *         __ht_create(&t, f, a, <thread id>)
 * No runtime system is needed; the result compiles with  gcc -pthread.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ht.h"
#include "common.h"

static HTContext *G;
static FILE *out;
static int g_inst;                 /* instrumented build: user main() is renamed */

static void indent(int n) { while (n-- > 0) fputs("    ", out); }

/* ---------- expressions ---------- */

enum { P_ASSIGN = 1, P_OR, P_AND, P_EQ, P_REL, P_ADD, P_MUL, P_UNARY, P_POST, P_PRIM };

static int binop_prec(const char *op)
{
    if (!strcmp(op, "||")) return P_OR;
    if (!strcmp(op, "&&")) return P_AND;
    if (!strcmp(op, "==") || !strcmp(op, "!=")) return P_EQ;
    if (op[0] == '<' || op[0] == '>') return P_REL;
    if (op[0] == '+' || op[0] == '-') return P_ADD;
    return P_MUL;
}

static int node_prec(const Node *e)
{
    switch (e->kind) {
    case N_ASSIGN: return P_ASSIGN;
    case N_BINOP:  return binop_prec(e->text);
    case N_UNOP: case N_ADDR: return P_UNARY;
    case N_INCDEC: return e->ival ? P_UNARY : P_POST;
    case N_INDEX:  return P_POST;
    default:       return P_PRIM;
    }
}

static void emit_expr(const Node *e, int minprec);

static const ThreadSite *site_of(const Node *call)
{
    int i;
    if (!G) return NULL;                       /* plain emission: no pinning */
    for (i = 0; i < G->nsites; i++)
        if (G->sites[i].call == call) return &G->sites[i];
    return NULL;
}

/* thread-id expression of a creation site (constant, or affine in the loop var) */
static void emit_thread_index(const ThreadSite *s)
{
    if (!s->in_loop) { fprintf(out, "%d", s->first_thread); return; }
    {
        long st = s->loop.step, a = s->loop.start;
        if (s->first_thread) fprintf(out, "%d + ", s->first_thread);
        if (st > 0) {
            if (a == 0) fprintf(out, "%s", s->loop.var);
            else fprintf(out, "(%s - %ld)", s->loop.var, a);
            if (st != 1) fprintf(out, " / %ld", st);
        } else {
            fprintf(out, "(%ld - %s)", a, s->loop.var);
            if (st != -1) fprintf(out, " / %ld", -st);
        }
    }
}

static void emit_args(const Node *a)
{
    const char *sep = "";
    for (; a; a = a->next) {
        fputs(sep, out);
        emit_expr(a, P_ASSIGN);
        sep = ", ";
    }
}

static void emit_expr(const Node *e, int minprec)
{
    int p = node_prec(e), paren = p < minprec;

    if (paren) fputc('(', out);
    switch (e->kind) {
    case N_INT:   fprintf(out, "%ld", e->ival); break;
    case N_FLOAT: case N_STR: fputs(e->text, out); break;
    case N_NULL:  fputs("NULL", out); break;
    case N_ID:    fputs(e->text, out); break;
    case N_ADDR:  fputc('&', out); emit_expr(e->a, P_UNARY); break;
    case N_UNOP:  fputs(e->text, out); emit_expr(e->a, P_UNARY); break;
    case N_INCDEC:
        if (e->ival) { fputs(e->text, out); emit_expr(e->a, P_UNARY); }
        else { emit_expr(e->a, P_POST); fputs(e->text, out); }
        break;
    case N_INDEX:
        emit_expr(e->a, P_POST);
        fputc('[', out);
        emit_expr(e->b, P_ASSIGN);
        fputc(']', out);
        break;
    case N_ASSIGN:
        emit_expr(e->a, P_UNARY);
        fprintf(out, " %s ", e->text);
        emit_expr(e->b, P_ASSIGN);
        break;
    case N_BINOP:
        emit_expr(e->a, p);
        fprintf(out, " %s ", e->text);
        emit_expr(e->b, p + 1);
        break;
    case N_CALL: {
        const ThreadSite *s = (lib_kind(e->text) == LIB_PTHREAD_CREATE) ? site_of(e) : NULL;
        if (s) {
            const Node *h = e->a, *fn = h->next->next, *arg = fn->next;
            fputs("__ht_create(", out);
            emit_expr(h, P_ASSIGN);
            fprintf(out, ", %s, ", fn->text);
            emit_expr(arg, P_ASSIGN);
            fputs(", ", out);
            emit_thread_index(s);
            fputc(')', out);
        } else {
            fprintf(out, "%s(", e->text);
            emit_args(e->a);
            fputc(')', out);
        }
        break;
    }
    default: break;
    }
    if (paren) fputc(')', out);
}

/* ---------- statements ---------- */

static void emit_decl_head(const Node *d)
{
    int i;
    fprintf(out, "%s %s", type_name(d->type), d->text);
    for (i = 0; i < d->ndims; i++) fprintf(out, "[%ld]", d->dims[i]);
    if (d->a) { fputs(" = ", out); emit_expr(d->a, P_ASSIGN); }
}

static void emit_stmt(const Node *s, int ind);

static void emit_body(const Node *s, int ind)
{
    /* always braces: harmless and avoids dangling-else surprises */
    fputs("{\n", out);
    if (s->kind == N_BLOCK) {
        const Node *c;
        for (c = s->a; c; c = c->next) emit_stmt(c, ind + 1);
    } else {
        const Node *c;
        for (c = s; c; c = c->next) emit_stmt(c, ind + 1);
    }
    indent(ind);
    fputc('}', out);
}

static void emit_stmt(const Node *s, int ind)
{
    switch (s->kind) {
    case N_BLOCK:
        indent(ind); emit_body(s, ind); fputc('\n', out);
        break;
    case N_VAR_DECL:
        indent(ind); emit_decl_head(s); fputs(";\n", out);
        break;
    case N_EXPR_STMT:
        indent(ind); emit_expr(s->a, P_ASSIGN); fputs(";\n", out);
        break;
    case N_EMPTY:
        indent(ind); fputs(";\n", out);
        break;
    case N_RETURN:
        indent(ind); fputs("return", out);
        if (s->a) { fputc(' ', out); emit_expr(s->a, P_ASSIGN); }
        fputs(";\n", out);
        break;
    case N_IF: {
        const Node *e = s;
        indent(ind);
        for (;;) {
            fputs("if (", out); emit_expr(e->a, P_ASSIGN); fputs(") ", out);
            emit_body(e->b, ind);
            if (!e->c) break;
            fputs(" else ", out);
            if (e->c->kind == N_IF) { e = e->c; continue; }
            emit_body(e->c, ind);
            break;
        }
        fputc('\n', out);
        break;
    }
    case N_FOR:
        indent(ind);
        fputs("for (", out);
        if (s->a) {
            if (s->a->kind == N_VAR_DECL) emit_decl_head(s->a);
            else emit_expr(s->a, P_ASSIGN);
        }
        fputs("; ", out);
        if (s->b) emit_expr(s->b, P_ASSIGN);
        fputs("; ", out);
        if (s->c) emit_expr(s->c, P_ASSIGN);
        fputs(") ", out);
        emit_body(s->d, ind);
        fputc('\n', out);
        break;
    case N_WHILE:
        indent(ind);
        fputs("while (", out); emit_expr(s->a, P_ASSIGN); fputs(") ", out);
        emit_body(s->b, ind);
        fputc('\n', out);
        break;
    default:
        break;
    }
}

static void emit_signature(const Node *f)
{
    const Node *p;
    fprintf(out, "%s %s%s(", type_name(f->type), f->is_ptr ? "*" : "",
            (g_inst && !strcmp(f->text, "main")) ? "__ht_user_main" : f->text);
    if (!f->a) fputs("void", out);
    for (p = f->a; p; p = p->next)
        fprintf(out, "%s%s %s%s", p == f->a ? "" : ", ", type_name(p->type),
                p->is_ptr ? "*" : "", p->text);
    fputc(')', out);
}

/* ---------- header, map, helper ---------- */

static void emit_header(const Mapping *m, const char *strategy, const char *input)
{
    int g, t;
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M", localtime(&now));

    fprintf(out, "/* ===================================================================\n");
    fprintf(out, " * Generated by HT-Compass %s from %s (%s)\n", HT_VERSION, input, ts);
    fprintf(out, " * Mapping strategy: %s (%s), policy: %s\n", strategy, m->algo,
            G->spread ? "spread" : "pack");
    fprintf(out, " * Predicted contention of this mapping: %.1f\n", m->total);
    for (g = 0; g < m->ngroups; g++) {
        int core = m->core[m->g[g][0]];
        fprintf(out, " *   core %d (CPU", core);
        for (t = 0; t < 2; t++)
            if (m->g[g][t] >= 0) fprintf(out, " %d", m->cpu[m->g[g][t]]);
        fputs("):", out);
        for (t = 0; t < 2; t++)
            if (m->g[g][t] >= 0)
                fprintf(out, "%s T%d %s", t ? " +" : "", m->g[g][t],
                        G->threads[m->g[g][t]].name);
        fputc('\n', out);
    }
    fprintf(out, " * ================================================================== */\n");
}

static void emit_helper(const Mapping *m)
{
    int t;
    fprintf(out, "\n/* ---- HT-Compass: compile-time thread-to-CPU map ---- */\n");
    fprintf(out, "#define HT_NUM_THREADS %d\n", G->nthreads);
    fprintf(out, "static const int __ht_cpu_map[HT_NUM_THREADS] = { ");
    for (t = 0; t < G->nthreads; t++)
        fprintf(out, "%s%d", t ? ", " : "", m->cpu[t]);
    fprintf(out, " };\n\n");
    fputs(
"static int __ht_create(pthread_t *t, void *(*fn)(void *), void *arg, int idx)\n"
"{\n"
"    /* Pin the new thread via its attribute so it never runs on another CPU. */\n"
"    if (idx >= 0 && idx < HT_NUM_THREADS && __ht_cpu_map[idx] >= 0 &&\n"
"        __ht_cpu_map[idx] < CPU_SETSIZE) {\n"
"        pthread_attr_t attr;\n"
"        cpu_set_t set;\n"
"        int rc = -1;\n"
"        CPU_ZERO(&set);\n"
"        CPU_SET(__ht_cpu_map[idx], &set);\n"
"        pthread_attr_init(&attr);\n"
"        if (pthread_attr_setaffinity_np(&attr, sizeof(set), &set) == 0)\n"
"            rc = pthread_create(t, &attr, fn, arg);\n"
"        pthread_attr_destroy(&attr);\n"
"        if (rc == 0) return 0;\n"
"    }\n"
"    return pthread_create(t, NULL, fn, arg);   /* CPU absent: run unpinned */\n"
"}\n", out);
}

static void emit_program(const Node *program)
{
    const Node *n;
    /* prototypes so that definition order never matters */
    fputs("\n/* ---- forward declarations ---- */\n", out);
    for (n = program->a; n; n = n->next)
        if (n->kind == N_FUNC && strcmp(n->text, "main") != 0) {
            emit_signature(n);
            fputs(";\n", out);
        }

    fputs("\n/* ---- program ---- */\n", out);
    for (n = program->a; n; n = n->next) {
        if (n->kind == N_VAR_DECL) {
            emit_decl_head(n);
            fputs(";\n", out);
        } else if (n->kind == N_FUNC) {
            fputc('\n', out);
            emit_signature(n);
            fputc(' ', out);
            emit_body(n->b, 0);
            fputc('\n', out);
        }
    }
}

static int preamble_has(const char *needle)
{
    int i;
    for (i = 0; i < g_npreamble; i++)
        if (strstr(g_preamble[i], needle)) return 1;
    return 0;
}

int codegen_emit(const char *path, HTContext *ctx, const Node *program,
                 const Mapping *map, const char *strategy)
{
    int i;

    G = ctx;
    out = fopen(path, "w");
    if (!out) { perror(path); return 0; }

    emit_header(map, strategy, g_filename);
    fputs("#ifndef _GNU_SOURCE\n#define _GNU_SOURCE\n#endif\n", out);
    for (i = 0; i < g_npreamble; i++) fprintf(out, "%s\n", g_preamble[i]);
    if (!preamble_has("pthread.h")) fputs("#include <pthread.h>\n", out);
    fputs("#include <sched.h>\n", out);

    if (ctx->nthreads > 0) emit_helper(map);
    else fputs("\n/* HT-Compass: no pthread_create found - program emitted unchanged. */\n", out);

    emit_program(program);
    fclose(out);
    return 1;
}

/* ================================================================== */
/* Plain and instrumented emission (verification, benchmarking, diffs)  */
/* ================================================================== */

char *function_text(const Node *func)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *saved = out, *m = open_memstream(&buf, &len);
    HTContext *sg = G;
    int si = g_inst;
    if (!m) return NULL;
    out = m; G = NULL; g_inst = 0;
    emit_signature(func);
    fputc(' ', out);
    emit_body(func->b, 0);
    fputc('\n', out);
    fclose(m);
    out = saved; G = sg; g_inst = si;
    return buf;
}

static long elem_count(const Node *d)
{
    long n = 1;
    int i;
    for (i = 0; i < d->ndims; i++) n *= d->dims[i];
    return n;
}

static int is_data_global(const Node *d)
{
    return d->kind == N_VAR_DECL && (d->type == T_INT || d->type == T_FLOAT || d->type == T_DOUBLE);
}

/* Fill every global with deterministic pseudo-random data (when HT_SEED is set) and
 * write a fingerprint of the final global state (when the program ends). */
static void emit_instrumentation(const Node *program)
{
    const Node *d;
    long bound = 1024;
    int have_array = 0;

    for (d = program->a; d; d = d->next)
        if (is_data_global(d) && d->ndims > 0) {
            long n = elem_count(d);
            if (!have_array || n < bound) bound = n;
            have_array = 1;
        }
    if (!have_array || bound > 1024) bound = have_array ? 1024 : 16;
    if (bound < 1) bound = 1;

    fputs(
"\n/* ---- HT-Compass instrumentation (verification builds only) ---- */\n"
"static unsigned long long __ht_rng;\n"
"static unsigned __ht_next(void)\n"
"{ __ht_rng = __ht_rng * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(__ht_rng >> 33); }\n"
"static void __ht_fill_int(int *p, long n, unsigned bound)\n"
"{ long i; for (i = 0; i < n; i++) p[i] = (int)(__ht_next() % bound); }\n"
"static void __ht_fill_flt(float *p, long n)\n"
"{ long i; for (i = 0; i < n; i++) p[i] = (float)((int)(__ht_next() % 2001u) - 1000) / 1000.0f; }\n"
"static void __ht_fill_dbl(double *p, long n)\n"
"{ long i; for (i = 0; i < n; i++) p[i] = (double)((int)(__ht_next() % 2001u) - 1000) / 1000.0; }\n"
"static unsigned long long __ht_hash(const void *p, long bytes)\n"
"{ const unsigned char *b = (const unsigned char *)p; unsigned long long h = 1469598103934665603ULL; long i;\n"
"  for (i = 0; i < bytes; i++) { h ^= b[i]; h *= 1099511628211ULL; } return h; }\n"
"static void __ht_dump_int(FILE *f, const char *name, const int *p, long n)\n"
"{ fprintf(f, \"G %s %ld %016llx - - -\\n\", name, n, __ht_hash(p, n * (long)sizeof(int))); }\n"
"static void __ht_dump_flt(FILE *f, const char *name, const float *p, long n)\n"
"{ double s = 0, q = 0, m = 0; long i;\n"
"  for (i = 0; i < n; i++) { double v = p[i], a = v < 0 ? -v : v; s += v; q += v * v; if (a > m) m = a; }\n"
"  fprintf(f, \"G %s %ld %016llx %.17g %.17g %.17g\\n\", name, n, __ht_hash(p, n * (long)sizeof(float)), s, q, m); }\n"
"static void __ht_dump_dbl(FILE *f, const char *name, const double *p, long n)\n"
"{ double s = 0, q = 0, m = 0; long i;\n"
"  for (i = 0; i < n; i++) { double v = p[i], a = v < 0 ? -v : v; s += v; q += v * v; if (a > m) m = a; }\n"
"  fprintf(f, \"G %s %ld %016llx %.17g %.17g %.17g\\n\", name, n, __ht_hash(p, n * (long)sizeof(double)), s, q, m); }\n"
"\nint main(void)\n{\n"
"    const char *seed = getenv(\"HT_SEED\");\n"
"    const char *dump = getenv(\"HT_DUMP\");\n"
"    FILE *f;\n"
"    int rc;\n"
"    if (seed) {\n"
"        __ht_rng = (unsigned long long)atol(seed) * 2654435761ULL + 12345ULL;\n", out);
    for (d = program->a; d; d = d->next) {
        const char *amp = d->ndims > 0 ? "" : "&";
        if (!is_data_global(d)) continue;
        if (d->type == T_INT)
            fprintf(out, "        __ht_fill_int((int *)%s%s, %ld, %ldu);\n", amp, d->text, elem_count(d), bound);
        else if (d->type == T_FLOAT)
            fprintf(out, "        __ht_fill_flt((float *)%s%s, %ld);\n", amp, d->text, elem_count(d));
        else
            fprintf(out, "        __ht_fill_dbl((double *)%s%s, %ld);\n", amp, d->text, elem_count(d));
    }
    fputs(
"    }\n"
"    rc = __ht_user_main();\n"
"    f = dump ? fopen(dump, \"w\") : stderr;\n"
"    if (f) {\n", out);
    for (d = program->a; d; d = d->next) {
        const char *amp = d->ndims > 0 ? "" : "&";
        if (!is_data_global(d)) continue;
        fprintf(out, "        __ht_dump_%s(f, \"%s\", (const %s *)%s%s, %ld);\n",
                d->type == T_INT ? "int" : d->type == T_FLOAT ? "flt" : "dbl",
                d->text, type_name(d->type), amp, d->text, elem_count(d));
    }
    fputs(
"        if (dump) fclose(f);\n"
"    }\n"
"    return rc;\n"
"}\n", out);
}

int codegen_emit_plain(const char *path, const Node *program, int instrument)
{
    int i;
    FILE *saved = out;
    HTContext *sg = G;
    int si = g_inst;

    out = fopen(path, "w");
    if (!out) { out = saved; return 0; }
    G = NULL;
    g_inst = instrument;
    fputs("#ifndef _GNU_SOURCE\n#define _GNU_SOURCE\n#endif\n", out);
    for (i = 0; i < g_npreamble; i++) fprintf(out, "%s\n", g_preamble[i]);
    if (!preamble_has("pthread.h")) fputs("#include <pthread.h>\n", out);
    if (instrument) fputs("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n", out);
    emit_program(program);
    if (instrument) emit_instrumentation(program);
    fclose(out);
    out = saved; G = sg; g_inst = si;
    return 1;
}
