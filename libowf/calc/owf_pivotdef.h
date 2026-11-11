/*
 * libowf: the code of a pivot table's definition (owf_pivot.h). Included
 * once by libowf/src/owf_book.c, and by GFOpenCalc's src/calc/oc_pivotdef.c
 * when the core runs on owfcalc.library. The includer defines
 * OWF_PIVOT_NUMBER(text, length, double *) as its decimal reader (1 when the
 * whole text is a number). Only owf.h's calls: no C library beyond string
 * work and snprintf.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_pivot.h"

#include <stdio.h>
#include <string.h>

void owf_pivot_init(owf_pivotdef *def)
{
    memset(def, 0, sizeof *def);
    def->source_sheet = -1;
    def->row_grand = def->col_grand = 1;
}

int owf_pivot_is_name(const char *name)
{
    return name && (!strcmp(name, OWF_PIVOT_SOURCE) || !strcmp(name, OWF_PIVOT_CONFIG));
}

static int pivot_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static int pivot_same_word(const char *a, const char *b, size_t blen)
{
    size_t i;
    for (i = 0; i < blen; i++)
        if (!a[i] || pivot_lower((unsigned char)a[i]) != pivot_lower((unsigned char)b[i]))
            return 0;
    return !a[blen];
}

/* "B7" or "$B$7": its row and column; the length read, 0 when it is none. */
static size_t pivot_cell(const char *s, unsigned *row, unsigned *col)
{
    const char *p = s;
    unsigned long c = 0, r = 0;
    int letters = 0, digits = 0;
    if (*p == '$')
        p++;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
        c = c * 26 + (unsigned long)(pivot_lower((unsigned char)*p) - 'a' + 1);
        if (++letters > 3)
            return 0;
        p++;
    }
    if (*p == '$')
        p++;
    while (*p >= '0' && *p <= '9') {
        r = r * 10 + (unsigned long)(*p - '0');
        if (++digits > 7)
            return 0;
        p++;
    }
    if (!letters || !digits || !r || c > 16384 || r > 1048576)
        return 0;
    *row = (unsigned)(r - 1);
    *col = (unsigned)(c - 1);
    return (size_t)(p - s);
}

/* A sheet's name at s (quoted with '' for ', or plain up to stop): its
 * index, and where it ends. -1 when there is no such sheet. */
static int pivot_sheet(const owf_book *book, const char *s, char stop, const char **end)
{
    char name[256];
    size_t n = 0;
    const char *p = s;
    int i;
    if (*p == '$')
        p++;
    if (*p == '\'') {
        for (p++; *p; p++) {
            if (*p == '\'') {
                if (p[1] != '\'')
                    break;
                p++;
            }
            if (n + 1 >= sizeof name)
                return -1;
            name[n++] = *p;
        }
        if (*p != '\'')
            return -1;
        p++;
    } else {
        while (*p && *p != stop) {
            if (n + 1 >= sizeof name)
                return -1;
            name[n++] = *p++;
        }
    }
    if (*p != stop)
        return -1;
    name[n] = 0;
    *end = p + 1;
    for (i = 0; i < book->nsheets; i++)
        if (pivot_same_word(book->sheets[i].name, name, n))
            return i;
    return -1;
}

int owf_pivot_parse_source(const owf_book *book, const char *text, int *sheet, owf_range *range)
{
    const char *p = text, *bang, *rest;
    unsigned r0, c0, r1, c1;
    size_t k;
    int s, s2;
    if (!book || !text)
        return 0;
    if (*p == '=')
        p++;
    bang = strchr(p, '!');
    if (bang) {                                    /* Excel's: Sheet!A1:C9 */
        if ((s = pivot_sheet(book, p, '!', &rest)) < 0)
            return 0;
        if (!(k = pivot_cell(rest, &r0, &c0)) || rest[k] != ':')
            return 0;
        rest += k + 1;
        if (!(k = pivot_cell(rest, &r1, &c1)) || rest[k])
            return 0;
    } else {                                       /* ODF's: Sheet.A1:Sheet.C9 or Sheet.A1:.C9 */
        if ((s = pivot_sheet(book, p, '.', &rest)) < 0)
            return 0;
        if (!(k = pivot_cell(rest, &r0, &c0)) || rest[k] != ':')
            return 0;
        rest += k + 1;
        if (*rest == '.')
            rest++;
        else if (strchr(rest, '.')) {
            if ((s2 = pivot_sheet(book, rest, '.', &rest)) != s)
                return 0;
        }
        if (!(k = pivot_cell(rest, &r1, &c1)) || rest[k])
            return 0;
    }
    if (r1 < r0 || c1 < c0)
        return 0;
    *sheet = s;
    range->first_row = r0;
    range->first_col = c0;
    range->last_row = r1;
    range->last_col = c1;
    return 1;
}

