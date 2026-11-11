/*
 * GFOpenPresentation's core: several objects at once (P5). Objects picked
 * together are moved, aligned and spaced as one Undo step each; a group
 * moves with everything in it. Grouping itself is in opr_edit.c.
 *
 * An object here is a top-level one (a group stands for its whole block).
 * Boxes are the drawn ones (owf_deck_resolve), in slide twips.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <stdlib.h>
#include <string.h>

/* An object's drawn box; a group's is the box round what is drawn in it. */
static int box_of(const owf_deck *deck, int slide, int obj, int box[4])
{
    owf_obj r;
    if (obj >= 0 && obj < deck->slides[slide].nobjs && deck->slides[slide].objs[obj].kind == OWF_OBJ_GROUP) {
        int end = opr_obj_block_end(deck, slide, obj), i, b[4], found = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (i = obj + 1; i < end; i++) {
            if (deck->slides[slide].objs[i].kind == OWF_OBJ_GROUP || box_of(deck, slide, i, b) < 0)
                continue;
            if (!found || b[0] < x0) x0 = b[0];
            if (!found || b[1] < y0) y0 = b[1];
            if (!found || b[0] + b[2] > x1) x1 = b[0] + b[2];
            if (!found || b[1] + b[3] > y1) y1 = b[1] + b[3];
            found = 1;
        }
        if (!found)
            return -1;
        box[0] = x0;
        box[1] = y0;
        box[2] = x1 - x0;
        box[3] = y1 - y0;
        return 0;
    }
    if (owf_deck_resolve(deck, slide, obj, &r, NULL) < 0 || r.width < 1 || r.height < 1)
        return -1;
    box[0] = r.x;
    box[1] = r.y;
    box[2] = r.width;
    box[3] = r.height;
    return 0;
}

int opr_objs_box(const owf_deck *deck, int slide, const int *objs, int n, int box[4])
{
    int i, b[4], x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!deck || slide < 0 || slide >= deck->nslides || !objs || n < 1 || !box)
        return -1;
    for (i = 0; i < n; i++) {
        if (objs[i] < 0 || objs[i] >= deck->slides[slide].nobjs || box_of(deck, slide, objs[i], b) < 0)
            return -1;
        if (!i || b[0] < x0) x0 = b[0];
        if (!i || b[1] < y0) y0 = b[1];
        if (!i || b[0] + b[2] > x1) x1 = b[0] + b[2];
        if (!i || b[1] + b[3] > y1) y1 = b[1] + b[3];
    }
    box[0] = x0;
    box[1] = y0;
    box[2] = x1 - x0;
    box[3] = y1 - y0;
    return 0;
}

/* Steps a move of these objects takes: one for each object and everything in it. */
static int steps(const owf_deck *deck, int slide, const int *objs, int n)
{
    int i, total = 0;
    for (i = 0; i < n; i++)
        total += opr_obj_block_end(deck, slide, objs[i]) - objs[i];
    return total;
}

/* One object and everything in it moved by dx, dy, inside an open Undo group. */
static int move_block(opr_undo *u, owf_deck *deck, int slide, int obj, int dx, int dy)
{
    int end = opr_obj_block_end(deck, slide, obj), i, b[4], rc;
    if (end < 0)
        return -1;
    for (i = obj; i < end; i++) {
        if (box_of(deck, slide, i, b) < 0)
            continue;            /* nothing drawn: nothing to move */
        rc = opr_obj_set_box(u, deck, slide, i, b[0] + dx, b[1] + dy, b[2], b[3]);
        if (rc < 0)
            return rc;
    }
    return 0;
}

/* Each object's own move, all as one Undo step. */
static int move_each(opr_undo *u, owf_deck *deck, int slide, const int *objs, int n, const int *dx,
                     const int *dy)
{
    int i, rc = 0;
    if (opr_undo_group_begin(u, steps(deck, slide, objs, n) + 1) < 0)
        return -1;
    for (i = 0; i < n && !rc; i++)
        if (dx[i] || dy[i])
            rc = move_block(u, deck, slide, objs[i], dx[i], dy[i]);
    if (rc < 0) {
        opr_undo_group_cancel(u, deck);
        return rc;
    }
    return opr_undo_group_end(u);
}

/* Objects the caller may move together: valid, top-level, no two the same. */
static int valid(const owf_deck *deck, int slide, const int *objs, int n)
{
    int i, j;
    if (!deck || slide < 0 || slide >= deck->nslides || !objs || n < 1 || n > 10000)
        return 0;
    for (i = 0; i < n; i++) {
        if (objs[i] < 0 || objs[i] >= deck->slides[slide].nobjs || deck->slides[slide].objs[objs[i]].parent >= 0)
            return 0;
        for (j = 0; j < i; j++)
            if (objs[j] == objs[i])
                return 0;
    }
    return 1;
}

