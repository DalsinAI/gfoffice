/*
 * libowf: the workbook model (the spreadsheet's), numbers as text, and the
 * table of workbook formats.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

static char *copy_text(const char *s, size_t length)
{
    char *out = malloc(length + 1);
    if (!out)
        return NULL;
    memcpy(out, s, length);
    out[length] = 0;
    return out;
}

/* Makes room for one more item in an array of size bytes each. */
static int grow(void **items, int count, int *capacity, size_t size, int first)
{
    void *p;
    int want;
    if (count < *capacity)
        return 1;
    want = *capacity ? *capacity * 2 : first;
    p = realloc(*items, (size_t)want * size);
    if (!p)
        return 0;
    *items = p;
    *capacity = want;
    return 1;
}

const char *owf_error_name(int error)
{
    switch (error) {
    case OWF_ERROR_NULL: return "#NULL!";
    case OWF_ERROR_DIV0: return "#DIV/0!";
    case OWF_ERROR_VALUE: return "#VALUE!";
    case OWF_ERROR_REF: return "#REF!";
    case OWF_ERROR_NAME: return "#NAME?";
    case OWF_ERROR_NUM: return "#NUM!";
    case OWF_ERROR_NA: return "#N/A";
    }
    return NULL;
}

/* ---- The book ---- */

void owf_cellstyle_init(owf_cellstyle *style)
{
    memset(style, 0, sizeof *style);
    owf_charfmt_init(&style->font);
    style->halign = OWF_HALIGN_GENERAL;
    style->valign = OWF_VALIGN_BOTTOM;
}

owf_book *owf_book_new(void)
{
    owf_book *book = calloc(1, sizeof *book);
    owf_cellstyle style;
    if (!book)
        return NULL;
    owf_cellstyle_init(&style);
    if (owf_book_style(book, &style) != 0 || owf_book_numfmt(book, "General") != 0) {
        owf_book_free(book);
        return NULL;
    }
    return book;
}

static void free_sheet(owf_sheet *sheet)
{
    int i;
    for (i = 0; i < sheet->ncells; i++) {
        free(sheet->cells[i].text);
        free(sheet->cells[i].formula);
    }
    free(sheet->cells);
    free(sheet->cols);
    free(sheet->rows);
    free(sheet->merges);
    free(sheet->name);
}

void owf_book_free(owf_book *book)
{
    int i;
    if (!book)
        return;
    for (i = 0; i < book->nsheets; i++)
        free_sheet(&book->sheets[i]);
    free(book->sheets);
    free(book->styles);
    for (i = 0; i < book->nnumfmts; i++)
        free(book->numfmts[i]);
    free(book->numfmts);
    for (i = 0; i < book->nfonts; i++)
        free(book->fonts[i].name);
    free(book->fonts);
    for (i = 0; i < book->nnames; i++) {
        free(book->names[i].name);
        free(book->names[i].formula);
    }
    free(book->names);
    free(book);
}

owf_sheet *owf_book_add_sheet(owf_book *book, const char *name)
{
    owf_sheet *sheet;
    if (!grow((void **)&book->sheets, book->nsheets, &book->capsheets, sizeof *sheet, 4))
        return NULL;
    sheet = &book->sheets[book->nsheets];
    memset(sheet, 0, sizeof *sheet);
    sheet->name = copy_text(name, strlen(name));
    if (!sheet->name)
        return NULL;
    sheet->default_width = 960;      /* Excel's: 64 pixels, 8.43 characters of Arial 10 */
    sheet->default_height = 255;     /* and 12.75 points */
    book->nsheets++;
    return sheet;
}

static int same_style(const owf_cellstyle *a, const owf_cellstyle *b)
{
    int i;
    if (a->font.flags != b->font.flags || a->font.font != b->font.font || a->font.size != b->font.size ||
        a->font.colour != b->font.colour || a->number_format != b->number_format || a->halign != b->halign ||
        a->valign != b->valign || a->wrap != b->wrap || a->indent != b->indent || a->fill != b->fill)
        return 0;
    for (i = 0; i < 4; i++)
        if (a->border[i] != b->border[i] || a->border_colour[i] != b->border_colour[i])
            return 0;
    return 1;
}

