/*
 * GFOpenCalc's portable core: one- and two-variable Multiple Operations.
 * Trial evaluation and output construction both happen off the live book.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <float.h>
#include <stdlib.h>

#define MULTIOP_MAX_TRIALS 65536u

static int finite_number(double value)
{
    return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

static int in_range(int sheet, unsigned row, unsigned col, int wanted_sheet, const owf_range *range)
{
    return sheet == wanted_sheet && row >= range->first_row && row <= range->last_row &&
           col >= range->first_col && col <= range->last_col;
}

int oc_core_multiple_operations(oc_core *core,
                                int formula_sheet, unsigned formula_row, unsigned formula_col,
                                int input_sheet, unsigned input_row, unsigned input_col)
{
    const oc_selection *selection;
    const owf_cell *formula_cell, *input_cell, *trial, *result;
    owf_range source, output, expanded;
    owf_book *scratch = NULL, *work = NULL, *old_book;
    owf_calc *scratch_calc = NULL, *work_calc = NULL, *old_calc;
    oc_undo *undo = NULL;
    double *values = NULL;
    unsigned count, i, row;
    int rc = OWF_ERR_FORMAT, sheet_index;
    char number[48];

    if (!core || formula_sheet < 0 || formula_sheet >= core->book->nsheets ||
        input_sheet < 0 || input_sheet >= core->book->nsheets ||
        formula_row >= OC_MAX_ROWS || formula_col >= OC_MAX_COLS ||
        input_row >= OC_MAX_ROWS || input_col >= OC_MAX_COLS ||
        (formula_sheet == input_sheet && formula_row == input_row && formula_col == input_col))
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    source = selection->ranges[0];
    if (source.first_col != source.last_col || source.last_col + 1u >= OC_MAX_COLS)
        return OWF_ERR_FORMAT;
    count = source.last_row - source.first_row + 1u;
    if (!count || count > MULTIOP_MAX_TRIALS)
        return OWF_ERR_UNSUPPORTED;
    sheet_index = core->sheet;
    output = source;
    output.first_col = output.last_col = source.last_col + 1u;
    if (in_range(formula_sheet, formula_row, formula_col, sheet_index, &source) ||
        in_range(formula_sheet, formula_row, formula_col, sheet_index, &output) ||
        in_range(input_sheet, input_row, input_col, sheet_index, &source) ||
        in_range(input_sheet, input_row, input_col, sheet_index, &output))
        return OWF_ERR_FORMAT;
    formula_cell = owf_sheet_find(&core->book->sheets[formula_sheet], formula_row, formula_col);
    input_cell = owf_sheet_find(&core->book->sheets[input_sheet], input_row, input_col);
    if (!formula_cell || !formula_cell->formula ||
        (input_cell && (input_cell->formula ||
         (input_cell->type != OWF_VALUE_EMPTY && input_cell->type != OWF_VALUE_NUMBER))))
        return OWF_ERR_FORMAT;
    values = malloc((size_t)count * sizeof *values);
    if (!values)
        return OWF_ERR_MEMORY;
    scratch = oc_book_copy(core->book);
    if (!scratch) { rc = OWF_ERR_MEMORY; goto done; }
    scratch_calc = owf_calc_new(scratch);
    if (!scratch_calc) { rc = OWF_ERR_MEMORY; goto done; }
    rc = owf_calc_recalc_all(scratch_calc);
    if (rc != OWF_OK) goto done;
    for (i = 0, row = source.first_row; i < count; ++i, ++row) {
        trial = owf_sheet_find(&scratch->sheets[sheet_index], row, source.first_col);
        if (!trial || trial->type != OWF_VALUE_NUMBER || !finite_number(trial->number)) {
            rc = OWF_ERR_UNSUPPORTED;
            goto done;
        }
        owf_number_text(trial->number, number);
        rc = owf_calc_enter(scratch_calc, input_sheet, input_row, input_col, number);
        if (rc != OWF_OK) goto done;
        rc = owf_calc_recalc(scratch_calc);
        if (rc != OWF_OK) goto done;
        result = owf_sheet_find(&scratch->sheets[formula_sheet], formula_row, formula_col);
        if (!result || result->type != OWF_VALUE_NUMBER || !finite_number(result->number)) {
            rc = OWF_ERR_UNSUPPORTED;
            goto done;
        }
        values[i] = result->number;
    }

    work = oc_book_copy(core->book);
    if (!work) { rc = OWF_ERR_MEMORY; goto done; }
    work_calc = owf_calc_new(work);
    if (!work_calc) { rc = OWF_ERR_MEMORY; goto done; }
    for (i = 0, row = source.first_row; i < count; ++i, ++row) {
        owf_number_text(values[i], number);
        rc = owf_calc_enter(work_calc, sheet_index, row, output.first_col, number);
        if (rc != OWF_OK) goto done;
    }
    rc = owf_calc_recalc(work_calc);
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
    core->calc = work_calc;
    work = NULL;
    work_calc = NULL;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    undo = NULL;
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    expanded = source;
    expanded.last_col++;
    oc_core_select_range(core, &expanded);
    rc = OWF_OK;
done:
    oc_undo_free(undo);
    owf_calc_free(scratch_calc);
    owf_book_free(scratch);
    owf_calc_free(work_calc);
    owf_book_free(work);
    free(values);
    return rc;
}

int oc_core_multiple_operations_2d(oc_core *core,
                                   int formula_sheet, unsigned formula_row, unsigned formula_col,
                                   int row_input_sheet, unsigned row_input_row,
                                   unsigned row_input_col,
                                   int column_input_sheet, unsigned column_input_row,
                                   unsigned column_input_col)
{
    const oc_selection *selection;
    const owf_cell *formula_cell, *input_cell, *trial, *result;
    owf_range table;
    owf_book *scratch = NULL, *work = NULL, *old_book;
    owf_calc *scratch_calc = NULL, *work_calc = NULL, *old_calc;
    oc_undo *undo = NULL;
    double *row_values = NULL, *column_values = NULL, *results = NULL;
    unsigned rows, cols, row, col, r, c;
    size_t cells;
    int rc = OWF_ERR_FORMAT, sheet_index;
    char number[48];

    if (!core || formula_sheet < 0 || formula_sheet >= core->book->nsheets ||
        row_input_sheet < 0 || row_input_sheet >= core->book->nsheets ||
        column_input_sheet < 0 || column_input_sheet >= core->book->nsheets ||
        formula_row >= OC_MAX_ROWS || formula_col >= OC_MAX_COLS ||
        row_input_row >= OC_MAX_ROWS || row_input_col >= OC_MAX_COLS ||
        column_input_row >= OC_MAX_ROWS || column_input_col >= OC_MAX_COLS ||
        (formula_sheet == row_input_sheet && formula_row == row_input_row &&
         formula_col == row_input_col) ||
        (formula_sheet == column_input_sheet && formula_row == column_input_row &&
         formula_col == column_input_col) ||
        (row_input_sheet == column_input_sheet && row_input_row == column_input_row &&
         row_input_col == column_input_col))
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    table = selection->ranges[0];
    if (table.first_row == table.last_row || table.first_col == table.last_col)
        return OWF_ERR_FORMAT;
    rows = table.last_row - table.first_row;
    cols = table.last_col - table.first_col;
    if (rows > (unsigned long)OC_MAX_RANGE_CELLS / cols)
        return OWF_ERR_UNSUPPORTED;
    cells = (size_t)rows * (size_t)cols;
    if (!cells || cells > (size_t)OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    sheet_index = core->sheet;
    if (in_range(formula_sheet, formula_row, formula_col, sheet_index, &table) ||
        in_range(row_input_sheet, row_input_row, row_input_col, sheet_index, &table) ||
        in_range(column_input_sheet, column_input_row, column_input_col,
                 sheet_index, &table))
        return OWF_ERR_FORMAT;
    formula_cell = owf_sheet_find(&core->book->sheets[formula_sheet], formula_row, formula_col);
    if (!formula_cell || !formula_cell->formula)
        return OWF_ERR_FORMAT;
    input_cell = owf_sheet_find(&core->book->sheets[row_input_sheet],
                                row_input_row, row_input_col);
    if (input_cell && (input_cell->formula ||
        (input_cell->type != OWF_VALUE_EMPTY && input_cell->type != OWF_VALUE_NUMBER)))
        return OWF_ERR_FORMAT;
    input_cell = owf_sheet_find(&core->book->sheets[column_input_sheet],
                                column_input_row, column_input_col);
    if (input_cell && (input_cell->formula ||
        (input_cell->type != OWF_VALUE_EMPTY && input_cell->type != OWF_VALUE_NUMBER)))
        return OWF_ERR_FORMAT;

    row_values = malloc((size_t)cols * sizeof *row_values);
    column_values = malloc((size_t)rows * sizeof *column_values);
    results = malloc(cells * sizeof *results);
    if (!row_values || !column_values || !results) {
        rc = OWF_ERR_MEMORY;
        goto done;
    }
    scratch = oc_book_copy(core->book);
    if (!scratch) { rc = OWF_ERR_MEMORY; goto done; }
    scratch_calc = owf_calc_new(scratch);
    if (!scratch_calc) { rc = OWF_ERR_MEMORY; goto done; }
    rc = owf_calc_recalc_all(scratch_calc);
    if (rc != OWF_OK) goto done;
    for (c = 0, col = table.first_col + 1u; c < cols; ++c, ++col) {
        trial = owf_sheet_find(&scratch->sheets[sheet_index], table.first_row, col);
        if (!trial || trial->type != OWF_VALUE_NUMBER || !finite_number(trial->number)) {
            rc = OWF_ERR_UNSUPPORTED;
            goto done;
        }
        row_values[c] = trial->number;
    }
    for (r = 0, row = table.first_row + 1u; r < rows; ++r, ++row) {
        trial = owf_sheet_find(&scratch->sheets[sheet_index], row, table.first_col);
        if (!trial || trial->type != OWF_VALUE_NUMBER || !finite_number(trial->number)) {
            rc = OWF_ERR_UNSUPPORTED;
            goto done;
        }
        column_values[r] = trial->number;
    }
    for (r = 0; r < rows; ++r) {
        owf_number_text(column_values[r], number);
        rc = owf_calc_enter(scratch_calc, column_input_sheet, column_input_row,
                            column_input_col, number);
        if (rc != OWF_OK) goto done;
        for (c = 0; c < cols; ++c) {
            owf_number_text(row_values[c], number);
            rc = owf_calc_enter(scratch_calc, row_input_sheet, row_input_row,
                                row_input_col, number);
            if (rc != OWF_OK) goto done;
            rc = owf_calc_recalc(scratch_calc);
            if (rc != OWF_OK) goto done;
            result = owf_sheet_find(&scratch->sheets[formula_sheet],
                                    formula_row, formula_col);
            if (!result || result->type != OWF_VALUE_NUMBER ||
                !finite_number(result->number)) {
                rc = OWF_ERR_UNSUPPORTED;
                goto done;
            }
            results[(size_t)r * cols + c] = result->number;
        }
    }

    work = oc_book_copy(core->book);
    if (!work) { rc = OWF_ERR_MEMORY; goto done; }
    work_calc = owf_calc_new(work);
    if (!work_calc) { rc = OWF_ERR_MEMORY; goto done; }
    for (r = 0, row = table.first_row + 1u; r < rows; ++r, ++row)
        for (c = 0, col = table.first_col + 1u; c < cols; ++c, ++col) {
            owf_number_text(results[(size_t)r * cols + c], number);
            rc = owf_calc_enter(work_calc, sheet_index, row, col, number);
            if (rc != OWF_OK) goto done;
        }
    rc = owf_calc_recalc(work_calc);
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
    core->calc = work_calc;
    work = NULL;
    work_calc = NULL;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    undo = NULL;
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    oc_core_select_range(core, &table);
    rc = OWF_OK;
done:
    oc_undo_free(undo);
    owf_calc_free(scratch_calc);
    owf_book_free(scratch);
    owf_calc_free(work_calc);
    owf_book_free(work);
    free(row_values);
    free(column_values);
    free(results);
    return rc;
}

int oc_core_multiple_operations_multi(oc_core *core,
                                      int formula_sheet, const owf_range *formulas,
                                      int input_sheet, unsigned input_row,
                                      unsigned input_col, int trial_axis)
{
    const oc_selection *selection;
    const owf_cell *cell, *result;
    owf_range table;
    owf_book *scratch = NULL, *work = NULL, *old_book;
    owf_calc *scratch_calc = NULL, *work_calc = NULL, *old_calc;
    oc_undo *undo = NULL;
    double *trials = NULL, *results = NULL;
    unsigned trial_count, formula_count, t, f, row, col;
    size_t cells;
    int rc = OWF_ERR_FORMAT, sheet_index;
    char number[48];

    if (!core || !formulas || formula_sheet < 0 ||
        formula_sheet >= core->book->nsheets || input_sheet < 0 ||
        input_sheet >= core->book->nsheets || input_row >= OC_MAX_ROWS ||
        input_col >= OC_MAX_COLS || (trial_axis != OC_MULTIOP_TRIALS_COLUMN &&
        trial_axis != OC_MULTIOP_TRIALS_ROW))
        return OWF_ERR_FORMAT;
    if (formulas->last_row >= OC_MAX_ROWS || formulas->last_col >= OC_MAX_COLS)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    selection = &oc_state_const(core)->sel;
    if (selection->nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    table = selection->ranges[0];
    if (trial_axis == OC_MULTIOP_TRIALS_COLUMN) {
        if (formulas->first_row != formulas->last_row ||
            table.first_col == table.last_col)
            return OWF_ERR_FORMAT;
        formula_count = formulas->last_col - formulas->first_col + 1u;
        trial_count = table.last_row - table.first_row + 1u;
        if (table.last_col - table.first_col != formula_count)
            return OWF_ERR_FORMAT;
    } else {
        if (formulas->first_col != formulas->last_col ||
            table.first_row == table.last_row)
            return OWF_ERR_FORMAT;
        formula_count = formulas->last_row - formulas->first_row + 1u;
        trial_count = table.last_col - table.first_col + 1u;
        if (table.last_row - table.first_row != formula_count)
            return OWF_ERR_FORMAT;
    }
    if (!trial_count || !formula_count ||
        trial_count > (unsigned long)OC_MAX_RANGE_CELLS / formula_count)
        return OWF_ERR_UNSUPPORTED;
    cells = (size_t)trial_count * formula_count;
    sheet_index = core->sheet;
    if (in_range(input_sheet, input_row, input_col, sheet_index, &table) ||
        (formula_sheet == sheet_index &&
         !(formulas->last_row < table.first_row || formulas->first_row > table.last_row ||
           formulas->last_col < table.first_col || formulas->first_col > table.last_col)))
        return OWF_ERR_FORMAT;
    cell = owf_sheet_find(&core->book->sheets[input_sheet], input_row, input_col);
    if (cell && (cell->formula ||
        (cell->type != OWF_VALUE_EMPTY && cell->type != OWF_VALUE_NUMBER)))
        return OWF_ERR_FORMAT;
    for (f = 0; f < formula_count; ++f) {
        row = formulas->first_row + (trial_axis == OC_MULTIOP_TRIALS_ROW ? f : 0u);
        col = formulas->first_col + (trial_axis == OC_MULTIOP_TRIALS_COLUMN ? f : 0u);
        cell = owf_sheet_find(&core->book->sheets[formula_sheet], row, col);
        if (!cell || !cell->formula ||
            (formula_sheet == input_sheet && row == input_row && col == input_col))
            return OWF_ERR_FORMAT;
    }

    trials = malloc((size_t)trial_count * sizeof *trials);
    results = malloc(cells * sizeof *results);
    if (!trials || !results) { rc = OWF_ERR_MEMORY; goto done; }
    scratch = oc_book_copy(core->book);
    if (!scratch) { rc = OWF_ERR_MEMORY; goto done; }
    scratch_calc = owf_calc_new(scratch);
    if (!scratch_calc) { rc = OWF_ERR_MEMORY; goto done; }
    rc = owf_calc_recalc_all(scratch_calc);
    if (rc != OWF_OK) goto done;
    for (t = 0; t < trial_count; ++t) {
        row = table.first_row + (trial_axis == OC_MULTIOP_TRIALS_COLUMN ? t : 0u);
        col = table.first_col + (trial_axis == OC_MULTIOP_TRIALS_ROW ? t : 0u);
        cell = owf_sheet_find(&scratch->sheets[sheet_index], row, col);
        if (!cell || cell->type != OWF_VALUE_NUMBER || !finite_number(cell->number)) {
            rc = OWF_ERR_UNSUPPORTED;
            goto done;
        }
        trials[t] = cell->number;
    }
    for (t = 0; t < trial_count; ++t) {
        owf_number_text(trials[t], number);
        rc = owf_calc_enter(scratch_calc, input_sheet, input_row, input_col, number);
        if (rc != OWF_OK) goto done;
        rc = owf_calc_recalc(scratch_calc);
        if (rc != OWF_OK) goto done;
        for (f = 0; f < formula_count; ++f) {
            row = formulas->first_row + (trial_axis == OC_MULTIOP_TRIALS_ROW ? f : 0u);
            col = formulas->first_col + (trial_axis == OC_MULTIOP_TRIALS_COLUMN ? f : 0u);
            result = owf_sheet_find(&scratch->sheets[formula_sheet], row, col);
            if (!result || result->type != OWF_VALUE_NUMBER ||
                !finite_number(result->number)) {
                rc = OWF_ERR_UNSUPPORTED;
                goto done;
            }
            results[(size_t)t * formula_count + f] = result->number;
        }
    }

    work = oc_book_copy(core->book);
    if (!work) { rc = OWF_ERR_MEMORY; goto done; }
    work_calc = owf_calc_new(work);
    if (!work_calc) { rc = OWF_ERR_MEMORY; goto done; }
    for (t = 0; t < trial_count; ++t)
        for (f = 0; f < formula_count; ++f) {
            row = table.first_row + (trial_axis == OC_MULTIOP_TRIALS_COLUMN ? t : f + 1u);
            col = table.first_col + (trial_axis == OC_MULTIOP_TRIALS_COLUMN ? f + 1u : t);
            owf_number_text(results[(size_t)t * formula_count + f], number);
            rc = owf_calc_enter(work_calc, sheet_index, row, col, number);
            if (rc != OWF_OK) goto done;
        }
    rc = owf_calc_recalc(work_calc);
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
    core->calc = work_calc;
    work = NULL;
    work_calc = NULL;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    undo = NULL;
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    oc_core_select_range(core, &table);
    rc = OWF_OK;
done:
    oc_undo_free(undo);
    owf_calc_free(scratch_calc);
    owf_book_free(scratch);
    owf_calc_free(work_calc);
    owf_book_free(work);
    free(trials);
    free(results);
    return rc;
}
