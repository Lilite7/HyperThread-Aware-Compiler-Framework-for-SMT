/* sema.c - scoped symbol table, expression typing, pthread call validation.
 *
 * After this pass every expression node carries `etype`, every identifier
 * node carries `sym`, and every call node carries `sym` (user functions).
 * The later analyses rely on these annotations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sema.h"
#include "common.h"

#define MAX_SCOPES 64

static Symbol *scopes[MAX_SCOPES];   /* scopes[0] = globals */
static int level = 0;
static Symbol *funcs = NULL;
static Node *cur_func = NULL;

/* ---------- library function table ---------- */

typedef struct { const char *name; LibKind kind; TypeKind ret; } LibEntry;
static const LibEntry libtab[] = {
    {"printf", LIB_IO, T_INT},   {"puts", LIB_IO, T_INT},
    {"sqrt", LIB_MATH, T_DOUBLE}, {"sin", LIB_MATH, T_DOUBLE},
    {"cos", LIB_MATH, T_DOUBLE},  {"tan", LIB_MATH, T_DOUBLE},
    {"exp", LIB_MATH, T_DOUBLE},  {"log", LIB_MATH, T_DOUBLE},
    {"pow", LIB_MATH, T_DOUBLE},  {"fabs", LIB_MATH, T_DOUBLE},
    {"floor", LIB_MATH, T_DOUBLE}, {"ceil", LIB_MATH, T_DOUBLE},
    {"rand", LIB_MISC, T_INT},    {"srand", LIB_MISC, T_VOID},
    {"pthread_create", LIB_PTHREAD_CREATE, T_INT},
    {"pthread_join", LIB_PTHREAD_JOIN, T_INT},
    {"pthread_mutex_lock", LIB_MUTEX_LOCK, T_INT},
    {"pthread_mutex_unlock", LIB_MUTEX_UNLOCK, T_INT},
    {NULL, LIB_NONE, T_VOID}
};

static const LibEntry *lib_find(const char *name)
{
    int i;
    for (i = 0; libtab[i].name; i++)
        if (!strcmp(libtab[i].name, name)) return &libtab[i];
    return NULL;
}

LibKind lib_kind(const char *name)
{
    const LibEntry *e = lib_find(name);
    return e ? e->kind : LIB_NONE;
}

/* ---------- symbol table ---------- */

static Symbol *new_sym(const char *name, SymKind k)
{
    Symbol *s = calloc(1, sizeof *s);
    s->name = ht_strdup(name);
    s->kind = k;
    return s;
}

static void push_scope(void)
{
    if (level + 1 >= MAX_SCOPES) { fprintf(stderr, "scope nesting too deep\n"); exit(2); }
    scopes[++level] = NULL;
}

static void pop_scope(void) { level--; }

static Symbol *lookup_var(const char *name)
{
    int l;
    Symbol *s;
    for (l = level; l >= 0; l--)
        for (s = scopes[l]; s; s = s->next)
            if (!strcmp(s->name, name)) return s;
    return NULL;
}

Symbol *sema_find_func(const char *name)
{
    Symbol *s;
    for (s = funcs; s; s = s->next)
        if (!strcmp(s->name, name)) return s;
    return NULL;
}

static Symbol *declare_var(Node *d, int is_param)
{
    Symbol *s;
    for (s = scopes[level]; s; s = s->next)
        if (!strcmp(s->name, d->text)) {
            ht_error("Semantic error", d->line, "redeclaration of '%s'", d->text);
            return s;
        }
    s = new_sym(d->text, S_VAR);
    s->type = d->type;
    s->is_ptr = d->is_ptr;
    s->ndims = d->ndims;
    s->dims[0] = d->dims[0];
    s->dims[1] = d->dims[1];
    s->elem_size = type_size(d->type);
    s->bytes = s->elem_size;
    if (d->ndims >= 1) s->bytes *= d->dims[0];
    if (d->ndims == 2) s->bytes *= d->dims[1];
    s->is_global = (level == 0);
    s->is_param = is_param;
    s->decl = d;
    s->next = scopes[level];
    scopes[level] = s;
    d->sym = s;
    return s;
}

