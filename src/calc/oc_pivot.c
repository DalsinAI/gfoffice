/*
 * GFOpenCalc's portable core: a DataPilot-style pivot-summary snapshot.
 * Aggregation is independent of the Amiga shell and output is ordinary cells,
 * so XLSX/ODS writers need no private extension to preserve the result.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIVOT_MAX_BUCKETS 65536u

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

static int category_index(const owf_book *book, pivot_category **items, unsigned *count,
                          unsigned *capacity, const owf_cell *cell)
{
    pivot_category *p;
    unsigned i, cap;
    for (i = 0; i < *count; ++i)
        if (same_value((*items)[i].sample, cell))
            return (int)i;
    if (*count == *capacity) {
        cap = *capacity ? *capacity * 2u : 16u;
        p = realloc(*items, (size_t)cap * sizeof *p);
        if (!p)
            return -1;
        *items = p;
        *capacity = cap;
    }
    p = &(*items)[*count];
    p->sample = cell;
    category_label(book, cell, p->label, sizeof p->label);
    return (int)(*count)++;
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
    char input[128];
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

#define PIVOT_SOURCE_NAME "_GF_PIVOT_SOURCE"
#define PIVOT_CONFIG_NAME "_GF_PIVOT_CONFIG"

static int pivot_args_ok(const owf_book *book, int source_index, const owf_range *range,
                         unsigned row_col, unsigned column_col, unsigned value_col, int function)
{
    return book && source_index >= 0 && source_index < book->nsheets && range &&
           function >= OC_SUBTOTAL_AVERAGE && function <= OC_SUBTOTAL_VARP &&
           range->first_row < range->last_row &&
           row_col >= range->first_col && row_col <= range->last_col &&
           value_col >= range->first_col && value_col <= range->last_col && row_col != value_col &&
           (column_col == OC_PIVOT_NO_COLUMN ||
            (column_col >= range->first_col && column_col <= range->last_col &&
             column_col != row_col && column_col != value_col));
}

static int source_formula(const owf_book *book, int source_index, const owf_range *range,
                          char *out, size_t size)
{
    const char *name = book->sheets[source_index].name;
    char first[8], last[8], *p = out;
    size_t left = size, i;
    int n;
    if (left < 4) return 0;
    *p++ = '\''; left--;
    for (i = 0; name[i]; ++i) {
        if (left <= (name[i] == '\'' ? 2u : 1u)) return 0;
        *p++ = name[i]; left--;
        if (name[i] == '\'') { *p++ = '\''; left--; }
    }
    if (left < 3) return 0;
    *p++ = '\''; *p++ = '!'; left -= 2;
    oc_col_name(range->first_col, first);
    oc_col_name(range->last_col, last);
    n = snprintf(p, left, "$%s$%u:$%s$%u", first, range->first_row + 1u,
                 last, range->last_row + 1u);
    return n >= 0 && (size_t)n < left;
}

static int add_metadata(owf_book *book, int output_index, int source_index, const owf_range *range,
                        unsigned row_col, unsigned column_col, unsigned value_col, int function)
{
    char source[128], config[80];
    int column = column_col == OC_PIVOT_NO_COLUMN ? -1 : (int)column_col;
    if (!source_formula(book, source_index, range, source, sizeof source))
        return OWF_ERR_FORMAT;
    snprintf(config, sizeof config, "\"%u,%d,%u,%d\"", row_col, column, value_col, function);
    if (owf_book_add_name(book, PIVOT_SOURCE_NAME, output_index, source, 1) < 0 ||
        owf_book_add_name(book, PIVOT_CONFIG_NAME, output_index, config, 1) < 0)
        return OWF_ERR_MEMORY;
    return OWF_OK;
}

static void clear_output(owf_calc *calc, owf_book *book, int sheet_index)
{
    owf_sheet *sheet = &book->sheets[sheet_index];
    int i;
    for (i = 0; i < sheet->ncells; ++i) {
        unsigned row = sheet->cells[i].row, col = sheet->cells[i].col;
        owf_calc_enter(calc, sheet_index, row, col, "");
        sheet->cells[i].style = 0;
    }
    free(sheet->merges);
    sheet->merges = NULL;
    sheet->nmerges = sheet->capmerges = 0;
}

/* Transforms an already private workbook. output_index < 0 creates a new
 * summary and its metadata; otherwise the existing output is refreshed. */
