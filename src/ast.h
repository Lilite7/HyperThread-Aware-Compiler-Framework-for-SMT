/* ast.h - Abstract Syntax Tree for the HT-Compass mini-C language */
#ifndef HT_AST_H
#define HT_AST_H

typedef enum { T_VOID, T_INT, T_FLOAT, T_DOUBLE, T_PTHREAD, T_MUTEX } TypeKind;

typedef enum {
    /* top level / statements */
    N_PROGRAM, N_VAR_DECL, N_FUNC, N_PARAM, N_BLOCK, N_IF, N_FOR, N_WHILE,
    N_RETURN, N_EXPR_STMT, N_EMPTY,
    /* expressions */
    N_ASSIGN, N_BINOP, N_UNOP, N_ADDR, N_INCDEC, N_INDEX, N_CALL,
    N_ID, N_INT, N_FLOAT, N_STR, N_NULL
} NodeKind;

struct Symbol;

/*
 * One generic node type keeps the compiler small. Field usage per kind:
 *
 *  N_PROGRAM   a = list of top-level declarations / functions
 *  N_VAR_DECL  text = name, type, ndims, dims[], a = initializer (opt)
 *  N_FUNC      text = name, type (+is_ptr for 'void *'), a = params, b = body
 *  N_PARAM     text = name, type, is_ptr
 *  N_BLOCK     a = statement list
 *  N_IF        a = cond, b = then, c = else
 *  N_FOR       a = init (expr or N_VAR_DECL), b = cond, c = update, d = body
 *  N_WHILE     a = cond, b = body
 *  N_RETURN    a = expr (opt)
 *  N_EXPR_STMT a = expr
 *  N_ASSIGN    text = "=", "+=", ... a = lhs, b = rhs
 *  N_BINOP     text = operator, a, b
 *  N_UNOP      text = "-" or "!", a
 *  N_ADDR      a = operand            ('&x')
 *  N_INCDEC    text = "++"/"--", ival = 1 if prefix, a = operand
 *  N_INDEX     a = base, b = index    ('a[i]' ; 'b[i][j]' nests)
 *  N_CALL      text = callee, a = argument list
 *  N_ID        text = name
 *  N_INT       ival     N_FLOAT text = lexeme, fval     N_STR text = lexeme
 */
typedef struct Node {
    NodeKind kind;
    int line;
    char *text;
    long ival;
    double fval;
    TypeKind type;           /* declared type            */
    int is_ptr;              /* 'void *'                 */
    int ndims;               /* array dimensions (0..2)  */
    long dims[2];
    TypeKind etype;          /* expression type (sema)   */
    struct Symbol *sym;      /* resolved symbol (sema)   */
    struct Node *a, *b, *c, *d;
    struct Node *next;       /* sibling in a list        */
} Node;

Node *mk(NodeKind k, int line);
Node *mk_text(NodeKind k, int line, const char *text);
Node *mk_bin(const char *op, Node *a, Node *b, int line);
Node *list_append(Node *head, Node *tail);

const char *type_name(TypeKind t);
long type_size(TypeKind t);
int is_fp_type(TypeKind t);

void ast_dump(const Node *n, int depth);

extern Node *g_program;

#endif
