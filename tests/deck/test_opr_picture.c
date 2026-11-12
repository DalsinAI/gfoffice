/* P9: slides as pictures, the PNG and ILBM writers. */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static int checks, failed;
#define CHECK(c) do { checks++; if (!(c)) { failed++; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

/* A test card: a gradient with a red square, odd width so the rows don't line up. */
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

static void test_png(void)
{
    int w = 101, h = 37;
    unsigned char *rgb = card(w, h), *png, *idat = NULL, *raw;
    size_t n = 0, at = 8, idn = 0;
    uLongf rawn = (uLongf)(w * 3 + 1) * h;
    int ok_crc = 1, y;
    png = opr_png_encode(rgb, w, h, (size_t)w * 3, &n);
    CHECK(png != NULL);
    if (!png) { free(rgb); return; }
    CHECK(n > 8 && !memcmp(png, "\x89PNG\r\n\x1a\n", 8));
    CHECK(!memcmp(png + 12, "IHDR", 4) && be32(png + 16) == (unsigned long)w && be32(png + 20) == (unsigned long)h);
    CHECK(png[24] == 8 && png[25] == 2);
    while (at + 12 <= n) {
        unsigned long len = be32(png + at);
        unsigned long crc = crc32(0, png + at + 4, (uInt)(len + 4));
        if (crc != be32(png + at + 8 + len)) ok_crc = 0;
        if (!memcmp(png + at + 4, "IDAT", 4)) {
            idat = realloc(idat, idn + len);
            memcpy(idat + idn, png + at + 8, len);
            idn += len;
        }
        if (!memcmp(png + at + 4, "IEND", 4)) { at += 12 + len; break; }
        at += 12 + len;
    }
    CHECK(ok_crc);
    CHECK(at == n);
    raw = malloc(rawn);
    CHECK(idat && uncompress(raw, &rawn, idat, (uLong)idn) == Z_OK);
    CHECK(rawn == (uLongf)(w * 3 + 1) * h);
    for (y = 0; y < h; y++)
        if (raw[(size_t)y * (w * 3 + 1)] != 0 || memcmp(raw + (size_t)y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3)) break;
    CHECK(y == h);
    free(raw);
    free(idat);
    free(png);
    /* stride wider than the row: the padding is not written */
    {
        unsigned char *wide = calloc((size_t)(w * 3 + 9) * h, 1), *png2;
        size_t n2 = 0;
        for (y = 0; y < h; y++) memcpy(wide + (size_t)y * (w * 3 + 9), rgb + (size_t)y * w * 3, (size_t)w * 3);
        png2 = opr_png_encode(wide, w, h, (size_t)w * 3 + 9, &n2);
        png = opr_png_encode(rgb, w, h, (size_t)w * 3, &n);
        CHECK(png2 && png && n2 == n && !memcmp(png, png2, n));
        free(png2);
        free(png);
        free(wide);
    }
    CHECK(opr_png_encode(rgb, 0, h, (size_t)w * 3, &n) == NULL);
    CHECK(opr_png_encode(rgb, w, h, 3, &n) == NULL);
    CHECK(opr_png_encode(NULL, w, h, (size_t)w * 3, &n) == NULL);
    free(rgb);
}

/* Unpack one ByteRun1 row; returns bytes read, 0 on a bad stream. */
static size_t unrun(const unsigned char *p, size_t avail, unsigned char *out, size_t want)
{
    size_t i = 0, o = 0;
    while (o < want) {
        signed char c;
        if (i >= avail) return 0;
        c = (signed char)p[i++];
        if (c >= 0) {
            size_t k = (size_t)c + 1;
            if (i + k > avail || o + k > want) return 0;
            memcpy(out + o, p + i, k);
            i += k; o += k;
        } else if (c != -128) {
            size_t k = (size_t)(1 - c);
            if (i >= avail || o + k > want) return 0;
            memset(out + o, p[i++], k);
            o += k;
        }
    }
    return i;
}

