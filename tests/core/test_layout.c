/*
 * OpenWrite's pages on OpenLayout (src/core/ow_layout.c): a document built
 * here and the sample documents, laid out into pages, with the caret and
 * the pointer mapped both ways.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "ow_layout.h"

#include <stdio.h>
#include <stdlib.h>
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
    ol_unit sx = -1, sy = -1;
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
    story = ow_layout_story(l, OW_STORY_FOOTER, 1, NULL, NULL, &sx, &sy);
    CHECK(story != NULL);
    if (story) {
        int found = 0;
        /* in the bottom margin, at the body's left */
        CHECK(sx == ow_layout_page(l)->margin_left);
        CHECK(sy > ow_layout_page(l)->height - ow_layout_page(l)->margin_bottom &&
              sy < ow_layout_page(l)->height - ow_layout_page(l)->margin_bottom / 2);
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
    CHECK(ow_layout_story(l, OW_STORY_HEADER, 0, NULL, NULL, NULL, NULL) == NULL);

    /* a table cell's text is its paragraph's run; the table is no run */
    {
        ol_text_position t;
        int para_of = -1, run_of = -2;
        pos.paragraph = 18;
        pos.run = 0;
        pos.byte_offset = 2;
        CHECK(ow_layout_to_text(l, pos, &t));
        CHECK(ow_layout_node_run(l, t.node_id, &para_of, &run_of) && para_of == 18 && run_of == 0);
        CHECK(!ow_layout_node_run(l, 1000000, &para_of, &run_of));
    }
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

/* The end of a paragraph's run on the page: its caret after its last byte. */
static int run_end(const ow_layout *l, const owf_doc *doc, int para, int run, int *page, ol_rect *box)
{
    ow_position pos;
    const owf_run *r = &doc->body.paras[para].runs[run];
    pos.paragraph = para;
    pos.run = run;
    pos.byte_offset = r->text ? strlen(r->text) : 0;
    return ow_layout_caret(l, pos, page, box);
}

/* A run's first op in the body (a tab's: its one). */
static const ol_display_op *run_op(const ow_layout *l, int para, int run)
{
    ow_position pos;
    ol_text_position t;
    const ol_document *od = ow_layout_body(l);
    size_t i;
    pos.paragraph = para;
    pos.run = run;
    pos.byte_offset = 0;
    if (!ow_layout_to_text(l, pos, &t)) return NULL;
    for (i = 0; i < ol_display_count(od); ++i) {
        const ol_display_op *op = ol_display_get(od, i);
        if (op->type == OL_OP_TEXT && op->u.text.node_id == t.node_id) return op;
    }
    return NULL;
}

/* tests/core/fixtures/contents.docx (make_contents_docx.py): a Word contents page. Each page number ends at
 * its right stop on its entry's line, after a tab that carries the stop's leader (2.0.3). */
static int same_name(const char *a, const char *b)
{
    return a && b ? !strcmp(a, b) : a == b;
}

