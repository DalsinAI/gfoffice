/*
 * libowf: Microsoft Publisher files (.pub), OpenPublisher's phase 0 reader.
 *
 * .pub has no published specification. What this file knows was worked out
 * from sample files' bytes alone (a clean room): docs/gfoffice/PUB-FORMAT.md
 * records each fact, the samples and offsets it came from, and whether it is
 * verified or a hypothesis. The container is an OLE compound file
 * ([MS-CFB], owf_cfb.c); the summary's version is read after [MS-OLEPS].
 *
 * Every read is bounded by what the file holds: a damaged or cut-short file
 * gives what can be read, with notes, never a read past the end.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"
#include "owf_pub.h"

#ifdef OWF_HAVE_ZLIB
#include "owf_zlib.h"
#endif

#include <stdlib.h>
#include <string.h>

#define EMU_PER_TWIP 635L
#define MAX_DEPTH 24             /* Escher containers and Contents blocks nest no deeper */

/* ---- Bounded little-endian reads ---- */

typedef struct {
    const unsigned char *p;
    size_t n;
} span;

static int has(span s, size_t at, size_t want)
{
    return at <= s.n && want <= s.n - at;
}

static unsigned rd16(span s, size_t at)
{
    return has(s, at, 2) ? (unsigned)(s.p[at] | (s.p[at + 1] << 8)) : 0;
}

static unsigned long rd32(span s, size_t at)
{
    return has(s, at, 4) ? (unsigned long)s.p[at] | ((unsigned long)s.p[at + 1] << 8) |
                               ((unsigned long)s.p[at + 2] << 16) | ((unsigned long)s.p[at + 3] << 24)
                         : 0;
}

static long signed32(unsigned long v)
{
    v &= 0xFFFFFFFFUL;
    return v & 0x80000000UL ? -(long)((~v & 0xFFFFFFFFUL) + 1) : (long)v;
}

static long rds32(span s, size_t at)
{
    return signed32(rd32(s, at));
}

static span sub(span s, size_t at, size_t n)
{
    span r;
    if (!has(s, at, 0)) {
        r.p = s.p;
        r.n = 0;
        return r;
    }
    r.p = s.p + at;
    r.n = has(s, at, n) ? n : s.n - at;
    return r;
}

/* ---- The tagged values of Contents and of Escher's client records ----
 * (id, type) byte pairs; the type gives what follows: nothing, 2, 4, 8 or
 * 16 bytes, or a block with its own u32 length (counting the length). An
 * array's items have id 0. PUB-FORMAT.md, section 4. */

typedef struct {
    unsigned id, type;
    unsigned long value;         /* a 2- or 4-byte value; the first 4 of 8 or 16 */
    unsigned long value2;        /* the next 4 of 8 */
    span body;                   /* a block's contents, after its length */
} field;

static int fixed_size(unsigned type)
{
    switch (type) {
    case 0x00: case 0x08: case 0x78: return 0;
    case 0x10: case 0x18: return 2;
    case 0x20: case 0x68: case 0x70: case 0xB8: return 4;
    case 0x28: return 8;
    case 0x38: return 16;
    case 0x88: case 0x90: case 0x98: case 0xA0: case 0xC0: return -2;   /* a block */
    default: return -1;
    }
}

/* The next field of block s from *at: 1, or 0 at the end, or -1 when the
 * bytes stop making sense (the walk ends there). */
static int next_field(span s, size_t *at, field *f)
{
    int size;
    if (*at >= s.n)
        return 0;
    if (!has(s, *at, 2))
        return -1;
    memset(f, 0, sizeof *f);
    f->id = s.p[*at];
    f->type = s.p[*at + 1];
    size = fixed_size(f->type);
    if (size == -1)
        return -1;
    if (size == -2) {
        unsigned long len = rd32(s, *at + 2);
        if (!has(s, *at + 2, 4) || len < 4 || !has(s, *at + 2, len))
            return -1;
        f->body = sub(s, *at + 6, len - 4);
        *at += 2 + len;
        return 1;
    }
    if (!has(s, *at + 2, (size_t)size))
        return -1;
    if (size == 2)
        f->value = rd16(s, *at + 2);
    else if (size >= 4)
        f->value = rd32(s, *at + 2);
    if (size >= 8)
        f->value2 = rd32(s, *at + 6);
    *at += 2 + (size_t)size;
    return 1;
}

/* A block at offset at of the stream: its fields, after the u32 length. */
static span block_at(span s, unsigned long at)
{
    unsigned long len = rd32(s, at);
    span none = { NULL, 0 };
    if (!has(s, at, 4) || len < 4 || !has(s, at, len))
        return none;
    return sub(s, at + 4, len - 4);
}

/* The first field with that id at the top of a block. */
static int find_field(span s, unsigned id, field *out)
{
    size_t at = 0;
    field f;
    while (next_field(s, &at, &f) > 0)
        if (f.id == id) {
            *out = f;
            return 1;
        }
    return 0;
}

static long field_value(span s, unsigned id, long otherwise)
{
    field f;
    return find_field(s, id, &f) ? (long)f.value : otherwise;
}

/* ---- The reader's state ---- */

typedef struct {
    unsigned kind;               /* 0: an empty slot */
    unsigned long offset;
    long parent;                 /* an index into the chunk table; -1: none */
} chunk;

typedef struct {
    long chunk;                  /* the Contents chunk the shape is (Escher's client data) */
    long text_id;                /* -1 */
    int type;                    /* the shape number (the shape record's instance) */
    int blip;                    /* 1-based picture; 0: none */
    int wordart;                 /* WordArt's text: into pub->stories; -1: none */
    long box[4];                 /* left, top, right, bottom: EMU from the page's centre */
    int has_box;
    int group;                   /* index of its group in shapes; -1 */
    int is_group;
    long grect[4];               /* a group's own coordinate rectangle */
} eshape;

typedef struct {
    owf_pub *pub;
    owf_report *report;
    int failed;                  /* memory ran out */
    owf_cfb cfb;
    span contents;
    unsigned char *contents_data;
    chunk *chunks;
    long nchunks;
    eshape *shapes;
    int nshapes, capshapes;
    long *story_ids;             /* SYID: text id by story */
    int nstory_ids;
    int *blip_image;             /* picture n (1-based) as res->images index; -1 */
    int nblips;
} reader;

