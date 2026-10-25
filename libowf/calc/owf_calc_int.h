/*
 * libowf: the formula engine's own types, shared by its files and not part
 * of the public interface (that is owf_calc.h).
 *
 *   owf_calc_parse.c  the tokenizer, the compiler (formula text to
 *                     bytecode) and the reference rewriting for move and
 *                     shift
 *   owf_calc_eval.c   values, conversions, operators and the bytecode
 *                     machine
 *   owf_calc_fn.c     the functions
 *   owf_calc_fmt.c    number formats, typed input, dates
 *   owf_calc.c        the engine: cells, dependencies, recalculation, edits
 *
 * A formula compiles to bytecode in reverse Polish order: operands push a
 * value, operators and functions pop theirs and push one. Nothing recurses
 * while it runs; the parser is an operator-precedence (shunting-yard) one
 * with its stacks on the heap.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_CALC_INT_H
#define OWF_CALC_INT_H

#include "owf_internal.h"
#include "owf_calc.h"

/* ---- Limits (Excel's where it has one) ---- */

#define CALC_MAX_ROWS 1048576u           /* rows and columns of a sheet */
#define CALC_MAX_COLS 16384u
#define CALC_MAX_FORMULA 8192            /* bytes of a formula's text */
#define CALC_MAX_NEST 64                 /* brackets and calls inside each other */
#define CALC_MAX_ARGS 255
#define CALC_MAX_TEXT 32767              /* characters of a text value */
#define CALC_MAX_NAME_DEPTH 8            /* defined names inside names */
#define CALC_ARENA_MAX (1024L * 1024L)   /* a formula's working values: past it, #NUM! */
#define CALC_SHEET_NAME 128              /* bytes of a sheet's name (31 characters in Excel) */
#define CALC_BOOK_NAME 256               /* bytes of another book's name */

/* ---- Values while a formula is worked out ---- */

enum { CV_EMPTY, CV_NUM, CV_STR, CV_BOOL, CV_ERR, CV_REF, CV_ARRAY, CV_MISSING };

/* A rectangle of cells on one sheet, or on sheets s0 to s1 (3-D). */
typedef struct {
    int s0, s1;
    unsigned r0, c0, r1, c1;
} calc_area;

typedef struct calc_array calc_array;

typedef struct {
    unsigned char type;          /* CV_... */
    union {
        double num;              /* NUM; BOOL: 0 or 1 */
        int err;                 /* ERR: OWF_ERROR_... */
        struct {
            const char *p;       /* UTF-8, not NUL-terminated */
            unsigned len;        /* bytes */
        } str;
        struct {
            const calc_area *a;  /* one or more (a union, a 3-D reference) */
            int n;
        } ref;
        calc_array *arr;
    } u;
} calc_value;

/* Elements are plain values: EMPTY, NUM, STR, BOOL or ERR. */
struct calc_array {
    unsigned rows, cols;
    calc_value v[1];             /* rows * cols of them, by row */
};

/* Working memory for one formula: freed all at once after it. */
typedef struct calc_block calc_block;
typedef struct {
    calc_block *head;
    long total;
} calc_arena;

void *calc_arena_alloc(calc_arena *arena, size_t n);
void calc_arena_reset(calc_arena *arena);   /* keeps the first block */
void calc_arena_free(calc_arena *arena);

/* ---- The engine (owf_calc.c) ---- */

/* A compiled formula. */
enum {
    PROG_UNKNOWN = 1,            /* a function the engine does not have */
    PROG_EXTERN = 2,             /* a reference to another book */
    PROG_VOLATILE = 4,           /* RAND, NOW, TODAY */
    PROG_BADARGS = 8             /* a known function with the wrong count of arguments */
};

typedef struct {
    unsigned char *code;         /* malloc'd */
    unsigned len;
    unsigned stack;              /* the most values on the stack at once */
    unsigned flags;              /* PROG_... */
} calc_prog;

/* What a formula is worked out for. */
typedef struct calc_ctx {
    owf_calc *calc;              /* for RAND's state and the clock; may be NULL */
    const owf_book *book;
    int sheet;
    unsigned row, col;
    int array;                   /* an array formula's cell: (adr, adc) into its result */
    unsigned adr, adc;
    calc_arena *arena;
    int failed;                  /* memory ran out */
    int too_big;                 /* the arena's limit was reached */
    int dmy;                     /* how text dates read: OWF_CALC_DMY or _MDY */
    int actx;                    /* the function being called is in an array context */
    long used_rows, used_cols;   /* the book's last row and column used, plus 1; -1: not known yet */
    int pending;                 /* OFFSET or INDIRECT reached a formula this recalc has still to work
                                    out: this one waits for it (calc_note_reach) */
} calc_ctx;

