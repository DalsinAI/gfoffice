/* Slides as pictures (P9): PNG and IFF ILBM writers for 24-bit RGB rows.
 *
 * The app paints a slide into an off-screen bitmap and reads its pixels back;
 * these turn the rows into a file. PNG is deflated with zlib when it is built
 * in (OWF_HAVE_ZLIB) and written with stored blocks otherwise; ILBM is a
 * 24-plane BODY packed with ByteRun1, which every Amiga datatype reads. */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef OWF_HAVE_ZLIB
#include "owf_zlib.h"
#endif

typedef struct {
    unsigned char *data;
    size_t len, cap;
    int failed;
} pic_out;

static void out_bytes(pic_out *o, const void *p, size_t n)
{
    if (o->failed) return;
    if (o->len + n > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        unsigned char *d;
        while (cap < o->len + n) cap *= 2;
        d = realloc(o->data, cap);
        if (!d) { o->failed = 1; return; }
        o->data = d;
        o->cap = cap;
    }
    memcpy(o->data + o->len, p, n);
    o->len += n;
}

static void out_be32(pic_out *o, unsigned long v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v >> 24); b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8); b[3] = (unsigned char)v;
    out_bytes(o, b, 4);
}

static void out_be16(pic_out *o, unsigned v)
{
    unsigned char b[2];
    b[0] = (unsigned char)(v >> 8); b[1] = (unsigned char)v;
    out_bytes(o, b, 2);
}

