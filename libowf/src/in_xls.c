/*
 * libowf: Excel 97-2003 workbooks (.xls), read from Microsoft's published
 * description of the format ([MS-XLS], BIFF8) inside an OLE compound file
 * ([MS-CFB]). Cells and their values, shared strings, the cells' styles
 * (fonts, number formats, alignment, borders, fills), column widths, row
 * heights, merged cells, frozen panes and defined names. Formulas come back
 * as the text Excel shows ("SUM(A1:A3)"), with their last results as the
 * cells' values. Excel 5 and 95 workbooks and password-protected ones are
 * recognised but not read yet.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

/* Record types ([MS-XLS] 2.3) */
#define R_FORMULA 0x0006
#define R_EOF 0x000A
#define R_EXTERNSHEET 0x0017
#define R_NAME 0x0018
#define R_NOTE 0x001C
#define R_DATEMODE 0x0022
#define R_EXTERNNAME 0x0023
#define R_FILEPASS 0x002F
#define R_FONT 0x0031
#define R_CONTINUE 0x003C
#define R_WINDOW1 0x003D
#define R_PANE 0x0041
#define R_DEFCOLWIDTH 0x0055
#define R_OBJ 0x005D
#define R_COLINFO 0x007D
#define R_BOUNDSHEET 0x0085
#define R_PALETTE 0x0092
#define R_STANDARDWIDTH 0x0099
#define R_MULRK 0x00BD
#define R_MULBLANK 0x00BE
#define R_RSTRING 0x00D6
#define R_XF 0x00E0
#define R_MERGEDCELLS 0x00E5
#define R_SST 0x00FC
#define R_LABELSST 0x00FD
#define R_SUPBOOK 0x01AE
#define R_CONDFMT 0x01B0
#define R_HLINK 0x01B8
#define R_DV 0x01BE
#define R_BLANK 0x0201
#define R_NUMBER 0x0203
#define R_LABEL 0x0204
#define R_BOOLERR 0x0205
#define R_STRING 0x0207
#define R_ROW 0x0208
#define R_ARRAY 0x0221
#define R_DEFAULTROWHEIGHT 0x0225
#define R_TABLE 0x0236
#define R_WINDOW2 0x023E
#define R_RK 0x027E
#define R_FORMAT 0x041E
#define R_SHRFMLA 0x04BC
#define R_BOF 0x0809

static unsigned le16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

/* An IEEE double stored little-endian, on a computer of either order. */
static double le_double(const unsigned char *p)
{
    union {
        double d;
        unsigned char b[8];
    } u;
    int i, little;
    u.d = 1.0;
    little = u.b[7] == 0x3F;
    for (i = 0; i < 8; i++)
        u.b[i] = p[little ? i : 7 - i];
    return u.d;
}

/* An RK number ([MS-XLS] 2.5.217): a 30-bit integer or the top of a double,
 * perhaps a hundredth of it. */
static double rk_number(unsigned long rk)
{
    double v;
    if (rk & 2) {
        long n = (long)((rk >> 2) & 0x3FFFFFFFUL);
        if (n & 0x20000000L)
            n -= 0x40000000L;
        v = (double)n;
    } else {
        unsigned char b[8];
        memset(b, 0, 4);
        b[4] = (unsigned char)(rk & 0xFC);
        b[5] = (unsigned char)(rk >> 8);
        b[6] = (unsigned char)(rk >> 16);
        b[7] = (unsigned char)(rk >> 24);
        v = le_double(b);
    }
    return rk & 1 ? v / 100 : v;
}

/* Makes room for one more item in an array of size bytes each. */
static int grow(void **items, int count, int *capacity, size_t size)
{
    void *p;
    int want;
    if (count < *capacity)
        return 1;
    want = *capacity ? *capacity * 2 : 8;
    p = realloc(*items, (size_t)want * size);
    if (!p)
        return 0;
    *items = p;
    *capacity = want;
    return 1;
}

/* ---- The reader's state ---- */

typedef struct {
    unsigned height, grbit, icv, weight, script, underline;
    int font;                    /* the book's */
} xls_font;

typedef struct {
    unsigned ifmt;
    int index;                   /* the book's */
} xls_numfmt;

typedef struct {
    unsigned long offset;        /* the sheet's BOF in the stream */
    int type;                    /* 0 worksheet, 1 macro sheet, 2 chart, 6 VBA module */
    char *name;
    int sheet;                   /* the book's, or -1 */
} xls_boundsheet;

enum { BOOK_SELF, BOOK_ADDIN, BOOK_OTHER };

typedef struct {
    int kind;
    char *file;                  /* BOOK_OTHER: the file's name */
    int nsheets;
    char **sheets;
    int nnames, capnames;
    char **names;                /* its EXTERNNAMEs */
} xls_supbook;

typedef struct {
    unsigned supbook, first, last;
} xls_xti;

typedef struct {
    char *name;
    int sheet;                   /* the book's, or -1 */
    int hidden;
    const unsigned char *rgce, *extra;
    unsigned cce;
    size_t nextra;
} xls_name;

/* A shared or array formula: its range and its tokens. */
typedef struct {
    unsigned r0, r1, c0, c1;
    int array;
    const unsigned char *rgce, *extra;
    unsigned cce;
    size_t nextra;
} xls_shared;

/* A cell whose formula is another's (PtgExp), found at the sheet's end. */
typedef struct {
    unsigned row, col, base_row, base_col;
} xls_pending;

typedef struct {
    const unsigned char *data;   /* the Workbook stream */
    size_t length;
    owf_book *book;
    owf_report *report;
    int failed;                  /* memory ran out */
    int cut;                     /* a record ran past the end */

    owf_buf sst;                 /* the shared strings, each with a NUL after */
    size_t *sst_at;
    unsigned long nsst;

    xls_font *fonts;
    int nfonts, capfonts;
    xls_numfmt *numfmts;
    int nnumfmts, capnumfmts;
    const unsigned char **xf;    /* the XF records' bytes */
    int nxf, capxf;
    int *xf_style;               /* the book's style for each */
    unsigned long palette[56];
    xls_boundsheet *bs;
    int nbs, capbs;
    xls_supbook *supbooks;
    int nsupbooks, capsupbooks;
    xls_xti *xti;
    unsigned nxti;
    xls_name *names;
    int nnames, capnames;
    unsigned active_tab;

    /* the sheet being read */
    owf_sheet *sheet;
    xls_shared *shared;
    int nshared, capshared, last_shared;
    xls_pending *pending;
    int npending, cappending;
    int want_string;
    unsigned string_row, string_col;
} xls;

typedef struct {
    unsigned type;
    const unsigned char *p;
    size_t len;
} xls_rec;

static int next_record(xls *x, size_t *pos, xls_rec *r)
{
    size_t len;
    if (*pos + 4 > x->length)
        return 0;
    r->type = le16(x->data + *pos);
    len = le16(x->data + *pos + 2);
    r->p = x->data + *pos + 4;
    if (len > x->length - *pos - 4) {
        len = x->length - *pos - 4;
        x->cut = 1;
    }
    r->len = len;
    *pos += 4 + len;
    return 1;
}

/* ---- Reading across CONTINUE records ----
 * A record longer than 8224 bytes goes on in CONTINUE records after it.
 * The cursor reads on into them; a string's characters that go on start
 * with a new flags byte (one byte or two to a character). */

typedef struct {
    const unsigned char *data;
    size_t length, pos, end;     /* the current part is pos..end */
    int bad;                     /* ran out */
} xls_cur;

static void cur_start(xls_cur *c, const xls *x, const xls_rec *r, size_t off)
{
    c->data = x->data;
    c->length = x->length;
    c->pos = (size_t)(r->p - x->data);
    c->end = c->pos + r->len;
    c->bad = 0;
    if (off > r->len) {
        c->pos = c->end;
        c->bad = 1;
    } else {
        c->pos += off;
    }
}

/* A cursor over bytes on their own, which never go on. */
static void cur_flat(xls_cur *c, const unsigned char *p, size_t length)
{
    c->data = p;
    c->length = c->end = length;
    c->pos = 0;
    c->bad = 0;
}

static int cur_continue(xls_cur *c)
{
    size_t len;
    if (c->bad || c->end + 4 > c->length || le16(c->data + c->end) != R_CONTINUE) {
        c->bad = 1;
        return 0;
    }
    len = le16(c->data + c->end + 2);
    c->pos = c->end + 4;
    c->end = len > c->length - c->pos ? c->length : c->pos + len;
    return 1;
}

