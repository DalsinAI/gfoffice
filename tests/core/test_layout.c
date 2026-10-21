/*
 * OpenWrite's pages on OpenLayout (src/core/ow_layout.c): a document built
 * here and the sample documents, laid out into pages, with the caret and
 * the pointer mapped both ways.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ow_layout.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

/* half the size a character; the document's first font a little wider */
static int measure(void *userdata, const char *utf8, size_t length,
                   const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    unsigned font = OL_TEXT_FONT_OF(style->text_flags);
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * (font == OW_LAYOUT_FONT(0) ? style->font_size * 6 / 10
                                                           : style->font_size / 2);
    m->ascent = style->font_size * 4 / 5;
    m->descent = style->font_size / 5;
    return 1;
}

static owf_para *para(owf_doc *doc, owf_story *story, int heading, const char *text, int size)
{
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    owf_parafmt_init(&pf);
    pf.heading = heading;
    pf.space_after = 120;
    p = owf_story_add(story ? story : &doc->body, &pf);
    owf_charfmt_init(&cf);
    cf.size = size;
    if (heading) cf.flags |= OWF_BOLD;
    if (text) owf_para_add_text(p, &cf, text, strlen(text));
    return p;
}

/* every text position in a paragraph: its caret, then the position at the caret */
static void round_trip(const ow_layout *l, const owf_doc *doc, int pi, int *checked)
{
    const owf_para *p = &doc->body.paras[pi];
    int j;
    for (j = 0; j < p->nruns; ++j) {
        const owf_run *r = &p->runs[j];
        size_t k, len;
        if (r->kind != OWF_RUN_TEXT || !r->text) continue;
        len = strlen(r->text);
        for (k = 0; k <= len; ++k) {
            ow_position pos, back;
            ol_rect box;
            int page;
            if (k < len && ((unsigned char)r->text[k] & 0xc0u) == 0x80u) continue;
            pos.paragraph = pi;
            pos.run = j;
            pos.byte_offset = k;
            CHECK(ow_layout_caret(l, pos, &page, &box));
            CHECK(ow_layout_hit(l, page, box.x, box.y + box.height / 2, &back));
            CHECK(back.paragraph == pi);
            /* the end of a wrapped line and the start of the next are one place */
            CHECK((back.run == j && back.byte_offset == k) ||
                  (back.run == j && k > 0 && r->text[k - 1] == ' ' && back.byte_offset == k - 1) ||
                  (k == len && back.run == j + 1 && back.byte_offset == 0) ||
                  (k == 0 && back.run == j - 1));
            ++*checked;
        }
    }
}

