/*
 * GFOpenCalc's portable core: the tables of a web page, for External Data
 * (oc_extdata.c). A page's bytes are read for its <table>s, numbered in the
 * order they open (a table inside a cell is a table of its own, after the one
 * it is in, as LibreOffice's "HTML_1", "HTML_2" name them): each one's caption,
 * id and size, and one table's cells as text, with colspan and rowspan taking
 * their room (the cells they cover are left empty), entities decoded and
 * white space made single spaces. A page in ISO-8859-1 or Windows-1252 (its
 * meta charset says so, or a byte is not UTF-8) is made UTF-8. Scripts,
 * styles and comments are skipped. Written here from the HTML elements'
 * meanings; no parser of anyone else's is in it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WEB_DEPTH 32                  /* tables inside tables, at most */
#define WEB_MAX_SPAN 1000             /* colspan and rowspan beyond this count as this */
#define WEB_CELL_TEXT 32767           /* a cell's text, at most, in bytes */

/* ---- oc_exttable ---- */

void oc_exttable_free(oc_exttable *t)
{
    unsigned long i, n;
    if (!t)
        return;
    n = (unsigned long)t->rows * t->cols;
    if (t->cells)
        for (i = 0; i < n; i++)
            owf_free(t->cells[i].text);
    owf_free(t->cells);
    t->cells = NULL;
    t->rows = t->cols = 0;
}

