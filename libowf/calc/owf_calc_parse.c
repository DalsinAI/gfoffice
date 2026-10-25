/*
 * libowf: formulas as text. The tokenizer reads Excel's A1 syntax as the
 * files keep it (without the "="); the compiler turns the tokens into
 * bytecode with an operator-precedence (shunting-yard) parser whose stacks
 * are on the heap, so a deeply nested formula costs memory, not stack; and
 * the rewriter adjusts the references in a formula's text for copy, fill
 * and inserted or deleted rows and columns.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_int.h"

#include <stdlib.h>
#include <string.h>

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_alpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/* A name's characters, and an unquoted sheet name's. */
static int is_name_char(int c)
{
    return is_alpha(c) || is_digit(c) || c == '_' || c == '.' || c == '\\' || c == '?' || (unsigned char)c >= 0x80;
}

static int is_name_start(int c)
{
    return is_alpha(c) || c == '_' || c == '\\' || (unsigned char)c >= 0x80;
}

static int same_nocase(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!a[i] || owf_upper((unsigned char)a[i]) != owf_upper((unsigned char)b[i]))
            return 0;
    return 1;
}

/* ---- References ---- */

/* A cell: "$A$1", "a1"; NULL when it is not one. */
static const char *cell_part(const char *p, unsigned *row, unsigned *col, unsigned *abs, unsigned rbit,
                             unsigned cbit)
{
    unsigned long c = 0, r = 0;
    unsigned a = 0;
    int n = 0;
    if (*p == '$') {
        a |= cbit;
        p++;
    }
    while (is_alpha(*p)) {
        if (++n > 3)
            return NULL;
        c = c * 26 + (unsigned long)(owf_upper((unsigned char)*p) - 'A' + 1);
        p++;
    }
    if (!n)
        return NULL;
    if (*p == '$') {
        a |= rbit;
        p++;
    }
    if (!is_digit(*p))
        return NULL;
    for (n = 0; is_digit(*p); p++)
        if (++n <= 8)
            r = r * 10 + (unsigned long)(*p - '0');
    if (n > 7 || r < 1 || r > CALC_MAX_ROWS || c > CALC_MAX_COLS)
        return NULL;
    *row = (unsigned)(r - 1);
    *col = (unsigned)(c - 1);
    *abs |= a;
    return p;
}

/* A whole column's letters: "$C" */
static const char *col_part(const char *p, unsigned *col, unsigned *abs, unsigned cbit)
{
    unsigned long c = 0;
    unsigned a = 0;
    int n = 0;
    if (*p == '$') {
        a = cbit;
        p++;
    }
    while (is_alpha(*p)) {
        if (++n > 3)
            return NULL;
        c = c * 26 + (unsigned long)(owf_upper((unsigned char)*p) - 'A' + 1);
        p++;
    }
    if (!n || c > CALC_MAX_COLS || is_digit(*p) || *p == '$')
        return NULL;
    *col = (unsigned)(c - 1);
    *abs |= a;
    return p;
}

/* A whole row's number: "$3" */
static const char *row_part(const char *p, unsigned *row, unsigned *abs, unsigned rbit)
{
    unsigned long r = 0;
    unsigned a = 0;
    int n = 0;
    if (*p == '$') {
        a = rbit;
        p++;
    }
    for (; is_digit(*p); p++)
        if (++n <= 8)
            r = r * 10 + (unsigned long)(*p - '0');
    if (!n || n > 7 || r < 1 || r > CALC_MAX_ROWS)
        return NULL;
    *row = (unsigned)(r - 1);
    *abs |= a;
    return p;
}

/* What may not follow a reference (it would be a name or a call). */
static int ends_ref(int c)
{
    return !is_name_char(c) && c != '(' && c != '!' && c != '$';
}

static void order(unsigned *a, unsigned *b, unsigned *abs, unsigned abit, unsigned bbit)
{
    if (*a > *b) {
        unsigned t = *a, fa = *abs & abit, fb = *abs & bbit;
        *a = *b;
        *b = t;
        *abs = (*abs & ~(abit | bbit)) | (fa ? bbit : 0) | (fb ? abit : 0);
    }
}

/* A reference's body (after any sheet): "#REF!", a cell, a range of
 * cells, of whole columns or of whole rows. NULL when it is none. */
static const char *ref_body(const char *p, calc_token *t)
{
    const char *q, *e;
    unsigned a = 0, b;
    if (same_nocase(p, "#REF!", 5)) {
        t->kind = RK_ERR;
        return p + 5;
    }
    if ((q = cell_part(p, &t->r0, &t->c0, &a, ABS_R0, ABS_C0)) != NULL) {
        b = a;
        if (*q == ':' && (e = cell_part(q + 1, &t->r1, &t->c1, &b, ABS_R1, ABS_C1)) != NULL && ends_ref(*e)) {
            t->kind = RK_AREA;
            order(&t->r0, &t->r1, &b, ABS_R0, ABS_R1);
            order(&t->c0, &t->c1, &b, ABS_C0, ABS_C1);
            t->abs = b;
            return e;
        }
        if (!ends_ref(*q))
            return NULL;
        t->kind = RK_CELL;
        t->r1 = t->r0;
        t->c1 = t->c0;
        t->abs = a;
        return q;
    }
    a = 0;
    if ((q = col_part(p, &t->c0, &a, ABS_C0)) != NULL && *q == ':' &&
        (e = col_part(q + 1, &t->c1, &a, ABS_C1)) != NULL && ends_ref(*e)) {
        t->kind = RK_COLS;
        t->r0 = 0;
        t->r1 = CALC_MAX_ROWS - 1;
        order(&t->c0, &t->c1, &a, ABS_C0, ABS_C1);
        t->abs = a | ABS_R0 | ABS_R1;
        return e;
    }
    a = 0;
    if ((q = row_part(p, &t->r0, &a, ABS_R0)) != NULL && *q == ':' &&
        (e = row_part(q + 1, &t->r1, &a, ABS_R1)) != NULL && ends_ref(*e) && *e != '.') {
        t->kind = RK_ROWS;
        t->c0 = 0;
        t->c1 = CALC_MAX_COLS - 1;
        order(&t->r0, &t->r1, &a, ABS_R0, ABS_R1);
        t->abs = a | ABS_C0 | ABS_C1;
        return e;
    }
    return NULL;
}

