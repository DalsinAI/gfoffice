/*
 * libowf: writing a publication as a Publisher file (.pub), the 2002-or-
 * later form as Publisher 2007 writes it, laid out the way the reader
 * (in_pub.c) and docs/gfoffice/PUB-FORMAT.md describe it: Contents (5),
 * the Quill text storage (6) and the drawing (7) in an OLE compound file
 * (owf_cfbw.c). This first writer keeps pages and master pages, text
 * boxes, shapes and tables, each frame's box and look, the text with its
 * paragraphs' alignment, character runs (bold, italic, underline, size,
 * typeface, colour), links and table cells, the colour scheme, and
 * pictures (PNG, GIF, JPEG, DIB, WMF, EMF: 8), groups and WordArt's
 * text. The scratch area is not written yet: a report note says so.
 *
 * What our reader reads back is checked (tests/pub/test_pub_save.py);
 * whether Publisher itself opens these files is not known yet.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"
#include "owf_pub.h"

#include <stdlib.h>
#include <string.h>

#define MARK_2007 0x15
#define TEXT_AT 0x200                    /* TEXT's place in the text storage */

/* ---- bytes and tagged values (PUB-FORMAT.md 4, 6.3) ---- */

static void put16(owf_buf *b, unsigned v)
{
    owf_buf_putc(b, (int)(v & 0xFF));
    owf_buf_putc(b, (int)((v >> 8) & 0xFF));
}

static void put32(owf_buf *b, unsigned long v)
{
    put16(b, (unsigned)(v & 0xFFFF));
    put16(b, (unsigned)((v >> 16) & 0xFFFF));
}

static void patch32(owf_buf *b, size_t at, unsigned long v)
{
    if (b->failed || at + 4 > b->length)
        return;
    b->data[at] = (unsigned char)v;
    b->data[at + 1] = (unsigned char)(v >> 8);
    b->data[at + 2] = (unsigned char)(v >> 16);
    b->data[at + 3] = (unsigned char)(v >> 24);
}

/* A field's head: the Contents' 8-bit ids, or the text storage's 11-bit ones. */
static void head(owf_buf *b, unsigned id, unsigned type, int wide)
{
    if (wide) {
        owf_buf_putc(b, (int)(id & 0xFF));
        owf_buf_putc(b, (int)(type | ((id >> 8) & 7)));
    } else {
        owf_buf_putc(b, (int)id);
        owf_buf_putc(b, (int)type);
    }
}

static void f16(owf_buf *b, unsigned id, unsigned type, unsigned v, int wide)
{
    head(b, id, type, wide);
    put16(b, v);
}

static void f32(owf_buf *b, unsigned id, unsigned type, unsigned long v, int wide)
{
    head(b, id, type, wide);
    put32(b, v);
}

/* A block: its u32 length (counting itself) patched in by close. */
static size_t open_block(owf_buf *b)
{
    size_t at = b->length;
    put32(b, 0);
    return at;
}

static void close_block(owf_buf *b, size_t at)
{
    patch32(b, at, (unsigned long)(b->length - at));
}

static size_t open_field(owf_buf *b, unsigned id, unsigned type, int wide)
{
    head(b, id, type, wide);
    return open_block(b);
}

/* UTF-8 as UTF-16LE (into b), and how many units. */
static unsigned long put_utf16(owf_buf *b, const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + n;
    unsigned long units = 0;
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        if (c >= 0x10000 && c <= 0x10FFFF) {
            c -= 0x10000;
            put16(b, (unsigned)(0xD800 + (c >> 10)));
            put16(b, (unsigned)(0xDC00 + (c & 0x3FF)));
            units += 2;
        } else {
            put16(b, (unsigned)(c <= 0xFFFF ? c : 0xFFFD));
            units++;
        }
    }
    return units;
}

/* A blob field (type 0xC0): UTF-16 text and its NUL. */
static void f_text(owf_buf *b, unsigned id, const char *s)
{
    size_t at = open_field(b, id, 0xC0, 0);
    put_utf16(b, s, strlen(s));
    put16(b, 0);
    close_block(b, at);
}