static void pivot_col_name(unsigned col, char *out)
{
    char tmp[8];
    int n = 0, i;
    col++;
    while (col && n < 7) {
        tmp[n++] = (char)('A' + (col - 1) % 26);
        col = (col - 1) / 26;
    }
    for (i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

int owf_pivot_source_text(const owf_book *book, const owf_pivotdef *def, char *out, size_t size)
{
    const char *name;
    char first[8], last[8];
    size_t at = 0, i;
    int n;
    if (!book || !def || def->source_sheet < 0 || def->source_sheet >= book->nsheets || size < 4)
        return 0;
    name = book->sheets[def->source_sheet].name;
    out[at++] = '\'';
    for (i = 0; name[i]; i++) {
        if (at + 3 >= size)
            return 0;
        out[at++] = name[i];
        if (name[i] == '\'')
            out[at++] = '\'';
    }
    if (at + 3 >= size)
        return 0;
    out[at++] = '\'';
    out[at++] = '!';
    pivot_col_name(def->source.first_col, first);
    pivot_col_name(def->source.last_col, last);
    n = snprintf(out + at, size - at, "$%s$%u:$%s$%u", first, def->source.first_row + 1u, last,
                 def->source.last_row + 1u);
    return n > 0 && (size_t)n < size - at;
}

/* ---- The configuration text ----
 * "2;r=0,1;c=2;d=3:9,4:1;p=5:tEast;g=3;t=0,0,6,4" inside quotes: rows,
 * columns, data (field:function), pages (field:kind and the item, %XX for
 * the characters the text uses), grand totals (1 row, 2 column), the
 * output's top left and size. The oldest form, "0,1,2,9", named a row
 * column, a column column (-1 none), a value column and a function, all
 * absolute. */

typedef struct {
    char *out;
    size_t size, at;
    int failed;
} pivot_text;

static void pt_put(pivot_text *t, const char *s)
{
    while (*s) {
        if (t->at + 1 >= t->size) {
            t->failed = 1;
            return;
        }
        t->out[t->at++] = *s++;
    }
    t->out[t->at] = 0;
}

static void pt_unsigned(pivot_text *t, unsigned v)
{
    char n[16];
    snprintf(n, sizeof n, "%u", v);
    pt_put(t, n);
}

static void pt_item(pivot_text *t, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        char one[4];
        if (c < 0x20 || c == '%' || c == '"' || c == ';' || c == ',' || c == ':' || c == '=') {
            one[0] = '%';
            one[1] = hex[c >> 4];
            one[2] = hex[c & 15];
            one[3] = 0;
        } else {
            one[0] = (char)c;
            one[1] = 0;
        }
        pt_put(t, one);
    }
}

int owf_pivot_config_text(const owf_pivotdef *def, char *out, size_t size)
{
    pivot_text t;
    unsigned i;
    if (!def || !out || size < 8)
        return 0;
    t.out = out;
    t.size = size;
    t.at = 0;
    t.failed = 0;
    out[0] = 0;
    pt_put(&t, "\"2;r=");
    for (i = 0; i < def->nrow; i++) {
        if (i) pt_put(&t, ",");
        pt_unsigned(&t, def->row[i]);
    }
    pt_put(&t, ";c=");
    for (i = 0; i < def->ncol; i++) {
        if (i) pt_put(&t, ",");
        pt_unsigned(&t, def->col[i]);
    }
    pt_put(&t, ";d=");
    for (i = 0; i < def->ndata; i++) {
        if (i) pt_put(&t, ",");
        pt_unsigned(&t, def->data[i].field);
        pt_put(&t, ":");
        pt_unsigned(&t, (unsigned)def->data[i].function);
    }
    pt_put(&t, ";p=");
    for (i = 0; i < def->npage; i++) {
        const owf_pivot_page *pg = &def->page[i];
        char item[OWF_PIVOT_ITEM + 2];
        if (i) pt_put(&t, ",");
        pt_unsigned(&t, pg->field);
        pt_put(&t, ":");
        switch (pg->kind) {
        case OWF_PIVOT_TEXT: pt_put(&t, "t"); pt_item(&t, pg->text); break;
        case OWF_PIVOT_NUMBER: owf_number_text(pg->number, item); pt_put(&t, "n"); pt_item(&t, item); break;
        case OWF_PIVOT_BOOL: pt_put(&t, pg->number != 0.0 ? "b1" : "b0"); break;
        case OWF_PIVOT_BLANK: pt_put(&t, "e"); break;
        default: pt_put(&t, "a"); break;
        }
    }
    pt_put(&t, ";g=");
    pt_unsigned(&t, (def->row_grand ? 1u : 0u) | (def->col_grand ? 2u : 0u));
    pt_put(&t, ";t=");
    pt_unsigned(&t, def->at_row);
    pt_put(&t, ",");
    pt_unsigned(&t, def->at_col);
    pt_put(&t, ",");
    pt_unsigned(&t, def->rows);
    pt_put(&t, ",");
    pt_unsigned(&t, def->cols);
    pt_put(&t, "\"");
    return !t.failed;
}

/* A whole number at *p, up to 7 digits; 1 when there was one. */
static int pivot_number_at(const char **p, unsigned *v)
{
    unsigned long n = 0;
    int digits = 0;
    while (**p >= '0' && **p <= '9') {
        n = n * 10 + (unsigned long)(**p - '0');
        if (++digits > 7)
            return 0;
        (*p)++;
    }
    *v = (unsigned)n;
    return digits > 0;
}

static int pivot_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* An item's text up to , ; or the closing quote, %XX decoded. */
static int pivot_item_at(const char **p, char *out, size_t size)
{
    size_t n = 0;
    while (**p && **p != ',' && **p != ';' && **p != '"') {
        int c = (unsigned char)**p;
        if (c == '%') {
            int h = pivot_hex((unsigned char)(*p)[1]), l = h < 0 ? -1 : pivot_hex((unsigned char)(*p)[2]);
            if (h < 0 || l < 0)
                return 0;
            c = h * 16 + l;
            *p += 2;
        }
        if (n + 1 >= size)
            return 0;
        out[n++] = (char)c;
        (*p)++;
    }
    out[n] = 0;
    return 1;
}

static int parse_old_config(const char *p, owf_pivotdef *def)
{
    unsigned row, value, function;
    int column = -1;
    if (!pivot_number_at(&p, &row) || *p++ != ',')
        return 0;
    if (*p == '-') {
        if (p[1] != '1')
            return 0;
        p += 2;
    } else {
        unsigned c;
        if (!pivot_number_at(&p, &c))
            return 0;
        column = (int)c;
    }
    if (*p++ != ',' || !pivot_number_at(&p, &value) || *p++ != ',' || !pivot_number_at(&p, &function) ||
        *p != '"' || p[1])
        return 0;
    if (row < def->source.first_col || value < def->source.first_col ||
        (column >= 0 && (unsigned)column < def->source.first_col))
        return 0;
    def->nrow = 1;
    def->row[0] = row - def->source.first_col;
    if (column >= 0) {
        def->ncol = 1;
        def->col[0] = (unsigned)column - def->source.first_col;
    }
    def->ndata = 1;
    def->data[0].field = value - def->source.first_col;
    def->data[0].function = (int)function;
    return 1;
}

int owf_pivot_parse_config(const char *formula, owf_pivotdef *def)
{
    const char *p = formula;
    unsigned v, w;
    if (!p || !def)
        return 0;
    if (*p == '=')
        p++;
    if (*p++ != '"')
        return 0;
    def->nrow = def->ncol = def->ndata = def->npage = 0;
    if (p[0] != '2' || p[1] != ';')
        return parse_old_config(p, def);
    p += 2;
    while (*p && *p != '"') {
        char key = *p;
        if (p[1] != '=')
            return 0;
        p += 2;
        switch (key) {
        case 'r':
        case 'c':
            while (*p >= '0' && *p <= '9') {
                unsigned *n = key == 'r' ? &def->nrow : &def->ncol;
                if (!pivot_number_at(&p, &v) || *n >= OWF_PIVOT_FIELDS)
                    return 0;
                (key == 'r' ? def->row : def->col)[(*n)++] = v;
                if (*p == ',')
                    p++;
            }
            break;
        case 'd':
            while (*p >= '0' && *p <= '9') {
                if (!pivot_number_at(&p, &v) || *p++ != ':' || !pivot_number_at(&p, &w) ||
                    def->ndata >= OWF_PIVOT_DATA)
                    return 0;
                def->data[def->ndata].field = v;
                def->data[def->ndata++].function = (int)w;
                if (*p == ',')
                    p++;
            }
            break;
        case 'p':
            while (*p >= '0' && *p <= '9') {
                owf_pivot_page *pg;
                char kind;
                if (!pivot_number_at(&p, &v) || *p++ != ':' || def->npage >= OWF_PIVOT_PAGES)
                    return 0;
                pg = &def->page[def->npage];
                memset(pg, 0, sizeof *pg);
                pg->field = v;
                kind = *p++;
                if (kind == 't') {
                    pg->kind = OWF_PIVOT_TEXT;
                    if (!pivot_item_at(&p, pg->text, sizeof pg->text))
                        return 0;
                } else if (kind == 'n') {
                    if (!pivot_item_at(&p, pg->text, sizeof pg->text) ||
                        !OWF_PIVOT_NUMBER(pg->text, strlen(pg->text), &pg->number))
                        return 0;
                    pg->kind = OWF_PIVOT_NUMBER;
                    pg->text[0] = 0;
                } else if (kind == 'b' && (*p == '0' || *p == '1')) {
                    pg->kind = OWF_PIVOT_BOOL;
                    pg->number = *p++ == '1';
                } else if (kind == 'e') {
                    pg->kind = OWF_PIVOT_BLANK;
                } else if (kind == 'a') {
                    pg->kind = OWF_PIVOT_ALL;
                } else
                    return 0;
                def->npage++;
                if (*p == ',')
                    p++;
            }
            break;
        case 'g':
            if (!pivot_number_at(&p, &v) || v > 3)
                return 0;
            def->row_grand = (v & 1u) != 0;
            def->col_grand = (v & 2u) != 0;
            break;
        case 't':
            if (!pivot_number_at(&p, &def->at_row) || *p++ != ',' || !pivot_number_at(&p, &def->at_col) ||
                *p++ != ',' || !pivot_number_at(&p, &def->rows) || *p++ != ',' ||
                !pivot_number_at(&p, &def->cols))
                return 0;
            break;
        default:
            /* a later key: skipped */
            while (*p && *p != ';' && *p != '"')
                p++;
            break;
        }
        if (*p == ';')
            p++;
        else if (*p != '"')
            return 0;
    }
    return *p == '"' && !p[1];
}

int owf_pivot_check(const owf_book *book, const owf_pivotdef *def)
{
    unsigned width, i, k, used[OWF_PIVOT_FIELDS * 2 + OWF_PIVOT_PAGES], nused = 0;
    if (!book || !def || def->source_sheet < 0 || def->source_sheet >= book->nsheets ||
        def->source.first_row >= def->source.last_row || def->source.last_col < def->source.first_col ||
        !def->ndata || def->ndata > OWF_PIVOT_DATA || def->nrow > OWF_PIVOT_FIELDS ||
        def->ncol > OWF_PIVOT_FIELDS || def->npage > OWF_PIVOT_PAGES || !(def->nrow + def->ncol))
        return 0;
    width = def->source.last_col - def->source.first_col + 1u;
    for (i = 0; i < def->nrow; i++)
        used[nused++] = def->row[i];
    for (i = 0; i < def->ncol; i++)
        used[nused++] = def->col[i];
    for (i = 0; i < def->npage; i++)
        used[nused++] = def->page[i].field;
    for (i = 0; i < nused; i++) {
        if (used[i] >= width)
            return 0;
        for (k = 0; k < i; k++)
            if (used[k] == used[i])
                return 0;
    }
    for (i = 0; i < def->ndata; i++)
        if (def->data[i].field >= width || def->data[i].function < OWF_PIVOT_FN_FIRST ||
            def->data[i].function > OWF_PIVOT_FN_LAST)
            return 0;
    return 1;
}

static const owf_defname *pivot_find_name(const owf_book *book, int sheet, const char *wanted)
{
    int i;
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].sheet == sheet && !strcmp(book->names[i].name, wanted))
            return &book->names[i];
    return NULL;
}

