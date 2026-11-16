/*
 * GFOpenCalc's portable core: Data > Consolidate. Several source ranges
 * (on any sheets) are summed up, or counted, averaged and so on, into one
 * table at a destination cell: cell by cell where they sit, or matched by
 * the labels in their first column, their first row, or both. The result is
 * values, or formulas that link to the sources (=SUM('Q1'!$B$2,'Q2'!$B$2))
 * so it follows them. The work is done on a private copy of the book and
 * swapped in as one Undo step; the settings stay on the sheet it was run from
 * in a hidden name, _GF_CONSOLIDATE, for the window to offer again, with
 * the table's size and, in _GF_CONSOLIDATE_S1.. and _GF_CONSOLIDATE_D, its
 * sources and the table's cells as references, which follow rows and columns put
 * in or taken out as any name's do: Refresh runs it again over the sources
 * as they are now, so a table grows with them. A source may be a defined
 * name of one range, as Excel's Reference box takes.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONSOLIDATE_NAME "_GF_CONSOLIDATE"
#define CONSOLIDATE_SOURCE "_GF_CONSOLIDATE_S"   /* _S1.. each source typed as a range, as a reference that follows */
#define CONSOLIDATE_DEST "_GF_CONSOLIDATE_D"     /* the table's cells, the same way */
#define CONSOLIDATE_KEYS 4096u            /* labels on one side, at most */

typedef struct {
    unsigned n, nums;
    double sum, prod, min, max, mean, m2;
} cs_bucket;

static void bucket_add(cs_bucket *b, const owf_cell *cell)
{
    double v, d;
    if (!cell || cell->type == OWF_VALUE_EMPTY)
        return;
    b->n++;
    if (cell->type != OWF_VALUE_NUMBER)
        return;
    v = cell->number;
    if (!b->nums) {
        b->min = b->max = v;
        b->prod = 1.0;
    }
    b->nums++;
    b->sum += v;
    b->prod *= v;
    if (v < b->min) b->min = v;
    if (v > b->max) b->max = v;
    d = v - b->mean;
    b->mean += d / b->nums;
    b->m2 += d * (v - b->mean);
}

/* 1 with *v set; 0 for a cell left empty (nothing to count); -1 for #DIV/0!. */
static int bucket_value(const cs_bucket *b, int function, double *v)
{
    switch (function) {
    case OC_SUBTOTAL_SUM: if (!b->n) return 0; *v = b->sum; return 1;
    case OC_SUBTOTAL_COUNT: if (!b->n) return 0; *v = b->nums; return 1;
    case OC_SUBTOTAL_COUNTA: if (!b->n) return 0; *v = b->n; return 1;
    case OC_SUBTOTAL_AVERAGE: if (!b->n) return 0; if (!b->nums) return -1; *v = b->mean; return 1;
    case OC_SUBTOTAL_MAX: if (!b->n) return 0; *v = b->nums ? b->max : 0.0; return 1;
    case OC_SUBTOTAL_MIN: if (!b->n) return 0; *v = b->nums ? b->min : 0.0; return 1;
    case OC_SUBTOTAL_PRODUCT: if (!b->n) return 0; *v = b->nums ? b->prod : 0.0; return 1;
    case OC_SUBTOTAL_STDEV: case OC_SUBTOTAL_VAR:
        if (!b->n) return 0;
        if (b->nums < 2) return -1;
        *v = b->m2 / (b->nums - 1);
        if (function == OC_SUBTOTAL_STDEV) *v = sqrt(*v);
        return 1;
    case OC_SUBTOTAL_STDEVP: case OC_SUBTOTAL_VARP:
        if (!b->n) return 0;
        if (!b->nums) return -1;
        *v = b->m2 / b->nums;
        if (function == OC_SUBTOTAL_STDEVP) *v = sqrt(*v);
        return 1;
    }
    return 0;
}

