/*
 * libowf: the Windows metafile player (owf_metafile.h), from [MS-WMF]. Record and field names in the comments are
 * the specification's. Every number in a file is little-endian; a record is its size in 16-bit words, its function,
 * then its fields.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_metafile.h"

#include <stdlib.h>
#include <string.h>

#define MF_MAX_OBJECTS 1024
#define MF_MAX_SAVED 16
#define MF_MAX_POINTS 16384

/* [MS-WMF] 2.1.1.1 RecordType */
enum {
    META_EOF = 0x0000, META_REALIZEPALETTE = 0x0035, META_SETPALENTRIES = 0x0037, META_SETBKMODE = 0x0102,
    META_SETMAPMODE = 0x0103, META_SETROP2 = 0x0104, META_SETRELABS = 0x0105, META_SETPOLYFILLMODE = 0x0106,
    META_SETSTRETCHBLTMODE = 0x0107, META_SETTEXTCHAREXTRA = 0x0108, META_RESTOREDC = 0x0127,
    META_INVERTREGION = 0x012A, META_PAINTREGION = 0x012B, META_SELECTCLIPREGION = 0x012C,
    META_SELECTOBJECT = 0x012D, META_SETTEXTALIGN = 0x012E, META_RESIZEPALETTE = 0x0139,
    META_DIBCREATEPATTERNBRUSH = 0x0142, META_SETLAYOUT = 0x0149, META_DELETEOBJECT = 0x01F0,
    META_CREATEPATTERNBRUSH = 0x01F9, META_SETBKCOLOR = 0x0201, META_SETTEXTCOLOR = 0x0209,
    META_SETTEXTJUSTIFICATION = 0x020A, META_SETWINDOWORG = 0x020B, META_SETWINDOWEXT = 0x020C,
    META_SETVIEWPORTORG = 0x020D, META_SETVIEWPORTEXT = 0x020E, META_OFFSETWINDOWORG = 0x020F,
    META_OFFSETVIEWPORTORG = 0x0211, META_LINETO = 0x0213, META_MOVETO = 0x0214, META_OFFSETCLIPRGN = 0x0220,
    META_FILLREGION = 0x0228, META_SETMAPPERFLAGS = 0x0231, META_SELECTPALETTE = 0x0234,
    META_CREATEPALETTE = 0x00F7, META_CREATEPENINDIRECT = 0x02FA, META_CREATEFONTINDIRECT = 0x02FB,
    META_CREATEBRUSHINDIRECT = 0x02FC, META_POLYGON = 0x0324, META_POLYLINE = 0x0325, META_SCALEWINDOWEXT = 0x0410,
    META_SCALEVIEWPORTEXT = 0x0412, META_EXCLUDECLIPRECT = 0x0415, META_INTERSECTCLIPRECT = 0x0416,
    META_ELLIPSE = 0x0418, META_FLOODFILL = 0x0419, META_RECTANGLE = 0x041B, META_SETPIXEL = 0x041F,
    META_FRAMEREGION = 0x0429, META_ANIMATEPALETTE = 0x0436, META_TEXTOUT = 0x0521, META_POLYPOLYGON = 0x0538,
    META_EXTFLOODFILL = 0x0548, META_ROUNDRECT = 0x061C, META_PATBLT = 0x061D, META_ESCAPE = 0x0626,
    META_CREATEREGION = 0x06FF, META_ARC = 0x0817, META_PIE = 0x081A, META_CHORD = 0x0830, META_BITBLT = 0x0922,
    META_DIBBITBLT = 0x0940, META_EXTTEXTOUT = 0x0A32, META_STRETCHBLT = 0x0B23, META_DIBSTRETCHBLT = 0x0B41,
    META_SETDIBTODEV = 0x0D33, META_STRETCHDIB = 0x0F43, META_SAVEDC = 0x001E
};

enum { OBJ_FREE, OBJ_PEN, OBJ_BRUSH, OBJ_FONT, OBJ_OTHER };

typedef struct {
    int type;
    unsigned long colour;    /* OWF_SHADE, 0: none (a null pen or brush) */
    int width;               /* a pen's, logical units */
    int dash;
    int height;              /* a font's, logical units: < 0 the character's, > 0 the cell's */
    unsigned flags;          /* a font's OWF_BOLD... */
    owf_font_kind kind;
} mf_object;

