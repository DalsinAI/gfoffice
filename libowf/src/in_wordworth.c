/*
 * libowf: Wordworth documents (Digita), IFF FORM WOWO.
 *
 * Wordworth's format is not published. This reader follows what we found in
 * real documents from Aminet (docs/formats/wordworth.md), with EvenMore's
 * plugin (Chris Perver) read for reference only:
 *   WFNT  a font: number, flags, size (word), name ("IF_" is Intellifont)
 *   WDOC  the document: page width and height in millipoints at 4 and 8
 *   WPAR  the next paragraphs' format: alignment at 18, then font, style,
 *         misc, pen and paper at 21-25
 *   WTXT  text; 0x0F ends each paragraph, and one chunk may hold several
 *   WFSC  changes inside the WTXT before it: 12-byte entries, the offset
 *         (long), then font, style, misc, pen, paper
 *   WPAG  a page break; WHED and WFOT start the header and footer
 *   FORM  pictures and drawings (GTID, ILBM), not brought across yet
 * Style bits are graphics.library's: 1 underline, 2 bold, 4 italic.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define END_OF_PARAGRAPH 0x0F

typedef struct {
    owf_doc *doc;
    owf_report *report;
    int fonts[256];          /* Wordworth font number -> document font */
    int sizes[256];          /* twips */
    unsigned long palette[256];
    int npalette;
    owf_builder body, header, footer, *cur;
    owf_buf scratch;
    int align, font, style, colour;     /* from the last WPAR */
    const unsigned char *text;
    unsigned long text_size;
    int pictures, tabs;
} reader;

static void char_format(reader *r, owf_charfmt *f, int font, int style, int pen)
{
    owf_charfmt_init(f);
    f->font = r->fonts[font & 0xFF];
    f->size = r->sizes[font & 0xFF];
    if (style & 1) f->flags |= OWF_UNDERLINE;
    if (style & 2) f->flags |= OWF_BOLD;
    if (style & 4) f->flags |= OWF_ITALIC;
    /* Pens 0 and 1 are the paper's text colour; others come from the palette. */
    if (pen > 1 && pen < r->npalette && r->palette[pen] != 0)
        f->colour = r->palette[pen];
}

static void set_fmt(owf_builder *b, const owf_charfmt *f)
{
    if (memcmp(f, &b->charfmt, sizeof *f) != 0) {
        owf_builder_flush(b);
        b->charfmt = *f;
    }
}

static void put(reader *r, const unsigned char *text, unsigned long n)
{
    if (!n)
        return;
    r->scratch.length = 0;
    owf_buf_put_latin1(&r->scratch, text, n);
    if (r->scratch.failed)
        r->cur->failed = 1;
    else
        owf_builder_text(r->cur, (const char *)r->scratch.data, r->scratch.length);
}

