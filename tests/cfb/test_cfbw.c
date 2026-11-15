/*
 * test_cfbw: libowf's compound file writer (owf_cfbw.c), read back with
 * its reader (owf_cfb.c). Streams of every size around the sector and
 * mini-stream edges, storages three deep, the names a .pub has, a storage
 * with 40 children (the tree in the specification's order), a file past
 * 109 FAT sectors (the DIFAT), class ids, the same bytes twice, and the
 * paths it refuses.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static unsigned char *pattern(size_t n, unsigned seed)
{
    unsigned char *p = malloc(n ? n : 1);
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (unsigned char)((i * 131u + seed * 17u + (i >> 9)) & 0xFF);
    return p;
}

/* The stream at path reads back as want. */
static int reads_back(owf_cfb *cfb, const char *path, const unsigned char *want, size_t n)
{
    long e = owf_cfb_find_path(cfb, path);
    unsigned char *got = NULL;
    size_t len = 0;
    int ok;
    if (e < 0 || owf_cfb_read_entry(cfb, (unsigned long)e, &got, &len) != OWF_OK)
        return 0;
    ok = len == n && (!n || !memcmp(got, want, n));
    free(got);
    return ok;
}

static int count_kid(const owf_cfb *cfb, unsigned long entry, void *user)
{
    (void)cfb; (void)entry;
    ++*(int *)user;
    return 0;
}