static const char *function_word(int function)
{
    static const char *const words[] = { "", "AVERAGE", "COUNT", "COUNTA", "MAX", "MIN", "PRODUCT",
                                         "STDEV", "STDEVP", "SUM", "VAR", "VARP" };
    return function >= OC_SUBTOTAL_AVERAGE && function <= OC_SUBTOTAL_VARP ? words[function] : NULL;
}

/* A label's text: a cell's shown value, without case for matching. */
typedef struct {
    char **text;                 /* as shown, first seen */
    unsigned n, cap;
} cs_keys;

static void label_of(const oc_core *core, int sheet, unsigned row, unsigned col, char *out, size_t size)
{
    oc_core_cell_text(core, sheet, row, col, out, size);   /* "" for an empty cell */
}

static int same_label(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return !*a && !*b;
}

/* The label's index, added when new; -1 for memory or too many. */
static int key_index(cs_keys *k, const char *label)
{
    unsigned i;
    char *copy;
    for (i = 0; i < k->n; i++)
        if (same_label(k->text[i], label))
            return (int)i;
    if (k->n >= CONSOLIDATE_KEYS)
        return -1;
    if (k->n == k->cap) {
        unsigned cap = k->cap ? k->cap * 2 : 32;
        char **more = realloc(k->text, cap * sizeof *more);
        if (!more)
            return -1;
        k->text = more;
        k->cap = cap;
    }
    copy = oc_strdup(label);
    if (!copy)
        return -1;
    k->text[k->n] = copy;
    return (int)k->n++;
}

static void keys_free(cs_keys *k)
{
    unsigned i;
    for (i = 0; i < k->n; i++)
        owf_free(k->text[i]);
    free(k->text);
}

typedef struct {
    int sheet;
    owf_range r;
} cs_source;

/* An absolute reference with its sheet: 'Q1'!$B$2 (or a range). */
static int ref_text(const owf_book *book, int sheet, unsigned r0, unsigned c0, unsigned r1, unsigned c1,
                    char *out, size_t size)
{
    char a[8], b[8];
    const char *name = book->sheets[sheet].name;
    size_t at = 0, i;
    int n;
    if (size < 8)
        return 0;
    out[at++] = '\'';
    for (i = 0; name[i]; i++) {
        if (at + 4 >= size)
            return 0;
        if (name[i] == '\'')
            out[at++] = '\'';
        out[at++] = name[i];
    }
    out[at++] = '\'';
    out[at++] = '!';
    oc_col_name(c0, a);
    oc_col_name(c1, b);
    if (r0 == r1 && c0 == c1)
        n = snprintf(out + at, size - at, "$%s$%u", a, r0 + 1u);
    else
        n = snprintf(out + at, size - at, "$%s$%u:$%s$%u", a, r0 + 1u, b, r1 + 1u);
    return n > 0 && (size_t)n < size - at;
}

/* The sheet's hidden name: set to formula (taken), or added. */
static int set_hidden(owf_book *book, int sheet, const char *name, char *formula)
{
    int k;
    if (!formula)
        return OWF_ERR_MEMORY;
    for (k = 0; k < book->nnames; k++)
        if (book->names[k].sheet == sheet && book->names[k].name && !strcmp(book->names[k].name, name)) {
            owf_free(book->names[k].formula);
            book->names[k].formula = formula;
            book->names[k].hidden = 1;
            return OWF_OK;
        }
    k = owf_book_add_name(book, name, sheet, formula, 1);
    owf_free(formula);
    return k < 0 ? OWF_ERR_MEMORY : OWF_OK;
}

static void drop_hidden(owf_book *book, int sheet, const char *name)
{
    int k;
    for (k = 0; k < book->nnames; k++)
        if (book->names[k].sheet == sheet && book->names[k].name && !strcmp(book->names[k].name, name)) {
            owf_free(book->names[k].name);
            owf_free(book->names[k].formula);
            memmove(&book->names[k], &book->names[k + 1], (size_t)(book->nnames - k - 1) * sizeof book->names[0]);
            book->nnames--;
            return;
        }
}

