/* sema.h - symbol table and semantic analysis */
#ifndef HT_SEMA_H
#define HT_SEMA_H

#include "ast.h"

typedef enum { S_VAR, S_FUNC } SymKind;

typedef struct Symbol {
    char *name;
    SymKind kind;
    TypeKind type;           /* variable type / function return type        */
    int is_ptr;              /* 'void *' parameter or return                */
    int ndims;               /* arrays: 0 (scalar), 1 or 2                  */
    long dims[2];
    long elem_size;          /* bytes per element                           */
    long bytes;              /* total bytes (arrays)                        */
    int is_global;
    int is_param;
    int nparams;             /* functions                                   */
    Node *decl;              /* N_VAR_DECL / N_PARAM / N_FUNC               */
    struct Symbol *next;
} Symbol;

/* Library functions the mini-C accepts without a definition. */
typedef enum {
    LIB_NONE = 0,            /* not a library function (user-defined)       */
    LIB_IO,                  /* printf, puts                                */
    LIB_MATH,                /* sqrt, sin, cos, ...                         */
    LIB_MISC,                /* rand, srand                                 */
    LIB_PTHREAD_CREATE, LIB_PTHREAD_JOIN,
    LIB_MUTEX_LOCK, LIB_MUTEX_UNLOCK
} LibKind;

LibKind lib_kind(const char *name);

/* Build symbol tables, type every expression, report semantic errors.
 * Errors are counted in g_errors. */
void sema_run(Node *program);

Symbol *sema_find_func(const char *name);

#endif
