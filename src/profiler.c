/* profiler.c - static resource profile of every thread function.
 *
 * Walks the AST of a worker (following calls to user functions) and counts,
 * weighted by the product of enclosing loop trip counts:
 *   - integer ops, FP add/mul ops, "heavy" ops (div, %, sqrt, sin, ...)
 *   - array loads/stores, branches, I/O calls, lock operations
 * Every array access is classified (sequential / strided / irregular /
 * loop-invariant) relative to the innermost loop variable and converted into
 * an estimated number of cache lines that must come from memory.
 * Scalars are assumed to live in registers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ht.h"
#include "astutil.h"
#include "common.h"

#define MODE_R 1
#define MODE_W 2

#define MAX_SEEN 24
/* One frame per enclosing loop. `seen` holds the array elements already touched in
 * the current iteration: touching the same element again is a cache hit. */
typedef struct {
    const char *var;
    long step;
    const Node *body;
    double trips;                 /* iterations of this loop (assumed when unknown) */
    int nseen;
    struct { const Symbol *sym; char key[88]; } seen[MAX_SEEN];
    /* accesses to one array whose subscripts differ only by a constant (a[i], a[i+1], ...) */
    int ngrp;
    struct { const Symbol *sym; char base[88]; int u; } grp[16];
} LoopFrame;

typedef struct {
    HTContext *ctx;
    Profile *p;
    LoopFrame st[16];
    int n;
    Node *calls[8];
    int ncalls;
} PCtx;

static void pf_expr(PCtx *c, Node *e, double m, int mode);
static void pf_stmt(PCtx *c, Node *s, double m);

/* ---------- small helpers ---------- */

static int add_name(const char **list, int *n, const char *name)
{
    int i;
    for (i = 0; i < *n; i++)
        if (!strcmp(list[i], name)) return 0;
    if (*n < MAX_NAMES) list[(*n)++] = name;
    return 1;
}

static ArrayUse *get_array(Profile *p, Symbol *s)
{
    int i;
    for (i = 0; i < p->narrays; i++)
        if (p->arrays[i].sym == s) return &p->arrays[i];
    if (p->narrays >= MAX_ARRAYS) return NULL;
    memset(&p->arrays[p->narrays], 0, sizeof(ArrayUse));
    p->arrays[p->narrays].sym = s;
    return &p->arrays[p->narrays++];
}

static void count_arith(Profile *p, const char *op, int fp, double m)
{
    if (!strcmp(op, "/") || !strcmp(op, "%")) p->heavy_ops += m;
    else if (fp) p->fp_ops += m;
    else p->int_ops += m;
}


/* Is `name` assigned inside `n` by something other than an affine update
 * (x++, x += c, x = x + c)?  Such a variable is data dependent, e.g.
 * x = next[x]  or  x = (x * 17 + 3) % M. */
static int assigned_nonaffine(const Node *n, const char *name)
{
    long k;
    for (; n; n = n->next) {
        if (n->kind == N_ASSIGN && n->a->kind == N_ID && !strcmp(n->a->text, name)) {
            int affine = 0;
            if ((!strcmp(n->text, "+=") || !strcmp(n->text, "-=")) && const_eval(n->b, &k))
                affine = 1;
            else if (!strcmp(n->text, "=") && n->b->kind == N_BINOP &&
                     (!strcmp(n->b->text, "+") || !strcmp(n->b->text, "-")) &&
                     n->b->a->kind == N_ID && !strcmp(n->b->a->text, name) &&
                     const_eval(n->b->b, &k))
                affine = 1;
            if (!affine) return 1;
        }
        if (assigned_nonaffine(n->a, name) || assigned_nonaffine(n->b, name) ||
            assigned_nonaffine(n->c, name) || assigned_nonaffine(n->d, name))
            return 1;
    }
    return 0;
}

