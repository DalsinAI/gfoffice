/*
 * The shared core's fills and picture writers (owf_fill.h, owf_picture.h): libowf's one copy gives what
 * GFOpenPresentation's and GFOpenDiag's own copies gave (golden FNV-1a sums of their output, taken from main
 * 3d40fb09 before the move with SHARED_GOLDEN_PRINT), the old names (opr_png_encode, opr_ilbm_encode,
 * opr_gradient_band) give the same bytes as the new, and the pieces are right on their own (PNG's CRC against
 * the specification's values, the sine, the blends, a PNG whose stored rows are its pixels).
 * Built with OWF_TEST_NO_ZLIBRARY, so PNG is written with stored blocks and the sums don't hang on a zlib version.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_pdf.h"
#ifndef SHARED_GOLDEN_PRINT
#include "owf_fill.h"
#include "owf_picture.h"
#include "owf_internal.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;
#define CHECK(c) do { checks++; if (!(c)) { failed++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned long fnv(unsigned long h, const void *data, size_t n)
{
    const unsigned char *p = data;
    size_t i;
    for (i = 0; i < n; i++)
        h = ((h ^ p[i]) * 16777619UL) & 0xffffffffUL;
    return h;
}

#define FNV0 2166136261UL

/* a test card: odd width so the rows don't line up, runs for ByteRun1 and changes for its literals */
static unsigned char *card(int w, int h)
{
    unsigned char *p = malloc((size_t)w * h * 3);
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *q = p + ((size_t)y * w + x) * 3;
            q[0] = (unsigned char)(x * 255 / (w - 1));
            q[1] = (unsigned char)(y * 255 / (h - 1));
            q[2] = (unsigned char)((x ^ y) & 0xff);
            if (x >= 10 && x < 30 && y >= 5 && y < 20) { q[0] = 255; q[1] = 0; q[2] = 0; }
        }
    return p;
}

/* each picture writer's file for the card, summed */
static unsigned long picture_sum(int ilbm)
{
    int w = 101, h = 37;
    unsigned char *rgb = card(w, h), *f;
    size_t n = 0;
    unsigned long s;
    f = ilbm ? opr_ilbm_encode(rgb, w, h, (size_t)w * 3, &n) : opr_png_encode(rgb, w, h, (size_t)w * 3, &n);
    s = f ? fnv(FNV0, f, n) ^ (unsigned long)n : 0;
    free(f);
    free(rgb);
    return s;
}

/* every band of a set of outlines, kinds, angles and band counts, points and colours, summed */
static unsigned long bands_sum(void)
{
    static const int box[] = { 0, 0, 4000, 0, 4000, 2000, 0, 2000 };
    static const int tri[] = { 100, 3000, 2000, 0, 3900, 3000 };
    static const int notch[] = { 0, 0, 3000, 0, 3000, 3000, 1500, 1500, 0, 3000 };
    static const int angles[] = { 0, 450, 900, 1350, 1800, 2250, 2700, 3150, 3599, -450, 123 };
    const int *shapes[3] = { box, tri, notch };
    static const int counts[3] = { 4, 3, 5 };
    static int out[2 * OPR_GRADIENT_MAX_POINTS];
    unsigned long s = FNV0, colour;
    int sh, kind, a, bands, band, m;
    for (sh = 0; sh < 3; sh++)
        for (kind = OPR_GRADIENT_LINEAR; kind <= OPR_GRADIENT_RADIAL; kind++)
            for (a = 0; a < (int)(sizeof angles / sizeof angles[0]); a++)
                for (bands = 1; bands <= 48; bands += 47 / 3)
                    for (band = 0; band < bands; band++) {
                        colour = 0;
                        m = opr_gradient_band(shapes[sh], counts[sh], 0, 0, 4000, 3000, kind, angles[a], 0x102030,
                                              0xF0E0D0, band, bands, out, OPR_GRADIENT_MAX_POINTS, &colour);
                        s = fnv(s, &m, sizeof m);
                        s = fnv(s, &colour, sizeof colour);
                        if (m > 0)
                            s = fnv(s, out, (size_t)m * 2 * sizeof out[0]);
                    }
    return s;
}

