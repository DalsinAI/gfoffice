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
 *   FORM GTID  the drawing layer: FORM GTIP holds objects (RECT, RRCT,
 *         ELLP, CIRC, LINE, GROU, PICT, PTXT text, and text boxes: a WOWO
 *         chunk, then a FORM WOWO with the box's own WPAR, WTXT, WFSC).
 *         Each object starts with its place: x, y, width and height in
 *         millipoints, longs at 2, 7, 12 and 17. Text boxes and PTXT text
 *         (length word at 38, text at 40) become frames on the page,
 *         anchored in reading order (top to bottom, then left to right);
 *         shapes and pictures are counted and reported
 *   FORM ILBM  pictures, not brought across yet
 * Style bits are graphics.library's: 1 underline, 2 bold, 4 italic.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
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
    int shapes, boxes;       /* the drawing layer's */
} reader;

/* A text box or text object of the drawing layer, waiting to be anchored. */
typedef struct {
    int frame;               /* into doc->frames */
    long x, y;
} placed;

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

/* An object's place: x, y, width, height in millipoints (longs at 2, 7,
 * 12 and 17), into a frame's twips. 0 when the chunk is too short. */
static int object_place(const owf_iff_chunk *c, owf_frame *fr, long *x, long *y)
{
    long v[4];
    int i;

    if (c->size < 21)
        return 0;
    for (i = 0; i < 4; i++)
        v[i] = (long)OWF_BE32(c->data + 2 + 5 * i);
    if (v[0] < 0 || v[1] < 0 || v[2] <= 0 || v[3] <= 0 || v[0] > 4000000 || v[1] > 4000000 || v[2] > 4000000 ||
        v[3] > 4000000)
        return 0;
    fr->x = (int)(v[0] / 50);                /* millipoints to twips */
    fr->y = (int)(v[1] / 50);
    fr->width = (int)(v[2] / 50);
    fr->height = (int)(v[3] / 50);
    *x = v[0];
    *y = v[1];
    return 1;
}

static void new_frame(owf_frame *fr)
{
    memset(fr, 0, sizeof *fr);
    fr->image = -1;
    fr->x_from = fr->y_from = OWF_FROM_PAGE;
    fr->x_place = fr->y_place = OWF_PLACE_AT;
    fr->wrap = OWF_WRAP_NONE;
}

static void emit(reader *r, const unsigned char *fscc, unsigned long fscc_size);

/* A frame made from fr, its story built: kept in places, or freed when
 * nothing came of it. */
static void keep_frame(reader *r, owf_frame *fr, long x, long y, placed **places, int *nplaces, int *cap)
{
    int idx;

    if (!fr->story.nparas)
        return;
    if (*nplaces == *cap) {
        int want = *cap ? *cap * 2 : 16;
        placed *p = realloc(*places, (size_t)want * sizeof *p);
        if (!p) {
            r->body.failed = 1;
            return;
        }
        *places = p;
        *cap = want;
    }
    idx = owf_doc_add_frame(r->doc, fr);
    if (idx < 0) {
        r->body.failed = 1;
        return;
    }
    (*places)[*nplaces].frame = idx;
    (*places)[*nplaces].x = x;
    (*places)[*nplaces].y = y;
    (*nplaces)++;
    r->boxes++;
}

