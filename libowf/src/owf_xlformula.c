/*
 * libowf: formulas as workbook files write them. The model keeps Excel's
 * A1 text without the "=" ("SUM(A1:A3)", "'Q1 sales'!B2*2"). This file
 * reads that text as tokens and rewrites it for the files:
 *   - XLSX: shared formulas, written once and moved to each cell they
 *     cover; Excel 2010's and later functions, which the files mark with
 *     "_xlfn." ([MS-XLSX] 2.2.2);
 *   - ODS: OpenFormula (OASIS ODF 1.2 part 2), "of:=SUM([.A1:.A3])", with
 *     its references in brackets, ";" between arguments, "~" for a union,
 *     "!" for an intersection, TRUE() and FALSE() as functions, "|" between
 *     an array's rows, and some functions under other names.
 * The text is rewritten token by token; nothing is calculated (that is the
 * formula engine's, owf_calc). Function names that the formats spell
 * differently were found by saving each name in both formats with another
 * program (tests/book/make_xml_fixtures.py keeps the evidence).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define MAX_COLS 16384L                  /* XFD */
#define MAX_ROWS 1048576L

/* ---- Function names ---- */

/* Excel 2010 and later: "_xlfn." in XLSX files. */
static const char *const future[] = {
    "ACOT", "ACOTH", "AGGREGATE", "ARABIC", "ARRAYTOTEXT", "BASE", "BETA.DIST", "BETA.INV", "BINOM.DIST",
    "BINOM.DIST.RANGE", "BINOM.INV", "BITAND", "BITLSHIFT", "BITOR", "BITRSHIFT", "BITXOR", "BYCOL", "BYROW",
    "CEILING.MATH", "CEILING.PRECISE", "CHISQ.DIST", "CHISQ.DIST.RT", "CHISQ.INV", "CHISQ.INV.RT", "CHISQ.TEST",
    "CHOOSECOLS", "CHOOSEROWS", "COMBINA", "CONCAT", "CONFIDENCE.NORM", "CONFIDENCE.T", "COT", "COTH",
    "COVARIANCE.P", "COVARIANCE.S", "CSC", "CSCH", "DAYS", "DECIMAL", "DROP", "ENCODEURL", "ERF.PRECISE",
    "ERFC.PRECISE", "EXPAND", "EXPON.DIST", "F.DIST", "F.DIST.RT", "F.INV", "F.INV.RT", "F.TEST", "FIELDVALUE",
    "FILTER", "FILTERXML", "FLOOR.MATH", "FLOOR.PRECISE", "FORECAST.ETS", "FORECAST.ETS.CONFINT",
    "FORECAST.ETS.SEASONALITY", "FORECAST.ETS.STAT", "FORECAST.LINEAR", "FORMULATEXT", "GAMMA", "GAMMA.DIST",
    "GAMMA.INV", "GAMMALN.PRECISE", "GAUSS", "GROUPBY", "HSTACK", "HYPGEOM.DIST", "IFNA", "IFS", "IMAGE",
    "ISFORMULA", "ISOMITTED", "ISOWEEKNUM", "LAMBDA", "LET", "LOGNORM.DIST", "LOGNORM.INV", "MAKEARRAY", "MAP",
    "MAXIFS", "MINIFS", "MODE.MULT", "MODE.SNGL", "MUNIT", "NEGBINOM.DIST", "NORM.DIST", "NORM.INV",
    "NORM.S.DIST", "NORM.S.INV", "NUMBERVALUE", "PDURATION", "PERCENTILE.EXC", "PERCENTILE.INC", "PERCENTOF",
    "PERCENTRANK.EXC", "PERCENTRANK.INC", "PERMUTATIONA", "PHI", "PIVOTBY", "POISSON.DIST", "QUARTILE.EXC",
    "QUARTILE.INC", "RANDARRAY", "RANK.AVG", "RANK.EQ", "REDUCE", "REGEXEXTRACT", "REGEXREPLACE", "REGEXTEST",
    "RRI", "SCAN", "SEC", "SECH", "SEQUENCE", "SHEET", "SHEETS", "SINGLE", "SKEW.P", "SORT", "SORTBY",
    "STDEV.P", "STDEV.S", "STOCKHISTORY", "SWITCH", "T.DIST", "T.DIST.2T", "T.DIST.RT", "T.INV", "T.INV.2T",
    "T.TEST", "TAKE", "TEXTAFTER", "TEXTBEFORE", "TEXTJOIN", "TEXTSPLIT", "TOCOL", "TOROW", "TRIMRANGE",
    "UNICHAR", "UNICODE", "UNIQUE", "VALUETOTEXT", "VAR.P", "VAR.S", "VSTACK", "WEBSERVICE", "WEIBULL.DIST",
    "WRAPCOLS", "WRAPROWS", "XLOOKUP", "XMATCH", "XOR", "Z.TEST", "ANCHORARRAY", NULL
};

/* Of those, the ones OpenFormula has under the same name. */
static const char *const odf_same[] = {
    "ACOT", "ACOTH", "ARABIC", "BASE", "BITAND", "BITLSHIFT", "BITOR", "BITRSHIFT", "BITXOR", "COMBINA",
    "COT", "COTH", "CSC", "CSCH", "DAYS", "DECIMAL", "GAMMA", "GAUSS", "IFNA", "ISFORMULA", "ISOWEEKNUM",
    "MUNIT", "NUMBERVALUE", "PDURATION", "PERMUTATIONA", "PHI", "RRI", "SEC", "SECH", "SHEET", "SHEETS",
    "UNICHAR", "UNICODE", "XOR", NULL
};

/* Excel's own names that OpenFormula keeps apart from its own functions. */
static const char *const odf_microsoft[] = {
    "CEILING", "FLOOR", "ISO.CEILING", "NETWORKDAYS.INTL", "WORKDAY.INTL", NULL
};
static const char *const odf_legacy[] = {
    "CHIDIST", "CHIINV", "CHITEST", "FDIST", "FINV", "NORMSDIST", "NORMSINV", "TDIST", NULL
};
/* Excel's name, then OpenFormula's. */
static const char *const odf_renamed[] = {
    "F.DIST", "FDIST", "F.INV", "FINV", "FORMULATEXT", "FORMULA", "SKEW.P", "SKEWP", NULL
};

