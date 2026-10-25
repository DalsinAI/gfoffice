/*
 * drawbench: W139's proof program for docs/suite/cairo-rendering.md.
 *
 * What GFOffice's page drawing would cost on a 68040 through cairo 1.18.6 and
 * pixman 0.46.4 (openamigacairo, linked statically, CAIRO_FONTS=user) and
 * through OpenGPU's stream v1.6 vector commands (openamigartg's ogpu_core.c,
 * the code opengpu.library's 68k back end runs, linked here so the batch
 * always runs on the 68k; and opengpu.library itself, as it routes the
 * batch), beside what OpenWrite and OpenCalc do today (point sampling, 1-pixel
 * lines). Everything is drawn into ARGB32 memory; nothing is put on a screen.
 *
 *   drawbench [ROUNDS] [png] [work=MB]   png: each result also as RAM:db-*.png;
 *                                        work: OpenGPU's work memory (default 1 MB)
 *
 * Build: tools/drawbench/build.sh (the os32-gcc16 stove). Results: docs/suite/cairo-rendering.md, section 6.
 * NO_CAIRO builds it without cairo, to weigh what cairo adds to a program.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/timer.h>

#ifndef NO_CAIRO
#include <cairo.h>
#endif
#include "ogpu_core.h"
#include <opengpu/build.h>
#include <opengpu/opengpu.h>
#include <inline/opengpu.h>

struct Library *OpenGPUBase;
struct Device *TimerBase;
static struct timerequest treq;
static ULONG efreq;

static void reset_fpcr(void)
{
    __asm__ volatile("fmove.l %0,%%fpcr" : : "d"(0));
}

static double now_us(void)
{
    struct EClockVal e;
    efreq = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1e6 / (double)efreq;
}

/* ---- targets ---- */

typedef struct {
    ULONG *px;
    int w, h;
    long bpr;
} target;

static void clear(target *t, ULONG argb)
{
    long i, n = (long)t->w * t->h;
    for (i = 0; i < n; i++)
        t->px[i] = argb;
}

/* ---- the OpenGPU core, linked ---- */

static struct ogpu_core core;
static ogpu_u8 *map_id(void *user, ogpu_u32 address, ogpu_u32 length)
{
    (void)user;
    (void)length;
    return (ogpu_u8 *)address;
}

#define BATCH_WORDS 8192
#define PATHS 64
#define PATH_WORDS 1024
static unsigned long WORK_BYTES = 1024UL * 1024UL; /* work=MB */
static ULONG *batch_buf, *path_buf, *grad_buf;
static UBYTE *work;
static struct OGPUBatch B, P[PATHS], G;
static int npaths;
static int use_lib;                     /* 1: OGPU_Submit, as opengpu.library routes it */

static void ob_begin(target *t)
{
    ogpu_batch_init(&B, batch_buf, BATCH_WORDS);
    ogpu_batch_init(&G, grad_buf, 512);
    npaths = 0;
    ogpu_surface(&B, 0, (unsigned long)t->px, (unsigned long)t->bpr, t->w, t->h, OGPU_FMT_ARGB32);
    ogpu_target(&B, 0);
}

static struct OGPUBatch *ob_path(void)
{
    struct OGPUBatch *p = &P[npaths];
    ogpu_batch_init(p, path_buf + (long)npaths * PATH_WORDS, PATH_WORDS);
    npaths++;
    return p;
}

static long ob_run(void)
{
    if (B.overflow)
        return -100;
    if (use_lib) {
        ULONG fence = 0;
        LONG r = OGPU_Submit(B.buf, (ULONG)B.words, &fence);
        if (r == OGPU_OK)
            r = OGPU_Wait(fence);
        return r;
    }
    ogpu_core_init(&core);
    core.map = map_id;
    core.fence = 0;
    core.ext = 0;
    core.user = 0;
    ogpu_core_run(&core, B.buf, B.words);
    return core.last_error;
}

static const long ID6[6] = { 0x10000, 0, 0, 0, 0x10000, 0 };
static long f8(double v) { return (long)floor(v * 256.0 + 0.5); }
static long f16(double v) { return (long)floor(v * 65536.0 + 0.5); }

/* An arc of the circle (cx, cy, r) from a0 to a1 (radians, y down, a1 > a0) as cubics, after the pen is at its
 * start: what cairo_arc does inside. */
