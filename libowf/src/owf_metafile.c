/*
 * libowf: the Windows metafile player (owf_metafile.h), from [MS-WMF] and [MS-EMF]. Record and field names in the
 * comments are the specifications'. Every number in a file is little-endian; a WMF record is its size in 16-bit
 * words, its function, then its fields; an EMF record its type and its size in bytes (32 bits each), then its
 * fields.
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
    /* an EMF's logical to device mapping ([MS-EMF] 2.1.21 MapMode): the window's and the viewport's origins and
     * extents; MM_TEXT (or a WMF) maps one to one. win_* above is then the picture's frame, device units. */
    int map_mode;
    long ewx, ewy, eww, ewh, evx, evy, evw, evh;
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
    int emf;                           /* an EMF: positions go through the logical to device mapping first */
    long dev_cx, dev_cy, mm_cx, mm_cy; /* an EMF's reference device: pixels, and millimetres */
    long *lp;                          /* an EMF figure's points, logical */
    int caplp;
    long *path;                        /* an EMF path (BeginPath to EndPath): figures of logical points */
    int npath, cappath;
    int *figs;                         /* each figure: its first point, its count, closed */
    int nfigs, capfigs;
    int in_path, path_done;
} mf_player;

static unsigned u16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static int s16(const unsigned char *p) { unsigned v = u16(p); return v & 0x8000u ? (int)v - 0x10000 : (int)v; }
static unsigned long u32(const unsigned char *p) { return (unsigned long)u16(p) | ((unsigned long)u16(p + 2) << 16); }

