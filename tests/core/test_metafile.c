/*
 * libowf's WMF and EMF player (owf_metafile.c) and the page PDF writer playing them (owf_pdf.c). Every metafile
 * here is made by this file from [MS-WMF]'s and [MS-EMF]'s record layouts, so no picture from any program goes into the
 * repository. Into pixels too (owf_mfraster.c). make metafile-test, make metafile-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "owf_internal.h"
#include "owf_metafile.h"
#include "owf_pdf.h"

static int checks, failures;

#define CHECK(cond, what)                                                                                  \
    do {                                                                                                    \
        ++checks;                                                                                           \
        if (!(cond)) {                                                                                      \
            ++failures;                                                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                                            \
        }                                                                                                   \
    } while (0)

/* ---- a metafile, built ---- */

typedef struct {
    unsigned char d[4096];
    size_t n;
    size_t rec;              /* where the record being built starts */
} wmf;

static void put16(wmf *w, int v) { w->d[w->n++] = (unsigned char)(v & 255); w->d[w->n++] = (unsigned char)((v >> 8) & 255); }
static void put32(wmf *w, long v) { put16(w, (int)(v & 0xFFFF)); put16(w, (int)((v >> 16) & 0xFFFF)); }
static void put8(wmf *w, int v) { w->d[w->n++] = (unsigned char)v; }

static void begin(wmf *w, int fn) { w->rec = w->n; put32(w, 0); put16(w, fn); }
static void end(wmf *w)
{
    long words;
    if (w->n & 1) put8(w, 0);
    words = (long)((w->n - w->rec) / 2);
    w->d[w->rec] = (unsigned char)(words & 255);
    w->d[w->rec + 1] = (unsigned char)((words >> 8) & 255);
    w->d[w->rec + 2] = (unsigned char)((words >> 16) & 255);
    w->d[w->rec + 3] = (unsigned char)((words >> 24) & 255);
}

/* the placeable header ([MS-WMF] 2.3.2.3) when placeable, then META_HEADER with nobjects */
static void start(wmf *w, int placeable, int l, int t, int r, int b, int nobjects)
{
    memset(w, 0, sizeof *w);
    if (placeable) {
        put32(w, 0x9AC6CDD7L); put16(w, 0); put16(w, l); put16(w, t); put16(w, r); put16(w, b);
        put16(w, 1440); put32(w, 0); put16(w, 0);
    }
    put16(w, 1); put16(w, 9); put16(w, 0x0300); put32(w, 0); put16(w, nobjects); put32(w, 0); put16(w, 0);
}

static void finish(wmf *w) { begin(w, 0x0000); end(w); }

static void colour(wmf *w, int r, int g, int b) { put8(w, r); put8(w, g); put8(w, b); put8(w, 0); }

static void pen(wmf *w, int style, int width, int r, int g, int b)
{
    begin(w, 0x02FA); put16(w, style); put16(w, width); put16(w, 0); colour(w, r, g, b); end(w);
}

static void brush(wmf *w, int style, int r, int g, int b)
{
    begin(w, 0x02FC); put16(w, style); colour(w, r, g, b); put16(w, 0); end(w);
}

static void select_object(wmf *w, int i) { begin(w, 0x012D); put16(w, i); end(w); }
static void delete_object(wmf *w, int i) { begin(w, 0x01F0); put16(w, i); end(w); }

/* Rectangle and Ellipse: Bottom, Right, Top, Left */
static void box_record(wmf *w, int fn, int l, int t, int r, int b)
{
    begin(w, fn); put16(w, b); put16(w, r); put16(w, t); put16(w, l); end(w);
}

static void font(wmf *w, int height, int weight, int italic, const char *face)
{
    size_t i;
    begin(w, 0x02FB);
    put16(w, height); put16(w, 0); put16(w, 0); put16(w, 0); put16(w, weight);
    put8(w, italic); put8(w, 0); put8(w, 0); put8(w, 0); put8(w, 0); put8(w, 0); put8(w, 0); put8(w, 0);
    for (i = 0; i < 32; i++) put8(w, i < strlen(face) ? face[i] : 0);
    end(w);
}

static void text_out(wmf *w, int x, int y, const char *s)
{
    size_t i, n = strlen(s);
    begin(w, 0x0521); put16(w, (int)n);
    for (i = 0; i < n; i++) put8(w, (unsigned char)s[i]);
    if (n & 1) put8(w, 0);
    put16(w, y); put16(w, x); end(w);
}

/* ---- a sink that writes down what it is given ---- */

typedef struct {
    char log[16384];
    char last[2048];         /* the last path call, whole, however full log is */
    size_t used;
    int stop_after;          /* > 0: the sink says stop at that call */
    int calls;
} recorder;

static void say(recorder *r, const char *s)
{
    size_t n = strlen(s);
    if (r->used + n + 1 < sizeof r->log) { memcpy(r->log + r->used, s, n); r->used += n; r->log[r->used] = 0; }
}

static int rec_path(void *ctx, const int *xy, int n, int closed, unsigned long fill, unsigned long line, int lw,
                    int dash)
{
    recorder *r = (recorder *)ctx;
    char buf[64];
    int i;
    char out[2048];
    size_t k = 0;
    if (r->stop_after && ++r->calls >= r->stop_after) return -1;
    out[k++] = closed ? 'P' : 'L';
    out[k] = 0;
    for (i = 0; i < n && k < sizeof out - 64; i++) {
        snprintf(buf, sizeof buf, " %d,%d", xy[i * 2], xy[i * 2 + 1]);
        strcpy(out + k, buf);
        k += strlen(buf);
    }
    snprintf(buf, sizeof buf, " f%06lx l%06lx w%d%s;", OWF_SHADE_SET(fill) ? OWF_SHADE_RGB(fill) : 0xEEEEEEUL,
             OWF_SHADE_SET(line) ? OWF_SHADE_RGB(line) : 0xEEEEEEUL, lw, dash ? " dash" : "");
    strcpy(out + k, buf);
    say(r, out);
    strcpy(r->last, out);
    return 0;
}

static int rec_ellipse(void *ctx, int x, int y, int w, int h, unsigned long fill, unsigned long line, int lw,
                       int dash)
{
    recorder *r = (recorder *)ctx;
    char buf[96];
    if (r->stop_after && ++r->calls >= r->stop_after) return -1;
    snprintf(buf, sizeof buf, "E %d,%d %dx%d f%06lx l%06lx w%d%s;", x, y, w, h,
             OWF_SHADE_SET(fill) ? OWF_SHADE_RGB(fill) : 0xEEEEEEUL, OWF_SHADE_SET(line) ? OWF_SHADE_RGB(line) : 0xEEEEEEUL,
             lw, dash ? " dash" : "");
    say(r, buf);
    return 0;
}

static int rec_text(void *ctx, int x, int baseline, const char *utf8, size_t length, owf_font_kind kind,
                    unsigned flags, int size, unsigned long colour)
{
    recorder *r = (recorder *)ctx;
    char buf[160];
    if (r->stop_after && ++r->calls >= r->stop_after) return -1;
    snprintf(buf, sizeof buf, "T %d,%d \"%.*s\" k%d f%u s%d c%06lx;", x, baseline, (int)length, utf8, (int)kind, flags,
             size, OWF_SHADE_RGB(colour));
    say(r, buf);
    return 0;
}