static void contents_checks(const ow_layout *l, const owf_doc *doc)
{
    static const struct { int para, stop; unsigned leader; } entries[] = {
        { 1, 9016, OL_LEADER_DOT }, { 2, 9016, OL_LEADER_DOT }, { 3, 9016, OL_LEADER_DOT }, { 4, 8000, OL_LEADER_HYPHEN }
    };
    ol_unit margin = ow_layout_page(l)->margin_left;
    ol_rect a, b;
    int i, pa, pb;
    const ol_display_op *op;
    CHECK(doc->body.nparas == 10);
    if (doc->body.nparas != 10) return;
    /* 2.0.12: the entries' styles, and the bookmarks their links go to (Word's _GoBack left out, a cell's too) */
    CHECK(same_name(doc->body.paras[0].style, "TOCHeading") && same_name(doc->body.paras[1].style, "TOC1") &&
          same_name(doc->body.paras[2].style, "TOC2") && same_name(doc->body.paras[4].style, "TOC1") &&
          doc->body.paras[5].style == NULL);
    CHECK(same_name(doc->body.paras[6].bookmarks, "_Toc1") && same_name(doc->body.paras[7].bookmarks, "_Toc2") &&
          same_name(doc->body.paras[8].bookmarks, "_Toc3 _Ref9") && same_name(doc->body.paras[9].bookmarks, "_Toc4") &&
          doc->body.paras[9].table_id >= 0 && doc->body.paras[1].bookmarks == NULL);
    CHECK(doc->body.paras[1].runs[0].href && !strcmp(doc->body.paras[1].runs[0].href, "#_Toc1"));
    /* the model: the style's stop, a paragraph's own added to it, one cleared for another */
    CHECK(doc->body.paras[1].fmt.ntabs == 1 && doc->body.paras[1].fmt.tabs[0].kind == OWF_TAB_RIGHT &&
          doc->body.paras[1].fmt.tabs[0].leader == OWF_LEADER_DOT && doc->body.paras[1].fmt.tabs[0].position == 9016);
    CHECK(doc->body.paras[2].fmt.ntabs == 1 && doc->body.paras[2].fmt.tabs[0].position == 9016 - 220);
    CHECK(doc->body.paras[3].fmt.ntabs == 2 && doc->body.paras[3].fmt.tabs[0].kind == OWF_TAB_LEFT &&
          doc->body.paras[3].fmt.tabs[1].kind == OWF_TAB_RIGHT);
    CHECK(doc->body.paras[4].fmt.ntabs == 1 && doc->body.paras[4].fmt.tabs[0].leader == OWF_LEADER_HYPHEN &&
          doc->body.paras[4].fmt.tabs[0].position == 8000);
    for (i = 0; i < 4; ++i) {
        const owf_para *p = &doc->body.paras[entries[i].para];
        CHECK(p->nruns == 3 && p->runs[1].kind == OWF_RUN_TAB && p->runs[2].kind == OWF_RUN_TEXT);
        if (p->nruns != 3) continue;
        CHECK(run_end(l, doc, entries[i].para, 0, &pa, &a) && run_end(l, doc, entries[i].para, 2, &pb, &b));
        /* on the entry's line, ending at the stop */
        CHECK(pa == pb && a.y == b.y);
        CHECK(b.x == margin + entries[i].stop * OL_UNITS_PER_TWIP);
        op = run_op(l, entries[i].para, 1);
        CHECK(op && OL_TEXT_LEADER_OF(op->u.text.style.text_flags) == entries[i].leader &&
              op->u.text.bounds.x + op->u.text.bounds.width < b.x && op->u.text.bounds.width > 0);
    }
    /* "Centred" centred at 4513, "1234.50" with its point at 8000 */
    {
        const owf_para *p = &doc->body.paras[5];
        ow_position pos;
        CHECK(p->nruns == 5 && p->fmt.ntabs == 2 && p->fmt.tabs[1].kind == OWF_TAB_DECIMAL);
        if (p->nruns == 5) {
            CHECK(run_end(l, doc, 5, 2, &pa, &b));
            pos.paragraph = 5;
            pos.run = 2;
            pos.byte_offset = 0;
            CHECK(ow_layout_caret(l, pos, &pa, &a));
            CHECK(a.x + b.x == 2 * (margin + 4513 * OL_UNITS_PER_TWIP) ||
                  a.x + b.x + 1 == 2 * (margin + 4513 * OL_UNITS_PER_TWIP));
            pos.run = 4;
            pos.byte_offset = 4;                         /* "1234|.50" */
            CHECK(ow_layout_caret(l, pos, &pa, &a));
            CHECK(a.x == margin + 8000 * OL_UNITS_PER_TWIP);
            op = run_op(l, 5, 3);
            CHECK(op && OL_TEXT_LEADER_OF(op->u.text.style.text_flags) == OL_LEADER_UNDERSCORE);
        }
    }
}

/* An op of a type on a page: the n-th (from 0), or NULL. */
static const ol_display_op *nth_op(const ol_document *od, int page, ol_op_type type, int n, size_t *at)
{
    size_t first, count, i;
    if (!ol_display_page_range(od, page, &first, &count)) return NULL;
    for (i = first; i < first + count; ++i)
        if (ol_display_get(od, i)->type == type && n-- == 0) {
            if (at) *at = i;
            return ol_display_get(od, i);
        }
    return NULL;
}

/* The first text op on a page whose node's text starts with s. */
static const ol_display_op *text_on(const ol_document *od, int page, const char *s, size_t *at)
{
    const ol_display_op *op;
    int n;
    for (n = 0; (op = nth_op(od, page, OL_OP_TEXT, n, at)) != NULL; ++n) {
        const char *t = ol_node_text(ol_node_by_id(od, op->u.text.node_id));
        if (t && !strncmp(t + op->u.text.text_offset, s, strlen(s))) return op;
    }
    return NULL;
}

static int story_has(const owf_story *story, const char *s)
{
    int i, j;
    for (i = 0; i < story->nparas; ++i)
        for (j = 0; j < story->paras[i].nruns; ++j)
            if (story->paras[i].runs[j].text && strstr(story->paras[i].runs[j].text, s)) return 1;
    return 0;
}