/* ---------- helpers ---------- */

static TypeKind arith_type(TypeKind a, TypeKind b)
{
    if (a == T_DOUBLE || b == T_DOUBLE) return T_DOUBLE;
    if (a == T_FLOAT || b == T_FLOAT) return T_FLOAT;
    return T_INT;
}

static int is_lvalue(const Node *n)
{
    return n->kind == N_ID || n->kind == N_INDEX;
}

static int count_args(const Node *a)
{
    int n = 0;
    for (; a; a = a->next) n++;
    return n;
}

static TypeKind sema_expr(Node *e);

/* Resolve 'a[i][j]'-style chains; returns element type. */
static TypeKind sema_index(Node *e)
{
    Node *idx[2];
    Node *base = e;
    int n = 0, i;
    Symbol *s;

    while (base->kind == N_INDEX) {
        if (n == 2) {
            ht_error("Semantic error", e->line, "too many array subscripts");
            return T_INT;
        }
        idx[n++] = base->b;
        base = base->a;
    }
    if (base->kind != N_ID) {
        ht_error("Semantic error", e->line, "only named arrays can be subscripted");
        return T_INT;
    }
    s = lookup_var(base->text);
    if (!s) {
        ht_error("Semantic error", base->line, "undeclared identifier '%s'", base->text);
        return T_INT;
    }
    base->sym = s;
    base->etype = s->type;
    if (s->ndims == 0) {
        ht_error("Semantic error", e->line, "'%s' is not an array", s->name);
        return s->type;
    }
    if (n != s->ndims)
        ht_error("Semantic error", e->line,
                 "array '%s' has %d dimension(s) but %d subscript(s) given",
                 s->name, s->ndims, n);
    for (i = 0; i < n; i++)
        if (is_fp_type(sema_expr(idx[i])))
            ht_error("Semantic error", idx[i]->line, "array subscript must be an integer");
    /* annotate the intermediate N_INDEX nodes */
    for (base = e; base->kind == N_INDEX; base = base->a) base->etype = s->type;
    return s->type;
}

static void sema_args(Node *args)
{
    for (; args; args = args->next) sema_expr(args);
}

static const char *handle_base(const Node *n)
{
    while (n && (n->kind == N_INDEX || n->kind == N_ADDR)) n = n->a;
    return (n && n->kind == N_ID) ? n->text : NULL;
}