static int same_n(const char *a, size_t n, const char *b)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!b[i] || owf_upper((unsigned char)a[i]) != owf_upper((unsigned char)b[i]))
            return 0;
    return b[n] == 0;
}

static int in_list(const char *const *list, const char *name, size_t n)
{
    int i;
    for (i = 0; list[i]; i++)
        if (same_n(name, n, list[i]))
            return 1;
    return 0;
}

static int starts_with(const char *s, size_t n, const char *prefix)
{
    size_t k = strlen(prefix), i;
    if (n < k)
        return 0;
    for (i = 0; i < k; i++)
        if (owf_upper((unsigned char)s[i]) != owf_upper((unsigned char)prefix[i]))
            return 0;
    return 1;
}

static void put_upper(owf_buf *b, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        owf_buf_putc(b, owf_upper((unsigned char)s[i]));
}

/* ---- Excel's tokens ---- */

enum {
    K_END, K_SPACE, K_NUMBER, K_STRING, K_ERROR, K_BOOL, K_REF, K_NAME, K_FUNC, K_OP, K_COMMA, K_SEMI,
    K_LPAREN, K_RPAREN, K_LBRACE, K_RBRACE, K_BAR, K_UNION, K_ISECT, K_OTHER
};

typedef struct {
    long col, row;               /* from 0; -1 for none (whole rows or columns) */
    int col_abs, row_abs;
} cell_ref;

typedef struct {
    int kind;
    const char *p;               /* the token's text */
    size_t n;
    const char *prefix;          /* REF, NAME, ERROR: the sheets before "!", as written, or NULL */
    size_t nprefix;
    const char *body;            /* NAME, ERROR: after the "!" */
    size_t nbody;
    int area;                    /* REF: two corners */
    cell_ref a, b;
} tok;

