/*
 * GFOpenCalc's portable core: pivot tables (DataPilot). A definition
 * (owf_pivot.h) names row, column, data and page fields of a headed source
 * table; the table is made of ordinary cells on its output sheet, so any
 * reader sees the result, and the definition rides along in two hidden names
 * (and in ODS's data-pilot-table and XLSX's pivot table, through libowf).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"
#include "owf_pivot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIVOT_MAX_BUCKETS 65536u        /* output value cells, data fields counted */

typedef struct {
    const owf_cell *sample;
    char label[96];
} pivot_category;

typedef struct {
    unsigned rows, nonempty, numbers;
    double sum, product, min, max, mean, m2;
} pivot_bucket;

static int same_value(const owf_cell *a, const owf_cell *b)
{
    owf_value_type at = a ? a->type : OWF_VALUE_EMPTY;
    owf_value_type bt = b ? b->type : OWF_VALUE_EMPTY;
    if (at != bt)
        return 0;
    switch (at) {
    case OWF_VALUE_NUMBER:
    case OWF_VALUE_BOOL: return a->number == b->number;
    case OWF_VALUE_TEXT: return !strcmp(a->text ? a->text : "", b->text ? b->text : "");
    case OWF_VALUE_ERROR: return a->error == b->error;
    default: return 1;
    }
}

static void category_label(const owf_book *book, const owf_cell *cell, char *out, size_t size)
{
    if (!cell || cell->type == OWF_VALUE_EMPTY)
        snprintf(out, size, "(blank)");
    else
        owf_calc_format(book, cell, out, size);
}

static void bucket_add(pivot_bucket *bucket, const owf_cell *cell)
{
    double delta, value;
    bucket->rows++;
    if (cell && cell->type != OWF_VALUE_EMPTY)
        bucket->nonempty++;
    if (!cell || (cell->type != OWF_VALUE_NUMBER && cell->type != OWF_VALUE_BOOL))
        return;
    value = cell->number;
    if (!bucket->numbers) {
        bucket->product = value;
        bucket->min = bucket->max = value;
        bucket->mean = value;
        bucket->m2 = 0.0;
    } else {
        bucket->product *= value;
        if (value < bucket->min) bucket->min = value;
        if (value > bucket->max) bucket->max = value;
        delta = value - bucket->mean;
        bucket->mean += delta / (double)(bucket->numbers + 1u);
        bucket->m2 += delta * (value - bucket->mean);
    }
    bucket->sum += value;
    bucket->numbers++;
}

static int bucket_value(const pivot_bucket *bucket, int function, double *value)
{
    if (!bucket->rows)
        return 0;
    switch (function) {
    case OC_SUBTOTAL_AVERAGE:
        if (!bucket->numbers) return 0;
        *value = bucket->mean;
        break;
    case OC_SUBTOTAL_COUNT: *value = (double)bucket->numbers; break;
    case OC_SUBTOTAL_COUNTA: *value = (double)bucket->nonempty; break;
    case OC_SUBTOTAL_MAX:
        if (!bucket->numbers) return 0;
        *value = bucket->max;
        break;
    case OC_SUBTOTAL_MIN:
        if (!bucket->numbers) return 0;
        *value = bucket->min;
        break;
    case OC_SUBTOTAL_PRODUCT:
        if (!bucket->numbers) return 0;
        *value = bucket->product;
        break;
    case OC_SUBTOTAL_STDEV:
        if (bucket->numbers < 2) return 0;
        *value = sqrt(bucket->m2 / (double)(bucket->numbers - 1u));
        break;
    case OC_SUBTOTAL_STDEVP:
        if (!bucket->numbers) return 0;
        *value = sqrt(bucket->m2 / (double)bucket->numbers);
        break;
    case OC_SUBTOTAL_SUM: *value = bucket->sum; break;
    case OC_SUBTOTAL_VAR:
        if (bucket->numbers < 2) return 0;
        *value = bucket->m2 / (double)(bucket->numbers - 1u);
        break;
    case OC_SUBTOTAL_VARP:
        if (!bucket->numbers) return 0;
        *value = bucket->m2 / (double)bucket->numbers;
        break;
    default: return 0;
    }
    return 1;
}

