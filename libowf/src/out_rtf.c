/*
 * libowf: Rich Text Format export. This writes the shared document model,
 * independently of GFOpenWrite's UI and GFOpenLayout's screen/page layout.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_internal.h"

#include <string.h>

#define RTF_OUT_COLOURS 64

typedef struct {
    const owf_doc *doc;
    owf_report *report;
    owf_buf out;
    unsigned long colours[RTF_OUT_COLOURS];
    int ncolours;
    int pictures, frames;
} rtf_out;

static int colour_index(rtf_out *w, unsigned long colour)
{
    int i;
    if (colour == OWF_COLOUR_AUTO)
        return 0;
    colour &= 0xFFFFFFUL;
    for (i = 0; i < w->ncolours; ++i)
        if (w->colours[i] == colour)
            return i + 1;
    if (w->ncolours >= RTF_OUT_COLOURS)
        return 0;
    w->colours[w->ncolours++] = colour;
    return w->ncolours;
}

static void collect_colours(rtf_out *w, const owf_story *story)
{
    int i, j;
    for (i = 0; i < story->nparas; ++i) {
        const owf_para *p = &story->paras[i];
        if (OWF_SHADE_SET(p->fmt.shading)) colour_index(w, OWF_SHADE_RGB(p->fmt.shading));
        if (OWF_SHADE_SET(p->fmt.border_colour)) colour_index(w, OWF_SHADE_RGB(p->fmt.border_colour));
        if (OWF_SHADE_SET(p->cell_shading)) colour_index(w, OWF_SHADE_RGB(p->cell_shading));
        for (j = 0; j < p->nruns; ++j)
            if (p->runs[j].fmt.colour != OWF_COLOUR_AUTO)
                colour_index(w, p->runs[j].fmt.colour);
    }
}

static void put_unicode_unit(owf_buf *out, unsigned long c)
{
    long signed_unit = c >= 0x8000 ? (long)c - 0x10000L : (long)c;
    owf_buf_printf(out, "\\u%ld?", signed_unit);
}

static void put_text(owf_buf *out, const char *text)
{
    const unsigned char *p = (const unsigned char *)text, *end = p + strlen(text);
    while (p < end) {
        unsigned long c = owf_utf8_next(&p, end);
        if (c == '\\' || c == '{' || c == '}') {
            owf_buf_putc(out, '\\');
            owf_buf_putc(out, (int)c);
        } else if (c == '\t') {
            owf_buf_puts(out, "\\tab ");
        } else if (c == '\n' || c == '\r') {
            owf_buf_puts(out, "\\line ");
        } else if (c >= 0x20 && c < 0x7F) {
            owf_buf_putc(out, (int)c);
        } else if (c <= 0xFF) {
            owf_buf_printf(out, "\\'%02lx", c);
        } else if (c <= 0xFFFF) {
            put_unicode_unit(out, c);
        } else if (c <= 0x10FFFF) {
            c -= 0x10000;
            put_unicode_unit(out, 0xD800 + (c >> 10));
            put_unicode_unit(out, 0xDC00 + (c & 0x3FF));
        } else {
            put_unicode_unit(out, 0xFFFD);
        }
    }
}

static int run_font(const owf_doc *doc, const owf_charfmt *f)
{
    int font = f->font >= 0 ? f->font : doc->base.font;
    return font >= 0 && font < doc->nfonts ? font : 0;
}

static int run_size(const owf_doc *doc, const owf_charfmt *f)
{
    int size = f->size ? f->size : doc->base.size;
    return size > 0 ? (size + 5) / 10 : 24;       /* RTF: half-points */
}

static void put_charfmt(rtf_out *w, const owf_charfmt *f)
{
    owf_buf_puts(&w->out, "\\plain");
    owf_buf_printf(&w->out, "\\f%d\\fs%d", run_font(w->doc, f), run_size(w->doc, f));
    if (f->flags & OWF_BOLD) owf_buf_puts(&w->out, "\\b");
    if (f->flags & OWF_ITALIC) owf_buf_puts(&w->out, "\\i");
    if (f->flags & OWF_UNDERLINE) owf_buf_puts(&w->out, "\\ul");
    if (f->flags & OWF_STRIKE) owf_buf_puts(&w->out, "\\strike");
    if (f->flags & OWF_SUPER) owf_buf_puts(&w->out, "\\super");
    else if (f->flags & OWF_SUB) owf_buf_puts(&w->out, "\\sub");
    if (f->colour != OWF_COLOUR_AUTO)
        owf_buf_printf(&w->out, "\\cf%d", colour_index(w, f->colour));
    owf_buf_putc(&w->out, ' ');
}

static void put_runs(rtf_out *w, const owf_para *p, int depth);

