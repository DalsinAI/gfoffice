/*
 * libowf: ProWrite documents (New Horizons Software), IFF FORM WORD.
 *
 * From the published specification (New Horizons, 1987): FONT, COLR, DOC,
 * HEAD, FOOT, PCTS, PARA, TABS, PAGE, TEXT, FSCC, PINF. The structures are
 * as a 68k C compiler lays them out: words on even offsets, so FONT's name
 * starts at byte 4. Measures are decipoints (1/720 inch, 2 twips).
 *
 * ProWrite 3's own files (Aminet text/misc/pwconv202, "Convert Notes")
 * show: COLR holds ISO 6429 colour numbers, 30 to 37 (1E 25 1F 24 20 23 22
 * 21: pens black, white, red, cyan, green, magenta, blue, yellow); the
 * style bits are graphics.library's (FSF_UNDERLINED 1, FSF_BOLD 2,
 * FSF_ITALIC 4); and PSET, a chunk the 1987 specification doesn't name,
 * holds the page's width and height in decipoints (longs at 2 and 6;
 * PREC repeats them). Still to confirm: what the right margin is measured
 * from, and where tab positions start.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

#define STYLE_UNDERLINED 1
#define STYLE_BOLD 2
#define STYLE_ITALIC 4

#define PAGENUM_CHAR 0x80
#define DATE_CHAR 0x81
#define TIME_CHAR 0x82

/* The line a right margin is measured on: 6.5 inches, US Letter with
 * one-inch margins, in decipoints. */
#define LINE_DECIPOINTS 4680

typedef struct {
    int font;                /* document font, or -1 */
    int size;                /* twips, or 0 */
} font_entry;

/* A header or footer section. The format allows several (for left, right
 * or both pages): each goes into a scratch document's body, and one for
 * right pages and one for left are kept (keep_furniture). */
#define MAX_FURNITURE 8
typedef struct {
    owf_doc *doc;
    owf_builder b;
    int footer, type, first;
} furniture;

typedef struct {
    owf_doc *doc;
    owf_report *report;
    font_entry fonts[256];
    unsigned char colr[8];
    int have_colr;
    owf_builder body, *cur;
    furniture furn[MAX_FURNITURE];
    int nfurn;
    owf_doc *spare;          /* sections past MAX_FURNITURE go here */
    owf_builder spare_b;
    int have_page;
    owf_buf scratch;
    /* PARA: what the next paragraphs start with */
    owf_parafmt parafmt;
    int font_num, style, misc, colour;
    /* a TEXT waiting to see whether an FSCC follows */
    const unsigned char *text;
    unsigned long text_size;
    int pictures;
} reader;

static void char_format(reader *r, owf_charfmt *fmt, int font_num, int style, int misc, int colour)
{
    owf_charfmt_init(fmt);
    fmt->font = r->fonts[font_num & 0xFF].font;
    fmt->size = r->fonts[font_num & 0xFF].size;
    if (style & STYLE_BOLD)
        fmt->flags |= OWF_BOLD;
    if (style & STYLE_ITALIC)
        fmt->flags |= OWF_ITALIC;
    if (style & STYLE_UNDERLINED)
        fmt->flags |= OWF_UNDERLINE;
    if (misc == 1)
        fmt->flags |= OWF_SUPER;
    else if (misc == 2)
        fmt->flags |= OWF_SUB;
    if (r->have_colr) {
        /* ISO 6429 colour numbers, 30-37; 0-7 taken as the same. */
        unsigned v = r->colr[colour & 7];
        unsigned iso = v >= 30 && v <= 37 ? v - 30 : v & 7;
        /* Black is the default colour, not a choice. */
        fmt->colour = iso == 0 ? OWF_COLOUR_AUTO : owf_iso_colours[iso];
    }
}

static void set_char_format(owf_builder *b, const owf_charfmt *fmt)
{
    if (memcmp(fmt, &b->charfmt, sizeof *fmt) != 0) {
        owf_builder_flush(b);
        b->charfmt = *fmt;
    }
}