/* Does expression `e` use a variable that the loop reassigns non-affinely? */
static int data_dependent(const Node *e, const LoopFrame *f)
{
    if (!e) return 0;
    if (e->kind == N_ID && (!f->var || strcmp(e->text, f->var)) &&
        assigned_nonaffine(f->body, e->text))
        return 1;
    return data_dependent(e->a, f) || data_dependent(e->b, f) ||
           data_dependent(e->c, f) || data_dependent(e->d, f);
}


/* ---- accesses that differ only by a constant offset share cache lines ---- */

static const Node *strip_const(const Node *e)
{
    while (e->kind == N_BINOP && e->text[1] == 0 && (e->text[0] == '+' || e->text[0] == '-')) {
        if (e->b->kind == N_INT) e = e->a;
        else if (e->text[0] == '+' && e->a->kind == N_INT) e = e->b;
        else break;
    }
    return e;
}

static void base_key(const Node *ref, char *buf, size_t n)
{
    const Node *idx[2], *b = ref;
    int k = 0, i;
    size_t len;
    char t[96];
    while (b->kind == N_INDEX && k < 2) { idx[k++] = b->b; b = b->a; }
    snprintf(buf, n, "%s", b->kind == N_ID ? b->text : "?");
    for (i = k - 1; i >= 0; i--) {
        len = strlen(buf);
        ast_key(strip_const(idx[i]), t, sizeof t);
        snprintf(buf + len, n - len, "[%s]", t);
    }
}

static void note_group(LoopFrame *f, const Node *ref, char (*fulls)[96], int *nfull, const Symbol *sym)
{
    char base[88], full[96];
    int i, g;
    base_key(ref, base, sizeof base);
    ast_key(ref, full, sizeof full);
    for (i = 0; i < *nfull; i++) if (!strcmp(fulls[i], full)) return;     /* already counted */
    if (*nfull >= 64) return;
    snprintf(fulls[(*nfull)++], 96, "%s", full);
    for (g = 0; g < f->ngrp; g++)
        if (f->grp[g].sym == sym && !strcmp(f->grp[g].base, base)) { f->grp[g].u++; return; }
    if (f->ngrp < 16) {
        f->grp[f->ngrp].sym = sym;
        snprintf(f->grp[f->ngrp].base, sizeof f->grp[0].base, "%s", base);
        f->grp[f->ngrp].u = 1;
        f->ngrp++;
    }
}

static void scan_groups(LoopFrame *f, const Node *n, char (*fulls)[96], int *nfull)
{
    for (; n; n = n->next) {
        if (n->kind == N_INDEX) {
            const Node *b = n;
            while (b->kind == N_INDEX) b = b->a;
            if (b->kind == N_ID && b->sym) note_group(f, n, fulls, nfull, b->sym);
            continue;
        }
        scan_groups(f, n->a, fulls, nfull);
        scan_groups(f, n->b, fulls, nfull);
        scan_groups(f, n->c, fulls, nfull);
        scan_groups(f, n->d, fulls, nfull);
    }
}

static int group_u(const LoopFrame *f, const Symbol *sym, const Node *ref)
{
    char base[88];
    int g;
    base_key(ref, base, sizeof base);
    for (g = 0; g < f->ngrp; g++)
        if (f->grp[g].sym == sym && !strcmp(f->grp[g].base, base)) return f->grp[g].u > 0 ? f->grp[g].u : 1;
    return 1;
}

/* ---------- array access classification ---------- */

enum { PAT_SEQ = 0, PAT_STRIDED = 1, PAT_IRREGULAR = 2, PAT_INVARIANT = 3 };


/* An access that does not change in the innermost loop still has to be fetched once per
 * iteration of the loop it DOES vary with.  Find that loop and classify against it;
 * *scale is 1 / (iterations of the loops inside it). */
