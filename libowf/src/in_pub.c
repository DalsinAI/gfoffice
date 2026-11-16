/*
 * libowf: Microsoft Publisher files (.pub), GFOpenPressmaster's phase 0 reader.
 *
 * .pub has no published specification. What this file knows was worked out
 * from sample files' bytes alone (a clean room): docs/gfoffice/PUB-FORMAT.md
 * records each fact, the samples and offsets it came from, and whether it is
 * verified or a hypothesis. The container is an OLE compound file
 * ([MS-CFB], owf_cfb.c); the summary's version is read after [MS-OLEPS].
 *
 * Every read is bounded by what the file holds: a damaged or cut-short file
 * gives what can be read, with notes, never a read past the end.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"
#include "owf_inflate.h"
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
static int read_field(span s, size_t *at, field *f, int wide)
{
    int size;
    if (*at >= s.n)
        return 0;
    if (!has(s, *at, 2))
        return -1;
    memset(f, 0, sizeof *f);
    f->id = s.p[*at];
    f->type = s.p[*at + 1];
    if (wide) {                          /* the text storage's: 11-bit ids (PUB-FORMAT.md, 6.4) */
        f->id |= (f->type & 7u) << 8;
        f->type &= 0xF8;
    }
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

static int next_field(span s, size_t *at, field *f)
{
    return read_field(s, at, f, 0);
}

static int next_qfield(span s, size_t *at, field *f)
{
    return read_field(s, at, f, 1);
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
    /* its look ([MS-ODRAW] properties, PUB-FORMAT.md 7.4); -1: not said */
    int filled, lined;
    unsigned long fill, line;    /* the property's value: 0x00BBGGRR, or 0x08 in the top byte and a scheme place */
    int has_fill, has_line;
    long line_width;
    long inset[4];
} eshape;

/* A run of the text (FDPC or FDPP): the characters up to end (not counting)
 * take props. */
typedef struct {
    unsigned long end;           /* in characters from TEXT's start */
    span props;                  /* its property fields; empty: none */
} trun;

/* A paragraph style (STSH): its character and paragraph properties. */
typedef struct {
    owf_charfmt chars;
    owf_align align;
} pstyle;

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
    int *font_map;               /* the text storage's font n as res->fonts index; -1 */
    int nfont_map;
    unsigned long *text_colours; /* PL's colours as character colours (see text_colour); AUTO: none */
    int ntext_colours;
    trun *cruns;                 /* the character runs (FDPC), in the text's order */
    int ncruns, capcruns;
    trun *pruns;                 /* the paragraph runs (FDPP) */
    int npruns, cappruns;
    pstyle *styles;              /* the paragraph styles (STSH 1); style 0 is the default */
    int nstyles;
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
        owf_free(story->paras[i].style);             /* 1.4 */
        owf_free(story->paras[i].bookmarks);
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
    f->filled = f->lined = -1;
    f->line_width = -1;
    f->inset[0] = f->inset[1] = f->inset[2] = f->inset[3] = -1;
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

#define QUILL_MAX_FDPC 256

