/*
 * GFOpenPresentation's core: the slide sorter (openpresentation_core.h, "The
 * slide sorter"). Which slides are picked, the grid of thumbnails in pixels
 * (where each one is, what is under the pointer, where a drop goes, the
 * keys), and the commands on several slides at once, each one Undo step
 * made of the single-slide steps in opr_edit.c.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdlib.h>
#include <string.h>

/* ---- The pick ---- */

int opr_pick_resize(opr_pick *p, int count)
{
    unsigned char *on;
    if (!p || count < 0)
        return -1;
    if (count == p->count && (p->on || !count))
        return 0;
    on = count ? owf_malloc((size_t)count) : NULL;
    if (count && !on) {
        owf_free(p->on);
        memset(p, 0, sizeof *p);
        p->anchor = p->focus = -1;
        return -1;
    }
    if (count) {
        memset(on, 0, (size_t)count);
        if (p->on && p->count)
            memcpy(on, p->on, (size_t)(p->count < count ? p->count : count));
    }
    owf_free(p->on);
    p->on = on;
    p->count = count;
    if (p->anchor >= count) p->anchor = count - 1;
    if (p->focus >= count) p->focus = count - 1;
    return 0;
}

void opr_pick_free(opr_pick *p)
{
    if (!p)
        return;
    owf_free(p->on);
    memset(p, 0, sizeof *p);
    p->anchor = p->focus = -1;
}

void opr_pick_clear(opr_pick *p)
{
    if (!p)
        return;
    if (p->on && p->count)
        memset(p->on, 0, (size_t)p->count);
    p->anchor = p->focus = -1;
}

int opr_pick_only(opr_pick *p, int index)
{
    if (!p || index < 0 || index >= p->count || !p->on)
        return -1;
    memset(p->on, 0, (size_t)p->count);
    p->on[index] = 1;
    p->anchor = p->focus = index;
    return 0;
}

int opr_pick_click(opr_pick *p, int index, opr_pick_how how)
{
    int a, b, i;
    if (!p || index < 0 || index >= p->count || !p->on)
        return -1;
    switch (how) {
    case OPR_PICK_ONE:
        return opr_pick_only(p, index);
    case OPR_PICK_TOGGLE:
        p->on[index] = !p->on[index];
        p->anchor = p->focus = index;
        return 0;
    case OPR_PICK_RANGE:
    case OPR_PICK_ADD_RANGE:
        a = p->anchor >= 0 && p->anchor < p->count ? p->anchor : index;
        b = index;
        if (a > b) { i = a; a = b; b = i; }
        if (how == OPR_PICK_RANGE)
            memset(p->on, 0, (size_t)p->count);
        for (i = a; i <= b; i++)
            p->on[i] = 1;
        if (p->anchor < 0 || p->anchor >= p->count)
            p->anchor = index;
        p->focus = index;
        return 0;
    }
    return -1;
}

void opr_pick_all(opr_pick *p)
{
    if (!p || !p->on || !p->count)
        return;
    memset(p->on, 1, (size_t)p->count);
    if (p->focus < 0) p->focus = 0;
    if (p->anchor < 0) p->anchor = 0;
}

int opr_pick_has(const opr_pick *p, int index)
{
    return p && p->on && index >= 0 && index < p->count && p->on[index];
}

int opr_pick_number(const opr_pick *p)
{
    int i, n = 0;
    for (i = 0; p && p->on && i < p->count; i++)
        n += p->on[i] != 0;
    return n;
}

int opr_pick_list(const opr_pick *p, int *out, int max)
{
    int i, n = 0;
    for (i = 0; p && p->on && i < p->count; i++)
        if (p->on[i]) {
            if (out && n < max) out[n] = i;
            n++;
        }
    return n;
}

/* Picks first..last (inclusive) only, focus at focus. */
static void pick_range(opr_pick *p, int first, int last, int focus)
{
    int i;
    if (!p->on)
        return;
    memset(p->on, 0, (size_t)p->count);
    for (i = first; i <= last && i < p->count; i++)
        if (i >= 0) p->on[i] = 1;
    p->anchor = first >= 0 && first < p->count ? first : -1;
    p->focus = focus >= 0 && focus < p->count ? focus : p->anchor;
}

/* ---- The grid ---- */

#define SORTER_MARGIN 12
#define SORTER_GAP 16
#define SORTER_TARGET 160
#define SORTER_SMALLEST 32