static const char *function_name(int function)
{
    static const char *const names[] = {
        "", "Average", "Count", "CountA", "Max", "Min", "Product",
        "StdDev", "StdDevP", "Sum", "Var", "VarP"
    };
    return function >= OC_SUBTOTAL_AVERAGE && function <= OC_SUBTOTAL_VARP ? names[function] : "Value";
}

static int put_text(owf_calc *calc, int sheet, unsigned row, unsigned col, const char *text)
{
    char input[256];
    snprintf(input, sizeof input, "'%s", text ? text : "");
    return owf_calc_enter(calc, sheet, row, col, input);
}

static int put_number(owf_calc *calc, int sheet, unsigned row, unsigned col, double value)
{
    char input[48];
    owf_number_text(value, input);
    return owf_calc_enter(calc, sheet, row, col, input);
}

static int bold_cell(owf_book *book, owf_sheet *sheet, unsigned row, unsigned col)
{
    owf_cell *cell = owf_sheet_cell(sheet, row, col, 1);
    owf_cellstyle style;
    int index;
    if (!cell)
        return OWF_ERR_MEMORY;
    index = cell->style >= 0 && cell->style < book->nstyles ? cell->style : 0;
    style = book->styles[index];
    style.font.flags |= OWF_BOLD;
    index = owf_book_style(book, &style);
    if (index < 0)
        return OWF_ERR_MEMORY;
    cell = owf_sheet_cell(sheet, row, col, 1);
    if (!cell)
        return OWF_ERR_MEMORY;
    cell->style = index;
    return OWF_OK;
}

static void pivot_name(const owf_book *book, char *out, size_t size)
{
    int suffix = 1, found, i;
    do {
        found = 0;
        if (suffix == 1)
            snprintf(out, size, "Pivot");
        else
            snprintf(out, size, "Pivot%d", suffix);
        for (i = 0; i < book->nsheets; ++i)
            if (!strcmp(book->sheets[i].name, out)) {
                found = 1;
                suffix++;
                break;
            }
    } while (found);
}

/* ---- Distinct values and keys, hashed ---- */

static unsigned hash_bytes(unsigned h, const void *data, size_t n)
{
    const unsigned char *p = data;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}

static unsigned value_hash(const owf_cell *cell)
{
    owf_value_type type = cell ? cell->type : OWF_VALUE_EMPTY;
    unsigned h = 2166136261u ^ (unsigned)type;
    double d;
    switch (type) {
    case OWF_VALUE_NUMBER:
    case OWF_VALUE_BOOL:
        d = cell->number == 0.0 ? 0.0 : cell->number;   /* -0 is 0 */
        return hash_bytes(h, &d, sizeof d);
    case OWF_VALUE_TEXT:
        return cell->text ? hash_bytes(h, cell->text, strlen(cell->text)) : h;
    case OWF_VALUE_ERROR:
        return hash_bytes(h, &cell->error, sizeof cell->error);
    default:
        return h;
    }
}

/* One field's distinct values, in the order they first come. */
typedef struct {
    const owf_cell **sample;
    unsigned *hash;
    unsigned n, cap;
    unsigned *slots;             /* index + 1; 0 empty */
    unsigned nslots;
} pivot_values;

/* Distinct tuples of value indexes (a row's or a column's key). */
typedef struct {
    unsigned width;
    unsigned *keys;              /* width per key */
    unsigned *hash;
    unsigned n, cap;
    unsigned *slots;
    unsigned nslots;
} pivot_keys;

static int rehash(unsigned **slots, unsigned *nslots, const unsigned *hash, unsigned n)
{
    unsigned want = *nslots ? *nslots * 2u : 64u, i, *p;
    p = calloc(want, sizeof *p);
    if (!p)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned at = hash[i] & (want - 1u);
        while (p[at])
            at = (at + 1u) & (want - 1u);
        p[at] = i + 1u;
    }
    free(*slots);
    *slots = p;
    *nslots = want;
    return 1;
}

