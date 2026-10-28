/* depend.c - dependence analysis for loop interchange and loop fusion.
 *
 * Method (classical, restricted to what can be decided exactly):
 *   1. Every array reference in a loop body is turned into one subscript per
 *      dimension, each classified as
 *         AFF   constant + sum(coef * loopvar)       (a[i+1][j-1], a[2*i])
 *         SYM   no loop variable at all, not constant (a[n])
 *         OTHER anything else                         (a[b[i]], a[i*n+j])
 *   2. For two references to the same array where at least one is a write, the
 *      subscript equations give a distance for every loop variable that occurs
 *      in them; variables that occur in no equation are free.
 *      A pair whose equations cannot be solved this way is UNKNOWN.
 *   3. A loop permutation (or fusion) is legal iff no conflicting pair of
 *      iterations would execute in the opposite order afterwards.  Because the
 *      test only needs the sign of every component, it enumerates sign patterns.
 *
 * UNKNOWN always means "refuse".  The analysis therefore never claims more than
 * it proved; it can miss legal transformations but must not accept illegal ones
 * (that property is what tests/fuzz_opt.py attacks).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "depend.h"
#include "ht.h"
#include "astutil.h"
#include "common.h"

/* ================================================================== */
/* Scanning                                                            */
/* ================================================================== */

static void forbid(Region *R, const char *what)
{
    if (!R->forbidden) {
        R->forbidden = 1;
        snprintf(R->bad, sizeof R->bad, "%s", what);
    }
}

static Scal *scal_get(Region *R, Symbol *sym, const char *name)
{
    int i;
    for (i = 0; i < R->nsc; i++)
        if (!strcmp(R->sc[i].name, name)) return &R->sc[i];
    if (R->nsc >= MAXSC) { R->sc_overflow = 1; return NULL; }
    memset(&R->sc[R->nsc], 0, sizeof(Scal));
    snprintf(R->sc[R->nsc].name, sizeof R->sc[0].name, "%s", name);
    R->sc[R->nsc].sym = sym;
    return &R->sc[R->nsc++];
}

const Scal *region_scalar(const Region *R, const char *name)
{
    int i;
    for (i = 0; i < R->nsc; i++)
        if (!strcmp(R->sc[i].name, name)) return &R->sc[i];
    return NULL;
}

/* ---------- affine subscripts ---------- */

typedef struct { int ok; long k; long coef[MAXV]; } Aff;

static Aff aff(const Node *e, const char **vars, int nv)
{
    Aff r, a, b;
    int v, isconst;
    memset(&r, 0, sizeof r);
    r.ok = 1;
    switch (e->kind) {
    case N_INT:
        r.k = e->ival;
        return r;
    case N_ID:
        for (v = 0; v < nv; v++)
            if (!strcmp(e->text, vars[v])) { r.coef[v] = 1; return r; }
        r.ok = 0;
        return r;
    case N_UNOP:
        if (e->text[0] == '-') {
            a = aff(e->a, vars, nv);
            if (!a.ok) return a;
            a.k = -a.k;
            for (v = 0; v < nv; v++) a.coef[v] = -a.coef[v];
            return a;
        }
        break;
    case N_BINOP:
        if (!strcmp(e->text, "+") || !strcmp(e->text, "-")) {
            int sgn = (e->text[0] == '+') ? 1 : -1;
            a = aff(e->a, vars, nv);
            b = aff(e->b, vars, nv);
            if (!a.ok || !b.ok) { r.ok = 0; return r; }
            r.k = a.k + sgn * b.k;
            for (v = 0; v < nv; v++) r.coef[v] = a.coef[v] + sgn * b.coef[v];
            return r;
        }
        if (!strcmp(e->text, "*")) {
            a = aff(e->a, vars, nv);
            b = aff(e->b, vars, nv);
            if (!a.ok || !b.ok) { r.ok = 0; return r; }
            isconst = 1;
            for (v = 0; v < nv; v++) if (a.coef[v]) isconst = 0;
            if (isconst) {                       /* constant * affine */
                r.k = a.k * b.k;
                for (v = 0; v < nv; v++) r.coef[v] = a.k * b.coef[v];
                return r;
            }
            isconst = 1;
            for (v = 0; v < nv; v++) if (b.coef[v]) isconst = 0;
            if (isconst) {
                r.k = a.k * b.k;
                for (v = 0; v < nv; v++) r.coef[v] = b.k * a.coef[v];
                return r;
            }
        }
        break;
    default:
        break;
    }
    r.ok = 0;
    return r;
}