/* After a sheet's prefix (p just past the '!'): a reference, or a name
 * that belongs to that sheet. */
static const char *after_sheet(const char *p, calc_token *t)
{
    const char *e;
    t->body = p;
    if ((e = ref_body(p, t)) != NULL) {
        t->type = TK_REF;
        return e;
    }
    if (is_name_start(*p)) {
        for (e = p; is_name_char(*e); e++)
            ;
        t->type = TK_NAME;
        return e;
    }
    t->type = TK_BAD;
    return p;
}

static const struct {
    const char *text;
    int error;
} error_words[] = {
    { "#NULL!", OWF_ERROR_NULL }, { "#DIV/0!", OWF_ERROR_DIV0 }, { "#VALUE!", OWF_ERROR_VALUE },
    { "#REF!", OWF_ERROR_REF }, { "#NAME?", OWF_ERROR_NAME }, { "#NUM!", OWF_ERROR_NUM },
    { "#N/A", OWF_ERROR_NA },
};

void calc_next_token(const char **pp, calc_token *t)
{
    const char *p = *pp, *q, *e;
    size_t i;

    t->space_before = 0;
    t->sheet = t->sheet_end = t->body = NULL;
    t->kind = RK_CELL;
    t->abs = 0;
    t->num = 0;
    t->value = 0;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        t->space_before = 1;
        p++;
    }
    t->start = p;
    t->type = TK_BAD;
    switch (*p) {
    case 0:
        t->type = TK_END;
        break;
    case '"':
        for (q = p + 1;; q++) {
            if (!*q) {
                q = NULL;
                break;
            }
            if (*q == '"') {
                if (q[1] != '"')
                    break;
                q++;
            }
        }
        if (q) {
            t->type = TK_STR;
            p = q + 1;
        }
        break;
    case '#':
        for (i = 0; i < sizeof error_words / sizeof error_words[0]; i++) {
            size_t n = strlen(error_words[i].text);
            if (same_nocase(p, error_words[i].text, n)) {
                t->type = TK_ERR;
                t->value = error_words[i].error;
                p += n;
                break;
            }
        }
        break;
    case '\'':                           /* 'Q1 sales'!A1 */
        for (q = p + 1;; q++) {
            if (!*q) {
                q = NULL;
                break;
            }
            if (*q == '\'') {
                if (q[1] != '\'')
                    break;
                q++;
            }
        }
        if (q && q[1] == '!') {
            t->sheet = p;
            t->sheet_end = q + 1;
            p = after_sheet(q + 2, t);
        }
        break;
    case '[':                            /* [Other.xls]Sheet1!A1 */
        for (q = p + 1; *q && *q != ']'; q++)
            ;
        if (*q == ']') {
            for (e = q + 1; is_name_char(*e); e++)
                ;
            if (*e == '!') {
                t->sheet = p;
                t->sheet_end = e;
                p = after_sheet(e + 1, t);
            }
        }
        break;
    case '$':
        if ((q = ref_body(p, t)) != NULL) {
            t->type = TK_REF;
            p = q;
        }
        break;
    case '+': t->type = TK_OP; t->value = OP_ADD; p++; break;
    case '-': t->type = TK_OP; t->value = OP_SUB; p++; break;
    case '*': t->type = TK_OP; t->value = OP_MUL; p++; break;
    case '/': t->type = TK_OP; t->value = OP_DIV; p++; break;
    case '^': t->type = TK_OP; t->value = OP_POW; p++; break;
    case '&': t->type = TK_OP; t->value = OP_CAT; p++; break;
    case '=': t->type = TK_OP; t->value = OP_EQ; p++; break;
    case ':': t->type = TK_OP; t->value = OP_RANGE; p++; break;
    case '<':
        t->type = TK_OP;
        if (p[1] == '=') {
            t->value = OP_LE;
            p += 2;
        } else if (p[1] == '>') {
            t->value = OP_NE;
            p += 2;
        } else {
            t->value = OP_LT;
            p++;
        }
        break;
    case '>':
        t->type = TK_OP;
        if (p[1] == '=') {
            t->value = OP_GE;
            p += 2;
        } else {
            t->value = OP_GT;
            p++;
        }
        break;
    case '%': t->type = TK_PCT; p++; break;
    case '(': t->type = TK_LPAREN; p++; break;
    case ')': t->type = TK_RPAREN; p++; break;
    case ',': t->type = TK_COMMA; p++; break;
    case ';': t->type = TK_SEMI; p++; break;
    case '{': t->type = TK_LBRACE; p++; break;
    case '}': t->type = TK_RBRACE; p++; break;
    default:
        if (is_digit(*p) || (*p == '.' && is_digit(p[1]))) {
            char num[64];
            size_t n;
            if ((q = ref_body(p, t)) != NULL && t->kind == RK_ROWS) {
                t->type = TK_REF;
                p = q;
                break;
            }
            q = p;
            while (is_digit(*q))
                q++;
            if (*q == '.')
                for (q++; is_digit(*q); q++)
                    ;
            if ((*q == 'e' || *q == 'E') && (is_digit(q[1]) || ((q[1] == '+' || q[1] == '-') && is_digit(q[2])))) {
                q += 2;
                while (is_digit(*q))
                    q++;
            }
            n = (size_t)(q - p);
            if (n >= sizeof num || is_name_char(*q))
                break;
            memcpy(num, p, n);
            num[n] = 0;
            t->num = strtod(num, NULL);
            t->type = TK_NUM;
            p = q;
            break;
        }
        if (is_name_start(*p)) {
            for (q = p; is_name_char(*q); q++)
                ;
            if (*q == '!') {             /* Sheet2!A1 */
                t->sheet = p;
                t->sheet_end = q;
                p = after_sheet(q + 1, t);
                break;
            }
            if (*q == ':' && is_name_start(q[1])) {   /* Sheet1:Sheet3!A1 */
                for (e = q + 1; is_name_char(*e); e++)
                    ;
                if (*e == '!') {
                    t->sheet = p;
                    t->sheet_end = e;
                    p = after_sheet(e + 1, t);
                    break;
                }
            }
            if (*q == '(') {
                t->type = TK_FUNC;
                p = q;
                break;
            }
            if ((e = ref_body(p, t)) != NULL) {
                t->type = TK_REF;
                p = e;
                break;
            }
            if ((q - p == 4 && same_nocase(p, "TRUE", 4)) || (q - p == 5 && same_nocase(p, "FALSE", 5))) {
                t->type = TK_BOOL;
                t->value = q - p == 4;
                p = q;
                break;
            }
            t->type = TK_NAME;
            p = q;
        }
        break;
    }
    t->end = p;
    *pp = p;
}