static int values_index(pivot_values *v, const owf_cell *cell)
{
    unsigned h = value_hash(cell), at;
    if (v->n * 2u >= v->nslots && !rehash(&v->slots, &v->nslots, v->hash, v->n))
        return -1;
    for (at = h & (v->nslots - 1u); v->slots[at]; at = (at + 1u) & (v->nslots - 1u)) {
        unsigned i = v->slots[at] - 1u;
        if (v->hash[i] == h && same_value(v->sample[i], cell))
            return (int)i;
    }
    if (v->n == v->cap) {
        unsigned cap = v->cap ? v->cap * 2u : 16u;
        const owf_cell **s = realloc(v->sample, (size_t)cap * sizeof *s);
        unsigned *hh;
        if (!s)
            return -1;
        v->sample = s;
        hh = realloc(v->hash, (size_t)cap * sizeof *hh);
        if (!hh)
            return -1;
        v->hash = hh;
        v->cap = cap;
    }
    v->sample[v->n] = cell;
    v->hash[v->n] = h;
    v->slots[at] = ++v->n;
    return (int)(v->n - 1u);
}

static int keys_index(pivot_keys *k, const unsigned *key)
{
    unsigned h = k->width ? hash_bytes(2166136261u, key, (size_t)k->width * sizeof *key) : 0u, at;
    if (k->n * 2u >= k->nslots && !rehash(&k->slots, &k->nslots, k->hash, k->n))
        return -1;
    for (at = h & (k->nslots - 1u); k->slots[at]; at = (at + 1u) & (k->nslots - 1u)) {
        unsigned i = k->slots[at] - 1u;
        if (k->hash[i] == h &&
            (!k->width || !memcmp(&k->keys[(size_t)i * k->width], key, (size_t)k->width * sizeof *key)))
            return (int)i;
    }
    if (k->n == k->cap) {
        unsigned cap = k->cap ? k->cap * 2u : 16u, *p;
        p = realloc(k->keys, (size_t)cap * (k->width ? k->width : 1u) * sizeof *p);
        if (!p)
            return -1;
        k->keys = p;
        p = realloc(k->hash, (size_t)cap * sizeof *p);
        if (!p)
            return -1;
        k->hash = p;
        k->cap = cap;
    }
    if (k->width)
        memcpy(&k->keys[(size_t)k->n * k->width], key, (size_t)k->width * sizeof *key);
    k->hash[k->n] = h;
    k->slots[at] = ++k->n;
    return (int)(k->n - 1u);
}

static void values_free(pivot_values *v)
{
    free(v->sample);
    free(v->hash);
    free(v->slots);
}

static void keys_free(pivot_keys *k)
{
    free(k->keys);
    free(k->hash);
    free(k->slots);
}

/* Keys in order: each field's values in their first order, outer fields first, so a group's
 * rows are together. A plain insertion-merge sort (no qsort context). */
static void sort_keys(const pivot_keys *k, unsigned *order, unsigned *tmp)
{
    unsigned width, n = k->n, i, lo, mid, hi, a, b, t;
    for (i = 0; i < n; i++)
        order[i] = i;
    for (width = 1; width < n; width *= 2u) {
        for (lo = 0; lo < n; lo += 2u * width) {
            mid = lo + width < n ? lo + width : n;
            hi = lo + 2u * width < n ? lo + 2u * width : n;
            a = lo; b = mid; t = lo;
            while (a < mid || b < hi) {
                int take_a;
                if (a >= mid) take_a = 0;
                else if (b >= hi) take_a = 1;
                else {
                    const unsigned *ka = &k->keys[(size_t)order[a] * k->width];
                    const unsigned *kb = &k->keys[(size_t)order[b] * k->width];
                    unsigned f;
                    take_a = 1;
                    for (f = 0; f < k->width; f++)
                        if (ka[f] != kb[f]) {
                            take_a = ka[f] < kb[f];
                            break;
                        }
                }
                tmp[t++] = take_a ? order[a++] : order[b++];
            }
        }
        memcpy(order, tmp, (size_t)n * sizeof *order);
    }
}

/* ---- Making the table ---- */

typedef struct {
    owf_book *work;
    owf_calc *calc;
    int sheet;
    int rc;
} pivot_out;

static void out_text(pivot_out *o, unsigned row, unsigned col, const char *text)
{
    if (o->rc == OWF_OK && row < OC_MAX_ROWS && col < OC_MAX_COLS)
        o->rc = put_text(o->calc, o->sheet, row, col, text);
}