static int rec_width(void *ctx, const char *utf8, size_t length, owf_font_kind kind, unsigned flags, int size)
{
    (void)ctx; (void)utf8; (void)kind; (void)flags; (void)size;
    return (int)length * 100;
}

static const owf_mf_sink sink = { rec_path, rec_ellipse, rec_text, rec_width, NULL };

static int rec_clip(void *ctx, int x, int y, int w, int h)
{
    recorder *r = (recorder *)ctx;
    char buf[64];
    snprintf(buf, sizeof buf, "C %d,%d %d,%d;", x, y, w, h);
    say(r, buf);
    return 0;
}

/* the same, told the clip */
static const owf_mf_sink csink = { rec_path, rec_ellipse, rec_text, rec_width, rec_clip };

/* The picture most of the checks play: placeable, bounds 0,0 to 1000,500, drawn into 2000 by 1000 (twice) */
static void sample(wmf *w)
{
    start(w, 1, 0, 0, 1000, 500, 4);
    pen(w, 0, 10, 0xFF, 0, 0);                  /* object 0: a red pen, 10 wide */
    brush(w, 0, 0, 0, 0xFF);                    /* object 1: a blue brush */
    select_object(w, 0);
    select_object(w, 1);
    box_record(w, 0x041B, 100, 50, 300, 250);   /* Rectangle */
    box_record(w, 0x0418, 400, 50, 500, 150);   /* Ellipse */
    begin(w, 0x0324); put16(w, 3);              /* Polygon: a triangle */
    put16(w, 0); put16(w, 0); put16(w, 10); put16(w, 0); put16(w, 0); put16(w, 10); end(w);
    begin(w, 0x0214); put16(w, 400); put16(w, 600); end(w);   /* MoveTo (Y, X) */
    begin(w, 0x0213); put16(w, 450); put16(w, 700); end(w);   /* LineTo */
    font(w, -100, 700, 1, "Times New Roman");   /* object 2 */
    select_object(w, 2);
    begin(w, 0x0209); colour(w, 0, 0x80, 0); end(w);          /* SetTextColor */
    text_out(w, 50, 300, "Hi\x80");             /* Windows-1252's euro */
    begin(w, 0x0F43); put32(w, 0); put16(w, 0); put16(w, 0); end(w);  /* StretchDIB: played as nothing */
    delete_object(w, 0);
    pen(w, 5, 0, 0, 0, 0);                      /* PS_NULL: in place 0 again, the lowest free */
    select_object(w, 0);
    box_record(w, 0x041B, 0, 400, 100, 500);    /* filled, no line */
    begin(w, 0x001E); end(w);                   /* SaveDC */
    begin(w, 0x020B); put16(w, -500); put16(w, -1000); end(w); /* SetWindowOrg: everything moves */
    begin(w, 0x0127); put16(w, -1); end(w);     /* RestoreDC: back */
    brush(w, 1, 0, 0, 0);                       /* BS_NULL: nothing filled, and no line: nothing drawn */
    select_object(w, 3);
    box_record(w, 0x041B, 0, 0, 10, 10);
    finish(w);
}

static void test_play(void)
{
    wmf w;
    recorder r;
    owf_mf_result res;
    sample(&w);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_is_wmf(w.d, w.n), "is a WMF");
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 2000, 1000, &sink, &r, &res) == OWF_OK, "played");
    CHECK(strstr(r.log, "P 200,100 600,100 600,500 200,500 f0000ff lff0000 w20;") != NULL,
          "the rectangle: twice its size, blue inside a red line 20 wide");
    CHECK(strstr(r.log, "E 800,100 200x200 f0000ff lff0000 w20;") != NULL, "the ellipse in its box");
    CHECK(strstr(r.log, "P 0,0 20,0 0,20 f0000ff lff0000 w20;") != NULL, "the polygon, closed and filled");
    CHECK(strstr(r.log, "L 1200,800 1400,900 feeeeee lff0000 w20;") != NULL, "MoveTo then LineTo: a line");
    /* the font is 100 high, 200 in the box; its top at y 300 (600), so the baseline 4/5 of it lower */
    CHECK(strstr(r.log, "T 100,760 \"Hi\xE2\x82\xAC\" k1 f3 s200 c008000;") != NULL,
          "the text: Times as serif, bold italic, green, the euro as UTF-8, on its baseline");
    CHECK(strstr(r.log, "P 0,800 200,800 200,1000 0,1000 f0000ff leeeeee w0;") != NULL,
          "a null pen in the deleted pen's place: filled, no line");
    CHECK(strstr(r.log, "P 0,0 20,0 20,20 0,20") == NULL, "a null pen and a null brush draw nothing");
    CHECK(res.drawn == 6, "six things drawn");
    CHECK(res.skipped == 1, "the StretchDIB counted as left out");
}

static void test_window(void)
{
    wmf w;
    recorder r;
    /* no placeable header: the window says how big it is (origin 100,100, extent 200 by -200: y goes up) */
    start(&w, 0, 0, 0, 0, 0, 0);
    begin(&w, 0x020B); put16(&w, 100); put16(&w, 100); end(&w);
    begin(&w, 0x020C); put16(&w, -200); put16(&w, 200); end(&w);
    box_record(&w, 0x041B, 100, 100, 300, -100);
    finish(&w);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 1000, 1000, 400, 400, &sink, &r, NULL) == OWF_OK, "a window's metafile");
    CHECK(strstr(r.log, "P 1000,1000 1400,1000 1400,1400 1000,1400 fffffff l000000 w0;") != NULL,
          "the window fills the box, the defaults: a white brush and a black pen");
    /* nothing saying how big: not played */
    start(&w, 0, 0, 0, 0, 0, 0);
    box_record(&w, 0x041B, 0, 0, 10, 10);
    finish(&w);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 100, 100, &sink, &r, NULL) == OWF_ERR_FORMAT && !r.used,
          "no window and no bounds: nothing drawn");
}

/* a point far outside a tiny window maps past an int: it is held at 0x3FFFFFFF, not wrapped */
static void test_far(void)
{
    wmf w;
    recorder r;
    start(&w, 0, 0, 0, 0, 0, 0);
    begin(&w, 0x020B); put16(&w, 0); put16(&w, 0); end(&w);
    begin(&w, 0x020C); put16(&w, 1); put16(&w, 1); end(&w);
    box_record(&w, 0x041B, -32768, -32768, 32767, 32767);
    finish(&w);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 1000000, 1000000, &sink, &r, NULL) == OWF_OK, "a far box played");
    CHECK(strstr(r.log, "P -1073741823,-1073741823 1073741823,-1073741823 1073741823,1073741823") != NULL,
          "its corners held at plus and minus 0x3FFFFFFF");
}

