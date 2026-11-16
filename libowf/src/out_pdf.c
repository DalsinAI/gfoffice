/*
 * libowf: lightweight PDF export for GFOpenWrite.
 * Base-14 fonts keep output searchable and the 68000/68020 path FPU-free.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"
#include "owf_note.h"
#include "owf_comment.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef OWF_HAVE_ZLIB
#include "owf_zlib.h"
#endif

#define PDF_MAX_LINE_PIECES 192
#define PDF_MAX_PLACED 256

typedef struct {
    const char *text;
    size_t len;
    owf_charfmt fmt;
    int font_id;
    int size;
    int width;
} pdf_piece;

#define PDF_MAX_LINE_MARKS 16
typedef struct {
    pdf_piece p[PDF_MAX_LINE_PIECES];
    int n, width, max_size;
    int marks[PDF_MAX_LINE_MARKS], nmarks;   /* 1.12.1: the comments whose marks are on the line */
} pdf_line;

/* 1.12.1: a comment's mark as laid out, for its note in the margin */
typedef struct {
    int comment, page, y;
} pdf_mark;

typedef struct {
    owf_buf *pages;
    int npages, cappages, current;
    int page_w, page_h, left, right, top, bottom;
    int y;
    const owf_doc *doc;
    owf_report *report;
    unsigned char *image_used;   /* per doc image: drawn somewhere, so it is written once */
    char (*note_marks)[32];      /* 1.8: each note's mark, kept while its line waits to be drawn */
    char (*comment_marks)[16];   /* 1.10.1: with comments, each comment's mark ("[1]"); "" for one not shown */
    int *comment_order;          /* 1.10.1: the comments' indexes in the order they are numbered */
    int ncomment_order;
    /* 1.12.1: "pdf-comments-margin": notes beside their lines in a strip at the text's right (strip > 0, its
     * width; the text narrowed by it), the marks as laid out, and the comments whose notes did not fit there
     * (listed after the text instead) */
    int strip;
    pdf_mark *placed;
    int nplaced, capplaced;
    unsigned char *listed;
    int crowded;                 /* 2.0.44: marks past PDF_MAX_LINE_MARKS on a line: listed, and the report says */
} pdf_layout;

static int pdf_points(int twips)
{
    if (twips >= 0) return (twips + 10) / 20;
    return -((-twips + 10) / 20);
}

static int pdf_heading_size(const owf_doc *doc, const owf_para *p, const owf_charfmt *f)
{
    static const int h[] = { 0, 18, 16, 14, 13, 12, 12 };
    int size = f->size ? pdf_points(f->size) : pdf_points(doc->base.size ? doc->base.size : 240);
    if (!f->size && p->fmt.heading >= 1 && p->fmt.heading <= 6 && size < h[p->fmt.heading])
        size = h[p->fmt.heading];
    if (size < 6) size = 6;
    if (size > 72) size = 72;
    return size;
}

static owf_font_kind pdf_family(const owf_doc *doc, const owf_charfmt *f)
{
    int ix = f->font >= 0 ? f->font : doc->base.font;
    if (ix >= 0 && ix < doc->nfonts) return doc->fonts[ix].kind;
    return OWF_FONT_SERIF;
}

/* F1-F4 Helvetica, F5-F8 Times, F9-F12 Courier; normal,bold,italic,bolditalic. */
static int pdf_font_id(const owf_doc *doc, const owf_charfmt *f)
{
    int base, variant = 0;
    owf_font_kind k = pdf_family(doc, f);
    if (k == OWF_FONT_MONO) base = 9;
    else if (k == OWF_FONT_SANS) base = 1;
    else base = 5;
    if (f->flags & OWF_BOLD) variant += 1;
    if (f->flags & OWF_ITALIC) variant += 2;
    return base + variant;
}

static int pdf_char_count(const char *s, size_t len)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + len;
    int n = 0;
    while (p < end) { (void)owf_utf8_next(&p, end); ++n; }
    return n;
}

static int pdf_piece_width(const owf_doc *doc, const char *s, size_t len,
                           const owf_charfmt *f, int size)
{
    int chars = pdf_char_count(s, len);
    owf_font_kind k = pdf_family(doc, f);
    int tenths = k == OWF_FONT_MONO ? 6 : k == OWF_FONT_SANS ? 5 : 5;
    return (chars * size * tenths + 5) / 10;
}

static void pdf_put_encoded(owf_buf *out, const char *s, size_t len)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + len;
    owf_buf_putc(out, '(');
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        unsigned v = '?';
        if (c == '\t') v = ' ';
        else if (c >= 32 && c <= 126) v = (unsigned)c;
        else if (c == 0x2022) v = 0x95;
        else if (c == 0x2018 || c == 0x2019) v = c == 0x2018 ? 0x91 : 0x92;
        else if (c == 0x201c || c == 0x201d) v = c == 0x201c ? 0x93 : 0x94;
        else if (c == 0x2013) v = 0x96;
        else if (c == 0x2014) v = 0x97;
        else if (c == 0x2020 || c == 0x2021) v = c == 0x2020 ? 0x86 : 0x87;   /* 1.12.1: notes' daggers */
        else if (c >= 160 && c <= 255) v = (unsigned)c;
        if (v == '(' || v == ')' || v == '\\') {
            owf_buf_putc(out, '\\'); owf_buf_putc(out, (int)v);
        } else if (v < 32 || v >= 127) {
            owf_buf_printf(out, "\\%03o", v & 255);
        } else owf_buf_putc(out, (int)v);
    }
    owf_buf_putc(out, ')');
}

static void pdf_colour(owf_buf *out, unsigned long colour)
{
    unsigned r, g, b;
    if (colour == OWF_COLOUR_AUTO) { owf_buf_puts(out, "0 g\n"); return; }
    r = (unsigned)((colour >> 16) & 255); g = (unsigned)((colour >> 8) & 255); b = (unsigned)(colour & 255);
    owf_buf_printf(out, "%u.%03u %u.%03u %u.%03u rg\n", r/255, (r%255)*1000/255,
                   g/255, (g%255)*1000/255, b/255, (b%255)*1000/255);
}

/* ---- pictures ---- */

typedef struct {
    int ok, w, h, gray;              /* gray: DeviceGray, else DeviceRGB */
    int dct;                         /* JPEG data as it is (DCTDecode) */
    unsigned char *data; size_t len; /* the image stream (deflated unless dct) */
    unsigned char *mask; size_t mlen;/* deflated alpha, or NULL */
} pdf_image;

