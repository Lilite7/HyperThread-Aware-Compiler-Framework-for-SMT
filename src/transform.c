/* transform.c - the transformation registry.
 *
 *   interchange  swap two perfectly nested loops          (locality)
 *   fusion       merge two adjacent loops over one range   (reuse)
 *   promotion    keep a loop-invariant array element in a register
 *                (removes loads/stores from the loop)
 *
 * Each transformation has
 *   find   enumerate opportunities in a program and decide their legality
 *          (dependence analysis in depend.c / the alias rules below);
 *          opportunities that cannot be proven safe are still reported,
 *          together with the reason, so nothing is silently skipped
 *   apply  re-locate an opportunity by its key and rewrite the AST
 *
 * `find` is also used by `apply` (with legality checking switched off) so that a
 * candidate chosen on one copy of the program can be applied to another copy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "opt.h"
#include "depend.h"
#include "astutil.h"
#include "common.h"

/* ================================================================== */
/* Generic AST walking                                                 */
/* ================================================================== */

typedef struct {
    Node *program;
    const char *func;
    Node *funcnode;
    void *user;
    void (*visit)(void *user, Node *stmt, Node *funcnode);
} Walk;

static void walk_list(Walk *w, Node *l);

static void walk_stmt(Walk *w, Node *s)
{
    w->visit(w->user, s, w->funcnode);
    switch (s->kind) {
    case N_BLOCK: walk_list(w, s->a); break;
    case N_IF:    if (s->b) walk_list(w, s->b); if (s->c) walk_list(w, s->c); break;
    case N_FOR:   if (s->d) walk_list(w, s->d); break;
    case N_WHILE: if (s->b) walk_list(w, s->b); break;
    default: break;
    }
}

static void walk_list(Walk *w, Node *l)
{
    for (; l; l = l->next) walk_stmt(w, l);
}

static void walk_program(Node *program, void *user,
                         void (*visit)(void *, Node *, Node *))
{
    Walk w;
    Node *f;
    memset(&w, 0, sizeof w);
    w.program = program;
    w.user = user;
    w.visit = visit;
    for (f = program->a; f; f = f->next) {
        if (f->kind != N_FUNC) continue;
        w.func = f->text;
        w.funcnode = f;
        walk_list(&w, f->b);
    }
}

/* statements of a loop body as a list head */
static Node *body_list(Node *loop)
{
    Node *b = loop->d;
    return (b && b->kind == N_BLOCK) ? b->a : b;
}

static int contains_loop(Node *l)
{
    for (; l; l = l->next) {
        if (l->kind == N_FOR || l->kind == N_WHILE) return 1;
        if (contains_loop(l->a) || contains_loop(l->b) || contains_loop(l->c) || contains_loop(l->d))
            return 1;
    }
    return 0;
}

/* ================================================================== */
/* Call graph and shared-memory policy                                  */
/* ================================================================== */

/* does `func` (or anything it calls) read / write global array `A`? */
static void array_access(Node *program, Node *func, const Symbol *A, int *rd, int *wr, int depth);

static void access_in(Node *program, Node *n, const Symbol *A, int *rd, int *wr, int depth, int lhs)
{
    for (; n; n = n->next) {
        if (n->kind == N_INDEX) {
            Node *base = n;
            while (base->kind == N_INDEX) base = base->a;
            if (base->kind == N_ID && base->sym == A) { if (lhs) *wr = 1; else *rd = 1; }
        }
        if (n->kind == N_ASSIGN) {
            access_in(program, n->a, A, rd, wr, depth, 1);
            if (strcmp(n->text, "=") != 0) access_in(program, n->a, A, rd, wr, depth, 0);
            access_in(program, n->b, A, rd, wr, depth, 0);
            continue;
        }
        if (n->kind == N_INCDEC) {
            access_in(program, n->a, A, rd, wr, depth, 1);
            access_in(program, n->a, A, rd, wr, depth, 0);
            continue;
        }
        if (n->kind == N_CALL && n->sym && n->sym->decl && depth < 8)
            array_access(program, n->sym->decl, A, rd, wr, depth + 1);
        access_in(program, n->a, A, rd, wr, depth, 0);
        access_in(program, n->b, A, rd, wr, depth, 0);
        access_in(program, n->c, A, rd, wr, depth, 0);
        access_in(program, n->d, A, rd, wr, depth, 0);
    }
}

static void array_access(Node *program, Node *func, const Symbol *A, int *rd, int *wr, int depth)
{
    access_in(program, func->b, A, rd, wr, depth, 0);
}

static int reaches(Node *from, Node *target, int depth);

static int reaches_in(Node *n, Node *target, int depth)
{
    for (; n; n = n->next) {
        if (n->kind == N_CALL && n->sym && n->sym->decl && depth < 8 &&
            reaches(n->sym->decl, target, depth + 1))
            return 1;
        if (reaches_in(n->a, target, depth) || reaches_in(n->b, target, depth) ||
            reaches_in(n->c, target, depth) || reaches_in(n->d, target, depth))
            return 1;
    }
    return 0;
}

static int reaches(Node *from, Node *target, int depth)
{
    if (from == target) return 1;
    return reaches_in(from->b, target, depth);
}

/* May another thread touch global array A while `f` runs its loop?
 * Returns 0 and a reason if shared-memory rules forbid the change. */
static int shared_ok(Node *program, const HTContext *ctx, Node *f, const Symbol *A,
                     int writes, char *why, size_t wn)
{
    int s, conc = 0, f_wr = 0, f_rd = 0;
    if (!ctx || ctx->nthreads == 0 || !A || !A->is_global) return 1;
    array_access(program, f, A, &f_rd, &f_wr, 0);
    for (s = 0; s < ctx->nsites; s++)
        if (reaches(ctx->sites[s].func, f, 0)) conc += ctx->sites[s].instances;
    for (s = 0; s < ctx->nsites; s++) {
        int rd = 0, wr = 0;
        if (reaches(ctx->sites[s].func, f, 0)) continue;
        array_access(program, ctx->sites[s].func, A, &rd, &wr, 0);
        if (writes ? (rd || wr) : wr) {
            snprintf(why, wn, "global array '%s' is also %s by thread function '%s'; keeping it in a "
                              "register could hide updates from (or overwrite those of) that thread",
                     A->name, wr ? "written" : "read", ctx->sites[s].func->text);
            return 0;
        }
    }
    if (conc >= 2 && (writes || f_wr)) {
        snprintf(why, wn, "'%s' runs in %d threads at once and they share global array '%s'",
                 f->text, conc, A->name);
        return 0;
    }
    return 1;
}

/* ================================================================== */
/* Common helpers for finders                                          */
/* ================================================================== */

