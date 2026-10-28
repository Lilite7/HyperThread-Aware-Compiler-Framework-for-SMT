/* json_test.c - unit tests for the bounded JSON parser (src/json.c).
 * Model output is untrusted, so the interesting cases are the hostile ones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json.h"

static int fails = 0, runs = 0;
#define CHECK(cond, msg) do { runs++; if (!(cond)) { fails++; printf("  FAIL  %s\n", msg); } } while (0)

static JVal *P(const char *s, char *err) { return json_parse(s, strlen(s), err, 200); }
static JVal *X(const char *s, char *err) { return json_extract(s, strlen(s), err, 200); }

int main(void)
{
    char err[200];
    JVal *v;
    int i;

    v = P("{\"a\":[1,2.5,\"x\",true,null],\"b\":{\"c\":\"d\"}}", err);
    CHECK(v && v->t == J_OBJ, "parses a nested document");
    CHECK(jlen(jget(v, "a")) == 5 && jnum(jat(jget(v, "a"), 1), 0) == 2.5, "array contents");
    CHECK(!strcmp(jstr(jget(jget(v, "b"), "c"), ""), "d"), "nested object access");
    json_free(v);

    v = P("\"line\\nbreak \\u0041 \\\"q\\\"\"", err);
    CHECK(v && !strcmp(v->s, "line\nbreak A \"q\""), "string escapes and \\u");
    json_free(v);

    CHECK(P("{\"a\":1,}", err) == NULL, "trailing comma rejected");
    CHECK(P("{\"a\":", err) == NULL, "truncated object rejected");
    CHECK(P("\"abc", err) == NULL, "unterminated string rejected");
    CHECK(P("{\"a\":1} extra", err) == NULL, "text after the value rejected");
    CHECK(P("{a:1}", err) == NULL, "unquoted key rejected");
    CHECK(P("nul", err) == NULL, "bare word rejected");
    CHECK(P("\"bad\\q\"", err) == NULL, "unknown escape rejected");
    CHECK(P("\"tab\there\"", err) == NULL, "raw control character in a string rejected");

    {   /* nesting deeper than the limit */
        char deep[200];
        memset(deep, '[', 60); memset(deep + 60, ']', 60); deep[120] = 0;
        CHECK(P(deep, err) == NULL && strstr(err, "deep"), "excessive nesting rejected");
    }
    {   /* more values than the limit */
        size_t n = (size_t)JSON_MAX_NODES + 50, k;
        char *big = malloc(n * 2 + 4);
        big[0] = '[';
        for (k = 0; k < n; k++) { big[1 + k * 2] = '1'; big[2 + k * 2] = ','; }
        big[n * 2] = '1'; big[n * 2 + 1] = ']'; big[n * 2 + 2] = 0;
        CHECK(P(big, err) == NULL && strstr(err, "too many"), "too many values rejected");
        free(big);
    }
    {   /* a single enormous string */
        size_t n = JSON_MAX_STRING + 100;
        char *big = malloc(n + 4);
        big[0] = '"'; memset(big + 1, 'a', n); big[n + 1] = '"'; big[n + 2] = 0;
        CHECK(P(big, err) == NULL && strstr(err, "too long"), "oversized string rejected");
        free(big);
    }

    /* extraction from prose / Markdown fences */
    v = X("Here you go:\n```json\n{\"proposals\":[{\"t\":\"a}b\"}]}\n```\nbye", err);
    CHECK(v && jlen(jget(v, "proposals")) == 1, "extracts JSON from a Markdown fence (brace inside a string)");
    json_free(v);
    CHECK(X("no json here at all", err) == NULL && strstr(err, "no JSON"), "prose without JSON reported");
    CHECK(X("{\"proposals\":[{\"a\":1", err) == NULL && strstr(err, "incomplete"), "cut-off JSON reported as incomplete");
    v = X("[1,2,3]", err);
    CHECK(v && v->t == J_ARR && v->n == 3, "top-level array");
    json_free(v);

    /* output buffer */
    {
        JBuf b;
        jb_init(&b);
        jb_str(&b, "a\"b\\c\nd\x01");
        CHECK(!strcmp(b.p, "\"a\\\"b\\\\c\\nd\\u0001\""), "JSON string escaping");
        for (i = 0; i < 5000; i++) jb_puts(&b, "0123456789");
        CHECK(b.len > 50000 && !b.oom, "buffer grows");
        jb_free(&b);
    }

    printf("json unit tests: %d checks, %d failed\n", runs, fails);
    return fails ? 1 : 0;
}