typedef struct {
    mf_object pen, brush, font;
    unsigned long text_colour;
    unsigned align;
    long win_x, win_y, win_w, win_h;   /* the window: its origin and extent, logical units */
    long cur_x, cur_y;                 /* the current position, logical */
} mf_dc;

typedef struct {
    const unsigned char *d;
    size_t n;
    const owf_mf_sink *sink;
    void *ctx;
    int bx, by, bw, bh;                /* the box */
    mf_dc dc, saved[MF_MAX_SAVED];
    int nsaved;
    mf_object *objects;
    int nobjects;
    owf_mf_result res;
    int *xy;                           /* points for a path, the caller's units */
    int capxy;
    int stop;                          /* the sink said stop, or memory ran out */
} mf_player;

static unsigned u16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static int s16(const unsigned char *p) { unsigned v = u16(p); return v & 0x8000u ? (int)v - 0x10000 : (int)v; }
static unsigned long u32(const unsigned char *p) { return (unsigned long)u16(p) | ((unsigned long)u16(p + 2) << 16); }

/* [MS-WMF] 2.2.2.8 ColorRef: red, green, blue, reserved */
static unsigned long colour_ref(const unsigned char *p)
{
    return OWF_SHADE(((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2]);
}

static int map_x(const mf_player *m, long lx)
{
    long long v = (long long)(lx - m->dc.win_x) * m->bw;
    return m->bx + (int)(m->dc.win_w ? v / m->dc.win_w : 0);
}

static int map_y(const mf_player *m, long ly)
{
    long long v = (long long)(ly - m->dc.win_y) * m->bh;
    return m->by + (int)(m->dc.win_h ? v / m->dc.win_h : 0);
}

/* a length across (a pen's width), the caller's units, never negative */
static int map_w(const mf_player *m, long l)
{
    long long v = (long long)l * m->bw;
    long w = m->dc.win_w < 0 ? -m->dc.win_w : m->dc.win_w;
    if (l < 0) v = -v;
    return w ? (int)(v / w) : 0;
}

static int map_h(const mf_player *m, long l)
{
    long long v = (long long)l * m->bh;
    long h = m->dc.win_h < 0 ? -m->dc.win_h : m->dc.win_h;
    if (l < 0) v = -v;
    return h ? (int)(v / h) : 0;
}

static unsigned long line_of(const mf_player *m) { return m->dc.pen.type == OBJ_PEN ? m->dc.pen.colour : OWF_SHADE(0); }
static unsigned long fill_of(const mf_player *m) { return m->dc.brush.type == OBJ_BRUSH ? m->dc.brush.colour : 0; }
static int line_width_of(const mf_player *m) { return map_w(m, m->dc.pen.width); }

static void drew(mf_player *m, int rc)
{
    if (rc < 0) m->stop = 1;
    else m->res.drawn++;
}

static int points(mf_player *m, int n)
{
    if (n > m->capxy) {
        int *q = (int *)realloc(m->xy, (size_t)n * 2 * sizeof *q);
        if (!q) { m->stop = 1; return 0; }
        m->xy = q;
        m->capxy = n;
    }
    return 1;
}

/* n points (x, y int16 pairs) at p as a path */
static void poly(mf_player *m, const unsigned char *p, int n, int closed)
{
    int i;
    if (n < 2 || n > MF_MAX_POINTS || !points(m, n)) return;
    for (i = 0; i < n; i++) {
        m->xy[i * 2] = map_x(m, s16(p + i * 4));
        m->xy[i * 2 + 1] = map_y(m, s16(p + i * 4 + 2));
    }
    if (!OWF_SHADE_SET(line_of(m)) && !(closed && OWF_SHADE_SET(fill_of(m)))) return;
    drew(m, m->sink->path(m->ctx, m->xy, n, closed, closed ? fill_of(m) : 0, line_of(m), line_width_of(m),
                          m->dc.pen.dash));
}

static void box_of(const mf_player *m, int l, int t, int r, int b, int *x, int *y, int *w, int *h)
{
    int x0 = map_x(m, l), y0 = map_y(m, t), x1 = map_x(m, r), y1 = map_y(m, b);
    *x = x0 < x1 ? x0 : x1;
    *y = y0 < y1 ? y0 : y1;
    *w = x0 < x1 ? x1 - x0 : x0 - x1;
    *h = y0 < y1 ? y1 - y0 : y0 - y1;
}

static void rectangle(mf_player *m, int l, int t, int r, int b)
{
    int x, y, w, h, xy[8];
    box_of(m, l, t, r, b, &x, &y, &w, &h);
    if (!OWF_SHADE_SET(line_of(m)) && !OWF_SHADE_SET(fill_of(m))) return;
    xy[0] = x; xy[1] = y; xy[2] = x + w; xy[3] = y; xy[4] = x + w; xy[5] = y + h; xy[6] = x; xy[7] = y + h;
    drew(m, m->sink->path(m->ctx, xy, 4, 1, fill_of(m), line_of(m), line_width_of(m), m->dc.pen.dash));
}

static void ellipse(mf_player *m, int l, int t, int r, int b)
{
    int x, y, w, h;
    box_of(m, l, t, r, b, &x, &y, &w, &h);
    if (!OWF_SHADE_SET(line_of(m)) && !OWF_SHADE_SET(fill_of(m))) return;
    drew(m, m->sink->ellipse(m->ctx, x, y, w, h, fill_of(m), line_of(m), line_width_of(m), m->dc.pen.dash));
}

/* a rectangle with rounded corners, as a polygon: each corner a quarter in four steps */
static void round_rect(mf_player *m, int l, int t, int r, int b, int cw, int ch)
{
    /* cos and sin of 0, 22.5, 45, 67.5 and 90 degrees, in 1/1024 */
    static const int cs[5] = { 1024, 946, 724, 392, 0 };
    int x, y, w, h, rx, ry, i, k, n = 0;
    box_of(m, l, t, r, b, &x, &y, &w, &h);
    rx = map_w(m, cw) / 2;
    ry = map_h(m, ch) / 2;
    if (rx > w / 2) rx = w / 2;
    if (ry > h / 2) ry = h / 2;
    if (!OWF_SHADE_SET(line_of(m)) && !OWF_SHADE_SET(fill_of(m))) return;
    if (!points(m, 20)) return;
    for (k = 0; k < 4; k++) {           /* top right, bottom right, bottom left, top left */
        int cx = k == 0 || k == 1 ? x + w - rx : x + rx, cy = k == 1 || k == 2 ? y + h - ry : y + ry;
        int sx = k == 0 || k == 1 ? 1 : -1, sy = k == 1 || k == 2 ? 1 : -1;
        for (i = 0; i < 5; i++) {
            int a = k == 0 || k == 2 ? 4 - i : i;   /* each corner from one side to the next */
            m->xy[n * 2] = cx + sx * (int)((long)rx * cs[a] / 1024);
            m->xy[n * 2 + 1] = cy + sy * (int)((long)ry * cs[4 - a] / 1024);
            n++;
        }
    }
    drew(m, m->sink->path(m->ctx, m->xy, n, 1, fill_of(m), line_of(m), line_width_of(m), m->dc.pen.dash));
}

/* Windows-1252's 0x80-0x9F as Unicode (0: none) */
static const unsigned short cp1252_high[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

/* the string's bytes (Windows-1252) as UTF-8 into out (3 bytes a byte at most): its length */
static size_t to_utf8(const unsigned char *s, size_t n, char *out)
{
    size_t i, o = 0;
    for (i = 0; i < n; i++) {
        unsigned c = s[i];
        if (!c) break;
        if (c >= 0x80 && c < 0xA0) c = cp1252_high[c - 0x80] ? cp1252_high[c - 0x80] : '?';
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); }
        else {
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        }
    }
    out[o] = 0;
    return o;
}