static int is_letter(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int word_char(int c)
{
    return is_letter(c) || is_digit(c) || c == '_' || c == '.' || c == '\\' || c == '?' || c >= 0x80;
}

static const char *const errors[] = {
    "#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A", "#GETTING_DATA", "#SPILL!",
    "#CALC!", "#FIELD!", "#BLOCKED!", "#CONNECT!", "#BUSY!", "#UNKNOWN!", NULL
};

static size_t error_at(const char *p)
{
    int i;
    for (i = 0; errors[i]; i++) {
        size_t n = strlen(errors[i]), k;
        for (k = 0; k < n && p[k] && owf_upper((unsigned char)p[k]) == errors[i][k]; k++)
            ;
        if (k == n)
            return n;
    }
    return 0;
}

/* Column letters at p (1 to 3, up to XFD): the column, or -1. */
static long column_at(const char **pp)
{
    const char *p = *pp;
    long col = 0;
    int n = 0;
    while (is_letter((unsigned char)p[n]) && n < 4) {
        col = col * 26 + (owf_upper((unsigned char)p[n]) - 'A' + 1);
        n++;
    }
    if (n < 1 || n > 3 || col > MAX_COLS)
        return -1;
    *pp = p + n;
    return col - 1;
}

/* Row digits at p (no leading zero, up to 1048576): the row from 0, or -1. */
static long row_at(const char **pp)
{
    const char *p = *pp;
    long row = 0;
    int n = 0;
    if (*p == '0')
        return -1;
    while (is_digit((unsigned char)p[n]) && n < 8) {
        row = row * 10 + (p[n] - '0');
        n++;
    }
    if (!n || n > 7 || row > MAX_ROWS)
        return -1;
    *pp = p + n;
    return row - 1;
}

/* "$A$1", "A1", and (want_col/want_row) "$A" or "$1"; 0 if not one. The
 * reference must not run on into a name or a function's "(". */
static int cell_at(const char **pp, cell_ref *c, int whole)
{
    const char *p = *pp;
    c->col_abs = c->row_abs = 0;
    c->col = c->row = -1;
    if (whole != 2) {                    /* a column */
        if (*p == '$') {
            c->col_abs = 1;
            p++;
        }
        if ((c->col = column_at(&p)) < 0)
            return 0;
    }
    if (whole != 1) {                    /* a row */
        if (*p == '$') {
            c->row_abs = 1;
            p++;
        }
        if ((c->row = row_at(&p)) < 0)
            return 0;
    }
    if (word_char((unsigned char)*p) || *p == '(' || *p == '$')
        return 0;
    *pp = p;
    return 1;
}

/* A reference after p: "A1", "A1:B2", "A:C", "1:3". */
static int ref_at(const char **pp, tok *t)
{
    const char *p = *pp, *q;
    int whole;
    for (whole = 0; whole <= 2; whole++) {
        p = *pp;
        if (!cell_at(&p, &t->a, whole))
            continue;
        q = p;
        if (*q == ':' && cell_at((q++, &q), &t->b, whole)) {
            t->area = 1;
            *pp = q;
            return 1;
        }
        if (whole)
            continue;                    /* "A" or "1" alone is no reference */
        t->area = 0;
        t->b = t->a;
        *pp = p;
        return 1;
    }
    return 0;
}

/* A sheet prefix up to its "!": 'Q1 sales'!, Sheet1!, Sheet1:Sheet3!,
 * [1]Sheet1!, '[Other.xls]Rates'!; the "!" or NULL. */
static const char *prefix_end(const char *p)
{
    if (*p == '\'') {
        for (p++; *p; p++)
            if (*p == '\'') {
                if (p[1] != '\'')
                    break;
                p++;
            }
        return *p == '\'' && p[1] == '!' ? p + 1 : NULL;
    }
    if (*p == '[') {
        while (*p && *p != ']')
            p++;
        if (!*p)
            return NULL;
        p++;
        if (*p == '!')
            return p;                    /* "[1]!Rate", another book's own name */
    }
    {
        const char *start = p;
        int colon = 0;
        while (word_char((unsigned char)*p) || (*p == ':' && !colon && p > start && (colon = 1)))
            p++;
        return p > start && *p == '!' && p[-1] != ':' ? p : NULL;
    }
}

static void next_xl(const char **pp, tok *t)
{
    const char *p = *pp, *bang;
    memset(t, 0, sizeof *t);
    t->p = p;
    if (!*p) {
        t->kind = K_END;
        return;
    }
    if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        t->kind = K_SPACE;
    } else if (*p == '"') {
        for (p++; *p; p++)
            if (*p == '"') {
                if (p[1] != '"')
                    break;
                p++;
            }
        if (*p)
            p++;
        t->kind = K_STRING;
    } else if (*p == '#' && error_at(p)) {
        p += error_at(p);
        t->kind = K_ERROR;
        t->body = t->p;
        t->nbody = (size_t)(p - t->p);
    } else if ((bang = prefix_end(p)) != NULL) {
        const char *q = bang + 1;
        t->prefix = p;
        t->nprefix = (size_t)(bang - p);
        if (ref_at(&q, t)) {
            t->kind = K_REF;
            p = q;
        } else if (*q == '#' && error_at(q)) {
            t->kind = K_ERROR;
            t->body = q;
            t->nbody = error_at(q);
            p = q + t->nbody;
        } else {
            const char *start = q;
            while (word_char((unsigned char)*q))
                q++;
            if (q == start) {            /* not a reference after all */
                t->prefix = NULL;
                t->nprefix = 0;
                t->kind = K_OTHER;
                p++;
            } else {
                t->kind = K_NAME;
                t->body = start;
                t->nbody = (size_t)(q - start);
                p = q;
            }
        }
    } else if (ref_at(&p, t)) {
        t->kind = K_REF;
    } else if (is_digit((unsigned char)*p) || (*p == '.' && is_digit((unsigned char)p[1]))) {
        while (is_digit((unsigned char)*p))
            p++;
        if (*p == '.')
            for (p++; is_digit((unsigned char)*p); p++)
                ;
        if ((*p == 'E' || *p == 'e') &&
            (is_digit((unsigned char)p[1]) || ((p[1] == '+' || p[1] == '-') && is_digit((unsigned char)p[2])))) {
            p += 2;
            while (is_digit((unsigned char)*p))
                p++;
        }
        t->kind = K_NUMBER;
    } else if (word_char((unsigned char)*p) && !is_digit((unsigned char)*p) && *p != '.') {
        while (word_char((unsigned char)*p))
            p++;
        if (*p == '(') {
            t->kind = K_FUNC;
        } else if (*p == '[') {          /* a table's structured reference */
            int depth = 0;
            for (; *p; p++) {
                if (*p == '[')
                    depth++;
                else if (*p == ']' && --depth == 0)
                    break;
            }
            if (*p)
                p++;
            t->kind = K_NAME;
        } else if (same_n(t->p, (size_t)(p - t->p), "TRUE") || same_n(t->p, (size_t)(p - t->p), "FALSE")) {
            t->kind = K_BOOL;
        } else {
            t->kind = K_NAME;
        }
        t->body = t->p;
        t->nbody = (size_t)(p - t->p);
    } else {
        switch (*p) {
        case '(': t->kind = K_LPAREN; break;
        case ')': t->kind = K_RPAREN; break;
        case '{': t->kind = K_LBRACE; break;
        case '}': t->kind = K_RBRACE; break;
        case ',': t->kind = K_COMMA; break;
        case ';': t->kind = K_SEMI; break;
        case '+': case '-': case '*': case '/': case '^': case '&': case '=': case '%': case ':':
            t->kind = K_OP;
            break;
        case '<':
            t->kind = K_OP;
            if (p[1] == '=' || p[1] == '>')
                p++;
            break;
        case '>':
            t->kind = K_OP;
            if (p[1] == '=')
                p++;
            break;
        default:
            t->kind = K_OTHER;
            break;
        }
        p++;
    }
    t->n = (size_t)(p - t->p);
    *pp = p;
}

/* ---- Writing references ---- */

static void put_column(owf_buf *b, long col)
{
    char name[8];
    owf_column_name((unsigned)col, name);
    owf_buf_puts(b, name);
}

/* Excel's "$A$1", "A", "$3". */
static void put_xl_cell(owf_buf *b, const cell_ref *c)
{
    if (c->col >= 0) {
        if (c->col_abs)
            owf_buf_putc(b, '$');
        put_column(b, c->col);
    }
    if (c->row >= 0) {
        if (c->row_abs)
            owf_buf_putc(b, '$');
        owf_buf_printf(b, "%ld", c->row + 1);
    }
}

static void put_xl_ref(owf_buf *b, const tok *t)
{
    put_xl_cell(b, &t->a);
    if (t->area) {
        owf_buf_putc(b, ':');
        put_xl_cell(b, &t->b);
    }
}

/* ---- Moving a formula (XLSX's shared formulas) ---- */

static int move_cell(cell_ref *c, long drow, long dcol)
{
    if (c->row >= 0 && !c->row_abs) {
        c->row += drow;
        if (c->row < 0 || c->row >= MAX_ROWS)
            return 0;
    }
    if (c->col >= 0 && !c->col_abs) {
        c->col += dcol;
        if (c->col < 0 || c->col >= MAX_COLS)
            return 0;
    }
    return 1;
}

static char *take_text(owf_buf *b)
{
    unsigned char *data;
    size_t length;
    if (b->failed || owf_buf_take(b, &data, &length) != OWF_OK) {
        owf_buf_free(b);
        return NULL;
    }
    return (char *)data;
}

