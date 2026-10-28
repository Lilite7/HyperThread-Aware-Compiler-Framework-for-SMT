/* astutil.c - deep clone, canonical keys, renaming */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "astutil.h"
#include "common.h"

Node *ast_clone(const Node *n)
{
    Node *c;
    if (!n) return NULL;
    c = malloc(sizeof *c);
    if (!c) { fprintf(stderr, "out of memory\n"); exit(2); }
    *c = *n;
    if (n->text) c->text = ht_strdup(n->text);
    c->sym = NULL;                       /* re-resolved by sema_run */
    c->a = ast_clone_list(n->a);
    c->b = ast_clone_list(n->b);
    c->c = ast_clone_list(n->c);
    c->d = ast_clone_list(n->d);
    c->next = NULL;
    return c;
}

Node *ast_clone_list(const Node *n)
{
    Node *head = NULL, **tail = &head;
    for (; n; n = n->next) {
        *tail = ast_clone(n);
        tail = &(*tail)->next;
    }
    return head;
}

Node *ast_id(const char *name, int line)
{
    return mk_text(N_ID, line, name);
}

/* ---------- canonical key ---------- */

typedef struct { char *buf; size_t cap, len; int overflow; } KB;

static void kput(KB *k, const char *s)
{
    size_t n = strlen(s);
    if (k->len + n + 1 > k->cap) { k->overflow = 1; return; }
    memcpy(k->buf + k->len, s, n + 1);
    k->len += n;
}

static void key_rec(const Node *n, KB *k)
{
    char tmp[48];
    const Node *a;
    if (!n) return;
    switch (n->kind) {
    case N_INT:   snprintf(tmp, sizeof tmp, "%ld", n->ival); kput(k, tmp); break;
    case N_FLOAT: case N_STR: kput(k, n->text); break;
    case N_NULL:  kput(k, "NULL"); break;
    case N_ID:    kput(k, n->text); break;
    case N_UNOP:  kput(k, "("); kput(k, n->text); key_rec(n->a, k); kput(k, ")"); break;
    case N_ADDR:  kput(k, "(&"); key_rec(n->a, k); kput(k, ")"); break;
    case N_INCDEC:
        kput(k, "(");
        if (n->ival) kput(k, n->text);
        key_rec(n->a, k);
        if (!n->ival) kput(k, n->text);
        kput(k, ")");
        break;
    case N_INDEX: key_rec(n->a, k); kput(k, "["); key_rec(n->b, k); kput(k, "]"); break;
    case N_BINOP: case N_ASSIGN:
        kput(k, "("); key_rec(n->a, k); kput(k, n->text); key_rec(n->b, k); kput(k, ")"); break;
    case N_CALL:
        kput(k, n->text); kput(k, "(");
        for (a = n->a; a; a = a->next) { key_rec(a, k); kput(k, ","); }
        kput(k, ")"); break;
    default: kput(k, "?"); break;
    }
}

void ast_key(const Node *n, char *buf, size_t cap)
{
    KB k;
    k.buf = buf; k.cap = cap; k.len = 0; k.overflow = 0;
    if (cap) buf[0] = 0;
    key_rec(n, &k);
    if (k.overflow)                       /* never compare equal to anything else */
        snprintf(buf, cap, "<long:%p>", (const void *)n);
}

int ast_same(const Node *a, const Node *b)
{
    char x[256], y[256];
    ast_key(a, x, sizeof x);
    ast_key(b, y, sizeof y);
    return strcmp(x, y) == 0;
}

void ast_rename(Node *n, const char *from, const char *to)
{
    Node *c;
    int i;
    Node *kids[4];
    if (!n) return;
    if (n->kind == N_ID && !strcmp(n->text, from)) { free(n->text); n->text = ht_strdup(to); }
    if (n->kind == N_VAR_DECL && !strcmp(n->text, from)) { free(n->text); n->text = ht_strdup(to); }
    kids[0] = n->a; kids[1] = n->b; kids[2] = n->c; kids[3] = n->d;
    for (i = 0; i < 4; i++)
        for (c = kids[i]; c; c = c->next) ast_rename(c, from, to);
}

int ast_assigns(const Node *n, const char *name)
{
    const Node *c;
    const Node *kids[4];
    int i;
    if (!n) return 0;
    if ((n->kind == N_ASSIGN || n->kind == N_INCDEC) && n->a && n->a->kind == N_ID &&
        !strcmp(n->a->text, name))
        return 1;
    if (n->kind == N_VAR_DECL && !strcmp(n->text, name)) return 1;
    kids[0] = n->a; kids[1] = n->b; kids[2] = n->c; kids[3] = n->d;
    for (i = 0; i < 4; i++)
        for (c = kids[i]; c; c = c->next)
            if (ast_assigns(c, name)) return 1;
    return 0;
}
