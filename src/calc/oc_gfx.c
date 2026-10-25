/*
 * OpenCalc's core: the charts' drawer that makes draw items (oc_gfx.h).
 * Lines are cut to the clip box (Cohen and Sutherland) and made of
 * 1-pixel segments (OC_DRAW_SEGMENT), a wide one of several side by side;
 * polygons are cut to it (Sutherland and Hodgman), a box becoming a fill
 * (OC_DRAW_FILL) and anything else a polygon (OC_DRAW_POLY) the window
 * fills; text is an OC_DRAW_TEXT item in its box.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_gfx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POLY 256                     /* points a polygon keeps once cut */

static int outcode(const oc_gfx_items *p, double x, double y)
{
    return (x < p->cx) | (x > p->cx + p->cw - 1) << 1 | (y < p->cy) << 2 | (y > p->cy + p->ch - 1) << 3;
}

/* A 1-pixel line, cut to the clip box. */
static void segment(oc_gfx_items *p, double x0, double y0, double x1, double y1, unsigned long rgb)
{
    int c0 = outcode(p, x0, y0), c1 = outcode(p, x1, y1), tries = 0;
    oc_draw_item *it;
    while ((c0 | c1) && tries++ < 8) {
        int c = c0 ? c0 : c1;
        double x, y, xmin = p->cx, xmax = p->cx + p->cw - 1, ymin = p->cy, ymax = p->cy + p->ch - 1;
        if (c0 & c1)
            return;
        if (c & 8) {
            x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0);
            y = ymax;
        } else if (c & 4) {
            x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0);
            y = ymin;
        } else if (c & 2) {
            y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0);
            x = xmax;
        } else {
            y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0);
            x = xmin;
        }
        if (c == c0) {
            x0 = x;
            y0 = y;
            c0 = outcode(p, x0, y0);
        } else {
            x1 = x;
            y1 = y;
            c1 = outcode(p, x1, y1);
        }
    }
    if ((c0 | c1) || p->g.failed)
        return;
    if (!(it = oc_draw_add(p->list, OC_DRAW_SEGMENT, OC_ROLE_CHART))) {
        p->g.failed = 1;
        return;
    }
    it->x = (int)floor(x0 + 0.5);
    it->y = (int)floor(y0 + 0.5);
    it->w = (int)floor(x1 + 0.5) - it->x;
    it->h = (int)floor(y1 + 0.5) - it->y;
    it->rgb = rgb;
}

static void items_line(oc_gfx *g, int x0, int y0, int x1, int y1, int width, unsigned long rgb)
{
    oc_gfx_items *p = (oc_gfx_items *)g;
    int k, flat = abs(x1 - x0) >= abs(y1 - y0);
    if (width < 1)
        width = 1;
    for (k = 0; k < width; k++) {
        int o = k - (width - 1) / 2;
        if (flat)
            segment(p, x0, y0 + o, x1, y1 + o, rgb);
        else
            segment(p, x0 + o, y0, x1 + o, y1, rgb);
    }
}

/* The polygon in (n points) cut by one side of the clip box into out; how many points. */
static int cut(const double *in, int n, double *out, int side, double at)
{
    int i, k = 0;
    for (i = 0; i < n && k < 2 * MAX_POLY - 2; i++) {
        const double *a = in + 2 * ((i + n - 1) % n), *b = in + 2 * i;
        double va = side < 2 ? a[0] : a[1], vb = side < 2 ? b[0] : b[1];
        int ina = side & 1 ? va <= at : va >= at, inb = side & 1 ? vb <= at : vb >= at;
        if (ina != inb) {
            double t = (at - va) / (vb - va);
            out[2 * k] = side < 2 ? at : a[0] + (b[0] - a[0]) * t;
            out[2 * k + 1] = side < 2 ? a[1] + (b[1] - a[1]) * t : at;
            k++;
        }
        if (inb) {
            out[2 * k] = b[0];
            out[2 * k + 1] = b[1];
            k++;
        }
    }
    return k;
}

