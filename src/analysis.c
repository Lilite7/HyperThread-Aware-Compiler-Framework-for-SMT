/* analysis.c - AST utilities, thread discovery and concurrency analysis.
 *
 * Thread discovery finds every pthread_create call site, resolves the worker
 * function, and expands creation loops with constant bounds into thread
 * instances.  The concurrency analyzer walks create/join calls in source
 * order and assigns each thread a lifetime [create event, join event];
 * two threads may share a core's resources only if their lifetimes overlap.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ht.h"
#include "common.h"

/* ================================================================== */
/* Expression utilities                                                */
/* ================================================================== */

int const_eval(const Node *e, long *out)
{
    long a, b;
    if (!e) return 0;
    switch (e->kind) {
    case N_INT:
        *out = e->ival;
        return 1;
    case N_UNOP:
        if (e->text[0] == '-' && const_eval(e->a, &a)) { *out = -a; return 1; }
        return 0;
    case N_BINOP:
        if (strlen(e->text) != 1) return 0;
        if (!const_eval(e->a, &a) || !const_eval(e->b, &b)) return 0;
        switch (e->text[0]) {
        case '+': *out = a + b; return 1;
        case '-': *out = a - b; return 1;
        case '*': *out = a * b; return 1;
        case '/': if (!b) return 0; *out = a / b; return 1;
        case '%': if (!b) return 0; *out = a % b; return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static int any_child(const Node *e, int (*fn)(const Node *, const char *), const char *s)
{
    const Node *c, *k;
    const Node *kids[4];
    int i;
    kids[0] = e->a; kids[1] = e->b; kids[2] = e->c; kids[3] = e->d;
    for (i = 0; i < 4; i++)
        for (c = kids[i], k = c; k; k = k->next)
            if (fn(k, s)) return 1;
    return 0;
}

int mentions(const Node *e, const char *name)
{
    if (!e) return 0;
    if (e->kind == N_ID && !strcmp(e->text, name)) return 1;
    return any_child(e, mentions, name);
}

static int has_array_ref_s(const Node *e, const char *unused)
{
    (void)unused;
    return has_array_ref(e);
}

int has_array_ref(const Node *e)
{
    if (!e) return 0;
    if (e->kind == N_INDEX) return 1;
    return any_child(e, has_array_ref_s, NULL);
}

static int has_identifier_s(const Node *e, const char *unused)
{
    (void)unused;
    return has_identifier(e);
}

int has_identifier(const Node *e)
{
    if (!e) return 0;
    if (e->kind == N_ID) return 1;
    return any_child(e, has_identifier_s, NULL);
}

/* Is `e` an affine function of `var` with a compile-time constant slope?
 * On success *coeff holds the slope (0 if `e` does not depend on var). */
int linear_coeff(const Node *e, const char *var, long *coeff)
{
    long ca, cb, k;
    if (!e) { *coeff = 0; return 1; }
    switch (e->kind) {
    case N_INT:
        *coeff = 0;
        return 1;
    case N_ID:
        *coeff = !strcmp(e->text, var) ? 1 : 0;
        return 1;
    case N_UNOP:
        if (e->text[0] == '-') {
            if (!linear_coeff(e->a, var, &ca)) return 0;
            *coeff = -ca;
            return 1;
        }
        break;
    case N_BINOP:
        if (!strcmp(e->text, "+") || !strcmp(e->text, "-")) {
            if (!linear_coeff(e->a, var, &ca) || !linear_coeff(e->b, var, &cb)) return 0;
            *coeff = (e->text[0] == '+') ? ca + cb : ca - cb;
            return 1;
        }
        if (!strcmp(e->text, "*")) {
            if (!linear_coeff(e->a, var, &ca) || !linear_coeff(e->b, var, &cb)) return 0;
            if (ca == 0 && cb == 0) { *coeff = 0; return 1; }
            if (ca == 0 && const_eval(e->a, &k)) { *coeff = k * cb; return 1; }
            if (cb == 0 && const_eval(e->b, &k)) { *coeff = k * ca; return 1; }
            return 0;                    /* slope depends on a runtime value */
        }
        break;
    default:
        break;
    }
    if (mentions(e, var)) return 0;      /* non-affine use of var */
    *coeff = 0;
    return 1;
}

/* ================================================================== */
/* Canonical for-loop recognition                                      */
/* ================================================================== */

static int is_var(const Node *n, const char *v)
{
    return n && n->kind == N_ID && !strcmp(n->text, v);
}

int loop_info(const Node *f, LoopInfo *li)
{
    const Node *init = f->a, *cond = f->b, *upd = f->c;
    const char *var = NULL;
    char op[3] = "";
    long v;

    memset(li, 0, sizeof *li);

    if (init && init->kind == N_VAR_DECL) {
        var = init->text;
        li->start_known = init->a && const_eval(init->a, &li->start);
    } else if (init && init->kind == N_ASSIGN && !strcmp(init->text, "=") &&
               init->a->kind == N_ID) {
        var = init->a->text;
        li->start_known = const_eval(init->b, &li->start);
    }
    if (!var) return 0;
    strncpy(li->var, var, sizeof li->var - 1);

    if (upd) {
        if (upd->kind == N_INCDEC && is_var(upd->a, var)) {
            li->step = (upd->text[0] == '+') ? 1 : -1;
        } else if (upd->kind == N_ASSIGN && is_var(upd->a, var)) {
            if (!strcmp(upd->text, "+=") && const_eval(upd->b, &v)) li->step = v;
            else if (!strcmp(upd->text, "-=") && const_eval(upd->b, &v)) li->step = -v;
            else if (!strcmp(upd->text, "=") && upd->b->kind == N_BINOP &&
                     is_var(upd->b->a, var) && const_eval(upd->b->b, &v)) {
                if (!strcmp(upd->b->text, "+")) li->step = v;
                else if (!strcmp(upd->b->text, "-")) li->step = -v;
            }
        }
    }
    if (li->step == 0) return 0;
    li->canonical = 1;

    if (cond && cond->kind == N_BINOP) {
        if (is_var(cond->a, var)) {
            strncpy(op, cond->text, 2);
            li->bound_known = const_eval(cond->b, &li->bound);
        } else if (is_var(cond->b, var)) {
            const char *t = cond->text;      /* flip the comparison */
            if (!strcmp(t, "<")) strcpy(op, ">");
            else if (!strcmp(t, ">")) strcpy(op, "<");
            else if (!strcmp(t, "<=")) strcpy(op, ">=");
            else if (!strcmp(t, ">=")) strcpy(op, "<=");
            else strncpy(op, t, 2);
            li->bound_known = const_eval(cond->a, &li->bound);
        }
    }
    if (li->start_known && li->bound_known) {
        long s = li->start, b = li->bound, st = li->step, t = -1;
        if (!strcmp(op, "<")) { if (st > 0) t = (b > s) ? (b - s + st - 1) / st : 0; }
        else if (!strcmp(op, "<=")) { if (st > 0) t = (b >= s) ? (b - s) / st + 1 : 0; }
        else if (!strcmp(op, ">")) { if (st < 0) t = (s > b) ? (s - b + (-st) - 1) / (-st) : 0; }
        else if (!strcmp(op, ">=")) { if (st < 0) t = (s >= b) ? (s - b) / (-st) + 1 : 0; }
        else if (!strcmp(op, "!=")) {
            if (st == 1 && b >= s) t = b - s;
            else if (st == -1 && s >= b) t = s - b;
        }
        if (t >= 0) { li->trips = t; li->trips_known = 1; }
    }
    return 1;
}

/* ================================================================== */
/* Thread discovery + create/join event timeline                       */
/* ================================================================== */

typedef struct {
    HTContext *ctx;
    const char *func;
    Node *loops[8];
    int nloops;
    int event;
} DCtx;

static const char *base_name(const Node *n)
{
    while (n && (n->kind == N_INDEX || n->kind == N_ADDR)) n = n->a;
    return (n && n->kind == N_ID) ? n->text : "?";
}

static void d_expr(DCtx *d, Node *e);
static void d_stmt(DCtx *d, Node *s);

static void d_create(DCtx *d, Node *call)
{
    HTContext *ctx = d->ctx;
    ThreadSite *s;
    Node *worker = call->a->next->next;

    if (ctx->nsites >= MAX_SITES) {
        ht_error("Thread analysis error", call->line, "too many pthread_create sites (max %d)", MAX_SITES);
        return;
    }
    if (!call->sym || !call->sym->decl) return;      /* already reported by sema */

    s = &ctx->sites[ctx->nsites];
    memset(s, 0, sizeof *s);
    s->id = ctx->nsites;
    s->call = call;
    s->func = call->sym->decl;
    s->line = call->line;
    s->handle = base_name(call->a);
    s->in_func = d->func;
    s->ev = d->event++;
    s->ev_end = -1;
    s->instances = 1;
    (void)worker;

    if (d->nloops == 1) {
        Node *lp = d->loops[0];
        s->in_loop = 1;
        s->loop_node = lp;
        if (lp->kind != N_FOR || !loop_info(lp, &s->loop) || !s->loop.trips_known) {
            ht_error("Thread analysis error", call->line,
                     "pthread_create inside a loop needs a 'for' loop with constant bounds "
                     "so the number of threads is known at compile time");
            return;
        }
        s->instances = (int)s->loop.trips;
    } else if (d->nloops > 1) {
        ht_error("Thread analysis error", call->line,
                 "pthread_create inside nested loops is not supported");
        return;
    }
    ctx->nsites++;
}

static void d_join(DCtx *d, Node *call)
{
    HTContext *ctx = d->ctx;
    const char *h = base_name(call->a);
    int ev = d->event++, i;
    Node *lp = d->nloops ? d->loops[d->nloops - 1] : NULL;

    for (i = 0; i < ctx->nsites; i++) {
        ThreadSite *s = &ctx->sites[i];
        if (s->ev_end < 0 && s->ev < ev && !strcmp(s->handle, h)) {
            s->ev_end = ev;
            if (s->loop_node && s->loop_node == lp) s->serial = 1;
        }
    }
}

static void d_expr(DCtx *d, Node *e)
{
    Node *a;
    if (!e) return;
    if (e->kind == N_CALL) {
        LibKind k = lib_kind(e->text);
        for (a = e->a; a; a = a->next) d_expr(d, a);
        if (k == LIB_PTHREAD_CREATE) d_create(d, e);
        else if (k == LIB_PTHREAD_JOIN) d_join(d, e);
        return;
    }
    d_expr(d, e->a);
    d_expr(d, e->b);
    d_expr(d, e->c);
    d_expr(d, e->d);
}

static void d_list(DCtx *d, Node *l)
{
    for (; l; l = l->next) d_stmt(d, l);
}

static void d_stmt(DCtx *d, Node *s)
{
    if (!s) return;
    switch (s->kind) {
    case N_BLOCK:     d_list(d, s->a); break;
    case N_VAR_DECL:  d_expr(d, s->a); break;
    case N_EXPR_STMT: d_expr(d, s->a); break;
    case N_RETURN:    d_expr(d, s->a); break;
    case N_IF:
        d_expr(d, s->a);
        d_stmt(d, s->b);
        d_stmt(d, s->c);
        break;
    case N_FOR:
        if (s->a) { if (s->a->kind == N_VAR_DECL) d_stmt(d, s->a); else d_expr(d, s->a); }
        if (d->nloops < 8) d->loops[d->nloops++] = s;
        d_stmt(d, s->d);
        d_expr(d, s->b);
        d_expr(d, s->c);
        d->nloops--;
        break;
    case N_WHILE:
        if (d->nloops < 8) d->loops[d->nloops++] = s;
        d_expr(d, s->a);
        d_stmt(d, s->b);
        d->nloops--;
        break;
    default:
        break;
    }
}

void discover_threads(HTContext *ctx, Node *program)
{
    DCtx d;
    Node *n;
    int i, k;

    memset(&d, 0, sizeof d);
    d.ctx = ctx;
    for (n = program->a; n; n = n->next) {
        if (n->kind != N_FUNC) continue;
        d.func = n->text;
        d.nloops = 0;
        d_stmt(&d, n->b);
    }
    if (g_errors) return;

    /* expand sites into thread instances */
    ctx->nthreads = 0;
    for (i = 0; i < ctx->nsites; i++) {
        ThreadSite *s = &ctx->sites[i];
        s->first_thread = ctx->nthreads;
        for (k = 0; k < s->instances; k++) {
            Thread *t;
            if (ctx->nthreads >= MAX_THREADS) {
                ht_error("Thread analysis error", s->line,
                         "more than %d thread instances (limit of this tool)", MAX_THREADS);
                return;
            }
            t = &ctx->threads[ctx->nthreads];
            memset(t, 0, sizeof *t);
            t->id = ctx->nthreads++;
            t->site = i;
            t->instance = k;
            t->func = s->func;
            t->line = s->line;
            t->ev_start = s->ev;
            t->ev_end = (s->ev_end >= 0) ? s->ev_end : EV_INF;
            if (s->instances > 1)
                snprintf(t->name, sizeof t->name, "%s#%d", s->func->text, k);
            else
                snprintf(t->name, sizeof t->name, "%s", s->func->text);
        }
    }
}

/* Can threads a and b execute at the same time? */
int threads_overlap(const HTContext *ctx, int a, int b)
{
    const Thread *A = &ctx->threads[a], *B = &ctx->threads[b];
    if (A->site == B->site && ctx->sites[A->site].serial) return 0;
    return A->ev_start < B->ev_end && B->ev_start < A->ev_end;
}

/* Run discovery, profiling, contention scoring and mapping on an already
 * semantically-analysed program.  Used by main and by the optimizer. */
int run_analysis(HTContext *ctx, Node *program, const Topology *topo, int spread)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->topo = *topo;
    discover_threads(ctx, program);
    if (g_errors) return 0;
    profile_threads(ctx);
    build_contention(ctx);
    build_mappings(ctx, spread);
    return 1;
}
