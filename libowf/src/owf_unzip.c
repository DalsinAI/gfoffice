/*
 * libowf: reading zip archives (ODT, DOCX). Stored files always; deflated
 * ones when zlib is built in (OWF_HAVE_ZLIB), which is every real ODT and
 * DOCX. Sizes are capped and CRCs checked, so a damaged or hostile archive
 * gives an error, not a crash or a flood of memory.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef OWF_HAVE_ZLIB
#include <zlib.h>
#endif

#define LE16(p) ((unsigned)((p)[0] | ((p)[1] << 8)))
#define LE32(p) ((unsigned long)(p)[0] | ((unsigned long)(p)[1] << 8) | \
                 ((unsigned long)(p)[2] << 16) | ((unsigned long)(p)[3] << 24))

/* No part of a document we read is bigger than this. */
#define MAX_PART (64UL * 1024 * 1024)

int owf_unzip_open(owf_unzip *zip, const unsigned char *data, size_t length)
{
    size_t i, stop;
    unsigned long dir_size, dir_offset;

    if (length < 22 || data[0] != 'P' || data[1] != 'K')
        return 0;
    /* The end record: the last 22 bytes, or earlier with a comment. */
    stop = length > 22 + 65535 ? length - 22 - 65535 : 0;
    for (i = length - 22;; i--) {
        if (LE32(data + i) == 0x06054B50UL) {
            zip->entries = LE16(data + i + 10);
            dir_size = LE32(data + i + 12);
            dir_offset = LE32(data + i + 16);
            if (dir_offset > length || dir_size > length - dir_offset)
                return 0;
            zip->data = data;
            zip->length = length;
            zip->dir = data + dir_offset;
            return 1;
        }
        if (i == stop)
            return 0;
    }
}

/* The central directory entry for name, or NULL. */
static const unsigned char *find(const owf_unzip *zip, const char *name)
{
    const unsigned char *p = zip->dir, *end = zip->data + zip->length;
    size_t namelen = strlen(name);
    unsigned i;

    for (i = 0; i < zip->entries; i++) {
        unsigned n, extra, comment;
        if (end - p < 46 || LE32(p) != 0x02014B50UL)
            return NULL;
        n = LE16(p + 28);
        extra = LE16(p + 30);
        comment = LE16(p + 32);
        if ((size_t)(end - p) < 46 + (size_t)n + extra + comment)
            return NULL;
        if (n == namelen && memcmp(p + 46, name, namelen) == 0)
            return p;
        p += 46 + n + extra + comment;
    }
    return NULL;
}

int owf_unzip_has(const owf_unzip *zip, const char *name)
{
    return find(zip, name) != NULL;
}

int owf_unzip_read(const owf_unzip *zip, const char *name, unsigned char **out, size_t *out_length)
{
    const unsigned char *e = find(zip, name), *local, *body;
    unsigned method, flags;
    unsigned long crc, packed, size, offset;
    unsigned char *data;

    *out = NULL;
    *out_length = 0;
    if (!e)
        return OWF_ERR_FORMAT;
    flags = LE16(e + 8);
    method = LE16(e + 10);
    crc = LE32(e + 16);
    packed = LE32(e + 20);
    size = LE32(e + 24);
    offset = LE32(e + 42);
    if (flags & 1)
        return OWF_ERR_UNSUPPORTED;      /* encrypted */
    if (size > MAX_PART || offset > zip->length || zip->length - offset < 30)
        return OWF_ERR_CORRUPT;
    local = zip->data + offset;
    if (LE32(local) != 0x04034B50UL)
        return OWF_ERR_CORRUPT;
    body = local + 30 + LE16(local + 26) + LE16(local + 28);
    if (body > zip->data + zip->length || packed > (unsigned long)(zip->data + zip->length - body))
        return OWF_ERR_CORRUPT;
    data = malloc(size + 1);
    if (!data)
        return OWF_ERR_MEMORY;

    if (method == 0) {
        if (packed != size) {
            free(data);
            return OWF_ERR_CORRUPT;
        }
        memcpy(data, body, size);
    } else if (method == 8) {
#ifdef OWF_HAVE_ZLIB
        z_stream z;
        int r;
        memset(&z, 0, sizeof z);
        if (inflateInit2(&z, -15) != Z_OK) {
            free(data);
            return OWF_ERR_MEMORY;
        }
        z.next_in = (Bytef *)body;
        z.avail_in = (uInt)packed;
        z.next_out = data;
        z.avail_out = (uInt)size;
        r = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        if (r != Z_STREAM_END || z.total_out != size) {
            free(data);
            return OWF_ERR_CORRUPT;
        }
#else
        free(data);
        return OWF_ERR_UNSUPPORTED;
#endif
    } else {
        free(data);
        return OWF_ERR_UNSUPPORTED;
    }
    if (owf_crc32(0, data, size) != crc) {
        free(data);
        return OWF_ERR_CORRUPT;
    }
    data[size] = 0;
    *out = data;
    *out_length = size;
    return OWF_OK;
}