/* a PDF of gradient-filled shapes (owf_pdf_gradient: the sine and blends now owf_fill.c's), summed */
static unsigned long pdf_sum(void)
{
    static const int box[] = { 1000, 1000, 9000, 1000, 9000, 5000, 1000, 5000 };
    static const int tri[] = { 2000, 9000, 6000, 6000, 9500, 9000 };
    owf_pdf *pdf = owf_pdf_new();
    unsigned char *data = NULL;
    size_t n = 0;
    unsigned long s = 0;
    int a;
    if (!pdf || owf_pdf_page(pdf, 11906, 16838) < 0)
        return 0;
    for (a = 0; a < 3600; a += 450) {
        owf_pdf_gradient(pdf, box, 4, 1000, 1000, 8000, 4000, OWF_PDF_GRADIENT_LINEAR, a, OWF_SHADE(0x0000FF),
                         OWF_SHADE(0xFFFF00));
        owf_pdf_gradient(pdf, tri, 3, 2000, 6000, 7500, 3000, OWF_PDF_GRADIENT_RADIAL, a, OWF_SHADE(0xFFFFFF),
                         OWF_SHADE(0x800000));
    }
    if (owf_pdf_finish(pdf, "gradients", &data, &n, NULL) == OWF_OK && data)
        s = fnv(FNV0, data, n) ^ (unsigned long)n;
    free(data);
    owf_pdf_free(pdf);
    return s;
}

/* the sums from main 3d40fb09, before the move (SHARED_GOLDEN_PRINT built against it on a 64-bit PC, as the
 * host tests run: the bands' sum takes an unsigned long's bytes) */
#define GOLDEN_PNG 0x26EC8768UL
#define GOLDEN_ILBM 0x89B17BC4UL
#define GOLDEN_BANDS 0x0A2DDB77UL
#define GOLDEN_PDF 0x5C6BC65DUL

#ifndef SHARED_GOLDEN_PRINT
static void test_unchanged(void)
{
    CHECK(picture_sum(0) == GOLDEN_PNG);
    CHECK(picture_sum(1) == GOLDEN_ILBM);
    CHECK(bands_sum() == GOLDEN_BANDS);
    CHECK(pdf_sum() == GOLDEN_PDF);
}

/* the old names are the new calls */
static void test_wrappers(void)
{
    static const int star[] = { 2000, 0, 2600, 1400, 4000, 1500, 2900, 2400, 3200, 4000, 2000, 3100, 800, 4000,
                                1100, 2400, 0, 1500, 1400, 1400 };
    static int a[2 * OWF_FILL_MAX_POINTS], b[2 * OWF_FILL_MAX_POINTS];
    int w = 33, h = 9, kind, band, m1, m2;
    unsigned char *rgb = card(w, h), *p1, *p2;
    size_t n1 = 0, n2 = 0;
    unsigned long c1, c2;
    p1 = owf_picture_png(rgb, w, h, (size_t)w * 3, &n1);
    p2 = opr_png_encode(rgb, w, h, (size_t)w * 3, &n2);
    CHECK(p1 && p2 && n1 == n2 && !memcmp(p1, p2, n1));
    free(p1);
    free(p2);
    p1 = owf_picture_ilbm(rgb, w, h, (size_t)w * 3, &n1);
    p2 = opr_ilbm_encode(rgb, w, h, (size_t)w * 3, &n2);
    CHECK(p1 && p2 && n1 == n2 && !memcmp(p1, p2, n1));
    free(p1);
    free(p2);
    free(rgb);
    CHECK(OWF_FILL_MAX_POINTS == OPR_GRADIENT_MAX_POINTS);
    for (kind = OWF_FILL_LINEAR; kind <= OWF_FILL_RADIAL; kind++)
        for (band = 0; band < 16; band++) {
            c1 = c2 = 1;
            m1 = owf_fill_gradient_band(star, 10, 0, 0, 4000, 4000, kind, 300, 0, 0xFFFFFF, band, 16, a,
                                        OWF_FILL_MAX_POINTS, &c1);
            m2 = opr_gradient_band(star, 10, 0, 0, 4000, 4000, kind, 300, 0, 0xFFFFFF, band, 16, b,
                                   OPR_GRADIENT_MAX_POINTS, &c2);
            CHECK(m1 == m2 && c1 == c2 && (m1 <= 0 || !memcmp(a, b, (size_t)m1 * 2 * sizeof a[0])));
        }
}