/* [MS-WMF] 2.1.2.3 TextAlignmentMode */
#define TA_UPDATECP 0x0001
#define TA_RIGHT 0x0002
#define TA_CENTER 0x0006
#define TA_BOTTOM 0x0008
#define TA_BASELINE 0x0018

static void text(mf_player *m, long lx, long ly, const unsigned char *s, size_t n)
{
    char *u;
    size_t len;
    int size, x, y, width, rc;
    const mf_object *f = &m->dc.font;
    if (!n) return;
    if (!(u = (char *)malloc(n * 3 + 1))) { m->stop = 1; return; }
    len = to_utf8(s, n, u);
    if (m->dc.align & TA_UPDATECP) { lx = m->dc.cur_x; ly = m->dc.cur_y; }
    if (f->type == OBJ_FONT && f->height)
        size = f->height < 0 ? map_h(m, -f->height) : map_h(m, f->height) * 7 / 8;   /* a cell holds its leading */
    else
        size = 240;                                                                 /* none chosen: 12 point */
    if (size < 1) size = 1;
    width = m->sink->text_width ? m->sink->text_width(m->ctx, u, len, f->kind, f->flags, size) : (int)len * size / 2;
    x = map_x(m, lx);
    y = map_y(m, ly);
    if ((m->dc.align & TA_CENTER) == TA_CENTER) x -= width / 2;
    else if (m->dc.align & TA_RIGHT) x -= width;
    if ((m->dc.align & TA_BASELINE) != TA_BASELINE) {
        if (m->dc.align & TA_BOTTOM) y -= size / 5;                                 /* up by the descent */
        else y += size * 4 / 5;                                                     /* the top: down by the ascent */
    }
    rc = len ? m->sink->text(m->ctx, x, y, u, len, f->kind, f->flags, size, m->dc.text_colour) : 0;
    if (len) drew(m, rc);
    if (m->dc.align & TA_UPDATECP) {
        long w = m->dc.win_w < 0 ? -m->dc.win_w : m->dc.win_w;
        m->dc.cur_x += m->bw ? (long)((long long)width * w / m->bw) : 0;
    }
    free(u);
}

