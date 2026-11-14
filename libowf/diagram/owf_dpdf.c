/*
 * owf_dpdf.c: a diagram's pages drawn as PDF (owf_diagram.h), through libowf's page-drawing PDF writer
 * (owf_pdf.h): the shapes, connectors with their arrowheads and labels, as the SVG writer (owf_dsvg.c) places
 * them, and the flowchart shapes as GFOpenDiag's canvas draws them. Each page is a PDF page the size of what is
 * on it. Text is in PDF's standard fonts, so it stays text. Integer arithmetic only.
 * Written from published descriptions, not from any program's code: mxGraph's API documentation (Apache 2.0, by
 * name only) for the style keys and their defaults, as owf_dsvg.c lists them.
 * GFOpenDiag step D6 (amicradle-teams designs/GFOPENDIAG-20261113.md).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"
#include "owf_pdf.h"

#include <stdlib.h>
#include <string.h>

#define MAX_LINES 64                     /* a label's lines, at most */
#define MARGIN OWF_DNUM(10)              /* round what a page has */

typedef long long wide;

typedef struct {
    owf_pdf *pdf;
    owf_dnum ox, oy;                     /* the page's top left, in the diagram */
    int bad;                             /* memory ran out */
} pen;

/* Diagram hundredths of a pixel as twips (a pixel is 15: 96 to the inch). */
static int tw(owf_dnum n)
{
    return (int)((wide)n * 15 / 100);
}

static int tx(const pen *p, owf_dnum x) { return tw(x - p->ox); }
static int ty(const pen *p, owf_dnum y) { return tw(y - p->oy); }

static const char *style_or(const char *style, const char *key, char *buf, size_t size, const char *def)
{
    if (owf_dstyle_get(style, key, buf, size) && buf[0])
        return buf;
    return def;
}

