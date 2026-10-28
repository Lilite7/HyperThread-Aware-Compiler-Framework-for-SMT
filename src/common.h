/* common.h - shared diagnostics and global state for HT-Compass */
#ifndef HT_COMMON_H
#define HT_COMMON_H

#include <stddef.h>

#define HT_VERSION "1.0.0"

extern const char *g_filename;      /* input file name                     */
extern int g_errors;                /* number of (non-fatal) errors so far */
extern int g_quiet;                 /* suppress diagnostics (internal re-analysis) */
extern char *g_warnings[64];        /* collected warnings (for JSON)       */
extern int g_nwarnings;

/* '#include' / '#define' lines seen by the lexer, re-emitted by codegen */
extern char *g_preamble[256];
extern int g_npreamble;

/* Non-fatal error: "<kind> at line N: message" (e.g. "Semantic error") */
void ht_error(const char *kind, int line, const char *fmt, ...);

/* Fatal: "Unsupported construct at line N: what" and exit(1) */
void ht_unsupported(int line, const char *what);

/* Warning: printed to stderr and stored for the JSON report */
void ht_warn(const char *fmt, ...);

char *ht_strdup(const char *s);

#endif
