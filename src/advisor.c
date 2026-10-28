/* advisor.c - prompt construction and strict validation of the model's reply */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "advisor.h"
#include "astutil.h"
#include "depend.h"
#include "common.h"

/* ================================================================== */
/* Regions                                                             */
/* ================================================================== */

typedef struct { LoopRegion *out; int n, max; const char *func; Node *fn; int counter; } RW;

static void rw_list(RW *w, Node *l, int depth, const char *parent);

static void rw_loop(RW *w, Node *s, int depth, const char *parent)
{
    char id[64];
    LoopRegion *r = NULL;
    LoopInfo li;
    snprintf(id, sizeof id, "%s/L%d", w->func, ++w->counter);
    if (w->n < w->max) {
        r = &w->out[w->n++];
        memset(r, 0, sizeof *r);
        snprintf(r->id, sizeof r->id, "%s", id);
        snprintf(r->func, sizeof r->func, "%s", w->func);
        r->line = s->line;
        r->depth = depth;
        r->is_for = s->kind == N_FOR;
        r->node = s;
        snprintf(r->parent, sizeof r->parent, "%s", parent);
        if (s->kind == N_FOR && loop_info(s, &li)) {
            snprintf(r->var, sizeof r->var, "%s", li.var);
            r->trips = li.trips;
            r->trips_known = li.trips_known;
        }
    }
    rw_list(w, s->kind == N_FOR ? s->d : s->b, depth + 1, id);
}

static void rw_list(RW *w, Node *l, int depth, const char *parent)
{
    for (; l; l = l->next) {
        switch (l->kind) {
        case N_FOR: case N_WHILE: rw_loop(w, l, depth, parent); break;
        case N_BLOCK: rw_list(w, l->a, depth, parent); break;
        case N_IF: rw_list(w, l->b, depth, parent); if (l->c) rw_list(w, l->c, depth, parent); break;
        default: break;
        }
    }
}

int advisor_regions(Node *program, LoopRegion *out, int max)
{
    RW w;
    Node *f;
    memset(&w, 0, sizeof w);
    w.out = out; w.max = max;
    for (f = program->a; f; f = f->next) {
        if (f->kind != N_FUNC) continue;
        w.func = f->text; w.fn = f; w.counter = 0;
        rw_list(&w, f->b, 1, "");
    }
    return w.n;
}

/* ---- per-loop access hints: how far each array reference moves per iteration ---- */

static void collect_refs(Node *n, Node **refs, int *nr, int max)
{
    for (; n; n = n->next) {
        if (n->kind == N_INDEX) {
            char k[128];
            int i, dup = 0;
            ast_key(n, k, sizeof k);
            for (i = 0; i < *nr; i++) {
                char k2[128];
                ast_key(refs[i], k2, sizeof k2);
                if (!strcmp(k, k2)) dup = 1;
            }
            if (!dup && *nr < max) refs[(*nr)++] = n;
            continue;                                   /* subscripts are part of the reference */
        }
        collect_refs(n->a, refs, nr, max);
        collect_refs(n->b, refs, nr, max);
        collect_refs(n->c, refs, nr, max);
        collect_refs(n->d, refs, nr, max);
    }
}

static void region_hints(const LoopRegion *r, char *out, size_t n)
{
    Node *refs[8];
    int nr = 0, i, len = 0;
    out[0] = 0;
    if (!r->is_for || !r->var[0]) return;
    collect_refs(r->node->d, refs, &nr, 8);
    for (i = 0; i < nr && len < (int)n - 60; i++) {
        Node *idx[2], *base = refs[i];
        int k = 0;
        long c0 = 0, c1 = 0, stride;
        char key[128], word[48];
        int ok;
        while (base->kind == N_INDEX && k < 2) { idx[k++] = base->b; base = base->a; }
        if (base->kind != N_ID || !base->sym) continue;
        ast_key(refs[i], key, sizeof key);
        if (k == 2) ok = linear_coeff(idx[1], r->var, &c0) && linear_coeff(idx[0], r->var, &c1);
        else        ok = linear_coeff(idx[0], r->var, &c0);
        if (!ok || has_array_ref(idx[0]) || (k == 2 && has_array_ref(idx[1]))) snprintf(word, sizeof word, "irregular");
        else {
            stride = (k == 2) ? c0 * base->sym->dims[1] + c1 : c0;
            if (stride < 0) stride = -stride;
            if (stride == 0) snprintf(word, sizeof word, "same element each iteration");
            else if (stride == 1) snprintf(word, sizeof word, "stride 1 (sequential)");
            else snprintf(word, sizeof word, "stride %ld elements", stride);
        }
        len += snprintf(out + len, n - (size_t)len, "%s%s: %s", i ? "; " : "", key, word);
    }
}