static int copy_name(char *out, size_t size, const char *s, const char *e)
{
    size_t n = (size_t)(e - s);
    if (n >= size)
        return 0;
    memcpy(out, s, n);
    out[n] = 0;
    return 1;
}

int calc_split_sheet(const calc_token *t, calc_sheetref *out)
{
    char *text = out->text;
    const char *s = t->sheet, *e = t->sheet_end, *close, *colon;
    size_t n = 0;

    out->book[0] = out->first[0] = out->last[0] = 0;
    out->has_book = 0;
    if (!s)
        return 0;
    if (*s == '\'') {                    /* unquote */
        for (s++; s < e - 1; s++) {
            if (*s == '\'' && s[1] == '\'')
                s++;
            if (n + 1 >= sizeof out->text)
                return 0;
            text[n++] = *s;
        }
    } else {
        if ((size_t)(e - s) >= sizeof out->text)
            return 0;
        memcpy(text, s, (size_t)(e - s));
        n = (size_t)(e - s);
    }
    text[n] = 0;
    s = text;
    close = strchr(text, ']');
    if (strchr(text, '[') && close) {    /* [Book.xls]Sheet, C:\dir\[Book.xls]Sheet */
        const char *open = strchr(text, '[');
        if (!copy_name(out->book, sizeof out->book, open + 1, close))
            return 0;
        out->has_book = 1;
        s = close + 1;
    }
    colon = strchr(s, ':');
    if (colon && !out->has_book) {
        if (!copy_name(out->first, sizeof out->first, s, colon) ||
            !copy_name(out->last, sizeof out->last, colon + 1, s + strlen(s)))
            return 0;
    } else {
        if (!copy_name(out->first, sizeof out->first, s, s + strlen(s)))
            return 0;
        strcpy(out->last, out->first);
    }
    return 1;
}

int calc_find_sheet(const owf_book *book, const char *name)
{
    int i;
    if (!book)
        return -1;
    for (i = 0; i < book->nsheets; i++)
        if (owf_same_word(book->sheets[i].name, name))
            return i;
    return -1;
}

/* ---- The compiler ---- */

enum { SY_OP, SY_PAREN, SY_FUNC };

/* Excel's precedence, loosest first: comparisons, &, + -, * /, ^, %, unary
 * minus, then the reference operators: union (,), intersection ( ), range (:). */
enum { P_NONE, P_CMP, P_CAT, P_ADD, P_MUL, P_POW, P_PCT, P_NEG, P_UNION, P_ISECT, P_RANGE };

typedef struct {
    unsigned char kind;          /* SY_... */
    unsigned char op;            /* SY_OP: OP_... */
    unsigned char prec;
    unsigned char ctx;           /* worked as arrays: the operator; a frame's arguments */
    unsigned char parent_ctx;    /* SY_FUNC: where the call is */
    int fn;                      /* SY_FUNC: the function, -1 when unknown */
    int argc;
} sy_item;

typedef struct {
    const owf_book *book;
    int sheet;                   /* a reference without a sheet is on this one */
    owf_buf out;
    unsigned depth, maxdepth;
    unsigned flags;              /* PROG_... */
    int names;                   /* defined names inside each other */
    int error;                   /* OWF_ERR_FORMAT or OWF_ERR_MEMORY */
    const char *at;              /* where it went wrong */
    calc_sheetref *sr;           /* working space, on the heap: names compile inside each other */
} compiler;

static void put_u16(compiler *c, unsigned v)
{
    unsigned short s = (unsigned short)v;
    owf_buf_put(&c->out, &s, 2);
}

static void put_u32(compiler *c, unsigned long v)
{
    unsigned int s = (unsigned int)v;   /* 32 bits on every target we build for */
    owf_buf_put(&c->out, &s, 4);
}

static void push_depth(compiler *c, int change)
{
    c->depth = (unsigned)((int)c->depth + change);
    if (c->depth > c->maxdepth)
        c->maxdepth = c->depth;
}

static void emit_num(compiler *c, double n)
{
    owf_buf_putc(&c->out, OP_NUM);
    owf_buf_put(&c->out, &n, sizeof n);
    push_depth(c, 1);
}

