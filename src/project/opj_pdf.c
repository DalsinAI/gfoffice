/*
 * GFOpenProject: the printed Gantt view as a PDF. Each page opj_print_layout
 * lays out is drawn by opj_print_draw onto an oldraw target that writes into
 * libowf's page PDF writer (owf_pdf.h): fills as rectangles, text in PDF's
 * standard fonts (so a plan's PDF stays small and searchable). A pixel is
 * OPJ_PDF_TWIPS twips; a page is A4 landscape with a 1 cm margin.
 * Integer arithmetic only.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_core.h"
#include "owf_pdf.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* The pens a page is printed with: black on white, whatever the screen's palette. */
static const oldraw_colour print_pens[OLDRAW_PEN_COUNT] = {
    OLDRAW_RGB(0x000000), /* DETAIL */
    OLDRAW_RGB(0xFFFFFF), /* BLOCK */
    OLDRAW_RGB(0x000000), /* TEXT */
    OLDRAW_RGB(0xFFFFFF), /* SHINE */
    OLDRAW_RGB(0x000000), /* SHADOW */
    OLDRAW_RGB(0x6688BB), /* FILL */
    OLDRAW_RGB(0xFFFFFF), /* FILLTEXT */
    OLDRAW_RGB(0xFFFFFF), /* BACKGROUND */
    OLDRAW_RGB(0x000000), /* HIGHLIGHTTEXT */
    OLDRAW_RGB(0x000000), /* BARDETAIL */
    OLDRAW_RGB(0xFFFFFF), /* BARBLOCK */
    OLDRAW_RGB(0x000000)  /* BARTRIM */
};

typedef struct {
    owf_pdf *pdf;
    int failed;
} pdf_target;

static unsigned long rgb_of(oldraw_colour c)
{
    if ((c & 0xFF000000UL) == 0xFE000000UL) c = (c & 0xFFUL) < OLDRAW_PEN_COUNT ? print_pens[c & 0xFFUL] : print_pens[OLDRAW_PEN_TEXT];
    return (unsigned long)(c & 0xFFFFFFUL);
}

static int has(const char *s, const char *word)
{
    size_t n = strlen(word);
    for (; s && *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i] && tolower((unsigned char)s[i]) == word[i]; i++)
            ;
        if (i == n) return 1;
    }
    return 0;
}

static owf_font_kind kind_of(const oldraw_font *f)
{
    const char *family = f ? f->family : NULL;
    if (has(family, "mono") || has(family, "courier") || has(family, "topaz")) return OWF_FONT_MONO;
    if (has(family, "sans") || has(family, "helvetica") || has(family, "arial")) return OWF_FONT_SANS;
    if (has(family, "serif") || has(family, "times")) return OWF_FONT_SERIF;
    return OWF_FONT_SANS;
}

static unsigned flags_of(unsigned style)
{
    return ((style & OLDRAW_BOLD) ? OWF_BOLD : 0) | ((style & OLDRAW_ITALIC) ? OWF_ITALIC : 0) |
           ((style & OLDRAW_UNDERLINE) ? OWF_UNDERLINE : 0);
}

static int px_of(const oldraw_font *f)
{
    return f && f->px > 0 ? f->px : OPJ_PDF_FONT_PX;
}

static void p_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    pdf_target *p = (pdf_target *)t;
    unsigned long rgb = rgb_of(c);
    if (rgb == 0xFFFFFFUL) return;                     /* the paper is white already: a smaller PDF */
    if (owf_pdf_rect(p->pdf, OPJ_PDF_MARGIN + x * OPJ_PDF_TWIPS, OPJ_PDF_MARGIN + y * OPJ_PDF_TWIPS,
                     w * OPJ_PDF_TWIPS, h * OPJ_PDF_TWIPS, OWF_SHADE(rgb), 0, 0) < 0)
        p->failed = 1;
}

static void p_invert(void *t, int x, int y, int w, int h)
{
    (void)t; (void)x; (void)y; (void)w; (void)h;      /* a printed page has no selection to show */
}

