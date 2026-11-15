/*
 * libowf: a WMF drawn into pixels (owf_metafile.h, owf_metafile_render), for the screens that show pictures as
 * pixels (GFOpenPresentation's and GFOpenPublisher's picture cache) where there is no metafile datatype. The
 * player's shapes are filled a row at a time at sixteenths of a pixel; lines are drawn as thin polygons; text is
 * left out here (counted), the PDF writer draws it. Integer arithmetic only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_metafile.h"

#include <stdlib.h>
#include <string.h>

#define SUB 16                   /* sixteenths of a pixel */

typedef struct {
    unsigned char *argb;
    int w, h;
    long *cross;                 /* a row's crossings */
    int capcross;
    int *pts;                    /* a line's or an ellipse's polygon */
    int cappts;
    int skipped_text;
    int cx0, cy0, cx1, cy1;      /* the pixels drawn: from cx0, cy0 up to, not at, cx1, cy1 */
} raster;

static int grow_cross(raster *r, int n)
{
    if (n > r->capcross) {
        long *q = (long *)realloc(r->cross, (size_t)n * sizeof *q);
        if (!q) return 0;
        r->cross = q;
        r->capcross = n;
    }
    return 1;
}

static int grow_pts(raster *r, int n)
{
    if (n > r->cappts) {
        int *q = (int *)realloc(r->pts, (size_t)n * 2 * sizeof *q);
        if (!q) return 0;
        r->pts = q;
        r->cappts = n;
    }
    return 1;
}

/* a / b rounded up, for any a and b > 0 */
static long ceil_div(long a, long b)
{
    return a >= 0 ? (a + b - 1) / b : -((-a) / b);
}

static void sort_longs(long *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        long v = a[i];
        for (j = i - 1; j >= 0 && a[j] > v; j--) a[j + 1] = a[j];
        a[j + 1] = v;
    }
}

/* the polygon (n points in sixteenths) filled with rgb, even-odd, each pixel whose centre is inside */
static int fill_polygon(raster *r, const int *xy, int n, unsigned long rgb)
{
    int i, y, ymin = xy[1], ymax = xy[1];
    unsigned char cr = (unsigned char)(rgb >> 16), cg = (unsigned char)(rgb >> 8), cb = (unsigned char)rgb;
    if (n < 3) return 1;
    for (i = 1; i < n; i++) {
        if (xy[i * 2 + 1] < ymin) ymin = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > ymax) ymax = xy[i * 2 + 1];
    }
    if (!grow_cross(r, n)) return 0;
    ymin = ymin / SUB - 1;
    ymax = ymax / SUB + 1;
    if (ymin < r->cy0) ymin = r->cy0;
    if (ymax > r->cy1 - 1) ymax = r->cy1 - 1;
    for (y = ymin; y <= ymax; y++) {
        long sy = (long)y * SUB + SUB / 2;
        int k = 0, j;
        for (i = 0; i < n; i++) {
            long long x0 = xy[i * 2], y0 = xy[i * 2 + 1];
            long long x1 = xy[((i + 1) % n) * 2], y1 = xy[((i + 1) % n) * 2 + 1];
            /* in long long: points reach +-0x3FFFFFFF (owf_metafile.c), and a long is 32 bits on m68k */
            if ((y0 <= sy && y1 > sy) || (y1 <= sy && y0 > sy))
                r->cross[k++] = (long)(x0 + (sy - y0) * (x1 - x0) / (y1 - y0));
        }
        sort_longs(r->cross, k);
        for (j = 0; j + 1 < k; j += 2) {
            /* the pixels whose centres x * SUB + SUB / 2 lie from one crossing up to the next */
            long xa = ceil_div(r->cross[j] - SUB / 2, SUB), xb = ceil_div(r->cross[j + 1] - SUB / 2, SUB) - 1;
            int x;
            if (xa < r->cx0) xa = r->cx0;
            if (xb > r->cx1 - 1) xb = r->cx1 - 1;
            for (x = (int)xa; x <= xb; x++) {
                unsigned char *p = r->argb + ((size_t)y * r->w + x) * 4;
                p[0] = 255; p[1] = cr; p[2] = cg; p[3] = cb;
            }
        }
    }
    return 1;
}

