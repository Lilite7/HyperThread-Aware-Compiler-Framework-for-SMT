%option noyywrap yylineno nounput noinput
%{
/* lexer.l - Flex scanner for the HT-Compass mini-C subset.
 *
 * Besides tokenising, the scanner
 *   - records '#include' and '#define' lines (re-emitted by the code generator),
 *   - expands numeric object-like macros ('#define N 1000') so that array
 *     sizes and loop bounds are compile-time constants for the analyses,
 *   - rejects every construct outside the supported subset with
 *       "Unsupported construct at line N: <what>".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ast.h"
#include "common.h"
#include "parser.tab.h"

#define YY_USER_ACTION yylloc.first_line = yylloc.last_line = yylineno;

typedef struct { char name[64]; int is_float; long iv; char text[64]; } Macro;
static Macro macros[256];
static int nmacros = 0;

static Macro *find_macro(const char *name)
{
    int i;
    for (i = 0; i < nmacros; i++)
        if (!strcmp(macros[i].name, name)) return &macros[i];
    return NULL;
}

static void add_preamble(const char *line)
{
    char buf[512];
    const char *p = line;
    size_t n;
    while (*p == ' ' || *p == '\t') p++;
    strncpy(buf, p, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\r' || buf[n-1] == ' ' || buf[n-1] == '\t')) buf[--n] = 0;
    if (g_npreamble < 256) g_preamble[g_npreamble++] = ht_strdup(buf);
}

static void handle_define(const char *line)
{
    const char *p = strstr(line, "define") + 6;
    char name[64], val[128], *end, *c;
    int n = 0;
    long iv;
    double dv;
    Macro *m;

    while (*p == ' ' || *p == '\t') p++;
    while ((isalnum((unsigned char)*p) || *p == '_') && n < 63) name[n++] = *p++;
    name[n] = 0;
    while (*p == ' ' || *p == '\t') p++;
    strncpy(val, p, sizeof val - 1);
    val[sizeof val - 1] = 0;
    if ((c = strstr(val, "//")) != NULL) *c = 0;
    if ((c = strstr(val, "/*")) != NULL) *c = 0;
    n = (int)strlen(val);
    while (n > 0 && isspace((unsigned char)val[n-1])) val[--n] = 0;

    if (nmacros >= 256) ht_unsupported(yylineno, "too many #define macros");
    m = &macros[nmacros];
    strncpy(m->name, name, 63);
    m->name[63] = 0;
    iv = strtol(val, &end, 0);
    if (end != val && *end == 0) {
        m->is_float = 0;
        m->iv = iv;
    } else {
        dv = strtod(val, &end);
        if (end != val && (*end == 0 || ((*end == 'f' || *end == 'F') && end[1] == 0))) {
            m->is_float = 1;
            strncpy(m->text, val, 63);
            m->text[63] = 0;
            (void)dv;
        } else {
            char msg[160];
            snprintf(msg, sizeof msg,
                     "#define %s: only plain numeric macros are supported", name);
            ht_unsupported(yylineno, msg);
        }
    }
    nmacros++;
    add_preamble(line);
}

static void bad_type(const char *kw)
{
    char msg[96];
    snprintf(msg, sizeof msg, "type '%s' (only int, float, double, void are supported)", kw);
    ht_unsupported(yylineno, msg);
}

static void bad_op(const char *op)
{
    char msg[96];
    snprintf(msg, sizeof msg, "operator '%s'", op);
    ht_unsupported(yylineno, msg);
}
%}

ID      [A-Za-z_][A-Za-z0-9_]*
EXP     [eE][+-]?[0-9]+

%%

^[ \t]*"#"[ \t]*"include"[^\n]*      { add_preamble(yytext); }
^[ \t]*"#"[ \t]*"define"[ \t]+{ID}[ \t]+[^\n]*   { handle_define(yytext); }
^[ \t]*"#"[^\n]*                     {
        char msg[200];
        snprintf(msg, sizeof msg, "preprocessor directive '%.60s' (only #include and "
                 "'#define NAME number' are supported)", yytext);
        ht_unsupported(yylineno, msg);
    }

"//"[^\n]*                           { /* line comment */ }
"/*"([^*]|\*+[^*/])*\*+"/"           { /* block comment */ }
"/*"                                 { ht_unsupported(yylineno, "unterminated comment"); }

