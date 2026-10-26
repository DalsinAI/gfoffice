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
            ow_position pos = { 0, 0, 0, 0 }, back = { 0, 0, 0, 0 };
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
    ow_position pos = { 0, 0, 0, 0 };

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
    ow_position pos = { 0, 0, 0, 0 };
    const owf_run *r = &doc->body.paras[para].runs[run];
    pos.paragraph = para;
    pos.run = run;
    pos.byte_offset = r->text ? strlen(r->text) : 0;
    return ow_layout_caret(l, pos, page, box);
}

/* A run's first op in the body (a tab's: its one). */
static const ol_display_op *run_op(const ow_layout *l, int para, int run)
{
    ow_position pos = { 0, 0, 0, 0 };
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
    /* 2.0.21: round their headings' text */
    CHECK(same_name(doc->body.paras[6].bookmarks, "_Toc1@0-20") &&
          same_name(doc->body.paras[7].bookmarks, "_Toc2@0-24") &&
          same_name(doc->body.paras[8].bookmarks, "_Toc3@0-8 _Ref9@0-8") &&
          same_name(doc->body.paras[9].bookmarks, "_Toc4@0-8") &&
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
        ow_position pos = { 0, 0, 0, 0 };
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
    ow_position pos = { 0, 0, 0, 0 };
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
    /* 2.0.15: the pointer on the title box finds its text: text box 2 (the second frame) */
    y = (3600 + 300) * tw;
    CHECK(ow_layout_hit(l, 0, pg->margin_left + 400 * tw, y, &pos) && pos.box == 2 && pos.paragraph == 0);
}

/* 2.0.15: the caret in a text box: the pointer puts it in the cover's title, typing goes there and the box
 * shows it, a new paragraph in the box has its caret below the title's line; the body is as it was. */
static void test_box_caret(const char *path)
{
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_layout *l;
    ow_selection sel;
    ow_position pos = { 0, 0, 0, 0 }, empty = { 0, 0, 0, 0 };
    const ol_display_op *op;
    ol_rect box, title;
    int page = -1, body_paras;
    if (owf_import_file(path, NULL, &doc, NULL, NULL) != OWF_OK || !doc) {
        CHECK(!"cover.docx read");
        return;
    }
    body_paras = doc->body.nparas;
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (!l) { owf_doc_free(doc); return; }
    op = text_on(ow_layout_body(l), 0, "Cover Title", NULL);
    CHECK(op != NULL);
    if (!op) { ow_layout_free(l); owf_doc_free(doc); return; }
    title = op->u.text.bounds;
    /* just left of the title's first letter: the box's start */
    CHECK(ow_layout_hit(l, 0, title.x - OL_UNITS_PER_TWIP * 20, title.y + title.height / 2, &pos) && pos.box == 2 &&
          pos.paragraph == 0 && pos.byte_offset == 0);
    CHECK(ow_layout_caret(l, pos, &page, &box) && page == 0 && box.x == title.x && box.y == title.y);
    ow_layout_free(l);
    ed = ow_editor_new(doc);
    CHECK(ed != NULL);
    if (!ed) { owf_doc_free(doc); return; }
    memset(&sel, 0, sizeof sel);
    sel.anchor = sel.focus = pos;
    ow_editor_set_selection(ed, &sel);
    CHECK(ow_editor_insert_utf8(ed, "New ", 4) == OWF_OK);
    CHECK(doc->frames[1].story.paras[0].runs[0].text && !strncmp(doc->frames[1].story.paras[0].runs[0].text, "New Cover", 9));
    CHECK(doc->body.nparas == body_paras);
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (l) {
        op = text_on(ow_layout_body(l), 0, "New Cover", NULL);
        CHECK(op && op->u.text.bounds.x == title.x && op->u.text.bounds.y == title.y);
        /* the caret after "New " is on the title's line, further right */
        CHECK(ow_layout_caret(l, ow_editor_selection(ed).focus, &page, &box) && page == 0 && box.y == title.y &&
              box.x > title.x);
        ow_layout_free(l);
    }
    /* a new, empty paragraph at the title's end: its caret under the title, in the box */
    ow_editor_move_caret(ed, OW_MOVE_END, 0);
    CHECK(ow_editor_newline(ed) == OWF_OK && doc->frames[1].story.nparas == 2 && doc->body.nparas == body_paras);
    empty = ow_editor_selection(ed).focus;
    CHECK(empty.box == 2 && empty.paragraph == 1);
    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL);
    if (l) {
        /* placed from the frame's corner, where the frame went; the pointer there finds it again */
        CHECK(ow_layout_caret(l, empty, &page, &box) && page == 0 && box.y > title.y);
        CHECK(ow_layout_hit(l, 0, box.x + OL_UNITS_PER_TWIP * 10, box.y + box.height / 2, &pos) && pos.box == 2 &&
              pos.paragraph == 1);
        ow_layout_free(l);
    }
    CHECK(ow_editor_undo(ed) == 1 && ow_editor_undo(ed) == 1);
    CHECK(doc->frames[1].story.nparas == 1 && !strcmp(doc->frames[1].story.paras[0].runs[0].text, "Cover Title"));
    ow_editor_free(ed);
    owf_doc_free(doc);
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
    ow_position pos = { 0, 0, 0, 0 };
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

/* tests/core/fixtures/header-frames.docx (make_header_docx.py): frames in a header and a footer (2.0.16), on every
 * page where Word puts them: the logo from the page's top right corner, the DRAFT box from the margin and the
 * page's top, the footer's box at the right margin from its paragraph. */
static void header_checks(const ow_layout *l, const owf_doc *doc)
{
    const ol_page *pg = ow_layout_page(l);
    ol_unit tw = OL_UNITS_PER_TWIP;
    int p, pages = ow_layout_page_count(l);
    CHECK(doc->nframes == 3 && pages >= 3);
    for (p = 0; p < pages; ++p) {
        ol_document *h, *f;
        ol_unit hx = 0, hy = 0, fx = 0, fy = 0;
        size_t i;
        int logo = 0, draft = 0, box = 0, words = 0;
        h = ow_layout_story(l, OW_STORY_HEADER, p, NULL, NULL, &hx, &hy);
        f = ow_layout_story(l, OW_STORY_FOOTER, p, NULL, NULL, &fx, &fy);
        CHECK(h != NULL && f != NULL);
        if (!h || !f) { ol_document_free(h); ol_document_free(f); return; }
        for (i = 0; i < ol_display_count(h); ++i) {
            const ol_display_op *op = ol_display_get(h, i);
            if (op->type == OL_OP_IMAGE) {
                CHECK(op->u.image.bounds.x + hx == (11906 - 360 - 720) * tw && op->u.image.bounds.y + hy == 360 * tw &&
                      op->u.image.bounds.width == 720 * tw);
                ++logo;
            } else if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.argb == 0xffc00000u) {
                CHECK(op->u.fill_rect.bounds.x + hx == pg->margin_left && op->u.fill_rect.bounds.y + hy == 720 * tw &&
                      op->u.fill_rect.bounds.width == 2880 * tw && op->u.fill_rect.bounds.height == 432 * tw);
                ++draft;
            } else if (op->type == OL_OP_TEXT && op->u.text.text_offset == 0 &&
                       !strncmp(ol_node_text(ol_node_by_id(h, op->u.text.node_id)), "DRAFT", 5)) {
                CHECK(op->u.text.bounds.y + hy >= 720 * tw && op->u.text.bounds.y + hy < (720 + 432) * tw);
                ++words;
            }
        }
        for (i = 0; i < ol_display_count(f); ++i) {
            const ol_display_op *op = ol_display_get(f, i);
            if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.argb == 0xffddddddu) {
                CHECK(op->u.fill_rect.bounds.x + fx == pg->margin_left + (9026 - 2160) * tw &&
                      op->u.fill_rect.bounds.y + fy == fy && op->u.fill_rect.bounds.width == 2160 * tw);
                ++box;
            }
        }
        CHECK(logo == 1 && draft == 1 && box == 1 && words == 1);
        CHECK(text_on(f, 0, "Footer box", NULL) != NULL && text_on(h, 0, "The header", NULL) != NULL);
        ol_document_free(h);
        ol_document_free(f);
    }
    {   /* saved as DOCX and read again: the header's and the footer's frames kept, where they were */
        const char *out = "/tmp/openwrite-header-frames.docx";
        owf_doc *back = NULL;
        int k;
        CHECK(owf_export_file(doc, out, "docx", NULL) == OWF_OK);
        CHECK(owf_import_file(out, NULL, &back, NULL, NULL) == OWF_OK && back);
        if (back) {
            CHECK(back->nframes == 3);
            for (k = 0; k < 2; ++k) {
                const owf_story *sa = k ? &doc->footer : &doc->header, *sb = k ? &back->footer : &back->header;
                int i, j, na = 0, nb = 0, fa[4], fb[4];
                for (i = 0; i < sa->nparas; ++i)
                    for (j = 0; j < sa->paras[i].nruns; ++j)
                        if (sa->paras[i].runs[j].kind == OWF_RUN_FRAME && na < 4) fa[na++] = sa->paras[i].runs[j].image;
                for (i = 0; i < sb->nparas; ++i)
                    for (j = 0; j < sb->paras[i].nruns; ++j)
                        if (sb->paras[i].runs[j].kind == OWF_RUN_FRAME && nb < 4) fb[nb++] = sb->paras[i].runs[j].image;
                CHECK(na == (k ? 1 : 2) && na == nb);
                for (i = 0; i < na && i < nb; ++i) {
                    const owf_frame *x = &doc->frames[fa[i]], *y = &back->frames[fb[i]];
                    CHECK(x->x == y->x && x->y == y->y && x->x_from == y->x_from && x->y_from == y->y_from &&
                          x->x_place == y->x_place && x->width == y->width && x->height == y->height &&
                          x->fill == y->fill && (x->image >= 0) == (y->image >= 0) && x->story.nparas == y->story.nparas);
                }
            }
            owf_doc_free(back);
        }
        remove(out);
    }
}