int oc_exttable_alloc(oc_exttable *t, unsigned rows, unsigned cols)
{
    t->rows = t->cols = 0;
    t->cells = NULL;
    if (!rows || !cols)
        return OWF_OK;
    if ((unsigned long)rows * cols > (unsigned long)OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    if (!(t->cells = owf_calloc((size_t)rows * cols, sizeof *t->cells)))
        return OWF_ERR_MEMORY;
    t->rows = rows;
    t->cols = cols;
    return OWF_OK;
}

int oc_exttable_set(oc_exttable *t, unsigned row, unsigned col, int kind, double number, const char *text,
                    size_t length)
{
    oc_extvalue *v;
    char *copy = NULL;
    if (!t || row >= t->rows || col >= t->cols)
        return OWF_ERR_UNSUPPORTED;
    if ((kind == OC_EXTV_TEXT || kind == OC_EXTV_INPUT) && text) {
        if (!(copy = owf_malloc(length + 1)))
            return OWF_ERR_MEMORY;
        memcpy(copy, text, length);
        copy[length] = 0;
    }
    v = &t->cells[(size_t)row * t->cols + col];
    owf_free(v->text);
    v->kind = kind;
    v->number = number;
    v->text = copy;
    if ((kind == OC_EXTV_TEXT || kind == OC_EXTV_INPUT) && !copy)
        v->kind = OC_EXTV_EMPTY;
    return OWF_OK;
}

/* ---- the page's characters ---- */

/* Windows-1252's 0x80..0x9F (ISO-8859-1's are control codes, never meant on a page). */
static const unsigned short cp1252[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152,
    0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122,
    0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178
};

static size_t put_utf8(char *out, unsigned long c)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c >= 0xD800 && c < 0xE000)
        c = 0xFFFD;
    if (c < 0x10000) {
        out[0] = (char)(0xE0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    if (c > 0x10FFFF)
        return put_utf8(out, 0xFFFD);
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

static unsigned long latin_char(unsigned char b)
{
    return b >= 0x80 && b < 0xA0 ? cp1252[b - 0x80] : b;
}

/* The length of a well-formed UTF-8 character at s (n bytes left), or 0. */
static size_t utf8_len(const unsigned char *s, size_t n)
{
    size_t len, i;
    if (s[0] < 0x80)
        return 1;
    if (s[0] >= 0xC2 && s[0] <= 0xDF)
        len = 2;
    else if (s[0] >= 0xE0 && s[0] <= 0xEF)
        len = 3;
    else if (s[0] >= 0xF0 && s[0] <= 0xF4)
        len = 4;
    else
        return 0;
    if (len > n)
        return 0;
    for (i = 1; i < len; i++)
        if ((s[i] & 0xC0) != 0x80)
            return 0;
    return len;
}

static int ci_prefix(const char *s, size_t n, const char *word)
{
    size_t i;
    for (i = 0; word[i]; i++)
        if (i >= n || tolower((unsigned char)s[i]) != word[i])
            return 0;
    return 1;
}

/* 1 when the page says it is ISO-8859-1, Windows-1252 or ASCII (charset= in its first 4 KB). */
static int page_is_latin(const char *s, size_t n)
{
    size_t i, lim = n < 4096 ? n : 4096;
    for (i = 0; i + 8 < lim; i++)
        if (ci_prefix(s + i, lim - i, "charset")) {
            size_t j = i + 7;
            while (j < lim && (s[j] == ' ' || s[j] == '='  || s[j] == '"' || s[j] == '\''))
                j++;
            return ci_prefix(s + j, lim - j, "iso-8859-1") || ci_prefix(s + j, lim - j, "iso8859-1") ||
                   ci_prefix(s + j, lim - j, "windows-1252") || ci_prefix(s + j, lim - j, "latin1") ||
                   ci_prefix(s + j, lim - j, "cp1252") || ci_prefix(s + j, lim - j, "us-ascii");
        }
    return 0;
}

/* ---- entities ---- */

static const struct {
    const char *name;
    unsigned short code;
} entities[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 },
    { "pound", 0xA3 }, { "euro", 0x20AC }, { "yen", 0xA5 }, { "cent", 0xA2 }, { "copy", 0xA9 }, { "reg", 0xAE },
    { "trade", 0x2122 }, { "deg", 0xB0 }, { "plusmn", 0xB1 }, { "times", 0xD7 }, { "divide", 0xF7 },
    { "frac12", 0xBD }, { "frac14", 0xBC }, { "frac34", 0xBE }, { "sup2", 0xB2 }, { "sup3", 0xB3 },
    { "micro", 0xB5 }, { "middot", 0xB7 }, { "sect", 0xA7 }, { "para", 0xB6 }, { "laquo", 0xAB },
    { "raquo", 0xBB }, { "ndash", 0x2013 }, { "mdash", 0x2014 }, { "minus", 0x2212 }, { "hellip", 0x2026 },
    { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bull", 0x2022 },
    { "dagger", 0x2020 }, { "permil", 0x2030 }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "shy", 0xAD },
    { "thinsp", 0x2009 }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "auml", 0xE4 }, { "ouml", 0xF6 },
    { "uuml", 0xFC }, { "Auml", 0xC4 }, { "Ouml", 0xD6 }, { "Uuml", 0xDC }, { "szlig", 0xDF },
    { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "ecirc", 0xEA }, { "aacute", 0xE1 }, { "agrave", 0xE0 },
    { "acirc", 0xE2 }, { "ccedil", 0xE7 }, { "iacute", 0xED }, { "oacute", 0xF3 }, { "uacute", 0xFA },
    { "ntilde", 0xF1 }, { "Eacute", 0xC9 }, { "aring", 0xE5 }, { "Aring", 0xC5 }, { "oslash", 0xF8 },
    { "Oslash", 0xD8 }, { "aelig", 0xE6 }, { "AElig", 0xC6 }
};

/* An entity at s ("&amp;", "&#163;", "&#xA3;"): its character, *used the bytes it took; 0 when it is not one
 * (the "&" is then itself). */
