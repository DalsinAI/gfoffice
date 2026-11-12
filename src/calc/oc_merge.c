/*
 * GFOpenCalc's portable core: merge and unmerge cells.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

static int touches(const owf_range *a, const owf_range *b)
{
    return a->first_row <= b->last_row && a->last_row >= b->first_row &&
           a->first_col <= b->last_col && a->last_col >= b->first_col;
}

static int other_content(const owf_sheet *sheet, const owf_range *range)
{
    int i;
    for (i = 0; i < sheet->ncells; ++i) {
        const owf_cell *cell = &sheet->cells[i];
        if (cell->row < range->first_row || cell->row > range->last_row ||
            cell->col < range->first_col || cell->col > range->last_col ||
            (cell->row == range->first_row && cell->col == range->first_col))
            continue;
        if (!oc_cell_empty(cell)) return 1;
    }
    return 0;
}

static oc_undo *book_undo(oc_core *core)
{
    oc_undo *undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    return undo;
}

int oc_core_merge(oc_core *core, int merge, int discard)
{
    oc_sheetstate *state;
    owf_sheet *sheet;
    owf_range range;
    oc_undo *undo;
    int i, out, found = 0, rc = OWF_OK;
    if (!core) return OWF_ERR_FORMAT;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (oc_protected(core, 0)) return OC_PROTECTED;
    state = oc_state(core);
    if (state->sel.nranges != 1 || !oc_range_clip(core, &state->sel.ranges[0], &range))
        return OWF_ERR_UNSUPPORTED;
    if ((long)(range.last_row - range.first_row + 1) *
        (long)(range.last_col - range.first_col + 1) > OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    sheet = oc_sheet(core);
    if (merge) {
        unsigned row, col;
        if (range.first_row == range.last_row && range.first_col == range.last_col)
            return OWF_ERR_UNSUPPORTED;
        for (i = 0; i < sheet->nmerges; ++i)
            if (touches(&range, &sheet->merges[i])) return OWF_ERR_UNSUPPORTED;
        if (!discard && other_content(sheet, &range)) return OC_MERGE_CONTENT;
        undo = book_undo(core);
        if (owf_sheet_merge(sheet, &range) < 0) {
            oc_undo_free(undo);
            return OWF_ERR_MEMORY;
        }
        oc_undo_push(core, undo);
        for (row = range.first_row; row <= range.last_row && rc != OWF_ERR_MEMORY; ++row)
            for (col = range.first_col; col <= range.last_col; ++col) {
                const owf_cell *cell;
                if (row == range.first_row && col == range.first_col) continue;
                cell = oc_find(core, core->sheet, row, col);
                if (cell && !oc_cell_empty(cell) && owf_calc_enter(core->calc, core->sheet, row, col, "") == OWF_ERR_MEMORY) {
                    rc = OWF_ERR_MEMORY;
                    break;
                }
            }
    } else {
        for (i = 0; i < sheet->nmerges; ++i)
            if (touches(&range, &sheet->merges[i])) { found = 1; break; }
        if (!found) return OWF_ERR_UNSUPPORTED;
        undo = book_undo(core);
        oc_undo_push(core, undo);
        for (i = out = 0; i < sheet->nmerges; ++i) {
            if (touches(&range, &sheet->merges[i])) continue;
            if (out != i) sheet->merges[out] = sheet->merges[i];
            ++out;
        }
        sheet->nmerges = out;
    }
    oc_changed(core);
    if (oc_recalc(core, 0) == OWF_ERR_MEMORY) rc = OWF_ERR_MEMORY;
    return rc;
}