static void outer_level(PCtx *c, Symbol *s, Node *i0, Node *i1, int *cls, double *cost, double *scale)
{
    long esz = s->elem_size;
    double inner = c->st[c->n - 1].trips;
    int k;
    for (k = c->n - 2; k >= 0; k--) {
        LoopFrame *f = &c->st[k];
        long c0 = 0, c1 = 0, stride;
        if (!f->var) {
            if (has_identifier(i0) || (i1 && has_identifier(i1))) {
                *cls = PAT_SEQ; *cost = (double)esz / 64.0; *scale = 1.0 / inner;
                return;
            }
            inner *= f->trips;
            continue;
        }
        if (!linear_coeff(i0, f->var, &c0) || (i1 && !linear_coeff(i1, f->var, &c1))) {
            *cls = PAT_STRIDED; *cost = 1.0; *scale = 1.0 / inner;
            return;
        }
        stride = i1 ? c0 * s->dims[1] + c1 : c0;
        if (stride < 0) stride = -stride;
        stride *= (f->step < 0 ? -f->step : f->step);
        if (stride == 0) { inner *= f->trips; continue; }
        if (stride == 1) { *cls = PAT_SEQ; *cost = (double)esz / 64.0; }
        else {
            double bytes = (double)stride * (double)esz;
            *cls = PAT_STRIDED;
            *cost = bytes >= 64.0 ? 1.0 : bytes / 64.0;
        }
        *scale = 1.0 / inner;
        return;
    }
    *cls = PAT_INVARIANT; *cost = 0.0; *scale = 1.0;       /* the same element in every loop */
}

static void classify(PCtx *c, Symbol *s, Node *i0, Node *i1, const Node *ref, int *cls, double *cost, double *scale)
{
    long esz = s->elem_size, c0 = 0, c1 = 0, stride;
    const char *var;
    long step;
    int ok0, ok1;

    *scale = 1.0;
    if (has_array_ref(i0) || (i1 && has_array_ref(i1))) {   /* a[b[i]] */
        *cls = PAT_IRREGULAR; *cost = 1.0; return;
    }
    if (c->n == 0) { *cls = PAT_INVARIANT; *cost = 0.0; return; }
    var = c->st[c->n - 1].var;
    step = c->st[c->n - 1].step;
    if (data_dependent(i0, &c->st[c->n - 1]) ||
        (i1 && data_dependent(i1, &c->st[c->n - 1]))) {    /* x = next[x] */
        *cls = PAT_IRREGULAR; *cost = 1.0; return;
    }
    if (!var) {                         /* while loop: induction variable unknown */
        if (has_identifier(i0) || (i1 && has_identifier(i1))) {
            *cls = PAT_SEQ; *cost = (double)esz / 64.0;
        } else {
            *cls = PAT_INVARIANT; *cost = 0.0;
        }
        return;
    }
    ok0 = linear_coeff(i0, var, &c0);
    ok1 = i1 ? linear_coeff(i1, var, &c1) : 1;
    if (!ok0 || !ok1) { *cls = PAT_STRIDED; *cost = 1.0; return; }
    stride = i1 ? c0 * s->dims[1] + c1 : c0;
    if (stride < 0) stride = -stride;
    stride *= (step < 0 ? -step : step);
    if (stride == 0) { outer_level(c, s, i0, i1, cls, cost, scale); return; }
    if (stride == 1) { *cls = PAT_SEQ; *cost = (double)esz / 64.0; return; }
    *cls = PAT_STRIDED;
    {
        double bytes = (double)stride * (double)esz;
        int u = (step > 1 || step < -1) ? group_u(&c->st[c->n - 1], s, ref) : 1;    /* a[i], a[i+1].. share lines */
        double per = bytes / (64.0 * (double)u);
        *cost = per >= 1.0 ? 1.0 : per;
    }
}