static int add_cand(Cand *out, int *n, int max, int tr, const char *func, int line,
                    const char *key, const char *desc)
{
    Cand *c;
    if (*n >= max) return 0;
    c = &out[*n];
    memset(c, 0, sizeof *c);
    c->tr = tr;
    c->proposal = -1;
    snprintf(c->func, sizeof c->func, "%s", func);
    c->line = line;
    snprintf(c->key, sizeof c->key, "%s", key);
    snprintf(c->desc, sizeof c->desc, "%s", desc);
    (*n)++;
    return 1;
}

static int canon_loop(Node *f, LoopInfo *li)
{
    return f->kind == N_FOR && loop_info(f, li) && li->start_known && li->trips_known && li->trips >= 1;
}

/* ================================================================== */
/* Stable region ids                                                   */
/* ================================================================== */

static int ord_walk(Node *l, Node *target, int *counter)
{
    for (; l; l = l->next) {
        int r;
        if (l->kind == N_FOR || l->kind == N_WHILE) {
            (*counter)++;
            if (l == target) return *counter;
        }
        switch (l->kind) {
        case N_BLOCK: r = ord_walk(l->a, target, counter); break;
        case N_IF:    r = ord_walk(l->b, target, counter); if (!r) r = ord_walk(l->c, target, counter); break;
        case N_FOR:   r = ord_walk(l->d, target, counter); break;
        case N_WHILE: r = ord_walk(l->b, target, counter); break;
        default:      r = 0;
        }
        if (r) return r;
    }
    return 0;
}

/* Position of `loop` among the loops of `func`, in source order, starting at 1.  Unlike a line
 * number it does not change when unrelated lines are added or removed. */
int loop_ordinal(Node *func, Node *loop)
{
    int counter = 0;
    return ord_walk(func->b, loop, &counter);
}

void loop_region_id(Node *func, Node *loop, char *out, size_t n)
{
    snprintf(out, n, "%s/L%d", func->text, loop_ordinal(func, loop));
}

static void set_region(Cand *c, Node *fn, Node *loop)
{
    loop_region_id(fn, loop, c->region, sizeof c->region);
}

/* ================================================================== */
/* Finder state shared by the three transformations                    */
/* ================================================================== */

typedef struct {
    Cand *out;
    int n, max, check, tr;
    Node **n1, **n2;                /* node pointers for apply (optional)           */
    const HTContext *ctx;
    Node *program;
} Find;

/* ================================================================== */
/* 1. Loop interchange                                                 */
/* ================================================================== */

static Node *sole_for(Node *body)
{
    if (!body) return NULL;
    if (body->kind == N_FOR && !body->next) return body;
    if (body->kind == N_BLOCK && body->a && !body->a->next && body->a->kind == N_FOR) return body->a;
    return NULL;
}

/* lines of cache touched per iteration of loop variable v for one reference */
static double ref_cost(const Ref *r, int v, long esz, const long *dims)
{
    long stride = 0;
    int d;
    double b;
    for (d = 0; d < r->ndims; d++) {
        long mult = (r->ndims == 2 && d == 0) ? dims[1] : 1;
        if (r->sub[d].kind == SUB_OTHER) return 1.0;      /* unknown: assume the worst */
        if (r->sub[d].kind == SUB_SYM) continue;          /* constant inside the nest  */
        stride += r->sub[d].coef[v] * mult;
    }
    if (stride < 0) stride = -stride;
    if (stride == 0) return 0.0;
    b = (double)stride * (double)esz;
    return b >= 64.0 ? 1.0 : b / 64.0;
}

static void interchange_visit(void *u, Node *s, Node *fn)
{
    Find *F = u;
    Node *chain[MAXV], *cur;
    LoopInfo li[MAXV];
    const char *vars[MAXV];
    int n = 0, i, ok = 1;
    char key[128], desc[200];
    Cand *c;

    if (s->kind != N_FOR || F->n >= F->max) return;
    for (cur = s; n < MAXV && cur; cur = sole_for(cur->d)) chain[n++] = cur;
    if (n < 2) return;                                 /* needs a perfectly nested inner loop */

    for (i = 0; i < n; i++) {
        if (!canon_loop(chain[i], &li[i])) ok = 0;
        else vars[i] = li[i].var;
    }
    snprintf(key, sizeof key, "interchange:%s:%d", fn->text, s->line);
    snprintf(desc, sizeof desc, "swap the loops over '%s' and '%s' (lines %d and %d) in %s",
             ok ? li[0].var : "?", ok ? li[1].var : "?", chain[0]->line, chain[1]->line, fn->text);
    c = &F->out[F->n];
    if (F->n1) { F->n1[F->n] = chain[0]; F->n2[F->n] = chain[1]; }
    add_cand(F->out, &F->n, F->max, F->tr, fn->text, s->line, key, desc);
    set_region(c, fn, chain[0]);
    if (!F->check) return;

    if (!ok) {
        c->safety = SAFE_NO;
        snprintf(c->why, sizeof c->why, "loop bounds are not compile-time constants, so the nest is not analysed");
        return;
    }
    for (i = 0; i < n; i++) {                          /* variables distinct and never assigned in the nest */
        int j;
        for (j = i + 1; j < n; j++) if (!strcmp(vars[i], vars[j])) ok = 0;
        if (ast_assigns(chain[i]->d, vars[i])) ok = 0;
    }
    if (!ok) {
        c->safety = SAFE_NO;
        snprintf(c->why, sizeof c->why, "a loop variable is reused or modified inside the nest");
        return;
    }
    {
        Region R;
        int perm[MAXV], fp_reorder = 0;
        region_scan_stmt(&R, chain[0], vars, n);
        for (i = 0; i < n; i++) perm[i] = i;
        perm[0] = 1; perm[1] = 0;
        if (!perm_legal(&R, n, perm, 1, c->why, sizeof c->why, c->evidence, sizeof c->evidence, &fp_reorder)) {
            c->safety = SAFE_NO;
            return;
        }
        c->safety = fp_reorder ? SAFE_FP : SAFE_EXACT;
        if (n == 2) {                                  /* does the new innermost loop touch fewer lines? */
            double before = 0, after = 0;
            for (i = 0; i < R.nrefs; i++) {
                long esz = R.refs[i].sym->elem_size;
                before += ref_cost(&R.refs[i], 1, esz, R.refs[i].sym->dims);
                after  += ref_cost(&R.refs[i], 0, esz, R.refs[i].sym->dims);
            }
            c->promising = after < before - 1e-9;
        }
    }
}

static int interchange_scan(Node *program, const HTContext *ctx, int check, Cand *out, int max, Node **n1, Node **n2)
{
    Find F;
    memset(&F, 0, sizeof F);
    F.out = out; F.max = max; F.check = check; F.ctx = ctx; F.tr = 0; F.program = program;
    F.n1 = n1; F.n2 = n2;
    walk_program(program, &F, interchange_visit);
    return F.n;
}