static void test_built(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p, *cells[4];
    ow_layout *l;
    ol_document *od, *story;
    int i, checked = 0, broken_page = -1;
    size_t k;
    char text[200];
    ol_rect box0, box1;
    ow_position pos;

    owf_doc_font(doc, "Liberation Serif", OWF_FONT_SERIF);
    para(doc, NULL, 1, "A heading that stays with what follows", 360);
    for (i = 0; i < 14; ++i) {
        snprintf(text, sizeof text,
                 "Paragraph %d has enough words in it to fill more than one line of the page, "
                 "so that it wraps and the caret can be tested across the wrap.", i);
        p = para(doc, NULL, 0, text, 240);
        if (i == 3) p->fmt.align = OWF_ALIGN_JUSTIFY;
        if (i == 5) {
            owf_charfmt_init(&cf);
            cf.font = 0;
            cf.flags = OWF_ITALIC;
            owf_para_add_text(p, &cf, " In the first font.", 19);
        }
    }
    /* an empty paragraph, then one starting a new page */
    para(doc, NULL, 0, NULL, 240);
    p = para(doc, NULL, 0, "This paragraph starts a page of its own.", 240);
    p->fmt.page_break_before = 1;
    /* a 2 x 2 table */
    for (i = 0; i < 4; ++i) {
        snprintf(text, sizeof text, "cell %d", i);
        cells[i] = para(doc, NULL, 0, text, 240);
        cells[i]->table_id = 1;
        cells[i]->table_row = i / 2;
        cells[i]->table_col = i % 2;
        cells[i]->table_cols = 2;
    }
    /* a footer with the page number */
    owf_parafmt_init(&pf);
    p = owf_story_add(&doc->footer, &pf);
    owf_charfmt_init(&cf);
    owf_para_add_text(p, &cf, "Page ", 5);
    owf_para_add_special(p, &cf, OWF_RUN_FIELD, OWF_FIELD_PAGE);

    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (!l) {
        owf_doc_free(doc);
        return;
    }
    od = ow_layout_body(l);
    CHECK(od != NULL && ol_page_count(od) == ow_layout_page_count(l));
    CHECK(ow_layout_page_count(l) >= 2);
    /* the page is the document's, in units */
    CHECK(ow_layout_page(l)->width == 11906 * OL_UNITS_PER_TWIP);

    for (i = 0; i < doc->body.nparas; ++i) round_trip(l, doc, i, &checked);
    CHECK(checked > 1500);

    /* the forced break */
    pos.paragraph = 16;
    pos.run = 0;
    pos.byte_offset = 0;
    CHECK(ow_layout_caret(l, pos, &broken_page, &box0));
    CHECK(broken_page > 0 && box0.y == ow_layout_page(l)->margin_top);
    /* the empty paragraph takes the caret */
    pos.paragraph = 15;
    CHECK(ow_layout_caret(l, pos, &i, &box0));
    CHECK(box0.height > 0);
    /* the table: cells side by side, then the next row below */
    pos.paragraph = 17;
    CHECK(ow_layout_caret(l, pos, &i, &box0));
    pos.paragraph = 18;
    CHECK(ow_layout_caret(l, pos, &i, &box1));
    CHECK(box1.x > box0.x && box1.y == box0.y);
    pos.paragraph = 19;
    CHECK(ow_layout_caret(l, pos, &i, &box1));
    CHECK(box1.y > box0.y && box1.x == box0.x);

    /* the footer of the second page says 2 */
    story = ow_layout_story(l, OW_STORY_FOOTER, 1, NULL, NULL);
    CHECK(story != NULL);
    if (story) {
        int found = 0;
        for (k = 0; k < ol_display_count(story); ++k) {
            const ol_display_op *op = ol_display_get(story, k);
            const char *t;
            if (op->type != OL_OP_TEXT) continue;
            t = ol_node_text(ol_node_by_id(story, op->u.text.node_id));
            if (t && !strcmp(t, "2")) found = 1;
        }
        CHECK(found);
        ol_document_free(story);
    }
    CHECK(ow_layout_story(l, OW_STORY_HEADER, 0, NULL, NULL) == NULL);
    ow_layout_free(l);
    owf_doc_free(doc);
    printf("ow_layout: built document, %d caret positions\n", checked);
}

static void test_page_numbers(void)
{
    owf_page pg;
    char out[16];
    memset(&pg, 0, sizeof pg);
    ow_layout_page_number(&pg, 12, out, sizeof out);
    CHECK(!strcmp(out, "12"));
    pg.pagenum_style = OWF_PAGENUM_ROMAN_LOWER;
    ow_layout_page_number(&pg, 4, out, sizeof out);
    CHECK(!strcmp(out, "iv"));
    pg.pagenum_style = OWF_PAGENUM_ROMAN_UPPER;
    ow_layout_page_number(&pg, 1994, out, sizeof out);
    CHECK(!strcmp(out, "MCMXCIV"));
    pg.pagenum_style = OWF_PAGENUM_LETTER_UPPER;
    ow_layout_page_number(&pg, 27, out, sizeof out);
    CHECK(!strcmp(out, "AA"));
    pg.pagenum_style = OWF_PAGENUM_LETTER_LOWER;
    ow_layout_page_number(&pg, 3, out, sizeof out);
    CHECK(!strcmp(out, "c"));
}

static void test_sample(const char *path)
{
    owf_doc *doc = NULL;
    ow_layout *l;
    int i, checked = 0, r = owf_import_file(path, NULL, &doc, NULL, NULL);
    if (r != OWF_OK) {
        printf("ow_layout: %s not read (%s), skipped\n", path, owf_error_text(r));
        return;
    }
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (l) {
        CHECK(ow_layout_page_count(l) >= 1);
        for (i = 0; i < doc->body.nparas && i < 40; ++i) round_trip(l, doc, i, &checked);
        printf("ow_layout: %s, %d pages, %d caret positions\n", path, ow_layout_page_count(l), checked);
        ow_layout_free(l);
    }
    owf_doc_free(doc);
}

int main(int argc, char **argv)
{
    int i;
    test_page_numbers();
    test_built();
    for (i = 1; i < argc; ++i) test_sample(argv[i]);
    if (failures) {
        printf("ow_layout tests: %d failed\n", failures);
        return 1;
    }
    printf("ow_layout tests passed\n");
    return 0;
}