static unsigned long bgr(unsigned long rgb)
{
    return ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

/* ---- the writer's state ---- */

typedef struct {
    unsigned kind;
    long parent;                         /* a chunk number; -1: none */
    owf_buf body;
} wchunk;

typedef struct {
    const owf_pub *pub;
    owf_report *report;
    int failed;
    wchunk *chunks;                      /* chunk 0x101 + i */
    int nchunks, capchunks;
    long *page_chunk;                    /* masters, then pages */
    long **frame_chunk;                  /* [page][frame] */
    int *story_out;                      /* pub story -> written story; -1: not written */
    long *story_id;                      /* written story -> text id */
    int *story_of;                       /* written story -> pub story */
    int nout;
    unsigned long colours[256];          /* PL: the text's colours as 0x00BBGGRR */
    int ncolours;
    int lost_pictures, lost_kinds, lost_colours;
} writer;

static int npages_all(const owf_pub *pub)
{
    return pub->nmasters + pub->npages;
}

static const owf_pub_page *page_at(const owf_pub *pub, int k)
{
    return k < pub->nmasters ? &pub->masters[k] : &pub->pages[k - pub->nmasters];
}

static long new_chunk(writer *w, unsigned kind, long parent)
{
    wchunk *c;
    if (w->nchunks == w->capchunks) {
        int cap = w->capchunks ? 2 * w->capchunks : 16;
        wchunk *grown = owf_realloc(w->chunks, (size_t)cap * sizeof *grown);
        if (!grown) {
            w->failed = 1;
            return -1;
        }
        w->chunks = grown;
        w->capchunks = cap;
    }
    c = &w->chunks[w->nchunks];
    c->kind = kind;
    c->parent = parent;
    owf_buf_init(&c->body);
    return 0x101 + w->nchunks++;
}

static owf_buf *chunk_body(writer *w, long n)
{
    return &w->chunks[n - 0x101].body;
}

/* ---- Contents (PUB-FORMAT.md 5) ---- */

static void write_pages(writer *w)
{
    const owf_pub *pub = w->pub;
    int k, i;
    for (k = 0; k < npages_all(pub) && !w->failed; k++) {
        const owf_pub_page *pg = page_at(pub, k);
        long n = new_chunk(w, 0x43, 0x100);
        owf_buf *b;
        size_t at;
        if (n < 0)
            return;
        w->page_chunk[k] = n;
        b = chunk_body(w, n);
        at = open_block(b);
        if (k < pub->nmasters) {
            char letter[2];
            letter[0] = (char)('A' + k % 26);
            letter[1] = 0;
            f16(b, 0x10, 0x18, 3, 0);
            f_text(b, 0x0E, letter);
            f_text(b, 0x0F, pg->name ? pg->name : "Master Page");
        } else {
            f16(b, 0x10, 0x18, 5, 0);
            head(b, 0x06, 0x28, 0);
            put32(b, 1);
            put32(b, 0);
            if (pg->master >= 0 && pg->master < pub->nmasters)
                f32(b, 0x0D, 0x68, (unsigned long)(0x101 + pg->master), 0);
        }
        {
            size_t at2 = open_field(b, 0x05, 0x88, 0);
            f32(b, 0x01, 0x20, 0, 0);
            f32(b, 0x02, 0x20, 0, 0);
            close_block(b, at2);
        }
        close_block(b, at);
    }
    /* the frames: each page's chunks after all the pages' (the reader finds them by parent) */
    for (k = 0; k < npages_all(pub) && !w->failed; k++) {
        const owf_pub_page *pg = page_at(pub, k);
        for (i = 0; i < pg->nframes && !w->failed; i++) {
            const owf_pub_frame *f = &pg->frames[i];
            long n;
            owf_buf *b;
            size_t at;
            /* in a group: the group's chunk is its parent */
            n = new_chunk(w, f->kind == OWF_PUB_TABLE ? 0x10 : 0x01,
                          f->parent >= 0 && f->parent < i && w->frame_chunk[k][f->parent] >= 0 ?
                          w->frame_chunk[k][f->parent] : w->page_chunk[k]);
            if (n < 0)
                return;
            w->frame_chunk[k][i] = n;
            b = chunk_body(w, n);
            at = open_block(b);
            if (f->story >= 0 && f->story < pub->nstories && w->story_out[f->story] >= 0) {
                int s = w->story_out[f->story], j, place = 0;
                f32(b, 0x27, 0x20, (unsigned long)w->story_id[s], 0);
                for (j = 0; j < i; j++)          /* a linked frame's place in its story's chain */
                    if (pg->frames[j].story == f->story && !pg->frames[j].wordart && pg->frames[j].kind != OWF_PUB_GROUP)
                        place++;
                if (place)
                    f32(b, 0x28, 0x20, (unsigned long)place, 0);
            } else if (f->text_id >= 0 && !f->wordart)
                f32(b, 0x27, 0x20, (unsigned long)f->text_id, 0);   /* a text id with no text, as read */
            if (f->kind == OWF_PUB_TABLE) {
                f32(b, 0x66, 0x20, (unsigned long)(f->rows > 0 ? f->rows : 1), 0);
                f32(b, 0x67, 0x20, (unsigned long)(f->cols > 0 ? f->cols : 1), 0);
            }
            close_block(b, at);
        }
    }
}

static void write_scheme(writer *w)
{
    const owf_pub *pub = w->pub;
    long n;
    owf_buf *b;
    size_t at, arr;
    int i;
    if (!pub->nscheme || (n = new_chunk(w, 0x5C, 0x100)) < 0)
        return;
    b = chunk_body(w, n);
    at = open_block(b);
    f16(b, 0x01, 0x18, (unsigned)pub->nscheme, 0);
    arr = open_field(b, 0x02, 0xA0, 0);
    for (i = 0; i < pub->nscheme; i++) {
        if (!pub->scheme[i]) {
            head(b, 0, 0x78, 0);          /* black: an empty item, as Publisher writes it */
        } else {
            size_t it = open_field(b, 0, 0x88, 0);
            f32(b, 0x01, 0x20, bgr(pub->scheme[i]), 0);
            close_block(b, it);
        }
    }
    close_block(b, arr);
    f_text(b, 0x06, "GFOffice");
    close_block(b, at);
}

static void write_contents(writer *w, owf_buf *out)
{
    const owf_pub *pub = w->pub;
    owf_buf doc;
    size_t hdr_at, index_at, second_at, at, arr;
    unsigned long *offset;
    long count = 0x101 + w->nchunks, n;
    int k;

    /* the document's own chunk: the page size and the pages */
    owf_buf_init(&doc);
    at = open_block(&doc);
    arr = open_field(&doc, 0x12, 0x88, 0);
    f32(&doc, 0x01, 0x20, (unsigned long)pub->width_emu, 0);
    f32(&doc, 0x02, 0x20, (unsigned long)pub->height_emu, 0);
    close_block(&doc, arr);
    arr = open_field(&doc, 0x02, 0xA0, 0);
    for (k = 0; k < npages_all(pub); k++)
        f32(&doc, 0, 0x68, (unsigned long)w->page_chunk[k], 0);
    close_block(&doc, arr);
    close_block(&doc, at);

    if (!(offset = owf_calloc((size_t)count, sizeof *offset))) {
        owf_buf_free(&doc);
        w->failed = 1;
        return;
    }
    while (out->length < 0x2C)
        owf_buf_putc(out, 0);
    hdr_at = out->length;
    for (k = 0; k < 36; k++)
        owf_buf_putc(out, 0);
    offset[0x100] = (unsigned long)out->length;
    owf_buf_put(out, doc.data, doc.length);
    owf_buf_free(&doc);
    for (k = 0; k < w->nchunks; k++) {
        offset[0x101 + k] = (unsigned long)out->length;
        owf_buf_put(out, w->chunks[k].body.data, w->chunks[k].body.length);
        if (w->chunks[k].body.failed)
            w->failed = 1;
    }
    /* the index: an item a chunk number, empty ones below 0x100 */
    index_at = out->length;
    at = open_block(out);
    f32(out, 0x01, 0x20, (unsigned long)count, 0);
    f32(out, 0x02, 0x20, (unsigned long)(count - 1), 0);
    arr = open_field(out, 0x03, 0xA0, 0);
    for (n = 0; n < count; n++) {
        size_t it;
        if (n < 0x100) {
            head(out, 0, 0x78, 0);
            continue;
        }
        it = open_field(out, 0, 0x88, 0);
        if (n == 0x100) {
            f16(out, 0x02, 0x10, 0x44, 0);
            f32(out, 0x04, 0xB8, offset[n], 0);
        } else {
            const wchunk *c = &w->chunks[n - 0x101];
            f16(out, 0x02, 0x10, c->kind, 0);
            f32(out, 0x04, 0xB8, offset[n], 0);
            if (c->parent >= 0)
                f32(out, 0x05, 0x68, (unsigned long)c->parent, 0);
        }
        close_block(out, it);
    }
    close_block(out, arr);
    close_block(out, at);
    second_at = out->length;
    at = open_block(out);
    f32(out, 0x01, 0x20, 0, 0);
    close_block(out, at);
    owf_free(offset);

    /* the header block and the first bytes (PUB-FORMAT.md 5.1) */
    {
        owf_buf h;
        owf_buf_init(&h);
        at = open_block(&h);
        f16(&h, 0x01, 0x18, 0xACE8, 0);
        f16(&h, 0x02, 0x18, 0x100, 0);
        f32(&h, 0x03, 0x20, (1000UL << 16) | MARK_2007, 0);
        f32(&h, 0x08, 0x20, (unsigned long)out->length, 0);
        f32(&h, 0x0A, 0x20, (unsigned long)index_at, 0);
        f32(&h, 0x0B, 0x20, (unsigned long)second_at, 0);
        close_block(&h, at);
        if (!h.failed && !out->failed && h.length == 36)
            memcpy(out->data + hdr_at, h.data, 36);
        else
            w->failed = 1;
        owf_buf_free(&h);
    }
    if (!out->failed) {
        unsigned char *p = out->data;
        p[0] = 0xE8; p[1] = 0xAC;
        p[2] = 0x2C; p[3] = 0x00;
        p[4] = 0xE8; p[5] = 0x03;
        p[6] = p[7] = 0;
        patch32(out, 8, (unsigned long)out->length);
        p[0x0C] = MARK_2007;
        patch32(out, 0x1A, (unsigned long)index_at);
        patch32(out, 0x1E, (unsigned long)second_at);
    }
}

/* ---- the drawing (PUB-FORMAT.md 7, after [MS-ODRAW]) ---- */

static size_t open_rec(owf_buf *b, unsigned type, unsigned inst, unsigned ver)
{
    size_t at;
    put16(b, (inst << 4) | ver);
    put16(b, type);
    at = b->length;
    put32(b, 0);
    return at;
}

static void close_rec(owf_buf *b, size_t at)
{
    patch32(b, at, (unsigned long)(b->length - at - 4));
}

/* ---- pictures: the BLIP store (PUB-FORMAT.md 8, after [MS-ODRAW] 2.2.32-2.2.33) ---- */

typedef struct {
    unsigned rtype, inst;                /* the record in EscherDelayStm and its instance */
    unsigned char win, mac;              /* the store entry's two types */
    int metafile;                        /* 1: WMF/EMF, its 34-byte header and zlib data */
} picture_kind;

static int picture_kind_of(const owf_image *im, picture_kind *k)
{
    const char *m = im->mime ? im->mime : "";
    memset(k, 0, sizeof *k);
    if (!im->data || !im->length)
        return 0;
    if (!strcmp(m, "image/png") || !strcmp(m, "image/gif")) {
        k->rtype = 0xF01E; k->inst = 0x6E0; k->win = k->mac = 6;
    } else if (!strcmp(m, "image/jpeg")) {
        k->rtype = 0xF01D; k->inst = 0x46A; k->win = k->mac = 5;
    } else if (!strcmp(m, "image/x-dib")) {
        k->rtype = 0xF01F; k->inst = 0x7A8; k->win = k->mac = 7;
    } else if (!strcmp(m, "image/x-wmf")) {
        k->rtype = 0xF01B; k->inst = 0x216; k->win = 3; k->mac = 4; k->metafile = 1;
    } else if (!strcmp(m, "image/x-emf")) {
        k->rtype = 0xF01A; k->inst = 0x3D4; k->win = 2; k->mac = 4; k->metafile = 1;
    } else
        return 0;
    return 1;
}

/* zlib data (RFC 1950) in stored DEFLATE blocks: no compressor needed, and
 * the same bytes every time. */
static void put_zlib_stored(owf_buf *b, const unsigned char *p, size_t n)
{
    unsigned long a = 1, s2 = 0;
    size_t k = 0, i;
    owf_buf_putc(b, 0x78);
    owf_buf_putc(b, 0x01);
    do {
        size_t len = n - k > 65535 ? 65535 : n - k;
        owf_buf_putc(b, k + len == n ? 1 : 0);
        owf_buf_putc(b, (int)(len & 0xFF));
        owf_buf_putc(b, (int)(len >> 8));
        owf_buf_putc(b, (int)(~len & 0xFF));
        owf_buf_putc(b, (int)((~len >> 8) & 0xFF));
        owf_buf_put(b, p + k, len);
        k += len;
    } while (k < n);
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521UL;
        s2 = (s2 + a) % 65521UL;
    }
    owf_buf_putc(b, (int)((s2 >> 8) & 0xFF));
    owf_buf_putc(b, (int)(s2 & 0xFF));
    owf_buf_putc(b, (int)((a >> 8) & 0xFF));
    owf_buf_putc(b, (int)(a & 0xFF));
}

