/*
 * owf_shape.h: GFOffice's one copy of shape geometry (docs/suite/owf-shared-core.md): outlines built as parts of
 * points, turned and flipped into a box, with the curves as runs of short lines from one sine table. Integer
 * arithmetic only, no Amiga or GFOpenLayout headers, so any library or program builds it alone.
 *
 * A shape is up to OWF_SHAPE_PARTS parts in drawing order, each a run of points (owf_shape_part: closed outlines
 * to fill and stroke, lines to stroke). A pen (owf_shape_pen) builds one: the shape is drawn pointing east, w across
 * and h down, and the pen turns it a quarter at a time clockwise and flips it to fill its box. The diagram's shapes
 * (owf_dshape.c, owf_diagram.h's owf_dshape is an owf_shape) are the first built on it; the deck's and .pub's
 * shapes follow. owf_shape_xy hands one part to code that wants plain int points in its own units (a fill's
 * outline: owf_fill.h's gradient bands and hatches take xy lists).
 *
 * Units are the caller's: the diagram's are hundredths of a page pixel (owf_dnum).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_SHAPE_H
#define OWF_SHAPE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef long owf_snum;

typedef struct {
    owf_snum x, y;
} owf_spoint;

#define OWF_SHAPE_POINTS 160
#define OWF_SHAPE_PARTS 12
#define OWF_SHAPE_CLOSED 0x01u           /* the part's last point joins its first */
#define OWF_SHAPE_FILL   0x02u           /* filled with the fill colour */
#define OWF_SHAPE_LINE   0x04u           /* drawn round with the line colour */
#define OWF_SHAPE_INK    0x08u           /* filled with the line colour (a dot, a light, a socket) */
#define OWF_SHAPE_DASHED 0x10u           /* its line dashed whatever the style says (a lifeline) */

typedef struct {
    int first, n;                        /* its points: points[first .. first + n - 1] */
    unsigned flags;                      /* OWF_SHAPE_ */
} owf_shape_part;

typedef struct {
    owf_spoint points[OWF_SHAPE_POINTS]; /* in the box's units, where the box is */
    int npoints;
    owf_shape_part parts[OWF_SHAPE_PARTS];   /* in drawing order */
    int nparts;
} owf_shape;

/* Building one: the box it fills (bx, by, bw, bh), the shape as built pointing east (w across, h down), quarter
 * turns clockwise (0 east, 1 south, 2 west, 3 north) and flips. full is set when a part or point didn't fit. */
typedef struct {
    owf_shape *s;
    owf_snum bx, by, bw, bh;
    owf_snum w, h;
    int turn;
    int flip_h, flip_v;
    int full;
} owf_shape_pen;

/* sin and cos of a/64 of a turn, times 10000; a turns clockwise on a page (y down), 0 east, 16 south. */
int owf_shape_sin64(int a);
int owf_shape_cos64(int a);

/* An empty s, and a pen on it for box (x, y, width, height) turned turn quarters and flipped. */
void owf_shape_pen_init(owf_shape_pen *pen, owf_shape *s, const owf_snum box[4], int turn, int flip_h, int flip_v);
/* A new part with flags (OWF_SHAPE_). */
void owf_shape_start(owf_shape_pen *pen, unsigned flags);
/* A point of the shape as built, turned and flipped into the box, onto the last part. */
void owf_shape_point(owf_shape_pen *pen, owf_snum x, owf_snum y);
/* An arc of the ellipse round cx, cy from angle a0 to a1 (64ths of a turn, either way), a point each step. */
void owf_shape_arc(owf_shape_pen *pen, owf_snum cx, owf_snum cy, owf_snum rx, owf_snum ry, int a0, int a1, int step);
/* A box from x0, y0 to x1, y1 as a part of its own; a line as one; a whole ellipse as one, a point each step. */
void owf_shape_rect(owf_shape_pen *pen, unsigned flags, owf_snum x0, owf_snum y0, owf_snum x1, owf_snum y1);
void owf_shape_line(owf_shape_pen *pen, unsigned flags, owf_snum x0, owf_snum y0, owf_snum x1, owf_snum y1);
void owf_shape_oval(owf_shape_pen *pen, unsigned flags, owf_snum cx, owf_snum cy, owf_snum rx, owf_snum ry,
                    int step);

/* Part part of s as plain int points in the caller's units: x' = ox + x * mul / div, the same for y, into xy as
 * x, y pairs, at most max points. Returns the points written; 0 when there is no such part or div is 0. */
int owf_shape_xy(const owf_shape *s, int part, long mul, long div, long ox, long oy, int *xy, int max);

#ifdef __cplusplus
}
#endif

#endif