static unsigned cur_u8(xls_cur *c)
{
    while (c->pos >= c->end)
        if (!cur_continue(c))
            return 0;
    return c->data[c->pos++];
}

static unsigned cur_u16(xls_cur *c)
{
    unsigned a = cur_u8(c);
    return a | (cur_u8(c) << 8);
}

static unsigned long cur_u32(xls_cur *c)
{
    unsigned long a = cur_u16(c);
    return a | ((unsigned long)cur_u16(c) << 16);
}

static void cur_skip(xls_cur *c, unsigned long n)
{
    while (n && !c->bad) {
        size_t room;
        while (c->pos >= c->end)
            if (!cur_continue(c))
                return;
        room = c->end - c->pos;
        if (room > n)
            room = (size_t)n;
        c->pos += room;
        n -= (unsigned long)room;
    }
}

/* cch characters, one byte each (the low half of UTF-16) or two, as UTF-8. */
static void cur_chars(xls_cur *c, unsigned long cch, int high, owf_buf *out)
{
    unsigned long surrogate = 0;
    while (cch && !c->bad) {
        unsigned long ch;
        if (c->pos >= c->end) {
            if (!cur_continue(c))
                break;
            if (c->pos < c->end)
                high = c->data[c->pos++] & 1;
            continue;
        }
        if (high) {
            if (c->end - c->pos < 2) {
                c->bad = 1;
                break;
            }
            ch = le16(c->data + c->pos);
            c->pos += 2;
        } else {
            ch = c->data[c->pos++];
        }
        cch--;
        if (surrogate) {
            if (ch >= 0xDC00 && ch < 0xE000) {
                owf_buf_put_utf8(out, 0x10000UL + ((surrogate - 0xD800) << 10) + (ch - 0xDC00));
                surrogate = 0;
                continue;
            }
            owf_buf_put_utf8(out, 0xFFFD);
            surrogate = 0;
        }
        if (ch >= 0xD800 && ch < 0xDC00)
            surrogate = ch;
        else if (ch)
            owf_buf_put_utf8(out, ch);
    }
    if (surrogate)
        owf_buf_put_utf8(out, 0xFFFD);
}

/* A string's flags, characters, and (rich) its formatting runs and
 * phonetic data, which are skipped. */
static void cur_string(xls_cur *c, unsigned long cch, int rich, owf_buf *out)
{
    unsigned flags = cur_u8(c), runs = 0;
    unsigned long ext = 0;
    if (rich && (flags & 8))
        runs = cur_u16(c);
    if (rich && (flags & 4))
        ext = cur_u32(c);
    cur_chars(c, cch, flags & 1, out);
    if (runs || ext)
        cur_skip(c, (unsigned long)runs * 4 + ext);
}

static char *take(xls *x, owf_buf *b)
{
    unsigned char *data;
    size_t length;
    if (owf_buf_take(b, &data, &length) != OWF_OK) {
        x->failed = 1;
        return NULL;
    }
    return (char *)data;
}

/* A string at the cursor with its length first, 16 bits (wide) or 8. */
static char *read_string(xls *x, xls_cur *c, int wide)
{
    owf_buf b;
    unsigned long cch = wide ? cur_u16(c) : cur_u8(c);
    owf_buf_init(&b);
    if (!c->bad)
        cur_string(c, cch, 0, &b);
    return take(x, &b);
}

static char *record_string(xls *x, const xls_rec *r, size_t off, int wide)
{
    xls_cur c;
    cur_start(&c, x, r, off);
    return read_string(x, &c, wide);
}

/* ---- Formulas: Excel's tokens ([MS-XLS] 2.5.198) back into text ---- */

/* How tightly each operator binds, loosest first. */
enum { P_COMPARE = 1, P_CONCAT, P_ADD, P_MUL, P_POWER, P_PERCENT, P_SIGN, P_UNION, P_ISECT, P_RANGE, P_ATOM };

typedef struct {
    char *text;
    int prec;
} xls_item;

typedef struct {
    xls *x;
    xls_item *items;
    int n, cap;
    int bad;                     /* the formula could not be read */
} xls_stack;

static void push(xls_stack *s, owf_buf *b, int prec)
{
    char *text = take(s->x, b);
    if (!text) {
        s->bad = 1;
        return;
    }
    if (!grow((void **)&s->items, s->n, &s->cap, sizeof *s->items)) {
        free(text);
        s->x->failed = 1;
        s->bad = 1;
        return;
    }
    s->items[s->n].text = text;
    s->items[s->n].prec = prec;
    s->n++;
}

static void push_text(xls_stack *s, const char *text)
{
    owf_buf b;
    owf_buf_init(&b);
    owf_buf_puts(&b, text);
    push(s, &b, P_ATOM);
}

/* An operand, in brackets when it binds more loosely than need. */
static void put_item(owf_buf *b, const xls_item *it, int need)
{
    if (it->prec < need) {
        owf_buf_putc(b, '(');
        owf_buf_puts(b, it->text);
        owf_buf_putc(b, ')');
    } else {
        owf_buf_puts(b, it->text);
    }
}

static void binary(xls_stack *s, const char *op, int prec)
{
    xls_item *a, *b;
    owf_buf out;
    if (s->n < 2) {
        s->bad = 1;
        return;
    }
    a = &s->items[s->n - 2];
    b = &s->items[s->n - 1];
    owf_buf_init(&out);
    put_item(&out, a, prec);
    owf_buf_puts(&out, op);
    put_item(&out, b, prec + 1);
    free(a->text);
    free(b->text);
    s->n -= 2;
    push(s, &out, prec);
}

static void unary(xls_stack *s, const char *before, const char *after, int prec)
{
    xls_item *a;
    owf_buf out;
    if (s->n < 1) {
        s->bad = 1;
        return;
    }
    a = &s->items[s->n - 1];
    owf_buf_init(&out);
    owf_buf_puts(&out, before);
    put_item(&out, a, prec);
    owf_buf_puts(&out, after);
    free(a->text);
    s->n--;
    push(s, &out, prec);
}

/* name(the top argc items); with name NULL, the first item is the name
 * (a user-defined or add-in function). */
static void function(xls_stack *s, const char *name, int argc)
{
    owf_buf out;
    int i, first;
    if (argc < 0 || argc > s->n || (!name && argc < 1)) {
        s->bad = 1;
        return;
    }
    first = s->n - argc;
    owf_buf_init(&out);
    if (!name) {
        name = s->items[first].text;
        if (!strncmp(name, "_xlfn.", 6))
            name += 6;
        owf_buf_puts(&out, name);
        free(s->items[first].text);
        first++;
    } else {
        owf_buf_puts(&out, name);
    }
    owf_buf_putc(&out, '(');
    for (i = first; i < s->n; i++) {
        if (i > first)
            owf_buf_putc(&out, ',');
        /* a union's comma would read as the next argument */
        put_item(&out, &s->items[i], s->items[i].prec == P_UNION ? P_ATOM : 0);
        free(s->items[i].text);
    }
    owf_buf_putc(&out, ')');
    s->n -= argc;
    push(s, &out, P_ATOM);
}