/* [MS-WMF] 2.2.2.8 ColorRef: red, green, blue, reserved */
static unsigned long colour_ref(const unsigned char *p)
{
    return OWF_SHADE(((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2]);
}

/* far enough for any page, and well inside an int when the box's corner is added and points are subtracted */
#define MAP_LIMIT 0x3FFFFFFFLL

static long long clamp_map(long long v)
{
    return v > MAP_LIMIT ? MAP_LIMIT : v < -MAP_LIMIT ? -MAP_LIMIT : v;
}

/* a logical position l in the window (its origin and extent) to the box (its corner and size) */
static int map_one(long win_origin, long win_extent, int box_corner, int box_size, long l)
{
    long long v = win_extent ? clamp_map((long long)(l - win_origin) * box_size / win_extent) : 0;
    return (int)clamp_map(v + box_corner);
}

/* an EMF's logical position to its device's, through the window and the viewport (one to one for MM_TEXT) */
static long to_device(long l, long worg, long wext, long vorg, long vext, int mode)
{
    if (mode == 1 || !wext || !vext) return l;
    return (long)clamp_map((long long)(l - worg) * vext / wext + vorg);
}

static long dev_x(const mf_player *m, long l)
{
    return m->emf ? to_device(l, m->dc.ewx, m->dc.eww, m->dc.evx, m->dc.evw, m->dc.map_mode) : l;
}

static long dev_y(const mf_player *m, long l)
{
    return m->emf ? to_device(l, m->dc.ewy, m->dc.ewh, m->dc.evy, m->dc.evh, m->dc.map_mode) : l;
}

/* a logical length to the device's, never negative */
static long dev_len(long l, long wext, long vext, int mode)
{
    long long v;
    if (l < 0) l = -l;
    if (mode == 1 || !wext || !vext) return l;
    v = (long long)l * (vext < 0 ? -vext : vext) / (wext < 0 ? -wext : wext);
    return (long)clamp_map(v);
}

static int map_x(const mf_player *m, long lx)
{
    return map_one(m->dc.win_x, m->dc.win_w, m->bx, m->bw, dev_x(m, lx));
}

static int map_y(const mf_player *m, long ly)
{
    return map_one(m->dc.win_y, m->dc.win_h, m->by, m->bh, dev_y(m, ly));
}

/* a length across (a pen's width), the caller's units, never negative */
static int map_w(const mf_player *m, long l)
{
    long long v;
    if (m->emf) l = dev_len(l, m->dc.eww, m->dc.evw, m->dc.map_mode);
    v = (long long)l * m->bw;
    long w = m->dc.win_w < 0 ? -m->dc.win_w : m->dc.win_w;
    if (l < 0) v = -v;
    return w ? (int)clamp_map(v / w) : 0;
}

static int map_h(const mf_player *m, long l)
{
    long long v;
    if (m->emf) l = dev_len(l, m->dc.ewh, m->dc.evh, m->dc.map_mode);
    v = (long long)l * m->bh;
    long h = m->dc.win_h < 0 ? -m->dc.win_h : m->dc.win_h;
    if (l < 0) v = -v;
    return h ? (int)clamp_map(v / h) : 0;
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

/* UTF-8 text u (len bytes) at the logical position, as the font, colour and alignment say */
static void text_utf8(mf_player *m, long lx, long ly, const char *u, size_t len)
{
    int size, x, y, width, rc;
    const mf_object *f = &m->dc.font;
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
}

static void text(mf_player *m, long lx, long ly, const unsigned char *s, size_t n)
{
    char *u;
    if (!n) return;
    if (!(u = (char *)malloc(n * 3 + 1))) { m->stop = 1; return; }
    text_utf8(m, lx, ly, u, to_utf8(s, n, u));
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

/* ---- EMF ([MS-EMF]): records of 32-bit Type and Size (bytes), then fields; positions 32 or 16 bits ---- */

/* [MS-EMF] 2.1.1 RecordType */
enum {
    EMR_HEADER = 1, EMR_POLYBEZIER = 2, EMR_POLYGON = 3, EMR_POLYLINE = 4, EMR_POLYBEZIERTO = 5,
    EMR_POLYLINETO = 6, EMR_POLYPOLYLINE = 7, EMR_POLYPOLYGON = 8, EMR_SETWINDOWEXTEX = 9, EMR_SETWINDOWORGEX = 10,
    EMR_SETVIEWPORTEXTEX = 11, EMR_SETVIEWPORTORGEX = 12, EMR_EOF = 14, EMR_SETMAPMODE = 17, EMR_SETTEXTALIGN = 22,
    EMR_SETTEXTCOLOR = 24, EMR_MOVETOEX = 27, EMR_EXCLUDECLIPRECT = 29, EMR_INTERSECTCLIPRECT = 30,
    EMR_SCALEVIEWPORTEXTEX = 31, EMR_SCALEWINDOWEXTEX = 32, EMR_SAVEDC = 33, EMR_RESTOREDC = 34,
    EMR_SETWORLDTRANSFORM = 35, EMR_MODIFYWORLDTRANSFORM = 36, EMR_SELECTOBJECT = 37, EMR_CREATEPEN = 38,
    EMR_CREATEBRUSHINDIRECT = 39, EMR_DELETEOBJECT = 40, EMR_ELLIPSE = 42, EMR_RECTANGLE = 43, EMR_ROUNDRECT = 44,
    EMR_ARC = 45, EMR_CHORD = 46, EMR_PIE = 47, EMR_LINETO = 54, EMR_ARCTO = 55, EMR_BEGINPATH = 59,
    EMR_ENDPATH = 60, EMR_CLOSEFIGURE = 61, EMR_FILLPATH = 62, EMR_STROKEANDFILLPATH = 63, EMR_STROKEPATH = 64,
    EMR_ABORTPATH = 68, EMR_GDICOMMENT = 70, EMR_EXTSELECTCLIPRGN = 75, EMR_BITBLT = 76, EMR_STRETCHBLT = 77,
    EMR_MASKBLT = 78, EMR_PLGBLT = 79, EMR_SETDIBITSTODEVICE = 80, EMR_STRETCHDIBITS = 81,
    EMR_EXTCREATEFONTINDIRECTW = 82, EMR_EXTTEXTOUTA = 83, EMR_EXTTEXTOUTW = 84, EMR_POLYBEZIER16 = 85,
    EMR_POLYGON16 = 86, EMR_POLYLINE16 = 87, EMR_POLYBEZIERTO16 = 88, EMR_POLYLINETO16 = 89,
    EMR_POLYPOLYLINE16 = 90, EMR_POLYPOLYGON16 = 91, EMR_EXTCREATEPEN = 95, EMR_SELECTCLIPPATH = 67,
    EMR_ALPHABLEND = 114, EMR_TRANSPARENTBLT = 116, EMR_GRADIENTFILL = 118
};

#define EMF_SIGNATURE 0x464D4520UL     /* " EMF" */
#define EMF_MAX_FIGURES 4096

static long s32(const unsigned char *p)
{
    unsigned long v = u32(p);
    return v & 0x80000000UL ? -(long)((~v & 0xFFFFFFFFUL) + 1) : (long)v;
}

int owf_metafile_is_emf(const unsigned char *data, size_t length)
{
    return data && length >= 88 && u32(data) == EMR_HEADER && u32(data + 40) == EMF_SIGNATURE &&
           u32(data + 4) >= 88 && u32(data + 4) <= length;
}

int owf_metafile_can_play(const unsigned char *data, size_t length)
{
    return owf_metafile_is_wmf(data, length) || owf_metafile_is_emf(data, length);
}

/* room for n logical points in m->lp */
static int lpoints(mf_player *m, int n)
{
    if (n > m->caplp) {
        long *q = (long *)realloc(m->lp, (size_t)n * 2 * sizeof *q);
        if (!q) { m->stop = 1; return 0; }
        m->lp = q;
        m->caplp = n;
    }
    return 1;
}

/* the logical points lp (n of them) drawn as one figure, or added to the path being built */
static void emf_figure(mf_player *m, const long *lp, int n, int closed, int filled)
{
    int i;
    if (n < 2 || m->stop) return;
    if (m->in_path) {
        long *q;
        int *f;
        if (n > MF_MAX_POINTS || m->npath + n > MF_MAX_POINTS * 4 || m->nfigs >= EMF_MAX_FIGURES) return;
        if (m->npath + n > m->cappath) {
            int want = (m->npath + n) * 2;
            if (!(q = (long *)realloc(m->path, (size_t)want * 2 * sizeof *q))) { m->stop = 1; return; }
            m->path = q;
            m->cappath = want;
        }
        if (m->nfigs >= m->capfigs) {
            int want = m->capfigs ? m->capfigs * 2 : 16;
            if (!(f = (int *)realloc(m->figs, (size_t)want * 3 * sizeof *f))) { m->stop = 1; return; }
            m->figs = f;
            m->capfigs = want;
        }
        memcpy(m->path + m->npath * 2, lp, (size_t)n * 2 * sizeof *lp);
        m->figs[m->nfigs * 3] = m->npath;
        m->figs[m->nfigs * 3 + 1] = n;
        m->figs[m->nfigs * 3 + 2] = closed;
        m->nfigs++;
        m->npath += n;
        return;
    }
    if (n > MF_MAX_POINTS || !points(m, n)) return;
    for (i = 0; i < n; i++) {
        m->xy[i * 2] = map_x(m, lp[i * 2]);
        m->xy[i * 2 + 1] = map_y(m, lp[i * 2 + 1]);
    }
    if (!OWF_SHADE_SET(line_of(m)) && !(filled && OWF_SHADE_SET(fill_of(m)))) return;
    drew(m, m->sink->path(m->ctx, m->xy, n, closed, filled ? fill_of(m) : 0, line_of(m), line_width_of(m),
                          m->dc.pen.dash));
}

/* the open figure of the path (a MoveTo then LineTo, PolylineTo...): its last point, or start one at the current
 * position */
static int path_open_figure(mf_player *m)
{
    if (m->nfigs && !m->figs[(m->nfigs - 1) * 3 + 2] &&
        m->figs[(m->nfigs - 1) * 3] + m->figs[(m->nfigs - 1) * 3 + 1] == m->npath) {
        long *last = m->path + (m->npath - 1) * 2;
        if (last[0] == m->dc.cur_x && last[1] == m->dc.cur_y) return 1;
    }
    {
        long here[4];
        here[0] = here[2] = m->dc.cur_x;
        here[1] = here[3] = m->dc.cur_y;
        emf_figure(m, here, 2, 0, 0);          /* a figure of the one point, twice; the next points follow it */
        if (m->nfigs) { m->figs[(m->nfigs - 1) * 3 + 1] = 1; m->npath--; }
    }
    return m->nfigs > 0;
}

/* appends lp (n points) to the path's open figure */
static void path_append(mf_player *m, const long *lp, int n)
{
    long *q;
    if (n < 1 || !path_open_figure(m) || m->npath + n > MF_MAX_POINTS * 4 ||
        m->figs[(m->nfigs - 1) * 3 + 1] + n > MF_MAX_POINTS)
        return;
    if (m->npath + n > m->cappath) {
        int want = (m->npath + n) * 2;
        if (!(q = (long *)realloc(m->path, (size_t)want * 2 * sizeof *q))) { m->stop = 1; return; }
        m->path = q;
        m->cappath = want;
    }
    memcpy(m->path + m->npath * 2, lp, (size_t)n * 2 * sizeof *lp);
    m->npath += n;
    m->figs[(m->nfigs - 1) * 3 + 1] += n;
}

/* the path's figures drawn: filled (each figure closed), stroked, or both */
static void path_draw(mf_player *m, int fill, int stroke)
{
    mf_object pen = m->dc.pen, brush = m->dc.brush;
    int f;
    if (!m->path_done) return;
    if (!stroke) m->dc.pen.colour = 0;
    if (!fill) m->dc.brush.colour = 0;
    m->in_path = 0;
    for (f = 0; f < m->nfigs && !m->stop; f++) {
        int at = m->figs[f * 3], n = m->figs[f * 3 + 1], closed = fill || m->figs[f * 3 + 2];
        emf_figure(m, m->path + at * 2, n, closed, fill);
    }
    m->dc.pen = pen;
    m->dc.brush = brush;
    m->npath = m->nfigs = 0;
    m->path_done = 0;
}

/* count points at p (32-bit pairs, or 16-bit when s), into m->lp after `from` points already there */
static int read_points(mf_player *m, const unsigned char *p, int count, int s, int from)
{
    int i;
    if (!lpoints(m, from + count)) return 0;
    for (i = 0; i < count; i++) {
        m->lp[(from + i) * 2] = s ? s16(p + i * 4) : s32(p + i * 8);
        m->lp[(from + i) * 2 + 1] = s ? s16(p + i * 4 + 2) : s32(p + i * 8 + 4);
    }
    return 1;
}

/* Bezier curves from lp[0] through each next three (control, control, end) as lines: 8 steps each, into
 * m->lp from `out` on; the count written, or -1 */
#define BEZIER_STEPS 8
static int flatten_beziers(mf_player *m, int n)
{
    int segs = (n - 1) / 3, i, k, at;
    long *src;
    if (n < 4 || segs * BEZIER_STEPS + 1 > MF_MAX_POINTS) return -1;
    if (!(src = (long *)malloc((size_t)n * 2 * sizeof *src))) { m->stop = 1; return -1; }
    memcpy(src, m->lp, (size_t)n * 2 * sizeof *src);
    if (!lpoints(m, segs * BEZIER_STEPS + 1)) { free(src); return -1; }
    m->lp[0] = src[0];
    m->lp[1] = src[1];
    at = 1;
    for (i = 0; i < segs; i++) {
        const long *q = src + i * 6;
        for (k = 1; k <= BEZIER_STEPS; k++) {
            /* B(t) = (1-t)^3 P0 + 3(1-t)^2 t P1 + 3(1-t) t^2 P2 + t^3 P3, t = k/8, in 512ths */
            long long a = BEZIER_STEPS - k, b = k, d = (long long)BEZIER_STEPS * BEZIER_STEPS * BEZIER_STEPS;
            int c;
            for (c = 0; c < 2; c++) {
                long long v = a * a * a * q[c] + 3 * a * a * b * q[2 + c] + 3 * a * b * b * q[4 + c] +
                              b * b * b * q[6 + c];
                m->lp[at * 2 + c] = (long)clamp_map(v >= 0 ? (v + d / 2) / d : -((-v + d / 2) / d));
            }
            at++;
        }
    }
    free(src);
    return at;
}

/* EMR_POLY... : Bounds (RectL), Count, then the points */
static void emf_poly(mf_player *m, unsigned type, const unsigned char *p, size_t n)
{
    int s = type >= EMR_POLYBEZIER16, size = s ? 4 : 8, base;
    unsigned kind = s ? type - (EMR_POLYBEZIER16 - EMR_POLYBEZIER) : type;
    unsigned long count;
    int to = kind == EMR_POLYBEZIERTO || kind == EMR_POLYLINETO, total;
    if (n < 20) return;
    count = u32(p + 16);
    if (!count || count > MF_MAX_POINTS || n < 20 + count * (unsigned long)size) return;
    base = to ? 1 : 0;
    if (to) {
        if (!lpoints(m, 1)) return;
        m->lp[0] = m->dc.cur_x;
        m->lp[1] = m->dc.cur_y;
    }
    if (!read_points(m, p + 20, (int)count, s, base)) return;
    total = (int)count + base;
    if (kind == EMR_POLYBEZIER || kind == EMR_POLYBEZIERTO) {
        total = flatten_beziers(m, total);
        if (total < 0) return;
    }
    if (to && m->in_path) path_append(m, m->lp + 2, total - 1);   /* onto the figure ending where the pen is */
    else emf_figure(m, m->lp, total, kind == EMR_POLYGON, kind == EMR_POLYGON);
    if (to) {
        m->dc.cur_x = m->lp[(total - 1) * 2];
        m->dc.cur_y = m->lp[(total - 1) * 2 + 1];
    }
}

/* EMR_POLYPOLY... : Bounds, NumberOfPolygons, Count, each polygon's count, then the points */
static void emf_polypoly(mf_player *m, unsigned type, const unsigned char *p, size_t n)
{
    int s = type == EMR_POLYPOLYLINE16 || type == EMR_POLYPOLYGON16, size = s ? 4 : 8;
    int closed = type == EMR_POLYPOLYGON || type == EMR_POLYPOLYGON16;
    unsigned long np, count, i;
    size_t at;
    if (n < 24) return;
    np = u32(p + 16);
    count = u32(p + 20);
    if (!np || np > EMF_MAX_FIGURES || count > MF_MAX_POINTS * 4 || n < 24 + np * 4 + count * (unsigned long)size)
        return;
    at = 24 + np * 4;
    for (i = 0; i < np && !m->stop; i++) {
        unsigned long k = u32(p + 24 + i * 4);
        if (k > MF_MAX_POINTS || at + k * (unsigned long)size > n) return;
        if (!read_points(m, p + at, (int)k, s, 0)) return;
        emf_figure(m, m->lp, (int)k, closed, closed);   /* each on its own, as a WMF's PolyPolygon */
        at += k * (unsigned long)size;
    }
}

/* a rectangle (RectL at p) as four logical points, for a path */
static void emf_rect_figure(mf_player *m, const unsigned char *p)
{
    long q[8];
    q[0] = s32(p); q[1] = s32(p + 4); q[2] = s32(p + 8); q[3] = s32(p + 4);
    q[4] = s32(p + 8); q[5] = s32(p + 12); q[6] = s32(p); q[7] = s32(p + 12);
    emf_figure(m, q, 4, 1, 1);
}

/* an ellipse in its box (RectL at p) as four Bezier quarters, flattened, for a path; 2263/4096 the quarter's
 * control distance */
static void emf_ellipse_figure(mf_player *m, const unsigned char *p)
{
    long l = s32(p), t = s32(p + 4), r = s32(p + 8), b = s32(p + 12);
    long cx = l + (r - l) / 2, cy = t + (b - t) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
    long kx = (long)((long long)rx * 2263 / 4096), ky = (long)((long long)ry * 2263 / 4096);
    long q[26];
    int n;
    q[0] = cx + rx; q[1] = cy;
    q[2] = cx + rx; q[3] = cy + ky; q[4] = cx + kx; q[5] = cy + ry; q[6] = cx; q[7] = cy + ry;
    q[8] = cx - kx; q[9] = cy + ry; q[10] = cx - rx; q[11] = cy + ky; q[12] = cx - rx; q[13] = cy;
    q[14] = cx - rx; q[15] = cy - ky; q[16] = cx - kx; q[17] = cy - ry; q[18] = cx; q[19] = cy - ry;
    q[20] = cx + kx; q[21] = cy - ry; q[22] = cx + rx; q[23] = cy - ky; q[24] = cx + rx; q[25] = cy;
    if (!lpoints(m, 13)) return;
    memcpy(m->lp, q, sizeof q);
    if ((n = flatten_beziers(m, 13)) > 0) emf_figure(m, m->lp, n, 1, 1);
}

/* the fixed mapping modes' window and viewport extents from the reference device ([MS-EMF] 2.1.21) */
static void emf_map_mode(mf_player *m, int mode)
{
    long per_mm10 = 0;                         /* logical units a tenth of a millimetre... as units in 254 mm */
    m->dc.map_mode = mode;
    switch (mode) {
    case 2: per_mm10 = 2540; break;            /* MM_LOMETRIC: 0.1 mm; 254 mm hold 2540 */
    case 3: per_mm10 = 25400; break;           /* MM_HIMETRIC: 0.01 mm */
    case 4: per_mm10 = 1000; break;            /* MM_LOENGLISH: 0.01 inch; 254 mm are 10 inches */
    case 5: per_mm10 = 10000; break;           /* MM_HIENGLISH: 0.001 inch */
    case 6: per_mm10 = 14400; break;           /* MM_TWIPS */
    default: return;                           /* MM_TEXT one to one; MM_ISOTROPIC, MM_ANISOTROPIC as set */
    }
    if (!m->mm_cx || !m->mm_cy) return;
    m->dc.ewx = m->dc.ewy = m->dc.evx = m->dc.evy = 0;
    m->dc.eww = m->mm_cx * per_mm10 / 254;
    m->dc.ewh = m->mm_cy * per_mm10 / 254;
    m->dc.evw = m->dev_cx;
    m->dc.evh = -m->dev_cy;                    /* y goes up */
}

/* [MS-EMF] 2.2.13 LogFont: Height, Width, Escapement, Orientation, Weight, Italic, Underline, StrikeOut,
 * CharSet, OutPrecision, ClipPrecision, Quality, PitchAndFamily, FaceName (32 UTF-16LE) */
static void emf_font(mf_object *o, const unsigned char *p, size_t n)
{
    char face[33];
    int i;
    memset(o, 0, sizeof *o);
    o->type = OBJ_FONT;
    if (n < 28) return;
    o->height = (int)clamp_map(s32(p));
    if (s32(p + 16) >= 600) o->flags |= OWF_BOLD;
    if (p[20]) o->flags |= OWF_ITALIC;
    if (p[21]) o->flags |= OWF_UNDERLINE;
    if (p[22]) o->flags |= OWF_STRIKE;
    for (i = 0; i < 32 && (size_t)(28 + i * 2 + 1) < n && u16(p + 28 + i * 2); i++) {
        unsigned c = u16(p + 28 + i * 2);
        face[i] = (char)(c < 0x80 ? c : '?');
    }
    face[i] = 0;
    if (strstr(face, "Courier") || strstr(face, "Mono") || strstr(face, "Consol")) o->kind = OWF_FONT_MONO;
    else if (strstr(face, "Times") || strstr(face, "Georgia") || strstr(face, "Garamond") || strstr(face, "Roman") ||
             strstr(face, "Book") || strstr(face, "Serif"))
        o->kind = OWF_FONT_SERIF;
    else if (face[0]) o->kind = OWF_FONT_SANS;
    else o->kind = (p[27] & 0xF0) == 0x10 ? OWF_FONT_SERIF : (p[27] & 0xF0) == 0x30 ? OWF_FONT_MONO : OWF_FONT_SANS;
}

/* UTF-16LE (n code units at s) as UTF-8 into out (3 bytes a unit at most; pairs as 4): its length */
static size_t utf16_to_utf8(const unsigned char *s, size_t n, char *out)
{
    size_t i, o = 0;
    for (i = 0; i < n; i++) {
        unsigned long c = u16(s + i * 2);
        if (!c) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && u16(s + i * 2 + 2) >= 0xDC00 && u16(s + i * 2 + 2) < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (u16(s + i * 2 + 2) - 0xDC00);
            i++;
        } else if (c >= 0xD800 && c < 0xE000) {
            c = '?';
        }
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) {
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        } else {
            out[o++] = (char)(0xF0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 63));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        }
    }
    out[o] = 0;
    return o;
}