static void out_number(pivot_out *o, unsigned row, unsigned col, double value, int style)
{
    owf_cell *cell;
    if (o->rc != OWF_OK || row >= OC_MAX_ROWS || col >= OC_MAX_COLS)
        return;
    o->rc = put_number(o->calc, o->sheet, row, col, value);
    if (o->rc == OWF_OK && (cell = owf_sheet_cell(&o->work->sheets[o->sheet], row, col, 0)) != NULL)
        cell->style = style;
}

static void out_bold(pivot_out *o, unsigned row, unsigned col)
{
    if (o->rc == OWF_OK && row < OC_MAX_ROWS && col < OC_MAX_COLS)
        o->rc = bold_cell(o->work, &o->work->sheets[o->sheet], row, col);
}

static void out_bucket(pivot_out *o, unsigned row, unsigned col, const pivot_bucket *bucket, int function,
                       int style)
{
    double value;
    if (bucket_value(bucket, function, &value))
        out_number(o, row, col, value, style);
}

/* Empties the old output: its rectangle when known, else the whole sheet. */
static void clear_output(owf_calc *calc, owf_book *book, int sheet_index, const owf_pivotdef *old)
{
    owf_sheet *sheet = &book->sheets[sheet_index];
    int i, k;
    for (i = 0; i < sheet->ncells; ++i) {
        unsigned row = sheet->cells[i].row, col = sheet->cells[i].col;
        if (old && old->rows && old->cols &&
            (row < old->at_row || row >= old->at_row + old->rows ||
             col < old->at_col || col >= old->at_col + old->cols))
            continue;
        owf_calc_enter(calc, sheet_index, row, col, "");
        sheet->cells[i].style = 0;
    }
    for (i = k = 0; i < sheet->nmerges; ++i) {
        const owf_range *m = &sheet->merges[i];
        int inside = !(old && old->rows && old->cols) ||
                     (m->first_row >= old->at_row && m->last_row < old->at_row + old->rows &&
                      m->first_col >= old->at_col && m->last_col < old->at_col + old->cols);
        if (!inside)
            sheet->merges[k++] = *m;
    }
    sheet->nmerges = k;
}

static void data_name(const owf_book *book, const owf_pivotdef *def, unsigned d, char *out, size_t size)
{
    char heading[80];
    owf_pivot_field_heading(book, def, def->data[d].field, heading, sizeof heading);
    snprintf(out, size, "%s of %s", function_name(def->data[d].function), heading);
}

/* Lays out def's table on the output sheet of an already private workbook,
 * and sets def->rows and def->cols to its size. old: the previous
 * definition on that sheet (its rectangle is emptied), or NULL. */
