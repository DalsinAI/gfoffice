/*
 * libowf: the Amiga's formatted text. IFF FTXT (EA IFF 85: CHRS text and
 * FONS fonts; the clipboard's format) and ANSI text (the console's codes)
 * share one reader of ISO 6429 style codes: CSI n m.
 *
 * As the FTXT specification has it: all of a FORM's CHRS chunks are one
 * stream, and text starts in font 1; ESC sequences (ESC, intermediates
 * 20-2F, a final 30-7E) and DCS, OSC, PM and APC strings (to ST) are
 * skipped whole; a CSI is CSI, parameters 30-3F, intermediates 20-2F, a
 * final 40-7E, and one cut short by any other byte is dropped without that
 * byte; a LIST FTXT (its PROP FTXT's FONS for every FORM) and a CAT of
 * FTXT are read FORM by FORM. Kept beyond the specification, for the files
 * programs write: ESC [ as CSI, tab, form feed (a page break) and vertical
 * tab (a line break).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <string.h>

#define CSI 0x9B
#define ESC 0x1B
#define ST 0x9C

typedef struct {
    owf_builder *b;
    owf_report *report;
    int fonts[10];            /* FONS id -> document font, -1 if none */
    int font_chosen;          /* an SGR 10-19 came: font 1 is no longer the default */
    int utf8;                 /* the text is UTF-8: no one-byte CSI */
    int unshown;              /* SGR renditions shown as normal text */
    owf_buf latin1;           /* scratch for ISO-8859-1 -> UTF-8 */
} sgr_reader;

static void set_flag(owf_builder *b, unsigned flag, int on)
{
    unsigned flags = on ? (b->charfmt.flags | flag) : (b->charfmt.flags & ~flag);
    if (flags != b->charfmt.flags) {
        owf_builder_flush(b);
        b->charfmt.flags = flags;
    }
}

static void set_colour(owf_builder *b, unsigned long colour)
{
    if (colour != b->charfmt.colour) {
        owf_builder_flush(b);
        b->charfmt.colour = colour;
    }
}

static void select_graphic_rendition(sgr_reader *r, const int *params, int count)
{
    owf_builder *b = r->b;
    int i;

    if (count == 0) {
        static const int zero = 0;
        params = &zero;
        count = 1;
    }
    for (i = 0; i < count; i++) {
        int p = params[i];
        if (p == 0) {
            set_flag(b, OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE | OWF_STRIKE, 0);
            set_colour(b, OWF_COLOUR_AUTO);
        } else if (p == 1) {
            set_flag(b, OWF_BOLD, 1);
        } else if (p == 3) {
            set_flag(b, OWF_ITALIC, 1);
        } else if (p == 4) {
            set_flag(b, OWF_UNDERLINE, 1);
        } else if (p == 9) {
            set_flag(b, OWF_STRIKE, 1);
        } else if (p == 22) {
            set_flag(b, OWF_BOLD, 0);
        } else if (p == 23) {
            set_flag(b, OWF_ITALIC, 0);
        } else if (p == 24) {
            set_flag(b, OWF_UNDERLINE, 0);
        } else if (p == 29) {
            set_flag(b, OWF_STRIKE, 0);
        } else if (p >= 10 && p <= 19) {
            int font = r->fonts[p - 10];
            r->font_chosen = 1;
            if (font != b->charfmt.font) {
                owf_builder_flush(b);
                b->charfmt.font = font;
            }
        } else if (p >= 30 && p <= 37) {
            /* Black text is the default colour, not a choice. */
            set_colour(b, p == 30 ? OWF_COLOUR_AUTO : owf_iso_colours[p - 30]);
        } else if (p == 39) {
            set_colour(b, OWF_COLOUR_AUTO);
        } else if (p >= 40 && p <= 49) {
            owf_report_add(r->report, OWF_NOTE_LOST, "Background colours were left out");
        } else if (p == 7) {
            owf_report_add(r->report, OWF_NOTE_LOST, "Inverse text was shown as normal text");
        } else if (p == 21) {
            /* Doubly underlined: underlined. */
            set_flag(b, OWF_UNDERLINE, 1);
            r->unshown |= 1;
        } else if (p == 2 || p == 5 || p == 6) {
            r->unshown |= 2;     /* faint, blinking */
        }
    }
}

static void put_text(sgr_reader *r, const unsigned char *text, size_t length)
{
    if (!length)
        return;
    if (r->utf8) {
        owf_builder_text(r->b, (const char *)text, length);
        return;
    }
    r->latin1.length = 0;
    owf_buf_put_latin1(&r->latin1, text, length);
    if (r->latin1.failed)
        r->b->failed = 1;
    else
        owf_builder_text(r->b, (const char *)r->latin1.data, r->latin1.length);
}