static int is_letter(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

/* A sheet's (or book's) name as a formula writes it: quoted unless it is
 * one word that could not be read as a cell ("A1", "IV10", "R1C1"). */
static int needs_quotes(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    int n = 0;
    if (!*p || is_digit(*p))
        return 1;
    for (; *p; p++)
        if (!(*p >= 0x80 || *p == '_' || *p == '.' || is_letter(*p) || is_digit(*p)))
            return 1;
    p = (const unsigned char *)name;
    while (is_letter(p[n]))
        n++;
    if (n >= 1 && n <= 3 && is_digit(p[n])) {
        while (is_digit(p[n]))
            n++;
        if (!p[n])
            return 1;
    }
    p = (const unsigned char *)name;
    if (owf_upper(*p) == 'R') {
        for (n = 1; is_digit(p[n]); n++)
            ;
        if (owf_upper(p[n]) == 'C') {
            for (n++; is_digit(p[n]); n++)
                ;
            if (!p[n])
                return 1;
        }
    }
    return 0;
}

static void put_unquoted(owf_buf *b, const char *s, int quoted)
{
    for (; *s; s++) {
        if (*s == '\'' && quoted)
            owf_buf_putc(b, '\'');
        owf_buf_putc(b, *s);
    }
}

/* "Sheet1", "Sheet1:Sheet3", "[Other.xls]Sheet1", quoted when a part needs it. */
static void put_sheet_names(owf_buf *b, const char *book, const char *first, const char *last)
{
    int quote = needs_quotes(first) || (last && needs_quotes(last)) || (book && needs_quotes(book));
    if (quote)
        owf_buf_putc(b, '\'');
    if (book) {
        owf_buf_putc(b, '[');
        put_unquoted(b, book, quote);
        owf_buf_putc(b, ']');
    }
    put_unquoted(b, first, quote);
    if (last) {
        owf_buf_putc(b, ':');
        put_unquoted(b, last, quote);
    }
    if (quote)
        owf_buf_putc(b, '\'');
}

/* "Sheet2!", "'Q1 sales'!", "Sheet1:Sheet3!", "'[Other.xls]Sheet1'!" for
 * an EXTERNSHEET entry: 1 written, 0 for a sheet that is gone, -1 for none
 * (a name for the whole book). */
static int put_sheets(xls *x, unsigned ixti, owf_buf *b)
{
    const xls_xti *t;
    const xls_supbook *sb;
    if (ixti >= x->nxti)
        return 0;
    t = &x->xti[ixti];
    if (t->supbook >= (unsigned)x->nsupbooks || t->first == 0xFFFF || t->last == 0xFFFF)
        return 0;
    if (t->first == 0xFFFE)
        return -1;
    sb = &x->supbooks[t->supbook];
    if (sb->kind == BOOK_SELF) {
        if (t->first >= (unsigned)x->nbs || t->last >= (unsigned)x->nbs)
            return 0;
        put_sheet_names(b, NULL, x->bs[t->first].name, t->last != t->first ? x->bs[t->last].name : NULL);
    } else if (sb->kind == BOOK_OTHER) {
        if (t->first >= (unsigned)sb->nsheets || t->last >= (unsigned)sb->nsheets)
            return 0;
        put_sheet_names(b, sb->file, sb->sheets[t->first], t->last != t->first ? sb->sheets[t->last] : NULL);
    } else {
        return 0;
    }
    owf_buf_putc(b, '!');
    return 1;
}

/* A cell: rw and col as the tokens hold them (col's top bits say which
 * are relative). In a shared formula the relative ones count from the
 * cell the formula is in. */
static void put_cell(owf_buf *b, unsigned rw, unsigned colf, int shared, unsigned row, unsigned col,
                     int no_row, int no_col)
{
    int rrel = (colf >> 15) & 1, crel = (colf >> 14) & 1;
    unsigned c = colf & 0x3FFF;
    char name[8];
    if (shared && rrel)
        rw = (unsigned)(row + (rw >= 0x8000 ? (long)rw - 0x10000L : (long)rw)) & 0xFFFF;
    if (shared && crel)
        c = (unsigned)(col + ((c & 0xFF) >= 0x80 ? (long)(c & 0xFF) - 0x100 : (long)(c & 0xFF))) & 0xFF;
    if (!no_col) {
        if (!crel)
            owf_buf_putc(b, '$');
        owf_column_name(c, name);
        owf_buf_puts(b, name);
    }
    if (!no_row) {
        if (!rrel)
            owf_buf_putc(b, '$');
        owf_buf_printf(b, "%lu", (unsigned long)rw + 1);
    }
}

/* An area: "A1:B4", "$A:$C" (whole columns), "2:3" (whole rows). */
static void put_area(owf_buf *b, const unsigned char *p, int shared, unsigned row, unsigned col)
{
    unsigned r0 = le16(p), r1 = le16(p + 2), c0 = le16(p + 4), c1 = le16(p + 6);
    /* in a shared formula, relative rows and columns are offsets */
    int rows_fixed = !shared || !((c0 | c1) & 0x8000), cols_fixed = !shared || !((c0 | c1) & 0x4000);
    int whole_cols = rows_fixed && r0 == 0 && r1 == 0xFFFF;
    int whole_rows = !whole_cols && cols_fixed && (c0 & 0xFF) == 0 && (c1 & 0xFF) == 0xFF;
    put_cell(b, r0, c0, shared, row, col, whole_cols, whole_rows);
    owf_buf_putc(b, ':');
    put_cell(b, r1, c1, shared, row, col, whole_cols, whole_rows);
}

/* A string literal, its quotes doubled. */
static void put_quoted(owf_buf *b, const char *s)
{
    owf_buf_putc(b, '"');
    for (; *s; s++) {
        if (*s == '"')
            owf_buf_putc(b, '"');
        owf_buf_putc(b, *s);
    }
    owf_buf_putc(b, '"');
}

/* An array constant's values ([MS-XLS] 2.5.198.8, PtgExtraArray):
 * "{1,2;3,4}"; returns the bytes used, 0 when they run out. */
static size_t put_array(xls *x, owf_buf *b, const unsigned char *p, size_t n)
{
    size_t at = 3;
    unsigned cols, rows, r, c;
    if (n < 3)
        return 0;
    cols = p[0] + 1u;
    rows = le16(p + 1) + 1;
    owf_buf_putc(b, '{');
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            unsigned type;
            if (c)
                owf_buf_putc(b, ',');
            else if (r)
                owf_buf_putc(b, ';');
            if (at >= n)
                return 0;
            type = p[at++];
            if (type == 0x02) {
                xls_cur cur;
                owf_buf s;
                char *text;
                if (n - at < 3)
                    return 0;
                cur_flat(&cur, p + at + 3, n - at - 3);
                owf_buf_init(&s);
                cur_chars(&cur, le16(p + at), p[at + 2] & 1, &s);
                if (cur.bad) {
                    owf_buf_free(&s);
                    return 0;
                }
                at += 3 + cur.pos;
                if (!(text = take(x, &s)))
                    return 0;
                put_quoted(b, text);
                free(text);
                continue;
            }
            if (n - at < 8)
                return 0;
            if (type == 0x01) {
                char num[32];
                owf_number_text(le_double(p + at), num);
                owf_buf_puts(b, num);
            } else if (type == 0x04) {
                owf_buf_puts(b, p[at] ? "TRUE" : "FALSE");
            } else if (type == 0x10) {
                const char *e = owf_error_name(p[at]);
                owf_buf_puts(b, e ? e : "#N/A");
            } else if (type != 0x00) {
                return 0;
            }
            at += 8;
        }
    }
    owf_buf_putc(b, '}');
    return at;
}

/* The formula text of tokens rgce (and their extra data), for the cell at
 * row, col; NULL when the tokens are not ones we can read. */
