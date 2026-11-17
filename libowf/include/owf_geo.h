/*
 * owf_geo.h: GFOffice's one copy of shape geometry: outlines built as parts of
 * points, turned and flipped into a box, with the curves as runs of short lines from one sine table. Integer
 * arithmetic only, no Amiga or GFOpenLayout headers, so any library or program builds it alone.
 *
 * A shape is up to OWF_GEO_PARTS parts in drawing order, each a run of points (owf_geo_part: closed outlines
 * to fill and stroke, lines to stroke). A pen (owf_geo_pen) builds one: the shape is drawn pointing east, w across
 * and h down, and the pen turns it a quarter at a time clockwise and flips it to fill its box. The diagram's shapes
 * (owf_dshape.c, owf_diagram.h's owf_dshape is an owf_geo) are the first built on it; the deck's and .pub's
 * shapes follow. owf_geo_xy hands one part to code that wants plain int points in its own units (a fill's
 * outline: a fill's gradient bands and hatches take xy lists).
 *
 * Named owf_geo, not owf_shape: owf_deck.h's owf_shape (a slide shape's kind) and OWF_SHAPE_ names are the deck's.
 *
 * Units are the caller's: the diagram's are hundredths of a page pixel (owf_dnum).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_GEO_H
#define OWF_GEO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef long owf_gnum;

typedef struct {
    owf_gnum x, y;
} owf_gpoint;

#define OWF_GEO_POINTS 160
#define OWF_GEO_PARTS 12
#define OWF_GEO_CLOSED 0x01u           /* the part's last point joins its first */
#define OWF_GEO_FILL   0x02u           /* filled with the fill colour */
#define OWF_GEO_LINE   0x04u           /* drawn round with the line colour */
#define OWF_GEO_INK    0x08u           /* filled with the line colour (a dot, a light, a socket) */
#define OWF_GEO_DASHED 0x10u           /* its line dashed whatever the style says (a lifeline) */

typedef struct {
    int first, n;                        /* its points: points[first .. first + n - 1] */
    unsigned flags;                      /* OWF_GEO_ */
} owf_geo_part;

typedef struct {
    owf_gpoint points[OWF_GEO_POINTS]; /* in the box's units, where the box is */
    int npoints;
    owf_geo_part parts[OWF_GEO_PARTS];   /* in drawing order */
    int nparts;
} owf_geo;

/* Building one: the box it fills (bx, by, bw, bh), the shape as built pointing east (w across, h down), quarter
 * turns clockwise (0 east, 1 south, 2 west, 3 north) and flips. full is set when a part or point didn't fit. */
typedef struct {
    owf_geo *s;
    owf_gnum bx, by, bw, bh;
    owf_gnum w, h;
    int turn;
    int flip_h, flip_v;
    int full;
} owf_geo_pen;

/* sin and cos of a/64 of a turn, times 10000; a turns clockwise on a page (y down), 0 east, 16 south. */
int owf_geo_sin64(int a);
int owf_geo_cos64(int a);

/* An empty s, and a pen on it for box (x, y, width, height) turned turn quarters and flipped. */
void owf_geo_pen_init(owf_geo_pen *pen, owf_geo *s, const owf_gnum box[4], int turn, int flip_h, int flip_v);
/* A new part with flags (OWF_GEO_). */
void owf_geo_start(owf_geo_pen *pen, unsigned flags);
/* A point of the shape as built, turned and flipped into the box, onto the last part. */
void owf_geo_point(owf_geo_pen *pen, owf_gnum x, owf_gnum y);
/* An arc of the ellipse round cx, cy from angle a0 to a1 (64ths of a turn, either way), a point each step. */
void owf_geo_arc(owf_geo_pen *pen, owf_gnum cx, owf_gnum cy, owf_gnum rx, owf_gnum ry, int a0, int a1, int step);
/* A box from x0, y0 to x1, y1 as a part of its own; a line as one; a whole ellipse as one, a point each step. */
void owf_geo_rect(owf_geo_pen *pen, unsigned flags, owf_gnum x0, owf_gnum y0, owf_gnum x1, owf_gnum y1);
void owf_geo_line(owf_geo_pen *pen, unsigned flags, owf_gnum x0, owf_gnum y0, owf_gnum x1, owf_gnum y1);
void owf_geo_oval(owf_geo_pen *pen, unsigned flags, owf_gnum cx, owf_gnum cy, owf_gnum rx, owf_gnum ry,
                    int step);

/* Part part of s as plain int points in the caller's units: x' = ox + x * mul / div, the same for y, into xy as
 * x, y pairs, at most max points. Returns the points written; 0 when there is no such part or div is 0. */
int owf_geo_xy(const owf_geo *s, int part, long mul, long div, long ox, long oy, int *xy, int max);

#ifdef __cplusplus
}
#endif

#endif