static unsigned long entity(const char *s, size_t n, size_t *used)
{
    size_t i = 1;
    unsigned long c = 0;
    if (n < 3 || s[0] != '&')
        return 0;
    if (s[1] == '#') {
        int hexa = i + 1 < n && (s[2] == 'x' || s[2] == 'X'), digits = 0;
        i = hexa ? 3 : 2;
        for (; i < n && digits < 8; i++, digits++) {
            int d = s[i];
            if (d >= '0' && d <= '9')
                d -= '0';
            else if (hexa && d >= 'a' && d <= 'f')
                d -= 'a' - 10;
            else if (hexa && d >= 'A' && d <= 'F')
                d -= 'A' - 10;
            else
                break;
            c = c * (hexa ? 16u : 10u) + (unsigned long)d;
        }
        if (!digits)
            return 0;
        if (i < n && s[i] == ';')
            i++;
        *used = i;
        if (!c)
            return 0xFFFD;
        return c >= 0x80 && c < 0xA0 ? cp1252[c - 0x80] : c;   /* "&#150;" as browsers take it */
    }
    {
        size_t k, len;
        for (len = 1; len < n && len < 12 && isalnum((unsigned char)s[len]); len++)
            ;
        for (k = 0; k < sizeof entities / sizeof entities[0]; k++) {
            size_t el = strlen(entities[k].name);
            if (el == len - 1 && !memcmp(s + 1, entities[k].name, el)) {
                *used = len < n && s[len] == ';' ? len + 1 : len;
                return entities[k].code;
            }
        }
    }
    return 0;
}

/* ---- the reader ---- */

typedef struct {
    int number;                   /* 1.. */
    long row;                     /* the row open, -1 before the first */
    int row_open;
    unsigned col;                 /* where the next cell goes */
    unsigned rows, cols;          /* the size so far */
    int in_cell;
    unsigned cell_row, cell_col;  /* the cell open */
    unsigned ncarry;              /* columns with a rowspan's count */
    unsigned *carry;              /* rows a rowspan still takes after the row open, by column */
    unsigned char *taken;         /* taken in the row open (from above, or by a colspan) */
} web_table;

typedef struct {
    const char *page;
    size_t length;
    int latin;
    /* pass 1: what every table is */
    oc_webtable_info *info;
    int max, count;
    /* pass 2: the cells of one */
    int want;
    oc_exttable *out;             /* the cells, rows x cols as pass 1 found them */
    char *text;                   /* the cell open's text */
    size_t ntext;
    int space;                    /* a space is owed before the next character */
    int in_caption;
    int caption_of;               /* the stack's table the caption is for */
    web_table stack[WEB_DEPTH];
    int depth;                    /* tables open */
    int skipped;                  /* tables opened past the depth (their tags are ignored) */
    int failed;                   /* OWF_ERR_MEMORY */
} web_reader;

static web_table *top(web_reader *w)
{
    return w->depth && !w->skipped ? &w->stack[w->depth - 1] : NULL;
}

static oc_webtable_info *info_of(web_reader *w, const web_table *t)
{
    return w->info && t && t->number <= w->max ? &w->info[t->number - 1] : NULL;
}

static int collecting(web_reader *w)
{
    web_table *t = top(w);
    return w->out && t && t->number == w->want && t->in_cell;
}

/* The character c of the open cell's text (or the caption's). */
static void add_char(web_reader *w, unsigned long c)
{
    char buf[8];
    size_t n;
    int space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == 0xA0;
    if (w->in_caption && w->info) {
        web_table *t = w->depth ? &w->stack[w->caption_of] : NULL;
        oc_webtable_info *in = info_of(w, t);
        size_t len;
        if (!in)
            return;
        len = strlen(in->caption);
        if (space) {
            if (len && in->caption[len - 1] != ' ' && len + 1 < sizeof in->caption) {
                in->caption[len] = ' ';
                in->caption[len + 1] = 0;
            }
            return;
        }
        n = put_utf8(buf, c);
        if (len + n < sizeof in->caption) {
            memcpy(in->caption + len, buf, n);
            in->caption[len + n] = 0;
        }
        return;
    }
    if (!collecting(w))
        return;
    if (space) {
        if (w->ntext)
            w->space = 1;
        return;
    }
    n = put_utf8(buf, c);
    if (w->ntext + n + 1 >= WEB_CELL_TEXT)
        return;
    if (w->space) {
        w->text[w->ntext++] = ' ';
        w->space = 0;
    }
    memcpy(w->text + w->ntext, buf, n);
    w->ntext += n;
}