static int pivot_transform(owf_book *work, int source_index, const owf_range *range,
                           unsigned row_col, unsigned column_col, unsigned value_col, int function,
                           int *output_index, unsigned *made_rows, unsigned *made_cols,
                           owf_calc **made_calc)
{
    const owf_sheet *source = &work->sheets[source_index];
    pivot_category *rows = NULL, *cols = NULL;
    pivot_bucket *matrix = NULL, *row_totals = NULL, *col_totals = NULL;
    pivot_bucket grand = { 0 };
    owf_calc *calc = NULL;
    owf_sheet *output;
    unsigned nrows = 0, ncols = 0, row_capacity = 0, col_capacity = 0;
    unsigned row, r, c, out_cols, out_rows;
    int ri, ci, rc = OWF_ERR_FORMAT, created = *output_index < 0;
    int value_style = 0;
    char name[32], heading[112];

    for (row = range->first_row + 1; row <= range->last_row; ++row) {
        const owf_cell *row_cell = owf_sheet_find(source, row, row_col);
        const owf_cell *col_cell = column_col == OC_PIVOT_NO_COLUMN ? NULL : owf_sheet_find(source, row, column_col);
        const owf_cell *value_cell = owf_sheet_find(source, row, value_col);
        ri = category_index(work, &rows, &nrows, &row_capacity, row_cell);
        if (ri < 0) { rc = OWF_ERR_MEMORY; goto done; }
        if (column_col == OC_PIVOT_NO_COLUMN)
            ci = 0;
        else {
            ci = category_index(work, &cols, &ncols, &col_capacity, col_cell);
            if (ci < 0) { rc = OWF_ERR_MEMORY; goto done; }
        }
        if (value_cell && value_cell->style >= 0 && value_cell->style < work->nstyles)
            value_style = value_cell->style;
    }
    if (!nrows) { rc = OWF_ERR_FORMAT; goto done; }
    if (column_col == OC_PIVOT_NO_COLUMN)
        ncols = 1;
    if (nrows > OC_MAX_ROWS - 2u || ncols > OC_MAX_COLS - 2u ||
        nrows > PIVOT_MAX_BUCKETS / ncols) {
        rc = OWF_ERR_UNSUPPORTED;
        goto done;
    }
    matrix = calloc((size_t)nrows * ncols, sizeof *matrix);
    row_totals = calloc(nrows, sizeof *row_totals);
    col_totals = calloc(ncols, sizeof *col_totals);
    if (!matrix || !row_totals || !col_totals) { rc = OWF_ERR_MEMORY; goto done; }
    for (row = range->first_row + 1; row <= range->last_row; ++row) {
        const owf_cell *row_cell = owf_sheet_find(source, row, row_col);
        const owf_cell *col_cell = column_col == OC_PIVOT_NO_COLUMN ? NULL : owf_sheet_find(source, row, column_col);
        const owf_cell *value_cell = owf_sheet_find(source, row, value_col);
        for (r = 0; r < nrows && !same_value(rows[r].sample, row_cell); ++r) {}
        if (column_col == OC_PIVOT_NO_COLUMN)
            c = 0;
        else
            for (c = 0; c < ncols && !same_value(cols[c].sample, col_cell); ++c) {}
        bucket_add(&matrix[r * ncols + c], value_cell);
        bucket_add(&row_totals[r], value_cell);
        bucket_add(&col_totals[c], value_cell);
        bucket_add(&grand, value_cell);
    }

    if (created) {
        pivot_name(work, name, sizeof name);
        output = owf_book_add_sheet(work, name);
        if (!output) { rc = OWF_ERR_MEMORY; goto done; }
        *output_index = work->nsheets - 1;
        source = &work->sheets[source_index];
        rc = add_metadata(work, *output_index, source_index, range,
                          row_col, column_col, value_col, function);
        if (rc != OWF_OK) goto done;
    } else
        output = &work->sheets[*output_index];
    work->active = *output_index;
    calc = owf_calc_new(work);
    if (!calc) { rc = OWF_ERR_MEMORY; goto done; }
    if (!created)
        clear_output(calc, work, *output_index);

    category_label(work, owf_sheet_find(source, range->first_row, row_col), heading, sizeof heading);
    if (!strcmp(heading, "(blank)")) snprintf(heading, sizeof heading, "Rows");
    if ((rc = put_text(calc, *output_index, 0, 0, heading)) != OWF_OK) goto done;
    if (column_col == OC_PIVOT_NO_COLUMN) {
        char value_heading[80];
        category_label(work, owf_sheet_find(source, range->first_row, value_col), value_heading, sizeof value_heading);
        snprintf(heading, sizeof heading, "%s of %s", function_name(function),
                 !strcmp(value_heading, "(blank)") ? "Value" : value_heading);
        if ((rc = put_text(calc, *output_index, 0, 1, heading)) != OWF_OK) goto done;
    } else {
        for (c = 0; c < ncols; ++c)
            if ((rc = put_text(calc, *output_index, 0, c + 1u, cols[c].label)) != OWF_OK) goto done;
        if ((rc = put_text(calc, *output_index, 0, ncols + 1u, "Grand Total")) != OWF_OK) goto done;
    }
    for (r = 0; r < nrows; ++r) {
        if ((rc = put_text(calc, *output_index, r + 1u, 0, rows[r].label)) != OWF_OK) goto done;
        for (c = 0; c < ncols; ++c) {
            double value;
            if (bucket_value(&matrix[r * ncols + c], function, &value)) {
                if ((rc = put_number(calc, *output_index, r + 1u, c + 1u, value)) != OWF_OK) goto done;
                owf_sheet_cell(output, r + 1u, c + 1u, 0)->style = value_style;
            }
        }
        if (column_col != OC_PIVOT_NO_COLUMN) {
            double value;
            if (bucket_value(&row_totals[r], function, &value)) {
                if ((rc = put_number(calc, *output_index, r + 1u, ncols + 1u, value)) != OWF_OK) goto done;
                owf_sheet_cell(output, r + 1u, ncols + 1u, 0)->style = value_style;
            }
        }
    }
    if ((rc = put_text(calc, *output_index, nrows + 1u, 0, "Grand Total")) != OWF_OK) goto done;
    for (c = 0; c < ncols; ++c) {
        double value;
        if (bucket_value(&col_totals[c], function, &value)) {
            if ((rc = put_number(calc, *output_index, nrows + 1u, c + 1u, value)) != OWF_OK) goto done;
            owf_sheet_cell(output, nrows + 1u, c + 1u, 0)->style = value_style;
        }
    }
    if (column_col != OC_PIVOT_NO_COLUMN) {
        double value;
        if (bucket_value(&grand, function, &value)) {
            if ((rc = put_number(calc, *output_index, nrows + 1u, ncols + 1u, value)) != OWF_OK) goto done;
            owf_sheet_cell(output, nrows + 1u, ncols + 1u, 0)->style = value_style;
        }
    }
    out_cols = ncols + (column_col == OC_PIVOT_NO_COLUMN ? 1u : 2u);
    out_rows = nrows + 2u;
    for (c = 0; c < out_cols; ++c) {
        if ((rc = bold_cell(work, output, 0, c)) != OWF_OK) goto done;
        if ((rc = bold_cell(work, output, out_rows - 1u, c)) != OWF_OK) goto done;
    }
    if (column_col != OC_PIVOT_NO_COLUMN)
        for (r = 1; r + 1u < out_rows; ++r)
            if ((rc = bold_cell(work, output, r, out_cols - 1u)) != OWF_OK) goto done;
    if ((rc = owf_calc_recalc(calc)) != OWF_OK) goto done;
    *made_rows = out_rows;
    *made_cols = out_cols;
    *made_calc = calc;
    calc = NULL;
    rc = OWF_OK;
done:
    owf_calc_free(calc);
    free(rows);
    free(cols);
    free(matrix);
    free(row_totals);
    free(col_totals);
    return rc;
}