static int jpeg_size(const unsigned char *d, size_t n, int *w, int *h, int *comps)
{
    size_t i = 2;
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;
    while (i + 9 < n) {
        unsigned m, len;
        if (d[i] != 0xFF) return 0;
        m = d[i + 1];
        len = ((unsigned)d[i + 2] << 8) | d[i + 3];
        if ((m >= 0xC0 && m <= 0xC3) || (m >= 0xC5 && m <= 0xC7) || (m >= 0xC9 && m <= 0xCB) || (m >= 0xCD && m <= 0xCF)) {
            *h = (d[i + 5] << 8) | d[i + 6]; *w = (d[i + 7] << 8) | d[i + 8]; *comps = d[i + 9];
            return *w > 0 && *h > 0;
        }
        i += 2 + len;
    }
    return 0;
}

#ifdef OWF_HAVE_ZLIB
/* a PNG chunk's big-endian length and the IHDR sizes (only PNG needs zlib) */
static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }
static unsigned char *pdf_deflate(const unsigned char *in, size_t n, size_t *out_len)
{
    uLongf cap = compressBound((uLong)n);
    unsigned char *o;
    if (cap < (uLongf)n || cap - (uLongf)n > (uLongf)n / 2 + 4096) return NULL;   /* not zlib's bound (see owf_zip.c) */
    o = (unsigned char *)malloc(cap ? cap : 1);
    if (!o) return NULL;
    if (compress2(o, &cap, in, (uLong)n, 6) != Z_OK) { free(o); return NULL; }
    *out_len = cap;
    return o;
}

static int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return pa <= pb && pa <= pc ? a : pb <= pc ? b : c; }

static int png_decode(const unsigned char *d, size_t n, pdf_image *img)
{
    size_t i = 8, zcap = 0, zlen = 0;
    unsigned char *z = NULL, *raw = NULL, *rgb = NULL, *alpha = NULL, plte[768], trns[256];
    int w = 0, h = 0, depth = 0, type = 0, inter = 0, nplte = 0, ntrns = 0, ch, bpp, ok = 0;
    size_t stride, need, x, y;
    uLongf rawlen;
    if (n < 8 || memcmp(d, "\211PNG\r\n\032\n", 8)) return 0;
    memset(trns, 255, sizeof trns);
    while (i + 12 <= n) {
        unsigned long len = be32(d + i);
        const unsigned char *ty = d + i + 4, *body = d + i + 8;
        if (len > n - i - 12) break;
        if (!memcmp(ty, "IHDR", 4) && len >= 13) { w = (int)be32(body); h = (int)be32(body + 4); depth = body[8]; type = body[9]; inter = body[12]; }
        else if (!memcmp(ty, "PLTE", 4) && len <= 768) { memcpy(plte, body, len); nplte = (int)(len / 3); }
        else if (!memcmp(ty, "tRNS", 4) && type == 3 && len <= 256) { memcpy(trns, body, len); ntrns = (int)len; }
        else if (!memcmp(ty, "IDAT", 4) && len) {   /* an empty IDAT adds nothing (and z may still be NULL) */
            if (zlen + len > zcap) { unsigned char *g; zcap = (zlen + len) * 2; g = (unsigned char *)realloc(z, zcap); if (!g) goto out; z = g; }
            memcpy(z + zlen, body, len); zlen += len;
        } else if (!memcmp(ty, "IEND", 4)) break;
        i += 12 + len;
    }
    (void)ntrns;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || inter || !z) goto out;
    if (type == 3) {
        if ((depth != 1 && depth != 2 && depth != 4 && depth != 8) || !nplte)
            goto out;
    }
    else if (depth != 8) goto out;
    ch = type == 0 ? 1 : type == 2 ? 3 : type == 3 ? 1 : type == 4 ? 2 : type == 6 ? 4 : 0;
    if (!ch) goto out;
    stride = type == 3 ? ((size_t)w * depth + 7) / 8 : (size_t)w * ch;
    bpp = type == 3 ? 1 : ch;
    need = (stride + 1) * (size_t)h;
    raw = (unsigned char *)malloc(need);
    rawlen = (uLongf)need;
    if (!raw || uncompress(raw, &rawlen, z, (uLong)zlen) != Z_OK || rawlen != need) goto out;
    for (y = 0; y < (size_t)h; y++) {           /* undo the row filters, in place */
        unsigned char *row = raw + y * (stride + 1) + 1, *prev = y ? row - (stride + 1) : NULL;
        int ft = row[-1];
        for (x = 0; x < stride; x++) {
            int a = x >= (size_t)bpp ? row[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= (size_t)bpp) ? prev[x - bpp] : 0;
            row[x] = (unsigned char)(row[x] + (ft == 1 ? a : ft == 2 ? b : ft == 3 ? (a + b) / 2 : ft == 4 ? paeth(a, b, c) : 0));
        }
    }
    img->gray = type == 0 || type == 4;
    rgb = (unsigned char *)malloc((size_t)w * h * (img->gray ? 1 : 3));
    if (type == 4 || type == 6 || (type == 3 && ntrns)) alpha = (unsigned char *)malloc((size_t)w * h);
    if (!rgb || ((type == 4 || type == 6 || (type == 3 && ntrns)) && !alpha)) goto out;
    for (y = 0; y < (size_t)h; y++) {
        const unsigned char *row = raw + y * (stride + 1) + 1;
        for (x = 0; x < (size_t)w; x++) {
            size_t o = y * (size_t)w + x;
            if (type == 3) {
                int idx = depth == 8 ? row[x] : (row[x * depth / 8] >> (8 - depth - (int)(x * depth % 8))) & ((1 << depth) - 1);
                if (idx >= nplte) idx = 0;
                rgb[o * 3] = plte[idx * 3]; rgb[o * 3 + 1] = plte[idx * 3 + 1]; rgb[o * 3 + 2] = plte[idx * 3 + 2];
                if (alpha) alpha[o] = trns[idx];
            } else if (img->gray) {
                rgb[o] = row[x * ch];
                if (alpha) alpha[o] = row[x * ch + 1];
            } else {
                rgb[o * 3] = row[x * ch]; rgb[o * 3 + 1] = row[x * ch + 1]; rgb[o * 3 + 2] = row[x * ch + 2];
                if (alpha) alpha[o] = row[x * ch + 3];
            }
        }
    }
    img->data = pdf_deflate(rgb, (size_t)w * h * (img->gray ? 1 : 3), &img->len);
    if (alpha) {
        size_t k; int opaque = 1;
        for (k = 0; k < (size_t)w * h && opaque; k++) opaque = alpha[k] == 255;
        if (!opaque) img->mask = pdf_deflate(alpha, (size_t)w * h, &img->mlen);
    }
    if (img->data) { img->w = w; img->h = h; ok = img->ok = 1; }