/* Every picture the publication holds, in its order: entry k + 1 is image k.
 * The store goes in EscherStm, the pictures in EscherDelayStm. */
static void write_blips(writer *w, owf_buf *store, owf_buf *delay)
{
    const owf_doc *res = w->pub->res;
    int k, i, j;
    for (k = 0; res && k < res->nimages; k++) {
        const owf_image *im = &res->images[k];
        picture_kind pk;
        unsigned long uses = 0, off = (unsigned long)delay->length;
        size_t r, bse;
        if (!picture_kind_of(im, &pk)) {
            pk.rtype = 0xF01E; pk.inst = 0x6E0; pk.win = pk.mac = 6;
            w->lost_kinds++;             /* a kind the store has no record for: an empty entry */
            bse = open_rec(store, 0xF007, pk.win, 2);
            owf_buf_putc(store, 0);
            owf_buf_putc(store, 0);
            for (i = 0; i < 34; i++)
                owf_buf_putc(store, 0);
            close_rec(store, bse);
            continue;
        }
        for (i = 0; i < npages_all(w->pub); i++) {
            const owf_pub_page *pg = page_at(w->pub, i);
            for (j = 0; j < pg->nframes; j++)
                if (w->frame_chunk[i][j] >= 0 && pg->frames[j].kind == OWF_PUB_PICTURE && pg->frames[j].image == k)
                    uses++;
        }
        r = open_rec(delay, pk.rtype, pk.inst, 0);
        for (i = 0; i < 16; i++)
            owf_buf_putc(delay, 0);      /* the picture's id (MD4 in [MS-ODRAW]; zero here, not checked by our reader) */
        if (pk.metafile) {
            size_t zat, after;
            put32(delay, (unsigned long)im->length);
            put32(delay, 0); put32(delay, 0); put32(delay, 0); put32(delay, 0);
            put32(delay, 0); put32(delay, 0);
            zat = delay->length;
            put32(delay, 0);
            owf_buf_putc(delay, 0);      /* DEFLATE */
            owf_buf_putc(delay, 0xFE);
            put_zlib_stored(delay, im->data, im->length);
            after = delay->length;
            patch32(delay, zat, (unsigned long)(after - zat - 6));
        } else {
            owf_buf_putc(delay, 0xFF);   /* the tag */
            owf_buf_put(delay, im->data, im->length);
        }
        close_rec(delay, r);
        bse = open_rec(store, 0xF007, pk.win, 2);
        owf_buf_putc(store, pk.win);
        owf_buf_putc(store, pk.mac);
        for (i = 0; i < 16; i++)
            owf_buf_putc(store, 0);
        put16(store, 0xFF);
        put32(store, (unsigned long)(delay->length - off));
        put32(store, uses);
        put32(store, off);
        put32(store, 0);
        close_rec(store, bse);
    }
}

/* The first WordArt's size, typeface and colour (its story's first run), for the default style. */
static int wordart_format(const owf_pub *pub, owf_charfmt *out)
{
    int k, i;
    for (k = 0; k < npages_all(pub); k++) {
        const owf_pub_page *pg = page_at(pub, k);
        for (i = 0; i < pg->nframes; i++) {
            const owf_pub_frame *f = &pg->frames[i];
            if (f->wordart && f->story >= 0 && f->story < pub->nstories && pub->stories[f->story].nparas &&
                pub->stories[f->story].paras[0].nruns) {
                *out = pub->stories[f->story].paras[0].runs[0].fmt;
                return 1;
            }
        }
    }
    return 0;
}

/* Whether every run written says its own colour: only then can the default
 * style have one, which a run in the automatic colour would take instead. */
static int all_runs_coloured(const writer *w)
{
    int i, j, k;
    for (i = 0; i < w->pub->nstories; i++)
        if (w->story_out[i] >= 0)
            for (j = 0; j < w->pub->stories[i].nparas; j++)
                for (k = 0; k < w->pub->stories[i].paras[j].nruns; k++)
                    if (w->pub->stories[i].paras[j].runs[k].fmt.colour == OWF_COLOUR_AUTO)
                        return 0;
    return 1;
}

/* WordArt's text as 0xC0C0 holds it: UTF-16, its paragraphs apart by
 * CR, and a NUL. */