static int pivot_commit(oc_core *core, owf_book *work, owf_calc *calc,
                        int output_index, unsigned out_rows, unsigned out_cols)
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
    core->states[output_index].sel.ranges[0].last_row = out_rows - 1u;
    core->states[output_index].sel.ranges[0].last_col = out_cols - 1u;
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

int oc_core_pivot_summary(oc_core *core, unsigned row_col, unsigned column_col,
                          unsigned value_col, int function)
{
    const oc_selection *selection;
    const owf_range *range;
    owf_book *work;
    owf_calc *calc = NULL;
    unsigned out_rows = 0, out_cols = 0;
    int output_index = -1, rc, source_index;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    range = &selection->ranges[0];
    source_index = core->sheet;
    if (!pivot_args_ok(core->book, source_index, range, row_col, column_col, value_col, function))
        return OWF_ERR_FORMAT;
    work = oc_book_copy(core->book);
    if (!work)
        return OWF_ERR_MEMORY;
    rc = pivot_transform(work, source_index, range, row_col, column_col, value_col, function,
                         &output_index, &out_rows, &out_cols, &calc);
    if (rc == OWF_OK)
        rc = pivot_commit(core, work, calc, output_index, out_rows, out_cols);
    if (rc != OWF_OK) {
        owf_calc_free(calc);
        owf_book_free(work);
    }
    return rc;
}