/* A context for a formula in (sheet, row, col). */
void calc_ctx_init(calc_ctx *ctx, owf_calc *calc, const owf_book *book, calc_arena *arena, int sheet,
                   unsigned row, unsigned col);

/* RAND's next number, from 0 up to 1. */
double calc_random(calc_ctx *ctx);
/* The serial date and time now (local), for NOW and TODAY. */
double calc_now(calc_ctx *ctx);
/* The engine's note of the book's rows and columns used ([0], [1]; -1 not
 * known), kept between formulas; NULL without an engine. */
long *calc_used_cache(owf_calc *calc);
/* OFFSET and INDIRECT make references as they are worked out, so the
 * dependency index cannot know them (they are volatile, as in Excel): each
 * says here what it reached. A formula there that this recalc has still to
 * work out sets ctx->pending, and the formula is worked out again after it
 * (once nothing else can go, it is worked out as it is). */
void calc_note_reach(calc_ctx *ctx, const calc_area *a);
/* The bytes the engine holds (its nodes, index and code), for the tests. */
size_t calc_memory(const owf_calc *calc);

/* ---- Bytecode (owf_calc_parse.c writes it, owf_calc_eval.c runs it) ---- */

enum {
    OP_NUM = 1,                  /* double */
    OP_STR,                      /* u16 length, bytes */
    OP_BOOL,                     /* u8 */
    OP_ERR,                      /* u8 */
    OP_MISSING,                  /* an argument left out: IF(A1,,2) */
    OP_REF,                      /* u16 sheet, u32 row, u16 col */
    OP_AREA,                     /* u16 s0, u16 s1, u32 r0, u16 c0, u32 r1, u16 c1 */
    OP_ARRAY,                    /* u16 rows, u16 cols: pops rows * cols constants */
    OP_FUNC,                     /* u16 function, u8 count of arguments */
    OP_UNKNOWN,                  /* u8 count: a function the engine lacks: #NAME? */
    OP_EXTERN,                   /* another book's reference: #REF! */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_CAT,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_NEG, OP_PLUS, OP_PCT,
    OP_RANGE, OP_UNION, OP_ISECT,
    OP_LAST
};
/* On operators and functions: worked out as arrays (an array formula, or
 * inside SUMPRODUCT), so that a range is all its cells, not the one in the
 * formula's row or column. */
#define OP_ARRAYCTX 0x80
#define OP_CODE(b) ((b) & 0x7F)

/* Compiling. sheet: the formula's sheet (a reference without a sheet is
 * on it). book may be NULL to check the syntax only (sheets and names are
 * then none). array: an array formula. A function the engine lacks compiles
 * (PROG_UNKNOWN); the engine decides whether the cell keeps its value.
 * OWF_OK, OWF_ERR_FORMAT (*at, if not NULL: the byte offset where it went
 * wrong), OWF_ERR_MEMORY. */
int calc_compile(const owf_book *book, int sheet, const char *text, int array, calc_prog *prog, int *at);
void calc_prog_free(calc_prog *prog);

/* The areas a compiled formula reads (not those only ROW, COLUMN, ROWS
 * and COLUMNS look at); calls back for each. */
typedef void (*calc_area_fn)(void *user, const calc_area *area);
void calc_prog_areas(const calc_prog *prog, calc_area_fn fn, void *user);

/* Runs a formula: the result is a plain value (a reference is read, an
 * array gives the element for the cell). Strings point into the arena or
 * the book. */
void calc_run(calc_ctx *ctx, const calc_prog *prog, calc_value *result);

/* ---- Tokens (owf_calc_parse.c) ---- */

enum {
    TK_END, TK_NUM, TK_STR, TK_BOOL, TK_ERR, TK_REF, TK_NAME, TK_FUNC, TK_OP, TK_PCT,
    TK_LPAREN, TK_RPAREN, TK_COMMA, TK_SEMI, TK_LBRACE, TK_RBRACE, TK_BAD
};
enum { RK_CELL, RK_AREA, RK_COLS, RK_ROWS, RK_ERR };
/* abs bits: which parts carry a $ */
enum { ABS_R0 = 1, ABS_C0 = 2, ABS_R1 = 4, ABS_C1 = 8 };

typedef struct {
    int type;                    /* TK_... */
    const char *start, *end;     /* the token's text */
    int space_before;            /* spaces or line breaks before it */
    double num;                  /* NUM */
    int value;                   /* BOOL: 0 or 1; ERR: OWF_ERROR_...; OP: OP_... */
    /* REF, and NAME with a sheet: */
    const char *sheet, *sheet_end;   /* the prefix before the '!', quotes and all; NULL: none */
    const char *body;            /* after the '!' */
    int kind;                    /* RK_... */
    unsigned r0, c0, r1, c1;     /* from 0 */
    unsigned abs;                /* ABS_... */
} calc_token;