/* a line from one point to the next as a polygon width wide (at least a pixel) */
static int stroke_segment(raster *r, long long x0, long long y0, long long x1, long long y1, int width,
                          unsigned long rgb)
{
    /* long long: far points' differences pass 32 bits */
    long long dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy, len, nx, ny;
    int half = (width > SUB ? width : SUB) / 2, q[8];
    if (half > 0x10000000) half = 0x10000000;      /* a point and twice this still fit an int */
    /* the length, near enough: the larger plus a third of the smaller */
    len = adx > ady ? adx + ady * 3 / 8 : ady + adx * 3 / 8;
    if (!len) {
        q[0] = (int)x0 - half; q[1] = (int)y0 - half; q[2] = (int)x0 + half; q[3] = (int)y0 - half;
        q[4] = (int)x0 + half; q[5] = (int)y0 + half; q[6] = (int)x0 - half; q[7] = (int)y0 + half;
        return fill_polygon(r, q, 4, rgb);
    }
    nx = -dy * half / len;
    ny = dx * half / len;
    /* square ends: out by half the width along the line too */
    x0 -= dx * half / len; y0 -= dy * half / len;
    x1 += dx * half / len; y1 += dy * half / len;
    q[0] = (int)(x0 + nx); q[1] = (int)(y0 + ny); q[2] = (int)(x1 + nx); q[3] = (int)(y1 + ny);
    q[4] = (int)(x1 - nx); q[5] = (int)(y1 - ny); q[6] = (int)(x0 - nx); q[7] = (int)(y0 - ny);
    return fill_polygon(r, q, 4, rgb);
}

static int stroke(raster *r, const int *xy, int n, int closed, unsigned long rgb, int width)
{
    int i;
    for (i = 0; i + 1 < n; i++)
        if (!stroke_segment(r, xy[i * 2], xy[i * 2 + 1], xy[i * 2 + 2], xy[i * 2 + 3], width, rgb)) return 0;
    if (closed && n > 2 && !stroke_segment(r, xy[(n - 1) * 2], xy[(n - 1) * 2 + 1], xy[0], xy[1], width, rgb))
        return 0;
    return 1;
}

static int r_path(void *ctx, const int *xy, int n, int closed, unsigned long fill, unsigned long line, int lw,
                  int dash)
{
    raster *r = (raster *)ctx;
    (void)dash;                  /* dashes are drawn solid here */
    if (closed && OWF_SHADE_SET(fill) && !fill_polygon(r, xy, n, OWF_SHADE_RGB(fill))) return -1;
    if (OWF_SHADE_SET(line) && !stroke(r, xy, n, closed, OWF_SHADE_RGB(line), lw)) return -1;
    return 0;
}

/* sin of 0, 1/64, ... 16/64 of a turn, in 1/1024 */
static const int quarter[17] = { 0, 100, 200, 297, 392, 483, 569, 650, 724, 792, 851, 903, 946, 980, 1004, 1019, 1024 };

static int r_ellipse(void *ctx, int x, int y, int w, int h, unsigned long fill, unsigned long line, int lw, int dash)
{
    raster *r = (raster *)ctx;
    long long cx = (long long)x + w / 2, cy = (long long)y + h / 2, rx = w / 2, ry = h / 2;
    int i;
    if (!grow_pts(r, 64)) return -1;
    for (i = 0; i < 64; i++) {
        int q = i / 16, k = i % 16, s, c;
        /* sin and cos of i/64 of a turn from the quarter table */
        s = q == 0 ? quarter[k] : q == 1 ? quarter[16 - k] : q == 2 ? -quarter[k] : -quarter[16 - k];
        c = q == 0 ? quarter[16 - k] : q == 1 ? -quarter[k] : q == 2 ? -quarter[16 - k] : quarter[k];
        r->pts[i * 2] = (int)(cx + rx * c / 1024);
        r->pts[i * 2 + 1] = (int)(cy + ry * s / 1024);
    }
    return r_path(ctx, r->pts, 64, 1, fill, line, lw, dash);
}