/* a new object in the lowest free place ([MS-WMF] 3.1.4.1); 0 when the table is full */
static mf_object *new_object(mf_player *m, int type)
{
    int i;
    for (i = 0; i < m->nobjects; i++)
        if (m->objects[i].type == OBJ_FREE) {
            memset(&m->objects[i], 0, sizeof m->objects[i]);
            m->objects[i].type = type;
            return &m->objects[i];
        }
    return NULL;
}

/* [MS-WMF] 2.2.1.2 Font: Height, Width, Escapement, Orientation, Weight, Italic, Underline, StrikeOut, CharSet,
 * OutPrecision, ClipPrecision, Quality, PitchAndFamily, FaceName */
static void create_font(mf_player *m, const unsigned char *p, size_t n)
{
    mf_object *o = new_object(m, OBJ_FONT);
    char face[33];
    unsigned family;
    size_t i;
    if (!o) return;
    if (n < 18) return;
    o->height = s16(p);
    if (s16(p + 8) >= 600) o->flags |= OWF_BOLD;
    if (p[10]) o->flags |= OWF_ITALIC;
    if (p[11]) o->flags |= OWF_UNDERLINE;
    if (p[12]) o->flags |= OWF_STRIKE;
    family = p[17] & 0xF0;
    for (i = 0; i < 32 && 18 + i < n && p[18 + i]; i++)
        face[i] = (char)p[18 + i];
    face[i] = 0;
    /* the face's kind: by its name first, then by its family (FF_ROMAN 0x10, FF_SWISS 0x20, FF_MODERN 0x30) */
    if (strstr(face, "Courier") || strstr(face, "Mono") || strstr(face, "Consol") || strstr(face, "Lucida Console"))
        o->kind = OWF_FONT_MONO;
    else if (strstr(face, "Times") || strstr(face, "Georgia") || strstr(face, "Garamond") || strstr(face, "Roman") ||
             strstr(face, "Book") || strstr(face, "Serif"))
        o->kind = OWF_FONT_SERIF;
    else if (face[0])
        o->kind = OWF_FONT_SANS;
    else
        o->kind = family == 0x10 ? OWF_FONT_SERIF : family == 0x30 ? OWF_FONT_MONO : OWF_FONT_SANS;
}