char *owf_xl_formula_move(const char *text, long drow, long dcol)
{
    const char *p = text;
    owf_buf out;
    tok t;
    owf_buf_init(&out);
    for (next_xl(&p, &t); t.kind != K_END; next_xl(&p, &t)) {
        if (t.kind != K_REF) {
            owf_buf_put(&out, t.p, t.n);
            continue;
        }
        if (!move_cell(&t.a, drow, dcol) || (t.area && !move_cell(&t.b, drow, dcol))) {
            owf_buf_puts(&out, "#REF!");
            continue;
        }
        if (t.prefix) {
            owf_buf_put(&out, t.prefix, t.nprefix);
            owf_buf_putc(&out, '!');
        }
        put_xl_ref(&out, &t);
    }
    return take_text(&out);
}

/* ---- Sheet names ---- */

/* The book, first and last sheet of a prefix as written, unquoted. */
typedef struct {
    owf_buf book, first, last;
} sheets;

static void sheets_init(sheets *s)
{
    owf_buf_init(&s->book);
    owf_buf_init(&s->first);
    owf_buf_init(&s->last);
}

static void sheets_free(sheets *s)
{
    owf_buf_free(&s->book);
    owf_buf_free(&s->first);
    owf_buf_free(&s->last);
}

static void split_prefix(const char *p, size_t n, sheets *s)
{
    owf_buf plain;
    const char *q, *end;
    owf_buf_init(&plain);
    if (n >= 2 && p[0] == '\'' && p[n - 1] == '\'') {
        for (q = p + 1; q < p + n - 1; q++) {
            owf_buf_putc(&plain, *q);
            if (*q == '\'' && q + 1 < p + n - 1 && q[1] == '\'')
                q++;
        }
    } else {
        owf_buf_put(&plain, p, n);
    }
    q = plain.data ? (const char *)plain.data : "";
    end = q + plain.length;
    if (q < end && *q == '[') {
        const char *close = memchr(q, ']', (size_t)(end - q));
        if (close) {
            owf_buf_put(&s->book, q + 1, (size_t)(close - q - 1));
            q = close + 1;
        }
    }
    {
        const char *colon = memchr(q, ':', (size_t)(end - q));
        owf_buf_put(&s->first, q, (size_t)((colon ? colon : end) - q));
        if (colon)
            owf_buf_put(&s->last, colon + 1, (size_t)(end - colon - 1));
    }
    owf_buf_free(&plain);
}

static const char *buf_text(const owf_buf *b)
{
    return b->data ? (const char *)b->data : "";
}

/* Excel quotes a sheet's name unless it is one word that could not be
 * read as a cell ("A1", "R1C1") or a number. */
static int xl_needs_quotes(const char *name)
{
    const char *p = name;
    cell_ref c;
    if (!*p || is_digit((unsigned char)*p) || *p == '.')
        return 1;
    for (; *p; p++)
        if (!(word_char((unsigned char)*p)) || *p == '?' || *p == '\\')
            return 1;
    p = name;
    if (cell_at(&p, &c, 0) && !*p)
        return 1;
    p = name;
    if (owf_upper((unsigned char)*p) == 'R') {
        for (p++; is_digit((unsigned char)*p); p++)
            ;
        if (owf_upper((unsigned char)*p) == 'C') {
            for (p++; is_digit((unsigned char)*p); p++)
                ;
            if (!*p)
                return 1;
        }
    }
    return same_n(name, strlen(name), "TRUE") || same_n(name, strlen(name), "FALSE");
}

static void put_quoted_name(owf_buf *b, const char *s, int quote)
{
    for (; *s; s++) {
        if (*s == '\'' && quote)
            owf_buf_putc(b, '\'');
        owf_buf_putc(b, *s);
    }
}

/* "Sheet1!", "'Q1 sales'!", "'[Other.xls]Rates'!", "Sheet1:Sheet3!" */
static void put_xl_prefix(owf_buf *b, const char *book, const char *first, const char *last)
{
    int quote = xl_needs_quotes(first) || (last && xl_needs_quotes(last)) || (book && *book && xl_needs_quotes(book));
    if (quote)
        owf_buf_putc(b, '\'');
    if (book && *book) {
        owf_buf_putc(b, '[');
        put_quoted_name(b, book, quote);
        owf_buf_putc(b, ']');
    }
    put_quoted_name(b, first, quote);
    if (last) {
        owf_buf_putc(b, ':');
        put_quoted_name(b, last, quote);
    }
    if (quote)
        owf_buf_putc(b, '\'');
    owf_buf_putc(b, '!');
}

/* ---- XLSX: "_xlfn." names and other books by number ---- */

static int is_number_text(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!is_digit((unsigned char)*s))
            return 0;
    return 1;
}

/* A prefix again, with the book given by number, "[1]Rates!": quoted when
 * a sheet's name needs it. */
static void put_numbered_prefix(owf_buf *b, int index, const char *first, const char *last)
{
    int quote = *first && (xl_needs_quotes(first) || (last && xl_needs_quotes(last)));
    if (quote)
        owf_buf_putc(b, '\'');
    owf_buf_printf(b, "[%d]", index);
    put_quoted_name(b, first, quote);
    if (last) {
        owf_buf_putc(b, ':');
        put_quoted_name(b, last, quote);
    }
    if (quote)
        owf_buf_putc(b, '\'');
    owf_buf_putc(b, '!');
}

/* "_xlfn.", "_xlws." and "_xlpm." come off, and "[1]" becomes the book's
 * file name: the model has the formula as Excel shows it. */