typedef struct {
    span text, strs, syid, font;
    unsigned long text_at;       /* TEXT's offset in the stream: the character runs count from it */
    span fdpc[QUILL_MAX_FDPC];   /* the character runs' 512-byte pages, in the directory's order */
    int nfdpc;
    span fdpp[QUILL_MAX_FDPC];   /* the paragraph runs' */
    int nfdpp;
    span stsh1;                  /* the style sheet's styles (STSH number 1) */
    span pl;                     /* the text's colours (PL number 0, PUB-FORMAT.md 6.9) */
    span tokn[QUILL_MAX_FDPC];   /* the hyperlinks: TOKN n is story n's (PUB-FORMAT.md 6.7) */
    unsigned tokn_story[QUILL_MAX_FDPC];
    int ntokn;
    span tcd[QUILL_MAX_FDPC];    /* a table's cells: TCD n is story n's (PUB-FORMAT.md 6.8) */
    unsigned tcd_story[QUILL_MAX_FDPC];
    int ntcd;
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
            if (!memcmp(q.p + e + 2, "TEXT", 4) && !rd16(q, e + 6)) {
                out->text = c;
                out->text_at = rd32(q, e + 16);
            }
            else if (!memcmp(q.p + e + 2, "FDPC", 4) && out->nfdpc < QUILL_MAX_FDPC)
                out->fdpc[out->nfdpc++] = c;
            else if (!memcmp(q.p + e + 2, "FDPP", 4) && out->nfdpp < QUILL_MAX_FDPC)
                out->fdpp[out->nfdpp++] = c;
            else if (!memcmp(q.p + e + 2, "STSH", 4) && rd16(q, e + 6) == 1)
                out->stsh1 = c;
            else if (!memcmp(q.p + e + 2, "TCD ", 4) && out->ntcd < QUILL_MAX_FDPC) {
                out->tcd_story[out->ntcd] = rd16(q, e + 6);
                out->tcd[out->ntcd++] = c;
            }
            else if (!memcmp(q.p + e + 2, "TOKN", 4) && out->ntokn < QUILL_MAX_FDPC) {
                out->tokn_story[out->ntokn] = rd16(q, e + 6);
                out->tokn[out->ntokn++] = c;
            }
            else if (!memcmp(q.p + e + 2, "PL  ", 4) && !rd16(q, e + 6))
                out->pl = c;
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

/* ---- Character formatting: FDPC (PUB-FORMAT.md, section 6.4) ----
 * 512-byte pages: u16 count, u16, u32, then count u32 run ends (byte
 * offsets in the stream, so TEXT's own offset counts), then count u16
 * offsets in the page of each run's properties: a u32 length (counting
 * itself), then fields with 11-bit ids. */

static int text_font(const reader *r, span block)
{
    size_t at = 0, in;
    field f, g;
    long first = -1, zero = -1;
    while (next_qfield(block, &at, &f) > 0) {
        long v = -1;
        if (f.type == 0x18 || f.type == 0x10)        /* the 98/2000 form: the slots as plain fields */
            v = (long)f.value;
        else if (f.type == 0x88) {                   /* 2002 and later: each slot a block */
            in = 0;
            while (next_qfield(f.body, &in, &g) > 0)
                if (g.type == 0x18 || g.type == 0x10) {
                    v = (long)g.value;
                    break;
                }
        }
        if (v < 0)
            continue;
        if (first < 0)
            first = v;
        if (f.id == 0 && zero < 0)
            zero = v;
    }
    if (zero < 0)
        zero = first;
    return zero >= 0 && zero < r->nfont_map ? r->font_map[zero] : -1;
}

/* Character properties (FDPC's, or a style's) applied onto fmt. */
static void char_props(const reader *r, span props, owf_charfmt *fmt)
{
    size_t at = 0;
    field f;
    while (next_qfield(props, &at, &f) > 0) {
        switch (f.id) {
        case 0x202:                              /* bold */
            if (f.type == 0x00 || f.type == 0x08 || f.value) fmt->flags |= OWF_BOLD; else fmt->flags &= ~(unsigned)OWF_BOLD;
            break;
        case 0x203:                              /* italic */
            if (f.type == 0x00 || f.type == 0x08 || f.value) fmt->flags |= OWF_ITALIC; else fmt->flags &= ~(unsigned)OWF_ITALIC;
            break;
        case 0x20C:                              /* the size, EMU (12700 a point) */
            if (f.type == 0x20 && f.value > 0 && f.value < 635UL * 20000)
                fmt->size = emu_twips((long)f.value);
            break;
        case 0x21E:                              /* underline: its low byte, 1 single; 0 none */
            if (f.type == 0x10 || f.type == 0x18 || f.type == 0x20) {
                if (f.value & 0xFF) fmt->flags |= OWF_UNDERLINE; else fmt->flags &= ~(unsigned)OWF_UNDERLINE;
            }
            break;
        case 0x244:                              /* the colour: a block whose 0x200 is a place in PL */
            if (f.type == 0x88) {
                field g;
                size_t in = 0;
                while (next_qfield(f.body, &in, &g) > 0)
                    if (g.id == 0x200 && g.type == 0x20) {
                        if (g.value < (unsigned long)r->ntext_colours && r->text_colours[g.value] != OWF_COLOUR_AUTO)
                            fmt->colour = r->text_colours[g.value];
                        break;
                    }
            }
            break;
        case 0x224:                              /* the typeface: FONT's numbers */
            if (f.type == 0x88 || f.type == 0x90 || f.type == 0x98 || f.type == 0xA0)
                fmt->font = text_font(r, f.body);
            break;
        default:
            break;
        }
    }
}

/* A run's field (PUB-FORMAT.md 6.4): 0x200's low byte, 0 for none. 5 is a
 * page number, whose character ("#", as Publisher shows the field on a
 * master) stands for the field. */
#define PUB_FIELD_PAGE 5

static int run_field(span props)
{
    size_t at = 0;
    field f;
    while (next_qfield(props, &at, &f) > 0)
        if (f.id == 0x200 && (f.type == 0x10 || f.type == 0x18 || f.type == 0x20))
            return (int)(f.value & 0xFF);
    return 0;
}

/* Paragraph properties (FDPP's, or a style's): the style (0x219) and the
 * alignment (0x204's low byte: 0 left, 1 right, 2 centred; PUB-FORMAT.md 6.5).
 * A paragraph's own alignment, left too, is over its style's. */
static void para_props(span props, int *style, owf_align *align)
{
    size_t at = 0;
    field f;
    while (next_qfield(props, &at, &f) > 0) {
        if (f.id == 0x219 && style && (f.type == 0x10 || f.type == 0x18 || f.type == 0x20))
            *style = (int)f.value;
        else if (f.id == 0x204 && (f.type == 0x10 || f.type == 0x18 || f.type == 0x20)) {
            switch (f.value & 0xFF) {
            case 0: *align = OWF_ALIGN_LEFT; break;
            case 1: *align = OWF_ALIGN_RIGHT; break;
            case 2: *align = OWF_ALIGN_CENTRE; break;
            default: break;                  /* 6 and others: not known yet, the style's stays */
            }
        }
    }
}

/* FDPC's or FDPP's pages into runs. */
static void read_runs(reader *r, const quill *qd, const span *chunks, int nchunks,
                      trun **runs, int *count, int *cap)
{
    unsigned long last = qd->text_at;
    int k;
    for (k = 0; k < nchunks && !r->failed; k++) {
        span c = chunks[k];
        size_t page;
        for (page = 0; page + 512 <= c.n && !r->failed; page += 512) {
            span pg = sub(c, page, 512);
            unsigned n = rd16(pg, 0), i;
            if (!n || 8 + 6 * (size_t)n > 512)
                continue;
            for (i = 0; i < n; i++) {
                unsigned long end = rd32(pg, 8 + 4 * i);
                unsigned off = rd16(pg, 8 + 4 * n + 2 * i);
                span props = { NULL, 0 };
                trun *tr;
                if (end <= last || end < qd->text_at || (end - qd->text_at) / 2 > qd->text.n / 2)
                    break;                       /* runs only go forward through the text */
                if (off >= 8 + 6 * n && off + 4 <= 512) {
                    unsigned long len = rd32(pg, off);
                    if (len >= 4 && off + len <= 512)
                        props = sub(pg, off + 4, len - 4);
                }
                tr = grow(r, *runs, cap, *count, sizeof **runs);
                if (!tr)
                    return;
                *runs = tr;
                tr = &(*runs)[(*count)++];
                tr->end = (end - qd->text_at) / 2;
                tr->props = props;
                last = end;
            }
        }
    }
}

/* ---- Paragraph styles: STSH number 1 (PUB-FORMAT.md, section 6.6) ----
 * u32 length, u32 n, u32, u32, u32, then n u32 offsets counted from +0x14.
 * Entry 2k is style k's character properties, entry 2k+1 its paragraph
 * properties; each is a u16, then a block (u32 length counting itself,
 * then fields), then a u16. */

static span stsh_entry(span st, unsigned long n, unsigned long k)
{
    span none = { NULL, 0 };
    unsigned long at, len;
    if (k >= n || !has(st, 0x14 + 4 * k, 4))
        return none;
    at = 0x14 + rd32(st, 0x14 + 4 * k);
    len = rd32(st, at + 2);
    if (!has(st, at + 2, 4) || len < 4 || !has(st, at + 2, len))
        return none;
    return sub(st, at + 6, len - 4);
}

static void read_styles(reader *r, span st)
{
    unsigned long n = rd32(st, 4), k;
    if (st.n < 0x14 || n < 2 || n > 4096 || !has(st, 0x14, 4 * n))
        return;
    r->styles = owf_calloc(n / 2, sizeof *r->styles);
    if (!r->styles) {
        r->failed = 1;
        return;
    }
    r->nstyles = (int)(n / 2);
    for (k = 0; k < n / 2; k++) {
        pstyle *ps = &r->styles[k];
        owf_charfmt_init(&ps->chars);
        ps->align = OWF_ALIGN_LEFT;
        char_props(r, stsh_entry(st, n, 2 * k), &ps->chars);
        para_props(stsh_entry(st, n, 2 * k + 1), NULL, &ps->align);
    }
    /* a style without a size or a typeface takes the default style's
     * (hypothesis: no style names a parent in the samples) */
    for (k = 1; k < n / 2; k++) {
        if (!r->styles[k].chars.size)
            r->styles[k].chars.size = r->styles[0].chars.size;
        if (r->styles[k].chars.font < 0)
            r->styles[k].chars.font = r->styles[0].chars.font;
    }
}

/* The run at character n: the first that ends after n. *hint walks
 * forward from the last answer; a jump back (a story before the last one)
 * finds its place by halving. NULL past the last. */
static const trun *run_at(const trun *runs, int count, unsigned long n, int *hint)
{
    if (*hint < 0 || *hint > count || (*hint > 0 && runs[*hint - 1].end > n)) {
        int lo = 0, hi = count;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (runs[mid].end <= n) lo = mid + 1; else hi = mid;
        }
        *hint = lo;
    }
    while (*hint < count && runs[*hint].end <= n)
        (*hint)++;
    return *hint < count ? &runs[*hint] : NULL;
}

