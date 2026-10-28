/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 1 "src/parser.y"

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

#line 97 "generated/parser.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "parser.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_T_ID = 3,                       /* T_ID  */
  YYSYMBOL_T_FLOATLIT = 4,                 /* T_FLOATLIT  */
  YYSYMBOL_T_STRING = 5,                   /* T_STRING  */
  YYSYMBOL_T_INTLIT = 6,                   /* T_INTLIT  */
  YYSYMBOL_INT = 7,                        /* INT  */
  YYSYMBOL_FLOAT = 8,                      /* FLOAT  */
  YYSYMBOL_DOUBLE = 9,                     /* DOUBLE  */
  YYSYMBOL_VOID = 10,                      /* VOID  */
  YYSYMBOL_PTHREAD_T = 11,                 /* PTHREAD_T  */
  YYSYMBOL_MUTEX_T = 12,                   /* MUTEX_T  */
  YYSYMBOL_IF = 13,                        /* IF  */
  YYSYMBOL_ELSE = 14,                      /* ELSE  */
  YYSYMBOL_FOR = 15,                       /* FOR  */
  YYSYMBOL_WHILE = 16,                     /* WHILE  */
  YYSYMBOL_RETURN = 17,                    /* RETURN  */
  YYSYMBOL_NULLT = 18,                     /* NULLT  */
  YYSYMBOL_ADDEQ = 19,                     /* ADDEQ  */
  YYSYMBOL_SUBEQ = 20,                     /* SUBEQ  */
  YYSYMBOL_MULEQ = 21,                     /* MULEQ  */
  YYSYMBOL_DIVEQ = 22,                     /* DIVEQ  */
  YYSYMBOL_INC = 23,                       /* INC  */
  YYSYMBOL_DEC = 24,                       /* DEC  */
  YYSYMBOL_AND = 25,                       /* AND  */
  YYSYMBOL_OR = 26,                        /* OR  */
  YYSYMBOL_EQ = 27,                        /* EQ  */
  YYSYMBOL_NE = 28,                        /* NE  */
  YYSYMBOL_LE = 29,                        /* LE  */
  YYSYMBOL_GE = 30,                        /* GE  */
  YYSYMBOL_LOWER_THAN_ELSE = 31,           /* LOWER_THAN_ELSE  */
  YYSYMBOL_32_ = 32,                       /* ';'  */
  YYSYMBOL_33_ = 33,                       /* ','  */
  YYSYMBOL_34_ = 34,                       /* '['  */
  YYSYMBOL_35_ = 35,                       /* ']'  */
  YYSYMBOL_36_ = 36,                       /* '='  */
  YYSYMBOL_37_ = 37,                       /* '('  */
  YYSYMBOL_38_ = 38,                       /* ')'  */
  YYSYMBOL_39_ = 39,                       /* '*'  */
  YYSYMBOL_40_ = 40,                       /* '{'  */
  YYSYMBOL_41_ = 41,                       /* '}'  */
  YYSYMBOL_42_ = 42,                       /* '<'  */
  YYSYMBOL_43_ = 43,                       /* '>'  */
  YYSYMBOL_44_ = 44,                       /* '+'  */
  YYSYMBOL_45_ = 45,                       /* '-'  */
  YYSYMBOL_46_ = 46,                       /* '/'  */
  YYSYMBOL_47_ = 47,                       /* '%'  */
  YYSYMBOL_48_ = 48,                       /* '!'  */
  YYSYMBOL_49_ = 49,                       /* '&'  */
  YYSYMBOL_YYACCEPT = 50,                  /* $accept  */
  YYSYMBOL_program = 51,                   /* program  */
  YYSYMBOL_external_list = 52,             /* external_list  */
  YYSYMBOL_external = 53,                  /* external  */
  YYSYMBOL_type = 54,                      /* type  */
  YYSYMBOL_decl = 55,                      /* decl  */
  YYSYMBOL_declarator_list = 56,           /* declarator_list  */
  YYSYMBOL_declarator = 57,                /* declarator  */
  YYSYMBOL_function = 58,                  /* function  */
  YYSYMBOL_param_list_opt = 59,            /* param_list_opt  */
  YYSYMBOL_param_list = 60,                /* param_list  */
  YYSYMBOL_param = 61,                     /* param  */
  YYSYMBOL_compound = 62,                  /* compound  */
  YYSYMBOL_stmt_list = 63,                 /* stmt_list  */
  YYSYMBOL_stmt = 64,                      /* stmt  */
  YYSYMBOL_for_init = 65,                  /* for_init  */
  YYSYMBOL_expr_opt = 66,                  /* expr_opt  */
  YYSYMBOL_expr = 67,                      /* expr  */
  YYSYMBOL_assign_op = 68,                 /* assign_op  */
  YYSYMBOL_assign_expr = 69,               /* assign_expr  */
  YYSYMBOL_logical_or = 70,                /* logical_or  */
  YYSYMBOL_logical_and = 71,               /* logical_and  */
  YYSYMBOL_equality = 72,                  /* equality  */
  YYSYMBOL_relational = 73,                /* relational  */
  YYSYMBOL_additive = 74,                  /* additive  */
  YYSYMBOL_multiplicative = 75,            /* multiplicative  */
  YYSYMBOL_unary = 76,                     /* unary  */
  YYSYMBOL_postfix = 77,                   /* postfix  */
  YYSYMBOL_primary = 78,                   /* primary  */
  YYSYMBOL_arg_list_opt = 79,              /* arg_list_opt  */
  YYSYMBOL_arg_list = 80                   /* arg_list  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
             && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE) \
             + YYSIZEOF (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   298

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  50
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  31
/* YYNRULES -- Number of rules.  */
#define YYNRULES  97
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  173

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   286


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    48,     2,     2,     2,    47,    49,     2,
      37,    38,    39,    44,    33,    45,     2,    46,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,    32,
      42,    36,    43,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    34,     2,    35,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    40,     2,    41,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29,    30,    31
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    57,    57,    61,    62,    66,    67,    73,    74,    75,
      76,    77,    78,    82,    93,    94,    98,   100,   106,   112,
     119,   124,   132,   133,   134,   138,   139,   143,   148,   155,
     159,   160,   164,   165,   166,   167,   168,   170,   172,   174,
     176,   177,   181,   182,   183,   191,   192,   198,   202,   203,
     204,   205,   206,   210,   211,   216,   217,   221,   222,   226,
     227,   228,   232,   233,   234,   235,   236,   240,   241,   242,
     246,   247,   248,   249,   253,   254,   255,   256,   257,   258,
     259,   260,   262,   267,   268,   270,   271,   275,   276,   277,
     278,   279,   280,   281,   286,   287,   291,   292
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "T_ID", "T_FLOATLIT",
  "T_STRING", "T_INTLIT", "INT", "FLOAT", "DOUBLE", "VOID", "PTHREAD_T",
  "MUTEX_T", "IF", "ELSE", "FOR", "WHILE", "RETURN", "NULLT", "ADDEQ",
  "SUBEQ", "MULEQ", "DIVEQ", "INC", "DEC", "AND", "OR", "EQ", "NE", "LE",
  "GE", "LOWER_THAN_ELSE", "';'", "','", "'['", "']'", "'='", "'('", "')'",
  "'*'", "'{'", "'}'", "'<'", "'>'", "'+'", "'-'", "'/'", "'%'", "'!'",
  "'&'", "$accept", "program", "external_list", "external", "type", "decl",
  "declarator_list", "declarator", "function", "param_list_opt",
  "param_list", "param", "compound", "stmt_list", "stmt", "for_init",
  "expr_opt", "expr", "assign_op", "assign_expr", "logical_or",
  "logical_and", "equality", "relational", "additive", "multiplicative",
  "unary", "postfix", "primary", "arg_list_opt", "arg_list", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-137)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-24)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
    -137,     5,   115,  -137,  -137,  -137,  -137,   -25,  -137,  -137,
    -137,    20,  -137,  -137,    30,    28,    36,  -137,   -20,    31,
     249,   122,  -137,    56,   122,     4,    26,  -137,  -137,  -137,
    -137,   249,   249,   189,   249,   249,   249,   249,  -137,    40,
      49,    45,   -14,    48,    14,    -9,     2,  -137,    59,    67,
      61,    53,  -137,   -16,  -137,    70,    60,   249,  -137,  -137,
    -137,    63,   100,  -137,  -137,  -137,  -137,  -137,   249,   249,
     249,   249,   249,   249,   249,   249,   249,   249,   249,   249,
     249,  -137,  -137,  -137,  -137,  -137,   249,  -137,  -137,   249,
     102,  -137,    99,   160,    99,    97,  -137,   103,    77,   249,
     104,  -137,    49,  -137,    45,   -14,   -14,    48,    48,    48,
      48,    14,    14,  -137,  -137,  -137,  -137,   105,  -137,  -137,
    -137,   117,  -137,  -137,   126,  -137,   249,  -137,   249,  -137,
      72,  -137,  -137,  -137,   125,   127,   136,   212,  -137,  -137,
      56,  -137,  -137,  -137,   131,   249,   189,   249,  -137,   143,
    -137,   138,   174,   146,  -137,   145,  -137,   142,   144,   249,
     142,   170,   249,   153,  -137,  -137,   142,  -137,   249,  -137,
     148,   142,  -137
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     2,     1,     7,     8,     9,    10,    11,    12,
       4,     0,     5,     6,     0,    16,     0,    14,     0,     0,
       0,    22,    13,     0,    22,     0,    87,    89,    90,    88,
      91,     0,     0,     0,     0,     0,     0,     0,    19,    53,
      55,    57,    59,    62,    67,    70,    74,    83,    10,     0,
       0,    24,    25,    16,    15,     0,    17,    94,    78,    79,
      10,     0,     0,    47,    80,    75,    76,    77,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,    49,    50,    51,    52,    48,     0,    85,    86,     0,
       0,    27,     0,     0,     0,     0,    96,     0,    95,     0,
       0,    92,    56,    70,    58,    60,    61,    65,    66,    63,
      64,    68,    69,    71,    72,    73,    54,     0,    28,    30,
      20,    10,    26,    21,     0,    93,     0,    81,     0,    84,
       0,    18,    97,    82,     0,     0,     0,     0,    35,    29,
       0,    33,    32,    31,     0,     0,    42,     0,    40,     0,
      34,     0,     0,     0,    43,     0,    41,     0,     0,    45,
       0,    36,     0,     0,    46,    39,     0,    44,    45,    37,
       0,     0,    38
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -137,  -137,  -137,  -137,    -2,   186,  -137,   166,  -137,   178,
    -137,   110,   -60,  -137,  -136,  -137,    37,   -31,  -137,   -19,
    -137,   140,   135,    65,   -18,    42,   -28,  -137,  -137,  -137,
    -137
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_uint8 yydefgoto[] =
{
       0,     1,     2,    10,   140,   141,    16,    17,    13,    50,
      51,    52,   142,   130,   143,   153,   163,   144,    86,    63,
      39,    40,    41,    42,    43,    44,    45,    46,    47,    97,
      98
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      11,    38,    62,    58,    59,     3,    64,    65,    66,    67,
      81,    82,    83,    84,    14,    72,    73,    24,    19,    49,
      20,   161,    49,    15,   165,    87,    88,    85,    74,    75,
     169,    61,   120,    18,   123,   172,    89,    25,    96,    56,
     103,   103,   103,   103,   103,   103,   103,   103,   103,   103,
     113,   114,   115,    78,   107,   108,   109,   110,   117,    53,
      79,    80,    19,    57,    20,    21,    68,   116,    22,    23,
      91,   127,    70,    71,    69,    26,    27,    28,    29,     4,
       5,     6,    60,     8,     9,   134,    93,   135,   136,   137,
      30,    49,    76,    77,    95,    31,    32,   -23,    90,    92,
     133,    99,   100,   124,   138,   118,   149,   132,    94,    33,
     126,    34,   119,   139,   151,   154,   155,    35,   111,   112,
      36,    37,     4,     5,     6,     7,     8,     9,   164,     4,
       5,     6,    48,     8,     9,   105,   106,   164,   101,   119,
     129,   125,   128,   167,   152,    26,    27,    28,    29,     4,
       5,     6,    60,     8,     9,   134,    90,   135,   136,   137,
      30,   131,   145,   150,   146,    31,    32,     4,     5,     6,
     121,     8,     9,   147,   138,   156,   157,   158,   159,    33,
     162,    34,   119,   160,   166,   168,   171,    35,    12,    54,
      36,    37,    26,    27,    28,    29,     4,     5,     6,    60,
       8,     9,    55,   122,   104,   170,     0,    30,   102,     0,
       0,     0,    31,    32,     0,    26,    27,    28,    29,     0,
       0,     0,     0,     0,     0,     0,    33,     0,    34,     0,
      30,     0,     0,     0,    35,    31,    32,    36,    37,     0,
       0,     0,     0,     0,   148,     0,     0,     0,     0,    33,
       0,    34,    26,    27,    28,    29,     0,    35,     0,     0,
      36,    37,     0,     0,     0,     0,     0,    30,     0,     0,
       0,     0,    31,    32,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    33,     0,    34,     0,
       0,     0,     0,     0,    35,     0,     0,    36,    37
};

