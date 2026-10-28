/* astutil.h - AST cloning, structural keys and renaming used by the optimizer */
#ifndef HT_ASTUTIL_H
#define HT_ASTUTIL_H

#include <stddef.h>
#include "ast.h"

Node *ast_clone(const Node *n);                 /* deep copy of one node (not its siblings) */
Node *ast_clone_list(const Node *n);            /* deep copy of a sibling list              */
void  ast_key(const Node *n, char *buf, size_t cap);   /* canonical text of an expression   */
int   ast_same(const Node *a, const Node *b);   /* structurally equal expressions           */
void  ast_rename(Node *n, const char *from, const char *to);   /* rename identifiers        */
int   ast_assigns(const Node *n, const char *name);            /* is `name` written in n?   */
Node *ast_id(const char *name, int line);

#endif