static void put_image(rtf_out *w, const owf_image *image)
{
    size_t i;
    const char *kind;
    if (!image || !image->data || !image->length) { ++w->pictures; return; }
    if (image->mime && !strcmp(image->mime, "image/png")) kind = "pngblip";
    else if (image->mime && (!strcmp(image->mime, "image/jpeg") || !strcmp(image->mime, "image/jpg"))) kind = "jpegblip";
    else { ++w->pictures; return; }
    owf_buf_printf(&w->out, "{\\pict\\%s\\picwgoal%d\\pichgoal%d\n", kind, image->width, image->height);
    for (i = 0; i < image->length; ++i) {
        owf_buf_printf(&w->out, "%02x", image->data[i]);
        if ((i & 31) == 31) owf_buf_putc(&w->out, '\n');
    }
    owf_buf_puts(&w->out, "}\n");
}

static void put_frame(rtf_out *w, const owf_frame *frame, int depth)
{
    int i;
    if (frame->image >= 0 && frame->image < w->doc->nimages) put_image(w, &w->doc->images[frame->image]);
    if (depth > 2) return;
    for (i = 0; i < frame->story.nparas; ++i) {
        if (i) owf_buf_puts(&w->out, "\\line ");
        put_runs(w, &frame->story.paras[i], depth + 1);
    }
    ++w->frames;
}

static void put_runs(rtf_out *w, const owf_para *p, int depth)
{
    int i;
    for (i = 0; i < p->nruns; ++i) {
        const owf_run *r = &p->runs[i];
        put_charfmt(w, &r->fmt);
        switch (r->kind) {
        case OWF_RUN_TEXT:
            if (r->href && *r->href) {
                owf_buf_puts(&w->out, "{\\field{\\*\\fldinst HYPERLINK \\\"");
                put_text(&w->out, r->href);
                owf_buf_puts(&w->out, "\\\"}{\\fldrslt ");
                put_text(&w->out, r->text ? r->text : "");
                owf_buf_puts(&w->out, "}}");
            } else {
                put_text(&w->out, r->text ? r->text : "");
            }
            break;
        case OWF_RUN_TAB: owf_buf_puts(&w->out, "\\tab "); break;
        case OWF_RUN_LINEBREAK: owf_buf_puts(&w->out, "\\line "); break;
        case OWF_RUN_FIELD:
            owf_buf_printf(&w->out, "{\\field{\\*\\fldinst %s}{\\fldrslt }}",
                           r->field == OWF_FIELD_PAGE ? "PAGE" : r->field == OWF_FIELD_PAGES ? "NUMPAGES" :
                           r->field == OWF_FIELD_DATE ? "DATE" : "TIME");
            break;
        case OWF_RUN_IMAGE:
            if (r->image >= 0 && r->image < w->doc->nimages) put_image(w, &w->doc->images[r->image]);
            else ++w->pictures;
            break;
        case OWF_RUN_FRAME:
            if (r->image >= 0 && r->image < w->doc->nframes)
                put_frame(w, &w->doc->frames[r->image], depth);
            break;
        }
    }
}

static void put_parafmt(rtf_out *w, const owf_para *p)
{
    const owf_parafmt *f = &p->fmt;
    int i;
    owf_buf_puts(&w->out, "\\pard");
    if (f->page_break_before) owf_buf_puts(&w->out, "\\page");
    if (f->heading > 0 && f->heading <= 6) owf_buf_printf(&w->out, "\\outlinelevel%d", f->heading - 1);
    owf_buf_puts(&w->out, f->align == OWF_ALIGN_CENTRE ? "\\qc" : f->align == OWF_ALIGN_RIGHT ? "\\qr" :
                              f->align == OWF_ALIGN_JUSTIFY ? "\\qj" : "\\ql");
    if (f->indent_left) owf_buf_printf(&w->out, "\\li%d", f->indent_left);
    if (f->indent_right) owf_buf_printf(&w->out, "\\ri%d", f->indent_right);
    if (f->indent_first) owf_buf_printf(&w->out, "\\fi%d", f->indent_first);
    if (f->space_before) owf_buf_printf(&w->out, "\\sb%d", f->space_before);
    if (f->space_after) owf_buf_printf(&w->out, "\\sa%d", f->space_after);
    if (OWF_SHADE_SET(f->shading)) owf_buf_printf(&w->out, "\\cbpat%d", colour_index(w, OWF_SHADE_RGB(f->shading)));
    for (i = 0; i < f->ntabs; ++i) {
        const owf_tab *t = &f->tabs[i];
        if (t->kind == OWF_TAB_CENTRE) owf_buf_puts(&w->out, "\\tqc");
        else if (t->kind == OWF_TAB_RIGHT) owf_buf_puts(&w->out, "\\tqr");
        else if (t->kind == OWF_TAB_DECIMAL) owf_buf_puts(&w->out, "\\tqdec");
        if (t->leader == OWF_LEADER_DOT) owf_buf_puts(&w->out, "\\tldot");
        else if (t->leader == OWF_LEADER_HYPHEN) owf_buf_puts(&w->out, "\\tlhyph");
        else if (t->leader == OWF_LEADER_UNDERSCORE) owf_buf_puts(&w->out, "\\tlul");
        owf_buf_printf(&w->out, "\\tx%d", t->position);
    }
    owf_buf_putc(&w->out, ' ');
}

