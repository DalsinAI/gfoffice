/*
 * owf_shape.c: GFOffice's one copy of shape geometry (owf_shape.h, docs/suite/owf-shared-core.md). Moved from
 * owf_dshape.c, where GFOpenDiag's shapes were built with their own copy; the diagram's outlines are the same
 * points as before (tests/diagram/test_dshape.c, tests/shared/test_owf_shape.c).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_shape.h"

#include <string.h>

typedef long long wide;

/* sin of k/64 of a turn, k 0 to 16 (a quarter), times 10000 */
static const int quarter[17] = { 0, 980, 1951, 2903, 3827, 4714, 5556, 6344, 7071, 7730, 8315, 8819, 9239, 9569,
                                 9808, 9952, 10000 };

int owf_shape_sin64(int a)
{
    a %= 64;
    if (a < 0)
        a += 64;
    if (a <= 16) return quarter[a];
    if (a <= 32) return quarter[32 - a];
    if (a <= 48) return -quarter[a - 32];
    return -quarter[64 - a];
}

int owf_shape_cos64(int a)
{
    return owf_shape_sin64(a + 16);
}

void owf_shape_pen_init(owf_shape_pen *pen, owf_shape *s, const owf_snum box[4], int turn, int flip_h, int flip_v)
{
    memset(s, 0, sizeof *s);
    memset(pen, 0, sizeof *pen);
    pen->s = s;
    pen->bx = box[0];
    pen->by = box[1];
    pen->bw = box[2];
    pen->bh = box[3];
    pen->turn = turn & 3;
    pen->flip_h = flip_h != 0;
    pen->flip_v = flip_v != 0;
    pen->w = pen->turn % 2 ? box[3] : box[2];   /* built pointing east, then turned to fill the box */
    pen->h = pen->turn % 2 ? box[2] : box[3];
}

void owf_shape_start(owf_shape_pen *pen, unsigned flags)
{
    owf_shape *s = pen->s;
    if (s->nparts == OWF_SHAPE_PARTS) {
        pen->full = 1;
        return;
    }
    s->parts[s->nparts].first = s->npoints;
    s->parts[s->nparts].n = 0;
    s->parts[s->nparts].flags = flags;
    s->nparts++;
}

void owf_shape_point(owf_shape_pen *pen, owf_snum x, owf_snum y)
{
    owf_shape *s = pen->s;
    owf_snum px, py;
    if (pen->full || !s->nparts || s->npoints == OWF_SHAPE_POINTS) {
        pen->full = 1;
        return;
    }
    switch (pen->turn) {
    case 1: px = pen->h - y; py = x; break;             /* south: east turns to point down */
    case 2: px = pen->w - x; py = pen->h - y; break;    /* west */
    case 3: px = y; py = pen->w - x; break;             /* north */
    default: px = x; py = y; break;
    }
    if (pen->flip_h) px = pen->bw - px;
    if (pen->flip_v) py = pen->bh - py;
    s->points[s->npoints].x = pen->bx + px;
    s->points[s->npoints].y = pen->by + py;
    s->npoints++;
    s->parts[s->nparts - 1].n++;
}

void owf_shape_arc(owf_shape_pen *pen, owf_snum cx, owf_snum cy, owf_snum rx, owf_snum ry, int a0, int a1, int step)
{
    int a = a0, dir = a1 >= a0 ? step : -step;
    for (;;) {
        owf_shape_point(pen, cx + (owf_snum)((wide)rx * owf_shape_cos64(a) / 10000),
                        cy + (owf_snum)((wide)ry * owf_shape_sin64(a) / 10000));
        if (a == a1)
            break;
        a += dir;
        if ((dir > 0 && a > a1) || (dir < 0 && a < a1))
            a = a1;
    }
}

void owf_shape_rect(owf_shape_pen *pen, unsigned flags, owf_snum x0, owf_snum y0, owf_snum x1, owf_snum y1)
{
    owf_shape_start(pen, flags);
    owf_shape_point(pen, x0, y0);
    owf_shape_point(pen, x1, y0);
    owf_shape_point(pen, x1, y1);
    owf_shape_point(pen, x0, y1);
}

void owf_shape_line(owf_shape_pen *pen, unsigned flags, owf_snum x0, owf_snum y0, owf_snum x1, owf_snum y1)
{
    owf_shape_start(pen, OWF_SHAPE_LINE | flags);
    owf_shape_point(pen, x0, y0);
    owf_shape_point(pen, x1, y1);
}

void owf_shape_oval(owf_shape_pen *pen, unsigned flags, owf_snum cx, owf_snum cy, owf_snum rx, owf_snum ry, int step)
{
    owf_shape_start(pen, flags);
    owf_shape_arc(pen, cx, cy, rx, ry, 0, 64 - step, step);
}

int owf_shape_xy(const owf_shape *s, int part, long mul, long div, long ox, long oy, int *xy, int max)
{
    const owf_shape_part *p;
    int i, n;
    if (!s || part < 0 || part >= s->nparts || div == 0 || !xy || max <= 0)
        return 0;
    p = &s->parts[part];
    n = p->n < max ? p->n : max;
    for (i = 0; i < n; i++) {
        const owf_spoint *q = &s->points[p->first + i];
        xy[2 * i] = (int)(ox + (wide)q->x * mul / div);
        xy[2 * i + 1] = (int)(oy + (wide)q->y * mul / div);
    }
    return n;
}
