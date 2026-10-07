/*
 * libowf: the document model, the import report and the builder the
 * importers share.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const unsigned long owf_iso_colours[8] = {
    0x000000, 0xCC0000, 0x008800, 0xCCAA00, 0x0000CC, 0xAA00AA, 0x00AAAA, 0xFFFFFF
};

static char *copy_text(const char *s, size_t length)
{
    char *out = malloc(length + 1);
    if (!out)
        return NULL;
    memcpy(out, s, length);
    out[length] = 0;
    return out;
}

void owf_charfmt_init(owf_charfmt *fmt)
{
    fmt->flags = 0;
    fmt->font = -1;
    fmt->size = 0;
    fmt->colour = OWF_COLOUR_AUTO;
}

void owf_parafmt_init(owf_parafmt *fmt)
{
    memset(fmt, 0, sizeof *fmt);
    fmt->align = OWF_ALIGN_LEFT;
    fmt->line_spacing = 100;
}

owf_doc *owf_doc_new(void)
{
    owf_doc *doc = calloc(1, sizeof *doc);
    if (!doc)
        return NULL;
    owf_charfmt_init(&doc->base);
    doc->base.size = 12 * OWF_TWIPS_PER_POINT;
    /* A4 with 2.5 cm margins until a document says otherwise. */
    doc->page.width = 11906;
    doc->page.height = 16838;
    doc->page.margin_top = doc->page.margin_bottom = 1417;
    doc->page.margin_left = doc->page.margin_right = 1417;
    doc->page.start_page = 1;
    doc->page.header_on_first = doc->page.footer_on_first = 1;
    return doc;
}

static void free_story(owf_story *story)
{
    int i, j;
    for (i = 0; i < story->nparas; i++) {
        for (j = 0; j < story->paras[i].nruns; j++) {
            free(story->paras[i].runs[j].text);
            free(story->paras[i].runs[j].href);
        }
        free(story->paras[i].runs);
    }
    free(story->paras);
    story->paras = NULL;
    story->nparas = story->capparas = 0;
}

void owf_doc_free(owf_doc *doc)
{
    int i;
    if (!doc)
        return;
    free_story(&doc->body);
    free_story(&doc->header);
    free_story(&doc->footer);
    for (i = 0; i < doc->nfonts; i++)
        free(doc->fonts[i].name);
    free(doc->fonts);
    for (i = 0; i < doc->nimages; ++i) { free(doc->images[i].name); free(doc->images[i].mime); free(doc->images[i].alt); free(doc->images[i].data); }
    free(doc->images);
    free(doc->title);
    free(doc);
}

owf_para *owf_story_add(owf_story *story, const owf_parafmt *fmt)
{
    owf_para *para;

    if (story->nparas == story->capparas) {
        int want = story->capparas ? story->capparas * 2 : 16;
        owf_para *paras = realloc(story->paras, (size_t)want * sizeof *paras);
        if (!paras)
            return NULL;
        story->paras = paras;
        story->capparas = want;
    }
    para = &story->paras[story->nparas++];
    memset(para, 0, sizeof *para);
    para->table_id = -1;
    if (fmt)
        para->fmt = *fmt;
    else
        owf_parafmt_init(&para->fmt);
    return para;
}

static int same_charfmt(const owf_charfmt *a, const owf_charfmt *b)
{
    return a->flags == b->flags && a->font == b->font && a->size == b->size && a->colour == b->colour;
}

static owf_run *add_run(owf_para *para)
{
    owf_run *run;

    if (para->nruns == para->capruns) {
        int want = para->capruns ? para->capruns * 2 : 4;
        owf_run *runs = realloc(para->runs, (size_t)want * sizeof *runs);
        if (!runs)
            return NULL;
        para->runs = runs;
        para->capruns = want;
    }
    run = &para->runs[para->nruns++];
    memset(run, 0, sizeof *run);
    return run;
}

int owf_para_add_text(owf_para *para, const owf_charfmt *fmt, const char *utf8, size_t len)
{
    owf_run *run;

    if (!len)
        return 0;
    /* Text in the same formatting as the run before joins it. */
    if (para->nruns) {
        run = &para->runs[para->nruns - 1];
        if (run->kind == OWF_RUN_TEXT && !run->href && same_charfmt(&run->fmt, fmt)) {
            size_t old = strlen(run->text);
            char *joined = realloc(run->text, old + len + 1);
            if (!joined)
                return -1;
            memcpy(joined + old, utf8, len);
            joined[old + len] = 0;
            run->text = joined;
            return 0;
        }
    }
    run = add_run(para);
    if (!run)
        return -1;
    run->text = copy_text(utf8, len);
    if (!run->text) {
        para->nruns--;
        return -1;
    }
    run->kind = OWF_RUN_TEXT;
    run->fmt = *fmt;
    return 0;
}