/* tests/core/fixtures/inline-box.docx (make_inline_docx.py): text boxes in the line (2.0.17), kept: each where its
 * room in its line is, the text after it after it; written back to DOCX as text boxes in the line. */
static void inline_checks(const ow_layout *l, const owf_doc *doc)
{
    const ol_document *od = ow_layout_body(l);
    static const char *const texts[2] = { "First box", "Second box" };
    owf_doc *back = NULL;
    int k;
    CHECK(doc->nframes == 2 && doc->frames[0].in_line && doc->frames[1].in_line && doc->nimages == 1);
    if (doc->nframes != 2) return;
    for (k = 0; k < 2; ++k) {
        const ol_display_op *room = NULL, *text = text_on(od, 0, texts[k], NULL), *after;
        size_t first = 0, count = 0, i;
        CHECK(ol_display_page_range(od, 0, &first, &count));
        for (i = first; i < first + count; ++i) {
            const ol_display_op *op = ol_display_get(od, i);
            int box = -1, para = -1, run = -1;
            if (op->type == OL_OP_IMAGE && op->u.image.image_id == OW_LAYOUT_NO_IMAGE &&
                ow_layout_node_ref(l, op->u.image.node_id, &box, &para, &run) && para == k)
                room = op;
        }
        CHECK(room && text);
        if (!room || !text) continue;
        /* the box's text inside its room, inside its insets */
        CHECK(text->u.text.bounds.x >= room->u.image.bounds.x &&
              text->u.text.bounds.x < room->u.image.bounds.x + room->u.image.bounds.width &&
              text->u.text.bounds.y >= room->u.image.bounds.y &&
              text->u.text.bounds.y < room->u.image.bounds.y + room->u.image.bounds.height);
        /* the line's text after the box starts after its room */
        after = text_on(od, 0, k ? " after the box" : " and the text", NULL);
        if (!after) after = text_on(od, 0, k ? "after" : "and", NULL);
        CHECK(after && after->u.text.bounds.x >= room->u.image.bounds.x + room->u.image.bounds.width);
        /* the second box comes after the text before it */
        if (k) CHECK(room->u.image.bounds.x > ow_layout_page(l)->margin_left);
    }
    /* written back as text boxes in the line, their text kept */
    CHECK(owf_export_file(doc, "/tmp/openwrite-inline-box.docx", NULL, NULL) == OWF_OK);
    CHECK(owf_import_file("/tmp/openwrite-inline-box.docx", NULL, &back, NULL, NULL) == OWF_OK && back);
    if (back) {
        CHECK(back->nframes == 2 && back->frames[0].in_line && back->frames[1].in_line &&
              back->frames[1].story.nparas == 1 && back->frames[1].story.paras[0].nruns &&
              !strcmp(back->frames[1].story.paras[0].runs[0].text, "Second box"));
        owf_doc_free(back);
    }
    remove("/tmp/openwrite-inline-box.docx");
}