static int interchange_apply(Node *program, const HTContext *ctx, const Cand *c)
{
    Cand tmp[128];
    Node *n1[128], *n2[128];
    int k, n = interchange_scan(program, ctx, 0, tmp, 128, n1, n2);
    for (k = 0; k < n; k++) {
        if (!strcmp(tmp[k].key, c->key)) {
            Node *a = n1[k], *b = n2[k], *t;
            t = a->a; a->a = b->a; b->a = t;          /* init, condition and update trade places; */
            t = a->b; a->b = b->b; b->b = t;          /* each body stays where it is              */
            t = a->c; a->c = b->c; b->c = t;
            return 1;
        }
    }
    return 0;
}

/* ================================================================== */
/* 2. Loop fusion                                                      */
/* ================================================================== */

static int shares_array(const Region *A, const Region *B)
{
    int i, j;
    for (i = 0; i < A->nrefs; i++)
        for (j = 0; j < B->nrefs; j++)
            if (A->refs[i].sym == B->refs[j].sym) return 1;
    return 0;
}

static void fusion_visit(void *u, Node *s, Node *fn)
{
    Find *F = u;
    Node *s2;
    LoopInfo l1, l2;
    char key[128], desc[200];
    const char *vars[1];
    Cand *c;

    if (s->kind != N_FOR || !s->next || s->next->kind != N_FOR || F->n >= F->max) return;
    s2 = s->next;
    if (!canon_loop(s, &l1) || !canon_loop(s2, &l2) ||
        strcmp(l1.var, l2.var) || l1.start != l2.start || l1.step != l2.step || l1.trips != l2.trips)
        return;                                        /* different iteration spaces: not an opportunity */
    snprintf(key, sizeof key, "fusion:%s:%d", fn->text, s->line);
    snprintf(desc, sizeof desc, "fuse the loops at lines %d and %d in %s", s->line, s2->line, fn->text);
    c = &F->out[F->n];
    if (F->n1) { F->n1[F->n] = s; F->n2[F->n] = s2; }
    add_cand(F->out, &F->n, F->max, F->tr, fn->text, s->line, key, desc);
    set_region(c, fn, s);
    if (!F->check) return;

    vars[0] = l1.var;
    if (ast_assigns(s->d, l1.var) || ast_assigns(s2->d, l1.var)) {
        c->safety = SAFE_NO;
        snprintf(c->why, sizeof c->why, "the loop variable is modified inside a loop body");
        return;
    }
    {
        Region R1, R2;
        region_scan_stmt(&R1, s, vars, 1);
        region_scan_stmt(&R2, s2, vars, 1);
        if (!fusion_legal(&R1, &R2, l1.step, 1, c->why, sizeof c->why, c->evidence, sizeof c->evidence)) {
            c->safety = SAFE_NO;
            return;
        }
        c->safety = SAFE_EXACT;
        c->promising = shares_array(&R1, &R2);
    }
}

static int fusion_scan(Node *program, const HTContext *ctx, int check, Cand *out, int max, Node **n1, Node **n2)
{
    Find F;
    memset(&F, 0, sizeof F);
    F.out = out; F.max = max; F.check = check; F.ctx = ctx; F.tr = 1; F.program = program;
    F.n1 = n1; F.n2 = n2;
    walk_program(program, &F, fusion_visit);
    return F.n;
}

static Node *as_block(Node *stmt)
{
    Node *b;
    if (stmt->kind == N_BLOCK) return stmt;
    b = mk(N_BLOCK, stmt->line);
    b->a = stmt;
    stmt->next = NULL;
    return b;
}

static int fusion_apply(Node *program, const HTContext *ctx, const Cand *c)
{
    Cand tmp[128];
    Node *n1[128], *n2[128];
    int k, n = fusion_scan(program, ctx, 0, tmp, 128, n1, n2);
    for (k = 0; k < n; k++) {
        if (!strcmp(tmp[k].key, c->key)) {
            Node *l1 = n1[k], *l2 = n2[k], *b1, *b2, *nb;
            b1 = as_block(l1->d);
            b2 = as_block(l2->d);
            b1->next = b2;
            b2->next = NULL;
            nb = mk(N_BLOCK, l1->line);
            nb->a = b1;                               /* { { body 1 } { body 2 } } keeps locals apart */
            l1->d = nb;
            l1->next = l2->next;                      /* loop 2 disappears */
            return 1;
        }
    }
    return 0;
}

/* ================================================================== */
/* 3. Scalar promotion of loop-invariant array elements                */
/* ================================================================== */

typedef struct {
    const Symbol *sym;
    char key[128];
    Node *first;                    /* one N_INDEX occurrence                          */
    int nacc, nwrite, bad;          /* bad: other elements of the array, or not invariant */
} PGroup;

typedef struct {
    PGroup g[16];
    int ng, bad_all;
    const char *assigned[32];
    int nassigned;
} PScan;

static void note_assigned(PScan *S, const char *name)
{
    int i;
    for (i = 0; i < S->nassigned; i++) if (!strcmp(S->assigned[i], name)) return;
    if (S->nassigned < 32) S->assigned[S->nassigned++] = name;
}

static void collect_assigned(PScan *S, Node *n)
{
    for (; n; n = n->next) {
        if ((n->kind == N_ASSIGN || n->kind == N_INCDEC) && n->a && n->a->kind == N_ID)
            note_assigned(S, n->a->text);
        if (n->kind == N_VAR_DECL) note_assigned(S, n->text);
        collect_assigned(S, n->a); collect_assigned(S, n->b);
        collect_assigned(S, n->c); collect_assigned(S, n->d);
    }
}

static int has_nested_ref_or_call(const Node *e)
{
    for (; e; e = e->next) {
        if (e->kind == N_INDEX || e->kind == N_CALL || e->kind == N_ADDR) return 1;
        if (has_nested_ref_or_call(e->a) || has_nested_ref_or_call(e->b)) return 1;
    }
    return 0;
}

static int subscripts_invariant(PScan *S, Node *idx_node, const char *loopvar)
{
    Node *n = idx_node;
    while (n->kind == N_INDEX) {
        int i;
        if (has_nested_ref_or_call(n->b) || mentions(n->b, loopvar)) return 0;
        for (i = 0; i < S->nassigned; i++)
            if (mentions(n->b, S->assigned[i])) return 0;
        n = n->a;
    }
    return 1;
}

