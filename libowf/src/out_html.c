/*
 * libowf: HTML out, in the shape the OpenWrite editor loads (DESIGN.md,
 * section 5): page setup in @page, paragraphs with inline formatting, our
 * own parts as ow- classes. Reading HTML back comes with the editor.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <string.h>

/* Twips as points with one decimal, without floating point (a 68000 has
 * no FPU). */
void owf_put_points(owf_buf *out, int twips)
{
    int tenths, negative = twips < 0;
    if (negative)
        twips = -twips;
    tenths = (twips + 1) / 2;   /* 1 twip = 0.05 pt; round to 0.1 */
    owf_buf_printf(out, negative ? "-%d" : "%d", tenths / 10);
    if (tenths % 10)
        owf_buf_printf(out, ".%d", tenths % 10);
    owf_buf_puts(out, "pt");
}

static const char *generic_family(owf_font_kind kind)
{
    switch (kind) {
    case OWF_FONT_SERIF: return "serif";
    case OWF_FONT_SANS: return "sans-serif";
    case OWF_FONT_MONO: return "monospace";
    default: return "serif";
    }
}

/* font-family for a document font: today's equivalent first, then its
 * own name. */
static void put_family(owf_buf *out, const owf_doc *doc, int font, owf_report *report)
{
    const owf_font *f = &doc->fonts[font];
    owf_font_kind kind = f->kind;
    const char *modern = owf_font_modern(f->name, &kind);

    if (modern) {
        owf_buf_puts(out, "'");
        owf_buf_puts_xml(out, modern);
        owf_buf_puts(out, "', ");
        owf_report_add(report, OWF_NOTE_APPROX, "The font %s is shown as %s", f->name, modern);
    }
    owf_buf_puts(out, "'");
    owf_buf_puts_xml(out, f->name);
    owf_buf_puts(out, "', ");
    owf_buf_puts(out, generic_family(kind));
}

static void put_run(owf_buf *out, const owf_doc *doc, const owf_run *run, owf_report *report)
{
    const owf_charfmt *f = &run->fmt;
    int span = f->font >= 0 || f->size || f->colour != OWF_COLOUR_AUTO;

    if (span) {
        owf_buf_puts(out, "<span style=\"");
        if (f->font >= 0 && f->font < doc->nfonts) {
            owf_buf_puts(out, "font-family: ");
            put_family(out, doc, f->font, report);
            owf_buf_puts(out, "; ");
        }
        if (f->size) {
            owf_buf_puts(out, "font-size: ");
            owf_put_points(out, f->size);
            owf_buf_puts(out, "; ");
        }
        if (f->colour != OWF_COLOUR_AUTO)
            owf_buf_printf(out, "color: #%06lx; ", f->colour & 0xFFFFFFUL);
        if (!out->failed)
            out->length -= 2;   /* the last "; " */
        owf_buf_puts(out, "\">");
    }
    if (f->flags & OWF_BOLD) owf_buf_puts(out, "<b>");
    if (f->flags & OWF_ITALIC) owf_buf_puts(out, "<i>");
    if (f->flags & OWF_UNDERLINE) owf_buf_puts(out, "<u>");
    if (f->flags & OWF_STRIKE) owf_buf_puts(out, "<s>");
    if (f->flags & OWF_SUPER) owf_buf_puts(out, "<sup>");
    else if (f->flags & OWF_SUB) owf_buf_puts(out, "<sub>");

    switch (run->kind) {
    case OWF_RUN_TEXT:
        owf_buf_puts_xml(out, run->text);
        break;
    case OWF_RUN_TAB:
        owf_buf_puts(out, "<span class=\"ow-tab\">\t</span>");
        break;
    case OWF_RUN_LINEBREAK:
        owf_buf_puts(out, "<br>");
        break;
    case OWF_RUN_IMAGE:
        owf_buf_puts(out, "[image]");
        break;
    case OWF_RUN_FIELD: {
        static const char *names[] = { "page", "pages", "date", "time" };
        static const char *shown[] = { "#", "##", "date", "time" };
        owf_buf_printf(out, "<span class=\"ow-field\" data-field=\"%s\">%s</span>",
                       names[run->field], shown[run->field]);
        break;
    }
    }

    if (f->flags & OWF_SUPER) owf_buf_puts(out, "</sup>");
    else if (f->flags & OWF_SUB) owf_buf_puts(out, "</sub>");
    if (f->flags & OWF_STRIKE) owf_buf_puts(out, "</s>");
    if (f->flags & OWF_UNDERLINE) owf_buf_puts(out, "</u>");
    if (f->flags & OWF_ITALIC) owf_buf_puts(out, "</i>");
    if (f->flags & OWF_BOLD) owf_buf_puts(out, "</b>");
    if (span)
        owf_buf_puts(out, "</span>");
}

