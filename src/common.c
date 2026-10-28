/* common.c - diagnostics helpers */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "common.h"

const char *g_filename = "<stdin>";
int g_errors = 0;
int g_quiet = 0;
char *g_warnings[64];
int g_nwarnings = 0;
char *g_preamble[256];
int g_npreamble = 0;

char *ht_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    memcpy(p, s, n);
    return p;
}

void ht_error(const char *kind, int line, const char *fmt, ...)
{
    va_list ap;
    if (!g_quiet) {
        fprintf(stderr, "%s at line %d: ", kind, line);
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
    }
    g_errors++;
}

void ht_unsupported(int line, const char *what)
{
    fprintf(stderr, "Unsupported construct at line %d: %s\n", line, what);
    exit(1);
}

void ht_warn(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (g_quiet) return;
    fprintf(stderr, "Warning: %s\n", buf);
    if (g_nwarnings < 64)
        g_warnings[g_nwarnings++] = ht_strdup(buf);
}