static void emit_simple(compiler *c, int op, int value)
{
    owf_buf_putc(&c->out, op);
    if (op == OP_BOOL || op == OP_ERR)
        owf_buf_putc(&c->out, value);
    push_depth(c, 1);
}

static int fail(compiler *c, const char *at)
{
    if (!c->error) {
        c->error = OWF_ERR_FORMAT;
        c->at = at;
    }
    return 0;
}

/* A string literal, its doubled quotes made single. */
static int emit_string(compiler *c, const calc_token *t)
{
    const char *s;
    size_t n = 0, at;
    owf_buf_putc(&c->out, OP_STR);
    at = c->out.length;
    put_u16(c, 0);
    for (s = t->start + 1; s < t->end - 1; s++) {
        if (*s == '"')
            s++;
        owf_buf_putc(&c->out, *s);
        n++;
    }
    if (n > 65535)
        return fail(c, t->start);
    if (!c->out.failed) {
        unsigned short len = (unsigned short)n;
        memcpy(c->out.data + at, &len, 2);
    }
    push_depth(c, 1);
    return 1;
}

static int emit_ref(compiler *c, const calc_token *t)
{
    int s0 = c->sheet < 0 ? 0 : c->sheet, s1 = s0;
    if (t->sheet) {
        if (!calc_split_sheet(t, c->sr))
            return fail(c, t->start);
        if (c->sr->has_book) {
            c->flags |= PROG_EXTERN;
            emit_simple(c, OP_EXTERN, 0);
            return 1;
        }
        s0 = calc_find_sheet(c->book, c->sr->first);
        s1 = calc_find_sheet(c->book, c->sr->last);
        if (s0 < 0 || s1 < 0) {
            emit_simple(c, OP_ERR, OWF_ERROR_REF);
            return 1;
        }
        if (s1 < s0) {
            int k = s0;
            s0 = s1;
            s1 = k;
        }
    }
    if (t->kind == RK_ERR) {
        emit_simple(c, OP_ERR, OWF_ERROR_REF);
        return 1;
    }
    if (t->kind == RK_CELL && s0 == s1) {
        owf_buf_putc(&c->out, OP_REF);
        put_u16(c, (unsigned)s0);
        put_u32(c, t->r0);
        put_u16(c, t->c0);
    } else {
        owf_buf_putc(&c->out, OP_AREA);
        put_u16(c, (unsigned)s0);
        put_u16(c, (unsigned)s1);
        put_u32(c, t->r0);
        put_u16(c, t->c0);
        put_u32(c, t->r1);
        put_u16(c, t->c1);
    }
    push_depth(c, 1);
    return 1;
}

static int compile_expr(compiler *c, const char *text, int ctx);

/* The defined name a token stands for, or -1 (*status: 0 none, 1 found,
 * 2 another book's, 3 a sheet that is not there). */
static int find_name(compiler *c, const calc_token *t, int *status)
{
    const owf_book *book = c->book;
    const char *name = t->body ? t->body : t->start;
    char *word = c->sr->text;
    size_t n = (size_t)(t->end - name);
    int want = c->sheet, i;

    *status = 0;
    if (!book)
        return -1;
    if (t->sheet) {
        if (!calc_split_sheet(t, c->sr))
            return -1;
        if (c->sr->has_book) {
            *status = 2;
            return -1;
        }
        want = calc_find_sheet(book, c->sr->first);
        if (want < 0) {
            *status = 3;
            return -1;
        }
    }
    if (n >= sizeof c->sr->text)
        return -1;
    memcpy(word, name, n);
    word[n] = 0;
    /* the sheet's own name first, then the book's */
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].sheet == want && book->names[i].formula && owf_same_word(book->names[i].name, word)) {
            *status = 1;
            return i;
        }
    if (!t->sheet)
        for (i = 0; i < book->nnames; i++)
            if (book->names[i].sheet < 0 && book->names[i].formula && owf_same_word(book->names[i].name, word)) {
                *status = 1;
                return i;
            }
    return -1;
}

/* A defined name: its formula, compiled in its place. One that is not
 * there, or will not compile, is #NAME?. */
static int emit_name(compiler *c, const calc_token *t, int ctx)
{
    int status, found = find_name(c, t, &status), saved_sheet = c->sheet;
    size_t mark = c->out.length;
    unsigned depth = c->depth, flags = c->flags;

    if (status == 2) {
        c->flags |= PROG_EXTERN;
        emit_simple(c, OP_EXTERN, 0);
        return 1;
    }
    if (status == 3) {
        emit_simple(c, OP_ERR, OWF_ERROR_REF);
        return 1;
    }
    if (found < 0 || c->names >= CALC_MAX_NAME_DEPTH) {
        emit_simple(c, OP_ERR, OWF_ERROR_NAME);
        return 1;
    }
    c->names++;
    if (c->book->names[found].sheet >= 0)
        c->sheet = c->book->names[found].sheet;
    if (!compile_expr(c, c->book->names[found].formula, ctx) && c->error == OWF_ERR_FORMAT) {
        /* the name's formula is broken: #NAME? here, not a broken formula */
        c->error = 0;
        c->at = NULL;
        c->out.length = mark;
        c->depth = depth;
        c->flags = flags;
        emit_simple(c, OP_ERR, OWF_ERROR_NAME);
    }
    c->sheet = saved_sheet;
    c->names--;
    return !c->error;
}