static int pivot_build(owf_book *work, owf_pivotdef *def, int output_index, const owf_pivotdef *old,
                       owf_calc **made_calc)
{
    const owf_sheet *source = &work->sheets[def->source_sheet];
    pivot_values values[OWF_PIVOT_FIELDS * 2];
    pivot_keys rkeys, ckeys;
    pivot_bucket *matrix = NULL, *rtot = NULL, *ctot = NULL, *grand = NULL;
    unsigned *row_of = NULL, *col_of = NULL, *rorder = NULL, *corder = NULL, *tmp = NULL;
    unsigned nsrc = def->source.last_row - def->source.first_row, kept = 0, nd = def->ndata;
    unsigned nf = def->nrow + def->ncol, i, f, r, c, d, n;
    unsigned key[OWF_PIVOT_FIELDS * 2];
    unsigned top, header_rows, label_cols, nrk, nck, total_cols, first_value_row;
    int value_style[OWF_PIVOT_DATA];
    pivot_out o;
    char text[160];

    memset(values, 0, sizeof values);
    memset(&rkeys, 0, sizeof rkeys);
    memset(&ckeys, 0, sizeof ckeys);
    rkeys.width = def->nrow;
    ckeys.width = def->ncol;
    o.work = work;
    o.calc = NULL;
    o.sheet = output_index;
    o.rc = OWF_ERR_MEMORY;
    for (d = 0; d < nd; d++)
        value_style[d] = 0;

    row_of = malloc((size_t)(nsrc ? nsrc : 1u) * sizeof *row_of);
    col_of = malloc((size_t)(nsrc ? nsrc : 1u) * sizeof *col_of);
    if (!row_of || !col_of)
        goto done;
    for (i = 0; i < nsrc; i++) {
        unsigned srow = def->source.first_row + 1u + i;
        int k, skip = 0;
        for (f = 0; f < def->npage && !skip; f++)
            skip = !owf_pivot_item_matches(&def->page[f],
                                           owf_sheet_find(source, srow, def->source.first_col + def->page[f].field));
        if (skip)
            continue;
        for (f = 0; f < nf; f++) {
            unsigned field = f < def->nrow ? def->row[f] : def->col[f - def->nrow];
            k = values_index(&values[f], owf_sheet_find(source, srow, def->source.first_col + field));
            if (k < 0)
                goto done;
            key[f] = (unsigned)k;
        }
        if ((k = keys_index(&rkeys, key)) < 0)
            goto done;
        row_of[kept] = (unsigned)k;
        if ((k = keys_index(&ckeys, key + def->nrow)) < 0)
            goto done;
        col_of[kept] = (unsigned)k;
        for (d = 0; d < nd; d++) {
            const owf_cell *v = owf_sheet_find(source, srow, def->source.first_col + def->data[d].field);
            if (v && v->style > 0 && v->style < work->nstyles)
                value_style[d] = v->style;
        }
        kept++;
    }
    /* no row fields: one row of totals; no column fields: one column per data field. A filter
       that keeps no source row leaves the table with headings only. */
    nrk = def->nrow ? rkeys.n : 1u;
    nck = def->ncol ? ckeys.n : 1u;
    if ((nck && nrk > PIVOT_MAX_BUCKETS / nck) || nrk * nck > PIVOT_MAX_BUCKETS / nd) {
        o.rc = OWF_ERR_UNSUPPORTED;
        goto done;
    }
    n = (nrk > nck ? nrk : nck) + 1u;
    matrix = calloc((size_t)nrk * nck * nd + 1u, sizeof *matrix);
    rtot = calloc((size_t)nrk * nd + 1u, sizeof *rtot);
    ctot = calloc((size_t)nck * nd + 1u, sizeof *ctot);
    grand = calloc(nd, sizeof *grand);
    rorder = malloc((size_t)n * sizeof *rorder);
    corder = malloc((size_t)n * sizeof *corder);
    tmp = malloc((size_t)n * sizeof *tmp);
    if (!matrix || !rtot || !ctot || !grand || !rorder || !corder || !tmp)
        goto done;
    for (i = 0, kept = 0; i < nsrc; i++) {
        unsigned srow = def->source.first_row + 1u + i;
        int skip = 0;
        for (f = 0; f < def->npage && !skip; f++)
            skip = !owf_pivot_item_matches(&def->page[f],
                                           owf_sheet_find(source, srow, def->source.first_col + def->page[f].field));
        if (skip)
            continue;
        r = row_of[kept];
        c = col_of[kept];
        kept++;
        for (d = 0; d < nd; d++) {
            const owf_cell *v = owf_sheet_find(source, srow, def->source.first_col + def->data[d].field);
            bucket_add(&matrix[((size_t)r * nck + c) * nd + d], v);
            bucket_add(&rtot[(size_t)r * nd + d], v);
            bucket_add(&ctot[(size_t)c * nd + d], v);
            bucket_add(&grand[d], v);
        }
    }
    rorder[0] = corder[0] = 0;
    if (def->nrow)
        sort_keys(&rkeys, rorder, tmp);
    if (def->ncol)
        sort_keys(&ckeys, corder, tmp);

    /* the layout */
    header_rows = def->ncol ? def->ncol + (nd > 1 ? 1u : 0u) : 1u;
    label_cols = def->nrow ? def->nrow : 1u;
    total_cols = def->ncol && def->col_grand ? nd : 0u;
    top = def->at_row + (def->npage ? def->npage + 1u : 0u);
    first_value_row = top + header_rows;
    def->rows = (top - def->at_row) + header_rows + nrk + (def->row_grand && def->nrow ? 1u : 0u);
    def->cols = label_cols + nck * nd + total_cols;
    if (def->at_row + def->rows > OC_MAX_ROWS || def->at_col + def->cols > OC_MAX_COLS) {
        o.rc = OWF_ERR_UNSUPPORTED;
        goto done;
    }
    if (def->source_sheet == output_index &&
        !(def->at_row + def->rows <= def->source.first_row || def->at_row > def->source.last_row ||
          def->at_col + def->cols <= def->source.first_col || def->at_col > def->source.last_col)) {
        o.rc = OWF_ERR_FORMAT;                  /* it would cover its own source */
        goto done;
    }

    work->active = output_index;
    o.calc = owf_calc_new(work);
    if (!o.calc)
        goto done;
    o.rc = OWF_OK;
    if (old)
        clear_output(o.calc, work, output_index, old);
    source = &work->sheets[def->source_sheet];

    for (f = 0; f < def->npage; f++) {           /* the page fields: a heading and the chosen item */
        owf_pivot_field_heading(work, def, def->page[f].field, text, sizeof text);
        out_text(&o, def->at_row + f, def->at_col, text);
        out_bold(&o, def->at_row + f, def->at_col);
        if (def->page[f].kind == OWF_PIVOT_ALL)
            snprintf(text, sizeof text, "- all -");
        else if (def->page[f].kind == OWF_PIVOT_BLANK)
            snprintf(text, sizeof text, "(blank)");
        else
            owf_pivot_item_text(&def->page[f], text, sizeof text);
        out_text(&o, def->at_row + f, def->at_col + 1u, text);
    }

    /* the header rows */
    for (f = 0; f < def->nrow; f++) {
        owf_pivot_field_heading(work, def, def->row[f], text, sizeof text);
        out_text(&o, first_value_row - 1u, def->at_col + f, text);
    }
    if (!def->nrow && !def->ncol)
        out_text(&o, first_value_row - 1u, def->at_col, "Rows");
    for (i = 0; i < nck; i++) {
        unsigned ck = corder[i], col0 = def->at_col + label_cols + i * nd;
        for (f = 0; f < def->ncol; f++) {
            const unsigned *kk = &ckeys.keys[(size_t)ck * ckeys.width];
            int differs = i == 0;
            unsigned g;
            if (!differs) {
                const unsigned *pk = &ckeys.keys[(size_t)corder[i - 1u] * ckeys.width];
                for (g = 0; g <= f && !differs; g++)
                    differs = pk[g] != kk[g];
            }
            if (differs) {
                category_label(work, values[def->nrow + f].sample[kk[f]], text, sizeof text);
                out_text(&o, top + f, col0, text);
            }
        }
        if (!def->ncol || nd > 1)
            for (d = 0; d < nd; d++) {
                data_name(work, def, d, text, sizeof text);
                out_text(&o, first_value_row - 1u, col0 + d, text);
            }
    }
    for (d = 0; d < total_cols; d++) {
        unsigned col = def->at_col + label_cols + nck * nd + d;
        if (nd > 1) {
            char name[140];
            data_name(work, def, d, name, sizeof name);
            snprintf(text, sizeof text, "Total %s", name);
        } else
            snprintf(text, sizeof text, "Grand Total");
        out_text(&o, first_value_row - 1u, col, text);
    }
    for (r = top; r < first_value_row; r++)
        for (c = 0; c < def->cols; c++)
            out_bold(&o, r, def->at_col + c);

    /* the body */
    for (i = 0; i < nrk; i++) {
        unsigned rk = rorder[i], row = first_value_row + i;
        for (f = 0; f < def->nrow; f++) {
            const unsigned *kk = &rkeys.keys[(size_t)rk * rkeys.width];
            int differs = i == 0 || f + 1u == def->nrow;
            unsigned g;
            if (!differs) {
                const unsigned *pk = &rkeys.keys[(size_t)rorder[i - 1u] * rkeys.width];
                for (g = 0; g <= f && !differs; g++)
                    differs = pk[g] != kk[g];
            }
            if (differs) {
                category_label(work, values[f].sample[kk[f]], text, sizeof text);
                out_text(&o, row, def->at_col + f, text);
            }
        }
        if (!def->nrow)
            out_text(&o, row, def->at_col, "Total");
        for (c = 0; c < nck; c++)
            for (d = 0; d < nd; d++)
                out_bucket(&o, row, def->at_col + label_cols + c * nd + d,
                           &matrix[((size_t)rk * nck + corder[c]) * nd + d], def->data[d].function, value_style[d]);
        for (d = 0; d < total_cols; d++) {
            unsigned col = def->at_col + label_cols + nck * nd + d;
            out_bucket(&o, row, col, &rtot[(size_t)rk * nd + d], def->data[d].function, value_style[d]);
            out_bold(&o, row, col);
        }
    }
    if (def->row_grand && def->nrow) {
        unsigned row = first_value_row + nrk;
        out_text(&o, row, def->at_col, "Grand Total");
        for (c = 0; c < nck; c++)
            for (d = 0; d < nd; d++)
                out_bucket(&o, row, def->at_col + label_cols + c * nd + d, &ctot[(size_t)corder[c] * nd + d],
                           def->data[d].function, value_style[d]);
        for (d = 0; d < total_cols; d++)
            out_bucket(&o, row, def->at_col + label_cols + nck * nd + d, &grand[d], def->data[d].function,
                       value_style[d]);
        for (c = 0; c < def->cols; c++)
            out_bold(&o, row, def->at_col + c);
    }
    if (o.rc == OWF_OK)
        o.rc = owf_calc_recalc(o.calc);
    if (o.rc == OWF_OK) {
        *made_calc = o.calc;
        o.calc = NULL;
    }
done:
    owf_calc_free(o.calc);
    for (f = 0; f < OWF_PIVOT_FIELDS * 2; f++)
        values_free(&values[f]);
    keys_free(&rkeys);
    keys_free(&ckeys);
    free(matrix);
    free(rtot);
    free(ctot);
    free(grand);
    free(row_of);
    free(col_of);
    free(rorder);
    free(corder);
    free(tmp);
    return o.rc;
}