static void array_access(PCtx *c, Node *e, double m, int mode)
{
    Node *idx[2], *base = e, *i0, *i1;
    int n = 0, cls;
    double cost, scale = 1.0;
    ArrayUse *au;
    Profile *p = c->p;

    while (base->kind == N_INDEX && n < 2) { idx[n++] = base->b; base = base->a; }
    i0 = (n == 2) ? idx[1] : idx[0];
    i1 = (n == 2) ? idx[0] : NULL;

    pf_expr(c, i0, m, MODE_R);
    if (i1) pf_expr(c, i1, m, MODE_R);

    if (base->kind != N_ID || !base->sym) return;
    if (mode & MODE_R) p->loads += m;
    if (mode & MODE_W) p->stores += m;

    au = get_array(p, base->sym);
    if (!au) return;
    classify(c, base->sym, i0, i1, e, &cls, &cost, &scale);
    if (c->n > 0) {                                  /* same element again in this iteration? */
        LoopFrame *f = &c->st[c->n - 1];
        char key[88];
        int k, dup = 0;
        ast_key(e, key, sizeof key);
        for (k = 0; k < f->nseen; k++)
            if (f->seen[k].sym == base->sym && !strcmp(f->seen[k].key, key)) { dup = 1; break; }
        if (dup) cost = 0.0;
        else if (f->nseen < MAX_SEEN) {
            f->seen[f->nseen].sym = base->sym;
            strcpy(f->seen[f->nseen].key, key);
            f->nseen++;
        }
    }
    if (mode & MODE_R) au->reads += m;
    if (mode & MODE_W) au->writes += m;
    au->lines += m * scale * cost;
    switch (cls) {
    case PAT_SEQ:       au->seq += m * scale;       p->seq_acc += m * scale; break;
    case PAT_STRIDED:   au->strided += m * scale;   p->strided_acc += m * scale; break;
    case PAT_IRREGULAR: au->irregular += m * scale; p->irregular_acc += m * scale; break;
    default: break;
    }
}

/* ---------- expressions ---------- */

static const char *lock_name(const Node *arg)
{
    while (arg && (arg->kind == N_ADDR || arg->kind == N_INDEX)) arg = arg->a;
    return (arg && arg->kind == N_ID) ? arg->text : "?";
}

static void pf_call(PCtx *c, Node *e, double m)
{
    Profile *p = c->p;
    LibKind k = lib_kind(e->text);
    Node *a;
    int i;

    for (a = e->a; a; a = a->next) pf_expr(c, a, m, MODE_R);

    switch (k) {
    case LIB_MATH: case LIB_MISC: p->heavy_ops += 10.0 * m; break;
    case LIB_IO:                  p->io_ops += 50.0 * m; break;
    case LIB_MUTEX_LOCK: case LIB_MUTEX_UNLOCK:
        p->lock_ops += 2.0 * m;
        if (e->a) {
            const char *nm = lock_name(e->a);
            int dup = 0;
            for (i = 0; i < p->nlocks; i++) if (!strcmp(p->locks[i], nm)) dup = 1;
            if (!dup && p->nlocks < 8) p->locks[p->nlocks++] = nm;
        }
        break;
    case LIB_PTHREAD_CREATE: case LIB_PTHREAD_JOIN:
        break;
    case LIB_NONE: {
        Node *callee = (e->sym && e->sym->decl) ? e->sym->decl : NULL;
        int recursive = 0;
        if (!callee) break;
        for (i = 0; i < c->ncalls; i++) if (c->calls[i] == callee) recursive = 1;
        p->int_ops += 2.0 * m;                          /* call/return overhead */
        if (!recursive && c->ncalls < 8) {
            c->calls[c->ncalls++] = callee;
            pf_stmt(c, callee->b, m);
            c->ncalls--;
        }
        break;
    }
    }
}