/* A DCS, OSC, PM or APC string's end: past ST (or ESC \\, or BEL, which
 * terminals take for OSC), or the text's end. */
static size_t skip_string(const sgr_reader *r, const unsigned char *text, size_t length, size_t i)
{
    while (i < length) {
        if ((text[i] == ST && !r->utf8) || text[i] == 0x07)
            return i + 1;
        if (text[i] == ESC && i + 1 < length && text[i + 1] == '\\')
            return i + 2;
        i++;
    }
    return length;
}

/* Text with ISO 6429 codes; ends paragraphs at line feeds. */
static void read_styled_text(sgr_reader *r, const unsigned char *text, size_t length)
{
    size_t i = 0, start = 0;

    while (i < length) {
        unsigned c = text[i];
        if (c >= 0x20 && c != 0x7F && (r->utf8 || !(c >= 0x80 && c < 0xA0))) {
            i++;
            continue;
        }
        put_text(r, text + start, i - start);
        if ((c == CSI && !r->utf8) || (c == ESC && i + 1 < length && text[i + 1] == '[')) {
            /* CSI, parameters 30-3F, intermediates 20-2F, a final 40-7E. */
            int params[16], count = 0, value = -1, private_ = 0;
            i += (c == CSI) ? 1 : 2;
            while (i < length && text[i] >= 0x30 && text[i] <= 0x3F) {
                if (text[i] == ';') {
                    if (count < 16)
                        params[count++] = value < 0 ? 0 : value;
                    value = -1;
                } else if (text[i] >= '0' && text[i] <= '9') {
                    value = (value < 0 ? 0 : value) * 10 + (text[i] - '0');
                    if (value > 9999)
                        value = 9999;
                } else {
                    private_ = 1;    /* : < = > ? */
                }
                i++;
            }
            if (value >= 0 && count < 16)
                params[count++] = value;
            while (i < length && text[i] >= 0x20 && text[i] <= 0x2F)
                i++;
            /* Cut short by anything else: dropped, and that byte is read
             * as itself. */
            if (i < length && text[i] >= 0x40 && text[i] <= 0x7E) {
                if (text[i] == 'm' && !private_)
                    select_graphic_rendition(r, params, count);
                i++;
            }
        } else if (c == ESC) {
            /* ESC P, ], ^ and _ open strings as DCS, OSC, PM and APC do;
             * other ESC sequences are ESC, intermediates 20-2F, a final. */
            i++;
            if (i < length && (text[i] == 'P' || text[i] == ']' || text[i] == '^' || text[i] == '_')) {
                i = skip_string(r, text, length, i + 1);
            } else {
                while (i < length && text[i] >= 0x20 && text[i] <= 0x2F)
                    i++;
                if (i < length && text[i] >= 0x30 && text[i] <= 0x7E)
                    i++;
            }
        } else if (!r->utf8 && (c == 0x90 || c == 0x9D || c == 0x9E || c == 0x9F)) {
            i = skip_string(r, text, length, i + 1);
        } else {
            if (c == '\n') {
                owf_builder_end_para(r->b);
            } else if (c == '\f') {
                /* A page break after a line feed starts no empty paragraph. */
                if (r->b->para || r->b->text.length)
                    owf_builder_end_para(r->b);
                r->b->parafmt.page_break_before = 1;
            } else if (c == '\t') {
                owf_builder_special(r->b, OWF_RUN_TAB, OWF_FIELD_PAGE);
            } else if (c == 0x0B) {
                owf_builder_special(r->b, OWF_RUN_LINEBREAK, OWF_FIELD_PAGE);
            }
            i++;
        }
        if (i > length)
            i = length;
        start = i;
    }
    put_text(r, text + start, length - start);
}

static void reader_init(sgr_reader *r, owf_builder *b, owf_report *report)
{
    int i;
    r->b = b;
    r->report = report;
    for (i = 0; i < 10; i++)
        r->fonts[i] = -1;
    r->font_chosen = 0;
    r->utf8 = 0;
    r->unshown = 0;
    owf_buf_init(&r->latin1);
}

/* ---- IFF FTXT ---- */

