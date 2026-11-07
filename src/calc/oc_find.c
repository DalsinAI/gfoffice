/* GFOpenCalc's portable core: find cells on a worksheet.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "oc_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int contains(const char *text, const char *needle, int match_case)
{
    size_t n = strlen(needle), i, j;
    if (!n) return 0;
    for (i = 0; text[i]; ++i) {
        for (j = 0; j < n && text[i + j]; ++j) {
            unsigned char a = (unsigned char)text[i + j], b = (unsigned char)needle[j];
            if (!match_case) { a = (unsigned char)tolower(a); b = (unsigned char)tolower(b); }
            if (a != b) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

static int after(const owf_cell *cell, unsigned row, unsigned col)
{
    return cell->row > row || (cell->row == row && cell->col > col);
}

int oc_core_find(oc_core *core, const char *needle, int match_case, int input, int backwards)
{
    oc_sheetstate *state;
    owf_sheet *sheet;
    int start, step, k;
    if (!core || !needle || !*needle) return OWF_ERR_FORMAT;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    state = oc_state(core);
    sheet = oc_sheet(core);
    if (!sheet->ncells) return OWF_ERR_UNSUPPORTED;
    for (start = 0; start < sheet->ncells && !after(&sheet->cells[start], state->sel.row, state->sel.col); ++start) {}
    if (backwards) start = start ? start - 1 : sheet->ncells - 1;
    else if (start == sheet->ncells) start = 0;
    step = backwards ? -1 : 1;
    for (k = 0; k < sheet->ncells; ++k) {
        int at = start + step * k;
        const owf_cell *cell;
        char shown[512], *typed = NULL;
        const char *text;
        while (at < 0) at += sheet->ncells;
        while (at >= sheet->ncells) at -= sheet->ncells;
        cell = &sheet->cells[at];
        if (oc_cell_empty(cell)) continue;
        if (input) {
            typed = oc_cell_input(core, core->sheet, cell->row, cell->col);
            if (!typed) return OWF_ERR_MEMORY;
            text = typed;
        } else {
            if (!oc_core_cell_text(core, core->sheet, cell->row, cell->col, shown, sizeof shown))
                continue;
            text = shown;
        }
        if (contains(text, needle, match_case)) {
            owf_free(typed);
            oc_collapse_to(core, cell->row, cell->col);
            oc_view_show_cell(core, cell->row, cell->col);
            return OWF_OK;
        }
        owf_free(typed);
    }
    return OWF_ERR_UNSUPPORTED;
}
