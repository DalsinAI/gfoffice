/*
 * libowf: OLE compound files ([MS-CFB]), the container of Excel 97-2003
 * workbooks and Word 97-2003 documents. Streams are read whole into memory;
 * a chain that runs off the end of the file gives the bytes there are, with
 * cut_short set, so a damaged file still shows what it can.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define END_OF_CHAIN 0xFFFFFFFEUL
#define DIR_ENTRY 128
#define MINI_SECTOR 64

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

/* Sector n, or NULL when it is not wholly in the file. */
static const unsigned char *sector(const owf_cfb *cfb, unsigned long n)
{
    size_t at;
    if (n >= cfb->nsectors)
        return NULL;
    at = (size_t)(n + 1) * cfb->sector_size;
    return at + cfb->sector_size <= cfb->length ? cfb->data + at : NULL;
}

/* Follows a chain through the FAT (or the mini FAT over the mini stream),
 * gathering up to want bytes (all of the chain when want is 0). */
static int read_chain(owf_cfb *cfb, unsigned long start, size_t want, int mini, owf_buf *out)
{
    const unsigned long *table = mini ? cfb->minifat : cfb->fat;
    unsigned long ntable = mini ? cfb->nminifat : cfb->nfat, n = start, steps = 0;
    size_t size = mini ? MINI_SECTOR : cfb->sector_size;

    while (n != END_OF_CHAIN && (!want || out->length < want)) {
        const unsigned char *p;
        size_t take = size;
        if (n >= ntable || ++steps > ntable) {       /* off the table, or a loop */
            cfb->cut_short = 1;
            break;
        }
        if (mini) {
            if ((size_t)(n + 1) * MINI_SECTOR > cfb->ministream_length) {
                cfb->cut_short = 1;
                break;
            }
            p = cfb->ministream + (size_t)n * MINI_SECTOR;
        } else if (!(p = sector(cfb, n))) {
            cfb->cut_short = 1;
            break;
        }
        if (want && want - out->length < take)
            take = want - out->length;
        owf_buf_put(out, p, take);
        n = table[n];
    }
    if (want && out->length < want)
        cfb->cut_short = 1;
    return out->failed ? OWF_ERR_MEMORY : OWF_OK;
}

int owf_cfb_detect(const unsigned char *data, size_t length)
{
    static const unsigned char sig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    return length >= 512 && !memcmp(data, sig, 8);
}

int owf_cfb_open(owf_cfb *cfb, const unsigned char *data, size_t length)
{
    unsigned shift;
    unsigned long nfatsect, difat, ndifat, per, i, k = 0, root_start, root_size;
    owf_buf buf;
    int result;

    memset(cfb, 0, sizeof *cfb);
    if (!owf_cfb_detect(data, length) || data[28] != 0xFE || data[29] != 0xFF)
        return OWF_ERR_FORMAT;
    shift = data[30] | (data[31] << 8);
    if (shift != 9 && shift != 12)
        return OWF_ERR_CORRUPT;
    cfb->data = data;
    cfb->length = length;
    cfb->sector_size = 1u << shift;
    cfb->nsectors = (unsigned long)(length / cfb->sector_size);
    if (cfb->nsectors)
        cfb->nsectors--;                         /* the header's */
    per = cfb->sector_size / 4;

    /* the FAT's sectors: 109 in the header, then the DIFAT's chain */
    nfatsect = le32(data + 0x2C);
    if (nfatsect > cfb->nsectors)
        nfatsect = cfb->nsectors;
    cfb->nfat = nfatsect * per;
    cfb->fat = malloc((cfb->nfat ? cfb->nfat : 1) * sizeof *cfb->fat);
    if (!cfb->fat)
        return OWF_ERR_MEMORY;
    for (i = 0; i < 109 && k < nfatsect; i++, k++) {
        const unsigned char *s = sector(cfb, le32(data + 0x4C + 4 * i));
        unsigned long j;
        if (!s)
            break;
        for (j = 0; j < per; j++)
            cfb->fat[k * per + j] = le32(s + 4 * j);
    }
    difat = le32(data + 0x44);
    ndifat = le32(data + 0x48);
    while (k < nfatsect && ndifat-- && difat != END_OF_CHAIN) {
        const unsigned char *d = sector(cfb, difat);
        if (!d)
            break;
        for (i = 0; i + 1 < per && k < nfatsect; i++, k++) {
            const unsigned char *s = sector(cfb, le32(d + 4 * i));
            unsigned long j;
            if (!s)
                break;
            for (j = 0; j < per; j++)
                cfb->fat[k * per + j] = le32(s + 4 * j);
        }
        difat = le32(d + 4 * (per - 1));
    }
    if (k < nfatsect) {
        cfb->nfat = k * per;                     /* the rest of the FAT was cut off */
        cfb->cut_short = 1;
    }

    /* the directory */
    owf_buf_init(&buf);
    result = read_chain(cfb, le32(data + 0x30), 0, 0, &buf);
    if (result != OWF_OK) {
        owf_buf_free(&buf);
        owf_cfb_close(cfb);
        return result;
    }
    cfb->dir = buf.data;
    cfb->ndir = buf.length / DIR_ENTRY;
    if (!cfb->ndir || cfb->dir[0x42] != 5) {     /* the first entry is the root */
        owf_cfb_close(cfb);
        return OWF_ERR_CORRUPT;
    }

    /* the mini FAT, and the mini stream (the root's) its sectors are in */
    owf_buf_init(&buf);
    result = read_chain(cfb, le32(data + 0x3C), 0, 0, &buf);
    if (result == OWF_OK) {
        cfb->nminifat = buf.length / 4;
        cfb->minifat = malloc((cfb->nminifat ? cfb->nminifat : 1) * sizeof *cfb->minifat);
        if (!cfb->minifat)
            result = OWF_ERR_MEMORY;
        for (i = 0; result == OWF_OK && i < cfb->nminifat; i++)
            cfb->minifat[i] = le32(buf.data + 4 * i);
    }
    owf_buf_free(&buf);
    root_start = le32(cfb->dir + 0x74);
    root_size = le32(cfb->dir + 0x78);
    if (result == OWF_OK && root_size && root_start != END_OF_CHAIN) {
        owf_buf_init(&buf);
        result = read_chain(cfb, root_start, root_size, 0, &buf);
        cfb->ministream = buf.data;
        cfb->ministream_length = buf.length;
    }
    if (result != OWF_OK) {
        owf_cfb_close(cfb);
        return result;
    }
    return OWF_OK;
}