int opr_objs_move(opr_undo *u, owf_deck *deck, int slide, const int *objs, int n, int dx, int dy)
{
    int box[4], *d, i, rc;
    if (!u || !valid(deck, slide, objs, n) || opr_objs_box(deck, slide, objs, n, box) < 0)
        return -1;
    /* kept on the slide, as a single object's move is */
    if (box[0] + dx < 0) dx = -box[0];
    if (box[1] + dy < 0) dy = -box[1];
    if ((long)box[0] + box[2] + dx > deck->width) dx = deck->width - box[0] - box[2];
    if ((long)box[1] + box[3] + dy > deck->height) dy = deck->height - box[1] - box[3];
    if (!dx && !dy)
        return 0;
    d = malloc((size_t)n * 2 * sizeof *d);
    if (!d)
        return -1;
    for (i = 0; i < n; i++) {
        d[i] = dx;
        d[n + i] = dy;
    }
    rc = move_each(u, deck, slide, objs, n, d, d + n);
    free(d);
    return rc;
}

int opr_objs_align(opr_undo *u, owf_deck *deck, int slide, const int *objs, int n, opr_align how)
{
    int area[4], b[4], *d, i, rc;
    if (!u || !valid(deck, slide, objs, n))
        return -1;
    if (how < OPR_ALIGN_LEFT || how > OPR_ALIGN_BOTTOM)
        return -2;
    /* one object to the slide, several to the box round them all */
    if (n == 1) {
        area[0] = area[1] = 0;
        area[2] = deck->width;
        area[3] = deck->height;
    } else if (opr_objs_box(deck, slide, objs, n, area) < 0)
        return -1;
    d = malloc((size_t)n * 2 * sizeof *d);
    if (!d)
        return -1;
    for (i = 0; i < n; i++) {
        d[i] = d[n + i] = 0;
        if (box_of(deck, slide, objs[i], b) < 0) {
            free(d);
            return -2;
        }
        if (how == OPR_ALIGN_LEFT)
            d[i] = area[0] - b[0];
        else if (how == OPR_ALIGN_CENTRE)
            d[i] = area[0] + (area[2] - b[2]) / 2 - b[0];
        else if (how == OPR_ALIGN_RIGHT)
            d[i] = area[0] + area[2] - b[2] - b[0];
        else if (how == OPR_ALIGN_TOP)
            d[n + i] = area[1] - b[1];
        else if (how == OPR_ALIGN_MIDDLE)
            d[n + i] = area[1] + (area[3] - b[3]) / 2 - b[1];
        else
            d[n + i] = area[1] + area[3] - b[3] - b[1];
    }
    rc = move_each(u, deck, slide, objs, n, d, d + n);
    free(d);
    return rc;
}

typedef struct {
    int index;                   /* into objs */
    int start, length;
} span;

static int span_order(const void *a, const void *b)
{
    const span *x = a, *y = b;
    if (x->start != y->start)
        return x->start < y->start ? -1 : 1;
    return x->index < y->index ? -1 : x->index > y->index;
}

int opr_objs_distribute(opr_undo *u, owf_deck *deck, int slide, const int *objs, int n, int vertical)
{
    span *sp;
    int *d, i, b[4], rc;
    long room, gap, at, extra;
    if (!u || !valid(deck, slide, objs, n))
        return -1;
    if (n < 3)
        return -2;
    sp = malloc((size_t)n * sizeof *sp);
    d = calloc((size_t)n * 2, sizeof *d);
    if (!sp || !d) {
        free(sp);
        free(d);
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (box_of(deck, slide, objs[i], b) < 0) {
            free(sp);
            free(d);
            return -2;
        }
        sp[i].index = i;
        sp[i].start = vertical ? b[1] : b[0];
        sp[i].length = vertical ? b[3] : b[2];
    }
    qsort(sp, (size_t)n, sizeof *sp, span_order);
    /* the first and the last stay; the space between the others is made equal */
    room = (long)sp[n - 1].start + sp[n - 1].length - sp[0].start;
    for (i = 0; i < n; i++)
        room -= sp[i].length;
    gap = room / (n - 1);
    extra = room - gap * (n - 1);    /* twips left over: one more in each early gap */
    at = (long)sp[0].start + sp[0].length;
    for (i = 1; i < n - 1; i++) {
        at += gap + (i <= extra ? 1 : 0);
        if (room < 0 && at < 0)
            at = 0;
        d[(vertical ? n : 0) + sp[i].index] = (int)(at - sp[i].start);
        at += sp[i].length;
    }
    rc = move_each(u, deck, slide, objs, n, d, d + n);
    free(sp);
    free(d);
    return rc;
}

int opr_objs_in_rect(const owf_deck *deck, int slide, int x, int y, int width, int height, int *out, int max)
{
    const owf_slide *s;
    int i, b[4], count = 0;
    if (!deck || slide < 0 || slide >= deck->nslides || width < 0 || height < 0)
        return -1;
    s = &deck->slides[slide];
    for (i = 0; i < s->nobjs; i++) {
        if (s->objs[i].parent >= 0 || (s->objs[i].flags & OWF_OBJ_HIDDEN) ||
            opr_objs_box(deck, slide, &i, 1, b) < 0)
            continue;
        if (b[0] >= x && b[1] >= y && (long)b[0] + b[2] <= (long)x + width &&
            (long)b[1] + b[3] <= (long)y + height) {
            if (out && count < max)
                out[count] = i;
            count++;
        }
    }
    return count;
}