/* tests/core/fixtures/cover.docx (make_cover_docx.py): a Word cover page, a picture the size of the page
 * behind the text and four text boxes in front of it, each where Word puts it; the body's lines where they
 * would be without them (2.0.10). */
static void cover_checks(const ow_layout *l, const owf_doc *doc)
{
    const ol_document *od = ow_layout_body(l);
    const ol_page *pg = ow_layout_page(l);
    const owf_frame *f = doc->frames;
    const ol_display_op *op, *pic;
    size_t at_pic = 0, at = 0;
    ol_unit tw = OL_UNITS_PER_TWIP, x, y;
    ow_position pos;
    int i, n, page;
    ol_rect box;

    /* the model: six frames, the cover's five in its first paragraph */
    CHECK(doc->nframes == 6);
    if (doc->nframes != 6) return;
    CHECK(doc->body.nparas == 5 && doc->body.paras[0].nruns == 5);
    for (i = 0; i < 5 && i < doc->body.paras[0].nruns; ++i)
        CHECK(doc->body.paras[0].runs[i].kind == OWF_RUN_FRAME && doc->body.paras[0].runs[i].image == i);
    CHECK(f[0].image >= 0 && f[0].image < doc->nimages && doc->images[f[0].image].length == 342 &&
          f[0].behind && f[0].x_from == OWF_FROM_PAGE && f[0].y_from == OWF_FROM_PAGE && f[0].x == 0 && f[0].y == 0 &&
          f[0].width == 11906 && f[0].height == 16838 && f[0].wrap == OWF_WRAP_NONE && !f[0].story.nparas);
    CHECK(f[1].image < 0 && !f[1].behind && f[1].story.nparas == 1 && f[1].x_from == OWF_FROM_MARGIN &&
          f[1].y_from == OWF_FROM_PAGE && f[1].y == 3600 && f[1].width == 9026 && f[1].height == 2160 &&
          f[1].inset[0] == 72 && f[1].inset[3] == 144 && !OWF_SHADE_SET(f[1].fill) && !OWF_SHADE_SET(f[1].line));
    if (f[1].story.nparas == 1 && f[1].story.paras[0].nruns == 1) {
        const owf_run *r = &f[1].story.paras[0].runs[0];
        CHECK(r->text && !strcmp(r->text, "Cover Title") && (r->fmt.flags & OWF_BOLD) && r->fmt.size == 720 &&
              r->fmt.colour == 0xFFFFFFUL);
    }
    CHECK(f[2].x_from == OWF_FROM_MARGIN && f[2].x_place == OWF_PLACE_CENTRE && f[2].story.nparas == 1);
    CHECK(f[3].x_from == OWF_FROM_PAGE && f[3].x_place == OWF_PLACE_END && f[3].y_place == OWF_PLACE_END &&
          f[3].fill == OWF_SHADE(0x1F3864) && f[3].line == OWF_SHADE(0xFFFFFF));
    CHECK(f[4].y_from == OWF_FROM_PARAGRAPH && f[4].y == 12960 && f[4].x_from == OWF_FROM_MARGIN);
    CHECK(f[5].image >= 0 && f[5].wrap == OWF_WRAP_SQUARE && f[5].x_from == OWF_FROM_MARGIN &&
          f[5].x_place == OWF_PLACE_END && f[5].y_from == OWF_FROM_PARAGRAPH && f[5].y == 0);
    /* the text boxes' text once, in the boxes: not their VML fallbacks' too, not in the body */
    CHECK(!story_has(&doc->body, "Cover Title") && !story_has(&doc->body, "October"));
    /* the header's picture from the header's own relationships */
    for (i = n = 0; i < doc->header.nparas; ++i)
        if (doc->header.paras[i].nruns && doc->header.paras[i].runs[0].kind == OWF_RUN_IMAGE &&
            doc->header.paras[i].runs[0].image >= 0 && doc->images[doc->header.paras[i].runs[0].image].length == 342)
            ++n;
    CHECK(n == 1);

    /* the layout: the body starts on page 2, as without the cover's frames */
    CHECK(ow_layout_page_count(l) == 2);
    CHECK(text_on(od, 1, "Introduction", NULL) != NULL && text_on(od, 0, "Introduction", NULL) == NULL);
    /* page 1: the picture first, at the page's corner, the page's size */
    pic = nth_op(od, 0, OL_OP_IMAGE, 0, &at_pic);
    CHECK(pic && pic->u.image.image_id == f[0].image && pic->u.image.bounds.x == 0 && pic->u.image.bounds.y == 0 &&
          pic->u.image.bounds.width == pg->width && pic->u.image.bounds.height == pg->height);
    CHECK(ol_display_page_range(od, 0, &at, NULL) || 1);
    CHECK(pic && at_pic == at);
    /* the title in front of it: after it, from the margin and 2.5 inches down, inside its insets */
    op = text_on(od, 0, "Cover Title", &at);
    CHECK(op && at > at_pic && op->u.text.bounds.x == pg->margin_left + 144 * tw &&
          op->u.text.bounds.y >= (3600 + 72) * tw && op->u.text.bounds.y < (3600 + 2160) * tw);
    /* the subtitle centred in a box centred between the margins */
    x = pg->margin_left + (9026 - 7200) / 2 * tw;
    op = text_on(od, 0, "A plan", NULL);
    CHECK(op && op->u.text.bounds.x > x + 144 * tw && op->u.text.bounds.y >= 6240 * tw);
    /* the filled box in the page's bottom right corner, outlined */
    op = nth_op(od, 0, OL_OP_FILL_RECT, 0, &at);
    for (n = 0; op && op->u.fill_rect.argb != 0xff1f3864u; ) op = nth_op(od, 0, OL_OP_FILL_RECT, ++n, &at);
    CHECK(op && at > at_pic && op->u.fill_rect.bounds.x == pg->width - 4320 * tw &&
          op->u.fill_rect.bounds.y == pg->height - 1440 * tw && op->u.fill_rect.bounds.width == 4320 * tw &&
          op->u.fill_rect.bounds.height == 1440 * tw);
    CHECK(text_on(od, 0, "Version", NULL) != NULL);
    /* the date from its paragraph's top, the top margin */
    op = text_on(od, 0, "25 October", NULL);
    CHECK(op && op->u.text.bounds.x == pg->margin_left + 144 * tw && op->u.text.bounds.y >= (1440 + 12960 + 72) * tw &&
          op->u.text.bounds.y < (1440 + 12960 + 720) * tw);
    /* page 2: the picture at the right margin, from its paragraph's top, behind nothing */
    pic = nth_op(od, 1, OL_OP_IMAGE, 0, NULL);
    for (n = 0; pic && pic->u.image.image_id != f[5].image; ) pic = nth_op(od, 1, OL_OP_IMAGE, ++n, NULL);
    pos.paragraph = 3;
    pos.run = 1;
    pos.byte_offset = 0;
    CHECK(pic && pic->u.image.bounds.x == pg->margin_left + (9026 - 1440) * tw &&
          ow_layout_caret(l, pos, &page, &box) && page == 1 && pic->u.image.bounds.y <= box.y &&
          pic->u.image.bounds.y + 200 * tw > box.y);
    /* 2.0.14: the text goes round it (wrapSquare): the lines beside it end an eighth of an inch before it (a
     * space may hang past), and fill up to there */
    if (pic) {
        size_t first = 0, count = 0, k;
        int beside = 0;
        ol_unit widest = 0;
        CHECK(ol_display_page_range(od, 1, &first, &count));
        for (k = first; k < first + count; ++k) {
            const ol_display_op *t = ol_display_get(od, k);
            if (t->type != OL_OP_TEXT) continue;
            if (t->u.text.bounds.y < pic->u.image.bounds.y + pic->u.image.bounds.height &&
                t->u.text.bounds.y + t->u.text.bounds.height > pic->u.image.bounds.y) {
                CHECK(t->u.text.bounds.x + t->u.text.bounds.width <= pic->u.image.bounds.x - 180 * tw + 120 * tw);
                if (t->u.text.bounds.x + t->u.text.bounds.width > widest)
                    widest = t->u.text.bounds.x + t->u.text.bounds.width;
                ++beside;
            }
        }
        CHECK(beside >= 6 && widest > pic->u.image.bounds.x - 180 * tw - 1440 * tw);
    }
    /* the pointer on the picture finds the body's text beside it; on the title, no box's text */
    if (pic) {
        CHECK(ow_layout_hit(l, 1, pic->u.image.bounds.x + 100 * tw, pic->u.image.bounds.y + 100 * tw, &pos) &&
              pos.paragraph == 3);
    }
    y = (3600 + 300) * tw;
    CHECK(!ow_layout_hit(l, 0, pg->margin_left + 400 * tw, y, &pos) || pos.paragraph <= 1);
}