static void test_damaged(void)
{
    static const unsigned char png[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    wmf w;
    recorder r;
    owf_mf_result res;
    size_t cut;
    int ok = 1;
    CHECK(!owf_metafile_is_wmf(png, sizeof png) && !owf_metafile_is_wmf(NULL, 0), "a PNG is not a WMF");
    CHECK(owf_metafile_play(png, sizeof png, 0, 0, 10, 10, &sink, &r, NULL) == OWF_ERR_FORMAT, "nor played");
    sample(&w);
    /* every length the file could be cut to plays without reading past it (ASan) and draws no more */
    for (cut = 0; cut <= w.n; cut++) {
        int rc;
        memset(&r, 0, sizeof r);
        rc = owf_metafile_play(w.d, cut, 0, 0, 2000, 1000, &sink, &r, &res);
        if (rc != OWF_OK && rc != OWF_ERR_FORMAT) ok = 0;
        if (rc == OWF_OK && res.drawn > 6) ok = 0;
    }
    CHECK(ok, "cut anywhere: played as far as it goes");
    /* a record saying it is longer than the file: the drawing before it stays */
    sample(&w);
    w.d[w.n - 6] = 0xFF; w.d[w.n - 5] = 0xFF;   /* META_EOF's size */
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 2000, 1000, &sink, &r, &res) == OWF_OK && res.drawn == 6,
          "a record too long ends the playing");
    /* the sink saying stop */
    memset(&r, 0, sizeof r);
    r.stop_after = 2;
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 2000, 1000, &sink, &r, &res) == OWF_ERR_MEMORY && res.drawn == 1,
          "the sink stops it");
}

/* ---- into pixels, for the screen (owf_mfraster.c) ---- */

static unsigned long pixel(const unsigned char *argb, int w, int x, int y)
{
    const unsigned char *p = argb + ((size_t)y * w + x) * 4;
    return ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

/* a wedge whose points lie far outside a 1 x 1 window (about 33 million sixteenths of a pixel either side): its
 * crossings need more than 32 bits on the way (a long is 32 bits on m68k). Its long edge crosses the picture's
 * middle almost level, so the bottom half is filled and the top half is not. */
static void test_render_far(void)
{
    wmf w;
    unsigned char *argb = (unsigned char *)malloc(64 * 64 * 4);
    int x, y, top = 0, bottom = 0;
    if (!argb) { CHECK(0, "memory"); return; }
    start(&w, 0, 0, 0, 0, 0, 2);
    begin(&w, 0x020B); put16(&w, 0); put16(&w, 0); end(&w);
    begin(&w, 0x020C); put16(&w, 1); put16(&w, 1); end(&w);
    pen(&w, 5, 0, 0, 0, 0);
    select_object(&w, 0);
    brush(&w, 0, 0, 0, 255);
    select_object(&w, 1);
    begin(&w, 0x0324); put16(&w, 3);
    put16(&w, -32768); put16(&w, 0); put16(&w, 32767); put16(&w, 1); put16(&w, -32768); put16(&w, 1);
    end(&w);
    finish(&w);
    memset(argb, 255, 64 * 64 * 4);
    CHECK(owf_metafile_render(w.d, w.n, argb, 64, 64, NULL) == OWF_OK, "the far wedge drawn");
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++)
            if (pixel(argb, 64, x, y) == 0x0000FF) { if (y < 32) top++; else bottom++; }
    CHECK(top == 0 && bottom == 32 * 64, "the far wedge: the bottom half filled, the top half not");
    free(argb);
}

static void test_render(void)
{
    wmf w;
    unsigned char *argb = (unsigned char *)malloc(100 * 50 * 4);
    owf_mf_result res;
    int tw = 0, th = 0, cut, ok = 1;
    if (!argb) { CHECK(0, "memory"); return; }
    sample(&w);
    CHECK(owf_metafile_size(w.d, w.n, &tw, &th) && tw == 1000 && th == 500, "its size: 1000 by 500 at 1440 an inch");
    memset(argb, 255, 100 * 50 * 4);
    CHECK(owf_metafile_render(w.d, w.n, argb, 100, 50, &res) == OWF_OK, "drawn into pixels");
    /* the bounds 1000 by 500 into 100 by 50: a tenth */
    CHECK(pixel(argb, 100, 20, 15) == 0x0000FF, "inside the rectangle: blue");
    /* the pen is a pixel wide, centred on the edge at x 10.0: the pixel left of it, whose centre is 9.5 */
    CHECK(pixel(argb, 100, 9, 15) == 0xFF0000 && pixel(argb, 100, 10, 15) == 0x0000FF,
          "the rectangle's left edge: the red pen, then the fill");
    CHECK(pixel(argb, 100, 20, 4) == 0xFF0000, "its top edge too");
    CHECK(pixel(argb, 100, 45, 10) == 0x0000FF, "the ellipse's middle");
    CHECK(pixel(argb, 100, 40, 5) == 0xFFFFFF, "outside the ellipse, inside its box: untouched");
    CHECK(pixel(argb, 100, 5, 45) == 0x0000FF, "the null pen's rectangle: filled");
    CHECK(pixel(argb, 100, 90, 45) == 0xFFFFFF, "nothing drawn there");
    CHECK(pixel(argb, 100, 65, 42) == 0xFF0000, "the line from 600,400 to 700,450");
    CHECK(res.drawn == 5 && res.skipped == 2, "five shapes; the text and the StretchDIB left out");
    CHECK(owf_metafile_render(w.d, w.n, argb, 0, 50, NULL) == OWF_ERR_FORMAT, "no pixels: not drawn");
    for (cut = 0; cut <= (int)w.n; cut++) {     /* every cut drawn inside the pixels (ASan) */
        int rc = owf_metafile_render(w.d, (size_t)cut, argb, 7, 3, NULL);
        if (rc != OWF_OK && rc != OWF_ERR_FORMAT) ok = 0;
    }
    CHECK(ok, "cut anywhere, tiny: no harm");
    start(&w, 0, 0, 0, 0, 0, 0);
    finish(&w);
    CHECK(!owf_metafile_size(w.d, w.n, &tw, &th) && !tw && !th, "no placeable header: no size");
    free(argb);
}

/* ---- through the page PDF writer ---- */

static int count(const unsigned char *d, size_t n, const char *s)
{
    size_t m = strlen(s), i;
    int c = 0;
    for (i = 0; m && i + m <= n; i++)
        if (!memcmp(d + i, s, m)) c++;
    return c;
}