static unsigned long png_crc(unsigned long crc, const unsigned char *p, size_t n)
{
    static unsigned long table[256];
    static int ready;
    size_t i;
    if (!ready) {
        unsigned long k, c;
        int b;
        for (k = 0; k < 256; k++) {
            c = k;
            for (b = 0; b < 8; b++) c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
        ready = 1;
    }
    crc ^= 0xffffffffUL;
    for (i = 0; i < n; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffUL;
}

static void png_chunk(pic_out *o, const char *type, const unsigned char *data, size_t n)
{
    unsigned long crc;
    out_be32(o, (unsigned long)n);
    out_bytes(o, type, 4);
    if (n) out_bytes(o, data, n);
    crc = png_crc(0, (const unsigned char *)type, 4);
    if (n) crc = png_crc(crc, data, n);
    out_be32(o, crc);
}

static unsigned long adler(const unsigned char *p, size_t n)
{
    unsigned long a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521UL;
        b = (b + a) % 65521UL;
    }
    return (b << 16) | a;
}

/* The filtered rows (filter byte 0 then RGB) as a zlib stream: deflated when
 * zlib (z.library on the Amiga) is there, stored blocks otherwise. */
static unsigned char *png_idat(const unsigned char *raw, size_t n, size_t *out_len)
{
    size_t blocks = n / 65535 + 1, at = 0, k = 2, b;
    unsigned char *z;
    unsigned long a;
#ifdef OWF_HAVE_ZLIB
    {
        z_stream zs;
        uLong bound;
        unsigned char *packed = NULL;
        memset(&zs, 0, sizeof zs);
        if (deflateInit2(&zs, 6, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) == Z_OK) {
            bound = deflateBound(&zs, (uLong)n);
            /* as owf_zip.c: a bound far from the length is a z.library answering another call */
            if (bound >= (uLong)n && bound - (uLong)n <= (uLong)n / 2 + 4096)
                packed = malloc(bound);
            if (packed) {
                zs.next_in = (Bytef *)raw;
                zs.avail_in = (uInt)n;
                zs.next_out = packed;
                zs.avail_out = (uInt)bound;
                if (deflate(&zs, Z_FINISH) == Z_STREAM_END) {
                    *out_len = zs.total_out;
                    deflateEnd(&zs);
                    return packed;
                }
                free(packed);
            }
            deflateEnd(&zs);
        }
    }
#endif
    z = malloc(n + blocks * 5 + 6);
    if (!z) return NULL;
    z[0] = 0x78; z[1] = 0x01;
    for (b = 0; b < blocks; b++) {
        size_t m = n - at > 65535 ? 65535 : n - at;
        z[k++] = (unsigned char)(b + 1 == blocks);
        z[k++] = (unsigned char)m; z[k++] = (unsigned char)(m >> 8);
        z[k++] = (unsigned char)~m; z[k++] = (unsigned char)(~m >> 8);
        memcpy(z + k, raw + at, m);
        k += m; at += m;
    }
    a = adler(raw, n);
    z[k++] = (unsigned char)(a >> 24); z[k++] = (unsigned char)(a >> 16);
    z[k++] = (unsigned char)(a >> 8); z[k++] = (unsigned char)a;
    *out_len = k;
    return z;
}

unsigned char *opr_png_encode(const unsigned char *rgb, int width, int height, size_t stride, size_t *length)
{
    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    unsigned char ihdr[13], *raw, *z;
    size_t row = (size_t)width * 3, n, zn = 0;
    pic_out o;
    int y;
    if (!rgb || width < 1 || height < 1 || width > 16384 || height > 16384 || stride < row || !length) return NULL;
    n = (row + 1) * (size_t)height;
    raw = malloc(n);
    if (!raw) return NULL;
    for (y = 0; y < height; y++) {
        raw[(row + 1) * (size_t)y] = 0;
        memcpy(raw + (row + 1) * (size_t)y + 1, rgb + stride * (size_t)y, row);
    }
    z = png_idat(raw, n, &zn);
    free(raw);
    if (!z) return NULL;
    memset(&o, 0, sizeof o);
    out_bytes(&o, sig, 8);
    ihdr[0] = (unsigned char)(width >> 24); ihdr[1] = (unsigned char)(width >> 16);
    ihdr[2] = (unsigned char)(width >> 8); ihdr[3] = (unsigned char)width;
    ihdr[4] = (unsigned char)(height >> 24); ihdr[5] = (unsigned char)(height >> 16);
    ihdr[6] = (unsigned char)(height >> 8); ihdr[7] = (unsigned char)height;
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;   /* 8-bit RGB */
    png_chunk(&o, "IHDR", ihdr, 13);
    png_chunk(&o, "IDAT", z, zn);
    png_chunk(&o, "IEND", NULL, 0);
    free(z);
    if (o.failed) { free(o.data); return NULL; }
    *length = o.len;
    return o.data;
}

/* One plane row (bytes) packed with ByteRun1. */
static void byterun1(pic_out *o, const unsigned char *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && p[i + run] == p[i]) run++;
        if (run >= 3) {
            unsigned char c = (unsigned char)(257 - run);
            out_bytes(o, &c, 1);
            out_bytes(o, p + i, 1);
            i += run;
        } else {
            size_t lit = 0;
            unsigned char c;
            while (i + lit < n && lit < 128) {
                if (i + lit + 2 < n && p[i + lit] == p[i + lit + 1] && p[i + lit] == p[i + lit + 2]) break;
                lit++;
            }
            c = (unsigned char)(lit - 1);
            out_bytes(o, &c, 1);
            out_bytes(o, p + i, lit);
            i += lit;
        }
    }
}