/* The distinct tops of a paragraph's lines (its first run's), in order: how many, into tops. */
static int line_tops(const ow_layout *l, int para, ol_unit *tops, int max)
{
    const ol_document *od = ow_layout_body(l);
    ow_position pos = { 0, 0, 0, 0 };
    ol_text_position t;
    size_t i;
    int n = 0;
    pos.paragraph = para;
    if (!ow_layout_to_text(l, pos, &t)) return 0;
    for (i = 0; i < ol_display_count(od); ++i) {
        const ol_display_op *op = ol_display_get(od, i);
        if (op->type != OL_OP_TEXT || op->u.text.node_id != t.node_id) continue;
        if (n && tops[n - 1] == op->u.text.bounds.y) continue;
        if (n < max) tops[n++] = op->u.text.bounds.y;
    }
    return n;
}

/* A paragraph's first text run's format. */
static owf_charfmt first_text_fmt(const owf_para *p)
{
    static const owf_charfmt none = { 0, -1, 0, OWF_COLOUR_AUTO };
    int i;
    for (i = 0; i < p->nruns; ++i)
        if (p->runs[i].kind == OWF_RUN_TEXT) return p->runs[i].fmt;
    return none;
}

/* tests/core/fixtures/table-styles.docx (make_tables_docx.py): Word's table styles (2.0.19), their pPr, rPr
 * and fills given to the cells, under the cells' paragraph styles and their own formats. */