int opr_sorter_layout(opr_sorter_grid *g, int view_w, int view_h, int slide_w, int slide_h,
                      int count, int columns, int label_h)
{
    int room, cols;
    if (!g)
        return -1;
    memset(g, 0, sizeof *g);
    if (view_w < 1 || view_h < 1 || slide_w < 1 || slide_h < 1 || count < 0 || columns < 0 ||
        columns > OPR_SORTER_MAX_COLUMNS || label_h < 0)
        return -1;
    g->count = count;
    g->view_w = view_w;
    g->view_h = view_h;
    g->margin = SORTER_MARGIN;
    g->gap = SORTER_GAP;
    g->label_h = label_h;
    room = view_w - 2 * g->margin;
    if (room < SORTER_SMALLEST) {
        g->margin = 2;
        g->gap = 4;
        room = view_w - 2 * g->margin;
        if (room < 1) room = 1;
    }
    if (columns)
        cols = columns;
    else
        cols = (room + g->gap) / (SORTER_TARGET + g->gap);
    if (cols < 1) cols = 1;
    if (cols > OPR_SORTER_MAX_COLUMNS) cols = OPR_SORTER_MAX_COLUMNS;
    while (cols > 1 && (room - (cols - 1) * g->gap) / cols < SORTER_SMALLEST)
        cols--;
    g->columns = cols;
    g->thumb_w = (room - (cols - 1) * g->gap) / cols;
    if (g->thumb_w < 1) g->thumb_w = 1;
    g->thumb_h = (int)((long)g->thumb_w * slide_h / slide_w);
    if (g->thumb_h < 1) g->thumb_h = 1;
    g->cell_w = g->thumb_w;
    g->cell_h = g->thumb_h + label_h;
    g->rows = (count + cols - 1) / cols;
    g->content_h = 2 * g->margin + g->rows * g->cell_h + (g->rows > 0 ? (g->rows - 1) * g->gap : 0);
    return 0;
}

int opr_sorter_clamp_scroll(const opr_sorter_grid *g, int scroll)
{
    int most;
    if (!g)
        return 0;
    most = g->content_h - g->view_h;
    if (most < 0) most = 0;
    if (scroll > most) scroll = most;
    if (scroll < 0) scroll = 0;
    return scroll;
}

int opr_sorter_cell(const opr_sorter_grid *g, int index, int scroll, int *x, int *y)
{
    int row, col;
    if (!g || index < 0 || index >= g->count || g->columns < 1)
        return -1;
    row = index / g->columns;
    col = index % g->columns;
    if (x) *x = g->margin + col * (g->cell_w + g->gap);
    if (y) *y = g->margin + row * (g->cell_h + g->gap) - scroll;
    return 0;
}

int opr_sorter_hit(const opr_sorter_grid *g, int x, int y, int scroll)
{
    int col, row, cx, cy, index;
    if (!g || g->columns < 1 || !g->count)
        return -1;
    x -= g->margin;
    y += scroll - g->margin;
    if (x < 0 || y < 0)
        return -1;
    col = x / (g->cell_w + g->gap);
    row = y / (g->cell_h + g->gap);
    cx = x % (g->cell_w + g->gap);
    cy = y % (g->cell_h + g->gap);
    if (col >= g->columns || cx >= g->cell_w || cy >= g->cell_h)
        return -1;
    index = row * g->columns + col;
    return index < g->count ? index : -1;
}

int opr_sorter_gap(const opr_sorter_grid *g, int x, int y, int scroll)
{
    int row, col, step, gap, in_row;
    if (!g || g->columns < 1 || !g->count)
        return 0;
    step = g->cell_h + g->gap;
    y += scroll - g->margin;
    row = y < 0 ? 0 : y / step;
    if (row >= g->rows) row = g->rows - 1;
    /* the gap nearest x: before column col when x is left of its middle */
    x -= g->margin;
    col = x < 0 ? 0 : (x + (g->cell_w + g->gap) / 2 + g->gap / 2) / (g->cell_w + g->gap);
    in_row = g->count - row * g->columns;
    if (in_row > g->columns) in_row = g->columns;
    if (col > in_row) col = in_row;
    gap = row * g->columns + col;
    return gap > g->count ? g->count : gap;
}