static void arc_cubics(struct OGPUBatch *p, double cx, double cy, double r, double a0, double a1)
{
    int n = (int)ceil((a1 - a0) / (M_PI / 2.0)), i;
    double step;
    if (n < 1)
        n = 1;
    step = (a1 - a0) / n;
    for (i = 0; i < n; i++) {
        double a = a0 + i * step, b = a + step, k = 4.0 / 3.0 * tan(step / 4.0);
        double x0 = cos(a), y0 = sin(a), x3 = cos(b), y3 = sin(b);
        ogpu_path_cubic(p, f8(cx + r * (x0 - k * y0)), f8(cy + r * (y0 + k * x0)), f8(cx + r * (x3 + k * y3)),
                        f8(cy + r * (y3 - k * x3)), f8(cx + r * x3), f8(cy + r * y3));
    }
}

/* ---- the scenes' data ---- */

#define W 800
#define H 480
static const ULONG accents[6] = { 0xFF4472C4UL, 0xFFED7D31UL, 0xFFA5A5A5UL, 0xFFFFC000UL, 0xFF5B9BD5UL, 0xFF70AD47UL };
static const double pie_values[6] = { 30, 22, 17, 13, 10, 8 };
#define SERIES 4
#define POINTS 24
static double series_y[SERIES][POINTS];
#define PLOT_X0 470
#define PLOT_X1 780
#define PLOT_Y0 40
#define PLOT_Y1 440
#define GRID 9

static void make_series(void)
{
    unsigned long seed = 12345;
    int s, i;
    for (s = 0; s < SERIES; s++)
        for (i = 0; i < POINTS; i++) {
            seed = seed * 1103515245UL + 12345UL;
            series_y[s][i] = PLOT_Y0 + 20 + (double)((seed >> 8) % 3600) / 10.0;
        }
}

static double series_x(int i)
{
    return PLOT_X0 + 6 + (double)i * (PLOT_X1 - PLOT_X0 - 12) / (POINTS - 1);
}

static double pie_angle(int k)
{
    double sum = 0, total = 0;
    int i;
    for (i = 0; i < 6; i++) {
        total += pie_values[i];
        if (i < k)
            sum += pie_values[i];
    }
    return -M_PI / 2.0 + 2.0 * M_PI * sum / total;
}

/* ---- scene 1: a pie, six slices with white borders ---- */

static long pie_ogpu(target *t)
{
    int k;
    ob_begin(t);
    for (k = 0; k < 6; k++) {
        struct OGPUBatch *p = ob_path();
        double a0 = pie_angle(k), a1 = pie_angle(k + 1);
        ogpu_path_move(p, f8(240), f8(240));
        ogpu_path_line(p, f8(240 + 200 * cos(a0)), f8(240 + 200 * sin(a0)));
        arc_cubics(p, 240, 240, 200, a0, a1);
        ogpu_path_close(p);
        ogpu_path_fill(&B, OGPU_PD_OVER, accents[k], 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                       (unsigned long)work, WORK_BYTES);
        ogpu_path_stroke(&B, OGPU_PD_OVER, 0xFFFFFFFFUL, 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words,
                         ID6, (unsigned long)work, WORK_BYTES, f16(2.0), OGPU_CAP_BUTT | OGPU_JOIN_ROUND, f16(10.0), 0,
                         0, 0);
    }
    return ob_run();
}

/* ---- scene 2: a line chart, gridlines and four series ---- */

static long lines_ogpu(target *t)
{
    int s, i;
    ob_begin(t);
    for (i = 0; i < GRID; i++)
        ogpu_fill(&B, PLOT_X0, PLOT_Y0 + i * (PLOT_Y1 - PLOT_Y0) / (GRID - 1), PLOT_X1 - PLOT_X0, 1, 0xFFD9D9D9UL);
    for (s = 0; s < SERIES; s++) {
        struct OGPUBatch *p = ob_path();
        for (i = 0; i < POINTS; i++)
            (i ? ogpu_path_line : ogpu_path_move)(p, f8(series_x(i)), f8(series_y[s][i]));
        ogpu_path_stroke(&B, OGPU_PD_OVER, accents[s], 0, 0, 0, 0, 0, (unsigned long)p->buf, (unsigned long)p->words, ID6,
                         (unsigned long)work, WORK_BYTES, f16(3.0), OGPU_CAP_ROUND | OGPU_JOIN_ROUND, f16(10.0), 0, 0,
                         0);
    }
    return ob_run();
}

