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

static void category_label(const owf_book *book, const owf_pivot_group *group, const owf_cell *cell, char *out,
                           size_t size)
{
    if (group && cell && cell->type == OWF_VALUE_NUMBER)
        owf_pivot_group_label(group, cell->number, out, size);
    else if (!cell || cell->type == OWF_VALUE_EMPTY)
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

/* from's rows put into into's, as if they had been added there (the
 * parallel form of the running mean and variance). */
static void bucket_merge(pivot_bucket *into, const pivot_bucket *from)
{
    if (!from->rows)
        return;
    into->rows += from->rows;
    into->nonempty += from->nonempty;
    if (!from->numbers)
        return;
    if (!into->numbers) {
        into->product = from->product;
        into->min = from->min;
        into->max = from->max;
        into->mean = from->mean;
        into->m2 = from->m2;
    } else {
        double n1 = (double)into->numbers, n2 = (double)from->numbers, delta = from->mean - into->mean;
        into->product *= from->product;
        if (from->min < into->min) into->min = from->min;
        if (from->max > into->max) into->max = from->max;
        into->mean += delta * n2 / (n1 + n2);
        into->m2 += from->m2 + delta * delta * n1 * n2 / (n1 + n2);
    }
    into->sum += from->sum;
    into->numbers += from->numbers;
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
    const owf_pivot_group *group;    /* a grouped field: its samples are its own group cells */
    owf_cell **owned;
    unsigned nowned;
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

/* copy: cell is the caller's for now only, so a new value keeps a copy of its own. */
static int values_index(pivot_values *v, const owf_cell *cell, int copy)
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
    if (copy) {
        owf_cell *own = malloc(sizeof *own), **list;
        if (!own)
            return -1;
        *own = *cell;                /* a number: nothing of its own to copy */
        list = realloc(v->owned, (size_t)(v->nowned + 1u) * sizeof *list);
        if (!list) {
            free(own);
            return -1;
        }
        v->owned = list;
        v->owned[v->nowned++] = own;
        cell = own;
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
    unsigned i;
    for (i = 0; i < v->nowned; i++)
        free(v->owned[i]);
    free(v->owned);
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

/* Sorted items (owf_pivot.h): blanks last either way; numbers and booleans, then text (case aside),
 * then errors; as Data > Sort orders cells. 0 for two values that sort together. */
static int item_class(const owf_cell *c)
{
    if (!c || c->type == OWF_VALUE_EMPTY || (c->type == OWF_VALUE_TEXT && !(c->text && *c->text)))
        return 3;
    if (c->type == OWF_VALUE_NUMBER || c->type == OWF_VALUE_BOOL)
        return 0;
    return c->type == OWF_VALUE_TEXT ? 1 : 2;
}

static int item_compare(const owf_cell *a, const owf_cell *b, int order)
{
    int ca = item_class(a), cb = item_class(b), result = 0;
    if (ca == 3 || cb == 3)
        return ca == cb ? 0 : ca == 3 ? 1 : -1;
    if (ca != cb)
        result = ca < cb ? -1 : 1;
    else if (ca == 0)
        result = a->number < b->number ? -1 : a->number > b->number ? 1 : 0;
    else if (ca == 1) {
        const unsigned char *p = (const unsigned char *)a->text, *q = (const unsigned char *)b->text;
        while (*p && *q) {
            int x = *p >= 'A' && *p <= 'Z' ? *p + 32 : *p, y = *q >= 'A' && *q <= 'Z' ? *q + 32 : *q;
            if (x != y) {
                result = x < y ? -1 : 1;
                break;
            }
            p++;
            q++;
        }
        if (!result)
            result = *p == *q ? 0 : *p ? 1 : -1;
    } else
        result = a->error < b->error ? -1 : a->error > b->error ? 1 : 0;
    return order == OWF_PIVOT_SORT_DESCENDING ? -result : result;
}

/* rank[i]: where a field's i-th value (in first order) comes once sorted; values that sort
 * together keep their first order. A merge sort, as sort_keys. 0 when out of memory. */
static int rank_values(const pivot_values *v, int sort, unsigned **rank)
{
    unsigned n = v->n, *order, *tmp, width, lo, mid, hi, a, b, t, i;
    *rank = NULL;
    if (sort == OWF_PIVOT_SORT_SOURCE || n < 2u)
        return 1;
    order = malloc((size_t)n * sizeof *order);
    tmp = malloc((size_t)n * sizeof *tmp);
    *rank = malloc((size_t)n * sizeof **rank);
    if (!order || !tmp || !*rank) {
        free(order);
        free(tmp);
        free(*rank);
        *rank = NULL;
        return 0;
    }
    for (i = 0; i < n; i++)
        order[i] = i;
    for (width = 1; width < n; width *= 2u) {
        for (lo = 0; lo < n; lo += 2u * width) {
            mid = lo + width < n ? lo + width : n;
            hi = lo + 2u * width < n ? lo + 2u * width : n;
            a = lo; b = mid; t = lo;
            while (a < mid || b < hi)
                tmp[t++] = a < mid && (b >= hi || item_compare(v->sample[order[a]], v->sample[order[b]], sort) <= 0)
                         ? order[a++] : order[b++];
        }
        memcpy(order, tmp, (size_t)n * sizeof *order);
    }
    for (i = 0; i < n; i++)
        (*rank)[order[i]] = i;
    free(order);
    free(tmp);
    return 1;
}

/* Keys in order: each field's values in their first order (or rank[f]'s, for a sorted field),
 * outer fields first, so a group's rows are together. A plain insertion-merge sort (no qsort
 * context). */
static void sort_keys(const pivot_keys *k, unsigned *const *rank, unsigned *order, unsigned *tmp)
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
                            take_a = rank[f] ? rank[f][ka[f]] < rank[f][kb[f]] : ka[f] < kb[f];
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

static void out_error(pivot_out *o, unsigned row, unsigned col, const char *word)
{
    if (o->rc == OWF_OK && row < OC_MAX_ROWS && col < OC_MAX_COLS)
        o->rc = owf_calc_enter(o->calc, o->sheet, row, col, word);
}

/* A value as its data field shows it (owf_pivot.h): cell is its own
 * bucket, row_total, col_total and grand the totals that it is shared out
 * of, each made with the same function. A share uses percent, the style
 * of its own; a total of 0 gives #DIV/0!. */
static void out_shown(pivot_out *o, unsigned row, unsigned col, const pivot_bucket *cell,
                      const pivot_bucket *row_total, const pivot_bucket *col_total, const pivot_bucket *grand,
                      const owf_pivot_data *data, int style, int percent_style)
{
    double value, r, c, g;
    if (!bucket_value(cell, data->function, &value))
        return;
    switch (data->show) {
    case OWF_PIVOT_SHOW_ROW:
    case OWF_PIVOT_SHOW_COLUMN:
    case OWF_PIVOT_SHOW_TOTAL:
        if (!bucket_value(data->show == OWF_PIVOT_SHOW_ROW ? row_total :
                          data->show == OWF_PIVOT_SHOW_COLUMN ? col_total : grand, data->function, &r) ||
            r == 0.0) {
            out_error(o, row, col, "#DIV/0!");
            return;
        }
        out_number(o, row, col, value / r, percent_style);
        return;
    case OWF_PIVOT_SHOW_INDEX:
        if (!bucket_value(row_total, data->function, &r) || !bucket_value(col_total, data->function, &c) ||
            !bucket_value(grand, data->function, &g) || r * c == 0.0) {
            out_error(o, row, col, "#DIV/0!");
            return;
        }
        out_number(o, row, col, value * g / (r * c), style);
        return;
    default:
        out_number(o, row, col, value, style);
    }
}

/* The style of a value cell with a percent format in place of its own. */
static int percent_of(owf_book *book, int style)
{
    owf_cellstyle copy;
    int format = owf_book_numfmt(book, "0.00%");
    if (format < 0)
        return -1;
    if (style >= 0 && style < book->nstyles)
        copy = book->styles[style];
    else if (book->nstyles > 0)
        copy = book->styles[0];
    else
        owf_cellstyle_init(&copy);           /* a book with no styles yet */
    copy.number_format = format;
    return owf_book_style(book, &copy);
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

/* A row or column field (where hidden items apply). */
static int is_axis(const owf_pivotdef *def, unsigned field)
{
    unsigned k;
    for (k = 0; k < def->nrow; k++)
        if (def->row[k] == field)
            return 1;
    for (k = 0; k < def->ncol; k++)
        if (def->col[k] == field)
            return 1;
    return 0;
}

/* Whether a source row is in the table: each page field chooses it, and none of its row or
 * column fields' items is hidden (a hidden item of a field not on the rows or columns now is
 * kept for when it is again). */
/* A grouped field's number as its group's cell (in *own), else cell as it is. */
static const owf_cell *grouped(const owf_book *book, const owf_pivotdef *def, unsigned field, const owf_cell *cell,
                               owf_cell *own)
{
    const owf_pivot_group *group = owf_pivot_group_of(def, field);
    if (!group || !cell || cell->type != OWF_VALUE_NUMBER)
        return cell;
    memset(own, 0, sizeof *own);
    own->type = OWF_VALUE_NUMBER;
    own->number = owf_pivot_group_key(group, cell->number, book->date1904);
    own->style = -1;
    return own;
}

static int row_kept(const owf_book *book, const owf_pivotdef *def, const owf_sheet *source, unsigned srow)
{
    unsigned f;
    owf_cell own;
    for (f = 0; f < def->npage; f++)
        if (!owf_pivot_item_matches(&def->page[f],
                                    owf_sheet_find(source, srow, def->source.first_col + def->page[f].field)))
            return 0;
    for (f = 0; f < def->nhide && f < OWF_PIVOT_HIDES; f++)
        if (is_axis(def, def->hide[f].field) &&
            owf_pivot_item_matches(&def->hide[f], grouped(book, def, def->hide[f].field,
                                   owf_sheet_find(source, srow, def->source.first_col + def->hide[f].field), &own)))
            return 0;
    return 1;
}

/* Whether the i-th row (or column) key in order starts a new item of field f or of any field
 * outside it. */
static int keys_break(const pivot_keys *k, const unsigned *order, unsigned i, unsigned f)
{
    const unsigned *a, *b;
    unsigned g;
    if (!i)
        return 1;
    a = &k->keys[(size_t)order[i - 1u] * k->width];
    b = &k->keys[(size_t)order[i] * k->width];
    for (g = 0; g <= f; g++)
        if (a[g] != b[g])
            return 1;
    return 0;
}

/* A column slot of the output: a column key (its place in the column order), or a column
 * field's subtotal over the keys start..end (index: its place among the subtotals). Each slot
 * is one column per data field. */
typedef struct {
    int sub;
    unsigned key, f, start, end, index;
} pivot_slot;

/* One row of value cells across the slots, then the total columns. base holds the row's
 * buckets by column (map: the column order's keys, or NULL when base is in that order
 * already), rowt the row's own totals; a column key shares out of its ctot, a subtotal
 * column out of its csubtot. */
static void out_value_row(pivot_out *o, const owf_pivotdef *def, unsigned row, unsigned col0,
                          const pivot_slot *slots, unsigned nslots, const pivot_bucket *base, const unsigned *map,
                          const pivot_bucket *rowt, const pivot_bucket *ctot, const unsigned *corder,
                          const pivot_bucket *csubtot, const pivot_bucket *grand, unsigned total_cols,
                          const int *value_style, const int *percent_style, int bold_total)
{
    unsigned nd = def->ndata, s, c, d;
    for (s = 0; s < nslots; s++) {
        const pivot_slot *sl = &slots[s];
        for (d = 0; d < nd; d++) {
            unsigned col = col0 + s * nd + d;
            if (!sl->sub) {
                c = sl->key;
                out_shown(o, row, col, &base[(size_t)(map ? map[c] : c) * nd + d], &rowt[d],
                          &ctot[(size_t)corder[c] * nd + d], &grand[d], &def->data[d], value_style[d],
                          percent_style[d]);
            } else {
                pivot_bucket merged;
                memset(&merged, 0, sizeof merged);
                for (c = sl->start; c <= sl->end; c++)
                    bucket_merge(&merged, &base[(size_t)(map ? map[c] : c) * nd + d]);
                out_shown(o, row, col, &merged, &rowt[d], &csubtot[(size_t)sl->index * nd + d], &grand[d],
                          &def->data[d], value_style[d], percent_style[d]);
            }
        }
    }
    for (d = 0; d < total_cols; d++) {
        unsigned col = col0 + nslots * nd + d;
        out_shown(o, row, col, &rowt[d], &rowt[d], &grand[d], &grand[d], &def->data[d], value_style[d],
                  percent_style[d]);
        if (bold_total)
            out_bold(o, row, col);
    }
}

/* A question to pivot_build in place of laying out: the source rows behind one output cell. */
typedef struct {
    unsigned row, col;               /* the output cell */
    unsigned *srows, n;              /* the answer: source sheet rows, in the source's order (malloc'd) */
} pivot_probe;

/* Lays out def's table on the output sheet of an already private workbook,
 * and sets def->rows and def->cols to its size. old: the previous
 * definition on that sheet (its rectangle is emptied), or NULL. */
static int pivot_build(owf_book *work, owf_pivotdef *def, int output_index, const owf_pivotdef *old,
                       owf_calc **made_calc, pivot_probe *probe)
{
    const owf_sheet *source = &work->sheets[def->source_sheet];
    pivot_values values[OWF_PIVOT_FIELDS * 2];
    pivot_keys rkeys, ckeys;
    pivot_bucket *matrix = NULL, *rtot = NULL, *ctot = NULL, *grand = NULL, *sub = NULL, *subr = NULL;
    pivot_bucket *csubtot = NULL;
    pivot_slot *slots = NULL;
    unsigned nslots = 0, ncsub = 0;
    unsigned *row_of = NULL, *col_of = NULL, *rorder = NULL, *corder = NULL, *tmp = NULL;
    unsigned *rank[OWF_PIVOT_FIELDS * 2];
    unsigned nsrc = def->source.last_row - def->source.first_row, kept = 0, nd = def->ndata;
    unsigned nf = def->nrow + def->ncol, i, f, r, c, d, n;
    unsigned key[OWF_PIVOT_FIELDS * 2];
    unsigned top, header_rows, label_cols, nrk, nck, total_cols, first_value_row, nsub = 0, shift = 0;
    unsigned gstart[OWF_PIVOT_FIELDS];
    int subtotalled[OWF_PIVOT_FIELDS], csubtotalled[OWF_PIVOT_FIELDS];
    int value_style[OWF_PIVOT_DATA], percent_style[OWF_PIVOT_DATA];
    pivot_out o;
    char text[160];

    memset(values, 0, sizeof values);
    for (f = 0; f < nf; f++)
        values[f].group = owf_pivot_group_of(def, f < def->nrow ? def->row[f] : def->col[f - def->nrow]);
    memset(rank, 0, sizeof rank);
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
        int k;
        if (!row_kept(work, def, source, srow))
            continue;
        for (f = 0; f < nf; f++) {
            unsigned field = f < def->nrow ? def->row[f] : def->col[f - def->nrow];
            const owf_cell *cell = owf_sheet_find(source, srow, def->source.first_col + field), *as;
            owf_cell own;
            as = grouped(work, def, field, cell, &own);
            k = values_index(&values[f], as, as == &own);
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
    for (d = 0; d < nd; d++) {
        int show = def->data[d].show;
        percent_style[d] = value_style[d];
        if ((show == OWF_PIVOT_SHOW_ROW || show == OWF_PIVOT_SHOW_COLUMN || show == OWF_PIVOT_SHOW_TOTAL) &&
            (percent_style[d] = percent_of(work, value_style[d])) < 0)
            goto done;
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
        if (!row_kept(work, def, source, srow))
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
    for (f = 0; f < nf; f++) {
        unsigned field = f < def->nrow ? def->row[f] : def->col[f - def->nrow];
        int sort = owf_pivot_sort_of(def, field);
        if (sort == OWF_PIVOT_SORT_SOURCE && values[f].group)
            sort = OWF_PIVOT_SORT_ASCENDING;     /* groups come in their own order */
        if (!rank_values(&values[f], sort, &rank[f]))
            goto done;
    }
    rorder[0] = corder[0] = 0;
    if (def->nrow)
        sort_keys(&rkeys, rank, rorder, tmp);
    if (def->ncol)
        sort_keys(&ckeys, rank + def->nrow, corder, tmp);
    /* subtotal rows: one under each item of a row field that has them, the innermost field aside */
    for (f = 0; f < OWF_PIVOT_FIELDS; f++) {
        subtotalled[f] = f + 1u < def->nrow && owf_pivot_subtotal_of(def, def->row[f]);
        gstart[f] = 0;
    }
    for (f = 0; f + 1u < def->nrow; f++)
        for (i = 0; subtotalled[f] && i < nrk; i++)
            nsub += i + 1u == nrk || keys_break(&rkeys, rorder, i + 1u, f);
    if (nsub) {
        sub = calloc((size_t)nck * nd + 1u, sizeof *sub);
        subr = calloc((size_t)nd + 1u, sizeof *subr);
        if (!sub || !subr)
            goto done;
    }
    /* the column slots: each column key, and after an item of a column field that has them, its
       subtotal (inner fields' first), the innermost field aside */
    for (f = 0; f < OWF_PIVOT_FIELDS; f++)
        csubtotalled[f] = f + 1u < def->ncol && owf_pivot_subtotal_of(def, def->col[f]);
    for (f = 0; f + 1u < def->ncol; f++)
        for (i = 0; csubtotalled[f] && i < nck; i++)
            ncsub += i + 1u == nck || keys_break(&ckeys, corder, i + 1u, f);
    slots = calloc((size_t)nck + ncsub + 1u, sizeof *slots);
    csubtot = calloc((size_t)ncsub * nd + 1u, sizeof *csubtot);
    if (!slots || !csubtot)
        goto done;
    {
        unsigned cstart[OWF_PIVOT_FIELDS], k = 0, j;
        memset(cstart, 0, sizeof cstart);
        for (i = 0; i < nck; i++) {
            for (f = 0; f < def->ncol; f++)
                if (keys_break(&ckeys, corder, i, f))
                    cstart[f] = i;
            slots[nslots].sub = 0;
            slots[nslots++].key = i;
            for (f = def->ncol ? def->ncol - 1u : 0u; f-- > 0;) {
                pivot_slot *sl;
                if (!csubtotalled[f] || (i + 1u < nck && !keys_break(&ckeys, corder, i + 1u, f)))
                    continue;
                sl = &slots[nslots++];
                sl->sub = 1;
                sl->f = f;
                sl->start = cstart[f];
                sl->end = i;
                sl->key = i;
                sl->index = k;
                for (j = sl->start; j <= sl->end; j++)
                    for (d = 0; d < nd; d++)
                        bucket_merge(&csubtot[(size_t)k * nd + d], &ctot[(size_t)corder[j] * nd + d]);
                k++;
            }
        }
    }

    /* the layout */
    header_rows = def->ncol ? def->ncol + (nd > 1 ? 1u : 0u) : 1u;
    label_cols = def->nrow ? def->nrow : 1u;
    total_cols = def->ncol && def->col_grand ? nd : 0u;
    top = def->at_row + (def->npage ? def->npage + 1u : 0u);
    first_value_row = top + header_rows;
    def->rows = (top - def->at_row) + header_rows + nrk + nsub + (def->row_grand && def->nrow ? 1u : 0u);
    def->cols = label_cols + nslots * nd + total_cols;
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
    if (probe) {
        /* the row: a data row (all its row fields), a subtotal row (its field and those outside
           it) or the Grand Total row (none); the column: a column key, a subtotal column or a
           total column the same way. OWF_ERR_FORMAT for a heading or label cell. */
        unsigned base = def->at_col + label_cols, rk_fix = 0, ck_fix = 0, rdepth = 0, cdepth = 0, sh = 0, off;
        int found = 0;
        o.rc = OWF_ERR_FORMAT;
        if (probe->row < first_value_row || probe->row >= def->at_row + def->rows || probe->col < base ||
            probe->col >= def->at_col + def->cols)
            goto done;
        for (i = 0; i < nrk && !found; i++) {
            if (first_value_row + i + sh == probe->row) {
                found = 1;
                rk_fix = rorder[i];
                rdepth = def->nrow;
                break;
            }
            for (f = def->nrow ? def->nrow - 1u : 0u; f-- > 0;) {
                if (!subtotalled[f] || (i + 1u < nrk && !keys_break(&rkeys, rorder, i + 1u, f)))
                    continue;
                if (first_value_row + i + ++sh == probe->row) {
                    found = 1;
                    rk_fix = rorder[i];
                    rdepth = f + 1u;
                    break;
                }
            }
        }
        off = probe->col - base;
        if (off < nslots * nd) {
            const pivot_slot *sl = &slots[off / nd];
            ck_fix = corder[sl->key];
            cdepth = sl->sub ? sl->f + 1u : def->ncol;
        }
        probe->n = 0;
        probe->srows = malloc((size_t)(nsrc ? nsrc : 1u) * sizeof *probe->srows);
        if (!probe->srows) {
            o.rc = OWF_ERR_MEMORY;
            goto done;
        }
        for (i = 0, kept = 0; i < nsrc; i++) {
            unsigned srow = def->source.first_row + 1u + i;
            int match = 1;
            if (!row_kept(work, def, source, srow))
                continue;
            r = row_of[kept];
            c = col_of[kept];
            kept++;
            for (f = 0; f < rdepth && match; f++)
                match = rkeys.keys[(size_t)r * rkeys.width + f] == rkeys.keys[(size_t)rk_fix * rkeys.width + f];
            for (f = 0; f < cdepth && match; f++)
                match = ckeys.keys[(size_t)c * ckeys.width + f] == ckeys.keys[(size_t)ck_fix * ckeys.width + f];
            if (match)
                probe->srows[probe->n++] = srow;
        }
        o.rc = OWF_OK;
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
    for (n = 0; n < nslots; n++) {
        const pivot_slot *sl = &slots[n];
        unsigned ck = corder[sl->key], col0 = def->at_col + label_cols + n * nd;
        i = sl->key;
        if (sl->sub) {                           /* a column subtotal: "East Total" on its field's row */
            const unsigned *kk = &ckeys.keys[(size_t)ck * ckeys.width];
            char item[96];
            category_label(work, values[def->nrow + sl->f].group, values[def->nrow + sl->f].sample[kk[sl->f]], item,
                           sizeof item);
            snprintf(text, sizeof text, "%s Total", item);
            out_text(&o, top + sl->f, col0, text);
        } else
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
                    category_label(work, values[def->nrow + f].group, values[def->nrow + f].sample[kk[f]], text,
                                   sizeof text);
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
        unsigned col = def->at_col + label_cols + nslots * nd + d;
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
        unsigned rk = rorder[i], row = first_value_row + i + shift;
        for (f = 0; f < def->nrow; f++)
            if (keys_break(&rkeys, rorder, i, f))
                gstart[f] = i;
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
                category_label(work, values[f].group, values[f].sample[kk[f]], text, sizeof text);
                out_text(&o, row, def->at_col + f, text);
            }
        }
        if (!def->nrow)
            out_text(&o, row, def->at_col, "Total");
        out_value_row(&o, def, row, def->at_col + label_cols, slots, nslots, &matrix[(size_t)rk * nck * nd], corder,
                      &rtot[(size_t)rk * nd], ctot, corder, csubtot, grand, total_cols, value_style, percent_style, 1);
        /* the items that end here, innermost first: a subtotal row each */
        for (f = def->nrow ? def->nrow - 1u : 0u; f-- > 0;) {
            const unsigned *kk = &rkeys.keys[(size_t)rk * rkeys.width];
            char item[96];
            unsigned j;
            if (!subtotalled[f] || (i + 1u < nrk && !keys_break(&rkeys, rorder, i + 1u, f)))
                continue;
            row = first_value_row + i + ++shift;
            memset(sub, 0, ((size_t)nck * nd + 1u) * sizeof *sub);
            memset(subr, 0, ((size_t)nd + 1u) * sizeof *subr);
            for (j = gstart[f]; j <= i; j++) {
                unsigned jk = rorder[j];
                for (c = 0; c < nck; c++)
                    for (d = 0; d < nd; d++)
                        bucket_merge(&sub[(size_t)c * nd + d], &matrix[((size_t)jk * nck + corder[c]) * nd + d]);
                for (d = 0; d < nd; d++)
                    bucket_merge(&subr[d], &rtot[(size_t)jk * nd + d]);
            }
            category_label(work, values[f].group, values[f].sample[kk[f]], item, sizeof item);
            snprintf(text, sizeof text, "%s Total", item);
            out_text(&o, row, def->at_col + f, text);
            out_value_row(&o, def, row, def->at_col + label_cols, slots, nslots, sub, NULL, subr, ctot, corder,
                          csubtot, grand, total_cols, value_style, percent_style, 0);
            for (c = f; c < def->cols; c++)
                out_bold(&o, row, def->at_col + c);
        }
    }
    if (def->row_grand && def->nrow) {
        unsigned row = first_value_row + nrk + nsub;
        out_text(&o, row, def->at_col, "Grand Total");
        out_value_row(&o, def, row, def->at_col + label_cols, slots, nslots, ctot, corder, grand, ctot, corder,
                      csubtot, grand, total_cols, value_style, percent_style, 0);
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
    for (f = 0; f < OWF_PIVOT_FIELDS * 2; f++) {
        values_free(&values[f]);
        free(rank[f]);
    }
    keys_free(&rkeys);
    keys_free(&ckeys);
    free(matrix);
    free(rtot);
    free(ctot);
    free(grand);
    free(sub);
    free(subr);
    free(csubtot);
    free(slots);
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
    rc = pivot_build(work, def, output_index, had_old ? &old : NULL, &calc, NULL);
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

/* A source cell's value into the details sheet, with its style (the same book's). */
static int copy_value(owf_calc *calc, owf_book *book, int to, unsigned row, unsigned col, int from, unsigned srow,
                      unsigned scol)
{
    const owf_cell *c = owf_sheet_find(&book->sheets[from], srow, scol);
    char input[48], shown[160];
    owf_cell *dst;
    int rc, style;
    if (!c || c->type == OWF_VALUE_EMPTY)
        return OWF_OK;
    style = c->style;
    switch (c->type) {
    case OWF_VALUE_NUMBER:
        owf_number_text(c->number, input);
        rc = owf_calc_enter(calc, to, row, col, input);
        break;
    case OWF_VALUE_BOOL: rc = owf_calc_enter(calc, to, row, col, c->number != 0.0 ? "TRUE" : "FALSE"); break;
    case OWF_VALUE_TEXT: rc = put_text(calc, to, row, col, c->text); break;
    default:
        owf_calc_format(book, c, shown, sizeof shown);
        rc = put_text(calc, to, row, col, shown);
        break;
    }
    if (rc == OWF_OK && style > 0 && style < book->nstyles && (dst = owf_sheet_cell(&book->sheets[to], row, col, 0)))
        dst->style = style;
    return rc;
}

/* "Details", "Details2"...: the first free name. */
static void details_name(const owf_book *book, char *out, size_t size)
{
    int suffix = 1, found, i;
    do {
        found = 0;
        if (suffix == 1)
            snprintf(out, size, "Details");
        else
            snprintf(out, size, "Details%d", suffix);
        for (i = 0; i < book->nsheets; i++)
            if (!strcmp(book->sheets[i].name, out))
                found = 1;
        suffix++;
    } while (found);
}

/* Whether the output rectangle of a's sheet holds what b's does (b: the table made afresh). */
static int same_output(const owf_book *a, const owf_book *b, int sheet, const owf_pivotdef *drawn,
                       const owf_pivotdef *now)
{
    const owf_sheet *sa = &a->sheets[sheet], *sb = &b->sheets[sheet];
    int i;
    if (drawn->rows != now->rows || drawn->cols != now->cols || drawn->at_row != now->at_row ||
        drawn->at_col != now->at_col)
        return 0;
    for (i = 0; i < sa->ncells; i++) {
        const owf_cell *c = &sa->cells[i];
        if (c->row >= now->at_row && c->row < now->at_row + now->rows && c->col >= now->at_col &&
            c->col < now->at_col + now->cols && !same_value(c, owf_sheet_find(sb, c->row, c->col)))
            return 0;
    }
    for (i = 0; i < sb->ncells; i++) {
        const owf_cell *c = &sb->cells[i];
        if (c->row >= now->at_row && c->row < now->at_row + now->rows && c->col >= now->at_col &&
            c->col < now->at_col + now->cols && !same_value(c, owf_sheet_find(sa, c->row, c->col)))
            return 0;
    }
    return 1;
}

int oc_core_pivot_details(oc_core *core, unsigned row, unsigned col)
{
    owf_pivotdef def, at, drawn;
    owf_book *work;
    owf_calc *calc = NULL;
    pivot_probe probe;
    char name[32];
    unsigned k, x, width;
    int rc, out;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!owf_pivot_get(core->book, core->sheet, &def))
        return OWF_ERR_UNSUPPORTED;
    work = oc_book_copy(core->book);
    if (!work)
        return OWF_ERR_MEMORY;
    memset(&probe, 0, sizeof probe);
    probe.row = row;
    probe.col = col;
    /* the cursor is on the table as last drawn: made afresh it must be the same, or the cell
       would stand for other rows */
    drawn = def;
    rc = pivot_build(work, &def, core->sheet, &drawn, &calc, NULL);
    owf_calc_free(calc);
    calc = NULL;
    if (rc == OWF_OK && !same_output(core->book, work, core->sheet, &drawn, &def))
        rc = OC_PIVOT_STALE;
    if (rc == OWF_OK)
        rc = pivot_build(work, &def, core->sheet, NULL, &calc, &probe);
    owf_calc_free(calc);                     /* none is made for a probe */
    calc = NULL;
    width = def.source.last_col - def.source.first_col + 1u;
    if (rc == OWF_OK && (probe.n + 1u > OC_MAX_ROWS || width > OC_MAX_COLS))
        rc = OWF_ERR_UNSUPPORTED;
    if (rc == OWF_OK) {
        details_name(work, name, sizeof name);
        if (!owf_book_add_sheet(work, name))
            rc = OWF_ERR_MEMORY;
    }
    if (rc == OWF_OK) {
        out = work->nsheets - 1;
        work->active = out;
        calc = owf_calc_new(work);
        if (!calc)
            rc = OWF_ERR_MEMORY;
        for (x = 0; rc == OWF_OK && x < width; x++) {      /* the heading row, bold */
            rc = copy_value(calc, work, out, 0, x, def.source_sheet, def.source.first_row, def.source.first_col + x);
            if (rc == OWF_OK)
                rc = bold_cell(work, &work->sheets[out], 0, x);
        }
        for (k = 0; rc == OWF_OK && k < probe.n; k++)
            for (x = 0; rc == OWF_OK && x < width; x++)
                rc = copy_value(calc, work, out, k + 1u, x, def.source_sheet, probe.srows[k], def.source.first_col + x);
        if (rc == OWF_OK)
            rc = owf_calc_recalc(calc);
        if (rc == OWF_OK) {
            owf_pivot_init(&at);                 /* the selection: the copied rows */
            at.rows = probe.n + 1u;
            at.cols = width;
            rc = pivot_commit(core, work, calc, out, &at);
        }
    }
    if (rc != OWF_OK) {
        owf_calc_free(calc);
        owf_book_free(work);
    }
    free(probe.srows);
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