/* The paragraph at character n: its style and its alignment. */
static int para_at(const reader *r, unsigned long n, int *hint, owf_align *align)
{
    const trun *p = run_at(r->pruns, r->npruns, n, hint);
    owf_align ignored = OWF_ALIGN_LEFT;
    int style = 0;
    if (p)
        para_props(p->props, &style, &ignored);
    if (style < 0 || style >= r->nstyles)
        style = 0;                           /* no styles, or one not in the sheet: the default */
    *align = style < r->nstyles ? r->styles[style].align : OWF_ALIGN_LEFT;
    if (p)
        para_props(p->props, NULL, align);   /* the paragraph's own over its style's */
    return style;
}

static int same_fmt(const owf_charfmt *a, const owf_charfmt *b)
{
    return a->flags == b->flags && a->font == b->font && a->size == b->size && a->colour == b->colour;
}

/* ---- Hyperlinks: TOKN (PUB-FORMAT.md, section 6.7) ----
 * u32 n, u32 12, u32, then n + 1 u32 places in the story (the last its
 * length); then n blocks of each link's properties, each holding only what
 * changed from the link before (0x201 the link text's length, 0x202 its
 * kind: 1 a web address, 3 a place in the publication, 4 an e-mail); then
 * n blocks, each 0x200 the link's target in the table; then the table: u32
 * the size of what follows its 12-byte head, u32 count, 12 bytes, count u32
 * offsets from the offsets' start, and at each a u16 length and UTF-16. */