static PGroup *pgroup(PScan *S, const Symbol *sym)
{
    int i;
    for (i = 0; i < S->ng; i++) if (S->g[i].sym == sym) return &S->g[i];
    if (S->ng >= 16) { S->bad_all = 1; return NULL; }
    memset(&S->g[S->ng], 0, sizeof(PGroup));
    S->g[S->ng].sym = sym;
    return &S->g[S->ng++];
}

static void pscan_ref(PScan *S, Node *e, int write, const char *loopvar)
{
    Node *base = e;
    PGroup *g;
    char key[128];
    while (base->kind == N_INDEX) base = base->a;
    if (base->kind != N_ID || !base->sym) { S->bad_all = 1; return; }
    g = pgroup(S, base->sym);
    if (!g) return;
    ast_key(e, key, sizeof key);
    if (!strncmp(key, "<long", 5)) { g->bad = 1; return; }
    if (!subscripts_invariant(S, e, loopvar)) { g->bad = 1; return; }
    if (g->nacc == 0) { snprintf(g->key, sizeof g->key, "%s", key); g->first = e; }
    else if (strcmp(g->key, key) != 0) g->bad = 1;     /* a different element of the same array may alias */
    g->nacc++;
    if (write) g->nwrite++;
}

static int pscan_expr(PScan *S, Node *e, int lhs, const char *loopvar)
{
    Node *a, *n;
    if (!e) return 1;
    switch (e->kind) {
    case N_INT: case N_FLOAT: case N_STR: case N_NULL: case N_ID:
        return 1;
    case N_ADDR:
        return 0;
    case N_UNOP:
        return pscan_expr(S, e->a, 0, loopvar);
    case N_BINOP:
        return pscan_expr(S, e->a, 0, loopvar) && pscan_expr(S, e->b, 0, loopvar);
    case N_INDEX:
        pscan_ref(S, e, lhs, loopvar);
        for (n = e; n->kind == N_INDEX; n = n->a)
            if (!pscan_expr(S, n->b, 0, loopvar)) return 0;
        return 1;
    case N_ASSIGN:
        if (!pscan_expr(S, e->b, 0, loopvar)) return 0;
        if (e->a->kind == N_INDEX) return pscan_expr(S, e->a, 1, loopvar);
        return e->a->kind == N_ID;
    case N_INCDEC:
        return e->a->kind == N_ID ? 1 : pscan_expr(S, e->a, 1, loopvar);
    case N_CALL:
        if (lib_kind(e->text) != LIB_MATH) return 0;
        for (a = e->a; a; a = a->next) if (!pscan_expr(S, a, 0, loopvar)) return 0;
        return 1;
    default:
        return 0;
    }
}

static int pscan_stmts(PScan *S, Node *l, const char *loopvar)
{
    for (; l; l = l->next) {
        switch (l->kind) {
        case N_EXPR_STMT:
            if (!pscan_expr(S, l->a, 0, loopvar)) return 0;
            break;
        case N_VAR_DECL:
            if (l->ndims > 0) return 0;
            if (l->a && !pscan_expr(S, l->a, 0, loopvar)) return 0;
            break;
        case N_BLOCK:
            if (!pscan_stmts(S, l->a, loopvar)) return 0;
            break;
        case N_EMPTY:
            break;
        default:
            return 0;                                  /* if / loops / return: not straight-line */
        }
    }
    return 1;
}

static void promotion_visit(void *u, Node *s, Node *fn)
{
    Find *F = u;
    LoopInfo li;
    PScan S;
    int gi;

    if (s->kind != N_FOR || contains_loop(s->d)) return;
    if (!canon_loop(s, &li)) return;
    memset(&S, 0, sizeof S);
    collect_assigned(&S, s->d);
    if (!pscan_stmts(&S, body_list(s), li.var) || S.bad_all) return;
    if (ast_assigns(s->d, li.var)) return;

    for (gi = 0; gi < S.ng; gi++) {
        PGroup *g = &S.g[gi];
        char key[128], desc[200], why[260] = "";
        Cand *c;
        if (g->bad || g->nacc == 0 || F->n >= F->max) continue;
        snprintf(key, sizeof key, "promotion:%s:%d:%s", fn->text, s->line, g->key);
        snprintf(desc, sizeof desc, "keep %s in a register across the loop over '%s' (line %d) in %s",
                 g->key, li.var, s->line, fn->text);
        c = &F->out[F->n];
        if (F->n1) { F->n1[F->n] = s; F->n2[F->n] = g->first; }
        add_cand(F->out, &F->n, F->max, F->tr, fn->text, s->line, key, desc);
        set_region(c, fn, s);
        if (!F->check) continue;
        c->promising = li.trips >= 2;
        if (!shared_ok(F->program, F->ctx, fn, g->sym, g->nwrite > 0, why, sizeof why)) {
            c->safety = SAFE_NO;
            snprintf(c->why, sizeof c->why, "%s", why);
            continue;
        }
        c->safety = SAFE_EXACT;
        snprintf(c->evidence, sizeof c->evidence,
                 "%s is the same element on every iteration; no other element of '%s' is touched in the loop, "
                 "there are no calls (other than math), the body is straight-line, and the loop runs %ld time(s) "
                 "so the element is certainly accessed",
                 g->key, g->sym->name, li.trips);
    }
}

static int promotion_scan(Node *program, const HTContext *ctx, int check, Cand *out, int max, Node **n1, Node **n2)
{
    Find F;
    memset(&F, 0, sizeof F);
    F.out = out; F.max = max; F.check = check; F.ctx = ctx; F.tr = 2; F.program = program;
    F.n1 = n1; F.n2 = n2;
    walk_program(program, &F, promotion_visit);
    return F.n;
}

/* Replace every N_INDEX whose canonical key equals `key` inside the children of n
 * by the identifier `tmp`. */
static void subst_in(Node *n, const char *key, const char *tmp)
{
    Node **fields[4];
    int f;
    fields[0] = &n->a; fields[1] = &n->b; fields[2] = &n->c; fields[3] = &n->d;
    for (f = 0; f < 4; f++) {
        Node **p;
        for (p = fields[f]; *p; p = &(*p)->next) {
            if ((*p)->kind == N_INDEX) {
                char k[128];
                ast_key(*p, k, sizeof k);
                if (!strcmp(k, key)) {
                    Node *r = mk_text(N_ID, (*p)->line, tmp);
                    r->next = (*p)->next;
                    *p = r;
                    continue;
                }
            }
            subst_in(*p, key, tmp);
        }
    }
}

/* Put the list `before` in front of `target` and the node `after` behind it, wherever target
 * lives.  Either may be NULL. */