/* The drawing layer (FORM GTID and what it holds), depth-first. */
static void read_drawing(reader *r, const unsigned char *data, unsigned long size, int depth, placed **places,
                         int *nplaces, int *cap)
{
    owf_iff_reader iff;
    owf_iff_chunk c;
    owf_frame place;
    long px = 0, py = 0;
    int have_place = 0;

    if (depth > 8 || size < 12)
        return;
    iff.pos = data + 12;
    iff.end = data + 8 + (OWF_BE32(data + 4) < size - 8 ? OWF_BE32(data + 4) : size - 8);
    iff.truncated = 0;
    new_frame(&place);
    while (owf_iff_next(&iff, &c)) {
        if (c.id == OWF_ID('F', 'O', 'R', 'M') && c.size >= 4) {
            unsigned long type = OWF_BE32(c.data);
            if (type == OWF_ID('W', 'O', 'W', 'O')) {
                /* A text box: its own paragraphs, in a frame at the place
                 * the WOWO chunk before it gave. */
                owf_frame fr;
                owf_builder fb, *saved = r->cur;
                int align = r->align, font = r->font, style = r->style, colour = r->colour;
                owf_iff_reader box;
                owf_iff_chunk bc;
                new_frame(&fr);
                if (have_place) {
                    fr.x = place.x; fr.y = place.y;
                    fr.width = place.width; fr.height = place.height;
                }
                owf_builder_init(&fb, r->doc, &fr.story);
                r->cur = &fb;
                r->text = NULL;
                box.pos = c.data + 4;
                box.end = c.data + c.size;
                box.truncated = 0;
                while (owf_iff_next(&box, &bc)) {
                    if (bc.id == OWF_ID('W', 'F', 'S', 'C')) {
                        emit(r, bc.data, bc.size);
                        continue;
                    }
                    if (bc.id == OWF_ID('W', 'S', 'P', 'C'))
                        continue;
                    emit(r, NULL, 0);
                    if (bc.id == OWF_ID('W', 'P', 'A', 'R') && bc.size >= 26) {
                        r->align = bc.data[18];
                        r->font = bc.data[21];
                        r->style = bc.data[22];
                        r->colour = bc.data[24];
                    } else if (bc.id == OWF_ID('W', 'T', 'X', 'T')) {
                        r->text = bc.data;
                        r->text_size = bc.size;
                    }
                }
                emit(r, NULL, 0);
                if (fb.para || fb.text.length)
                    owf_builder_end_para(&fb);
                if (owf_builder_done(&fb) != OWF_OK)
                    r->body.failed = 1;
                /* A box of empty paragraphs is no text. */
                {
                    int i, any = 0;
                    for (i = 0; i < fr.story.nparas && !any; i++)
                        any = fr.story.paras[i].nruns > 0;
                    if (!any) {
                        for (i = 0; i < fr.story.nparas; i++)
                            free(fr.story.paras[i].runs);
                        free(fr.story.paras);
                        fr.story.paras = NULL;
                        fr.story.nparas = fr.story.capparas = 0;
                    }
                }
                keep_frame(r, &fr, have_place ? px : 0, have_place ? py : 0, places, nplaces, cap);
                r->cur = saved;
                r->align = align; r->font = font; r->style = style; r->colour = colour;
                have_place = 0;
            } else if (type == OWF_ID('I', 'L', 'B', 'M')) {
                r->pictures++;
            } else {
                read_drawing(r, c.data - 8, c.size + 8, depth + 1, places, nplaces, cap);
            }
            continue;
        }
        if (c.id == OWF_ID('W', 'O', 'W', 'O')) {
            have_place = object_place(&c, &place, &px, &py);
        } else if (c.id == OWF_ID('P', 'T', 'X', 'T') && c.size >= 40) {
            /* A text object: its text, one paragraph, in a frame. */
            unsigned long n = OWF_BE16(c.data + 38);
            owf_frame fr;
            long x = 0, y = 0;
            new_frame(&fr);
            object_place(&c, &fr, &x, &y);
            if (n > c.size - 40)
                n = c.size - 40;
            if (n) {
                owf_builder fb;
                owf_builder_init(&fb, r->doc, &fr.story);
                r->scratch.length = 0;
                owf_buf_put_latin1(&r->scratch, c.data + 40, n);
                if (r->scratch.failed)
                    fb.failed = 1;
                else
                    owf_builder_text(&fb, (const char *)r->scratch.data, r->scratch.length);
                owf_builder_end_para(&fb);
                if (owf_builder_done(&fb) != OWF_OK)
                    r->body.failed = 1;
                keep_frame(r, &fr, x, y, places, nplaces, cap);
            }
        } else if (c.id == OWF_ID('R', 'E', 'C', 'T') || c.id == OWF_ID('R', 'R', 'C', 'T') ||
                   c.id == OWF_ID('E', 'L', 'L', 'P') || c.id == OWF_ID('C', 'I', 'R', 'C') ||
                   c.id == OWF_ID('L', 'I', 'N', 'E')) {
            r->shapes++;
        } else if (c.id == OWF_ID('P', 'I', 'C', 'T')) {
            r->pictures++;
        }
    }
}

/* Reading order: top to bottom, then left to right, then file order. An
 * insertion sort: there are few, and owf.library has no qsort. */
static int before(const placed *p, const placed *q)
{
    if (p->y != q->y)
        return p->y < q->y;
    if (p->x != q->x)
        return p->x < q->x;
    return p->frame < q->frame;
}

static void sort_places(placed *p, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        placed t = p[i];
        for (j = i; j > 0 && before(&t, &p[j - 1]); j--)
            p[j] = p[j - 1];
        p[j] = t;
    }
}

/* A FORM GTID: its text boxes and text as frames, anchored in the body's
 * last paragraph (or a new one) in reading order. */
static void drawing(reader *r, const owf_iff_chunk *c)
{
    placed *places = NULL;
    int nplaces = 0, cap = 0, i;
    owf_builder *b = &r->body;

    read_drawing(r, c->data - 8, c->size + 8, 0, &places, &nplaces, &cap);
    if (nplaces) {
        sort_places(places, nplaces);
        for (i = 0; i < nplaces; i++) {
            owf_story *story = &r->doc->body;
            if (!b->para && !b->text.length && story->nparas) {
                if (owf_para_add_frame(&story->paras[story->nparas - 1], &b->charfmt, places[i].frame) < 0)
                    b->failed = 1;
            } else {
                owf_builder_frame(b, places[i].frame);
            }
        }
    }
    free(places);
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
            /* The drawing layer comes after the header and footer, but
             * belongs to the pages: its frames go into the body. */
            if (c.size >= 4 && OWF_BE32(c.data) == OWF_ID('G', 'T', 'I', 'D'))
                drawing(r, &c);
            else
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
    if (r->boxes)
        owf_report_add(report, OWF_NOTE_APPROX, "%d text box(es) and text object(s) of the drawing layer became frames on the page", r->boxes);
    if (r->shapes)
        owf_report_add(report, OWF_NOTE_LOST, "%d drawn shape(s) (boxes, ellipses, lines) were left out", r->shapes);
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
