/* ast.c - AST construction helpers and a tree dumper (--dump-ast) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "common.h"

Node *mk(NodeKind k, int line)
{
    Node *n = calloc(1, sizeof *n);
    if (!n) { fprintf(stderr, "out of memory\n"); exit(2); }
    n->kind = k;
    n->line = line;
    n->type = T_VOID;
    n->etype = T_INT;
    return n;
}

Node *mk_text(NodeKind k, int line, const char *text)
{
    Node *n = mk(k, line);
    n->text = ht_strdup(text);
    return n;
}

Node *mk_bin(const char *op, Node *a, Node *b, int line)
{
    Node *n = mk_text(N_BINOP, line, op);
    n->a = a;
    n->b = b;
    return n;
}

Node *list_append(Node *head, Node *tail)
{
    Node *p;
    if (!head) return tail;
    for (p = head; p->next; p = p->next) ;
    p->next = tail;
    return head;
}

const char *type_name(TypeKind t)
{
    switch (t) {
    case T_VOID:    return "void";
    case T_INT:     return "int";
    case T_FLOAT:   return "float";
    case T_DOUBLE:  return "double";
    case T_PTHREAD: return "pthread_t";
    case T_MUTEX:   return "pthread_mutex_t";
    }
    return "?";
}

long type_size(TypeKind t)
{
    switch (t) {
    case T_INT: case T_FLOAT: return 4;
    case T_DOUBLE: return 8;
    case T_PTHREAD: return 8;
    case T_MUTEX: return 40;
    default: return 1;
    }
}

int is_fp_type(TypeKind t)
{
    return t == T_FLOAT || t == T_DOUBLE;
}

static const char *kind_name(NodeKind k)
{
    static const char *names[] = {
        "Program", "VarDecl", "Function", "Param", "Block", "If", "For", "While",
        "Return", "ExprStmt", "Empty",
        "Assign", "BinOp", "UnOp", "AddrOf", "IncDec", "Index", "Call",
        "Id", "IntLit", "FloatLit", "StrLit", "Null"
    };
    return names[k];
}

static void dump_node(const Node *n, int depth)
{
    int i;
    for (i = 0; i < depth; i++) fputs("  ", stdout);
    printf("%s", kind_name(n->kind));
    switch (n->kind) {
    case N_VAR_DECL:
        printf(" %s %s", type_name(n->type), n->text);
        for (i = 0; i < n->ndims; i++) printf("[%ld]", n->dims[i]);
        break;
    case N_FUNC:
        printf(" %s%s %s", type_name(n->type), n->is_ptr ? " *" : "", n->text);
        break;
    case N_PARAM:
        printf(" %s%s %s", type_name(n->type), n->is_ptr ? " *" : "", n->text);
        break;
    case N_INT:   printf(" %ld", n->ival); break;
    case N_ASSIGN: case N_BINOP: case N_UNOP: case N_ID: case N_CALL:
    case N_FLOAT: case N_STR:
        printf(" %s", n->text);
        break;
    case N_INCDEC: printf(" %s%s", n->ival ? "prefix " : "postfix ", n->text); break;
    default: break;
    }
    printf("   (line %d)\n", n->line);
}

/* Dump a node list: each node, then its children a,b,c,d (each of which may
 * itself be a sibling list, e.g. statements of a block or call arguments). */
void ast_dump(const Node *n, int depth)
{
    for (; n; n = n->next) {
        dump_node(n, depth);
        if (n->a) ast_dump(n->a, depth + 1);
        if (n->b) ast_dump(n->b, depth + 1);
        if (n->c) ast_dump(n->c, depth + 1);
        if (n->d) ast_dump(n->d, depth + 1);
    }
}