char *owf_xl_formula_from_file(const char *text, char *const *books, int nbooks)
{
    const char *p = text;
    owf_buf out;
    tok t;
    owf_buf_init(&out);
    for (next_xl(&p, &t); t.kind != K_END; next_xl(&p, &t)) {
        const char *s = t.p;
        size_t n = t.n;
        if (t.prefix) {
            sheets sh;
            long index;
            sheets_init(&sh);
            split_prefix(t.prefix, t.nprefix, &sh);
            index = sh.book.length && is_number_text(buf_text(&sh.book)) && sh.book.length < 6
                        ? strtol(buf_text(&sh.book), NULL, 10) : -1;
            if (index == 0 || (index > 0 && index <= nbooks && books[index - 1])) {
                const char *last = sh.last.length ? buf_text(&sh.last) : NULL;
                if (index == 0)
                    put_xl_prefix(&out, NULL, buf_text(&sh.first), last);
                else if (!sh.first.length)           /* the book's own name: "Other.xls!Rate" */
                    put_xl_prefix(&out, NULL, books[index - 1], NULL);
                else
                    put_xl_prefix(&out, books[index - 1], buf_text(&sh.first), last);
                s = t.prefix + t.nprefix + 1;
                n = (size_t)(t.p + t.n - s);
            }
            sheets_free(&sh);
        }
        if (t.kind == K_FUNC || (t.kind == K_NAME && !t.prefix)) {
            while (starts_with(s, n, "_xlfn.") || starts_with(s, n, "_xlws.") || starts_with(s, n, "_xlpm.")) {
                s += 6;
                n -= 6;
            }
        }
        owf_buf_put(&out, s, n);
    }
    return take_text(&out);
}

void owf_xl_links_free(owf_xl_links *links)
{
    int i, k;
    for (i = 0; i < links->count; i++) {
        owf_xl_link *l = &links->items[i];
        free(l->file);
        for (k = 0; k < l->nsheets; k++)
            free(l->sheets[k]);
        free(l->sheets);
        for (k = 0; k < l->nnames; k++)
            free(l->names[k]);
        free(l->names);
    }
    free(links->items);
    memset(links, 0, sizeof *links);
}

/* s added to a list of names if new (ignoring case); 0 when memory ran out. */
static int add_word(char ***list, int *count, int *capacity, const char *s)
{
    int i;
    size_t n = strlen(s);
    for (i = 0; i < *count; i++)
        if (owf_same_word((*list)[i], s))
            return 1;
    if (*count == *capacity) {
        int want = *capacity ? *capacity * 2 : 4;
        char **p = realloc(*list, (size_t)want * sizeof *p);
        if (!p)
            return 0;
        *list = p;
        *capacity = want;
    }
    if (!((*list)[*count] = malloc(n + 1)))
        return 0;
    memcpy((*list)[*count], s, n + 1);
    (*count)++;
    return 1;
}

/* The book's number (from 1), added if new; 0 when memory ran out. */
static int link_index(owf_xl_links *links, const char *file)
{
    int i;
    owf_xl_link *l;
    size_t n = strlen(file);
    for (i = 0; i < links->count; i++)
        if (owf_same_word(links->items[i].file, file))
            return i + 1;
    if (links->count == links->capacity) {
        int want = links->capacity ? links->capacity * 2 : 4;
        owf_xl_link *p = realloc(links->items, (size_t)want * sizeof *p);
        if (!p)
            return 0;
        links->items = p;
        links->capacity = want;
    }
    l = &links->items[links->count];
    memset(l, 0, sizeof *l);
    if (!(l->file = malloc(n + 1)))
        return 0;
    memcpy(l->file, file, n + 1);
    return ++links->count;
}

static int is_sheet(const owf_book *book, const char *name)
{
    int i;
    for (i = 0; book && i < book->nsheets; i++)
        if (owf_same_word(book->sheets[i].name, name))
            return 1;
    return 0;
}

char *owf_xl_formula_for_file(const char *text, const owf_book *book, owf_xl_links *links)
{
    const char *p = text;
    owf_buf out;
    tok t;
    owf_buf_init(&out);
    for (next_xl(&p, &t); t.kind != K_END; next_xl(&p, &t)) {
        if (t.prefix && links) {
            sheets sh;
            const char *rest = t.prefix + t.nprefix + 1;
            sheets_init(&sh);
            split_prefix(t.prefix, t.nprefix, &sh);
            if (sh.book.length || (t.kind == K_NAME && !is_sheet(book, buf_text(&sh.first)))) {
                /* another book: "[Other.xls]Rates!A1" or "Other.xls!Rate" */
                const char *file = sh.book.length ? buf_text(&sh.book) : buf_text(&sh.first);
                const char *first = sh.book.length ? buf_text(&sh.first) : "";
                const char *last = sh.last.length ? buf_text(&sh.last) : NULL;
                int index = link_index(links, file);
                owf_xl_link *l = index ? &links->items[index - 1] : NULL;
                if (!l || (*first && !add_word(&l->sheets, &l->nsheets, &l->capsheets, first)) ||
                    (last && !add_word(&l->sheets, &l->nsheets, &l->capsheets, last))) {
                    links->failed = 1;
                } else if (t.kind == K_NAME && !*first) {
                    char *name = malloc(t.nbody + 1);
                    if (!name) {
                        links->failed = 1;
                    } else {
                        memcpy(name, t.body, t.nbody);
                        name[t.nbody] = 0;
                        if (!add_word(&l->names, &l->nnames, &l->capnames, name))
                            links->failed = 1;
                        free(name);
                    }
                }
                put_numbered_prefix(&out, index, first, last);
                owf_buf_put(&out, rest, (size_t)(t.p + t.n - rest));
                sheets_free(&sh);
                continue;
            }
            sheets_free(&sh);
        }
        if (t.kind == K_FUNC && in_list(future, t.p, t.n)) {
            owf_buf_puts(&out, "_xlfn.");
            if (same_n(t.p, t.n, "FILTER") || same_n(t.p, t.n, "SORT"))
                owf_buf_puts(&out, "_xlws.");
        }
        owf_buf_put(&out, t.p, t.n);
    }
    return take_text(&out);
}

/* OpenFormula quotes a sheet's name unless it is letters, digits and "_". */
static void put_of_sheet(owf_buf *b, const char *name)
{
    const char *p;
    int quote = !*name || is_digit((unsigned char)*name);
    for (p = name; *p && !quote; p++)
        quote = !(is_letter((unsigned char)*p) || is_digit((unsigned char)*p) || *p == '_');
    owf_buf_putc(b, '$');
    if (quote)
        owf_buf_putc(b, '\'');
    put_quoted_name(b, name, quote);
    if (quote)
        owf_buf_putc(b, '\'');
}