static const yytype_int16 yycheck[] =
{
       2,    20,    33,    31,    32,     0,    34,    35,    36,    37,
      19,    20,    21,    22,    39,    29,    30,    37,    34,    21,
      36,   157,    24,     3,   160,    23,    24,    36,    42,    43,
     166,    33,    92,     3,    94,   171,    34,     6,    57,    35,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      78,    79,    80,    39,    72,    73,    74,    75,    89,     3,
      46,    47,    34,    37,    36,    37,    26,    86,    32,    33,
       3,    99,    27,    28,    25,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    33,    15,    16,    17,
      18,    93,    44,    45,    34,    23,    24,    38,    39,    38,
     128,    38,    39,     6,    32,     3,   137,   126,    38,    37,
      33,    39,    40,    41,   145,   146,   147,    45,    76,    77,
      48,    49,     7,     8,     9,    10,    11,    12,   159,     7,
       8,     9,    10,    11,    12,    70,    71,   168,    38,    40,
      35,    38,    38,   162,   146,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    12,    13,    39,    15,    16,    17,
      18,    35,    37,    32,    37,    23,    24,     7,     8,     9,
      10,    11,    12,    37,    32,    32,    38,     3,    32,    37,
      36,    39,    40,    38,    14,    32,    38,    45,     2,    23,
      48,    49,     3,     4,     5,     6,     7,     8,     9,    10,
      11,    12,    24,    93,    69,   168,    -1,    18,    68,    -1,
      -1,    -1,    23,    24,    -1,     3,     4,     5,     6,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,    39,    -1,
      18,    -1,    -1,    -1,    45,    23,    24,    48,    49,    -1,
      -1,    -1,    -1,    -1,    32,    -1,    -1,    -1,    -1,    37,
      -1,    39,     3,     4,     5,     6,    -1,    45,    -1,    -1,
      48,    49,    -1,    -1,    -1,    -1,    -1,    18,    -1,    -1,
      -1,    -1,    23,    24,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    37,    -1,    39,    -1,
      -1,    -1,    -1,    -1,    45,    -1,    -1,    48,    49
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    51,    52,     0,     7,     8,     9,    10,    11,    12,
      53,    54,    55,    58,    39,     3,    56,    57,     3,    34,
      36,    37,    32,    33,    37,     6,     3,     4,     5,     6,
      18,    23,    24,    37,    39,    45,    48,    49,    69,    70,
      71,    72,    73,    74,    75,    76,    77,    78,    10,    54,
      59,    60,    61,     3,    57,    59,    35,    37,    76,    76,
      10,    54,    67,    69,    76,    76,    76,    76,    26,    25,
      27,    28,    29,    30,    42,    43,    44,    45,    39,    46,
      47,    19,    20,    21,    22,    36,    68,    23,    24,    34,
      39,     3,    38,    33,    38,    34,    69,    79,    80,    38,
      39,    38,    71,    76,    72,    73,    73,    74,    74,    74,
      74,    75,    75,    76,    76,    76,    69,    67,     3,    40,
      62,    10,    61,    62,     6,    38,    33,    76,    38,    35,
      63,    35,    69,    76,    13,    15,    16,    17,    32,    41,
      54,    55,    62,    64,    67,    37,    37,    37,    32,    67,
      32,    67,    54,    65,    67,    67,    32,    38,     3,    32,
      38,    64,    36,    66,    67,    64,    14,    69,    32,    64,
      66,    38,    64
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    50,    51,    52,    52,    53,    53,    54,    54,    54,
      54,    54,    54,    55,    56,    56,    57,    57,    57,    57,
      58,    58,    59,    59,    59,    60,    60,    61,    61,    62,
      63,    63,    64,    64,    64,    64,    64,    64,    64,    64,
      64,    64,    65,    65,    65,    66,    66,    67,    68,    68,
      68,    68,    68,    69,    69,    70,    70,    71,    71,    72,
      72,    72,    73,    73,    73,    73,    73,    74,    74,    74,
      75,    75,    75,    75,    76,    76,    76,    76,    76,    76,
      76,    76,    76,    77,    77,    77,    77,    78,    78,    78,
      78,    78,    78,    78,    79,    79,    80,    80
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     0,     2,     1,     1,     1,     1,     1,
       1,     1,     1,     3,     1,     3,     1,     4,     7,     3,
       6,     7,     0,     1,     1,     1,     3,     2,     3,     3,
       0,     2,     1,     1,     2,     1,     5,     7,     9,     5,
       2,     3,     0,     1,     4,     0,     1,     1,     1,     1,
       1,     1,     1,     1,     3,     1,     3,     1,     3,     1,
       3,     3,     1,     3,     3,     3,     3,     1,     3,     3,
       1,     3,     3,     3,     1,     2,     2,     2,     2,     2,
       2,     4,     5,     1,     4,     2,     2,     1,     1,     1,
       1,     1,     3,     4,     0,     1,     1,     3
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF

/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)                                \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;        \
          (Current).first_column = YYRHSLOC (Rhs, 1).first_column;      \
          (Current).last_line    = YYRHSLOC (Rhs, N).last_line;         \
          (Current).last_column  = YYRHSLOC (Rhs, N).last_column;       \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).first_line   = (Current).last_line   =              \
            YYRHSLOC (Rhs, 0).last_line;                                \
          (Current).first_column = (Current).last_column =              \
            YYRHSLOC (Rhs, 0).last_column;                              \
        }                                                               \
    while (0)
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K])


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)


/* YYLOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

# ifndef YYLOCATION_PRINT

#  if defined YY_LOCATION_PRINT

   /* Temporary convenience wrapper in case some people defined the
      undocumented and private YY_LOCATION_PRINT macros.  */
#   define YYLOCATION_PRINT(File, Loc)  YY_LOCATION_PRINT(File, *(Loc))

#  elif defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL

/* Print *YYLOCP on YYO.  Private, do not rely on its existence. */

YY_ATTRIBUTE_UNUSED
static int
yy_location_print_ (FILE *yyo, YYLTYPE const * const yylocp)
{
  int res = 0;
  int end_col = 0 != yylocp->last_column ? yylocp->last_column - 1 : 0;
  if (0 <= yylocp->first_line)
    {
      res += YYFPRINTF (yyo, "%d", yylocp->first_line);
      if (0 <= yylocp->first_column)
        res += YYFPRINTF (yyo, ".%d", yylocp->first_column);
    }
  if (0 <= yylocp->last_line)
    {
      if (yylocp->first_line < yylocp->last_line)
        {
          res += YYFPRINTF (yyo, "-%d", yylocp->last_line);
          if (0 <= end_col)
            res += YYFPRINTF (yyo, ".%d", end_col);
        }
      else if (0 <= end_col && yylocp->first_column < end_col)
        res += YYFPRINTF (yyo, "-%d", end_col);
    }
  return res;
}

