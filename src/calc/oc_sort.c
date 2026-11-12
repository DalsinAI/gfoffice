/*
 * GFOpenCalc's portable core: stable row sorting. The application supplies
 * the selected range, key and header choice; this file owns the workbook
 * mutation, formula relocation, comments and undo.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    oc_core *core;
    const oc_sort_key *keys;
    unsigned nkeys;
} sort_context;

/* Empty keys always follow nonempty ones. Numbers and booleans sort before
 * text, errors after it. This is deterministic and keeps equal rows stable. */
static int cell_rank(const owf_cell *c)
{
    if (!c || c->type == OWF_VALUE_EMPTY) return 4;
    if (c->type == OWF_VALUE_NUMBER || c->type == OWF_VALUE_BOOL) return 0;
    if (c->type == OWF_VALUE_TEXT) return 1;
    return 2;
}

static int text_compare(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)(a ? a : "");
    const unsigned char *q = (const unsigned char *)(b ? b : "");
    while (*p && *q) {
        int x = tolower(*p), y = tolower(*q);
        if (x != y) return x < y ? -1 : 1;
        ++p;
        ++q;
    }
    return *p == *q ? 0 : *p ? 1 : -1;
}

static int cell_compare(const sort_context *ctx, unsigned a, unsigned b, const oc_sort_key *key)
{
    const owf_cell *ca = oc_find(ctx->core, ctx->core->sheet, a, key->col);
    const owf_cell *cb = oc_find(ctx->core, ctx->core->sheet, b, key->col);
    int ra = cell_rank(ca), rb = cell_rank(cb), result = 0;
    if (ra == 4 || rb == 4) return ra == rb ? 0 : ra == 4 ? 1 : -1;
    if (ra != rb)
        result = ra < rb ? -1 : 1;
    else if (ra == 0) {
        double x = ca->number, y = cb->number;
        result = x < y ? -1 : x > y ? 1 : 0;
    } else if (ra == 1)
        result = text_compare(ca->text, cb->text);
    else {
        int x = ca ? ca->error : 0, y = cb ? cb->error : 0;
        result = x < y ? -1 : x > y ? 1 : 0;
    }
    return key->direction == OC_SORT_DESCENDING ? -result : result;
}

static int row_compare(const sort_context *ctx, unsigned a, unsigned b)
{
    unsigned i;
    for (i = 0; i < ctx->nkeys; ++i) {
        int result = cell_compare(ctx, a, b, &ctx->keys[i]);
        if (result) return result;
    }
    return 0;
}

static void merge_rows(const sort_context *ctx, unsigned *rows, unsigned *tmp,
                       size_t lo, size_t mid, size_t hi)
{
    size_t a = lo, b = mid, n = lo;
    while (a < mid && b < hi)
        tmp[n++] = row_compare(ctx, rows[a], rows[b]) <= 0 ? rows[a++] : rows[b++];
    while (a < mid) tmp[n++] = rows[a++];
    while (b < hi) tmp[n++] = rows[b++];
    for (n = lo; n < hi; ++n) rows[n] = tmp[n];
}

static void stable_sort_rows(const sort_context *ctx, unsigned *rows, unsigned *tmp,
                             size_t lo, size_t hi)
{
    size_t mid;
    if (hi - lo < 2) return;
    mid = lo + (hi - lo) / 2;
    stable_sort_rows(ctx, rows, tmp, lo, mid);
    stable_sort_rows(ctx, rows, tmp, mid, hi);
    merge_rows(ctx, rows, tmp, lo, mid, hi);
}

static int comment_order(const void *aa, const void *bb)
{
    const owf_comment *a = (const owf_comment *)aa, *b = (const owf_comment *)bb;
    if (a->row != b->row) return a->row < b->row ? -1 : 1;
    if (a->col != b->col) return a->col < b->col ? -1 : 1;
    return 0;
}

static int merge_touches(const owf_range *a, const owf_range *b)
{
    return a->first_row <= b->last_row && a->last_row >= b->first_row &&
           a->first_col <= b->last_col && a->last_col >= b->first_col;
}

