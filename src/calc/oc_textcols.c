/*
 * GFOpenCalc's portable core: Data > Text to Columns and Data > Remove
 * Duplicates. Both work on a private copy of the book through the formula
 * engine (owf_calc_enter, owf_calc_move_range), so references follow the
 * cells, and the whole change is one Undo step.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- commit a private book as one Undo step (as Subtotals does) ---- */

static void commit_book(oc_core *core, owf_book *work, owf_calc *calc, const owf_range *select)
{
    owf_book *old_book = core->book;
    owf_calc *old_calc = core->calc;
    oc_undo *undo = oc_undo_new(core, OC_UNDO_BOOK);

    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    oc_cf_forget(core);
    oc_charts_forget(core);
    core->book = work;
    core->calc = calc;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    oc_core_select_range(core, select);
}

/* ---- Text to Columns ---- */

/* The text a cell splits: its text, or its value as shown. */
static const char *cell_source(const owf_book *book, const owf_cell *cell, char *buf, size_t size)
{
    if (!cell || cell->type == OWF_VALUE_EMPTY)
        return NULL;
    if (cell->type == OWF_VALUE_TEXT)
        return cell->text ? cell->text : "";
    owf_calc_format(book, cell, buf, size);
    return buf;
}

static int is_delim(const oc_textcols_options *opt, char c)
{
    return ((opt->delimiters & OC_TEXTCOLS_TAB) && c == '\t') ||
           ((opt->delimiters & OC_TEXTCOLS_COMMA) && c == ',') ||
           ((opt->delimiters & OC_TEXTCOLS_SEMICOLON) && c == ';') ||
           ((opt->delimiters & OC_TEXTCOLS_SPACE) && c == ' ') ||
           ((opt->delimiters & OC_TEXTCOLS_OTHER) && opt->other && c == opt->other);
}

/* The next field of s from *pos into out (at least strlen(s) + 1 bytes):
 * 1 while there is one, 0 at the end. */
static int next_field(const oc_textcols_options *opt, const char *s, size_t *pos, int *started, char *out)
{
    size_t i = *pos, n = 0, len = strlen(s);

    if (opt->nbreaks) {                         /* fixed width */
        unsigned k;
        size_t from = 0, to = len;
        int field = *started;
        if ((unsigned)field > opt->nbreaks)
            return 0;
        if (field > 0)
            from = opt->breaks[field - 1];
        if ((unsigned)field < opt->nbreaks)
            to = opt->breaks[field];
        if (from > len)
            from = len;
        if (to > len)
            to = len;
        for (k = (unsigned)from; k < to; ++k)
            out[n++] = s[k];
        out[n] = 0;
        *started = field + 1;
        return 1;
    }
    if (*started) {
        if (i > len)
            return 0;
        if (opt->merge) {
            while (i < len && is_delim(opt, s[i]))
                ++i;
            if (i >= len)                       /* merged: nothing after the last */
                return 0;
        }
    }
    *started = 1;
    if (opt->quote && i < len && s[i] == opt->quote) {
        ++i;
        while (i < len) {
            if (s[i] == opt->quote) {
                if (i + 1 < len && s[i + 1] == opt->quote) {
                    out[n++] = opt->quote;
                    i += 2;
                    continue;
                }
                ++i;
                break;
            }
            out[n++] = s[i++];
        }
    }
    while (i < len && !is_delim(opt, s[i]))
        out[n++] = s[i++];
    out[n] = 0;
    *pos = i + 1;                               /* past the delimiter (or the end) */
    return 1;
}