static void wordart_text(writer *w, const owf_story *st, owf_buf *out)
{
    int i, j;
    for (i = 0; i < st->nparas; i++) {
        if (i)
            put16(out, 0x0D);
        for (j = 0; j < st->paras[i].nruns; j++) {
            const owf_run *r = &st->paras[i].runs[j];
            if (r->kind == OWF_RUN_TEXT && r->text)
                put_utf16(out, r->text, strlen(r->text));
            else if (r->kind == OWF_RUN_TAB)
                put16(out, 9);
            else if (r->kind == OWF_RUN_LINEBREAK)
                put16(out, 0x0B);
        }
    }
    if (out->length)
        put16(out, 0);
    if (out->failed)
        w->failed = 1;
}

static void write_shape(writer *w, owf_buf *b, const owf_pub_frame *f, long chunk, unsigned long spid, int in_group)
{
    unsigned long pid[13], val[13];
    int n = 0, k, stype;
    size_t sp = open_rec(b, 0xF004, 0, 15), r, blk;
    long box[4];
    owf_buf art;
    owf_buf_init(&art);
    box[0] = f->left - w->pub->width_emu / 2;    /* EMU about the page's centre (PUB-FORMAT.md 7.2) */
    box[1] = f->top - w->pub->height_emu / 2;
    box[2] = f->right - w->pub->width_emu / 2;
    box[3] = f->bottom - w->pub->height_emu / 2;
    if (f->kind == OWF_PUB_GROUP) {
        /* a group: its own rectangle is its box, so its frames' boxes need no scaling */
        r = open_rec(b, 0xF009, 0, 1);
        for (k = 0; k < 4; k++)
            put32(b, (unsigned long)box[k]);
        close_rec(b, r);
    }
    if (f->kind == OWF_PUB_TEXT)
        stype = 202;
    else if (f->shape_type >= 0 && w->pub->version >= OWF_PUB_2003)
        stype = f->shape_type;           /* the file's own number (the 98/2000 forms number theirs apart: 9) */
    else if (f->kind == OWF_PUB_PICTURE)
        stype = 75;
    else if (f->kind == OWF_PUB_TABLE)
        stype = 1;
    else
        stype = f->shape_type >= 0 ? f->shape_type : 1;
    if (f->kind == OWF_PUB_GROUP)
        stype = 0;
    r = open_rec(b, 0xF00A, (unsigned)stype, 2);
    put32(b, spid);
    put32(b, (f->kind == OWF_PUB_GROUP ? 0x201UL : 0xA00UL) | (in_group ? 0x2UL : 0));   /* group / child */
    close_rec(b, r);
    for (k = 0; k < 4; k++)
        if (f->inset[k] >= 0) {
            pid[n] = 0x81 + (unsigned)k;
            val[n++] = (unsigned long)f->inset[k];
        }
    if (f->kind == OWF_PUB_PICTURE && f->image >= 0 && w->pub->res && f->image < w->pub->res->nimages) {
        picture_kind pk;
        if (picture_kind_of(&w->pub->res->images[f->image], &pk)) {
            pid[n] = 0x4104;             /* the store's entry, from 1 */
            val[n++] = (unsigned long)f->image + 1;
        }
    }
    if (f->filled == 1) {
        pid[n] = 0x181;
        val[n++] = bgr(f->fill);
    }
    if (f->filled >= 0) {
        pid[n] = 0x1BF;
        val[n++] = f->filled ? 0x100010UL : 0x100000UL;
    }
    if (f->lined == 1) {
        pid[n] = 0x1C0;
        val[n++] = bgr(f->line);
    }
    if (f->line_width >= 0) {
        pid[n] = 0x1CB;
        val[n++] = (unsigned long)f->line_width;
    }
    if (f->lined >= 0) {
        pid[n] = 0x1FF;
        val[n++] = f->lined ? 0x80008UL : 0x80000UL;
    }
    if (f->wordart && f->story >= 0 && f->story < w->pub->nstories)
        wordart_text(w, &w->pub->stories[f->story], &art);
    if (art.length) {
        pid[n] = 0xC0C0;                 /* WordArt's text: its data after the table (PUB-FORMAT.md 7.4) */
        val[n++] = (unsigned long)art.length;
    }
    if (n) {
        r = open_rec(b, 0xF00B, (unsigned)n, 3);
        for (k = 0; k < n; k++) {
            put16(b, (unsigned)pid[k]);
            put32(b, val[k]);
        }
        owf_buf_put(b, art.data, art.length);
        close_rec(b, r);
    }
    owf_buf_free(&art);
    if (in_group) {
        r = open_rec(b, 0xF00F, 0, 0);   /* the box in its group's rectangle: the same numbers */
        for (k = 0; k < 4; k++)
            put32(b, (unsigned long)box[k]);
        close_rec(b, r);
    } else {
        r = open_rec(b, 0xF010, 0, 0);   /* the box */
        blk = open_block(b);
        for (k = 0; k < 4; k++)
            f32(b, (unsigned)k + 1, 0x20, (unsigned long)box[k], 0);
        close_block(b, blk);
        close_rec(b, r);
    }
    r = open_rec(b, 0xF011, 0, 0);       /* its Contents chunk */
    blk = open_block(b);
    f32(b, 0x01, 0x68, (unsigned long)chunk, 0);
    close_block(b, blk);
    close_rec(b, r);
    if ((f->story >= 0 && f->story < w->pub->nstories && w->story_out[f->story] >= 0) || f->text_id >= 0) {
        int has = f->story >= 0 && f->story < w->pub->nstories && w->story_out[f->story] >= 0;
        r = open_rec(b, 0xF00D, 0, 0);   /* its text */
        blk = open_block(b);
        f32(b, 0x01, 0x20, has ? (unsigned long)w->story_id[w->story_out[f->story]] : (unsigned long)f->text_id, 0);
        close_block(b, blk);
        close_rec(b, r);
    }
    close_rec(b, sp);
}

/* A frame whose group is written goes in that group's container, not on the page. */
static int in_written_group(const writer *w, int k, int i)
{
    int p = page_at(w->pub, k)->frames[i].parent;
    return p >= 0 && p < i && w->frame_chunk[k][p] >= 0;
}

/* A frame, and when it is a group, its container: the group's own shape,
 * then its frames (MS-ODRAW 2.2.16), as deep as the reader reads. */
static void write_tree(writer *w, owf_buf *b, int k, int i, unsigned long base, int *shapes, int depth)
{
    const owf_pub_page *pg = page_at(w->pub, k);
    const owf_pub_frame *f = &pg->frames[i];
    int j;
    size_t c = 0;
    if (f->kind == OWF_PUB_PICTURE && (f->image < 0 || !w->pub->res || f->image >= w->pub->res->nimages))
        w->lost_pictures++;              /* a picture frame the reader found no picture for */
    if (f->kind == OWF_PUB_GROUP)
        c = open_rec(b, 0xF003, 0, 15);
    write_shape(w, b, f, w->frame_chunk[k][i], base + (unsigned long)++*shapes, depth > 0);
    if (f->kind == OWF_PUB_GROUP) {
        for (j = i + 1; j < pg->nframes; j++)
            if (pg->frames[j].parent == i && w->frame_chunk[k][j] >= 0) {
                if (depth + 1 < 16)
                    write_tree(w, b, k, j, base, shapes, depth + 1);
            }
        close_rec(b, c);
    }
}