/* Puts def into the output sheet's two hidden names, replacing any. */
static int set_names(owf_book *work, int sheet, const owf_pivotdef *def)
{
    char source[600], config[OWF_PIVOT_TEXT_MAX];
    const char *names[2] = { OWF_PIVOT_SOURCE, OWF_PIVOT_CONFIG };
    const char *formulas[2];
    int k, i;
    if (!owf_pivot_source_text(work, def, source, sizeof source) ||
        !owf_pivot_config_text(def, config, sizeof config))
        return OWF_ERR_FORMAT;
    formulas[0] = source;
    formulas[1] = config;
    for (k = 0; k < 2; k++) {
        for (i = 0; i < work->nnames; i++)
            if (work->names[i].sheet == sheet && !strcmp(work->names[i].name, names[k]))
                break;
        if (i < work->nnames) {
            char *copy = oc_strdup(formulas[k]);
            if (!copy)
                return OWF_ERR_MEMORY;
            owf_free(work->names[i].formula);
            work->names[i].formula = copy;
            work->names[i].hidden = 1;
        } else if (owf_book_add_name(work, names[k], sheet, formulas[k], 1) < 0)
            return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

static int pivot_commit(oc_core *core, owf_book *work, owf_calc *calc, int output_index,
                        const owf_pivotdef *def)
{
    owf_book *old_book;
    owf_calc *old_calc;
    oc_undo *undo = oc_undo_new(core, OC_UNDO_BOOK);
    oc_sheetstate *states;
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    states = realloc(core->states, (size_t)work->nsheets * sizeof *states);
    if (!states) {
        oc_undo_free(undo);
        return OWF_ERR_MEMORY;
    }
    core->states = states;
    memset(&core->states[output_index], 0, sizeof core->states[output_index]);
    core->states[output_index].sel.nranges = 1;
    core->states[output_index].sel.ranges[0].first_row = def->at_row;
    core->states[output_index].sel.ranges[0].first_col = def->at_col;
    core->states[output_index].sel.ranges[0].last_row = def->at_row + (def->rows ? def->rows - 1u : 0u);
    core->states[output_index].sel.ranges[0].last_col = def->at_col + (def->cols ? def->cols - 1u : 0u);
    core->states[output_index].tab_col = -1;
    core->nstates = work->nsheets;
    oc_cf_forget(core);
    oc_charts_forget(core);
    old_book = core->book;
    old_calc = core->calc;
    core->book = work;
    core->calc = calc;
    core->sheet = output_index;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    return OWF_OK;
}


/* Makes (output_index < 0: on a new sheet) or remakes the table on a private
 * copy of the book and commits it as one Undo step. */
static int pivot_run(oc_core *core, owf_pivotdef *def, int output_index)
{
    owf_pivotdef old;
    owf_book *work;
    owf_calc *calc = NULL;
    int rc, had_old;
    char name[32];
    if (!owf_pivot_check(core->book, def))
        return OWF_ERR_FORMAT;
    had_old = output_index >= 0 && owf_pivot_get(core->book, output_index, &old);
    work = oc_book_copy(core->book);
    if (!work)
        return OWF_ERR_MEMORY;
    if (output_index < 0) {
        pivot_name(work, name, sizeof name);
        if (!owf_book_add_sheet(work, name)) {
            owf_book_free(work);
            return OWF_ERR_MEMORY;
        }
        output_index = work->nsheets - 1;
        def->at_row = def->at_col = 0;
    }
    rc = pivot_build(work, def, output_index, had_old ? &old : NULL, &calc);
    if (rc == OWF_OK)
        rc = set_names(work, output_index, def);
    if (rc == OWF_OK)
        rc = pivot_commit(core, work, calc, output_index, def);
    if (rc != OWF_OK) {
        owf_calc_free(calc);
        owf_book_free(work);
    }
    return rc;
}

int oc_core_pivot_create(oc_core *core, const owf_pivotdef *layout)
{
    const oc_selection *selection;
    owf_pivotdef def;
    if (!core || !layout)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    def = *layout;
    def.source_sheet = core->sheet;
    def.source = selection->ranges[0];
    def.rows = def.cols = 0;
    return pivot_run(core, &def, -1);
}

int oc_core_pivot_summary(oc_core *core, unsigned row_col, unsigned column_col,
                          unsigned value_col, int function)
{
    const oc_selection *selection;
    owf_pivotdef def;
    unsigned first;
    if (!core)
        return OWF_ERR_FORMAT;
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    first = selection->ranges[0].first_col;
    if (row_col < first || value_col < first || row_col == value_col ||
        (column_col != OC_PIVOT_NO_COLUMN && (column_col < first || column_col == value_col)))
        return OWF_ERR_FORMAT;
    owf_pivot_init(&def);
    def.nrow = 1;
    def.row[0] = row_col - first;
    if (column_col != OC_PIVOT_NO_COLUMN) {
        def.ncol = 1;
        def.col[0] = column_col - first;
    }
    def.ndata = 1;
    def.data[0].field = value_col - first;
    def.data[0].function = function;
    return oc_core_pivot_create(core, &def);
}

int oc_core_pivot_get(const oc_core *core, owf_pivotdef *def)
{
    if (!core || !def)
        return 0;
    return owf_pivot_get(core->book, core->sheet, def);
}

int oc_core_pivot_set_layout(oc_core *core, const owf_pivotdef *layout)
{
    owf_pivotdef def, now;
    if (!core || !layout)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!owf_pivot_get(core->book, core->sheet, &now))
        return OWF_ERR_UNSUPPORTED;
    def = *layout;
    def.source_sheet = now.source_sheet;
    def.source = now.source;
    def.at_row = now.at_row;
    def.at_col = now.at_col;
    return pivot_run(core, &def, core->sheet);
}

int oc_core_refresh_pivot(oc_core *core)
{
    owf_pivotdef def;
    int i, named = 0;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (owf_pivot_get(core->book, core->sheet, &def))
        return pivot_run(core, &def, core->sheet);
    for (i = 0; i < core->book->nnames; i++)
        named |= core->book->names[i].sheet == core->sheet && owf_pivot_is_name(core->book->names[i].name);
    return named ? OWF_ERR_FORMAT : OWF_ERR_UNSUPPORTED;
}