int owf_para_add_link_text(owf_para *para, const owf_charfmt *fmt,
                           const char *utf8, size_t len, const char *href)
{
    owf_run *run;
    if (!para || !fmt || !utf8 || !len || !href || !*href) return -1;
    run = add_run(para);
    if (!run) return -1;
    run->text = copy_text(utf8, len);
    run->href = copy_text(href, strlen(href));
    if (!run->text || !run->href) {
        free(run->text); free(run->href); memset(run, 0, sizeof *run); para->nruns--; return -1;
    }
    run->kind = OWF_RUN_TEXT;
    run->fmt = *fmt;
    return 0;
}

int owf_para_add_special(owf_para *para, const owf_charfmt *fmt, owf_run_kind kind, owf_field field)
{
    owf_run *run = add_run(para);
    if (!run)
        return -1;
    run->kind = kind;
    run->fmt = *fmt;
    run->field = field;
    return 0;
}

int owf_para_add_image(owf_para *para, const owf_charfmt *fmt, int image_index)
{
    owf_run *run=add_run(para);if(!run)return -1;run->kind=OWF_RUN_IMAGE;run->fmt=*fmt;run->image=image_index;return 0;
}

int owf_doc_add_image(owf_doc *doc, const char *name, const char *mime, const void *data, size_t length, int width, int height, const char *alt)
{
    owf_image *im;int want;
    if(!doc||!data||!length)return -1;
    if(doc->nimages==doc->capimages){want=doc->capimages?doc->capimages*2:8;im=(owf_image*)realloc(doc->images,(size_t)want*sizeof(*im));if(!im)return -1;doc->images=im;doc->capimages=want;}
    im=&doc->images[doc->nimages];memset(im,0,sizeof(*im));
    im->data=(unsigned char*)malloc(length);if(!im->data)return -1;memcpy(im->data,data,length);im->length=length;
    if(name){im->name=copy_text(name,strlen(name));if(!im->name)goto fail;}if(mime){im->mime=copy_text(mime,strlen(mime));if(!im->mime)goto fail;}if(alt){im->alt=copy_text(alt,strlen(alt));if(!im->alt)goto fail;}
    im->width=width>0?width:4320;im->height=height>0?height:2880;return doc->nimages++;
fail:free(im->name);free(im->mime);free(im->alt);free(im->data);memset(im,0,sizeof(*im));return -1;
}

int owf_doc_font(owf_doc *doc, const char *name, owf_font_kind kind)
{
    int i;
    for (i = 0; i < doc->nfonts; i++)
        if (strcmp(doc->fonts[i].name, name) == 0)
            return i;
    if (doc->nfonts == doc->capfonts) {
        int want = doc->capfonts ? doc->capfonts * 2 : 8;
        owf_font *fonts = realloc(doc->fonts, (size_t)want * sizeof *fonts);
        if (!fonts)
            return -1;
        doc->fonts = fonts;
        doc->capfonts = want;
    }
    doc->fonts[doc->nfonts].name = copy_text(name, strlen(name));
    if (!doc->fonts[doc->nfonts].name)
        return -1;
    doc->fonts[doc->nfonts].kind = kind;
    return doc->nfonts++;
}

int owf_doc_set_title(owf_doc *doc, const char *utf8)
{
    char *title = copy_text(utf8, strlen(utf8));
    if (!title)
        return -1;
    free(doc->title);
    doc->title = title;
    return 0;
}

/* Amiga and PC font names, and what we draw them with. */
static const struct {
    const char *name;
    const char *modern;
    owf_font_kind kind;
} font_map[] = {
    { "times", "Liberation Serif", OWF_FONT_SERIF },
    { "cgtimes", "Liberation Serif", OWF_FONT_SERIF },
    { "cg times", "Liberation Serif", OWF_FONT_SERIF },
    { "times new roman", "Liberation Serif", OWF_FONT_SERIF },
    { "timesroman", "Liberation Serif", OWF_FONT_SERIF },
    { "helvetica", "Liberation Sans", OWF_FONT_SANS },
    { "cgtriumvirate", "Liberation Sans", OWF_FONT_SANS },
    { "cg triumvirate", "Liberation Sans", OWF_FONT_SANS },
    { "arial", "Liberation Sans", OWF_FONT_SANS },
    { "helvetica narrow", "Liberation Sans Narrow", OWF_FONT_SANS },
    /* The Workbench's own bitmap fonts: nearest is a plain sans. */
    { "diamond", "Liberation Sans", OWF_FONT_SANS },
    { "emerald", "Liberation Sans", OWF_FONT_SANS },
    { "garnet", "Liberation Sans", OWF_FONT_SANS },
    { "opal", "Liberation Sans", OWF_FONT_SANS },
    { "ruby", "Liberation Sans", OWF_FONT_SANS },
    { "sapphire", "Liberation Sans", OWF_FONT_SANS },
    { "xen", "Liberation Sans", OWF_FONT_SANS },
    { "calibri", "Carlito", OWF_FONT_SANS },
    { "cambria", "Caladea", OWF_FONT_SERIF },
    { "courier", "Liberation Mono", OWF_FONT_MONO },
    { "courier new", "Liberation Mono", OWF_FONT_MONO },
    { "topaz", "Liberation Mono", OWF_FONT_MONO },
    { "lettergothic", "Liberation Mono", OWF_FONT_MONO },
    { "letter gothic", "Liberation Mono", OWF_FONT_MONO },
};