static void test_pdf(void)
{
    wmf w;
    owf_image im;
    owf_pdf *pdf = owf_pdf_new();
    owf_report *rp = owf_report_new();
    unsigned char *out = NULL;
    size_t n = 0;
    int i, lost = 0, approx = 0;
    sample(&w);
    memset(&im, 0, sizeof im);
    im.mime = (char *)"image/x-wmf";
    im.data = w.d;
    im.length = w.n;
    CHECK(pdf && rp && owf_pdf_page(pdf, 12240, 15840) == 0, "a page");
    CHECK(owf_pdf_image(pdf, &im, 1440, 1440, 2000, 1000, NULL) == 0, "the WMF placed");
    CHECK(owf_pdf_finish(pdf, "wmf", &out, &n, rp) == OWF_OK && out, "the PDF");
    if (out) {
        CHECK(count(out, n, " re W n") == 1, "clipped to its box");
        CHECK(count(out, n, "/XObject") == 0, "no picture object: drawn as paths");
        CHECK(count(out, n, "(Hi\\200) Tj") == 1, "its text, the euro as WinAnsi's 0x80");
        CHECK(count(out, n, " l ") >= 4, "its lines");
        CHECK(count(out, n, " c ") >= 4 || count(out, n, " c\n") >= 4, "the ellipse's curves");
    }
    for (i = 0; i < owf_report_count(rp); ++i) {
        owf_note_level level;
        const char *note = owf_report_note(rp, i, &level, NULL);
        if (note && strstr(note, "does not draw yet")) lost = 1;
        if (note && strstr(note, "Parts of WMF and EMF pictures")) approx = 1;
    }
    CHECK(!lost, "not reported as a picture left out");
    CHECK(approx, "the StretchDIB inside reported");
    free(out);
    owf_report_free(rp);
    owf_pdf_free(pdf);
}

/* ---- an EMF, built ([MS-EMF] 2.3): records of Type, Size (bytes), fields ---- */

typedef struct {
    unsigned char d[131072];
    size_t n;
    size_t rec;
    int records;
} emf;

static void e32(emf *e, long v)
{
    unsigned long u = (unsigned long)v;
    e->d[e->n++] = (unsigned char)(u & 255); e->d[e->n++] = (unsigned char)((u >> 8) & 255);
    e->d[e->n++] = (unsigned char)((u >> 16) & 255); e->d[e->n++] = (unsigned char)((u >> 24) & 255);
}
static void e16(emf *e, int v) { e->d[e->n++] = (unsigned char)(v & 255); e->d[e->n++] = (unsigned char)((v >> 8) & 255); }
static void ebegin(emf *e, long type) { e->rec = e->n; e32(e, type); e32(e, 0); }
static void eend(emf *e)
{
    size_t size;
    while (e->n & 3) e->d[e->n++] = 0;
    size = e->n - e->rec;
    e->d[e->rec + 4] = (unsigned char)(size & 255);
    e->d[e->rec + 5] = (unsigned char)((size >> 8) & 255);
    e->d[e->rec + 6] = 0;
    e->d[e->rec + 7] = 0;
    e->records++;
}
static void ecolour(emf *e, int r, int g, int b) { e->d[e->n++] = (unsigned char)r; e->d[e->n++] = (unsigned char)g; e->d[e->n++] = (unsigned char)b; e->d[e->n++] = 0; }

/* the header: Frame 0,0 to fw,fh (0.01 mm) on a device of 1000 by 500 pixels and 100 by 50 mm (10 a mm), so the
 * frame is fw/10 by fh/10 pixels; handles objects' places (0 the file's own) */
static void estart(emf *e, long fw, long fh, int handles)
{
    memset(e, 0, sizeof *e);
    ebegin(e, 1);
    e32(e, 0); e32(e, 0); e32(e, fw / 10 - 1); e32(e, fh / 10 - 1);   /* Bounds, inclusive */
    e32(e, 0); e32(e, 0); e32(e, fw); e32(e, fh);                     /* Frame */
    e32(e, 0x464D4520L); e32(e, 0x10000); e32(e, 0); e32(e, 0);       /* " EMF", Version, Bytes, Records */
    e16(e, handles); e16(e, 0); e32(e, 0); e32(e, 0); e32(e, 0);      /* Handles, Reserved, nDescription, off, nPal */
    e32(e, 1000); e32(e, 500); e32(e, 100); e32(e, 50);               /* Device, Millimeters */
    eend(e);
}
static void efinish(emf *e)
{
    ebegin(e, 14); e32(e, 0); e32(e, 16); e32(e, 20); eend(e);        /* EOF: nPalEntries, offPalEntries, SizeLast */
    e->d[48] = (unsigned char)(e->n & 255); e->d[49] = (unsigned char)((e->n >> 8) & 255);
    e->d[52] = (unsigned char)e->records;
}
static void erect(emf *e, long type, long l, long t, long r, long b)
{
    ebegin(e, type); e32(e, l); e32(e, t); e32(e, r); e32(e, b); eend(e);
}
static void epoint(emf *e, long type, long x, long y) { ebegin(e, type); e32(e, x); e32(e, y); eend(e); }
static void eone(emf *e, long type, long v) { ebegin(e, type); e32(e, v); eend(e); }
static void enone(emf *e, long type) { ebegin(e, type); eend(e); }
static void epen(emf *e, int index, int style, int width, int r, int g, int b)
{
    ebegin(e, 38); e32(e, index); e32(e, style); e32(e, width); e32(e, 0); ecolour(e, r, g, b); eend(e);
}
static void ebrush(emf *e, int index, int style, int r, int g, int b)
{
    ebegin(e, 39); e32(e, index); e32(e, style); ecolour(e, r, g, b); e32(e, 0); eend(e);
}
/* PolyXxx16 (type): Bounds, Count, the points as int16 pairs */
static void epoly16(emf *e, long type, const short *xy, int n)
{
    int i;
    ebegin(e, type); e32(e, 0); e32(e, 0); e32(e, 0); e32(e, 0); e32(e, n);
    for (i = 0; i < n * 2; i++) e16(e, xy[i]);
    eend(e);
}
static void efont(emf *e, int index, long height, long weight, int italic, const char *face)
{
    int i;
    ebegin(e, 82); e32(e, index);
    e32(e, height); e32(e, 0); e32(e, 0); e32(e, 0); e32(e, weight);
    e->d[e->n++] = (unsigned char)italic; e->d[e->n++] = 0; e->d[e->n++] = 0; e->d[e->n++] = 0;
    e->d[e->n++] = 0; e->d[e->n++] = 0; e->d[e->n++] = 0; e->d[e->n++] = 0;
    for (i = 0; i < 32; i++) e16(e, i < (int)strlen(face) ? face[i] : 0);
    eend(e);
}
/* ExtTextOutW: Bounds, iGraphicsMode, exScale, eyScale, Reference, Chars, offString, Options, Rectangle, offDx */
static void etext(emf *e, long x, long y, const unsigned short *s, int n)
{
    int i;
    ebegin(e, 84);
    e32(e, 0); e32(e, 0); e32(e, 0); e32(e, 0); e32(e, 1); e32(e, 0); e32(e, 0);
    e32(e, x); e32(e, y); e32(e, n); e32(e, 76); e32(e, 0);
    e32(e, 0); e32(e, 0); e32(e, 0); e32(e, 0); e32(e, 0);
    for (i = 0; i < n; i++) e16(e, s[i]);
    eend(e);
}