/* The token at p; *p moves past it. */
void calc_next_token(const char **p, calc_token *t);

/* The sheet prefix of a token, unquoted: book (NULL for this book), first
 * and last sheet (last == first unless 3-D). 0 when it is not one. */
typedef struct {
    char book[CALC_BOOK_NAME];
    char first[CALC_SHEET_NAME];
    char last[CALC_SHEET_NAME];
    int has_book;
    char text[CALC_BOOK_NAME + 2 * CALC_SHEET_NAME];   /* working space */
} calc_sheetref;
int calc_split_sheet(const calc_token *t, calc_sheetref *out);
/* A sheet by name (any case), or -1. */
int calc_find_sheet(const owf_book *book, const char *name);

/* Rewrites the references in a formula: for each one (on sheet `sheet` when
 * it has no prefix), fn adjusts the rows and columns in place and returns 1
 * to keep it, 0 to make it #REF!; it may set *to (else -1) to the sheet the
 * reference is on now, written as a prefix unless that is `home`, where
 * the formula is (and it had none). 3-D references and other books' stay.
 * Text that won't tokenize comes back as it was. NULL when memory runs
 * out. */
typedef int (*calc_ref_fn)(void *user, int ref_sheet, int kind, unsigned *r0, unsigned *c0,
                           unsigned *r1, unsigned *c1, unsigned abs, int *to);
char *calc_rewrite(const owf_book *book, int sheet, int home, const char *text, calc_ref_fn fn, void *user);
/* The text with every sheet prefix naming old_name (any case) naming
 * new_name, quoted as it needs. NULL when memory runs out. */
char *calc_rename_sheet_in(const char *text, const char *old_name, const char *new_name);
/* "Sheet2!", "'Q1 sales'!", "'A:B c'!" (last NULL for one sheet). */
void calc_put_sheet_prefix(owf_buf *b, const char *first, const char *last);

/* A typed formula as Excel keeps it: function names, references,
 * booleans and errors in capitals ("sum(a1:b2)" is "SUM(A1:B2)"); text,
 * names and spaces as typed. From malloc; NULL when memory runs out. */
char *calc_normalize(const char *text);

/* ---- Functions (owf_calc_fn.c) ---- */

typedef void (*calc_fn)(calc_ctx *ctx, calc_value *args, int argc, calc_value *result);

enum {
    FN_VOLATILE = 1,             /* worked out at every recalc */
    FN_ADDRESS = 2               /* reads only where its reference is (ROW, COLUMN, ROWS, COLUMNS) */
};

typedef struct {
    const char *name;
    unsigned char min, max;      /* arguments; max 255 for "as many as Excel allows" */
    const char *args;            /* each argument's class, the last repeated:
                                    V a value (a range gives the cell in the formula's
                                      row or column; arrays are worked element by element),
                                    R a reference or array as it is,
                                    A an array (what is inside is worked out as arrays) */
    unsigned char flags;         /* FN_... */
    calc_fn fn;
} calc_function;

/* A function by name (any case, without "_xlfn."), or -1. */
int calc_function_find(const char *name, size_t length);
const calc_function *calc_function_at(int id);
int calc_function_count(void);
/* 'V', 'R' or 'A' for argument i (from 0). */
int calc_function_arg_class(const calc_function *f, int i);

/* ---- Values and conversions (owf_calc_eval.c) ---- */

void cv_num(calc_value *v, double n);     /* #NUM! for infinities and NaN */
void cv_bool(calc_value *v, int b);
void cv_err(calc_value *v, int err);
void cv_str(calc_value *v, const char *p, unsigned len);
void cv_empty(calc_value *v);
/* A copy of len bytes in the arena; #VALUE! for more than CALC_MAX_TEXT
 * characters, #NUM! when the arena is full. */
void cv_str_copy(calc_ctx *ctx, calc_value *v, const char *p, size_t len);

/* The value in a cell (EMPTY when there is none). */
void calc_cell_value(calc_ctx *ctx, int sheet, unsigned row, unsigned col, calc_value *out);
/* A reference or array as one value: a one-cell reference is that cell; a
 * bigger one gives the cell in the formula's row or column (implicit
 * intersection) or #VALUE!; an array gives its first element. Plain values
 * come back as they are; MISSING as EMPTY. */
void calc_scalar(calc_ctx *ctx, const calc_value *in, calc_value *out);
/* A value as an array: a reference's cells, a plain value as 1 x 1. */
int calc_to_array(calc_ctx *ctx, const calc_value *in, calc_array **out);
calc_array *calc_array_new(calc_ctx *ctx, unsigned rows, unsigned cols);
/* An operand or value argument: a reference becomes its cells as an
 * array (actx, and more than one cell) or the one cell for the formula. */