#   define YYLOCATION_PRINT  yy_location_print_

    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT(File, Loc)  YYLOCATION_PRINT(File, &(Loc))

#  else

#   define YYLOCATION_PRINT(File, Loc) ((void) 0)
    /* Temporary convenience wrapper in case some people defined the
       undocumented and private YY_LOCATION_PRINT macros.  */
#   define YY_LOCATION_PRINT  YYLOCATION_PRINT

#  endif
# endif /* !defined YYLOCATION_PRINT */


# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value, Location); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  YY_USE (yylocationp);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  YYLOCATION_PRINT (yyo, yylocationp);
  YYFPRINTF (yyo, ": ");
  yy_symbol_value_print (yyo, yykind, yyvaluep, yylocationp);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp, YYLTYPE *yylsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)],
                       &(yylsp[(yyi + 1) - (yynrhs)]));
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, yylsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
{
  YY_USE (yyvaluep);
  YY_USE (yylocationp);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Location data for the lookahead symbol.  */
YYLTYPE yylloc
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
  = { 1, 1, 1, 1 }
# endif
;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

    /* The location stack: array, bottom, top.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls = yylsa;
    YYLTYPE *yylsp = yyls;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[3];



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  yylsp[0] = yylloc;
  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;
        YYLTYPE *yyls1 = yyls;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yyls1, yysize * YYSIZEOF (*yylsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
        yyls = yyls1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
        YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      yyerror_range[1] = yylloc;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END
  *++yylsp = yylloc;

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  yyerror_range[1] = yyloc;
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* program: external_list  */
#line 57 "src/parser.y"
                         { g_program = mk(N_PROGRAM, 1); g_program->a = (yyvsp[0].node); }
#line 1421 "generated/parser.tab.c"
    break;

  case 3: /* external_list: %empty  */
#line 61 "src/parser.y"
                                  { (yyval.node) = NULL; }
#line 1427 "generated/parser.tab.c"
    break;

  case 4: /* external_list: external_list external  */
#line 62 "src/parser.y"
                                  { (yyval.node) = list_append((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1433 "generated/parser.tab.c"
    break;

  case 5: /* external: decl  */
#line 66 "src/parser.y"
                                  { (yyval.node) = (yyvsp[0].node); }
#line 1439 "generated/parser.tab.c"
    break;

  case 6: /* external: function  */
#line 67 "src/parser.y"
                                  { (yyval.node) = (yyvsp[0].node); }
#line 1445 "generated/parser.tab.c"
    break;

  case 7: /* type: INT  */
#line 73 "src/parser.y"
                 { (yyval.type) = T_INT; }
#line 1451 "generated/parser.tab.c"
    break;

  case 8: /* type: FLOAT  */
#line 74 "src/parser.y"
                 { (yyval.type) = T_FLOAT; }
#line 1457 "generated/parser.tab.c"
    break;

  case 9: /* type: DOUBLE  */
#line 75 "src/parser.y"
                 { (yyval.type) = T_DOUBLE; }
#line 1463 "generated/parser.tab.c"
    break;

  case 10: /* type: VOID  */
#line 76 "src/parser.y"
                 { (yyval.type) = T_VOID; }
#line 1469 "generated/parser.tab.c"
    break;

  case 11: /* type: PTHREAD_T  */
#line 77 "src/parser.y"
                 { (yyval.type) = T_PTHREAD; }
#line 1475 "generated/parser.tab.c"
    break;

  case 12: /* type: MUTEX_T  */
#line 78 "src/parser.y"
                 { (yyval.type) = T_MUTEX; }
#line 1481 "generated/parser.tab.c"
    break;

  case 13: /* decl: type declarator_list ';'  */
#line 83 "src/parser.y"
        {
            Node *n;
            if ((yyvsp[-2].type) == T_VOID)
                ht_unsupported((yylsp[-2]).first_line, "variable of type void");
            for (n = (yyvsp[-1].node); n; n = n->next) n->type = (TypeKind)(yyvsp[-2].type);
            (yyval.node) = (yyvsp[-1].node);
        }
#line 1493 "generated/parser.tab.c"
    break;

  case 14: /* declarator_list: declarator  */
#line 93 "src/parser.y"
                                         { (yyval.node) = (yyvsp[0].node); }
#line 1499 "generated/parser.tab.c"
    break;

  case 15: /* declarator_list: declarator_list ',' declarator  */
#line 94 "src/parser.y"
                                         { (yyval.node) = list_append((yyvsp[-2].node), (yyvsp[0].node)); }
#line 1505 "generated/parser.tab.c"
    break;

  case 16: /* declarator: T_ID  */
#line 99 "src/parser.y"
        { (yyval.node) = mk_decl((yyvsp[0].str), (yylsp[0]).first_line); }
#line 1511 "generated/parser.tab.c"
    break;

  case 17: /* declarator: T_ID '[' T_INTLIT ']'  */
#line 101 "src/parser.y"
        {
            if ((yyvsp[-1].ival) <= 0) ht_unsupported((yylsp[-3]).first_line, "array size must be positive");
            (yyval.node) = mk_decl((yyvsp[-3].str), (yylsp[-3]).first_line);
            (yyval.node)->ndims = 1; (yyval.node)->dims[0] = (yyvsp[-1].ival);
        }
#line 1521 "generated/parser.tab.c"
    break;

  case 18: /* declarator: T_ID '[' T_INTLIT ']' '[' T_INTLIT ']'  */
#line 107 "src/parser.y"
        {
            if ((yyvsp[-4].ival) <= 0 || (yyvsp[-1].ival) <= 0) ht_unsupported((yylsp[-6]).first_line, "array size must be positive");
            (yyval.node) = mk_decl((yyvsp[-6].str), (yylsp[-6]).first_line);
            (yyval.node)->ndims = 2; (yyval.node)->dims[0] = (yyvsp[-4].ival); (yyval.node)->dims[1] = (yyvsp[-1].ival);
        }
#line 1531 "generated/parser.tab.c"
    break;

  case 19: /* declarator: T_ID '=' assign_expr  */
#line 113 "src/parser.y"
        { (yyval.node) = mk_decl((yyvsp[-2].str), (yylsp[-2]).first_line); (yyval.node)->a = (yyvsp[0].node); }
#line 1537 "generated/parser.tab.c"
    break;

  case 20: /* function: type T_ID '(' param_list_opt ')' compound  */
#line 120 "src/parser.y"
        {
            (yyval.node) = mk_text(N_FUNC, (yylsp[-4]).first_line, (yyvsp[-4].str));
            (yyval.node)->type = (TypeKind)(yyvsp[-5].type); (yyval.node)->a = (yyvsp[-2].node); (yyval.node)->b = (yyvsp[0].node);
        }
#line 1546 "generated/parser.tab.c"
    break;

  case 21: /* function: VOID '*' T_ID '(' param_list_opt ')' compound  */
#line 125 "src/parser.y"
        {
            (yyval.node) = mk_text(N_FUNC, (yylsp[-4]).first_line, (yyvsp[-4].str));
            (yyval.node)->type = T_VOID; (yyval.node)->is_ptr = 1; (yyval.node)->a = (yyvsp[-2].node); (yyval.node)->b = (yyvsp[0].node);
        }
#line 1555 "generated/parser.tab.c"
    break;

  case 22: /* param_list_opt: %empty  */
#line 132 "src/parser.y"
                    { (yyval.node) = NULL; }
#line 1561 "generated/parser.tab.c"
    break;

  case 23: /* param_list_opt: VOID  */
#line 133 "src/parser.y"
                    { (yyval.node) = NULL; }
#line 1567 "generated/parser.tab.c"
    break;

  case 24: /* param_list_opt: param_list  */
#line 134 "src/parser.y"
                    { (yyval.node) = (yyvsp[0].node); }
#line 1573 "generated/parser.tab.c"
    break;

  case 25: /* param_list: param  */
#line 138 "src/parser.y"
                               { (yyval.node) = (yyvsp[0].node); }
#line 1579 "generated/parser.tab.c"
    break;

  case 26: /* param_list: param_list ',' param  */
#line 139 "src/parser.y"
                               { (yyval.node) = list_append((yyvsp[-2].node), (yyvsp[0].node)); }
#line 1585 "generated/parser.tab.c"
    break;

  case 27: /* param: type T_ID  */
#line 144 "src/parser.y"
        {
            if ((yyvsp[-1].type) == T_VOID) ht_unsupported((yylsp[-1]).first_line, "parameter of type void");
            (yyval.node) = mk_text(N_PARAM, (yylsp[0]).first_line, (yyvsp[0].str)); (yyval.node)->type = (TypeKind)(yyvsp[-1].type);
        }
#line 1594 "generated/parser.tab.c"
    break;

  case 28: /* param: VOID '*' T_ID  */
#line 149 "src/parser.y"
        { (yyval.node) = mk_text(N_PARAM, (yylsp[0]).first_line, (yyvsp[0].str)); (yyval.node)->type = T_VOID; (yyval.node)->is_ptr = 1; }
#line 1600 "generated/parser.tab.c"
    break;

  case 29: /* compound: '{' stmt_list '}'  */
#line 155 "src/parser.y"
                           { (yyval.node) = mk(N_BLOCK, (yylsp[-2]).first_line); (yyval.node)->a = (yyvsp[-1].node); }
#line 1606 "generated/parser.tab.c"
    break;

  case 30: /* stmt_list: %empty  */
#line 159 "src/parser.y"
                           { (yyval.node) = NULL; }
#line 1612 "generated/parser.tab.c"
    break;

  case 31: /* stmt_list: stmt_list stmt  */
#line 160 "src/parser.y"
                           { (yyval.node) = list_append((yyvsp[-1].node), (yyvsp[0].node)); }
#line 1618 "generated/parser.tab.c"
    break;

  case 32: /* stmt: compound  */
#line 164 "src/parser.y"
                           { (yyval.node) = (yyvsp[0].node); }
#line 1624 "generated/parser.tab.c"
    break;

  case 33: /* stmt: decl  */
#line 165 "src/parser.y"
                           { (yyval.node) = (yyvsp[0].node); }
#line 1630 "generated/parser.tab.c"
    break;

  case 34: /* stmt: expr ';'  */
#line 166 "src/parser.y"
                           { (yyval.node) = mk(N_EXPR_STMT, (yylsp[-1]).first_line); (yyval.node)->a = (yyvsp[-1].node); }
#line 1636 "generated/parser.tab.c"
    break;

  case 35: /* stmt: ';'  */
#line 167 "src/parser.y"
                           { (yyval.node) = mk(N_EMPTY, (yylsp[0]).first_line); }
#line 1642 "generated/parser.tab.c"
    break;

  case 36: /* stmt: IF '(' expr ')' stmt  */
#line 169 "src/parser.y"
        { (yyval.node) = mk(N_IF, (yylsp[-4]).first_line); (yyval.node)->a = (yyvsp[-2].node); (yyval.node)->b = (yyvsp[0].node); }
#line 1648 "generated/parser.tab.c"
    break;

  case 37: /* stmt: IF '(' expr ')' stmt ELSE stmt  */
#line 171 "src/parser.y"
        { (yyval.node) = mk(N_IF, (yylsp[-6]).first_line); (yyval.node)->a = (yyvsp[-4].node); (yyval.node)->b = (yyvsp[-2].node); (yyval.node)->c = (yyvsp[0].node); }
#line 1654 "generated/parser.tab.c"
    break;

  case 38: /* stmt: FOR '(' for_init ';' expr_opt ';' expr_opt ')' stmt  */
#line 173 "src/parser.y"
        { (yyval.node) = mk(N_FOR, (yylsp[-8]).first_line); (yyval.node)->a = (yyvsp[-6].node); (yyval.node)->b = (yyvsp[-4].node); (yyval.node)->c = (yyvsp[-2].node); (yyval.node)->d = (yyvsp[0].node); }
#line 1660 "generated/parser.tab.c"
    break;

  case 39: /* stmt: WHILE '(' expr ')' stmt  */
#line 175 "src/parser.y"
        { (yyval.node) = mk(N_WHILE, (yylsp[-4]).first_line); (yyval.node)->a = (yyvsp[-2].node); (yyval.node)->b = (yyvsp[0].node); }
#line 1666 "generated/parser.tab.c"
    break;

  case 40: /* stmt: RETURN ';'  */
#line 176 "src/parser.y"
                           { (yyval.node) = mk(N_RETURN, (yylsp[-1]).first_line); }
#line 1672 "generated/parser.tab.c"
    break;

  case 41: /* stmt: RETURN expr ';'  */
#line 177 "src/parser.y"
                           { (yyval.node) = mk(N_RETURN, (yylsp[-2]).first_line); (yyval.node)->a = (yyvsp[-1].node); }
#line 1678 "generated/parser.tab.c"
    break;

  case 42: /* for_init: %empty  */
#line 181 "src/parser.y"
                           { (yyval.node) = NULL; }
#line 1684 "generated/parser.tab.c"
    break;

  case 43: /* for_init: expr  */
#line 182 "src/parser.y"
                           { (yyval.node) = (yyvsp[0].node); }
#line 1690 "generated/parser.tab.c"
    break;

  case 44: /* for_init: type T_ID '=' assign_expr  */
#line 184 "src/parser.y"
        {
            if ((yyvsp[-3].type) == T_VOID) ht_unsupported((yylsp[-3]).first_line, "variable of type void");
            (yyval.node) = mk_decl((yyvsp[-2].str), (yylsp[-2]).first_line); (yyval.node)->type = (TypeKind)(yyvsp[-3].type); (yyval.node)->a = (yyvsp[0].node);
        }
#line 1699 "generated/parser.tab.c"
    break;

  case 45: /* expr_opt: %empty  */
#line 191 "src/parser.y"
                           { (yyval.node) = NULL; }
#line 1705 "generated/parser.tab.c"
    break;

  case 46: /* expr_opt: expr  */
#line 192 "src/parser.y"
                           { (yyval.node) = (yyvsp[0].node); }
#line 1711 "generated/parser.tab.c"
    break;

  case 47: /* expr: assign_expr  */
#line 198 "src/parser.y"
                           { (yyval.node) = (yyvsp[0].node); }
#line 1717 "generated/parser.tab.c"
    break;

  case 48: /* assign_op: '='  */
#line 202 "src/parser.y"
               { (yyval.str) = "="; }
#line 1723 "generated/parser.tab.c"
    break;

  case 49: /* assign_op: ADDEQ  */
#line 203 "src/parser.y"
               { (yyval.str) = "+="; }
#line 1729 "generated/parser.tab.c"
    break;

  case 50: /* assign_op: SUBEQ  */
#line 204 "src/parser.y"
               { (yyval.str) = "-="; }
#line 1735 "generated/parser.tab.c"
    break;

  case 51: /* assign_op: MULEQ  */
#line 205 "src/parser.y"
               { (yyval.str) = "*="; }
#line 1741 "generated/parser.tab.c"
    break;

  case 52: /* assign_op: DIVEQ  */
#line 206 "src/parser.y"
               { (yyval.str) = "/="; }
#line 1747 "generated/parser.tab.c"
    break;

  case 53: /* assign_expr: logical_or  */
#line 210 "src/parser.y"
                           { (yyval.node) = (yyvsp[0].node); }
#line 1753 "generated/parser.tab.c"
    break;

  case 54: /* assign_expr: unary assign_op assign_expr  */
#line 212 "src/parser.y"
        { (yyval.node) = mk_text(N_ASSIGN, (yylsp[-2]).first_line, (yyvsp[-1].str)); (yyval.node)->a = (yyvsp[-2].node); (yyval.node)->b = (yyvsp[0].node); }
#line 1759 "generated/parser.tab.c"
    break;

  case 55: /* logical_or: logical_and  */
#line 216 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1765 "generated/parser.tab.c"
    break;

  case 56: /* logical_or: logical_or OR logical_and  */
#line 217 "src/parser.y"
                                     { (yyval.node) = mk_bin("||", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1771 "generated/parser.tab.c"
    break;

  case 57: /* logical_and: equality  */
#line 221 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1777 "generated/parser.tab.c"
    break;

  case 58: /* logical_and: logical_and AND equality  */
#line 222 "src/parser.y"
                                     { (yyval.node) = mk_bin("&&", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1783 "generated/parser.tab.c"
    break;

  case 59: /* equality: relational  */
#line 226 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1789 "generated/parser.tab.c"
    break;

  case 60: /* equality: equality EQ relational  */
#line 227 "src/parser.y"
                                     { (yyval.node) = mk_bin("==", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1795 "generated/parser.tab.c"
    break;

  case 61: /* equality: equality NE relational  */
#line 228 "src/parser.y"
                                     { (yyval.node) = mk_bin("!=", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1801 "generated/parser.tab.c"
    break;

  case 62: /* relational: additive  */
#line 232 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1807 "generated/parser.tab.c"
    break;

  case 63: /* relational: relational '<' additive  */
#line 233 "src/parser.y"
                                     { (yyval.node) = mk_bin("<",  (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1813 "generated/parser.tab.c"
    break;

  case 64: /* relational: relational '>' additive  */
#line 234 "src/parser.y"
                                     { (yyval.node) = mk_bin(">",  (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1819 "generated/parser.tab.c"
    break;

  case 65: /* relational: relational LE additive  */
#line 235 "src/parser.y"
                                     { (yyval.node) = mk_bin("<=", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1825 "generated/parser.tab.c"
    break;

  case 66: /* relational: relational GE additive  */
#line 236 "src/parser.y"
                                     { (yyval.node) = mk_bin(">=", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1831 "generated/parser.tab.c"
    break;

  case 67: /* additive: multiplicative  */
#line 240 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1837 "generated/parser.tab.c"
    break;

  case 68: /* additive: additive '+' multiplicative  */
#line 241 "src/parser.y"
                                     { (yyval.node) = mk_bin("+", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1843 "generated/parser.tab.c"
    break;

  case 69: /* additive: additive '-' multiplicative  */
#line 242 "src/parser.y"
                                     { (yyval.node) = mk_bin("-", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1849 "generated/parser.tab.c"
    break;

  case 70: /* multiplicative: unary  */
#line 246 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1855 "generated/parser.tab.c"
    break;

  case 71: /* multiplicative: multiplicative '*' unary  */
#line 247 "src/parser.y"
                                     { (yyval.node) = mk_bin("*", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1861 "generated/parser.tab.c"
    break;

  case 72: /* multiplicative: multiplicative '/' unary  */
#line 248 "src/parser.y"
                                     { (yyval.node) = mk_bin("/", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1867 "generated/parser.tab.c"
    break;

  case 73: /* multiplicative: multiplicative '%' unary  */
#line 249 "src/parser.y"
                                     { (yyval.node) = mk_bin("%", (yyvsp[-2].node), (yyvsp[0].node), (yylsp[-1]).first_line); }
#line 1873 "generated/parser.tab.c"
    break;

  case 74: /* unary: postfix  */
#line 253 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1879 "generated/parser.tab.c"
    break;

  case 75: /* unary: '-' unary  */
#line 254 "src/parser.y"
                   { (yyval.node) = mk_text(N_UNOP, (yylsp[-1]).first_line, "-"); (yyval.node)->a = (yyvsp[0].node); }
#line 1885 "generated/parser.tab.c"
    break;

  case 76: /* unary: '!' unary  */
#line 255 "src/parser.y"
                   { (yyval.node) = mk_text(N_UNOP, (yylsp[-1]).first_line, "!"); (yyval.node)->a = (yyvsp[0].node); }
#line 1891 "generated/parser.tab.c"
    break;

  case 77: /* unary: '&' unary  */
#line 256 "src/parser.y"
                   { (yyval.node) = mk(N_ADDR, (yylsp[-1]).first_line); (yyval.node)->a = (yyvsp[0].node); }
#line 1897 "generated/parser.tab.c"
    break;

  case 78: /* unary: INC unary  */
#line 257 "src/parser.y"
                   { (yyval.node) = mk_text(N_INCDEC, (yylsp[-1]).first_line, "++"); (yyval.node)->ival = 1; (yyval.node)->a = (yyvsp[0].node); }
#line 1903 "generated/parser.tab.c"
    break;

  case 79: /* unary: DEC unary  */
#line 258 "src/parser.y"
                   { (yyval.node) = mk_text(N_INCDEC, (yylsp[-1]).first_line, "--"); (yyval.node)->ival = 1; (yyval.node)->a = (yyvsp[0].node); }
#line 1909 "generated/parser.tab.c"
    break;

  case 80: /* unary: '*' unary  */
#line 259 "src/parser.y"
                   { ht_unsupported((yylsp[-1]).first_line, "pointer dereference"); (yyval.node) = NULL; }
#line 1915 "generated/parser.tab.c"
    break;

  case 81: /* unary: '(' type ')' unary  */
#line 261 "src/parser.y"
                   { ht_unsupported((yylsp[-3]).first_line, "type cast"); (yyval.node) = NULL; }
#line 1921 "generated/parser.tab.c"
    break;

  case 82: /* unary: '(' type '*' ')' unary  */
#line 263 "src/parser.y"
                   { ht_unsupported((yylsp[-4]).first_line, "type cast"); (yyval.node) = NULL; }
#line 1927 "generated/parser.tab.c"
    break;

  case 83: /* postfix: primary  */
#line 267 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 1933 "generated/parser.tab.c"
    break;

  case 84: /* postfix: postfix '[' expr ']'  */
#line 269 "src/parser.y"
        { (yyval.node) = mk(N_INDEX, (yylsp[-2]).first_line); (yyval.node)->a = (yyvsp[-3].node); (yyval.node)->b = (yyvsp[-1].node); }
#line 1939 "generated/parser.tab.c"
    break;

  case 85: /* postfix: postfix INC  */
#line 270 "src/parser.y"
                   { (yyval.node) = mk_text(N_INCDEC, (yylsp[0]).first_line, "++"); (yyval.node)->a = (yyvsp[-1].node); }
#line 1945 "generated/parser.tab.c"
    break;

  case 86: /* postfix: postfix DEC  */
#line 271 "src/parser.y"
                   { (yyval.node) = mk_text(N_INCDEC, (yylsp[0]).first_line, "--"); (yyval.node)->a = (yyvsp[-1].node); }
#line 1951 "generated/parser.tab.c"
    break;

  case 87: /* primary: T_ID  */
#line 275 "src/parser.y"
                          { (yyval.node) = mk_text(N_ID, (yylsp[0]).first_line, (yyvsp[0].str)); }
#line 1957 "generated/parser.tab.c"
    break;

  case 88: /* primary: T_INTLIT  */
#line 276 "src/parser.y"
                          { (yyval.node) = mk(N_INT, (yylsp[0]).first_line); (yyval.node)->ival = (yyvsp[0].ival); }
#line 1963 "generated/parser.tab.c"
    break;

  case 89: /* primary: T_FLOATLIT  */
#line 277 "src/parser.y"
                          { (yyval.node) = mk_text(N_FLOAT, (yylsp[0]).first_line, (yyvsp[0].str)); (yyval.node)->fval = atof((yyvsp[0].str)); }
#line 1969 "generated/parser.tab.c"
    break;

  case 90: /* primary: T_STRING  */
#line 278 "src/parser.y"
                          { (yyval.node) = mk_text(N_STR, (yylsp[0]).first_line, (yyvsp[0].str)); }
#line 1975 "generated/parser.tab.c"
    break;

  case 91: /* primary: NULLT  */
#line 279 "src/parser.y"
                          { (yyval.node) = mk(N_NULL, (yylsp[0]).first_line); }
#line 1981 "generated/parser.tab.c"
    break;

  case 92: /* primary: '(' expr ')'  */
#line 280 "src/parser.y"
                          { (yyval.node) = (yyvsp[-1].node); }
#line 1987 "generated/parser.tab.c"
    break;

  case 93: /* primary: T_ID '(' arg_list_opt ')'  */
#line 282 "src/parser.y"
        { (yyval.node) = mk_text(N_CALL, (yylsp[-3]).first_line, (yyvsp[-3].str)); (yyval.node)->a = (yyvsp[-1].node); }
#line 1993 "generated/parser.tab.c"
    break;

  case 94: /* arg_list_opt: %empty  */
#line 286 "src/parser.y"
                          { (yyval.node) = NULL; }
#line 1999 "generated/parser.tab.c"
    break;

  case 95: /* arg_list_opt: arg_list  */
#line 287 "src/parser.y"
                          { (yyval.node) = (yyvsp[0].node); }
#line 2005 "generated/parser.tab.c"
    break;

  case 96: /* arg_list: assign_expr  */
#line 291 "src/parser.y"
                                     { (yyval.node) = (yyvsp[0].node); }
#line 2011 "generated/parser.tab.c"
    break;

  case 97: /* arg_list: arg_list ',' assign_expr  */
#line 292 "src/parser.y"
                                     { (yyval.node) = list_append((yyvsp[-2].node), (yyvsp[0].node)); }
#line 2017 "generated/parser.tab.c"
    break;


#line 2021 "generated/parser.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  yyerror_range[1] = yylloc;
  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval, &yylloc);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  yyerror_range[2] = yylloc;
  ++yylsp;
  YYLLOC_DEFAULT (*yylsp, yyerror_range, 2);

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 295 "src/parser.y"


void yyerror(const char *msg)
{
    (void)msg;
    if (yytext && yytext[0])
        fprintf(stderr, "Syntax error at line %d: unexpected '%s'\n", yylineno, yytext);
    else
        fprintf(stderr, "Syntax error at line %d: unexpected end of input\n", yylineno);
    g_errors++;
}