/* the EMF most of its checks play: a 1000 by 500 pixel frame, drawn into 2000 by 1000 */
static void emf_sample(emf *e)
{
    static const short tri[6] = { 0, 0, 10, 0, 0, 10 };
    static const short curve[8] = { 0, 500, 0, 400, 100, 400, 100, 500 };
    static const unsigned short hi[3] = { 'H', 'i', 0x20AC };
    estart(e, 10000, 5000, 5);
    epen(e, 1, 0, 10, 0xFF, 0, 0);                /* a red pen, 10 wide */
    ebrush(e, 2, 0, 0, 0, 0xFF);                  /* a blue brush */
    eone(e, 37, 1);                               /* SelectObject */
    eone(e, 37, 2);
    erect(e, 43, 100, 50, 300, 250);              /* Rectangle */
    erect(e, 42, 400, 50, 500, 150);              /* Ellipse */
    epoly16(e, 86, tri, 3);                       /* Polygon16 */
    epoint(e, 27, 600, 400);                      /* MoveToEx */
    epoint(e, 54, 700, 450);                      /* LineTo */
    efont(e, 3, -100, 700, 1, "Times New Roman");
    eone(e, 37, 3);
    eone(e, 24, 0x008000L);                       /* SetTextColor: green (ColorRef 00 80 00) */
    etext(e, 50, 300, hi, 3);
    ebegin(e, 81); e32(e, 0); eend(e);            /* StretchDIBits: played as nothing */
    ebegin(e, 70); e32(e, 4); e32(e, 0x2B464D45L); eend(e);   /* a comment (EMF+): passed over */
    eone(e, 37, (long)0x80000008UL);              /* the stock NULL_PEN */
    erect(e, 43, 0, 400, 100, 500);               /* filled, no line */
    enone(e, 59);                                 /* BeginPath */
    epoint(e, 27, 800, 0);
    epoint(e, 54, 900, 0);
    epoint(e, 54, 900, 100);
    enone(e, 61);                                 /* CloseFigure */
    enone(e, 60);                                 /* EndPath */
    ebegin(e, 62); e32(e, 0); e32(e, 0); e32(e, 0); e32(e, 0); eend(e);   /* FillPath */
    eone(e, 37, (long)0x80000007UL);              /* the stock BLACK_PEN */
    epoly16(e, 85, curve, 4);                     /* PolyBezier16: one curve, as lines */
    eone(e, 37, (long)0x80000005UL);              /* NULL_BRUSH, and */
    eone(e, 37, (long)0x80000008UL);              /* NULL_PEN: nothing drawn */
    erect(e, 43, 0, 0, 10, 10);
    efinish(e);
}

static void test_emf_play(void)
{
    emf e;
    recorder r;
    owf_mf_result res;
    emf_sample(&e);
    CHECK(owf_metafile_is_emf(e.d, e.n) && !owf_metafile_is_wmf(e.d, e.n), "an EMF, not a WMF");
    CHECK(owf_metafile_can_play(e.d, e.n), "played");
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(e.d, e.n, 0, 0, 2000, 1000, &sink, &r, &res) == OWF_OK, "an EMF plays");
    CHECK(strstr(r.log, "P 200,100 600,100 600,500 200,500 f0000ff lff0000 w20;") != NULL,
          "Rectangle: twice the size, blue under a red pen 20 wide");
    CHECK(strstr(r.log, "E 800,100 200x200 f0000ff lff0000 w20;") != NULL, "Ellipse");
    CHECK(strstr(r.log, "P 0,0 20,0 0,20 f0000ff lff0000 w20;") != NULL, "Polygon16");
    CHECK(strstr(r.log, "L 1200,800 1400,900 feeeeee lff0000 w20;") != NULL, "MoveToEx and LineTo");
    CHECK(strstr(r.log, "\"Hi\xE2\x82\xAC\"") != NULL, "ExtTextOutW's UTF-16, the euro as UTF-8");
    CHECK(strstr(r.log, "k1 f3 s200 c008000;") != NULL, "Times (serif), bold italic, 100 tall: 200, green");
    CHECK(strstr(r.log, "P 0,800 200,800 200,1000 0,1000 f0000ff leeeeee w0;") != NULL, "a null pen: fill only");
    CHECK(strstr(r.log, "P 1600,0 1800,0 1800,200 f0000ff leeeeee w0;") != NULL,
          "a path, filled (FillPath draws no line)");
    CHECK(strstr(r.log, "L 0,1000 ") != NULL && strstr(r.log, " 200,1000 feeeeee l000000 w0;") != NULL,
          "PolyBezier16 as lines, from its first point to its last");
    CHECK(strstr(r.log, "P 0,0 20,0 20,20 0,20") == NULL, "the null pen and brush: nothing");
    CHECK(res.skipped == 1 && res.drawn == 8, "eight drawn; the StretchDIBits left out");
}

/* MM_ANISOTROPIC with a window and a viewport; MM_HIMETRIC (y up, through the reference device) */
static void test_emf_mapping(void)
{
    emf e;
    recorder r;
    estart(&e, 10000, 5000, 1);
    eone(&e, 17, 8);                                  /* SetMapMode MM_ANISOTROPIC */
    epoint(&e, 9, 100, 100);                          /* SetWindowExtEx */
    epoint(&e, 11, 1000, 500);                        /* SetViewportExtEx: 10 across, 5 down a unit */
    epoint(&e, 10, 10, 10);                           /* SetWindowOrgEx: 10,10 at the corner */
    erect(&e, 43, 10, 10, 20, 20);
    enone(&e, 33);                                    /* SaveDC */
    eone(&e, 17, 3);                                  /* MM_HIMETRIC: 0.01 mm, 10 pixels a mm, y up */
    erect(&e, 43, 0, -1000, 1000, -2000);
    eone(&e, 34, -1);                                 /* RestoreDC: back to the window and viewport */
    erect(&e, 43, 20, 20, 30, 30);
    efinish(&e);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(e.d, e.n, 0, 0, 1000, 500, &sink, &r, NULL) == OWF_OK, "mapped EMF plays");
    CHECK(strstr(r.log, "P 0,0 100,0 100,50 0,50 ") != NULL, "the window into the viewport");
    CHECK(strstr(r.log, "P 0,100 100,100 100,200 0,200 ") != NULL, "MM_HIMETRIC: 10 mm is 100 pixels, y going up");
    CHECK(strstr(r.log, "P 100,50 200,50 200,100 100,100 ") != NULL, "RestoreDC brings the mapping back");
}

