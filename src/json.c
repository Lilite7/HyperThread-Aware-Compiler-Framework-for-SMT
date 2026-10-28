/* json.c - bounded JSON parser and output buffer (see json.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include "json.h"

typedef struct {
    const char *p, *end;
    int nodes;
    char *err;
    size_t en;
    int failed;
} P;

static void fail(P *p, const char *msg)
{
    if (!p->failed) {
        snprintf(p->err, p->en, "%s", msg);
        p->failed = 1;
    }
}

static JVal *mkv(P *p, JType t)
{
    JVal *v;
    if (++p->nodes > JSON_MAX_NODES) { fail(p, "too many JSON values"); return NULL; }
    v = calloc(1, sizeof *v);
    if (!v) { fail(p, "out of memory"); return NULL; }
    v->t = t;
    return v;
}

static void skip_ws(P *p)
{
    while (p->p < p->end && (*p->p == ' ' || *p->p == '\t' || *p->p == '\n' || *p->p == '\r')) p->p++;
}

static int hex4(const char *s)
{
    int i, v = 0;
    for (i = 0; i < 4; i++) {
        int c = (unsigned char)s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static void put_utf8(char *out, size_t *n, unsigned cp)
{
    if (cp < 0x80) out[(*n)++] = (char)cp;
    else if (cp < 0x800) { out[(*n)++] = (char)(0xC0 | (cp >> 6)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
    else { out[(*n)++] = (char)(0xE0 | (cp >> 12)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[(*n)++] = (char)(0x80 | (cp & 0x3F)); }
}

static char *parse_string(P *p)
{
    char *buf;
    size_t n = 0, cap = 64;
    if (p->p >= p->end || *p->p != '"') { fail(p, "expected a string"); return NULL; }
    p->p++;
    buf = malloc(cap + 4);
    if (!buf) { fail(p, "out of memory"); return NULL; }
    while (p->p < p->end && *p->p != '"') {
        unsigned char c = (unsigned char)*p->p++;
        if (n + 4 >= cap) {
            if (cap >= JSON_MAX_STRING) { free(buf); fail(p, "string too long"); return NULL; }
            cap *= 2;
            buf = realloc(buf, cap + 4);
            if (!buf) { fail(p, "out of memory"); return NULL; }
        }
        if (c < 0x20) { free(buf); fail(p, "control character inside a string"); return NULL; }
        if (c != '\\') { buf[n++] = (char)c; continue; }
        if (p->p >= p->end) break;
        c = (unsigned char)*p->p++;
        switch (c) {
        case '"': buf[n++] = '"'; break;
        case '\\': buf[n++] = '\\'; break;
        case '/': buf[n++] = '/'; break;
        case 'b': buf[n++] = '\b'; break;
        case 'f': buf[n++] = '\f'; break;
        case 'n': buf[n++] = '\n'; break;
        case 'r': buf[n++] = '\r'; break;
        case 't': buf[n++] = '\t'; break;
        case 'u': {
            int cp;
            if (p->end - p->p < 4 || (cp = hex4(p->p)) < 0) { free(buf); fail(p, "bad \\u escape"); return NULL; }
            p->p += 4;
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';          /* surrogates: not needed here */
            put_utf8(buf, &n, (unsigned)cp);
            break;
        }
        default: free(buf); fail(p, "bad escape in a string"); return NULL;
        }
    }
    if (p->p >= p->end) { free(buf); fail(p, "unterminated string"); return NULL; }
    p->p++;                                                     /* closing quote */
    buf[n] = 0;
    return buf;
}

static JVal *parse_value(P *p, int depth);

static JVal *parse_array(P *p, int depth)
{
    JVal *v = mkv(p, J_ARR);
    int cap = 0;
    if (!v) return NULL;
    p->p++;                                                     /* [ */
    skip_ws(p);
    if (p->p < p->end && *p->p == ']') { p->p++; return v; }
    for (;;) {
        JVal *e = parse_value(p, depth + 1);
        if (!e) { json_free(v); return NULL; }
        if (v->n >= cap) { cap = cap ? cap * 2 : 8; v->items = realloc(v->items, (size_t)cap * sizeof *v->items); }
        v->items[v->n++] = e;
        skip_ws(p);
        if (p->p < p->end && *p->p == ',') { p->p++; continue; }
        if (p->p < p->end && *p->p == ']') { p->p++; return v; }
        fail(p, "expected ',' or ']' in an array");
        json_free(v);
        return NULL;
    }
}

