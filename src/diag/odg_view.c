/*
 * odg_view.c: GFOpenDiag's canvas (opendiag_view.h). The page is drawn as owf_dsvg.c writes it as SVG
 * (the same shapes, colours, arrows and label places), but into an oldraw at the view's zoom, so the
 * window and an exported picture agree. Everything is whole numbers.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opendiag_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long wide;

#define FAR (1L << 28)                   /* the furthest a pixel goes off the view: inside oldraw's reach */
#define MAX_LINES 32
#define MAX_PTS 64

static wide floor_div(wide a, wide b)
{
    wide q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

static long far_cut(wide v) { return (long)(v > FAR ? FAR : v < -FAR ? -FAR : v); }

static wide isqrt_wide(wide v)
{
    wide r = 0, bit = (wide)1 << 62;
    if (v <= 0)
        return 0;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* ---- the view's places ---- */

void odg_view_init(odg_view *v, int width, int height)
{
    memset(v, 0, sizeof *v);
    v->width = width;
    v->height = height;
    v->zoom = 100;
    v->grid = 1;
    v->paper = OLDRAW_RGB(0xffffff);
    v->grid_colour = OLDRAW_RGB(0xd0d0d0);
    v->select_colour = OLDRAW_RGB(0x2060e0);
}

static int zoom_of(const odg_view *v)
{
    return v->zoom < ODG_ZOOM_MIN ? ODG_ZOOM_MIN : v->zoom > ODG_ZOOM_MAX ? ODG_ZOOM_MAX : v->zoom;
}

long odg_view_x(const odg_view *v, owf_dnum x)
{
    return far_cut(v->x + floor_div(((wide)x - v->left) * zoom_of(v), 10000));
}

long odg_view_y(const odg_view *v, owf_dnum y)
{
    return far_cut(v->y + floor_div(((wide)y - v->top) * zoom_of(v), 10000));
}
long odg_view_len(const odg_view *v, owf_dnum n) { return far_cut(floor_div((wide)n * zoom_of(v), 10000)); }

owf_dnum odg_page_x(const odg_view *v, long px)
{
    return (owf_dnum)(v->left + floor_div(((wide)px - v->x) * 10000 + zoom_of(v) / 2, zoom_of(v)));
}

owf_dnum odg_page_y(const odg_view *v, long py)
{
    return (owf_dnum)(v->top + floor_div(((wide)py - v->y) * 10000 + zoom_of(v) / 2, zoom_of(v)));
}

void odg_view_rect(const odg_view *v, const owf_dnum box[4], oldraw_rect *r)
{
    long x0 = odg_view_x(v, box[0]), y0 = odg_view_y(v, box[1]);
    long x1 = odg_view_x(v, box[0] + box[2]), y1 = odg_view_y(v, box[1] + box[3]);
    r->x = (int32_t)x0;
    r->y = (int32_t)y0;
    r->w = (int32_t)(x1 - x0 > 0 ? x1 - x0 : 1);
    r->h = (int32_t)(y1 - y0 > 0 ? y1 - y0 : 1);
}

void odg_view_zoom_at(odg_view *v, int zoom, long px, long py)
{
    owf_dnum x = odg_page_x(v, px), y = odg_page_y(v, py);
    v->zoom = zoom < ODG_ZOOM_MIN ? ODG_ZOOM_MIN : zoom > ODG_ZOOM_MAX ? ODG_ZOOM_MAX : zoom;
    v->left = (owf_dnum)(x - floor_div(((wide)px - v->x) * 10000, v->zoom));
    v->top = (owf_dnum)(y - floor_div(((wide)py - v->y) * 10000, v->zoom));
}

void odg_view_fit(odg_view *v, const odg_doc *doc)
{
    owf_dnum b[4], margin = OWF_DNUM(20);
    wide zx, zy, z;
    if (!owf_dpage_bounds(odg_page(doc), b)) {
        v->zoom = 100;
        v->left = v->top = 0;
        return;
    }
    b[0] -= margin;
    b[1] -= margin;
    b[2] += 2 * margin;
    b[3] += 2 * margin;
    zx = (wide)v->width * 10000 / (b[2] > 0 ? b[2] : 1);
    zy = (wide)v->height * 10000 / (b[3] > 0 ? b[3] : 1);
    z = zx < zy ? zx : zy;
    v->zoom = (int)(z > 100 ? 100 : z < ODG_ZOOM_MIN ? ODG_ZOOM_MIN : z);
    /* centred: the drawing's middle at the view's */
    v->left = (owf_dnum)(b[0] + b[2] / 2 - (wide)v->width * 10000 / v->zoom / 2);
    v->top = (owf_dnum)(b[1] + b[3] / 2 - (wide)v->height * 10000 / v->zoom / 2);
}

/* ---- styles ---- */

static int hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

oldraw_colour odg_style_colour(const char *style, const char *key, oldraw_colour def)
{
    char buf[24];
    size_t n, i;
    unsigned long rgb = 0;
    if (!owf_dstyle_get(style, key, buf, sizeof buf) || !buf[0] || !strcmp(buf, "default") || !strcmp(buf, "inherit"))
        return def;
    if (!strcmp(buf, "none"))
        return OLDRAW_NONE;
    n = strlen(buf);
    if (buf[0] != '#' || (n != 7 && n != 4))
        return def;
    for (i = 1; i < n; i++)
        if (hex(buf[i]) < 0)
            return def;
    if (n == 4)
        for (i = 1; i < 4; i++)
            rgb = rgb << 8 | (unsigned long)(hex(buf[i]) * 17);
    else
        for (i = 1; i < 7; i++)
            rgb = rgb << 4 | (unsigned long)hex(buf[i]);
    return OLDRAW_RGB(rgb);
}

static owf_dnum style_num(const char *style, const char *key, owf_dnum def)
{
    char buf[24];
    if (owf_dstyle_get(style, key, buf, sizeof buf) && buf[0])
        return owf_dnum_parse(buf);
    return def;
}

static int style_is(const char *style, const char *key, const char *value)
{
    char buf[24];
    return owf_dstyle_get(style, key, buf, sizeof buf) && !strcmp(buf, value);
}

/* the stroke's width in pixels, one at least */
static int stroke_px(const odg_view *v, const char *style)
{
    long w = odg_view_len(v, style_num(style, "strokeWidth", 100));
    return w < 1 ? 1 : w > 64 ? 64 : (int)w;
}

/* ---- lines, dashed or not ---- */

/* n points as a polyline: dashed as draw.io's "3 3" (three widths on, three off). */
/* odg_draw's area: dashes are only worked out where a segment crosses it, so a long line costs what is shown */
static const oldraw_rect *dash_area;

/* t from *lo to *hi (of len) along one axis (a + d * t / len) that may be within l..h; 0 when none is */
static int cut_axis(wide a, wide d, wide l, wide h, wide len, wide *lo, wide *hi)
{
    wide ta, tb;
    if (!d)
        return a >= l && a <= h;
    ta = floor_div((l - a) * len, d);
    tb = floor_div((h - a) * len, d);
    if (ta > tb) { wide k = ta; ta = tb; tb = k; }
    ta -= 1;                             /* a pixel either way for the rounding */
    tb += 1;
    if (ta > *lo) *lo = ta;
    if (tb < *hi) *hi = tb;
    return *lo <= *hi;
}

static void stroke_path(oldraw *d, const oldraw_point *pts, int n, int closed, int width, int dashed, oldraw_colour c)
{
    int i, segs;
    wide dash, at = 0;
    if (c == OLDRAW_NONE || n < 1)
        return;
    if (!dashed || n < 2) {
        oldraw_polyline(d, pts, n, closed, width, c);
        return;
    }
    dash = 3 * (wide)width;
    segs = closed ? n : n - 1;
    for (i = 0; i < segs; i++) {
        const oldraw_point *a = &pts[i], *b = &pts[(i + 1) % n];
        wide dx = (wide)b->x - a->x, dy = (wide)b->y - a->y, len = isqrt_wide(dx * dx + dy * dy), t = 0;
        wide end = len;
        if (!len)
            continue;
        if (dash_area) {
            wide pad = width + 1, lo = 0, hi = len;
            const oldraw_rect *r = dash_area;
            if (!cut_axis(a->x, dx, (wide)r->x - pad, (wide)r->x + r->w + pad, len, &lo, &hi) ||
                !cut_axis(a->y, dy, (wide)r->y - pad, (wide)r->y + r->h + pad, len, &lo, &hi)) {
                at += len;
                continue;
            }
            t = lo < 0 ? 0 : lo;
            end = hi > len ? len : hi;
        }
        while (t < end) {
            wide phase = (at + t) % (2 * dash), run = phase < dash ? dash - phase : 2 * dash - phase;
            wide t1 = t + run < len ? t + run : len;
            if (phase < dash) {
                wide e = t1 - t > 1 ? t1 - 1 : t1;        /* a dash of three pixels is three, not four */
                oldraw_line(d, (int)(a->x + dx * t / len), (int)(a->y + dy * t / len), (int)(a->x + dx * e / len),
                            (int)(a->y + dy * e / len), width, c);
            }
            t = t1;
        }
        at += len;
    }
}

/* ---- labels ---- */

/* A label's lines at x across (anchor 0 start, 1 middle, 2 end) and y down (valign 0 top: the first line's
 * top at y; 1 middle; 2 bottom: the last line's foot at y), with a background when bg is a colour. */
static void draw_label(oldraw *d, const odg_view *v, const char *value, const char *style, long x, long y, int anchor,
                       int valign, oldraw_colour bg_def)
{
    char *text;
    size_t length;
    oldraw_font font;
    oldraw_text_size size;
    char family[48];
    size_t starts[MAX_LINES], lens[MAX_LINES], at, from = 0;
    int widths[MAX_LINES], nlines = 0, i, widest = 0, ascent = 0, line;
    long fstyle, top, px;
    oldraw_colour colour = odg_style_colour(style, "fontColor", OLDRAW_RGB(0x000000));
    oldraw_colour bg = odg_style_colour(style, "labelBackgroundColor", bg_def);
    unsigned st = 0;

    if (!value || !*value || colour == OLDRAW_NONE)
        return;
    px = odg_view_len(v, style_num(style, "fontSize", 1200));
    if (px < 4)
        return;                          /* too small to read: drawn as nothing, as draw.io does when zoomed out */
    fstyle = style_num(style, "fontStyle", 0) / 100;
    if (fstyle & 1) st |= OLDRAW_BOLD;
    if (fstyle & 2) st |= OLDRAW_ITALIC;
    if (fstyle & 4) st |= OLDRAW_UNDERLINE;
    font.family = v->font;
    if (!font.family && owf_dstyle_get(style, "fontFamily", family, sizeof family) && family[0])
        font.family = family;
    font.px = (int)(px > 400 ? 400 : px);
    font.style = st;

    text = owf_dlabel_text(value, style_is(style, "html", "1"), &length);
    if (!text || !length) {
        owf_free(text);
        return;
    }
    for (at = 0; at <= length && nlines < MAX_LINES; at++)
        if (at == length || text[at] == '\n') {
            starts[nlines] = from;
            lens[nlines] = at - from;
            widths[nlines] = 0;
            if (oldraw_text_measure(d, text + from, at - from, &font, st, &size)) {
                widths[nlines] = size.width;
                if (size.ascent > ascent)
                    ascent = size.ascent;
            }
            if (widths[nlines] > widest)
                widest = widths[nlines];
            nlines++;
            from = at + 1;
        }
    line = font.px * 6 / 5;
    top = valign == 0 ? y : valign == 2 ? y - (long)line * nlines : y - (long)line * nlines / 2;
    if (bg != OLDRAW_NONE) {
        oldraw_rect r;
        r.w = widest + 4;
        r.h = line * nlines;
        r.x = (int32_t)(anchor == 0 ? x - 2 : anchor == 2 ? x - r.w + 2 : x - r.w / 2);
        r.y = (int32_t)top;
        oldraw_fill(d, &r, bg);
    }
    for (i = 0; i < nlines; i++) {
        long lx = anchor == 0 ? x : anchor == 2 ? x - widths[i] : x - widths[i] / 2;
        long base = top + (long)line * i + (line - font.px) / 2 + (ascent ? ascent : font.px * 4 / 5);
        if (lens[i])
            oldraw_text(d, (int)lx, (int)base, text + starts[i], lens[i], &font, colour, st);
    }
    owf_free(text);
}

/* A label inside a box drawn at r: placed as align and verticalAlign say (a swimlane's in its header). */
static void box_label(oldraw *d, const odg_view *v, const owf_dcell *c, const oldraw_rect *r, long header)
{
    char ab[16], vb[16];
    long pad = odg_view_len(v, 200), x, y;
    int anchor = 1, valign = 1;
    if (owf_dstyle_get(c->style, "align", ab, sizeof ab)) {
        if (!strcmp(ab, "left")) anchor = 0;
        else if (!strcmp(ab, "right")) anchor = 2;
    }
    if (!header && owf_dstyle_get(c->style, "verticalAlign", vb, sizeof vb)) {
        if (!strcmp(vb, "top")) valign = 0;
        else if (!strcmp(vb, "bottom")) valign = 2;
    }
    x = anchor == 0 ? r->x + pad : anchor == 2 ? r->x + r->w - pad : r->x + r->w / 2;
    y = header ? r->y + header / 2 : valign == 0 ? r->y + pad : valign == 2 ? r->y + r->h - pad : r->y + r->h / 2;
    draw_label(d, v, c->value, c->style, x, y, anchor, valign, OLDRAW_NONE);
}

/* ---- shapes ---- */

/* cos of 0, 15, ... 90 degrees in thousandths: a rounded corner's quarter circle */
static const int arc_cos[7] = { 1000, 966, 866, 707, 500, 259, 0 };

static int rounded_box(const oldraw_rect *r, long rad, oldraw_point *pts)
{
    int n = 0, k, corner;
    /* each corner's centre and which way its arc runs, clockwise from the top left */
    static const int sx[4] = { -1, 1, 1, -1 }, sy[4] = { -1, -1, 1, 1 };
    long cx[4], cy[4];
    cx[0] = cx[3] = r->x + rad;
    cx[1] = cx[2] = r->x + r->w - 1 - rad;
    cy[0] = cy[1] = r->y + rad;
    cy[2] = cy[3] = r->y + r->h - 1 - rad;
    for (corner = 0; corner < 4; corner++)
        for (k = 0; k < 7; k++) {
            /* top left runs from the left side up to the top; each next corner a quarter turn on */
            int a = (corner % 2 == 0) ? k : 6 - k;
            long ox = (long)((wide)rad * arc_cos[a] / 1000), oy = (long)((wide)rad * arc_cos[6 - a] / 1000);
            pts[n].x = (int32_t)(cx[corner] + sx[corner] * ox);
            pts[n].y = (int32_t)(cy[corner] + sy[corner] * oy);
            n++;
        }
    return n;
}

static void draw_vertex(oldraw *d, const odg_view *v, const owf_dpage *p, const owf_dcell *c)
{
    owf_dnum box[4];
    oldraw_rect r;
    oldraw_point pts[28];
    const char *style = c->style;
    oldraw_colour fill = odg_style_colour(style, "fillColor", OLDRAW_RGB(0xffffff));
    oldraw_colour stroke = odg_style_colour(style, "strokeColor", OLDRAW_RGB(0x000000));
    int w = stroke_px(v, style), dashed = style_is(style, "dashed", "1");
    long header = 0;

    if (!owf_dcell_box(p, c, box))
        return;
    odg_view_rect(v, box, &r);
    if (owf_dstyle_is(style, "text") || owf_dstyle_is(style, "edgeLabel"))
        ;
    else if (owf_dstyle_is(style, "ellipse")) {
        oldraw_ellipse(d, &r, fill);
        if (stroke != OLDRAW_NONE)
            oldraw_ellipse_frame(d, &r, w, stroke);
    } else if (owf_dstyle_is(style, "rhombus")) {
        pts[0].x = r.x + r.w / 2; pts[0].y = r.y;
        pts[1].x = r.x + r.w - 1; pts[1].y = r.y + r.h / 2;
        pts[2].x = r.x + r.w / 2; pts[2].y = r.y + r.h - 1;
        pts[3].x = r.x;           pts[3].y = r.y + r.h / 2;
        oldraw_polygon(d, pts, 4, fill);
        stroke_path(d, pts, 4, 1, w, dashed, stroke);
    } else {
        int n = 4;
        if (style_is(style, "rounded", "1")) {
            long small = r.w < r.h ? r.w : r.h;
            long arc = style_num(style, "arcSize", 1500);    /* a percent of the shorter side: 0 to 100 */
            long rad = (long)((wide)small * (arc < 0 ? 0 : arc > 10000 ? 10000 : arc) / 10000 / 2);
            if (rad > 1) n = rounded_box(&r, rad, pts);
        }
        if (n == 4) {
            pts[0].x = r.x;           pts[0].y = r.y;
            pts[1].x = r.x + r.w - 1; pts[1].y = r.y;
            pts[2].x = r.x + r.w - 1; pts[2].y = r.y + r.h - 1;
            pts[3].x = r.x;           pts[3].y = r.y + r.h - 1;
            oldraw_fill(d, &r, fill);
        } else {
            /* the polygon fills to its points' corners: one more pixel right and down for the edge pixels */
            oldraw_point grown[28];
            int k;
            for (k = 0; k < n; k++) {
                grown[k].x = pts[k].x + (pts[k].x > r.x + r.w / 2);
                grown[k].y = pts[k].y + (pts[k].y > r.y + r.h / 2);
            }
            oldraw_polygon(d, grown, n, fill);
        }
        stroke_path(d, pts, n, 1, w, dashed, stroke);
        if (owf_dstyle_is(style, "swimlane")) {
            header = odg_view_len(v, style_num(style, "startSize", 2300));
            if (header > r.h)
                header = r.h;
            oldraw_line(d, r.x, (int)(r.y + header), r.x + r.w - 1, (int)(r.y + header), w, stroke);
        }
    }
    box_label(d, v, c, &r, header);
}

/* ---- connectors ---- */

/* An arrowhead at tip, pointing from from: 8 page pixels long and 6 across, as the SVG writer draws it. */
static void draw_arrow(oldraw *d, const odg_view *v, oldraw_point from, oldraw_point tip, oldraw_colour c)
{
    wide dx = (wide)tip.x - from.x, dy = (wide)tip.y - from.y, len = isqrt_wide(dx * dx + dy * dy);
    wide size = odg_view_len(v, 800), half = odg_view_len(v, 300), bx, by;
    oldraw_point tri[3];
    if (!len || size < 2)
        return;
    bx = tip.x - dx * size / len;
    by = tip.y - dy * size / len;
    tri[0] = tip;
    tri[1].x = (int32_t)(bx - dy * half / len);
    tri[1].y = (int32_t)(by + dx * half / len);
    tri[2].x = (int32_t)(bx + dy * half / len);
    tri[2].y = (int32_t)(by - dx * half / len);
    oldraw_polygon(d, tri, 3, c);
    oldraw_polyline(d, tri, 3, 1, 1, c);
}

/* The point frac (hundredths, -100 the start to 100 the end) of the way along a path. */
static oldraw_point along(const oldraw_point *pts, int n, long frac)
{
    wide total = 0, want, len;
    int i;
    oldraw_point at = pts[0];
    for (i = 1; i < n; i++) {
        wide dx = (wide)pts[i].x - pts[i - 1].x, dy = (wide)pts[i].y - pts[i - 1].y;
        total += isqrt_wide(dx * dx + dy * dy);
    }
    if (frac < -100) frac = -100;
    if (frac > 100) frac = 100;
    want = total * (frac + 100) / 200;
    for (i = 1; i < n; i++) {
        wide dx = (wide)pts[i].x - pts[i - 1].x, dy = (wide)pts[i].y - pts[i - 1].y;
        len = isqrt_wide(dx * dx + dy * dy);
        if (want <= len && len) {
            at.x = (int32_t)(pts[i - 1].x + dx * want / len);
            at.y = (int32_t)(pts[i - 1].y + dy * want / len);
            return at;
        }
        want -= len;
        at = pts[i];
    }
    return at;
}

static int view_path(const odg_view *v, const owf_dpage *p, const owf_dcell *e, oldraw_point *out)
{
    int n, i;
    owf_dpoint *pts = owf_dedge_path(p, e, &n);
    if (!pts)
        return 0;
    if (n > MAX_PTS)
        n = MAX_PTS;
    for (i = 0; i < n; i++) {
        out[i].x = (int32_t)odg_view_x(v, pts[i].x);
        out[i].y = (int32_t)odg_view_y(v, pts[i].y);
    }
    owf_free(pts);
    return n;
}

static void draw_edge(oldraw *d, const odg_view *v, const owf_dpage *p, const owf_dcell *e)
{
    oldraw_point pts[MAX_PTS];
    char buf[24];
    int n = view_path(v, p, e, pts), i;
    oldraw_colour stroke = odg_style_colour(e->style, "strokeColor", OLDRAW_RGB(0x000000));
    if (n < 2)
        return;
    stroke_path(d, pts, n, 0, stroke_px(v, e->style), style_is(e->style, "dashed", "1"), stroke);
    if (stroke != OLDRAW_NONE) {
        if (!owf_dstyle_get(e->style, "endArrow", buf, sizeof buf) || strcmp(buf, "none"))
            draw_arrow(d, v, pts[n - 2], pts[n - 1], stroke);
        if (owf_dstyle_get(e->style, "startArrow", buf, sizeof buf) && buf[0] && strcmp(buf, "none"))
            draw_arrow(d, v, pts[1], pts[0], stroke);
    }
    if (e->value && *e->value) {
        oldraw_point at = along(pts, n, 0);
        draw_label(d, v, e->value, e->style, at.x + odg_view_len(v, e->geo.offset.x),
                   at.y + odg_view_len(v, e->geo.offset.y), 1, 1, OLDRAW_RGB(0xffffff));
    }
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *l = &p->cells[i];
        if (l->kind == OWF_DCELL_VERTEX && l->parent && !strcmp(l->parent, e->id) && owf_dcell_shown(p, l)) {
            oldraw_point at = along(pts, n, (long)l->geo.x);
            draw_label(d, v, l->value, l->style, at.x + odg_view_len(v, l->geo.offset.x),
                       at.y + odg_view_len(v, l->geo.offset.y), 1, 1, OLDRAW_RGB(0xffffff));
        }
    }
}

/* ---- the grid and the selection ---- */

static void draw_grid(oldraw *d, const odg_view *v, const odg_doc *doc, const oldraw_rect *area)
{
    owf_dnum g = doc->grid, x0, y0, x, y;
    long step = odg_view_len(v, g);
    if (!v->grid || g <= 0 || step < 6 || v->grid_colour == OLDRAW_NONE)
        return;
    x0 = (owf_dnum)(floor_div(odg_page_x(v, area->x), g) * g);
    y0 = (owf_dnum)(floor_div(odg_page_y(v, area->y), g) * g);
    for (y = y0; odg_view_y(v, y) < area->y + area->h; y += g)
        for (x = x0; odg_view_x(v, x) < area->x + area->w; x += g) {
            oldraw_rect dot;
            dot.x = (int32_t)odg_view_x(v, x);
            dot.y = (int32_t)odg_view_y(v, y);
            dot.w = dot.h = 1;
            oldraw_fill(d, &dot, v->grid_colour);
        }
}

void odg_handle_rect(const oldraw_rect *r, odg_handle h, oldraw_rect *out)
{
    /* each handle's place as halves of the box: 0 its left (top), 1 its middle, 2 its right (bottom) */
    static const int hx[8] = { 0, 1, 2, 2, 2, 1, 0, 0 }, hy[8] = { 0, 0, 0, 1, 2, 2, 2, 1 };
    long cx, cy;
    if (h < 0 || h > 7) {
        out->x = out->y = out->w = out->h = 0;
        return;
    }
    cx = r->x + (long)(r->w - 1) * hx[h] / 2;
    cy = r->y + (long)(r->h - 1) * hy[h] / 2;
    out->x = (int32_t)(cx - ODG_HANDLE / 2);
    out->y = (int32_t)(cy - ODG_HANDLE / 2);
    out->w = out->h = ODG_HANDLE;
}

static void draw_handle(oldraw *d, const odg_view *v, const oldraw_rect *h)
{
    oldraw_rect in = { h->x + 1, h->y + 1, h->w - 2, h->h - 2 };
    oldraw_fill(d, h, v->select_colour);
    oldraw_fill(d, &in, v->paper);
}

static void draw_selection(oldraw *d, const odg_view *v, const owf_dpage *p)
{
    int i, k;
    for (i = 0; i < v->nselected; i++) {
        const owf_dcell *c = v->selected[i] ? owf_dpage_find(p, v->selected[i]) : NULL;
        owf_dnum box[4];
        if (!c)
            continue;
        if (c->kind == OWF_DCELL_EDGE) {
            oldraw_point pts[MAX_PTS];
            int n = view_path(v, p, c, pts);
            for (k = 0; k < n; k++) {
                oldraw_rect h = { pts[k].x - ODG_HANDLE / 2, pts[k].y - ODG_HANDLE / 2, ODG_HANDLE, ODG_HANDLE };
                draw_handle(d, v, &h);
            }
        } else if (owf_dcell_box(p, c, box)) {
            oldraw_rect r, h;
            oldraw_point pts[4];
            odg_view_rect(v, box, &r);
            pts[0].x = r.x - 1;   pts[0].y = r.y - 1;
            pts[1].x = r.x + r.w; pts[1].y = r.y - 1;
            pts[2].x = r.x + r.w; pts[2].y = r.y + r.h;
            pts[3].x = r.x - 1;   pts[3].y = r.y + r.h;
            stroke_path(d, pts, 4, 1, 1, 1, v->select_colour);
            for (k = 0; k < 8; k++) {
                odg_handle_rect(&r, (odg_handle)k, &h);
                draw_handle(d, v, &h);
            }
        }
    }
}

/* ---- the whole view ---- */

void odg_draw(oldraw *d, const odg_view *v, const odg_doc *doc, const oldraw_rect *area)
{
    const owf_dpage *p = odg_page(doc);
    oldraw_rect all = { v->x, v->y, v->width, v->height }, a = area ? *area : all;
    int i;
    if (!d || !p)
        return;
    oldraw_clip(d, &a);
    dash_area = &a;
    oldraw_fill(d, &a, v->paper);
    draw_grid(d, v, doc, &a);
    for (i = 0; i < p->ncells; i++) {
        const owf_dcell *c = &p->cells[i];
        const owf_dcell *parent = c->parent ? owf_dpage_find(p, c->parent) : NULL;
        if (!owf_dcell_shown(p, c))
            continue;
        if (c->kind == OWF_DCELL_EDGE)
            draw_edge(d, v, p, c);
        else if (c->kind == OWF_DCELL_VERTEX && !(parent && parent->kind == OWF_DCELL_EDGE))
            draw_vertex(d, v, p, c);           /* a label on an edge is drawn with its edge */
    }
    draw_selection(d, v, p);
    dash_area = NULL;
    oldraw_clip(d, NULL);                /* the caller's clip as it was: none of odg_draw's own */
}

/* ---- handles ---- */

odg_handle odg_view_handle(const odg_view *v, const odg_doc *doc, long px, long py, char *id, size_t size)
{
    const owf_dpage *p = odg_page(doc);
    int i, k;
    for (i = v->nselected - 1; i >= 0; i--) {
        const owf_dcell *c = v->selected[i] ? owf_dpage_find(p, v->selected[i]) : NULL;
        owf_dnum box[4];
        oldraw_rect r, h;
        if (!c || c->kind != OWF_DCELL_VERTEX || !owf_dcell_box(p, c, box))
            continue;
        odg_view_rect(v, box, &r);
        for (k = 0; k < 8; k++) {
            odg_handle_rect(&r, (odg_handle)k, &h);
            if (px >= h.x && px < h.x + h.w && py >= h.y && py < h.y + h.h) {
                if (size)
                    snprintf(id, size, "%s", c->id);
                return (odg_handle)k;
            }
        }
    }
    return ODG_HANDLE_NONE;
}

void odg_handle_drag(const odg_view *v, const odg_doc *doc, const owf_dnum box[4], odg_handle h, long dx, long dy,
                     owf_dnum out[4])
{
    owf_dnum l = box[0], t = box[1], r = box[0] + box[2], b = box[1] + box[3];
    owf_dnum ddx = (owf_dnum)floor_div((wide)dx * 10000, zoom_of(v)), ddy = (owf_dnum)floor_div((wide)dy * 10000, zoom_of(v));
    owf_dnum least = doc->grid > 0 ? doc->grid : 100;
    int west = h == ODG_HANDLE_NW || h == ODG_HANDLE_W || h == ODG_HANDLE_SW;
    int east = h == ODG_HANDLE_NE || h == ODG_HANDLE_E || h == ODG_HANDLE_SE;
    int north = h == ODG_HANDLE_NW || h == ODG_HANDLE_N || h == ODG_HANDLE_NE;
    int south = h == ODG_HANDLE_SW || h == ODG_HANDLE_S || h == ODG_HANDLE_SE;
    if (west) {
        l = odg_snap(doc, l + ddx);
        if (r - l < least) l = r - least;
    }
    if (east) {
        r = odg_snap(doc, r + ddx);
        if (r - l < least) r = l + least;
    }
    if (north) {
        t = odg_snap(doc, t + ddy);
        if (b - t < least) t = b - least;
    }
    if (south) {
        b = odg_snap(doc, b + ddy);
        if (b - t < least) b = t + least;
    }
    out[0] = l;
    out[1] = t;
    out[2] = r - l;
    out[3] = b - t;
}
