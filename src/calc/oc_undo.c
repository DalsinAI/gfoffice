/*
 * OpenCalc's core: undo and redo. A step keeps what it is about to change:
 * the cells of some ranges (an edit, a fill, a paste, a style), a sheet's
 * column and row lists (widths, heights, frozen panes), or, for what moves
 * cells and references about (deleting rows, sheets), a copy of the whole
 * book. Undoing keeps the present state of the same things for redo first.
 * Bounded by OC_UNDO_LIMIT steps and OC_UNDO_BUDGET bytes, as OpenWrite's
 * undo is bounded by its 16 steps.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---- Cells ---- */

void oc_free_cell(owf_cell *cell)
{
    owf_free(cell->text);
    owf_free(cell->formula);
    cell->text = cell->formula = NULL;
}

int oc_copy_cell(owf_cell *dst, const owf_cell *src)
{
    *dst = *src;
    dst->text = dst->formula = NULL;
    if (src->text && !(dst->text = oc_strdup(src->text)))
        return 0;
    if (src->formula && !(dst->formula = oc_strdup(src->formula))) {
        owf_free(dst->text);
        dst->text = NULL;
        return 0;
    }
    return 1;
}

static long cell_bytes(const owf_cell *c)
{
    return (long)sizeof *c + (c->text ? (long)strlen(c->text) + 1 : 0) + (c->formula ? (long)strlen(c->formula) + 1 : 0);
}