static void pf_expr(PCtx *c, Node *e, double m, int mode)
{
    Profile *p = c->p;
    const char *o;

    if (!e) return;
    switch (e->kind) {
    case N_INT: case N_FLOAT: case N_STR: case N_NULL: case N_ADDR:
        return;
    case N_ID:
        if (e->sym && e->sym->kind == S_VAR && e->sym->is_global && e->sym->ndims == 0 &&
            e->sym->type != T_PTHREAD && e->sym->type != T_MUTEX) {
            if (mode & MODE_R) add_name(p->gread, &p->ngr, e->sym->name);
            if (mode & MODE_W) add_name(p->gwritten, &p->ngw, e->sym->name);
        }
        return;
    case N_UNOP:
        if (e->text[0] == '-' && is_fp_type(e->etype)) p->fp_ops += m;
        else p->int_ops += m;
        pf_expr(c, e->a, m, MODE_R);
        return;
    case N_BINOP:
        o = e->text;
        if (!strcmp(o, "&&") || !strcmp(o, "||")) {
            p->int_ops += m; p->branches += m;
        } else if (!strcmp(o, "<") || !strcmp(o, ">") || !strcmp(o, "<=") ||
                   !strcmp(o, ">=") || !strcmp(o, "==") || !strcmp(o, "!=")) {
            if (is_fp_type(e->a->etype) || is_fp_type(e->b->etype)) p->fp_ops += m;
            else p->int_ops += m;
        } else {
            count_arith(p, o, is_fp_type(e->etype), m);
        }
        pf_expr(c, e->a, m, MODE_R);
        pf_expr(c, e->b, m, MODE_R);
        return;
    case N_INCDEC:
        if (is_fp_type(e->etype)) p->fp_ops += m; else p->int_ops += m;
        pf_expr(c, e->a, m, MODE_R | MODE_W);
        return;
    case N_ASSIGN: {
        int compound = strcmp(e->text, "=") != 0;
        pf_expr(c, e->b, m, MODE_R);
        if (compound) {
            char op[2] = { e->text[0], 0 };
            count_arith(p, op, is_fp_type(e->a->etype) || is_fp_type(e->b->etype), m);
        }
        pf_expr(c, e->a, m, compound ? (MODE_R | MODE_W) : MODE_W);
        return;
    }
    case N_INDEX:
        array_access(c, e, m, mode);
        return;
    case N_CALL:
        pf_call(c, e, m);
        return;
    default:
        return;
    }
}

/* ---------- statements ---------- */

static void push_loop(PCtx *c, const char *var, long step, const Node *body, double trips)
{
    if (c->n < 16) {
        c->st[c->n].var = var;
        c->st[c->n].step = step;
        c->st[c->n].body = body;
        c->st[c->n].trips = trips > 0 ? trips : 1;
        c->st[c->n].nseen = 0;
        c->st[c->n].ngrp = 0;
        {
            char (*fulls)[96] = malloc(64 * 96);
            int nfull = 0;
            if (fulls) { scan_groups(&c->st[c->n], body, fulls, &nfull); free(fulls); }
        }
        c->n++;
    }
}

static void pf_stmt(PCtx *c, Node *s, double m)
{
    Profile *p = c->p;
    Node *k;

    for (; s; s = s->next) {
        switch (s->kind) {
        case N_BLOCK:
            pf_stmt(c, s->a, m);
            break;
        case N_VAR_DECL:
            if (s->a) pf_expr(c, s->a, m, MODE_R);
            break;
        case N_EXPR_STMT:
            pf_expr(c, s->a, m, MODE_R);
            break;
        case N_RETURN:
            pf_expr(c, s->a, m, MODE_R);
            break;
        case N_IF:
            pf_expr(c, s->a, m, MODE_R);
            p->branches += m;
            pf_stmt(c, s->b, m * 0.5);
            if (s->c) pf_stmt(c, s->c, m * 0.5);
            break;
        case N_FOR: {
            LoopInfo li;
            int canon = loop_info(s, &li);
            double trips = (canon && li.trips_known) ? (double)li.trips : (double)DEFAULT_TRIPS;
            double bm;
            if (!(canon && li.trips_known)) p->assumed_trips++;
            if (s->a) {
                if (s->a->kind == N_VAR_DECL) { if (s->a->a) pf_expr(c, s->a->a, m, MODE_R); }
                else pf_expr(c, s->a, m, MODE_R);
            }
            push_loop(c, canon ? li.var : NULL, canon ? li.step : 1, s->d, trips);
            bm = m * trips;
            pf_expr(c, s->b, bm, MODE_R);
            p->branches += bm;
            pf_expr(c, s->c, bm, MODE_R);
            pf_stmt(c, s->d, bm);
            c->n--;
            break;
        }
        case N_WHILE: {
            double bm = m * (double)DEFAULT_TRIPS;
            p->assumed_trips++;
            push_loop(c, NULL, 1, s->b, (double)DEFAULT_TRIPS);
            pf_expr(c, s->a, bm, MODE_R);
            p->branches += bm;
            pf_stmt(c, s->b, bm);
            c->n--;
            break;
        }
        default:
            break;
        }
        (void)k;
    }
}

