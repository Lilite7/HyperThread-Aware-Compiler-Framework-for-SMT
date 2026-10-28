/* advisor.h - what the local model is asked, and how its answer is checked.
 *
 * The model proposes; this module only turns the reply into a list of PROPOSALS that
 * refer to stable REGIONS of the program.  Nothing in a proposal is trusted: the optimizer
 * later checks each one with dependence analysis, differential testing and timing.
 */
#ifndef HT_ADVISOR_H
#define HT_ADVISOR_H

#include "opt.h"
#include "json.h"

#define MAX_REGIONS   96
#define MAX_PROPOSALS 16

typedef struct {
    char id[64];            /* "main/L3": the 3rd loop of main, in source order         */
    char func[48];
    int  line;
    int  depth;             /* 1 = outermost loop                                       */
    int  is_for;
    char var[64];
    long trips;
    int  trips_known;
    char parent[64];        /* id of the enclosing loop, or ""                          */
    Node *node;
} LoopRegion;

enum { PS_OK = 0, PS_UNSUPPORTED, PS_INVALID, PS_DUPLICATE };

typedef struct {
    char transformation[48];    /* as the model wrote it                                */
    char canonical[40];         /* normalised name                                      */
    int  tr;                    /* index in the registry, -1 = no implementation        */
    char region[64];
    int  param;                 /* unroll factor / tile size, 0 = default               */
    char explanation[420], benefit[220], preconditions[320], risks[320];
    int  priority;
    int  status;
    char note[200];
} Proposal;

int  advisor_regions(Node *program, LoopRegion *out, int max);

/* system + user messages; returns 1 on success */
int  advisor_build_prompt(const char *source_text, Node *program, const HTContext *ctx,
                          const LoopRegion *r, int nr, JBuf *system, JBuf *user);

/* Validate a model reply.  Returns the number of proposals (>= 0, including unsupported /
 * invalid / duplicate ones, each flagged), or -1 if the reply is not usable JSON (err set). */
int  advisor_parse(const char *reply, const LoopRegion *regions, int nr, Proposal *out, int max,
                   char *err, size_t en, char notes[][200], int *nnotes, int max_notes);

const char *advisor_status_word(int status);

#endif
