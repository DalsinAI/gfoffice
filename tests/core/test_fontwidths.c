/*
 * OpenWrite's text widths for the layout (app/ow_fontwidths.c), on fonts
 * made up here, as the lab's Amiga has them (25 October 2026): OS 3.2's
 * Intellifont CGTriumvirate and CGTimes, which don't open (openfont.library
 * can't read Intellifont, and they have no bitmap sizes); bitmap families
 * with a few designed sizes (helvetica 11, 13, 15, 18, 24), which diskfont
 * scales to any other; no DejaVu or Liberation file; and the window's font
 * from the ROM at one size (23 pixels). The rule: what a word is laid out
 * with is what it is drawn with. At 100% the sample of letters and digits is
 * drawn exactly as wide as it is laid out, at every other zoom no wider, and
 * the widths are never the window font's taken as they are (OpenWrite
 * 2.0.3's first build: 81 pages, not 32; its second: drawn 1.5 times as wide
 * as laid out).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ow_fontwidths.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(x) do { ++checks; if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

/* A made-up font: an outline (any size, exact), a bitmap family with
 * designed sizes (a size between them scaled from the nearest when scaling
 * is on, as OpenDiskFont does without FPF_DESIGNED; else the nearest as it
 * is), or one that never opens (no sizes, not an outline). */
typedef struct {
    const char *file;
    int outline;
    int sizes[6];
    int em;                  /* advances: em thousandths of the designed size a character */
} fake_font;

typedef struct {
    const fake_font *f;
    int px;                  /* its height */
    int from;                /* the designed size it is scaled from; 0: none */
} fake_open;

static fake_font fonts[8];
static int nfonts, scaling = 1;
static fake_open opened[256];
static int nopened;
static fake_open window_font;

static void add_font(const char *file, int outline, const int *sizes, int em)
{
    fake_font *f = &fonts[nfonts++];
    int k;
    memset(f, 0, sizeof *f);
    f->file = file;
    f->outline = outline;
    for (k = 0; sizes && sizes[k] && k < 6; ++k) f->sizes[k] = sizes[k];
    f->em = em;
}

static void reset_fonts(void)
{
    nfonts = 0;
    nopened = 0;
    scaling = 1;
}

/* a character's width at the designed (or outline) size px */
static int designed_advance(const fake_font *f, int px, unsigned char c)
{
    int em = c == ' ' ? f->em / 2 : c == 'i' || c == 'l' ? f->em / 3 : c == 'm' || c == 'W' ? f->em * 3 / 2 : f->em;
    /* small bitmap sizes run wide: a pixel more a letter below 14 */
    return (em * px + 500) / 1000 + (!f->outline && px < 14 && c != ' ' ? 1 : 0);
}

static int fake_advance(void *user, void *font, unsigned char c, int bold)
{
    const fake_open *o = (const fake_open *)font;
    int a;
    (void)user;
    if (o->from)                                     /* scaled from a designed size, rounded a glyph */
        a = (designed_advance(o->f, o->from, c) * o->px + o->from / 2) / o->from;
    else
        a = designed_advance(o->f, o->px, c);
    return a + (bold ? 1 : 0);
}

static void *fake_open_font(void *user, const char *file, int pixels, int *ysize, int *ascent)
{
    int i, k, best = 0;
    (void)user;
    for (i = 0; i < nfonts; ++i) {
        const fake_font *f = &fonts[i];
        fake_open *o;
        if (strcmp(f->file, file)) continue;
        if (nopened >= 256) return NULL;
        o = &opened[nopened];
        o->f = f;
        o->px = pixels;
        o->from = 0;
        if (!f->outline) {
            for (k = 0; k < 6 && f->sizes[k]; ++k)
                if (!best || (f->sizes[k] > pixels ? f->sizes[k] - pixels : pixels - f->sizes[k]) <
                             (best > pixels ? best - pixels : pixels - best))
                    best = f->sizes[k];
            if (!best) return NULL;                  /* no sizes (an Intellifont openfont can't read) */
            if (best != pixels && scaling) o->from = best;
            else o->px = best;
        }
        ++nopened;
        *ysize = o->px;
        *ascent = o->px * 4 / 5;
        return o;
    }
    return NULL;
}

static void init(ow_fontwidths *fw, const char *theme, const char *window)
{
    ow_fw_backend be;
    memset(&be, 0, sizeof be);
    be.open = fake_open_font;
    be.advance = fake_advance;
    window_font.f = &fonts[nfonts > 0 ? nfonts - 1 : 0];
    be.fallback = NULL;
    ow_fw_init(fw, &be, theme, window);
}

/* The rule, for a style at the zooms OpenWrite offers and a few between. */
static void check_rule(ow_fontwidths *fw, const char *file, int bold, long size, const char *face)
{
    static const int zooms[] = { 100, 25, 50, 60, 75, 90, 110, 125, 150, 200 };
    int z;
    for (z = 0; z < (int)(sizeof zooms / sizeof zooms[0]); ++z) {
        ow_fw_check c;
        ow_fw_check_sample(fw, file, bold, size, zooms[z], &c);
        CHECK(!strcmp(c.face, face));
        if (c.zoom_px < 5) continue;                 /* drawn as grey lines, not letters (paint_text) */
        CHECK(c.drawn > 0 && c.draw_ysize == c.draw_px);
        if (zooms[z] == 100) {
            CHECK(c.drawn == c.laid && c.draw_px == c.px && c.zoom_px == c.px);
        } else {
            CHECK(c.drawn <= c.laid);
            /* and not much narrower: at most a quarter smaller, or the size before it */
            CHECK(c.draw_px >= c.zoom_px * 3 / 4);
        }
        if (c.drawn > c.laid || (zooms[z] == 100 && c.drawn != c.laid))
            printf("  %s %ld units at %d%%: laid %ld, drawn %ld at %dpx\n", file, size, zooms[z], c.laid, c.drawn,
                   c.draw_px);
    }
}

static void test_pixels(void)
{
    CHECK(ow_fw_pixels(880) == 15);              /* 11 points at 100%: 14.67 */
    CHECK(ow_fw_pixels(660) == 11);
    CHECK(ow_fw_pixels(800) == 13);
    CHECK(ow_fw_pixels(0) == 4 && ow_fw_pixels(-5) == 4);
    CHECK(ow_fw_pixels(100000) == 128);
    CHECK(ow_fw_pixels_at(880, 100) == 15 && ow_fw_pixels_at(880, 60) == 9 && ow_fw_pixels_at(880, 75) == 11);
    CHECK(ow_fw_pixels_at(880, 50) == 7 && ow_fw_pixels_at(880, 200) == 29);
}

/* The lab: the Intellifont faces don't open; helvetica's bitmaps do, scaled between their sizes. */
static void test_lab_fonts(void)
{
    static const int helvetica[] = { 11, 13, 15, 18, 24, 0 };
    static const int times[] = { 11, 13, 15, 18, 24, 0 };
    static const int window[] = { 23, 0 };
    ow_fontwidths fw;
    const ow_fw_table *t;
    reset_fonts();
    add_font("CGTriumvirate.font", 0, NULL, 550);   /* never opens */
    add_font("CGTimes.font", 0, NULL, 520);
    add_font("helvetica.font", 0, helvetica, 560);
    add_font("times.font", 0, times, 500);
    add_font("DejaVuSansMono.font", 0, window, 600);
    init(&fw, "DejaVuSans.font", "DejaVuSansMono.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), "helvetica.font"));
    CHECK(!strcmp(ow_fw_face(&fw, "CGTimes.font"), "times.font"));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15);
    CHECK(t->from == OW_FW_FROM_FACE && t->px == 15 && !strcmp(t->face, "helvetica.font"));
    CHECK(t->ascent + t->descent == 15);
    /* the same answer again, from the table kept */
    CHECK(ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15) == t);
    /* 11 point body text, 16 point headings, bold, 9 point: drawn as laid out */
    check_rule(&fw, "CGTriumvirate.font", 0, 880, "helvetica.font");
    check_rule(&fw, "CGTriumvirate.font", 1, 880, "helvetica.font");
    check_rule(&fw, "CGTriumvirate.font", 1, 1280, "helvetica.font");
    check_rule(&fw, "CGTriumvirate.font", 0, 720, "helvetica.font");
    check_rule(&fw, "CGTimes.font", 0, 960, "times.font");
}