static void put_para(owf_buf *out, const owf_doc *doc, const owf_para *para, owf_report *report)
{
    const owf_parafmt *f = &para->fmt;
    char tag[4] = "p";
    owf_buf style;
    int i;

    if (f->heading >= 1 && f->heading <= 6) {
        tag[0] = 'h';
        tag[1] = (char)('0' + f->heading);
        tag[2] = 0;
    }
    owf_buf_init(&style);
    if (f->align == OWF_ALIGN_CENTRE) owf_buf_puts(&style, "text-align: center; ");
    else if (f->align == OWF_ALIGN_RIGHT) owf_buf_puts(&style, "text-align: right; ");
    else if (f->align == OWF_ALIGN_JUSTIFY) owf_buf_puts(&style, "text-align: justify; ");
    if (f->indent_left) {
        owf_buf_puts(&style, "margin-left: ");
        owf_put_points(&style, f->indent_left);
        owf_buf_puts(&style, "; ");
    }
    if (f->indent_right) {
        owf_buf_puts(&style, "margin-right: ");
        owf_put_points(&style, f->indent_right);
        owf_buf_puts(&style, "; ");
    }
    if (f->indent_first) {
        owf_buf_puts(&style, "text-indent: ");
        owf_put_points(&style, f->indent_first);
        owf_buf_puts(&style, "; ");
    }
    if (f->space_before) {
        owf_buf_puts(&style, "margin-top: ");
        owf_put_points(&style, f->space_before);
        owf_buf_puts(&style, "; ");
    }
    if (f->space_after) {
        owf_buf_puts(&style, "margin-bottom: ");
        owf_put_points(&style, f->space_after);
        owf_buf_puts(&style, "; ");
    }
    if (f->line_spacing && f->line_spacing != 100)
        owf_buf_printf(&style, "line-height: %d%%; ", f->line_spacing);

    owf_buf_printf(out, "<%s", tag);
    if (f->page_break_before)
        owf_buf_puts(out, " class=\"ow-page-break\"");
    if (style.length && !style.failed) {
        style.length -= 2;
        style.data[style.length] = 0;
        owf_buf_puts(out, " style=\"");
        owf_buf_put(out, style.data, style.length);
        owf_buf_puts(out, "\"");
    }
    if (style.failed)
        out->failed = 1;
    owf_buf_free(&style);
    if (f->ntabs) {
        static const char *kinds[] = { "left", "centre", "right", "decimal" };
        owf_buf_puts(out, " data-ow-tabs=\"");
        for (i = 0; i < f->ntabs; i++)
            owf_buf_printf(out, i ? " %d:%s" : "%d:%s", f->tabs[i].position, kinds[f->tabs[i].kind]);
        owf_buf_puts(out, "\"");
    }
    owf_buf_puts(out, ">");
    for (i = 0; i < para->nruns; i++)
        put_run(out, doc, &para->runs[i], report);
    if (!para->nruns)
        owf_buf_puts(out, "<br>");
    owf_buf_printf(out, "</%s>\n", tag);
}

static void put_story(owf_buf *out, const owf_doc *doc, const owf_story *story, owf_report *report)
{
    int i;
    for (i = 0; i < story->nparas; i++)
        put_para(out, doc, &story->paras[i], report);
}

static int export_html(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    owf_buf out;
    const owf_page *pg = &doc->page;

    owf_buf_init(&out);
    owf_buf_puts(&out, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
                       "<meta name=\"generator\" content=\"OpenWrite libowf " OWF_VERSION "\">\n");
    owf_buf_puts(&out, "<title>");
    owf_buf_puts_xml(&out, doc->title ? doc->title : "");
    owf_buf_puts(&out, "</title>\n<style>\n@page { size: ");
    owf_put_points(&out, pg->width);
    owf_buf_puts(&out, " ");
    owf_put_points(&out, pg->height);
    owf_buf_puts(&out, "; margin: ");
    owf_put_points(&out, pg->margin_top);
    owf_buf_puts(&out, " ");
    owf_put_points(&out, pg->margin_right);
    owf_buf_puts(&out, " ");
    owf_put_points(&out, pg->margin_bottom);
    owf_buf_puts(&out, " ");
    owf_put_points(&out, pg->margin_left);
    owf_buf_puts(&out, "; }\nbody { white-space: pre-wrap; font-family: ");
    if (doc->base.font >= 0 && doc->base.font < doc->nfonts)
        put_family(&out, doc, doc->base.font, report);
    else
        owf_buf_puts(&out, "'Liberation Serif', serif");
    owf_buf_puts(&out, "; font-size: ");
    owf_put_points(&out, doc->base.size ? doc->base.size : 240);
    owf_buf_puts(&out, "; }\n"
                       "p { margin: 0; }\n"
                       ".ow-page-break { break-before: page; }\n"
                       ".ow-header, .ow-footer { color: #666; font-size: 90%; }\n"
                       "</style>\n</head>\n<body>\n");
    if (doc->header.nparas) {
        owf_buf_printf(&out, "<div class=\"ow-header\" data-first-page=\"%d\">\n", pg->header_on_first);
        put_story(&out, doc, &doc->header, report);
        owf_buf_puts(&out, "</div>\n");
    }
    put_story(&out, doc, &doc->body, report);
    if (doc->footer.nparas) {
        owf_buf_printf(&out, "<div class=\"ow-footer\" data-first-page=\"%d\">\n", pg->footer_on_first);
        put_story(&out, doc, &doc->footer, report);
        owf_buf_puts(&out, "</div>\n");
    }
    owf_buf_puts(&out, "</body>\n</html>\n");
    return owf_buf_take(&out, data, length);
}

const owf_format owf_format_html = {
    "html", "HTML (OpenWrite's editor shape)", "html htm", NULL, NULL, export_html
};