static const char *hidden_formula(const owf_book *book, int sheet, const char *name)
{
    int k;
    for (k = 0; k < book->nnames; k++)
        if (book->names[k].sheet == sheet && book->names[k].name && !strcmp(book->names[k].name, name))
            return book->names[k].formula;
    return NULL;
}

/* The settings; with each source typed as a range, and the destination, as references (ref: the ranges
 * read, dest_sheet/row/col the destination) and the table's size. */
static int put_settings(owf_book *book, int sheet, const oc_consolidate_spec *spec, const cs_source *src,
                        int dest_sheet, unsigned dest_row, unsigned dest_col, unsigned out_rows, unsigned out_cols)
{
    char text[OC_CONSOLIDATE_SOURCES * (OC_CONSOLIDATE_REF + 1) + OC_CONSOLIDATE_REF + 96], name[40],
         ref[OC_CONSOLIDATE_REF + 8];
    size_t at;
    unsigned i;
    int k, rc;
    at = (size_t)snprintf(text, sizeof text, "\"1;f=%d;l=%d;k=%d;o=%u,%u;d=", spec->function,
                          (spec->row_labels ? 1 : 0) | (spec->column_labels ? 2 : 0), spec->link ? 1 : 0, out_rows,
                          out_cols);
    for (i = 0; spec->dest[i] && at + 4 < sizeof text; i++)
        if (spec->dest[i] != '"' && spec->dest[i] != ';')
            text[at++] = spec->dest[i];
    for (k = 0; k < spec->nsources; k++) {
        if (at + 4 >= sizeof text)
            break;
        memcpy(text + at, ";s=", 3);
        at += 3;
        for (i = 0; spec->source[k][i] && at + 2 < sizeof text; i++)
            if (spec->source[k][i] != '"' && spec->source[k][i] != ';')
                text[at++] = spec->source[k][i];
    }
    text[at++] = '"';
    text[at] = 0;
    if ((rc = set_hidden(book, sheet, CONSOLIDATE_NAME, oc_strdup(text))) != OWF_OK)
        return rc;
    for (k = 0; k < OC_CONSOLIDATE_SOURCES; k++) {
        int s2;
        owf_range r;
        snprintf(name, sizeof name, CONSOLIDATE_SOURCE "%d", k + 1);
        /* a source typed as a range follows as a reference; one typed otherwise (a name) is read afresh */
        if (k < spec->nsources && oc_sheet_range(book, spec->source[k], sheet, &s2, &r)) {
            if (!ref_text(book, src[k].sheet, src[k].r.first_row, src[k].r.first_col, src[k].r.last_row,
                          src[k].r.last_col, ref, sizeof ref))
                return OWF_ERR_UNSUPPORTED;
            if ((rc = set_hidden(book, sheet, name, oc_strdup(ref))) != OWF_OK)
                return rc;
        } else {
            drop_hidden(book, sheet, name);
        }
    }
    /* the whole table, so that a row or column put in inside it is emptied on Refresh too */
    if (!ref_text(book, dest_sheet, dest_row, dest_col, dest_row + (out_rows ? out_rows - 1u : 0u),
                  dest_col + (out_cols ? out_cols - 1u : 0u), ref, sizeof ref))
        return OWF_ERR_UNSUPPORTED;
    return set_hidden(book, sheet, CONSOLIDATE_DEST, oc_strdup(ref));
}

/* A reference kept in a hidden name, as one types it: "B2:D9" on the sheet the settings are on, else
 * "'Q1'!A1:C20" (first: its first cell only); its range into *range when given. 0 when there is none or it
 * no longer reads (its cells were taken out). */