/* Only the window's font, a ROM bitmap at one size (23), which diskfont scales to any other. */
static void test_only_the_window_font(void)
{
    static const int window[] = { 23, 0 };
    ow_fontwidths fw;
    const ow_fw_table *t;
    reset_fonts();
    add_font("CGTriumvirate.font", 0, NULL, 550);
    add_font("DejaVuSansMono.font", 0, window, 600);
    init(&fw, "DejaVuSans.font", "DejaVuSansMono.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), "DejaVuSansMono.font"));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15);
    /* its widths at 15, as scaled to 15, not its 23's as they are */
    CHECK(t->from == OW_FW_FROM_FACE && t->w['a'] == (designed_advance(&fonts[1], 23, 'a') * 15 + 11) / 23);
    CHECK(t->w['a'] != designed_advance(&fonts[1], 23, 'a'));
    check_rule(&fw, "CGTriumvirate.font", 0, 880, "DejaVuSansMono.font");
    check_rule(&fw, "CGTriumvirate.font", 1, 880, "DejaVuSansMono.font");
    check_rule(&fw, "courier.font", 0, 800, "DejaVuSansMono.font");
}

/* An outline that opens at any size; a serif and a monospaced document font find their kind's. */
static void test_outline_faces(void)
{
    ow_fontwidths fw;
    reset_fonts();
    add_font("CGTriumvirate.font", 0, NULL, 550);
    add_font("DejaVuSans.font", 1, NULL, 550);
    add_font("DejaVuSerif.font", 1, NULL, 520);
    add_font("DejaVuSansMono.font", 1, NULL, 600);
    init(&fw, "DejaVuSans.font", NULL);
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), "DejaVuSans.font"));
    CHECK(!strcmp(ow_fw_face(&fw, "CGTimes.font"), "DejaVuSerif.font"));
    CHECK(!strcmp(ow_fw_face(&fw, "courier.font"), "DejaVuSansMono.font"));
    CHECK(!strcmp(ow_fw_face(&fw, "DejaVuSans.font"), "DejaVuSans.font"));
    check_rule(&fw, "CGTriumvirate.font", 0, 880, "DejaVuSans.font");
    check_rule(&fw, "CGTimes.font", 1, 1440, "DejaVuSerif.font");
    /* bold: its own table, the smear in each advance */
    CHECK(ow_fw_widths(&fw, "CGTriumvirate.font", 1, 15)->w['a'] == ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15)->w['a'] + 1);
    ow_fw_reset(&fw);
    CHECK(fw.ntables == 0 && fw.nfaces == 0 && fw.nfits == 0);
}

/* Bitmaps that can't be scaled (no diskfont): a size between the designed ones is the nearest's widths,
 * scaled, and says so; at its designed sizes the rule holds. */
static void test_no_scaling(void)
{
    static const int helvetica[] = { 11, 13, 15, 18, 24, 0 };
    ow_fontwidths fw;
    const ow_fw_table *t;
    reset_fonts();
    add_font("helvetica.font", 0, helvetica, 560);
    scaling = 0;
    init(&fw, NULL, NULL);
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 16);
    CHECK(t->from == OW_FW_FROM_SCALED && t->px == 16);
    CHECK(t->w['a'] == (designed_advance(&fonts[0], 15, 'a') * 16 + 7) / 15);
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15);
    CHECK(t->from == OW_FW_FROM_FACE);
    {
        ow_fw_check c;
        ow_fw_check_sample(&fw, "CGTriumvirate.font", 0, 880, 100, &c);
        CHECK(c.drawn == c.laid);
    }
}

/* Nothing opens at all: the window's font, its widths scaled to the size asked. */
static void test_nothing_opens(void)
{
    static const int window[] = { 20, 0 };
    ow_fontwidths fw;
    ow_fw_backend be;
    const ow_fw_table *t;
    reset_fonts();
    add_font("screen", 0, window, 600);
    window_font.f = &fonts[0];
    window_font.px = 20;
    window_font.from = 0;
    memset(&be, 0, sizeof be);
    be.open = fake_open_font;
    be.advance = fake_advance;
    be.fallback = &window_font;
    be.fallback_ysize = 20;
    be.fallback_ascent = 16;
    ow_fw_init(&fw, &be, "Missing.font", "Missing too.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), ""));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15);
    /* the 12 px 'a' at 20, scaled to 15: 9, never its 12 as it is */
    CHECK(t->from == OW_FW_FROM_SCREEN && t->w['a'] == 9);
    CHECK(t->ascent == 12 && t->descent == 3);
    CHECK(ow_fw_widths(&fw, "CGTriumvirate.font", 0, 30)->w['a'] == 18);
}

int main(void)
{
    test_pixels();
    test_lab_fonts();
    test_only_the_window_font();
    test_outline_faces();
    test_no_scaling();
    test_nothing_opens();
    printf("test-fontwidths: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
