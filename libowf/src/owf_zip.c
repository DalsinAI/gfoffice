/*
 * libowf: writing zip archives, the container of ODT and DOCX. Files are
 * deflated with zlib when it is built in (OWF_HAVE_ZLIB), stored otherwise;
 * both are valid.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef OWF_HAVE_ZLIB
#include <zlib.h>
#endif

typedef struct {
    char *name;
    unsigned long crc, compressed, size, offset;
    int method;
} entry;

struct owf_zip {
    owf_buf *out;
    entry *entries;
    int count, capacity;
    unsigned dos_time, dos_date;
    int failed;
};

unsigned long owf_crc32(unsigned long crc, const unsigned char *data, size_t length)
{
    static unsigned long table[256];
    static int ready;
    size_t i;

    if (!ready) {
        unsigned long n, k, c;
        for (n = 0; n < 256; n++) {
            c = n;
            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = 1;
    }
    crc = ~crc & 0xFFFFFFFFUL;
    for (i = 0; i < length; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc & 0xFFFFFFFFUL;
}

static void put16(owf_buf *b, unsigned v)
{
    unsigned char x[2];
    x[0] = (unsigned char)(v & 0xFF);
    x[1] = (unsigned char)((v >> 8) & 0xFF);
    owf_buf_put(b, x, 2);
}

static void put32(owf_buf *b, unsigned long v)
{
    put16(b, (unsigned)(v & 0xFFFF));
    put16(b, (unsigned)((v >> 16) & 0xFFFF));
}

owf_zip *owf_zip_new(owf_buf *out)
{
    owf_zip *zip = calloc(1, sizeof *zip);
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    if (!zip)
        return NULL;
    zip->out = out;
    if (t && t->tm_year >= 80) {
        zip->dos_time = (unsigned)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
        zip->dos_date = (unsigned)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
    } else {
        zip->dos_date = (1 << 5) | 1;   /* 1 January 1980 */
    }
    return zip;
}

int owf_zip_add(owf_zip *zip, const char *name, const void *data, size_t length, int compress)
{
    entry *e;
    const unsigned char *body = data;
    unsigned char *packed = NULL;
    unsigned long body_length = (unsigned long)length;
    size_t namelen = strlen(name);

    if (zip->failed)
        return OWF_ERR_MEMORY;
    if (zip->count == zip->capacity) {
        int want = zip->capacity ? zip->capacity * 2 : 8;
        entry *entries = realloc(zip->entries, (size_t)want * sizeof *entries);
        if (!entries) {
            zip->failed = 1;
            return OWF_ERR_MEMORY;
        }
        zip->entries = entries;
        zip->capacity = want;
    }
    e = &zip->entries[zip->count];
    e->name = malloc(namelen + 1);
    if (!e->name) {
        zip->failed = 1;
        return OWF_ERR_MEMORY;
    }
    memcpy(e->name, name, namelen + 1);
    e->crc = owf_crc32(0, data, length);
    e->size = (unsigned long)length;
    e->method = 0;

#ifdef OWF_HAVE_ZLIB
    if (compress && length > 64) {
        z_stream z;
        uLong bound;
        memset(&z, 0, sizeof z);
        if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) == Z_OK) {
            bound = deflateBound(&z, (uLong)length);
            packed = malloc(bound);
            if (packed) {
                z.next_in = (Bytef *)data;
                z.avail_in = (uInt)length;
                z.next_out = packed;
                z.avail_out = (uInt)bound;
                if (deflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out < length) {
                    body = packed;
                    body_length = z.total_out;
                    e->method = 8;
                }
            }
            deflateEnd(&z);
        }
    }
#else
    (void)compress;
#endif
    e->compressed = body_length;
    e->offset = (unsigned long)zip->out->length;

    put32(zip->out, 0x04034B50UL);
    put16(zip->out, 20);
    put16(zip->out, 0);
    put16(zip->out, (unsigned)e->method);
    put16(zip->out, zip->dos_time);
    put16(zip->out, zip->dos_date);
    put32(zip->out, e->crc);
    put32(zip->out, e->compressed);
    put32(zip->out, e->size);
    put16(zip->out, (unsigned)namelen);
    put16(zip->out, 0);
    owf_buf_put(zip->out, name, namelen);
    owf_buf_put(zip->out, body, body_length);
    free(packed);
    zip->count++;
    if (zip->out->failed) {
        zip->failed = 1;
        return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

int owf_zip_finish(owf_zip *zip)
{
    unsigned long start = (unsigned long)zip->out->length, size;
    int i, result;

    for (i = 0; i < zip->count; i++) {
        entry *e = &zip->entries[i];
        size_t namelen = strlen(e->name);
        put32(zip->out, 0x02014B50UL);
        put16(zip->out, 20);
        put16(zip->out, 20);
        put16(zip->out, 0);
        put16(zip->out, (unsigned)e->method);
        put16(zip->out, zip->dos_time);
        put16(zip->out, zip->dos_date);
        put32(zip->out, e->crc);
        put32(zip->out, e->compressed);
        put32(zip->out, e->size);
        put16(zip->out, (unsigned)namelen);
        put16(zip->out, 0);
        put16(zip->out, 0);
        put16(zip->out, 0);
        put16(zip->out, 0);
        put32(zip->out, 0);
        put32(zip->out, e->offset);
        owf_buf_put(zip->out, e->name, namelen);
    }
    size = (unsigned long)zip->out->length - start;
    put32(zip->out, 0x06054B50UL);
    put16(zip->out, 0);
    put16(zip->out, 0);
    put16(zip->out, (unsigned)zip->count);
    put16(zip->out, (unsigned)zip->count);
    put32(zip->out, size);
    put32(zip->out, start);
    put16(zip->out, 0);

    result = (zip->failed || zip->out->failed) ? OWF_ERR_MEMORY : OWF_OK;
    for (i = 0; i < zip->count; i++)
        free(zip->entries[i].name);
    free(zip->entries);
    free(zip);
    return result;
}
