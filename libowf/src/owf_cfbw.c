/*
 * libowf: writing OLE compound files ([MS-CFB], Microsoft's published
 * specification), the container a Publisher file (and an Excel 97-2003
 * workbook) lives in. Version 3: 512-byte sectors, a mini stream of
 * 64-byte sectors for streams under 4096 bytes. Each storage's children
 * are a balanced binary tree in the specification's order (shorter names
 * first, then by upper-case letters), all black. The sectors go: the FAT,
 * the DIFAT (past 109 FAT sectors), the directory, the mini FAT, the mini
 * stream, then the big streams in the order they were added. The same
 * streams give the same bytes.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define SECTOR 512
#define MINI 64
#define CUTOFF 4096
#define NOSTREAM 0xFFFFFFFFUL
#define FREESECT 0xFFFFFFFFUL
#define ENDOFCHAIN 0xFFFFFFFEUL
#define FATSECT 0xFFFFFFFDUL
#define DIFSECT 0xFFFFFFFCUL
#define MAX_NAME 31                      /* UTF-16 units, the NUL makes 32 */

typedef struct {
    unsigned short name[MAX_NAME + 1];
    int nname;                           /* units, without the NUL */
    int type;                            /* 1 storage, 2 stream, 5 root */
    int parent;
    unsigned char *data;
    size_t length;
    unsigned char clsid[16];
    unsigned long left, right, child, start;
} wentry;

struct owf_cfbw {
    wentry *e;
    int n, cap;
    int failed;
};

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

owf_cfbw *owf_cfbw_new(void)
{
    owf_cfbw *w = owf_calloc(1, sizeof *w);
    if (!w)
        return NULL;
    w->cap = 8;
    if (!(w->e = owf_calloc((size_t)w->cap, sizeof *w->e))) {
        owf_free(w);
        return NULL;
    }
    w->n = 1;                            /* the root */
    w->e[0].type = 5;
    w->e[0].parent = -1;
    {
        static const char root[] = "Root Entry";
        int i;
        for (i = 0; root[i]; i++)
            w->e[0].name[i] = (unsigned short)root[i];
        w->e[0].nname = i;
    }
    return w;
}

void owf_cfbw_free(owf_cfbw *w)
{
    int i;
    if (!w)
        return;
    for (i = 0; i < w->n; i++)
        owf_free(w->e[i].data);
    owf_free(w->e);
    owf_free(w);
}

/* One path part (bytes, Latin-1: "\005SummaryInformation") as UTF-16. */
static int set_name(wentry *e, const char *s, size_t n)
{
    size_t i;
    if (!n || n > MAX_NAME)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '/' || c == '\\' || c == ':' || c == '!')
            return 0;                    /* not allowed in a name ([MS-CFB] 2.6.1) */
        e->name[i] = c;
    }
    e->name[n] = 0;
    e->nname = (int)n;
    return 1;
}

static unsigned short upper(unsigned short c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return (unsigned short)(c - 0x20);
    return c;
}