/* PNG's CRC is owf_crc32: the specification's check value, carried on as png_chunk does, and IEND's */
static void test_crc(void)
{
    static const unsigned char iend[] = "IEND", nine[] = "123456789";
    CHECK(owf_crc32(0, nine, 9) == 0xCBF43926UL);
    CHECK(owf_crc32(owf_crc32(0, nine, 4), nine + 4, 5) == 0xCBF43926UL);
    CHECK(owf_crc32(0, iend, 4) == 0xAE426082UL);
}

/* a 3 x 2 PNG, stored blocks: its rows are in it as they went in, behind filter byte 0 */
static void test_png_rows(void)
{
    static const unsigned char rgb[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 99, 99, 99,   /* stride 12: 3 bytes over */
                                         10, 11, 12, 13, 14, 15, 16, 17, 18, 99, 99, 99 };
    size_t n = 0;
    unsigned char *png = owf_picture_png(rgb, 3, 2, 12, &n);
    CHECK(png != NULL && n > 8 + 25 + 12);
    if (png) {
        static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
        /* IDAT's zlib stream: header, one final stored block of 20 bytes, the rows, Adler-32 */
        const unsigned char *z = png + 8 + 25 + 8;
        CHECK(!memcmp(png, sig, 8));
        CHECK(!memcmp(png + 12, "IHDR", 4) && png[16 + 3] == 3 && png[20 + 3] == 2);
        CHECK(png[24] == 8 && png[25] == 2);                                  /* 8-bit RGB */
        CHECK(!memcmp(png + 8 + 25 + 4, "IDAT", 4));
        CHECK(z[0] == 0x78 && z[2] == 1 && z[3] == 20 && z[4] == 0 && z[5] == (unsigned char)~20);
        CHECK(z[7] == 0 && !memcmp(z + 8, rgb, 9) && z[17] == 0 && !memcmp(z + 18, rgb + 12, 9));
        CHECK(!memcmp(png + n - 8, "IEND\xAE\x42\x60\x82", 8));
    }
    free(png);
    CHECK(owf_picture_png(rgb, 0, 2, 12, &n) == NULL);
    CHECK(owf_picture_png(rgb, 3, 2, 8, &n) == NULL);
    CHECK(owf_picture_png(rgb, OWF_PICTURE_MAX_SIDE + 1, 1, 0x10000000, &n) == NULL);
    CHECK(owf_picture_ilbm(NULL, 3, 2, 12, &n) == NULL);
    CHECK(owf_picture_ilbm(rgb, 3, 2, 12, NULL) == NULL);
}

/* the ILBM's header: 24 planes, ByteRun1, the size; FORM's length is the file's less 8 */
static void test_ilbm_header(void)
{
    static const unsigned char rgb[] = { 255, 0, 0, 0, 255, 0, 0, 0, 255 };
    size_t n = 0;
    unsigned char *f = owf_picture_ilbm(rgb, 3, 1, 9, &n);
    CHECK(f != NULL);
    if (f) {
        unsigned long form = ((unsigned long)f[4] << 24) | ((unsigned long)f[5] << 16) |
                             ((unsigned long)f[6] << 8) | f[7];
        CHECK(!memcmp(f, "FORM", 4) && !memcmp(f + 8, "ILBMBMHD", 8) && form == n - 8 && !(n & 1));
        CHECK(f[20] == 0 && f[21] == 3 && f[22] == 0 && f[23] == 1);          /* 3 x 1 */
        CHECK(f[28] == 24 && f[29] == 0 && f[30] == 1);                       /* planes, mask, compression */
        CHECK(!memcmp(f + 40, "BODY", 4));
    }
    free(f);
}