int owf_pivot_get(const owf_book *book, int sheet, owf_pivotdef *def)
{
    const owf_defname *source, *config;
    owf_pivot_init(def);
    if (!book || sheet < 0 || sheet >= book->nsheets)
        return 0;
    source = pivot_find_name(book, sheet, OWF_PIVOT_SOURCE);
    config = pivot_find_name(book, sheet, OWF_PIVOT_CONFIG);
    if (!source || !config ||
        !owf_pivot_parse_source(book, source->formula, &def->source_sheet, &def->source) ||
        !owf_pivot_parse_config(config->formula, def))
        return 0;
    return owf_pivot_check(book, def);
}

/* ---- Items ---- */

int owf_pivot_item_matches(const owf_pivot_page *page, const owf_cell *cell)
{
    owf_value_type type = cell ? cell->type : OWF_VALUE_EMPTY;
    switch (page->kind) {
    case OWF_PIVOT_ALL: return 1;
    case OWF_PIVOT_TEXT: return type == OWF_VALUE_TEXT && !strcmp(cell->text ? cell->text : "", page->text);
    case OWF_PIVOT_NUMBER: return type == OWF_VALUE_NUMBER && cell->number == page->number;
    case OWF_PIVOT_BOOL: return type == OWF_VALUE_BOOL && (cell->number != 0.0) == (page->number != 0.0);
    case OWF_PIVOT_BLANK: return type == OWF_VALUE_EMPTY || (type == OWF_VALUE_TEXT && !(cell->text && *cell->text));
    default: return 0;
    }
}