"int"        { return INT; }
"float"      { return FLOAT; }
"double"     { return DOUBLE; }
"void"       { return VOID; }
"if"         { return IF; }
"else"       { return ELSE; }
"for"        { return FOR; }
"while"      { return WHILE; }
"return"     { return RETURN; }
"NULL"       { return NULLT; }
"pthread_t"        { return PTHREAD_T; }
"pthread_mutex_t"  { return MUTEX_T; }

"switch"     { ht_unsupported(yylineno, "switch statement"); }
"case"       { ht_unsupported(yylineno, "case label"); }
"default"    { ht_unsupported(yylineno, "default label"); }
"do"         { ht_unsupported(yylineno, "do-while loop"); }
"break"      { ht_unsupported(yylineno, "break statement"); }
"continue"   { ht_unsupported(yylineno, "continue statement"); }
"goto"       { ht_unsupported(yylineno, "goto statement"); }
"struct"     { ht_unsupported(yylineno, "struct declaration"); }
"union"      { ht_unsupported(yylineno, "union declaration"); }
"enum"       { ht_unsupported(yylineno, "enum declaration"); }
"typedef"    { ht_unsupported(yylineno, "typedef"); }
"sizeof"     { ht_unsupported(yylineno, "sizeof operator"); }
"char"       { bad_type("char"); }
"short"      { bad_type("short"); }
"long"       { bad_type("long"); }
"unsigned"   { bad_type("unsigned"); }
"signed"     { bad_type("signed"); }
"static"     { ht_unsupported(yylineno, "storage class 'static'"); }
"extern"     { ht_unsupported(yylineno, "storage class 'extern'"); }
"register"   { ht_unsupported(yylineno, "storage class 'register'"); }
"const"      { ht_unsupported(yylineno, "qualifier 'const'"); }
"volatile"   { ht_unsupported(yylineno, "qualifier 'volatile'"); }
"inline"     { ht_unsupported(yylineno, "inline function"); }

"+="   { return ADDEQ; }
"-="   { return SUBEQ; }
"*="   { return MULEQ; }
"/="   { return DIVEQ; }
"++"   { return INC; }
"--"   { return DEC; }
"&&"   { return AND; }
"||"   { return OR; }
"=="   { return EQ; }
"!="   { return NE; }
"<="   { return LE; }
">="   { return GE; }

"->"   { bad_op("->"); }
"<<"   { bad_op("<<"); }
">>"   { bad_op(">>"); }
"%="   { bad_op("%="); }
"|"    { bad_op("|"); }
"^"    { bad_op("^"); }
"~"    { bad_op("~"); }
"?"    { bad_op("?:"); }
":"    { bad_op(":"); }
"."    { bad_op("."); }

[-+*/%<>=!&()\[\]{};,]   { return yytext[0]; }

0[xX][0-9a-fA-F]+                 { yylval.ival = strtol(yytext, NULL, 16); return T_INTLIT; }
[0-9]+                            { yylval.ival = strtol(yytext, NULL, 0);  return T_INTLIT; }
[0-9]+"."[0-9]*({EXP})?[fF]?      { yylval.str = ht_strdup(yytext); return T_FLOATLIT; }
"."[0-9]+({EXP})?[fF]?            { yylval.str = ht_strdup(yytext); return T_FLOATLIT; }
[0-9]+{EXP}[fF]?                  { yylval.str = ht_strdup(yytext); return T_FLOATLIT; }

\"([^\\\"\n]|\\.)*\"              { yylval.str = ht_strdup(yytext); return T_STRING; }
\'([^\\\'\n]|\\.)+\'              { ht_unsupported(yylineno, "character literal"); }

{ID}   {
        Macro *m = find_macro(yytext);
        if (m) {
            if (m->is_float) { yylval.str = ht_strdup(m->text); return T_FLOATLIT; }
            yylval.ival = m->iv;
            return T_INTLIT;
        }
        yylval.str = ht_strdup(yytext);
        return T_ID;
    }

[ \t\r\n]+   { /* whitespace */ }

.      {
        char msg[64];
        snprintf(msg, sizeof msg, "illegal character '%c'", yytext[0]);
        ht_unsupported(yylineno, msg);
    }

%%