/* one record's fields at p, n bytes of them */
static void record(mf_player *m, unsigned fn, const unsigned char *p, size_t n)
{
#define NEED(k) do { if (n < (size_t)(k)) return; } while (0)
    switch (fn) {
    case META_SETWINDOWORG: NEED(4); m->dc.win_y = s16(p); m->dc.win_x = s16(p + 2); break;
    case META_SETWINDOWEXT: NEED(4); if (s16(p) && s16(p + 2)) { m->dc.win_h = s16(p); m->dc.win_w = s16(p + 2); } break;
    case META_OFFSETWINDOWORG: NEED(4); m->dc.win_y += s16(p); m->dc.win_x += s16(p + 2); break;
    case META_SCALEWINDOWEXT:              /* yDenom, yNum, xDenom, xNum */
        NEED(8);
        if (s16(p) && s16(p + 4) && s16(p + 2) && s16(p + 6)) {
            m->dc.win_h = m->dc.win_h * s16(p + 2) / s16(p);
            m->dc.win_w = m->dc.win_w * s16(p + 6) / s16(p + 4);
        }
        break;
    case META_SETTEXTCOLOR: NEED(4); m->dc.text_colour = colour_ref(p); break;
    case META_SETTEXTALIGN: NEED(2); m->dc.align = u16(p); break;
    case META_MOVETO: NEED(4); m->dc.cur_y = s16(p); m->dc.cur_x = s16(p + 2); break;
    case META_LINETO: {
        int xy[4];
        NEED(4);
        xy[0] = map_x(m, m->dc.cur_x); xy[1] = map_y(m, m->dc.cur_y);
        m->dc.cur_y = s16(p); m->dc.cur_x = s16(p + 2);
        xy[2] = map_x(m, m->dc.cur_x); xy[3] = map_y(m, m->dc.cur_y);
        if (OWF_SHADE_SET(line_of(m)))
            drew(m, m->sink->path(m->ctx, xy, 2, 0, 0, line_of(m), line_width_of(m), m->dc.pen.dash));
        break;
    }
    case META_RECTANGLE: NEED(8); rectangle(m, s16(p + 6), s16(p + 4), s16(p + 2), s16(p)); break;
    case META_ELLIPSE: NEED(8); ellipse(m, s16(p + 6), s16(p + 4), s16(p + 2), s16(p)); break;
    case META_ROUNDRECT:                   /* Height, Width, Bottom, Right, Top, Left */
        NEED(12);
        round_rect(m, s16(p + 10), s16(p + 8), s16(p + 6), s16(p + 4), s16(p + 2), s16(p));
        break;
    case META_POLYGON: case META_POLYLINE: {
        int k;
        NEED(2);
        k = s16(p);
        if (k < 0 || n < 2 + (size_t)k * 4) return;
        poly(m, p + 2, k, fn == META_POLYGON);
        break;
    }
    case META_POLYPOLYGON: {               /* each polygon on its own: a hole is filled over, not cut out */
        int np, i, at;
        NEED(2);
        np = s16(p);
        if (np <= 0 || n < 2 + (size_t)np * 2) return;
        at = 2 + np * 2;
        for (i = 0; i < np && !m->stop; i++) {
            int k = s16(p + 2 + i * 2);
            if (k < 0 || (size_t)at + (size_t)k * 4 > n) return;
            poly(m, p + at, k, 1);
            at += k * 4;
        }
        break;
    }
    case META_TEXTOUT: {                   /* StringLength, String (to a word), YStart, XStart */
        int k;
        size_t padded;
        NEED(2);
        k = s16(p);
        if (k < 0) return;
        padded = ((size_t)k + 1) & ~(size_t)1;
        NEED(2 + padded + 4);
        text(m, s16(p + 2 + padded + 2), s16(p + 2 + padded), p + 2, (size_t)k);
        break;
    }
    case META_EXTTEXTOUT: {                /* Y, X, StringLength, fwOpts, [Rectangle], String, [Dx] */
        int k;
        unsigned opts;
        size_t at = 8;
        NEED(8);
        k = s16(p + 4);
        opts = u16(p + 6);
        if (opts & 0x0006) at += 8;        /* ETO_OPAQUE, ETO_CLIPPED: the rectangle is there */
        if (k < 0 || at + (size_t)k > n) return;
        text(m, s16(p + 2), s16(p), p + at, (size_t)k);
        break;
    }
    case META_CREATEPENINDIRECT: {         /* PenStyle, Width (a PointS, its x), ColorRef */
        mf_object *o = new_object(m, OBJ_PEN);
        unsigned style;
        if (!o || n < 10) return;
        style = u16(p) & 0x0F;
        o->width = s16(p + 2);
        o->colour = style == 5 ? 0 : colour_ref(p + 6);    /* PS_NULL */
        o->dash = style >= 1 && style <= 4;
        break;
    }
    case META_CREATEBRUSHINDIRECT: {       /* BrushStyle, ColorRef, BrushHatch */
        mf_object *o = new_object(m, OBJ_BRUSH);
        unsigned style;
        if (!o || n < 6) return;
        style = u16(p);
        o->colour = style == 0 || style == 2 ? colour_ref(p + 2) : 0;   /* BS_SOLID; BS_HATCHED as its colour */
        if (style != 0 && style != 1 && style != 2) m->res.skipped++;
        break;
    }
    case META_CREATEFONTINDIRECT: create_font(m, p, n); break;
    case META_DIBCREATEPATTERNBRUSH: case META_CREATEPATTERNBRUSH: {    /* a picture's pattern: no fill here */
        mf_object *o = new_object(m, OBJ_BRUSH);
        if (o) o->colour = 0;
        m->res.skipped++;
        break;
    }
    case META_CREATEPALETTE: case META_CREATEREGION: new_object(m, OBJ_OTHER); break;   /* they take a place */
    case META_SELECTOBJECT: {
        int i;
        NEED(2);
        i = (int)u16(p);
        if (i >= m->nobjects) return;
        if (m->objects[i].type == OBJ_PEN) m->dc.pen = m->objects[i];
        else if (m->objects[i].type == OBJ_BRUSH) m->dc.brush = m->objects[i];
        else if (m->objects[i].type == OBJ_FONT) m->dc.font = m->objects[i];
        break;
    }
    case META_DELETEOBJECT: {
        int i;
        NEED(2);
        i = (int)u16(p);
        if (i < m->nobjects) m->objects[i].type = OBJ_FREE;
        break;
    }
    case META_SAVEDC:
        if (m->nsaved < MF_MAX_SAVED) m->saved[m->nsaved] = m->dc;
        m->nsaved++;
        break;
    case META_RESTOREDC: {                 /* nSavedDC: < 0 back that many; > 0 that one */
        int k, to;
        NEED(2);
        k = s16(p);
        to = k < 0 ? m->nsaved + k : k - 1;
        if (to < 0 || to >= m->nsaved) return;
        m->nsaved = to;
        if (to < MF_MAX_SAVED) m->dc = m->saved[to];
        break;
    }
    /* state that doesn't change what is drawn here */
    case META_SETBKCOLOR: case META_SETBKMODE: case META_SETMAPMODE: case META_SETROP2: case META_SETRELABS:
    case META_SETPOLYFILLMODE: case META_SETSTRETCHBLTMODE: case META_SETTEXTCHAREXTRA:
    case META_SETTEXTJUSTIFICATION: case META_SETVIEWPORTORG: case META_SETVIEWPORTEXT:
    case META_OFFSETVIEWPORTORG: case META_SCALEVIEWPORTEXT: case META_SETMAPPERFLAGS: case META_SETLAYOUT:
    case META_SELECTPALETTE: case META_REALIZEPALETTE: case META_SETPALENTRIES: case META_RESIZEPALETTE:
    case META_ANIMATEPALETTE: case META_ESCAPE:
        break;
    default:                               /* pictures, arcs, regions, clipping, flood fills */
        m->res.skipped++;
        break;
    }
#undef NEED
}