static int followed(const oc_core *core, const char *name, int first, char *out, size_t size, owf_range *range)
{
    const char *f = hidden_formula(core->book, core->sheet, name);
    char a[16], b[16], quoted[OC_CONSOLIDATE_REF];
    const char *sheet_name;
    owf_range r;
    int s2, n;
    size_t q = 0, i;
    if (!f || !oc_sheet_range(core->book, f, core->sheet, &s2, &r) || s2 < 0 || s2 >= core->book->nsheets)
        return 0;
    if (range)
        *range = r;
    if (first) {
        r.last_row = r.first_row;
        r.last_col = r.first_col;
    }
    oc_cell_name(r.first_row, r.first_col, a);
    oc_cell_name(r.last_row, r.last_col, b);
    quoted[0] = 0;
    if (s2 != core->sheet) {
        sheet_name = core->book->sheets[s2].name;
        quoted[q++] = '\'';
        for (i = 0; sheet_name[i] && q + 4 < sizeof quoted; i++) {
            if (sheet_name[i] == '\'')
                quoted[q++] = '\'';
            quoted[q++] = sheet_name[i];
        }
        quoted[q++] = '\'';
        quoted[q++] = '!';
        quoted[q] = 0;
    }
    if (r.first_row == r.last_row && r.first_col == r.last_col)
        n = snprintf(out, size, "%s%s", quoted, a);
    else
        n = snprintf(out, size, "%s%s:%s", quoted, a, b);
    return n > 0 && (size_t)n < size;
}

/* A defined name the active sheet sees: its own first, else the workbook's (case aside); NULL when none. */
static const owf_defname *seen_name(const oc_core *core, const char *name)
{
    int i, pass;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < core->book->nnames; i++) {
            const owf_defname *d = &core->book->names[i];
            if (d->name && !d->hidden && d->sheet == (pass ? -1 : core->sheet) && same_label(d->name, name))
                return d;
        }
    return NULL;
}

/* A name's one range: 1 with its sheet and range. */
static int name_range(const oc_core *core, const owf_defname *d, int *sheet, owf_range *r)
{
    return d && d->formula && oc_sheet_range(core->book, d->formula, d->sheet >= 0 ? d->sheet : core->sheet, sheet, r);
}

/* A source as typed: a range ("B2:D9", "'Q1'!A1:C20") or a defined name of one range. */
static int source_range(const oc_core *core, const char *text, int *sheet, owf_range *r)
{
    while (*text == ' ')
        text++;
    if (oc_sheet_range(core->book, text, core->sheet, sheet, r))
        return 1;
    return name_range(core, seen_name(core, text), sheet, r);
}

int oc_core_consolidate_names(const oc_core *core, int *index, int max)
{
    int i, n = 0, sheet;
    owf_range r;
    if (!core || !index)
        return 0;
    for (i = 0; i < core->book->nnames && n < max; i++) {
        const owf_defname *d = &core->book->names[i];
        if (!d->name || d->hidden || (d->sheet != -1 && d->sheet != core->sheet))
            continue;
        if (seen_name(core, d->name) != d || !name_range(core, d, &sheet, &r))
            continue;                    /* a workbook name the sheet's own hides, or not one range */
        index[n++] = i;
    }
    return n;
}

void oc_consolidate_init(oc_consolidate_spec *spec)
{
    memset(spec, 0, sizeof *spec);
    spec->function = OC_SUBTOTAL_SUM;
}

