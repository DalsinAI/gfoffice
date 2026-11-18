/*
 * GFOpenPresentation's and GFOpenPressmaster's name for libowf's gradient fill as plain polygons
 * (openpresentation_core.h, opr_gradient_band): it is owf_fill_gradient_band (libowf/src/owf_fill.c,
 * owf_fill.h), the one copy every GFOffice program uses, kept so its callers need no change; and the
 * hatch lines cut to an outline (opr_hatch_lines), for a screen that draws lines but can't clip.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_fill.h"

#include <stdlib.h>

/* the same kinds, and the same largest outline as GFOpenLayout's, or the build stops here */
typedef char opr_gradient_kinds_match[OPR_GRADIENT_LINEAR == OWF_FILL_LINEAR && OPR_GRADIENT_RADIAL == OWF_FILL_RADIAL
                                      ? 1 : -1];
typedef char opr_gradient_outline_fits[OL_MAX_SHAPE_POINTS == OWF_FILL_MAX_OUTLINE ? 1 : -1];

int opr_gradient_band(const int *xy, int n, int x, int y, int w, int h, int kind, int angle, unsigned long from,
                      unsigned long to, int band, int bands, int *out, int cap, unsigned long *colour)
{
    return owf_fill_gradient_band(xy, n, x, y, w, h, kind, angle, from, to, band, bands, out, cap, colour);
}

/* ---- Hatching (Presentation gap 1) ---- */

static int compare_long(const void *a, const void *b)
{
    long long x = *(const long long *)a, y = *(const long long *)b;
    return x < y ? -1 : x > y;
}

/* One set of lines at angle, distance apart through (x, y), inside the closed outline xy (n points):
 * along each line t (the distance across the lines from (x, y)), where the outline's edges cross it,
 * sorted, paired inside to outside (even-odd) */
static int hatch_set(const int *xy, int n, int x, int y, int angle, int distance, int *out, int cap, int m)
{
    long long dx = (long long)owf_fill_sin16k(angle + 900) / 16, dy = -(long long)owf_fill_sin16k(angle) / 16;   /* along, * 1024: y runs down */
    long long nx = -dy, ny = dx;                                                    /* across */
    long long tmin = 0, tmax = 0, t, cross[2 * OL_MAX_SHAPE_POINTS];
    long long k, kfirst, klast;
    int i, c;
    if (n < 3 || n > 2 * OL_MAX_SHAPE_POINTS || distance <= 0)
        return m;
    for (i = 0; i < n; i++) {
        t = (xy[i * 2] - x) * nx + (xy[i * 2 + 1] - y) * ny;
        if (!i || t < tmin) tmin = t;
        if (!i || t > tmax) tmax = t;
    }
    /* the lines t = k * distance * 1024 that cross the outline, at most 4096 of them */
    kfirst = tmin >= 0 ? (tmin + (long long)distance * 1024 - 1) / ((long long)distance * 1024)
                       : -((-tmin) / ((long long)distance * 1024));
    klast = tmax >= 0 ? tmax / ((long long)distance * 1024)
                      : -((-tmax + (long long)distance * 1024 - 1) / ((long long)distance * 1024));
    if (klast - kfirst > 4096)
        klast = kfirst + 4096;
    for (k = kfirst; k <= klast; k++) {
        long long line = k * distance * 1024;
        c = 0;
        for (i = 0; i < n; i++) {
            long long x0 = xy[i * 2] - x, y0 = xy[i * 2 + 1] - y;
            long long x1 = xy[((i + 1) % n) * 2] - x, y1 = xy[((i + 1) % n) * 2 + 1] - y;
            long long t0 = x0 * nx + y0 * ny, t1 = x1 * nx + y1 * ny;
            long long s0 = x0 * dx + y0 * dy, s1 = x1 * dx + y1 * dy;
            /* an edge counts from its start up to but not at its end, so a corner on the line counts once */
            if ((t0 <= line && t1 > line) || (t1 <= line && t0 > line))
                cross[c++] = s0 + (s1 - s0) * (line - t0) / (t1 - t0);
        }
        qsort(cross, (size_t)c, sizeof cross[0], compare_long);
        for (i = 0; i + 1 < c; i += 2) {
            /* back from (along, across), both * 1024, to the page */
            long long a0 = cross[i], a1 = cross[i + 1];
            if (m >= cap)
                return -1;
            out[m * 4] = x + (int)((a0 * dx + line * nx) / (1024LL * 1024));
            out[m * 4 + 1] = y + (int)((a0 * dy + line * ny) / (1024LL * 1024));
            out[m * 4 + 2] = x + (int)((a1 * dx + line * nx) / (1024LL * 1024));
            out[m * 4 + 3] = y + (int)((a1 * dy + line * ny) / (1024LL * 1024));
            m++;
        }
    }
    return m;
}

int opr_hatch_lines(const int *xy, int n, int x, int y, int kind, int angle, int distance, int *out, int cap)
{
    int m = -1, tries;
    if (kind < OPR_HATCH_SINGLE || kind > OPR_HATCH_TRIPLE || !out || cap < 0 || distance <= 0)
        return 0;
    /* more lines than out holds (a tiny distance on a big shape): twice as far apart each time, through
     * the same (x, y), so every other line stays where it was, rather than none drawn at all */
    for (tries = 0; m < 0 && tries < 24 && distance > 0; tries++, distance = distance > 0x3FFFFFFF ? 0 : distance * 2) {
        m = hatch_set(xy, n, x, y, angle, distance, out, cap, 0);
        if (m >= 0 && kind >= OPR_HATCH_DOUBLE)
            m = hatch_set(xy, n, x, y, angle + 900, distance, out, cap, m);
        if (m >= 0 && kind == OPR_HATCH_TRIPLE)
            m = hatch_set(xy, n, x, y, angle + 450, distance, out, cap, m);
    }
    return m;
}