void owf_pivot_item_from_cell(owf_pivot_page *page, const owf_cell *cell)
{
    owf_value_type type = cell ? cell->type : OWF_VALUE_EMPTY;
    page->text[0] = 0;
    page->number = 0.0;
    if (type == OWF_VALUE_TEXT && cell->text && *cell->text) {
        page->kind = OWF_PIVOT_TEXT;
        snprintf(page->text, sizeof page->text, "%s", cell->text);
    } else if (type == OWF_VALUE_NUMBER) {
        page->kind = OWF_PIVOT_NUMBER;
        page->number = cell->number;
    } else if (type == OWF_VALUE_BOOL) {
        page->kind = OWF_PIVOT_BOOL;
        page->number = cell->number != 0.0;
    } else
        page->kind = OWF_PIVOT_BLANK;
}

void owf_pivot_item_from_text(owf_pivot_page *page, const char *text)
{
    page->text[0] = 0;
    page->number = 0.0;
    if (!text || !*text) {
        page->kind = OWF_PIVOT_BLANK;
    } else if (OWF_PIVOT_NUMBER(text, strlen(text), &page->number)) {
        page->kind = OWF_PIVOT_NUMBER;
    } else {
        page->kind = OWF_PIVOT_TEXT;
        page->number = 0.0;
        snprintf(page->text, sizeof page->text, "%s", text);
    }
}