static int r_text(void *ctx, int x, int baseline, const char *utf8, size_t length, owf_font_kind kind,
                  unsigned flags, int size, unsigned long colour)
{
    raster *r = (raster *)ctx;
    (void)x; (void)baseline; (void)utf8; (void)length; (void)kind; (void)flags; (void)size; (void)colour;
    r->skipped_text++;
    return 0;
}

/* the pixels whose centres are inside x, y, w, h (sixteenths); w -1: all of them */
static int r_clip(void *ctx, int x, int y, int w, int h)
{
    raster *r = (raster *)ctx;
    long long x1 = (long long)x + w, y1 = (long long)y + h;
    if (w < 0) { r->cx0 = r->cy0 = 0; r->cx1 = r->w; r->cy1 = r->h; return 0; }
    r->cx0 = (int)ceil_div(x - SUB / 2, SUB);
    r->cy0 = (int)ceil_div(y - SUB / 2, SUB);
    r->cx1 = (int)ceil_div((long)(x1 > 0x3FFFFFFF ? 0x3FFFFFFF : x1) - SUB / 2, SUB);
    r->cy1 = (int)ceil_div((long)(y1 > 0x3FFFFFFF ? 0x3FFFFFFF : y1) - SUB / 2, SUB);
    if (r->cx0 < 0) r->cx0 = 0;
    if (r->cy0 < 0) r->cy0 = 0;
    if (r->cx1 > r->w) r->cx1 = r->w;
    if (r->cy1 > r->h) r->cy1 = r->h;
    return 0;
}

int owf_metafile_render(const unsigned char *data, size_t length, unsigned char *argb, int w, int h,
                        owf_mf_result *result)
{
    static const owf_mf_sink sink = { r_path, r_ellipse, r_text, NULL, r_clip };
    raster r;
    owf_mf_result res;
    int rc;
    if (result) memset(result, 0, sizeof *result);
    if (!argb || w <= 0 || h <= 0 || w > 8192 || h > 8192) return OWF_ERR_FORMAT;
    memset(&r, 0, sizeof r);
    r.argb = argb;
    r.w = w;
    r.h = h;
    r.cx1 = w;
    r.cy1 = h;
    rc = owf_metafile_play(data, length, 0, 0, w * SUB, h * SUB, &sink, &r, &res);
    res.drawn -= r.skipped_text;
    res.skipped += r.skipped_text;
    free(r.cross);
    free(r.pts);
    if (result) *result = res;
    return rc;
}

/* a signed 32-bit little-endian number */
static long le32s(const unsigned char *p)
{
    unsigned long v = (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 |
                      (unsigned long)p[3] << 24;
    return v & 0x80000000UL ? -(long)((~v & 0xFFFFFFFFUL) + 1) : (long)v;
}

int owf_metafile_size(const unsigned char *data, size_t length, int *w, int *h)
{
    long dx, dy, inch;
    if (w) *w = 0;
    if (h) *h = 0;
    if (owf_metafile_is_emf(data, length)) {
        /* Frame (RectL, 0.01 mm) at 24: 2540 to the inch, 1440 twips */
        long l = le32s(data + 24), t = le32s(data + 28), r = le32s(data + 32), b = le32s(data + 36);
        long long fw = (long long)r - l, fh = (long long)b - t;
        if (fw <= 0 || fh <= 0 || fw > 10000000 || fh > 10000000) return 0;
        if (w) *w = (int)(fw * 1440 / 2540);
        if (h) *h = (int)(fh * 1440 / 2540);
        return fw * 1440 / 2540 > 0 && fh * 1440 / 2540 > 0;
    }
    if (!owf_metafile_is_wmf(data, length) || length < 22 || data[0] != 0xD7 || data[1] != 0xCD) return 0;
    dx = (long)(short)(data[10] | data[11] << 8) - (long)(short)(data[6] | data[7] << 8);
    dy = (long)(short)(data[12] | data[13] << 8) - (long)(short)(data[8] | data[9] << 8);
    inch = (long)(data[14] | data[15] << 8);
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (!dx || !dy || !inch) return 0;
    if (w) *w = (int)(dx * 1440 / inch);
    if (h) *h = (int)(dy * 1440 / inch);
    return 1;
}