/* The first FORM FTXT in a LIST or CAT (or its own), to tell FTXT. */
static int holds_ftxt(const unsigned char *data, size_t length, int depth)
{
    size_t pos = 12;
    unsigned long id;

    if (length < 12 || depth > 4)
        return 0;
    id = OWF_BE32(data);
    if (id == OWF_ID('F', 'O', 'R', 'M'))
        return OWF_BE32(data + 8) == OWF_ID('F', 'T', 'X', 'T');
    if (id != OWF_ID('L', 'I', 'S', 'T') && id != OWF_ID('C', 'A', 'T', ' '))
        return 0;
    if (OWF_BE32(data + 8) == OWF_ID('F', 'T', 'X', 'T'))
        return 1;
    while (pos + 12 <= length) {
        unsigned long size = OWF_BE32(data + pos + 4);
        unsigned long cid = OWF_BE32(data + pos);
        size_t sub = size > length - pos - 8 ? length - pos - 8 : (size_t)size;
        if (cid != OWF_ID('P', 'R', 'O', 'P') && holds_ftxt(data + pos, sub + 8, depth + 1))
            return 1;
        pos += 8 + (size_t)sub + (sub & 1);
    }
    return 0;
}

static int detect_ftxt(const unsigned char *data, size_t length)
{
    if (owf_iff_is_form(data, length, OWF_ID('F', 'T', 'X', 'T')))
        return 100;
    return holds_ftxt(data, length, 0) ? 90 : 0;
}

/* One FONS: id, pad, proportional (0 unknown, 1 no, 2 yes), serif (the
 * same), then the font's name. Into fonts[id] (-1 when it can't). */
static int read_fons(owf_doc *doc, const owf_iff_chunk *chunk, int fonts[10])
{
    unsigned id;
    char name[64];
    size_t n = 0;
    owf_font_kind kind = OWF_FONT_ANY;

    if (chunk->size < 5)
        return 0;
    id = chunk->data[0];
    while (4 + n < chunk->size && chunk->data[4 + n] && n < sizeof name - 1) {
        name[n] = (char)chunk->data[4 + n];
        n++;
    }
    name[n] = 0;
    if (chunk->data[2] == 1)
        kind = OWF_FONT_MONO;
    else if (chunk->data[3] == 2)
        kind = OWF_FONT_SERIF;
    else if (chunk->data[3] == 1)
        kind = OWF_FONT_SANS;
    if (id > 9 || !n)
        return 0;
    fonts[id] = owf_doc_font(doc, name, kind);
    return fonts[id] < 0 ? -1 : 1;
}

typedef struct {
    owf_doc *doc;
    owf_builder *b;
    owf_report *report;
    int any, forms, truncated, failed;
} ftxt_state;

/* A FORM FTXT's chunks: one stream of text from all its CHRS, starting in
 * font 1, with the fonts an enclosing LIST's PROP FTXT gave (inherit). */
static void read_form(ftxt_state *st, const unsigned char *data, size_t length, const int inherit[10])
{
    owf_iff_reader iff;
    owf_iff_chunk chunk;
    sgr_reader r;
    owf_builder *b = st->b;

    if (!owf_iff_open(&iff, data, length, OWF_ID('F', 'T', 'X', 'T')))
        return;
    reader_init(&r, b, st->report);
    memcpy(r.fonts, inherit, sizeof r.fonts);
    /* Formatting doesn't carry from one FORM to the next. */
    owf_builder_flush(b);
    owf_charfmt_init(&b->charfmt);
    b->charfmt.font = r.fonts[1];
    while (owf_iff_next(&iff, &chunk)) {
        if (chunk.id == OWF_ID('F', 'O', 'N', 'S')) {
            if (read_fons(st->doc, &chunk, r.fonts) < 0)
                st->failed = 1;
            /* Text starts in font 1, whenever its FONS comes. */
            if (chunk.size && chunk.data[0] == 1 && !r.font_chosen && r.fonts[1] != b->charfmt.font) {
                owf_builder_flush(b);
                b->charfmt.font = r.fonts[1];
            }
        } else if (chunk.id == OWF_ID('C', 'H', 'R', 'S')) {
            /* All of a FORM's CHRS are one stream: a paragraph may go on
             * from one to the next. */
            read_styled_text(&r, chunk.data, chunk.size);
            st->any = 1;
        }
    }
    /* The FORM's last paragraph ends with it. */
    if (b->para || b->text.length)
        owf_builder_end_para(b);
    if (iff.truncated)
        st->truncated = 1;
    if (r.unshown & 1)
        owf_report_add(st->report, OWF_NOTE_APPROX, "Doubly underlined text was underlined once");
    if (r.unshown & 2)
        owf_report_add(st->report, OWF_NOTE_LOST, "Faint and blinking text were shown as normal text");
    owf_buf_free(&r.latin1);
    st->forms++;
}