static JVal *parse_object(P *p, int depth)
{
    JVal *v = mkv(p, J_OBJ);
    int cap = 0;
    if (!v) return NULL;
    p->p++;                                                     /* { */
    skip_ws(p);
    if (p->p < p->end && *p->p == '}') { p->p++; return v; }
    for (;;) {
        char *k;
        JVal *e;
        skip_ws(p);
        k = parse_string(p);
        if (!k) { json_free(v); return NULL; }
        skip_ws(p);
        if (p->p >= p->end || *p->p != ':') { free(k); fail(p, "expected ':' after a key"); json_free(v); return NULL; }
        p->p++;
        e = parse_value(p, depth + 1);
        if (!e) { free(k); json_free(v); return NULL; }
        if (v->n >= cap) {
            cap = cap ? cap * 2 : 8;
            v->items = realloc(v->items, (size_t)cap * sizeof *v->items);
            v->keys = realloc(v->keys, (size_t)cap * sizeof *v->keys);
        }
        v->keys[v->n] = k;
        v->items[v->n++] = e;
        skip_ws(p);
        if (p->p < p->end && *p->p == ',') { p->p++; continue; }
        if (p->p < p->end && *p->p == '}') { p->p++; return v; }
        fail(p, "expected ',' or '}' in an object");
        json_free(v);
        return NULL;
    }
}

static JVal *parse_value(P *p, int depth)
{
    JVal *v;
    if (depth > JSON_MAX_DEPTH) { fail(p, "JSON nested too deeply"); return NULL; }
    skip_ws(p);
    if (p->p >= p->end) { fail(p, "unexpected end of JSON"); return NULL; }
    switch (*p->p) {
    case '{': return parse_object(p, depth);
    case '[': return parse_array(p, depth);
    case '"': {
        char *s = parse_string(p);
        if (!s) return NULL;
        v = mkv(p, J_STR);
        if (!v) { free(s); return NULL; }
        v->s = s;
        return v;
    }
    case 't':
        if (p->end - p->p >= 4 && !strncmp(p->p, "true", 4)) { p->p += 4; v = mkv(p, J_BOOL); if (v) v->b = 1; return v; }
        break;
    case 'f':
        if (p->end - p->p >= 5 && !strncmp(p->p, "false", 5)) { p->p += 5; return mkv(p, J_BOOL); }
        break;
    case 'n':
        if (p->end - p->p >= 4 && !strncmp(p->p, "null", 4)) { p->p += 4; return mkv(p, J_NULL); }
        break;
    default:
        if (*p->p == '-' || isdigit((unsigned char)*p->p)) {
            char tmp[64];
            size_t n = 0;
            const char *q = p->p;
            while (q < p->end && n < sizeof tmp - 1 && (isdigit((unsigned char)*q) || *q == '-' || *q == '+' ||
                                                         *q == '.' || *q == 'e' || *q == 'E'))
                tmp[n++] = *q++;
            tmp[n] = 0;
            v = mkv(p, J_NUM);
            if (!v) return NULL;
            v->num = strtod(tmp, NULL);
            p->p = q;
            return v;
        }
        break;
    }
    fail(p, "unexpected character in JSON");
    return NULL;
}

JVal *json_parse(const char *text, size_t len, char *err, size_t en)
{
    P p;
    JVal *v;
    if (len > JSON_MAX_INPUT) { snprintf(err, en, "JSON input too large"); return NULL; }
    memset(&p, 0, sizeof p);
    p.p = text; p.end = text + len; p.err = err; p.en = en;
    if (en) err[0] = 0;
    v = parse_value(&p, 0);
    if (!v) { if (!p.failed) snprintf(err, en, "invalid JSON"); return NULL; }
    skip_ws(&p);
    if (p.p != p.end) { json_free(v); snprintf(err, en, "extra text after the JSON value"); return NULL; }
    return v;
}