int oc_core_text_to_columns(oc_core *core, const oc_textcols_options *opt, unsigned *ncols_out)
{
    const owf_range *range;
    owf_book *work = NULL;
    owf_calc *calc = NULL;
    char *field = NULL, *input = NULL, shown[128];
    size_t cap = 0;
    unsigned row, widest = 0;
    int rc = OWF_OK, sheet_index;

    if (ncols_out)
        *ncols_out = 0;
    if (!core || !opt || (!opt->nbreaks && !opt->delimiters) || opt->nbreaks > OC_TEXTCOLS_MAX)
        return OWF_ERR_FORMAT;
    if (opt->nbreaks) {
        unsigned k;
        for (k = 0; k < opt->nbreaks; ++k)
            if (!opt->breaks[k] || (k && opt->breaks[k] <= opt->breaks[k - 1]))
                return OWF_ERR_FORMAT;
    }
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (oc_state_const(core)->sel.nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    range = &oc_state_const(core)->sel.ranges[0];
    if (range->first_col != range->last_col)
        return OWF_ERR_UNSUPPORTED;
    sheet_index = core->sheet;

    work = oc_book_copy(core->book);
    if (!work)
        return OWF_ERR_MEMORY;
    calc = owf_calc_new(work);
    if (!calc) { rc = OWF_ERR_MEMORY; goto done; }
    for (row = range->first_row; row <= range->last_row; ++row) {
        const owf_cell *cell = owf_sheet_find(&core->book->sheets[sheet_index], row, range->first_col);
        const char *s = cell_source(core->book, cell, shown, sizeof shown);
        size_t len, pos = 0;
        int started = 0;
        unsigned field_no = 0, col = range->first_col;
        if (!s)
            continue;
        len = strlen(s);
        if (len + 3 > cap) {
            char *f = realloc(field, len + 3), *in;
            if (!f) { rc = OWF_ERR_MEMORY; goto done; }
            field = f;
            in = realloc(input, len + 3);
            if (!in) { rc = OWF_ERR_MEMORY; goto done; }
            input = in;
            cap = len + 3;
        }
        while (next_field(opt, s, &pos, &started, field)) {
            int kind = field_no < OC_TEXTCOLS_MAX ? opt->kind[field_no] : OC_TEXTCOLS_GENERAL;
            ++field_no;
            if (kind == OC_TEXTCOLS_SKIP)
                continue;
            if (col >= OC_MAX_COLS) { rc = OWF_ERR_FORMAT; goto done; }
            if (field[0] && (kind == OC_TEXTCOLS_TEXT || field[0] == '=' || field[0] == '\''))
                snprintf(input, cap, "'%s", field);       /* text, as it was */
            else
                memcpy(input, field, strlen(field) + 1);
            rc = owf_calc_enter(calc, sheet_index, row, col, input);
            if (rc != OWF_OK)
                goto done;
            ++col;
        }
        if (col - range->first_col > widest)
            widest = col - range->first_col;
    }
    rc = owf_calc_recalc(calc);
    if (rc != OWF_OK)
        goto done;
    {
        owf_range result = *range;
        if (widest > 1)
            result.last_col = range->first_col + widest - 1;
        commit_book(core, work, calc, &result);
        work = NULL;
        calc = NULL;
    }
    if (ncols_out)
        *ncols_out = widest;
done:
    owf_calc_free(calc);
    owf_book_free(work);
    free(field);
    free(input);
    return rc;
}

/* ---- Remove Duplicates ---- */

typedef struct {
    const owf_sheet *sheet;
    const unsigned *cols;
    unsigned ncols;
} dup_ctx;

static const dup_ctx *dup_sort_ctx;           /* qsort has no user pointer */

static int ci_compare(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb || !ca)
            return ca - cb;
    }
}

static int value_compare(const owf_cell *a, const owf_cell *b)
{
    int at = a ? (int)a->type : (int)OWF_VALUE_EMPTY;
    int bt = b ? (int)b->type : (int)OWF_VALUE_EMPTY;
    if (at != bt)
        return at < bt ? -1 : 1;
    switch (at) {
    case OWF_VALUE_NUMBER:
    case OWF_VALUE_BOOL: return a->number < b->number ? -1 : a->number > b->number;
    case OWF_VALUE_TEXT: return ci_compare(a->text ? a->text : "", b->text ? b->text : "");
    case OWF_VALUE_ERROR: return (int)a->error - (int)b->error;
    default: return 0;
    }
}

static int row_compare(unsigned ra, unsigned rb)
{
    const dup_ctx *ctx = dup_sort_ctx;
    unsigned k;
    for (k = 0; k < ctx->ncols; ++k) {
        int c = value_compare(owf_sheet_find(ctx->sheet, ra, ctx->cols[k]),
                              owf_sheet_find(ctx->sheet, rb, ctx->cols[k]));
        if (c)
            return c;
    }
    return 0;
}

static int row_sort(const void *pa, const void *pb)
{
    unsigned ra = *(const unsigned *)pa, rb = *(const unsigned *)pb;
    int c = row_compare(ra, rb);
    return c ? c : (ra < rb ? -1 : ra > rb);
}

