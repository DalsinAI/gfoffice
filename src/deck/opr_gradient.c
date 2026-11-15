/*
 * GFOpenPresentation's and GFOpenPublisher's gradient fills as plain polygons (openpresentation_core.h,
 * opr_gradient_band), for screens that fill polygons but cannot clip to one: each band of the gradient is
 * the shape's outline cut by the band's own edges (two parallel lines for a linear gradient, an ellipse
 * for a radial one), with the band's colour. The PDF clips instead (owf_pdf_gradient). Integer only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"

#include <string.h>

/* sin of whole degrees 0..90, * 16384 */
static const unsigned short sin_degree[91] = {
    0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406, 3686, 3964, 4240, 4516, 4790, 5063,
    5334, 5604, 5872, 6138, 6402, 6664, 6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630,
    9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733, 12911,
    13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082, 15191,
    15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182, 16225, 16262, 16294,
    16322, 16344, 16362, 16374, 16382, 16384
};

/* sin of tenths of a degree (to the nearest degree), * 16384 */
static long sin16k(int tenths)
{
    int d = ((tenths % 3600 + 3600) % 3600 + 5) / 10 % 360;
    return d <= 90 ? sin_degree[d] : d <= 180 ? sin_degree[180 - d] : d <= 270 ? -(long)sin_degree[d - 180]
                                                                                : -(long)sin_degree[360 - d];
}

static unsigned long blend(unsigned long from, unsigned long to, long k, long n)
{
    unsigned long r = 0;
    int shift;
    for (shift = 16; shift >= 0; shift -= 8) {
        long a = (long)((from >> shift) & 255), b = (long)((to >> shift) & 255);
        r |= (unsigned long)(a + (b - a) * k / n) << shift;
    }
    return r;
}

/* the polygon in (n points) cut to where a*x + b*y >= c, into out (at most cap points); its count */
static int keep_side(const int *in, int n, long long a, long long b, long long c, int *out, int cap)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        long long x0 = in[i * 2], y0 = in[i * 2 + 1];
        long long x1 = in[((i + 1) % n) * 2], y1 = in[((i + 1) % n) * 2 + 1];
        long long d0 = a * x0 + b * y0 - c, d1 = a * x1 + b * y1 - c;
        if (d0 >= 0) {
            if (m >= cap) return -1;
            out[m * 2] = (int)x0;
            out[m * 2 + 1] = (int)y0;
            m++;
        }
        if ((d0 >= 0) != (d1 >= 0)) {
            /* where the edge crosses the line, rounded */
            long long den = d0 - d1, nx = x0 * den + (x1 - x0) * d0, ny = y0 * den + (y1 - y0) * d0;
            if (m >= cap) return -1;
            if (den < 0) { den = -den; nx = -nx; ny = -ny; }
            out[m * 2] = (int)((nx >= 0 ? nx + den / 2 : nx - den / 2) / den);
            out[m * 2 + 1] = (int)((ny >= 0 ? ny + den / 2 : ny - den / 2) / den);
            m++;
        }
    }
    return m;
}

#define RING_SIDES 32

int opr_gradient_band(const int *xy, int n, int x, int y, int w, int h, int kind, int angle, unsigned long from,
                      unsigned long to, int band, int bands, int *out, int cap, unsigned long *colour)
{
    int a[2 * OPR_GRADIENT_MAX_POINTS], b[2 * OPR_GRADIENT_MAX_POINTS];
    int cur = n, i, k;
    long long cx = (long long)x * 2 + w, cy = (long long)y * 2 + h;   /* doubled: the centre to half a unit */
    if (!xy || n < 3 || n > OL_MAX_SHAPE_POINTS || bands < 1 || band < 0 || band >= bands || !out || !colour ||
        w <= 0 || h <= 0)
        return 0;
    from &= 0xFFFFFFUL;
    to &= 0xFFFFFFUL;
    memcpy(a, xy, (size_t)n * 2 * sizeof *a);
    if (kind == OPR_GRADIENT_RADIAL) {
        /* band 0 the outermost ring (the box's ellipse through its corners, sqrt 2 as 181/128), each
         * after it smaller and drawn over it; the last the centre's colour. Worked in sixteenths, so a
         * small ring's sides keep their slant and the centre stays inside each of them. */
        long long rx = (long long)w * 181 * 16 * (bands - band) / (256 * bands) + 16;
        long long ry = (long long)h * 181 * 16 * (bands - band) / (256 * bands) + 16;
        long long mx = cx * 8, my = cy * 8;                            /* the centre, sixteenths */
        *colour = blend(from, to, (long)(bands - band) * 2 - 1, (long)bands * 2);
        for (i = 0; i < n * 2; i++) a[i] = xy[i] * 16;
        for (k = 0; k < RING_SIDES && cur >= 3; k++) {
            long long px = mx + rx * sin16k(k * 3600 / RING_SIDES + 900) / 16384;
            long long py = my - ry * sin16k(k * 3600 / RING_SIDES) / 16384;
            long long qx = mx + rx * sin16k((k + 1) * 3600 / RING_SIDES + 900) / 16384;
            long long qy = my - ry * sin16k((k + 1) * 3600 / RING_SIDES) / 16384;
            long long la = py - qy, lb = qx - px, lc = la * px + lb * py;
            /* keep the side the centre is on */
            if (la * mx + lb * my < lc) { la = -la; lb = -lb; lc = -lc; }
            cur = keep_side(k & 1 ? b : a, cur, la, lb, lc, k & 1 ? a : b, OPR_GRADIENT_MAX_POINTS);
            if (cur < 0) return -1;
        }
        if (cur < 3) return 0;
        if (cur > cap) return -1;
        for (i = 0; i < cur * 2; i++) {
            int v = (RING_SIDES & 1 ? b : a)[i];
            out[i] = v >= 0 ? (v + 8) / 16 : -((-v + 8) / 16);
        }
        return cur;
    } else {
        /* the strip from t0 to t1 along the way (y down), the outline's own extent cut into bands */
        long ux = sin16k(angle + 900), uy = -sin16k(angle);
        long long lo = 0, hi = 0, t0, t1;
        for (i = 0; i < n; i++) {
            long long t = (long long)xy[i * 2] * ux + (long long)xy[i * 2 + 1] * uy;
            if (!i || t < lo) lo = t;
            if (!i || t > hi) hi = t;
        }
        t0 = lo + (hi - lo) * band / bands;
        t1 = lo + (hi - lo) * (band + 1) / bands;
        *colour = blend(from, to, (long)band * 2 + 1, (long)bands * 2);
        cur = keep_side(a, n, ux, uy, band ? t0 : lo - 1, b, OPR_GRADIENT_MAX_POINTS);
        if (cur < 3) return cur < 0 ? -1 : 0;
        cur = keep_side(b, cur, -ux, -uy, band + 1 < bands ? -t1 - 16384 : -hi - 1, a, OPR_GRADIENT_MAX_POINTS);
        if (cur < 3) return cur < 0 ? -1 : 0;
        if (cur > cap) return -1;
        memcpy(out, a, (size_t)cur * 2 * sizeof *out);
        return cur;
    }
}