/* a stock object ([MS-EMF] 2.1.31 StockObject, the index's top bit set) */
static void emf_stock(mf_player *m, unsigned long index)
{
    static const unsigned long brushes[5] = { 0xFFFFFF, 0xC0C0C0, 0x808080, 0x404040, 0x000000 };
    unsigned long k = index & 0x7FFFFFFFUL;
    if (k <= 4) { m->dc.brush.type = OBJ_BRUSH; m->dc.brush.colour = OWF_SHADE(brushes[k]); }
    else if (k == 5) { m->dc.brush.type = OBJ_BRUSH; m->dc.brush.colour = 0; }            /* NULL_BRUSH */
    else if (k == 6 || k == 7 || k == 8) {                                               /* WHITE, BLACK, NULL pens */
        m->dc.pen.type = OBJ_PEN;
        m->dc.pen.colour = k == 6 ? OWF_SHADE(0xFFFFFF) : k == 7 ? OWF_SHADE(0) : 0;
        m->dc.pen.width = 0;
        m->dc.pen.dash = 0;
    } else if (k >= 10 && k <= 17) { memset(&m->dc.font, 0, sizeof m->dc.font); m->dc.font.type = OBJ_FONT; }
    else if (k == 18) { m->dc.brush.type = OBJ_BRUSH; m->dc.brush.colour = OWF_SHADE(0xFFFFFF); }  /* DC_BRUSH */
    else if (k == 19) { m->dc.pen.type = OBJ_PEN; m->dc.pen.colour = OWF_SHADE(0); }              /* DC_PEN */
}