static void write_escher(writer *w, owf_buf *b, owf_buf *delay)
{
    const owf_pub *pub = w->pub;
    size_t r, r2, dg, sp;
    int k, i;
    r = open_rec(b, 0xF000, 0, 15);
    r2 = open_rec(b, 0xF006, 0, 0);
    put32(b, 1024);
    put32(b, 2);
    put32(b, 1);
    put32(b, 1);
    close_rec(b, r2);
    {
        owf_buf store;
        owf_buf_init(&store);
        write_blips(w, &store, delay);
        if (store.failed)
            w->failed = 1;
        r2 = open_rec(b, 0xF001, (unsigned)(w->pub->res ? w->pub->res->nimages : 0), 15);
        owf_buf_put(b, store.data, store.length);
        close_rec(b, r2);
        owf_buf_free(&store);
    }
    close_rec(b, r);
    for (k = 0; k < npages_all(pub); k++) {
        const owf_pub_page *pg = page_at(pub, k);
        int shapes = 0;
        unsigned long base = 1024UL * (unsigned long)(k + 1);
        for (i = 0; i < pg->nframes; i++)
            if (w->frame_chunk[k][i] >= 0)
                shapes++;
        put32(b, 0);
        dg = open_rec(b, 0xF002, 0, 15);
        r = open_rec(b, 0xF008, (unsigned)(k + 1), 0);
        put32(b, (unsigned long)shapes + 1);
        put32(b, base + (unsigned long)shapes);
        close_rec(b, r);
        r = open_rec(b, 0xF003, 0, 15);
        sp = open_rec(b, 0xF004, 0, 15);
        r2 = open_rec(b, 0xF009, 0, 1);
        put32(b, 0); put32(b, 0); put32(b, 0); put32(b, 0);
        close_rec(b, r2);
        r2 = open_rec(b, 0xF00A, 0, 2);
        put32(b, base);
        put32(b, 5);
        close_rec(b, r2);
        close_rec(b, sp);
        shapes = 0;
        for (i = 0; i < pg->nframes; i++)
            if (w->frame_chunk[k][i] >= 0 && !in_written_group(w, k, i))
                write_tree(w, b, k, i, base, &shapes, 0);
        close_rec(b, r);
        close_rec(b, dg);
    }
}

/* ---- the text storage (PUB-FORMAT.md 6) ---- */

typedef struct {
    unsigned long end;                   /* in characters from the text's start */
    owf_buf props;
} wrun;

typedef struct {
    wrun *r;
    int n, cap;
} wruns;

static int same_props(const owf_buf *a, const owf_buf *b)
{
    return a->length == b->length && (!a->length || !memcmp(a->data, b->data, a->length));
}

static wrun *add_run(writer *w, wruns *rs, unsigned long end)
{
    if (rs->n == rs->cap) {
        int cap = rs->cap ? 2 * rs->cap : 64;
        wrun *grown = owf_realloc(rs->r, (size_t)cap * sizeof *grown);
        if (!grown) {
            w->failed = 1;
            return NULL;
        }
        rs->r = grown;
        rs->cap = cap;
    }
    rs->r[rs->n].end = end;
    owf_buf_init(&rs->r[rs->n].props);
    return &rs->r[rs->n++];
}

static void free_runs(wruns *rs)
{
    int i;
    for (i = 0; i < rs->n; i++)
        owf_buf_free(&rs->r[i].props);
    owf_free(rs->r);
}

static int colour_place(writer *w, unsigned long rgb)
{
    unsigned long v = bgr(rgb);
    int i;
    for (i = 1; i < w->ncolours; i++)
        if (w->colours[i] == v)
            return i;
    if (w->ncolours >= 256)
        return -1;
    w->colours[w->ncolours] = v;
    return w->ncolours++;
}

static void char_props(writer *w, owf_buf *b, const owf_charfmt *f)
{
    if (f->flags & OWF_BOLD)
        head(b, 0x202, 0x08, 1);
    if (f->flags & OWF_ITALIC)
        head(b, 0x203, 0x08, 1);
    if (f->size > 0)
        f32(b, 0x20C, 0x20, (unsigned long)f->size * 635UL, 1);
    if (f->flags & OWF_UNDERLINE)
        f16(b, 0x21E, 0x10, 0x801, 1);
    if (f->font >= 0) {
        size_t a = open_field(b, 0x224, 0x88, 1), s;
        int slot;
        for (slot = 0; slot < 2; slot++) {
            s = open_field(b, (unsigned)slot, 0x88, 1);
            f16(b, 0x0B, 0x18, (unsigned)f->font, 1);
            close_block(b, s);
        }
        close_block(b, a);
    }
    if (f->colour != OWF_COLOUR_AUTO) {
        int k = colour_place(w, f->colour);
        if (k < 0)
            w->lost_colours++;               /* PL is full: the run keeps the automatic colour */
        if (k > 0) {
            size_t a = open_field(b, 0x244, 0x88, 1);
            f32(b, 0x200, 0x20, (unsigned long)k, 1);
            close_block(b, a);
        }
    }
}

/* FDPC or FDPP: 512-byte pages, as many runs a page as fit. */
static void fdp_pages(owf_buf *out, const wruns *rs)
{
    int k = 0;
    while (k < rs->n && !out->failed) {
        unsigned char pg[512];
        int n = 0, i;
        size_t room = 512 - 8, top = 512;
        while (k + n < rs->n) {
            size_t need = 6 + (rs->r[k + n].props.length ? ((rs->r[k + n].props.length + 4 + 1) & ~(size_t)1) : 0);
            if (need > room || n == 120)
                break;
            room -= need;
            n++;
        }
        if (!n)
            n = 1;                       /* props too big for a page: written without them */
        memset(pg, 0, sizeof pg);
        pg[0] = (unsigned char)n;
        pg[1] = (unsigned char)(n >> 8);
        for (i = 0; i < n; i++) {
            const wrun *r = &rs->r[k + i];
            unsigned long end = TEXT_AT + 2 * r->end;
            size_t len = r->props.length + 4, off = 0;
            pg[8 + 4 * i] = (unsigned char)end;
            pg[9 + 4 * i] = (unsigned char)(end >> 8);
            pg[10 + 4 * i] = (unsigned char)(end >> 16);
            pg[11 + 4 * i] = (unsigned char)(end >> 24);
            if (r->props.length && len + 8 + 6 * (size_t)n <= top) {
                top = (top - len) & ~(size_t)1;
                pg[top] = (unsigned char)len;
                pg[top + 1] = (unsigned char)(len >> 8);
                memcpy(pg + top + 4, r->props.data, r->props.length);
                off = top;
            }
            pg[8 + 4 * n + 2 * i] = (unsigned char)off;
            pg[9 + 4 * n + 2 * i] = (unsigned char)(off >> 8);
        }
        owf_buf_put(out, pg, sizeof pg);
        k += n;
    }
}

typedef struct {
    char name[5];
    unsigned index;
    owf_buf data;
} qchunk;

typedef struct {
    qchunk c[64 + 2 * 256];
    int n;
} qdir;

static owf_buf *qadd(qdir *d, const char *name, unsigned index)
{
    qchunk *c;
    if (d->n >= (int)(sizeof d->c / sizeof d->c[0]))
        return NULL;
    c = &d->c[d->n++];
    memcpy(c->name, name, 4);
    c->name[4] = 0;
    c->index = index;
    owf_buf_init(&c->data);
    return &c->data;
}

/* TOKN n: story n's links (PUB-FORMAT.md 6.7) */
typedef struct {
    unsigned long from, length;
    const char *href;
} wlink;