void owf_cfb_close(owf_cfb *cfb)
{
    free(cfb->fat);
    free(cfb->minifat);
    free(cfb->ministream);
    free(cfb->dir);
    memset(cfb, 0, sizeof *cfb);
}

/* The directory entry of a stream named name (ignoring case), or -1. */
static long find(const owf_cfb *cfb, const char *name)
{
    size_t i, n = strlen(name);
    for (i = 1; i < cfb->ndir; i++) {
        const unsigned char *e = cfb->dir + i * DIR_ENTRY;
        unsigned len = e[0x40] | (e[0x41] << 8);
        size_t j;
        if (e[0x42] != 2 || len != 2 * (n + 1) || n > 31)
            continue;
        for (j = 0; j < n; j++)
            if (e[2 * j + 1] || owf_lower(e[2 * j]) != owf_lower((unsigned char)name[j]))
                break;
        if (j == n)
            return (long)i;
    }
    return -1;
}

int owf_cfb_has(const owf_cfb *cfb, const char *name)
{
    return find(cfb, name) >= 0;
}

int owf_cfb_read(owf_cfb *cfb, const char *name, unsigned char **data, size_t *length)
{
    long i = find(cfb, name);
    const unsigned char *e;
    unsigned long size, start, cutoff;
    owf_buf buf;
    int result;

    *data = NULL;
    *length = 0;
    if (i < 0)
        return OWF_ERR_FORMAT;
    e = cfb->dir + (size_t)i * DIR_ENTRY;
    start = le32(e + 0x74);
    size = le32(e + 0x78);
    cutoff = le32(cfb->data + 0x38);
    owf_buf_init(&buf);
    if (size)
        result = read_chain(cfb, start, size, size < (cutoff ? cutoff : 4096), &buf);
    else
        result = OWF_OK;
    if (result != OWF_OK) {
        owf_buf_free(&buf);
        return result;
    }
    owf_buf_putc(&buf, 0);                       /* a NUL after, as files read have */
    if (buf.failed) {
        owf_buf_free(&buf);
        return OWF_ERR_MEMORY;
    }
    *data = buf.data;
    *length = buf.length - 1;
    return OWF_OK;
}

/* ---- Storages and entries ([MS-CFB] 2.6: each storage's children are a
 * tree through the left and right sibling fields, from its child field) ---- */

#define NOSTREAM 0xFFFFFFFFUL

const unsigned char *owf_cfb_entry(const owf_cfb *cfb, unsigned long entry)
{
    return entry < cfb->ndir ? cfb->dir + (size_t)entry * DIR_ENTRY : NULL;
}