out:
    free(z); free(raw); free(rgb); free(alpha);
    return ok;
}
#endif

static void pdf_image_encode(const owf_image *im, pdf_image *img)
{
    int comps = 0;
    memset(img, 0, sizeof *img);
    if (!im->data || !im->length) return;
    if (jpeg_size(im->data, im->length, &img->w, &img->h, &comps) && (comps == 1 || comps == 3)) {
        img->ok = img->dct = 1;
        img->gray = comps == 1;
        img->data = im->data;              /* borrowed: the document owns it */
        img->len = im->length;
        return;
    }
#ifdef OWF_HAVE_ZLIB
    png_decode(im->data, im->length, img);
#endif
}

static int pdf_new_page(pdf_layout *l)
{
    owf_buf *grown;
    int want;
    if (l->npages == l->cappages) {
        want = l->cappages ? l->cappages * 2 : 8;
        grown = (owf_buf *)realloc(l->pages, (size_t)want * sizeof(*grown));
        if (!grown) return 0;
        l->pages = grown;
        l->cappages = want;
    }
    owf_buf_init(&l->pages[l->npages]);
    l->current = l->npages++;
    l->y = l->page_h - l->top;
    return 1;
}

/* One header or footer paragraph's runs on one line, from x, aligned in the
 * width avail; fields filled in. Text advances by the font's own widths. */
static void pdf_story_para(owf_buf *out, const owf_doc *doc, const owf_para *p,
                           int x, int y, int avail, int page_num, int total_pages)
{
    int j, width = 0, pass;
    for (pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (p->fmt.align == OWF_ALIGN_CENTRE && width < avail) x += (avail - width) / 2;
            else if (p->fmt.align == OWF_ALIGN_RIGHT && width < avail) x += avail - width;
            owf_buf_printf(out, "BT %d %d Td ", x, y);
        }
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            char field[32]; const char *text = NULL; size_t n = 0;
            int size = r->fmt.size ? pdf_points(r->fmt.size) : 9;
            if (r->kind == OWF_RUN_TEXT && r->text) { text = r->text; n = strlen(text); }
            else if (r->kind == OWF_RUN_TAB) { text = "    "; n = 4; }
            else if (r->kind == OWF_RUN_FIELD) {
                if (r->field == OWF_FIELD_PAGE) snprintf(field,sizeof field,"%d",page_num);
                else if (r->field == OWF_FIELD_PAGES) snprintf(field,sizeof field,"%d",total_pages);
                else if (r->field > OWF_FIELD_TIME) snprintf(field,sizeof field,"%s",r->text?r->text:"");
                else snprintf(field,sizeof field,"%s",r->field==OWF_FIELD_DATE?"date":"time");
                text=field;n=strlen(field);
            }
            if (!text || !n) continue;
            if (pass == 0) { width += pdf_piece_width(doc, text, n, &r->fmt, size); continue; }
            pdf_colour(out, r->fmt.colour);
            owf_buf_printf(out, "/F%d %d Tf ", pdf_font_id(doc, &r->fmt), size);
            pdf_put_encoded(out, text, n); owf_buf_puts(out, " Tj ");
        }
    }
    owf_buf_puts(out, "ET\n");
}

/* A header (from y down) or footer (ending at y): its paragraphs one per line,
 * a table's row with its cells side by side across the width. */
static void pdf_story(owf_buf *out, const owf_doc *doc, const owf_story *story, int left, int width,
                      int y, int is_footer, int page_num, int total_pages)
{
    int i, lines = 0, lh = 11;
    for (i = 0; i < story->nparas; ++i)
        if (story->paras[i].table_id < 0 || story->paras[i].table_col == 0) ++lines;
    if (is_footer) y += (lines - 1) * lh;
    for (i = 0; i < story->nparas; ++i) {
        const owf_para *p = &story->paras[i];
        if (p->table_id >= 0) {
            int cols = p->table_cols > 0 ? p->table_cols : 1, cw = width / cols;
            pdf_story_para(out, doc, p, left + p->table_col * cw, y, cw, page_num, total_pages);
            if (i + 1 < story->nparas && story->paras[i + 1].table_id == p->table_id &&
                story->paras[i + 1].table_row == p->table_row) continue;
        } else
            pdf_story_para(out, doc, p, left, y, width, page_num, total_pages);
        y -= lh;
    }
}

static void pdf_add_headers_footers(pdf_layout *l)
{
    int i, width = l->page_w - l->left - l->right;
    for (i = 0; i < l->npages; ++i) {
        owf_buf *out = &l->pages[i];
        int shown = l->doc->page.start_page + i, left;
        const owf_story *header, *footer;
        if (shown < 1) shown = i + 1;
        /* 2.0.23: an even-numbered page is a left one, with left pages' own when there are */
        left = !(shown & 1);
        header = left && l->doc->header_left.nparas ? &l->doc->header_left : &l->doc->header;
        footer = left && l->doc->footer_left.nparas ? &l->doc->footer_left : &l->doc->footer;
        if (header->nparas && (i || l->doc->page.header_on_first))
            pdf_story(out, l->doc, header, l->left, width, l->page_h - (l->top / 2), 0, shown, l->npages);
        if (footer->nparas && (i || l->doc->page.footer_on_first))
            pdf_story(out, l->doc, footer, l->left, width, l->bottom / 2, 1, shown, l->npages);
    }
}

static void pdf_line_reset(pdf_line *line)
{
    memset(line, 0, sizeof(*line));
}

static int pdf_line_add(pdf_line *line, const char *text, size_t len,
                        const owf_charfmt *fmt, int font_id, int size, int width)
{
    pdf_piece *p;
    if (!len) return 1;
    if (line->n >= PDF_MAX_LINE_PIECES) return 0;
    p = &line->p[line->n++];
    p->text = text; p->len = len; p->fmt = *fmt; p->font_id = font_id; p->size = size; p->width = width;
    line->width += width;
    if (size > line->max_size) line->max_size = size;
    return 1;
}