/* the object at index ihObject (1 on: 0 is the file's own), or NULL */
static mf_object *emf_object(mf_player *m, unsigned long index)
{
    return index >= 1 && index < (unsigned long)m->nobjects ? &m->objects[index] : NULL;
}

/* one EMF record's fields at p (after Type and Size), n bytes */
static void emf_record(mf_player *m, unsigned long type, const unsigned char *p, size_t n)
{
#define NEED(k) do { if (n < (size_t)(k)) return; } while (0)
    switch (type) {
    case EMR_SETMAPMODE: NEED(4); emf_map_mode(m, (int)u32(p)); break;
    case EMR_SETWINDOWEXTEX:
        NEED(8);
        if ((m->dc.map_mode == 7 || m->dc.map_mode == 8) && s32(p) && s32(p + 4)) {
            m->dc.eww = s32(p); m->dc.ewh = s32(p + 4);
        }
        break;
    case EMR_SETWINDOWORGEX: NEED(8); m->dc.ewx = s32(p); m->dc.ewy = s32(p + 4); break;
    case EMR_SETVIEWPORTEXTEX:
        NEED(8);
        if ((m->dc.map_mode == 7 || m->dc.map_mode == 8) && s32(p) && s32(p + 4)) {
            m->dc.evw = s32(p); m->dc.evh = s32(p + 4);
        }
        break;
    case EMR_SETVIEWPORTORGEX: NEED(8); m->dc.evx = s32(p); m->dc.evy = s32(p + 4); break;
    case EMR_SCALEWINDOWEXTEX: case EMR_SCALEVIEWPORTEXTEX: {   /* xNum, xDenom, yNum, yDenom */
        long *w = type == EMR_SCALEWINDOWEXTEX ? &m->dc.eww : &m->dc.evw;
        long *h = type == EMR_SCALEWINDOWEXTEX ? &m->dc.ewh : &m->dc.evh;
        NEED(16);
        if ((m->dc.map_mode == 7 || m->dc.map_mode == 8) && s32(p) && s32(p + 4) && s32(p + 8) && s32(p + 12)) {
            *w = (long)clamp_map((long long)*w * s32(p) / s32(p + 4));
            *h = (long)clamp_map((long long)*h * s32(p + 8) / s32(p + 12));
        }
        break;
    }
    case EMR_SETTEXTCOLOR: NEED(4); m->dc.text_colour = colour_ref(p); break;
    case EMR_SETTEXTALIGN: NEED(4); m->dc.align = (unsigned)u32(p); break;
    case EMR_MOVETOEX: NEED(8); m->dc.cur_x = s32(p); m->dc.cur_y = s32(p + 4); break;
    case EMR_LINETO: {
        long q[4];
        NEED(8);
        q[0] = m->dc.cur_x; q[1] = m->dc.cur_y; q[2] = s32(p); q[3] = s32(p + 4);
        if (m->in_path) path_append(m, q + 2, 1);          /* onto the figure ending where the pen is */
        else emf_figure(m, q, 2, 0, 0);
        m->dc.cur_x = q[2]; m->dc.cur_y = q[3];
        break;
    }
    case EMR_RECTANGLE:
        NEED(16);
        if (m->in_path) emf_rect_figure(m, p);
        else rectangle(m, (int)clamp_map(s32(p)), (int)clamp_map(s32(p + 4)), (int)clamp_map(s32(p + 8)),
                       (int)clamp_map(s32(p + 12)));
        break;
    case EMR_ELLIPSE:
        NEED(16);
        if (m->in_path) emf_ellipse_figure(m, p);
        else ellipse(m, (int)clamp_map(s32(p)), (int)clamp_map(s32(p + 4)), (int)clamp_map(s32(p + 8)),
                     (int)clamp_map(s32(p + 12)));
        break;
    case EMR_ROUNDRECT:                                  /* Box (RectL), Corner (SizeL) */
        NEED(24);
        if (m->in_path) emf_rect_figure(m, p);
        else round_rect(m, (int)clamp_map(s32(p)), (int)clamp_map(s32(p + 4)), (int)clamp_map(s32(p + 8)),
                        (int)clamp_map(s32(p + 12)), (int)clamp_map(s32(p + 16)), (int)clamp_map(s32(p + 20)));
        break;
    case EMR_POLYBEZIER: case EMR_POLYGON: case EMR_POLYLINE: case EMR_POLYBEZIERTO: case EMR_POLYLINETO:
    case EMR_POLYBEZIER16: case EMR_POLYGON16: case EMR_POLYLINE16: case EMR_POLYBEZIERTO16:
    case EMR_POLYLINETO16:
        emf_poly(m, (unsigned)type, p, n);
        break;
    case EMR_POLYPOLYLINE: case EMR_POLYPOLYGON: case EMR_POLYPOLYLINE16: case EMR_POLYPOLYGON16:
        emf_polypoly(m, (unsigned)type, p, n);
        break;
    case EMR_BEGINPATH: m->in_path = 1; m->path_done = 0; m->npath = m->nfigs = 0; break;
    case EMR_ENDPATH: if (m->in_path) { m->in_path = 0; m->path_done = 1; } break;
    case EMR_ABORTPATH: m->in_path = m->path_done = 0; m->npath = m->nfigs = 0; break;
    case EMR_CLOSEFIGURE: if (m->in_path && m->nfigs) m->figs[(m->nfigs - 1) * 3 + 2] = 1; break;
    case EMR_FILLPATH: path_draw(m, 1, 0); break;
    case EMR_STROKEANDFILLPATH: path_draw(m, 1, 1); break;
    case EMR_STROKEPATH: path_draw(m, 0, 1); break;
    case EMR_CREATEPEN: {                                /* ihPen, LogPen: PenStyle, Width (PointL), ColorRef */
        mf_object *o;
        unsigned style;
        NEED(20);
        if (!(o = emf_object(m, u32(p)))) return;
        memset(o, 0, sizeof *o);
        o->type = OBJ_PEN;
        style = (unsigned)(u32(p + 4) & 0x0F);
        o->width = (int)clamp_map(s32(p + 8));
        o->colour = style == 5 ? 0 : colour_ref(p + 16);              /* PS_NULL */
        o->dash = style >= 1 && style <= 4;
        break;
    }
    case EMR_EXTCREATEPEN: {                             /* ihPen, offBmi, cbBmi, offBits, cbBits, LogPenEx */
        mf_object *o;
        unsigned style;
        NEED(36);
        if (!(o = emf_object(m, u32(p)))) return;
        memset(o, 0, sizeof *o);
        o->type = OBJ_PEN;
        style = (unsigned)(u32(p + 20) & 0x0F);
        o->width = (int)clamp_map(s32(p + 24));
        o->colour = style == 5 || u32(p + 28) == 1 ? 0 : colour_ref(p + 32);   /* PS_NULL, BS_NULL */
        o->dash = style >= 1 && style <= 4;
        break;
    }
    case EMR_CREATEBRUSHINDIRECT: {                      /* ihBrush, LogBrush: BrushStyle, Color, BrushHatch */
        mf_object *o;
        unsigned long style;
        NEED(16);
        if (!(o = emf_object(m, u32(p)))) return;
        memset(o, 0, sizeof *o);
        o->type = OBJ_BRUSH;
        style = u32(p + 4);
        o->colour = style == 0 || style == 2 ? colour_ref(p + 8) : 0;   /* BS_SOLID; BS_HATCHED as its colour */
        if (style != 0 && style != 1 && style != 2) m->res.skipped++;
        break;
    }
    case EMR_EXTCREATEFONTINDIRECTW: {
        mf_object *o;
        NEED(4);
        if ((o = emf_object(m, u32(p)))) emf_font(o, p + 4, n - 4);
        break;
    }
    case EMR_SELECTOBJECT: {
        unsigned long i;
        mf_object *o;
        NEED(4);
        i = u32(p);
        if (i & 0x80000000UL) { emf_stock(m, i); break; }
        if (!(o = emf_object(m, i))) return;
        if (o->type == OBJ_PEN) m->dc.pen = *o;
        else if (o->type == OBJ_BRUSH) m->dc.brush = *o;
        else if (o->type == OBJ_FONT) m->dc.font = *o;
        break;
    }
    case EMR_DELETEOBJECT: {
        mf_object *o;
        NEED(4);
        if ((o = emf_object(m, u32(p)))) o->type = OBJ_FREE;
        break;
    }
    case EMR_SAVEDC:
        if (m->nsaved < MF_MAX_SAVED) m->saved[m->nsaved] = m->dc;
        m->nsaved++;
        break;
    case EMR_RESTOREDC: {                                /* SavedDC: < 0, back that many */
        long k, to;
        NEED(4);
        k = s32(p);
        to = k < 0 ? m->nsaved + k : k - 1;
        if (to < 0 || to >= m->nsaved) return;
        m->nsaved = (int)to;
        if (to < MF_MAX_SAVED) m->dc = m->saved[to];
        break;
    }
    case EMR_EXTTEXTOUTA: case EMR_EXTTEXTOUTW: {
        /* Bounds, iGraphicsMode, exScale, eyScale, then EmrText: Reference (PointL), Chars, offString (from the
         * record's start), Options, Rectangle, offDx */
        unsigned long chars, off;
        char *u;
        size_t len;
        int wide = type == EMR_EXTTEXTOUTW;
        NEED(44);
        chars = u32(p + 36);
        off = u32(p + 40);
        if (!chars || off < 8 || chars > 65536 || (off - 8) + chars * (wide ? 2 : 1) > n) return;
        if (!(u = (char *)malloc(chars * 4 + 1))) { m->stop = 1; return; }
        len = wide ? utf16_to_utf8(p + off - 8, chars, u) : to_utf8(p + off - 8, chars, u);
        text_utf8(m, s32(p + 28), s32(p + 32), u, len);
        free(u);
        break;
    }
    /* drawn as nothing and counted: pictures inside, arcs, clipping, world transforms, gradients */
    case EMR_ARC: case EMR_CHORD: case EMR_PIE: case EMR_ARCTO: case EMR_EXCLUDECLIPRECT:
    case EMR_INTERSECTCLIPRECT: case EMR_EXTSELECTCLIPRGN: case EMR_SELECTCLIPPATH: case EMR_SETWORLDTRANSFORM:
    case EMR_MODIFYWORLDTRANSFORM: case EMR_BITBLT: case EMR_STRETCHBLT: case EMR_MASKBLT: case EMR_PLGBLT:
    case EMR_SETDIBITSTODEVICE: case EMR_STRETCHDIBITS: case EMR_ALPHABLEND: case EMR_TRANSPARENTBLT:
    case EMR_GRADIENTFILL:
        m->res.skipped++;
        break;
    default:                                             /* state that doesn't change what is drawn here, comments */
        break;
    }
#undef NEED
}