int opr_sorter_gap_mark(const opr_sorter_grid *g, int gap, int scroll, int *x, int *y, int *h)
{
    int cx, cy;
    if (!g || gap < 0 || gap > g->count || !g->count || g->columns < 1)
        return -1;
    if (gap < g->count) {
        /* before slide gap; at a row's start, at that row's left edge */
        opr_sorter_cell(g, gap, scroll, &cx, &cy);
        if (x) *x = cx - g->gap / 2;
    } else {
        /* after the last slide, at its right */
        opr_sorter_cell(g, g->count - 1, scroll, &cx, &cy);
        if (x) *x = cx + g->cell_w + g->gap / 2;
    }
    if (y) *y = cy;
    if (h) *h = g->thumb_h;
    return 0;
}

int opr_sorter_scroll_to(const opr_sorter_grid *g, int index, int scroll)
{
    int y;
    if (!g || opr_sorter_cell(g, index, 0, NULL, &y) < 0)
        return opr_sorter_clamp_scroll(g, scroll);
    if (y - g->margin < scroll)
        scroll = y - g->margin;
    else if (y + g->cell_h + g->margin > scroll + g->view_h)
        scroll = y + g->cell_h + g->margin - g->view_h;
    return opr_sorter_clamp_scroll(g, scroll);
}

int opr_sorter_step(const opr_sorter_grid *g, int index, opr_sorter_key key)
{
    int page;
    if (!g || !g->count)
        return -1;
    if (index < 0) index = 0;
    if (index >= g->count) index = g->count - 1;
    page = g->cell_h + g->gap > 0 ? g->view_h / (g->cell_h + g->gap) : 1;
    if (page < 1) page = 1;
    switch (key) {
    case OPR_SORTER_LEFT: index--; break;
    case OPR_SORTER_RIGHT: index++; break;
    case OPR_SORTER_UP: if (index - g->columns >= 0) index -= g->columns; break;
    case OPR_SORTER_DOWN: if (index + g->columns < g->count) index += g->columns; break;
    case OPR_SORTER_PAGE_UP: index -= page * g->columns; break;
    case OPR_SORTER_PAGE_DOWN: index += page * g->columns; break;
    case OPR_SORTER_HOME: index = 0; break;
    case OPR_SORTER_END: index = g->count - 1; break;
    }
    if (index < 0) index = 0;
    if (index >= g->count) index = g->count - 1;
    return index;
}

/* ---- Several slides at once ---- */

/* The picked slides, from owf_malloc; *n how many. NULL with *n 0 when none
 * are picked, NULL with *n -1 when memory runs out. */
static int *picked(const owf_deck *deck, const opr_pick *pick, int *n)
{
    int *list;
    *n = 0;
    if (!deck || !pick || pick->count != deck->nslides)
        return NULL;
    *n = opr_pick_list(pick, NULL, 0);
    if (!*n)
        return NULL;
    list = owf_malloc((size_t)*n * sizeof *list);
    if (!list) {
        *n = -1;
        return NULL;
    }
    opr_pick_list(pick, list, *n);
    return list;
}

/* Where slide id is in order (n of them). */
static int where(const int *order, int n, int id)
{
    int i;
    for (i = 0; i < n; i++)
        if (order[i] == id)
            return i;
    return -1;
}

static void order_move(int *order, int from, int to)
{
    int id = order[from];
    if (from < to)
        memmove(&order[from], &order[from + 1], (size_t)(to - from) * sizeof *order);
    else if (to < from)
        memmove(&order[to + 1], &order[to], (size_t)(from - to) * sizeof *order);
    order[to] = id;
}