void calc_operand(calc_ctx *ctx, calc_value *v, int actx);
/* Element (i, j) of an array, a single row or column (or a plain value)
 * repeating as Excel stretches them; #N/A past the end of a bigger one. */
void calc_element(const calc_value *v, unsigned i, unsigned j, calc_value *out);
/* Grows rows and cols to an array's. */
void calc_dims(const calc_value *v, unsigned *rows, unsigned *cols);
/* a^b with Excel's errors (0^0 #NUM!, 0^-1 #DIV/0!). */
void calc_power(double a, double b, calc_value *r);
/* Plain-value conversions as Excel makes them; 0 or an OWF_ERROR_. */
int calc_to_number(calc_ctx *ctx, const calc_value *v, double *out);
int calc_to_bool(calc_ctx *ctx, const calc_value *v, int *out);
/* Text of a plain value (numbers as General); the text may be in the
 * arena. */
int calc_to_text(calc_ctx *ctx, const calc_value *v, const char **p, unsigned *len);
/* Excel's comparison of two plain values: -1, 0, 1 (numbers < text <
 * booleans; text without case). *err set for an error operand. */
int calc_compare(const calc_value *a, const calc_value *b, int *err);
/* Numbers equal but for the last bits, as Excel compares them; a sum that
 * is that close to 0 is 0. */
int calc_approx_equal(double a, double b);
double calc_approx_add(double a, double b);
/* floor and ceil that take a number a hair from a whole one as that one. */
double calc_approx_floor(double x);
double calc_approx_ceil(double x);

/* Calls fn for each cell there is in an area (empty ones are not there),
 * by sheet, row, column. Stops when fn returns nonzero, and returns that. */
typedef int (*calc_cell_fn)(void *user, const owf_cell *cell, int sheet);
int calc_area_each(calc_ctx *ctx, const calc_area *area, calc_cell_fn fn, void *user);
/* An area's last row and column, cut to the last the book uses (a whole
 * column is a million rows, almost all empty). */
void calc_area_clip(calc_ctx *ctx, const calc_area *a, unsigned *r1, unsigned *c1);
/* A value made from a cell (EMPTY for NULL). */
void calc_value_of_cell(const owf_cell *cell, calc_value *out);

/* ---- Text (owf_calc_eval.c) ---- */

/* Characters (code points) in UTF-8 text. */
unsigned calc_utf8_count(const char *p, unsigned len);
/* The byte offset of character n (from 0); len when past the end. */
unsigned calc_utf8_offset(const char *p, unsigned len, unsigned n);
unsigned long calc_fold(unsigned long c);       /* lower case, for comparing */
unsigned long calc_upper(unsigned long c);
unsigned long calc_lower(unsigned long c);
int calc_is_letter(unsigned long c);
/* Two texts the same but for case. */
int calc_text_equal_nocase(const char *a, unsigned alen, const char *b, unsigned blen);
int calc_text_compare_nocase(const char *a, unsigned alen, const char *b, unsigned blen);
/* Excel's wildcards: ? one character, * any, ~ before either means it.
 * Without case. */
int calc_wild_match(const char *pat, unsigned plen, const char *s, unsigned slen);
/* Where pat (with wildcards) first matches in s from byte start: the byte
 * offset, or -1. */
long calc_wild_search(const char *pat, unsigned plen, const char *s, unsigned slen, unsigned start);
void calc_put_utf8(owf_buf *b, unsigned long c);

/* ---- Numbers, dates and formats (owf_calc_fmt.c) ---- */

/* Text as a number, as typed input or VALUE reads it: "1,234.5",
 * "50%", "£3", "(12)", "1E3", dates and times. 1 when it is one; *kind
 * says what it looked like (for a typed cell's format). */
enum { NK_NUMBER, NK_PERCENT, NK_CURRENCY, NK_DATE, NK_TIME, NK_DATETIME };
int calc_parse_number(const char *p, size_t len, int dmy, int date1904, double *out, int *kind, int *decimals,
                      char *currency, size_t currency_size);
/* Days since the epoch for a date; 0 when it is outside Excel's range
 * (year 0 to 9999 after Excel's own adjustments). */
int calc_date_serial(long year, long month, long day, int date1904, double *serial);
/* A serial date as year, month, day (Excel's 29 February 1900 included). */
void calc_serial_date(long serial, int date1904, int *year, int *month, int *day);
/* Number as Excel's General text ("0.1", "1E+20"). */
void calc_number_text(double n, char *out);
/* Formats a plain value through a format's text. */
int calc_format(const char *format, const calc_value *v, int date1904, int width, owf_buf *out,
                unsigned long *colour);

#endif