static int is_on(const char *style, const char *key)
{
    char v[8];
    return owf_dstyle_get(style, key, v, sizeof v) && !strcmp(v, "1");
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A style's colour as OWF_SHADE(0xRRGGBB), 0 for "none"; def (an RGB) when it has none, "default" or one this
 * doesn't read. #rrggbb and #rgb are read. */
static unsigned long shade(const char *style, const char *key, long def)
{
    char buf[40];
    const char *c = style_or(style, key, buf, sizeof buf, NULL);
    unsigned long rgb = 0;
    int i;
    if (!c || !strcmp(c, "default") || !strcmp(c, "inherit"))
        return def < 0 ? 0 : OWF_SHADE(def);
    if (!strcmp(c, "none"))
        return 0;
    if (c[0] == '#' && strlen(c) == 7) {
        for (i = 1; i < 7; i++) {
            int h = hexval((unsigned char)c[i]);
            if (h < 0)
                return def < 0 ? 0 : OWF_SHADE(def);
            rgb = rgb * 16 + (unsigned long)h;
        }
        return OWF_SHADE(rgb);
    }
    if (c[0] == '#' && strlen(c) == 4) {
        for (i = 1; i < 4; i++) {
            int h = hexval((unsigned char)c[i]);
            if (h < 0)
                return def < 0 ? 0 : OWF_SHADE(def);
            rgb = rgb * 256 + (unsigned long)(h * 17);
        }
        return OWF_SHADE(rgb);
    }
    return def < 0 ? 0 : OWF_SHADE(def);
}

static int line_width(const char *style)
{
    char buf[24];
    return tw(owf_dnum_parse(style_or(style, "strokeWidth", buf, sizeof buf, "1")));
}

static void note(pen *p, int r)
{
    if (r < 0)
        p->bad = 1;
}

/* A closed or open path in diagram units. */
static void path(pen *p, const owf_dpoint *pts, int n, int closed, unsigned long fill, unsigned long line, int width)
{
    int xy[2 * 32], i;
    if (n > 32)
        n = 32;
    for (i = 0; i < n; i++) {
        xy[2 * i] = tx(p, pts[i].x);
        xy[2 * i + 1] = ty(p, pts[i].y);
    }
    note(p, owf_pdf_path(p->pdf, xy, n, closed, fill, line, width));
}

/* ---- Labels ---- */

static owf_font_kind font_kind(const char *style)
{
    char buf[64];
    const char *f = style_or(style, "fontFamily", buf, sizeof buf, "Helvetica");
    if (strstr(f, "Courier") || strstr(f, "courier") || strstr(f, "Mono") || strstr(f, "mono"))
        return OWF_FONT_MONO;
    if (strstr(f, "Times") || strstr(f, "Georgia") || strstr(f, "Garamond") || !strcmp(f, "serif"))
        return OWF_FONT_SERIF;
    return OWF_FONT_SANS;
}

/* A label at (x, y), as owf_dsvg.c places one: anchor 0 start, 1 middle, 2 end across; valign 0 top (its first
 * line's top at y), 1 middle (centred on y), 2 bottom (its last line's foot at y). bg_def: the background when the
 * style names none (-1: none). */
static void label(pen *p, const char *value, const char *style, owf_dnum x, owf_dnum y, int anchor, int valign,
                  long bg_def)
{
    char buf[40];
    owf_dnum size = owf_dnum_parse(style_or(style, "fontSize", buf, sizeof buf, "12")), line, top;
    long fstyle = owf_dnum_parse(style_or(style, "fontStyle", buf, sizeof buf, "0")) / 100;
    unsigned flags = (fstyle & 1 ? OWF_BOLD : 0) | (fstyle & 2 ? OWF_ITALIC : 0) | (fstyle & 4 ? OWF_UNDERLINE : 0);
    owf_font_kind kind = font_kind(style);
    unsigned long colour = shade(style, "fontColor", 0x000000), bg = shade(style, "labelBackgroundColor", bg_def);
    size_t starts[MAX_LINES], lens[MAX_LINES], len = 0, at, from = 0;
    int widths[MAX_LINES], nlines = 0, i, widest = 0, pt;
    char *text;

    if (!value || !*value)
        return;
    if (size <= 0)
        size = OWF_DNUM(12);
    line = size * 6 / 5;
    pt = tw(size);
    if (!(text = owf_dlabel_text(value, is_on(style, "html"), &len))) {
        p->bad = 1;
        return;
    }
    for (at = 0; at <= len && nlines < MAX_LINES; at++)
        if (at == len || text[at] == '\n') {
            starts[nlines] = from;
            lens[nlines] = at - from;
            widths[nlines] = owf_pdf_text_width(text + from, at - from, kind, flags, pt);
            if (widths[nlines] > widest)
                widest = widths[nlines];
            nlines++;
            from = at + 1;
        }
    top = valign == 0 ? y : valign == 2 ? y - line * nlines : y - line * nlines / 2;
    if (bg) {                            /* behind the text, a pixel more each side */
        int left = anchor == 0 ? tx(p, x) - 15 : anchor == 2 ? tx(p, x) - widest - 15 : tx(p, x) - widest / 2 - 15;
        note(p, owf_pdf_rect(p->pdf, left, ty(p, top), widest + 30, tw(line * nlines), bg, 0, 0));
    }
    for (i = 0; i < nlines; i++) {
        int left = anchor == 0 ? tx(p, x) : anchor == 2 ? tx(p, x) - widths[i] : tx(p, x) - widths[i] / 2;
        if (lens[i])
            note(p, owf_pdf_text(p->pdf, left, ty(p, top + line * i + size * 9 / 10), 0, text + starts[i], lens[i],
                                 kind, flags, pt, colour ? OWF_SHADE_RGB(colour) : 0));
    }
    owf_free(text);
}

static void box_label(pen *p, const owf_dcell *c, const owf_dnum box[4], owf_dnum header)
{
    char ab[16], vb[16];
    const char *align = style_or(c->style, "align", ab, sizeof ab, "center");
    const char *valign = header ? "middle" : style_or(c->style, "verticalAlign", vb, sizeof vb, "middle");
    owf_dnum x, y, pad = 200;
    int anchor = 1, va = !strcmp(valign, "top") ? 0 : !strcmp(valign, "bottom") ? 2 : 1;
    if (!strcmp(align, "left")) {
        x = box[0] + pad;
        anchor = 0;
    } else if (!strcmp(align, "right")) {
        x = box[0] + box[2] - pad;
        anchor = 2;
    } else
        x = box[0] + box[2] / 2;
    if (header)
        y = box[1] + header / 2;
    else if (va == 0)
        y = box[1] + pad;
    else if (va == 2)
        y = box[1] + box[3] - pad;
    else
        y = box[1] + box[3] / 2;
    label(p, c->value, c->style, x, y, anchor, va, -1);
}

/* ---- Shapes ---- */

static owf_dnum style_num(const char *style, const char *key, owf_dnum def)
{
    char buf[24];
    return owf_dstyle_get(style, key, buf, sizeof buf) && buf[0] ? owf_dnum_parse(buf) : def;
}

static void vertex(pen *p, const owf_dpage *page, const owf_dcell *c)
{
    owf_dnum box[4], header = 0;
    const char *style = c->style;
    unsigned long fill = shade(style, "fillColor", 0xffffff), line = shade(style, "strokeColor", 0x000000);
    int w = line_width(style), dashed = is_on(style, "dashed");
    owf_dpoint pts[6];

    if (!owf_dcell_box(page, c, box))
        return;
    owf_pdf_dash(p->pdf, dashed);
    if (owf_dstyle_is(style, "text") || owf_dstyle_is(style, "edgeLabel"))
        ;
    else if (owf_dstyle_is(style, "ellipse"))
        note(p, owf_pdf_ellipse(p->pdf, tx(p, box[0]), ty(p, box[1]), tw(box[2]), tw(box[3]), fill, line, w));
    else if (owf_dstyle_is(style, "triangle") || owf_dstyle_is(style, "parallelogram") ||
             owf_dstyle_is(style, "hexagon")) {
        /* as the canvas (src/diag/odg_view.c) draws them: a triangle pointing east; a parallelogram leaning and a
         * hexagon cut in at each end by size, pixels with fixedSize=1 (20 when not given), else a part of the width
         * (0.2 and 0.25 when not given) */
        int hex = owf_dstyle_is(style, "hexagon"), n;
        owf_dnum k = is_on(style, "fixedSize") ? style_num(style, "size", OWF_DNUM(20))
                                               : (owf_dnum)((wide)box[2] * style_num(style, "size", hex ? 25 : 20) / 100);
        owf_dnum x = box[0], y = box[1], bw = box[2], bh = box[3];
        if (k < 0) k = 0;
        if (k > bw / 2) k = bw / 2;
        if (owf_dstyle_is(style, "triangle")) {
            pts[0].x = x;      pts[0].y = y;
            pts[1].x = x + bw; pts[1].y = y + bh / 2;
            pts[2].x = x;      pts[2].y = y + bh;
            n = 3;
        } else if (owf_dstyle_is(style, "parallelogram")) {
            pts[0].x = x + k;      pts[0].y = y;
            pts[1].x = x + bw;     pts[1].y = y;
            pts[2].x = x + bw - k; pts[2].y = y + bh;
            pts[3].x = x;          pts[3].y = y + bh;
            n = 4;
        } else {
            pts[0].x = x + k;      pts[0].y = y;
            pts[1].x = x + bw - k; pts[1].y = y;
            pts[2].x = x + bw;     pts[2].y = y + bh / 2;
            pts[3].x = x + bw - k; pts[3].y = y + bh;
            pts[4].x = x + k;      pts[4].y = y + bh;
            pts[5].x = x;          pts[5].y = y + bh / 2;
            n = 6;
        }
        path(p, pts, n, 1, fill, line, w);
    } else if (owf_dstyle_is(style, "rhombus")) {
        pts[0].x = box[0] + box[2] / 2; pts[0].y = box[1];
        pts[1].x = box[0] + box[2];     pts[1].y = box[1] + box[3] / 2;
        pts[2].x = box[0] + box[2] / 2; pts[2].y = box[1] + box[3];
        pts[3].x = box[0];              pts[3].y = box[1] + box[3] / 2;
        path(p, pts, 4, 1, fill, line, w);
    } else {
        if (is_on(style, "rounded")) {
            owf_dnum arc = style_num(style, "arcSize", OWF_DNUM(15));
            owf_dnum small = box[2] < box[3] ? box[2] : box[3];
            if (arc < 0) arc = 0;
            if (arc > OWF_DNUM(100)) arc = OWF_DNUM(100);
            note(p, owf_pdf_round_rect(p->pdf, tx(p, box[0]), ty(p, box[1]), tw(box[2]), tw(box[3]),
                                       tw((owf_dnum)((wide)small * arc / 10000 / 2)), fill, line, w));
        } else
            note(p, owf_pdf_rect(p->pdf, tx(p, box[0]), ty(p, box[1]), tw(box[2]), tw(box[3]), fill, line, w));
        if (owf_dstyle_is(style, "process") && box[2] / 10 > OWF_DNUM(1)) {   /* a line a tenth in from each side */
            owf_dnum in = box[2] / 10;
            pts[0].x = pts[1].x = box[0] + in;
            pts[0].y = box[1];
            pts[1].y = box[1] + box[3];
            path(p, pts, 2, 0, 0, line, w);
            pts[0].x = pts[1].x = box[0] + box[2] - in;
            path(p, pts, 2, 0, 0, line, w);
        }
        if (owf_dstyle_is(style, "swimlane")) {
            header = style_num(style, "startSize", OWF_DNUM(23));
            if (header > box[3])
                header = box[3];
            pts[0].x = box[0];          pts[0].y = box[1] + header;
            pts[1].x = box[0] + box[2]; pts[1].y = box[1] + header;
            path(p, pts, 2, 0, 0, line, w);
        }
    }
    owf_pdf_dash(p->pdf, 0);
    box_label(p, c, box, header);
}

/* ---- Connectors ---- */

/* An arrowhead at tip, pointing from from: 8 pixels long, 6 across, as the SVG writer draws it. */
static void arrow(pen *p, owf_dpoint from, owf_dpoint tip, unsigned long colour)
{
    wide dx = tip.x - from.x, dy = tip.y - from.y, len = owf_diag_isqrt(dx * dx + dy * dy);
    wide ux, uy, size = 800, half = 300;
    owf_dnum bx, by;
    owf_dpoint head[3];
    if (!len)
        return;
    ux = dx * 1000 / len;
    uy = dy * 1000 / len;
    bx = (owf_dnum)(tip.x - ux * size / 1000);
    by = (owf_dnum)(tip.y - uy * size / 1000);
    head[0] = tip;
    head[1].x = (owf_dnum)(bx - uy * half / 1000);
    head[1].y = (owf_dnum)(by + ux * half / 1000);
    head[2].x = (owf_dnum)(bx + uy * half / 1000);
    head[2].y = (owf_dnum)(by - ux * half / 1000);
    path(p, head, 3, 1, colour, colour, 0);
}

static int wants_arrow(const char *style, const char *key, const char *def)
{
    char buf[24];
    return strcmp(style_or(style, key, buf, sizeof buf, def), "none") != 0;
}

static void edge(pen *p, const owf_dpage *page, const owf_dcell *e)
{
    int n, i, k;
    owf_dpoint *pts = owf_dedge_path(page, e, &n);
    unsigned long line = shade(e->style, "strokeColor", 0x000000);
    int w = line_width(e->style);
    if (!pts)
        return;
    if (line) {
        int *xy = (int *)owf_malloc((size_t)n * 2 * sizeof *xy);
        if (!xy) {
            p->bad = 1;
            owf_free(pts);
            return;
        }
        for (k = 0; k < n; k++) {
            xy[2 * k] = tx(p, pts[k].x);
            xy[2 * k + 1] = ty(p, pts[k].y);
        }
        owf_pdf_dash(p->pdf, is_on(e->style, "dashed"));
        note(p, owf_pdf_path(p->pdf, xy, n, 0, 0, line, w));
        owf_pdf_dash(p->pdf, 0);
        owf_free(xy);
        if (n >= 2 && wants_arrow(e->style, "endArrow", "classic"))
            arrow(p, pts[n - 2], pts[n - 1], line);
        if (n >= 2 && wants_arrow(e->style, "startArrow", "none"))
            arrow(p, pts[1], pts[0], line);
    }
    if (e->value && *e->value) {
        owf_dpoint at = owf_diag_along(pts, n, 0);
        label(p, e->value, e->style, at.x + e->geo.offset.x, at.y + e->geo.offset.y, 1, 1, 0xffffff);
    }
    for (i = 0; i < page->ncells; i++) {   /* labels placed along it */
        const owf_dcell *l = &page->cells[i];
        if (l->kind == OWF_DCELL_VERTEX && l->parent && !strcmp(l->parent, e->id) && owf_dcell_shown(page, l)) {
            owf_dpoint at = owf_diag_along(pts, n, (long)l->geo.x);
            label(p, l->value, l->style, at.x + l->geo.offset.x, at.y + l->geo.offset.y, 1, 1, 0xffffff);
        }
    }
    owf_free(pts);
}

/* ---- Pages ---- */

static void draw_page(pen *p, const owf_dpage *page)
{
    int i;
    for (i = 0; i < page->ncells; i++) {  /* in file order, which is drawing order */
        const owf_dcell *c = &page->cells[i];
        const owf_dcell *parent = owf_dpage_find(page, c->parent);
        if (!owf_dcell_shown(page, c))
            continue;
        if (c->kind == OWF_DCELL_EDGE)
            edge(p, page, c);
        else if (c->kind == OWF_DCELL_VERTEX && !(parent && parent->kind == OWF_DCELL_EDGE))
            vertex(p, page, c);
    }
}

int owf_dpdf_write(const owf_diagram *d, int page, unsigned char **data, size_t *length)
{
    pen p;
    int i, first = page < 0 ? 0 : page, last = page < 0 ? d->npages - 1 : page, r;
    *data = NULL;
    if (length)
        *length = 0;
    if (d->npages < 1 || page >= d->npages)
        return OWF_ERR_FORMAT;
    memset(&p, 0, sizeof p);
    if (!(p.pdf = owf_pdf_new()))
        return OWF_ERR_MEMORY;
    for (i = first; i <= last && !p.bad; i++) {
        const owf_dpage *pg = &d->pages[i];
        owf_dnum box[4];
        if (!owf_dpage_bounds(pg, box)) {   /* nothing on it: a blank page 100 pixels square */
            box[0] = box[1] = 0;
            box[2] = box[3] = OWF_DNUM(100);
        }
        p.ox = box[0] - MARGIN;
        p.oy = box[1] - MARGIN;
        if (owf_pdf_page(p.pdf, tw(box[2] + 2 * MARGIN), tw(box[3] + 2 * MARGIN)) < 0) {
            p.bad = 1;
            break;
        }
        draw_page(&p, pg);
    }
    if (p.bad) {
        owf_pdf_free(p.pdf);
        return OWF_ERR_MEMORY;
    }
    r = owf_pdf_finish(p.pdf, d->pages[first].name, data, length, NULL);
    owf_pdf_free(p.pdf);
    return r == OWF_OK ? OWF_OK : r < 0 ? OWF_ERR_MEMORY : r;
}