/* {1,2;3,4}: constants only; p is just past the '{'. */
static int emit_array(compiler *c, const char **p)
{
    calc_token t;
    unsigned rows = 1, cols = 0, col = 0;
    for (;;) {
        int negative = 0;
        calc_next_token(p, &t);
        if (t.type == TK_OP && (t.value == OP_SUB || t.value == OP_ADD)) {
            negative = t.value == OP_SUB;
            calc_next_token(p, &t);
            if (t.type != TK_NUM)
                return fail(c, t.start);
        }
        switch (t.type) {
        case TK_NUM: emit_num(c, negative ? -t.num : t.num); break;
        case TK_STR:
            if (!emit_string(c, &t))
                return 0;
            break;
        case TK_BOOL: emit_simple(c, OP_BOOL, t.value); break;
        case TK_ERR: emit_simple(c, OP_ERR, t.value); break;
        default: return fail(c, t.start);
        }
        col++;
        calc_next_token(p, &t);
        if (t.type == TK_COMMA)
            continue;
        if (rows == 1)
            cols = col;
        else if (col != cols)
            return fail(c, t.start);
        if (t.type == TK_RBRACE)
            break;
        if (t.type != TK_SEMI || rows >= 65535)
            return fail(c, t.start);
        rows++;
        col = 0;
    }
    if (cols > 65535)
        return fail(c, *p);
    owf_buf_putc(&c->out, OP_ARRAY);
    put_u16(c, rows);
    put_u16(c, cols);
    push_depth(c, 1 - (int)(rows * cols));
    return 1;
}

typedef struct {
    sy_item *items;
    int n, cap;
} sy_stack;

static int sy_push(compiler *c, sy_stack *s, const sy_item *it)
{
    if (s->n == s->cap) {
        int want = s->cap ? s->cap * 2 : 16;
        sy_item *p = realloc(s->items, (size_t)want * sizeof *p);
        if (!p) {
            c->error = OWF_ERR_MEMORY;
            return 0;
        }
        s->items = p;
        s->cap = want;
    }
    s->items[s->n++] = *it;
    return 1;
}

static void emit_op(compiler *c, const sy_item *it)
{
    owf_buf_putc(&c->out, it->op | (it->ctx ? OP_ARRAYCTX : 0));
    if (it->op != OP_NEG && it->op != OP_PLUS && it->op != OP_PCT)
        push_depth(c, -1);
}

/* The context (arrays or not) where the next operand goes. */
static int cur_ctx(const sy_stack *s, int base)
{
    int i;
    for (i = s->n - 1; i >= 0; i--)
        if (s->items[i].kind != SY_OP)
            return s->items[i].ctx;
    return base;
}

/* Pops operators down to the innermost bracket or call. */
static void pop_ops(compiler *c, sy_stack *s, int prec)
{
    while (s->n && s->items[s->n - 1].kind == SY_OP && s->items[s->n - 1].prec >= prec) {
        emit_op(c, &s->items[s->n - 1]);
        s->n--;
    }
}

static int nesting(const sy_stack *s)
{
    int i, n = 0;
    for (i = 0; i < s->n; i++)
        n += s->items[i].kind != SY_OP;
    return n;
}

static void arg_ctx(sy_item *f)
{
    const calc_function *fn = f->fn >= 0 ? calc_function_at(f->fn) : NULL;
    f->ctx = (unsigned char)(f->parent_ctx || (fn && calc_function_arg_class(fn, f->argc) == 'A'));
}

/* Ends a call: its arguments are on the stack. */
static int close_call(compiler *c, sy_item *f, const char *at)
{
    const calc_function *fn = f->fn >= 0 ? calc_function_at(f->fn) : NULL;
    if (f->argc > CALC_MAX_ARGS)
        return fail(c, at);
    if (fn && (f->argc < fn->min || f->argc > fn->max)) {
        c->flags |= PROG_BADARGS;
        fn = NULL;
    }
    if (!fn) {
        c->flags |= PROG_UNKNOWN;
        owf_buf_putc(&c->out, OP_UNKNOWN);
        owf_buf_putc(&c->out, f->argc);
    } else {
        owf_buf_putc(&c->out, OP_FUNC | (f->parent_ctx ? OP_ARRAYCTX : 0));
        put_u16(c, (unsigned)f->fn);
        owf_buf_putc(&c->out, f->argc);
        if (fn->flags & FN_VOLATILE)
            c->flags |= PROG_VOLATILE;
    }
    push_depth(c, 1 - f->argc);
    return 1;
}

static int binary_prec(int op)
{
    switch (op) {
    case OP_ADD: case OP_SUB: return P_ADD;
    case OP_MUL: case OP_DIV: return P_MUL;
    case OP_POW: return P_POW;
    case OP_CAT: return P_CAT;
    case OP_RANGE: return P_RANGE;
    case OP_UNION: return P_UNION;
    case OP_ISECT: return P_ISECT;
    }
    return P_CMP;
}