/* ---- Excel to OpenFormula ---- */

static void put_of_name(owf_buf *b, const char *name, size_t n)
{
    int i;
    while (starts_with(name, n, "_xlfn.") || starts_with(name, n, "_xlws.")) {
        name += 6;
        n -= 6;
    }
    for (i = 0; odf_renamed[i]; i += 2)
        if (same_n(name, n, odf_renamed[i])) {
            owf_buf_puts(b, odf_renamed[i + 1]);
            return;
        }
    if (in_list(odf_legacy, name, n)) {
        owf_buf_puts(b, "LEGACY.");
        put_upper(b, name, n);
    } else if (in_list(odf_microsoft, name, n) || (in_list(future, name, n) && !in_list(odf_same, name, n))) {
        owf_buf_puts(b, "COM.MICROSOFT.");
        put_upper(b, name, n);
    } else {
        owf_buf_put(b, name, n);
    }
}

static void put_of_ref(owf_buf *b, const tok *t)
{
    sheets s;
    sheets_init(&s);
    if (t->prefix)
        split_prefix(t->prefix, t->nprefix, &s);
    owf_buf_putc(b, '[');
    if (s.book.length) {
        owf_buf_putc(b, '\'');
        put_quoted_name(b, buf_text(&s.book), 1);
        owf_buf_puts(b, "'#");
    }
    if (t->prefix)
        put_of_sheet(b, buf_text(&s.first));
    owf_buf_putc(b, '.');
    put_xl_cell(b, &t->a);
    if (t->area || s.last.length) {
        owf_buf_putc(b, ':');
        if (s.last.length)
            put_of_sheet(b, buf_text(&s.last));
        owf_buf_putc(b, '.');
        put_xl_cell(b, &t->b);
    }
    owf_buf_putc(b, ']');
    sheets_free(&s);
}

static int operand_end(int kind)
{
    return kind == K_NUMBER || kind == K_STRING || kind == K_ERROR || kind == K_BOOL || kind == K_REF ||
           kind == K_NAME || kind == K_RPAREN || kind == K_RBRACE;
}

static int operand_start(int kind)
{
    return kind == K_NUMBER || kind == K_STRING || kind == K_ERROR || kind == K_BOOL || kind == K_REF ||
           kind == K_NAME || kind == K_FUNC || kind == K_LPAREN || kind == K_LBRACE;
}

#define MAX_NEST 64

enum { IN_FUNC = 1, IN_GROUP, IN_ARRAY };

char *owf_excel_to_of(const char *text)
{
    const char *p = text;
    owf_buf out;
    tok t, ahead;
    int stack[MAX_NEST], depth = 0, prev = K_END, func_next = 0;

    owf_buf_init(&out);
    owf_buf_puts(&out, "of:=");
    for (next_xl(&p, &t); t.kind != K_END; next_xl(&p, &t)) {
        int top = depth ? stack[depth - 1] : 0;
        switch (t.kind) {
        case K_SPACE: {
            const char *q = p;
            next_xl(&q, &ahead);
            if (operand_end(prev) && operand_start(ahead.kind) && top != IN_ARRAY)
                owf_buf_putc(&out, '!');      /* Excel's space between references intersects */
            else
                owf_buf_put(&out, t.p, t.n);
            continue;                         /* prev stays the token before */
        }
        case K_FUNC:
            put_of_name(&out, t.p, t.n);
            func_next = 1;
            break;
        case K_LPAREN:
        case K_LBRACE:
            if (depth == MAX_NEST) {
                owf_buf_free(&out);
                return NULL;
            }
            stack[depth++] = t.kind == K_LBRACE ? IN_ARRAY : func_next ? IN_FUNC : IN_GROUP;
            owf_buf_putc(&out, t.kind == K_LBRACE ? '{' : '(');
            break;
        case K_RPAREN:
        case K_RBRACE:
            if (depth)
                depth--;
            owf_buf_putc(&out, t.kind == K_RBRACE ? '}' : ')');
            break;
        case K_COMMA:
            owf_buf_putc(&out, top == IN_FUNC || top == IN_ARRAY ? ';' : '~');
            break;
        case K_SEMI:
            owf_buf_putc(&out, top == IN_ARRAY ? '|' : ';');
            break;
        case K_BOOL:
            owf_buf_put(&out, t.p, t.n);
            if (top != IN_ARRAY)
                owf_buf_puts(&out, "()");
            break;
        case K_REF:
            put_of_ref(&out, &t);
            break;
        case K_ERROR:
            owf_buf_put(&out, t.body, t.nbody);
            break;
        case K_NAME:
            /* OpenFormula has no names of a sheet's or another book's own */
            if (t.prefix)
                owf_buf_put(&out, t.body, t.nbody);
            else
                owf_buf_put(&out, t.p, t.n);
            break;
        default:
            owf_buf_put(&out, t.p, t.n);
            break;
        }
        if (t.kind != K_FUNC)
            func_next = 0;
        prev = t.kind;
    }
    return take_text(&out);
}

/* ---- OpenFormula's tokens ---- */

