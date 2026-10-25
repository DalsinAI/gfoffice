/*
 * OpenCalc's core: the commands on the selection. Fill, insert and delete
 * rows and columns, clear, styles, column widths and row heights, hiding,
 * frozen panes. Each one is one undo step.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <stdlib.h>
#include <string.h>

const char *const oc_number_formats[] = {
    "General", "0", "0.00", "#,##0", "#,##0.00", "0%", "0.00%", "0.00E+00", "\xC2\xA3#,##0.00",
    "dd/mm/yyyy", "dd-mmm-yyyy", "hh:mm", "@", NULL
};
const char *const oc_number_format_names[] = {
    "General", "Number", "Number, 2 decimals", "Thousands", "Thousands, 2 decimals", "Percent",
    "Percent, 2 decimals", "Scientific", "Currency", "Date", "Date, month name", "Time", "Text", NULL
};

/* ---- Styles of columns and cells ---- */

int oc_col_style(const owf_sheet *sheet, unsigned col)
{
    int i;
    for (i = sheet->ncols - 1; i >= 0; i--)          /* the last that covers it wins */
        if (col >= sheet->cols[i].first && col <= sheet->cols[i].last)
            return sheet->cols[i].style;
    return 0;
}

const owf_cellstyle *oc_cell_style(const oc_core *core, const owf_cell *cell, unsigned col)
{
    int s = cell ? cell->style : oc_col_style(oc_sheet(core), col);
    if (s < 0 || s >= core->book->nstyles)
        s = 0;
    return &core->book->styles[s];
}

/* ---- Putting a cell ---- */

/* A cell's content and style as they are in src (NULL: empty), through the
 * engine: a formula (or `formula`, moved) as typed, a value as typed then
 * made exact. *rebuild is set when a value had to be set past the engine
 * (a number with more digits than typing keeps). */
int oc_put_cell(oc_core *core, int sheet, unsigned row, unsigned col, const owf_cell *src, const char *formula,
                const owf_cellstyle *style, int *rebuild)
{
    char num[40], *buf = NULL;
    const char *in = "";
    const char *f = formula ? formula : src ? src->formula : NULL;
    int rc, numeric = 0, sidx = 0;
    double number = 0;
    owf_cell *cell;
    if (f) {
        if (f[0] == '=')
            in = f;
        else {
            size_t n = strlen(f);
            if (!(buf = owf_malloc(n + 2)))
                return OWF_ERR_MEMORY;
            buf[0] = '=';
            memcpy(buf + 1, f, n + 1);
            in = buf;
        }
    } else if (src) {
        switch (src->type) {
        case OWF_VALUE_NUMBER:
            owf_number_text(src->number, num);
            in = num;
            numeric = 1;
            number = src->number;
            break;
        case OWF_VALUE_BOOL:
            in = src->number ? "TRUE" : "FALSE";
            break;
        case OWF_VALUE_ERROR:
            in = owf_error_name(src->error) ? owf_error_name(src->error) : "#N/A";
            break;
        case OWF_VALUE_TEXT: {
            size_t n = src->text ? strlen(src->text) : 0;
            if (!(buf = owf_malloc(n + 2)))
                return OWF_ERR_MEMORY;
            buf[0] = '\'';                           /* text stays text, whatever it looks like */
            if (n)
                memcpy(buf + 1, src->text, n);
            buf[n + 1] = 0;
            in = buf;
            break;
        }
        case OWF_VALUE_EMPTY:
            break;
        }
    }
    if (!f && (!src || src->type == OWF_VALUE_EMPTY) && oc_cell_empty(oc_find(core, sheet, row, col)))
        rc = OWF_OK;                                 /* empty onto empty: nothing for the engine */
    else
        rc = owf_calc_enter(core->calc, sheet, row, col, in);
    owf_free(buf);
    if (rc == OWF_ERR_MEMORY || rc == OWF_ERR_UNSUPPORTED)
        return rc;
    if (style) {
        owf_cellstyle copy = *style;                 /* the book's list may move as it grows */
        sidx = owf_book_style(core->book, &copy);
        if (sidx < 0)
            sidx = 0;
    }
    cell = owf_sheet_cell(&core->book->sheets[sheet], row, col, sidx != 0);
    if (cell) {
        cell->style = sidx;
        if (numeric && (cell->type != OWF_VALUE_NUMBER || cell->number != number)) {
            owf_cell_set_number(cell, number);
            *rebuild = 1;
        }
    }
    return rc;
}