static void write_tokn(owf_buf *b, const wlink *l, int n, unsigned long story_length)
{
    owf_buf strs;
    unsigned long last_len = 0, last_kind = 0, at;
    size_t *offs;
    int i;
    put32(b, (unsigned long)n);
    put32(b, 12);
    put32(b, 0x1FFFF);
    for (i = 0; i < n; i++)
        put32(b, l[i].from);
    put32(b, story_length);
    for (i = 0; i < n; i++) {
        unsigned long kind = !strncmp(l[i].href, "mailto:", 7) ? 4 : 1;
        size_t blk = open_block(b);
        if (!i)
            f32(b, 0x200, 0x20, 0x8C0, 1);
        if (!i || l[i].length != last_len)
            f32(b, 0x201, 0x20, l[i].length, 1);
        if (!i || kind != last_kind)
            f32(b, 0x202, 0x20, kind, 1);
        last_len = l[i].length;
        last_kind = kind;
        close_block(b, blk);
    }
    for (i = 0; i < n; i++) {
        size_t blk = open_block(b);
        f32(b, 0x200, 0x20, (unsigned long)i, 1);
        close_block(b, blk);
    }
    owf_buf_init(&strs);
    offs = owf_malloc((size_t)(n ? n : 1) * sizeof *offs);
    if (!offs) {
        b->failed = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        owf_buf u;
        unsigned long units;
        offs[i] = strs.length;
        owf_buf_init(&u);
        units = put_utf16(&u, l[i].href, strlen(l[i].href));
        put16(&strs, (unsigned)units);
        owf_buf_put(&strs, u.data, u.length);
        owf_buf_free(&u);
    }
    put32(b, (unsigned long)(4 * (size_t)n + strs.length));
    put32(b, (unsigned long)n);
    for (at = 0; at < 12; at++)
        owf_buf_putc(b, 0);
    for (i = 0; i < n; i++)
        put32(b, (unsigned long)(4 * (size_t)n + offs[i]));
    owf_buf_put(b, strs.data, strs.length);
    if (strs.failed)
        b->failed = 1;
    owf_buf_free(&strs);
    owf_free(offs);
}

