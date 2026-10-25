/*
 * OpenCalc's core: the view. Which rows and columns show (frozen ones,
 * then from the scroll position), at a zoom, in pixels; scrolling; what is
 * under the pointer and what a click does; and the grid as a list of
 * things to draw, so that the window only paints: cell fills, the
 * selection, grid lines, borders, text placed and clipped (text running on
 * over empty neighbours as Excel's does), the row numbers and column
 * letters, the active cell, frozen pane lines and the text being typed.
 * Integer arithmetic only.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <stdlib.h>
#include <string.h>

#define PAD 2                       /* pixels between a cell's edge and its text, at 100% */
#define MAX_LINES 2048              /* rows or columns showing at most */
#define NO_TEXT ((size_t)-1)

/* ---- Sizes ---- */

int oc_px(const oc_core *core, int twips)
{
    long px;
    if (twips <= 0)
        return 0;
    px = ((long)twips * core->zoom + 750) / 1500;
    return px < 1 ? 1 : (int)px;
}

int oc_view_px_to_twips(const oc_core *core, int px)
{
    return (int)((long)px * 1500 / (core->zoom > 0 ? core->zoom : 100));
}

int oc_col_twips(const owf_sheet *sheet, unsigned col)
{
    int i;
    for (i = sheet->ncols - 1; i >= 0; i--)
        if (col >= sheet->cols[i].first && col <= sheet->cols[i].last) {
            if (sheet->cols[i].hidden)
                return 0;
            return sheet->cols[i].width > 0 ? sheet->cols[i].width : sheet->default_width;
        }
    return sheet->default_width;
}

int oc_row_twips(const owf_sheet *sheet, unsigned row)
{
    int lo = 0, hi = sheet->nrows;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sheet->rows[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < sheet->nrows && sheet->rows[lo].row == row) {
        if (sheet->rows[lo].hidden)
            return 0;
        return sheet->rows[lo].height > 0 ? sheet->rows[lo].height : sheet->default_height;
    }
    return sheet->default_height;
}

static int line_px(const oc_core *core, int axis, unsigned i)
{
    const owf_sheet *s = oc_sheet(core);
    return oc_px(core, axis ? oc_col_twips(s, i) : oc_row_twips(s, i));
}

/* The row numbers' bar: as wide as set (oc_view_set_headers), or, following the rows showing
 * (oc_view_set_row_numbers), as wide as the largest row number showing, at least two digits. Only the
 * rows' heights and the area's height decide which show, never this width. */
static int row_header(const oc_core *core)
{
    const owf_sheet *s;
    const oc_sheetstate *st;
    unsigned i, last = 0, frozen, n = 0;
    int pos, digits = 1, skipped = 0;
    if (core->rn_digit <= 0 || core->rowhdr <= 0)
        return core->rowhdr;
    s = oc_sheet(core);
    st = oc_state_const(core);
    frozen = s->freeze_rows;
    pos = core->colhdr;
    for (i = 0; i < frozen && i < OC_MAX_ROWS && pos < core->height; i++) {
        int sz = line_px(core, 0, i);
        if (sz) {
            last = i;
            pos += sz;
        }
    }
    for (i = st->top > frozen ? st->top : frozen; i < OC_MAX_ROWS && pos < core->height && n < MAX_LINES; i++) {
        int sz = line_px(core, 0, i);
        if (!sz) {
            if (++skipped > 65536)
                break;
            continue;
        }
        last = i;
        pos += sz;
        n++;
    }
    for (i = last + 1; i >= 10; i /= 10)
        digits++;
    if (digits < 2)
        digits = 2;
    return core->rn_pad + digits * core->rn_digit;
}

int oc_view_row_header(const oc_core *core)
{
    return row_header(core);
}

void oc_view_set_row_numbers(oc_core *core, int digit_width, int pad)
{
    core->rn_digit = digit_width > 0 ? digit_width : 0;
    core->rn_pad = pad > 0 ? pad : 0;
}

int oc_measure(const oc_core *core, const char *s, size_t n, const owf_cellstyle *style)
{
    size_t i, chars = 0;
    if (core->measure)
        return core->measure(core->measure_user, s, n, style ? style->font.flags : 0, style ? style->font.font : -1,
                             style ? style->font.size : 0);
    for (i = 0; i < n; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80)
            chars++;
    return (int)((long)chars * 7 * core->zoom / 100);
}

/* ---- What shows ---- */

/* The rows (axis 0) or columns showing, in order: the frozen ones, then
 * from the scroll position until the area ends. *split is where the
 * scrolling part begins (pixels). The count. */
static int lines(const oc_core *core, int axis, oc_line *out, int max, int *split)
{
    const owf_sheet *s = oc_sheet(core);
    const oc_sheetstate *st = oc_state_const(core);
    unsigned frozen = axis ? s->freeze_cols : s->freeze_rows, i, end = axis ? OC_MAX_COLS : OC_MAX_ROWS;
    int pos = axis ? row_header(core) : core->colhdr, limit = axis ? core->width : core->height, n = 0, skipped = 0;
    for (i = 0; i < frozen && i < end && pos < limit && n < max; i++) {
        int sz = line_px(core, axis, i);
        if (!sz)
            continue;
        out[n].index = i;
        out[n].pos = pos;
        out[n].size = sz;
        out[n].pane = 0;
        n++;
        pos += sz;
    }
    *split = pos;
    for (i = axis ? st->left : st->top; i < frozen; i++)
        ;
    for (; i < end && pos < limit && n < max; i++) {
        int sz = line_px(core, axis, i);
        if (!sz) {
            if (++skipped > 65536)                     /* a sheet of hidden rows: enough */
                break;
            continue;
        }
        out[n].index = i;
        out[n].pos = pos;
        out[n].size = sz;
        out[n].pane = 1;
        n++;
        pos += sz;
    }
    return n;
}

static int count_showing(const oc_core *core, int axis, int whole)
{
    oc_line *l = malloc(MAX_LINES * sizeof *l);
    int n, split, i, k = 0, limit = axis ? core->width : core->height;
    if (!l)
        return 1;
    n = lines(core, axis, l, MAX_LINES, &split);
    for (i = 0; i < n; i++)
        if (l[i].pane == 1 && (!whole || l[i].pos + l[i].size <= limit))
            k++;
    free(l);
    return k;
}

int oc_rows_showing(const oc_core *core, int whole)
{
    return count_showing(core, 0, whole);
}

int oc_cols_showing(const oc_core *core, int whole)
{
    return count_showing(core, 1, whole);
}

void oc_view_set_measure(oc_core *core, oc_measure_fn measure, void *user)
{
    core->measure = measure;
    core->measure_user = user;
}

void oc_view_set_size(oc_core *core, int width, int height)
{
    core->width = width > 0 ? width : 0;
    core->height = height > 0 ? height : 0;
}

void oc_view_set_headers(oc_core *core, int row_header_width, int col_header_height)
{
    core->rowhdr = row_header_width > 0 ? row_header_width : 0;
    core->colhdr = col_header_height > 0 ? col_header_height : 0;
}

void oc_view_set_zoom(oc_core *core, int percent)
{
    core->zoom = percent < 25 ? 25 : percent > 400 ? 400 : percent;
}

int oc_view_zoom(const oc_core *core)
{
    return core->zoom;
}

void oc_view_scroll_pos(const oc_core *core, unsigned *top, unsigned *left)
{
    const oc_sheetstate *st = oc_state_const(core);
    *top = st->top;
    *left = st->left;
}

void oc_view_scroll_to(oc_core *core, unsigned top, unsigned left)
{
    oc_sheetstate *st = oc_state(core);
    const owf_sheet *s = oc_sheet(core);
    if (top >= OC_MAX_ROWS)
        top = OC_MAX_ROWS - 1;
    if (left >= OC_MAX_COLS)
        left = OC_MAX_COLS - 1;
    st->top = top < s->freeze_rows ? s->freeze_rows : top;
    st->left = left < s->freeze_cols ? s->freeze_cols : left;
}