static int compile_expr(compiler *c, const char *text, int base)
{
    sy_stack s = { NULL, 0, 0 };
    const char *p = text;
    calc_token t;
    sy_item it;
    int expect = 1, after_lparen = 0, ok = 0, again = 0;

    memset(&it, 0, sizeof it);
    if (strlen(text) > CALC_MAX_FORMULA)
        return fail(c, text + CALC_MAX_FORMULA);
    for (;;) {
        int was_lparen = after_lparen;
        if (!again)
            calc_next_token(&p, &t);
        again = 0;
        after_lparen = 0;
        if (c->error)
            break;
        if (t.type == TK_BAD) {
            fail(c, t.start);
            break;
        }
        if (expect) {
            int ctx = cur_ctx(&s, base);
            switch (t.type) {
            case TK_NUM: emit_num(c, t.num); expect = 0; break;
            case TK_STR: emit_string(c, &t); expect = 0; break;
            case TK_BOOL: emit_simple(c, OP_BOOL, t.value); expect = 0; break;
            case TK_ERR: emit_simple(c, OP_ERR, t.value); expect = 0; break;
            case TK_REF: emit_ref(c, &t); expect = 0; break;
            case TK_NAME: emit_name(c, &t, ctx); expect = 0; break;
            case TK_LBRACE: emit_array(c, &p); expect = 0; break;
            case TK_OP:
                if (t.value != OP_SUB && t.value != OP_ADD) {
                    fail(c, t.start);
                    break;
                }
                it.kind = SY_OP;
                it.op = (unsigned char)(t.value == OP_SUB ? OP_NEG : OP_PLUS);
                it.prec = P_NEG;
                it.ctx = (unsigned char)ctx;
                sy_push(c, &s, &it);
                break;
            case TK_LPAREN:
                if (nesting(&s) >= CALC_MAX_NEST) {
                    fail(c, t.start);
                    break;
                }
                it.kind = SY_PAREN;
                it.ctx = (unsigned char)ctx;
                sy_push(c, &s, &it);
                break;
            case TK_FUNC: {
                const char *name = t.start;
                size_t n = (size_t)(t.end - t.start);
                calc_next_token(&p, &t);   /* the '(' */
                if (t.type != TK_LPAREN || nesting(&s) >= CALC_MAX_NEST) {
                    fail(c, t.start);
                    break;
                }
                if (n > 6 && same_nocase(name, "_xlfn.", 6)) {
                    name += 6;
                    n -= 6;
                }
                it.kind = SY_FUNC;
                it.fn = calc_function_find(name, n);
                it.argc = 0;
                it.parent_ctx = (unsigned char)ctx;
                arg_ctx(&it);
                sy_push(c, &s, &it);
                after_lparen = 1;
                break;
            }
            case TK_RPAREN:
            case TK_COMMA:
                /* an argument left out: F(), F(A1,), F(,A1) */
                if (!s.n || s.items[s.n - 1].kind != SY_FUNC) {
                    fail(c, t.start);
                    break;
                }
                if (!(t.type == TK_RPAREN && was_lparen))
                    emit_simple(c, OP_MISSING, 0);
                if (t.type == TK_COMMA) {
                    s.items[s.n - 1].argc++;
                    arg_ctx(&s.items[s.n - 1]);
                } else {
                    sy_item f = s.items[--s.n];
                    if (!was_lparen)
                        f.argc++;
                    close_call(c, &f, t.start);
                    expect = 0;
                }
                break;
            default:
                fail(c, t.start);
                break;
            }
            continue;
        }
        /* an operand is done: an operator, a separator or the end */
        if (t.space_before && (t.type == TK_REF || t.type == TK_NAME || t.type == TK_FUNC || t.type == TK_LPAREN)) {
            /* "A1:C3 B2:D4": the space is the intersection */
            pop_ops(c, &s, P_ISECT);
            it.kind = SY_OP;
            it.op = OP_ISECT;
            it.prec = P_ISECT;
            it.ctx = (unsigned char)cur_ctx(&s, base);
            sy_push(c, &s, &it);
            expect = 1;
            again = 1;
            continue;
        }
        switch (t.type) {
        case TK_OP:
            pop_ops(c, &s, binary_prec(t.value));
            it.kind = SY_OP;
            it.op = (unsigned char)t.value;
            it.prec = (unsigned char)binary_prec(t.value);
            it.ctx = (unsigned char)cur_ctx(&s, base);
            sy_push(c, &s, &it);
            expect = 1;
            break;
        case TK_PCT:
            pop_ops(c, &s, P_NEG + 1);
            owf_buf_putc(&c->out, OP_PCT | (cur_ctx(&s, base) ? OP_ARRAYCTX : 0));
            break;
        case TK_COMMA:
            pop_ops(c, &s, P_NONE + 1);
            if (s.n && s.items[s.n - 1].kind == SY_FUNC) {
                s.items[s.n - 1].argc++;
                arg_ctx(&s.items[s.n - 1]);
            } else {                     /* (A1,B2): the union */
                pop_ops(c, &s, P_UNION);
                it.kind = SY_OP;
                it.op = OP_UNION;
                it.prec = P_UNION;
                it.ctx = (unsigned char)cur_ctx(&s, base);
                sy_push(c, &s, &it);
            }
            expect = 1;
            break;
        case TK_RPAREN:
            pop_ops(c, &s, P_NONE + 1);
            if (!s.n) {
                fail(c, t.start);
                break;
            }
            if (s.items[s.n - 1].kind == SY_PAREN) {
                s.n--;
            } else {
                sy_item f = s.items[--s.n];
                f.argc++;
                close_call(c, &f, t.start);
            }
            break;
        case TK_END:
            pop_ops(c, &s, P_NONE + 1);
            if (s.n)
                fail(c, t.start);
            else
                ok = 1;
            break;
        default:
            fail(c, t.start);
            break;
        }
        if (ok || c->error)
            break;
    }
    free(s.items);
    if (c->out.failed && !c->error)
        c->error = OWF_ERR_MEMORY;
    return ok && !c->error;
}

int calc_compile(const owf_book *book, int sheet, const char *text, int array, calc_prog *prog, int *at)
{
    compiler c;
    unsigned start;
    memset(&c, 0, sizeof c);
    memset(prog, 0, sizeof *prog);
    c.book = book;
    c.sheet = sheet;
    owf_buf_init(&c.out);
    if (at)
        *at = 0;
    c.sr = malloc(sizeof *c.sr);
    if (!c.sr)
        return OWF_ERR_MEMORY;
    start = c.depth;
    if (!compile_expr(&c, text, array ? 1 : 0) || c.depth != start + 1) {
        int error = c.error ? c.error : OWF_ERR_FORMAT;
        if (at && c.at)
            *at = (int)(c.at - text);
        owf_buf_free(&c.out);
        free(c.sr);
        return error;
    }
    free(c.sr);
    prog->code = c.out.data;
    prog->len = (unsigned)c.out.length;
    prog->stack = c.maxdepth;
    prog->flags = c.flags;
    return OWF_OK;
}