/* today: 1-pixel lines (Bresenham, as graphics.library's Draw) and the gridlines as fills */
static void hline(target *t, int x, int y, int w, ULONG c)
{
    ULONG *q = t->px + (long)y * t->w + x;
    while (w-- > 0)
        *q++ = c;
}

static void line1(target *t, int x0, int y0, int x1, int y1, ULONG c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy, e2;
    for (;;) {
        t->px[(long)y0 * t->w + x0] = c;
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static long lines_today(target *t)
{
    int s, i;
    for (i = 0; i < GRID; i++)
        hline(t, PLOT_X0, PLOT_Y0 + i * (PLOT_Y1 - PLOT_Y0) / (GRID - 1), PLOT_X1 - PLOT_X0, 0xFFD9D9D9UL);
    for (s = 0; s < SERIES; s++)
        for (i = 1; i < POINTS; i++)
            line1(t, (int)series_x(i - 1), (int)series_y[s][i - 1], (int)series_x(i), (int)series_y[s][i], accents[s]);
    return 0;
}

/* ---- scene 3: a slide's background, a three-stop linear gradient over the whole target ---- */

static long gradient_ogpu(target *t)
{
    struct OGPUBatch *p;
    long geo[6];
    ob_begin(t);
    geo[0] = 0; geo[1] = 0; geo[2] = f16(t->w); geo[3] = f16(t->h); geo[4] = 0; geo[5] = 0;
    ogpu_gradient(&G, OGPU_GRAD_LINEAR, OGPU_SPREAD_PAD, 3, ID6, geo);
    ogpu_gradient_stop(&G, 0, 0xFF1F3864UL);
    ogpu_gradient_stop(&G, f16(0.5), 0xFF4472C4UL);
    ogpu_gradient_stop(&G, f16(1.0), 0xFFDEEBF7UL);
    p = ob_path();
    ogpu_path_move(p, 0, 0);
    ogpu_path_line(p, f8(t->w), 0);
    ogpu_path_line(p, f8(t->w), f8(t->h));
    ogpu_path_line(p, 0, f8(t->h));
    ogpu_path_close(p);
    ogpu_path_fill(&B, OGPU_PD_SRC | OGPU_PATH_GRADIENT, (unsigned long)G.buf, 0, 0, 0, 0, 0, (unsigned long)p->buf,
                   (unsigned long)p->words, ID6, (unsigned long)work, WORK_BYTES);
    return ob_run();
}

/* ---- scene 4: a full-page picture, scaled ---- */

#define PIC_W 860
#define PIC_H 1216                      /* about OpenWrite's megapixel cap, an A4 page's shape */
static target pic;

static void make_picture(void)
{
    int x, y;
    for (y = 0; y < PIC_H; y++)
        for (x = 0; x < PIC_W; x++) {
            ULONG r = (ULONG)(x * 255 / PIC_W), g = (ULONG)(y * 255 / PIC_H), b = 160;
            if (((x + y) / 3) % 7 == 0) { r = 255; g = 255; b = 255; }      /* fine diagonal lines */
            if ((x / 4 + y / 4) % 2 && x > PIC_W / 2 && y > PIC_H / 2) { r /= 2; g /= 2; b /= 2; } /* a checker */
            pic.px[(long)y * PIC_W + x] = 0xFF000000UL | (r << 16) | (g << 8) | b;
        }
}

/* today: OpenWrite 2.0.13's draw_picture, each target pixel picked from the cached picture */
static long picture_point(target *t)
{
    static UWORD xs[2048];
    int k, y, bw = t->w, bh = t->h;
    for (k = 0; k < bw; k++)
        xs[k] = (UWORD)((ULONG)k * PIC_W / (ULONG)bw);
    for (y = 0; y < bh; y++) {
        const ULONG *src = pic.px + (ULONG)y * PIC_H / (ULONG)bh * PIC_W;
        ULONG *dst = t->px + (long)y * t->w;
        for (k = 0; k < bw; k++)
            dst[k] = src[xs[k]];
    }
    return 0;
}

static long picture_ogpu(target *t)
{
    struct OGPUBatch *p;
    long m[6];
    ob_begin(t);
    ogpu_surface(&B, 1, (unsigned long)pic.px, (unsigned long)pic.bpr, PIC_W, PIC_H, OGPU_FMT_ARGB32);
    m[0] = f16((double)t->w / PIC_W); m[1] = 0; m[2] = 0;
    m[3] = 0; m[4] = f16((double)t->h / PIC_H); m[5] = 0;
    ogpu_image_paint(&G, 1, OGPU_PATTERN_PAD | OGPU_PATTERN_BILINEAR, 0, 0, PIC_W, PIC_H, m, 255);
    p = ob_path();
    ogpu_path_move(p, 0, 0);
    ogpu_path_line(p, f8(t->w), 0);
    ogpu_path_line(p, f8(t->w), f8(t->h));
    ogpu_path_line(p, 0, f8(t->h));
    ogpu_path_close(p);
    ogpu_path_fill(&B, OGPU_PD_SRC | OGPU_PATH_PATTERN, (unsigned long)G.buf, 0, 0, 0, 0, 0, (unsigned long)p->buf,
                   (unsigned long)p->words, ID6, (unsigned long)work, WORK_BYTES);
    return ob_run();
}

/* the same picture through COMPOSITE's bilinear scale: straight colour, which is the same as premultiplied for an
 * opaque picture (OpenWrite blends a picture's alpha onto the paper once, when it caches it) */
static long picture_composite(target *t)
{
    ob_begin(t);
    ogpu_surface(&B, 1, (unsigned long)pic.px, (unsigned long)pic.bpr, PIC_W, PIC_H, OGPU_FMT_ARGB32);
    ogpu_composite(&B, 1, 0, 0, PIC_W, PIC_H, 0, 0, t->w, t->h, 255, OGPU_COMP_BILINEAR);
    return ob_run();
}

/* ---- cairo ---- */

#ifndef NO_CAIRO
static cairo_surface_t *cs_of(target *t)
{
    return cairo_image_surface_create_for_data((unsigned char *)t->px, CAIRO_FORMAT_ARGB32, t->w, t->h, (int)t->bpr);
}

static long pie_cairo(target *t)
{
    cairo_surface_t *s = cs_of(t);
    cairo_t *cr = cairo_create(s);
    int k;
    long r;
    cairo_set_line_width(cr, 2.0);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (k = 0; k < 6; k++) {
        double a0 = pie_angle(k), a1 = pie_angle(k + 1);
        ULONG c = accents[k];
        cairo_move_to(cr, 240, 240);
        cairo_arc(cr, 240, 240, 200, a0, a1);
        cairo_close_path(cr);
        cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
        cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_stroke(cr);
    }
    cairo_surface_flush(s);
    r = cairo_status(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return r;
}

static long lines_cairo(target *t)
{
    cairo_surface_t *s = cs_of(t);
    cairo_t *cr = cairo_create(s);
    int k, i;
    long r;
    cairo_set_source_rgb(cr, 0xD9 / 255.0, 0xD9 / 255.0, 0xD9 / 255.0);
    for (i = 0; i < GRID; i++)
        cairo_rectangle(cr, PLOT_X0, PLOT_Y0 + i * (PLOT_Y1 - PLOT_Y0) / (GRID - 1), PLOT_X1 - PLOT_X0, 1);
    cairo_fill(cr);
    cairo_set_line_width(cr, 3.0);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (k = 0; k < SERIES; k++) {
        ULONG c = accents[k];
        for (i = 0; i < POINTS; i++)
            (i ? cairo_line_to : cairo_move_to)(cr, series_x(i), series_y[k][i]);
        cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
        cairo_stroke(cr);
    }
    cairo_surface_flush(s);
    r = cairo_status(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return r;
}

static long gradient_cairo(target *t)
{
    cairo_surface_t *s = cs_of(t);
    cairo_t *cr = cairo_create(s);
    cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, t->w, t->h);
    long r;
    cairo_pattern_add_color_stop_rgb(g, 0, 0x1F / 255.0, 0x38 / 255.0, 0x64 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 0.5, 0x44 / 255.0, 0x72 / 255.0, 0xC4 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, 0xDE / 255.0, 0xEB / 255.0, 0xF7 / 255.0);
    cairo_set_source(cr, g);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_surface_flush(s);
    r = cairo_status(cr);
    cairo_pattern_destroy(g);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return r;
}

static cairo_surface_t *pic_surface;
static int pic_filter;

static long picture_cairo(target *t)
{
    cairo_surface_t *s = cs_of(t);
    cairo_t *cr = cairo_create(s);
    long r;
    cairo_scale(cr, (double)t->w / PIC_W, (double)t->h / PIC_H);
    cairo_set_source_surface(cr, pic_surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), (cairo_filter_t)pic_filter);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_surface_flush(s);
    r = cairo_status(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return r;
}
#endif

/* ---- timing and reports ---- */

static int rounds = 5, want_png;
static target chart, page, half;

static int dcmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* Runs fn rounds times on t (cleared to white before each, not timed); prints the fastest and the median. */
static void timeit(const char *scene, const char *how, long (*fn)(target *), target *t)
{
    double us[32], t0;
    long r = 0;
    int i, n = rounds > 32 ? 32 : rounds;
    for (i = 0; i < n; i++) {
        clear(t, 0xFFFFFFFFUL);
        t0 = now_us();
        r = fn(t);
        us[i] = now_us() - t0;
        if (r)
            break;
    }
    if (r) {
        printf("%-9s %-22s error %ld\n", scene, how, r);
        return;
    }
    qsort(us, (size_t)n, sizeof us[0], dcmp);
    printf("%-9s %-22s best %9.0f us  median %9.0f us\n", scene, how, us[0], us[n / 2]);
#ifndef NO_CAIRO
    if (want_png) {
        char name[64];
        cairo_surface_t *s = cs_of(t);
        snprintf(name, sizeof name, "RAM:db-%s-%s.png", scene, how);
        cairo_surface_write_to_png(s, name);
        cairo_surface_destroy(s);
    }
#endif
}

/* how far two results are apart: the largest channel difference, and how many pixels differ by more than 8 */
static ULONG *keep;
static void keep_copy(target *t)
{
    memcpy(keep, t->px, (size_t)t->w * t->h * 4);
}

static void compare(const char *what, target *t)
{
    long i, n = (long)t->w * t->h, over = 0;
    int maxd = 0;
    for (i = 0; i < n; i++) {
        ULONG a = keep[i], b = t->px[i];
        int k, d = 0;
        for (k = 0; k < 32; k += 8) {
            int e = abs((int)((a >> k) & 255) - (int)((b >> k) & 255));
            if (e > d)
                d = e;
        }
        if (d > maxd)
            maxd = d;
        if (d > 8)
            over++;
    }
    printf("%-32s largest difference %3d, pixels over 8: %ld of %ld\n", what, maxd, over, n);
}

static int alloc_target(target *t, int w, int h)
{
    t->w = w;
    t->h = h;
    t->bpr = (long)w * 4;
    t->px = (ULONG *)malloc((size_t)w * h * 4);
    return t->px != NULL;
}

int main(int argc, char **argv)
{
    ULONG mem0, mem1, mem2;
    int i;
    reset_fpcr();
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "png"))
            want_png = 1;
        else if (!strncmp(argv[i], "work=", 5))
            WORK_BYTES = strtoul(argv[i] + 5, NULL, 10) << 20;
        else if (atoi(argv[i]) > 0)
            rounds = atoi(argv[i]);
    }
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) {
        printf("no timer.device\n");
        return 20;
    }
    TimerBase = treq.tr_node.io_Device;
    now_us();
    mem0 = AvailMem(MEMF_ANY);
    batch_buf = (ULONG *)malloc(BATCH_WORDS * 4);
    path_buf = (ULONG *)malloc((size_t)PATHS * PATH_WORDS * 4);
    grad_buf = (ULONG *)malloc(512 * 4);
    work = (UBYTE *)malloc(WORK_BYTES);
    pic.w = PIC_W;
    pic.h = PIC_H;
    pic.bpr = PIC_W * 4;
    pic.px = (ULONG *)malloc((size_t)PIC_W * PIC_H * 4);
    keep = (ULONG *)malloc((size_t)794 * 1123 * 4);
    if (!batch_buf || !path_buf || !grad_buf || !work || !pic.px || !keep || !alloc_target(&chart, W, H) ||
        !alloc_target(&page, 794, 1123) || !alloc_target(&half, 397, 561)) {
        printf("out of memory\n");
        return 20;
    }
    make_series();
    make_picture();
    mem1 = AvailMem(MEMF_ANY);
    printf("drawbench: %d rounds, %lu KB of OpenGPU work memory, E-clock %lu Hz, free memory %lu KB, %lu KB after the buffers\n", rounds, WORK_BYTES >> 10,
           (unsigned long)efreq, (unsigned long)(mem0 >> 10), (unsigned long)(mem1 >> 10));
    OpenGPUBase = OpenLibrary((STRPTR)"opengpu.library", 0);
    if (OpenGPUBase) {
        ULONG q = OGPU_Query(OGPU_OP_PATH_FILL, OGPU_FMT_ARGB32);
        STRPTR be = OGPU_BackEndName((ULONG)OGPU_BACKEND(q));
        printf("opengpu.library %d.%d: PATH_FILL on ARGB32 answers %d by back end %d (%s)\n", OpenGPUBase->lib_Version,
               OpenGPUBase->lib_Revision, OGPU_ANSWER(q), OGPU_BACKEND(q), be ? (char *)be : "?");
        if (OGPU_ANSWER(q) == OGPU_NONE) {
            CloseLibrary(OpenGPUBase);
            OpenGPUBase = NULL;
        }
    } else
        printf("no opengpu.library\n");
    printf("\n");

    use_lib = 0;
    timeit("pie", "ogpu-core-68k", pie_ogpu, &chart);
    keep_copy(&chart);