static char *decompile(xls *x, const unsigned char *rgce, unsigned cce, const unsigned char *extra,
                       size_t nextra, unsigned row, unsigned col, int shared)
{
    xls_stack s;
    size_t i = 0, ex = 0;
    char *result = NULL;
    int k;

    memset(&s, 0, sizeof s);
    s.x = x;
    while (i < cce && !s.bad) {
        unsigned ptg = rgce[i++], left = cce - (unsigned)i;
        owf_buf b;
        char num[32];
        owf_buf_init(&b);
        if (ptg >= 0x20)
            ptg = 0x20 | (ptg & 0x1F);     /* the same token in any class */
#define NEED(n) if (left < (n)) { s.bad = 1; break; }
        switch (ptg) {
        case 0x03: binary(&s, "+", P_ADD); break;
        case 0x04: binary(&s, "-", P_ADD); break;
        case 0x05: binary(&s, "*", P_MUL); break;
        case 0x06: binary(&s, "/", P_MUL); break;
        case 0x07: binary(&s, "^", P_POWER); break;
        case 0x08: binary(&s, "&", P_CONCAT); break;
        case 0x09: binary(&s, "<", P_COMPARE); break;
        case 0x0A: binary(&s, "<=", P_COMPARE); break;
        case 0x0B: binary(&s, "=", P_COMPARE); break;
        case 0x0C: binary(&s, ">=", P_COMPARE); break;
        case 0x0D: binary(&s, ">", P_COMPARE); break;
        case 0x0E: binary(&s, "<>", P_COMPARE); break;
        case 0x0F: binary(&s, " ", P_ISECT); break;
        case 0x10: binary(&s, ",", P_UNION); break;
        case 0x11: binary(&s, ":", P_RANGE); break;
        case 0x12: unary(&s, "+", "", P_SIGN); break;
        case 0x13: unary(&s, "-", "", P_SIGN); break;
        case 0x14: unary(&s, "", "%", P_PERCENT); break;
        case 0x15:                         /* brackets the author wrote */
            if (!s.n) {
                s.bad = 1;
                break;
            }
            owf_buf_putc(&b, '(');
            owf_buf_puts(&b, s.items[s.n - 1].text);
            owf_buf_putc(&b, ')');
            free(s.items[--s.n].text);
            push(&s, &b, P_ATOM);
            break;
        case 0x16:                         /* a missing argument */
            push_text(&s, "");
            break;
        case 0x17: {                       /* a string */
            xls_cur cur;
            char *text;
            NEED(2);
            cur_flat(&cur, rgce + i + 2, left - 2);
            cur_chars(&cur, rgce[i], rgce[i + 1] & 1, &b);
            if (cur.bad || !(text = take(x, &b))) {
                owf_buf_free(&b);
                s.bad = 1;
                break;
            }
            i += 2 + cur.pos;
            put_quoted(&b, text);
            free(text);
            push(&s, &b, P_ATOM);
            break;
        }
        case 0x19: {                       /* attributes: control flow, spaces, SUM */
            unsigned type;
            NEED(3);
            type = rgce[i];
            if (type & 0x04) {             /* CHOOSE's jump table */
                unsigned n = le16(rgce + i + 1);
                NEED(3 + 2 * (n + 1));
                i += 3 + 2 * (n + 1);
                break;
            }
            i += 3;
            if (type & 0x10)
                function(&s, "SUM", 1);
            break;
        }
        case 0x1C: {
            const char *e;
            NEED(1);
            e = owf_error_name(rgce[i++]);
            push_text(&s, e ? e : "#N/A");
            break;
        }
        case 0x1D:
            NEED(1);
            push_text(&s, rgce[i++] ? "TRUE" : "FALSE");
            break;
        case 0x1E:
            NEED(2);
            owf_buf_printf(&b, "%u", le16(rgce + i));
            i += 2;
            push(&s, &b, P_ATOM);
            break;
        case 0x1F:
            NEED(8);
            owf_number_text(le_double(rgce + i), num);
            i += 8;
            owf_buf_puts(&b, num);
            push(&s, &b, P_ATOM);
            break;
        case 0x20: {                       /* an array constant, its values after the tokens */
            size_t used;
            NEED(7);
            i += 7;
            used = ex <= nextra ? put_array(x, &b, extra + ex, nextra - ex) : 0;
            if (!used) {
                owf_buf_free(&b);
                s.bad = 1;
                break;
            }
            ex += used;
            push(&s, &b, P_ATOM);
            break;
        }
        case 0x21: {                       /* a function with a fixed count of arguments */
            const owf_xl_function *f;
            NEED(2);
            f = owf_xl_function_at(le16(rgce + i));
            i += 2;
            if (!f) {
                s.bad = 1;
                break;
            }
            function(&s, f->name, f->min);
            break;
        }
        case 0x22: {                       /* a function with a count of arguments */
            unsigned argc, tab;
            const owf_xl_function *f;
            NEED(3);
            argc = rgce[i] & 0x7F;
            tab = le16(rgce + i + 1);
            i += 3;
            if (tab & 0x8000) {            /* a macro command */
                s.bad = 1;
                break;
            }
            if (tab == 255) {
                function(&s, NULL, (int)argc);
                break;
            }
            f = owf_xl_function_at(tab);
            if (!f) {
                s.bad = 1;
                break;
            }
            function(&s, f->name, (int)argc);
            break;
        }
        case 0x23: {                       /* a defined name */
            unsigned long n;
            NEED(4);
            n = le32(rgce + i);
            i += 4;
            if (n < 1 || n > (unsigned long)x->nnames) {
                s.bad = 1;
                break;
            }
            push_text(&s, x->names[n - 1].name);
            break;
        }
        case 0x24:
        case 0x2C:                         /* a cell; RefN's relative parts are offsets */
            NEED(4);
            put_cell(&b, le16(rgce + i), le16(rgce + i + 2), shared || ptg == 0x2C, row, col, 0, 0);
            i += 4;
            push(&s, &b, P_ATOM);
            break;
        case 0x25:
        case 0x2D:
            NEED(8);
            put_area(&b, rgce + i, shared || ptg == 0x2D, row, col);
            i += 8;
            push(&s, &b, P_ATOM);
            break;
        case 0x26:                         /* the value of the sub-expression after */
        case 0x27:
        case 0x28:
            NEED(6);
            i += 6;
            if (ptg == 0x26) {             /* PtgExtraMem: its areas, 8 bytes each */
                if (ex + 2 > nextra || nextra - ex - 2 < 8u * le16(extra + ex)) {
                    s.bad = 1;
                    break;
                }
                ex += 2 + 8u * le16(extra + ex);
            }
            break;
        case 0x29:
            NEED(2);
            i += 2;
            break;
        case 0x2A:
        case 0x2B:
            NEED(ptg == 0x2A ? 4 : 8);
            i += ptg == 0x2A ? 4 : 8;
            push_text(&s, "#REF!");
            break;
        case 0x39: {                       /* a name in another book or an add-in */
            unsigned ixti;
            unsigned long n;
            const xls_supbook *sb;
            NEED(6);
            ixti = le16(rgce + i);
            n = le32(rgce + i + 2);
            i += 6;
            if (ixti >= x->nxti || x->xti[ixti].supbook >= (unsigned)x->nsupbooks) {
                s.bad = 1;
                break;
            }
            sb = &x->supbooks[x->xti[ixti].supbook];
            if (sb->kind == BOOK_SELF) {   /* this book's own names */
                if (n < 1 || n > (unsigned long)x->nnames) {
                    s.bad = 1;
                    break;
                }
                push_text(&s, x->names[n - 1].name);
                break;
            }
            if (n < 1 || n > (unsigned long)sb->nnames) {
                s.bad = 1;
                break;
            }
            if (sb->kind == BOOK_OTHER) {
                put_sheet_names(&b, NULL, sb->file, NULL);
                owf_buf_putc(&b, '!');
            }
            owf_buf_puts(&b, sb->names[n - 1]);
            push(&s, &b, P_ATOM);
            break;
        }
        case 0x3A:
        case 0x3B:
        case 0x3C:
        case 0x3D: {                       /* on another sheet */
            int there;
            NEED(ptg & 1 ? 10 : 6);
            there = ptg < 0x3C ? put_sheets(x, le16(rgce + i), &b) : 0;
            if (!there) {                  /* a sheet or cell since deleted */
                b.length = 0;
                owf_buf_puts(&b, "#REF!");
            } else if (ptg == 0x3A) {
                put_cell(&b, le16(rgce + i + 2), le16(rgce + i + 4), shared, row, col, 0, 0);
            } else {
                put_area(&b, rgce + i + 2, shared, row, col);
            }
            i += ptg & 1 ? 10 : 6;
            push(&s, &b, P_ATOM);
            break;
        }
        default:                           /* tables, macro tokens */
            s.bad = 1;
            break;
        }
#undef NEED
        owf_buf_free(&b);
    }
    if (!s.bad && s.n == 1) {
        result = s.items[0].text;
        s.n = 0;
    }
    for (k = 0; k < s.n; k++)
        free(s.items[k].text);
    free(s.items);
    return result;
}

/* ---- The workbook's globals ---- */

static const unsigned long default_palette[56] = {
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080,
    0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF,
    0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF, 0x800080, 0x800000, 0x008080, 0x0000FF,
    0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
    0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696,
    0x003366, 0x339966, 0x003300, 0x333300, 0x993300, 0x993366, 0x333399, 0x333333
};

/* A colour index: the eight fixed colours, then the palette; others
 * (64 and up) are the system's, automatic. */
static int colour_of(const xls *x, unsigned icv, unsigned long *rgb)
{
    static const unsigned long fixed[8] = { 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00,
                                            0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF };
    if (icv < 8)
        *rgb = fixed[icv];
    else if (icv < 64)
        *rgb = x->palette[icv - 8];
    else
        return 0;
    return 1;
}

/* The number formats Excel has without FORMAT records ([MS-XLS] 2.4.126). */
static const char *builtin_format(unsigned ifmt)
{
    static const char *const formats[50] = {
        "General", "0", "0.00", "#,##0", "#,##0.00", NULL, NULL, NULL, NULL, "0%",
        "0.00%", "0.00E+00", "# ?/?", "# ?\?/?\?", "m/d/yyyy", "d-mmm-yy", "d-mmm", "mmm-yy",
        "h:mm AM/PM", "h:mm:ss AM/PM", "h:mm", "h:mm:ss", "m/d/yyyy h:mm", NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "#,##0 ;(#,##0)",
        "#,##0 ;[Red](#,##0)", "#,##0.00;(#,##0.00)", "#,##0.00;[Red](#,##0.00)", NULL, NULL, NULL,
        NULL, "mm:ss", "[h]:mm:ss", "mm:ss.0", "##0.0E+0", "@"
    };
    return ifmt < 50 ? formats[ifmt] : NULL;
}