static int splice_in(Node *parent, Node *target, Node *before, Node *after)
{
    Node **fields[4];
    Node *btail = before;
    int f;
    while (btail && btail->next) btail = btail->next;
    fields[0] = &parent->a; fields[1] = &parent->b; fields[2] = &parent->c; fields[3] = &parent->d;
    for (f = 0; f < 4; f++) {
        Node **slot = fields[f], **p;
        for (p = slot; *p; p = &(*p)->next) {
            if (*p == target) {
                Node *rest = target->next;
                int list_ctx = (parent->kind == N_BLOCK && f == 0);
                if (!list_ctx && p == slot) {          /* single-statement slot: wrap it in a block */
                    Node *blk = mk(N_BLOCK, target->line);
                    blk->a = before ? before : target;
                    if (btail) btail->next = target;
                    target->next = after;
                    if (after) after->next = rest;
                    *slot = blk;
                    return 1;
                }
                *p = before ? before : target;
                if (btail) btail->next = target;
                target->next = after ? after : rest;
                if (after) after->next = rest;
                return 1;
            }
        }
    }
    for (f = 0; f < 4; f++) {
        Node *c;
        for (c = *fields[f]; c; c = c->next)
            if (splice_in(c, target, before, after)) return 1;
    }
    return 0;
}

static int name_in_use(Node *n, const char *name)
{
    for (; n; n = n->next) {
        if ((n->kind == N_ID || n->kind == N_VAR_DECL) && n->text && !strcmp(n->text, name)) return 1;
        if (name_in_use(n->a, name) || name_in_use(n->b, name) ||
            name_in_use(n->c, name) || name_in_use(n->d, name))
            return 1;
    }
    return 0;
}

static int writes_elem(Node *n, const char *key)
{
    for (; n; n = n->next) {
        if ((n->kind == N_ASSIGN || n->kind == N_INCDEC) && n->a && n->a->kind == N_INDEX) {
            char k[128];
            ast_key(n->a, k, sizeof k);
            if (!strcmp(k, key)) return 1;
        }
        if (writes_elem(n->a, key) || writes_elem(n->b, key) || writes_elem(n->c, key) || writes_elem(n->d, key))
            return 1;
    }
    return 0;
}

static int promotion_apply(Node *program, const HTContext *ctx, const Cand *c)
{
    Cand tmp[128];
    Node *n1[128], *n2[128];
    int k, n = promotion_scan(program, ctx, 0, tmp, 128, n1, n2);
    for (k = 0; k < n; k++) {
        if (!strcmp(tmp[k].key, c->key)) {
            Node *loop = n1[k], *elem = n2[k], *base = elem, *decl, *store = NULL;
            char key[128], name[80];
            int n = 0, has_write;
            while (base->kind == N_INDEX) base = base->a;
            ast_key(elem, key, sizeof key);
            has_write = writes_elem(loop->d, key);
            snprintf(name, sizeof name, "__ht_%s_%d", base->text, loop->line);
            while (name_in_use(program, name))
                snprintf(name, sizeof name, "__ht_%s_%d_%d", base->text, loop->line, ++n);

            decl = mk_text(N_VAR_DECL, loop->line, name);       /* T tmp = A[idx]; */
            decl->type = base->sym->type;
            decl->a = ast_clone(elem);
            if (has_write) {                                     /* A[idx] = tmp;   */
                Node *as = mk_text(N_ASSIGN, loop->line, "=");
                as->a = ast_clone(elem);
                as->b = mk_text(N_ID, loop->line, name);
                store = mk(N_EXPR_STMT, loop->line);
                store->a = as;
            }
            subst_in(loop->d, key, name);
            return splice_in(program, loop, decl, store);
        }
    }
    return 0;
}

/* ================================================================== */
/* Shared helpers for the transformations below                        */
/* ================================================================== */

static int count_nodes(const Node *n)
{
    int c = 0;
    for (; n; n = n->next) {
        c++;
        c += count_nodes(n->a) + count_nodes(n->b) + count_nodes(n->c) + count_nodes(n->d);
    }
    return c;
}

static Node *mk_int(long v, int line)
{
    Node *n = mk(N_INT, line);
    n->ival = v;
    return n;
}

/* replace every use of identifier `var` in n's children by (var + off) */
static void subst_var_offset(Node *n, const char *var, long off)
{
    Node **fields[4];
    int f;
    fields[0] = &n->a; fields[1] = &n->b; fields[2] = &n->c; fields[3] = &n->d;
    for (f = 0; f < 4; f++) {
        Node **p;
        for (p = fields[f]; *p; p = &(*p)->next) {
            if ((*p)->kind == N_ID && !strcmp((*p)->text, var)) {
                Node *id = *p, *bin;
                bin = mk_text(N_BINOP, id->line, off >= 0 ? "+" : "-");
                bin->next = id->next;
                id->next = NULL;
                bin->a = id;
                bin->b = mk_int(off >= 0 ? off : -off, id->line);
                *p = bin;
                continue;
            }
            subst_var_offset(*p, var, off);
        }
    }
}

/* replace every expression structurally equal to `pat` inside n's children by identifier tmp */
static void subst_expr(Node *n, const Node *pat, const char *tmp)
{
    Node **fields[4];
    int f;
    fields[0] = &n->a; fields[1] = &n->b; fields[2] = &n->c; fields[3] = &n->d;
    for (f = 0; f < 4; f++) {
        Node **p;
        for (p = fields[f]; *p; p = &(*p)->next) {
            if (ast_same(*p, pat)) {
                Node *r = mk_text(N_ID, (*p)->line, tmp);
                r->next = (*p)->next;
                *p = r;
                continue;
            }
            subst_expr(*p, pat, tmp);
        }
    }
}

static int program_has_threads(Node *program)
{
    Node *f;
    for (f = program->a; f; f = f->next) {
        if (f->kind != N_FUNC) continue;
        {
            /* any call to pthread_create */
            Node *stack[256];
            int sp = 0;
            stack[sp++] = f->b;
            while (sp) {
                Node *n = stack[--sp];
                for (; n; n = n->next) {
                    if (n->kind == N_CALL && lib_kind(n->text) == LIB_PTHREAD_CREATE) return 1;
                    if (sp + 4 < 256) { if (n->a) stack[sp++] = n->a; if (n->b) stack[sp++] = n->b; if (n->c) stack[sp++] = n->c; if (n->d) stack[sp++] = n->d; }
                }
            }
        }
    }
    return 0;
}

static int writes_scalar_sym(Node *n, const Symbol *s)
{
    for (; n; n = n->next) {
        if ((n->kind == N_ASSIGN || n->kind == N_INCDEC) && n->a && n->a->kind == N_ID && n->a->sym == s) return 1;
        if (writes_scalar_sym(n->a, s) || writes_scalar_sym(n->b, s) || writes_scalar_sym(n->c, s) || writes_scalar_sym(n->d, s))
            return 1;
    }
    return 0;
}