typedef struct {
    unsigned long from, to;      /* characters in the story */
    char *href;                  /* UTF-8 */
} tlink;

#define MAX_LINKS 4096

static char *link_target(span table, unsigned long count, unsigned long k)
{
    unsigned long off, len;
    size_t at, end;
    owf_buf b;
    char *out;
    if (k >= count || !has(table, 20 + 4 * k, 4))
        return NULL;
    off = rd32(table, 20 + 4 * k);
    len = rd16(table, 20 + off);
    if (!len || off > table.n || !has(table, 20 + off + 2, 2 * (size_t)len))
        return NULL;
    owf_buf_init(&b);
    at = 20 + off + 2;
    end = at + 2 * (size_t)len;
    while (at < end)
        put_utf16_char(&b, table, &at);
    owf_buf_putc(&b, 0);
    out = b.failed ? NULL : dup_text((const char *)b.data, b.length - 1);
    owf_buf_free(&b);
    return out;
}

/* A story's links, in order; 0 when there are none or they don't read. */
static int read_links(reader *r, span t, unsigned long length, tlink **out)
{
    unsigned long n = rd32(t, 0), k, len = 0, kind = 0, count;
    size_t at;
    long *target;
    tlink *links;
    span table;
    int made = 0;
    *out = NULL;
    if (!n || n > MAX_LINKS || !has(t, 12, 4 * (n + 1)))
        return 0;
    if (!(target = owf_malloc(n * sizeof *target)) || !(links = owf_calloc(n, sizeof *links))) {
        owf_free(target);
        r->failed = 1;
        return 0;
    }
    at = 12 + 4 * (n + 1);
    for (k = 0; k < 2 * n; k++) {            /* n property blocks, then n target blocks */
        unsigned long bl = rd32(t, at);
        span body;
        size_t in = 0;
        field f;
        if (!has(t, at, 4) || bl < 4 || !has(t, at, bl))
            goto done;
        body = sub(t, at + 4, bl - 4);
        if (k >= n)
            target[k - n] = -1;
        while (next_qfield(body, &in, &f) > 0) {
            if (k < n && f.id == 0x201)
                len = f.value;
            else if (k < n && f.id == 0x202)
                kind = f.value;
            else if (k >= n && f.id == 0x200)
                target[k - n] = (long)f.value;
        }
        if (k < n) {
            links[k].from = rd32(t, 12 + 4 * k);
            links[k].to = links[k].from + len;
            if (links[k].to > rd32(t, 12 + 4 * (k + 1)) || links[k].to > length)
                links[k].to = rd32(t, 12 + 4 * (k + 1)) < length ? rd32(t, 12 + 4 * (k + 1)) : length;
            if (kind == 3)                   /* a place in the publication: kept as text for now */
                links[k].to = links[k].from;
        }
        at += bl;
    }
    table = sub(t, at, t.n - at);
    count = rd32(table, 4);
    if (!has(table, 0, 20) || count > MAX_LINKS)
        goto done;
    for (k = 0; k < n && !r->failed; k++) {
        if (k && links[k].from < links[k - 1].to)
            break;                           /* places only go forward */
        if (links[k].to <= links[k].from || target[k] < 0)
            continue;
        links[made] = links[k];
        if (!(links[made].href = link_target(table, count, (unsigned long)target[k])))
            continue;
        made++;
    }
done:
    owf_free(target);
    if (!made) {
        owf_free(links);
        return 0;
    }
    *out = links;
    return made;
}

static void free_links(tlink *links, int n)
{
    int i;
    for (i = 0; i < n; i++)
        owf_free(links[i].href);
    owf_free(links);
}

/* The run's text, as a link when href is set. */
static void put_run(owf_builder *b, owf_buf *run, const char *href)
{
    if (!run->length)
        return;
    if (href)
        owf_builder_link_text(b, (const char *)run->data, run->length, href);
    else
        owf_builder_text(b, (const char *)run->data, run->length);
    run->length = 0;
}