void oc_view_scroller(oc_core *core, int axis, unsigned *first, unsigned *visible, unsigned *total)
{
    const oc_sheetstate *st = oc_state_const(core);
    const owf_sheet *s = oc_sheet(core);
    unsigned rows, cols, used, pos, frozen = axis ? s->freeze_cols : s->freeze_rows, max = axis ? OC_MAX_COLS : OC_MAX_ROWS;
    unsigned vis = (unsigned)count_showing(core, axis, 1);
    if (!core->used_valid || core->used_stamp != core->changes || core->used_sheet != core->sheet) {
        oc_used_range(s, &core->used_rows, &core->used_cols);    /* only when the book changed */
        core->used_stamp = core->changes;
        core->used_sheet = core->sheet;
        core->used_valid = 1;
    }
    rows = core->used_rows;
    cols = core->used_cols;
    used = axis ? cols : rows;
    pos = axis ? st->left : st->top;
    if (vis < 1)
        vis = 1;
    {
        /* the used part or where the view is, and a screen more */
        unsigned span = (used > pos + vis ? used : pos + vis) + vis;
        if (span > max)
            span = max;
        *first = pos - frozen;
        *visible = vis;
        *total = span > frozen ? span - frozen : 1;
    }
}

/* Scrolls one axis so that index i shows (frozen ones always do). */
static void show_line(oc_core *core, int axis, unsigned i)
{
    oc_sheetstate *st = oc_state(core);
    const owf_sheet *s = oc_sheet(core);
    unsigned frozen = axis ? s->freeze_cols : s->freeze_rows, *top = axis ? &st->left : &st->top, k;
    int limit = axis ? core->width : core->height, start = axis ? row_header(core) : core->colhdr, avail, used = 0, steps = 0;
    if (i < frozen)
        return;
    for (k = 0; k < frozen; k++)
        start += line_px(core, axis, k);
    avail = limit - start;
    if (*top < frozen)
        *top = frozen;
    if (i < *top) {
        *top = i;
        return;
    }
    /* does it show from here? */
    if ((long)(i - *top) < (long)(avail > 0 ? avail : 0) + 1) {
        for (k = *top; k <= i; k++) {
            used += line_px(core, axis, k);
            if (used > avail)
                break;
        }
        if (k > i)
            return;
    }
    /* no: the top that has it as the last whole line */
    used = line_px(core, axis, i);
    for (k = i; k > frozen && steps < 65536; steps++) {
        int sz = line_px(core, axis, k - 1);
        if (used + sz > avail)
            break;
        used += sz;
        k--;
    }
    *top = k;
}

void oc_view_show_cell(oc_core *core, unsigned row, unsigned col)
{
    show_line(core, 0, row);
    show_line(core, 1, col);
}

void oc_view_follow(oc_core *core)
{
    const oc_sheetstate *st = oc_state_const(core);
    oc_view_show_cell(core, st->sel.row, st->sel.col);
}

/* ---- The pointer ---- */

static int find_line(const oc_line *l, int n, int p)
{
    int i;
    for (i = 0; i < n; i++)
        if (p >= l[i].pos && p < l[i].pos + l[i].size)
            return i;
    return -1;
}

int oc_view_hit(const oc_core *core, int x, int y, oc_hit *hit)
{
    oc_line *rl = malloc(MAX_LINES * sizeof *rl), *cl = malloc(MAX_LINES * sizeof *cl);
    int nr, nc, split, ri, ci, rowhdr = row_header(core);
    memset(hit, 0, sizeof *hit);
    if (!rl || !cl) {
        free(rl);
        free(cl);
        return OC_HIT_NONE;
    }
    nr = lines(core, 0, rl, MAX_LINES, &split);
    nc = lines(core, 1, cl, MAX_LINES, &split);
    if (x < 0 || y < 0 || x >= core->width || y >= core->height) {
        hit->kind = OC_HIT_NONE;
    } else if (x < rowhdr && y < core->colhdr) {
        hit->kind = OC_HIT_CORNER;
        hit->w = rowhdr;
        hit->h = core->colhdr;
    } else if (y < core->colhdr) {
        ci = find_line(cl, nc, x);
        if (ci >= 0) {
            hit->kind = OC_HIT_COL_HEADER;
            hit->col = cl[ci].index;
            hit->x = cl[ci].pos;
            hit->w = cl[ci].size;
            hit->h = core->colhdr;
            if (x >= cl[ci].pos + cl[ci].size - 3)
                hit->kind = OC_HIT_COL_EDGE;
            else if (x < cl[ci].pos + 3 && ci > 0 && cl[ci - 1].pos + cl[ci - 1].size == cl[ci].pos) {
                hit->kind = OC_HIT_COL_EDGE;
                hit->col = cl[ci - 1].index;
                hit->x = cl[ci - 1].pos;
                hit->w = cl[ci - 1].size;
            }
        }
    } else if (x < rowhdr) {
        ri = find_line(rl, nr, y);
        if (ri >= 0) {
            hit->kind = OC_HIT_ROW_HEADER;
            hit->row = rl[ri].index;
            hit->y = rl[ri].pos;
            hit->h = rl[ri].size;
            hit->w = rowhdr;
            if (y >= rl[ri].pos + rl[ri].size - 3)
                hit->kind = OC_HIT_ROW_EDGE;
            else if (y < rl[ri].pos + 2 && ri > 0 && rl[ri - 1].pos + rl[ri - 1].size == rl[ri].pos) {
                hit->kind = OC_HIT_ROW_EDGE;
                hit->row = rl[ri - 1].index;
                hit->y = rl[ri - 1].pos;
                hit->h = rl[ri - 1].size;
            }
        }
    } else {
        ri = find_line(rl, nr, y);
        ci = find_line(cl, nc, x);
        if (ri >= 0 && ci >= 0) {
            hit->kind = OC_HIT_CELL;
            hit->row = rl[ri].index;
            hit->col = cl[ci].index;
            hit->x = cl[ci].pos;
            hit->y = rl[ri].pos;
            hit->w = cl[ci].size;
            hit->h = rl[ri].size;
        }
    }
    free(rl);
    free(cl);
    return hit->kind;
}

/* The top row (left column) showing: where a click on a header puts the
 * active cell. */
static unsigned first_showing(const oc_core *core, int axis)
{
    const owf_sheet *s = oc_sheet(core);
    const oc_sheetstate *st = oc_state_const(core);
    unsigned i, frozen = axis ? s->freeze_cols : s->freeze_rows;
    for (i = 0; i < frozen; i++)
        if (line_px(core, axis, i))
            return i;
    return axis ? st->left : st->top;
}

static void header_select(oc_core *core, int axis, unsigned at, int extend, int add)
{
    oc_sheetstate *st = oc_state(core);
    owf_range *r;
    if (extend) {
        r = &st->sel.ranges[st->sel.nranges - 1];
        if (axis) {
            unsigned a = st->sel.anchor_col;
            r->first_col = a < at ? a : at;
            r->last_col = a < at ? at : a;
            r->first_row = 0;
            r->last_row = OC_MAX_ROWS - 1;
            st->sel.focus_col = at;
        } else {
            unsigned a = st->sel.anchor_row;
            r->first_row = a < at ? a : at;
            r->last_row = a < at ? at : a;
            r->first_col = 0;
            r->last_col = OC_MAX_COLS - 1;
            st->sel.focus_row = at;
        }
        return;
    }
    if (axis)
        oc_core_select_cell(core, first_showing(core, 0), at, 0, add);
    else
        oc_core_select_cell(core, at, first_showing(core, 1), 0, add);
    st = oc_state(core);
    r = &st->sel.ranges[st->sel.nranges - 1];
    if (axis) {
        r->first_col = r->last_col = at;
        r->first_row = 0;
        r->last_row = OC_MAX_ROWS - 1;
        st->sel.anchor_col = st->sel.focus_col = at;
    } else {
        r->first_row = r->last_row = at;
        r->first_col = 0;
        r->last_col = OC_MAX_COLS - 1;
        st->sel.anchor_row = st->sel.focus_row = at;
    }
}