static void table_style_checks(const ow_layout *l, const owf_doc *doc)
{
    const owf_para *p = doc->body.paras;
    int i;
    (void)l;
    CHECK(doc->body.nparas == 20);
    if (doc->body.nparas != 20) return;
    /* outside: the document's defaults */
    CHECK(p[0].fmt.space_after == 160 && p[0].fmt.line_spacing == 108);
    /* Table Grid: no space after, single lines */
    for (i = 1; i <= 4; ++i) {
        CHECK(p[i].table_id >= 0);
        CHECK(p[i].fmt.space_after == 0 && p[i].fmt.line_spacing == 100);
        CHECK(p[i].fmt.align == OWF_ALIGN_LEFT && p[i].cell_shading == 0);
    }
    CHECK(p[5].table_id < 0 && p[5].fmt.space_after == 160);
    /* Plan Table, on Table Grid: centred, 10 point, Table Grid's spacing */
    for (i = 6; i <= 15; ++i) {
        CHECK(p[i].table_id >= 0 && p[i].fmt.align == OWF_ALIGN_CENTRE);
        CHECK(p[i].fmt.line_spacing == 100);
        CHECK(first_text_fmt(&p[i]).size == 200);
    }
    /* its header row: bold, white on dark blue */
    for (i = 6; i <= 7; ++i) {
        owf_charfmt f = first_text_fmt(&p[i]);
        CHECK(p[i].table_row == 0 && p[i].fmt.space_after == 0);
        CHECK((f.flags & OWF_BOLD) && !(f.flags & OWF_ITALIC) && f.colour == 0xFFFFFFUL);
        CHECK(p[i].cell_shading == OWF_SHADE(0x1F3864));
    }
    /* banded rows (light blue, white, light blue), banded columns off (no red) */
    CHECK(p[8].cell_shading == OWF_SHADE(0xD9E2F3) && p[9].cell_shading == OWF_SHADE(0xD9E2F3));
    CHECK(p[10].cell_shading == 0 && p[11].cell_shading == 0);
    CHECK(p[12].cell_shading == OWF_SHADE(0xD9E2F3) && p[13].cell_shading == OWF_SHADE(0xD9E2F3));
    CHECK(!(first_text_fmt(&p[8]).flags & OWF_BOLD) && first_text_fmt(&p[8]).colour != 0xFFFFFFUL);
    /* the cell's own spacing, and a paragraph style's, over the table style's */
    CHECK(p[8].fmt.space_after == 0);
    CHECK(p[9].fmt.space_after == 120);
    CHECK(p[11].fmt.space_after == 80);
    /* the last row: italic, not banded */
    for (i = 14; i <= 15; ++i) {
        owf_charfmt f = first_text_fmt(&p[i]);
        CHECK(p[i].table_row == 4 && (f.flags & OWF_ITALIC) && !(f.flags & OWF_BOLD));
        CHECK(p[i].cell_shading == 0);
    }
    /* no style: the default table style (Normal Table), which gives nothing */
    for (i = 17; i <= 18; ++i) {
        CHECK(p[i].table_id >= 0 && p[i].fmt.space_after == 160 && p[i].fmt.line_spacing == 108);
        CHECK(p[i].fmt.align == OWF_ALIGN_LEFT && p[i].cell_shading == 0);
        CHECK(first_text_fmt(&p[i]).size == first_text_fmt(&p[0]).size && !(first_text_fmt(&p[i]).flags & OWF_BOLD));
    }
    CHECK(p[19].table_id < 0 && p[19].fmt.space_after == 160);
    /* written back to DOCX (as the cells' own formats) and read again: the same. (ODT doesn't keep a cell's
     * paragraph format yet, with a table style or without.) */
    {
        static const char *const paths[1] = { "/tmp/openwrite-table-styles.docx" };
        int k;
        for (k = 0; k < 1; ++k) {
            owf_doc *back = NULL;
            CHECK(owf_export_file(doc, paths[k], NULL, NULL) == OWF_OK);
            CHECK(owf_import_file(paths[k], NULL, &back, NULL, NULL) == OWF_OK && back);
            if (back && back->body.nparas == 20) {
                const owf_para *q = back->body.paras;
                CHECK(q[1].fmt.space_after == 0 && q[1].fmt.line_spacing == 100);
                CHECK(q[6].fmt.align == OWF_ALIGN_CENTRE && q[6].cell_shading == OWF_SHADE(0x1F3864));
                CHECK((first_text_fmt(&q[6]).flags & OWF_BOLD) && first_text_fmt(&q[6]).colour == 0xFFFFFFUL);
                CHECK(q[8].cell_shading == OWF_SHADE(0xD9E2F3) && q[10].cell_shading == 0);
                CHECK(q[9].fmt.space_after == 120 && q[11].fmt.space_after == 80);
                CHECK(first_text_fmt(&q[14]).flags & OWF_ITALIC);
                CHECK(q[17].fmt.space_after == 160);
            } else {
                CHECK(back && back->body.nparas == 20);
            }
            if (back) owf_doc_free(back);
            remove(paths[k]);
        }
    }
}