/* ---- Table cells: TCD (PUB-FORMAT.md, section 6.8) ----
 * u32 n, u32 0, u32 0xFF00, then n + 1 u32: each cell's last character
 * in the story (its last paragraph's mark; the last cell's is the story's
 * length). A table's story gives each paragraph its cell; the frame, which
 * knows the columns, makes the cell a row and a column (place_shapes).
 * Until then a cell's paragraph has table_col PENDING_CELL and the cell's
 * number in table_row. */

#define PENDING_CELL (-2)

static void mark_cells(owf_story *story, span tcd, const unsigned long *starts, int nstarts)
{
    unsigned long n = rd32(tcd, 0), k;
    int i;
    if (!n || n > 65536 || !has(tcd, 12, 4 * (n + 1)) || nstarts != story->nparas)
        return;
    for (i = 0; i < story->nparas; i++) {
        unsigned long cell = 0;
        for (k = 0; k <= n; k++)
            if (rd32(tcd, 12 + 4 * k) < starts[i])
                cell++;
        story->paras[i].table_id = 0;
        story->paras[i].table_row = (int)cell;
        story->paras[i].table_col = PENDING_CELL;
    }
}

/* Characters [from, to) of TEXT as a story's paragraphs, with their character runs and links;
 * a table's when tcd is not empty. */