static void write_quill(writer *w, owf_buf *out)
{
    const owf_pub *pub = w->pub;
    qdir *d = owf_calloc(1, sizeof *d);
    owf_buf *text, *b;
    wruns cr = { NULL, 0, 0 }, pr = { NULL, 0, 0 };
    unsigned long units = 0, *lengths;
    int s, i, j;

    if (!d || !(lengths = owf_calloc((size_t)(w->nout ? w->nout : 1), sizeof *lengths))) {
        owf_free(d);
        w->failed = 1;
        return;
    }
    w->ncolours = 1;
    w->colours[0] = 0x08000000UL;        /* place 0: the scheme's first colour, as Publisher's block 0 */
    text = qadd(d, "TEXT", 0);

    for (s = 0; s < w->nout && !w->failed; s++) {
        const owf_story *st = &pub->stories[w->story_of[s]];
        unsigned long start = units, *marks = NULL;
        wlink *links = NULL;
        int nlinks = 0, caplinks = 0;
        if (st->nparas && !(marks = owf_malloc((size_t)st->nparas * sizeof *marks))) {
            w->failed = 1;
            break;
        }
        for (i = 0; i < st->nparas && !w->failed; i++) {
            const owf_para *p = &st->paras[i];
            int had = 0;
            for (j = 0; j < p->nruns; j++) {
                const owf_run *r = &p->runs[j];
                unsigned long before = units;
                wrun *wr;
                if (r->kind == OWF_RUN_TEXT && r->text)
                    units += put_utf16(text, r->text, strlen(r->text));
                else if (r->kind == OWF_RUN_TAB) {
                    put16(text, 9);
                    units++;
                } else if (r->kind == OWF_RUN_LINEBREAK) {
                    put16(text, 0x0B);
                    units++;
                }
                if (units == before)
                    continue;
                if (!(wr = add_run(w, &cr, units)))
                    break;
                char_props(w, &wr->props, &r->fmt);
                if (had && cr.n > 1 && same_props(&cr.r[cr.n - 2].props, &wr->props)) {
                    cr.r[cr.n - 2].end = units;  /* written the same (a colour PL had no room for): one run, as read back */
                    owf_buf_free(&wr->props);
                    cr.n--;
                }
                had = 1;
                if (r->kind == OWF_RUN_TEXT && r->href && *r->href) {
                    if (nlinks && links[nlinks - 1].href && !strcmp(links[nlinks - 1].href, r->href) &&
                        links[nlinks - 1].from + links[nlinks - 1].length == before - start) {
                        links[nlinks - 1].length += units - before;
                    } else {
                        if (nlinks == caplinks) {
                            int cap = caplinks ? 2 * caplinks : 8;
                            wlink *grown = owf_realloc(links, (size_t)cap * sizeof *grown);
                            if (!grown) {
                                w->failed = 1;
                                break;
                            }
                            links = grown;
                            caplinks = cap;
                        }
                        links[nlinks].from = before - start;
                        links[nlinks].length = units - before;
                        links[nlinks++].href = r->href;
                    }
                }
            }
            marks[i] = units - start;
            put16(text, 0x0D);           /* the paragraph's mark */
            units++;
            if (had)
                cr.r[cr.n - 1].end = units;      /* the mark takes its last run's look */
            else if (!add_run(w, &cr, units))
                break;
            {
                wrun *wp = add_run(w, &pr, units);
                if (!wp)
                    break;
                if (p->fmt.align == OWF_ALIGN_RIGHT || p->fmt.align == OWF_ALIGN_CENTRE)
                    f16(&wp->props, 0x204, 0x18, p->fmt.align == OWF_ALIGN_RIGHT ? 1 : 2, 1);
            }
        }
        lengths[s] = units - start;
        if (nlinks && !w->failed && (b = qadd(d, "TOKN", (unsigned)s)) != NULL)
            write_tokn(b, links, nlinks, lengths[s]);
        /* TCD: a table story's cells, each one's last paragraph mark (PUB-FORMAT.md 6.8) */
        if (st->nparas && st->paras[0].table_id >= 0 && !w->failed && (b = qadd(d, "TCD ", (unsigned)s)) != NULL) {
            unsigned long ncells = 0;
            for (i = 0; i < st->nparas; i++)
                if (i + 1 == st->nparas || st->paras[i + 1].table_row != st->paras[i].table_row ||
                    st->paras[i + 1].table_col != st->paras[i].table_col)
                    ncells++;
            put32(b, ncells - 1);
            put32(b, 0);
            put32(b, 0xFF00);
            for (i = 0; i < st->nparas; i++)
                if (i + 1 == st->nparas)
                    put32(b, lengths[s]);
                else if (st->paras[i + 1].table_row != st->paras[i].table_row ||
                         st->paras[i + 1].table_col != st->paras[i].table_col)
                    put32(b, marks[i]);
        }
        owf_free(marks);
        owf_free(links);
    }

    if (!w->failed) {
        /* STRS: each story's length; SYID: each story's text id */
        b = qadd(d, "STRS", 0);
        put32(b, (unsigned long)w->nout);
        put32(b, 8);
        put32(b, 0xFF);
        for (s = 0; s < w->nout; s++)
            put32(b, lengths[s]);
        b = qadd(d, "SYID", 0);
        put32(b, 0);
        put32(b, (unsigned long)w->nout);
        for (s = 0; s < w->nout; s++)
            put32(b, (unsigned long)w->story_id[s]);
        /* FONT: the names, after five u32 and the offsets */
        b = qadd(d, "FONT", 0);
        {
            int nf = pub->res ? pub->res->nfonts : 0;
            owf_buf names;
            unsigned long *offs = owf_calloc((size_t)(nf ? nf : 1), sizeof *offs);
            owf_buf_init(&names);
            if (!offs)
                w->failed = 1;
            for (i = 0; i < nf && offs; i++) {
                owf_buf u;
                unsigned long n;
                const char *name = pub->res->fonts[i].name ? pub->res->fonts[i].name : "";
                offs[i] = (unsigned long)(4 * (size_t)nf + names.length);
                owf_buf_init(&u);
                n = put_utf16(&u, name, strlen(name));
                put16(&names, (unsigned)n);
                owf_buf_put(&names, u.data, u.length);
                owf_buf_free(&u);
            }
            put32(b, 0);
            put32(b, (unsigned long)nf);
            put32(b, 0);
            put32(b, 0);
            put32(b, 0);
            for (i = 0; i < nf && offs; i++)
                put32(b, offs[i]);
            owf_buf_put(b, names.data, names.length);
            owf_buf_free(&names);
            owf_free(offs);
        }
        /* STSH 0 and 1: one style, the default. Every run says its own size and
         * typeface, so the default's matter only to WordArt, whose text has no
         * runs: it takes the first WordArt's size, typeface and colour, as read. */
        {
            owf_buf dc;
            owf_charfmt def;
            owf_buf_init(&dc);
            if (wordart_format(w->pub, &def)) {
                if (def.size > 0)
                    f32(&dc, 0x20C, 0x20, (unsigned long)def.size * 635UL, 1);
                if (def.font >= 0) {
                    size_t a = open_field(&dc, 0x224, 0x88, 1), sl;
                    for (i = 0; i < 2; i++) {
                        sl = open_field(&dc, (unsigned)i, 0x88, 1);
                        f16(&dc, 0x0B, 0x18, (unsigned)def.font, 1);
                        close_block(&dc, sl);
                    }
                    close_block(&dc, a);
                }
                if (def.colour != OWF_COLOUR_AUTO && all_runs_coloured(w)) {
                    int c = colour_place(w, def.colour);
                    if (c > 0) {
                        size_t a = open_field(&dc, 0x244, 0x88, 1);
                        f32(&dc, 0x200, 0x20, (unsigned long)c, 1);
                        close_block(&dc, a);
                    }
                }
            }
            b = qadd(d, "STSH", 0);
            put32(b, 0);
            put32(b, 0);
            b = qadd(d, "STSH", 1);
            put32(b, (unsigned long)(0x14 + 8 + 2 * 8 + dc.length));
            put32(b, 2);
            put32(b, 0);
            put32(b, 0);
            put32(b, 0);
            put32(b, 8);
            put32(b, (unsigned long)(16 + dc.length));
            put16(b, 0);
            put32(b, (unsigned long)(4 + dc.length));
            owf_buf_put(b, dc.data, dc.length);
            put16(b, 0);
            put16(b, 0);
            put32(b, 4);
            put16(b, 0);
            if (dc.failed)
                w->failed = 1;
            owf_buf_free(&dc);
        }
        b = qadd(d, "FDPC", 0);
        fdp_pages(b, &cr);
        b = qadd(d, "FDPP", 0);
        fdp_pages(b, &pr);
        if (w->ncolours > 1) {           /* PL: the text's colours (PUB-FORMAT.md 6.9) */
            b = qadd(d, "PL  ", 0);
            put32(b, (unsigned long)w->ncolours);
            put32(b, 0x1C);
            put32(b, 0);
            for (i = 0; i < w->ncolours; i++) {
                size_t blk = open_block(b);
                f32(b, 0x201, 0x20, w->colours[i], 1);
                close_block(b, blk);
            }
        }
    }

    /* the directory: up to 20 entries in the first block (0x18), then more blocks at the end */
    if (!w->failed) {
        int first = d->n < 20 ? d->n : 20, k;
        size_t *at = owf_malloc((size_t)d->n * sizeof *at), dir2 = 0;
        if (!at) {
            w->failed = 1;
        } else {
            unsigned char z[TEXT_AT];
            memset(z, 0, sizeof z);
            owf_buf_put(out, z, sizeof z);
            for (k = 0; k < d->n; k++) {
                if (out->length % 2)
                    owf_buf_putc(out, 0);
                at[k] = out->length;
                owf_buf_put(out, d->c[k].data.data, d->c[k].data.length);
                if (d->c[k].data.failed)
                    w->failed = 1;
            }
            if (d->n > first) {          /* the rest: blocks of 21 at the end, 512-aligned */
                while (out->length % 512)
                    owf_buf_putc(out, 0);
                dir2 = out->length;
                for (k = first; k < d->n; k += 21) {
                    int m = d->n - k < 21 ? d->n - k : 21, e;
                    size_t blk = out->length;
                    put16(out, 0x1F8);
                    put16(out, (unsigned)m);
                    put32(out, k + 21 < d->n ? (unsigned long)(blk + 512) : 0xFFFFFFFFUL);
                    for (e = 0; e < m; e++) {
                        const qchunk *c = &d->c[k + e];
                        int istext = !strcmp(c->name, "STRS") || !strcmp(c->name, "TCD ");
                        put16(out, 0x18);
                        owf_buf_put(out, c->name, 4);
                        put16(out, c->index);
                        put32(out, 1);
                        owf_buf_put(out, istext ? "PLC " : c->name, 4);
                        put32(out, (unsigned long)at[k + e]);
                        put32(out, (unsigned long)c->data.length);
                    }
                    while ((out->length - blk) % 512)
                        owf_buf_putc(out, 0);
                }
            }
            if (!out->failed) {
                unsigned char *p = out->data;
                memcpy(p, "CHNKINK ", 8);
                p[0x08] = 4; p[0x0A] = 7;
                p[0x0C] = (unsigned char)d->n;
                p[0x0D] = (unsigned char)(d->n >> 8);
                p[0x18] = 0xF8; p[0x19] = 0x01;
                p[0x1A] = (unsigned char)first;
                patch32(out, 0x1C, dir2 ? (unsigned long)dir2 : 0xFFFFFFFFUL);
                for (k = 0; k < first; k++) {
                    unsigned char *e = p + 0x20 + 24 * k;
                    const qchunk *c = &d->c[k];
                    int istext = !strcmp(c->name, "STRS") || !strcmp(c->name, "TCD ");
                    e[0] = 0x18;
                    memcpy(e + 2, c->name, 4);
                    e[6] = (unsigned char)c->index;
                    e[7] = (unsigned char)(c->index >> 8);
                    e[8] = 1;
                    memcpy(e + 12, istext ? "PLC " : c->name, 4);
                    patch32(out, 0x20 + 24 * (size_t)k + 16, (unsigned long)at[k]);
                    patch32(out, 0x20 + 24 * (size_t)k + 20, (unsigned long)c->data.length);
                }
            }
            owf_free(at);
        }
    }
    for (i = 0; i < d->n; i++)
        owf_buf_free(&d->c[i].data);
    owf_free(d);
    owf_free(lengths);
    free_runs(&cr);
    free_runs(&pr);
}