static int pdf_flush_line(pdf_layout *l, const owf_para *para, pdf_line *line,
                          int first_line)
{
    owf_buf *out; int i, x, avail, line_height, extra = 0, spaces = 0;
    if (!line->n) line->max_size = line->max_size ? line->max_size : 12;
    line_height = (line->max_size * 6 + 4) / 5;
    if (para->fmt.line_spacing > 0) line_height = line_height * para->fmt.line_spacing / 100;
    if (line_height < line->max_size + 1) line_height = line->max_size + 1;
    if (OWF_LINE_IS_HEIGHT(para->fmt.line_spacing)) {   /* 2.0.18: points, from twips: exactly, or at least */
        int h = (OWF_LINE_HEIGHT(para->fmt.line_spacing) + 10) / 20;
        if (OWF_LINE_IS_EXACT(para->fmt.line_spacing) || h > line_height) line_height = h > 0 ? h : 1;
    }
    if (l->y - line_height < l->bottom) {
        if (!pdf_new_page(l)) return 0;
    }
    out = &l->pages[l->current];
    x = l->left + pdf_points(para->fmt.indent_left) + (first_line ? pdf_points(para->fmt.indent_first) : 0);
    avail = l->page_w - l->right - pdf_points(para->fmt.indent_right) - x;
    if (para->fmt.align == OWF_ALIGN_CENTRE && line->width < avail) x += (avail-line->width)/2;
    else if (para->fmt.align == OWF_ALIGN_RIGHT && line->width < avail) x += avail-line->width;
    else if (para->fmt.align == OWF_ALIGN_JUSTIFY && line->width < avail) {
        for (i=0;i<line->n;++i) if (line->p[i].len==1 && line->p[i].text[0]==' ') ++spaces;
        if (spaces) extra=(avail-line->width)/spaces;
    }
    /* One text object for the line: each word follows the last by the font's
     * own widths (placing words at estimated positions overlapped some and
     * spread others). Justified lines widen their spaces with Tw. */
    if (line->n) {
        int baseline = l->y - line->max_size, ex = x;
        owf_buf_printf(out, "BT %d %d Td ", x, baseline);
        if (extra) owf_buf_printf(out, "%d Tw ", extra);
        for (i = 0; i < line->n; ++i) {
            pdf_piece *p = &line->p[i];
            pdf_colour(out, p->fmt.colour);
            owf_buf_printf(out, "/F%d %d Tf ", p->font_id, p->size);
            pdf_put_encoded(out, p->text, p->len); owf_buf_puts(out, " Tj\n");
        }
        owf_buf_puts(out, extra ? "0 Tw ET\n" : "ET\n");
        for (i = 0; i < line->n; ++i) {      /* underline and strike: at the estimated places */
            pdf_piece *p = &line->p[i];
            if (p->fmt.flags & (OWF_UNDERLINE|OWF_STRIKE)) {
                int uy = baseline - (p->fmt.flags & OWF_UNDERLINE ? 2 : -(p->size / 3));
                owf_buf_printf(out, "0 G 0.5 w %d %d m %d %d l S\n", ex, uy, ex + p->width, uy);
            }
            ex += p->width;
            if (extra && p->len == 1 && p->text[0] == ' ') ex += extra;
        }
    }
    for (i = 0; i < line->nmarks; ++i) {          /* 1.12.1: where each comment's mark went */
        if (l->nplaced == l->capplaced) {
            int want = l->capplaced ? l->capplaced * 2 : 16;
            pdf_mark *grown = (pdf_mark *)realloc(l->placed, (size_t)want * sizeof(*grown));
            if (!grown) return 0;
            l->placed = grown;
            l->capplaced = want;
        }
        l->placed[l->nplaced].comment = line->marks[i];
        l->placed[l->nplaced].page = l->current;
        l->placed[l->nplaced].y = l->y;
        ++l->nplaced;
    }
    l->y -= line_height;
    pdf_line_reset(line);
    return 1;
}

static int pdf_layout_text_run(pdf_layout *l, const owf_para *para, const owf_run *run,
                               pdf_line *line, int *first_line)
{
    const char *s=run->text?run->text:""; size_t len=strlen(s), at=0;
    int fid=pdf_font_id(l->doc,&run->fmt), size=pdf_heading_size(l->doc,para,&run->fmt);
    while (at < len) {
        size_t start=at,end; int is_space=0,width,avail,xbase;
        if (s[at]=='\n') { if(!pdf_flush_line(l,para,line,*first_line))return 0;*first_line=0;++at;continue; }
        if (s[at]==' ' || s[at]=='\t') { start=at; while(at<len&&(s[at]==' '||s[at]=='\t'))++at; end=at; is_space=1; }
        else { start=at; while(at<len&&s[at]!=' '&&s[at]!='\t'&&s[at]!='\n')++at; end=at; }
        width=pdf_piece_width(l->doc,s+start,end-start,&run->fmt,size);
        if(is_space && line->n==0)continue;
        xbase=l->left+pdf_points(para->fmt.indent_left)+(*first_line?pdf_points(para->fmt.indent_first):0);
        avail=l->page_w-l->right-pdf_points(para->fmt.indent_right)-xbase;
        if(line->n && line->width+width>avail){if(!pdf_flush_line(l,para,line,*first_line))return 0;*first_line=0;if(is_space)continue;}
        if(!pdf_line_add(line,s+start,end-start,&run->fmt,fid,size,width)){if(!pdf_flush_line(l,para,line,*first_line))return 0;*first_line=0;if(!pdf_line_add(line,s+start,end-start,&run->fmt,fid,size,width))return 0;}
    }
    return 1;
}

static int pdf_place_image(pdf_layout *l, const owf_para *para, int index)
{
    const owf_image *im = &l->doc->images[index];
    int w = pdf_points(im->width > 0 ? im->width : 2880), h = pdf_points(im->height > 0 ? im->height : 2160);
    int x = l->left + pdf_points(para->fmt.indent_left);
    int avail = l->page_w - l->right - x, tall = l->page_h - l->top - l->bottom;
    if (w > avail && w > 0) { h = h * avail / w; w = avail; }
    if (h > tall && h > 0) { w = w * tall / h; h = tall; }
    if (w < 1 || h < 1) return 1;
    if (l->y - h < l->bottom && !pdf_new_page(l)) return 0;
    if (para->fmt.align == OWF_ALIGN_CENTRE && w < avail) x += (avail - w) / 2;
    else if (para->fmt.align == OWF_ALIGN_RIGHT && w < avail) x += avail - w;
    l->y -= h;
    owf_buf_printf(&l->pages[l->current], "q %d 0 0 %d %d %d cm /Im%d Do Q\n", w, h, x, l->y, index + 1);
    l->image_used[index] = 1;
    l->y -= 4;
    return 1;
}