/* A run of the page's text, its entities decoded. */
static void add_text(web_reader *w, const char *s, size_t n)
{
    size_t i = 0;
    if (!collecting(w) && !(w->in_caption && w->info))
        return;
    while (i < n) {
        unsigned char b = (unsigned char)s[i];
        size_t used = 1, len;
        unsigned long c;
        if (b == '&' && (c = entity(s + i, n - i, &used)) != 0) {
            add_char(w, c);
            i += used;
            continue;
        }
        if (b < 0x80) {
            add_char(w, b);
            i++;
            continue;
        }
        if (!w->latin && (len = utf8_len((const unsigned char *)s + i, n - i)) > 1) {
            unsigned long u = b & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
            size_t k;
            for (k = 1; k < len; k++)
                u = (u << 6) | ((unsigned char)s[i + k] & 0x3F);
            add_char(w, u);
            i += len;
            continue;
        }
        add_char(w, latin_char(b));      /* not UTF-8: a byte of Windows-1252 */
        i++;
    }
}

static int grow_carry(web_reader *w, web_table *t, unsigned need)
{
    unsigned n = t->ncarry ? t->ncarry : 16, *c;
    unsigned char *k;
    if (need <= t->ncarry)
        return 1;
    while (n < need)
        n *= 2;
    if (!(c = owf_realloc(t->carry, n * sizeof *c)) ) {
        w->failed = OWF_ERR_MEMORY;
        return 0;
    }
    t->carry = c;
    if (!(k = owf_realloc(t->taken, n))) {
        w->failed = OWF_ERR_MEMORY;
        return 0;
    }
    t->taken = k;
    memset(t->carry + t->ncarry, 0, (n - t->ncarry) * sizeof *c);
    memset(t->taken + t->ncarry, 0, n - t->ncarry);
    t->ncarry = n;
    return 1;
}

static void end_cell(web_reader *w, web_table *t)
{
    if (!t->in_cell)
        return;
    t->in_cell = 0;
    if (w->out && t->number == w->want) {
        if (w->ntext && t->cell_row < w->out->rows && t->cell_col < w->out->cols &&
            oc_exttable_set(w->out, t->cell_row, t->cell_col, OC_EXTV_INPUT, 0, w->text, w->ntext) != OWF_OK)
            w->failed = OWF_ERR_MEMORY;
        w->ntext = 0;
        w->space = 0;
    }
}

static void end_row(web_reader *w, web_table *t)
{
    unsigned c;
    end_cell(w, t);
    if (!t->row_open)
        return;
    t->row_open = 0;
    for (c = 0; c < t->ncarry; c++)
        t->taken[c] = 0;
}

static void start_row(web_reader *w, web_table *t)
{
    unsigned c;
    end_row(w, t);
    t->row++;
    t->row_open = 1;
    t->col = 0;
    for (c = 0; c < t->ncarry; c++) {           /* a rowspan from above takes its column */
        t->taken[c] = t->carry[c] > 0;
        if (t->carry[c])
            t->carry[c]--;
    }
}

