/*
 * libowf: reading zip archives (ODT, DOCX). Stored files, and deflated ones
 * (every real ODT and DOCX): through zlib when it is built in
 * (OWF_HAVE_ZLIB) and there (on the Amiga, z.library), else with libowf's
 * own inflate (owf_inflate.c), so no package needs z.library to be read.
 * Sizes are capped and CRCs checked, so a damaged or hostile archive gives
 * an error, not a crash or a flood of memory.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"
#include "owf_inflate.h"

#include <stdlib.h>
#include <string.h>

#ifdef OWF_HAVE_ZLIB
#include "owf_zlib.h"
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

/* A deflated entry into out, which it must fill: size bytes. zlib's
 * inflate is the faster, so it is used when it is there; when it is not (on
 * the Amiga, no z.library in LIBS:), or cannot start, libowf's own. */
static int inflate_entry(unsigned char *out, unsigned long size, const unsigned char *in, unsigned long packed)
{
#ifdef OWF_HAVE_ZLIB
    z_stream z;
    int r;

    memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) == Z_OK) {
        z.next_in = (Bytef *)in;
        z.avail_in = (uInt)packed;
        z.next_out = out;
        z.avail_out = (uInt)size;
        r = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        return r == Z_STREAM_END && z.total_out == size ? OWF_OK : OWF_ERR_CORRUPT;
    }
#endif
    return owf_inflate(out, size, in, packed);
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
        int result = inflate_entry(data, size, body, packed);
        if (result != OWF_OK) {
            free(data);
            return result;
        }
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

/* ---- What a package holds ----
 * ODF says it in its "mimetype" entry (or its manifest's root entry), OOXML
 * in [Content_Types].xml's main part, so that a document reader never
 * claims a workbook or a presentation, nor the other way round. */

static int has_text(const char *text, const char *word)
{
    return text && strstr(text, word) != NULL;
}

int owf_odf_kind(const unsigned char *data, size_t length)
{
    owf_unzip zip;
    unsigned char *part;
    size_t n;
    const char *t;
    int kind = OWF_PACKAGE_UNKNOWN;
    if (!owf_unzip_open(&zip, data, length))
        return OWF_PACKAGE_UNKNOWN;
    if (owf_unzip_read(&zip, "mimetype", &part, &n) != OWF_OK &&
        owf_unzip_read(&zip, "META-INF/manifest.xml", &part, &n) != OWF_OK)
        return OWF_PACKAGE_UNKNOWN;
    t = (const char *)part;
    if (has_text(t, "application/vnd.oasis.opendocument.spreadsheet"))
        kind = OWF_PACKAGE_SHEET;
    else if (has_text(t, "application/vnd.oasis.opendocument.presentation"))
        kind = OWF_PACKAGE_SLIDES;
    else if (has_text(t, "application/vnd.oasis.opendocument.text"))
        kind = OWF_PACKAGE_TEXT;
    else if (has_text(t, "application/vnd.oasis.opendocument."))
        kind = OWF_PACKAGE_OTHER;        /* drawings, charts, formulas, databases */
    free(part);
    return kind;
}

int owf_ooxml_kind(const unsigned char *data, size_t length)
{
    owf_unzip zip;
    unsigned char *part;
    size_t n;
    int kind = OWF_PACKAGE_UNKNOWN;
    if (!owf_unzip_open(&zip, data, length))
        return OWF_PACKAGE_UNKNOWN;
    if (owf_unzip_read(&zip, "[Content_Types].xml", &part, &n) == OWF_OK) {
        const char *t = (const char *)part;
        if (has_text(t, "application/vnd.ms-excel.sheet.binary.macroEnabled.main"))
            kind = OWF_PACKAGE_SHEET_BINARY;
        else if (has_text(t, "application/vnd.ms-excel.sheet.macroEnabled.main+xml") ||
                 has_text(t, "application/vnd.ms-excel.template.macroEnabled.main+xml"))
            kind = OWF_PACKAGE_SHEET_MACROS;
        else if (has_text(t, "spreadsheetml.sheet.main+xml") || has_text(t, "spreadsheetml.template.main+xml"))
            kind = OWF_PACKAGE_SHEET;
        else if (has_text(t, "presentationml.") || has_text(t, "application/vnd.ms-powerpoint."))
            kind = OWF_PACKAGE_SLIDES;
        else if (has_text(t, "wordprocessingml.document.main+xml") || has_text(t, "wordprocessingml.template.main+xml") ||
                 has_text(t, "application/vnd.ms-word.document.macroEnabled.main+xml") ||
                 has_text(t, "application/vnd.ms-word.template.macroEnabledTemplate.main+xml"))
            kind = OWF_PACKAGE_TEXT;
        free(part);
    }
    if (kind == OWF_PACKAGE_UNKNOWN) {   /* no content types: the parts' usual names */
        if (owf_unzip_has(&zip, "xl/workbook.xml"))
            kind = owf_unzip_has(&zip, "xl/vbaProject.bin") ? OWF_PACKAGE_SHEET_MACROS : OWF_PACKAGE_SHEET;
        else if (owf_unzip_has(&zip, "xl/workbook.bin"))
            kind = OWF_PACKAGE_SHEET_BINARY;
        else if (owf_unzip_has(&zip, "ppt/presentation.xml"))
            kind = OWF_PACKAGE_SLIDES;
        else if (owf_unzip_has(&zip, "word/document.xml"))
            kind = OWF_PACKAGE_TEXT;
    }
    return kind;
}