int oc_core_remove_duplicates(oc_core *core, const unsigned *cols, unsigned ncols,
                              int has_header, unsigned *removed_out)
{
    const owf_range *range;
    owf_range sel;
    dup_ctx ctx;
    unsigned *rows = NULL, *all = NULL, first, n, i, removed = 0, shift = 0;
    unsigned char *dup = NULL;
    owf_book *work = NULL;
    owf_calc *calc = NULL;
    int rc = OWF_OK, sheet_index;

    if (removed_out)
        *removed_out = 0;
    if (!core || ncols > OC_MAX_COLS)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (oc_state_const(core)->sel.nranges != 1)
        return OWF_ERR_UNSUPPORTED;
    range = &oc_state_const(core)->sel.ranges[0];
    sel = *range;
    sheet_index = core->sheet;
    if (sel.last_row == OC_MAX_ROWS - 1 || sel.last_col == OC_MAX_COLS - 1) {
        /* a whole column or row: as far as the sheet is used */
        unsigned used_rows, used_cols;
        oc_used_range(&core->book->sheets[sheet_index], &used_rows, &used_cols);
        if (sel.last_row == OC_MAX_ROWS - 1)
            sel.last_row = used_rows ? used_rows - 1 : 0;
        if (sel.last_col == OC_MAX_COLS - 1)
            sel.last_col = used_cols ? used_cols - 1 : 0;
        if (sel.last_row < sel.first_row || sel.last_col < sel.first_col)
            return OWF_ERR_FORMAT;
    }
    for (i = 0; i < ncols; ++i)
        if (cols[i] < sel.first_col || cols[i] > sel.last_col)
            return OWF_ERR_FORMAT;
    first = sel.first_row + (has_header ? 1u : 0u);
    if (first > sel.last_row)
        return OWF_ERR_FORMAT;
    n = sel.last_row - first + 1;
    rows = malloc((size_t)n * sizeof *rows);
    dup = calloc((size_t)n, 1);
    if (!cols || !ncols) {
        ncols = sel.last_col - sel.first_col + 1;
        all = malloc((size_t)ncols * sizeof *all);
        if (all)
            for (i = 0; i < ncols; ++i)
                all[i] = sel.first_col + i;
        cols = all;
    }
    if (!rows || !dup || !cols) { rc = OWF_ERR_MEMORY; goto done; }
    for (i = 0; i < n; ++i)
        rows[i] = first + i;
    ctx.sheet = &core->book->sheets[sheet_index];
    ctx.cols = cols;
    ctx.ncols = ncols;
    dup_sort_ctx = &ctx;
    qsort(rows, n, sizeof *rows, row_sort);   /* equal rows together, the first first */
    for (i = 1; i < n; ++i)
        if (!row_compare(rows[i - 1], rows[i])) {
            dup[rows[i] - first] = 1;
            ++removed;
        }
    dup_sort_ctx = NULL;

    if (removed) {
        work = oc_book_copy(core->book);
        if (!work) { rc = OWF_ERR_MEMORY; goto done; }
        calc = owf_calc_new(work);
        if (!calc) { rc = OWF_ERR_MEMORY; goto done; }
        /* the rows kept move up over those removed, in runs, inside the
         * selection only; references to a removed row become #REF! */
        i = 0;
        while (i < n) {
            unsigned run;
            if (dup[i]) {
                ++shift;
                ++i;
                continue;
            }
            for (run = i; run + 1 < n && !dup[run + 1]; ++run)
                ;
            if (shift) {
                owf_range block;
                block.first_row = first + i;
                block.last_row = first + run;
                block.first_col = sel.first_col;
                block.last_col = sel.last_col;
                rc = owf_calc_move_range(calc, sheet_index, &block, sheet_index, -(int)shift, 0);
                if (rc != OWF_OK)
                    goto done;
            }
            i = run + 1;
        }
        {
            /* the rows below the kept ones: the moves emptied some, but
             * duplicates not moved over still hold their values */
            unsigned r, c;
            for (r = sel.last_row - removed + 1; r <= sel.last_row; ++r)
                for (c = sel.first_col; c <= sel.last_col; ++c)
                    if (owf_sheet_find(&work->sheets[sheet_index], r, c)) {
                        rc = owf_calc_enter(calc, sheet_index, r, c, "");
                        if (rc != OWF_OK)
                            goto done;
                    }
        }
        rc = owf_calc_recalc(calc);
        if (rc != OWF_OK)
            goto done;
        sel.last_row -= removed;
        commit_book(core, work, calc, &sel);
        work = NULL;
        calc = NULL;
    }
    if (removed_out)
        *removed_out = removed;
done:
    owf_calc_free(calc);
    owf_book_free(work);
    free(rows);
    free(dup);
    free(all);
    return rc;
}