/* ================================================================== */
/* Prompt                                                              */
/* ================================================================== */

static const char *SYSTEM_TEXT =
"You are an optimization advisor inside a C compiler. The compiler accepts a small restricted subset of C "
"(int/float/double, 1-D and 2-D global arrays, for/while/if, pthreads). You do NOT write code. You look at "
"the program and the compiler's analysis and propose which optimizations are worth trying and WHERE.\n"
"\n"
"The compiler will independently prove each proposal safe, test it, and time it; unsafe or useless "
"proposals are simply rejected, so propose what you genuinely believe will make this program faster.\n"
"\n"
"Reply with ONE JSON object and nothing else, in exactly this shape:\n"
"{\"proposals\":[{\"transformation\":\"<name>\",\"region\":\"<region id>\",\"parameters\":{},"
"\"explanation\":\"<why this program benefits>\",\"expected_benefit\":\"<what gets faster and roughly why>\","
"\"preconditions\":[\"<what must hold>\"],\"risks\":[\"<what could go wrong>\"],\"priority\":1}]}\n"
"\n"
"Rules:\n"
"- \"region\" must be copied exactly from the REGIONS list (for example main/L3). Never invent a region.\n"
"- \"transformation\" is one of the names listed under TRANSFORMATIONS.\n"
"- \"priority\" 1 is the most promising. Give at most 8 proposals. Propose nothing if nothing would help.\n"
"- Base every proposal on the actual loops and access strides shown. Do not repeat the same proposal.\n";

static void text_listing(JBuf *u, const char *src)
{
    int line = 1, shown = 0;
    const char *p = src;
    size_t used = 0;
    jb_puts(u, "PROGRAM (line numbers on the left)\n");
    while (*p && shown < 260 && used < 24000) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        char num[16];
        snprintf(num, sizeof num, "%4d  ", line++);
        jb_puts(u, num);
        { size_t k; for (k = 0; k < l && k < 200; k++) jb_putc(u, p[k]); }
        jb_putc(u, '\n');
        used += l + 6;
        shown++;
        p = e ? e + 1 : p + l;
    }
    if (*p) jb_puts(u, "... (listing truncated)\n");
}

int advisor_build_prompt(const char *source_text, Node *program, const HTContext *ctx,
                         const LoopRegion *r, int nr, JBuf *system, JBuf *user)
{
    int i, t;
    char hints[400];
    (void)program;
    jb_puts(system, SYSTEM_TEXT);

    text_listing(user, source_text ? source_text : "");

    jb_puts(user, "\nREGIONS (each one loop; ids are stable)\n");
    for (i = 0; i < nr; i++) {
        region_hints(&r[i], hints, sizeof hints);
        jb_printf(user, "%s  line %d  %s", r[i].id, r[i].line, r[i].is_for ? "for" : "while");
        if (r[i].var[0]) jb_printf(user, " %s", r[i].var);
        if (r[i].trips_known) jb_printf(user, ", %ld iterations", r[i].trips);
        jb_printf(user, ", depth %d", r[i].depth);
        if (r[i].parent[0]) jb_printf(user, ", inside %s", r[i].parent);
        if (hints[0]) jb_printf(user, "\n    accesses per iteration of this loop: %s", hints);
        jb_putc(user, '\n');
    }

    if (ctx && ctx->nthreads > 0) {
        jb_puts(user, "\nTHREADS (compiler's resource estimate)\n");
        for (t = 0; t < ctx->nthreads && t < 12; t++) {
            const Profile *p = &ctx->threads[t].prof;
            jb_printf(user, "%s: %s, about %.3g operations, memory demand %.2f, %.0f strided accesses\n",
                      ctx->threads[t].name, p->klass, p->total_ops, p->n_bw, p->strided_acc);
        }
    }

    jb_puts(user, "\nTRANSFORMATIONS (the compiler can apply these; the region is where to apply it)\n");
    for (t = 0; t < g_ntransforms; t++) {
        jb_printf(user, "- %s: region = %s", g_transforms[t].name, g_transforms[t].region_doc);
        if (g_transforms[t].param_name)
            jb_printf(user, "; optional parameter \"%s\" (default %d)", g_transforms[t].param_name, g_transforms[t].param_default);
        jb_putc(user, '\n');
    }
    jb_puts(user,
"\nOTHER STRATEGIES you may also propose if they genuinely fit (the compiler will report that it has no "
"implementation for them): tiling, strength_reduction, redundant_computation_elimination, parallelization.\n"
"\nTASK: Which of these would make this program faster, and in which regions? Reply with the JSON object only.\n");
    return !system->oom && !user->oom;
}