int owf_cfb_entry_name(const owf_cfb *cfb, unsigned long entry, char *out, size_t size)
{
    const unsigned char *e = owf_cfb_entry(cfb, entry);
    unsigned len, i;
    size_t k = 0;
    if (!size)
        return 0;
    out[0] = 0;
    if (!e)
        return 0;
    len = (e[0x40] | (e[0x41] << 8)) / 2;
    if (len > 32)
        len = 32;
    for (i = 0; i < len; i++) {
        unsigned c = e[2 * i] | (e[2 * i + 1] << 8);
        if (!c)
            break;
        if (k + 7 >= size)
            break;
        if (c >= 0x20 && c < 0x7F) {
            out[k++] = (char)c;
        } else if (c > 0xFF) {
            out[k++] = '?';
        } else {                             /* "\x01", as a dump shows it */
            static const char hex[] = "0123456789ABCDEF";
            out[k++] = '\\';
            out[k++] = 'x';
            out[k++] = hex[(c >> 4) & 15];
            out[k++] = hex[c & 15];
        }
    }
    out[k] = 0;
    return 1;
}

/* Calls fn for each child of storage (in no particular order); stops when fn
 * returns non-zero and returns that. Bounded: a sibling loop ends the walk. */
int owf_cfb_children(const owf_cfb *cfb, unsigned long storage,
                     int (*fn)(const owf_cfb *cfb, unsigned long entry, void *user), void *user)
{
    const unsigned char *e = owf_cfb_entry(cfb, storage);
    unsigned long *stack, n, steps = 0;
    size_t depth = 0;
    int r = 0;
    if (!e || (e[0x42] != 1 && e[0x42] != 5))
        return 0;
    if (!(stack = malloc(cfb->ndir * sizeof *stack)))
        return 0;
    n = le32(e + 0x4C);
    while (n != NOSTREAM || depth) {
        if (n == NOSTREAM) {
            n = stack[--depth];
            if ((r = fn(cfb, n, user)) != 0)
                break;
            n = le32(cfb->dir + (size_t)n * DIR_ENTRY + 0x48);   /* right */
            continue;
        }
        if (n >= cfb->ndir || ++steps > cfb->ndir)
            break;                           /* off the directory, or a loop */
        stack[depth++] = n;
        n = le32(cfb->dir + (size_t)n * DIR_ENTRY + 0x44);       /* left */
    }
    free(stack);
    return r;
}

struct cfb_find { const char *name; size_t len; unsigned long found; };

static int find_child(const owf_cfb *cfb, unsigned long entry, void *user)
{
    struct cfb_find *f = user;
    const unsigned char *e = owf_cfb_entry(cfb, entry);
    unsigned len = e[0x40] | (e[0x41] << 8);
    size_t j;
    if (len != 2 * (f->len + 1) || f->len > 31)
        return 0;
    for (j = 0; j < f->len; j++)
        if (e[2 * j + 1] || owf_lower(e[2 * j]) != owf_lower((unsigned char)f->name[j]))
            return 0;
    f->found = entry;
    return 1;
}

long owf_cfb_find_path(const owf_cfb *cfb, const char *path)
{
    unsigned long at = 0;
    while (*path) {
        struct cfb_find f;
        const char *slash = strchr(path, '/');
        f.name = path;
        f.len = slash ? (size_t)(slash - path) : strlen(path);
        f.found = NOSTREAM;
        if (f.len && !owf_cfb_children(cfb, at, find_child, &f))
            return -1;
        if (f.len)
            at = f.found;
        path += f.len + (slash ? 1 : 0);
    }
    return (long)at;
}

int owf_cfb_read_entry(owf_cfb *cfb, unsigned long entry, unsigned char **data, size_t *length)
{
    const unsigned char *e = owf_cfb_entry(cfb, entry);
    unsigned long size, start, cutoff;
    owf_buf buf;
    int result = OWF_OK;

    *data = NULL;
    *length = 0;
    if (!e || e[0x42] != 2)
        return OWF_ERR_FORMAT;
    start = le32(e + 0x74);
    size = le32(e + 0x78);
    cutoff = le32(cfb->data + 0x38);
    owf_buf_init(&buf);
    if (size)
        result = read_chain(cfb, start, size, size < (cutoff ? cutoff : 4096), &buf);
    if (result == OWF_OK)
        owf_buf_putc(&buf, 0);
    if (result != OWF_OK || buf.failed) {
        owf_buf_free(&buf);
        return result != OWF_OK ? result : OWF_ERR_MEMORY;
    }
    *data = buf.data;
    *length = buf.length - 1;
    return OWF_OK;
}