/* ---- Small helpers ---- */

static char *dup_text(const char *s, size_t n)
{
    char *d = owf_malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

static void free_story(owf_story *story)
{
    int i, j;
    for (i = 0; i < story->nparas; i++) {
        for (j = 0; j < story->paras[i].nruns; j++) {
            owf_free(story->paras[i].runs[j].text);
            owf_free(story->paras[i].runs[j].href);
        }
        owf_free(story->paras[i].runs);
    }
    owf_free(story->paras);
    memset(story, 0, sizeof *story);
}

static void free_page(owf_pub_page *page)
{
    owf_free(page->name);
    owf_free(page->frames);
}

void owf_pub_free(owf_pub *pub)
{
    int i;
    if (!pub)
        return;
    for (i = 0; i < pub->npages; i++)
        free_page(&pub->pages[i]);
    owf_free(pub->pages);
    for (i = 0; i < pub->nmasters; i++)
        free_page(&pub->masters[i]);
    owf_free(pub->masters);
    for (i = 0; i < pub->nstories; i++)
        free_story(&pub->stories[i]);
    owf_free(pub->stories);
    owf_doc_free(pub->res);
    owf_free(pub);
}

static void *grow(reader *r, void *array, int *cap, int count, size_t size)
{
    void *p;
    int want;
    if (count < *cap)
        return array;
    want = *cap ? *cap * 2 : 8;
    p = owf_realloc(array, (size_t)want * size);
    if (!p) {
        r->failed = 1;
        return NULL;
    }
    *cap = want;
    return p;
}

static owf_pub_page *add_page(reader *r, int master)
{
    owf_pub *pub = r->pub;
    owf_pub_page *p;
    int *n = master ? &pub->nmasters : &pub->npages, *cap = master ? &pub->capmasters : &pub->cappages;
    owf_pub_page **arr = master ? &pub->masters : &pub->pages;
    p = grow(r, *arr, cap, *n, sizeof **arr);
    if (!p)
        return NULL;
    *arr = p;
    p = &(*arr)[(*n)++];
    memset(p, 0, sizeof *p);
    p->master = -1;
    return p;
}

static owf_pub_frame *add_frame(reader *r, owf_pub_page *page)
{
    owf_pub_frame *f = grow(r, page->frames, &page->capframes, page->nframes, sizeof *page->frames);
    if (!f)
        return NULL;
    page->frames = f;
    f = &page->frames[page->nframes++];
    memset(f, 0, sizeof *f);
    f->parent = -1;
    f->shape_type = -1;
    f->text_id = -1;
    f->story = -1;
    f->image = -1;
    f->chunk = -1;
    return f;
}

static int emu_twips(long emu)
{
    return (int)(emu >= 0 ? (emu + EMU_PER_TWIP / 2) / EMU_PER_TWIP : -((-emu + EMU_PER_TWIP / 2) / EMU_PER_TWIP));
}

/* A frame's box from a box about the page's centre. */
static void set_box(owf_pub *pub, owf_pub_frame *f, const long box[4])
{
    f->left = box[0] + pub->width_emu / 2;
    f->top = box[1] + pub->height_emu / 2;
    f->right = box[2] + pub->width_emu / 2;
    f->bottom = box[3] + pub->height_emu / 2;
    f->x = emu_twips(f->left);
    f->y = emu_twips(f->top);
    f->width = emu_twips(f->right) - f->x;
    f->height = emu_twips(f->bottom) - f->y;
}

static int story_of(const reader *r, long text_id)
{
    int i;
    for (i = 0; i < r->nstory_ids; i++)
        if (r->story_ids[i] == text_id)
            return i < r->pub->nstories ? i : -1;
    return -1;
}

/* ---- The summary's version ([MS-OLEPS]: DocumentSummaryInformation's
 * property 0x17, VT_I4: the major version in the high word) ---- */

static void read_app_version(reader *r)
{
    unsigned char *data;
    size_t length;
    span s;
    unsigned long sect, n, i;
    if (owf_cfb_read(&r->cfb, "\005DocumentSummaryInformation", &data, &length) != OWF_OK)
        return;
    s.p = data;
    s.n = length;
    sect = rd32(s, 44);
    n = rd32(s, sect + 4);
    for (i = 0; i < n && i < 256 && has(s, sect + 8 + 8 * i, 8); i++) {
        unsigned long pid = rd32(s, sect + 8 + 8 * i), at = sect + rd32(s, sect + 12 + 8 * i);
        if (pid == 0x17 && rd16(s, at) == 3 && has(s, at + 4, 4)) {
            unsigned long v = rd32(s, at + 4);
            r->pub->app_major = (int)(v >> 16);
            r->pub->app_minor = (int)(v & 0xFFFF);
        }
    }
    owf_free(data);
}

/* ---- Text: the Quill storage (PUB-FORMAT.md, section 6) ----
 * "CHNKINK " then a directory of 24-byte entries: u16, a 4-letter name,
 * u16 index, u32, a 4-letter type, u32 offset, u32 length. The directory
 * is a chain of blocks, each with an 8-byte head (u16 0x1F8, u16 entries
 * in the block, u32 the next block or 0xFFFFFFFF); the first head is at
 * 0x18. TEXT is UTF-16LE; STRS gives each story's length; SYID each
 * story's text id. */

typedef struct {
    span text, strs, syid, font;
} quill;

static int read_quill_dir(span q, quill *out, owf_report *report)
{
    unsigned long head = 0x18, total, seen = 0;
    int blocks = 0;
    memset(out, 0, sizeof *out);
    if (q.n < 0x20 || memcmp(q.p, "CHNKINK ", 8)) {
        owf_report_add(report, OWF_NOTE_LOST, "The text storage is not one this reader knows; the text is left out");
        return 0;
    }
    total = rd16(q, 0x0C);
    while (seen < total && has(q, head, 8) && blocks++ < 256) {
        unsigned long n = rd16(q, head + 2), i, next = rd32(q, head + 4);
        for (i = 0; i < n && seen < total && has(q, head + 8 + 24 * i, 24); i++, seen++) {
            size_t e = head + 8 + 24 * i;
            span c = sub(q, rd32(q, e + 16), rd32(q, e + 20));
            if (c.n < rd32(q, e + 20))
                owf_report_add(report, OWF_NOTE_APPROX, "The file is cut short in its text; some text is missing");
            if (!memcmp(q.p + e + 2, "TEXT", 4) && !rd16(q, e + 6))
                out->text = c;
            else if (!memcmp(q.p + e + 2, "STRS", 4))
                out->strs = c;
            else if (!memcmp(q.p + e + 2, "SYID", 4))
                out->syid = c;
            else if (!memcmp(q.p + e + 2, "FONT", 4))
                out->font = c;
        }
        if (next == 0xFFFFFFFFUL || next <= head)
            break;                           /* the end, or a loop */
        head = next;
    }
    return 1;
}

static void put_utf16_char(owf_buf *b, span t, size_t *at)
{
    unsigned long c = rd16(t, *at);
    *at += 2;
    if (c >= 0xD800 && c < 0xDC00 && has(t, *at, 2)) {
        unsigned long lo = rd16(t, *at);
        if (lo >= 0xDC00 && lo < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            *at += 2;
        }
    }
    if (c >= 0xD800 && c < 0xE000)
        c = 0xFFFD;
    owf_buf_put_utf8(b, c);
}

/* Characters [from, to) of TEXT as a story's paragraphs. */
static void make_story(reader *r, owf_story *story, span text, size_t from, size_t to)
{
    owf_builder b;
    owf_buf run;
    size_t at = from * 2, end = to * 2;
    int open = 0;
    owf_builder_init(&b, r->pub->res, story);
    owf_buf_init(&run);
    if (end > text.n)
        end = text.n & ~(size_t)1;
    while (at < end) {
        unsigned c = rd16(text, at);
        if (c >= 0x20) {
            put_utf16_char(&run, text, &at);
            open = 1;
            continue;
        }
        if (run.length) {
            owf_builder_text(&b, (const char *)run.data, run.length);
            run.length = 0;
        }
        at += 2;
        if (c == 0x0D) {                     /* a paragraph's end */
            owf_builder_end_para(&b);
            open = 0;
            continue;
        }
        open = 1;
        if (c == 0x09)
            owf_builder_special(&b, OWF_RUN_TAB, OWF_FIELD_PAGE);
        else if (c == 0x0A || c == 0x0B)     /* a line break (hypothesis) */
            owf_builder_special(&b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
        else
            owf_report_add(r->report, OWF_NOTE_APPROX, "Text control character U+%04X left out", c);
    }
    if (run.length)
        owf_builder_text(&b, (const char *)run.data, run.length);
    if (run.failed)
        r->failed = 1;
    owf_buf_free(&run);
    if (open)
        owf_builder_end_para(&b);
    if (owf_builder_done(&b) != OWF_OK)
        r->failed = 1;
}

static void read_text(reader *r)
{
    long id = owf_cfb_find_path(&r->cfb, "Quill/QuillSub/CONTENTS");
    unsigned char *data;
    size_t length, chars, pos = 0;
    span q;
    quill qd;
    unsigned long n, i, base;

    if (id < 0 || owf_cfb_read_entry(&r->cfb, (unsigned long)id, &data, &length) != OWF_OK)
        return;                              /* none: frames with text say so */
    q.p = data;
    q.n = length;
    if (!read_quill_dir(q, &qd, r->report)) {
        owf_free(data);
        return;
    }
    chars = qd.text.n / 2;

    /* STRS: u32 count, u32 8, u32 0xFF, then count story lengths */
    n = rd32(qd.strs, 0);
    if (!qd.strs.n || !has(qd.strs, 12, 4 * (size_t)n) || n > 100000) {
        n = chars ? 1 : 0;                   /* no story table: the text as one story */
        if (qd.strs.n)
            owf_report_add(r->report, OWF_NOTE_APPROX, "The story table is damaged; the text is read as one story");
        qd.strs.n = 0;
    }
    if (n) {
        r->pub->stories = owf_calloc(n, sizeof *r->pub->stories);
        if (!r->pub->stories) {
            r->failed = 1;
            owf_free(data);
            return;
        }
        r->pub->capstories = (int)n;
    }
    for (i = 0; i < n && !r->failed; i++) {
        size_t len = qd.strs.n ? rd32(qd.strs, 12 + 4 * i) : chars;
        if (pos > chars)
            pos = chars;
        if (len > chars - pos) {
            owf_report_add(r->report, OWF_NOTE_APPROX, "A story runs past the end of the text; it is cut short");
            len = chars - pos;
        }
        make_story(r, &r->pub->stories[r->pub->nstories++], qd.text, pos, pos + len);
        pos += len;
    }

    /* SYID: u32, u32 count, then count text ids */
    n = rd32(qd.syid, 4);
    if (n && n <= 100000 && has(qd.syid, 8, 4 * (size_t)n)) {
        r->story_ids = owf_malloc(n * sizeof *r->story_ids);
        if (!r->story_ids)
            r->failed = 1;
        for (i = 0; r->story_ids && i < n; i++)
            r->story_ids[i] = (long)rd32(qd.syid, 8 + 4 * i);
        r->nstory_ids = r->story_ids ? (int)n : 0;
    }

    /* FONT: five u32 (the second the count), offsets from +0x14, then each
     * name as a u16 length and UTF-16 */
    n = rd32(qd.font, 4);
    base = 0x14;
    for (i = 0; i < n && i < 1024 && has(qd.font, base + 4 * i, 4) && !r->failed; i++) {
        unsigned long at = base + rd32(qd.font, base + 4 * i);
        unsigned len = rd16(qd.font, at);
        size_t k = at + 2;
        owf_buf name;
        if (!len || !has(qd.font, at + 2, 2 * (size_t)len))
            continue;
        owf_buf_init(&name);
        while (k < at + 2 + 2 * (size_t)len)
            put_utf16_char(&name, qd.font, &k);
        owf_buf_putc(&name, 0);
        if (name.failed || owf_doc_font(r->pub->res, (const char *)name.data, OWF_FONT_ANY) < 0)
            r->failed = 1;
        owf_buf_free(&name);
    }
    owf_free(data);
}

/* ---- Pictures: Escher's picture store (PUB-FORMAT.md, section 8) ---- */

static const char *picture_mime(span p, size_t *skip)
{
    size_t k;
    for (k = 0; k < 64 && has(p, k, 8); k++) {
        const unsigned char *q = p.p + k;
        *skip = k;
        if (!memcmp(q, "\211PNG\r\n\032\n", 8))
            return "image/png";
        if (q[0] == 0xFF && q[1] == 0xD8 && q[2] == 0xFF)
            return "image/jpeg";
        if (!memcmp(q, "GIF87a", 6) || !memcmp(q, "GIF89a", 6))
            return "image/gif";
    }
    *skip = 0;
    return NULL;
}

static int add_picture(reader *r, unsigned rtype, span body, int n)
{
    char name[24] = "picture";
    size_t skip, k = 7;
    const char *mime;
    int index, d = 1;
    while (d <= n / 10)
        d *= 10;
    for (; d && k < sizeof name - 1; d /= 10)
        name[k++] = (char)('0' + n / d % 10);
    name[k] = 0;
    if (rtype == 0xF01A || rtype == 0xF01B) {
        /* a metafile: 16-byte id, then a 34-byte header (u32 size, a box, a
           size, u32 compressed size, u8 compression 0, u8), then zlib data */
        unsigned long full = rd32(body, 16), packed = rd32(body, 44);
        span z = sub(body, 50, packed);
        if (body.p && has(body, 48, 1) && body.p[48] == 0 && full && full < 0x4000000UL) {
#ifdef OWF_HAVE_ZLIB
            const char *kind = rtype == 0xF01A ? "image/x-emf" : "image/x-wmf";
            unsigned char *out = owf_malloc(full);
            z_stream zs;
            int ok = 0;
            if (!out) {
                r->failed = 1;
                return -1;
            }
            memset(&zs, 0, sizeof zs);
            if (inflateInit2(&zs, 15) == Z_OK) {
                zs.next_in = (unsigned char *)z.p;
                zs.avail_in = (unsigned)z.n;
                zs.next_out = out;
                zs.avail_out = (unsigned)full;
                ok = inflate(&zs, Z_FINISH) == Z_STREAM_END && zs.total_out == full;
                inflateEnd(&zs);
            }
            index = ok ? owf_doc_add_image(r->pub->res, name, kind, out, full, 0, 0, NULL) : -1;
            owf_free(out);
            if (ok && index < 0)
                r->failed = 1;
            if (!ok)
                owf_report_add(r->report, OWF_NOTE_LOST, "A metafile picture is damaged; it is left out");
            return index;
#else
            index = owf_doc_add_image(r->pub->res, name, rtype == 0xF01A ? "image/x-emf-deflate" : "image/x-wmf-deflate",
                                      z.p, z.n, 0, 0, NULL);
            if (index < 0)
                r->failed = 1;
            return index;
#endif
        }
        owf_report_add(r->report, OWF_NOTE_LOST, "A metafile picture is stored in a way not known yet; it is left out");
        return -1;
    }
    if (rtype == 0xF01F) {                   /* a DIB: 16-byte id, a tag byte, then BITMAPINFOHEADER */
        span d = sub(body, 17, body.n);
        if (rd32(d, 0) == 40) {
            index = owf_doc_add_image(r->pub->res, name, "image/x-dib", d.p, d.n, 0, 0, NULL);
            if (index < 0)
                r->failed = 1;
            return index;
        }
    }
    mime = picture_mime(body, &skip);
    if (!mime) {
        owf_report_add(r->report, OWF_NOTE_LOST, "A picture of a kind not known yet (record %04X) is left out", rtype);
        return -1;
    }
    index = owf_doc_add_image(r->pub->res, name, mime, body.p + skip, body.n - skip, 0, 0, NULL);
    if (index < 0)
        r->failed = 1;
    return index;
}

/* ---- Escher: the drawing (PUB-FORMAT.md, section 7) ----
 * Records: u16 (version in the low 4 bits, instance above), u16 type, u32
 * length; version 15 holds records. */

typedef struct {
    unsigned type, inst, ver;
    span body;
} erec;

static int next_rec(span s, size_t *at, erec *e)
{
    unsigned vi;
    unsigned long len;
    if (!has(s, *at, 8))
        return 0;
    vi = rd16(s, *at);
    e->type = rd16(s, *at + 2);
    len = rd32(s, *at + 4);
    e->ver = vi & 15;
    e->inst = vi >> 4;
    if (e->type < 0xF000 || !has(s, *at + 8, len))
        return 0;
    e->body = sub(s, *at + 8, len);
    *at += 8 + len;
    return 1;
}

static void read_blips(reader *r, span store, span delay)
{
    size_t at = 0;
    erec e;
    int n = 0;
    while (next_rec(store, &at, &e) && !r->failed) {
        int *p;
        unsigned long size, off;
        if (e.type != 0xF007)
            continue;
        n++;
        p = owf_realloc(r->blip_image, (size_t)n * sizeof *p);
        if (!p) {
            r->failed = 1;
            return;
        }
        r->blip_image = p;
        r->nblips = n;
        p[n - 1] = -1;
        size = rd32(e.body, 20);
        off = rd32(e.body, 28);
        if (has(delay, off, 8) && size) {
            erec pic;
            size_t pat = off;
            if (next_rec(delay, &pat, &pic))
                p[n - 1] = add_picture(r, pic.type, pic.body, n);
            else
                owf_report_add(r->report, OWF_NOTE_LOST, "A picture is cut short; it is left out");
        } else if (size) {
            owf_report_add(r->report, OWF_NOTE_LOST, "A picture is missing from the file; it is left out");
        }
    }
}

/* A client record's tagged values (u32 length, then fields). */
static span client(span body)
{
    unsigned long len = rd32(body, 0);
    return sub(body, 4, len >= 4 ? len - 4 : 0);
}

static int add_eshape(reader *r)
{
    eshape *s = grow(r, r->shapes, &r->capshapes, r->nshapes, sizeof *r->shapes);
    if (!s)
        return -1;
    r->shapes = s;
    s = &r->shapes[r->nshapes];
    memset(s, 0, sizeof *s);
    s->chunk = -1;
    s->text_id = -1;
    s->type = -1;
    s->group = -1;
    s->wordart = -1;
    return r->nshapes++;
}

/* WordArt's text (UTF-16, with a closing NUL) as a story of its own. */
static int wordart_story(reader *r, span text)
{
    owf_story *st;
    size_t len = text.n / 2;
    if (!len)
        return -1;
    st = grow(r, r->pub->stories, &r->pub->capstories, r->pub->nstories, sizeof *st);
    if (!st)
        return -1;
    r->pub->stories = st;
    st = &r->pub->stories[r->pub->nstories];
    memset(st, 0, sizeof *st);
    while (len && !rd16(text, 2 * (len - 1)))
        len--;
    make_story(r, st, text, 0, len);
    return r->pub->nstories++;
}

static void read_shape(reader *r, span body, int group, int index)
{
    size_t at = 0;
    erec e;
    eshape *s = &r->shapes[index];
    s->group = group;
    while (next_rec(body, &at, &e)) {
        span c;
        int k;
        switch (e.type) {
        case 0xF00A:                         /* the shape: its number in the instance */
            s->type = (int)e.inst;
            break;
        case 0xF009:                         /* a group's coordinate rectangle */
            for (k = 0; k < 4; k++)
                s->grect[k] = rds32(e.body, 4 * (size_t)k);
            s->is_group = 1;
            break;
        case 0xF010:                         /* the box: left, top, right, bottom about the page's centre */
            c = client(e.body);
            for (k = 0; k < 4; k++) {
                field f;
                if (!find_field(c, (unsigned)k + 1, &f))
                    break;
                s->box[k] = signed32(f.value);
            }
            s->has_box = k == 4;
            break;
        case 0xF00F:                         /* a box in its group's rectangle */
            for (k = 0; k < 4; k++)
                s->box[k] = rds32(e.body, 4 * (size_t)k);
            s->has_box = 2;
            break;
        case 0xF011:                         /* the Contents chunk it is */
            s->chunk = field_value(client(e.body), 1, -1);
            break;
        case 0xF00D:                         /* its text's id */
            s->text_id = field_value(client(e.body), 1, -1);
            break;
        case 0xF00B: {                       /* properties: u16 id, u32 value; 0x4104 the picture */
            unsigned i;                      /* (bit 0x8000: its data follows the table, in order) */
            size_t extra = 6 * (size_t)e.inst;
            for (i = 0; i < e.inst && has(e.body, 6 * (size_t)i, 6); i++) {
                unsigned pid = rd16(e.body, 6 * (size_t)i);
                unsigned long v = rd32(e.body, 6 * (size_t)i + 2);
                if (pid == 0x4104)
                    s->blip = (int)v;
                if (pid & 0x8000) {
                    if (pid == 0xC0C0 && s->wordart < 0)   /* WordArt's text */
                        s->wordart = wordart_story(r, sub(e.body, extra, v));
                    if (extra < e.body.n && v <= e.body.n - extra)
                        extra += v;
                    else
                        extra = e.body.n;
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

/* A group container (or a drawing's top one): its first shape is the
 * group's own; the rest are its members. */
static void read_group(reader *r, span body, int parent, int depth)
{
    size_t at = 0;
    erec e;
    int first = 1, self = parent;
    if (depth > MAX_DEPTH) {
        owf_report_add(r->report, OWF_NOTE_LOST, "Groups nest too deeply; the innermost are left out");
        return;
    }
    while (next_rec(body, &at, &e) && !r->failed) {
        if (e.type == 0xF004) {
            int i = add_eshape(r);
            if (i < 0)
                return;
            read_shape(r, e.body, first ? parent : self, i);
            if (first)
                self = i;
            first = 0;
        } else if (e.type == 0xF003) {
            read_group(r, e.body, self, depth + 1);
        }
    }
}

static void read_escher(reader *r)
{
    long id = owf_cfb_find_path(&r->cfb, "Escher/EscherStm"), did = owf_cfb_find_path(&r->cfb, "Escher/EscherDelayStm");
    unsigned char *data = NULL, *delay = NULL;
    size_t length = 0, dlength = 0, at = 0;
    span s, d;

    if (id < 0 || owf_cfb_read_entry(&r->cfb, (unsigned long)id, &data, &length) != OWF_OK)
        return;
    if (did >= 0 && owf_cfb_read_entry(&r->cfb, (unsigned long)did, &delay, &dlength) != OWF_OK)
        delay = NULL;
    s.p = data;
    s.n = length;
    d.p = delay;
    d.n = delay ? dlength : 0;
    /* drawing-group and drawing containers, with a u32 between them */
    while (has(s, at, 8) && !r->failed) {
        erec e;
        size_t was = at;
        if (!next_rec(s, &at, &e) || e.ver != 15) {
            at = was + 4;
            continue;
        }
        if (e.type == 0xF000) {              /* the drawing group: its picture store */
            size_t k = 0;
            erec c;
            while (next_rec(e.body, &k, &c))
                if (c.type == 0xF001)
                    read_blips(r, c.body, d);
        } else if (e.type == 0xF002) {       /* a drawing: its shapes */
            size_t k = 0;
            erec c;
            while (next_rec(e.body, &k, &c))
                if (c.type == 0xF003)
                    read_group(r, c.body, -1, 0);
        }
    }
    owf_free(data);
    owf_free(delay);
}

/* A shape's box about its page's centre, through its groups. */
static int shape_box(const reader *r, int i, long out[4], int depth)
{
    const eshape *s = &r->shapes[i];
    int k;
    if (!s->has_box || depth > MAX_DEPTH)
        return 0;
    if (s->has_box == 1) {
        for (k = 0; k < 4; k++)
            out[k] = s->box[k];
        return 1;
    } else {
        long g[4];
        const eshape *grp;
        double sx, sy;
        if (s->group < 0 || !shape_box(r, s->group, g, depth + 1))
            return 0;
        grp = &r->shapes[s->group];
        sx = grp->grect[2] != grp->grect[0] ? (double)(g[2] - g[0]) / (double)(grp->grect[2] - grp->grect[0]) : 1.0;
        sy = grp->grect[3] != grp->grect[1] ? (double)(g[3] - g[1]) / (double)(grp->grect[3] - grp->grect[1]) : 1.0;
        out[0] = g[0] + (long)((double)(s->box[0] - grp->grect[0]) * sx);
        out[1] = g[1] + (long)((double)(s->box[1] - grp->grect[1]) * sy);
        out[2] = g[0] + (long)((double)(s->box[2] - grp->grect[0]) * sx);
        out[3] = g[1] + (long)((double)(s->box[3] - grp->grect[1]) * sy);
        return 1;
    }
}

/* ---- Contents, the 2002-and-later form (PUB-FORMAT.md, section 5) ---- */

static int read_chunk_table(reader *r)
{
    span hdr = block_at(r->contents, 0x2C), trailer, arr;
    field f;
    size_t at = 0;
    long n = 0;
    unsigned long toff;

    if (!find_field(hdr, 0x0A, &f))
        return 0;
    toff = f.value;
    trailer = block_at(r->contents, toff);
    if (!find_field(trailer, 0x03, &f))
        return 0;
    arr = f.body;
    /* count the items, then read them */
    while (next_field(arr, &at, &f) > 0)
        n++;
    if (!n || n > 1000000L)
        return 0;
    r->chunks = owf_calloc((size_t)n, sizeof *r->chunks);
    if (!r->chunks) {
        r->failed = 1;
        return 0;
    }
    r->nchunks = n;
    at = 0;
    n = 0;
    while (n < r->nchunks && next_field(arr, &at, &f) > 0) {
        chunk *c = &r->chunks[n++];
        c->parent = -1;
        if (f.type != 0x88)
            continue;                        /* 0x78: an empty slot */
        c->kind = (unsigned)field_value(f.body, 0x02, 0);
        c->offset = (unsigned long)field_value(f.body, 0x04, 0);
        c->parent = field_value(f.body, 0x05, -1);
        if (c->parent >= r->nchunks)
            c->parent = -1;
    }
    return 1;
}

static span chunk_body(const reader *r, long index)
{
    span none = { NULL, 0 };
    if (index < 0 || index >= r->nchunks || !r->chunks[index].kind)
        return none;
    return block_at(r->contents, r->chunks[index].offset);
}

/* The page chunk a chunk sits on, through its parents. */
static long page_of(const reader *r, long index)
{
    int steps = 0;
    while (index >= 0 && index < r->nchunks && steps++ < MAX_DEPTH) {
        if (r->chunks[index].kind == 0x43)
            return index;
        index = r->chunks[index].parent;
    }
    return -1;
}

static owf_pub_kind frame_kind(const reader *r, const eshape *s)
{
    unsigned kind = s->chunk >= 0 && s->chunk < r->nchunks ? r->chunks[s->chunk].kind : 0;
    if (kind == 0x10)
        return OWF_PUB_TABLE;
    if (s->is_group)
        return OWF_PUB_GROUP;
    if (s->blip > 0)
        return OWF_PUB_PICTURE;
    if (s->type == 202 || (s->text_id >= 0 && s->type < 0))
        return OWF_PUB_TEXT;                 /* a text box (shape 202); other shapes may hold text too */
    if (s->type == 20)
        return OWF_PUB_LINE;
    if (s->type >= 0)
        return OWF_PUB_SHAPE;                /* 0: a freeform */
    return OWF_PUB_OTHER;
}

/* Puts the shapes that sit on page chunk pc onto page, back to front,
 * groups before their members. */
static void place_shapes(reader *r, long pc, owf_pub_page *page)
{
    int i, *at;
    if (!r->nshapes)
        return;
    at = owf_malloc((size_t)r->nshapes * sizeof *at);
    if (!at) {
        r->failed = 1;
        return;
    }
    for (i = 0; i < r->nshapes && !r->failed; i++) {
        eshape *s = &r->shapes[i];
        owf_pub_frame *f;
        long box[4];
        at[i] = -1;
        if (s->chunk < 0 || page_of(r, s->chunk) != pc)
            continue;
        if (!shape_box(r, i, box, 0))
            continue;
        if (!(f = add_frame(r, page)))
            break;
        at[i] = page->nframes - 1;
        f->kind = frame_kind(r, s);
        f->chunk = s->chunk;
        f->shape_type = s->type;
        f->parent = s->group >= 0 && s->group < i ? at[s->group] : -1;
        set_box(r->pub, f, box);
        f->text_id = s->text_id;
        if (f->text_id < 0)
            f->text_id = field_value(chunk_body(r, s->chunk), 0x27, -1);
        if (f->text_id >= 0) {
            f->story = story_of(r, f->text_id);
            if (f->story < 0)
                owf_report_add(r->report, OWF_NOTE_LOST, "A frame's text is not in the file's text; it is left out");
        }
        if (s->blip > 0 && s->blip <= r->nblips)
            f->image = r->blip_image[s->blip - 1];
        if (s->wordart >= 0 && f->story < 0) {
            f->story = s->wordart;
            f->wordart = 1;
        }
        if (f->kind == OWF_PUB_TABLE) {
            span t = chunk_body(r, s->chunk);
            f->rows = (int)field_value(t, 0x66, 0);
            f->cols = (int)field_value(t, 0x67, 0);
        }
    }
    owf_free(at);
}

static void read_modern(reader *r)
{
    owf_pub *pub = r->pub;
    long root = -1, i, master_y = 0;
    long *masters = NULL;
    int nmasters = 0;
    span body;
    field f;

    pub->build_mark = r->contents.p[0x0C];
    switch (pub->build_mark) {
    case 0x13: pub->version = OWF_PUB_2003; break;
    case 0x15: pub->version = OWF_PUB_2007; break;
    case 0x18: pub->version = OWF_PUB_2010; break;
    case 0x1A: pub->version = OWF_PUB_2013; break;
    default: pub->version = OWF_PUB_MODERN; break;
    }
    if (!read_chunk_table(r)) {
        if (!r->failed)
            owf_report_add(r->report, OWF_NOTE_LOST, "The file's table of contents is damaged; no pages could be read");
        return;
    }
    for (i = 0; i < r->nchunks; i++)
        if (r->chunks[i].kind == 0x44) {
            root = i;
            break;
        }
    body = chunk_body(r, root);
    if (!body.n) {
        owf_report_add(r->report, OWF_NOTE_LOST, "The document's own record is missing; no pages could be read");
        return;
    }
    if (find_field(body, 0x12, &f)) {        /* the page: width, height in EMU */
        pub->width_emu = field_value(f.body, 0x01, 0);
        pub->height_emu = field_value(f.body, 0x02, 0);
    }
    if (!find_field(body, 0x02, &f))         /* the pages: masters, then pages, then the program's own */
        return;
    masters = owf_malloc(sizeof *masters * (size_t)(r->nchunks + 1));
    if (!masters) {
        r->failed = 1;
        return;
    }
    /* masters first (field 0x10 is 3) */
    {
        size_t at = 0;
        field item;
        while (next_field(f.body, &at, &item) > 0 && !r->failed) {
            long pc = (long)item.value;
            span pb = chunk_body(r, pc);
            field name, pos;
            owf_pub_page *page;
            if (pc < 0 || pc >= r->nchunks || r->chunks[pc].kind != 0x43 ||
                field_value(pb, 0x10, 0) != 3)
                continue;
            if (!(page = add_page(r, 1)))
                break;
            masters[nmasters++] = pc;
            if (find_field(pb, 0x0F, &name)) {
                owf_buf b;
                size_t k = 0;
                owf_buf_init(&b);
                while (has(name.body, k, 2) && rd16(name.body, k))
                    put_utf16_char(&b, name.body, &k);
                owf_buf_putc(&b, 0);
                page->name = b.failed ? NULL : dup_text((const char *)b.data, b.length - 1);
                owf_buf_free(&b);
            }
            if (nmasters == 1 && find_field(pb, 0x05, &pos))
                master_y = field_value(pos.body, 0x02, 0);
            place_shapes(r, pc, page);
        }
    }
    /* then the document's pages: not masters, field 0x06 set, at the
       masters' height on the workspace (the scratch area sits half a page
       up: PUB-FORMAT.md 5.4, a hypothesis on the corpus) */
    {
        size_t at = 0;
        field item;
        while (next_field(f.body, &at, &item) > 0 && !r->failed) {
            long pc = (long)item.value, m;
            span pb = chunk_body(r, pc);
            field f6, pos;
            owf_pub_page *page;
            int k;
            if (pc < 0 || pc >= r->nchunks || r->chunks[pc].kind != 0x43 || field_value(pb, 0x10, 0) == 3)
                continue;
            if (!find_field(pb, 0x06, &f6) || !f6.value ||
                (nmasters && find_field(pb, 0x05, &pos) && field_value(pos.body, 0x02, 0) != master_y)) {
                owf_pub_page scratch;
                memset(&scratch, 0, sizeof scratch);
                place_shapes(r, pc, &scratch);
                pub->scratch_frames += scratch.nframes;
                owf_free(scratch.frames);
                continue;
            }
            if (!(page = add_page(r, 0)))
                break;
            m = field_value(pb, 0x0D, -1);
            for (k = 0; k < nmasters; k++)
                if (masters[k] == m)
                    page->master = k;
            place_shapes(r, pc, page);
        }
    }
    owf_free(masters);
    if (pub->scratch_frames)
        owf_report_add(r->report, OWF_NOTE_INFO, "%d frames on the scratch area, off the pages, are not shown", pub->scratch_frames);
}

/* ---- Contents, the 98/2000 form (PUB-FORMAT.md, section 9) ----
 * A directory at the header's u32 0x16: u16 count, then 10-byte entries
 * (u16 flags, u16 id, u16 parent, u32 offset). Each chunk starts with u16
 * type, u16 mark. */

static void read_old(reader *r)
{
    owf_pub *pub = r->pub;
    span s = r->contents;
    unsigned long dir = rd32(s, 0x16), i, n = rd16(s, dir);
    unsigned long root = 0;
    int any = 0;

    for (i = 0; i < n && has(s, dir + 2 + 10 * i, 10); i++)
        if (rd16(s, dir + 4 + 10 * i) == 0x100)
            root = rd32(s, dir + 8 + 10 * i);
    if (!root || !has(s, root, 0x1C)) {
        owf_report_add(r->report, OWF_NOTE_LOST, "The document's own record is missing; no pages could be read");
        return;
    }
    switch (rd16(s, root + 2)) {
    case 0xB2D2: pub->version = OWF_PUB_98; break;
    case 0xB2E8: pub->version = OWF_PUB_2000; break;
    default: break;
    }
    if (owf_cfb_find_path(&r->cfb, "Quill/QuillSub/CONTENTS") < 0)
        pub->version = OWF_PUB_OLD;          /* no Quill storage (fdo59355-1: hypothesis) */
    else if (pub->version == OWF_PUB_UNKNOWN)
        pub->version = OWF_PUB_2000;
    pub->width_emu = (long)rd32(s, root + 0x14);
    pub->height_emu = (long)rd32(s, root + 0x18);
    if (pub->version == OWF_PUB_OLD) {
        owf_report_add(r->report, OWF_NOTE_LOST, "Pages, frames and text of this older format are not read yet");
        return;
    }
    /* pages: type 0x14 under the document, with u32 +0x14 set, in file order */
    for (i = 0; i < n && has(s, dir + 2 + 10 * i, 10) && !r->failed; i++) {
        size_t e = dir + 2 + 10 * i, list;
        unsigned long off = rd32(s, e + 6), count, k;
        owf_pub_page *page;
        if (rd16(s, e + 4) != 0x100 || rd16(s, off) != 0x14 || !rd32(s, off + 0x14))
            continue;
        if (!(page = add_page(r, 0)))
            break;
        any = 1;
        /* the frames: an owner (u16 in 98, u32 in 2000), then u16 count, u16,
           u16, u32, then the u16 ids */
        list = off + 0x1C + (pub->version == OWF_PUB_98 ? 2 : 4);
        count = rd16(s, list);
        for (k = 0; k < count && k < 4096 && has(s, list + 10 + 2 * k, 2); k++) {
            unsigned id = rd16(s, list + 10 + 2 * k);
            unsigned long j, coff = 0;
            unsigned type;
            long box[4];
            int b;
            owf_pub_frame *f;
            for (j = 0; j < n && has(s, dir + 2 + 10 * j, 10); j++)
                if (rd16(s, dir + 4 + 10 * j) == id)
                    coff = rd32(s, dir + 8 + 10 * j);
            if (!coff || !has(s, coff, 22))
                continue;
            if (!(f = add_frame(r, page)))
                break;
            type = rd16(s, coff);
            for (b = 0; b < 4; b++)
                box[b] = rds32(s, coff + 6 + 4 * (size_t)b);
            set_box(pub, f, box);
            f->chunk = (long)id;
            f->shape_type = (int)type;
            f->kind = type == 0x08 ? OWF_PUB_TEXT : type == 0x0A ? OWF_PUB_TABLE : type == 0x05 ? OWF_PUB_SHAPE : OWF_PUB_OTHER;
            if (type == 0x08 && has(s, coff + 0x58, 2))         /* a text frame: its text id */
                f->text_id = (long)rd16(s, coff + 0x58);
            if (type == 0x0A && has(s, coff + 0x66, 2)) {       /* a table: rows, columns, text id */
                f->rows = (int)rd16(s, coff + 0x56);
                f->cols = (int)rd16(s, coff + 0x50);
                f->text_id = (long)rd16(s, coff + 0x66);
            }
            if (f->text_id >= 0) {
                f->story = story_of(r, f->text_id);
                if (f->story < 0)
                    owf_report_add(r->report, OWF_NOTE_LOST, "A frame's text is not in the file's text; it is left out");
            }
        }
    }
    if (!any)
        owf_report_add(r->report, OWF_NOTE_LOST, "No pages were found");
    owf_report_add(r->report, OWF_NOTE_APPROX, "In this older format, master pages and pictures are not read yet");
}

/* ---- The entry points ---- */

const char *owf_pub_version_name(int version)
{
    switch (version) {
    case OWF_PUB_OLD: return "Publisher 97 or older";
    case OWF_PUB_98: return "Publisher 98";
    case OWF_PUB_2000: return "Publisher 2000";
    case OWF_PUB_2003: return "Publisher 2002/2003";
    case OWF_PUB_2007: return "Publisher 2007";
    case OWF_PUB_2010: return "Publisher 2010";
    case OWF_PUB_2013: return "Publisher 2013 or later";
    case OWF_PUB_MODERN: return "Publisher 2002 or later";
    default: return "unknown";
    }
}

int owf_pub_detect(const unsigned char *data, size_t length)
{
    owf_cfb cfb;
    unsigned char *c;
    size_t n;
    int score = 0;
    if (!owf_cfb_detect(data, length) || owf_cfb_open(&cfb, data, length) != OWF_OK)
        return 0;
    if (owf_cfb_read(&cfb, "Contents", &c, &n) == OWF_OK) {
        if (n >= 8 && c[0] == 0xE8 && c[1] == 0xAC)
            score = 95;
        owf_free(c);
    }
    owf_cfb_close(&cfb);
    return score;
}

int owf_pub_read(const unsigned char *data, size_t length, owf_pub **out, owf_report *report)
{
    reader r;
    int result;
    size_t n;

    *out = NULL;
    memset(&r, 0, sizeof r);
    r.report = report;
    if (!owf_cfb_detect(data, length))
        return OWF_ERR_FORMAT;
    result = owf_cfb_open(&r.cfb, data, length);
    if (result != OWF_OK)
        return result;
    if (owf_cfb_read(&r.cfb, "Contents", &r.contents_data, &n) != OWF_OK || n < 0x2C ||
        r.contents_data[0] != 0xE8 || r.contents_data[1] != 0xAC) {
        /* Publisher's own storages without a readable Contents: a damaged .pub */
        result = owf_cfb_find_path(&r.cfb, "Quill") >= 0 || owf_cfb_find_path(&r.cfb, "Escher") >= 0
                     ? OWF_ERR_CORRUPT : OWF_ERR_FORMAT;
        owf_free(r.contents_data);
        owf_cfb_close(&r.cfb);
        return result;
    }
    r.contents.p = r.contents_data;
    r.contents.n = n;
    if (!(r.pub = owf_calloc(1, sizeof *r.pub)) || !(r.pub->res = owf_doc_new())) {
        owf_free(r.pub);
        owf_free(r.contents_data);
        owf_cfb_close(&r.cfb);
        return OWF_ERR_MEMORY;
    }
    r.pub->build_mark = -1;
    if (r.cfb.cut_short)
        owf_report_add(report, OWF_NOTE_APPROX, "The file is cut short or damaged; some of it may be missing");
    read_app_version(&r);
    if (r.contents.p[2] == 0x22 && r.contents.p[4] == 0x2C && r.contents.p[5] == 0x01) {
        read_text(&r);
        read_old(&r);
    } else if (r.contents.p[2] == 0x2C && r.contents.p[4] == 0xE8 && r.contents.p[5] == 0x03) {
        read_text(&r);
        read_escher(&r);
        read_modern(&r);
    } else {
        owf_report_add(report, OWF_NOTE_LOST, "This form of Publisher file is not known yet");
    }
    r.pub->width = emu_twips(r.pub->width_emu);
    r.pub->height = emu_twips(r.pub->height_emu);
    owf_free(r.chunks);
    owf_free(r.shapes);
    owf_free(r.story_ids);
    owf_free(r.blip_image);
    owf_free(r.contents_data);
    owf_cfb_close(&r.cfb);
    if (r.failed) {
        owf_pub_free(r.pub);
        return OWF_ERR_MEMORY;
    }
    *out = r.pub;
    return OWF_OK;
}

int owf_pub_read_file(const char *path, owf_pub **pub, owf_report *report)
{
    unsigned char *data;
    size_t length;
    int result = owf_sys_read_file(path, &data, &length);
    *pub = NULL;
    if (result != OWF_OK)
        return result;
    result = owf_pub_read(data, length, pub, report);
    free(data);
    return result;
}