static void put_story(rtf_out *w, const owf_story *story)
{
    int i = 0;
    while (i < story->nparas) {
        const owf_para *p = &story->paras[i];
        if (p->table_id >= 0) {
            int table = p->table_id, cols = p->table_cols > 0 ? p->table_cols : 1;
            int usable = w->doc->page.width - w->doc->page.margin_left - w->doc->page.margin_right;
            while (i < story->nparas && story->paras[i].table_id == table) {
                int row = story->paras[i].table_row, col;
                owf_buf_puts(&w->out, "\\pard\\trowd\\trgaph108");
                for (col = 0; col < cols; ++col)
                    owf_buf_printf(&w->out, "\\cellx%d", usable * (col + 1) / cols);
                owf_buf_putc(&w->out, ' ');
                while (i < story->nparas && story->paras[i].table_id == table && story->paras[i].table_row == row) {
                    owf_buf_puts(&w->out, "\\intbl ");
                    put_runs(w, &story->paras[i], 0);
                    owf_buf_puts(&w->out, "\\cell ");
                    ++i;
                }
                owf_buf_puts(&w->out, "\\row\n");
            }
        } else {
            put_parafmt(w, p);
            put_runs(w, p, 0);
            owf_buf_puts(&w->out, "\\par\n");
            ++i;
        }
    }
}

static void put_story_group(rtf_out *w, const char *name, const owf_story *story)
{
    if (!story->nparas) return;
    owf_buf_printf(&w->out, "{\\%s\n", name);
    put_story(w, story);
    owf_buf_puts(&w->out, "}\n");
}

int owf_export_rtf(const owf_doc *doc, unsigned char **data, size_t *length, owf_report *report)
{
    rtf_out w;
    int i;
    memset(&w, 0, sizeof w);
    w.doc = doc;
    w.report = report;
    owf_buf_init(&w.out);
    collect_colours(&w, &doc->body);
    collect_colours(&w, &doc->header);
    collect_colours(&w, &doc->footer);
    collect_colours(&w, &doc->header_left);
    collect_colours(&w, &doc->footer_left);
    owf_buf_puts(&w.out, "{\\rtf1\\ansi\\ansicpg1252\\uc1");
    owf_buf_printf(&w.out, "\\deff%d\n", doc->base.font >= 0 && doc->base.font < doc->nfonts ? doc->base.font : 0);
    owf_buf_puts(&w.out, "{\\fonttbl");
    if (!doc->nfonts) owf_buf_puts(&w.out, "{\\f0\\froman Times New Roman;}");
    for (i = 0; i < doc->nfonts; ++i) {
        const owf_font *f = &doc->fonts[i];
        owf_buf_printf(&w.out, "{\\f%d%s ", i, f->kind == OWF_FONT_SERIF ? "\\froman" :
                       f->kind == OWF_FONT_MONO ? "\\fmodern" : "\\fswiss");
        put_text(&w.out, f->name ? f->name : "Times New Roman");
        owf_buf_puts(&w.out, ";}");
    }
    owf_buf_puts(&w.out, "}\n{\\colortbl;");
    for (i = 0; i < w.ncolours; ++i)
        owf_buf_printf(&w.out, "\\red%lu\\green%lu\\blue%lu;", (w.colours[i] >> 16) & 255,
                       (w.colours[i] >> 8) & 255, w.colours[i] & 255);
    owf_buf_puts(&w.out, "}\n");
    if (doc->title && *doc->title) {
        owf_buf_puts(&w.out, "{\\info{\\title "); put_text(&w.out, doc->title); owf_buf_puts(&w.out, "}}\n");
    }
    owf_buf_printf(&w.out, "\\paperw%d\\paperh%d\\margl%d\\margr%d\\margt%d\\margb%d\n",
                   doc->page.width, doc->page.height, doc->page.margin_left, doc->page.margin_right,
                   doc->page.margin_top, doc->page.margin_bottom);
    if (doc->header_left.nparas) {
        put_story_group(&w, "headerr", &doc->header);
        put_story_group(&w, "headerl", &doc->header_left);
    } else put_story_group(&w, "header", &doc->header);
    if (doc->footer_left.nparas) {
        put_story_group(&w, "footerr", &doc->footer);
        put_story_group(&w, "footerl", &doc->footer_left);
    } else put_story_group(&w, "footer", &doc->footer);
    put_story(&w, &doc->body);
    owf_buf_puts(&w.out, "}\n");
    if (w.pictures) owf_report_add(report, OWF_NOTE_LOST, "Pictures are not saved in RTF yet");
    if (w.frames) owf_report_add(report, OWF_NOTE_APPROX, "Text frames are saved in RTF at their anchor as ordinary text");
    return owf_buf_take(&w.out, data, length);
}