int oc_core_press(oc_core *core, const oc_hit *hit, int extend, int add)
{
    core->dragging = 0;
    if (core->edit_mode) {
        if (hit->kind == OC_HIT_CELL && oc_edit_wants_ref(core)) {
            owf_range r;
            r.first_row = r.last_row = hit->row;
            r.first_col = r.last_col = hit->col;
            core->point = 0;                                 /* a click starts a new reference */
            core->dragging = -1;
            core->point_row = hit->row;
            core->point_col = hit->col;
            {
                int rc = oc_edit_insert_ref(core, &r);
                core->point_row = hit->row;
                core->point_col = hit->col;
                return rc;
            }
        }
        if (hit->kind == OC_HIT_COL_EDGE || hit->kind == OC_HIT_ROW_EDGE)
            return OWF_OK;
        {
            int rc = oc_edit_commit(core, OC_MOVE_NONE);
            if (rc == OWF_ERR_MEMORY)
                return rc;
        }
    }
    switch (hit->kind) {
    case OC_HIT_CELL:
        oc_core_select_cell(core, hit->row, hit->col, extend, add);
        core->dragging = OC_HIT_CELL;
        break;
    case OC_HIT_COL_HEADER:
        header_select(core, 1, hit->col, extend, add);
        core->dragging = OC_HIT_COL_HEADER;
        break;
    case OC_HIT_ROW_HEADER:
        header_select(core, 0, hit->row, extend, add);
        core->dragging = OC_HIT_ROW_HEADER;
        break;
    case OC_HIT_CORNER:
        oc_core_select_all(core);
        break;
    default:
        return OWF_ERR_UNSUPPORTED;
    }
    return OWF_OK;
}

int oc_core_drag(oc_core *core, const oc_hit *hit)
{
    oc_sheetstate *st = oc_state(core);
    if (core->dragging == -1 && core->edit_mode && (hit->kind == OC_HIT_CELL)) {
        owf_range r;
        unsigned ar = core->point_row, ac = core->point_col;
        r.first_row = ar < hit->row ? ar : hit->row;
        r.last_row = ar < hit->row ? hit->row : ar;
        r.first_col = ac < hit->col ? ac : hit->col;
        r.last_col = ac < hit->col ? hit->col : ac;
        oc_edit_insert_ref(core, &r);
        core->point_row = ar;                                /* the reference grows from where the drag began */
        core->point_col = ac;
        return OWF_OK;
    }
    switch (core->dragging) {
    case OC_HIT_CELL:
        if (hit->kind == OC_HIT_CELL || hit->kind == OC_HIT_ROW_HEADER || hit->kind == OC_HIT_COL_HEADER) {
            unsigned row = hit->kind == OC_HIT_COL_HEADER ? st->sel.focus_row : hit->row;
            unsigned col = hit->kind == OC_HIT_ROW_HEADER ? st->sel.focus_col : hit->col;
            oc_core_select_cell(core, row, col, 1, 0);
        }
        break;
    case OC_HIT_COL_HEADER:
        if (hit->kind == OC_HIT_CELL || hit->kind == OC_HIT_COL_HEADER)
            header_select(core, 1, hit->col, 1, 0);
        break;
    case OC_HIT_ROW_HEADER:
        if (hit->kind == OC_HIT_CELL || hit->kind == OC_HIT_ROW_HEADER)
            header_select(core, 0, hit->row, 1, 0);
        break;
    default:
        return OWF_ERR_UNSUPPORTED;
    }
    return OWF_OK;
}

/* ---- The draw list ---- */

void oc_draw_list_init(oc_draw_list *list)
{
    memset(list, 0, sizeof *list);
}

void oc_draw_list_free(oc_draw_list *list)
{
    free(list->items);
    free(list->pool);
    memset(list, 0, sizeof *list);
}