/* paths: an open figure stroked, a PolylineTo and a PolyBezierTo carrying a figure on, an ellipse in a path */
static void test_emf_paths(void)
{
    static const short to[4] = { 100, 0, 100, 100 };
    static const short curve[6] = { 100, 200, 0, 200, 0, 100 };
    emf e;
    recorder r;
    owf_mf_result res;
    estart(&e, 10000, 5000, 1);
    enone(&e, 59);                                    /* BeginPath */
    epoint(&e, 27, 0, 0);
    epoly16(&e, 89, to, 2);                           /* PolylineTo16 */
    epoly16(&e, 88, curve, 3);                        /* PolyBezierTo16: on from 100,100 */
    enone(&e, 60);
    ebegin(&e, 64); e32(&e, 0); e32(&e, 0); e32(&e, 0); e32(&e, 0); eend(&e);   /* StrokePath */
    enone(&e, 59);
    erect(&e, 42, 400, 0, 600, 200);                  /* Ellipse, in a path */
    enone(&e, 60);
    ebegin(&e, 63); e32(&e, 0); e32(&e, 0); e32(&e, 0); e32(&e, 0); eend(&e);   /* StrokeAndFillPath */
    ebegin(&e, 64); e32(&e, 0); e32(&e, 0); e32(&e, 0); e32(&e, 0); eend(&e);   /* the path is used up: nothing */
    epoint(&e, 54, 700, 0);                           /* LineTo, outside a path: from where the curve ended */
    efinish(&e);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(e.d, e.n, 0, 0, 1000, 500, &sink, &r, &res) == OWF_OK, "paths play");
    CHECK(!strncmp(r.log, "L 0,0 100,0 100,100 ", 20) && strstr(r.log, " 0,100 feeeeee l000000 w0;") != NULL,
          "one open figure, stroked: MoveTo, PolylineTo, then the curve's lines to 0,100");
    CHECK(strstr(r.log, "P 600,100 ") != NULL && strstr(strstr(r.log, "P 600,100 "), " fffffff l000000 w0;") &&
          res.drawn == 3,
          "the ellipse as a closed figure from its right, filled and stroked; then the LineTo: three in all");
    CHECK(strstr(r.log, "L 0,100 700,0 ") != NULL, "the pen ends where the curve did");
}

static void test_emf_path_full(void)
{
    static emf e;                                     /* 4096 figures: too big for the stack's taste */
    recorder *r = (recorder *)calloc(1, sizeof *r);
    owf_mf_result res;
    char first[512];
    int i;
    estart(&e, 10000, 5000, 1);
    enone(&e, 59);                                    /* BeginPath */
    for (i = 0; i < 4096; i++) erect(&e, 43, 0, 0, 100, 100);   /* Rectangle: the path's figures used up */
    epoint(&e, 27, 900, 400);
    epoint(&e, 54, 950, 450);                         /* no room for this figure: dropped, the last one kept */
    enone(&e, 60);
    ebegin(&e, 64); e32(&e, 0); e32(&e, 0); e32(&e, 0); e32(&e, 0); eend(&e);   /* StrokePath */
    efinish(&e);
    CHECK(r && owf_metafile_play(e.d, e.n, 0, 0, 1000, 500, &sink, r, &res) == OWF_OK, "a full path plays");
    if (!r) return;
    i = (int)strcspn(r->log, ";");
    memcpy(first, r->log, (size_t)i + 1);
    first[i + 1] = 0;
    CHECK(res.drawn == 4096 && !strcmp(r->last, first) && !strstr(r->last, "90,40") && !strstr(r->last, "95,45"),
          "every figure as it was put in, and the one past the limit left out");
    free(r);
}

/* ---- arcs, chords and pies ---- */

/* the n-th path in the log: its points into xy (at most max), the count; *closed P or L */
static int log_points(const char *log, int nth, int *xy, int max, int *closed)
{
    const char *q = log;
    int n = 0;
    while (nth-- > 0 && q) { q = strchr(q, ';'); if (q) q++; }
    if (!q || !*q) return -1;
    *closed = *q == 'P';
    q++;
    while (*q == ' ' && q[1] != 'f') {
        int x, y, used;
        if (sscanf(q, " %d,%d%n", &x, &y, &used) != 2) break;
        if (n < max) { xy[n * 2] = x; xy[n * 2 + 1] = y; }
        n++;
        q += used;
    }
    return n;
}

static void arc_wmf(wmf *w, int fn, int l, int t, int r, int b, int xs, int ys, int xe, int ye)
{
    begin(w, fn); put16(w, ye); put16(w, xe); put16(w, ys); put16(w, xs);
    put16(w, b); put16(w, r); put16(w, t); put16(w, l); end(w);
}

/* every point in the quarter right and up of cx, cy (y down) */
static int upper_right(const int *xy, int n, int cx, int cy)
{
    int i;
    for (i = 0; i < n; i++) if (xy[i * 2] < cx || xy[i * 2 + 1] > cy) return 0;
    return 1;
}

static int has_point(const int *xy, int n, int x, int y)
{
    int i;
    for (i = 0; i < n; i++) if (xy[i * 2] == x && xy[i * 2 + 1] == y) return 1;
    return 0;
}