/* ---- Counting and walking the selection ---- */

static long range_cells(const owf_range *r)
{
    return (long)(r->last_row - r->first_row + 1) * (long)(r->last_col - r->first_col + 1);
}

static int whole_cols(const owf_range *r)
{
    return r->first_row == 0 && r->last_row == OC_MAX_ROWS - 1;
}

static int whole_rows(const owf_range *r)
{
    return r->first_col == 0 && r->last_col == OC_MAX_COLS - 1;
}

/* The positions of the cells there are in a range (row, col pairs), so
 * that a command can change them while the sheet's array moves. */
static unsigned *cells_in(const owf_sheet *s, const owf_range *r, int *count)
{
    unsigned *pos;
    int i, n = 0, k = 0;
    *count = 0;
    for (i = 0; i < s->ncells; i++)
        if (s->cells[i].row >= r->first_row && s->cells[i].row <= r->last_row && s->cells[i].col >= r->first_col &&
            s->cells[i].col <= r->last_col)
            n++;
    if (!n)
        return NULL;
    pos = malloc((size_t)n * 2 * sizeof *pos);
    if (!pos) {
        *count = -1;
        return NULL;
    }
    for (i = 0; i < s->ncells; i++)
        if (s->cells[i].row >= r->first_row && s->cells[i].row <= r->last_row && s->cells[i].col >= r->first_col &&
            s->cells[i].col <= r->last_col) {
            pos[k++] = s->cells[i].row;
            pos[k++] = s->cells[i].col;
        }
    *count = n;
    return pos;
}

static void end_editing(oc_core *core)
{
    if (core->edit_mode)
        oc_edit_commit(core, OC_MOVE_NONE);
}

/* ---- Fill ---- */

