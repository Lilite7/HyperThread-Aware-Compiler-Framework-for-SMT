/* json.h - a small, bounded JSON parser and a growable output buffer.
 *
 * Everything that comes back from the language model is untrusted input, so the
 * parser enforces hard limits (depth, node count, string length, total size)
 * instead of trusting the sender.
 */
#ifndef HT_JSON_H
#define HT_JSON_H

#include <stddef.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal {
    JType t;
    double num;
    int b;
    char *s;                    /* J_STR */
    struct JVal **items;        /* J_ARR elements / J_OBJ values */
    char **keys;                /* J_OBJ keys */
    int n;
} JVal;

#define JSON_MAX_DEPTH   24
#define JSON_MAX_NODES   20000
#define JSON_MAX_STRING  (256 * 1024)
#define JSON_MAX_INPUT   (2 * 1024 * 1024)

/* Parse exactly one JSON value (trailing whitespace allowed).  NULL + message on error. */
JVal *json_parse(const char *text, size_t len, char *err, size_t en);

/* The model may wrap its answer in prose or a Markdown fence.  Find the first complete
 * top-level {...} or [...] in `text` and parse that.  The span is chosen by bracket
 * matching that understands strings; nothing is evaluated. */
JVal *json_extract(const char *text, size_t len, char *err, size_t en);

void json_free(JVal *v);

const JVal *jget(const JVal *obj, const char *key);       /* NULL if absent / not an object */
const JVal *jat(const JVal *arr, int i);
int         jlen(const JVal *v);                          /* array or object size, else 0 */
const char *jstr(const JVal *v, const char *def);
double      jnum(const JVal *v, double def);
int         jbool(const JVal *v, int def);

/* ---------- output buffer ---------- */
typedef struct { char *p; size_t len, cap; int oom; } JBuf;

void jb_init(JBuf *b);
void jb_free(JBuf *b);
void jb_puts(JBuf *b, const char *s);
void jb_putc(JBuf *b, char c);
void jb_str(JBuf *b, const char *s);                      /* "..." with JSON escaping */
void jb_printf(JBuf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif
