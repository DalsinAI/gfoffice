/*
 * OpenWrite's text widths for the layout (app/ow_fontwidths.c), on fonts
 * made up here: a face that never opens (OS 3.2's Intellifont
 * CGTriumvirate, which openfont.library can't read and which has no bitmap
 * sizes: the lab's case, 25 October 2026), outline faces that open at any
 * size, a bitmap face with two sizes, and the screen's font. The widths
 * must come from the face that draws the text, at the size asked or scaled
 * to it, and never from the screen's font as they are (OpenWrite 2.0.3's
 * first build took the screen font's widths unscaled: 81 pages, not 32).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ow_fontwidths.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(x) do { ++checks; if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

/* A made-up font: an outline (any size), a bitmap (its sizes only, else
 * the nearest, as openfont's FPF_DESIGNED request answers), or none. */
typedef struct {
    const char *file;
    int outline;
    int sizes[4];            /* a bitmap's */
    int em;                  /* its advances: em thousandths of the size a character */
    int opened;              /* how many times it was asked for */
} fake_font;

typedef struct {
    fake_font *f;
    int px;
} fake_open;

static fake_font fonts[8];
static int nfonts;
static fake_open opens[64];
static int nopens;
static fake_font screen = { "screen", 0, { 20, 0, 0, 0 }, 600, 0 };

static void fonts_set(const char *const files[], const int outline[], int em)
{
    int i;
    memset(fonts, 0, sizeof fonts);
    for (i = 0; files[i]; ++i) {
        fonts[i].file = files[i];
        fonts[i].outline = outline[i];
        fonts[i].em = em + 20 * i;
        if (!outline[i]) {
            fonts[i].sizes[0] = 11;
            fonts[i].sizes[1] = 13;
        }
    }
    nfonts = i;
    nopens = 0;
}

static int fake_advance_of(const fake_font *f, int px, unsigned char c, int bold)
{
    int em = c == ' ' ? f->em / 2 : c == 'i' || c == 'l' ? f->em / 3 : c == 'm' || c == 'W' ? f->em * 3 / 2 : f->em;
    return (em * px + 500) / 1000 + (bold ? 1 : 0);
}

static void *fake_open_font(void *user, const char *file, int pixels, int *ysize, int *ascent)
{
    int i, k, best = 0;
    (void)user;
    for (i = 0; i < nfonts; ++i) {
        fake_font *f = &fonts[i];
        if (strcmp(f->file, file)) continue;
        ++f->opened;
        if (!f->outline) {                           /* a bitmap: its nearest size */
            for (k = 0; k < 4 && f->sizes[k]; ++k)
                if (!best || (f->sizes[k] > pixels ? f->sizes[k] - pixels : pixels - f->sizes[k]) <
                             (best > pixels ? best - pixels : pixels - best))
                    best = f->sizes[k];
            if (!best) return NULL;                  /* no bitmaps: it doesn't open */
            pixels = best;
        }
        if (nopens >= 64) return NULL;
        opens[nopens].f = f;
        opens[nopens].px = pixels;
        *ysize = pixels;
        *ascent = pixels * 4 / 5;
        return &opens[nopens++];
    }
    return NULL;
}

static int fake_advance(void *user, void *font, unsigned char c, int bold)
{
    (void)user;
    if (font == &screen) return fake_advance_of(&screen, 20, c, bold);
    return fake_advance_of(((fake_open *)font)->f, ((fake_open *)font)->px, c, bold);
}

static void init(ow_fontwidths *fw, const char *theme)
{
    ow_fw_backend be;
    memset(&be, 0, sizeof be);
    be.open = fake_open_font;
    be.advance = fake_advance;
    be.fallback = &screen;
    be.fallback_ysize = 20;
    be.fallback_ascent = 16;
    ow_fw_init(fw, &be, theme);
}

static fake_font *font_named(const char *file)
{
    int i;
    for (i = 0; i < nfonts; ++i)
        if (!strcmp(fonts[i].file, file)) return &fonts[i];
    return NULL;
}

static void test_pixels(void)
{
    CHECK(ow_fw_pixels(880) == 15);              /* 11 points at 100%: 14.67 */
    CHECK(ow_fw_pixels(660) == 11);
    CHECK(ow_fw_pixels(800) == 13);
    CHECK(ow_fw_pixels(0) == 4 && ow_fw_pixels(-5) == 4);
    CHECK(ow_fw_pixels(100000) == 128);
}