int owf_book_style(owf_book *book, const owf_cellstyle *style)
{
    int i;
    for (i = 0; i < book->nstyles; i++)
        if (same_style(&book->styles[i], style))
            return i;
    if (!grow((void **)&book->styles, book->nstyles, &book->capstyles, sizeof *style, 16))
        return -1;
    book->styles[book->nstyles] = *style;
    return book->nstyles++;
}

int owf_book_numfmt(owf_book *book, const char *format)
{
    int i;
    for (i = 0; i < book->nnumfmts; i++)
        if (!strcmp(book->numfmts[i], format))
            return i;
    if (!grow((void **)&book->numfmts, book->nnumfmts, &book->capnumfmts, sizeof(char *), 16))
        return -1;
    book->numfmts[book->nnumfmts] = copy_text(format, strlen(format));
    if (!book->numfmts[book->nnumfmts])
        return -1;
    return book->nnumfmts++;
}

int owf_book_font(owf_book *book, const char *name, owf_font_kind kind)
{
    int i;
    for (i = 0; i < book->nfonts; i++)
        if (!strcmp(book->fonts[i].name, name))
            return i;
    if (!grow((void **)&book->fonts, book->nfonts, &book->capfonts, sizeof(owf_font), 8))
        return -1;
    book->fonts[book->nfonts].name = copy_text(name, strlen(name));
    if (!book->fonts[book->nfonts].name)
        return -1;
    book->fonts[book->nfonts].kind = kind;
    return book->nfonts++;
}

/* ---- Cells ---- */