int oc_core_fill(oc_core *core, int direction)
{
    oc_sheetstate *st = oc_state(core);
    owf_range targets[OC_MAX_RANGES];
    unsigned src[OC_MAX_RANGES];
    int n = 0, k, rc = OWF_OK, rebuild = 0, down = direction == OC_FILL_DOWN;
    long total = 0;
    oc_undo *u;
    end_editing(core);
    for (k = 0; k < st->sel.nranges; k++) {
        owf_range r, t;
        if (!oc_range_clip(core, &st->sel.ranges[k], &r))
            continue;
        t = r;
        if (down) {
            if (r.first_row == r.last_row) {             /* one row: from the row above */
                if (r.first_row == 0)
                    continue;
                src[n] = r.first_row - 1;
            } else {
                src[n] = r.first_row;
                t.first_row++;
            }
        } else {
            if (r.first_col == r.last_col) {
                if (r.first_col == 0)
                    continue;
                src[n] = r.first_col - 1;
            } else {
                src[n] = r.first_col;
                t.first_col++;
            }
        }
        targets[n] = t;
        total += range_cells(&t);
        n++;
    }
    if (!n)
        return OWF_ERR_UNSUPPORTED;
    if (total > OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    u = oc_undo_new(core, OC_UNDO_CELLS);
    for (k = 0; u && k < n; k++)
        if (oc_undo_save_range(core, u, core->sheet, &targets[k], 0) != OWF_OK) {
            oc_undo_free(u);
            u = NULL;
        }
    oc_undo_push(core, u);
    for (k = 0; k < n && rc != OWF_ERR_MEMORY; k++) {
        const owf_range *t = &targets[k];
        unsigned a, b;
        /* down: for each column, its source cell into the rows below; right: for each row */
        for (a = down ? t->first_col : t->first_row; a <= (down ? t->last_col : t->last_row) && rc != OWF_ERR_MEMORY; a++) {
            const owf_cell *found = down ? oc_find(core, core->sheet, src[k], a) : oc_find(core, core->sheet, a, src[k]);
            owf_cell cell;
            owf_cellstyle style;
            int have = 0;
            if (found) {
                if (!oc_copy_cell(&cell, found)) {
                    rc = OWF_ERR_MEMORY;
                    break;
                }
                have = 1;
                style = *oc_cell_style(core, found, down ? a : src[k]);
            }
            for (b = down ? t->first_row : t->first_col; b <= (down ? t->last_row : t->last_col); b++) {
                unsigned row = down ? b : a, col = down ? a : b;
                char *moved = NULL;
                int one;
                if (have && cell.formula) {
                    moved = owf_calc_move_formula(cell.formula, down ? (int)(row - src[k]) : 0, down ? 0 : (int)(col - src[k]));
                    if (!moved) {
                        rc = OWF_ERR_MEMORY;
                        break;
                    }
                }
                one = oc_put_cell(core, core->sheet, row, col, have ? &cell : NULL, moved, have ? &style : NULL, &rebuild);
                owf_free(moved);
                if (one == OWF_ERR_MEMORY) {
                    rc = one;
                    break;
                }
            }
            if (have)
                oc_free_cell(&cell);
        }
    }
    oc_changed(core);
    if (oc_recalc(core, rebuild) == OWF_ERR_MEMORY)
        rc = OWF_ERR_MEMORY;
    return rc;
}

/* ---- Inserting and deleting rows and columns ---- */

/* The rows (axis 0) or columns the selection's last range crosses. */
static void crossed(const oc_core *core, int axis, unsigned *at, unsigned *count)
{
    const oc_sheetstate *st = oc_state_const(core);
    const owf_range *r = &st->sel.ranges[st->sel.nranges - 1];
    *at = axis ? r->first_col : r->first_row;
    *count = (axis ? r->last_col - r->first_col : r->last_row - r->first_row) + 1;
}

int oc_core_insert(oc_core *core, int axis)
{
    unsigned at, count, rows, cols, max = axis ? OC_MAX_COLS : OC_MAX_ROWS;
    oc_undo *u;
    int rc;
    end_editing(core);
    crossed(core, axis, &at, &count);
    if (count >= max)
        return OWF_ERR_UNSUPPORTED;
    oc_used_range(oc_sheet(core), &rows, &cols);
    if ((axis ? cols : rows) + count > max)          /* cells would fall off the sheet, as Excel refuses */
        return OWF_ERR_FORMAT;
    u = oc_undo_new(core, OC_UNDO_SHIFT);
    if (u) {
        u->shift_sheet = core->sheet;
        u->shift_axis = axis;
        u->shift_at = at;
        u->shift_count = -(int)count;
    }
    rc = owf_calc_shift(core->calc, core->sheet, axis, at, (int)count);
    oc_charts_forget(core);                          /* their anchors and cells moved: read again */
    if (rc == OWF_ERR_MEMORY) {                      /* half moved, maybe: start again from the book */
        oc_undo_free(u);
        oc_undo_push(core, NULL);
        oc_recalc(core, 1);
        oc_changed(core);
        return rc;
    }
    if (rc != OWF_OK) {
        oc_undo_free(u);
        return rc;
    }
    oc_undo_push(core, u);
    oc_changed(core);
    return oc_recalc(core, 0);
}

int oc_core_delete(oc_core *core, int axis)
{
    unsigned at, count;
    oc_undo *u;
    int rc;
    end_editing(core);
    crossed(core, axis, &at, &count);
    u = oc_undo_new(core, OC_UNDO_BOOK);
    if (u && oc_undo_save_book(core, u) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    rc = owf_calc_shift(core->calc, core->sheet, axis, at, -(int)count);
    oc_charts_forget(core);
    if (rc == OWF_ERR_UNSUPPORTED) {
        oc_undo_free(u);
        return rc;
    }
    oc_undo_push(core, u);
    if (rc == OWF_ERR_MEMORY)
        oc_recalc(core, 1);
    else
        rc = oc_recalc(core, 0);
    oc_changed(core);
    {
        /* the cursor stays where it was, the sheet's end permitting */
        oc_sheetstate *st = oc_state(core);
        if (core->clip.valid)
            core->clip.cut = 0;
        oc_collapse_to(core, st->sel.row, st->sel.col);
    }
    return rc;
}

/* ---- Clearing ---- */

/* The column list as segments that don't overlap, sorted, the later of two
 * entries winning where they did, with columns first..last covered (gaps
 * filled with the sheet's defaults). OWF_OK or OWF_ERR_MEMORY. */
static int cols_split(owf_sheet *s, unsigned first, unsigned last)
{
    unsigned *b;
    owf_colinfo *out;
    int nb = 0, i, j, n = 0, cap = (s->ncols * 2 + 4) * 2;
    b = malloc((size_t)(s->ncols * 2 + 2) * sizeof *b);
    out = owf_malloc((size_t)cap * sizeof *out);
    if (!b || !out) {
        free(b);
        owf_free(out);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < s->ncols; i++) {
        b[nb++] = s->cols[i].first;
        b[nb++] = s->cols[i].last + 1;
    }
    b[nb++] = first;
    b[nb++] = last + 1;
    /* sort the edges (few: insertion sort), without repeats */
    for (i = 1; i < nb; i++) {
        unsigned v = b[i];
        for (j = i - 1; j >= 0 && b[j] > v; j--)
            b[j + 1] = b[j];
        b[j + 1] = v;
    }
    for (i = j = 0; i < nb; i++)
        if (!j || b[j - 1] != b[i])
            b[j++] = b[i];
    nb = j;
    for (i = 0; i + 1 < nb; i++) {
        unsigned lo = b[i], hi = b[i + 1] - 1;
        int k, win = -1;
        for (k = 0; k < s->ncols; k++)
            if (lo >= s->cols[k].first && lo <= s->cols[k].last)
                win = k;
        if (win >= 0) {
            out[n] = s->cols[win];
        } else if (lo >= first && hi <= last) {
            memset(&out[n], 0, sizeof out[n]);
            out[n].width = s->default_width;
        } else
            continue;
        out[n].first = lo;
        out[n].last = hi;
        n++;
    }
    free(b);
    owf_free(s->cols);
    s->cols = out;
    s->ncols = n;
    s->capcols = cap;
    return OWF_OK;
}

/* Joins neighbouring segments that say the same. */
static void cols_join(owf_sheet *s)
{
    int i, j = 0;
    for (i = 0; i < s->ncols; i++) {
        if (j && s->cols[j - 1].last + 1 == s->cols[i].first && s->cols[j - 1].width == s->cols[i].width &&
            s->cols[j - 1].style == s->cols[i].style && s->cols[j - 1].hidden == s->cols[i].hidden) {
            s->cols[j - 1].last = s->cols[i].last;
            continue;
        }
        s->cols[j++] = s->cols[i];
    }
    s->ncols = j;
}

int oc_core_clear(oc_core *core, unsigned what)
{
    oc_sheetstate *st = oc_state(core);
    owf_sheet *s;
    owf_range r[OC_MAX_RANGES];
    int n = 0, k, i, rc = OWF_OK;
    oc_undo *u;
    if (core->edit_mode)
        oc_edit_cancel(core);
    for (k = 0; k < st->sel.nranges; k++)                /* only cells there are change: no need to cut ranges down */
        r[n++] = st->sel.ranges[k];
    u = oc_undo_new(core, OC_UNDO_CELLS);
    for (k = 0; u && k < n; k++)
        if (oc_undo_save_range(core, u, core->sheet, &r[k], (what & OC_CLEAR_FORMATS) && whole_cols(&r[k])) != OWF_OK) {
            oc_undo_free(u);
            u = NULL;
        }
    oc_undo_push(core, u);
    for (k = 0; k < n && rc == OWF_OK; k++) {
        int count;
        unsigned *pos = cells_in(oc_sheet(core), &r[k], &count);
        if (count < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        for (i = 0; i < count; i++) {
            owf_cell *c;
            if (what & OC_CLEAR_CONTENTS) {
                const owf_cell *f = oc_find(core, core->sheet, pos[2 * i], pos[2 * i + 1]);
                if (!oc_cell_empty(f) && owf_calc_enter(core->calc, core->sheet, pos[2 * i], pos[2 * i + 1], "") == OWF_ERR_MEMORY)
                    rc = OWF_ERR_MEMORY;
            }
            if ((what & OC_CLEAR_FORMATS) && (c = owf_sheet_cell(oc_sheet(core), pos[2 * i], pos[2 * i + 1], 0)))
                c->style = 0;
        }
        free(pos);
        s = oc_sheet(core);
        if ((what & OC_CLEAR_FORMATS) && whole_cols(&r[k]) && cols_split(s, r[k].first_col, r[k].last_col) == OWF_OK) {
            for (i = 0; i < s->ncols; i++)
                if (s->cols[i].first >= r[k].first_col && s->cols[i].last <= r[k].last_col)
                    s->cols[i].style = 0;
            cols_join(s);
        }
    }
    oc_changed(core);
    if (oc_recalc(core, 0) == OWF_ERR_MEMORY)
        rc = OWF_ERR_MEMORY;
    return rc;
}

/* ---- Styles ---- */

typedef struct {
    int what;
    unsigned flags;
    int on, value, line;
    unsigned long colour;
    const owf_range *range;
} style_change;

enum { SC_FLAGS, SC_HALIGN, SC_VALIGN, SC_NUMFMT, SC_FILL, SC_COLOUR, SC_BORDERS };

static void change(owf_cellstyle *st, const style_change *sc, unsigned row, unsigned col)
{
    const owf_range *r = sc->range;
    int side;
    switch (sc->what) {
    case SC_FLAGS:
        if (sc->on)
            st->font.flags |= sc->flags;
        else
            st->font.flags &= ~sc->flags;
        break;
    case SC_HALIGN:
        st->halign = (unsigned char)sc->value;
        break;
    case SC_VALIGN:
        st->valign = (unsigned char)sc->value;
        break;
    case SC_NUMFMT:
        st->number_format = sc->value;
        break;
    case SC_FILL:
        st->fill = sc->colour;
        break;
    case SC_COLOUR:
        st->font.colour = sc->colour;
        break;
    case SC_BORDERS:
        for (side = 0; side < 4; side++) {
            int on;
            switch (sc->value) {
            case OC_BORDERS_ALL:
                on = 1;
                break;
            case OC_BORDERS_OUTLINE:
                on = (side == OWF_SIDE_TOP && row == r->first_row) || (side == OWF_SIDE_BOTTOM && row == r->last_row) ||
                     (side == OWF_SIDE_LEFT && col == r->first_col) || (side == OWF_SIDE_RIGHT && col == r->last_col);
                break;
            case OC_BORDERS_BOTTOM:
                on = side == OWF_SIDE_BOTTOM && row == r->last_row;
                break;
            default:
                on = 1;
                break;
            }
            if (!on)
                continue;
            st->border[side] = (unsigned char)(sc->value == OC_BORDERS_NONE ? OWF_LINE_NONE : sc->line);
            st->border_colour[side] = sc->value == OC_BORDERS_NONE ? 0 : sc->colour;
        }
        break;
    }
}

/* One cell's style changed (made when there was none: it starts from its
 * column's). 0 when memory ran out. */
static int change_cell(oc_core *core, unsigned row, unsigned col, const style_change *sc)
{
    owf_sheet *s = oc_sheet(core);
    owf_cell *c = owf_sheet_cell(s, row, col, 0);
    owf_cellstyle st = *oc_cell_style(core, c, col);
    int idx;
    change(&st, sc, row, col);
    if ((idx = owf_book_style(core->book, &st)) < 0)
        return 0;
    if (!c && !idx)
        return 1;
    if (!c && !(c = owf_sheet_cell(s, row, col, 1)))
        return 0;
    c->style = idx;
    return 1;
}

static int apply_style(oc_core *core, style_change *sc)
{
    oc_sheetstate *st = oc_state(core);
    owf_range r[OC_MAX_RANGES], cut;
    int n = 0, k, i, rc = OWF_OK;
    long total = 0;
    oc_undo *u;
    end_editing(core);
    for (k = 0; k < st->sel.nranges; k++) {
        r[n] = st->sel.ranges[k];
        if (!whole_cols(&r[n]) && !whole_rows(&r[n]))
            total += range_cells(&r[n]);
        n++;
    }
    if (total > OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    u = oc_undo_new(core, OC_UNDO_CELLS);
    for (k = 0; u && k < n; k++)
        if (oc_undo_save_range(core, u, core->sheet, &r[k], whole_cols(&r[k])) != OWF_OK) {
            oc_undo_free(u);
            u = NULL;
        }
    oc_undo_push(core, u);
    for (k = 0; k < n && rc == OWF_OK; k++) {
        sc->range = &r[k];
        if (whole_cols(&r[k]) || whole_rows(&r[k])) {
            /* whole rows or columns: the cells there are; whole columns: the columns' own style too
               (rows have none in the book) */
            int count;
            unsigned *pos = cells_in(oc_sheet(core), &r[k], &count);
            if (count < 0) {
                rc = OWF_ERR_MEMORY;
                break;
            }
            for (i = 0; i < count; i++)
                if (!change_cell(core, pos[2 * i], pos[2 * i + 1], sc))
                    rc = OWF_ERR_MEMORY;
            free(pos);
            if (whole_cols(&r[k])) {
                owf_sheet *s = oc_sheet(core);
                if (cols_split(s, r[k].first_col, r[k].last_col) != OWF_OK) {
                    rc = OWF_ERR_MEMORY;
                    break;
                }
                for (i = 0; i < s->ncols; i++)
                    if (s->cols[i].first >= r[k].first_col && s->cols[i].last <= r[k].last_col) {
                        owf_cellstyle cs = core->book->styles[s->cols[i].style >= 0 && s->cols[i].style < core->book->nstyles ? s->cols[i].style : 0];
                        int idx;
                        change(&cs, sc, 0, s->cols[i].first);
                        if ((idx = owf_book_style(core->book, &cs)) >= 0)
                            s->cols[i].style = idx;
                    }
                cols_join(s);
            }
        } else {
            unsigned row, col;
            cut = r[k];
            for (row = cut.first_row; row <= cut.last_row && rc == OWF_OK; row++)
                for (col = cut.first_col; col <= cut.last_col; col++)
                    if (!change_cell(core, row, col, sc)) {
                        rc = OWF_ERR_MEMORY;
                        break;
                    }
        }
    }
    oc_changed(core);
    return rc;
}

void oc_core_current_style(const oc_core *core, owf_cellstyle *out)
{
    const oc_sheetstate *st = oc_state_const(core);
    *out = *oc_cell_style(core, oc_find(core, core->sheet, st->sel.row, st->sel.col), st->sel.col);
}

int oc_core_toggle_flags(oc_core *core, unsigned flags)
{
    owf_cellstyle cur;
    style_change sc;
    memset(&sc, 0, sizeof sc);
    oc_core_current_style(core, &cur);
    sc.what = SC_FLAGS;
    sc.flags = flags;
    sc.on = (cur.font.flags & flags) != flags;
    return apply_style(core, &sc);
}

int oc_core_set_halign(oc_core *core, int halign)
{
    style_change sc;
    memset(&sc, 0, sizeof sc);
    sc.what = SC_HALIGN;
    sc.value = halign;
    return apply_style(core, &sc);
}

int oc_core_set_valign(oc_core *core, int valign)
{
    style_change sc;
    memset(&sc, 0, sizeof sc);
    sc.what = SC_VALIGN;
    sc.value = valign;
    return apply_style(core, &sc);
}

int oc_core_set_number_format(oc_core *core, const char *format)
{
    style_change sc;
    int idx = owf_book_numfmt(core->book, format);
    if (idx < 0)
        return OWF_ERR_MEMORY;
    memset(&sc, 0, sizeof sc);
    sc.what = SC_NUMFMT;
    sc.value = idx;
    return apply_style(core, &sc);
}

int oc_core_set_fill(oc_core *core, unsigned long fill)
{
    style_change sc;
    memset(&sc, 0, sizeof sc);
    sc.what = SC_FILL;
    sc.colour = fill;
    return apply_style(core, &sc);
}

int oc_core_set_font_colour(oc_core *core, unsigned long rgb)
{
    style_change sc;
    memset(&sc, 0, sizeof sc);
    sc.what = SC_COLOUR;
    sc.colour = rgb;
    return apply_style(core, &sc);
}

int oc_core_set_borders(oc_core *core, int where, int line, unsigned long colour)
{
    style_change sc;
    memset(&sc, 0, sizeof sc);
    sc.what = SC_BORDERS;
    sc.value = where;
    sc.line = line;
    sc.colour = colour;
    return apply_style(core, &sc);
}

/* ---- Column widths, row heights, hiding, frozen panes ---- */

int oc_core_col_width(const oc_core *core, unsigned col)
{
    return oc_col_twips(oc_sheet(core), col);
}

int oc_core_row_height(const oc_core *core, unsigned row)
{
    return oc_row_twips(oc_sheet(core), row);
}

static oc_undo *layout_step(oc_core *core)
{
    oc_undo *u = oc_undo_new(core, OC_UNDO_LAYOUT);
    if (u && oc_undo_save_layout(core, u, core->sheet) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    return u;
}

/* what: 1 width, 2 hidden */
static int cols_set(oc_core *core, unsigned first, unsigned last, int what, int width, int hidden)
{
    owf_sheet *s = oc_sheet(core);
    int i;
    oc_undo *u;
    if (first > last || last >= OC_MAX_COLS)
        return OWF_ERR_UNSUPPORTED;
    u = layout_step(core);
    if (cols_split(s, first, last) != OWF_OK) {
        oc_undo_free(u);
        return OWF_ERR_MEMORY;
    }
    oc_undo_push(core, u);
    for (i = 0; i < s->ncols; i++)
        if (s->cols[i].first >= first && s->cols[i].last <= last) {
            if (what & 1)
                s->cols[i].width = width > 0 ? width : s->default_width;
            if (what & 2)
                s->cols[i].hidden = hidden;
        }
    cols_join(s);
    oc_changed(core);
    return OWF_OK;
}

static const owf_rowinfo *row_info(const owf_sheet *s, unsigned row)
{
    int lo = 0, hi = s->nrows;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->rows[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < s->nrows && s->rows[lo].row == row ? &s->rows[lo] : NULL;
}

static int rows_set(oc_core *core, unsigned first, unsigned last, int what, int height, int hidden)
{
    owf_sheet *s = oc_sheet(core);
    unsigned row, rows, cols;
    oc_undo *u;
    int rc = OWF_OK;
    if (first > last || last >= OC_MAX_ROWS)
        return OWF_ERR_UNSUPPORTED;
    if (first == 0 && last == OC_MAX_ROWS - 1) {     /* every row: as far as the sheet is used */
        oc_used_range(s, &rows, &cols);
        last = rows ? rows - 1 : 0;
    }
    if ((long)(last - first) > OC_MAX_RANGE_CELLS)
        return OWF_ERR_UNSUPPORTED;
    u = layout_step(core);
    oc_undo_push(core, u);
    for (row = first; row <= last; row++) {
        const owf_rowinfo *ri = row_info(s, row);
        int h = (what & 1) ? (height > 0 ? height : s->default_height) : ri && ri->height > 0 ? ri->height : s->default_height;
        int hid = (what & 2) ? hidden : ri ? ri->hidden : 0;
        if (owf_sheet_set_row(s, row, h, hid) < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
    }
    oc_changed(core);
    return rc;
}

int oc_core_set_cols_width(oc_core *core, unsigned first, unsigned last, int twips)
{
    end_editing(core);
    return cols_set(core, first, last, 1, twips, 0);
}

int oc_core_set_rows_height(oc_core *core, unsigned first, unsigned last, int twips)
{
    end_editing(core);
    return rows_set(core, first, last, 1, twips, 0);
}

int oc_core_set_col_width(oc_core *core, int twips)
{
    unsigned at, count;
    end_editing(core);
    crossed(core, 1, &at, &count);
    return cols_set(core, at, at + count - 1, 1, twips, 0);
}

int oc_core_set_row_height(oc_core *core, int twips)
{
    unsigned at, count;
    end_editing(core);
    crossed(core, 0, &at, &count);
    return rows_set(core, at, at + count - 1, 1, twips, 0);
}

int oc_core_hide(oc_core *core, int axis, int hide)
{
    unsigned at, count;
    end_editing(core);
    crossed(core, axis, &at, &count);
    if (axis)
        return cols_set(core, at, at + count - 1, 2, 0, hide);
    return rows_set(core, at, at + count - 1, 2, 0, hide);
}

int oc_core_freeze(oc_core *core)
{
    owf_sheet *s = oc_sheet(core);
    oc_sheetstate *st = oc_state(core);
    end_editing(core);
    oc_undo_push(core, layout_step(core));
    if (s->freeze_rows || s->freeze_cols) {
        s->freeze_rows = s->freeze_cols = 0;
    } else {
        s->freeze_rows = st->sel.row;
        s->freeze_cols = st->sel.col;
    }
    if (st->top < s->freeze_rows)
        st->top = s->freeze_rows;
    if (st->left < s->freeze_cols)
        st->left = s->freeze_cols;
    oc_changed(core);
    return OWF_OK;
}