/* Directory entry e's name as upper-case ASCII, for checking the order. */
static int entry_key(const owf_cfb *cfb, unsigned long e, char *out)
{
    const unsigned char *d = owf_cfb_entry(cfb, e);
    int n, i;
    if (!d)
        return -1;
    n = (d[0x40] | d[0x41] << 8) / 2 - 1;
    for (i = 0; i < n && i < 31; i++) {
        char c = (char)d[2 * i];
        out[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    out[i] = 0;
    return n;
}

static int before(const owf_cfb *cfb, unsigned long a, unsigned long b)
{
    char x[32], y[32];
    int nx = entry_key(cfb, a, x), ny = entry_key(cfb, b, y);
    return nx != ny ? nx < ny : strcmp(x, y) < 0;
}

/* Every node of the tree under e is in order: left below, right above; count them. */
static int tree_ok(const owf_cfb *cfb, unsigned long e, unsigned long lo, unsigned long hi, int *count, int depth)
{
    const unsigned char *d;
    if (e == 0xFFFFFFFFUL)
        return 1;
    if (depth > 64 || !(d = owf_cfb_entry(cfb, e)) || d[0x43] != 1)
        return 0;
    if ((lo != 0xFFFFFFFFUL && !before(cfb, lo, e)) || (hi != 0xFFFFFFFFUL && !before(cfb, e, hi)))
        return 0;
    ++*count;
    return tree_ok(cfb, le32(d + 0x44), lo, e, count, depth + 1) && tree_ok(cfb, le32(d + 0x48), e, hi, count, depth + 1);
}

static void sizes(void)
{
    static const size_t n[] = { 0, 1, 63, 64, 65, 511, 512, 513, 4095, 4096, 4097, 100000 };
    unsigned char *data[12], *file = NULL;
    size_t length = 0;
    char path[64];
    owf_cfbw *w = owf_cfbw_new();
    owf_cfb cfb;
    int i, kids = 0;
    CHECK(w != NULL);
    for (i = 0; i < 12; i++) {
        data[i] = pattern(n[i], (unsigned)i);
        sprintf(path, i % 3 == 0 ? "S%02d" : i % 3 == 1 ? "Quill/QuillSub/S%02d" : "Escher/S%02d", i);
        CHECK(owf_cfbw_add(w, path, data[i], n[i]) == OWF_OK);
    }
    CHECK(owf_cfbw_add(w, "\005SummaryInformation", data[5], n[5]) == OWF_OK);
    CHECK(owf_cfbw_add(w, "Quill/QuillSub/\001CompObj", data[2], n[2]) == OWF_OK);
    CHECK(owf_cfbw_finish(w, &file, &length) == OWF_OK && file && length % 512 == 0);
    CHECK(file && owf_cfb_detect(file, length));
    CHECK(file && owf_cfb_open(&cfb, file, length) == OWF_OK);
    if (file) {
        for (i = 0; i < 12; i++) {
            sprintf(path, i % 3 == 0 ? "S%02d" : i % 3 == 1 ? "Quill/QuillSub/S%02d" : "Escher/S%02d", i);
            CHECK(reads_back(&cfb, path, data[i], n[i]));
        }
        CHECK(reads_back(&cfb, "\005SummaryInformation", data[5], n[5]));
        CHECK(reads_back(&cfb, "quill/quillsub/\001compobj", data[2], n[2]));    /* names ignore case */
        CHECK(owf_cfb_find_path(&cfb, "Quill/QuillSub") > 0 && owf_cfb_find_path(&cfb, "Nothing") < 0);
        owf_cfb_children(&cfb, 0, count_kid, &kids);
        CHECK(kids == 7);                /* S00 S03 S06 S09, Quill, Escher, SummaryInformation */
        CHECK(!cfb.cut_short);
        owf_cfb_close(&cfb);
    }
    for (i = 0; i < 12; i++)
        free(data[i]);
    free(file);
    owf_cfbw_free(w);
}

static void many(void)
{
    unsigned char *file = NULL, byte = 7;
    size_t length = 0;
    char path[48];
    owf_cfbw *w = owf_cfbw_new();
    owf_cfb cfb;
    int i, count = 0, kids = 0;
    for (i = 0; i < 40; i++) {           /* names of mixed lengths and case */
        sprintf(path, "Many/%s%d", i % 2 ? "item" : "IT", (i * 37) % 100);
        CHECK(owf_cfbw_add(w, path, &byte, 1) == OWF_OK);
    }
    CHECK(owf_cfbw_finish(w, &file, &length) == OWF_OK);
    CHECK(file && owf_cfb_open(&cfb, file, length) == OWF_OK);
    if (file) {
        long s = owf_cfb_find_path(&cfb, "Many");
        const unsigned char *d = s > 0 ? owf_cfb_entry(&cfb, (unsigned long)s) : NULL;
        CHECK(d && tree_ok(&cfb, le32(d + 0x4C), 0xFFFFFFFFUL, 0xFFFFFFFFUL, &count, 0) && count == 40);
        owf_cfb_children(&cfb, (unsigned long)s, count_kid, &kids);
        CHECK(kids == 40);
        for (i = 0; i < 40; i++) {
            sprintf(path, "Many/%s%d", i % 2 ? "item" : "IT", (i * 37) % 100);
            CHECK(reads_back(&cfb, path, &byte, 1));
        }
        owf_cfb_close(&cfb);
    }
    free(file);
    owf_cfbw_free(w);
}

static void big(void)
{
    size_t n = (size_t)8 * 1024 * 1024 + 300;      /* over 109 FAT sectors' 6.8 MB */
    unsigned char *data = pattern(n, 3), *file = NULL;
    size_t length = 0;
    owf_cfbw *w = owf_cfbw_new();
    owf_cfb cfb;
    CHECK(owf_cfbw_add(w, "Contents", data, n) == OWF_OK);
    CHECK(owf_cfbw_add(w, "Small", data, 10) == OWF_OK);
    CHECK(owf_cfbw_finish(w, &file, &length) == OWF_OK);
    CHECK(file && le32(file + 0x2C) > 109 && le32(file + 0x48) >= 1);   /* FAT sectors, DIFAT sectors */
    CHECK(file && owf_cfb_open(&cfb, file, length) == OWF_OK);
    if (file) {
        CHECK(reads_back(&cfb, "Contents", data, n));
        CHECK(reads_back(&cfb, "Small", data, 10));
        CHECK(!cfb.cut_short);
        owf_cfb_close(&cfb);
    }
    free(data);
    free(file);
    owf_cfbw_free(w);
}

static void same_twice_and_clsid(void)
{
    static const unsigned char clsid[16] = { 0x01, 0x12, 0x02, 0, 0, 0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46 };
    unsigned char *a = NULL, *b = NULL, x[3] = { 1, 2, 3 };
    size_t la = 0, lb = 0;
    int k;
    for (k = 0; k < 2; k++) {
        owf_cfbw *w = owf_cfbw_new();
        CHECK(owf_cfbw_set_clsid(w, "", clsid) == OWF_OK);
        CHECK(owf_cfbw_add(w, "Contents", x, 3) == OWF_OK && owf_cfbw_add(w, "Quill/QuillSub/CONTENTS", x, 2) == OWF_OK);
        CHECK(owf_cfbw_set_clsid(w, "Quill", clsid) == OWF_OK);
        CHECK(owf_cfbw_set_clsid(w, "Contents", clsid) == OWF_ERR_FORMAT);  /* a stream has none */
        CHECK(owf_cfbw_finish(w, k ? &b : &a, k ? &lb : &la) == OWF_OK);
        owf_cfbw_free(w);
    }
    CHECK(a && b && la == lb && !memcmp(a, b, la));
    if (a) {
        owf_cfb cfb;
        CHECK(owf_cfb_open(&cfb, a, la) == OWF_OK);
        CHECK(!memcmp(owf_cfb_entry(&cfb, 0) + 0x50, clsid, 16));
        CHECK(!memcmp(owf_cfb_entry(&cfb, (unsigned long)owf_cfb_find_path(&cfb, "Quill")) + 0x50, clsid, 16));
        owf_cfb_close(&cfb);
    }
    free(a);
    free(b);
}

static void refused(void)
{
    unsigned char x = 1, *file = NULL;
    size_t length = 0;
    owf_cfbw *w = owf_cfbw_new();
    CHECK(owf_cfbw_add(w, "A/B", &x, 1) == OWF_OK);
    CHECK(owf_cfbw_add(w, "A/B", &x, 1) == OWF_ERR_FORMAT);           /* a stream once */
    CHECK(owf_cfbw_add(w, "a/b", &x, 1) == OWF_ERR_FORMAT);           /* names ignore case */
    CHECK(owf_cfbw_add(w, "A/B/C", &x, 1) == OWF_ERR_FORMAT);         /* under a stream */
    CHECK(owf_cfbw_add(w, "A", &x, 1) == OWF_ERR_FORMAT);             /* a storage already */
    CHECK(owf_cfbw_add(w, "", &x, 1) == OWF_ERR_FORMAT);
    CHECK(owf_cfbw_add(w, "A//B", &x, 1) == OWF_ERR_FORMAT);
    CHECK(owf_cfbw_add(w, "Has:colon", &x, 1) == OWF_ERR_FORMAT);
    CHECK(owf_cfbw_add(w, "ThirtyTwoCharactersAreOneTooMany", &x, 1) == OWF_ERR_FORMAT);
    CHECK(owf_cfbw_add(w, "ThirtyOneCharactersFitExactly1", &x, 1) == OWF_OK);
    CHECK(owf_cfbw_add(w, "X", NULL, 1) == OWF_ERR_FORMAT);
    CHECK(owf_cfbw_finish(w, &file, &length) == OWF_OK);
    free(file);
    owf_cfbw_free(w);
    {                                    /* nothing in it: a root alone */
        owf_cfb cfb;
        int kids = 0;
        w = owf_cfbw_new();
        CHECK(owf_cfbw_finish(w, &file, &length) == OWF_OK && length == 3 * 512);
        CHECK(file && owf_cfb_open(&cfb, file, length) == OWF_OK);
        if (file) {
            owf_cfb_children(&cfb, 0, count_kid, &kids);
            CHECK(kids == 0);
            owf_cfb_close(&cfb);
        }
        free(file);
        owf_cfbw_free(w);
    }
}

int main(void)
{
    sizes();
    many();
    big();
    same_twice_and_clsid();
    refused();
    if (failures) {
        printf("cfbw: %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("cfbw: compound file writer tests passed (%d checks)\n", checks);
    return 0;
}