/* The settings, with the sources and destination where they are now, and the table's size when kept. */
static int read_settings(const oc_core *core, oc_consolidate_spec *spec, unsigned *out_rows, unsigned *out_cols)
{
    const char *p = NULL;
    int k;
    *out_rows = *out_cols = 0;
    if (!core || !spec)
        return 0;
    oc_consolidate_init(spec);
    for (k = 0; k < core->book->nnames; k++)
        if (core->book->names[k].sheet == core->sheet && !strcmp(core->book->names[k].name, CONSOLIDATE_NAME))
            p = core->book->names[k].formula;
    if (!p)
        return 0;
    while (*p == '=' || *p == ' ')
        p++;
    if (*p++ != '"' || *p != '1')
        return 0;
    while (*p && *p != '"') {
        char *out = NULL;
        size_t size = 0, n = 0;
        if (*p == ';') { p++; continue; }
        if (!strncmp(p, "f=", 2)) spec->function = atoi(p + 2);
        else if (!strncmp(p, "l=", 2)) { spec->row_labels = (atoi(p + 2) & 1) != 0; spec->column_labels = (atoi(p + 2) & 2) != 0; }
        else if (!strncmp(p, "k=", 2)) spec->link = atoi(p + 2) != 0;
        else if (!strncmp(p, "o=", 2)) {
            const char *comma = strchr(p, ',');
            *out_rows = (unsigned)strtoul(p + 2, NULL, 10);
            *out_cols = comma ? (unsigned)strtoul(comma + 1, NULL, 10) : 0u;
        }
        else if (!strncmp(p, "d=", 2)) { out = spec->dest; size = sizeof spec->dest; }
        else if (!strncmp(p, "s=", 2) && spec->nsources < OC_CONSOLIDATE_SOURCES) {
            out = spec->source[spec->nsources++];
            size = sizeof spec->source[0];
        }
        p += 2;
        while (*p && *p != ';' && *p != '"') {
            if (out && n + 1 < size)
                out[n++] = *p;
            p++;
        }
        if (out)
            out[n] = 0;
    }
    /* the ranges as they are now: rows and columns put in or taken out since */
    for (k = 0; k < spec->nsources; k++) {
        char name[40], now[OC_CONSOLIDATE_REF];
        snprintf(name, sizeof name, CONSOLIDATE_SOURCE "%d", k + 1);
        if (followed(core, name, 0, now, sizeof now, NULL))
            memcpy(spec->source[k], now, sizeof now);
    }
    {
        char now[OC_CONSOLIDATE_REF];
        owf_range table;
        if (followed(core, CONSOLIDATE_DEST, 1, now, sizeof now, &table)) {
            memcpy(spec->dest, now, sizeof now);
            if (*out_rows && *out_cols) {            /* the table as it is now: rows put in inside it count */
                *out_rows = table.last_row - table.first_row + 1u;
                *out_cols = table.last_col - table.first_col + 1u;
            }
        }
    }
    return spec->nsources > 0;
}

int oc_core_consolidate_settings(const oc_core *core, oc_consolidate_spec *spec)
{
    unsigned rows, cols;
    return read_settings(core, spec, &rows, &cols);
}