/* One paragraph: the pending TEXT, with an FSCC's changes if one came. */
static void emit_text(reader *r, const unsigned char *fscc, unsigned long fscc_size)
{
    owf_builder *b = r->cur;
    owf_charfmt fmt;
    unsigned long i, start = 0, nchanges = fscc ? fscc_size / 8 : 0, next = 0;

    if (!r->text)
        return;
    b->parafmt.align = r->parafmt.align;
    b->parafmt.indent_left = r->parafmt.indent_left;
    b->parafmt.indent_right = r->parafmt.indent_right;
    b->parafmt.indent_first = r->parafmt.indent_first;
    b->parafmt.line_spacing = r->parafmt.line_spacing;
    b->parafmt.ntabs = r->parafmt.ntabs;
    memcpy(b->parafmt.tabs, r->parafmt.tabs, sizeof b->parafmt.tabs);
    char_format(r, &fmt, r->font_num, r->style, r->misc, r->colour);
    set_char_format(b, &fmt);

    for (i = 0; i <= r->text_size; i++) {
        unsigned c;
        /* Formatting changes that start here. */
        while (next < nchanges && OWF_BE16(fscc + next * 8) <= i) {
            const unsigned char *ch = fscc + next * 8;
            if (i > start) {
                r->scratch.length = 0;
                owf_buf_put_latin1(&r->scratch, r->text + start, i - start);
                owf_builder_text(b, (const char *)r->scratch.data, r->scratch.length);
                start = i;
            }
            char_format(r, &fmt, ch[2], ch[3], ch[4], ch[5]);
            set_char_format(b, &fmt);
            next++;
        }
        if (i == r->text_size)
            break;
        c = r->text[i];
        if (c >= 0x20 && !(c >= 0x7F && c < 0xA0))
            continue;
        if (i > start) {
            r->scratch.length = 0;
            owf_buf_put_latin1(&r->scratch, r->text + start, i - start);
            owf_builder_text(b, (const char *)r->scratch.data, r->scratch.length);
        }
        start = i + 1;
        if (c == '\t')
            owf_builder_special(b, OWF_RUN_TAB, OWF_FIELD_PAGE);
        else if (c == PAGENUM_CHAR)
            owf_builder_special(b, OWF_RUN_FIELD, OWF_FIELD_PAGE);
        else if (c == DATE_CHAR)
            owf_builder_special(b, OWF_RUN_FIELD, OWF_FIELD_DATE);
        else if (c == TIME_CHAR)
            owf_builder_special(b, OWF_RUN_FIELD, OWF_FIELD_TIME);
    }
    if (r->text_size > start) {
        r->scratch.length = 0;
        owf_buf_put_latin1(&r->scratch, r->text + start, r->text_size - start);
        owf_builder_text(b, (const char *)r->scratch.data, r->scratch.length);
    }
    if (r->scratch.failed)
        b->failed = 1;
    owf_builder_end_para(b);
    r->text = NULL;
}

static void read_font(reader *r, const owf_iff_chunk *c)
{
    char name[64];
    unsigned long n = 0;
    int font;
    owf_font_kind kind = OWF_FONT_ANY;

    if (c->size < 5)
        return;
    while (4 + n < c->size && c->data[4 + n] && n < sizeof name - 1) {
        name[n] = (char)c->data[4 + n];
        n++;
    }
    name[n] = 0;
    if (!n)
        return;
    owf_font_modern(name, &kind);
    font = owf_doc_font(r->doc, name, kind);
    if (font < 0) {
        r->body.failed = 1;
        return;
    }
    r->fonts[c->data[0]].font = font;
    r->fonts[c->data[0]].size = (int)OWF_BE16(c->data + 2) * OWF_TWIPS_PER_POINT;
}

static void read_para(reader *r, const owf_iff_chunk *c)
{
    int left_indent, left_margin, right_margin;

    if (c->size < 12)
        return;
    left_indent = (int)OWF_BE16(c->data);
    left_margin = (int)OWF_BE16(c->data + 2);
    right_margin = (int)OWF_BE16(c->data + 4);
    r->parafmt.indent_left = left_margin * 2;
    r->parafmt.indent_first = (left_indent - left_margin) * 2;
    r->parafmt.indent_right = 0;
    if (right_margin > left_margin && right_margin < LINE_DECIPOINTS) {
        r->parafmt.indent_right = (LINE_DECIPOINTS - right_margin) * 2;
        owf_report_add(r->report, OWF_NOTE_APPROX, "Right margins are approximate");
    }
    r->parafmt.line_spacing = c->data[6] == 0x10 ? 200 : 100;
    switch (c->data[7]) {
    case 1: r->parafmt.align = OWF_ALIGN_CENTRE; break;
    case 2: r->parafmt.align = OWF_ALIGN_RIGHT; break;
    case 3: r->parafmt.align = OWF_ALIGN_JUSTIFY; break;
    default: r->parafmt.align = OWF_ALIGN_LEFT; break;
    }
    r->font_num = c->data[8];
    r->style = c->data[9];
    r->misc = c->data[10];
    r->colour = c->data[11];
}