static const owf_defname *pivot_name_at(const owf_book *book, int sheet, const char *wanted)
{
    int i;
    for (i = 0; i < book->nnames; ++i)
        if (book->names[i].sheet == sheet && !strcmp(book->names[i].name, wanted))
            return &book->names[i];
    return NULL;
}

int oc_core_refresh_pivot(oc_core *core)
{
    const owf_defname *source_name, *config_name;
    owf_range range;
    owf_book *work;
    owf_calc *calc = NULL;
    unsigned row_col, value_col, out_rows = 0, out_cols = 0;
    int column, function, used = 0, source_index, output_index, rc;
    if (!core)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    output_index = core->sheet;
    source_name = pivot_name_at(core->book, output_index, PIVOT_SOURCE_NAME);
    config_name = pivot_name_at(core->book, output_index, PIVOT_CONFIG_NAME);
    if (!source_name || !config_name)
        return OWF_ERR_UNSUPPORTED;
    if (!oc_sheet_range(core->book, source_name->formula, output_index, &source_index, &range) ||
        sscanf(config_name->formula, "\"%u,%d,%u,%d\"%n",
               &row_col, &column, &value_col, &function, &used) != 4 ||
        config_name->formula[used] || column < -1 || source_index == output_index)
        return OWF_ERR_FORMAT;
    if (!pivot_args_ok(core->book, source_index, &range, row_col,
                       column < 0 ? OC_PIVOT_NO_COLUMN : (unsigned)column, value_col, function))
        return OWF_ERR_FORMAT;
    work = oc_book_copy(core->book);
    if (!work)
        return OWF_ERR_MEMORY;
    rc = pivot_transform(work, source_index, &range, row_col,
                         column < 0 ? OC_PIVOT_NO_COLUMN : (unsigned)column, value_col, function,
                         &output_index, &out_rows, &out_cols, &calc);
    if (rc == OWF_OK)
        rc = pivot_commit(core, work, calc, output_index, out_rows, out_cols);
    if (rc != OWF_OK) {
        owf_calc_free(calc);
        owf_book_free(work);
    }
    return rc;
}