static int has_placeable(const unsigned char *d, size_t n)
{
    return n >= 22 && d[0] == 0xD7 && d[1] == 0xCD && d[2] == 0xC6 && d[3] == 0x9A;
}

/* [MS-WMF] 2.3.2.2 META_HEADER at p: Type 1 or 2, HeaderSize 9 words */
static int meta_header(const unsigned char *p, size_t n)
{
    return n >= 18 && (u16(p) == 1 || u16(p) == 2) && u16(p + 2) == 9;
}

int owf_metafile_is_wmf(const unsigned char *data, size_t length)
{
    if (!data) return 0;
    if (has_placeable(data, length)) return meta_header(data + 22, length - 22);
    return meta_header(data, length);
}

/* every record from at, to META_EOF or the end */
static void play_records(mf_player *m, size_t at)
{
    while (!m->stop && at + 6 <= m->n) {
        unsigned long words = u32(m->d + at);
        unsigned fn = u16(m->d + at + 4);
        size_t bytes;
        if (words < 3 || words > (m->n - at) / 2) break;     /* cut short: what was drawn stays */
        bytes = (size_t)words * 2;
        if (fn == META_EOF) break;
        record(m, fn, m->d + at + 6, bytes - 6);
        at += bytes;
    }
}

/* With no window and no placeable header: the window is the first SETWINDOWORG and SETWINDOWEXT's */
static int find_window(const unsigned char *d, size_t n, size_t at, long *x, long *y, long *w, long *h)
{
    int got = 0;
    while (at + 6 <= n) {
        unsigned long words = u32(d + at);
        unsigned fn = u16(d + at + 4);
        if (words < 3 || words > (n - at) / 2 || fn == META_EOF) break;
        if (fn == META_SETWINDOWORG && words >= 5) { *y = s16(d + at + 6); *x = s16(d + at + 8); }
        if (fn == META_SETWINDOWEXT && words >= 5 && s16(d + at + 6) && s16(d + at + 8)) {
            *h = s16(d + at + 6); *w = s16(d + at + 8); got = 1; break;
        }
        at += (size_t)words * 2;
    }
    return got;
}