#ifndef NO_CAIRO
    timeit("pie", "cairo", pie_cairo, &chart);
    compare("pie: cairo against the core", &chart);
#endif
    if (OpenGPUBase) {
        use_lib = 1;
        timeit("pie", "opengpu.library", pie_ogpu, &chart);
        use_lib = 0;
    }

    timeit("lines", "today-1px", lines_today, &chart);
    timeit("lines", "ogpu-core-68k", lines_ogpu, &chart);
    keep_copy(&chart);
#ifndef NO_CAIRO
    timeit("lines", "cairo", lines_cairo, &chart);
    compare("lines: cairo against the core", &chart);
#endif
    if (OpenGPUBase) {
        use_lib = 1;
        timeit("lines", "opengpu.library", lines_ogpu, &chart);
        use_lib = 0;
    }

    timeit("gradient", "ogpu-core-68k", gradient_ogpu, &chart);
    keep_copy(&chart);
#ifndef NO_CAIRO
    timeit("gradient", "cairo", gradient_cairo, &chart);
    compare("gradient: cairo against the core", &chart);
#endif
    if (OpenGPUBase) {
        use_lib = 1;
        timeit("gradient", "opengpu.library", gradient_ogpu, &chart);
        use_lib = 0;
    }

#ifndef NO_CAIRO
    pic_surface = cairo_image_surface_create_for_data((unsigned char *)pic.px, CAIRO_FORMAT_ARGB32, PIC_W, PIC_H,
                                                      PIC_W * 4);