static void sub_of(const Node *e, const char **vars, int nv, Sub *s)
{
    Aff a = aff(e, vars, nv);
    int v, m = 0;
    memset(s, 0, sizeof *s);
    ast_key(e, s->key, sizeof s->key);
    if (a.ok) {
        s->kind = SUB_AFF;
        s->k = a.k;
        for (v = 0; v < nv; v++) s->coef[v] = a.coef[v];
        return;
    }
    for (v = 0; v < nv; v++) if (mentions(e, vars[v])) m = 1;
    s->kind = (!m && !has_array_ref(e)) ? SUB_SYM : SUB_OTHER;
}

/* ---------- region scan ---------- */

static void scan_expr(Region *R, Node *e, int is_write);
static void scan_stmt(Region *R, Node *s);

static int is_local_array(const Region *R, const char *name)
{
    int i;
    for (i = 0; i < R->nlocals; i++)
        if (!strcmp(R->locals[i], name)) return 1;
    return 0;
}

static void add_ref(Region *R, Node *e, int write)
{
    Node *subs[2], *base = e;
    int n = 0, i, d;
    Ref *r;
    char key[128];

    while (base->kind == N_INDEX && n < 2) { subs[n++] = base->b; base = base->a; }
    if (base->kind != N_ID || !base->sym) { forbid(R, "an array reference the analysis cannot resolve"); return; }
    /* scan subscript expressions themselves (they may read scalars / arrays) */
    for (i = 0; i < n; i++) scan_expr(R, subs[i], 0);
    if (is_local_array(R, base->text)) return;      /* private to one iteration */

    ast_key(e, key, sizeof key);
    for (i = 0; i < R->nrefs; i++)
        if (!strcmp(R->refs[i].key, key)) { if (write) R->refs[i].write = 1; return; }
    if (R->nrefs >= MAXREFS) { R->ref_overflow = 1; return; }
    r = &R->refs[R->nrefs++];
    memset(r, 0, sizeof *r);
    r->sym = base->sym;
    r->write = write;
    r->ndims = n;
    r->node = e;
    snprintf(r->key, sizeof r->key, "%s", key);
    for (d = 0; d < n; d++)                         /* subs[] is innermost-last: reverse */
        sub_of(subs[n - 1 - d], R->vars, R->nv, &r->sub[d]);
}

static void scan_expr(Region *R, Node *e, int is_write)
{
    Node *a;
    (void)is_write;
    if (!e) return;
    switch (e->kind) {
    case N_INT: case N_FLOAT: case N_STR: case N_NULL:
        return;
    case N_ID:
        if (e->sym && e->sym->kind == S_VAR && e->sym->ndims == 0 &&
            e->sym->type != T_PTHREAD && e->sym->type != T_MUTEX) {
            Scal *s = scal_get(R, e->sym, e->text);
            if (s) s->nread++;
        }
        return;
    case N_ADDR:
        forbid(R, "address-of");
        return;
    case N_UNOP:
        scan_expr(R, e->a, 0);
        return;
    case N_BINOP:
        scan_expr(R, e->a, 0);
        scan_expr(R, e->b, 0);
        return;
    case N_INDEX:
        add_ref(R, e, 0);
        return;
    case N_ASSIGN: {
        int compound = strcmp(e->text, "=") != 0;
        scan_expr(R, e->b, 0);
        if (e->a->kind == N_ID) {
            if (e->a->sym && e->a->sym->ndims == 0) {
                Scal *s = scal_get(R, e->a->sym, e->a->text);
                if (s) {
                    if (!compound) s->nwrite_plain++;
                    else if (!strcmp(e->text, "+=") || !strcmp(e->text, "-=")) s->nwrite_comp++;
                    else { s->nwrite_comp++; s->comp_other++; }
                }
            }
        } else if (e->a->kind == N_INDEX) {
            add_ref(R, e->a, 1);
        }
        return;
    }
    case N_INCDEC:
        if (e->a->kind == N_ID) {
            if (e->a->sym && e->a->sym->ndims == 0) {
                Scal *s = scal_get(R, e->a->sym, e->a->text);
                if (s) s->nwrite_comp++;
            }
        } else if (e->a->kind == N_INDEX) {
            add_ref(R, e->a, 1);
        }
        return;
    case N_CALL:
        for (a = e->a; a; a = a->next) scan_expr(R, a, 0);
        if (lib_kind(e->text) != LIB_MATH) {
            char b[96];
            snprintf(b, sizeof b, "a call to %s()", e->text);
            forbid(R, b);
        }
        return;
    default:
        return;
    }
}

