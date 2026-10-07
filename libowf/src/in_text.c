/*
 * libowf: plain text in and out. "text" is UTF-8; "amiga-text" is the
 * Amiga's ISO-8859-1. Reading, a file that is not valid UTF-8 is taken as
 * ISO-8859-1. Each line is a paragraph; a form feed is a page break.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

static int detect_text(const unsigned char *data, size_t length)
{
    size_t i, n = length < 4096 ? length : 4096;

    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return 60;
    for (i = 0; i < n; i++) {
        unsigned c = data[i];
        if (c == 0 || (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != 0x1B))
            return 0;
    }
    return 10;  /* anything else that recognises the file wins */
}

static int import_text(const unsigned char *data, size_t length, owf_doc *doc, owf_report *report)
{
    owf_builder b;
    owf_buf line;
    size_t i, start;
    int utf8;

    if (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        data += 3;
        length -= 3;
    }
    utf8 = owf_utf8_valid(data, length);
    owf_report_add(report, OWF_NOTE_INFO, utf8 ? "Read as UTF-8 text" : "Read as Amiga (ISO-8859-1) text");
    owf_builder_init(&b, doc, &doc->body);
    owf_buf_init(&line);
    start = 0;
    for (i = 0; i <= length; i++) {
        int c = i < length ? data[i] : -1;
        if (c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == -1) {
            if (i > start) {
                if (utf8) {
                    owf_builder_text(&b, (const char *)data + start, i - start);
                } else {
                    line.length = 0;
                    owf_buf_put_latin1(&line, data + start, i - start);
                    if (line.failed)
                        b.failed = 1;
                    else
                        owf_builder_text(&b, (const char *)line.data, line.length);
                }
            }
            start = i + 1;
            if (c == '\t') {
                owf_builder_special(&b, OWF_RUN_TAB, OWF_FIELD_PAGE);
            } else if (c == '\n' || c == '\r') {
                if (c == '\r' && i + 1 < length && data[i + 1] == '\n') {
                    i++;
                    start = i + 1;
                }
                owf_builder_end_para(&b);
            } else if (c == '\f') {
                /* A page break after a line feed starts no empty paragraph. */
                if (b.para || b.text.length)
                    owf_builder_end_para(&b);
                b.parafmt.page_break_before = 1;
            } else if (c == -1 && (b.para || b.text.length)) {
                owf_builder_end_para(&b);
            }
        }
    }
    owf_buf_free(&line);
    return owf_builder_done(&b);
}

static void put_field(owf_buf *out, owf_field field)
{
    switch (field) {
    case OWF_FIELD_PAGE: owf_buf_puts(out, "#"); break;
    case OWF_FIELD_PAGES: owf_buf_puts(out, "##"); break;
    case OWF_FIELD_DATE: owf_buf_puts(out, "[date]"); break;
    case OWF_FIELD_TIME: owf_buf_puts(out, "[time]"); break;
    }
}

static void put_story(owf_buf *out, const owf_story *story, int *first)
{
    int i, j;

    for (i = 0; i < story->nparas; i++) {
        const owf_para *para = &story->paras[i];
        if (para->fmt.page_break_before && !*first)
            owf_buf_putc(out, '\f');
        *first = 0;
        for (j = 0; j < para->nruns; j++) {
            const owf_run *run = &para->runs[j];
            switch (run->kind) {
            case OWF_RUN_TEXT: owf_buf_puts(out, run->text); break;
            case OWF_RUN_TAB: owf_buf_putc(out, '\t'); break;
            case OWF_RUN_LINEBREAK: owf_buf_putc(out, '\n'); break;
            case OWF_RUN_FIELD: put_field(out, run->field); break;
            case OWF_RUN_IMAGE: owf_buf_puts(out, "[image]"); break;
            }
        }
        owf_buf_putc(out, '\n');
    }
}

static int export_utf8(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    owf_buf out;
    int first = 1;

    owf_buf_init(&out);
    put_story(&out, &doc->body, &first);
    if (doc->header.nparas || doc->footer.nparas)
        owf_report_add(report, OWF_NOTE_LOST, "Headers and footers are not kept in plain text");
    owf_report_add(report, OWF_NOTE_LOST, "Plain text keeps no formatting");
    return owf_buf_take(&out, data, length);
}

static int export_latin1(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    unsigned char *utf8;
    size_t utf8_length;
    const unsigned char *p, *end;
    owf_buf out;
    int result = export_utf8(doc, &utf8, &utf8_length, report);

    if (result != OWF_OK)
        return result;
    owf_buf_init(&out);
    p = utf8;
    end = utf8 + utf8_length;
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        if (c > 0xFF) {
            owf_report_add(report, OWF_NOTE_APPROX, "Characters the Amiga's character set lacks became ?");
            c = '?';
        }
        owf_buf_putc(&out, (int)c);
    }
    free(utf8);
    return owf_buf_take(&out, data, length);
}

const owf_format owf_format_text = {
    "text", "Plain text (UTF-8)", "txt text", detect_text, import_text, export_utf8
};

const owf_format owf_format_amiga_text = {
    "amiga-text", "Plain text (Amiga, ISO-8859-1)", "asc", NULL, import_text, export_latin1
};