int opr_sorter_move(opr_undo *u, owf_deck *deck, opr_pick *pick, int gap, int *selected)
{
    int *list, *order = NULL, k, n, i, before = 0, dest, rc = 0, focus_id, focus = -1;
    if (!u || !deck || !pick || gap < 0 || gap > deck->nslides)
        return -1;
    list = picked(deck, pick, &k);
    if (k < 0)
        return -1;
    if (!k)
        return -2;
    n = deck->nslides;
    for (i = 0; i < k; i++)
        if (list[i] < gap) before++;
    dest = gap - before;
    /* nothing to do when the picked slides already sit together there */
    if (list[k - 1] - list[0] == k - 1 && list[0] == dest) {
        if (selected) *selected = pick->focus >= 0 ? pick->focus : list[0];
        owf_free(list);
        return 0;
    }
    order = owf_malloc((size_t)n * sizeof *order);
    if (!order || opr_undo_group_begin(u, k) < 0) {
        owf_free(order);
        owf_free(list);
        return -1;
    }
    for (i = 0; i < n; i++)
        order[i] = i;
    focus_id = opr_pick_has(pick, pick->focus) ? pick->focus : list[0];
    /* those before the gap last first, then those after it first first: no
     * move then disturbs a slide already put in its place */
    for (i = before - 1; i >= 0 && !rc; i--) {
        int from = where(order, n, list[i]);
        if (from != dest + i) rc = opr_slide_move(u, deck, from, dest + i, NULL);
        if (!rc) order_move(order, from, dest + i);
    }
    for (i = before; i < k && !rc; i++) {
        int from = where(order, n, list[i]);
        if (from != dest + i) rc = opr_slide_move(u, deck, from, dest + i, NULL);
        if (!rc) order_move(order, from, dest + i);
    }
    if (rc) {
        opr_undo_group_cancel(u, deck);
        owf_free(order);
        owf_free(list);
        return -1;
    }
    if (opr_undo_group_end(u) < 0)
        rc = -1;
    focus = where(order, n, focus_id);
    pick_range(pick, dest, dest + k - 1, focus);
    if (selected) *selected = focus;
    owf_free(order);
    owf_free(list);
    return rc;
}

int opr_sorter_duplicate(opr_undo *u, owf_deck *deck, opr_pick *pick, int *selected)
{
    int *list, k, i, last, rc = 0;
    if (!u || !deck || !pick)
        return -1;
    list = picked(deck, pick, &k);
    if (k < 0)
        return -1;
    if (!k)
        return -2;
    last = list[k - 1];
    if (opr_undo_group_begin(u, k) < 0) {
        owf_free(list);
        return -1;
    }
    for (i = 0; i < k && !rc; i++)
        rc = opr_slide_duplicate(u, deck, list[i], last + 1 + i, NULL);
    owf_free(list);
    if (rc) {
        opr_undo_group_cancel(u, deck);
        opr_pick_resize(pick, deck->nslides);
        return -1;
    }
    rc = opr_undo_group_end(u) < 0 ? -1 : 0;
    if (opr_pick_resize(pick, deck->nslides) < 0)
        return -1;
    pick_range(pick, last + 1, last + k, last + 1);
    if (selected) *selected = last + 1;
    return rc;
}

int opr_sorter_delete(opr_undo *u, owf_deck *deck, opr_pick *pick, int *selected)
{
    int *list, k, i, last, after, rc = 0;
    if (!u || !deck || !pick)
        return -1;
    list = picked(deck, pick, &k);
    if (k < 0)
        return -1;
    if (!k || k >= deck->nslides) {
        owf_free(list);
        return -2;
    }
    last = list[k - 1];
    if (opr_undo_group_begin(u, k) < 0) {
        owf_free(list);
        return -1;
    }
    for (i = k - 1; i >= 0 && !rc; i--)
        rc = opr_slide_delete(u, deck, list[i], NULL);
    owf_free(list);
    if (rc) {
        opr_undo_group_cancel(u, deck);
        return -1;
    }
    rc = opr_undo_group_end(u) < 0 ? -1 : 0;
    after = last + 1 - k;
    if (after >= deck->nslides) after = deck->nslides - 1;
    if (opr_pick_resize(pick, deck->nslides) < 0)
        return -1;
    opr_pick_only(pick, after);
    if (selected) *selected = after;
    return rc;
}

int opr_sorter_set_hidden(opr_undo *u, owf_deck *deck, opr_pick *pick, int hidden, int *selected)
{
    int *list, k, i, changes = 0, rc = 0;
    if (!u || !deck || !pick)
        return -1;
    list = picked(deck, pick, &k);
    if (k < 0)
        return -1;
    if (!k)
        return -2;
    hidden = hidden != 0;
    for (i = 0; i < k; i++)
        changes += deck->slides[list[i]].hidden != hidden;
    if (changes && opr_undo_group_begin(u, changes) < 0) {
        owf_free(list);
        return -1;
    }
    for (i = 0; i < k && changes && !rc; i++)
        if (deck->slides[list[i]].hidden != hidden)
            rc = opr_slide_set_hidden(u, deck, list[i], hidden, NULL);
    if (selected) *selected = opr_pick_has(pick, pick->focus) ? pick->focus : list[0];
    owf_free(list);
    if (!changes)
        return 0;
    if (rc) {
        opr_undo_group_cancel(u, deck);
        return -1;
    }
    return opr_undo_group_end(u) < 0 ? -1 : 0;
}