static int has_non_math_call(const Node *n)
{
    for (; n; n = n->next) {
        if (n->kind == N_CALL && lib_kind(n->text) != LIB_MATH) return 1;
        if (has_non_math_call(n->a) || has_non_math_call(n->b) || has_non_math_call(n->c) || has_non_math_call(n->d))
            return 1;
    }
    return 0;
}

/* ================================================================== */
/* 4. Loop unrolling                                                   */
/* ================================================================== */

static void unroll_visit(void *u, Node *s, Node *fn)
{
    Find *F = u;
    LoopInfo li;
    char key[128], desc[200];
    Cand *c;

    if (s->kind != N_FOR || F->n >= F->max) return;
    if (contains_loop(s->d) || !canon_loop(s, &li) || li.trips < 4) return;
    snprintf(key, sizeof key, "unrolling:%s:%d", fn->text, s->line);
    snprintf(desc, sizeof desc, "unroll the loop over '%s' (line %d, %ld iterations) in %s", li.var, s->line, li.trips, fn->text);
    c = &F->out[F->n];
    if (F->n1) { F->n1[F->n] = s; F->n2[F->n] = s; }
    add_cand(F->out, &F->n, F->max, F->tr, fn->text, s->line, key, desc);
    set_region(c, fn, s);
    c->param = 4;
    if (!F->check) return;
    if (ast_assigns(s->d, li.var)) {
        c->safety = SAFE_NO;
        snprintf(c->why, sizeof c->why, "the loop variable '%s' is modified inside the body", li.var);
        return;
    }
    if (count_nodes(s->d) > 400) {
        c->safety = SAFE_NO;
        snprintf(c->why, sizeof c->why, "the loop body is too large to duplicate");
        return;
    }
    c->safety = SAFE_EXACT;
    c->promising = li.trips >= 16 && count_nodes(s->d) <= 160;
    snprintf(c->evidence, sizeof c->evidence,
             "unrolling replays the iterations in exactly the original order, so no dependence can change; "
             "the trip count (%ld) is a compile-time constant and '%s' is not modified in the body",
             li.trips, li.var);
}

static int unroll_scan(Node *program, const HTContext *ctx, int check, Cand *out, int max, Node **n1, Node **n2)
{
    Find F;
    memset(&F, 0, sizeof F);
    F.out = out; F.max = max; F.check = check; F.ctx = ctx; F.tr = 3; F.program = program;
    F.n1 = n1; F.n2 = n2;
    walk_program(program, &F, unroll_visit);
    return F.n;
}

static int unroll_apply(Node *program, const HTContext *ctx, const Cand *c)
{
    Cand tmp[128];
    Node *n1[128], *n2[128];
    int k, n = unroll_scan(program, ctx, 0, tmp, 128, n1, n2);
    for (k = 0; k < n; k++) {
        Node *loop, *blk, *epi = NULL, *tail = NULL;
        LoopInfo li;
        int f, j;
        long q, rem, mt, newbound;
        char var[64];
        if (strcmp(tmp[k].key, c->key) != 0) continue;
        loop = n1[k];
        if (!loop_info(loop, &li) || !li.trips_known) return 0;
        f = c->param;
        if (f != 2 && f != 4 && f != 8 && f != 16) f = 4;
        while (f > 2 && li.trips < 2L * f) f /= 2;
        if (li.trips < 2L * f) return 0;
        snprintf(var, sizeof var, "%s", li.var);
        q = li.trips / f; rem = li.trips % f; mt = q * f;
        newbound = li.start + mt * li.step;

        if (rem) {                                        /* epilogue: the iterations that do not fill a group */
            long es = li.start + mt * li.step;
            epi = ast_clone(loop);
            epi->next = NULL;
            if (epi->a->kind == N_VAR_DECL) epi->a->a = mk_int(es, loop->line);
            else epi->a->b = mk_int(es, loop->line);
        }
        blk = mk(N_BLOCK, loop->line);
        for (j = 0; j < f; j++) {
            Node *cp = (j == 0) ? as_block(loop->d) : as_block(ast_clone(loop->d));
            if (j > 0) subst_var_offset(cp, var, (long)j * li.step);
            cp->next = NULL;
            if (tail) tail->next = cp; else blk->a = cp;
            tail = cp;
        }
        loop->b = mk_bin(li.step > 0 ? "<" : ">", mk_text(N_ID, loop->line, var), mk_int(newbound, loop->line), loop->line);
        {
            Node *as = mk_text(N_ASSIGN, loop->line, "+=");
            as->a = mk_text(N_ID, loop->line, var);
            as->b = mk_int((long)f * li.step, loop->line);
            loop->c = as;
        }
        loop->d = blk;
        if (epi && !splice_in(program, loop, NULL, epi)) return 0;
        return 1;
    }
    return 0;
}

/* ================================================================== */
/* 5. Loop-invariant code motion                                       */
/* ================================================================== */

typedef struct {
    PScan assigned;                 /* names the loop assigns (reuses the promotion scan)    */
    const char *loopvar;
    Node *program;
    int threads;                    /* the program creates threads                            */
    int body_calls;                 /* the body calls something other than math functions     */
    Node *inv[6];
    char key[6][96];
    int ninv;
} LScan;

static int l_scalar_ok(LScan *L, const Node *id)
{
    int i;
    const Symbol *sy = id->sym;
    if (!sy || sy->kind != S_VAR || sy->ndims != 0) return 0;
    if (sy->type != T_INT && sy->type != T_FLOAT && sy->type != T_DOUBLE) return 0;
    if (!strcmp(id->text, L->loopvar)) return 0;
    for (i = 0; i < L->assigned.nassigned; i++)
        if (!strcmp(L->assigned.assigned[i], id->text)) return 0;
    if (sy->is_global) {
        if (L->body_calls) return 0;                    /* a called function might change it           */
        if (L->threads) {                               /* another thread might: only constants are safe */
            Node *f;
            for (f = L->program->a; f; f = f->next)
                if (f->kind == N_FUNC && writes_scalar_sym(f->b, sy)) return 0;
        }
    }
    return 1;
}