static void scan_list(Region *R, Node *l)
{
    for (; l; l = l->next) scan_stmt(R, l);
}

static void scan_stmt(Region *R, Node *s)
{
    if (!s) return;
    switch (s->kind) {
    case N_BLOCK:
        scan_list(R, s->a);
        break;
    case N_VAR_DECL:
        if (s->ndims > 0) {
            if (R->nlocals < 8) snprintf(R->locals[R->nlocals++], 64, "%s", s->text);
        } else {
            Scal *sc = scal_get(R, s->sym, s->text);
            if (sc) sc->is_private = 1;
        }
        if (s->a) scan_expr(R, s->a, 0);
        break;
    case N_EXPR_STMT:
        scan_expr(R, s->a, 0);
        break;
    case N_IF:
        scan_expr(R, s->a, 0);
        scan_stmt(R, s->b);
        scan_stmt(R, s->c);
        break;
    case N_WHILE:
        scan_expr(R, s->a, 0);
        scan_stmt(R, s->b);
        break;
    case N_FOR: {
        LoopInfo li;
        int canon = loop_info(s, &li);
        if (canon) {
            /* the loop variable is managed by the header: not a data dependence */
            const Node *init = s->a;
            if (init->kind == N_VAR_DECL) {
                Scal *sc = scal_get(R, init->sym, init->text);
                if (sc) sc->is_private = 1;
                if (init->a) scan_expr(R, init->a, 0);
            } else {
                Scal *sc = scal_get(R, init->a->sym, init->a->text);
                if (sc) sc->nhdr++;
                scan_expr(R, init->b, 0);
            }
            scan_expr(R, s->b, 0);                       /* condition */
            {
                Scal *sc = scal_get(R, NULL, li.var);
                if (sc) { sc->nhdr++; if (sc->nread > 0) sc->nread--; }   /* condition read */
            }
        } else {
            if (s->a) { if (s->a->kind == N_VAR_DECL) scan_stmt(R, s->a); else scan_expr(R, s->a, 0); }
            scan_expr(R, s->b, 0);
            scan_expr(R, s->c, 0);
        }
        scan_stmt(R, s->d);
        break;
    }
    case N_RETURN:
        forbid(R, "a return statement");
        break;
    default:
        break;
    }
}

void region_scan(Region *R, Node *n, const char **vars, int nv)
{
    int i;
    memset(R, 0, sizeof *R);
    R->nv = nv;
    for (i = 0; i < nv && i < MAXV; i++) R->vars[i] = vars[i];
    scan_list(R, n);
}

/* Scan exactly one statement (a loop): its following siblings are NOT part of the region. */
void region_scan_stmt(Region *R, Node *stmt, const char **vars, int nv)
{
    int i;
    memset(R, 0, sizeof *R);
    R->nv = nv;
    for (i = 0; i < nv && i < MAXV; i++) R->vars[i] = vars[i];
    scan_stmt(R, stmt);
}

/* ================================================================== */
/* Pair analysis                                                       */
/* ================================================================== */

typedef struct { int fixed[MAXV]; long delta[MAXV]; } Dist;

/* 0 = no conflict possible, 1 = distance known, -1 = unknown.
 * delta[v] = (iteration of `a`) - (iteration of `b`) for loop variable v. */