/* The specification's order: the shorter name first, then letter by letter, upper case. */
static int compare(const wentry *a, const wentry *b)
{
    int i;
    if (a->nname != b->nname)
        return a->nname < b->nname ? -1 : 1;
    for (i = 0; i < a->nname; i++) {
        unsigned short x = upper(a->name[i]), y = upper(b->name[i]);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

static int find_child(const owf_cfbw *w, int parent, const wentry *probe)
{
    int i;
    for (i = 1; i < w->n; i++)
        if (w->e[i].parent == parent && !compare(&w->e[i], probe))
            return i;
    return -1;
}

static int new_entry(owf_cfbw *w)
{
    if (w->n == w->cap) {
        wentry *grown = owf_realloc(w->e, (size_t)w->cap * 2 * sizeof *grown);
        if (!grown)
            return -1;
        memset(grown + w->cap, 0, (size_t)w->cap * sizeof *grown);
        w->e = grown;
        w->cap *= 2;
    }
    return w->n++;
}

/* The entry at path ("" the root), making storages on the way when make
 * is set; the last part is made a stream when stream is set. -1: none, or
 * a part that can't be. */
static int walk(owf_cfbw *w, const char *path, int make, int stream)
{
    int at = 0;
    while (*path) {
        const char *slash = strchr(path, '/');
        size_t n = slash ? (size_t)(slash - path) : strlen(path);
        wentry probe;
        int k, last = !slash;
        memset(&probe, 0, sizeof probe);
        if (!set_name(&probe, path, n) || w->e[at].type == 2)
            return -1;
        k = find_child(w, at, &probe);
        if (k >= 0) {
            if (last && stream)
                return -1;               /* a stream is added once */
            if (!last && w->e[k].type == 2)
                return -1;
        } else {
            if (!make)
                return -1;
            if ((k = new_entry(w)) < 0) {
                w->failed = 1;
                return -1;
            }
            w->e[k] = probe;
            w->e[k].type = last && stream ? 2 : 1;
            w->e[k].parent = at;
        }
        at = k;
        path += n + (slash ? 1 : 0);
    }
    return at;
}

int owf_cfbw_add(owf_cfbw *w, const char *path, const unsigned char *data, size_t length)
{
    int k;
    if (!w || w->failed || !path || !*path || (length && !data))
        return w && w->failed ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    if (length > 0x7FFFFFFFUL)
        return OWF_ERR_UNSUPPORTED;      /* version 3: sizes under 2 GB */
    if ((k = walk(w, path, 1, 1)) < 0)
        return w->failed ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    if (length) {
        if (!(w->e[k].data = owf_malloc(length))) {
            w->failed = 1;
            return OWF_ERR_MEMORY;
        }
        memcpy(w->e[k].data, data, length);
    }
    w->e[k].length = length;
    return OWF_OK;
}

int owf_cfbw_set_clsid(owf_cfbw *w, const char *storage, const unsigned char clsid[16])
{
    int k;
    if (!w || w->failed || !storage || !clsid)
        return OWF_ERR_FORMAT;
    if ((k = walk(w, storage, 1, 0)) < 0)
        return w->failed ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    if (w->e[k].type == 2)
        return OWF_ERR_FORMAT;
    memcpy(w->e[k].clsid, clsid, 16);
    return OWF_OK;
}

/* ---- the tree ---- */

/* kids in the specification's order: an insertion sort, as a storage has
 * few children, and with no global state for qsort's comparator. */
static void sort_kids(const owf_cfbw *w, int *kids, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        int k = kids[i];
        for (j = i; j > 0 && compare(&w->e[kids[j - 1]], &w->e[k]) > 0; j--)
            kids[j] = kids[j - 1];
        kids[j] = k;
    }
}

/* kids[lo, hi) sorted: the middle one is the subtree's root. */
static unsigned long balance(owf_cfbw *w, const int *kids, int lo, int hi)
{
    int mid;
    if (lo >= hi)
        return NOSTREAM;
    mid = lo + (hi - lo) / 2;
    w->e[kids[mid]].left = balance(w, kids, lo, mid);
    w->e[kids[mid]].right = balance(w, kids, mid + 1, hi);
    return (unsigned long)kids[mid];
}

static int make_trees(owf_cfbw *w)
{
    int *kids = owf_malloc((size_t)w->n * sizeof *kids), i, j, nk;
    if (!kids)
        return 0;
    for (i = 0; i < w->n; i++)
        w->e[i].left = w->e[i].right = w->e[i].child = NOSTREAM;
    for (i = 0; i < w->n; i++) {
        if (w->e[i].type == 2)
            continue;
        for (nk = 0, j = 1; j < w->n; j++)
            if (w->e[j].parent == i)
                kids[nk++] = j;
        sort_kids(w, kids, nk);
        w->e[i].child = balance(w, kids, 0, nk);
    }
    owf_free(kids);
    return 1;
}

/* ---- the sectors ---- */

static unsigned long sectors_for(size_t bytes, size_t size)
{
    return (unsigned long)((bytes + size - 1) / size);
}

int owf_cfbw_finish(owf_cfbw *w, unsigned char **out, size_t *out_length)
{
    unsigned long ndir, nminifat, nmini, nbig = 0, rest, nfat, ndifat, total, at;
    unsigned long dir_start, minifat_start, mini_start, *fat = NULL;
    size_t mini_bytes = 0, length;
    unsigned char *file, *p;
    int i;

    if (!w || !out || !out_length)
        return OWF_ERR_FORMAT;
    *out = NULL;
    *out_length = 0;
    if (w->failed || !make_trees(w))
        return OWF_ERR_MEMORY;

    /* the mini stream holds the small streams, 64 bytes a sector */
    for (i = 1; i < w->n; i++)
        if (w->e[i].type == 2 && w->e[i].length < CUTOFF) {
            w->e[i].start = w->e[i].length ? (unsigned long)(mini_bytes / MINI) : ENDOFCHAIN;
            mini_bytes += (size_t)sectors_for(w->e[i].length, MINI) * MINI;
        } else if (w->e[i].type == 2)
            nbig += sectors_for(w->e[i].length, SECTOR);
    ndir = sectors_for((size_t)w->n * 128, SECTOR);
    nminifat = sectors_for((size_t)(mini_bytes / MINI) * 4, SECTOR);
    nmini = sectors_for(mini_bytes, SECTOR);
    rest = ndir + nminifat + nmini + nbig;
    nfat = 1;
    for (;;) {                           /* enough FAT for every sector, its own and the DIFAT's too */
        ndifat = nfat > 109 ? (nfat - 109 + 126) / 127 : 0;
        if (nfat * 128 >= rest + nfat + ndifat)
            break;
        nfat++;
    }
    total = nfat + ndifat + rest;
    if ((unsigned long)(0x7FFFFFFFUL / SECTOR) <= total + 1)
        return OWF_ERR_UNSUPPORTED;
    length = (size_t)(total + 1) * SECTOR;
    if (!(file = owf_calloc(1, length)) || !(fat = owf_malloc((size_t)nfat * 128 * sizeof *fat))) {
        owf_free(file);
        return OWF_ERR_MEMORY;
    }
    for (at = 0; at < nfat * 128; at++)
        fat[at] = FREESECT;
    for (at = 0; at < nfat; at++)
        fat[at] = FATSECT;
    for (; at < nfat + ndifat; at++)
        fat[at] = DIFSECT;

#define CHAIN(first, count) do { unsigned long k_; \
        for (k_ = 0; k_ < (count); k_++) fat[(first) + k_] = k_ + 1 < (count) ? (first) + k_ + 1 : ENDOFCHAIN; \
    } while (0)
#define SECT(n) (file + ((size_t)(n) + 1) * SECTOR)

    dir_start = at;
    CHAIN(dir_start, ndir);
    at += ndir;
    minifat_start = nminifat ? at : ENDOFCHAIN;
    CHAIN(at, nminifat);
    at += nminifat;
    mini_start = nmini ? at : ENDOFCHAIN;
    CHAIN(at, nmini);
    at += nmini;
    for (i = 1; i < w->n; i++)
        if (w->e[i].type == 2 && w->e[i].length >= CUTOFF) {
            unsigned long n = sectors_for(w->e[i].length, SECTOR);
            w->e[i].start = at;
            CHAIN(at, n);
            memcpy(SECT(at), w->e[i].data, w->e[i].length);
            at += n;
        }

    /* the mini FAT and the mini stream */
    {
        unsigned long m = 0;
        for (i = 1; i < w->n; i++) {
            unsigned long n, k;
            if (w->e[i].type != 2 || w->e[i].length >= CUTOFF || !w->e[i].length)
                continue;
            n = sectors_for(w->e[i].length, MINI);
            for (k = 0; k < n; k++, m++)
                put32(SECT(minifat_start) + 4 * m, k + 1 < n ? m + 1 : ENDOFCHAIN);
            memcpy(SECT(mini_start) + (size_t)w->e[i].start * MINI, w->e[i].data, w->e[i].length);
        }
        for (; m < nminifat * 128; m++)
            put32(SECT(minifat_start) + 4 * m, FREESECT);
    }

    /* the directory */
    for (i = 0; i < (int)(ndir * 4); i++) {
        unsigned char *d = SECT(dir_start) + (size_t)i * 128;
        put32(d + 0x44, NOSTREAM);
        put32(d + 0x48, NOSTREAM);
        put32(d + 0x4C, NOSTREAM);
        if (i < w->n) {
            const wentry *e = &w->e[i];
            int c;
            for (c = 0; c < e->nname; c++)
                put16(d + 2 * c, e->name[c]);
            put16(d + 0x40, (unsigned)(2 * (e->nname + 1)));
            d[0x42] = (unsigned char)e->type;
            d[0x43] = 1;                 /* black */
            put32(d + 0x44, e->left);
            put32(d + 0x48, e->right);
            put32(d + 0x4C, e->child);
            memcpy(d + 0x50, e->clsid, 16);
            if (e->type == 5) {
                put32(d + 0x74, mini_start);
                put32(d + 0x78, (unsigned long)mini_bytes);
            } else if (e->type == 2) {
                put32(d + 0x74, e->start);
                put32(d + 0x78, (unsigned long)e->length);
            }
        }
    }

    /* the FAT, the DIFAT and the header */
    for (at = 0; at < nfat * 128; at++)
        put32(SECT(at / 128) + 4 * (at % 128), fat[at]);
    p = file;
    memcpy(p, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    put16(p + 0x18, 0x3E);
    put16(p + 0x1A, 3);
    put16(p + 0x1C, 0xFFFE);
    put16(p + 0x1E, 9);
    put16(p + 0x20, 6);
    put32(p + 0x2C, nfat);
    put32(p + 0x30, dir_start);
    put32(p + 0x38, CUTOFF);
    put32(p + 0x3C, minifat_start);
    put32(p + 0x40, nminifat);
    put32(p + 0x44, ndifat ? nfat : ENDOFCHAIN);
    put32(p + 0x48, ndifat);
    for (at = 0; at < 109; at++)
        put32(p + 0x4C + 4 * at, at < nfat ? at : FREESECT);
    for (at = 0; at < ndifat; at++) {
        unsigned char *d = SECT(nfat + at);
        unsigned long k;
        for (k = 0; k < 127; k++) {
            unsigned long f = 109 + at * 127 + k;
            put32(d + 4 * k, f < nfat ? f : FREESECT);
        }
        put32(d + 4 * 127, at + 1 < ndifat ? nfat + at + 1 : ENDOFCHAIN);
    }
#undef CHAIN
#undef SECT
    owf_free(fat);
    *out = file;
    *out_length = length;
    return OWF_OK;
}