static int numfmt_of(xls *x, unsigned ifmt)
{
    const char *builtin;
    int i;
    for (i = 0; i < x->nnumfmts; i++)
        if (x->numfmts[i].ifmt == ifmt)
            return x->numfmts[i].index;
    builtin = builtin_format(ifmt);
    if (!builtin)
        return 0;
    i = owf_book_numfmt(x->book, builtin);
    if (i < 0) {
        x->failed = 1;
        return 0;
    }
    return i;
}

static void font_fmt(const xls *x, unsigned ifnt, owf_charfmt *fmt)
{
    const xls_font *f;
    unsigned i = ifnt >= 4 ? ifnt - 1 : ifnt;   /* there is no font 4 */
    unsigned long rgb;
    owf_charfmt_init(fmt);
    if (i >= (unsigned)x->nfonts)
        return;
    f = &x->fonts[i];
    if (f->weight >= 600)
        fmt->flags |= OWF_BOLD;
    if (f->grbit & 2)
        fmt->flags |= OWF_ITALIC;
    if (f->grbit & 8)
        fmt->flags |= OWF_STRIKE;
    if (f->underline)
        fmt->flags |= OWF_UNDERLINE;
    if (f->script == 1)
        fmt->flags |= OWF_SUPER;
    else if (f->script == 2)
        fmt->flags |= OWF_SUB;
    fmt->font = f->font;
    fmt->size = (int)f->height;
    if (colour_of(x, f->icv, &rgb))
        fmt->colour = rgb;
}

/* An XF record ([MS-XLS] 2.4.353) as a cell style. */
static void xf_cellstyle(xls *x, const unsigned char *p, owf_cellstyle *s)
{
    static const unsigned char halign[8] = { OWF_HALIGN_GENERAL, OWF_HALIGN_LEFT, OWF_HALIGN_CENTRE,
                                             OWF_HALIGN_RIGHT, OWF_HALIGN_FILL, OWF_HALIGN_JUSTIFY,
                                             OWF_HALIGN_CENTRE_ACROSS, OWF_HALIGN_JUSTIFY };
    static const unsigned char valign[8] = { OWF_VALIGN_TOP, OWF_VALIGN_CENTRE, OWF_VALIGN_BOTTOM,
                                             OWF_VALIGN_TOP, OWF_VALIGN_CENTRE, OWF_VALIGN_BOTTOM,
                                             OWF_VALIGN_BOTTOM, OWF_VALIGN_BOTTOM };
    unsigned long b1 = le32(p + 10), b2 = le32(p + 14), rgb;
    unsigned fill = le16(p + 18), fls = (unsigned)(b2 >> 26) & 0x3F, i;
    unsigned lines[4], icv[4];

    owf_cellstyle_init(s);
    font_fmt(x, le16(p), &s->font);
    s->number_format = numfmt_of(x, le16(p + 2));
    s->halign = halign[p[6] & 7];
    s->wrap = (p[6] >> 3) & 1;
    s->valign = valign[(p[6] >> 4) & 7];
    s->indent = p[8] & 15;
    lines[OWF_SIDE_LEFT] = b1 & 15;
    lines[OWF_SIDE_RIGHT] = (b1 >> 4) & 15;
    lines[OWF_SIDE_TOP] = (b1 >> 8) & 15;
    lines[OWF_SIDE_BOTTOM] = (b1 >> 12) & 15;
    icv[OWF_SIDE_LEFT] = (b1 >> 16) & 0x7F;
    icv[OWF_SIDE_RIGHT] = (b1 >> 23) & 0x7F;
    icv[OWF_SIDE_TOP] = b2 & 0x7F;
    icv[OWF_SIDE_BOTTOM] = (b2 >> 7) & 0x7F;
    for (i = 0; i < 4; i++) {
        s->border[i] = (unsigned char)(lines[i] <= OWF_LINE_SLANTED_DASH_DOT ? lines[i] : OWF_LINE_THIN);
        if (lines[i] && colour_of(x, icv[i], &rgb))
            s->border_colour[i] = OWF_SHADE(rgb);
    }
    if (fls && colour_of(x, fill & 0x7F, &rgb)) {
        s->fill = OWF_SHADE(rgb);
        if (fls != 1)
            owf_report_add(x->report, OWF_NOTE_APPROX, "Patterned cell backgrounds became solid colours");
    }
}

static void build_styles(xls *x)
{
    owf_cellstyle style;
    int i, normal;
    if (!x->nxf)
        return;
    x->xf_style = malloc((size_t)x->nxf * sizeof *x->xf_style);
    if (!x->xf_style) {
        x->failed = 1;
        return;
    }
    /* the book's default look is the Normal cell format's, XF 15 */
    normal = x->nxf > 15 ? 15 : 0;
    xf_cellstyle(x, x->xf[normal], &style);
    x->book->styles[0] = style;
    for (i = 0; i < x->nxf; i++) {
        xf_cellstyle(x, x->xf[i], &style);
        x->xf_style[i] = owf_book_style(x->book, &style);
        if (x->xf_style[i] < 0) {
            x->xf_style[i] = 0;
            x->failed = 1;
        }
    }
}

static void read_font(xls *x, const xls_rec *r)
{
    static const owf_font_kind kinds[6] = { OWF_FONT_ANY, OWF_FONT_SERIF, OWF_FONT_SANS,
                                            OWF_FONT_MONO, OWF_FONT_ANY, OWF_FONT_ANY };
    xls_font *f;
    char *name;
    if (r->len < 16)
        return;
    if (!grow((void **)&x->fonts, x->nfonts, &x->capfonts, sizeof *f)) {
        x->failed = 1;
        return;
    }
    if (!(name = record_string(x, r, 14, 0)))
        return;
    f = &x->fonts[x->nfonts];
    f->height = le16(r->p);
    f->grbit = le16(r->p + 2);
    f->icv = le16(r->p + 4);
    f->weight = le16(r->p + 6);
    f->script = le16(r->p + 8);
    f->underline = r->p[10];
    f->font = owf_book_font(x->book, name[0] ? name : "Arial", kinds[r->p[11] < 6 ? r->p[11] : 0]);
    free(name);
    if (f->font < 0) {
        x->failed = 1;
        return;
    }
    x->nfonts++;
}

static void read_format(xls *x, const xls_rec *r)
{
    char *text;
    int index;
    if (r->len < 5 || !(text = record_string(x, r, 2, 1)))
        return;
    index = owf_book_numfmt(x->book, text);
    free(text);
    if (index < 0 || !grow((void **)&x->numfmts, x->nnumfmts, &x->capnumfmts, sizeof *x->numfmts)) {
        x->failed = 1;
        return;
    }
    x->numfmts[x->nnumfmts].ifmt = le16(r->p);
    x->numfmts[x->nnumfmts].index = index;
    x->nnumfmts++;
}

static void read_boundsheet(xls *x, const xls_rec *r)
{
    xls_boundsheet *b;
    char *name;
    if (r->len < 8 || !(name = record_string(x, r, 6, 0)))
        return;
    if (!grow((void **)&x->bs, x->nbs, &x->capbs, sizeof *b)) {
        free(name);
        x->failed = 1;
        return;
    }
    b = &x->bs[x->nbs++];
    b->offset = le32(r->p);
    b->type = r->p[5];
    b->name = name;
    b->sheet = -1;
    if (b->type == 0) {
        owf_sheet *sheet = owf_book_add_sheet(x->book, name);
        if (!sheet) {
            x->failed = 1;
            return;
        }
        sheet->hidden = r->p[4] & 3;
        b->sheet = x->book->nsheets - 1;
    }
}

/* A file name from a SUPBOOK's encoded path ([MS-XLS] 2.5.277): the
 * part after the last folder. */
static char *file_name(xls *x, const char *path)
{
    const char *p, *start = path;
    owf_buf b;
    for (p = path; *p; p++)
        if ((unsigned char)*p < 0x20 || *p == '\\' || *p == '/' || *p == ':')
            start = p + 1;
    owf_buf_init(&b);
    owf_buf_puts(&b, start);
    return take(x, &b);
}