static int pdf_layout_para(pdf_layout *l, const owf_para *p, int i)
{
    int j;
    {
        pdf_line line; int first=1;
        if(p->fmt.page_break_before && (i || l->y < l->page_h-l->top) && !pdf_new_page(l))return 0;
        l->y-=pdf_points(p->fmt.space_before);
        pdf_line_reset(&line);
        for(j=0;j<p->nruns;++j){
            const owf_run *r=&p->runs[j];
            if(r->kind==OWF_RUN_TEXT&&r->note>0&&r->note<=l->doc->nnotes&&l->note_marks){
                /* 1.8: a note's mark, raised; the notes follow the text */
                owf_run fake=*r;fake.text=l->note_marks[r->note-1];fake.fmt.flags|=OWF_SUPER;
                if(!pdf_layout_text_run(l,p,&fake,&line,&first))return 0;}
            else if(r->kind==OWF_RUN_TEXT&&r->comment>0&&r->comment<=l->doc->ncomments&&l->comment_marks){
                /* 1.10.1: a comment's mark, raised where it is anchored; the comments follow the text */
                owf_run fake=*r;fake.text=l->comment_marks[r->comment-1];fake.fmt.flags|=OWF_SUPER;
                if(fake.text[0]&&!pdf_layout_text_run(l,p,&fake,&line,&first))return 0;
                if(fake.text[0]&&l->strip){
                    if(line.nmarks<PDF_MAX_LINE_MARKS)line.marks[line.nmarks++]=r->comment-1;
                    else ++l->crowded;}}     /* 2.0.44: no room beside the line: listed (pdf_layout_document) */
            else if(r->kind==OWF_RUN_TEXT){if(!pdf_layout_text_run(l,p,r,&line,&first))return 0;}
            else if(r->kind==OWF_RUN_TAB){owf_run fake=*r;fake.kind=OWF_RUN_TEXT;fake.text=(char *)"    ";if(!pdf_layout_text_run(l,p,&fake,&line,&first))return 0;}
            else if(r->kind==OWF_RUN_LINEBREAK){if(!pdf_flush_line(l,p,&line,first))return 0;first=0;}
            else if(r->kind==OWF_RUN_IMAGE&&r->image>=0&&r->image<l->doc->nimages&&l->image_used){
                if(line.n){if(!pdf_flush_line(l,p,&line,first))return 0;first=0;}
                if(!pdf_place_image(l,p,r->image))return 0;}
            else if(r->kind==OWF_RUN_FRAME&&r->image>=0&&r->image<l->doc->nframes&&l->image_used&&
                    l->doc->frames[r->image].image>=0&&l->doc->frames[r->image].image<l->doc->nimages){
                /* 1.3: a frame's picture where it is anchored, as before frames were read */
                if(line.n){if(!pdf_flush_line(l,p,&line,first))return 0;first=0;}
                if(!pdf_place_image(l,p,l->doc->frames[r->image].image))return 0;}
            else if(r->kind==OWF_RUN_FIELD){owf_run fake=*r;fake.kind=OWF_RUN_TEXT;fake.text=(char *)(r->field>OWF_FIELD_TIME?(r->text?r->text:""):"#");   /* a literal: the line keeps the pointer until it is flushed, after this block */
                if(!pdf_layout_text_run(l,p,&fake,&line,&first))return 0;}
        }
        if(line.n || !p->nruns){if(!pdf_flush_line(l,p,&line,first))return 0;}
        l->y-=pdf_points(p->fmt.space_after);
    }
    return 1;
}

/* 1.8: a kind of note after the text: a paragraph each of its paragraphs, the first with its mark */
static int pdf_layout_notes(pdf_layout *l, int endnote)
{
    const owf_doc *doc=l->doc; int i,j,any=0;
    for(i=0;i<doc->body.nparas;++i)for(j=0;j<doc->body.paras[i].nruns;++j){
        int m=doc->body.paras[i].runs[j].note; const char *t,*nl; int first=1;
        if(m<=0||m>doc->nnotes||doc->notes[m-1].endnote!=endnote||!l->note_marks)continue;
        for(t=doc->notes[m-1].text;;t=nl+1,first=0){
            owf_para np; owf_run runs[2]; char *piece; size_t len; int ok;
            nl=strchr(t,'\n'); len=nl?(size_t)(nl-t):strlen(t);
            piece=(char *)malloc(len+2); if(!piece)return 0;
            piece[0]=' '; memcpy(piece+1,t,len); piece[len+1]=0;
            memset(&np,0,sizeof np); owf_parafmt_init(&np.fmt); np.table_id=-1;
            np.fmt.space_before=first&&!any?240:0;
            memset(runs,0,sizeof runs);
            runs[0].kind=runs[1].kind=OWF_RUN_TEXT;
            owf_charfmt_init(&runs[0].fmt); runs[0].fmt=doc->base; runs[0].fmt.size=doc->base.size?doc->base.size*5/6:200;
            runs[1].fmt=runs[0].fmt; runs[0].fmt.flags|=OWF_SUPER;
            runs[0].text=first?l->note_marks[m-1]:(char *)""; runs[1].text=piece;
            np.runs=runs; np.nruns=2; np.capruns=2;
            ok=pdf_layout_para(l,&np,1);
            free(piece);
            if(!ok)return 0;
            any=1;
            if(!nl)break;
        }
    }
    return 1;
}

/* 1.10.1: a paragraph of the comments' list: one run, or the mark's and then the text's (bold the first) */
static int pdf_comment_para(pdf_layout *l, const char *head, const char *text, int indent, int space)
{
    const owf_doc *doc=l->doc; owf_para np; owf_run runs[2]; int n=0;
    memset(&np,0,sizeof np); owf_parafmt_init(&np.fmt); np.table_id=-1;
    np.fmt.space_before=space; np.fmt.indent_left=indent;
    memset(runs,0,sizeof runs);
    owf_charfmt_init(&runs[0].fmt); runs[0].fmt=doc->base; runs[0].fmt.size=doc->base.size?doc->base.size*5/6:200;
    runs[1].fmt=runs[0].fmt;
    if(head){runs[n].kind=OWF_RUN_TEXT;runs[n].text=(char *)head;runs[n].fmt.flags|=OWF_BOLD;++n;}
    if(text){runs[n].kind=OWF_RUN_TEXT;runs[n].text=(char *)text;++n;}
    np.runs=runs; np.nruns=n; np.capruns=2;
    return pdf_layout_para(l,&np,1);
}

