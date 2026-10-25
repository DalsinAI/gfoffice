/* ogt_iconfile: reads an icon strip (an ILBM, see ogt_iconfile.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogt_iconfile.h"

static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }
static unsigned be16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }

static int fail(char *err, size_t errlen, const char *why)
{
    if (err && errlen) snprintf(err, errlen, "%s", why);
    return 0;
}

/* ByteRun1 into out (n bytes); returns the bytes of data used, or 0 on bad data. */
static size_t unpack(const unsigned char *data, size_t len, unsigned char *out, size_t n)
{
    size_t i = 0, o = 0;
    while (o < n) {
        int c;
        if (i >= len) return 0;
        c = data[i++];
        if (c < 128) {
            size_t k = (size_t)c + 1;
            if (i + k > len || o + k > n) return 0;
            memcpy(out + o, data + i, k);
            i += k; o += k;
        } else if (c > 128) {
            size_t k = 257 - (size_t)c;
            if (i >= len || o + k > n) return 0;
            memset(out + o, data[i++], k);
            o += k;
        }
    }
    return i;
}

void ogt_iconfile_free(ogt_iconfile *f)
{
    free(f->pix);
    free(f->names);
    memset(f, 0, sizeof *f);
}

int ogt_iconfile_parse(ogt_iconfile *f, const unsigned char *data, size_t len, char *err, size_t errlen)
{
    const unsigned char *body = NULL, *bmhd = NULL;
    size_t pos, end, body_len = 0;
    int w = 0, h = 0, planes = 0, comp = 0, have_dis = 0, ncmap = 0, i;
    unsigned char *row = NULL;
    memset(f, 0, sizeof *f);
    if (len < 12 || memcmp(data, "FORM", 4) || memcmp(data + 8, "ILBM", 4)) return fail(err, errlen, "not an ILBM picture");
    end = 8 + be32(data + 4);
    if (end > len) end = len;
    for (pos = 12; pos + 8 <= end; ) {
        const unsigned char *c = data + pos;
        size_t n = be32(c + 4);
        if (pos + 8 + n > end) return fail(err, errlen, "a chunk runs past the end");
        if (!memcmp(c, "BMHD", 4) && n >= 20) bmhd = c + 8;
        else if (!memcmp(c, "CMAP", 4)) {
            ncmap = (int)(n / 3) > 32 ? 32 : (int)(n / 3);
            for (i = 0; i < ncmap; i++) { f->normal[i].r = c[8 + 3 * i]; f->normal[i].g = c[9 + 3 * i]; f->normal[i].b = c[10 + 3 * i]; }
        } else if (!memcmp(c, "OGTD", 4)) {
            int k = (int)(n / 3) > 32 ? 32 : (int)(n / 3);
            for (i = 0; i < k; i++) { f->disabled[i].r = c[8 + 3 * i]; f->disabled[i].g = c[9 + 3 * i]; f->disabled[i].b = c[10 + 3 * i]; }
            have_dis = 1;
        } else if (!memcmp(c, "OGTN", 4) && n >= 4) {
            size_t k;
            int found = 0;
            f->size = (int)be16(c + 8);
            f->count = (int)be16(c + 10);
            if (f->count > OGT_ICONFILE_MAX) return fail(err, errlen, "too many icons in one strip");
            if (!(f->names = malloc(n - 4 + 1))) return fail(err, errlen, "out of memory");
            memcpy(f->names, c + 12, n - 4);
            f->names[n - 4] = 0;
            for (k = 0; k < n - 4 && found < f->count; ) {
                f->name[found++] = f->names + k;
                k += strlen(f->names + k) + 1;
            }
            if (found < f->count) { ogt_iconfile_free(f); return fail(err, errlen, "fewer names than icons"); }
        } else if (!memcmp(c, "BODY", 4)) { body = c + 8; body_len = n; }
        pos += 8 + n + (n & 1);
    }
    if (!bmhd || !body) { ogt_iconfile_free(f); return fail(err, errlen, "no BMHD or no BODY"); }
    w = (int)be16(bmhd); h = (int)be16(bmhd + 2);
    planes = bmhd[8]; comp = bmhd[10];
    if (planes != 3 && planes != 5) { ogt_iconfile_free(f); return fail(err, errlen, "not 8 or 32 colours"); }
    if (comp > 1 || bmhd[9] == 1) { ogt_iconfile_free(f); return fail(err, errlen, "an unknown compression or a mask plane"); }
    if (!f->names || f->size <= 0 || h != f->size || w != f->size * f->count) { ogt_iconfile_free(f); return fail(err, errlen, "the names don't match the picture"); }
    f->colours = 1 << planes;
    if (ncmap < f->colours) { ogt_iconfile_free(f); return fail(err, errlen, "the palette is short"); }
    if (!have_dis)                                         /* greyed toward colour 0, as the generator's rule */
        for (i = 0; i < 32; i++) {
            int l = (f->normal[i].r * 77 + f->normal[i].g * 151 + f->normal[i].b * 28) >> 8;
            f->disabled[i].r = (unsigned char)(f->normal[0].r + (l - f->normal[0].r) * 42 / 100);
            f->disabled[i].g = (unsigned char)(f->normal[0].g + (l - f->normal[0].g) * 42 / 100);
            f->disabled[i].b = (unsigned char)(f->normal[0].b + (l - f->normal[0].b) * 42 / 100);
        }
    f->stride = w;
    {
        int rowbytes = ((w + 15) >> 4) << 1, y, p, x;
        size_t used = 0;
        if (!(f->pix = calloc((size_t)w * h, 1)) || !(row = malloc((size_t)rowbytes))) {
            free(row); ogt_iconfile_free(f);
            return fail(err, errlen, "out of memory");
        }
        for (y = 0; y < h; y++) {
            unsigned char *out = f->pix + (size_t)y * w;
            for (p = 0; p < planes; p++) {
                if (comp) {
                    size_t k = unpack(body + used, body_len - used, row, (size_t)rowbytes);
                    if (!k) { free(row); ogt_iconfile_free(f); return fail(err, errlen, "the picture's data is damaged"); }
                    used += k;
                } else {
                    if (used + (size_t)rowbytes > body_len) { free(row); ogt_iconfile_free(f); return fail(err, errlen, "the picture is short"); }
                    memcpy(row, body + used, (size_t)rowbytes);
                    used += (size_t)rowbytes;
                }
                for (x = 0; x < w; x++)
                    if (row[x >> 3] & (0x80 >> (x & 7))) out[x] |= (unsigned char)(1 << p);
            }
        }
        free(row);
    }
    return 1;
}

int ogt_iconfile_find(const ogt_iconfile *f, const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < f->count; i++) if (!strcmp(f->name[i], name)) return i;
    return -1;
}

const unsigned char *ogt_iconfile_pixels(const ogt_iconfile *f, int index)
{
    return f->pix + (size_t)index * f->size;
}