static void p_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    (void)t; (void)img; (void)sx; (void)sy; (void)x; (void)y; (void)w; (void)h; (void)flags;
}

static int p_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    int px = px_of(f), tw;
    (void)t;
    tw = owf_pdf_text_width(s, n, kind_of(f), flags_of(f ? f->style : 0), px * OPJ_PDF_TWIPS);
    size->width = (tw + OPJ_PDF_TWIPS - 1) / OPJ_PDF_TWIPS;
    size->ascent = (px * 4 + 2) / 5;
    size->descent = px - size->ascent;
    return 1;
}

static int p_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    pdf_target *p = (pdf_target *)t;
    int px = px_of(f), size = px * OPJ_PDF_TWIPS;
    unsigned flags = flags_of(st | (f ? f->style : 0));
    /* a PDF has no clip here: text that starts left of the clip (a label run on from the page before) is left
     * out rather than drawn over the table */
    if (clip && x < clip->x) return 0;
    if (owf_pdf_text(p->pdf, OPJ_PDF_MARGIN + x * OPJ_PDF_TWIPS, OPJ_PDF_MARGIN + y * OPJ_PDF_TWIPS, 0, s, n,
                     kind_of(f), flags, size, rgb_of(c)) < 0) {
        p->failed = 1;
        return 0;
    }
    return 1;
}

static void p_flush(void *t)
{
    (void)t;
}

static int p_copy(void *t, int sx, int sy, int dx, int dy, int w, int h)
{
    (void)t; (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h;
    return 1;                                          /* nothing to copy from: oldraw draws it again */
}

static const oldraw_ops pdf_ops = { p_fill, p_invert, p_image, p_text, p_measure, p_flush, NULL, p_copy };

int opj_pdf_write(const opj_doc *doc, const opj_view *proto, unsigned char **data, size_t *length, int *pages)
{
    opj_print pr;
    opj_view v;
    pdf_target t;
    oldraw *d = NULL;
    int w = (OPJ_PDF_PAGE_W - 2 * OPJ_PDF_MARGIN) / OPJ_PDF_TWIPS, h = (OPJ_PDF_PAGE_H - 2 * OPJ_PDF_MARGIN) / OPJ_PDF_TWIPS;
    int n, page, rc;
    if (data) *data = NULL;
    if (length) *length = 0;
    if (pages) *pages = 0;
    if (!doc || !doc->plan || !proto || !data || !length) return OWF_ERR_FORMAT;
    if (!doc->plan->ntasks) return OWF_ERR_FORMAT;
    v = *proto;                                        /* the screen's columns and zoom; paper's own font */
    v.font.family = NULL;
    v.font.px = OPJ_PDF_FONT_PX;
    v.font.style = 0;
    v.row_height = OPJ_PDF_ROW;
    if (!(n = opj_print_layout(&pr, &v, doc, w, h))) return OWF_ERR_FORMAT;
    memset(&t, 0, sizeof t);
    if (!(t.pdf = owf_pdf_new())) return OWF_ERR_MEMORY;
    if (!(d = oldraw_new(&pdf_ops, &t, w, h, print_pens))) {
        owf_pdf_free(t.pdf);
        return OWF_ERR_MEMORY;
    }
    for (page = 0; page < n && !t.failed; page++) {
        if (owf_pdf_page(t.pdf, OPJ_PDF_PAGE_W, OPJ_PDF_PAGE_H) < 0) {
            t.failed = 1;
            break;
        }
        opj_print_draw(d, &pr, doc, page);
    }
    oldraw_free(d);
    rc = t.failed ? OWF_ERR_MEMORY
                  : owf_pdf_finish(t.pdf, doc->plan->title && *doc->plan->title ? doc->plan->title : "GFOpenProject",
                                   data, length, NULL);
    owf_pdf_free(t.pdf);
    if (rc == OWF_OK && pages) *pages = n;
    return rc;
}