static void make_story(reader *r, owf_story *story, span text, size_t from, size_t to,
                       const tlink *links, int nlinks, span tcd)
{
    unsigned long *starts = NULL;            /* each paragraph's first character in the story */
    int nstarts = 0, capstarts = 0;
    owf_builder b;
    owf_buf run;
    size_t at = from * 2, end = to * 2;
    int open = 0, chint = -1, phint = -1, style = 0, made_style = -1, lk = 0;
    const char *href = NULL;
    const trun *made_run = NULL;             /* want below is made for this run and style */
    const trun *field_run = NULL;            /* the page number run whose field is made */
    int page_run = 0;                        /* made_run is a page number's */
    owf_charfmt want;
    owf_builder_init(&b, r->pub->res, story);
    owf_buf_init(&run);
    if (end > text.n)
        end = text.n & ~(size_t)1;
    while (at < end) {
        unsigned c = rd16(text, at);
        const trun *cr;
        if (!open) {                         /* a paragraph starts: its style and alignment */
            style = para_at(r, at / 2, &phint, &b.parafmt.align);
            if (tcd.n && nstarts >= capstarts) {
                unsigned long *more = grow(r, starts, &capstarts, nstarts, sizeof *starts);
                if (more)
                    starts = more;
            }
            if (tcd.n && nstarts < capstarts)
                starts[nstarts++] = (unsigned long)(at / 2 - from);
        }
        cr = run_at(r->cruns, r->ncruns, at / 2, &chint);
        if (made_style < 0 || cr != made_run || style != made_style) {
            if (style < r->nstyles)
                want = r->styles[style].chars;   /* the style's, then the run's own over it */
            else
                owf_charfmt_init(&want);
            if (cr)
                char_props(r, cr->props, &want);
            page_run = cr && run_field(cr->props) == PUB_FIELD_PAGE;
            made_run = cr;
            made_style = style;
        }
        {
            const char *link = NULL;
            unsigned long n = (unsigned long)(at / 2 - from);
            while (lk < nlinks && links[lk].to <= n)
                lk++;
            if (lk < nlinks && links[lk].from <= n)
                link = links[lk].href;
            if (link != href) {
                put_run(&b, &run, href);
                href = link;
            }
        }
        if (!same_fmt(&want, &b.charfmt)) {
            put_run(&b, &run, href);
            owf_builder_flush(&b);
            b.charfmt = want;
        }
        if (c >= 0x20 && page_run) {         /* a page number: one field for the run's characters */
            if (cr != field_run) {
                put_run(&b, &run, href);
                owf_builder_special(&b, OWF_RUN_FIELD, OWF_FIELD_PAGE);
                field_run = cr;
            }
            at += 2;
            open = 1;
            continue;
        }
        if (c >= 0x20) {
            put_utf16_char(&run, text, &at);
            open = 1;
            continue;
        }
        put_run(&b, &run, href);
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
    put_run(&b, &run, href);
    if (run.failed)
        r->failed = 1;
    owf_buf_free(&run);
    if (open)
        owf_builder_end_para(&b);
    if (owf_builder_done(&b) != OWF_OK)
        r->failed = 1;
    else if (tcd.n && starts)
        mark_cells(story, tcd, starts, nstarts);
    owf_free(starts);
}

/* PL: u32 n, u32, u32, then n blocks one after another; block k's field
 * 0x201 is colour k, which a character run's 0x244 names (PUB-FORMAT.md
 * 6.9): 0x00BBGGRR, or 0x08 in the top byte and a place in the colour
 * scheme. The scheme is read after the text, so a scheme place is kept as
 * SCHEME_COLOUR | place until text_colours_resolve. */
#define SCHEME_COLOUR 0x08000000UL

static unsigned long text_colour(unsigned long v)
{
    if ((v >> 24) == 0x08)
        return (v & 0xFFFFFFUL) < 16 ? SCHEME_COLOUR | (v & 0xFFFFFFUL) : OWF_COLOUR_AUTO;
    if (v >> 24)
        return OWF_COLOUR_AUTO;              /* 0xFFFFFFFE and the like: not known yet */
    return ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
}

static void read_text_colours(reader *r, span pl)
{
    unsigned long n = rd32(pl, 0), k, at = 12;
    if (!pl.n || !n || n > 4096 || r->failed)
        return;
    if (!(r->text_colours = owf_malloc(n * sizeof *r->text_colours))) {
        r->failed = 1;
        return;
    }
    r->ntext_colours = (int)n;
    for (k = 0; k < n; k++) {
        unsigned long len = rd32(pl, at);
        r->text_colours[k] = OWF_COLOUR_AUTO;
        if (!has(pl, at, 4) || len < 4 || !has(pl, at, len))
            continue;                        /* the rest stay unknown */
        {
            span b = sub(pl, at + 4, len - 4);
            size_t in = 0;
            field f;
            while (next_qfield(b, &in, &f) > 0)
                if (f.id == 0x201 && f.type == 0x20) {
                    r->text_colours[k] = text_colour(f.value);
                    break;
                }
        }
        at += len;
    }
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

    /* FONT: five u32 (the second the count), offsets from +0x14, then each
     * name as a u16 length and UTF-16. The character runs name fonts by
     * their place here, so font_map keeps the place. */
    n = rd32(qd.font, 4);
    base = 0x14;
    if (n > 1024)
        n = 1024;
    if (n && !(r->font_map = owf_malloc(n * sizeof *r->font_map))) {
        r->failed = 1;
        owf_free(data);
        return;
    }
    r->nfont_map = (int)n;
    for (i = 0; i < n; i++)
        r->font_map[i] = -1;
    for (i = 0; i < n && has(qd.font, base + 4 * i, 4) && !r->failed; i++) {
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
        if (name.failed || (r->font_map[i] = owf_doc_font(r->pub->res, (const char *)name.data, OWF_FONT_ANY)) < 0)
            r->failed = 1;
        owf_buf_free(&name);
    }
    read_text_colours(r, qd.pl);
    read_styles(r, qd.stsh1);
    read_runs(r, &qd, qd.fdpc, qd.nfdpc, &r->cruns, &r->ncruns, &r->capcruns);
    read_runs(r, &qd, qd.fdpp, qd.nfdpp, &r->pruns, &r->npruns, &r->cappruns);

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
        {
            tlink *links = NULL;
            int nlinks = 0, k;
            span tcd = { NULL, 0 };
            /* a story's first TOKN and first TCD; any later copy is ignored */
            for (k = 0; k < qd.ntokn; k++)
                if (qd.tokn_story[k] == i) {
                    nlinks = read_links(r, qd.tokn[k], (unsigned long)len, &links);
                    break;
                }
            for (k = 0; k < qd.ntcd; k++)
                if (qd.tcd_story[k] == i) {
                    tcd = qd.tcd[k];
                    break;
                }
            make_story(r, &r->pub->stories[r->pub->nstories++], qd.text, pos, pos + len, links, nlinks, tcd);
            free_links(links, nlinks);
        }
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

    /* the runs point into data: later text (WordArt's) has none */
    r->ncruns = r->npruns = 0;
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

/* zlib data (RFC 1950) into out, which it must fill exactly: through zlib
 * when it is built in, else libowf's own inflate (raw DEFLATE after the
 * 2-byte header; the checksum after the last block is not checked). */
static int inflate_zlib(unsigned char *out, unsigned long full, span z)
{
#ifdef OWF_HAVE_ZLIB
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, 15) == Z_OK) {
        int ok;
        zs.next_in = (unsigned char *)z.p;
        zs.avail_in = (unsigned)z.n;
        zs.next_out = out;
        zs.avail_out = (unsigned)full;
        ok = inflate(&zs, Z_FINISH) == Z_STREAM_END && zs.total_out == full;
        inflateEnd(&zs);
        return ok ? OWF_OK : OWF_ERR_CORRUPT;
    }
#endif
    if (z.n < 2 || (z.p[0] & 0x0F) != 8 || (z.p[1] & 0x20) || ((unsigned)z.p[0] << 8 | z.p[1]) % 31)
        return OWF_ERR_CORRUPT;
    return owf_inflate(out, full, z.p + 2, z.n - 2);
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
        unsigned char *out;
        if (!body.p || !has(body, 48, 1) || body.p[48] != 0 || !full || full >= 0x4000000UL) {
            owf_report_add(r->report, OWF_NOTE_LOST, "A metafile picture is stored in a way not known yet; it is left out");
            return -1;
        }
        if (!(out = owf_malloc(full))) {
            owf_report_add(r->report, OWF_NOTE_LOST, "A picture is too big for the memory free; it is left out");
            return -1;
        }
        index = -1;
        k = (size_t)inflate_zlib(out, full, z);
        if (k == OWF_OK) {
            index = owf_doc_add_image(r->pub->res, name, rtype == 0xF01A ? "image/x-emf" : "image/x-wmf", out, full, 0, 0, NULL);
            if (index < 0)
                r->failed = 1;
        } else if (k == OWF_ERR_MEMORY) {
            owf_report_add(r->report, OWF_NOTE_LOST, "A picture is too big for the memory free; it is left out");
        } else {
            owf_report_add(r->report, OWF_NOTE_LOST, "A metafile picture is damaged; it is left out");
        }
        owf_free(out);
        return index;
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
    s->filled = s->lined = -1;
    s->line_width = -1;
    s->inset[0] = s->inset[1] = s->inset[2] = s->inset[3] = -1;
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
    {
        span none = { NULL, 0 };
        make_story(r, st, text, 0, len, NULL, 0, none);
    }
    return r->pub->nstories++;
}