/* The pending WTXT, with an WFSC's changes if one came. */
static void emit(reader *r, const unsigned char *fscc, unsigned long fscc_size)
{
    owf_builder *b = r->cur;
    owf_charfmt f;
    unsigned long i, start = 0, n = fscc ? fscc_size / 12 : 0, next = 0;

    if (!r->text)
        return;
    b->parafmt.align = r->align == 1 ? OWF_ALIGN_CENTRE : r->align == 2 ? OWF_ALIGN_RIGHT
                     : r->align == 3 ? OWF_ALIGN_JUSTIFY : OWF_ALIGN_LEFT;
    char_format(r, &f, r->font, r->style, r->colour);
    set_fmt(b, &f);
    for (i = 0; i <= r->text_size; i++) {
        unsigned c;
        while (next < n && OWF_BE32(fscc + next * 12) <= i) {
            const unsigned char *e = fscc + next * 12;
            put(r, r->text + start, i - start);
            start = i;
            char_format(r, &f, e[4], e[5], e[7]);
            set_fmt(b, &f);
            next++;
        }
        if (i == r->text_size)
            break;
        c = r->text[i];
        if (c >= 0x20 && !(c >= 0x7F && c < 0xA0))
            continue;
        put(r, r->text + start, i - start);
        start = i + 1;
        if (c == END_OF_PARAGRAPH) {
            owf_builder_end_para(b);
            b->parafmt.align = r->align == 1 ? OWF_ALIGN_CENTRE : r->align == 2 ? OWF_ALIGN_RIGHT
                             : r->align == 3 ? OWF_ALIGN_JUSTIFY : OWF_ALIGN_LEFT;
        } else if (c == '\t') {
            owf_builder_special(b, OWF_RUN_TAB, OWF_FIELD_PAGE);
        } else if (c == '\n') {
            owf_builder_special(b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
        }
    }
    put(r, r->text + start, r->text_size - start);
    r->text = NULL;
}

static void read_font(reader *r, const owf_iff_chunk *c)
{
    char name[64];
    unsigned long n = 0;
    const char *p;
    owf_font_kind kind = OWF_FONT_ANY;
    int font;

    if (c->size < 5)
        return;
    while (4 + n < c->size && c->data[4 + n] && n < sizeof name - 1) {
        name[n] = (char)c->data[4 + n];
        n++;
    }
    name[n] = 0;
    p = !strncmp(name, "IF_", 3) ? name + 3 : name;     /* Intellifont outline fonts */
    if (!*p)
        return;
    owf_font_modern(p, &kind);
    font = owf_doc_font(r->doc, p, kind);
    if (font < 0) {
        r->body.failed = 1;
        return;
    }
    r->fonts[c->data[0]] = font;
    r->sizes[c->data[0]] = (int)OWF_BE16(c->data + 2) * OWF_TWIPS_PER_POINT;
}

static void drop_if_empty(owf_story *story)
{
    int i;
    for (i = 0; i < story->nparas; i++)
        if (story->paras[i].nruns)
            return;
    for (i = 0; i < story->nparas; i++)
        free(story->paras[i].runs);
    story->nparas = 0;
}

static int detect_wordworth(const unsigned char *data, size_t length)
{
    return owf_iff_is_form(data, length, OWF_ID('W', 'O', 'W', 'O')) ? 100 : 0;
}

static int import_wordworth(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_iff_reader iff;
    owf_iff_chunk c;
    reader *r;
    int i, result;

    if (!owf_iff_open(&iff, data, length, OWF_ID('W', 'O', 'W', 'O')))
        return OWF_ERR_FORMAT;
    r = calloc(1, sizeof *r);
    if (!r)
        return OWF_ERR_MEMORY;
    r->doc = doc;
    r->report = report;
    for (i = 0; i < 256; i++)
        r->fonts[i] = -1;
    owf_builder_init(&r->body, doc, &doc->body);
    owf_builder_init(&r->header, doc, &doc->header);
    owf_builder_init(&r->footer, doc, &doc->footer);
    owf_buf_init(&r->scratch);
    r->cur = &r->body;
    r->font = 0xFF;

    while (owf_iff_next(&iff, &c)) {
        if (c.id == OWF_ID('W', 'F', 'S', 'C')) {
            emit(r, c.data, c.size);
            continue;
        }
        if (c.id == OWF_ID('W', 'S', 'P', 'C'))
            continue;               /* goes with WFSC; not understood yet */
        emit(r, NULL, 0);
        switch (c.id) {
        case OWF_ID('W', 'F', 'N', 'T'):
            read_font(r, &c);
            break;
        case OWF_ID('W', 'D', 'O', 'C'):
            if (c.size >= 12) {
                unsigned long w = OWF_BE32(c.data + 4), h = OWF_BE32(c.data + 8);
                /* millipoints: 1000 to the point, 20 twips to the point */
                if (w >= 72000 && w <= 2000000 && h >= 72000 && h <= 2000000) {
                    doc->page.width = (int)(w / 50);
                    doc->page.height = (int)(h / 50);
                }
            }
            break;
        case OWF_ID('W', 'K', 'C', 'O'):
            if (c.size >= 2) {
                int count = c.data[1];
                for (i = 0; i < count && 2 + (unsigned long)i * 3 + 3 <= c.size; i++)
                    r->palette[i] = ((unsigned long)c.data[2 + i * 3] << 16) | ((unsigned long)c.data[3 + i * 3] << 8) |
                                    c.data[4 + i * 3];
                r->npalette = i;
            }
            break;
        case OWF_ID('W', 'P', 'A', 'R'):
            if (c.size >= 26) {
                r->align = c.data[18];
                r->font = c.data[21];
                r->style = c.data[22];
                r->colour = c.data[24];
            }
            break;
        case OWF_ID('W', 'T', 'A', 'B'):
            if (c.size)
                r->tabs = 1;
            break;
        case OWF_ID('W', 'T', 'X', 'T'):
            r->text = c.data;
            r->text_size = c.size;
            break;
        case OWF_ID('W', 'P', 'A', 'G'):
            if (r->cur->para || r->cur->text.length)
                owf_builder_end_para(r->cur);
            r->cur->parafmt.page_break_before = 1;
            break;
        case OWF_ID('W', 'H', 'E', 'D'):
        case OWF_ID('W', 'F', 'O', 'T'):
            if (r->cur->para || r->cur->text.length)
                owf_builder_end_para(r->cur);
            r->cur = c.id == OWF_ID('W', 'H', 'E', 'D') ? &r->header : &r->footer;
            if (c.size >= 2) {
                if (c.id == OWF_ID('W', 'H', 'E', 'D'))
                    doc->page.header_on_first = c.data[1] != 0;
                else
                    doc->page.footer_on_first = c.data[1] != 0;
            }
            break;
        case OWF_ID('F', 'O', 'R', 'M'):
            r->pictures++;
            break;
        default:
            break;
        }
    }
    emit(r, NULL, 0);
    for (i = 0; i < 3; i++) {
        owf_builder *b = i == 0 ? &r->body : i == 1 ? &r->header : &r->footer;
        if (b->para || b->text.length)
            owf_builder_end_para(b);
    }
    if (r->pictures)
        owf_report_add(report, OWF_NOTE_LOST, "%d picture(s) or drawing(s) are not brought across yet", r->pictures);
    if (r->tabs)
        owf_report_add(report, OWF_NOTE_APPROX, "Tab stops are at the default positions");
    if (iff.truncated)
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the text up to the cut was read");
    owf_report_add(report, OWF_NOTE_INFO, "Wordworth's format is not published: indents and margins use the defaults");

    owf_buf_free(&r->scratch);
    result = owf_builder_done(&r->body);
    if (owf_builder_done(&r->header) != OWF_OK || owf_builder_done(&r->footer) != OWF_OK)
        result = OWF_ERR_MEMORY;
    /* Wordworth always has header and footer sections; empty ones go. */
    drop_if_empty(&doc->header);
    drop_if_empty(&doc->footer);
    free(r);
    return result;
}

const owf_format owf_format_wordworth = {
    "wordworth", "Wordworth (Digita, IFF WOWO)", "wwd ww", detect_wordworth, import_wordworth, NULL
};