static TypeKind sema_call(Node *e)
{
    const LibEntry *lib = lib_find(e->text);
    Node *a0 = e->a, *a1 = a0 ? a0->next : NULL, *a2 = a1 ? a1->next : NULL;
    Node *a3 = a2 ? a2->next : NULL;
    int n = count_args(e->a);

    if (lib) {
        switch (lib->kind) {
        case LIB_PTHREAD_CREATE: {
            Symbol *f;
            if (n != 4) {
                ht_error("Semantic error", e->line, "pthread_create expects 4 arguments");
                break;
            }
            if (a0->kind != N_ADDR || !handle_base(a0))
                ht_error("Semantic error", e->line,
                         "pthread_create: first argument must be '&thread' or '&threads[i]'");
            else
                sema_expr(a0);
            if (a1->kind != N_NULL)
                ht_error("Semantic error", e->line,
                         "pthread_create: attribute argument must be NULL "
                         "(HT-Compass installs its own affinity attribute)");
            if (a2->kind != N_ID) {
                ht_error("Semantic error", e->line,
                         "pthread_create: third argument must be a thread function name");
            } else {
                f = sema_find_func(a2->text);
                if (!f)
                    ht_error("Semantic error", a2->line, "undefined thread function '%s'", a2->text);
                else {
                    if (!(f->is_ptr && f->type == T_VOID))
                        ht_error("Semantic error", a2->line,
                                 "thread function '%s' must have signature 'void *%s(void *arg)'",
                                 f->name, f->name);
                    if (f->nparams > 1)
                        ht_error("Semantic error", a2->line,
                                 "thread function '%s' must take one 'void *' parameter", f->name);
                    a2->sym = f;
                    e->sym = f;
                }
            }
            sema_expr(a3);
            break;
        }
        case LIB_PTHREAD_JOIN:
            if (n != 2) {
                ht_error("Semantic error", e->line, "pthread_join expects 2 arguments");
                break;
            }
            if (!(a0->kind == N_ID || a0->kind == N_INDEX))
                ht_error("Semantic error", e->line,
                         "pthread_join: first argument must be 'thread' or 'threads[i]'");
            else if (sema_expr(a0), a0->etype != T_PTHREAD)
                ht_error("Semantic error", e->line, "pthread_join: argument is not a pthread_t");
            if (a1->kind != N_NULL)
                ht_error("Semantic error", e->line, "pthread_join: second argument must be NULL");
            break;
        case LIB_MUTEX_LOCK:
        case LIB_MUTEX_UNLOCK:
            if (n != 1 || a0->kind != N_ADDR || !handle_base(a0))
                ht_error("Semantic error", e->line, "%s expects '&mutex'", e->text);
            else
                sema_expr(a0);
            break;
        case LIB_IO:
            if (n < 1 || a0->kind != N_STR)
                ht_error("Semantic error", e->line, "%s: first argument must be a string literal", e->text);
            sema_args(e->a);
            break;
        default:
            if (n < 1)
                ht_error("Semantic error", e->line, "%s: missing argument", e->text);
            sema_args(e->a);
            break;
        }
        e->etype = lib->ret;
        return e->etype;
    }

    /* user-defined function */
    {
        Symbol *f = sema_find_func(e->text);
        if (!f) {
            ht_error("Semantic error", e->line, "call to undefined function '%s'", e->text);
            sema_args(e->a);
            return T_INT;
        }
        if (n != f->nparams)
            ht_error("Semantic error", e->line,
                     "function '%s' expects %d argument(s) but %d given", f->name, f->nparams, n);
        sema_args(e->a);
        e->sym = f;
        e->etype = f->type;
        return e->etype;
    }
}

static TypeKind sema_expr(Node *e)
{
    TypeKind ta, tb;
    Symbol *s;

    if (!e) return T_INT;
    switch (e->kind) {
    case N_INT:  e->etype = T_INT; break;
    case N_FLOAT: {
        size_t l = strlen(e->text);
        char c = e->text[l - 1];
        e->etype = (c == 'f' || c == 'F') ? T_FLOAT : T_DOUBLE;
        break;
    }
    case N_STR: case N_NULL:
        e->etype = T_INT;
        break;
    case N_ID:
        s = lookup_var(e->text);
        if (!s) {
            if (!strcmp(e->text, "PTHREAD_MUTEX_INITIALIZER")) { e->etype = T_INT; break; }
            if (sema_find_func(e->text))
                ht_error("Semantic error", e->line,
                         "function '%s' used as a value (function pointers are unsupported)", e->text);
            else
                ht_error("Semantic error", e->line, "undeclared identifier '%s'", e->text);
            break;
        }
        e->sym = s;
        e->etype = s->type;
        if (s->ndims > 0)
            ht_error("Semantic error", e->line, "array '%s' used without subscript", s->name);
        break;
    case N_INDEX:
        e->etype = sema_index(e);
        break;
    case N_ADDR:
        if (!is_lvalue(e->a))
            ht_error("Semantic error", e->line, "'&' needs a variable or array element");
        sema_expr(e->a);
        e->etype = T_INT;
        break;
    case N_UNOP:
        ta = sema_expr(e->a);
        e->etype = (e->text[0] == '!') ? T_INT : ta;
        break;
    case N_INCDEC:
        if (!is_lvalue(e->a))
            ht_error("Semantic error", e->line, "'%s' needs a variable", e->text);
        e->etype = sema_expr(e->a);
        break;
    case N_BINOP:
        ta = sema_expr(e->a);
        tb = sema_expr(e->b);
        if (!strcmp(e->text, "%") && (is_fp_type(ta) || is_fp_type(tb)))
            ht_error("Semantic error", e->line, "operator '%%' needs integer operands");
        if (!strcmp(e->text, "+") || !strcmp(e->text, "-") ||
            !strcmp(e->text, "*") || !strcmp(e->text, "/") || !strcmp(e->text, "%"))
            e->etype = arith_type(ta, tb);
        else
            e->etype = T_INT;
        break;
    case N_ASSIGN:
        if (!is_lvalue(e->a))
            ht_error("Semantic error", e->line, "left side of '%s' is not assignable", e->text);
        ta = sema_expr(e->a);
        sema_expr(e->b);
        e->etype = ta;
        break;
    case N_CALL:
        sema_call(e);
        break;
    default:
        break;
    }
    return e->etype;
}