/* the EMF's frame in device units: Frame (0.01 mm) through the reference device, else Bounds */
static int emf_frame(const unsigned char *d, mf_player *m)
{
    long fl = s32(d + 24), ft = s32(d + 28), fr = s32(d + 32), fb = s32(d + 36);
    m->dev_cx = s32(d + 72);
    m->dev_cy = s32(d + 76);
    m->mm_cx = s32(d + 80);
    m->mm_cy = s32(d + 84);
    if (m->dev_cx > 0 && m->dev_cy > 0 && m->mm_cx > 0 && m->mm_cy > 0 && fr > fl && fb > ft) {
        m->dc.win_x = (long)((long long)fl * m->dev_cx / (m->mm_cx * 100LL));
        m->dc.win_y = (long)((long long)ft * m->dev_cy / (m->mm_cy * 100LL));
        m->dc.win_w = (long)((long long)(fr - fl) * m->dev_cx / (m->mm_cx * 100LL));
        m->dc.win_h = (long)((long long)(fb - ft) * m->dev_cy / (m->mm_cy * 100LL));
        if (m->dc.win_w > 0 && m->dc.win_h > 0) return 1;
    }
    if (s32(d + 16) < s32(d + 8) || s32(d + 20) < s32(d + 12)) return 0;    /* Bounds, inclusive */
    m->dc.win_x = s32(d + 8);
    m->dc.win_y = s32(d + 12);
    m->dc.win_w = s32(d + 16) - s32(d + 8) + 1;
    m->dc.win_h = s32(d + 20) - s32(d + 12) + 1;
    return 1;
}