static int ref_dist(const Ref *a, const Ref *b, int nv, Dist *d)
{
    int t, v;
    memset(d, 0, sizeof *d);
    for (t = 0; t < a->ndims; t++) {
        const Sub *sa = &a->sub[t], *sb = &b->sub[t];
        if (sa->kind == SUB_AFF && sb->kind == SUB_AFF) {
            int nz = 0, which = -1;
            for (v = 0; v < nv; v++) {
                if (sa->coef[v] != sb->coef[v]) return -1;      /* different slopes */
                if (sa->coef[v]) { nz++; which = v; }
            }
            if (nz == 0) {
                if (sa->k != sb->k) return 0;                   /* different constant elements */
                continue;
            }
            if (nz > 1) return -1;                              /* several variables in one subscript */
            {
                long diff = sb->k - sa->k, c = sa->coef[which];
                if (diff % c != 0) return 0;                    /* never equal */
                diff /= c;
                if (d->fixed[which] && d->delta[which] != diff) return 0;
                d->fixed[which] = 1;
                d->delta[which] = diff;
            }
        } else if (sa->kind == SUB_SYM && sb->kind == SUB_SYM) {
            if (strcmp(sa->key, sb->key) != 0) return -1;       /* may or may not be equal */
        } else {
            return -1;
        }
    }
    return 1;
}

static int lexpos(const int *w, int n)
{
    int k;
    for (k = 0; k < n; k++) {
        if (w[k] > 0) return 1;
        if (w[k] < 0) return 0;
    }
    return 0;                        /* all zero */
}

/* Is the permutation safe for this dependence (all sign patterns)? */
static int perm_ok_for_dist(const Dist *d, int nv, const int *perm)
{
    int s[MAXV], w[MAXV], pw[MAXV], free_idx[MAXV], nf = 0, v, m, total = 1, k;
    for (v = 0; v < nv; v++) if (!d->fixed[v]) free_idx[nf++] = v;
    for (m = 0; m < nf; m++) total *= 3;
    for (k = 0; k < total; k++) {
        int code = k, allzero = 1;
        for (v = 0; v < nv; v++)
            s[v] = d->fixed[v] ? (d->delta[v] > 0) - (d->delta[v] < 0) : 0;
        for (m = 0; m < nf; m++) { s[free_idx[m]] = (code % 3) - 1; code /= 3; }
        for (v = 0; v < nv; v++) if (s[v]) allzero = 0;
        if (allzero) continue;                       /* same iteration: order unchanged */
        for (v = 0; v < nv; v++) w[v] = lexpos(s, nv) ? s[v] : -s[v];
        for (v = 0; v < nv; v++) pw[v] = w[perm[v]];
        if (!lexpos(pw, nv)) return 0;
    }
    return 1;
}

static int is_fp_sym(const Scal *s)
{
    return s->sym && is_fp_type(s->sym->type);
}

/* ================================================================== */
/* Loop permutation                                                    */
/* ================================================================== */

int perm_legal(const Region *R, int nv, const int *perm, int fp_ok,
               char *why, size_t wn, char *evidence, size_t en, int *fp_reorder)
{
    int i, j, pairs = 0, scal_checked = 0;
    char pstr[40] = "";

    *fp_reorder = 0;
    if (R->forbidden) { snprintf(why, wn, "the loop body contains %s", R->bad); return 0; }
    if (R->ref_overflow || R->sc_overflow) { snprintf(why, wn, "too many array references or scalars to analyse"); return 0; }

    /* scalars: only private temporaries, loop variables and pure reductions may change */
    for (i = 0; i < R->nsc; i++) {
        const Scal *s = &R->sc[i];
        int written = s->nwrite_plain || s->nwrite_comp;
        if (s->is_private || s->nhdr) continue;
        if (!written) continue;
        scal_checked++;
        if (s->sym && s->sym->is_global) {
            snprintf(why, wn, "global scalar '%s' is written in the loop nest", s->name);
            return 0;
        }
        if (s->nwrite_plain || s->nread || s->comp_other) {
            snprintf(why, wn, "scalar '%s' carries a value between iterations (not a pure sum reduction)", s->name);
            return 0;
        }
        if (is_fp_sym(s)) {
            if (!fp_ok) {
                snprintf(why, wn, "reordering the floating-point sum into '%s' changes rounding "
                                  "(allow it with --fp-reassoc)", s->name);
                return 0;
            }
            *fp_reorder = 1;
        }
    }

    for (i = 0; i < R->nrefs; i++)
        for (j = i; j < R->nrefs; j++) {
            Dist d;
            int r;
            if (R->refs[i].sym != R->refs[j].sym) continue;
            if (!R->refs[i].write && !R->refs[j].write) continue;
            r = ref_dist(&R->refs[i], &R->refs[j], nv, &d);
            if (r == 0) continue;
            if (r < 0) {
                snprintf(why, wn, "cannot prove %s and %s independent (non-affine or mismatching subscripts)",
                         R->refs[i].key, R->refs[j].key);
                return 0;
            }
            pairs++;
            if (!perm_ok_for_dist(&d, nv, perm)) {
                snprintf(why, wn, "dependence between %s and %s would be reversed", R->refs[i].key, R->refs[j].key);
                return 0;
            }
        }
    for (i = 0; i < nv; i++) { char t[8]; snprintf(t, sizeof t, "%s%d", i ? "," : "", perm[i]); strcat(pstr, t); }
    snprintf(evidence, en, "checked %d conflicting reference pair(s) and %d scalar(s); every dependence stays "
             "lexicographically positive under loop order (%s)", pairs, scal_checked, pstr);
    return 1;
}