/* tests/core/fixtures/cover-wpg.docx (make_cover_docx.py --wpg): the cover as Word writes it in a document
 * with a group anywhere (the plan's): every drawing's Choice requires wpg, the drawing namespaces are declared
 * on the elements, the picture is aligned to the page's right and placed 1536 twips above its paragraph, and a
 * filled band has no text (2.0.10). The lab's cover was blank: no Choice was read, and the VML fallbacks aren't. */
static void cover_wpg_checks(const ow_layout *l, const owf_doc *doc)
{
    const ol_document *od = ow_layout_body(l);
    const ol_page *pg = ow_layout_page(l);
    const owf_frame *f = doc->frames;
    const ol_display_op *op, *pic;
    size_t at_pic = 0, at = 0, first = 0, count = 0;
    ol_unit tw = OL_UNITS_PER_TWIP;
    ow_position pos;
    ol_rect box;
    int n, page;

    /* the picture, title, version box and band in the first paragraph; the subtitle and date in the second */
    CHECK(doc->nframes == 7);
    if (doc->nframes != 7) return;
    CHECK(doc->body.nparas == 6 && doc->body.paras[0].nruns == 4 && doc->body.paras[1].nruns == 2);
    CHECK(f[0].image >= 0 && f[0].behind && f[0].x_from == OWF_FROM_PAGE && f[0].x_place == OWF_PLACE_END &&
          f[0].y_from == OWF_FROM_PARAGRAPH && f[0].y_place == OWF_PLACE_AT && f[0].y == -1536 &&
          f[0].width == 11931 && f[0].height == 16968 && !f[0].story.nparas);
    CHECK(f[1].story.nparas == 1 && f[2].story.nparas == 1 && f[4].story.nparas == 1 && f[5].story.nparas == 1);
    CHECK(f[3].image < 0 && !f[3].story.nparas && f[3].fill == OWF_SHADE(0xC00000) && f[3].width == 11906 &&
          f[3].height == 360);
    CHECK(f[4].y_from == OWF_FROM_PARAGRAPH && f[4].y == 3600 && f[5].y_from == OWF_FROM_PARAGRAPH && f[5].y == 11520);
    CHECK(f[6].image >= 0 && f[6].wrap == OWF_WRAP_SQUARE);
    CHECK(!story_has(&doc->body, "Cover Title") && !story_has(&doc->body, "October"));

    CHECK(ow_layout_page_count(l) == 2);
    CHECK(text_on(od, 1, "Introduction", NULL) != NULL);
    /* the picture first on page 1: its right edge at the page's, its top 1536 twips above the top margin */
    pic = nth_op(od, 0, OL_OP_IMAGE, 0, &at_pic);
    CHECK(ol_display_page_range(od, 0, &first, &count) && pic && at_pic == first &&
          pic->u.image.image_id == f[0].image);
    CHECK(pic && pic->u.image.bounds.x == pg->width - 11931 * tw && pic->u.image.bounds.y == (1440 - 1536) * tw &&
          pic->u.image.bounds.width == 11931 * tw && pic->u.image.bounds.height == 16968 * tw);
    /* the text boxes and the band in front of it */
    op = text_on(od, 0, "Cover Title", &at);
    CHECK(op && at > at_pic && op->u.text.bounds.x == pg->margin_left + 144 * tw);
    CHECK(text_on(od, 0, "Version", NULL) && text_on(od, 0, "25 October", NULL));
    op = nth_op(od, 0, OL_OP_FILL_RECT, 0, &at);
    for (n = 0; op && op->u.fill_rect.argb != 0xffc00000u; ) op = nth_op(od, 0, OL_OP_FILL_RECT, ++n, &at);
    CHECK(op && at > at_pic && op->u.fill_rect.bounds.x == 0 && op->u.fill_rect.bounds.y == 0 &&
          op->u.fill_rect.bounds.width == pg->width && op->u.fill_rect.bounds.height == 360 * tw);
    /* the subtitle 3600 twips (and its inset) below its paragraph's top, the second paragraph's */
    pos.paragraph = 1;
    pos.run = 0;
    pos.byte_offset = 0;
    op = text_on(od, 0, "A plan", NULL);
    CHECK(op && ow_layout_caret(l, pos, &page, &box) && page == 0 && op->u.text.bounds.y >= box.y + (3600 + 72) * tw &&
          op->u.text.bounds.y < box.y + (3600 + 1080) * tw);
}