static void read_tabs(reader *r, const owf_iff_chunk *c)
{
    unsigned long i;
    r->parafmt.ntabs = 0;
    for (i = 0; i + 4 <= c->size && r->parafmt.ntabs < OWF_MAX_TABS; i += 4) {
        owf_tab *tab = &r->parafmt.tabs[r->parafmt.ntabs++];
        int pos = (int)OWF_BE16(c->data + i) * 2 - r->parafmt.indent_left;
        tab->position = pos < 0 ? 0 : pos;
        tab->leader = OWF_LEADER_NONE;
        switch (c->data[i + 2]) {
        case 1: tab->kind = OWF_TAB_CENTRE; break;
        case 2: tab->kind = OWF_TAB_RIGHT; break;
        case 3: tab->kind = OWF_TAB_DECIMAL; break;
        default: tab->kind = OWF_TAB_LEFT; break;
        }
    }
}

/* The headers (footer 0) or footers (1) kept, 2.0.23: one for right pages, the odd-numbered (a section for
 * right pages, else one for both), and one for left pages (for left pages, else for both) into header_left;
 * none for "no pages". When the two are the same section, header_left stays empty (every page has it); when
 * only one side has one, the other side's story is an empty paragraph (nothing on those pages). The first of
 * each kind is kept; the others are left out and reported. */
static int keep_furniture(reader *r, int footer)
{
    owf_story *right = footer ? &r->doc->footer : &r->doc->header;
    owf_story *left = footer ? &r->doc->footer_left : &r->doc->header_left;
    const char *what = footer ? "footer" : "header";
    int i, of[4] = { -1, -1, -1, -1 }, others = 0, result = OWF_OK, rs, ls, first_page_left;

    for (i = 0; i < r->nfurn; i++) {
        furniture *f = &r->furn[i];
        if (f->footer != footer)
            continue;
        if (owf_builder_done(&f->b) != OWF_OK)
            result = OWF_ERR_MEMORY;
        if (!f->type)
            continue;
        if (of[f->type] < 0)
            of[f->type] = i;
        else
            others++;
    }
    rs = of[2] >= 0 ? of[2] : of[3];                /* none, left, right, both */
    ls = of[1] >= 0 ? of[1] : of[3];
    if (of[1] >= 0 && of[2] >= 0 && of[3] >= 0)
        others++;                                    /* both sides have their own: "both" is left out */
    if (rs < 0 && ls < 0)
        return result;
    if (rs >= 0) {
        *right = r->furn[rs].doc->body;
        memset(&r->furn[rs].doc->body, 0, sizeof r->furn[rs].doc->body);
    } else if (!owf_story_add(right, NULL))
        result = OWF_ERR_MEMORY;
    if (ls >= 0 && ls != rs) {
        *left = r->furn[ls].doc->body;
        memset(&r->furn[ls].doc->body, 0, sizeof r->furn[ls].doc->body);
    } else if (ls < 0 && !owf_story_add(left, NULL))
        result = OWF_ERR_MEMORY;
    /* the first page's own section says whether it has one there */
    first_page_left = !(r->doc->page.start_page & 1);
    i = first_page_left ? ls : rs;
    if (i >= 0) {
        if (footer)
            r->doc->page.footer_on_first = r->furn[i].first;
        else
            r->doc->page.header_on_first = r->furn[i].first;
    }
    if (others)
        owf_report_add(r->report, OWF_NOTE_LOST, "%d more %s(s) for the same pages were left out: a document here has one %s for left pages and one for right",
                       others, what, what);
    return result;
}

static int detect_prowrite(const unsigned char *data, size_t length)
{
    return owf_iff_is_form(data, length, OWF_ID('W', 'O', 'R', 'D')) ? 100 : 0;
}