static void start_cell(web_reader *w, web_table *t, unsigned colspan, unsigned rowspan)
{
    unsigned c;
    end_cell(w, t);
    if (!t->row_open)
        start_row(w, t);
    while (t->col < t->ncarry && t->taken[t->col])
        t->col++;
    if (t->col >= OC_MAX_COLS || (unsigned long)t->row >= OC_MAX_ROWS)
        return;                          /* past what a sheet holds: dropped */
    if (colspan > OC_MAX_COLS - t->col)
        colspan = OC_MAX_COLS - t->col;
    if (!grow_carry(w, t, t->col + colspan))
        return;
    for (c = t->col; c < t->col + colspan; c++) {
        t->taken[c] = 1;
        if (rowspan - 1 > t->carry[c])
            t->carry[c] = rowspan - 1;
    }
    t->in_cell = 1;
    t->cell_row = (unsigned)t->row;
    t->cell_col = t->col;
    if ((unsigned)t->row + 1 > t->rows)
        t->rows = (unsigned)t->row + 1;
    if (t->col + colspan > t->cols)
        t->cols = t->col + colspan;
    t->col += colspan;
    if (w->out && t->number == w->want) {
        w->ntext = 0;
        w->space = 0;
    }
}

static void open_table(web_reader *w, const char *id, size_t idlen)
{
    web_table *t;
    oc_webtable_info *in;
    if (w->skipped || w->depth == WEB_DEPTH) {
        w->skipped++;
        return;
    }
    t = &w->stack[w->depth++];
    memset(t, 0, sizeof *t);
    t->number = ++w->count;
    t->row = -1;
    if ((in = info_of(w, t)) != NULL) {
        memset(in, 0, sizeof *in);
        in->number = t->number;
        in->depth = w->depth - 1;
        if (idlen >= sizeof in->id)
            idlen = sizeof in->id - 1;
        memcpy(in->id, id, idlen);
        in->id[idlen] = 0;
    }
}

static void close_table(web_reader *w)
{
    web_table *t;
    oc_webtable_info *in;
    if (w->skipped) {
        w->skipped--;
        return;
    }
    if (!w->depth)
        return;
    t = &w->stack[w->depth - 1];
    end_row(w, t);
    if (w->in_caption && w->caption_of == w->depth - 1)
        w->in_caption = 0;
    if ((in = info_of(w, t)) != NULL) {
        size_t n = strlen(in->caption);
        while (n && in->caption[n - 1] == ' ')
            in->caption[--n] = 0;
        in->rows = t->rows;
        in->cols = t->cols;
    }
    owf_free(t->carry);
    owf_free(t->taken);
    w->depth--;
}

/* An attribute's value in a tag's text (from after its name to its ">"): *len its length, NULL when it has
 * none. The value is as written (entities not decoded: ids and numbers have none). */
static const char *attribute(const char *s, size_t n, const char *name, size_t *len)
{
    size_t i = 0, nl = strlen(name);
    while (i < n) {
        size_t a, al;
        while (i < n && (isspace((unsigned char)s[i]) || s[i] == '/'))
            i++;
        a = i;
        while (i < n && !isspace((unsigned char)s[i]) && s[i] != '=' && s[i] != '>' && s[i] != '/')
            i++;
        al = i - a;
        while (i < n && isspace((unsigned char)s[i]))
            i++;
        if (i < n && s[i] == '=') {
            size_t v, vl;
            i++;
            while (i < n && isspace((unsigned char)s[i]))
                i++;
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                v = i;
                while (i < n && s[i] != q)
                    i++;
                vl = i - v;
                if (i < n)
                    i++;
            } else {
                v = i;
                while (i < n && !isspace((unsigned char)s[i]) && s[i] != '>')
                    i++;
                vl = i - v;
            }
            if (al == nl && ci_prefix(s + a, al, name)) {
                *len = vl;
                return s + v;
            }
        } else if (!al) {
            i++;
        }
    }
    return NULL;
}

static unsigned span(const char *s, size_t n, const char *name)
{
    size_t len = 0, i;
    const char *v = attribute(s, n, name, &len);
    unsigned long x = 0;
    if (!v)
        return 1;
    for (i = 0; i < len && v[i] >= '0' && v[i] <= '9'; i++)
        if ((x = x * 10 + (unsigned long)(v[i] - '0')) > WEB_MAX_SPAN)
            x = WEB_MAX_SPAN;
    return x ? (unsigned)x : 1u;
}

