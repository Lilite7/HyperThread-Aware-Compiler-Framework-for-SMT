/* depend.h - array reference collection and dependence tests for loop transformations
 *
 * Scope of the analysis (stated precisely because soundness depends on it):
 *   - subscripts that are affine in the loop variables with constant coefficients
 *     (a[i+1][j-1], a[2*i]) are analysed exactly via distance vectors;
 *   - everything else (indirect a[b[i]], symbolic coefficients a[i*n+j],
 *     calls, I/O, locks) makes the answer UNKNOWN and the transformation is refused.
 */
#ifndef HT_DEPEND_H
#define HT_DEPEND_H

#include "ast.h"
#include "sema.h"

#define MAXV    3
#define MAXREFS 64
#define MAXSC   40

typedef enum { SUB_AFF, SUB_SYM, SUB_OTHER } SubKind;

typedef struct {
    SubKind kind;
    long k;                 /* constant term                          */
    long coef[MAXV];        /* coefficient of each nest loop variable */
    char key[96];           /* canonical text of the subscript        */
} Sub;

typedef struct {
    Symbol *sym;
    int write;
    int ndims;
    Sub sub[2];
    char key[128];          /* canonical text of the whole reference  */
    Node *node;             /* the outermost N_INDEX node             */
} Ref;

typedef struct {
    char name[64];
    Symbol *sym;
    int nwrite_plain;       /* x = ...                                     */
    int nwrite_comp;        /* x += ..., x -= ..., x++, x--                */
    int nread;              /* reads other than as the target of x += ...  */
    int comp_other;         /* compound writes that are not +/- (x *= ...) */
    int nhdr;               /* assigned by the header of a canonical loop  */
    int is_private;         /* declared inside the region                  */
} Scal;

typedef struct {
    int forbidden;          /* call / I/O / lock / return inside region    */
    char bad[96];           /* what it was                                 */
    Ref refs[MAXREFS];
    int nrefs, ref_overflow;
    Scal sc[MAXSC];
    int nsc, sc_overflow;
    char locals[8][64];     /* arrays declared inside the region           */
    int nlocals;
    const char *vars[MAXV]; /* nest loop variables used to classify subscripts */
    int nv;
} Region;

/* Scan `n` (a statement, list of statements, or whole loop). */
void region_scan(Region *R, Node *n, const char **vars, int nv);
void region_scan_stmt(Region *R, Node *stmt, const char **vars, int nv);   /* one statement only */

/* 1 = legal, 0 = refused (reason in why).  evidence describes the proof. */
int  perm_legal(const Region *R, int nv, const int *perm, int fp_ok,
                char *why, size_t wn, char *evidence, size_t en, int *fp_reorder);
int  fusion_legal(const Region *R1, const Region *R2, long step, int fp_ok,
                  char *why, size_t wn, char *evidence, size_t en);

const Scal *region_scalar(const Region *R, const char *name);

#endif