static void next_of(const char **pp, tok *t)
{
    const char *p = *pp;
    memset(t, 0, sizeof *t);
    t->p = p;
    if (!*p) {
        t->kind = K_END;
        return;
    }
    if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        t->kind = K_SPACE;
    } else if (*p == '"') {
        for (p++; *p; p++)
            if (*p == '"') {
                if (p[1] != '"')
                    break;
                p++;
            }
        if (*p)
            p++;
        t->kind = K_STRING;
    } else if (*p == '[') {
        int quoted = 0;
        for (p++; *p && (quoted || *p != ']'); p++)
            if (*p == '\'')
                quoted = !quoted;    /* '' inside quotes flips twice */
        if (*p)
            p++;
        t->kind = K_REF;
    } else if (*p == '#' && error_at(p)) {
        p += error_at(p);
        t->kind = K_ERROR;
    } else if (is_digit((unsigned char)*p) || (*p == '.' && is_digit((unsigned char)p[1]))) {
        while (is_digit((unsigned char)*p))
            p++;
        if (*p == '.')
            for (p++; is_digit((unsigned char)*p); p++)
                ;
        if ((*p == 'E' || *p == 'e') &&
            (is_digit((unsigned char)p[1]) || ((p[1] == '+' || p[1] == '-') && is_digit((unsigned char)p[2])))) {
            p += 2;
            while (is_digit((unsigned char)*p))
                p++;
        }
        t->kind = K_NUMBER;
    } else if (word_char((unsigned char)*p) && *p != '.' && *p != '?') {
        while (word_char((unsigned char)*p) && *p != '?')
            p++;
        t->kind = *p == '(' ? K_FUNC : K_NAME;
        if (*p == '[') {                 /* Excel's structured reference, kept as it was */
            int depth = 0;
            for (; *p; p++) {
                if (*p == '[')
                    depth++;
                else if (*p == ']' && --depth == 0)
                    break;
            }
            if (*p)
                p++;
        }
    } else {
        switch (*p) {
        case '(': t->kind = K_LPAREN; break;
        case ')': t->kind = K_RPAREN; break;
        case '{': t->kind = K_LBRACE; break;
        case '}': t->kind = K_RBRACE; break;
        case ';': t->kind = K_SEMI; break;
        case '|': t->kind = K_BAR; break;
        case '~': t->kind = K_UNION; break;
        case '!': t->kind = K_ISECT; break;
        case '+': case '-': case '*': case '/': case '^': case '&': case '=': case '%': case ':':
            t->kind = K_OP;
            break;
        case '<':
            t->kind = K_OP;
            if (p[1] == '=' || p[1] == '>')
                p++;
            break;
        case '>':
            t->kind = K_OP;
            if (p[1] == '=')
                p++;
            break;
        default:
            t->kind = K_OTHER;
            break;
        }
        p++;
    }
    t->n = (size_t)(p - t->p);
    *pp = p;
}

/* One part of an OpenFormula reference: [$]sheet. then a cell, column or
 * row, or #REF!. sheet is unquoted into name (empty for none). 1 when it
 * is one, 2 when it is #REF!. */
static int of_part(const char **pp, const char *end, owf_buf *name, cell_ref *c)
{
    const char *p = *pp;
    int whole;
    if (p < end && *p == '$')
        p++;
    if (p < end && *p == '\'') {
        for (p++; p < end; p++) {
            if (*p == '\'') {
                if (p + 1 < end && p[1] == '\'') {
                    p++;
                } else {
                    break;
                }
            }
            owf_buf_putc(name, *p);
        }
        if (p >= end)
            return 0;
        p++;
    } else {
        const char *start = p;
        while (p < end && *p != '.' && *p != ':' && *p != ']')
            p++;
        if (p < end && *p == '.')
            owf_buf_put(name, start, (size_t)(p - start));
        else
            p = start;                   /* no sheet: "A1" alone */
    }
    if (p < end && *p == '.')
        p++;
    if (end - p >= 5 && !memcmp(p, "#REF!", 5)) {
        *pp = p + 5;
        return 2;
    }
    for (whole = 0; whole <= 2; whole++) {
        const char *q = p;
        cell_ref r;
        long col = -1, row = -1;
        r.col_abs = r.row_abs = 0;
        if (whole != 2) {
            if (q < end && *q == '$') {
                r.col_abs = 1;
                q++;
            }
            if ((col = column_at(&q)) < 0)
                continue;
        }
        if (whole != 1) {
            if (q < end && *q == '$') {
                r.row_abs = 1;
                q++;
            }
            if ((row = row_at(&q)) < 0)
                continue;
        }
        if (q > end || (q < end && *q != ':' && *q != ']'))
            continue;
        r.col = col;
        r.row = row;
        *c = r;
        *pp = q;
        return 1;
    }
    return 0;
}

/* "[.A1]", "[$Sheet1.A1:.B2]", "['file:///x.ods'#$S.A1]" as Excel's text;
 * 0 when it is not a reference we can read. */
static int put_xl_from_of(owf_buf *b, const char *p, size_t n)
{
    const char *end = p + n - 1;         /* before the "]" */
    owf_buf book, first, last;
    cell_ref a, c2;
    int ok = 0, r1, r2 = 1, area = 0;

    if (n < 3 || p[0] != '[' || p[n - 1] != ']')
        return 0;
    p++;
    owf_buf_init(&book);
    owf_buf_init(&first);
    owf_buf_init(&last);
    if (*p == '\'') {                    /* a source: 'file:///...'# */
        const char *q;
        for (q = p + 1; q < end; q++)
            if (*q == '\'') {
                if (q + 1 < end && q[1] == '\'') {
                    q++;
                    continue;
                }
                break;
            }
        if (q + 1 < end && q[1] == '#') {
            const char *s, *start = p + 1;
            for (s = p + 1; s < q; s++)
                if (*s == '/' || *s == '\\' || *s == ':')
                    start = s + 1;
            for (s = start; s < q; s++) {
                owf_buf_putc(&book, *s);
                if (*s == '\'' && s + 1 < q && s[1] == '\'')
                    s++;
            }
            p = q + 2;
        }
    }
    r1 = of_part(&p, end, &first, &a);
    if (r1 && p < end && *p == ':') {
        p++;
        r2 = of_part(&p, end, &last, &c2);
        area = 1;
    }
    if (!r1 || !r2 || p != end)
        goto done;
    ok = 1;
    if (r1 == 2 || r2 == 2) {
        owf_buf_puts(b, "#REF!");
        goto done;
    }
    if (area && ((a.col < 0) != (c2.col < 0) || (a.row < 0) != (c2.row < 0)))
        goto bad;
    if (!area && (a.col < 0 || a.row < 0))
        goto bad;
    if (first.length || book.length) {
        const char *l = last.length && strcmp(buf_text(&last), buf_text(&first)) ? buf_text(&last) : NULL;
        put_xl_prefix(b, buf_text(&book), buf_text(&first), l);
        if (l && a.col == c2.col && a.row == c2.row && a.col_abs == c2.col_abs && a.row_abs == c2.row_abs)
            area = 0;                    /* "Sheet1:Sheet3!A1" */
    }
    put_xl_cell(b, &a);
    if (area) {
        owf_buf_putc(b, ':');
        put_xl_cell(b, &c2);
    }
    goto done;
bad:
    ok = 0;
done:
    owf_buf_free(&book);
    owf_buf_free(&first);
    owf_buf_free(&last);
    return ok;
}