static int is_tag(const char *name, size_t n, const char *word)
{
    return strlen(word) == n && ci_prefix(name, n, word);
}

/* A tag: its name (n bytes), whether it closes, and its attributes' text. */
static void tag(web_reader *w, const char *name, size_t n, int closing, const char *attrs, size_t alen)
{
    web_table *t = top(w);
    if (is_tag(name, n, "table")) {
        if (closing) {
            close_table(w);
        } else {
            size_t idlen = 0;
            const char *id = attribute(attrs, alen, "id", &idlen);
            if (w->in_caption)
                w->in_caption = 0;
            open_table(w, id ? id : "", id ? idlen : 0);
        }
        return;
    }
    if (!t)
        return;
    if (is_tag(name, n, "tr")) {
        if (closing)
            end_row(w, t);
        else
            start_row(w, t);
    } else if (is_tag(name, n, "td") || is_tag(name, n, "th")) {
        if (closing)
            end_cell(w, t);
        else
            start_cell(w, t, span(attrs, alen, "colspan"), span(attrs, alen, "rowspan"));
    } else if (is_tag(name, n, "thead") || is_tag(name, n, "tbody") || is_tag(name, n, "tfoot")) {
        end_row(w, t);
    } else if (is_tag(name, n, "caption")) {
        if (closing) {
            w->in_caption = 0;
        } else if (!t->in_cell) {
            w->in_caption = 1;
            w->caption_of = w->depth - 1;
        }
    } else if (is_tag(name, n, "br") || is_tag(name, n, "p") || is_tag(name, n, "div") || is_tag(name, n, "li")) {
        add_char(w, ' ');                /* a line's end: a space between words */
    }
}

/* Skips raw text (a script's, a style's) to its closing tag: the position after it. */
static size_t skip_raw(const char *s, size_t n, size_t i, const char *name)
{
    size_t nl = strlen(name);
    for (; i + nl + 2 <= n; i++)
        if (s[i] == '<' && s[i + 1] == '/' && ci_prefix(s + i + 2, n - i - 2, name)) {
            const char *gt = memchr(s + i, '>', n - i);
            return gt ? (size_t)(gt - s) + 1 : n;
        }
    return n;
}

static int run(web_reader *w)
{
    const char *s = w->page;
    size_t n = w->length, i = 0, text = 0;
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        i = text = 3;
    w->latin = page_is_latin(s, n);
    while (i < n && !w->failed) {
        size_t a, nm, end;
        int closing = 0;
        const char *gt;
        if (s[i] != '<') {
            i++;
            continue;
        }
        if (i > text)
            add_text(w, s + text, i - text);
        if (i + 3 < n && !memcmp(s + i, "<!--", 4)) {
            size_t k;
            for (k = i + 4; k + 2 < n && memcmp(s + k, "-->", 3); k++)
                ;
            i = text = k + 2 < n ? k + 3 : n;
            continue;
        }
        if (i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '?')) {
            gt = memchr(s + i, '>', n - i);
            i = text = gt ? (size_t)(gt - s) + 1 : n;
            continue;
        }
        a = i + 1;
        if (a < n && s[a] == '/') {
            closing = 1;
            a++;
        }
        if (a >= n || !isalpha((unsigned char)s[a])) {
            add_text(w, "<", 1);         /* "<" not starting a tag: itself */
            i = text = i + 1;
            continue;
        }
        for (nm = a; nm < n && (isalnum((unsigned char)s[nm]) || s[nm] == '-' || s[nm] == ':'); nm++)
            ;
        /* the tag's end: the first ">" outside quotes */
        {
            char q = 0;
            for (end = nm; end < n; end++) {
                if (q) {
                    if (s[end] == q)
                        q = 0;
                } else if (s[end] == '"' || s[end] == '\'') {
                    q = s[end];
                } else if (s[end] == '>') {
                    break;
                }
            }
        }
        tag(w, s + a, nm - a, closing, s + nm, end - nm);
        i = text = end < n ? end + 1 : n;
        if (!closing && (is_tag(s + a, nm - a, "script") || is_tag(s + a, nm - a, "style") ||
                         is_tag(s + a, nm - a, "textarea") || is_tag(s + a, nm - a, "title")))
            i = text = skip_raw(s, n, i, is_tag(s + a, nm - a, "script") ? "script"
                                         : is_tag(s + a, nm - a, "style") ? "style"
                                         : is_tag(s + a, nm - a, "title") ? "title" : "textarea");
    }
    if (!w->failed && n > text)
        add_text(w, s + text, n - text);
    while (w->depth || w->skipped)
        close_table(w);                  /* a page cut short: what it had */
    return w->failed ? w->failed : OWF_OK;
}