/* A LIST or CAT: its FORM FTXT in order, nested LISTs and CATs too; a
 * LIST's PROP FTXT gives its FORMs their fonts. Other FORMs are skipped. */
static void read_group(ftxt_state *st, const unsigned char *data, size_t length, const int inherit[10], int depth)
{
    int fonts[10];
    size_t pos = 12, end;
    unsigned long size;
    int is_list = OWF_BE32(data) == OWF_ID('L', 'I', 'S', 'T');

    if (length < 12 || depth > 8)
        return;
    memcpy(fonts, inherit, sizeof fonts);
    size = OWF_BE32(data + 4);
    end = size > length - 8 ? length : (size_t)size + 8;
    if (size > length - 8)
        st->truncated = 1;
    while (pos + 12 <= end) {
        unsigned long cid = OWF_BE32(data + pos), csize = OWF_BE32(data + pos + 4);
        size_t sub = csize > end - pos - 8 ? end - pos - 8 : (size_t)csize;
        if (csize > end - pos - 8)
            st->truncated = 1;
        if (cid == OWF_ID('P', 'R', 'O', 'P') && is_list && OWF_BE32(data + pos + 8) == OWF_ID('F', 'T', 'X', 'T')) {
            owf_iff_reader iff;
            owf_iff_chunk chunk;
            iff.pos = data + pos + 12;
            iff.end = data + pos + 8 + sub;
            iff.truncated = 0;
            while (owf_iff_next(&iff, &chunk))
                if (chunk.id == OWF_ID('F', 'O', 'N', 'S') && read_fons(st->doc, &chunk, fonts) < 0)
                    st->failed = 1;
        } else if (cid == OWF_ID('F', 'O', 'R', 'M')) {
            if (OWF_BE32(data + pos + 8) == OWF_ID('F', 'T', 'X', 'T'))
                read_form(st, data + pos, sub + 8, fonts);
            else
                owf_report_add(st->report, OWF_NOTE_LOST, "A picture or other IFF form beside the text was left out");
        } else if (cid == OWF_ID('L', 'I', 'S', 'T') || cid == OWF_ID('C', 'A', 'T', ' ')) {
            read_group(st, data + pos, sub + 8, fonts, depth + 1);
        }
        pos += 8 + sub + (sub & 1);
    }
}

static int import_ftxt(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_builder b;
    ftxt_state st;
    int none[10], i;

    if (!detect_ftxt(data, length))
        return OWF_ERR_FORMAT;
    for (i = 0; i < 10; i++)
        none[i] = -1;
    owf_builder_init(&b, doc, &doc->body);
    memset(&st, 0, sizeof st);
    st.doc = doc;
    st.b = &b;
    st.report = report;
    if (OWF_BE32(data) == OWF_ID('F', 'O', 'R', 'M'))
        read_form(&st, data, length, none);
    else
        read_group(&st, data, length, none, 0);
    if (st.failed)
        b.failed = 1;
    if (st.truncated)
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the text up to the cut was read");
    if (st.forms > 1)
        owf_report_add(report, OWF_NOTE_INFO, "%d FTXT texts were read one after another", st.forms);
    if (!st.any)
        owf_report_add(report, OWF_NOTE_INFO, "The FTXT file holds no text");
    return owf_builder_done(&b);
}

static void put_sgr(owf_buf *out, unsigned *on, unsigned want)
{
    if (*on == want)
        return;
    if ((*on & ~want) != 0) {
        owf_buf_puts(out, "\x9b" "0m");
        *on = 0;
    }
    if ((want & OWF_BOLD) && !(*on & OWF_BOLD))
        owf_buf_puts(out, "\x9b" "1m");
    if ((want & OWF_ITALIC) && !(*on & OWF_ITALIC))
        owf_buf_puts(out, "\x9b" "3m");
    if ((want & OWF_UNDERLINE) && !(*on & OWF_UNDERLINE))
        owf_buf_puts(out, "\x9b" "4m");
    *on = want;
}

/* FTXT out: one CHRS with the text and its bold, italic and underline, in
 * ISO-8859-1. Enough for the clipboard and for programs that read FTXT. */