static void read_supbook(xls *x, const xls_rec *r)
{
    xls_supbook *sb;
    unsigned ctab, cch, i;
    xls_cur c;
    owf_buf b;
    char *path;
    if (r->len < 4)
        return;
    if (!grow((void **)&x->supbooks, x->nsupbooks, &x->capsupbooks, sizeof *sb)) {
        x->failed = 1;
        return;
    }
    sb = &x->supbooks[x->nsupbooks++];
    memset(sb, 0, sizeof *sb);
    ctab = le16(r->p);
    cch = le16(r->p + 2);
    if (cch == 0x0401) {
        sb->kind = BOOK_SELF;
        return;
    }
    if (cch == 0x3A01) {
        sb->kind = BOOK_ADDIN;
        return;
    }
    sb->kind = BOOK_OTHER;
    cur_start(&c, x, r, 4);
    owf_buf_init(&b);
    cur_string(&c, cch, 0, &b);
    if (!(path = take(x, &b)))
        return;
    sb->file = file_name(x, path);
    free(path);
    if (!sb->file || !ctab || c.bad)
        return;
    sb->sheets = calloc(ctab, sizeof *sb->sheets);
    if (!sb->sheets) {
        x->failed = 1;
        return;
    }
    for (i = 0; i < ctab && !c.bad; i++) {
        sb->sheets[i] = read_string(x, &c, 1);
        if (!sb->sheets[i])
            return;
        sb->nsheets++;
    }
}

static void read_externname(xls *x, const xls_rec *r)
{
    xls_supbook *sb;
    char *name;
    if (!x->nsupbooks || r->len < 8)
        return;
    sb = &x->supbooks[x->nsupbooks - 1];
    if (!(name = record_string(x, r, 6, 0)))
        return;
    if (!grow((void **)&sb->names, sb->nnames, &sb->capnames, sizeof *sb->names)) {
        free(name);
        x->failed = 1;
        return;
    }
    sb->names[sb->nnames++] = name;
}

static void read_externsheet(xls *x, const xls_rec *r)
{
    xls_cur c;
    unsigned n, i;
    if (x->xti || r->len < 2)
        return;
    cur_start(&c, x, r, 0);
    n = cur_u16(&c);
    x->xti = malloc((n ? n : 1) * sizeof *x->xti);
    if (!x->xti) {
        x->failed = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        x->xti[i].supbook = cur_u16(&c);
        x->xti[i].first = cur_u16(&c);
        x->xti[i].last = cur_u16(&c);
        if (c.bad)
            break;
    }
    x->nxti = i;
}

static void read_name(xls *x, const xls_rec *r)
{
    static const char *const builtin[14] = {
        "Consolidate_Area", "Auto_Open", "Auto_Close", "Extract", "Database", "Criteria", "Print_Area",
        "Print_Titles", "Recorder", "Data_Form", "Auto_Activate", "Auto_Deactivate", "Sheet_Title",
        "_FilterDatabase"
    };
    xls_name *n;
    xls_cur c;
    owf_buf b;
    unsigned grbit, cch, cce, itab;
    char *name;
    if (r->len < 15)
        return;
    grbit = le16(r->p);
    cch = r->p[3];
    cce = le16(r->p + 4);
    itab = le16(r->p + 8);
    cur_start(&c, x, r, 14);
    owf_buf_init(&b);
    cur_string(&c, cch, 0, &b);
    if (c.bad || c.pos > (size_t)(r->p + r->len - x->data)) {
        owf_buf_free(&b);
        return;
    }
    if ((grbit & 0x20) && b.length == 1 && b.data[0] < 14) {
        unsigned k = b.data[0];
        b.length = 0;
        owf_buf_puts(&b, builtin[k]);
    }
    if (!(name = take(x, &b)))
        return;
    if (!grow((void **)&x->names, x->nnames, &x->capnames, sizeof *n)) {
        free(name);
        x->failed = 1;
        return;
    }
    n = &x->names[x->nnames++];
    n->name = name;
    n->hidden = grbit & 1;
    n->sheet = itab >= 1 && itab <= (unsigned)x->nbs ? x->bs[itab - 1].sheet : -1;
    n->rgce = x->data + c.pos;
    n->cce = cce;
    if ((size_t)cce > (size_t)(r->p + r->len - n->rgce))
        n->cce = 0;                     /* cut short: no formula */
    n->extra = n->rgce + n->cce;
    n->nextra = (size_t)(r->p + r->len - n->extra);
    if (grbit & 0x0A)                   /* a macro's name: nothing to stand for */
        n->cce = 0;
}

static void read_sst(xls *x, const xls_rec *r)
{
    xls_cur c;
    unsigned long n, i;
    if (x->sst_at || r->len < 8)
        return;
    n = le32(r->p + 4);
    if (n > x->length / 3 + 1)          /* each string takes 3 bytes at least */
        n = x->length / 3 + 1;
    x->sst_at = malloc((n ? n : 1) * sizeof *x->sst_at);
    if (!x->sst_at) {
        x->failed = 1;
        return;
    }
    cur_start(&c, x, r, 8);
    for (i = 0; i < n; i++) {
        unsigned cch = cur_u16(&c);
        if (c.bad)
            break;
        x->sst_at[i] = x->sst.length;
        cur_string(&c, cch, 1, &x->sst);
        owf_buf_putc(&x->sst, 0);
    }
    x->nsst = i;
    if (x->sst.failed)
        x->failed = 1;
}

static int read_globals(xls *x, size_t *pos)
{
    xls_rec r;
    int depth = 1;
    while (depth && !x->failed && next_record(x, pos, &r)) {
        if (r.type == R_BOF) {
            depth++;
            continue;
        }
        if (r.type == R_EOF) {
            depth--;
            continue;
        }
        if (depth > 1)
            continue;
        switch (r.type) {
        case R_FILEPASS:
            owf_report_add(x->report, OWF_NOTE_LOST, "This workbook is protected with a password; protected workbooks cannot be opened yet");
            return OWF_ERR_UNSUPPORTED;
        case R_DATEMODE:
            if (r.len >= 2)
                x->book->date1904 = le16(r.p) != 0;
            break;
        case R_WINDOW1:
            if (r.len >= 12)
                x->active_tab = le16(r.p + 10);
            break;
        case R_FONT: read_font(x, &r); break;
        case R_FORMAT: read_format(x, &r); break;
        case R_XF:
            if (r.len >= 20) {
                if (!grow((void **)&x->xf, x->nxf, &x->capxf, sizeof *x->xf))
                    x->failed = 1;
                else
                    x->xf[x->nxf++] = r.p;
            }
            break;
        case R_PALETTE: {
            unsigned n = r.len >= 2 ? le16(r.p) : 0, i;
            for (i = 0; i < n && i < 56 && 2 + 4 * (size_t)i + 4 <= r.len; i++) {
                const unsigned char *c = r.p + 2 + 4 * i;
                x->palette[i] = ((unsigned long)c[0] << 16) | ((unsigned long)c[1] << 8) | c[2];
            }
            break;
        }
        case R_BOUNDSHEET: read_boundsheet(x, &r); break;
        case R_SUPBOOK: read_supbook(x, &r); break;
        case R_EXTERNNAME: read_externname(x, &r); break;
        case R_EXTERNSHEET: read_externsheet(x, &r); break;
        case R_NAME: read_name(x, &r); break;
        case R_SST: read_sst(x, &r); break;
        }
    }
    return x->failed ? OWF_ERR_MEMORY : OWF_OK;
}

static void build_names(xls *x)
{
    int i;
    for (i = 0; i < x->nnames && !x->failed; i++) {
        const xls_name *n = &x->names[i];
        char *formula;
        if (!n->cce)
            continue;
        formula = decompile(x, n->rgce, n->cce, n->extra, n->nextra, 0, 0, 0);
        if (!formula) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Some defined names could not be read");
            continue;
        }
        if (owf_book_add_name(x->book, n->name, n->sheet, formula, n->hidden) < 0)
            x->failed = 1;
        free(formula);
    }
}

/* ---- A worksheet ---- */

static owf_cell *cell_at(xls *x, const unsigned char *p)
{
    unsigned ixfe = le16(p + 4);
    owf_cell *c = owf_sheet_cell(x->sheet, le16(p), le16(p + 2), 1);
    if (!c) {
        x->failed = 1;
        return NULL;
    }
    c->style = ixfe < (unsigned)x->nxf && x->xf_style ? x->xf_style[ixfe] : 0;
    x->want_string = 0;
    return c;
}

static void set_text(xls *x, owf_cell *c, const char *text)
{
    if (owf_cell_set_text(c, text, strlen(text)) < 0)
        x->failed = 1;
}

static void set_formula(xls *x, owf_cell *c, const char *text)
{
    if (owf_cell_set_formula(c, text) < 0)
        x->failed = 1;
}