static int l_invariant(LScan *L, Node *e)
{
    long v;
    switch (e->kind) {
    case N_INT: case N_FLOAT: return 1;
    case N_ID: return l_scalar_ok(L, e);
    case N_UNOP: return e->text[0] == '-' && l_invariant(L, e->a);
    case N_BINOP: {
        const char *o = e->text;
        if (o[1] == 0 && (o[0] == '+' || o[0] == '-' || o[0] == '*'))
            return l_invariant(L, e->a) && l_invariant(L, e->b);
        if (o[1] == 0 && o[0] == '/') {
            if (is_fp_type(e->etype)) return l_invariant(L, e->a) && l_invariant(L, e->b);
            return const_eval(e->b, &v) && v > 0 && l_invariant(L, e->a);       /* no division by zero / INT_MIN/-1 */
        }
        if (o[1] == 0 && o[0] == '%') return const_eval(e->b, &v) && v > 0 && l_invariant(L, e->a);
        return 0;
    }
    case N_CALL: {
        Node *a;
        if (lib_kind(e->text) != LIB_MATH) return 0;
        for (a = e->a; a; a = a->next) if (!l_invariant(L, a)) return 0;
        return 1;
    }
    default: return 0;
    }
}

static int l_nontrivial(const Node *e)
{
    if (e->kind == N_BINOP || e->kind == N_CALL) return 1;
    if (e->kind == N_UNOP) return e->a->kind != N_INT && e->a->kind != N_FLOAT;
    return 0;
}

static void l_expr(LScan *L, Node *e)
{
    Node *a;
    if (!e) return;
    if (l_nontrivial(e) && l_invariant(L, e) &&
        (e->etype == T_INT || e->etype == T_FLOAT || e->etype == T_DOUBLE)) {
        char k[96];
        int i;
        ast_key(e, k, sizeof k);
        for (i = 0; i < L->ninv; i++) if (!strcmp(L->key[i], k)) return;
        if (L->ninv < 6 && strncmp(k, "<long", 5) != 0) {
            snprintf(L->key[L->ninv], sizeof L->key[0], "%s", k);
            L->inv[L->ninv++] = e;
        }
        return;                                         /* maximal: do not look inside */
    }
    switch (e->kind) {
    case N_ASSIGN:
        l_expr(L, e->b);
        if (e->a->kind == N_INDEX) { Node *n; for (n = e->a; n->kind == N_INDEX; n = n->a) l_expr(L, n->b); }
        break;
    case N_INDEX: { Node *n; for (n = e; n->kind == N_INDEX; n = n->a) l_expr(L, n->b); break; }
    case N_BINOP:
        l_expr(L, e->a);
        if (strcmp(e->text, "&&") != 0 && strcmp(e->text, "||") != 0) l_expr(L, e->b);   /* right side is conditional */
        break;
    case N_UNOP: l_expr(L, e->a); break;
    case N_CALL: for (a = e->a; a; a = a->next) l_expr(L, a); break;
    default: break;
    }
}

static void l_stmts(LScan *L, Node *l)
{
    LoopInfo li;
    for (; l; l = l->next) {
        switch (l->kind) {
        case N_EXPR_STMT: l_expr(L, l->a); break;
        case N_VAR_DECL: if (l->a && l->ndims == 0) l_expr(L, l->a); break;
        case N_BLOCK: l_stmts(L, l->a); break;
        case N_IF: l_expr(L, l->a); break;               /* only the condition runs unconditionally */
        case N_FOR:
            if (loop_info(l, &li) && li.trips_known && li.trips >= 1) l_stmts(L, l->d);
            break;
        default: break;
        }
    }
}

static void licm_visit(void *u, Node *s, Node *fn)
{
    Find *F = u;
    LoopInfo li;
    LScan L;
    char key[128], desc[200];
    Cand *c;

    if (s->kind != N_FOR || F->n >= F->max) return;
    if (!canon_loop(s, &li) || li.trips < 2) return;
    memset(&L, 0, sizeof L);
    L.loopvar = li.var;
    L.program = F->program;
    L.threads = program_has_threads(F->program);
    L.body_calls = has_non_math_call(s->d);
    collect_assigned(&L.assigned, s->d);
    if (ast_assigns(s->d, li.var)) return;
    l_stmts(&L, body_list(s));
    if (L.ninv == 0) return;

    snprintf(key, sizeof key, "licm:%s:%d", fn->text, s->line);
    snprintf(desc, sizeof desc, "move %d loop-invariant computation(s) out of the loop over '%s' (line %d) in %s",
             L.ninv, li.var, s->line, fn->text);
    c = &F->out[F->n];
    if (F->n1) { F->n1[F->n] = s; F->n2[F->n] = s; }
    add_cand(F->out, &F->n, F->max, F->tr, fn->text, s->line, key, desc);
    set_region(c, fn, s);
    if (!F->check) return;
    c->safety = SAFE_EXACT;
    c->promising = 1;
    snprintf(c->evidence, sizeof c->evidence,
             "%d expression(s), e.g. %.40s, use only constants and variables the loop never changes, call only math functions "
             "and cannot trap (no division by a variable); each is computed with the same operations on the same values, "
             "so the result is bit-identical", L.ninv, L.key[0]);
}

static int licm_scan(Node *program, const HTContext *ctx, int check, Cand *out, int max, Node **n1, Node **n2)
{
    Find F;
    memset(&F, 0, sizeof F);
    F.out = out; F.max = max; F.check = check; F.ctx = ctx; F.tr = 4; F.program = program;
    F.n1 = n1; F.n2 = n2;
    walk_program(program, &F, licm_visit);
    return F.n;
}

static int licm_apply(Node *program, const HTContext *ctx, const Cand *c)
{
    Cand tmp[128];
    Node *n1[128], *n2[128];
    int k, n = licm_scan(program, ctx, 0, tmp, 128, n1, n2);
    for (k = 0; k < n; k++) {
        Node *loop, *decls = NULL, *tail = NULL;
        LoopInfo li;
        LScan L;
        int i;
        if (strcmp(tmp[k].key, c->key) != 0) continue;
        loop = n1[k];
        if (!loop_info(loop, &li)) return 0;
        memset(&L, 0, sizeof L);
        L.loopvar = li.var;
        L.program = program;
        L.threads = program_has_threads(program);
        L.body_calls = has_non_math_call(loop->d);
        collect_assigned(&L.assigned, loop->d);
        l_stmts(&L, body_list(loop));
        if (L.ninv == 0) return 0;
        for (i = 0; i < L.ninv; i++) {
            char name[96];
            Node *d;
            int tries = 0;
            snprintf(name, sizeof name, "__ht_inv_%d_%d", loop->line, i);
            while (name_in_use(program, name)) snprintf(name, sizeof name, "__ht_inv_%d_%d_%d", loop->line, i, ++tries);
            d = mk_text(N_VAR_DECL, loop->line, name);
            d->type = L.inv[i]->etype;
            d->a = ast_clone(L.inv[i]);                    /* clone before the body is rewritten */
            if (tail) tail->next = d; else decls = d;
            tail = d;
        }
        for (i = 0, tail = decls; i < L.ninv && tail; i++, tail = tail->next)
            subst_expr(loop->d, ast_clone(L.inv[i]), tail->text);
        return splice_in(program, loop, decls, NULL);
    }
    return 0;
}