/* ================================================================== */
/* Reply validation                                                    */
/* ================================================================== */

static void canon(const char *in, char *out, size_t n)
{
    size_t k = 0;
    int last_us = 1;
    for (; *in && k + 1 < n; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c)) { out[k++] = (char)tolower(c); last_us = 0; }
        else if (!last_us) { out[k++] = '_'; last_us = 1; }
    }
    while (k > 0 && out[k - 1] == '_') k--;
    out[k] = 0;
}

typedef struct { const char *name, *aliases; } KnownStrategy;
static const KnownStrategy KNOWN_UNSUPPORTED[] = {
    { "tiling", "loop_tiling blocking loop_blocking cache_blocking tile loop_tile" },
    { "strength_reduction", "operator_strength_reduction induction_variable_strength_reduction" },
    { "redundant_computation_elimination", "cse common_subexpression_elimination redundant_computation redundant_expression_elimination" },
    { "parallelization", "parallelize parallelisation loop_parallelization thread_parallelism openmp vectorization simd" },
    { NULL, NULL }
};

static int in_aliases(const char *aliases, const char *c)
{
    size_t n = strlen(c);
    while (aliases && *aliases) {
        const char *sp = strchr(aliases, ' ');
        size_t l = sp ? (size_t)(sp - aliases) : strlen(aliases);
        if (l == n && !strncmp(aliases, c, n)) return 1;
        aliases = sp ? sp + 1 : aliases + l;
    }
    return 0;
}

static void clean_copy(char *dst, size_t n, const char *src)
{
    size_t k = 0;
    for (; *src && k + 1 < n; src++) {
        unsigned char c = (unsigned char)*src;
        dst[k++] = (c < 0x20 || c == 0x7f) ? ' ' : (char)c;
    }
    dst[k] = 0;
}

/* a string, or an array of strings joined with "; " */
static void text_of(const JVal *v, char *dst, size_t n)
{
    dst[0] = 0;
    if (!v) return;
    if (v->t == J_STR) { clean_copy(dst, n, v->s); return; }
    if (v->t == J_ARR) {
        int i;
        size_t used = 0;
        for (i = 0; i < v->n && i < 6; i++) {
            const char *s = jstr(v->items[i], NULL);
            if (!s) continue;
            used += (size_t)snprintf(dst + used, n - used, "%s%.120s", used ? "; " : "", s);
            if (used + 2 >= n) break;
        }
        { size_t k; for (k = 0; dst[k]; k++) if ((unsigned char)dst[k] < 0x20) dst[k] = ' '; }
    }
}

static const JVal *first_of(const JVal *o, const char *a, const char *b, const char *c, const char *d)
{
    const JVal *v;
    if ((v = jget(o, a))) return v;
    if (b && (v = jget(o, b))) return v;
    if (c && (v = jget(o, c))) return v;
    if (d && (v = jget(o, d))) return v;
    return NULL;
}

static void note(char notes[][200], int *nn, int max, const char *fmt, int idx, const char *a)
{
    if (*nn < max) { snprintf(notes[*nn], 200, fmt, idx, a ? a : ""); (*nn)++; }
}

const char *advisor_status_word(int s)
{
    switch (s) {
    case PS_OK: return "ok";
    case PS_UNSUPPORTED: return "unsupported";
    case PS_INVALID: return "invalid";
    default: return "duplicate";
    }
}