/* Where the cell at row, col is, or would go. */
static int cell_place(const owf_sheet *sheet, unsigned row, unsigned col)
{
    int lo = 0, hi = sheet->ncells;
    const owf_cell *last = sheet->ncells ? &sheet->cells[sheet->ncells - 1] : NULL;
    if (!last || last->row < row || (last->row == row && last->col < col))
        return sheet->ncells;            /* in order, as files give them */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const owf_cell *c = &sheet->cells[mid];
        if (c->row < row || (c->row == row && c->col < col))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

const owf_cell *owf_sheet_find(const owf_sheet *sheet, unsigned row, unsigned col)
{
    int at = cell_place(sheet, row, col);
    if (at < sheet->ncells && sheet->cells[at].row == row && sheet->cells[at].col == col)
        return &sheet->cells[at];
    return NULL;
}

owf_cell *owf_sheet_cell(owf_sheet *sheet, unsigned row, unsigned col, int create)
{
    int at = cell_place(sheet, row, col);
    owf_cell *c;
    if (at < sheet->ncells && sheet->cells[at].row == row && sheet->cells[at].col == col)
        return &sheet->cells[at];
    if (!create || !grow((void **)&sheet->cells, sheet->ncells, &sheet->capcells, sizeof *c, 64))
        return NULL;
    c = &sheet->cells[at];
    if (at < sheet->ncells)
        memmove(c + 1, c, (size_t)(sheet->ncells - at) * sizeof *c);
    memset(c, 0, sizeof *c);
    c->row = row;
    c->col = col;
    c->type = OWF_VALUE_EMPTY;
    sheet->ncells++;
    return c;
}

static void clear_value(owf_cell *cell)
{
    free(cell->text);
    cell->text = NULL;
    cell->number = 0;
    cell->error = 0;
}

void owf_cell_set_number(owf_cell *cell, double number)
{
    clear_value(cell);
    cell->type = OWF_VALUE_NUMBER;
    cell->number = number;
}

int owf_cell_set_text(owf_cell *cell, const char *utf8, size_t length)
{
    char *text = copy_text(utf8, length);
    if (!text)
        return -1;
    clear_value(cell);
    cell->type = OWF_VALUE_TEXT;
    cell->text = text;
    return 0;
}

void owf_cell_set_bool(owf_cell *cell, int value)
{
    clear_value(cell);
    cell->type = OWF_VALUE_BOOL;
    cell->number = value ? 1 : 0;
}

void owf_cell_set_error(owf_cell *cell, int error)
{
    clear_value(cell);
    cell->type = OWF_VALUE_ERROR;
    cell->error = error;
}

int owf_cell_set_formula(owf_cell *cell, const char *text)
{
    char *formula = NULL;
    if (text && !(formula = copy_text(text, strlen(text))))
        return -1;
    free(cell->formula);
    cell->formula = formula;
    return 0;
}

int owf_sheet_set_columns(owf_sheet *sheet, unsigned first, unsigned last, int width, int style, int hidden)
{
    owf_colinfo *c;
    if (!grow((void **)&sheet->cols, sheet->ncols, &sheet->capcols, sizeof *c, 8))
        return -1;
    c = &sheet->cols[sheet->ncols++];
    c->first = first;
    c->last = last;
    c->width = width;
    c->style = style;
    c->hidden = hidden;
    return 0;
}

int owf_sheet_set_row(owf_sheet *sheet, unsigned row, int height, int hidden)
{
    int lo = 0, hi = sheet->nrows;
    owf_rowinfo *r;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sheet->rows[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < sheet->nrows && sheet->rows[lo].row == row) {
        r = &sheet->rows[lo];
    } else {
        if (!grow((void **)&sheet->rows, sheet->nrows, &sheet->caprows, sizeof *r, 16))
            return -1;
        r = &sheet->rows[lo];
        if (lo < sheet->nrows)
            memmove(r + 1, r, (size_t)(sheet->nrows - lo) * sizeof *r);
        sheet->nrows++;
        r->row = row;
    }
    r->height = height;
    r->hidden = hidden;
    return 0;
}

int owf_sheet_merge(owf_sheet *sheet, const owf_range *range)
{
    if (!grow((void **)&sheet->merges, sheet->nmerges, &sheet->capmerges, sizeof *range, 8))
        return -1;
    sheet->merges[sheet->nmerges++] = *range;
    return 0;
}

int owf_book_add_name(owf_book *book, const char *name, int sheet, const char *formula, int hidden)
{
    owf_defname *n;
    if (!grow((void **)&book->names, book->nnames, &book->capnames, sizeof *n, 8))
        return -1;
    n = &book->names[book->nnames];
    n->name = copy_text(name, strlen(name));
    n->formula = copy_text(formula, strlen(formula));
    if (!n->name || !n->formula) {
        free(n->name);
        free(n->formula);
        return -1;
    }
    n->sheet = sheet;
    n->hidden = hidden;
    book->nnames++;
    return 0;
}

/* ---- Numbers as text ---- */

/* Up to 15 significant digits, as Excel shows them, with the system's
 * doubles only (no C library: owf.library has none). */
void owf_number_text(double v, char *out)
{
    static const double powers[9] = { 1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256 };
    char digits[16];
    double x, m;
    unsigned long hi, lo;
    int shift = 0, k, nd, e, i;
    char *p = out;

    if (v != v || v > 1.7976931348623157e308 || v < -1.7976931348623157e308) {
        strcpy(out, "#NUM!");
        return;
    }
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    if (v == 0) {
        strcpy(out, "0");
        return;
    }
    /* x = v / 10^shift, from 1e14 to 1e15: its 15 digits are v's */
    x = v;
    if (x >= 1e15) {
        for (k = 8; k >= 0; k--)
            if (x / powers[k] >= 1e14) {
                x /= powers[k];
                shift += 1 << k;
            }
    } else if (x < 1e14) {
        for (k = 8; k >= 0; k--)
            if (x * powers[k] < 1e15) {
                x *= powers[k];
                shift -= 1 << k;
            }
    }
    m = x + 0.5;
    hi = (unsigned long)(m / 1e8);
    lo = (unsigned long)(m - (double)hi * 1e8);
    if (lo >= 100000000UL) {
        hi++;
        lo -= 100000000UL;
    }
    if (hi >= 10000000UL) {              /* rounding carried into a 16th digit */
        hi = 1000000UL;
        lo = 0;
        shift++;
    }
    for (i = 6; i >= 0; i--) {
        digits[i] = (char)('0' + hi % 10);
        hi /= 10;
    }
    for (i = 14; i >= 7; i--) {
        digits[i] = (char)('0' + lo % 10);
        lo /= 10;
    }
    for (nd = 15; nd > 1 && digits[nd - 1] == '0'; nd--)
        ;
    e = shift + 14;                      /* the first digit's power of ten */
    if (e >= 15 || e < -9) {
        *p++ = digits[0];
        if (nd > 1) {
            *p++ = '.';
            for (i = 1; i < nd; i++)
                *p++ = digits[i];
        }
        *p++ = 'E';
        *p++ = e < 0 ? '-' : '+';
        if (e < 0)
            e = -e;
        if (e >= 100)
            *p++ = (char)('0' + e / 100);
        *p++ = (char)('0' + e / 10 % 10);
        *p++ = (char)('0' + e % 10);
    } else if (e >= 0) {
        for (i = 0; i <= e; i++)
            *p++ = i < nd ? digits[i] : '0';
        if (nd > e + 1) {
            *p++ = '.';
            for (; i < nd; i++)
                *p++ = digits[i];
        }
    } else {
        *p++ = '0';
        *p++ = '.';
        for (i = -1; i > e; i--)
            *p++ = '0';
        for (i = 0; i < nd; i++)
            *p++ = digits[i];
    }
    *p = 0;
}

/* ---- Workbook formats ---- */

static const owf_book_format *const book_formats[] = {
    &owf_book_format_xls,
    &owf_book_format_csv,
};

#define NBOOKFORMATS ((int)(sizeof book_formats / sizeof book_formats[0]))

int owf_book_format_count(void)
{
    return NBOOKFORMATS;
}

const owf_book_format *owf_book_format_at(int index)
{
    return index >= 0 && index < NBOOKFORMATS ? book_formats[index] : NULL;
}

const owf_book_format *owf_book_format_named(const char *name)
{
    int i;
    for (i = 0; i < NBOOKFORMATS; i++)
        if (owf_same_word(book_formats[i]->name, name))
            return book_formats[i];
    return NULL;
}

const owf_book_format *owf_book_format_for_filename(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    int i;
    if (!dot || strchr(dot, '/') || strchr(dot, ':'))
        return NULL;
    for (i = 0; i < NBOOKFORMATS; i++)
        if (book_formats[i]->export && owf_ext_in_list(book_formats[i]->extensions, dot + 1))
            return book_formats[i];
    return NULL;
}

int owf_import_book_memory(const unsigned char *data, size_t length, const char *format,
                           owf_book **book_out, owf_report *report, const owf_book_format **used)
{
    const owf_book_format *f = NULL;
    owf_book *book;
    int i, best = 0, result;

    *book_out = NULL;
    if (format) {
        f = owf_book_format_named(format);
        if (!f || !f->import)
            return OWF_ERR_UNSUPPORTED;
    } else {
        for (i = 0; i < NBOOKFORMATS; i++) {
            int score;
            if (!book_formats[i]->detect || !book_formats[i]->import)
                continue;
            score = book_formats[i]->detect(data, length);
            if (score > best) {
                best = score;
                f = book_formats[i];
            }
        }
        if (!f)
            return OWF_ERR_FORMAT;
    }
    book = owf_book_new();
    if (!book)
        return OWF_ERR_MEMORY;
    result = f->import(data, length, book, report);
    if (result != OWF_OK) {
        owf_book_free(book);
        return result;
    }
    if (used)
        *used = f;
    *book_out = book;
    return OWF_OK;
}

int owf_import_book_file(const char *path, const char *format, owf_book **book,
                         owf_report *report, const owf_book_format **used)
{
    unsigned char *data;
    size_t size;
    int result;

    *book = NULL;
    result = owf_sys_read_file(path, &data, &size);
    if (result != OWF_OK)
        return result;
    result = owf_import_book_memory(data, size, format, book, report, used);
    free(data);
    return result;
}

int owf_export_book_file(const owf_book *book, const char *path, const char *format, owf_report *report)
{
    const owf_book_format *f = format ? owf_book_format_named(format) : owf_book_format_for_filename(path);
    unsigned char *data;
    size_t length;
    int result;

    if (!f)
        return format ? OWF_ERR_UNSUPPORTED : OWF_ERR_FORMAT;
    if (!f->export)
        return OWF_ERR_UNSUPPORTED;
    result = f->export(book, &data, &length, report);
    if (result != OWF_OK)
        return result;
    result = owf_sys_write_file(path, data, length);
    free(data);
    return result;
}