/* How many text ops on a page cross r, but those of node skip. */
static int crossing(const ol_document *od, int page, ol_rect r, ol_id skip)
{
    size_t first = 0, count = 0, k;
    int n = 0;
    if (!ol_display_page_range(od, page, &first, &count)) return 0;
    for (k = first; k < first + count; ++k) {
        const ol_display_op *t = ol_display_get(od, k);
        if (t->type != OL_OP_TEXT || t->u.text.node_id == skip) continue;
        if (t->u.text.bounds.x < r.x + r.width && t->u.text.bounds.x + t->u.text.bounds.width > r.x &&
            t->u.text.bounds.y < r.y + r.height && t->u.text.bounds.y + t->u.text.bounds.height > r.y)
            ++n;
    }
    return n;
}

/* tests/core/fixtures/wrap.docx (make_wrap_docx.py): text round frames (2.0.14). No body text under a picture
 * or the box, nor an eighth of an inch from a picture's sides; text on both sides of the centred one, none
 * beside the box. */
static void wrap_checks(const ow_layout *l, const owf_doc *doc)
{
    const ol_document *od = ow_layout_body(l);
    const ol_page *pg = ow_layout_page(l);
    ol_unit tw = OL_UNITS_PER_TWIP, gap = 180 * tw;
    int p, pictures = 0, boxes = 0, both = 0;

    CHECK(doc->nframes == 5);
    if (doc->nframes != 5) return;
    CHECK(doc->frames[0].wrap == OWF_WRAP_SQUARE && doc->frames[1].wrap == OWF_WRAP_SQUARE &&
          doc->frames[2].wrap == OWF_WRAP_TOP_BOTTOM && doc->frames[3].wrap == OWF_WRAP_TIGHT &&
          doc->frames[4].wrap == OWF_WRAP_SQUARE);
    for (p = 0; p < ow_layout_page_count(l); ++p) {
        size_t first = 0, count = 0, k;
        CHECK(ol_display_page_range(od, p, &first, &count));
        for (k = first; k < first + count; ++k) {
            const ol_display_op *op = ol_display_get(od, k);
            if (op->type == OL_OP_IMAGE) {
                ol_rect r = op->u.image.bounds, wide = r;
                size_t j;
                int left = 0, right = 0;
                wide.x -= gap;
                wide.width += 2 * gap;
                CHECK(crossing(od, p, r, 0) == 0);
                /* nothing starts in its gaps either (a word's trailing space may hang into one) */
                for (j = first; j < first + count; ++j) {
                    const ol_display_op *t = ol_display_get(od, j);
                    if (t->type != OL_OP_TEXT || t->u.text.bounds.y >= r.y + r.height ||
                        t->u.text.bounds.y + t->u.text.bounds.height <= r.y)
                        continue;
                    CHECK(t->u.text.bounds.x < wide.x || t->u.text.bounds.x >= wide.x + wide.width);
                    if (t->u.text.bounds.x + t->u.text.bounds.width <= r.x) ++left;
                    if (t->u.text.bounds.x >= r.x + r.width) ++right;
                }
                if (left && right) ++both;
                ++pictures;
            } else if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.argb == 0xff1f3864u) {
                /* the box: across the whole column, nothing beside it but its own text */
                const ol_display_op *own = text_on(od, p, "box", NULL);
                ol_rect band = op->u.fill_rect.bounds;
                band.x = pg->margin_left;
                band.width = pg->width - pg->margin_left - pg->margin_right;
                CHECK(own != NULL);
                CHECK(crossing(od, p, band, own ? own->u.text.node_id : 0) == 0);
                ++boxes;
            }
        }
    }
    /* text on both sides of the centred picture and of the page's own, 2 inches from the margin */
    CHECK(pictures == 4 && boxes == 1 && both == 2);
}