void owf_pivot_item_text(const owf_pivot_page *page, char *out, size_t size)
{
    char n[40];
    if (!size)
        return;
    switch (page->kind) {
    case OWF_PIVOT_TEXT: snprintf(out, size, "%s", page->text); break;
    case OWF_PIVOT_NUMBER: owf_number_text(page->number, n); snprintf(out, size, "%s", n); break;
    case OWF_PIVOT_BOOL: snprintf(out, size, "%s", page->number != 0.0 ? "TRUE" : "FALSE"); break;
    default: out[0] = 0; break;
    }
}

/* ---- Functions ---- */

static const char *const pivot_odf_words[] = {
    "", "average", "countnums", "count", "max", "min", "product", "stdev", "stdevp", "sum", "var", "varp"
};
static const char *const pivot_xml_words[] = {
    "", "average", "countNums", "count", "max", "min", "product", "stdDev", "stdDevp", "sum", "var", "varp"
};

const char *owf_pivot_function_word(int function, int ooxml)
{
    if (function < OWF_PIVOT_FN_FIRST || function > OWF_PIVOT_FN_LAST)
        return NULL;
    return ooxml ? pivot_xml_words[function] : pivot_odf_words[function];
}

int owf_pivot_function_from_word(const char *word)
{
    int i;
    if (!word)
        return 0;
    for (i = OWF_PIVOT_FN_FIRST; i <= OWF_PIVOT_FN_LAST; i++)
        if (pivot_same_word(word, pivot_odf_words[i], strlen(pivot_odf_words[i])) ||
            pivot_same_word(word, pivot_xml_words[i], strlen(pivot_xml_words[i])))
            return i;
    return 0;
}

void owf_pivot_field_heading(const owf_book *book, const owf_pivotdef *def, unsigned field, char *out,
                             size_t size)
{
    const owf_cell *cell = NULL;
    char n[40];
    if (!size)
        return;
    if (book && def && def->source_sheet >= 0 && def->source_sheet < book->nsheets)
        cell = owf_sheet_find(&book->sheets[def->source_sheet], def->source.first_row,
                              def->source.first_col + field);
    if (cell && cell->type == OWF_VALUE_TEXT && cell->text && *cell->text)
        snprintf(out, size, "%s", cell->text);
    else if (cell && cell->type == OWF_VALUE_NUMBER) {
        owf_number_text(cell->number, n);
        snprintf(out, size, "%s", n);
    } else if (cell && cell->type == OWF_VALUE_BOOL)
        snprintf(out, size, "%s", cell->number != 0.0 ? "TRUE" : "FALSE");
    else
        snprintf(out, size, "Column %u", field + 1u);
}