/* One of a shape's simple properties ([MS-ODRAW] 2.3; PUB-FORMAT.md 7.4). */
static void shape_prop(eshape *s, unsigned pid, unsigned long v)
{
    switch (pid) {
    case 0x0081: case 0x0082: case 0x0083: case 0x0084:   /* the text's margins, EMU */
        if (v < 914400UL * 22)
            s->inset[pid - 0x0081] = (long)v;
        break;
    case 0x0181:                                        /* fill colour */
        s->fill = v;
        s->has_fill = 1;
        break;
    case 0x01BF:                                        /* fill booleans: fFilled (bit 4) when its use bit (20) is set */
        if (v & 0x100000UL)
            s->filled = (v & 0x10UL) != 0;
        break;
    case 0x01C0:                                        /* line colour */
        s->line = v;
        s->has_line = 1;
        break;
    case 0x01CB:                                        /* line width, EMU */
        if (v < 914400UL)
            s->line_width = (long)v;
        break;
    case 0x01FF:                                        /* line booleans: fLine (bit 3) when its use bit (19) is set */
        if (v & 0x80000UL)
            s->lined = (v & 0x8UL) != 0;
        break;
    default:
        break;
    }
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
                else if (!(pid & 0xC000))
                    shape_prop(s, pid, v);
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
/* A drawing property's colour as 0xRRGGBB: 0x00BBGGRR, or 0x08 in the top
 * byte and a place in the colour scheme. -1: one not known. */
static long shape_colour(const owf_pub *pub, unsigned long v)
{
    if ((v >> 24) == 0x08)
        return (v & 0xFFFFFFUL) < (unsigned long)pub->nscheme ? (long)pub->scheme[v & 0xFFFFFFUL] : -1;
    if (v >> 24)
        return -1;                       /* system colours and the like: not known yet */
    return (long)(((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF));
}

static void frame_look(const owf_pub *pub, owf_pub_frame *f, const eshape *s)
{
    long c;
    int k;
    f->filled = s->filled;
    if (s->has_fill && (c = shape_colour(pub, s->fill)) >= 0)
        f->fill = (unsigned long)c;
    else if (f->filled == 1 && !s->has_fill)
        f->fill = 0xFFFFFFUL;            /* filled with no colour said: [MS-ODRAW]'s default, white */
    else if (f->filled == 1)
        f->filled = -1;                  /* filled, but with a colour not known: as if not said */
    f->lined = s->lined;
    if (s->has_line && (c = shape_colour(pub, s->line)) >= 0)
        f->line = (unsigned long)c;
    else if (f->lined == 1 && s->has_line)
        f->lined = -1;
    f->line_width = s->line_width;
    for (k = 0; k < 4; k++)
        f->inset[k] = s->inset[k];
}

/* A linked text frame's place in its story's chain: its chunk's field
 * 0x28 (none: the first; the chunk's 0x36 and 0x37 name the frames before
 * and after, PUB-FORMAT.md 5.6). */
static long chain_place(reader *r, const owf_pub_frame *f)
{
    return field_value(chunk_body(r, f->chunk), 0x28, 0);
}

/* The layout flows a story through its frames in the page's order, and the
 * file may list them in another (a newsletter's three columns middle, left,
 * right). So one story's frames swap among their own places into their
 * chain's order; every other frame keeps its place. Only frames on the page
 * itself: a group's members keep theirs. */
static void order_chains(reader *r, owf_pub_page *page, int first)
{
    int i, j, n = page->nframes - first;
    long *place, tp;
    if (n < 2)
        return;
    place = owf_malloc((size_t)n * sizeof *place);
    if (!place) {
        r->failed = 1;
        return;
    }
    for (i = 0; i < n; i++)            /* each chunk's 0x28 read once */
        place[i] = chain_place(r, &page->frames[first + i]);
    for (i = first; i < page->nframes; i++) {
        owf_pub_frame *a = &page->frames[i];
        if (a->story < 0 || a->wordart || a->parent >= 0 || a->kind == OWF_PUB_GROUP)
            continue;
        for (j = i + 1; j < page->nframes; j++) {
            owf_pub_frame *b = &page->frames[j], t;
            if (b->story != a->story || b->wordart || b->parent >= 0 || b->kind == OWF_PUB_GROUP)
                continue;
            if (place[j - first] < place[i - first]) {
                t = *a;
                *a = *b;
                *b = t;
                tp = place[i - first];
                place[i - first] = place[j - first];
                place[j - first] = tp;
            }
        }
    }
    owf_free(place);
}

/* A table frame's story: each cell's paragraphs into their row and column. */
static void table_cells(owf_pub *pub, const owf_pub_frame *f)
{
    owf_story *st;
    int i;
    if (f->story < 0 || f->story >= pub->nstories || f->cols < 1 || f->cols > 64)
        return;
    st = &pub->stories[f->story];
    for (i = 0; i < st->nparas; i++) {
        owf_para *p = &st->paras[i];
        if (p->table_col != PENDING_CELL)
            continue;
        p->table_col = p->table_row % f->cols;
        p->table_row /= f->cols;
        p->table_cols = f->cols;
    }
}

/* Text colours that name a place in the colour scheme, now it is read. */
static void text_colours_resolve(owf_pub *pub)
{
    int i, j, k;
    for (i = 0; i < pub->nstories; i++)
        for (j = 0; j < pub->stories[i].nparas; j++)
            for (k = 0; k < pub->stories[i].paras[j].nruns; k++) {
                unsigned long *c = &pub->stories[i].paras[j].runs[k].fmt.colour;
                if (*c != OWF_COLOUR_AUTO && (*c & SCHEME_COLOUR))
                    *c = (int)(*c & 0xFF) < pub->nscheme ? pub->scheme[*c & 0xFF] : OWF_COLOUR_AUTO;
            }
}

/* Cells no table frame took: plain paragraphs again. */
static void loose_cells(owf_pub *pub)
{
    int i, j;
    for (i = 0; i < pub->nstories; i++)
        for (j = 0; j < pub->stories[i].nparas; j++) {
            owf_para *p = &pub->stories[i].paras[j];
            if (p->table_col == PENDING_CELL) {
                p->table_id = -1;
                p->table_row = p->table_col = 0;
            }
        }
}

static void place_shapes(reader *r, long pc, owf_pub_page *page)
{
    int i, *at, first = page->nframes;
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
        frame_look(r->pub, f, s);
        if (s->wordart >= 0 && f->story < 0) {
            f->story = s->wordart;
            f->wordart = 1;
        }
        if (f->kind == OWF_PUB_TABLE) {
            span t = chunk_body(r, s->chunk);
            f->rows = (int)field_value(t, 0x66, 0);
            f->cols = (int)field_value(t, 0x67, 0);
            table_cells(r->pub, f);
        }
    }
    owf_free(at);
    if (!r->failed)
        order_chains(r, page, first);
}

/* The colour scheme: the Contents chunk of kind 0x5C. Field 0x02 is an
 * array, an item a block whose field 0x01 is a colour as R, G, B bytes (an
 * empty item: black). */
static void read_scheme(reader *r)
{
    long i;
    for (i = 0; i < r->nchunks; i++) {
        span c, arr;
        field f;
        size_t at = 0;
        if (r->chunks[i].kind != 0x5C)
            continue;
        c = block_at(r->contents, r->chunks[i].offset);
        if (!find_field(c, 0x02, &f) || (f.type != 0xA0 && f.type != 0x98 && f.type != 0x90))
            return;
        arr = f.body;
        /* an array's body: its items, one after another (section 4) */
        at = 0;
        {
            field it;
            while (next_field(arr, &at, &it) > 0 && r->pub->nscheme < 16) {
                if (it.type == 0x78)
                    r->pub->scheme[r->pub->nscheme++] = 0;
                else if (it.type == 0x88) {
                    unsigned long v = (unsigned long)field_value(it.body, 0x01, 0);
                    r->pub->scheme[r->pub->nscheme++] =
                        ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
                }
            }
        }
        return;
    }
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
    read_scheme(r);
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
            if (f->kind == OWF_PUB_TABLE)
                table_cells(pub, f);
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
    loose_cells(r.pub);
    text_colours_resolve(r.pub);
    r.pub->width = emu_twips(r.pub->width_emu);
    r.pub->height = emu_twips(r.pub->height_emu);
    owf_free(r.chunks);
    owf_free(r.shapes);
    owf_free(r.story_ids);
    owf_free(r.blip_image);
    owf_free(r.font_map);
    owf_free(r.text_colours);
    owf_free(r.cruns);
    owf_free(r.pruns);
    owf_free(r.styles);
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