/* Where the cover's frames are on the page: the pictures and fills on pages 1 and 2, and the text boxes' first
 * words, in that order. */
typedef struct { int n; ol_rect at[16]; } frame_places;

static void frame_places_of(const ow_layout *l, frame_places *fp)
{
    static const char *const words[] = { "Cover Title", "A plan", "Version", "25 October", NULL };
    const ol_document *od = ow_layout_body(l);
    const ol_display_op *op;
    int page, k;
    fp->n = 0;
    for (page = 0; page < 2; ++page) {
        for (k = 0; fp->n < 16 && (op = nth_op(od, page, OL_OP_IMAGE, k, NULL)) != NULL; ++k)
            fp->at[fp->n++] = op->u.image.bounds;
        for (k = 0; fp->n < 16 && (op = nth_op(od, page, OL_OP_FILL_RECT, k, NULL)) != NULL; ++k)
            if (op->u.fill_rect.argb == 0xffc00000u || op->u.fill_rect.argb == 0xff1f3864u)
                fp->at[fp->n++] = op->u.fill_rect.bounds;
    }
    for (k = 0; words[k] && fp->n < 16; ++k) {
        op = text_on(od, 0, words[k], NULL);
        if (op) fp->at[fp->n++] = op->u.text.bounds;
        else memset(&fp->at[fp->n++], 0, sizeof fp->at[0]);
    }
}