/* 1.10.1: the comments after the text, numbered as their marks are: who and when, the comment it answers,
 * done; then its paragraphs */
static int pdf_layout_comments(pdf_layout *l)
{
    const owf_doc *doc=l->doc; int k;
    if(!l->ncomment_order)return 1;
    if(l->listed){                                   /* 1.12.1: the margin's: only those it had no room for */
        for(k=0;k<l->ncomment_order&&!l->listed[l->comment_order[k]];++k){}
        if(k==l->ncomment_order)return 1;
    }
    if(!pdf_comment_para(l,"Comments",NULL,0,240))return 0;
    for(k=0;k<l->ncomment_order;++k){
        const owf_annotation *c=&doc->comments[l->comment_order[k]];
        if(l->listed&&!l->listed[l->comment_order[k]])continue;
        char head[64],who[200],when[24]; const char *t,*nl; size_t n;
        snprintf(head,sizeof head,"%s ",l->comment_marks[l->comment_order[k]]);
        when[0]=0;
        if(c->date&&c->date[0]){
            snprintf(when,sizeof when,"%.16s",c->date);
            if(when[10]=='T')when[10]=' ';
        }
        snprintf(who,sizeof who,"%s%s%s",c->author&&c->author[0]?c->author:"Unknown",when[0]?", ":"",when);
        if(c->parent>=0&&c->parent<doc->ncomments&&l->comment_marks[c->parent][0]){
            n=strlen(who); snprintf(who+n,sizeof who-n,", answering %s",l->comment_marks[c->parent]);}
        if(c->done){n=strlen(who); snprintf(who+n,sizeof who-n,", done");}
        if(!pdf_comment_para(l,head,who,0,120))return 0;
        for(t=c->text?c->text:"";;t=nl+1){
            char *piece; int ok;
            nl=strchr(t,'\n'); n=nl?(size_t)(nl-t):strlen(t);
            piece=(char *)malloc(n+1); if(!piece)return 0;
            memcpy(piece,t,n); piece[n]=0;
            ok=pdf_comment_para(l,NULL,piece,360,0);
            free(piece);
            if(!ok)return 0;
            if(!nl)break;
        }
    }
    return 1;
}

/* 1.12.1: one line of a margin note, at 8 points: x, its baseline, the font (1 Helvetica, 2 bold) */
static void pdf_note_text(owf_buf *out, int x, int y, int font, const char *s, size_t len)
{
    owf_buf_printf(out, "0 g BT /F%d 8 Tf %d %d Td ", font, x, y);
    pdf_put_encoded(out, s, len);
    owf_buf_puts(out, " Tj ET\n");
}

/* The width at 8 points of Helvetica, about 4 points a character */
static int pdf_note_width(const char *s, size_t len)
{
    return (pdf_char_count(s, len) * 8 * 5 + 5) / 10;
}

/* The lines a comment's note takes in width points at 8 points: its head (cut to the width), then its text,
 * wrapped at spaces (a word wider than the width on a line of its own) and at its new lines. With out, drawn
 * from x, the first baseline 8 under y, 10 apart. */
static int pdf_note_lines(const char *head, const char *text, int width, owf_buf *out, int x, int y)
{
    const char *t = text ? text : "";
    size_t n = strlen(t), start = 0, hn = strlen(head);
    int lines = 1;
    while (hn > 1 && pdf_note_width(head, hn) > width) --hn;
    if (out) pdf_note_text(out, x, y - 8, 2, head, hn);
    while (start < n) {
        size_t cut = start;
        while (cut < n && t[cut] != '\n') {
            size_t we = cut;
            while (we < n && t[we] == ' ') ++we;
            while (we < n && t[we] != ' ' && t[we] != '\n') ++we;
            if (cut > start && pdf_note_width(t + start, we - start) > width) break;
            cut = we;
        }
        ++lines;
        if (out) pdf_note_text(out, x, y - 8 - (lines - 1) * 10, 1, t + start, cut - start);
        start = cut;
        while (start < n && t[start] == ' ') ++start;
        if (start < n && t[start] == '\n') ++start;
    }
    return lines;
}

/* 1.12.1: the notes in the strip beside their marks, page by page: stacked 4 points apart, a reply indented
 * 8, each in a pale box with a rule from the text's edge at its mark's line. One that runs past the foot of
 * the page is listed after the text instead (l->listed). */
static int pdf_place_margin_notes(pdf_layout *l)
{
    const owf_doc *doc = l->doc;
    int k, page = -1, bottom = 0;
    int edge = l->page_w - l->right, x0 = edge + 10, x1 = edge + l->strip;
    for (k = 0; k < l->nplaced; ++k) {
        const pdf_mark *m = &l->placed[k];
        const owf_annotation *c = &doc->comments[m->comment];
        char head[220], when[24];
        int x = x0 + (c->parent >= 0 ? 8 : 0), w = x1 - x, lines, h, top;
        owf_buf *out;
        if (m->page != page) { page = m->page; bottom = l->page_h; }
        when[0] = 0;
        if (c->date && c->date[0]) {
            snprintf(when, sizeof when, "%.10s", c->date);
        }
        snprintf(head, sizeof head, "%s %s%s%s%s", l->comment_marks[m->comment],
                 c->author && c->author[0] ? c->author : "Unknown", when[0] ? ", " : "", when, c->done ? ", done" : "");
        lines = pdf_note_lines(head, c->text, w - 8, NULL, 0, 0);
        h = lines * 10 + 6;
        top = m->y < bottom - 4 ? m->y : bottom - 4;
        if (top - h < l->bottom) { l->listed[m->comment] = 1; continue; }
        out = &l->pages[m->page];
        owf_buf_printf(out, "q %s rg %d %d %d %d re f 0.6 G 0.5 w %d %d %d %d re S Q\n",
                       c->done ? "0.93 0.93 0.93" : "1 0.96 0.75", x, top - h, w, h, x, top - h, w, h);
        owf_buf_printf(out, "q 0.6 G 0.5 w %d %d m %d %d l S Q\n", edge + 2, m->y - 4, x, top - 4);   /* mark's line to note */
        pdf_note_lines(head, c->text, w - 8, out, x + 4, top - 2);
        bottom = top - h;
    }
    return 1;
}

