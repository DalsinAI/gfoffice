/*
 * owf_dpng.c: a diagram inside a PNG (owf_diagram.h): put in as a tEXt chunk keyed "mxfile" holding the
 * URI-encoded mxfile, read back from that or from a zTXt chunk. Integer arithmetic only.
 * Written from published descriptions, not from any program's code: W3C's PNG specification (second
 * edition) for chunks, their CRC and tEXt and zTXt; RFC 1950 for zTXt's zlib stream; diagrams.net's
 * documentation on editable PNG exports for the keyword and its URI encoding.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_diag_int.h"

#include <string.h>

#define KEY "mxfile"
#define MAX_CHUNK 0x7fffffffUL           /* PNG's own limit on a chunk's length */

static const unsigned char signature[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };

static unsigned long crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xedb88320UL ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

/* PNG's CRC over a chunk's type and data. */
static unsigned long crc(const unsigned char *type, const unsigned char *data, size_t n)
{
    unsigned long c = 0xffffffffUL;
    size_t i;
    if (!crc_ready)
        crc_init();
    for (i = 0; i < 4; i++)
        c = crc_table[(c ^ type[i]) & 0xff] ^ (c >> 8);
    for (i = 0; i < n; i++)
        c = crc_table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return (c ^ 0xffffffffUL) & 0xffffffffUL;
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
}

static void put_be32(owf_buf *b, unsigned long v)
{
    owf_buf_putc(b, (int)((v >> 24) & 0xff));
    owf_buf_putc(b, (int)((v >> 16) & 0xff));
    owf_buf_putc(b, (int)((v >> 8) & 0xff));
    owf_buf_putc(b, (int)(v & 0xff));
}

/* The next chunk at *at: its type and data. 1 when there is one, 0 at the end, -1 when damaged. */
static int next_chunk(const unsigned char *png, size_t n, size_t *at, const unsigned char **type,
                      const unsigned char **data, size_t *len)
{
    unsigned long l;
    if (*at == n)
        return 0;
    if (n - *at < 12)
        return -1;
    l = be32(png + *at);
    if (l > MAX_CHUNK || l > n - *at - 12)
        return -1;
    *type = png + *at + 4;
    *data = png + *at + 8;
    *len = (size_t)l;
    if (crc(*type, *data, *len) != be32(*data + *len))
        return -1;
    *at += 12 + (size_t)l;
    return 1;
}

/* A tEXt or zTXt chunk keyed "mxfile". */
static int is_ours(const unsigned char *type, const unsigned char *data, size_t len)
{
    return (!memcmp(type, "tEXt", 4) || !memcmp(type, "zTXt", 4)) && len > sizeof KEY - 1 &&
           !memcmp(data, KEY, sizeof KEY);   /* the keyword and its NUL */
}

int owf_dpng_embed(const unsigned char *png, size_t png_length, const owf_diagram *d, unsigned char **data,
                   size_t *length)
{
    owf_buf out, text;
    unsigned char *mx = NULL;
    size_t mxn = 0, at = sizeof signature, len;
    const unsigned char *type, *cdata;
    int r, put = 0;

    if (png_length < sizeof signature || memcmp(png, signature, sizeof signature))
        return OWF_ERR_FORMAT;
    r = owf_drawio_write(d, 0, &mx, &mxn);
    if (r != OWF_OK)
        return r;
    while (mxn && mx[mxn - 1] == '\n')
        mxn--;
    owf_buf_init(&text);
    owf_buf_put(&text, KEY, sizeof KEY);     /* with its NUL */
    owf_diag_uri_encode(&text, mx, mxn);
    owf_free(mx);
    owf_buf_init(&out);
    owf_buf_put(&out, signature, sizeof signature);
    while ((r = next_chunk(png, png_length, &at, &type, &cdata, &len)) > 0) {
        if (is_ours(type, cdata, len))
            continue;                    /* the diagram it held */
        owf_buf_put(&out, cdata - 8, len + 12);
        if (!put && !memcmp(type, "IHDR", 4)) {
            put_be32(&out, (unsigned long)text.length);
            owf_buf_put(&out, "tEXt", 4);
            owf_buf_put(&out, text.data, text.length);
            put_be32(&out, text.failed ? 0 : crc((const unsigned char *)"tEXt", text.data, text.length));
            put = 1;
        }
    }
    if (text.failed)
        out.failed = 1;
    owf_buf_free(&text);
    if (r < 0 || !put) {
        owf_buf_free(&out);
        return OWF_ERR_FORMAT;
    }
    return owf_buf_take(&out, data, length);
}

int owf_dpng_read(const unsigned char *png, size_t length, owf_diagram *d)
{
    size_t at = sizeof signature, len;
    const unsigned char *type, *cdata;
    int r;

    if (length < sizeof signature || memcmp(png, signature, sizeof signature))
        return OWF_ERR_FORMAT;
    while ((r = next_chunk(png, length, &at, &type, &cdata, &len)) > 0) {
        const unsigned char *body = cdata + sizeof KEY;
        size_t n = len - sizeof KEY;
        unsigned char *xml = NULL;
        size_t xn = 0;
        if (!is_ours(type, cdata, len))
            continue;
        if (!memcmp(type, "zTXt", 4)) {
            /* a method byte (0: zlib), then a zlib stream: a two-byte header, raw DEFLATE, Adler-32 */
            if (n < 7 || body[0] != 0 || (body[1] & 0x0f) != 8 || ((body[1] << 8) | body[2]) % 31)
                return OWF_ERR_CORRUPT;
            r = owf_diag_inflate_any(body + 3, n - 3, &xml, &xn);
            if (r != OWF_OK)
                return r;
        } else {
            xml = owf_malloc(n + 1);
            if (!xml)
                return OWF_ERR_MEMORY;
            memcpy(xml, body, n);
            xml[n] = 0;
            xn = n;
        }
        if (xn && xml[0] == '%')
            xn = owf_diag_uri_decode((char *)xml, xn);
        r = owf_drawio_read(xml, xn, d);
        owf_free(xml);
        return r;
    }
    return r < 0 ? OWF_ERR_CORRUPT : OWF_ERR_FORMAT;
}