/* ---------- finalisation ---------- */

static double fmin2(double a, double b) { return a < b ? a : b; }

static void finalize(HTContext *ctx, Profile *p)
{
    int i;
    double l2 = (double)ctx->topo.l2, total, irr_share, sum;

    p->total_ops = p->int_ops + p->fp_ops + p->heavy_ops + p->loads + p->stores +
                   p->branches + p->io_ops + p->lock_ops;
    p->dram_lines = 0;
    p->ws_bytes = 0;
    for (i = 0; i < p->narrays; i++) {
        ArrayUse *a = &p->arrays[i];
        double bytes = (double)a->sym->bytes;
        double fp = a->lines * 64.0;
        if (fp < 64.0) fp = 64.0;
        if (fp > bytes) fp = bytes;
        a->footprint = fp;
        /* arrays that fit in (half of) L2 only cost compulsory misses */
        a->dram_lines = (bytes <= l2 / 2.0) ? fmin2(a->lines, bytes / 64.0) : a->lines;
        p->dram_lines += a->dram_lines;
        p->ws_bytes += fp;
    }

    total = p->total_ops;
    if (total <= 0) {
        strcpy(p->klass, "idle");
        return;
    }
    p->n_int   = p->int_ops / total;
    p->n_fp    = p->fp_ops / total;
    p->n_heavy = p->heavy_ops / total;
    p->n_ldst  = (p->loads + p->stores) / total;
    p->n_bw    = fmin2(1.0, (p->dram_lines * 64.0 / total) / BW_REF);

    sum = p->seq_acc + p->strided_acc + p->irregular_acc;
    irr_share = sum > 0 ? p->irregular_acc / sum : 0.0;

    if (irr_share > 0.5 && p->n_bw > 0.2)  strcpy(p->klass, "latency-bound (irregular access)");
    else if (p->n_bw >= 0.3)               strcpy(p->klass, "memory-bound");
    else if (p->n_heavy >= 0.10)           strcpy(p->klass, "compute-bound (div/sqrt/math)");
    else if (p->n_fp >= 0.15)              strcpy(p->klass, "compute-bound (floating point)");
    else if (p->n_ldst >= 0.35)            strcpy(p->klass, "cache-resident (load/store heavy)");
    else                                   strcpy(p->klass, "compute-bound (integer)");
}

void profile_threads(HTContext *ctx)
{
    int i, j;
    for (i = 0; i < ctx->nthreads; i++) {
        Thread *t = &ctx->threads[i];
        int done = 0;
        /* instances of the same worker share one profile */
        for (j = 0; j < i; j++) {
            if (ctx->threads[j].func == t->func) {
                t->prof = ctx->threads[j].prof;
                done = 1;
                break;
            }
        }
        if (done) continue;
        {
            PCtx c;
            memset(&c, 0, sizeof c);
            c.ctx = ctx;
            c.p = &t->prof;
            c.calls[c.ncalls++] = t->func;
            pf_stmt(&c, t->func->b, 1.0);
            finalize(ctx, &t->prof);
            ctx->assumed_trips_total += t->prof.assumed_trips;
        }
    }
}

/* Profile one function body (used for main and for sequential programs). */
void profile_function(HTContext *ctx, Node *func, Profile *out)
{
    PCtx c;
    memset(&c, 0, sizeof c);
    memset(out, 0, sizeof *out);
    c.ctx = ctx;
    c.p = out;
    c.calls[c.ncalls++] = func;
    pf_stmt(&c, func->b, 1.0);
    finalize(ctx, out);
}