static void test_ilbm(void)
{
    int w = 101, h = 37, x, y, c, b, bad = 0;
    size_t n = 0, rowbytes = (size_t)((w + 15) / 16) * 2, at, body_len;
    unsigned char *rgb = card(w, h), *ilbm = opr_ilbm_encode(rgb, w, h, (size_t)w * 3, &n), *plane = malloc(rowbytes);
    const unsigned char *body;
    CHECK(ilbm != NULL);
    if (!ilbm) { free(rgb); free(plane); return; }
    CHECK(!memcmp(ilbm, "FORM", 4) && be32(ilbm + 4) == n - 8 && !memcmp(ilbm + 8, "ILBM", 4));
    CHECK(!memcmp(ilbm + 12, "BMHD", 4) && be32(ilbm + 16) == 20);
    CHECK(ilbm[20] == 0 && ilbm[21] == w && ilbm[22] == 0 && ilbm[23] == h);
    CHECK(ilbm[28] == 24 && ilbm[29] == 0 && ilbm[30] == 1);
    CHECK(!memcmp(ilbm + 40, "BODY", 4));
    body_len = be32(ilbm + 44);
    body = ilbm + 48;
    CHECK(48 + body_len + (body_len & 1) == n);
    at = 0;
    for (y = 0; y < h && !bad; y++)
        for (c = 0; c < 3 && !bad; c++)
            for (b = 0; b < 8 && !bad; b++) {
                size_t used = unrun(body + at, body_len - at, plane, rowbytes);
                if (!used) { bad = 1; break; }
                at += used;
                for (x = 0; x < w; x++) {
                    int bit = (plane[x >> 3] >> (7 - (x & 7))) & 1;
                    if (bit != ((rgb[((size_t)y * w + x) * 3 + c] >> b) & 1)) { bad = 1; break; }
                }
            }
    CHECK(!bad);
    CHECK(at == body_len);
    free(ilbm);
    free(plane);
    free(rgb);
}

static void test_names(void)
{
    char out[64];
    CHECK(opr_picture_format_for("RAM:Talk.png") == OPR_PICTURE_PNG);
    CHECK(opr_picture_format_for("RAM:Talk.IFF") == OPR_PICTURE_ILBM);
    CHECK(opr_picture_format_for("Work:Pics/talk.ilbm") == OPR_PICTURE_ILBM);
    CHECK(opr_picture_format_for("Work:Pics/talk") == OPR_PICTURE_PNG);
    CHECK(opr_picture_name(out, sizeof out, "RAM:Talk.png", 2, 12) == 0 && !strcmp(out, "RAM:Talk-03.png"));
    CHECK(opr_picture_name(out, sizeof out, "RAM:Talk.png", 0, 3) == 0 && !strcmp(out, "RAM:Talk-01.png"));
    CHECK(opr_picture_name(out, sizeof out, "Work:a.b/Talk", 9, 120) == 0 && !strcmp(out, "Work:a.b/Talk-010"));
    CHECK(opr_picture_name(out, sizeof out, "Work:Pics.dir/x.iff", 0, 1) == 0 && !strcmp(out, "Work:Pics.dir/x-01.iff"));
    CHECK(opr_picture_name(out, 8, "RAM:Talk.png", 0, 1) == -1);
}

static void test_write(void)
{
    int w = 20, h = 10;
    unsigned char *rgb = card(w, h);
    const char *path = "test-opr-picture.png";
    FILE *f;
    long len = 0;
    CHECK(opr_picture_write(path, OPR_PICTURE_PNG, rgb, w, h, (size_t)w * 3) == 0);
    f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) { fseek(f, 0, SEEK_END); len = ftell(f); fclose(f); }
    CHECK(len > 50);
    remove(path);
    CHECK(opr_picture_write("no/such/drawer/x.png", OPR_PICTURE_PNG, rgb, w, h, (size_t)w * 3) == -2);
    CHECK(opr_picture_write("", OPR_PICTURE_PNG, rgb, w, h, (size_t)w * 3) == -1);
    free(rgb);
}

int main(void)
{
    test_png();
    test_ilbm();
    test_names();
    test_write();
    printf("test_opr_picture: %d checks, %d failed\n", checks, failed);
    return failed != 0;
}