void calc_prog_free(calc_prog *prog)
{
    free(prog->code);
    memset(prog, 0, sizeof *prog);
}

/* ---- Walking compiled code ---- */

static unsigned get_u16(const unsigned char *p)
{
    unsigned short v;
    memcpy(&v, p, 2);
    return v;
}

static unsigned long get_u32(const unsigned char *p)
{
    unsigned int v;
    memcpy(&v, p, 4);
    return v;
}

/* The bytes of the instruction at p. */
static unsigned op_size(const unsigned char *p)
{
    switch (OP_CODE(*p)) {
    case OP_NUM: return 1 + sizeof(double);
    case OP_STR: return 3 + get_u16(p + 1);
    case OP_BOOL: case OP_ERR: case OP_UNKNOWN: return 2;
    case OP_REF: return 9;
    case OP_AREA: return 17;
    case OP_ARRAY: return 5;
    case OP_FUNC: return 4;
    }
    return 1;
}

void calc_prog_areas(const calc_prog *prog, calc_area_fn fn, void *user)
{
    const unsigned char *p = prog->code, *end = p + prog->len;
    while (p < end) {
        const unsigned char *next = p + op_size(p);
        int op = OP_CODE(*p);
        if (op == OP_REF || op == OP_AREA) {
            calc_area a;
            int address_only = 0;
            if (next < end && OP_CODE(*next) == OP_FUNC && next[3] == 1) {
                const calc_function *f = calc_function_at((int)get_u16(next + 1));
                address_only = f && (f->flags & FN_ADDRESS);
            }
            if (op == OP_REF) {
                a.s0 = a.s1 = (int)get_u16(p + 1);
                a.r0 = a.r1 = (unsigned)get_u32(p + 3);
                a.c0 = a.c1 = get_u16(p + 7);
            } else {
                a.s0 = (int)get_u16(p + 1);
                a.s1 = (int)get_u16(p + 3);
                a.r0 = (unsigned)get_u32(p + 5);
                a.c0 = get_u16(p + 9);
                a.r1 = (unsigned)get_u32(p + 11);
                a.c1 = get_u16(p + 15);
            }
            if (!address_only)
                fn(user, &a);
        }
        p = next;
    }
}

/* ---- Rewriting references ---- */

static void put_cell(owf_buf *b, unsigned row, unsigned col, int rabs, int cabs, int put_row, int put_col)
{
    char name[16];
    if (put_col) {
        if (cabs)
            owf_buf_putc(b, '$');
        owf_column_name(col, name);
        owf_buf_puts(b, name);
    }
    if (put_row) {
        if (rabs)
            owf_buf_putc(b, '$');
        owf_buf_printf(b, "%lu", (unsigned long)row + 1);
    }
}

char *calc_rewrite(const owf_book *book, int sheet, const char *text, calc_ref_fn fn, void *user)
{
    owf_buf b;
    const char *p = text, *done = text;
    calc_token t;
    calc_sheetref *sr = malloc(sizeof *sr);
    unsigned char *data;
    size_t length;

    if (!sr)
        return NULL;
    owf_buf_init(&b);
    if (*p == '=')
        p++;
    for (;;) {
        calc_next_token(&p, &t);
        if (t.type == TK_END || t.type == TK_BAD)
            break;
        if (t.type == TK_REF && t.kind != RK_ERR) {
            int ref_sheet = sheet;
            unsigned r0 = t.r0, c0 = t.c0, r1 = t.r1, c1 = t.c1;
            if (t.sheet) {
                if (!calc_split_sheet(&t, sr) || sr->has_book || strcmp(sr->first, sr->last))
                    continue;            /* other books and 3-D references stay as they are */
                ref_sheet = book ? calc_find_sheet(book, sr->first) : -1;
            }
            if (fn(user, ref_sheet, t.kind, &r0, &c0, &r1, &c1, t.abs)) {
                if (r0 == t.r0 && c0 == t.c0 && r1 == t.r1 && c1 == t.c1)
                    continue;            /* as it was, as it was written */
                owf_buf_put(&b, done, (size_t)(t.start - done));
                if (t.sheet)
                    owf_buf_put(&b, t.start, (size_t)(t.body - t.start));
                switch (t.kind) {
                case RK_CELL:
                    put_cell(&b, r0, c0, t.abs & ABS_R0, t.abs & ABS_C0, 1, 1);
                    break;
                case RK_AREA:
                    put_cell(&b, r0, c0, t.abs & ABS_R0, t.abs & ABS_C0, 1, 1);
                    owf_buf_putc(&b, ':');
                    put_cell(&b, r1, c1, t.abs & ABS_R1, t.abs & ABS_C1, 1, 1);
                    break;
                case RK_COLS:
                    put_cell(&b, 0, c0, 0, t.abs & ABS_C0, 0, 1);
                    owf_buf_putc(&b, ':');
                    put_cell(&b, 0, c1, 0, t.abs & ABS_C1, 0, 1);
                    break;
                case RK_ROWS:
                    put_cell(&b, r0, 0, t.abs & ABS_R0, 0, 1, 0);
                    owf_buf_putc(&b, ':');
                    put_cell(&b, r1, 0, t.abs & ABS_R1, 0, 1, 0);
                    break;
                }
            } else {
                owf_buf_put(&b, done, (size_t)(t.start - done));
                if (t.sheet)
                    owf_buf_put(&b, t.start, (size_t)(t.body - t.start));
                owf_buf_puts(&b, "#REF!");
            }
            done = t.end;
        }
    }
    if (t.type == TK_BAD) {              /* won't parse: as it was */
        b.length = 0;
        done = text;
    }
    free(sr);
    owf_buf_puts(&b, done);
    if (b.failed || owf_buf_take(&b, &data, &length) != OWF_OK) {
        owf_buf_free(&b);
        return NULL;
    }
    return (char *)data;
}