JVal *json_extract(const char *text, size_t len, char *err, size_t en)
{
    size_t i, start = (size_t)-1;
    int depth = 0, in_str = 0, esc = 0;
    char open = 0;
    for (i = 0; i < len; i++) {
        char c = text[i];
        if (start == (size_t)-1) {
            if (c == '{' || c == '[') { start = i; open = c; depth = 1; }
            continue;
        }
        if (in_str) {
            if (esc) esc = 0;
            else if (c == '\\') esc = 1;
            else if (c == '"') in_str = 0;
            continue;
        }
        if (c == '"') in_str = 1;
        else if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') {
            if (--depth == 0) {
                (void)open;
                return json_parse(text + start, i - start + 1, err, en);
            }
        }
    }
    snprintf(err, en, start == (size_t)-1 ? "the reply contains no JSON object"
                                           : "the JSON in the reply is incomplete (cut off?)");
    return NULL;
}

void json_free(JVal *v)
{
    int i;
    if (!v) return;
    for (i = 0; i < v->n; i++) {
        json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->s);
    free(v);
}

const JVal *jget(const JVal *o, const char *key)
{
    int i;
    if (!o || o->t != J_OBJ) return NULL;
    for (i = 0; i < o->n; i++) if (!strcmp(o->keys[i], key)) return o->items[i];
    return NULL;
}

const JVal *jat(const JVal *a, int i)
{
    return (a && a->t == J_ARR && i >= 0 && i < a->n) ? a->items[i] : NULL;
}

int jlen(const JVal *v) { return (v && (v->t == J_ARR || v->t == J_OBJ)) ? v->n : 0; }
const char *jstr(const JVal *v, const char *def) { return (v && v->t == J_STR && v->s) ? v->s : def; }
double jnum(const JVal *v, double def) { return (v && v->t == J_NUM) ? v->num : def; }
int jbool(const JVal *v, int def) { return (v && v->t == J_BOOL) ? v->b : def; }

/* ---------- output buffer ---------- */

void jb_init(JBuf *b) { memset(b, 0, sizeof *b); }
void jb_free(JBuf *b) { free(b->p); memset(b, 0, sizeof *b); }

static void jb_grow(JBuf *b, size_t extra)
{
    if (b->oom) return;
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 256;
        char *np;
        while (nc < b->len + extra + 1) nc *= 2;
        np = realloc(b->p, nc);
        if (!np) { b->oom = 1; return; }
        b->p = np; b->cap = nc;
    }
}

void jb_puts(JBuf *b, const char *s)
{
    size_t n = strlen(s);
    jb_grow(b, n);
    if (b->oom) return;
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

void jb_putc(JBuf *b, char c)
{
    jb_grow(b, 1);
    if (b->oom) return;
    b->p[b->len++] = c;
    b->p[b->len] = 0;
}

void jb_str(JBuf *b, const char *s)
{
    jb_putc(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  jb_puts(b, "\\\""); break;
        case '\\': jb_puts(b, "\\\\"); break;
        case '\n': jb_puts(b, "\\n"); break;
        case '\r': jb_puts(b, "\\r"); break;
        case '\t': jb_puts(b, "\\t"); break;
        default:
            if (c < 0x20) { char t[8]; snprintf(t, sizeof t, "\\u%04x", c); jb_puts(b, t); }
            else jb_putc(b, (char)c);
        }
    }
    jb_putc(b, '"');
}

void jb_printf(JBuf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n < sizeof tmp) { jb_puts(b, tmp); return; }
    {
        char *big = malloc((size_t)n + 1);
        if (!big) { b->oom = 1; return; }
        va_start(ap, fmt);
        vsnprintf(big, (size_t)n + 1, fmt, ap);
        va_end(ap);
        jb_puts(b, big);
        free(big);
    }
}