static oc_draw_item *add(oc_draw_list *list, int kind, int role)
{
    oc_draw_item *it;
    if (list->n == list->cap) {
        int want = list->cap ? list->cap * 2 : 256;
        oc_draw_item *p = realloc(list->items, (size_t)want * sizeof *p);
        if (!p)
            return NULL;
        list->items = p;
        list->cap = want;
    }
    it = &list->items[list->n++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->role = role;
    it->text_off = NO_TEXT;
    it->font = -1;
    return it;
}

static int add_text(oc_draw_list *list, oc_draw_item *it, const char *s, size_t n)
{
    if (list->pool_used + n + 1 > list->pool_cap) {
        size_t want = (list->pool_used + n + 1) * 2;
        char *p;
        if (want < 4096)
            want = 4096;
        if (!(p = realloc(list->pool, want)))
            return 0;
        list->pool = p;
        list->pool_cap = want;
    }
    memcpy(list->pool + list->pool_used, s, n);
    list->pool[list->pool_used + n] = 0;
    it->text_off = list->pool_used;
    list->pool_used += n + 1;
    return 1;
}

static oc_draw_item *add_box(oc_draw_list *list, int kind, int role, int x, int y, int w, int h)
{
    oc_draw_item *it = add(list, kind, role);
    if (it) {
        it->x = x;
        it->y = y;
        it->w = w;
        it->h = h;
    }
    return it;
}

/* Per visible cell, what the grid lines need to know. */
enum {
    CI_VALUE = 1,           /* has a value: text can't run over it */
    CI_FILLED = 2,          /* its own fill hides its lines */
    CI_NO_RIGHT = 4,        /* no grid line on its right: text runs on, or a merge goes on */
    CI_NO_BOTTOM = 8,       /* a merge goes on below */
    CI_INNER = 16           /* inside a merge, not its first cell */
};

typedef struct {
    oc_core *core;
    oc_draw_list *list;
    const owf_sheet *sheet;
    oc_line *rl, *cl;
    int nr, nc, ysplit, xsplit;
    int rowhdr;             /* the row numbers' bar's width */
    unsigned char *ci;      /* nr x nc */
} drawing;

#define CI(d, i, j) ((d)->ci[(size_t)(i) * (size_t)(d)->nc + (size_t)(j)])

/* The quadrant (pane of rows, pane of columns) of the grid area, in pixels. */
static void quadrant(const drawing *d, int pr, int pc, int *x, int *y, int *w, int *h)
{
    *x = pc ? d->xsplit : d->rowhdr;
    *w = (pc ? d->core->width : d->xsplit) - *x;
    *y = pr ? d->ysplit : d->core->colhdr;
    *h = (pr ? d->core->height : d->ysplit) - *y;
}

static const char *format_of(const oc_core *core, const owf_cellstyle *st)
{
    if (st->number_format > 0 && st->number_format < core->book->nnumfmts)
        return core->book->numfmts[st->number_format];
    return NULL;
}

/* A cell's text as the grid shows it, and its colour. A General number is
 * fitted to as many digits as the cell's pixels hold in its font (fewer
 * decimals, then scientific), as Excel fits it to its column. */
static int cell_text(const oc_core *core, const owf_cell *c, const owf_cellstyle *st, int px, char *out,
                     size_t size, unsigned long *colour)
{
    int digit = oc_measure(core, "0", 1, st), pad = PAD * core->zoom / 100 + 1;
    int chars = digit > 0 ? (px - 2 * pad) / digit : 11;
    char *nl;
    int n = owf_calc_format_value(format_of(core, st), c, core->book->date1904, chars > 0 ? chars : 1, out, size, colour);
    if ((nl = strchr(out, '\n')) != NULL) {               /* one line: up to a line break */
        *nl = 0;
        n = (int)(nl - out);
    }
    if ((nl = strchr(out, '\r')) != NULL) {
        *nl = 0;
        n = (int)(nl - out);
    }
    return n;
}

static int resolve_align(const owf_cell *c, const owf_cellstyle *st)
{
    switch (st->halign) {
    case OWF_HALIGN_LEFT:
    case OWF_HALIGN_FILL:
    case OWF_HALIGN_JUSTIFY:
        return OC_ALIGN_LEFT;
    case OWF_HALIGN_CENTRE:
    case OWF_HALIGN_CENTRE_ACROSS:
        return OC_ALIGN_CENTRE;
    case OWF_HALIGN_RIGHT:
        return OC_ALIGN_RIGHT;
    }
    if (c->type == OWF_VALUE_NUMBER)
        return OC_ALIGN_RIGHT;
    if (c->type == OWF_VALUE_BOOL || c->type == OWF_VALUE_ERROR)
        return OC_ALIGN_CENTRE;
    return OC_ALIGN_LEFT;
}

int oc_font_px(const oc_core *core, int twips)
{
    int px = oc_px(core, twips > 0 ? twips : OC_DEFAULT_FONT_TWIPS);
    return px < 4 ? 4 : px > 160 ? 160 : px;
}

int oc_line_px(const oc_core *core, int twips)
{
    return (oc_font_px(core, twips) * 23 + 10) / 20;
}

/* Where a cell's text starts: by its alignment, pad and indent (10 pixels a step at 100%). */
static int text_start(const oc_core *core, const owf_cellstyle *st, int align, int x, int w, int tw)
{
    int pad = PAD * core->zoom / 100 + 1, indent = st->indent * 10 * core->zoom / 100;
    return align == OC_ALIGN_LEFT ? x + pad + indent : align == OC_ALIGN_RIGHT ? x + w - pad - tw - indent : x + (w - tw) / 2;
}

#define MAX_WRAP_LINES 64
#define WRAP_TEXT 2048

/* The end of the line of s[start..n) that fits avail pixels: at a space or
 * the text's own line break, else (one long word) where the word must
 * break, never before one character. */
static size_t wrap_line(const oc_core *core, const owf_cellstyle *st, const char *s, size_t start, size_t n, int avail)
{
    size_t end = start, k = start, fit;
    for (;;) {
        size_t b = k;
        while (b < n && s[b] != ' ' && s[b] != '\n')
            b++;
        if (b > start && oc_measure(core, s + start, b - start, st) > avail)
            break;
        end = b;
        if (b >= n || s[b] == '\n')
            return end;
        k = b + 1;
    }
    if (end > start)
        return end;
    /* a word wider than the cell alone: after its last "/" or "-" that fits ("Software/" "Service"), else as
       many characters as fit */
    for (k = start + 1; k < n && s[k - 1] != ' ' && s[k - 1] != '\n'; k++) {
        if (s[k - 1] != '/' && s[k - 1] != '-')
            continue;
        if (oc_measure(core, s + start, k - start, st) > avail)
            break;
        end = k;
    }
    if (end > start)
        return end;
    fit = start;
    for (k = start + 1; k <= n; k++) {
        if (k < n && ((unsigned char)s[k] & 0xC0) == 0x80)
            continue;
        if (s[k - 1] == ' ' || s[k - 1] == '\n' || oc_measure(core, s + start, k - start, st) > avail)
            break;
        fit = k;
    }
    if (fit == start) {                                      /* not even one: one character all the same */
        fit = start + 1;
        while (fit < n && ((unsigned char)s[fit] & 0xC0) == 0x80)
            fit++;
    }
    return fit;
}

/* Wrapped text (wrap set): a text item for each line, broken at spaces and
 * the text's own line breaks to the cell's width less its pads, a word
 * wider than that alone broken after a "/" or "-" in it, else where it must.
 * The lines are a block placed in the box (y, h) by the cell's vertical
 * alignment, oc_line_px apart; lines that don't fit are left out, though one always shows (the
 * first from the top, the last from the bottom). */
static int wrapped_items(drawing *d, const owf_cell *c, const owf_cellstyle *st, const char *s, size_t n,
                         unsigned long colour, int align, int x, int w, int y, int h, int qx, int qw)
{
    oc_core *core = d->core;
    int pad = PAD * core->zoom / 100 + 1, avail = w - 2 * pad - st->indent * 10 * core->zoom / 100;
    int lp = oc_line_px(core, st->font.size), nl = 0, k, top, first, last;
    size_t at = 0, starts[MAX_WRAP_LINES], ends[MAX_WRAP_LINES];
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        n--;
    if (avail < 1)
        avail = 1;
    while (at < n && nl < MAX_WRAP_LINES) {
        size_t e = wrap_line(core, st, s, at, n, avail), next = e;
        if (next < n && (s[next] == ' ' || s[next] == '\n'))
            next++;
        while (e > at && (s[e - 1] == ' ' || s[e - 1] == '\r'))
            e--;
        starts[nl] = at;
        ends[nl] = e;
        nl++;
        at = next;
    }
    if (!nl)
        return 1;
    h -= 1;                                                  /* less the grid line */
    top = st->valign == OWF_VALIGN_TOP ? y + 1 : st->valign == OWF_VALIGN_CENTRE ? y + (h - nl * lp) / 2 : y + h - nl * lp - 1;
    first = 0;
    last = nl - 1;
    while (first < last && top + first * lp < y)
        first++;
    while (last > first && top + (last + 1) * lp > y + h + 1)
        last--;
    if (first == last && top + first * lp < y)
        top = y - first * lp;                                /* the one line that shows starts at the top */
    if ((x > qx ? x : qx) >= (x + w - 1 < qx + qw ? x + w - 1 : qx + qw))
        return 1;                                            /* not in its pane */
    for (k = first; k <= last; k++) {
        oc_draw_item *it = add(d->list, OC_DRAW_TEXT, OC_ROLE_TEXT);
        int tw = oc_measure(core, s + starts[k], ends[k] - starts[k], st);
        if (!it)
            return 0;
        it->x = x;
        it->w = w;
        it->y = top + k * lp;
        it->h = lp;
        it->clip_x = x > qx ? x : qx;
        it->clip_w = (x + w - 1 < qx + qw ? x + w - 1 : qx + qw) - it->clip_x;
        it->clip_y = it->y;
        it->clip_h = lp;
        it->align = align;
        it->valign = OWF_VALIGN_CENTRE;                      /* in its own line */
        it->flags = st->font.flags;
        it->font = st->font.font;
        it->size = st->font.size;
        it->rgb = colour != OWF_COLOUR_AUTO ? colour : st->font.colour;
        it->row = c->row;
        it->col = c->col;
        it->text_x = text_start(core, st, align, x, w, tw);
        if (!add_text(d->list, it, s + starts[k], ends[k] - starts[k]))
            return 0;
    }
    return 1;
}

/* A cell's text in the box (x, y, w, h): wrapped (wrapped_items), or one
 * line, its clip box run on over empty neighbours in the same pane (text
 * only, not wrapped, as Excel). j is the column's place in cl (-1: a cell
 * left of those showing, at x; -2: a merged range's first cell). */
static int text_item(drawing *d, int i, int j, int pane, const owf_cell *c, int x, int w, int y, int h)
{
    oc_core *core = d->core;
    const owf_cellstyle *st = oc_cell_style(core, c, c->col);
    char buf[256];
    unsigned long colour = OWF_COLOUR_AUTO;
    int n, tw, pad = PAD * core->zoom / 100 + 1, align, lo, hi;
    int clip_x = x, clip_w = w, qx, qy, qw, qh, k;
    oc_draw_item *it;
    if (st->wrap && c->type == OWF_VALUE_TEXT) {
        char *all = malloc(WRAP_TEXT);
        int ok;
        if (!all)
            return 0;
        n = owf_calc_format_value(format_of(core, st), c, core->book->date1904, 1, all, WRAP_TEXT, &colour);
        quadrant(d, d->rl[i].pane, pane, &qx, &qy, &qw, &qh);
        ok = n <= 0 || wrapped_items(d, c, st, all, (size_t)n, colour, resolve_align(c, st), x, w, y, h, qx, qw);
        free(all);
        return ok;
    }
    n = cell_text(core, c, st, w, buf, sizeof buf, &colour);
    if (n <= 0)
        return 1;
    align = resolve_align(c, st);
    tw = oc_measure(core, buf, (size_t)n, st);
    if (c->type == OWF_VALUE_NUMBER && tw > w - 2 * pad) {   /* a number that doesn't fit: #### */
        int hw = oc_measure(core, "#", 1, st), m = hw > 0 ? (w - 2 * pad) / hw : 0;
        if (m < 1)
            m = 1;
        if (m > (int)sizeof buf - 1)
            m = (int)sizeof buf - 1;
        memset(buf, '#', (size_t)m);
        buf[m] = 0;
        n = m;
        tw = oc_measure(core, buf, (size_t)n, st);
        align = OC_ALIGN_RIGHT;
    }
    lo = hi = j;
    if (c->type == OWF_VALUE_TEXT && !st->wrap && tw > w - 2 * pad && j >= -1) {
        int need = tw + 2 * pad - w, right = align == OC_ALIGN_LEFT ? need : align == OC_ALIGN_CENTRE ? (need + 1) / 2 : 0,
            left = align == OC_ALIGN_RIGHT ? need : align == OC_ALIGN_CENTRE ? need / 2 : 0;
        /* run on to the right over empty cells */
        for (k = j + 1; right > 0 && k < d->nc && d->cl[k].pane == pane && d->cl[k].pos == clip_x + clip_w; k++) {
            if (CI(d, i, k) & (CI_VALUE | CI_INNER))
                break;
            if (k - 1 >= 0)
                CI(d, i, k - 1) |= CI_NO_RIGHT;
            clip_w += d->cl[k].size;
            right -= d->cl[k].size;
            hi = k;
        }
        for (k = j - 1; j >= 0 && left > 0 && k >= 0 && d->cl[k].pane == pane && d->cl[k].pos + d->cl[k].size == clip_x; k--) {
            if (CI(d, i, k) & (CI_VALUE | CI_INNER))
                break;
            CI(d, i, k) |= CI_NO_RIGHT;
            clip_x -= d->cl[k].size;
            clip_w += d->cl[k].size;
            left -= d->cl[k].size;
            lo = k;
        }
    }
    (void)lo;
    (void)hi;
    quadrant(d, d->rl[i].pane, pane, &qx, &qy, &qw, &qh);
    if (clip_x < qx) {
        clip_w -= qx - clip_x;
        clip_x = qx;
    }
    if (clip_x + clip_w > qx + qw)
        clip_w = qx + qw - clip_x;
    if (clip_w <= 0)
        return 1;
    if (!(it = add(d->list, OC_DRAW_TEXT, OC_ROLE_TEXT)))
        return 0;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h - 1;
    it->clip_x = clip_x;
    it->clip_y = y;
    it->clip_w = clip_w - 1;
    it->clip_h = h - 1;
    it->align = align;
    it->valign = st->valign;
    it->flags = st->font.flags;
    it->font = st->font.font;
    it->size = st->font.size;
    it->rgb = colour != OWF_COLOUR_AUTO ? colour : st->font.colour;
    it->row = c->row;
    it->col = c->col;
    it->text_x = text_start(core, st, align, x, w, tw);
    return add_text(d->list, it, buf, (size_t)n);
}

/* Where a range shows in a quadrant: the box, and which sides go on past
 * what shows. 0 when it doesn't show there. */
static int range_box(const drawing *d, const owf_range *r, int pr, int pc, int *x, int *y, int *w, int *h, int *open)
{
    int i, r0 = -1, r1 = -1, c0 = -1, c1 = -1;
    *open = 0;
    for (i = 0; i < d->nr; i++)
        if (d->rl[i].pane == pr && d->rl[i].index >= r->first_row && d->rl[i].index <= r->last_row) {
            if (r0 < 0)
                r0 = i;
            r1 = i;
        }
    for (i = 0; i < d->nc; i++)
        if (d->cl[i].pane == pc && d->cl[i].index >= r->first_col && d->cl[i].index <= r->last_col) {
            if (c0 < 0)
                c0 = i;
            c1 = i;
        }
    if (r0 < 0 || c0 < 0)
        return 0;
    *x = d->cl[c0].pos;
    *y = d->rl[r0].pos;
    *w = d->cl[c1].pos + d->cl[c1].size - *x;
    *h = d->rl[r1].pos + d->rl[r1].size - *y;
    if (d->rl[r0].index > r->first_row)
        *open |= 1;                                          /* top */
    if (d->rl[r1].index < r->last_row)
        *open |= 2;                                          /* bottom */
    if (d->cl[c0].index > r->first_col)
        *open |= 4;
    if (d->cl[c1].index < r->last_col)
        *open |= 8;
    return 1;
}

static int line_item(oc_draw_list *list, int role, int x, int y, int w, int h)
{
    oc_draw_item *it = add_box(list, OC_DRAW_LINE, role, x, y, w, h);
    return it != NULL;
}

/* Grid lines, joined where they run on. */
static int grid_lines(drawing *d)
{
    int i, j;
    /* down each column's right edge */
    for (j = 0; j < d->nc; j++) {
        int x = d->cl[j].pos + d->cl[j].size - 1, start = -1;
        for (i = 0; i <= d->nr; i++) {
            int on = 0;
            if (i < d->nr) {
                unsigned char f = CI(d, i, j), g = j + 1 < d->nc ? CI(d, i, j + 1) : 0;
                on = !(f & (CI_NO_RIGHT | CI_FILLED)) && !((g & CI_FILLED) && d->cl[j + 1].pos == x + 1);
            }
            if (on && start < 0)
                start = i;
            if ((!on || (i < d->nr && i > 0 && d->rl[i].pane != d->rl[i - 1].pane)) && start >= 0) {
                int end = on ? i - 1 : i - 1, y0 = d->rl[start].pos, y1 = d->rl[end].pos + d->rl[end].size;
                if (!line_item(d->list, OC_ROLE_GRID, x, y0, 0, y1 - y0))
                    return 0;
                start = on ? i : -1;
            }
        }
    }
    /* along each row's bottom edge */
    for (i = 0; i < d->nr; i++) {
        int y = d->rl[i].pos + d->rl[i].size - 1, start = -1;
        for (j = 0; j <= d->nc; j++) {
            int on = 0;
            if (j < d->nc) {
                unsigned char f = CI(d, i, j), g = i + 1 < d->nr ? CI(d, i + 1, j) : 0;
                on = !(f & (CI_NO_BOTTOM | CI_FILLED)) && !((g & CI_FILLED) && d->rl[i + 1].pos == y + 1);
            }
            if (on && start < 0)
                start = j;
            if ((!on || (j < d->nc && j > 0 && d->cl[j].pane != d->cl[j - 1].pane)) && start >= 0) {
                int end = j - 1, x0 = d->cl[start].pos, x1 = d->cl[end].pos + d->cl[end].size;
                if (!line_item(d->list, OC_ROLE_GRID, x0, y, x1 - x0, 0))
                    return 0;
                start = on ? j : -1;
            }
        }
    }
    return 1;
}

/* ---- Cell borders ----
 * Two cells meet on one edge, and each may give it a line (the left one
 * its right side, the right one its left side): the edge is drawn once,
 * in the heavier of the two lines, as Excel and LibreOffice draw it. No
 * line is drawn inside a merged range. A line goes on along a row or down
 * a column as one item, so that its dashes run on. */

int oc_line_weight(int line)
{
    switch (line) {
    case OWF_LINE_NONE:
        return 0;
    case OWF_LINE_MEDIUM:
    case OWF_LINE_MEDIUM_DASHED:
    case OWF_LINE_MEDIUM_DASH_DOT:
    case OWF_LINE_MEDIUM_DASH_DOT_DOT:
    case OWF_LINE_SLANTED_DASH_DOT:
        return 2;
    case OWF_LINE_THICK:
    case OWF_LINE_DOUBLE:
        return 3;
    }
    return 1;                                                /* thin, hair, dashed, dotted, dash-dot(-dot) */
}

unsigned oc_line_pattern(int line)
{
    switch (line) {
    case OWF_LINE_HAIR:
        return 0xAAAAu;                                      /* every other pixel */
    case OWF_LINE_DOTTED:
        return 0xCCCCu;                                      /* 2 on, 2 off */
    case OWF_LINE_DASHED:
        return 0xF0F0u;                                      /* 4 on, 4 off */
    case OWF_LINE_MEDIUM_DASHED:
        return 0xFF00u;                                      /* 8 on, 8 off */
    case OWF_LINE_DASH_DOT:
    case OWF_LINE_MEDIUM_DASH_DOT:
    case OWF_LINE_SLANTED_DASH_DOT:
        return 0xFF18u;                                      /* 8 on, 3 off, 2 on, 3 off */
    case OWF_LINE_DASH_DOT_DOT:
    case OWF_LINE_MEDIUM_DASH_DOT_DOT:
        return 0xFCCCu;                                      /* 6 on, 2 off, 2 on, 2 off, 2 on, 2 off */
    }
    return 0xFFFFu;
}

/* Which of two lines on one edge is drawn: the heavier (double, thick,
 * medium, the medium dashes, thin, then the light dashes and dots). */
static int line_rank(int line)
{
    static const unsigned char rank[14] = {
        0,  /* none */           7,  /* thin */           11, /* medium */     6,  /* dashed */
        3,  /* dotted */         12, /* thick */          13, /* double */     1,  /* hair */
        10, /* medium dashed */  5,  /* dash dot */       9,  /* medium dash dot */
        4,  /* dash dot dot */   8,  /* medium dash dot dot */                 9   /* slanted dash dot */
    };
    return line > 0 && line < 14 ? rank[line] : 0;
}

static const owf_cellstyle *style_at(const drawing *d, unsigned row, unsigned col)
{
    return oc_cell_style(d->core, owf_sheet_find(d->sheet, row, col), col);
}

/* The line on the edge a's side_a shares with b's side_b (b NULL: none). */
static int edge_line(const owf_cellstyle *a, int side_a, const owf_cellstyle *b, int side_b, unsigned long *rgb)
{
    int la = a->border[side_a], lb = b ? b->border[side_b] : OWF_LINE_NONE;
    unsigned long c;
    if (line_rank(lb) > line_rank(la)) {
        la = lb;
        c = b->border_colour[side_b];
    } else {
        c = a->border_colour[side_a];
    }
    *rgb = OWF_SHADE_SET(c) ? OWF_SHADE_RGB(c) : OWF_COLOUR_AUTO;
    return la;
}

/* Both cells in one merged range: the edge between them is inside it. */
static int same_merge(const owf_sheet *s, unsigned r0, unsigned c0, unsigned r1, unsigned c1)
{
    const owf_range *m = oc_merge_at(s, r0, c0);
    return m && r1 >= m->first_row && r1 <= m->last_row && c1 >= m->first_col && c1 <= m->last_col;
}

/* An edge's line: joined on to the item last made (*last, an index) when
 * it goes on from it in the same line, else a new item. */
static int edge_item(drawing *d, int *last, int vertical, int x, int y, int len, int line, unsigned long rgb,
                     unsigned row, unsigned col)
{
    oc_draw_item *it;
    if (!line) {
        *last = -1;
        return 1;
    }
    if (*last >= 0) {
        it = &d->list->items[*last];
        if (it->line == line && it->rgb == rgb) {
            if (vertical && it->x == x && it->y + it->h >= y && y + len > it->y + it->h) {
                it->h = y + len - it->y;
                return 1;
            }
            if (!vertical && it->y == y && it->x + it->w >= x && x + len > it->x + it->w) {
                it->w = x + len - it->x;
                return 1;
            }
        }
    }
    if (!(it = add_box(d->list, OC_DRAW_LINE, OC_ROLE_BORDER, x, y, vertical ? 0 : len, vertical ? len : 0)))
        return 0;
    it->line = line;
    it->rgb = rgb;
    it->row = row;
    it->col = col;
    *last = d->list->n - 1;
    return 1;
}

/* Whether visible line k touches k - 1 (hidden ones between, or the frozen pane's edge, count as nothing). */
static int follows(const oc_line *l, int k)
{
    return k > 0 && l[k].pos == l[k - 1].pos + l[k - 1].size;
}

static int border_lines(drawing *d)
{
    const owf_sheet *s = d->sheet;
    int i, j, last;
    unsigned long rgb = OWF_COLOUR_AUTO;
    /* along the rows: each row's top edge where the row above isn't showing next to it, and every bottom edge */
    for (i = 0; i < d->nr; i++) {
        unsigned r = d->rl[i].index;
        int top = !follows(d->rl, i) && r > 0, y = d->rl[i].pos + d->rl[i].size - 1;
        unsigned below = i + 1 < d->nr && follows(d->rl, i + 1) ? d->rl[i + 1].index : r + 1;
        if (top) {
            last = -1;
            for (j = 0; j < d->nc; j++) {
                unsigned c = d->cl[j].index;
                int line = same_merge(s, r - 1, c, r, c) ? 0
                           : edge_line(style_at(d, r, c), OWF_SIDE_TOP, style_at(d, r - 1, c), OWF_SIDE_BOTTOM, &rgb);
                if (!follows(d->cl, j))
                    last = -1;
                if (!edge_item(d, &last, 0, d->cl[j].pos - 1, d->rl[i].pos - 1, d->cl[j].size + 1, line, rgb, r, c))
                    return 0;
            }
        }
        last = -1;
        for (j = 0; j < d->nc; j++) {
            unsigned c = d->cl[j].index;
            int line = below < OC_MAX_ROWS && same_merge(s, r, c, below, c) ? 0
                       : edge_line(style_at(d, r, c), OWF_SIDE_BOTTOM, below < OC_MAX_ROWS ? style_at(d, below, c) : NULL,
                                   OWF_SIDE_TOP, &rgb);
            if (!follows(d->cl, j))
                last = -1;
            if (!edge_item(d, &last, 0, d->cl[j].pos - 1, y, d->cl[j].size + 1, line, rgb, r, c))
                return 0;
        }
    }
    /* down the columns: each column's left edge where the column before isn't showing next to it, and every right edge */
    for (j = 0; j < d->nc; j++) {
        unsigned c = d->cl[j].index;
        int left = !follows(d->cl, j) && c > 0, x = d->cl[j].pos + d->cl[j].size - 1;
        unsigned next = j + 1 < d->nc && follows(d->cl, j + 1) ? d->cl[j + 1].index : c + 1;
        if (left) {
            last = -1;
            for (i = 0; i < d->nr; i++) {
                unsigned r = d->rl[i].index;
                int line = same_merge(s, r, c - 1, r, c) ? 0
                           : edge_line(style_at(d, r, c), OWF_SIDE_LEFT, style_at(d, r, c - 1), OWF_SIDE_RIGHT, &rgb);
                if (!follows(d->rl, i))
                    last = -1;
                if (!edge_item(d, &last, 1, d->cl[j].pos - 1, d->rl[i].pos - 1, d->rl[i].size + 1, line, rgb, r, c))
                    return 0;
            }
        }
        last = -1;
        for (i = 0; i < d->nr; i++) {
            unsigned r = d->rl[i].index;
            int line = next < OC_MAX_COLS && same_merge(s, r, c, r, next) ? 0
                       : edge_line(style_at(d, r, c), OWF_SIDE_RIGHT, next < OC_MAX_COLS ? style_at(d, r, next) : NULL,
                                   OWF_SIDE_LEFT, &rgb);
            if (!follows(d->rl, i))
                last = -1;
            if (!edge_item(d, &last, 1, x, d->rl[i].pos - 1, d->rl[i].size + 1, line, rgb, r, c))
                return 0;
        }
    }
    return 1;
}

static int range_selected_col(const oc_sheetstate *st, unsigned col)
{
    int k;
    for (k = 0; k < st->sel.nranges; k++)
        if (col >= st->sel.ranges[k].first_col && col <= st->sel.ranges[k].last_col)
            return 1;
    return 0;
}

static int range_selected_row(const oc_sheetstate *st, unsigned row)
{
    int k;
    for (k = 0; k < st->sel.nranges; k++)
        if (row >= st->sel.ranges[k].first_row && row <= st->sel.ranges[k].last_row)
            return 1;
    return 0;
}

static int build(drawing *d)
{
    oc_core *core = d->core;
    oc_draw_list *list = d->list;
    const owf_sheet *s = d->sheet;
    const oc_sheetstate *st = oc_state_const(core);
    int i, j, k, m, pr, pc;
    char label[16];
    oc_draw_item *it;

    /* what each cell is: values, fills, merges */
    for (i = 0; i < d->nr; i++)
        for (j = 0; j < d->nc; j++) {
            const owf_cell *c = owf_sheet_find(s, d->rl[i].index, d->cl[j].index);
            const owf_cellstyle *cs = oc_cell_style(core, c, d->cl[j].index);
            if (!oc_cell_empty(c))
                CI(d, i, j) |= CI_VALUE;
            if (OWF_SHADE_SET(cs->fill))
                CI(d, i, j) |= CI_FILLED;
        }
    for (m = 0; m < s->nmerges; m++) {
        const owf_range *g = &s->merges[m];
        for (i = 0; i < d->nr; i++) {
            unsigned r = d->rl[i].index;
            if (r < g->first_row || r > g->last_row)
                continue;
            for (j = 0; j < d->nc; j++) {
                unsigned c = d->cl[j].index;
                if (c < g->first_col || c > g->last_col)
                    continue;
                if (r != g->first_row || c != g->first_col)
                    CI(d, i, j) |= CI_INNER;
                if (c < g->last_col)
                    CI(d, i, j) |= CI_NO_RIGHT;
                if (r < g->last_row)
                    CI(d, i, j) |= CI_NO_BOTTOM;
            }
        }
    }

    /* 1. cells' own fills (a merge's: its first cell's, over all of it) */
    for (i = 0; i < d->nr; i++)
        for (j = 0; j < d->nc; j++) {
            const owf_cell *c;
            const owf_cellstyle *cs;
            if (!(CI(d, i, j) & CI_FILLED))
                continue;
            c = owf_sheet_find(s, d->rl[i].index, d->cl[j].index);
            cs = oc_cell_style(core, c, d->cl[j].index);
            if (!(it = add_box(list, OC_DRAW_FILL, OC_ROLE_CELL_FILL, d->cl[j].pos, d->rl[i].pos, d->cl[j].size, d->rl[i].size)))
                return 0;
            it->rgb = OWF_SHADE_RGB(cs->fill);
            it->row = d->rl[i].index;
            it->col = d->cl[j].index;
        }
    for (m = 0; m < s->nmerges; m++) {
        const owf_range *g = &s->merges[m];
        const owf_cell *c = owf_sheet_find(s, g->first_row, g->first_col);
        const owf_cellstyle *cs = oc_cell_style(core, c, g->first_col);
        if (!OWF_SHADE_SET(cs->fill))
            continue;
        for (pr = 0; pr < 2; pr++)
            for (pc = 0; pc < 2; pc++) {
                int x, y, w, h, open;
                if (!range_box(d, g, pr, pc, &x, &y, &w, &h, &open))
                    continue;
                if (!(it = add_box(list, OC_DRAW_FILL, OC_ROLE_CELL_FILL, x, y, w, h)))
                    return 0;
                it->rgb = OWF_SHADE_RGB(cs->fill);
                it->row = g->first_row;
                it->col = g->first_col;
            }
    }

    /* 2. the selection (not when it is the active cell alone) */
    {
        const owf_range *r0 = &st->sel.ranges[0];
        const owf_range *mg = oc_merge_at(s, st->sel.row, st->sel.col);
        int one = st->sel.nranges == 1 &&
                  ((r0->first_row == r0->last_row && r0->first_col == r0->last_col) ||
                   (mg && mg->first_row == r0->first_row && mg->last_row == r0->last_row && mg->first_col == r0->first_col &&
                    mg->last_col == r0->last_col));
        if (!one) {
            for (k = 0; k < st->sel.nranges; k++)
                for (pr = 0; pr < 2; pr++)
                    for (pc = 0; pc < 2; pc++) {
                        int x, y, w, h, open;
                        if (!range_box(d, &st->sel.ranges[k], pr, pc, &x, &y, &w, &h, &open))
                            continue;
                        if (!add_box(list, OC_DRAW_FILL, OC_ROLE_SELECTION, x, y, w, h))
                            return 0;
                    }
            /* the active cell stays clear, as Excel's */
            {
                owf_range a;
                a.first_row = a.last_row = st->sel.row;
                a.first_col = a.last_col = st->sel.col;
                if (mg)
                    a = *mg;
                for (pr = 0; pr < 2; pr++)
                    for (pc = 0; pc < 2; pc++) {
                        int x, y, w, h, open;
                        const owf_cellstyle *cs = oc_cell_style(core, owf_sheet_find(s, a.first_row, a.first_col), a.first_col);
                        if (!range_box(d, &a, pr, pc, &x, &y, &w, &h, &open))
                            continue;
                        if (!(it = add_box(list, OC_DRAW_FILL, OWF_SHADE_SET(cs->fill) ? OC_ROLE_CELL_FILL : OC_ROLE_PAPER, x, y, w, h)))
                            return 0;
                        it->rgb = OWF_SHADE_SET(cs->fill) ? OWF_SHADE_RGB(cs->fill) : 0xFFFFFFUL;
                    }
            }
        }
    }

    /* 3. text: worked out before the grid lines, which stop where text runs on */
    {
        size_t mark = (size_t)list->n;
        oc_draw_list texts;
        (void)mark;
        oc_draw_list_init(&texts);
        d->list = &texts;
        for (i = 0; i < d->nr; i++) {
            /* a cell left of the first column showing in the scrolling part, its text running into view */
            for (j = 0; j < d->nc && d->cl[j].pane != 1; j++)
                ;
            if (j < d->nc && d->cl[j].index > s->freeze_cols && !(CI(d, i, j) & CI_VALUE)) {
                int lo = 0, hi = s->ncells;
                unsigned r = d->rl[i].index, c0 = d->cl[j].index;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    const owf_cell *q = &s->cells[mid];
                    if (q->row < r || (q->row == r && q->col < c0))
                        lo = mid + 1;
                    else
                        hi = mid;
                }
                for (k = lo - 1; k >= 0 && s->cells[k].row == r; k--)
                    if (!oc_cell_empty(&s->cells[k]))
                        break;
                if (k >= 0 && s->cells[k].row == r && s->cells[k].col >= s->freeze_cols && s->cells[k].type == OWF_VALUE_TEXT &&
                    !oc_merge_at(s, r, s->cells[k].col)) {
                    const owf_cell *q = &s->cells[k];
                    int x = d->cl[j].pos, w;
                    unsigned c;
                    for (c = c0; c > q->col && d->cl[j].pos - x < core->width; c--)
                        x -= line_px(core, 1, c - 1);
                    w = line_px(core, 1, q->col);
                    if (x + w < d->cl[j].pos && resolve_align(q, oc_cell_style(core, q, q->col)) == OC_ALIGN_LEFT &&
                        !oc_cell_style(core, q, q->col)->wrap) {
                        /* as if it were the column before the first: run on from there */
                        int saved_pos = d->cl[j].pos;
                        oc_draw_item *t;
                        int before = texts.n;
                        if (!text_item(d, i, -1, 1, q, x, w, d->rl[i].pos, d->rl[i].size)) {
                            d->list = list;
                            oc_draw_list_free(&texts);
                            return 0;
                        }
                        (void)saved_pos;
                        if (texts.n > before) {
                            /* widen its clip over the empty cells it covers */
                            int need, cw = 0;
                            t = &texts.items[texts.n - 1];
                            need = t->text_x + oc_measure(core, texts.pool + t->text_off, strlen(texts.pool + t->text_off),
                                                          oc_cell_style(core, q, q->col)) + PAD - d->cl[j].pos;
                            for (k = j; need > 0 && k < d->nc && d->cl[k].pane == 1; k++) {
                                if (CI(d, i, k) & (CI_VALUE | CI_INNER))
                                    break;
                                if (k > j)
                                    CI(d, i, k - 1) |= CI_NO_RIGHT;
                                cw += d->cl[k].size;
                                need -= d->cl[k].size;
                            }
                            t->clip_x = d->cl[j].pos;
                            t->clip_w = cw - 1;
                            if (cw <= 0)
                                texts.n--;
                        }
                    }
                }
            }
            for (j = 0; j < d->nc; j++) {
                const owf_cell *c;
                const owf_range *g;
                int x = d->cl[j].pos, w = d->cl[j].size;
                if (!(CI(d, i, j) & CI_VALUE) || (CI(d, i, j) & CI_INNER))
                    continue;
                c = owf_sheet_find(s, d->rl[i].index, d->cl[j].index);
                if ((g = oc_merge_at(s, c->row, c->col)) != NULL) {
                    int y, h, open;
                    if (!range_box(d, g, d->rl[i].pane, d->cl[j].pane, &x, &y, &w, &h, &open))
                        continue;
                    /* the merge's height: its text placed in all of it */
                    if (!text_item(d, i, -2, d->cl[j].pane, c, x, w, y, h)) {
                        d->list = list;
                        oc_draw_list_free(&texts);
                        return 0;
                    }
                    continue;
                }
                if (!text_item(d, i, j, d->cl[j].pane, c, x, w, d->rl[i].pos, d->rl[i].size)) {
                    d->list = list;
                    oc_draw_list_free(&texts);
                    return 0;
                }
            }
        }
        d->list = list;

        /* 4. grid lines, 5. borders */
        if (!grid_lines(d) || !border_lines(d)) {
            oc_draw_list_free(&texts);
            return 0;
        }
        /* then the text, moved over */
        for (k = 0; k < texts.n; k++) {
            oc_draw_item *t = &texts.items[k];
            if (!(it = add(list, OC_DRAW_TEXT, OC_ROLE_TEXT))) {
                oc_draw_list_free(&texts);
                return 0;
            }
            *it = *t;
            if (!add_text(list, it, texts.pool + t->text_off, strlen(texts.pool + t->text_off))) {
                oc_draw_list_free(&texts);
                return 0;
            }
        }
        oc_draw_list_free(&texts);
    }

    /* 6. the row numbers and column letters */
    if (core->colhdr > 0) {
        if (!add_box(list, OC_DRAW_FILL, OC_ROLE_HEADER, 0, 0, core->width, core->colhdr))
            return 0;
        for (j = 0; j < d->nc; j++) {
            oc_col_name(d->cl[j].index, label);
            if (!(it = add_box(list, OC_DRAW_HEADER, range_selected_col(st, d->cl[j].index) ? OC_ROLE_HEADER_SELECTED : OC_ROLE_HEADER,
                               d->cl[j].pos, 0, d->cl[j].size, core->colhdr)))
                return 0;
            it->flags = it->role == OC_ROLE_HEADER_SELECTED;
            it->col = d->cl[j].index;
            it->align = OC_ALIGN_CENTRE;
            if (!add_text(list, it, label, strlen(label)) ||
                !line_item(list, OC_ROLE_HEADER_LINE, d->cl[j].pos + d->cl[j].size - 1, 0, 0, core->colhdr))
                return 0;
        }
        if (!line_item(list, OC_ROLE_HEADER_LINE, 0, core->colhdr - 1, core->width, 0))
            return 0;
    }
    if (d->rowhdr > 0) {
        if (!add_box(list, OC_DRAW_FILL, OC_ROLE_HEADER, 0, core->colhdr, d->rowhdr, core->height - core->colhdr))
            return 0;
        for (i = 0; i < d->nr; i++) {
            int n = 0;
            unsigned v = d->rl[i].index + 1;
            char t[12];
            while (v) {
                t[n++] = (char)('0' + v % 10);
                v /= 10;
            }
            for (k = 0; k < n; k++)
                label[k] = t[n - 1 - k];
            label[n] = 0;
            if (!(it = add_box(list, OC_DRAW_HEADER, range_selected_row(st, d->rl[i].index) ? OC_ROLE_HEADER_SELECTED : OC_ROLE_HEADER,
                               0, d->rl[i].pos, d->rowhdr, d->rl[i].size)))
                return 0;
            it->flags = it->role == OC_ROLE_HEADER_SELECTED;
            it->row = d->rl[i].index;
            if (core->rn_digit > 0) {                         /* following the rows: left, a small inset in */
                it->align = OC_ALIGN_LEFT;
                it->text_x = core->rn_pad / 2;
            } else {
                it->align = OC_ALIGN_CENTRE;
            }
            if (!add_text(list, it, label, (size_t)n) ||
                !line_item(list, OC_ROLE_HEADER_LINE, 0, d->rl[i].pos + d->rl[i].size - 1, d->rowhdr, 0))
                return 0;
        }
        if (!line_item(list, OC_ROLE_HEADER_LINE, d->rowhdr - 1, 0, 0, core->height))
            return 0;
    }

    /* 7. frozen panes' lines */
    if (s->freeze_rows && d->ysplit > core->colhdr && !line_item(list, OC_ROLE_FREEZE, d->rowhdr, d->ysplit - 1, core->width - d->rowhdr, 0))
        return 0;
    if (s->freeze_cols && d->xsplit > d->rowhdr && !line_item(list, OC_ROLE_FREEZE, d->xsplit - 1, core->colhdr, 0, core->height - core->colhdr))
        return 0;

    /* 8. the selection's outline and the active cell's frame */
    for (k = 0; k < st->sel.nranges; k++) {
        const owf_range *r = &st->sel.ranges[k];
        if (r->first_row == r->last_row && r->first_col == r->last_col)
            continue;
        for (pr = 0; pr < 2; pr++)
            for (pc = 0; pc < 2; pc++) {
                int x, y, w, h, open, qx, qy, qw, qh;
                if (!range_box(d, r, pr, pc, &x, &y, &w, &h, &open))
                    continue;
                quadrant(d, pr, pc, &qx, &qy, &qw, &qh);
                if (open & 1) { y -= 4; h += 4; }
                if (open & 2) h += 4;
                if (open & 4) { x -= 4; w += 4; }
                if (open & 8) w += 4;
                if (!(it = add_box(list, OC_DRAW_FRAME, OC_ROLE_SELECTION, x - 1, y - 1, w + 1, h + 1)))
                    return 0;
                it->line = 1;
                it->clip_x = qx;
                it->clip_y = qy;
                it->clip_w = qw;
                it->clip_h = qh;
            }
    }
    {
        owf_range a;
        const owf_range *mg = oc_merge_at(s, st->sel.row, st->sel.col);
        a.first_row = a.last_row = st->sel.row;
        a.first_col = a.last_col = st->sel.col;
        if (mg)
            a = *mg;
        for (pr = 0; pr < 2; pr++)
            for (pc = 0; pc < 2; pc++) {
                int x, y, w, h, open, qx, qy, qw, qh;
                if (!range_box(d, &a, pr, pc, &x, &y, &w, &h, &open))
                    continue;
                quadrant(d, pr, pc, &qx, &qy, &qw, &qh);
                if (!(it = add_box(list, OC_DRAW_FRAME, OC_ROLE_CURSOR, x - 1, y - 1, w + 1, h + 1)))
                    return 0;
                it->line = 2;
                it->row = st->sel.row;
                it->col = st->sel.col;
                it->clip_x = qx - 1;
                it->clip_y = qy - 1;
                it->clip_w = qw + 1;
                it->clip_h = qh + 1;

                /* 9. the text being typed, over the cell and on to the right as far as it needs */
                if (core->edit_mode) {
                    const owf_cellstyle *cs = oc_cell_style(core, owf_sheet_find(s, st->sel.row, st->sel.col), st->sel.col);
                    int pad = PAD * core->zoom / 100 + 1, tw = oc_measure(core, core->edit, core->edit_len, cs);
                    int ew = tw + 2 * pad + 2 > w ? tw + 2 * pad + 2 : w, cx;
                    if (x + ew > core->width)
                        ew = core->width - x;
                    if (!(it = add_box(list, OC_DRAW_FILL, OC_ROLE_EDIT, x, y, ew - 1, h - 1)))
                        return 0;
                    if (!(it = add(list, OC_DRAW_TEXT, OC_ROLE_TEXT)))
                        return 0;
                    it->x = x;
                    it->y = y;
                    it->w = ew;
                    it->h = h - 1;
                    it->clip_x = x;
                    it->clip_y = y;
                    it->clip_w = ew - 1;
                    it->clip_h = h - 1;
                    it->align = OC_ALIGN_LEFT;
                    it->valign = cs->valign;
                    it->flags = cs->font.flags;
                    it->font = cs->font.font;
                    it->size = cs->font.size;
                    it->rgb = cs->font.colour;
                    it->row = st->sel.row;
                    it->col = st->sel.col;
                    /* the caret stays in view: the text slides left when it is longer than the box */
                    cx = oc_measure(core, core->edit, core->caret, cs);
                    it->text_x = x + pad;
                    if (cx + 2 * pad > ew)
                        it->text_x = x + ew - pad - cx - 1;
                    cx += it->text_x;
                    if (!add_text(list, it, core->edit, core->edit_len))
                        return 0;
                    if (!(it = add_box(list, OC_DRAW_CARET, OC_ROLE_CURSOR, cx, y + 2, 0, h - 5)))
                        return 0;
                }
            }
    }
    return 1;
}