static int same_places(const frame_places *a, const frame_places *b)
{
    int i;
    if (a->n != b->n) return 0;
    for (i = 0; i < a->n; ++i)
        if (a->at[i].x != b->at[i].x || a->at[i].y != b->at[i].y || a->at[i].width != b->at[i].width ||
            a->at[i].height != b->at[i].height) {
            printf("frame place %d: %ld,%ld -> %ld,%ld\n", i, (long)a->at[i].x, (long)a->at[i].y, (long)b->at[i].x,
                   (long)b->at[i].y);
            return 0;
        }
    return 1;
}

/* 2.0.11: typing on the cover (the lab: the caret in the paragraph that holds the picture and the title, "XYZ"
 * typed) moved the subtitle and the date, anchored in the next paragraph, up 44 pixels at 100%. The typed
 * text took the document's 11 points, not the 36 its paragraph's runs have, and a single spaced line with
 * text was measured by its font, an empty one at 1.2 times the size. Now every frame stays where it is,
 * through typing, undo and redo. */
static void test_cover_typing(const char *path)
{
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_layout *l;
    ow_selection sel;
    frame_places before, after;
    const owf_para *p;
    int i, text_run = -1;
    if (owf_import_file(path, NULL, &doc, NULL, NULL) != OWF_OK || !doc) {
        CHECK(!"cover-wpg.docx read");
        return;
    }
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (!l) { owf_doc_free(doc); return; }
    frame_places_of(l, &before);
    ow_layout_free(l);
    CHECK(before.n >= 7);
    ed = ow_editor_new(doc);
    CHECK(ed != NULL);
    if (!ed) { owf_doc_free(doc); return; }
    memset(&sel, 0, sizeof sel);
    ow_editor_set_selection(ed, &sel);
    CHECK(ow_editor_insert_utf8(ed, "XYZ", 3) == OWF_OK);
    p = &doc->body.paras[0];
    for (i = 0; i < p->nruns; ++i)
        if (p->runs[i].kind == OWF_RUN_TEXT && p->runs[i].text && !strcmp(p->runs[i].text, "XYZ")) text_run = i;
    /* the typed text in the size of the paragraph's runs, as Word types in a paragraph's mark's */
    CHECK(text_run >= 0 && p->runs[text_run].fmt.size == 720);
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (l) {
        frame_places_of(l, &after);
        CHECK(same_places(&before, &after));
        CHECK(text_on(ow_layout_body(l), 0, "XYZ", NULL) != NULL);
        ow_layout_free(l);
    }
    CHECK(ow_editor_undo(ed) == 1);
    l = ow_layout_new(doc, measure, NULL);
    if (l) {
        frame_places_of(l, &after);
        CHECK(same_places(&before, &after));
        ow_layout_free(l);
    }
    CHECK(ow_editor_redo(ed) == 1);
    l = ow_layout_new(doc, measure, NULL);
    if (l) {
        frame_places_of(l, &after);
        CHECK(same_places(&before, &after));
        ow_layout_free(l);
    }
    ow_editor_free(ed);
    owf_doc_free(doc);
}

/* 2.0.11: a document through DOCX and back keeps every paragraph's format and run's, and its page count. The
 * lab's 34-page plan came back with 27: a table cell's paragraph took the document's defaults alone, ours
 * once we had saved it; a heading's 0 before and after became our Heading style's; line spacing lost a
 * percent each time (259 read as 107, written as 256). */