int oc_web_tables(const char *page, size_t length, oc_webtable_info *info, int max)
{
    web_reader w;
    if (!page)
        return 0;
    memset(&w, 0, sizeof w);
    w.page = page;
    w.length = length;
    w.info = info;
    w.max = info ? max : 0;
    if (run(&w) != OWF_OK)
        return -1;
    return w.count;
}

static int same_text(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return !*a && !*b;
}

int oc_web_table_number(const char *page, size_t length, const char *which)
{
    oc_webtable_info *info;
    int count, i, number = 0;
    const char *p = which;
    if (!page || !which)
        return 0;
    while (*p == ' ')
        p++;
    if (ci_prefix(p, strlen(p), "html_"))
        p += 5;
    if (*p >= '0' && *p <= '9') {
        const char *q = p;
        long k = strtol(p, (char **)&q, 10);
        while (*q == ' ')
            q++;
        if (!*q && k > 0 && k < 1000000L)
            number = (int)k;
    }
    count = oc_web_tables(page, length, NULL, 0);
    if (count <= 0)
        return 0;
    if (number)
        return number <= count ? number : 0;
    if (!(info = owf_calloc((size_t)count, sizeof *info)))
        return -1;
    if (oc_web_tables(page, length, info, count) < 0) {
        owf_free(info);
        return -1;
    }
    for (p = which; *p == ' '; p++)
        ;
    for (i = 0; i < count && !number; i++)          /* an id first, then a caption */
        if (info[i].id[0] && same_text(info[i].id, p))
            number = i + 1;
    for (i = 0; i < count && !number; i++)
        if (info[i].caption[0] && same_text(info[i].caption, p))
            number = i + 1;
    owf_free(info);
    return number;
}

int oc_web_table(const char *page, size_t length, const char *which, oc_exttable *out)
{
    web_reader w;
    oc_webtable_info *info;
    int number, count, rc;
    if (!out)
        return OWF_ERR_UNSUPPORTED;
    out->rows = out->cols = 0;
    out->cells = NULL;
    number = oc_web_table_number(page, length, which);
    if (number < 0)
        return OWF_ERR_MEMORY;
    if (!number)
        return OWF_ERR_FORMAT;
    count = oc_web_tables(page, length, NULL, 0);
    if (count < number || !(info = owf_calloc((size_t)count, sizeof *info)))
        return OWF_ERR_MEMORY;
    if (oc_web_tables(page, length, info, count) < 0) {
        owf_free(info);
        return OWF_ERR_MEMORY;
    }
    rc = oc_exttable_alloc(out, info[number - 1].rows, info[number - 1].cols);
    owf_free(info);
    if (rc != OWF_OK)
        return rc;
    memset(&w, 0, sizeof w);
    w.page = page;
    w.length = length;
    w.want = number;
    w.out = out;
    if (!(w.text = owf_malloc(WEB_CELL_TEXT))) {
        oc_exttable_free(out);
        return OWF_ERR_MEMORY;
    }
    rc = run(&w);
    owf_free(w.text);
    if (rc != OWF_OK)
        oc_exttable_free(out);
    return rc;
}