/* The lab's case: CGTriumvirate doesn't open at any size; DejaVu Sans (the theme's font) does. */
static void test_face_that_never_opens(void)
{
    static const char *const files[] = { "DejaVuSans.font", "CGTimes.font", "DejaVuSerif.font", NULL };
    static const int outline[] = { 1, 0, 1 };
    ow_fontwidths fw;
    const ow_fw_table *t, *b;
    fonts_set(files, outline, 550);
    font_named("CGTimes.font")->sizes[0] = 0;     /* an Intellifont with no bitmaps: never opens */
    font_named("CGTimes.font")->outline = 0;
    init(&fw, "DejaVuSans.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), "DejaVuSans.font"));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15);
    CHECK(!strcmp(t->face, "DejaVuSans.font") && t->from == OW_FW_FROM_FACE && t->px == 15);
    /* the face's own advances at 15, not the screen font's (600 thousandths at 20 px: 12) */
    CHECK(t->w['a'] == fake_advance_of(font_named("DejaVuSans.font"), 15, 'a', 0));
    CHECK(t->w['a'] == 8 && t->w['m'] == 12 && t->w[' '] == 4);
    CHECK(t->ascent + t->descent == 15);
    /* bold: its own table, the smear in each advance */
    b = ow_fw_widths(&fw, "CGTriumvirate.font", 1, 15);
    CHECK(b != t && b->w['a'] == t->w['a'] + 1);
    /* the same answer again, from the table kept */
    CHECK(ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15) == t);
    /* a serif face that doesn't open: the serif kind's next */
    CHECK(!strcmp(ow_fw_face(&fw, "CGTimes.font"), "DejaVuSerif.font"));
    CHECK(!strcmp(ow_fw_widths(&fw, "CGTimes.font", 0, 11)->face, "DejaVuSerif.font"));
    /* a face that opens is its own */
    CHECK(!strcmp(ow_fw_face(&fw, "DejaVuSans.font"), "DejaVuSans.font"));
    /* the faces are tried once: asking again opens nothing more */
    {
        int before = font_named("DejaVuSans.font")->opened;
        ow_fw_face(&fw, "CGTriumvirate.font");
        CHECK(font_named("DejaVuSans.font")->opened == before);
    }
    ow_fw_reset(&fw);
    CHECK(ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15)->w['a'] == 8);
}

/* A bitmap face with 11 and 13 only: other sizes' widths are scaled from the nearest. */
static void test_bitmap_face(void)
{
    static const char *const files[] = { "helvetica.font", NULL };
    static const int outline[] = { 0 };
    ow_fontwidths fw;
    const ow_fw_table *t;
    fonts_set(files, outline, 600);
    init(&fw, "Missing.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), "helvetica.font"));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 13);
    CHECK(t->from == OW_FW_FROM_FACE && t->w['a'] == fake_advance_of(&fonts[0], 13, 'a', 0));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 26);
    /* 13's 'a' (8) scaled to 26 */
    CHECK(t->from == OW_FW_FROM_SCALED && t->px == 26 && t->w['a'] == 16);
    CHECK(t->ascent + t->descent == 26);
}

/* An outline face that won't open at one size: the probe size's widths, scaled. */
static void test_size_that_fails(void)
{
    static const char *const files[] = { "DejaVuSans.font", NULL };
    static const int outline[] = { 1 };
    ow_fontwidths fw;
    const ow_fw_table *t;
    fonts_set(files, outline, 600);
    init(&fw, NULL);
    fonts[0].outline = 0;                           /* only 15 opens now: as a bitmap of one size */
    fonts[0].sizes[0] = 15;
    fonts[0].sizes[1] = 0;
    t = ow_fw_widths(&fw, "DejaVuSans.font", 0, 9);
    CHECK(t->from == OW_FW_FROM_SCALED && t->w['a'] == (fake_advance_of(&fonts[0], 15, 'a', 0) * 9 + 7) / 15);
}

/* Nothing opens: the theme's face, and without that the screen's font, scaled to the size asked. */
static void test_nothing_opens(void)
{
    static const char *const files[] = { "Ubuntu.font", NULL };
    static const int outline[] = { 1 };
    static const char *const none[] = { NULL };
    ow_fontwidths fw;
    const ow_fw_table *t;
    fonts_set(files, outline, 500);
    init(&fw, "Ubuntu.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), "Ubuntu.font"));
    CHECK(!strcmp(ow_fw_face(&fw, "courier.font"), "Ubuntu.font"));
    fonts_set(none, outline, 500);
    init(&fw, "Ubuntu.font");
    CHECK(!strcmp(ow_fw_face(&fw, "CGTriumvirate.font"), ""));
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 15);
    /* the screen font's 12 px 'a' at 20 px, scaled to 15: 9; never its 12 as it is */
    CHECK(t->from == OW_FW_FROM_SCREEN && t->w['a'] == 9 && t->w['a'] != 12);
    CHECK(t->ascent == 12 && t->descent == 3);
    t = ow_fw_widths(&fw, "CGTriumvirate.font", 0, 30);
    CHECK(t->w['a'] == 18);
}

int main(void)
{
    test_pixels();
    test_face_that_never_opens();
    test_bitmap_face();
    test_size_that_fails();
    test_nothing_opens();
    printf("test-fontwidths: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