/* ---- A typed formula as Excel keeps it ---- */

char *calc_normalize(const char *text)
{
    owf_buf b;
    const char *p = text, *done = text;
    calc_token t;
    unsigned char *data;
    size_t length;
    owf_buf_init(&b);
    for (;;) {
        const char *q;
        calc_next_token(&p, &t);
        if (t.type == TK_END || t.type == TK_BAD)
            break;
        if (t.type != TK_FUNC && t.type != TK_REF && t.type != TK_BOOL && t.type != TK_ERR)
            continue;
        owf_buf_put(&b, done, (size_t)(t.start - done));
        switch (t.type) {
        case TK_FUNC: {
            const char *name = t.start;
            size_t n = (size_t)(t.end - t.start);
            int id;
            if (n > 6 && same_nocase(name, "_xlfn.", 6)) {
                owf_buf_put(&b, name, 6);
                name += 6;
                n -= 6;
            }
            id = calc_function_find(name, n);
            if (id >= 0) {
                owf_buf_puts(&b, calc_function_at(id)->name);
                break;
            }
            for (q = name; q < t.end; q++)
                owf_buf_putc(&b, owf_upper((unsigned char)*q));
            break;
        }
        case TK_REF:
            q = t.start;
            if (t.sheet) {
                owf_buf_put(&b, t.start, (size_t)(t.body - t.start));
                q = t.body;
            }
            for (; q < t.end; q++)
                owf_buf_putc(&b, owf_upper((unsigned char)*q));
            break;
        case TK_BOOL:
            owf_buf_puts(&b, t.value ? "TRUE" : "FALSE");
            break;
        default:
            owf_buf_puts(&b, owf_error_name(t.value));
            break;
        }
        done = t.end;
    }
    if (t.type == TK_BAD) {              /* won't read: as typed */
        b.length = 0;
        done = text;
    }
    owf_buf_puts(&b, done);
    if (b.failed || owf_buf_take(&b, &data, &length) != OWF_OK) {
        owf_buf_free(&b);
        return NULL;
    }
    return (char *)data;
}

/* ---- Moving a formula (copy and fill) ---- */

typedef struct {
    long drow, dcol;
} move_by;

static int move_ref(void *user, int ref_sheet, int kind, unsigned *r0, unsigned *c0, unsigned *r1, unsigned *c1,
                    unsigned abs)
{
    const move_by *m = user;
    long v;
    (void)ref_sheet;
    if (kind != RK_COLS) {
        if (!(abs & ABS_R0)) {
            v = (long)*r0 + m->drow;
            if (v < 0 || v >= (long)CALC_MAX_ROWS)
                return 0;
            *r0 = (unsigned)v;
        }
        if (!(abs & ABS_R1)) {
            v = (long)*r1 + m->drow;
            if (v < 0 || v >= (long)CALC_MAX_ROWS)
                return 0;
            *r1 = (unsigned)v;
        }
    }
    if (kind != RK_ROWS) {
        if (!(abs & ABS_C0)) {
            v = (long)*c0 + m->dcol;
            if (v < 0 || v >= (long)CALC_MAX_COLS)
                return 0;
            *c0 = (unsigned)v;
        }
        if (!(abs & ABS_C1)) {
            v = (long)*c1 + m->dcol;
            if (v < 0 || v >= (long)CALC_MAX_COLS)
                return 0;
            *c1 = (unsigned)v;
        }
    }
    return 1;
}

char *owf_calc_move_formula(const char *text, int drow, int dcol)
{
    move_by m;
    calc_prog prog;
    char *out, *result;
    size_t n;
    int r = calc_compile(NULL, 0, *text == '=' ? text + 1 : text, 0, &prog, NULL);
    m.drow = drow;
    m.dcol = dcol;
    if (r == OWF_ERR_MEMORY)
        return NULL;
    if (r == OWF_OK) {
        calc_prog_free(&prog);
        out = calc_rewrite(NULL, 0, text, move_ref, &m);
    } else {                             /* won't parse: as it was */
        n = strlen(text);
        out = malloc(n + 1);
        if (out)
            memcpy(out, text, n + 1);
    }
    if (!out)
        return NULL;
    /* the caller frees it with owf_free: give it owf_malloc's memory */
    n = strlen(out);
    result = owf_malloc(n + 1);
    if (result)
        memcpy(result, out, n + 1);
    free(out);
    return result;
}

/* ---- Cell names ---- */

void owf_calc_cell_name(unsigned row, unsigned col, char *out)
{
    char *p;
    owf_column_name(col, out);
    p = out + strlen(out);
    {
        char digits[12];
        int n = 0;
        unsigned long r = (unsigned long)row + 1;
        do {
            digits[n++] = (char)('0' + r % 10);
            r /= 10;
        } while (r);
        while (n)
            *p++ = digits[--n];
        *p = 0;
    }
}

int owf_calc_parse_cell(const char *text, unsigned *row, unsigned *col)
{
    unsigned abs = 0, r, c;
    const char *e;
    while (*text == ' ')
        text++;
    e = cell_part(text, &r, &c, &abs, ABS_R0, ABS_C0);
    if (!e)
        return 0;
    while (*e == ' ')
        e++;
    if (*e)
        return 0;
    *row = r;
    *col = c;
    return 1;
}