int oc_view_draw(oc_core *core, oc_draw_list *list)
{
    drawing d;
    int ok, k;
    list->n = 0;
    list->pool_used = 0;
    memset(&d, 0, sizeof d);
    d.core = core;
    d.list = list;
    d.sheet = oc_sheet(core);
    d.rl = malloc(MAX_LINES * sizeof *d.rl);
    d.cl = malloc(MAX_LINES * sizeof *d.cl);
    if (!d.rl || !d.cl) {
        free(d.rl);
        free(d.cl);
        return OWF_ERR_MEMORY;
    }
    d.rowhdr = row_header(core);
    d.nr = lines(core, 0, d.rl, MAX_LINES, &d.ysplit);
    d.nc = lines(core, 1, d.cl, MAX_LINES, &d.xsplit);
    d.ci = calloc((size_t)(d.nr ? d.nr : 1) * (size_t)(d.nc ? d.nc : 1), 1);
    ok = d.ci != NULL && build(&d);
    free(d.ci);
    free(d.rl);
    free(d.cl);
    for (k = 0; k < list->n; k++)
        list->items[k].text = list->items[k].text_off != NO_TEXT ? list->pool + list->items[k].text_off : NULL;
    return ok ? OWF_OK : OWF_ERR_MEMORY;
}