/* ================================================================== */
/* Loop fusion                                                         */
/* ================================================================== */

int fusion_legal(const Region *R1, const Region *R2, long step, int fp_ok,
                 char *why, size_t wn, char *evidence, size_t en)
{
    int i, j, pairs = 0;
    (void)fp_ok;

    if (R1->forbidden) { snprintf(why, wn, "the first loop contains %s", R1->bad); return 0; }
    if (R2->forbidden) { snprintf(why, wn, "the second loop contains %s", R2->bad); return 0; }
    if (R1->ref_overflow || R2->ref_overflow || R1->sc_overflow || R2->sc_overflow) {
        snprintf(why, wn, "too many array references or scalars to analyse");
        return 0;
    }

    /* a scalar written in one loop must not be touched by the other */
    for (i = 0; i < R1->nsc; i++) {
        const Scal *a = &R1->sc[i], *b = region_scalar(R2, a->name);
        int wa = a->nwrite_plain || a->nwrite_comp;
        if (a->is_private || a->nhdr) continue;
        if (a->sym && a->sym->is_global && wa) {
            snprintf(why, wn, "global scalar '%s' is written", a->name);
            return 0;
        }
        if (b && !b->is_private && !b->nhdr && (wa || b->nwrite_plain || b->nwrite_comp)) {
            snprintf(why, wn, "scalar '%s' is shared between the two loops", a->name);
            return 0;
        }
    }
    for (i = 0; i < R2->nsc; i++) {
        const Scal *b = &R2->sc[i];
        if (b->is_private || b->nhdr) continue;
        if (b->sym && b->sym->is_global && (b->nwrite_plain || b->nwrite_comp)) {
            snprintf(why, wn, "global scalar '%s' is written", b->name);
            return 0;
        }
    }

    for (i = 0; i < R1->nrefs; i++)
        for (j = 0; j < R2->nrefs; j++) {
            Dist d;
            int r;
            if (R1->refs[i].sym != R2->refs[j].sym) continue;
            if (!R1->refs[i].write && !R2->refs[j].write) continue;
            r = ref_dist(&R1->refs[i], &R2->refs[j], 1, &d);
            if (r == 0) continue;
            if (r < 0) {
                snprintf(why, wn, "cannot prove %s (loop 1) and %s (loop 2) independent",
                         R1->refs[i].key, R2->refs[j].key);
                return 0;
            }
            pairs++;
            /* delta = iteration in loop 1 minus iteration in loop 2 of the same element.
             * After fusion loop-1 work must not come after loop-2 work on that element. */
            if (!d.fixed[0]) {
                snprintf(why, wn, "%s and %s may touch the same element at unrelated iterations",
                         R1->refs[i].key, R2->refs[j].key);
                return 0;
            }
            if ((step > 0 && d.delta[0] > 0) || (step < 0 && d.delta[0] < 0)) {
                snprintf(why, wn, "loop 2 would read/write %s before loop 1 has produced/consumed it (%s)",
                         R2->refs[j].key, R1->refs[i].key);
                return 0;
            }
        }
    snprintf(evidence, en, "identical iteration space; checked %d array pair(s) across the two loops, "
             "every shared element is reached by loop 1 no later than by loop 2", pairs);
    return 1;
}