/* tests/core/fixtures/line-rules.docx (make_lines_docx.py): Word's exact and at least line spacing (2.0.18), read,
 * laid out (OpenLayout 0.7's exact lines) and written back to DOCX and ODT. */
static void line_checks(const ow_layout *l, const owf_doc *doc)
{
    static const char *const paths[2] = { "/tmp/openwrite-line-rules.docx", "/tmp/openwrite-line-rules.odt" };
    ol_unit tops[16], tw = OL_UNITS_PER_TWIP;
    int n, i, k;
    CHECK(doc->body.nparas == 4);
    if (doc->body.nparas != 4) return;
    CHECK(doc->body.paras[0].fmt.line_spacing == 150);
    CHECK(doc->body.paras[1].fmt.line_spacing == OWF_LINE_EXACT(240));
    CHECK(doc->body.paras[2].fmt.line_spacing == OWF_LINE_AT_LEAST(600));
    CHECK(doc->body.paras[3].fmt.line_spacing == OWF_LINE_AT_LEAST(120));
    /* exactly 12 points apart, its 16 point text closer than it is tall */
    n = line_tops(l, 1, tops, 16);
    CHECK(n >= 3);
    for (i = 1; i < n; ++i) CHECK(tops[i] - tops[i - 1] == 240 * tw);
    /* at least 30 points */
    n = line_tops(l, 2, tops, 16);
    CHECK(n >= 3);
    for (i = 1; i < n; ++i) CHECK(tops[i] - tops[i - 1] == 600 * tw);
    /* at least 6 points: as its 11 point text, more than 6 */
    n = line_tops(l, 3, tops, 16);
    CHECK(n >= 3);
    for (i = 1; i < n; ++i) CHECK(tops[i] - tops[i - 1] > 120 * tw && tops[i] - tops[i - 1] < 360 * tw);
    /* written back and read again: the same */
    for (k = 0; k < 2; ++k) {
        owf_doc *back = NULL;
        CHECK(owf_export_file(doc, paths[k], NULL, NULL) == OWF_OK);
        CHECK(owf_import_file(paths[k], NULL, &back, NULL, NULL) == OWF_OK && back);
        if (back) {
            CHECK(back->body.nparas == 4);
            if (back->body.nparas == 4) {
                CHECK(back->body.paras[0].fmt.line_spacing == 150);
                CHECK(back->body.paras[1].fmt.line_spacing == OWF_LINE_EXACT(240));
                CHECK(back->body.paras[2].fmt.line_spacing == OWF_LINE_AT_LEAST(600));
                CHECK(back->body.paras[3].fmt.line_spacing == OWF_LINE_AT_LEAST(120));
            }
            owf_doc_free(back);
        }
        remove(paths[k]);
    }
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

/* The frames a story's runs hold, in order: their indices into frames, how many. */
static int story_frames(const owf_story *st, int *frames, int max)
{
    int i, j, n = 0;
    for (i = 0; i < st->nparas; ++i)
        for (j = 0; j < st->paras[i].nruns; ++j)
            if (st->paras[i].runs[j].kind == OWF_RUN_FRAME && n < max) frames[n++] = st->paras[i].runs[j].image;
    return n;
}

static int near_twips(int a, int b) { return a - b <= 2 && b - a <= 2; }

/* 2.0.20: a document's frames through ODT and back (ODT reads them back as frames): every story's frames, in
 * order, the same size, in the same place, wrapped and filled as before, with the same picture or text. */
static void test_odt_frames(const char *path)
{
    owf_doc *a = NULL, *b = NULL;
    const char *out = "/tmp/openwrite-layout-frames.odt";
    int k, i, fa[32], fb[32], bad = 0;
    if (!strstr(path, ".docx") || owf_import_file(path, NULL, &a, NULL, NULL) != OWF_OK) return;
    if (!a->nframes) { owf_doc_free(a); return; }
    CHECK(owf_export_file(a, out, "odt", NULL) == OWF_OK);
    CHECK(owf_import_file(out, NULL, &b, NULL, NULL) == OWF_OK && b);
    if (!b) { owf_doc_free(a); return; }
    CHECK(a->nframes == b->nframes);
    for (k = 0; k < 3; ++k) {
        const owf_story *sa = k == 0 ? &a->body : k == 1 ? &a->header : &a->footer;
        const owf_story *sb = k == 0 ? &b->body : k == 1 ? &b->header : &b->footer;
        int na = story_frames(sa, fa, 32), nb = story_frames(sb, fb, 32);
        CHECK(na == nb);
        for (i = 0; i < na && i < nb; ++i) {
            const owf_frame *x = &a->frames[fa[i]], *y = &b->frames[fb[i]];
            int same = near_twips(x->width, y->width) && near_twips(x->height, y->height) &&
                       near_twips(x->x, y->x) && near_twips(x->y, y->y) && x->x_from == y->x_from &&
                       x->y_from == y->y_from && x->x_place == y->x_place && x->y_place == y->y_place &&
                       x->behind == y->behind && x->wrap == y->wrap && x->in_line == y->in_line &&
                       x->fill == y->fill && x->line == y->line && (x->image >= 0) == (y->image >= 0) &&
                       x->story.nparas == y->story.nparas;
            if (same && x->story.nparas) {
                const owf_para *pa = &x->story.paras[0], *pb = &y->story.paras[0];
                same = near_twips(x->inset[0], y->inset[0]) && near_twips(x->inset[3], y->inset[3]) &&
                       pa->nruns && pb->nruns && pa->runs[0].text && pb->runs[0].text &&
                       !strcmp(pa->runs[0].text, pb->runs[0].text);
            }
            if (!same) {
                printf("odt frames %s: story %d frame %d: %dx%d at %d/%d/%d, %d/%d/%d wrap %d behind %d, back "
                       "%dx%d at %d/%d/%d, %d/%d/%d wrap %d behind %d\n", path, k, i, x->width, x->height,
                       x->x_from, x->x_place, x->x, x->y_from, x->y_place, x->y, x->wrap, x->behind, y->width,
                       y->height, y->x_from, y->x_place, y->x, y->y_from, y->y_place, y->y, y->wrap, y->behind);
                ++bad;
            }
        }
    }
    CHECK(bad == 0);
    owf_doc_free(a);
    owf_doc_free(b);
    remove(out);
}

/* tests/core/fixtures/frames.odt (make_frames_odt.py): frames as Writer writes them (2.0.20), their places
 * and looks from their styles and the common styles those are based on. */
static void frames_odt_checks(const ow_layout *l, const owf_doc *doc)
{
    const owf_frame *f;
    int body[8], header[4], n;
    (void)l;
    CHECK(doc->nframes == 4);
    n = story_frames(&doc->body, body, 8);
    CHECK(n == 3);
    CHECK(story_frames(&doc->header, header, 4) == 1);
    if (n != 3 || doc->nframes != 4 || doc->body.nparas < 3) return;
    /* the page's picture: with the first paragraph, at its start, 1 inch from the page's corner, under the text */
    CHECK(doc->body.paras[0].runs[0].kind == OWF_RUN_FRAME && doc->body.paras[0].runs[0].image == body[0]);
    f = &doc->frames[body[0]];
    CHECK(f->image >= 0 && !f->story.nparas && f->width == 2880 && f->height == 4032);
    CHECK(f->x_from == OWF_FROM_PAGE && f->x_place == OWF_PLACE_AT && f->x == 1440);
    CHECK(f->y_from == OWF_FROM_PAGE && f->y_place == OWF_PLACE_AT && f->y == 1440);
    CHECK(f->wrap == OWF_WRAP_NONE && f->behind);
    /* the text box: in the first paragraph after its text, centred in the paragraph, Frame's wrap, border and
     * padding, its own fill, as high as its text box's least */
    f = &doc->frames[body[1]];
    CHECK(f->image < 0 && f->story.nparas == 2 && f->width == 4320 && f->height == 1080);
    CHECK(f->x_from == OWF_FROM_PARAGRAPH && f->x_place == OWF_PLACE_CENTRE);
    CHECK(f->y_from == OWF_FROM_PARAGRAPH && f->y_place == OWF_PLACE_START);
    CHECK(f->wrap == OWF_WRAP_SQUARE && !f->behind && !f->in_line);
    CHECK(f->fill == OWF_SHADE(0xFFF2CC) && f->line == OWF_SHADE(0));
    CHECK(near_twips(f->inset[0], 85) && near_twips(f->inset[3], 85));
    CHECK(f->story.paras[0].nruns && !strcmp(f->story.paras[0].runs[0].text, "Text in the box"));
    /* the picture at the margin's right, wrapped round its outline: the JPEG after the SVG */
    f = &doc->frames[body[2]];
    CHECK(f->image >= 0 && !strcmp(doc->images[f->image].mime, "image/jpeg"));
    CHECK(f->x_from == OWF_FROM_MARGIN && f->x_place == OWF_PLACE_END && f->wrap == OWF_WRAP_TIGHT);
    CHECK(f->width == 1440 && f->height == 2016);
    /* the picture as a character stays in the line */
    CHECK(doc->body.paras[2].nruns >= 2 && doc->body.paras[2].runs[1].kind == OWF_RUN_IMAGE);
    /* the header's box, 2 inches from the page's left, from its paragraph down, filled and outlined */
    f = &doc->frames[header[0]];
    CHECK(f->story.nparas == 1 && f->x_from == OWF_FROM_PAGE && f->x == 2880 && f->y_from == OWF_FROM_PARAGRAPH);
    CHECK(f->width == 2880 && f->height == 432 && f->fill == OWF_SHADE(0xC00000) && f->wrap == OWF_WRAP_SQUARE);
}

/* tests/core/fixtures/bookmarks.docx (make_bookmarks_docx.py): bookmarks at their places in the text (2.0.21),
 * from DOCX and through DOCX and ODT. */
static void bookmark_checks(const ow_layout *l, const owf_doc *doc)
{
    static const struct { int para; const char *marks; } want[] = {
        { 0, "_Ref1@11-16 Point@21" },   /* round the bold word; empty, inside a run */
        { 1, "_Toc5@0-7" },              /* round a heading's text */
        { 2, "Inner@8-12" },             /* inside a link */
        { 3, "Span@5-16" },              /* ending in the next paragraph: at this one's end */
        { 4, NULL },
        { 5, "Between@0-34" },           /* begun between the paragraphs */
        { 6, "Accent@5-12" },            /* after a two-byte character */
        { 8, "Cell@15-19" }              /* in a cell's second paragraph */
    };
    static const char *const paths[2] = { "/tmp/openwrite-bookmarks.docx", "/tmp/openwrite-bookmarks.odt" };
    size_t i;
    int k;
    (void)l;
    CHECK(doc->body.nparas == 10);
    if (doc->body.nparas != 10) return;
    for (i = 0; i < sizeof want / sizeof want[0]; ++i)
        CHECK(same_name(doc->body.paras[want[i].para].bookmarks, want[i].marks));
    for (k = 0; k < 2; ++k) {
        owf_doc *back = NULL;
        CHECK(owf_export_file(doc, paths[k], NULL, NULL) == OWF_OK);
        CHECK(owf_import_file(paths[k], NULL, &back, NULL, NULL) == OWF_OK && back);
        if (back && back->body.nparas == 10)
            for (i = 0; i < sizeof want / sizeof want[0]; ++i)
                CHECK(same_name(back->body.paras[want[i].para].bookmarks, want[i].marks));
        else
            CHECK(back && back->body.nparas == 10);
        if (back) owf_doc_free(back);
        remove(paths[k]);
    }
}

/* 2.0.21: the bookmarks keep their places through typing, Return, Backspace and undo. */
static void test_bookmark_edits(const char *path)
{
    owf_doc *doc = NULL;
    ow_editor *ed;
    ow_selection sel;
    if (owf_import_file(path, NULL, &doc, NULL, NULL) != OWF_OK || !doc) {
        CHECK(!"bookmarks.docx read");
        return;
    }
    ed = ow_editor_new(doc);
    CHECK(ed != NULL);
    if (!ed) { owf_doc_free(doc); return; }
    CHECK(same_name(doc->body.paras[0].bookmarks, "_Ref1@11-16 Point@21"));
    /* typed before them: moved on */
    memset(&sel, 0, sizeof sel);
    ow_editor_set_selection(ed, &sel);
    CHECK(ow_editor_insert_utf8(ed, "Our ", 4) == OWF_OK);
    CHECK(same_name(doc->body.paras[0].bookmarks, "_Ref1@15-20 Point@25"));
    /* Return just before the bold word: they go with the text */
    CHECK(doc->body.paras[0].nruns >= 2 && !strcmp(doc->body.paras[0].runs[1].text, "first"));
    sel.anchor.paragraph = sel.focus.paragraph = 0;
    sel.anchor.run = sel.focus.run = 1;
    sel.anchor.byte_offset = sel.focus.byte_offset = 0;
    ow_editor_set_selection(ed, &sel);
    CHECK(ow_editor_newline(ed) == OWF_OK);
    CHECK(doc->body.paras[0].bookmarks == NULL);
    CHECK(same_name(doc->body.paras[1].bookmarks, "_Ref1@0-5 Point@10"));
    /* Backspace at the new paragraph's start: back where they were */
    CHECK(ow_editor_backspace(ed) == OWF_OK);
    CHECK(same_name(doc->body.paras[0].bookmarks, "_Ref1@15-20 Point@25"));
    /* two characters of the bold word deleted: the range is shorter, the point after it moves back */
    {
        const owf_para *p = &doc->body.paras[0];
        int r;
        for (r = 0; r < p->nruns && !(p->runs[r].kind == OWF_RUN_TEXT && !strcmp(p->runs[r].text, "first")); ++r)
            ;
        CHECK(r < p->nruns);
        sel.anchor.run = sel.focus.run = r;
        sel.anchor.byte_offset = sel.focus.byte_offset = 5;
        ow_editor_set_selection(ed, &sel);
    }
    CHECK(ow_editor_backspace(ed) == OWF_OK && ow_editor_backspace(ed) == OWF_OK);
    CHECK(same_name(doc->body.paras[0].bookmarks, "_Ref1@15-18 Point@23"));
    /* undone: as they were read */
    while (ow_editor_undo(ed) == 1)
        ;
    CHECK(same_name(doc->body.paras[0].bookmarks, "_Ref1@11-16 Point@21"));
    ow_editor_free(ed);
    owf_doc_free(doc);
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
            ow_position pos = { 0, 0, 0, 0 };
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
        if (strstr(path, "header-frames.docx")) header_checks(l, doc);
        if (strstr(path, "inline-box.docx")) inline_checks(l, doc);
        if (strstr(path, "line-rules.docx")) line_checks(l, doc);
        if (strstr(path, "table-styles.docx")) table_style_checks(l, doc);
        if (strstr(path, "frames.odt")) frames_odt_checks(l, doc);
        if (strstr(path, "bookmarks.docx")) bookmark_checks(l, doc);
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
    for (i = 1; i < argc; ++i)
        if (strstr(argv[i], "cover.docx") && !strstr(argv[i], "wpg")) test_box_caret(argv[i]);
    for (i = 1; i < argc; ++i) test_round_trip(argv[i]);
    for (i = 1; i < argc; ++i) test_odt_frames(argv[i]);
    for (i = 1; i < argc; ++i)
        if (strstr(argv[i], "bookmarks.docx")) test_bookmark_edits(argv[i]);
    if (failures) {
        printf("ow_layout tests: %d failed\n", failures);
        return 1;
    }
    printf("ow_layout tests passed\n");
    return 0;
}