static void put_xl_name(owf_buf *b, const char *name, size_t n)
{
    int i, prefixed = 0;
    for (;;) {
        if (starts_with(name, n, "COM.MICROSOFT.")) {
            name += 14;
            n -= 14;
        } else if (starts_with(name, n, "LEGACY.")) {
            name += 7;
            n -= 7;
        } else if (starts_with(name, n, "_xlfn.") || starts_with(name, n, "_xlws.")) {
            name += 6;
            n -= 6;
        } else {
            break;
        }
        prefixed = 1;
    }
    if (!prefixed)
        for (i = 0; odf_renamed[i]; i += 2)
            if (same_n(name, n, odf_renamed[i + 1])) {
                owf_buf_puts(b, odf_renamed[i]);
                return;
            }
    put_upper(b, name, n);
}

typedef struct {
    int kind;
    size_t start;                /* in the output: where this argument began */
    int unions;                  /* a "~" at this level */
} level;

/* A function's argument holding a union goes in brackets in Excel, whose
 * comma would end the argument. */
static void end_argument(owf_buf *out, level *l)
{
    if (l->kind != IN_FUNC || !l->unions)
        return;
    l->unions = 0;
    owf_buf_putc(out, ')');
    owf_buf_putc(out, ')');              /* room for the "(" */
    if (out->failed)
        return;
    memmove(out->data + l->start + 1, out->data + l->start, out->length - 1 - l->start);
    out->data[l->start] = '(';
}

char *owf_of_to_excel(const char *text)
{
    const char *p = text;
    owf_buf out;
    tok t, ahead;
    level stack[MAX_NEST];
    int depth = 0, prev = K_END, func_next = 0, bad = 0;

    /* the namespace's prefix: OpenFormula, old OpenOffice's (the same
     * references) or Excel's own text */
    if (!strncmp(p, "msoxl:", 6)) {
        p += 6;
        if (*p == '=')
            p++;
        owf_buf_init(&out);
        owf_buf_puts(&out, p);
        return take_text(&out);
    }
    if (!strncmp(p, "of:", 3))
        p += 3;
    else if (!strncmp(p, "oooc:", 5))
        p += 5;
    if (*p == '=')
        p++;
    owf_buf_init(&out);
    for (next_of(&p, &t); t.kind != K_END && !bad; next_of(&p, &t)) {
        level *top = depth ? &stack[depth - 1] : NULL;
        int kind = top ? top->kind : 0;
        switch (t.kind) {
        case K_SPACE: {
            const char *q = p;
            next_of(&q, &ahead);
            if (!(operand_end(prev) && operand_start(ahead.kind)))
                owf_buf_put(&out, t.p, t.n);
            continue;
        }
        case K_FUNC:
            if (same_n(t.p, t.n, "TRUE") || same_n(t.p, t.n, "FALSE")) {
                const char *q = p;
                next_of(&q, &ahead);
                if (ahead.kind == K_LPAREN) {
                    next_of(&q, &ahead);
                    while (ahead.kind == K_SPACE)
                        next_of(&q, &ahead);
                    if (ahead.kind == K_RPAREN) {
                        put_upper(&out, t.p, t.n);
                        p = q;
                        prev = K_BOOL;
                        func_next = 0;
                        continue;
                    }
                }
            }
            put_xl_name(&out, t.p, t.n);
            func_next = 1;
            break;
        case K_LPAREN:
        case K_LBRACE:
            if (depth == MAX_NEST) {
                bad = 1;
                break;
            }
            owf_buf_putc(&out, t.kind == K_LBRACE ? '{' : '(');
            stack[depth].kind = t.kind == K_LBRACE ? IN_ARRAY : func_next ? IN_FUNC : IN_GROUP;
            stack[depth].start = out.length;
            stack[depth].unions = 0;
            depth++;
            break;
        case K_RPAREN:
        case K_RBRACE:
            if (top) {
                end_argument(&out, top);
                depth--;
            }
            owf_buf_putc(&out, t.kind == K_RBRACE ? '}' : ')');
            break;
        case K_SEMI:
            if (top)
                end_argument(&out, top);
            owf_buf_putc(&out, ',');
            if (top)
                top->start = out.length;
            break;
        case K_BAR:
            owf_buf_putc(&out, ';');
            break;
        case K_UNION:
            if (top)
                top->unions = 1;
            owf_buf_putc(&out, ',');
            break;
        case K_ISECT:
            owf_buf_putc(&out, ' ');
            break;
        case K_REF:
            if (!put_xl_from_of(&out, t.p, t.n))
                bad = 1;
            break;
        case K_NAME:
            if (kind == IN_ARRAY && (same_n(t.p, t.n, "TRUE") || same_n(t.p, t.n, "FALSE")))
                put_upper(&out, t.p, t.n);
            else
                owf_buf_put(&out, t.p, t.n);
            break;
        default:
            owf_buf_put(&out, t.p, t.n);
            break;
        }
        if (t.kind != K_FUNC)
            func_next = 0;
        prev = t.kind == K_ISECT || t.kind == K_UNION ? K_OP : t.kind;
    }
    if (bad) {
        owf_buf_free(&out);
        return NULL;
    }
    return take_text(&out);
}