static int emf_play(mf_player *m)
{
    size_t at;
    unsigned long handles = u16(m->d + 56);
    if (!emf_frame(m->d, m)) return OWF_ERR_FORMAT;
    m->emf = 1;
    m->dc.map_mode = 1;
    m->dc.eww = m->dc.ewh = m->dc.evw = m->dc.evh = 1;
    m->nobjects = handles < MF_MAX_OBJECTS ? (int)handles : MF_MAX_OBJECTS;
    if (m->nobjects && !(m->objects = (mf_object *)calloc((size_t)m->nobjects, sizeof *m->objects)))
        return OWF_ERR_MEMORY;
    at = u32(m->d + 4);
    while (!m->stop && at + 8 <= m->n) {
        unsigned long type = u32(m->d + at), size = u32(m->d + at + 4);
        if (size < 8 || size > m->n - at || (size & 3)) break;    /* cut short: what was drawn stays */
        if (type == EMR_EOF) break;
        emf_record(m, type, m->d + at + 8, size - 8);
        at += size;
    }
    return OWF_OK;
}

int owf_metafile_play(const unsigned char *data, size_t length, int x, int y, int w, int h, const owf_mf_sink *sink,
                      void *ctx, owf_mf_result *result)
{
    mf_player m;
    size_t at = 0;
    int nobj;
    if (result) memset(result, 0, sizeof *result);
    if (!owf_metafile_can_play(data, length) || !sink || !sink->path || !sink->ellipse || !sink->text || w <= 0 ||
        h <= 0)
        return OWF_ERR_FORMAT;
    memset(&m, 0, sizeof m);
    m.d = data; m.n = length; m.sink = sink; m.ctx = ctx;
    m.bx = x; m.by = y; m.bw = w; m.bh = h;
    /* the defaults ([MS-WMF] 3.1.5, [MS-EMF] 3.1): a black pen, a white brush, black text */
    m.dc.pen.type = OBJ_PEN; m.dc.pen.colour = OWF_SHADE(0);
    m.dc.brush.type = OBJ_BRUSH; m.dc.brush.colour = OWF_SHADE(0xFFFFFF);
    m.dc.text_colour = OWF_SHADE(0);
    if (owf_metafile_is_emf(data, length)) {
        int rc = emf_play(&m);
        free(m.objects);
        free(m.xy);
        free(m.lp);
        free(m.path);
        free(m.figs);
        if (result) *result = m.res;
        return rc != OWF_OK ? rc : m.stop ? OWF_ERR_MEMORY : OWF_OK;
    }
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
    m.nobjects = nobj < MF_MAX_OBJECTS ? nobj : MF_MAX_OBJECTS;
    if (m.nobjects && !(m.objects = (mf_object *)calloc((size_t)m.nobjects, sizeof *m.objects)))
        return OWF_ERR_MEMORY;
    play_records(&m, at);
    free(m.objects);
    free(m.xy);
    if (result) *result = m.res;
    return m.stop ? OWF_ERR_MEMORY : OWF_OK;
}
