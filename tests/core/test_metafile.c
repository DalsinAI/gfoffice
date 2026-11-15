/*
 * libowf's WMF player (owf_metafile.c) and the page PDF writer playing WMF pictures (owf_pdf.c). Every metafile
 * here is made by this file from [MS-WMF]'s record layouts, so no picture from any program goes into the
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
    char log[4096];
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
    if (r->stop_after && ++r->calls >= r->stop_after) return -1;
    say(r, closed ? "P" : "L");
    for (i = 0; i < n; i++) { snprintf(buf, sizeof buf, " %d,%d", xy[i * 2], xy[i * 2 + 1]); say(r, buf); }
    snprintf(buf, sizeof buf, " f%06lx l%06lx w%d%s;", OWF_SHADE_SET(fill) ? OWF_SHADE_RGB(fill) : 0xEEEEEEUL,
             OWF_SHADE_SET(line) ? OWF_SHADE_RGB(line) : 0xEEEEEEUL, lw, dash ? " dash" : "");
    say(r, buf);
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

static const owf_mf_sink sink = { rec_path, rec_ellipse, rec_text, rec_width };

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
        if (note && strstr(note, "Parts of WMF pictures")) approx = 1;
    }
    CHECK(!lost, "not reported as a picture left out");
    CHECK(approx, "the StretchDIB inside reported");
    free(out);
    owf_report_free(rp);
    owf_pdf_free(pdf);
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
    printf("metafile: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