static int first_at(const owf_sheet *s, unsigned row, unsigned col)
{
    int lo = 0, hi = s->ncells;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const owf_cell *c = &s->cells[mid];
        if (c->row < row || (c->row == row && c->col < col))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static int in_range(const owf_cell *c, const owf_range *r)
{
    return c->row >= r->first_row && c->row <= r->last_row && c->col >= r->first_col && c->col <= r->last_col;
}

int oc_remove_cells(owf_sheet *sheet, const owf_range *range)
{
    int i = first_at(sheet, range->first_row, 0), j = i, removed = 0;
    for (; i < sheet->ncells; i++) {
        owf_cell *c = &sheet->cells[i];
        if (in_range(c, range)) {
            oc_free_cell(c);
            removed++;
            continue;
        }
        if (j != i)
            sheet->cells[j] = *c;
        j++;
    }
    sheet->ncells = j;
    return removed;
}

/* ---- Copies of a book ---- */

static void *dup_array(const void *p, size_t n)
{
    void *q;
    if (!n)
        return NULL;
    q = owf_malloc(n);
    if (q)
        memcpy(q, p, n);
    return q;
}

owf_book *oc_book_copy(const owf_book *b)
{
    owf_book *c = owf_calloc(1, sizeof *c);
    int i, j;
    if (!c)
        return NULL;
    c->date1904 = b->date1904;
    c->active = b->active;
    if (owf_book_copy_parts(c, b) < 0)                   /* kept drawings, charts... */
        goto fail;
    if (b->nstyles && !(c->styles = dup_array(b->styles, (size_t)b->nstyles * sizeof *b->styles)))
        goto fail;
    c->nstyles = c->capstyles = b->nstyles;
    if (b->nnumfmts && !(c->numfmts = owf_calloc((size_t)b->nnumfmts, sizeof(char *))))
        goto fail;
    c->capnumfmts = b->nnumfmts;
    for (i = 0; i < b->nnumfmts; i++, c->nnumfmts++)
        if (!(c->numfmts[i] = oc_strdup(b->numfmts[i])))
            goto fail;
    if (b->nfonts && !(c->fonts = owf_calloc((size_t)b->nfonts, sizeof *c->fonts)))
        goto fail;
    c->capfonts = b->nfonts;
    for (i = 0; i < b->nfonts; i++, c->nfonts++) {
        c->fonts[i] = b->fonts[i];
        if (!(c->fonts[i].name = oc_strdup(b->fonts[i].name)))
            goto fail;
    }
    if (b->nnames && !(c->names = owf_calloc((size_t)b->nnames, sizeof *c->names)))
        goto fail;
    c->capnames = b->nnames;
    for (i = 0; i < b->nnames; i++, c->nnames++) {
        c->names[i] = b->names[i];
        c->names[i].name = oc_strdup(b->names[i].name);
        c->names[i].formula = oc_strdup(b->names[i].formula);
        if (!c->names[i].name || !c->names[i].formula) {
            c->nnames++;
            goto fail;
        }
    }
    if (b->nsheets && !(c->sheets = owf_calloc((size_t)b->nsheets, sizeof *c->sheets)))
        goto fail;
    c->capsheets = b->nsheets;
    for (i = 0; i < b->nsheets; i++, c->nsheets++) {
        const owf_sheet *s = &b->sheets[i];
        owf_sheet *d = &c->sheets[i];
        *d = *s;
        d->name = NULL;
        d->cells = NULL;
        d->cols = NULL;
        d->rows = NULL;
        d->merges = NULL;
        d->ncells = d->ncols = d->nrows = d->nmerges = 0;
        d->capcells = d->capcols = d->caprows = d->capmerges = 0;
        if (owf_sheet_copy_extras(d, s) < 0)                 /* conditional formats... (none on failure) */
            goto fail_sheet;
        if (!(d->name = oc_strdup(s->name)))
            goto fail_sheet;
        if (s->ncols && !(d->cols = dup_array(s->cols, (size_t)s->ncols * sizeof *s->cols)))
            goto fail_sheet;
        d->ncols = d->capcols = s->ncols;
        if (s->nrows && !(d->rows = dup_array(s->rows, (size_t)s->nrows * sizeof *s->rows)))
            goto fail_sheet;
        d->nrows = d->caprows = s->nrows;
        if (s->nmerges && !(d->merges = dup_array(s->merges, (size_t)s->nmerges * sizeof *s->merges)))
            goto fail_sheet;
        d->nmerges = d->capmerges = s->nmerges;
        if (s->ncells && !(d->cells = owf_calloc((size_t)s->ncells, sizeof *d->cells)))
            goto fail_sheet;
        d->capcells = s->ncells;
        for (j = 0; j < s->ncells; j++, d->ncells++)
            if (!oc_copy_cell(&d->cells[j], &s->cells[j]))
                goto fail_sheet;
    }
    return c;
fail_sheet:
    c->nsheets++;                                        /* so that the half-made sheet is freed too */
fail:
    owf_book_free(c);
    return NULL;
}

static long book_bytes(const owf_book *b)
{
    long n = (long)sizeof *b + (long)b->nstyles * (long)sizeof(owf_cellstyle);
    int i, j;
    for (i = 0; i < b->nsheets; i++) {
        const owf_sheet *s = &b->sheets[i];
        n += (long)sizeof *s + (long)s->ncols * (long)sizeof(owf_colinfo) + (long)s->nrows * (long)sizeof(owf_rowinfo);
        for (j = 0; j < s->ncells; j++)
            n += cell_bytes(&s->cells[j]);
    }
    for (i = 0; i < b->nnames; i++)
        n += (long)strlen(b->names[i].formula) + 32;
    for (i = 0; i < b->nparts; i++)
        n += (long)b->parts[i].length + 64;
    return n;
}

/* ---- Steps ---- */

oc_undo *oc_undo_new(oc_core *core, int kind)
{
    oc_undo *u = calloc(1, sizeof *u);
    if (!u)
        return NULL;
    u->kind = kind;
    u->sheet = core->sheet;
    u->state = *oc_state(core);
    u->dirty = core->dirty;
    u->bytes = (long)sizeof *u;
    return u;
}

static void free_save(oc_cellsave *s)
{
    int i;
    for (i = 0; i < s->ncells; i++)
        oc_free_cell(&s->cells[i]);
    free(s->cells);
    free(s->cols);
    free(s->rows);
}

void oc_undo_free(oc_undo *u)
{
    int i;
    if (!u)
        return;
    for (i = 0; i < u->nsaves; i++)
        free_save(&u->saves[i]);
    free(u->saves);
    free(u->cols);
    free(u->rows);
    owf_book_free(u->book);
    free(u->states);
    free(u);
}

int oc_undo_save_range(oc_core *core, oc_undo *u, int sheet, const owf_range *range, int with)
{
    const owf_sheet *s = &core->book->sheets[sheet];
    oc_cellsave *save, *p;
    int i, n = 0;
    for (i = first_at(s, range->first_row, 0); i < s->ncells && s->cells[i].row <= range->last_row; i++)
        if (in_range(&s->cells[i], range))
            n++;
    p = realloc(u->saves, (size_t)(u->nsaves + 1) * sizeof *p);
    if (!p)
        return OWF_ERR_MEMORY;
    u->saves = p;
    save = &p[u->nsaves];
    memset(save, 0, sizeof *save);
    save->sheet = sheet;
    save->range = *range;
    if (n && !(save->cells = calloc((size_t)n, sizeof *save->cells)))
        return OWF_ERR_MEMORY;
    for (i = first_at(s, range->first_row, 0); i < s->ncells && s->cells[i].row <= range->last_row; i++)
        if (in_range(&s->cells[i], range)) {
            if (!oc_copy_cell(&save->cells[save->ncells], &s->cells[i])) {
                free_save(save);
                return OWF_ERR_MEMORY;
            }
            u->bytes += cell_bytes(&s->cells[i]);
            save->ncells++;
        }
    save->has_cols = with;
    if (with & OC_SAVE_COLS) {
        if (s->ncols && !(save->cols = malloc((size_t)s->ncols * sizeof *s->cols))) {
            free_save(save);
            return OWF_ERR_MEMORY;
        }
        if (s->ncols)
            memcpy(save->cols, s->cols, (size_t)s->ncols * sizeof *s->cols);
        save->ncols = s->ncols;
        u->bytes += (long)s->ncols * (long)sizeof *s->cols;
    }
    if (with & OC_SAVE_ROWS) {
        if (s->nrows && !(save->rows = malloc((size_t)s->nrows * sizeof *s->rows))) {
            free_save(save);
            return OWF_ERR_MEMORY;
        }
        if (s->nrows)
            memcpy(save->rows, s->rows, (size_t)s->nrows * sizeof *s->rows);
        save->nrows = s->nrows;
        u->bytes += (long)s->nrows * (long)sizeof *s->rows;
    }
    u->nsaves++;
    return OWF_OK;
}

int oc_undo_save_layout(oc_core *core, oc_undo *u, int sheet)
{
    const owf_sheet *s = &core->book->sheets[sheet];
    u->lsheet = sheet;
    if (s->ncols && !(u->cols = malloc((size_t)s->ncols * sizeof *s->cols)))
        return OWF_ERR_MEMORY;
    if (s->nrows && !(u->rows = malloc((size_t)s->nrows * sizeof *s->rows)))
        return OWF_ERR_MEMORY;
    if (s->ncols)
        memcpy(u->cols, s->cols, (size_t)s->ncols * sizeof *s->cols);
    if (s->nrows)
        memcpy(u->rows, s->rows, (size_t)s->nrows * sizeof *s->rows);
    u->ncols = s->ncols;
    u->nrows = s->nrows;
    u->freeze_rows = s->freeze_rows;
    u->freeze_cols = s->freeze_cols;
    u->bytes += (long)s->ncols * (long)sizeof *s->cols + (long)s->nrows * (long)sizeof *s->rows;
    return OWF_OK;
}

int oc_undo_save_book(oc_core *core, oc_undo *u)
{
    long bytes = book_bytes(core->book);
    if (bytes > OC_UNDO_BUDGET)                          /* too big to keep: don't even copy it */
        return OWF_ERR_MEMORY;
    if (!(u->states = malloc((size_t)core->nstates * sizeof *u->states)))
        return OWF_ERR_MEMORY;
    memcpy(u->states, core->states, (size_t)core->nstates * sizeof *u->states);
    u->nstates = core->nstates;
    if (!(u->book = oc_book_copy(core->book)))
        return OWF_ERR_MEMORY;
    u->bytes += bytes;
    return OWF_OK;
}

static long stack_bytes(oc_undo **stack, int n)
{
    long b = 0;
    int i;
    for (i = 0; i < n; i++)
        b += stack[i]->bytes;
    return b;
}

static void drop_oldest(oc_undo **stack, int *n)
{
    oc_undo_free(stack[0]);
    memmove(stack, stack + 1, (size_t)(*n - 1) * sizeof *stack);
    (*n)--;
}

static void stack_clear(oc_undo **stack, int *n)
{
    int i;
    for (i = 0; i < *n; i++)
        oc_undo_free(stack[i]);
    *n = 0;
}

void oc_undo_clear(oc_core *core)
{
    stack_clear(core->undo, &core->nundo);
    stack_clear(core->redo, &core->nredo);
}

/* Keeps the budget: the steps furthest from now go first (redo's, then
 * undo's oldest), never the last of each. */
static void keep_budget(oc_core *core)
{
    while (stack_bytes(core->undo, core->nundo) + stack_bytes(core->redo, core->nredo) > OC_UNDO_BUDGET) {
        if (core->nredo > 1)
            drop_oldest(core->redo, &core->nredo);
        else if (core->nundo > 1)
            drop_oldest(core->undo, &core->nundo);
        else
            break;
    }
}

/* A new step (NULL: it could not be kept, so nothing before it can be
 * undone either). */
void oc_undo_push(oc_core *core, oc_undo *u)
{
    stack_clear(core->redo, &core->nredo);
    if (!u || u->bytes > OC_UNDO_BUDGET) {
        oc_undo_free(u);
        stack_clear(core->undo, &core->nundo);
        core->undo_lost = 1;
        return;
    }
    if (core->nundo == OC_UNDO_LIMIT)
        drop_oldest(core->undo, &core->nundo);
    core->undo[core->nundo++] = u;
    core->undo_lost = 0;
    keep_budget(core);
}

int oc_core_can_undo(const oc_core *core)
{
    return core->nundo > 0;
}

int oc_core_can_redo(const oc_core *core)
{
    return core->nredo > 0;
}

int oc_core_undo_lost(const oc_core *core)
{
    return core->undo_lost;
}

/* The same things as u holds, as they are now: the step that goes back. */
static oc_undo *capture_like(oc_core *core, const oc_undo *u)
{
    oc_undo *r = oc_undo_new(core, u->kind);
    int i;
    if (!r)
        return NULL;
    for (i = 0; i < u->nsaves; i++)
        if (oc_undo_save_range(core, r, u->saves[i].sheet, &u->saves[i].range, u->saves[i].has_cols) != OWF_OK)
            goto fail;
    if (u->kind == OC_UNDO_LAYOUT && oc_undo_save_layout(core, r, u->lsheet) != OWF_OK)
        goto fail;
    if (u->kind == OC_UNDO_SHIFT) {
        r->shift_sheet = u->shift_sheet;
        r->shift_axis = u->shift_axis;
        r->shift_at = u->shift_at;
        r->shift_count = -u->shift_count;
    }
    /* BOOK: the present book is swapped into it while restoring */
    return r;
fail:
    oc_undo_free(r);
    return NULL;
}

static void restore(oc_core *core, oc_undo *u, oc_undo *back)
{
    int i, j;
    for (i = 0; i < u->nsaves; i++) {
        oc_cellsave *s = &u->saves[i];
        owf_sheet *sheet;
        if (s->sheet >= core->book->nsheets)
            continue;
        sheet = &core->book->sheets[s->sheet];
        oc_remove_cells(sheet, &s->range);
        for (j = 0; j < s->ncells; j++) {
            owf_cell *c = owf_sheet_cell(sheet, s->cells[j].row, s->cells[j].col, 1);
            if (!c)
                break;
            *c = s->cells[j];                            /* the step's strings move into the book */
            s->cells[j].text = s->cells[j].formula = NULL;
        }
        owf_calc_touch(core->calc, s->sheet, &s->range);   /* the engine takes the range again */
        if (s->has_cols & OC_SAVE_ROWS) {
            owf_rowinfo *rows = NULL;
            if (s->nrows && (rows = owf_malloc((size_t)s->nrows * sizeof *rows)))
                memcpy(rows, s->rows, (size_t)s->nrows * sizeof *rows);
            if (rows || !s->nrows) {
                owf_free(sheet->rows);
                sheet->rows = rows;
                sheet->nrows = sheet->caprows = s->nrows;
            }
        }
        if (s->has_cols & OC_SAVE_COLS) {
            owf_colinfo *cols = NULL;
            if (s->ncols && (cols = owf_malloc((size_t)s->ncols * sizeof *cols)))
                memcpy(cols, s->cols, (size_t)s->ncols * sizeof *cols);
            if (cols || !s->ncols) {
                owf_free(sheet->cols);
                sheet->cols = cols;
                sheet->ncols = sheet->capcols = s->ncols;
            }
        }
    }
    if (u->kind == OC_UNDO_LAYOUT && u->lsheet < core->book->nsheets) {
        owf_sheet *sheet = &core->book->sheets[u->lsheet];
        owf_colinfo *cols = u->ncols ? owf_malloc((size_t)u->ncols * sizeof *cols) : NULL;
        owf_rowinfo *rows = u->nrows ? owf_malloc((size_t)u->nrows * sizeof *rows) : NULL;
        if ((cols || !u->ncols) && (rows || !u->nrows)) {
            if (u->ncols)
                memcpy(cols, u->cols, (size_t)u->ncols * sizeof *cols);
            if (u->nrows)
                memcpy(rows, u->rows, (size_t)u->nrows * sizeof *rows);
            owf_free(sheet->cols);
            owf_free(sheet->rows);
            sheet->cols = cols;
            sheet->ncols = sheet->capcols = u->ncols;
            sheet->rows = rows;
            sheet->nrows = sheet->caprows = u->nrows;
        } else {
            owf_free(cols);
            owf_free(rows);
        }
        sheet->freeze_rows = u->freeze_rows;
        sheet->freeze_cols = u->freeze_cols;
    }
    if (u->kind == OC_UNDO_SHIFT && u->shift_sheet < core->book->nsheets)
        owf_calc_shift(core->calc, u->shift_sheet, u->shift_axis, u->shift_at, u->shift_count);
    if (u->kind == OC_UNDO_BOOK && u->book) {
        owf_book t = *core->book;
        *core->book = *u->book;
        *u->book = t;
        back->book = u->book;                            /* what was there goes to the other stack */
        back->bytes += book_bytes(back->book);
        u->book = NULL;
        if (u->states) {
            back->states = core->states;
            back->nstates = core->nstates;
            core->states = u->states;
            core->nstates = u->nstates;
            u->states = NULL;
        }
    }
}

/* Undo (from undo to redo) or redo. */
static int step_back(oc_core *core, oc_undo **from, int *nfrom, oc_undo **to, int *nto)
{
    oc_undo *u, *back;
    int sheets, whole;
    if (!*nfrom)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_cancel(core);
    u = from[*nfrom - 1];
    back = capture_like(core, u);
    if (!back)
        return OWF_ERR_MEMORY;
    (*nfrom)--;
    whole = u->kind == OC_UNDO_BOOK;
    restore(core, u, back);
    /* the sheets may have come or gone: a cursor for each (the step knows the one it left) */
    sheets = core->book->nsheets;
    if (sheets != core->nstates) {
        oc_sheetstate *p = realloc(core->states, (size_t)sheets * sizeof *p);
        if (p) {
            int i;
            for (i = core->nstates; i < sheets; i++) {
                memset(&p[i], 0, sizeof p[i]);
                p[i].sel.nranges = 1;
                p[i].tab_col = -1;
                p[i].top = core->book->sheets[i].freeze_rows;
                p[i].left = core->book->sheets[i].freeze_cols;
            }
            core->states = p;
            core->nstates = sheets;
        } else if (sheets < core->nstates)
            core->nstates = sheets;
    }
    core->sheet = u->sheet < core->nstates ? u->sheet : 0;
    core->states[core->sheet] = u->state;
    oc_undo_free(u);
    if (*nto == OC_UNDO_LIMIT)
        drop_oldest(to, nto);
    to[(*nto)++] = back;
    keep_budget(core);
    /* a whole book back: parsed again; cells back: touched as they came
       back; rows and columns back: owf_calc_shift parsed it again */
    if (whole)
        owf_calc_rebuild(core->calc);
    owf_calc_recalc(core->calc);
    core->dirty = 1;
    core->changes++;
    if (core->clip.valid)
        core->clip.cut = 0;
    return OWF_OK;
}

int oc_core_undo(oc_core *core)
{
    return step_back(core, core->undo, &core->nundo, core->redo, &core->nredo);
}

int oc_core_redo(oc_core *core)
{
    return step_back(core, core->redo, &core->nredo, core->undo, &core->nundo);
}
