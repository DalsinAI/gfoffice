/*
 * GFOpenCalc's portable core: Data > Subtotals. Existing SUBTOTAL formula
 * semantics do the arithmetic; this file owns the grouped-row authoring,
 * atomic private-workbook transaction and one-step Undo.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned first, last;
    char label[96];
} subtotal_group;

static int same_group(const owf_cell *a, const owf_cell *b)
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

static void group_label(const owf_book *book, const owf_cell *cell, char *out, size_t size)
{
    char value[72];
    if (!cell || cell->type == OWF_VALUE_EMPTY)
        snprintf(value, sizeof value, "(blank)");
    else
        owf_calc_format(book, cell, value, sizeof value);
    snprintf(out, size, "%s Total", value);
}

static int bold_cell(owf_book *book, owf_sheet *sheet, unsigned row, unsigned col)
{
    owf_cell *cell = owf_sheet_cell(sheet, row, col, 0);
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
    cell = owf_sheet_cell(sheet, row, col, 0);
    if (!cell)
        return OWF_ERR_MEMORY;
    cell->style = index;
    return OWF_OK;
}

static int put_subtotal(owf_calc *calc, owf_book *book, int sheet_index,
                        unsigned row, unsigned group_col, const char *label,
                        const unsigned *value_cols, unsigned nvalue_cols,
                        int function, unsigned first, unsigned last)
{
    owf_sheet *sheet = &book->sheets[sheet_index];
    char formula[80], a[8], b[8];
    unsigned i;
    int rc = owf_calc_enter(calc, sheet_index, row, group_col, label);
    if (rc != OWF_OK)
        return rc;
    if ((rc = bold_cell(book, sheet, row, group_col)) != OWF_OK)
        return rc;
    for (i = 0; i < nvalue_cols; ++i) {
        oc_col_name(value_cols[i], a);
        oc_col_name(value_cols[i], b);
        snprintf(formula, sizeof formula, "=SUBTOTAL(%d,%s%u:%s%u)",
                 function, a, first + 1, b, last + 1);
        rc = owf_calc_enter(calc, sheet_index, row, value_cols[i], formula);
        if (rc != OWF_OK)
            return rc;
        if ((rc = bold_cell(book, sheet, row, value_cols[i])) != OWF_OK)
            return rc;
    }
    return OWF_OK;
}

int oc_core_subtotals(oc_core *core, unsigned group_col,
                      const unsigned *value_cols, unsigned nvalue_cols,
                      int function, int has_header, int grand_total)
{
    const oc_selection *selection;
    const owf_range *range;
    subtotal_group *groups = NULL;
    owf_book *work = NULL, *old_book;
    owf_calc *calc = NULL, *old_calc;
    oc_undo *undo = NULL;
    unsigned data_first, row, i, ngroups = 0, used_rows, used_cols;
    int rc = OWF_ERR_FORMAT, sheet_index;

    if (!core || !value_cols || !nvalue_cols || nvalue_cols > 64 ||
        function < OC_SUBTOTAL_AVERAGE || function > OC_SUBTOTAL_VARP)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    range = &selection->ranges[0];
    data_first = range->first_row + (has_header ? 1u : 0u);
    if (data_first > range->last_row || group_col < range->first_col || group_col > range->last_col ||
        range->last_row >= OC_MAX_ROWS - 1)
        return OWF_ERR_FORMAT;
    for (i = 0; i < nvalue_cols; ++i) {
        unsigned j;
        if (value_cols[i] < range->first_col || value_cols[i] > range->last_col || value_cols[i] == group_col)
            return OWF_ERR_FORMAT;
        for (j = 0; j < i; ++j)
            if (value_cols[j] == value_cols[i])
                return OWF_ERR_FORMAT;
    }
    groups = calloc((size_t)(range->last_row - data_first + 1), sizeof *groups);
    if (!groups)
        return OWF_ERR_MEMORY;
    sheet_index = core->sheet;
    groups[0].first = data_first;
    for (row = data_first + 1; row <= range->last_row; ++row)
        if (!same_group(owf_sheet_find(&core->book->sheets[sheet_index], row - 1, group_col),
                        owf_sheet_find(&core->book->sheets[sheet_index], row, group_col))) {
            groups[ngroups].last = row - 1;
            group_label(core->book,
                        owf_sheet_find(&core->book->sheets[sheet_index], groups[ngroups].first, group_col),
                        groups[ngroups].label, sizeof groups[ngroups].label);
            ++ngroups;
            groups[ngroups].first = row;
        }
    groups[ngroups].last = range->last_row;
    group_label(core->book,
                owf_sheet_find(&core->book->sheets[sheet_index], groups[ngroups].first, group_col),
                groups[ngroups].label, sizeof groups[ngroups].label);
    ++ngroups;
    oc_used_range(&core->book->sheets[sheet_index], &used_rows, &used_cols);
    (void)used_cols;
    if (used_rows + ngroups + (grand_total ? 1u : 0u) > OC_MAX_ROWS) {
        rc = OWF_ERR_FORMAT;
        goto done;
    }

    work = oc_book_copy(core->book);
    if (!work) { rc = OWF_ERR_MEMORY; goto done; }
    calc = owf_calc_new(work);
    if (!calc) { rc = OWF_ERR_MEMORY; goto done; }
    if (grand_total) {
        rc = owf_calc_shift(calc, sheet_index, 0, range->last_row + 1, 1);
        if (rc != OWF_OK) goto done;
    }
    for (i = ngroups; i-- > 0;) {
        unsigned at = groups[i].last + 1;
        rc = owf_calc_shift(calc, sheet_index, 0, at, 1);
        if (rc != OWF_OK) goto done;
        rc = put_subtotal(calc, work, sheet_index, at, group_col, groups[i].label,
                          value_cols, nvalue_cols, function, groups[i].first, groups[i].last);
        if (rc != OWF_OK) goto done;
    }
    if (grand_total) {
        unsigned grand_row = range->last_row + ngroups + 1;
        rc = put_subtotal(calc, work, sheet_index, grand_row, group_col, "Grand Total",
                          value_cols, nvalue_cols, function, data_first,
                          range->last_row + ngroups);
        if (rc != OWF_OK) goto done;
    }
    rc = owf_calc_recalc(calc);
    if (rc != OWF_OK) goto done;

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
    {
        owf_range expanded = *range;
        expanded.last_row += ngroups + (grand_total ? 1u : 0u);
        oc_core_select_range(core, &expanded);
    }
done:
    owf_calc_free(calc);
    owf_book_free(work);
    free(groups);
    return rc;
}