unsigned char *opr_ilbm_encode(const unsigned char *rgb, int width, int height, size_t stride, size_t *length)
{
    size_t rowbytes = (size_t)((width + 15) / 16) * 2, body_at, form_at;
    unsigned char *plane;
    pic_out o;
    int y, c, b, x;
    if (!rgb || width < 1 || height < 1 || width > 16384 || height > 16384 || stride < (size_t)width * 3 || !length)
        return NULL;
    plane = malloc(rowbytes);
    if (!plane) return NULL;
    memset(&o, 0, sizeof o);
    out_bytes(&o, "FORM", 4);
    form_at = o.len;
    out_be32(&o, 0);
    out_bytes(&o, "ILBM", 4);
    out_bytes(&o, "BMHD", 4);
    out_be32(&o, 20);
    out_be16(&o, (unsigned)width); out_be16(&o, (unsigned)height);
    out_be16(&o, 0); out_be16(&o, 0);                  /* x, y */
    out_bytes(&o, "\x18\x00\x01\x00", 4);              /* 24 planes, no mask, ByteRun1, pad */
    out_be16(&o, 0);                                   /* transparent colour */
    out_bytes(&o, "\x01\x01", 2);                      /* aspect */
    out_be16(&o, (unsigned)width); out_be16(&o, (unsigned)height);
    out_bytes(&o, "BODY", 4);
    body_at = o.len;
    out_be32(&o, 0);
    for (y = 0; y < height && !o.failed; y++) {
        const unsigned char *row = rgb + stride * (size_t)y;
        for (c = 0; c < 3; c++)                         /* red planes 0-7, green 8-15, blue 16-23 */
            for (b = 0; b < 8; b++) {
                memset(plane, 0, rowbytes);
                for (x = 0; x < width; x++)
                    if (row[x * 3 + c] & (1 << b)) plane[x >> 3] |= (unsigned char)(0x80 >> (x & 7));
                byterun1(&o, plane, rowbytes);
            }
    }
    free(plane);
    if (o.failed) { free(o.data); return NULL; }
    {
        size_t body = o.len - body_at - 4;
        if (body & 1) out_bytes(&o, "", 1);
        if (o.failed) { free(o.data); return NULL; }
        o.data[body_at] = (unsigned char)(body >> 24); o.data[body_at + 1] = (unsigned char)(body >> 16);
        o.data[body_at + 2] = (unsigned char)(body >> 8); o.data[body_at + 3] = (unsigned char)body;
        body = o.len - form_at - 4;
        o.data[form_at] = (unsigned char)(body >> 24); o.data[form_at + 1] = (unsigned char)(body >> 16);
        o.data[form_at + 2] = (unsigned char)(body >> 8); o.data[form_at + 3] = (unsigned char)body;
    }
    *length = o.len;
    return o.data;
}

static int ends_ci(const char *s, const char *ext)
{
    size_t n = strlen(s), e = strlen(ext), i;
    if (n <= e) return 0;
    for (i = 0; i < e; i++)
        if (tolower((unsigned char)s[n - e + i]) != tolower((unsigned char)ext[i])) return 0;
    return 1;
}

int opr_picture_format_for(const char *path)
{
    if (path && (ends_ci(path, ".iff") || ends_ci(path, ".ilbm"))) return OPR_PICTURE_ILBM;
    return OPR_PICTURE_PNG;
}

int opr_picture_write(const char *path, int format, const unsigned char *rgb, int width, int height, size_t stride)
{
    size_t n = 0;
    unsigned char *data;
    FILE *f;
    int ok;
    if (!path || !*path) return -1;
    data = format == OPR_PICTURE_ILBM ? opr_ilbm_encode(rgb, width, height, stride, &n)
                                      : opr_png_encode(rgb, width, height, stride, &n);
    if (!data) return -1;
    f = fopen(path, "wb");
    if (!f) { free(data); return -2; }
    ok = fwrite(data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    free(data);
    return ok ? 0 : -2;
}

/* "Talk.png" and slide 3 of 12 -> "Talk-03.png": the number at least two wide, before the extension. */
int opr_picture_name(char *out, size_t size, const char *path, int index, int count)
{
    const char *dot = NULL, *p;
    int digits = 1, c = count, n;
    if (!out || !size || !path || index < 0) return -1;
    for (p = path; *p; p++) {
        if (*p == '.') dot = p;
        else if (*p == '/' || *p == ':') dot = NULL;
    }
    while (c >= 10) { c /= 10; digits++; }
    if (digits < 2) digits = 2;
    if (dot)
        n = snprintf(out, size, "%.*s-%0*d%s", (int)(dot - path), path, digits, index + 1, dot);
    else
        n = snprintf(out, size, "%s-%0*d", path, digits, index + 1);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}