static void test_sine_and_blend(void)
{
    CHECK(owf_fill_sin16k(0) == 0);
    CHECK(owf_fill_sin16k(300) == 8192);
    CHECK(owf_fill_sin16k(900) == 16384);
    CHECK(owf_fill_sin16k(1800) == 0);
    CHECK(owf_fill_sin16k(2700) == -16384);
    CHECK(owf_fill_sin16k(-900) == -16384);
    CHECK(owf_fill_sin16k(3600 + 900) == 16384);
    CHECK(owf_fill_sin16k(904) == 16384 && owf_fill_sin16k(906) == owf_fill_sin16k(910));   /* nearest degree */
    CHECK(owf_fill_blend(0x000000, 0xFFFFFF, 0, 2) == 0x000000);
    CHECK(owf_fill_blend(0x000000, 0xFFFFFF, 2, 2) == 0xFFFFFF);
    CHECK(owf_fill_blend(0x000000, 0xFFFFFF, 1, 2) == 0x7F7F7F);
    CHECK(owf_fill_blend(0xFF0000, 0x0000FF, 1, 4) == 0xC0003F);   /* 255 - 255 / 4, 255 / 4: each part truncated */
}

/* a band's points stay inside the box, and the bands cover the shape: band 0 of a radial is the box's ellipse
 * cut to it, the last band the centre's colour */
static void test_bands(void)
{
    static const int box[] = { 0, 0, 4000, 0, 4000, 2000, 0, 2000 };
    static int out[2 * OWF_FILL_MAX_POINTS];
    unsigned long colour = 0;
    int m, i, inside = 1;
    m = owf_fill_gradient_band(box, 4, 0, 0, 4000, 2000, OWF_FILL_LINEAR, 0, 0x000000, 0xFFFFFF, 0, 4, out,
                               OWF_FILL_MAX_POINTS, &colour);
    CHECK(m >= 4 && colour == owf_fill_blend(0, 0xFFFFFF, 1, 8));
    for (i = 0; i < m; i++)
        if (out[i * 2] < 0 || out[i * 2] > 1001 || out[i * 2 + 1] < 0 || out[i * 2 + 1] > 2000)
            inside = 0;
    CHECK(inside);
    m = owf_fill_gradient_band(box, 4, 0, 0, 4000, 2000, OWF_FILL_RADIAL, 0, 0xFF0000, 0x0000FF, 0, 8, out,
                               OWF_FILL_MAX_POINTS, &colour);
    CHECK(m >= 4 && colour == owf_fill_blend(0xFF0000, 0x0000FF, 15, 16));   /* the box, its corners just cut */
    CHECK(owf_fill_gradient_band(box, 4, 0, 0, 4000, 2000, OWF_FILL_LINEAR, 0, 0, 0xFFFFFF, 0, 4, out, 2,
                                 &colour) == -1);
    CHECK(owf_fill_gradient_band(box, 2, 0, 0, 4000, 2000, OWF_FILL_LINEAR, 0, 0, 0xFFFFFF, 0, 4, out, 8,
                                 &colour) == 0);
    CHECK(owf_fill_gradient_band(box, OWF_FILL_MAX_OUTLINE + 1, 0, 0, 4000, 2000, OWF_FILL_LINEAR, 0, 0, 0, 0, 4,
                                 out, 8, &colour) == 0);
}
#endif

int main(void)
{
#ifdef SHARED_GOLDEN_PRINT
    printf("#define GOLDEN_PNG 0x%08lXUL\n#define GOLDEN_ILBM 0x%08lXUL\n#define GOLDEN_BANDS 0x%08lXUL\n"
           "#define GOLDEN_PDF 0x%08lXUL\n", picture_sum(0), picture_sum(1), bands_sum(), pdf_sum());
    return 0;
#else
    test_unchanged();
    test_wrappers();
    test_crc();
    test_png_rows();
    test_ilbm_header();
    test_sine_and_blend();
    test_bands();
    printf("test_owf_shared: %d checks, %d failed\n", checks, failed);
    return failed != 0;
#endif
}
