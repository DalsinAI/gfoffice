/*
 * libowf: the Amiga's formatted text. IFF FTXT (EA IFF 85: CHRS text and
 * FONS fonts; the clipboard's format) and ANSI text (the console's codes)
 * share one reader of ISO 6429 style codes: CSI n m.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <string.h>

#define CSI 0x9B
#define ESC 0x1B

typedef struct {
    owf_builder *b;
    owf_report *report;
    int fonts[10];            /* FONS id -> document font, -1 if none */
    int utf8;                 /* the text is UTF-8: no one-byte CSI */
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
            int params[16], count = 0, value = -1;
            i += (c == CSI) ? 1 : 2;
            /* Parameters, then intermediates, then one final byte. */
            while (i < length && ((text[i] >= '0' && text[i] <= '9') || text[i] == ';')) {
                if (text[i] == ';') {
                    if (count < 16)
                        params[count++] = value < 0 ? 0 : value;
                    value = -1;
                } else {
                    value = (value < 0 ? 0 : value) * 10 + (text[i] - '0');
                    if (value > 9999)
                        value = 9999;
                }
                i++;
            }
            if (value >= 0 && count < 16)
                params[count++] = value;
            while (i < length && text[i] >= 0x20 && text[i] <= 0x2F)
                i++;
            if (i < length) {
                if (text[i] == 'm')
                    select_graphic_rendition(r, params, count);
                i++;
            }
        } else if (c == ESC) {
            i += 2;     /* ESC and one more byte: not a style */
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
    r->utf8 = 0;
    owf_buf_init(&r->latin1);
}

/* ---- IFF FTXT ---- */

static int detect_ftxt(const unsigned char *data, size_t length)
{
    return owf_iff_is_form(data, length, OWF_ID('F', 'T', 'X', 'T')) ? 100 : 0;
}

static int import_ftxt(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_iff_reader iff;
    owf_iff_chunk chunk;
    owf_builder b;
    sgr_reader r;
    int result, any = 0;

    if (!owf_iff_open(&iff, data, length, OWF_ID('F', 'T', 'X', 'T')))
        return OWF_ERR_FORMAT;
    owf_builder_init(&b, doc, &doc->body);
    reader_init(&r, &b, report);
    while (owf_iff_next(&iff, &chunk)) {
        if (chunk.id == OWF_ID('F', 'O', 'N', 'S') && chunk.size >= 5) {
            /* id, pad, proportional (0 unknown, 1 no, 2 yes), serif (the
             * same), then the font's name. */
            unsigned id = chunk.data[0];
            char name[64];
            size_t n = 0;
            owf_font_kind kind = OWF_FONT_ANY;
            while (4 + n < chunk.size && chunk.data[4 + n] && n < sizeof name - 1) {
                name[n] = (char)chunk.data[4 + n];
                n++;
            }
            name[n] = 0;
            if (chunk.data[2] == 1)
                kind = OWF_FONT_MONO;
            else if (chunk.data[3] == 2)
                kind = OWF_FONT_SERIF;
            else if (chunk.data[3] == 1)
                kind = OWF_FONT_SANS;
            if (id <= 9 && n) {
                r.fonts[id] = owf_doc_font(doc, name, kind);
                if (r.fonts[id] < 0)
                    b.failed = 1;
            }
        } else if (chunk.id == OWF_ID('C', 'H', 'R', 'S')) {
            read_styled_text(&r, chunk.data, chunk.size);
            /* Each CHRS is a piece of text of its own. */
            if (b.para || b.text.length)
                owf_builder_end_para(&b);
            any = 1;
        }
    }
    if (iff.truncated)
        owf_report_add(report, OWF_NOTE_LOST, "The file is cut short; the text up to the cut was read");
    if (!any)
        owf_report_add(report, OWF_NOTE_INFO, "The FTXT file holds no text");
    owf_buf_free(&r.latin1);
    result = owf_builder_done(&b);
    return result;
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