static void test_arcs(void)
{
    wmf w;
    recorder r;
    owf_mf_result res;
    int xy[200], n, closed;
    start(&w, 1, 0, 0, 1000, 1000, 0);
    arc_wmf(&w, 0x0817, 0, 0, 1000, 1000, 1000, 500, 500, 0);      /* Arc: from the right up to the top */
    arc_wmf(&w, 0x0817, 0, 0, 1000, 1000, 500, 0, 1000, 500);      /* and from the top round to the right */
    arc_wmf(&w, 0x0830, 0, 0, 1000, 1000, 1000, 500, 500, 0);      /* Chord */
    arc_wmf(&w, 0x081A, 0, 0, 1000, 1000, 1000, 500, 500, 0);      /* Pie */
    arc_wmf(&w, 0x0817, 0, 0, 1000, 1000, 900, 500, 1000, 500);    /* the same ray twice: all the way round */
    arc_wmf(&w, 0x0817, 10, 10, 10, 50, 0, 0, 50, 50);             /* no width: nothing */
    finish(&w);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 1000, 1000, &sink, &r, &res) == OWF_OK, "a WMF of arcs plays");
    CHECK(res.drawn == 5 && res.skipped == 0, "five drawn, none left out");
    n = log_points(r.log, 0, xy, 100, &closed);
    CHECK(n == 17 && !closed && xy[0] == 1000 && xy[1] == 500 && xy[32] == 500 && xy[33] == 0 &&
          upper_right(xy, n, 500, 500) && has_point(xy, n, 853, 147),
          "Arc: counterclockwise as seen, the short way from the right to the top, a line");
    n = log_points(r.log, 1, xy, 100, &closed);
    CHECK(n == 49 && xy[0] == 500 && xy[1] == 0 && xy[96] == 1000 && xy[97] == 500 && has_point(xy, n, 0, 500) &&
          has_point(xy, n, 500, 1000), "the other way: three quarters, by the left and the bottom");
    n = log_points(r.log, 2, xy, 100, &closed);
    CHECK(n == 17 && closed && strstr(r.log, "P 1000,500 ") && upper_right(xy, n, 500, 500),
          "Chord: the same points, closed and filled");
    n = log_points(r.log, 3, xy, 100, &closed);
    CHECK(n == 18 && closed && xy[34] == 500 && xy[35] == 500, "Pie: on to the centre");
    n = log_points(r.log, 4, xy, 100, &closed);
    CHECK(n == 65 && xy[0] == 1000 && xy[1] == 500 && has_point(xy, n, 0, 500), "one ray twice: the whole ellipse");

    {
        emf e;
        estart(&e, 10000, 5000, 1);
        eone(&e, 57, 2);                                    /* SetArcDirection AD_CLOCKWISE */
        ebegin(&e, 45); e32(&e, 0); e32(&e, 0); e32(&e, 400); e32(&e, 400);
        e32(&e, 400); e32(&e, 200); e32(&e, 200); e32(&e, 0); eend(&e);      /* Arc: right to top, clockwise */
        eone(&e, 57, 1);
        enone(&e, 33);                                      /* SaveDC */
        eone(&e, 17, 3);                                    /* MM_HIMETRIC, y up */
        ebegin(&e, 45); e32(&e, 0); e32(&e, 0); e32(&e, 4000); e32(&e, -4000);
        e32(&e, 4000); e32(&e, -2000); e32(&e, 2000); e32(&e, 0); eend(&e);  /* right to top as seen */
        eone(&e, 34, -1);
        enone(&e, 59);                                      /* a Pie in a path, filled */
        ebegin(&e, 47); e32(&e, 0); e32(&e, 0); e32(&e, 400); e32(&e, 400);
        e32(&e, 400); e32(&e, 200); e32(&e, 200); e32(&e, 0); eend(&e);
        enone(&e, 60);
        ebegin(&e, 62); e32(&e, 0); e32(&e, 0); e32(&e, 0); e32(&e, 0); eend(&e);   /* FillPath */
        ebegin(&e, 46); e32(&e, -2147483647L); e32(&e, -2147483647L); e32(&e, 2147483647L); e32(&e, 2147483647L);
        e32(&e, 2147483647L); e32(&e, 0); e32(&e, 0); e32(&e, -2147483647L); eend(&e);   /* far: no overflow */
        efinish(&e);
        memset(&r, 0, sizeof r);
        CHECK(owf_metafile_play(e.d, e.n, 0, 0, 1000, 500, &sink, &r, &res) == OWF_OK, "an EMF of arcs plays");
        CHECK(res.skipped == 0 && res.drawn == 4, "the arcs drawn, none left out");
        n = log_points(r.log, 0, xy, 100, &closed);
        CHECK(n == 49 && !closed && xy[0] == 400 && xy[1] == 200 && xy[96] == 200 && xy[97] == 0 &&
              has_point(xy, n, 200, 400) && has_point(xy, n, 0, 200), "AD_CLOCKWISE: right to top the long way");
        n = log_points(r.log, 1, xy, 100, &closed);
        CHECK(n == 17 && xy[0] == 400 && xy[1] == 200 && xy[32] == 200 && xy[33] == 0 && upper_right(xy, n, 200, 200),
              "y up (MM_HIMETRIC): still counterclockwise as seen, the short way");
        n = log_points(r.log, 2, xy, 100, &closed);
        CHECK(n == 18 && closed && xy[34] == 200 && xy[35] == 200 && strstr(r.log, "P 400,200 "),
              "a Pie in a path, filled: closed at the centre");
    }
    {
        emf e;
        estart(&e, 10000, 5000, 1);
        epoint(&e, 27, 600, 200);                           /* MoveToEx */
        ebegin(&e, 55); e32(&e, 0); e32(&e, 0); e32(&e, 400); e32(&e, 400);
        e32(&e, 400); e32(&e, 200); e32(&e, 200); e32(&e, 0); eend(&e);      /* ArcTo: right to top */
        epoint(&e, 54, 0, 0);                               /* LineTo: on from the arc's end */
        enone(&e, 59);                                      /* in a path: one figure */
        epoint(&e, 27, 600, 200);
        ebegin(&e, 55); e32(&e, 0); e32(&e, 0); e32(&e, 400); e32(&e, 400);
        e32(&e, 400); e32(&e, 200); e32(&e, 200); e32(&e, 0); eend(&e);
        epoint(&e, 54, 0, 0);
        enone(&e, 60);
        ebegin(&e, 64); e32(&e, 0); e32(&e, 0); e32(&e, 0); e32(&e, 0); eend(&e);   /* StrokePath */
        efinish(&e);
        memset(&r, 0, sizeof r);
        CHECK(owf_metafile_play(e.d, e.n, 0, 0, 1000, 500, &sink, &r, &res) == OWF_OK && res.skipped == 0,
              "ArcTo plays, none left out");
        n = log_points(r.log, 0, xy, 100, &closed);
        CHECK(n == 18 && !closed && xy[0] == 600 && xy[1] == 200 && xy[2] == 400 && xy[3] == 200 && xy[34] == 200 &&
              xy[35] == 0, "ArcTo: a line from the pen to the start, then the arc");
        n = log_points(r.log, 1, xy, 100, &closed);
        CHECK(n == 2 && xy[0] == 200 && xy[1] == 0 && xy[2] == 0 && xy[3] == 0, "the pen left at the arc's end");
        n = log_points(r.log, 2, xy, 100, &closed);
        CHECK(n == 19 && !closed && xy[0] == 600 && xy[2] == 400 && xy[34] == 200 && xy[35] == 0 && xy[36] == 0 &&
              xy[37] == 0, "in a path: MoveTo, ArcTo, LineTo as one figure");
    }
}

static void test_emf_damaged(void)
{
    emf e;
    recorder r;
    size_t cut;
    int ok = 1;
    emf_sample(&e);
    for (cut = 0; cut <= e.n; cut++) {           /* every cut (ASan): played as far as it holds, or refused */
        int rc;
        memset(&r, 0, sizeof r);
        rc = owf_metafile_play(e.d, cut, 0, 0, 2000, 1000, &sink, &r, NULL);
        if (rc != OWF_OK && rc != OWF_ERR_FORMAT) ok = 0;
    }
    CHECK(ok, "an EMF cut anywhere: no harm");
    memset(&r, 0, sizeof r);
    r.stop_after = 2;
    CHECK(owf_metafile_play(e.d, e.n, 0, 0, 2000, 1000, &sink, &r, NULL) == OWF_ERR_MEMORY, "the sink stops it");
    e.d[40] = 'X';
    CHECK(!owf_metafile_is_emf(e.d, e.n) && owf_metafile_play(e.d, e.n, 0, 0, 10, 10, &sink, &r, NULL) ==
          OWF_ERR_FORMAT, "no signature: not an EMF");
    {
        /* a Poly record whose Count runs past its record, and an object index past Handles */
        emf b;
        static const short tri[6] = { 0, 0, 10, 0, 0, 10 };
        estart(&b, 10000, 5000, 2);
        epoly16(&b, 86, tri, 3);
        b.d[b.rec + 24] = 200;                    /* Count 200 in a record of 3 points */
        epen(&b, 9, 0, 1, 0, 0, 0);               /* place 9 of 2 */
        eone(&b, 37, 9);
        ebegin(&b, 81); e32(&b, 0); eend(&b);
        b.d[b.rec + 4] = 0xFF; b.d[b.rec + 5] = 0xFF;   /* a size past the end */
        memset(&r, 0, sizeof r);
        CHECK(owf_metafile_play(b.d, b.n, 0, 0, 100, 100, &sink, &r, NULL) == OWF_OK && !r.log[0],
              "bad counts and places: nothing drawn, no harm");
    }
}