static void test_round_trip(const char *path)
{
    owf_doc *a = NULL, *b = NULL;
    ow_layout *la, *lb;
    const char *out = "/tmp/openwrite-layout-round-trip.docx";
    int i, j, bad = 0;
    if (!strstr(path, ".docx") || owf_import_file(path, NULL, &a, NULL, NULL) != OWF_OK) return;
    CHECK(owf_export_file(a, out, "docx", NULL) == OWF_OK);
    CHECK(owf_import_file(out, NULL, &b, NULL, NULL) == OWF_OK);
    if (!b) { owf_doc_free(a); return; }
    CHECK(a->body.nparas == b->body.nparas);
    for (i = 0; i < a->body.nparas && i < b->body.nparas && bad < 5; ++i) {
        const owf_para *p = &a->body.paras[i], *q = &b->body.paras[i];
        const owf_parafmt *x = &p->fmt, *y = &q->fmt;
        int same = x->heading == y->heading && x->align == y->align && x->indent_left == y->indent_left &&
                   x->indent_right == y->indent_right && x->indent_first == y->indent_first &&
                   x->space_before == y->space_before && x->space_after == y->space_after &&
                   x->line_spacing == y->line_spacing && x->page_break_before == y->page_break_before &&
                   x->ntabs == y->ntabs && x->shading == y->shading && x->borders == y->borders &&
                   p->table_id == q->table_id && p->table_cols == q->table_cols && p->nruns == q->nruns &&
                   same_name(p->bookmarks, q->bookmarks) &&
                   (x->heading || same_name(p->style, q->style));   /* 2.0.12: a heading's style is its level */
        for (j = 0; same && j < p->nruns; ++j)
            same = p->runs[j].kind == q->runs[j].kind && p->runs[j].fmt.size == q->runs[j].fmt.size &&
                   p->runs[j].fmt.flags == q->runs[j].fmt.flags && p->runs[j].fmt.colour == q->runs[j].fmt.colour;
        if (!same) {
            printf("round trip %s: paragraph %d: before %d->%d after %d->%d line %d->%d runs %d->%d\n", path, i,
                   x->space_before, y->space_before, x->space_after, y->space_after, x->line_spacing,
                   y->line_spacing, p->nruns, q->nruns);
            ++bad;
        }
    }
    CHECK(bad == 0);
    la = ow_layout_new(a, measure, NULL);
    lb = ow_layout_new(b, measure, NULL);
    CHECK(la && lb && ow_layout_page_count(la) == ow_layout_page_count(lb));
    ow_layout_free(la);
    ow_layout_free(lb);
    owf_doc_free(a);
    owf_doc_free(b);
    remove(out);
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
        int lists = 0;
        CHECK(ow_layout_page_count(l) >= 1);
        for (i = 0; i < doc->body.nparas && i < 40; ++i) round_trip(l, doc, i, &checked);
        /* a list item's bullet hangs and its text, after the tab, starts at the paragraph's indent */
        for (i = 0; i < doc->body.nparas; ++i) {
            const owf_para *p = &doc->body.paras[i];
            ow_position pos;
            ol_rect box;
            int page;
            if (p->table_id >= 0 || p->nruns < 3 || p->fmt.indent_first >= 0 ||
                p->runs[0].kind != OWF_RUN_TEXT || p->runs[1].kind != OWF_RUN_TAB || p->runs[2].kind != OWF_RUN_TEXT)
                continue;
            pos.paragraph = i;
            pos.run = 2;
            pos.byte_offset = 0;
            CHECK(ow_layout_caret(l, pos, &page, &box));
            CHECK(box.x == ow_layout_page(l)->margin_left + p->fmt.indent_left * OL_UNITS_PER_TWIP);
            ++lists;
        }
        if (strstr(path, "Tata.docx")) CHECK(lists > 0);
        {   /* 2.0.13: laid out again with its display list's room made first, the same ops */
            const ol_document *a = ow_layout_body(l);
            ow_layout *r = ow_layout_new_reserved(doc, measure, NULL, ol_display_count(a) + 100);
            CHECK(r && ol_display_count(ow_layout_body(r)) == ol_display_count(a) &&
                  ow_layout_page_count(r) == ow_layout_page_count(l) &&
                  !memcmp(ol_display_get(ow_layout_body(r), 0), ol_display_get(a, 0),
                          ol_display_count(a) * sizeof(ol_display_op)));
            ow_layout_free(r);
        }
        if (strstr(path, "contents.docx")) contents_checks(l, doc);
        if (strstr(path, "cover.docx")) cover_checks(l, doc);
        if (strstr(path, "cover-wpg.docx")) cover_wpg_checks(l, doc);
        if (strstr(path, "wrap.docx")) wrap_checks(l, doc);
        printf("ow_layout: %s, %d pages, %d caret positions, %d list items\n", path, ow_layout_page_count(l),
               checked, lists);
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
    for (i = 1; i < argc; ++i)
        if (strstr(argv[i], "cover-wpg.docx")) test_cover_typing(argv[i]);
    for (i = 1; i < argc; ++i) test_round_trip(argv[i]);
    if (failures) {
        printf("ow_layout tests: %d failed\n", failures);
        return 1;
    }
    printf("ow_layout tests passed\n");
    return 0;
}