static int import_prowrite(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    reader *r;               /* 2 KB of font table: not on a 4 KB stack */
    owf_iff_reader iff;
    owf_iff_chunk c;
    int i, result;

    if (!owf_iff_open(&iff, data, length, OWF_ID('W', 'O', 'R', 'D')))
        return OWF_ERR_FORMAT;
    r = calloc(1, sizeof *r);
    if (!r)
        return OWF_ERR_MEMORY;
    r->doc = doc;
    r->report = report;
    for (i = 0; i < 256; i++)
        r->fonts[i].font = -1;
    owf_builder_init(&r->body, doc, &doc->body);
    r->spare = owf_doc_new();
    if (!r->spare) {
        free(r);
        return OWF_ERR_MEMORY;
    }
    owf_builder_init(&r->spare_b, r->spare, &r->spare->body);
    owf_buf_init(&r->scratch);
    owf_parafmt_init(&r->parafmt);
    r->cur = &r->body;

    while (owf_iff_next(&iff, &c)) {
        if (c.id == OWF_ID('F', 'S', 'C', 'C')) {
            emit_text(r, c.data, c.size);
            continue;
        }
        emit_text(r, NULL, 0);
        switch (c.id) {
        case OWF_ID('F', 'O', 'N', 'T'):
            read_font(r, &c);
            break;
        case OWF_ID('C', 'O', 'L', 'R'):
            if (c.size >= 8) {
                memcpy(r->colr, c.data, 8);
                r->have_colr = 1;
            }
            break;
        case OWF_ID('D', 'O', 'C', ' '):
            r->cur = &r->body;
            if (c.size >= 3) {
                doc->page.start_page = (int)OWF_BE16(c.data);
                doc->page.pagenum_style = c.data[2] <= 4 ? (owf_pagenum_style)c.data[2] : OWF_PAGENUM_ARABIC;
            }
            break;
        case OWF_ID('H', 'E', 'A', 'D'):
        case OWF_ID('F', 'O', 'O', 'T'):
            r->cur = &r->spare_b;
            if (r->nfurn < MAX_FURNITURE) {
                furniture *f = &r->furn[r->nfurn];
                f->doc = owf_doc_new();
                if (!f->doc) {
                    r->body.failed = 1;
                    break;
                }
                r->nfurn++;
                owf_builder_init(&f->b, f->doc, &f->doc->body);
                f->footer = c.id == OWF_ID('F', 'O', 'O', 'T');
                f->type = c.size >= 1 ? c.data[0] & 3 : 3;
                f->first = c.size >= 2 ? c.data[1] != 0 : 1;
                r->cur = &f->b;
            }
            break;
        case OWF_ID('P', 'S', 'E', 'T'):
        case OWF_ID('P', 'R', 'E', 'C'):
            /* ProWrite 3: the page's size in decipoints (PSET's longs at 2
             * and 6; PREC's words at 22 and 24), PSET first. */
            if (!r->have_page || c.id == OWF_ID('P', 'S', 'E', 'T')) {
                unsigned long w = 0, h = 0;
                if (c.id == OWF_ID('P', 'S', 'E', 'T') && c.size >= 10) {
                    w = OWF_BE32(c.data + 2);
                    h = OWF_BE32(c.data + 6);
                } else if (c.id == OWF_ID('P', 'R', 'E', 'C') && c.size >= 26) {
                    w = OWF_BE16(c.data + 22);
                    h = OWF_BE16(c.data + 24);
                }
                if (w >= 720 && w <= 20000 && h >= 720 && h <= 20000) {
                    doc->page.width = (int)w * 2;
                    doc->page.height = (int)h * 2;
                    r->have_page = 1;
                }
            }
            break;
        case OWF_ID('P', 'C', 'T', 'S'):
            r->cur = &r->body;
            break;
        case OWF_ID('P', 'I', 'N', 'F'):
            r->pictures++;
            break;
        case OWF_ID('P', 'A', 'R', 'A'):
            read_para(r, &c);
            break;
        case OWF_ID('T', 'A', 'B', 'S'):
            read_tabs(r, &c);
            break;
        case OWF_ID('P', 'A', 'G', 'E'):
            if (r->cur->para)
                owf_builder_end_para(r->cur);
            /* Two breaks in a row: an empty page between them. */
            else if (r->cur->parafmt.page_break_before)
                owf_builder_end_para(r->cur);
            r->cur->parafmt.page_break_before = 1;
            break;
        case OWF_ID('T', 'E', 'X', 'T'):
            r->text = c.data;
            r->text_size = c.size;
            break;
        default:
            break;
        }
    }
    emit_text(r, NULL, 0);

    if (r->pictures)
        owf_report_add(report, OWF_NOTE_LOST, "%d picture(s) are not brought across yet", r->pictures);
    if (iff.truncated)
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the text up to the cut was read");
    if (!r->have_page)
        owf_report_add(report, OWF_NOTE_INFO, "This ProWrite file does not store the paper size; the default was used");
    owf_buf_free(&r->scratch);
    result = owf_builder_done(&r->body);
    if (keep_furniture(r, 0) != OWF_OK || keep_furniture(r, 1) != OWF_OK)
        result = OWF_ERR_MEMORY;
    owf_builder_done(&r->spare_b);
    owf_doc_free(r->spare);
    for (i = 0; i < r->nfurn; i++)
        owf_doc_free(r->furn[i].doc);
    free(r);
    return result;
}

const owf_format owf_format_prowrite = {
    "prowrite", "ProWrite (IFF WORD)", "pw prowrite", detect_prowrite, import_prowrite, NULL
};
