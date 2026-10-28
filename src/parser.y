%{
/* parser.y - Bison grammar for the HT-Compass mini-C subset.
 * Builds the AST (ast.h). Constructs outside the subset that are still
 * syntactically recognisable (casts, '*p') are rejected with a clear message.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "common.h"

int yylex(void);
void yyerror(const char *msg);
extern int yylineno;
extern char *yytext;

Node *g_program = NULL;

static Node *mk_decl(const char *name, int line)
{
    Node *n = mk_text(N_VAR_DECL, line, name);
    n->type = T_INT;
    return n;
}
%}

%locations

%union {
    long  ival;
    char *str;
    Node *node;
    int   type;
}

%token <str>  T_ID T_FLOATLIT T_STRING
%token <ival> T_INTLIT
%token INT FLOAT DOUBLE VOID PTHREAD_T MUTEX_T
%token IF ELSE FOR WHILE RETURN NULLT
%token ADDEQ SUBEQ MULEQ DIVEQ INC DEC AND OR EQ NE LE GE

%type <type> type
%type <str>  assign_op
%type <node> external_list external function decl declarator_list declarator
%type <node> param_list_opt param_list param
%type <node> compound stmt_list stmt for_init expr_opt
%type <node> expr assign_expr logical_or logical_and equality relational
%type <node> additive multiplicative unary postfix primary
%type <node> arg_list_opt arg_list

%precedence LOWER_THAN_ELSE
%precedence ELSE

%%

program
    : external_list      { g_program = mk(N_PROGRAM, 1); g_program->a = $1; }
    ;

external_list
    : /* empty */                 { $$ = NULL; }
    | external_list external      { $$ = list_append($1, $2); }
    ;

external
    : decl                        { $$ = $1; }
    | function                    { $$ = $1; }
    ;

/* ---------- declarations ---------- */

type
    : INT        { $$ = T_INT; }
    | FLOAT      { $$ = T_FLOAT; }
    | DOUBLE     { $$ = T_DOUBLE; }
    | VOID       { $$ = T_VOID; }
    | PTHREAD_T  { $$ = T_PTHREAD; }
    | MUTEX_T    { $$ = T_MUTEX; }
    ;

decl
    : type declarator_list ';'
        {
            Node *n;
            if ($1 == T_VOID)
                ht_unsupported(@1.first_line, "variable of type void");
            for (n = $2; n; n = n->next) n->type = (TypeKind)$1;
            $$ = $2;
        }
    ;

declarator_list
    : declarator                         { $$ = $1; }
    | declarator_list ',' declarator     { $$ = list_append($1, $3); }
    ;

declarator
    : T_ID
        { $$ = mk_decl($1, @1.first_line); }
    | T_ID '[' T_INTLIT ']'
        {
            if ($3 <= 0) ht_unsupported(@1.first_line, "array size must be positive");
            $$ = mk_decl($1, @1.first_line);
            $$->ndims = 1; $$->dims[0] = $3;
        }
    | T_ID '[' T_INTLIT ']' '[' T_INTLIT ']'
        {
            if ($3 <= 0 || $6 <= 0) ht_unsupported(@1.first_line, "array size must be positive");
            $$ = mk_decl($1, @1.first_line);
            $$->ndims = 2; $$->dims[0] = $3; $$->dims[1] = $6;
        }
    | T_ID '=' assign_expr
        { $$ = mk_decl($1, @1.first_line); $$->a = $3; }
    ;

/* ---------- functions ---------- */

function
    : type T_ID '(' param_list_opt ')' compound
        {
            $$ = mk_text(N_FUNC, @2.first_line, $2);
            $$->type = (TypeKind)$1; $$->a = $4; $$->b = $6;
        }
    | VOID '*' T_ID '(' param_list_opt ')' compound
        {
            $$ = mk_text(N_FUNC, @3.first_line, $3);
            $$->type = T_VOID; $$->is_ptr = 1; $$->a = $5; $$->b = $7;
        }
    ;

param_list_opt
    : /* empty */   { $$ = NULL; }
    | VOID          { $$ = NULL; }
    | param_list    { $$ = $1; }
    ;

param_list
    : param                    { $$ = $1; }
    | param_list ',' param     { $$ = list_append($1, $3); }
    ;

param
    : type T_ID
        {
            if ($1 == T_VOID) ht_unsupported(@1.first_line, "parameter of type void");
            $$ = mk_text(N_PARAM, @2.first_line, $2); $$->type = (TypeKind)$1;
        }
    | VOID '*' T_ID
        { $$ = mk_text(N_PARAM, @3.first_line, $3); $$->type = T_VOID; $$->is_ptr = 1; }
    ;

/* ---------- statements ---------- */

compound
    : '{' stmt_list '}'    { $$ = mk(N_BLOCK, @1.first_line); $$->a = $2; }
    ;

stmt_list
    : /* empty */          { $$ = NULL; }
    | stmt_list stmt       { $$ = list_append($1, $2); }
    ;

stmt
    : compound             { $$ = $1; }
    | decl                 { $$ = $1; }
    | expr ';'             { $$ = mk(N_EXPR_STMT, @1.first_line); $$->a = $1; }
    | ';'                  { $$ = mk(N_EMPTY, @1.first_line); }
    | IF '(' expr ')' stmt %prec LOWER_THAN_ELSE
        { $$ = mk(N_IF, @1.first_line); $$->a = $3; $$->b = $5; }
    | IF '(' expr ')' stmt ELSE stmt
        { $$ = mk(N_IF, @1.first_line); $$->a = $3; $$->b = $5; $$->c = $7; }
    | FOR '(' for_init ';' expr_opt ';' expr_opt ')' stmt
        { $$ = mk(N_FOR, @1.first_line); $$->a = $3; $$->b = $5; $$->c = $7; $$->d = $9; }
    | WHILE '(' expr ')' stmt
        { $$ = mk(N_WHILE, @1.first_line); $$->a = $3; $$->b = $5; }
    | RETURN ';'           { $$ = mk(N_RETURN, @1.first_line); }
    | RETURN expr ';'      { $$ = mk(N_RETURN, @1.first_line); $$->a = $2; }
    ;

for_init
    : /* empty */          { $$ = NULL; }
    | expr                 { $$ = $1; }
    | type T_ID '=' assign_expr
        {
            if ($1 == T_VOID) ht_unsupported(@1.first_line, "variable of type void");
            $$ = mk_decl($2, @2.first_line); $$->type = (TypeKind)$1; $$->a = $4;
        }
    ;

expr_opt
    : /* empty */          { $$ = NULL; }
    | expr                 { $$ = $1; }
    ;

/* ---------- expressions (one nonterminal per precedence level) ---------- */

expr
    : assign_expr          { $$ = $1; }
    ;

assign_op
    : '='      { $$ = "="; }
    | ADDEQ    { $$ = "+="; }
    | SUBEQ    { $$ = "-="; }
    | MULEQ    { $$ = "*="; }
    | DIVEQ    { $$ = "/="; }
    ;

assign_expr
    : logical_or           { $$ = $1; }
    | unary assign_op assign_expr
        { $$ = mk_text(N_ASSIGN, @1.first_line, $2); $$->a = $1; $$->b = $3; }
    ;

logical_or
    : logical_and                    { $$ = $1; }
    | logical_or OR logical_and      { $$ = mk_bin("||", $1, $3, @2.first_line); }
    ;

logical_and
    : equality                       { $$ = $1; }
    | logical_and AND equality       { $$ = mk_bin("&&", $1, $3, @2.first_line); }
    ;

equality
    : relational                     { $$ = $1; }
    | equality EQ relational         { $$ = mk_bin("==", $1, $3, @2.first_line); }
    | equality NE relational         { $$ = mk_bin("!=", $1, $3, @2.first_line); }
    ;

relational
    : additive                       { $$ = $1; }
    | relational '<' additive        { $$ = mk_bin("<",  $1, $3, @2.first_line); }
    | relational '>' additive        { $$ = mk_bin(">",  $1, $3, @2.first_line); }
    | relational LE additive         { $$ = mk_bin("<=", $1, $3, @2.first_line); }
    | relational GE additive         { $$ = mk_bin(">=", $1, $3, @2.first_line); }
    ;

additive
    : multiplicative                 { $$ = $1; }
    | additive '+' multiplicative    { $$ = mk_bin("+", $1, $3, @2.first_line); }
    | additive '-' multiplicative    { $$ = mk_bin("-", $1, $3, @2.first_line); }
    ;

multiplicative
    : unary                          { $$ = $1; }
    | multiplicative '*' unary       { $$ = mk_bin("*", $1, $3, @2.first_line); }
    | multiplicative '/' unary       { $$ = mk_bin("/", $1, $3, @2.first_line); }
    | multiplicative '%' unary       { $$ = mk_bin("%", $1, $3, @2.first_line); }
    ;

unary
    : postfix                        { $$ = $1; }
    | '-' unary    { $$ = mk_text(N_UNOP, @1.first_line, "-"); $$->a = $2; }
    | '!' unary    { $$ = mk_text(N_UNOP, @1.first_line, "!"); $$->a = $2; }
    | '&' unary    { $$ = mk(N_ADDR, @1.first_line); $$->a = $2; }
    | INC unary    { $$ = mk_text(N_INCDEC, @1.first_line, "++"); $$->ival = 1; $$->a = $2; }
    | DEC unary    { $$ = mk_text(N_INCDEC, @1.first_line, "--"); $$->ival = 1; $$->a = $2; }
    | '*' unary    { ht_unsupported(@1.first_line, "pointer dereference"); $$ = NULL; }
    | '(' type ')' unary
                   { ht_unsupported(@1.first_line, "type cast"); $$ = NULL; }
    | '(' type '*' ')' unary
                   { ht_unsupported(@1.first_line, "type cast"); $$ = NULL; }
    ;

postfix
    : primary                        { $$ = $1; }
    | postfix '[' expr ']'
        { $$ = mk(N_INDEX, @2.first_line); $$->a = $1; $$->b = $3; }
    | postfix INC  { $$ = mk_text(N_INCDEC, @2.first_line, "++"); $$->a = $1; }
    | postfix DEC  { $$ = mk_text(N_INCDEC, @2.first_line, "--"); $$->a = $1; }
    ;

primary
    : T_ID                { $$ = mk_text(N_ID, @1.first_line, $1); }
    | T_INTLIT            { $$ = mk(N_INT, @1.first_line); $$->ival = $1; }
    | T_FLOATLIT          { $$ = mk_text(N_FLOAT, @1.first_line, $1); $$->fval = atof($1); }
    | T_STRING            { $$ = mk_text(N_STR, @1.first_line, $1); }
    | NULLT               { $$ = mk(N_NULL, @1.first_line); }
    | '(' expr ')'        { $$ = $2; }
    | T_ID '(' arg_list_opt ')'
        { $$ = mk_text(N_CALL, @1.first_line, $1); $$->a = $3; }
    ;

arg_list_opt
    : /* empty */         { $$ = NULL; }
    | arg_list            { $$ = $1; }
    ;

arg_list
    : assign_expr                    { $$ = $1; }
    | arg_list ',' assign_expr       { $$ = list_append($1, $3); }
    ;

%%

void yyerror(const char *msg)
{
    (void)msg;
    if (yytext && yytext[0])
        fprintf(stderr, "Syntax error at line %d: unexpected '%s'\n", yylineno, yytext);
    else
        fprintf(stderr, "Syntax error at line %d: unexpected end of input\n", yylineno);
    g_errors++;
}