static void items_polygon(oc_gfx *g, const int *xy, int n, unsigned long rgb)
{
    oc_gfx_items *p = (oc_gfx_items *)g;
    static const int sides[4] = { 0, 1, 2, 3 };
    double *a, *b, *t;
    int i, m = n, *pts;
    oc_draw_item *it;
    if (n < 3 || n > MAX_POLY || g->failed)
        return;
    /* a box, as its edges go round: a fill */
    if (n == 4 && xy[1] == xy[3] && xy[2] == xy[4] && xy[5] == xy[7] && xy[6] == xy[0] && xy[2] > xy[0] && xy[5] > xy[1]) {
        int x = xy[0] > p->cx ? xy[0] : p->cx, y = xy[1] > p->cy ? xy[1] : p->cy;
        int x1 = xy[2] < p->cx + p->cw ? xy[2] : p->cx + p->cw, y1 = xy[5] < p->cy + p->ch ? xy[5] : p->cy + p->ch;
        if (x1 <= x || y1 <= y)
            return;
        if (!(it = oc_draw_add(p->list, OC_DRAW_FILL, OC_ROLE_CHART))) {
            g->failed = 1;
            return;
        }
        it->x = x;
        it->y = y;
        it->w = x1 - x;
        it->h = y1 - y;
        it->rgb = rgb;
        return;
    }
    a = malloc(4 * MAX_POLY * sizeof *a);
    b = malloc(4 * MAX_POLY * sizeof *b);
    if (!a || !b) {
        free(a);
        free(b);
        g->failed = 1;
        return;
    }
    for (i = 0; i < 2 * n; i++)
        a[i] = xy[i];
    for (i = 0; i < 4 && m >= 3; i++) {
        double at = sides[i] == 0 ? p->cx : sides[i] == 1 ? p->cx + p->cw : sides[i] == 2 ? p->cy : p->cy + p->ch;
        m = cut(a, m, b, sides[i], at);
        t = a;
        a = b;
        b = t;
    }
    if (m >= 3 && (it = oc_draw_add(p->list, OC_DRAW_POLY, OC_ROLE_CHART)) != NULL) {
        int x0 = 32767, y0 = 32767, x1 = -32768, y1 = -32768;
        pts = malloc((size_t)m * 2 * sizeof *pts);
        if (!pts) {
            g->failed = 1;
            p->list->n--;                    /* the item goes again */
        } else {
            for (i = 0; i < m; i++) {
                pts[2 * i] = (int)floor(a[2 * i] + 0.5);
                pts[2 * i + 1] = (int)floor(a[2 * i + 1] + 0.5);
                if (pts[2 * i] < x0)
                    x0 = pts[2 * i];
                if (pts[2 * i] > x1)
                    x1 = pts[2 * i];
                if (pts[2 * i + 1] < y0)
                    y0 = pts[2 * i + 1];
                if (pts[2 * i + 1] > y1)
                    y1 = pts[2 * i + 1];
            }
            it->x = x0;
            it->y = y0;
            it->w = x1 - x0;
            it->h = y1 - y0;
            it->rgb = rgb;
            if (!oc_draw_add_points(p->list, it, pts, m))
                g->failed = 1;
            free(pts);
        }
    } else if (m >= 3) {
        g->failed = 1;
    }
    free(a);
    free(b);
}

static int items_text_width(oc_gfx *g, const char *s, int twips, unsigned flags)
{
    oc_gfx_items *p = (oc_gfx_items *)g;
    p->style.font.size = twips;
    p->style.font.flags = flags;
    return oc_measure(p->core, s, strlen(s), &p->style);
}

static void items_text(oc_gfx *g, const char *s, int x, int y, int w, int h, int align, int twips, unsigned flags,
                       unsigned long rgb)
{
    oc_gfx_items *p = (oc_gfx_items *)g;
    oc_draw_item *it;
    int tw, l = x > p->cx ? x : p->cx, r = x + w < p->cx + p->cw ? x + w : p->cx + p->cw;
    if (!*s || g->failed || y < p->cy || y + h > p->cy + p->ch || r <= l)
        return;
    tw = items_text_width(g, s, twips, flags);
    if (!(it = oc_draw_add(p->list, OC_DRAW_TEXT, OC_ROLE_TEXT)) || !oc_draw_add_text(p->list, it, s, strlen(s))) {
        g->failed = 1;
        return;
    }
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->clip_x = l;
    it->clip_y = y;
    it->clip_w = r - l;
    it->clip_h = h;
    it->align = align;
    it->valign = OWF_VALIGN_CENTRE;
    it->text_x = align == OC_ALIGN_CENTRE ? x + (w - tw) / 2 : align == OC_ALIGN_RIGHT ? x + w - tw : x;
    it->flags = flags;
    it->size = twips;
    it->rgb = rgb;
}

void oc_gfx_items_init(oc_gfx_items *gi, oc_core *core, oc_draw_list *list, int cx, int cy, int cw, int ch)
{
    memset(gi, 0, sizeof *gi);
    gi->g.line = items_line;
    gi->g.polygon = items_polygon;
    gi->g.text = items_text;
    gi->g.text_width = items_text_width;
    gi->core = core;
    gi->list = list;
    gi->cx = cx;
    gi->cy = cy;
    gi->cw = cw;
    gi->ch = ch;
    gi->style.font.font = -1;
}

void oc_gfx_box(oc_gfx *g, int x, int y, int w, int h, unsigned long rgb)
{
    int xy[8];
    if (w <= 0 || h <= 0)
        return;
    xy[0] = x;
    xy[1] = y;
    xy[2] = x + w;
    xy[3] = y;
    xy[4] = x + w;
    xy[5] = y + h;
    xy[6] = x;
    xy[7] = y + h;
    g->polygon(g, xy, 4, rgb);
}