static int export_ftxt(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    owf_buf chrs, out;
    int i, j;
    unsigned on = 0;
    unsigned long size;

    owf_buf_init(&chrs);
    for (i = 0; i < doc->body.nparas; i++) {
        const owf_para *para = &doc->body.paras[i];
        if (para->fmt.page_break_before && i)
            owf_buf_putc(&chrs, '\f');
        for (j = 0; j < para->nruns; j++) {
            const owf_run *run = &para->runs[j];
            put_sgr(&chrs, &on, run->fmt.flags & (OWF_BOLD | OWF_ITALIC | OWF_UNDERLINE));
            if (run->kind == OWF_RUN_TEXT) {
                const unsigned char *p = (const unsigned char *)run->text;
                const unsigned char *end = p + strlen(run->text);
                while (p < end) {
                    unsigned long c = owf_utf8_next(&p, end);
                    if (c > 0xFF || (c >= 0x80 && c < 0xA0)) {
                        owf_report_add(report, OWF_NOTE_APPROX, "Characters the Amiga's character set lacks became ?");
                        c = '?';
                    }
                    owf_buf_putc(&chrs, (int)c);
                }
            } else if (run->kind == OWF_RUN_TAB) {
                owf_buf_putc(&chrs, '\t');
            } else if (run->kind == OWF_RUN_LINEBREAK) {
                owf_buf_putc(&chrs, '\n');
            }
        }
        put_sgr(&chrs, &on, 0);
        owf_buf_putc(&chrs, '\n');
    }
    owf_report_add(report, OWF_NOTE_LOST, "FTXT keeps only bold, italic and underline");
    if (chrs.failed) {
        owf_buf_free(&chrs);
        return OWF_ERR_MEMORY;
    }
    owf_buf_init(&out);
    size = 4 + 8 + (unsigned long)chrs.length + (chrs.length & 1);
    owf_buf_puts(&out, "FORM");
    owf_buf_putc(&out, (int)(size >> 24) & 0xFF);
    owf_buf_putc(&out, (int)(size >> 16) & 0xFF);
    owf_buf_putc(&out, (int)(size >> 8) & 0xFF);
    owf_buf_putc(&out, (int)size & 0xFF);
    owf_buf_puts(&out, "FTXTCHRS");
    size = (unsigned long)chrs.length;
    owf_buf_putc(&out, (int)(size >> 24) & 0xFF);
    owf_buf_putc(&out, (int)(size >> 16) & 0xFF);
    owf_buf_putc(&out, (int)(size >> 8) & 0xFF);
    owf_buf_putc(&out, (int)size & 0xFF);
    owf_buf_put(&out, chrs.data, chrs.length);
    if (chrs.length & 1)
        owf_buf_putc(&out, 0);
    owf_buf_free(&chrs);
    return owf_buf_take(&out, data, length);
}

const owf_format owf_format_ftxt = {
    "ftxt", "IFF FTXT (Amiga formatted text, the clipboard)", "ftxt iff", detect_ftxt, import_ftxt, export_ftxt
};

/* ---- ANSI text ---- */

static int detect_ansi(const unsigned char *data, size_t length)
{
    size_t i, n = length < 4096 ? length : 4096;
    int codes = 0;

    for (i = 0; i + 2 < n; i++) {
        if (data[i] == 0)
            return 0;
        if ((data[i] == ESC && data[i + 1] == '[') || data[i] == CSI) {
            size_t j = i + (data[i] == CSI ? 1 : 2);
            while (j < n && ((data[j] >= '0' && data[j] <= '9') || data[j] == ';'))
                j++;
            if (j < n && data[j] == 'm')
                codes++;
        }
    }
    return codes ? 40 : 0;
}

static int import_ansi(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_builder b;
    sgr_reader r;
    size_t i;
    owf_buf clean;
    int result;

    /* Carriage returns before line feeds are not paragraph ends of their own. */
    owf_buf_init(&clean);
    for (i = 0; i < length; i++)
        if (!(data[i] == '\r' && i + 1 < length && data[i + 1] == '\n'))
            owf_buf_putc(&clean, data[i] == '\r' ? '\n' : data[i]);
    if (clean.failed) {
        owf_buf_free(&clean);
        return OWF_ERR_MEMORY;
    }
    owf_builder_init(&b, doc, &doc->body);
    reader_init(&r, &b, report);
    /* Amiga ANSI files are ISO-8859-1; ones from today's systems are UTF-8. */
    for (i = 0; i < clean.length; i++)
        if (clean.data[i] >= 0x80) {
            r.utf8 = owf_utf8_valid(clean.data, clean.length);
            break;
        }
    read_styled_text(&r, clean.data ? clean.data : (const unsigned char *)"", clean.length);
    if (b.para || b.text.length)
        owf_builder_end_para(&b);
    owf_buf_free(&r.latin1);
    owf_buf_free(&clean);
    result = owf_builder_done(&b);
    return result;
}

const owf_format owf_format_ansi = {
    "ansi", "ANSI text (Amiga console styles and colours)", "ans ansi", detect_ansi, import_ansi, NULL
};