int oc_core_sort_keys(oc_core *core, const oc_sort_key *keys, unsigned nkeys, int has_header)
{
    oc_sheetstate *st;
    owf_sheet *sheet;
    owf_range range;
    owf_cell *saved = NULL;
    unsigned *rows = NULL, *tmp = NULL, *destination = NULL;
    int *source_index = NULL;
    int nsaved = 0, i, rc = OWF_OK;
    unsigned first, count, k;
    long cells;
    oc_undo *undo;
    sort_context ctx;

    if (!core || !keys || !nkeys)
        return OWF_ERR_FORMAT;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (oc_protected(core, OWF_ALLOW_SORT) || oc_protected_selection(core)) return OC_PROTECTED;
    st = oc_state(core);
    if (st->sel.nranges != 1 || !oc_range_clip(core, &st->sel.ranges[0], &range))
        return OWF_ERR_UNSUPPORTED;
    if (nkeys > range.last_col - range.first_col + 1)
        return OWF_ERR_UNSUPPORTED;
    for (k = 0; k < nkeys; ++k) {
        unsigned j;
        if (keys[k].col < range.first_col || keys[k].col > range.last_col ||
            (keys[k].direction != OC_SORT_ASCENDING && keys[k].direction != OC_SORT_DESCENDING))
            return OWF_ERR_UNSUPPORTED;
        for (j = 0; j < k; ++j)
            if (keys[j].col == keys[k].col) return OWF_ERR_UNSUPPORTED;
    }
    first = range.first_row + (has_header ? 1u : 0u);
    if (first >= range.last_row) return OWF_ERR_UNSUPPORTED;
    count = range.last_row - first + 1;
    cells = (long)count * (long)(range.last_col - range.first_col + 1);
    if (cells > OC_MAX_RANGE_CELLS) return OWF_ERR_UNSUPPORTED;
    sheet = oc_sheet(core);
    for (i = 0; i < sheet->nmerges; ++i)
        if (merge_touches(&range, &sheet->merges[i])) return OWF_ERR_UNSUPPORTED;

    rows = (unsigned *)malloc((size_t)count * sizeof *rows);
    tmp = (unsigned *)malloc((size_t)count * sizeof *tmp);
    destination = (unsigned *)malloc((size_t)count * sizeof *destination);
    if (!rows || !tmp || !destination) { rc = OWF_ERR_MEMORY; goto done; }
    for (k = 0; k < count; ++k) rows[k] = first + k;
    ctx.core = core;
    ctx.keys = keys;
    ctx.nkeys = nkeys;
    stable_sort_rows(&ctx, rows, tmp, 0, count);
    for (k = 0; k < count; ++k) destination[rows[k] - first] = first + k;

    for (i = 0; i < sheet->ncells; ++i)
        if (sheet->cells[i].row >= first && sheet->cells[i].row <= range.last_row &&
            sheet->cells[i].col >= range.first_col && sheet->cells[i].col <= range.last_col)
            ++nsaved;
    if (nsaved) {
        int at = 0;
        saved = (owf_cell *)calloc((size_t)nsaved, sizeof *saved);
        source_index = (int *)malloc((size_t)nsaved * sizeof *source_index);
        if (!saved || !source_index) { rc = OWF_ERR_MEMORY; goto done; }
        for (i = 0; i < sheet->ncells; ++i) {
            const owf_cell *cell = &sheet->cells[i];
            int delta;
            char *moved;
            if (cell->row < first || cell->row > range.last_row ||
                cell->col < range.first_col || cell->col > range.last_col)
                continue;
            if (!oc_copy_cell(&saved[at], cell)) { rc = OWF_ERR_MEMORY; goto done; }
            source_index[at] = (int)(cell->row - first);
            delta = (int)destination[source_index[at]] - (int)cell->row;
            if (saved[at].formula && delta) {
                moved = owf_calc_move_formula(saved[at].formula, delta, 0);
                if (!moved) { rc = OWF_ERR_MEMORY; goto done; }
                owf_free(saved[at].formula);
                saved[at].formula = moved;
            }
            ++at;
        }
    }

    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    oc_undo_push(core, undo);
    {
        owf_range data = range;
        data.first_row = first;
        oc_remove_cells(sheet, &data);
    }
    for (i = 0; i < nsaved; ++i) {
        unsigned row = destination[source_index[i]];
        owf_cell *cell = owf_sheet_cell(sheet, row, saved[i].col, 1);
        if (!cell) { rc = OWF_ERR_MEMORY; break; } /* capacity was retained, but keep the API honest */
        oc_free_cell(cell);
        *cell = saved[i];
        cell->row = row;
        saved[i].text = saved[i].formula = NULL;
    }
    if (rc == OWF_OK) {
        for (i = 0; i < sheet->ncomments; ++i)
            if (sheet->comments[i].row >= first && sheet->comments[i].row <= range.last_row &&
                sheet->comments[i].col >= range.first_col && sheet->comments[i].col <= range.last_col)
                sheet->comments[i].row = destination[sheet->comments[i].row - first];
        if (sheet->ncomments > 1)
            qsort(sheet->comments, (size_t)sheet->ncomments, sizeof *sheet->comments, comment_order);
    }
    oc_charts_forget(core);
    oc_changed(core);
    if (oc_recalc(core, 1) == OWF_ERR_MEMORY) rc = OWF_ERR_MEMORY;

done:
    if (saved) {
        for (i = 0; i < nsaved; ++i) oc_free_cell(&saved[i]);
        free(saved);
    }
    free(source_index);
    free(destination);
    free(tmp);
    free(rows);
    return rc;
}

int oc_core_sort(oc_core *core, unsigned key_col, int direction, int has_header)
{
    oc_sort_key key;
    key.col = key_col;
    key.direction = direction;
    return oc_core_sort_keys(core, &key, 1, has_header);
}