static void read_formula(xls *x, const xls_rec *r)
{
    const unsigned char *p = r->p, *val = p + 6;
    owf_cell *c;
    unsigned row, col, cce;
    char *text;
    if (r->len < 22 || !(c = cell_at(x, p)))
        return;
    row = le16(p);
    col = le16(p + 2);
    if (val[6] == 0xFF && val[7] == 0xFF) {
        switch (val[0]) {
        case 0:                         /* a string, in the STRING record after */
            set_text(x, c, "");
            x->want_string = 1;
            x->string_row = row;
            x->string_col = col;
            break;
        case 1: owf_cell_set_bool(c, val[2]); break;
        case 2: owf_cell_set_error(c, val[2]); break;
        default: set_text(x, c, ""); break;
        }
    } else {
        owf_cell_set_number(c, le_double(val));
    }
    cce = le16(p + 20);
    if (cce > r->len - 22)
        cce = (unsigned)(r->len - 22);
    if (cce == 5 && p[22] == 0x01) {    /* a shared or array formula's cell */
        xls_pending *pd;
        if (!grow((void **)&x->pending, x->npending, &x->cappending, sizeof *pd)) {
            x->failed = 1;
            return;
        }
        pd = &x->pending[x->npending++];
        pd->row = row;
        pd->col = col;
        pd->base_row = le16(p + 23);
        pd->base_col = le16(p + 25);
        return;
    }
    if (cce && p[22] == 0x02) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Data tables keep their values; their formulas are not read yet");
        return;
    }
    text = decompile(x, p + 22, cce, p + 22 + cce, r->len - 22 - cce, row, col, 0);
    if (!text) {
        owf_report_add(x->report, OWF_NOTE_LOST, "Some formulas could not be read; their cells keep their values");
        return;
    }
    if ((c = owf_sheet_cell(x->sheet, row, col, 0)) != NULL)
        set_formula(x, c, text);
    free(text);
}

static void read_shared(xls *x, const xls_rec *r, int array)
{
    xls_shared *s;
    size_t at = array ? 12 : 8;
    unsigned cce;
    if (r->len < at + 2)
        return;
    cce = le16(r->p + at);
    if (cce > r->len - at - 2)
        return;
    if (!grow((void **)&x->shared, x->nshared, &x->capshared, sizeof *s)) {
        x->failed = 1;
        return;
    }
    s = &x->shared[x->nshared++];
    s->r0 = le16(r->p);
    s->r1 = le16(r->p + 2);
    s->c0 = r->p[4];
    s->c1 = r->p[5];
    s->array = array;
    s->rgce = r->p + at + 2;
    s->cce = cce;
    s->extra = s->rgce + cce;
    s->nextra = r->len - at - 2 - cce;
}

static const xls_shared *find_shared(xls *x, const xls_pending *pd)
{
    int i, k;
    /* the one the last cell used, then the one based here, then any around */
    for (k = 0; k < 2; k++) {
        if (x->last_shared < x->nshared) {
            const xls_shared *s = &x->shared[x->last_shared];
            if (s->r0 == pd->base_row && s->c0 == pd->base_col)
                return s;
        }
        for (i = 0; i < x->nshared; i++) {
            const xls_shared *s = &x->shared[i];
            if (k ? (pd->row >= s->r0 && pd->row <= s->r1 && pd->col >= s->c0 && pd->col <= s->c1)
                  : (s->r0 == pd->base_row && s->c0 == pd->base_col)) {
                x->last_shared = i;
                return s;
            }
        }
    }
    return NULL;
}

static void resolve_shared(xls *x)
{
    int i;
    for (i = 0; i < x->npending && !x->failed; i++) {
        const xls_pending *pd = &x->pending[i];
        const xls_shared *s = find_shared(x, pd);
        owf_cell *c;
        char *text;
        if (!s) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Some formulas could not be read; their cells keep their values");
            continue;
        }
        text = decompile(x, s->rgce, s->cce, s->extra, s->nextra, pd->row, pd->col, !s->array);
        if (!text) {
            owf_report_add(x->report, OWF_NOTE_LOST, "Some formulas could not be read; their cells keep their values");
            continue;
        }
        if ((c = owf_sheet_cell(x->sheet, pd->row, pd->col, 0)) != NULL) {
            set_formula(x, c, text);
            if (s->array)
                c->flags |= OWF_CELL_ARRAY;
        }
        free(text);
    }
}

static void read_sheet(xls *x, const xls_boundsheet *bs)
{
    size_t pos = bs->offset;
    xls_rec r;
    int depth = 1, frozen = 0, std_width = 0;
    unsigned top = 0, left = 0;
    owf_sheet *sheet = &x->book->sheets[bs->sheet];

    if (!next_record(x, &pos, &r) || r.type != R_BOF || r.len < 4 || le16(r.p + 2) != 0x0010) {
        owf_report_add(x->report, OWF_NOTE_LOST, "A sheet could not be found in the file");
        return;
    }
    x->sheet = sheet;
    x->nshared = x->npending = x->last_shared = 0;
    x->want_string = 0;
    while (depth && !x->failed && next_record(x, &pos, &r)) {
        const unsigned char *p = r.p;
        owf_cell *c;
        unsigned i;
        if (r.type == R_BOF) {
            depth++;
            continue;
        }
        if (r.type == R_EOF) {
            depth--;
            continue;
        }
        if (depth > 1)
            continue;
        switch (r.type) {
        case R_NUMBER:
            if (r.len >= 14 && (c = cell_at(x, p)))
                owf_cell_set_number(c, le_double(p + 6));
            break;
        case R_RK:
            if (r.len >= 10 && (c = cell_at(x, p)))
                owf_cell_set_number(c, rk_number(le32(p + 6)));
            break;
        case R_MULRK:
            for (i = 0; r.len >= 6 && 4 + 6 * (size_t)i + 6 <= r.len - 2; i++) {
                unsigned char cell[6];
                const unsigned char *q = p + 4 + 6 * i;
                cell[0] = p[0];
                cell[1] = p[1];
                cell[2] = (unsigned char)((le16(p + 2) + i) & 0xFF);
                cell[3] = (unsigned char)((le16(p + 2) + i) >> 8);
                cell[4] = q[0];
                cell[5] = q[1];
                if (!(c = cell_at(x, cell)))
                    break;
                owf_cell_set_number(c, rk_number(le32(q + 2)));
            }
            break;
        case R_BLANK:
            if (r.len >= 6)
                cell_at(x, p);
            break;
        case R_MULBLANK:
            for (i = 0; r.len >= 6 && 4 + 2 * (size_t)i + 2 <= r.len - 2; i++) {
                unsigned char cell[6];
                cell[0] = p[0];
                cell[1] = p[1];
                cell[2] = (unsigned char)((le16(p + 2) + i) & 0xFF);
                cell[3] = (unsigned char)((le16(p + 2) + i) >> 8);
                cell[4] = p[4 + 2 * i];
                cell[5] = p[5 + 2 * i];
                if (!cell_at(x, cell))
                    break;
            }
            break;
        case R_LABELSST:
            if (r.len >= 10 && (c = cell_at(x, p))) {
                unsigned long n = le32(p + 6);
                if (n < x->nsst)
                    set_text(x, c, (const char *)x->sst.data + x->sst_at[n]);
                else
                    set_text(x, c, "");
            }
            break;
        case R_LABEL:
        case R_RSTRING:
            if (r.len >= 9) {
                char *text = record_string(x, &r, 6, 1);
                if (text && (c = cell_at(x, p)))
                    set_text(x, c, text);
                free(text);
            }
            break;
        case R_BOOLERR:
            if (r.len >= 8 && (c = cell_at(x, p))) {
                if (p[7])
                    owf_cell_set_error(c, p[6]);
                else
                    owf_cell_set_bool(c, p[6]);
            }
            break;
        case R_FORMULA:
            read_formula(x, &r);
            break;
        case R_STRING:
            if (x->want_string) {
                char *text = record_string(x, &r, 0, 1);
                x->want_string = 0;
                c = owf_sheet_cell(sheet, x->string_row, x->string_col, 0);
                if (text && c)
                    set_text(x, c, text);
                free(text);
            }
            break;
        case R_SHRFMLA: read_shared(x, &r, 0); break;
        case R_ARRAY: read_shared(x, &r, 1); break;
        case R_DEFCOLWIDTH:
            /* characters, and 5 pixels' margin, in eights of pixels */
            if (r.len >= 2 && !std_width)
                sheet->default_width = (int)(((le16(p) * 7 + 5 + 7) / 8 * 8) * 15);
            break;
        case R_STANDARDWIDTH:
            if (r.len >= 2) {
                sheet->default_width = (int)((unsigned long)le16(p) * 105 / 256);
                std_width = 1;
            }
            break;
        case R_DEFAULTROWHEIGHT:
            if (r.len >= 4 && !(le16(p) & 2))
                sheet->default_height = (int)le16(p + 2);
            break;
        case R_COLINFO:
            if (r.len >= 10) {
                unsigned first = le16(p), last = le16(p + 2), ixfe = le16(p + 6);
                if (first > 255)
                    break;
                if (last > 255)
                    last = 255;
                if (owf_sheet_set_columns(sheet, first, last, (int)((unsigned long)le16(p + 4) * 105 / 256),
                                          ixfe < (unsigned)x->nxf && x->xf_style ? x->xf_style[ixfe] : 0,
                                          le16(p + 8) & 1) < 0)
                    x->failed = 1;
            }
            break;
        case R_ROW:
            if (r.len >= 16) {
                unsigned height = le16(p + 6) & 0x7FFF;
                unsigned long flags = le32(p + 12);
                int hidden = (flags & 0x20) != 0;
                if ((flags & 0x40) || hidden || (int)height != sheet->default_height)
                    if (owf_sheet_set_row(sheet, le16(p), (int)height, hidden) < 0)
                        x->failed = 1;
            }
            break;
        case R_MERGEDCELLS:
            for (i = 0; r.len >= 2 && i < le16(p) && 2 + 8 * (size_t)i + 8 <= r.len; i++) {
                const unsigned char *q = p + 2 + 8 * i;
                owf_range range;
                range.first_row = le16(q);
                range.last_row = le16(q + 2);
                range.first_col = le16(q + 4);
                range.last_col = le16(q + 6);
                if (owf_sheet_merge(sheet, &range) < 0)
                    x->failed = 1;
            }
            break;
        case R_WINDOW2:
            if (r.len >= 6) {
                frozen = (le16(p) & 8) != 0;
                top = le16(p + 2);
                left = le16(p + 4);
            }
            break;
        case R_PANE:
            if (r.len >= 8 && frozen) {
                sheet->freeze_cols = le16(p) ? left + le16(p) : 0;
                sheet->freeze_rows = le16(p + 2) ? top + le16(p + 2) : 0;
            }
            break;
        case R_NOTE:
            owf_report_add(x->report, OWF_NOTE_LOST, "Comments on cells are not read yet");
            break;
        case R_OBJ:                     /* each drawn thing has one; a comment's is its box */
            if (r.len >= 6 && le16(p + 4) != 0x19)
                owf_report_add(x->report, OWF_NOTE_LOST, "Pictures, charts and drawings on sheets are not read yet");
            break;
        case R_HLINK:
            owf_report_add(x->report, OWF_NOTE_LOST, "Links in cells keep their text; where they go is not read yet");
            break;
        case R_CONDFMT:
            owf_report_add(x->report, OWF_NOTE_LOST, "Conditional formatting is not read yet");
            break;
        case R_DV:
            owf_report_add(x->report, OWF_NOTE_LOST, "Data validation (lists to pick from, limits) is not read yet");
            break;
        case R_TABLE:
            owf_report_add(x->report, OWF_NOTE_LOST, "Data tables keep their values; their formulas are not read yet");
            break;
        }
    }
    resolve_shared(x);
}