int owf_metafile_play(const unsigned char *data, size_t length, int x, int y, int w, int h, const owf_mf_sink *sink,
                      void *ctx, owf_mf_result *result)
{
    mf_player m;
    size_t at = 0;
    int nobj;
    if (result) memset(result, 0, sizeof *result);
    if (!owf_metafile_is_wmf(data, length) || !sink || !sink->path || !sink->ellipse || !sink->text || w <= 0 ||
        h <= 0)
        return OWF_ERR_FORMAT;
    memset(&m, 0, sizeof m);
    m.d = data; m.n = length; m.sink = sink; m.ctx = ctx;
    m.bx = x; m.by = y; m.bw = w; m.bh = h;
    if (has_placeable(data, length)) {     /* Key, HWmf, BoundingBox (Left, Top, Right, Bottom), Inch... */
        m.dc.win_x = s16(data + 6); m.dc.win_y = s16(data + 8);
        m.dc.win_w = s16(data + 10) - s16(data + 6); m.dc.win_h = s16(data + 12) - s16(data + 8);
        at = 22;
    }
    nobj = (int)u16(data + at + 10);       /* NumberOfObjects, after Type, HeaderSize, Version and Size */
    at += 18;
    if (!m.dc.win_w || !m.dc.win_h) {
        m.dc.win_x = m.dc.win_y = 0;
        if (!find_window(data, length, at, &m.dc.win_x, &m.dc.win_y, &m.dc.win_w, &m.dc.win_h))
            return OWF_ERR_FORMAT;         /* nothing says how big it is */
    }
    /* the defaults ([MS-WMF] 3.1.5): a black pen, a white brush, black text */
    m.dc.pen.type = OBJ_PEN; m.dc.pen.colour = OWF_SHADE(0);
    m.dc.brush.type = OBJ_BRUSH; m.dc.brush.colour = OWF_SHADE(0xFFFFFF);
    m.dc.text_colour = OWF_SHADE(0);
    m.nobjects = nobj < MF_MAX_OBJECTS ? nobj : MF_MAX_OBJECTS;
    if (m.nobjects && !(m.objects = (mf_object *)calloc((size_t)m.nobjects, sizeof *m.objects)))
        return OWF_ERR_MEMORY;
    play_records(&m, at);
    free(m.objects);
    free(m.xy);
    if (result) *result = m.res;
    return m.stop ? OWF_ERR_MEMORY : OWF_OK;
}