static void test_emf_render_and_pdf(void)
{
    emf e;
    unsigned char *argb = (unsigned char *)malloc(100 * 50 * 4);
    owf_image im;
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *out = NULL;
    size_t n = 0;
    int tw = 0, th = 0;
    if (!argb || !pdf) { CHECK(0, "memory"); free(argb); owf_pdf_free(pdf); return; }
    emf_sample(&e);
    CHECK(owf_metafile_size(e.d, e.n, &tw, &th) && tw == 5669 && th == 2834, "its size from Frame: 100 by 50 mm");
    memset(argb, 255, 100 * 50 * 4);
    CHECK(owf_metafile_render(e.d, e.n, argb, 100, 50, NULL) == OWF_OK, "an EMF into pixels");
    CHECK(pixel(argb, 100, 20, 15) == 0x0000FF, "inside its rectangle: blue");
    CHECK(pixel(argb, 100, 9, 15) == 0xFF0000, "its left edge: the red pen");
    CHECK(pixel(argb, 100, 88, 3) == 0x0000FF, "the filled path");
    memset(&im, 0, sizeof im);
    im.mime = (char *)"image/x-emf";
    im.data = e.d;
    im.length = e.n;
    CHECK(owf_pdf_page(pdf, 12240, 15840) == 0 && owf_pdf_image(pdf, &im, 1440, 1440, 2000, 1000, NULL) == 0,
          "the EMF placed on a page");
    CHECK(owf_pdf_finish(pdf, "emf", &out, &n, NULL) == OWF_OK && out, "the PDF");
    if (out) {
        CHECK(count(out, n, "/XObject") == 0, "drawn as paths, no picture object");
        CHECK(count(out, n, "(Hi\\200) Tj") == 1, "its text");
        CHECK(count(out, n, "0.000 0.000 1.000 rg") >= 1, "its blue");
    }
    free(out);
    owf_pdf_free(pdf);
    free(argb);
}

/* ---- clipping: IntersectClipRect, SaveDC/RestoreDC keeping it, ExtSelectClipRgn's reset ---- */
static void test_clip(void)
{
    wmf w;
    recorder r;
    owf_mf_result res;
    start(&w, 1, 0, 0, 1000, 1000, 0);
    box_record(&w, 0x0416, 100, 100, 500, 500);          /* IntersectClipRect */
    box_record(&w, 0x041B, 0, 0, 1000, 1000);
    box_record(&w, 0x0416, 300, 300, 800, 800);          /* with the one before: 300,300 to 500,500 */
    box_record(&w, 0x041B, 0, 0, 1000, 1000);
    begin(&w, 0x001E); end(&w);                           /* SaveDC */
    box_record(&w, 0x0416, 900, 900, 950, 950);          /* outside it: nothing shows */
    box_record(&w, 0x041B, 0, 0, 1000, 1000);
    begin(&w, 0x0127); put16(&w, -1); end(&w);           /* RestoreDC */
    box_record(&w, 0x041B, 0, 0, 1000, 1000);
    finish(&w);
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 1000, 1000, &csink, &r, &res) == OWF_OK && res.skipped == 0 &&
          res.drawn == 4, "a WMF with clipping plays, none left out");
    CHECK(!strncmp(r.log, "C 100,100 400,400;P ", 20) && strstr(r.log, ";C 300,300 200,200;P ") &&
          strstr(r.log, ";C 900,900 0,0;P ") && strstr(strstr(r.log, ";C 900,900 0,0;P "), ";C 300,300 200,200;P "),
          "the sink told each clip before it draws: set, cut down, empty, and back after RestoreDC");
    memset(&r, 0, sizeof r);
    CHECK(owf_metafile_play(w.d, w.n, 0, 0, 1000, 1000, &sink, &r, &res) == OWF_OK && res.skipped == 3 &&
          res.drawn == 4, "a sink that can't clip: the clip records counted as left out, all drawn");
    {
        emf e;
        unsigned char *argb = (unsigned char *)malloc(100 * 50 * 4);
        owf_image im;
        owf_pdf *pdf = owf_pdf_new();
        unsigned char *out = NULL;
        size_t n = 0;
        estart(&e, 10000, 5000, 2);
        ebrush(&e, 1, 0, 255, 0, 0);
        eone(&e, 37, 1);                                  /* SelectObject: red */
        erect(&e, 30, 0, 0, 500, 250);                    /* IntersectClipRect: the top left quarter */
        erect(&e, 43, 0, 0, 1000, 500);
        ebegin(&e, 75); e32(&e, 0); e32(&e, 5); eend(&e);  /* ExtSelectClipRgn RGN_COPY, no region: no clip */
        erect(&e, 43, 900, 400, 1000, 500);
        ebegin(&e, 75); e32(&e, 32); e32(&e, 1); for (n = 0; n < 8; n++) e32(&e, 0); eend(&e);   /* a region */
        efinish(&e);
        memset(&r, 0, sizeof r);
        CHECK(owf_metafile_play(e.d, e.n, 0, 0, 1000, 500, &csink, &r, &res) == OWF_OK && res.skipped == 1,
              "an EMF clip plays; a clip region is left out");
        CHECK(!strncmp(r.log, "C 0,0 500,250;P 0,0 1000,0 1000,500 0,500 ", 42) && strstr(r.log, ";C 0,0 -1,-1;P "),
              "IntersectClipRect, then ExtSelectClipRgn taking it off");
        if (argb && pdf) {
            memset(argb, 255, 100 * 50 * 4);
            CHECK(owf_metafile_render(e.d, e.n, argb, 100, 50, NULL) == OWF_OK, "clipped into pixels");
            CHECK(pixel(argb, 100, 10, 10) == 0xFF0000 && pixel(argb, 100, 49, 24) == 0xFF0000 &&
                  pixel(argb, 100, 50, 10) == 0xFFFFFF && pixel(argb, 100, 10, 25) == 0xFFFFFF &&
                  pixel(argb, 100, 95, 45) == 0xFF0000, "red only inside the clip, then all of the corner after it");
            memset(&im, 0, sizeof im);
            im.mime = (char *)"image/x-emf";
            im.data = e.d;
            im.length = e.n;
            n = 0;
            CHECK(owf_pdf_page(pdf, 12240, 15840) == 0 && owf_pdf_image(pdf, &im, 1440, 1440, 2000, 1000, NULL) == 0 &&
                  owf_pdf_finish(pdf, "clip", &out, &n, NULL) == OWF_OK && out, "the clipped EMF in a PDF");
            if (out)
                CHECK(count(out, n, " re W n") == 4 && count(out, n, "Q\nq\n") == 2,
                      "PDF: the box, then box and clip, then the box again");
        }
        free(out);
        owf_pdf_free(pdf);
        free(argb);
    }
}

int main(void)
{
    test_play();
    test_window();
    test_far();
    test_damaged();
    test_render();
    test_render_far();
    test_pdf();
    test_emf_play();
    test_emf_mapping();
    test_emf_paths();
    test_emf_path_full();
    test_arcs();
    test_clip();
    test_emf_damaged();
    test_emf_render_and_pdf();
    printf("metafile: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