/* ---- The whole book ---- */

static int read_book(xls *x)
{
    size_t pos = 0;
    xls_rec r;
    int result, i;

    if (!next_record(x, &pos, &r) || r.type != R_BOF || r.len < 4)
        return OWF_ERR_CORRUPT;
    if (le16(r.p) != 0x0600) {
        owf_report_add(x->report, OWF_NOTE_LOST, "This workbook is from Excel 5 or Excel 95; only workbooks from Excel 97 and later are read yet");
        return OWF_ERR_UNSUPPORTED;
    }
    if (le16(r.p + 2) != 0x0005)
        return OWF_ERR_CORRUPT;
    result = read_globals(x, &pos);
    if (result != OWF_OK)
        return result;
    build_styles(x);
    build_names(x);
    for (i = 0; i < x->nbs && !x->failed; i++) {
        const xls_boundsheet *b = &x->bs[i];
        if (b->sheet >= 0)
            read_sheet(x, b);
        else if (b->type == 2)
            owf_report_add(x->report, OWF_NOTE_LOST, "Chart sheets are not read yet");
        else if (b->type == 1)
            owf_report_add(x->report, OWF_NOTE_LOST, "Excel 4 macro sheets are not read");
        else
            owf_report_add(x->report, OWF_NOTE_LOST, "Visual Basic modules are not read");
    }
    if (x->active_tab < (unsigned)x->nbs && x->bs[x->active_tab].sheet >= 0)
        x->book->active = x->bs[x->active_tab].sheet;
    if (!x->book->nsheets)
        owf_report_add(x->report, OWF_NOTE_INFO, "The workbook has no worksheets");
    return x->failed ? OWF_ERR_MEMORY : OWF_OK;
}

static void free_reader(xls *x)
{
    int i, k;
    owf_buf_free(&x->sst);
    free(x->sst_at);
    free(x->fonts);
    free(x->numfmts);
    free(x->xf);
    free(x->xf_style);
    for (i = 0; i < x->nbs; i++)
        free(x->bs[i].name);
    free(x->bs);
    for (i = 0; i < x->nsupbooks; i++) {
        xls_supbook *sb = &x->supbooks[i];
        free(sb->file);
        for (k = 0; k < sb->nsheets; k++)
            free(sb->sheets[k]);
        free(sb->sheets);
        for (k = 0; k < sb->nnames; k++)
            free(sb->names[k]);
        free(sb->names);
    }
    free(x->supbooks);
    free(x->xti);
    for (i = 0; i < x->nnames; i++)
        free(x->names[i].name);
    free(x->names);
    free(x->shared);
    free(x->pending);
}

/* Excel 2, 3 and 4 kept a workbook's records in the file itself, with no
 * compound file around them: a BOF record first. */
static int old_excel(const unsigned char *data, size_t length)
{
    unsigned type, len;
    if (length < 8)
        return 0;
    type = le16(data);
    len = le16(data + 2);
    return (type == 0x0009 || type == 0x0209 || type == 0x0409) && len >= 4 && len <= 20 &&
           (le16(data + 6) == 0x0010 || le16(data + 6) == 0x0020 || le16(data + 6) == 0x0040 ||
            le16(data + 6) == 0x0100 || (type == 0x0009 && le16(data + 6) <= 0x0040));
}

static int detect_xls(const unsigned char *data, size_t length)
{
    owf_cfb cfb;
    int score = 0;
    if (!owf_cfb_detect(data, length))
        return old_excel(data, length) ? 50 : 0;
    if (owf_cfb_open(&cfb, data, length) != OWF_OK)
        return 0;
    if (owf_cfb_has(&cfb, "Workbook"))
        score = 100;
    else if (owf_cfb_has(&cfb, "Book"))
        score = 90;
    owf_cfb_close(&cfb);
    return score;
}

static int import_xls(const unsigned char *data, size_t length, owf_book *book, owf_report *report)
{
    owf_cfb cfb;
    unsigned char *stream;
    size_t size;
    int result, cut;
    xls x;

    if (!owf_cfb_detect(data, length) && old_excel(data, length)) {
        owf_report_add(report, OWF_NOTE_LOST, "This workbook is from Excel 4 or earlier; only workbooks from Excel 97 and later are read yet");
        return OWF_ERR_UNSUPPORTED;
    }
    result = owf_cfb_open(&cfb, data, length);
    if (result != OWF_OK)
        return result;
    if (!owf_cfb_has(&cfb, "Workbook")) {
        int old = owf_cfb_has(&cfb, "Book");
        owf_cfb_close(&cfb);
        if (!old)
            return OWF_ERR_FORMAT;
        owf_report_add(report, OWF_NOTE_LOST, "This workbook is from Excel 5 or Excel 95; only workbooks from Excel 97 and later are read yet");
        return OWF_ERR_UNSUPPORTED;
    }
    result = owf_cfb_read(&cfb, "Workbook", &stream, &size);
    cut = cfb.cut_short;
    owf_cfb_close(&cfb);
    if (result != OWF_OK)
        return result;

    memset(&x, 0, sizeof x);
    x.data = stream;
    x.length = size;
    x.book = book;
    x.report = report;
    owf_buf_init(&x.sst);
    memcpy(x.palette, default_palette, sizeof x.palette);
    result = read_book(&x);
    if (result == OWF_OK && (cut || x.cut))
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the cells up to the cut were read");
    free_reader(&x);
    free(stream);
    return result;
}

const owf_book_format owf_book_format_xls = {
    "xls", "Excel 97-2003 workbook", "xls xlt", detect_xls, import_xls, NULL
};