/* With clear_rows x clear_cols cells at the destination emptied first (the table as it was, for Refresh). */
static int consolidate(oc_core *core, const oc_consolidate_spec *spec, unsigned clear_rows, unsigned clear_cols)
{
    cs_source src[OC_CONSOLIDATE_SOURCES];
    cs_keys rows, cols;
    cs_bucket *grid = NULL;
    unsigned nrows = 0, ncols = 0, r, c, dest_row, dest_col, lr, lc, out_rows, out_cols;
    int dest_sheet, k, rc = OWF_ERR_FORMAT;
    owf_range dr;
    owf_book *work = NULL, *old_book;
    owf_calc *calc = NULL, *old_calc;
    oc_undo *undo;
    char label[256], text[OC_CONSOLIDATE_SOURCES * 48 + 32];

    if (!core || !spec || spec->nsources < 1 || spec->nsources > OC_CONSOLIDATE_SOURCES || !function_word(spec->function))
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!oc_sheet_range(core->book, spec->dest, core->sheet, &dest_sheet, &dr))
        return OWF_ERR_FORMAT;
    dest_row = dr.first_row;
    dest_col = dr.first_col;
    memset(&rows, 0, sizeof rows);
    memset(&cols, 0, sizeof cols);
    lr = spec->column_labels ? 1u : 0u;          /* the sources' first row holds labels */
    lc = spec->row_labels ? 1u : 0u;             /* their first column */
    for (k = 0; k < spec->nsources; k++) {
        unsigned h, w;
        if (!source_range(core, spec->source[k], &src[k].sheet, &src[k].r))
            return OWF_ERR_FORMAT;
        h = src[k].r.last_row - src[k].r.first_row + 1u;
        w = src[k].r.last_col - src[k].r.first_col + 1u;
        if (h <= lr || w <= lc)
            return OWF_ERR_FORMAT;
        if ((unsigned long)h * w > OC_MAX_RANGE_CELLS)
            return OWF_ERR_UNSUPPORTED;
        /* the destination may not be inside a source: the table would read itself */
        if (src[k].sheet == dest_sheet && dest_row >= src[k].r.first_row && dest_row <= src[k].r.last_row &&
            dest_col >= src[k].r.first_col && dest_col <= src[k].r.last_col)
            return OWF_ERR_FORMAT;
        if (h - lr > nrows) nrows = h - lr;
        if (w - lc > ncols) ncols = w - lc;
    }
    /* the labels, in the order they come */
    rc = OWF_ERR_MEMORY;
    for (k = 0; k < spec->nsources; k++) {
        const owf_range *g = &src[k].r;
        if (lc)
            for (r = g->first_row + lr; r <= g->last_row; r++) {
                label_of(core, src[k].sheet, r, g->first_col, label, sizeof label);
                if (key_index(&rows, label) < 0) goto done;
            }
        if (lr)
            for (c = g->first_col + lc; c <= g->last_col; c++) {
                label_of(core, src[k].sheet, g->first_row, c, label, sizeof label);
                if (key_index(&cols, label) < 0) goto done;
            }
    }
    if (lc) nrows = rows.n;
    if (lr) ncols = cols.n;
    out_rows = nrows + lr;
    out_cols = ncols + lc;
    if (dest_row + out_rows > OC_MAX_ROWS || dest_col + out_cols > OC_MAX_COLS) {
        rc = OWF_ERR_FORMAT;
        goto done;
    }
    if ((unsigned long)nrows * ncols > OC_MAX_RANGE_CELLS) {
        rc = OWF_ERR_UNSUPPORTED;
        goto done;
    }
    grid = calloc(nrows && ncols ? (size_t)nrows * ncols : 1u, sizeof *grid);
    if (!grid)
        goto done;
    work = oc_book_copy(core->book);
    if (!work || !(calc = owf_calc_new(work)))
        goto done;
    /* the table as it was, emptied: a smaller one leaves nothing of it behind */
    if ((unsigned long)clear_rows * clear_cols <= OC_MAX_RANGE_CELLS)
        for (r = 0; r < clear_rows && dest_row + r < OC_MAX_ROWS; r++)
            for (c = 0; c < clear_cols && dest_col + c < OC_MAX_COLS; c++)
                if (owf_sheet_find(&work->sheets[dest_sheet], dest_row + r, dest_col + c) &&
                    owf_calc_enter(calc, dest_sheet, dest_row + r, dest_col + c, "") != OWF_OK)
                    goto done;
    /* each source cell into its bucket */
    for (k = 0; k < spec->nsources; k++) {
        const owf_range *g = &src[k].r;
        for (r = g->first_row + lr; r <= g->last_row; r++) {
            unsigned at_r = r - g->first_row - lr;
            if (lc) {
                label_of(core, src[k].sheet, r, g->first_col, label, sizeof label);
                at_r = (unsigned)key_index(&rows, label);
            }
            for (c = g->first_col + lc; c <= g->last_col; c++) {
                unsigned at_c = c - g->first_col - lc;
                if (lr) {
                    label_of(core, src[k].sheet, g->first_row, c, label, sizeof label);
                    at_c = (unsigned)key_index(&cols, label);
                }
                bucket_add(&grid[(size_t)at_r * ncols + at_c],
                           owf_sheet_find(&core->book->sheets[src[k].sheet], r, c));
            }
        }
    }
    /* the table */
    rc = OWF_OK;
    for (r = 0; r < nrows && rc == OWF_OK; r++) {
        if (lc) {
            snprintf(text, sizeof text, "'%s", rows.text[r]);
            rc = owf_calc_enter(calc, dest_sheet, dest_row + lr + r, dest_col, rows.text[r][0] ? text : "");
        }
        for (c = 0; c < ncols && rc == OWF_OK; c++) {
            const cs_bucket *b = &grid[(size_t)r * ncols + c];
            double v;
            int got = bucket_value(b, spec->function, &v);
            unsigned orow = dest_row + lr + r, ocol = dest_col + lc + c;
            if (spec->link && b->n) {
                /* =FN(each source's cell for this place) */
                size_t at = (size_t)snprintf(text, sizeof text, "=%s(", function_word(spec->function));
                int any = 0;
                for (k = 0; k < spec->nsources; k++) {
                    const owf_range *g = &src[k].r;
                    unsigned sr, sc;
                    int found = 0;
                    char one[OC_CONSOLIDATE_REF];
                    for (sr = g->first_row + lr; sr <= g->last_row && !found; sr++) {
                        if (lc) {
                            label_of(core, src[k].sheet, sr, g->first_col, label, sizeof label);
                            if (!same_label(label, rows.text[r])) continue;
                        } else if (sr - g->first_row - lr != r) {
                            continue;
                        }
                        for (sc = g->first_col + lc; sc <= g->last_col && !found; sc++) {
                            if (lr) {
                                label_of(core, src[k].sheet, g->first_row, sc, label, sizeof label);
                                if (!same_label(label, cols.text[c])) continue;
                            } else if (sc - g->first_col - lc != c) {
                                continue;
                            }
                            found = 1;
                            if (!ref_text(core->book, src[k].sheet, sr, sc, sr, sc, one, sizeof one) ||
                                at + strlen(one) + 3 >= sizeof text) {
                                rc = OWF_ERR_UNSUPPORTED;
                                break;
                            }
                            at += (size_t)snprintf(text + at, sizeof text - at, "%s%s", any++ ? "," : "", one);
                        }
                    }
                }
                if (rc != OWF_OK)
                    break;
                snprintf(text + at, sizeof text - at, ")");
                rc = owf_calc_enter(calc, dest_sheet, orow, ocol, text);
            } else if (got > 0) {
                owf_number_text(v, text);
                rc = owf_calc_enter(calc, dest_sheet, orow, ocol, text);
            } else {
                rc = owf_calc_enter(calc, dest_sheet, orow, ocol, got < 0 ? "=1/0" : "");
            }
        }
    }
    for (c = 0; lr && c < ncols && rc == OWF_OK; c++) {
        snprintf(text, sizeof text, "'%s", cols.text[c]);
        rc = owf_calc_enter(calc, dest_sheet, dest_row, dest_col + lc + c, cols.text[c][0] ? text : "");
    }
    if (rc == OWF_OK)
        rc = put_settings(work, core->sheet, spec, src, dest_sheet, dest_row, dest_col, out_rows, out_cols);
    if (rc == OWF_OK)
        rc = owf_calc_recalc(calc);
    if (rc != OWF_OK)
        goto done;
    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    oc_cf_forget(core);
    oc_charts_forget(core);
    old_book = core->book;
    old_calc = core->calc;
    core->book = work;
    core->calc = calc;
    work = NULL;
    calc = NULL;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    if (dest_sheet == core->sheet) {
        owf_range sel;
        sel.first_row = dest_row;
        sel.first_col = dest_col;
        sel.last_row = dest_row + (out_rows ? out_rows - 1u : 0u);
        sel.last_col = dest_col + (out_cols ? out_cols - 1u : 0u);
        oc_core_select_range(core, &sel);
    }
done:
    owf_calc_free(calc);
    owf_book_free(work);
    free(grid);
    keys_free(&rows);
    keys_free(&cols);
    return rc;
}

int oc_core_consolidate(oc_core *core, const oc_consolidate_spec *spec)
{
    return consolidate(core, spec, 0, 0);
}

int oc_core_consolidate_refresh(oc_core *core)
{
    oc_consolidate_spec spec;
    unsigned rows, cols;
    if (!core)
        return OWF_ERR_FORMAT;
    if (!read_settings(core, &spec, &rows, &cols))
        return OWF_ERR_UNSUPPORTED;
    return consolidate(core, &spec, rows, cols);
}