/* \005DocumentSummaryInformation ([MS-OLEPS]) with the writing program's version (0x17): 12.0 */
static void write_summary(owf_buf *b)
{
    static const unsigned char fmtid[16] = { 0x02, 0xD5, 0xCD, 0xD5, 0x9C, 0x2E, 0x1B, 0x10,
                                             0x93, 0x97, 0x08, 0x00, 0x2B, 0x2C, 0xF9, 0xAE };
    int i;
    put16(b, 0xFFFE);
    put16(b, 0);
    put32(b, 0x00020006UL);
    for (i = 0; i < 16; i++)
        owf_buf_putc(b, 0);
    put32(b, 1);
    owf_buf_put(b, fmtid, 16);
    put32(b, 48);
    put32(b, 8 + 8 + 8);                 /* the section: size, count, one (id, offset), the value */
    put32(b, 1);
    put32(b, 0x17);
    put32(b, 16);
    put16(b, 3);
    put16(b, 0);
    put32(b, 12UL << 16);
}

int owf_pub_write(const owf_pub *pub, unsigned char **data, size_t *length, owf_report *report)
{
    writer w;
    owf_buf contents, quill, escher, delay, summary;
    owf_cfbw *cf = NULL;
    int k, s, rc = OWF_OK, n = pub ? npages_all(pub) : 0;

    if (!pub || !data || !length)
        return OWF_ERR_FORMAT;
    *data = NULL;
    *length = 0;
    memset(&w, 0, sizeof w);
    w.pub = pub;
    w.report = report;
    owf_buf_init(&contents);
    owf_buf_init(&quill);
    owf_buf_init(&escher);
    owf_buf_init(&delay);
    owf_buf_init(&summary);
    w.page_chunk = owf_calloc((size_t)(n ? n : 1), sizeof *w.page_chunk);
    w.frame_chunk = owf_calloc((size_t)(n ? n : 1), sizeof *w.frame_chunk);
    w.story_out = owf_calloc((size_t)(pub->nstories ? pub->nstories : 1), sizeof *w.story_out);
    w.story_id = owf_calloc((size_t)(pub->nstories ? pub->nstories : 1), sizeof *w.story_id);
    w.story_of = owf_calloc((size_t)(pub->nstories ? pub->nstories : 1), sizeof *w.story_of);
    if (!w.page_chunk || !w.frame_chunk || !w.story_out || !w.story_id || !w.story_of)
        w.failed = 1;
    for (k = 0; k < n && !w.failed; k++) {
        const owf_pub_page *pg = page_at(pub, k);
        if (!(w.frame_chunk[k] = owf_calloc((size_t)(pg->nframes ? pg->nframes : 1), sizeof **w.frame_chunk)))
            w.failed = 1;
    }

    /* the stories, in the publication's order, but WordArt's own (not written yet); each keeps the
     * text id its first frame names, and one with no frame (the scratch area's, a cut-short file's)
     * gets a new one */
    for (s = 0; s < pub->nstories && w.story_out; s++)
        w.story_out[s] = -1;
    for (s = 0; s < pub->nstories && !w.failed; s++) {
        long id = -1;
        int wordart = 0, i;
        for (k = 0; k < n && id < 0; k++) {
            const owf_pub_page *pg = page_at(pub, k);
            for (i = 0; i < pg->nframes; i++)
                if (pg->frames[i].story == s) {
                    if (pg->frames[i].wordart) {
                        wordart = 1;
                        continue;
                    }
                    if (pg->frames[i].kind != OWF_PUB_GROUP) {
                        id = pg->frames[i].text_id;
                        wordart = 0;
                        break;
                    }
                }
        }
        if (wordart && id < 0)
            continue;
        for (i = 0; i < w.nout && id >= 0; i++)
            if (w.story_id[i] == id)
                id = -1;                 /* taken: a new one below */
        w.story_out[s] = w.nout;
        w.story_of[w.nout] = s;
        w.story_id[w.nout++] = id;
    }
    for (s = 0; s < w.nout; s++)
        if (w.story_id[s] < 0) {          /* past every text id a story or a frame has */
            long id = 1;
            int i;
            for (i = 0; i < w.nout; i++)
                if (w.story_id[i] >= id)
                    id = w.story_id[i] + 1;
            for (k = 0; k < n; k++) {
                const owf_pub_page *pg = page_at(pub, k);
                for (i = 0; i < pg->nframes; i++)
                    if (pg->frames[i].text_id >= id)
                        id = pg->frames[i].text_id + 1;
            }
            w.story_id[s] = id;
        }

    if (!w.failed)
        write_pages(&w);
    if (!w.failed)
        write_scheme(&w);
    if (!w.failed)
        write_contents(&w, &contents);
    if (!w.failed)
        write_quill(&w, &quill);
    if (!w.failed)
        write_escher(&w, &escher, &delay);
    write_summary(&summary);
    if (contents.failed || quill.failed || escher.failed || delay.failed || summary.failed)
        w.failed = 1;

    if (!w.failed && !(cf = owf_cfbw_new()))
        w.failed = 1;
    if (!w.failed) {
        static const unsigned char clsid[16] = { 0x01, 0x12, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 };
        if (owf_cfbw_set_clsid(cf, "", clsid) != OWF_OK ||
            owf_cfbw_add(cf, "Contents", contents.data, contents.length) != OWF_OK ||
            owf_cfbw_add(cf, "Quill/QuillSub/CONTENTS", quill.data, quill.length) != OWF_OK ||
            owf_cfbw_add(cf, "Escher/EscherStm", escher.data, escher.length) != OWF_OK ||
            (delay.length && owf_cfbw_add(cf, "Escher/EscherDelayStm", delay.data, delay.length) != OWF_OK) ||
            owf_cfbw_add(cf, "\005DocumentSummaryInformation", summary.data, summary.length) != OWF_OK)
            w.failed = 1;
        else
            rc = owf_cfbw_finish(cf, data, length);
    }
    if (w.failed)
        rc = OWF_ERR_MEMORY;
    if (rc == OWF_OK) {
        if (w.lost_pictures)
            owf_report_add(report, OWF_NOTE_LOST, "%d picture frame(s) with no picture saved as empty frames", w.lost_pictures);
        if (w.lost_kinds)
            owf_report_add(report, OWF_NOTE_LOST, "%d picture(s) of a kind .pub does not store saved as empty entries", w.lost_kinds);
        if (w.lost_colours)
            owf_report_add(report, OWF_NOTE_APPROX, "%d run(s) of text past the first 255 colours saved in the automatic colour",
                           w.lost_colours);
        if (pub->scratch_frames)
            owf_report_add(report, OWF_NOTE_LOST, "%d object(s) on the scratch area were not kept", pub->scratch_frames);
    }

    owf_cfbw_free(cf);
    owf_buf_free(&contents);
    owf_buf_free(&quill);
    owf_buf_free(&escher);
    owf_buf_free(&delay);
    owf_buf_free(&summary);
    for (k = 0; k < n && w.frame_chunk; k++)
        owf_free(w.frame_chunk[k]);
    owf_free(w.frame_chunk);
    owf_free(w.page_chunk);
    owf_free(w.story_out);
    owf_free(w.story_id);
    owf_free(w.story_of);
    for (k = 0; k < w.nchunks; k++)
        owf_buf_free(&w.chunks[k].body);
    owf_free(w.chunks);
    return rc;
}