/* ================================================================== */
/* Registry                                                            */
/* ================================================================== */

const Transformation g_transforms[] = {
    { "interchange", "Loop interchange", "loop_interchange permutation loop_permutation",
      "the OUTER loop of two perfectly nested loops", NULL, 0, interchange_scan, interchange_apply },
    { "fusion", "Loop fusion", "loop_fusion jamming loop_jamming",
      "the FIRST of two adjacent loops over the same range", NULL, 0, fusion_scan, fusion_apply },
    { "promotion", "Scalar promotion", "scalar_promotion scalar_replacement register_promotion",
      "the innermost loop that touches the same array element on every iteration", NULL, 0, promotion_scan, promotion_apply },
    { "unrolling", "Loop unrolling", "unroll loop_unrolling loop_unroll",
      "the innermost loop to unroll", "factor", 4, unroll_scan, unroll_apply },
    { "licm", "Loop-invariant code motion", "loop_invariant_code_motion invariant_code_motion code_motion hoisting loop_hoisting",
      "the loop whose invariant computations should move out of it", NULL, 0, licm_scan, licm_apply },
};
const int g_ntransforms = 5;

/* ---- the three roles of a transformation ---- */

int transform_find_sites(int tr, Node *program, Cand *out, int max)
{
    return g_transforms[tr].scan(program, NULL, 0, out, max, NULL, NULL);       /* patterns only */
}

int transform_find(int tr, Node *program, const HTContext *ctx, Cand *out, int max)
{
    return g_transforms[tr].scan(program, ctx, 1, out, max, NULL, NULL);        /* pattern + preconditions */
}

int transform_check(int tr, Node *program, const HTContext *ctx, const Cand *site, Cand *verdict)
{
    Cand tmp[128];
    int k, n = g_transforms[tr].scan(program, ctx, 1, tmp, 128, NULL, NULL);
    for (k = 0; k < n; k++)
        if (!strcmp(tmp[k].key, site->key)) { *verdict = tmp[k]; return 1; }
    return 0;
}

int transform_apply(Node *program, const HTContext *ctx, const Cand *c)
{
    return g_transforms[c->tr].apply(program, ctx, c);
}

int transform_by_name(const char *canon)
{
    int t;
    for (t = 0; t < g_ntransforms; t++) {
        const char *al = g_transforms[t].aliases;
        size_t n = strlen(canon);
        if (!strcmp(g_transforms[t].name, canon)) return t;
        while (al && *al) {
            const char *sp = strchr(al, ' ');
            size_t l = sp ? (size_t)(sp - al) : strlen(al);
            if (l == n && !strncmp(al, canon, n)) return t;
            al = sp ? sp + 1 : al + l;
        }
    }
    return -1;
}

/* ================================================================== */
/* Plain-language explanation of an opportunity                        */
/* ================================================================== */

/* 12960000 -> "13.0 million" (explanations are read by people) */
static void human(double v, char *out, size_t n)
{
    if (v >= 1e9)      snprintf(out, n, "%.1f billion", v / 1e9);
    else if (v >= 1e6) snprintf(out, n, "%.1f million", v / 1e6);
    else if (v >= 1e3) snprintf(out, n, "%.0f thousand", v / 1e3);
    else               snprintf(out, n, "%.0f", v);
}

void explain_candidate(const Cand *c, const Stats *b, const Stats *a, char *out, size_t n)
{
    const char *nm = g_transforms[c->tr].name;
    int have = (a != NULL && b != NULL);
    char h1[32], h2[32], h3[32], h4[32];

    if (!strcmp(nm, "interchange")) {
        if (have && b->strided > a->strided) {
            human(b->strided, h1, sizeof h1); human(b->dram, h2, sizeof h2); human(a->dram, h3, sizeof h3);
            snprintf(out, n, "The inner loop currently walks arrays with a large stride (%s strided accesses), "
                     "so almost every access starts a new cache line. After the swap those accesses are sequential "
                     "and each fetched line is fully used (estimated memory lines %s to %s).", h1, h2, h3);
        } else if (have) {
            human(b->dram, h2, sizeof h2); human(a->dram, h3, sizeof h3);
            snprintf(out, n, "Swapping the two loops changes the order in which memory is visited "
                     "(estimated memory lines %s to %s).", h2, h3);
        } else {
            snprintf(out, n, "Swapping the loops can make the innermost loop walk memory sequentially.");
        }
    } else if (!strcmp(nm, "fusion")) {
        if (have) {
            human(b->dram, h1, sizeof h1); human(a->dram, h2, sizeof h2);
            human(b->ops, h3, sizeof h3); human(a->ops, h4, sizeof h4);
            snprintf(out, n, "Both loops cover the same range and use the same arrays. Fusing them lets each "
                     "element be reused while it is still in cache (estimated memory lines %s to %s, "
                     "operations %s to %s).", h1, h2, h3, h4);
        } else {
            snprintf(out, n, "Both loops cover the same range and use the same arrays; fusing them reuses data "
                     "while it is still in cache and removes one loop's overhead.");
        }
    } else if (!strcmp(nm, "unrolling")) {
        if (have) {
            human(b->ops, h1, sizeof h1); human(a->ops, h2, sizeof h2);
            snprintf(out, n, "Running several iterations per trip removes loop-control work (compare and branch) and gives "
                     "the processor longer straight-line code to schedule (estimated operations %s to %s). The iterations "
                     "still run in exactly the original order.", h1, h2);
        } else {
            snprintf(out, n, "Running several iterations per trip removes loop-control work and gives the processor "
                     "longer straight-line code to schedule; the iterations still run in the original order.");
        }
    } else if (!strcmp(nm, "licm")) {
        if (have) {
            human(b->ops, h1, sizeof h1); human(a->ops, h2, sizeof h2);
            snprintf(out, n, "Some computations in the loop produce the same value every iteration. Computing them once "
                     "before the loop saves repeating them (estimated operations %s to %s). Note that gcc -O2 often "
                     "does this already.", h1, h2);
        } else {
            snprintf(out, n, "Some computations in the loop produce the same value every iteration; computing them once "
                     "before the loop avoids repeating them.");
        }
    } else {
        if (have) {
            human(b->loads, h1, sizeof h1); human(a->loads, h2, sizeof h2);
            human(b->stores, h3, sizeof h3); human(a->stores, h4, sizeof h4);
            snprintf(out, n, "The element is the same on every iteration, so it can live in a register: loads "
                     "drop from %s to %s and stores from %s to %s.", h1, h2, h3, h4);
        } else {
            snprintf(out, n, "The element is the same on every iteration, so it can live in a register instead of "
                     "being loaded (and stored) every time.");
        }
    }
}