static int pdf_layout_document(pdf_layout *l)
{
    int i;
    if(!pdf_new_page(l))return 0;
    for(i=0;i<l->doc->body.nparas;++i)
        if(!pdf_layout_para(l,&l->doc->body.paras[i],i))return 0;
    if(l->note_marks&&(!pdf_layout_notes(l,0)||!pdf_layout_notes(l,1)))return 0;
    if(l->strip&&l->comment_marks){                  /* 1.12.1: those with no mark in the body: listed */
        for(i=0;i<l->doc->ncomments;++i)l->listed[i]=1;
        for(i=0;i<l->nplaced;++i)l->listed[l->placed[i].comment]=0;
        if(!pdf_place_margin_notes(l))return 0;
    }
    if(l->comment_marks&&!pdf_layout_comments(l))return 0;
    pdf_add_headers_footers(l);
    return 1;
}

static const char *pdf_font_name(int id)
{
    static const char *n[]={NULL,
        "Helvetica","Helvetica-Bold","Helvetica-Oblique","Helvetica-BoldOblique",
        "Times-Roman","Times-Bold","Times-Italic","Times-BoldItalic",
        "Courier","Courier-Bold","Courier-Oblique","Courier-BoldOblique"};
    return id>=1&&id<=12?n[id]:"Times-Roman";
}

/* 1.10.1: number the comments: those marked in the body in the order their marks come, then the rest that are
 * in the document (a header's, a text box's), which have no mark in the text. 0 when memory runs out. */
static int pdf_number_comments(pdf_layout *l)
{
    const owf_doc *doc=l->doc; int i,j,k;
    l->comment_marks=(char (*)[16])calloc((size_t)doc->ncomments,sizeof *l->comment_marks);
    l->comment_order=(int *)calloc((size_t)doc->ncomments,sizeof *l->comment_order);
    if(!l->comment_marks||!l->comment_order)return 0;
    for(i=0;i<doc->body.nparas;++i)for(j=0;j<doc->body.paras[i].nruns;++j){
        int m=doc->body.paras[i].runs[j].comment;
        if(m<=0||m>doc->ncomments||l->comment_marks[m-1][0])continue;
        l->comment_order[l->ncomment_order++]=m-1;
        snprintf(l->comment_marks[m-1],sizeof l->comment_marks[m-1],"[%d]",l->ncomment_order);
    }
    for(k=0;k<doc->ncomments;++k){
        if(l->comment_marks[k][0]||!owf_comment_place(doc,k,NULL,NULL,NULL))continue;
        l->comment_order[l->ncomment_order++]=k;
        snprintf(l->comment_marks[k],sizeof l->comment_marks[k],"[%d]",l->ncomment_order);
    }
    /* the list shows a mark for these too; in the text only the body's runs draw one */
    return 1;
}