/* ---------- statements ---------- */

static void sema_stmt(Node *s);

static void sema_list(Node *l)
{
    for (; l; l = l->next) sema_stmt(l);
}

static void sema_decl(Node *d)
{
    if (d->a) sema_expr(d->a);       /* initializer first: 'int x = x;' is an error */
    if (d->ndims > 0 && d->a)
        ht_error("Semantic error", d->line, "array initializers are unsupported");
    declare_var(d, 0);
}

static void sema_stmt(Node *s)
{
    if (!s) return;
    switch (s->kind) {
    case N_BLOCK:
        push_scope();
        sema_list(s->a);
        pop_scope();
        break;
    case N_VAR_DECL:
        sema_decl(s);
        break;
    case N_EXPR_STMT:
        sema_expr(s->a);
        break;
    case N_IF:
        sema_expr(s->a);
        sema_stmt(s->b);
        sema_stmt(s->c);
        break;
    case N_FOR:
        push_scope();
        if (s->a) {
            if (s->a->kind == N_VAR_DECL) sema_decl(s->a);
            else sema_expr(s->a);
        }
        sema_expr(s->b);
        sema_expr(s->c);
        sema_stmt(s->d);
        pop_scope();
        break;
    case N_WHILE:
        sema_expr(s->a);
        sema_stmt(s->b);
        break;
    case N_RETURN:
        if (s->a) sema_expr(s->a);
        if (cur_func) {
            int ptr_ret = cur_func->is_ptr;
            int void_ret = (cur_func->type == T_VOID && !ptr_ret);
            if (void_ret && s->a)
                ht_error("Semantic error", s->line, "void function '%s' returns a value", cur_func->text);
            if (!void_ret && !ptr_ret && !s->a)
                ht_error("Semantic error", s->line, "function '%s' must return a value", cur_func->text);
            if (ptr_ret && s->a && s->a->kind != N_NULL)
                ht_error("Semantic error", s->line,
                         "thread function '%s' must 'return NULL;'", cur_func->text);
        }
        break;
    default:
        break;
    }
}

/* ---------- entry point ---------- */

void sema_run(Node *program)
{
    Node *n, *p;
    Symbol *f;
    int seen_main = 0;

    level = 0;
    scopes[0] = NULL;
    funcs = NULL;                 /* sema_run may be called again on a transformed AST */
    cur_func = NULL;

    /* pass 1: register all functions (so calls may precede definitions) */
    for (n = program->a; n; n = n->next) {
        if (n->kind != N_FUNC) continue;
        if (lib_find(n->text) || sema_find_func(n->text)) {
            ht_error("Semantic error", n->line, "redefinition of function '%s'", n->text);
            continue;
        }
        f = new_sym(n->text, S_FUNC);
        f->type = n->type;
        f->is_ptr = n->is_ptr;
        f->decl = n;
        for (p = n->a; p; p = p->next) f->nparams++;
        f->next = funcs;
        funcs = f;
        n->sym = f;
        if (!strcmp(n->text, "main")) seen_main = 1;
    }
    if (!seen_main)
        ht_error("Semantic error", 1, "program has no 'main' function");

    /* pass 2: globals and function bodies in source order */
    for (n = program->a; n; n = n->next) {
        if (n->kind == N_VAR_DECL) {
            sema_decl(n);
        } else if (n->kind == N_FUNC) {
            cur_func = n;
            push_scope();
            for (p = n->a; p; p = p->next) declare_var(p, 1);
            /* body block shares the parameter scope level for simplicity */
            sema_list(n->b->a);
            pop_scope();
            cur_func = NULL;
        }
    }
}