int advisor_parse(const char *reply, const LoopRegion *regions, int nr, Proposal *out, int max,
                  char *err, size_t en, char notes[][200], int *nnotes, int max_notes)
{
    JVal *root;
    const JVal *list;
    int i, n = 0;
    char jerr[160];

    *nnotes = 0;
    root = json_extract(reply, strlen(reply), jerr, sizeof jerr);
    if (!root) { snprintf(err, en, "%s", jerr); return -1; }
    list = (root->t == J_ARR) ? root : jget(root, "proposals");
    if (!list || list->t != J_ARR) {
        if (root->t == J_OBJ && jget(root, "transformation")) { /* a single proposal object */ list = NULL; }
        else {
            snprintf(err, en, "the reply is JSON but has no \"proposals\" list");
            json_free(root);
            return -1;
        }
    }
    {
        int count = list ? list->n : 1;
        for (i = 0; i < count && n < max; i++) {
            const JVal *it = list ? list->items[i] : root;
            Proposal *p = &out[n];
            const char *name, *reg;
            char cn[64];
            int k, ri;
            const JVal *par;

            memset(p, 0, sizeof *p);
            p->tr = -1;
            if (!it || it->t != J_OBJ) { note(notes, nnotes, max_notes, "proposal %d is not a JSON object%s", i + 1, ""); continue; }
            name = jstr(first_of(it, "transformation", "name", "type", "optimization"), "");
            reg = jstr(first_of(it, "region", "target_region", "region_id", "loop"), "");
            clean_copy(p->transformation, sizeof p->transformation, name);
            clean_copy(p->region, sizeof p->region, reg);
            text_of(first_of(it, "explanation", "reason", "rationale", "description"), p->explanation, sizeof p->explanation);
            text_of(first_of(it, "expected_benefit", "benefit", "expected_performance_benefit", NULL), p->benefit, sizeof p->benefit);
            text_of(first_of(it, "preconditions", "required_preconditions", NULL, NULL), p->preconditions, sizeof p->preconditions);
            text_of(first_of(it, "risks", "correctness_risks", "potential_correctness_risks", NULL), p->risks, sizeof p->risks);
            p->priority = (int)jnum(jget(it, "priority"), i + 1);

            par = jget(it, "parameters");
            {
                const JVal *pv = par ? first_of(par, "factor", "unroll_factor", "tile_size", "size") : NULL;
                if (!pv) pv = first_of(it, "factor", "unroll_factor", "tile_size", NULL);
                if (pv && pv->t == J_NUM && pv->num >= 1 && pv->num <= 4096) p->param = (int)pv->num;
            }

            canon(name, cn, sizeof cn);
            snprintf(p->canonical, sizeof p->canonical, "%.39s", cn[0] ? cn : "unnamed");
            if (!cn[0]) { p->status = PS_INVALID; snprintf(p->note, sizeof p->note, "the proposal names no transformation"); n++; continue; }

            p->tr = transform_by_name(cn);
            if (p->tr >= 0) snprintf(p->canonical, sizeof p->canonical, "%.39s", g_transforms[p->tr].name);
            else {
                for (k = 0; KNOWN_UNSUPPORTED[k].name; k++)
                    if (!strcmp(KNOWN_UNSUPPORTED[k].name, cn) || in_aliases(KNOWN_UNSUPPORTED[k].aliases, cn)) {
                        snprintf(p->canonical, sizeof p->canonical, "%.39s", KNOWN_UNSUPPORTED[k].name);
                        break;
                    }
            }

            if (!p->region[0]) { p->status = PS_INVALID; snprintf(p->note, sizeof p->note, "the proposal names no region"); n++; continue; }
            for (ri = 0; ri < nr; ri++) if (!strcmp(regions[ri].id, p->region)) break;
            if (ri == nr) {
                p->status = PS_INVALID;
                snprintf(p->note, sizeof p->note, "unknown region '%.60s' (not in the REGIONS list)", p->region);
                n++;
                continue;
            }
            for (k = 0; k < n; k++)
                if (out[k].status != PS_INVALID && !strcmp(out[k].canonical, p->canonical) &&
                    !strcmp(out[k].region, p->region) && out[k].param == p->param) break;
            if (k < n) {
                p->status = PS_DUPLICATE;
                snprintf(p->note, sizeof p->note, "duplicate of proposal %d (same transformation, region and parameter)", k + 1);
                n++;
                continue;
            }
            if (p->tr < 0) {
                p->status = PS_UNSUPPORTED;
                snprintf(p->note, sizeof p->note, "proposed, but this compiler has no implementation of '%s'", p->canonical);
            } else if (g_transforms[p->tr].param_name == NULL && p->param) {
                snprintf(p->note, sizeof p->note, "parameter ignored: %s takes none", p->canonical);
                p->param = 0;
            }
            n++;
        }
    }
    json_free(root);
    return n;
}