#endif
    {
        target *ts[2];
        const char *names[2] = { "page100", "page50" };
        int j;
        ts[0] = &page;
        ts[1] = &half;
        for (j = 0; j < 2; j++) {
            timeit(names[j], "today-point", picture_point, ts[j]);
            timeit(names[j], "ogpu-core-68k-composite", picture_composite, ts[j]);
            timeit(names[j], "ogpu-core-68k-bilinear", picture_ogpu, ts[j]);
            keep_copy(ts[j]);
#ifndef NO_CAIRO
            pic_filter = CAIRO_FILTER_BILINEAR;
            timeit(names[j], "cairo-bilinear", picture_cairo, ts[j]);
            compare("picture: cairo bilinear against the core", ts[j]);
            pic_filter = CAIRO_FILTER_GOOD;
            timeit(names[j], "cairo-good", picture_cairo, ts[j]);
#endif
            if (OpenGPUBase) {
                use_lib = 1;
                timeit(names[j], "opengpu.library", picture_ogpu, ts[j]);
                timeit(names[j], "opengpu.library-composite", picture_composite, ts[j]);
                use_lib = 0;
            }
        }
    }
    mem2 = AvailMem(MEMF_ANY);
    printf("\nfree memory after the runs: %lu KB (%ld KB less than after the buffers)\n", (unsigned long)(mem2 >> 10),
           ((long)mem1 - (long)mem2) >> 10);
#ifndef NO_CAIRO
    cairo_surface_destroy(pic_surface);
    printf("cairo %s\n", cairo_version_string());
#endif
    if (OpenGPUBase)
        CloseLibrary(OpenGPUBase);
    CloseDevice((struct IORequest *)&treq);
    return 0;
}