static int export_pdf_as(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report,
                         int comments)
{
    pdf_layout l; owf_buf out; unsigned long *offs=NULL,xref; int i,obj_count,result=OWF_OK;
    pdf_image *imgs=NULL; int *imobj=NULL, nextobj;
    memset(&l,0,sizeof l);l.doc=doc;l.report=report;
    if(doc->nimages>0){l.image_used=(unsigned char *)calloc((size_t)doc->nimages,1);imgs=(pdf_image *)calloc((size_t)doc->nimages,sizeof *imgs);imobj=(int *)calloc((size_t)doc->nimages,sizeof *imobj);
        if(!l.image_used||!imgs||!imobj){free(l.image_used);free(imgs);free(imobj);return OWF_ERR_MEMORY;}}
    l.page_w=pdf_points(doc->page.width);l.page_h=pdf_points(doc->page.height);
    l.left=pdf_points(doc->page.margin_left);l.right=pdf_points(doc->page.margin_right);
    l.top=pdf_points(doc->page.margin_top);l.bottom=pdf_points(doc->page.margin_bottom);
    if(l.page_w<72||l.page_h<72)return OWF_ERR_FORMAT;
    if(doc->nnotes>0){                               /* 1.8 */
        l.note_marks=(char (*)[32])calloc((size_t)doc->nnotes,sizeof *l.note_marks);
        if(!l.note_marks){result=OWF_ERR_MEMORY;goto done;}
        for(i=0;i<doc->nnotes;++i)owf_note_mark_text(doc,i,l.note_marks[i],sizeof l.note_marks[i]);
    }
    if(comments&&doc->ncomments>0&&!pdf_number_comments(&l)){result=OWF_ERR_MEMORY;goto done;}   /* 1.10.1 */
    if(comments==2&&doc->ncomments>0){               /* 1.12.1: a strip for the notes, a third of the text's width */
        int text=l.page_w-l.left-l.right;
        l.strip=text/3>180?180:text/3;
        if(text-l.strip<144)l.strip=0;               /* too narrow a page: the list after the text, as before */
        else{
            l.right+=l.strip;
            l.listed=(unsigned char *)calloc((size_t)doc->ncomments,1);
            if(!l.listed){result=OWF_ERR_MEMORY;goto done;}
        }
    }
    if(!pdf_layout_document(&l)){result=OWF_ERR_MEMORY;goto done;}
    if(l.strip){                                     /* 2.0.44 (Budgie, #269): */
        int shown=0;
        for(i=0;i<doc->ncomments&&!shown;++i)shown=!l.listed[i];
        if(!shown){result=-1;goto done;}             /* none in the margin: laid out again with the text full width */
        if(l.crowded)owf_report_add(report,OWF_NOTE_APPROX,"%d comment%s on lines with more than %d marks %s listed after the text, not in the margin",
                                    l.crowded,l.crowded==1?"":"s",PDF_MAX_LINE_MARKS,l.crowded==1?"is":"are");
    }
    /* objects after the pages: each picture drawn, and its soft mask */
    nextobj=15+l.npages*2;
    for(i=0;i<doc->nimages;++i){
        if(!l.image_used[i])continue;
        pdf_image_encode(&doc->images[i],&imgs[i]);
        if(!imgs[i].ok){owf_report_add(report,OWF_NOTE_LOST,"Pictures in a format the PDF export does not draw yet (only JPEG and PNG) are left out");continue;}
        imobj[i]=nextobj;nextobj+=imgs[i].mask?2:1;
    }
    obj_count=nextobj-1;
    offs=(unsigned long *)calloc((size_t)obj_count+1,sizeof(*offs));if(!offs){result=OWF_ERR_MEMORY;goto done;}
    owf_buf_init(&out);owf_buf_puts(&out,"%PDF-1.4\n% GFOpenWrite PDF\n");
#define OBJ(N) do{offs[(N)]=(unsigned long)out.length;owf_buf_printf(&out,"%d 0 obj\n",(N));}while(0)
    OBJ(1);owf_buf_puts(&out,"<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    OBJ(2);owf_buf_printf(&out,"<< /Type /Pages /Count %d /Kids [",l.npages);for(i=0;i<l.npages;++i)owf_buf_printf(&out," %d 0 R",16+i*2);owf_buf_puts(&out," ] >>\nendobj\n");
    for(i=1;i<=12;++i){OBJ(2+i);owf_buf_printf(&out,"<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>\nendobj\n",pdf_font_name(i));}
    for(i=0;i<l.npages;++i){int co=15+i*2,po=co+1,j;OBJ(co);owf_buf_printf(&out,"<< /Length %lu >>\nstream\n",(unsigned long)l.pages[i].length);owf_buf_put(&out,l.pages[i].data,l.pages[i].length);owf_buf_puts(&out,"endstream\nendobj\n");OBJ(po);owf_buf_printf(&out,"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /Font <<",l.page_w,l.page_h);for(j=1;j<=12;++j)owf_buf_printf(&out," /F%d %d 0 R",j,2+j);owf_buf_puts(&out," >>");
        {int any=0;for(j=0;j<doc->nimages;++j)if(imobj&&imobj[j]){if(!any)owf_buf_puts(&out," /XObject <<");any=1;owf_buf_printf(&out," /Im%d %d 0 R",j+1,imobj[j]);}if(any)owf_buf_puts(&out," >>");}
        owf_buf_printf(&out," >> /Contents %d 0 R >>\nendobj\n",co);}
    for(i=0;i<doc->nimages;++i){
        pdf_image *im=&imgs[i];
        if(!imobj||!imobj[i])continue;
        OBJ(imobj[i]);owf_buf_printf(&out,"<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /%s /BitsPerComponent 8 /Filter /%s /Length %lu",
            im->w,im->h,im->gray?"DeviceGray":"DeviceRGB",im->dct?"DCTDecode":"FlateDecode",(unsigned long)im->len);
        if(im->mask)owf_buf_printf(&out," /SMask %d 0 R",imobj[i]+1);
        owf_buf_puts(&out," >>\nstream\n");owf_buf_put(&out,im->data,im->len);owf_buf_puts(&out,"\nendstream\nendobj\n");
        if(im->mask){OBJ(imobj[i]+1);owf_buf_printf(&out,"<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length %lu >>\nstream\n",im->w,im->h,(unsigned long)im->mlen);
            owf_buf_put(&out,im->mask,im->mlen);owf_buf_puts(&out,"\nendstream\nendobj\n");}
    }
    xref=(unsigned long)out.length;owf_buf_printf(&out,"xref\n0 %d\n0000000000 65535 f \n",obj_count+1);for(i=1;i<=obj_count;++i)owf_buf_printf(&out,"%010lu 00000 n \n",offs[i]);owf_buf_printf(&out,"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%lu\n%%%%EOF\n",obj_count+1,xref);
#undef OBJ
    if(out.failed){owf_buf_free(&out);result=OWF_ERR_MEMORY;goto done;}
    result=owf_buf_take(&out,data,length);
done:
    free(l.note_marks);
    free(l.comment_marks);free(l.comment_order);free(l.placed);free(l.listed);
    free(offs);for(i=0;i<l.npages;++i)owf_buf_free(&l.pages[i]);free(l.pages);
    for(i=0;imgs&&i<doc->nimages;++i){if(!imgs[i].dct)free(imgs[i].data);free(imgs[i].mask);}
    free(imgs);free(imobj);free(l.image_used);
    if(result==-1){                                  /* 2.0.44: no note had room in the margin */
        owf_report_add(report,OWF_NOTE_APPROX,"No comment had room in the margin, so they are listed after the text and the text keeps its width");
        return export_pdf_as(doc,data,length,report,1);
    }
    return result;
}

static int export_pdf(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    return export_pdf_as(doc, data, length, report, 0);
}

/* 1.10.1: the same PDF with the comments: a raised mark where each is anchored and a list after the text (as
 * printing with comments at the end of the document does) */
static int export_pdf_comments(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    return export_pdf_as(doc, data, length, report, 1);
}

const owf_format owf_format_pdf = {
    "pdf", "Portable Document Format", "pdf", NULL, NULL, export_pdf
};

const owf_format owf_format_pdf_comments = {
    "pdf-comments", "Portable Document Format, with the comments", "pdf", NULL, NULL, export_pdf_comments
};

/* 1.12.1: the comments beside their lines, as Word prints markup in balloons: the text narrowed by a strip at
 * its right where each comment's note is drawn level with its mark; one with no room there (or no mark in the
 * body) is listed after the text */
static int export_pdf_comments_margin(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    return export_pdf_as(doc, data, length, report, 2);
}

const owf_format owf_format_pdf_comments_margin = {
    "pdf-comments-margin", "Portable Document Format, with the comments in the margin", "pdf", NULL, NULL,
    export_pdf_comments_margin
};

/* ---- Pictures for the page writer (owf_pdf.c) ---- */

int owf_pdf_encode_picture(const owf_image *image, owf_pdf_picture *out)
{
    pdf_image img;
    memset(out, 0, sizeof *out);
    if (!image)
        return 0;
    pdf_image_encode(image, &img);
    if (!img.ok) {
        if (!img.dct)
            free(img.data);
        free(img.mask);
        return 0;
    }
    out->w = img.w;
    out->h = img.h;
    out->gray = img.gray;
    out->dct = img.dct;
    out->data = img.data;
    out->len = img.len;
    out->mask = img.mask;
    out->mlen = img.mlen;
    out->owned = !img.dct;       /* JPEG's bytes are the image's own */
    return 1;
}

void owf_pdf_picture_free(owf_pdf_picture *p)
{
    if (!p)
        return;
    if (p->owned)
        free(p->data);
    free(p->mask);
    memset(p, 0, sizeof *p);
}