const char *owf_font_modern(const char *name, owf_font_kind *kind)
{
    char lower[64];
    size_t i, n = 0;

    /* "times.font" and "Times" are the same font. */
    for (i = 0; name[i] && n < sizeof lower - 1; i++)
        lower[n++] = (char)tolower((unsigned char)name[i]);
    lower[n] = 0;
    if (n > 5 && strcmp(lower + n - 5, ".font") == 0)
        lower[n - 5] = 0;
    for (i = 0; i < sizeof font_map / sizeof font_map[0]; i++)
        if (strcmp(lower, font_map[i].name) == 0) {
            if (kind)
                *kind = font_map[i].kind;
            return font_map[i].modern;
        }
    return NULL;
}

/* ---- The import report ---- */

typedef struct {
    char *text;
    owf_note_level level;
    int times;
} note;

struct owf_report {
    note *notes;
    int count, capacity;
};

owf_report *owf_report_new(void)
{
    return calloc(1, sizeof(owf_report));
}

void owf_report_free(owf_report *report)
{
    int i;
    if (!report)
        return;
    for (i = 0; i < report->count; i++)
        free(report->notes[i].text);
    free(report->notes);
    free(report);
}

void owf_report_add(owf_report *report, owf_note_level level, const char *fmt, ...)
{
    char text[256];
    va_list args;
    int i;

    if (!report)
        return;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    for (i = 0; i < report->count; i++)
        if (report->notes[i].level == level && strcmp(report->notes[i].text, text) == 0) {
            report->notes[i].times++;
            return;
        }
    if (report->count == report->capacity) {
        int want = report->capacity ? report->capacity * 2 : 8;
        note *notes = realloc(report->notes, (size_t)want * sizeof *notes);
        if (!notes)
            return;
        report->notes = notes;
        report->capacity = want;
    }
    report->notes[report->count].text = copy_text(text, strlen(text));
    if (!report->notes[report->count].text)
        return;
    report->notes[report->count].level = level;
    report->notes[report->count].times = 1;
    report->count++;
}

int owf_report_count(const owf_report *report)
{
    return report ? report->count : 0;
}

const char *owf_report_note(const owf_report *report, int n, owf_note_level *level, int *times)
{
    if (!report || n < 0 || n >= report->count)
        return NULL;
    if (level)
        *level = report->notes[n].level;
    if (times)
        *times = report->notes[n].times;
    return report->notes[n].text;
}

/* ---- The builder ---- */

void owf_builder_init(owf_builder *b, owf_doc *doc, owf_story *story)
{
    b->doc = doc;
    b->story = story;
    b->para = NULL;
    owf_parafmt_init(&b->parafmt);
    owf_charfmt_init(&b->charfmt);
    owf_buf_init(&b->text);
    b->failed = 0;
}

static int ensure_para(owf_builder *b)
{
    if (b->para)
        return 1;
    b->para = owf_story_add(b->story, &b->parafmt);
    if (!b->para) {
        b->failed = 1;
        return 0;
    }
    /* A page break belongs to one paragraph only. */
    b->parafmt.page_break_before = 0;
    return 1;
}

void owf_builder_flush(owf_builder *b)
{
    if (!b->text.length)
        return;
    if (b->text.failed) {
        b->failed = 1;
    } else if (ensure_para(b) &&
               owf_para_add_text(b->para, &b->charfmt, (const char *)b->text.data, b->text.length) < 0) {
        b->failed = 1;
    }
    b->text.length = 0;
}

void owf_builder_text(owf_builder *b, const char *utf8, size_t length)
{
    owf_buf_put(&b->text, utf8, length);
}

void owf_builder_link_text(owf_builder *b, const char *utf8, size_t length, const char *href)
{
    owf_builder_flush(b);
    if (ensure_para(b) && owf_para_add_link_text(b->para, &b->charfmt, utf8, length, href) < 0)
        b->failed = 1;
}

void owf_builder_special(owf_builder *b, owf_run_kind kind, owf_field field)
{
    owf_builder_flush(b);
    if (ensure_para(b) && owf_para_add_special(b->para, &b->charfmt, kind, field) < 0)
        b->failed = 1;
}

void owf_builder_image(owf_builder *b, int image_index)
{
    owf_builder_flush(b);
    if (ensure_para(b) && owf_para_add_image(b->para, &b->charfmt, image_index) < 0) b->failed = 1;
}

void owf_builder_end_para(owf_builder *b)
{
    owf_builder_flush(b);
    ensure_para(b);
    b->para = NULL;
}

int owf_builder_done(owf_builder *b)
{
    owf_builder_flush(b);
    owf_buf_free(&b->text);
    b->para = NULL;
    return b->failed ? OWF_ERR_MEMORY : OWF_OK;
}
