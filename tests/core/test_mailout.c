/*
 * GFOpenWrite's gap 4.2 (core 1.11): label sheets of one's own (checked, read and written in millimetres), the
 * records to merge (as typed), envelopes with a return address (one Undo step, kept through ODT), and printing on
 * the printer's envelope feed. make mailout-test, make mailout-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"
#include "owf_internal.h"

static int checks, failures;

#define CHECK(cond, what)                                                                                  \
    do {                                                                                                    \
        ++checks;                                                                                           \
        if (!(cond)) {                                                                                      \
            ++failures;                                                                                     \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);                                            \
        }                                                                                                   \
    } while (0)

static const char csv[] = "Name,Town\nAnn,Leeds\nBob,York\nCy,Bath\nDi,Hull\n";

/* a story's text, paragraphs between '|' */
static const char *story_text(const owf_story *st)
{
    static char out[1024];
    size_t used = 0;
    int i, j;
    out[0] = 0;
    for (i = 0; i < st->nparas; ++i) {
        for (j = 0; j < st->paras[i].nruns; ++j) {
            const owf_run *r = &st->paras[i].runs[j];
            const char *t = r->kind == OWF_RUN_TEXT || r->kind == OWF_RUN_FIELD ? r->text : NULL;
            size_t n;
            if (!t) continue;
            n = strlen(t);
            if (used + n + 2 < sizeof out) { memcpy(out + used, t, n); used += n; }
        }
        if (i + 1 < st->nparas && used + 2 < sizeof out) out[used++] = '|';
        out[used] = 0;
    }
    return out;
}

static owf_doc *one_line(const char *text)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    owf_para *p;
    owf_charfmt_init(&cf);
    owf_parafmt_init(&pf);
    if (!doc || !(p = owf_story_add(&doc->body, &pf)) || owf_para_add_text(p, &cf, text, strlen(text)) != OWF_OK) {
        owf_doc_free(doc);
        return NULL;
    }
    return doc;
}

static void test_sheet_check(void)
{
    ow_label_sheet s;
    int i;
    for (i = 0; i < ow_label_sheet_count(); ++i)
        CHECK(ow_label_sheet_check(ow_label_sheet_at(i)) == OWF_OK, ow_label_sheet_at(i)->name);
    s = *ow_label_sheet_at(0);
    CHECK(ow_label_sheet_check(&s) == OWF_OK, "a stock sheet");
    s.across = s.label_width - 1;
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT, "labels on top of each other across");
    s = *ow_label_sheet_at(0);
    s.down = s.label_height - 1;
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT, "and down");
    s = *ow_label_sheet_at(0);
    s.left += 2000;
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT, "off the page's right");
    s = *ow_label_sheet_at(0);
    s.rows = 8;
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT, "off its bottom");
    s = *ow_label_sheet_at(0);
    s.columns = 1;
    s.across = 0;                                    /* one column: the pitch across doesn't matter */
    CHECK(ow_label_sheet_check(&s) == OWF_OK, "one column");
    s.columns = 0;
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT && ow_label_sheet_check(NULL) == OWF_ERR_FORMAT, "none");
    s = *ow_label_sheet_at(0);
    s.columns = s.rows = 999999;                     /* as the parse lets through: no int overflow (UBSan) */
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT, "a million by a million");
    s = *ow_label_sheet_at(0);
    s.columns = 2;
    s.rows = 1;
    s.across = 2147483000;                           /* (columns - 1) * across past 32 bits */
    s.left = 2147483000;
    CHECK(ow_label_sheet_check(&s) == OWF_ERR_FORMAT, "a pitch past 32 bits");
}

static void test_sheet_text(void)
{
    ow_label_sheet s, back;
    char text[200];
    int i, close = 1;
    CHECK(ow_label_sheet_parse("page 210 x 297 mm, 3 x 7 labels 63.5 x 38.1 mm, at 7.2, 15.4 mm, pitch 65.5 x 38.1 mm",
                               &s) == OWF_OK, "a sheet read");
    CHECK(!strcmp(s.name, "Custom") && s.page_width == 11906 && s.page_height == 16838 && s.columns == 3 &&
          s.rows == 7 && s.label_width == 3600 && s.label_height == 2160 && s.left == 408 && s.top == 873 &&
          s.across == 3713 && s.down == 2160, "its measures in twips");
    CHECK(ow_label_sheet_parse("148 210 2 4 70 50 4 5", &s) == OWF_OK && s.across == s.label_width &&
          s.down == s.label_height && s.page_width == 8391, "eight numbers: the pitch is the label");
    CHECK(ow_label_sheet_parse("210,297,2,4,99.1,67.7,4.7,13", &s) == OWF_OK && s.columns == 2 && s.rows == 4 &&
          s.page_width == 11906, "commas between the numbers");
    CHECK(ow_label_sheet_parse("210 297 2.5 4 70 50 4 5", &s) == OWF_ERR_FORMAT, "half a column");
    CHECK(ow_label_sheet_parse("210 297 2 4 70 50 4", &s) == OWF_ERR_FORMAT, "a number short");
    CHECK(ow_label_sheet_parse("210 297 4 4 70 50 4 5", &s) == OWF_ERR_FORMAT, "four 70 mm labels across 210 mm");
    CHECK(ow_label_sheet_parse("", &s) == OWF_ERR_FORMAT && ow_label_sheet_parse(NULL, &s) == OWF_ERR_FORMAT,
          "nothing");
    for (i = 0; i < ow_label_sheet_count(); ++i) {
        const ow_label_sheet *a = ow_label_sheet_at(i);
        if (ow_label_sheet_format(a, text, sizeof text) != OWF_OK || ow_label_sheet_parse(text, &back) != OWF_OK ||
            back.columns != a->columns || back.rows != a->rows || abs(back.page_width - a->page_width) > 3 ||
            abs(back.label_width - a->label_width) > 3 || abs(back.left - a->left) > 3 ||
            abs(back.top - a->top) > 3 || abs(back.across - a->across) > 3 || abs(back.down - a->down) > 3) {
            close = 0;
            printf("  %s: %s\n", a->name, text);
        }
    }
    CHECK(close, "every stock sheet written and read back, to 3 twips");
    ow_label_sheet_format(ow_label_sheet_at(0), text, sizeof text);
    CHECK(!strcmp(text, "page 210.01 x 297 mm, 3 x 7 labels 63.5 x 38.1 mm, at 7.2, 15.4 mm, pitch 65.51 x 38.1 mm"),
          text);
    CHECK(ow_label_sheet_format(ow_label_sheet_at(0), text, 10) == OWF_ERR_FORMAT, "too little room");
}

static void test_custom_labels(void)
{
    owf_doc *doc = one_line("To "), *out = NULL;
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    ow_label_sheet s;
    if (!ed) { CHECK(0, "editor"); owf_doc_free(doc); return; }
    CHECK(ow_label_sheet_parse("148 210 2 2 70 100 4 5 70 100", &s) == OWF_OK, "an A5 sheet of four");
    CHECK(ow_editor_merge_load_text(ed, "RAM:people.csv", csv, sizeof csv - 1) == OWF_OK, "the data source");
    CHECK(ow_editor_labels(ed, &s, -1, -1, &out) == OWF_OK && out && out->nframes == 4 && out->body.nparas == 1,
          "four labels on one A5 page");
    if (out) CHECK(out->page.width == 8391 && out->frames[3].x == s.left + s.across && out->frames[3].y == s.top + s.down,
                   "at their own pitch");
    owf_doc_free(out);
    out = NULL;
    s.across = 10;
    CHECK(ow_editor_labels(ed, &s, -1, -1, &out) == OWF_ERR_FORMAT && !out, "an overlapping sheet refused");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_range(void)
{
    int a = -9, b = -9;
    CHECK(ow_merge_range_parse("all", 4, &a, &b) == OWF_OK && a == 0 && b == 3, "all");
    CHECK(ow_merge_range_parse("", 4, &a, &b) == OWF_OK && a == 0 && b == 3, "empty: all");
    CHECK(ow_merge_range_parse("3", 4, &a, &b) == OWF_OK && a == 2 && b == 2, "one");
    CHECK(ow_merge_range_parse(" 2-3 ", 4, &a, &b) == OWF_OK && a == 1 && b == 2, "two to three");
    CHECK(ow_merge_range_parse("2 to 9", 4, &a, &b) == OWF_OK && a == 1 && b == 3, "to nine: held to four");
    CHECK(ow_merge_range_parse("3-", 4, &a, &b) == OWF_OK && a == 2 && b == 3, "three to the end");
    CHECK(ow_merge_range_parse("0", 4, &a, &b) == OWF_ERR_FORMAT, "nought");
    CHECK(ow_merge_range_parse("5", 4, &a, &b) == OWF_ERR_FORMAT, "past the end");
    CHECK(ow_merge_range_parse("3-2", 4, &a, &b) == OWF_ERR_FORMAT, "backwards");
    CHECK(ow_merge_range_parse("two", 4, &a, &b) == OWF_ERR_FORMAT, "words");
    CHECK(ow_merge_range_parse("2-3x", 4, &a, &b) == OWF_ERR_FORMAT, "something after");
    CHECK(ow_merge_range_parse("1", 0, &a, &b) == OWF_ERR_FORMAT, "no records");
}

/* merging some records (what Merge to Printer... prints): the range as typed into ow_editor_merge_document */
static void test_merge_some(void)
{
    owf_doc *doc = one_line("Dear "), *out = NULL;
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    int a, b;
    if (!ed) { CHECK(0, "editor"); owf_doc_free(doc); return; }
    ow_editor_merge_load_text(ed, "RAM:people.csv", csv, sizeof csv - 1);
    {
        ow_selection sel;
        memset(&sel, 0, sizeof sel);
        sel.anchor.byte_offset = 5;
        sel.focus = sel.anchor;
        ow_editor_set_selection(ed, &sel);
    }
    CHECK(ow_editor_insert_merge_field(ed, "Name") == OWF_OK, "a field");
    CHECK(ow_merge_range_parse("2-3", ow_editor_merge_records(ed), &a, &b) == OWF_OK &&
          ow_editor_merge_document(ed, a, b, &out) == OWF_OK && out, "records 2 and 3 merged");
    if (out) CHECK(!strcmp(story_text(&out->body), "Dear Bob|Dear Cy") && out->body.paras[1].fmt.page_break_before,
                   story_text(&out->body));
    owf_doc_free(out);
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_envelope(void)
{
    owf_doc *doc = one_line("Ann Smith"), *back = NULL;
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    const ow_envelope_size *dl = ow_envelope_at(0);
    int box, i, runs = 0;
    if (!ed || !dl) { CHECK(0, "editor"); ow_editor_free(ed); owf_doc_free(doc); return; }
    CHECK(ow_envelope_count() >= 4 && !ow_envelope_at(-1) && !ow_envelope_at(ow_envelope_count()), "the envelopes");
    for (i = 0; i < ow_envelope_count(); ++i) {
        const ow_envelope_size *e = ow_envelope_at(i);
        CHECK(e->width > e->height && e->address_left < e->width - 2835 && e->address_top < e->height - 1134, e->name);
    }
    CHECK(ow_editor_envelope(ed, dl, "J Bloggs\n2 Low Road\nYork") == OWF_OK, "a DL envelope with a return address");
    doc = ow_editor_document(ed);
    CHECK(doc->page.width == dl->width && doc->page.height == dl->height && doc->page.margin_left == dl->address_left &&
          doc->page.margin_top == dl->address_top && doc->page.margin_right == 567, "the page and the address's place");
    box = ow_editor_envelope_return_box(ed);
    CHECK(box == 0 && doc->nframes == 1, "the return address's box");
    if (box >= 0) {
        const owf_frame *f = &doc->frames[box];
        CHECK(!strcmp(story_text(&f->story), "J Bloggs|2 Low Road|York"), story_text(&f->story));
        CHECK(f->x == 567 && f->y == 567 && f->x_from == OWF_FROM_PAGE && f->story.paras[0].runs[0].fmt.size == 180,
              "10 mm in, 9 point");
        CHECK(f->y + f->height <= dl->address_top, "above the address");
    }
    for (i = 0; i < doc->body.paras[0].nruns; ++i) runs += doc->body.paras[0].runs[i].kind == OWF_RUN_FRAME;
    CHECK(runs == 1 && !strcmp(story_text(&doc->body), "Ann Smith"), "anchored in the first paragraph; the address stays");
    CHECK(ow_editor_envelope(ed, ow_envelope_at(2), "Jo Bloggs") == OWF_OK && doc->nframes == 1 &&
          !strcmp(story_text(&doc->frames[0].story), "Jo Bloggs") && doc->page.width == ow_envelope_at(2)->width,
          "again: the box replaced, not added");
    runs = 0;
    for (i = 0; i < doc->body.paras[0].nruns; ++i) runs += doc->body.paras[0].runs[i].kind == OWF_RUN_FRAME;
    CHECK(runs == 1, "one anchor still");
    CHECK(ow_editor_undo(ed) > 0, "Undo");
    doc = ow_editor_document(ed);
    CHECK(doc->nframes == 1 && !strcmp(story_text(&doc->frames[0].story), "J Bloggs|2 Low Road|York") &&
          doc->page.width == dl->width, "one step back: the DL and its return address");
    CHECK(owf_export_file(doc, "/tmp/openwrite-envelope.odt", "odt", NULL) == OWF_OK &&
          owf_import_file("/tmp/openwrite-envelope.odt", NULL, &back, NULL, NULL) == OWF_OK && back, "saved, read");
    if (back) {
        char got[160];
        snprintf(got, sizeof got, "ODT: %d frames, %d,%d, page %d", back->nframes,
                 back->nframes ? back->frames[0].x : -1, back->nframes ? back->frames[0].y : -1, back->page.width);
        CHECK(back->nframes == 1 && abs(back->frames[0].x - 567) <= 1 && abs(back->frames[0].y - 567) <= 1 &&
              abs(back->page.width - dl->width) <= 1, got);
    }
    owf_doc_free(back);
    CHECK(ow_editor_envelope(ed, dl, "") == OWF_OK && ow_editor_document(ed)->nframes == 0 &&
          ow_editor_envelope_return_box(ed) == -1, "no return address: the box goes");
    {
        ow_envelope_size bad = *dl;
        bad.address_left = bad.width;
        CHECK(ow_editor_envelope(ed, &bad, "x") == OWF_ERR_FORMAT && ow_editor_envelope(ed, NULL, "x") == OWF_ERR_FORMAT,
              "an address off the envelope");
    }
    ow_editor_free(ed);
    owf_doc_free(ow_editor_document(NULL));
    owf_doc_free(doc);
}

static void test_feed(void)
{
    owf_doc *doc = one_line("Ann Smith");
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    const ow_envelope_size *c6 = ow_envelope_at(2), *dl = ow_envelope_at(0);
    owf_page before;
    int dx, x0;
    if (!ed) { CHECK(0, "editor"); owf_doc_free(doc); return; }
    ow_editor_envelope(ed, c6, "J Bloggs");
    doc = ow_editor_document(ed);
    before = doc->page;
    x0 = doc->frames[0].x;
    dx = ow_doc_feed_shift(doc, 11906, OW_FEED_CENTRE);
    CHECK(dx == (11906 - c6->width) / 2 && doc->page.width == 11906 &&
          doc->page.margin_left == before.margin_left + dx &&
          doc->page.width - doc->page.margin_right == dx + before.width - before.margin_right,
          "a C6 in the middle of an A4 feed: the text where it was on the envelope");
    CHECK(doc->frames[0].x == x0 + dx, "the return address with it");
    ow_doc_feed_unshift(doc, 11906, before.width, dx);
    CHECK(!memcmp(&doc->page, &before, sizeof before) && doc->frames[0].x == x0, "put back exactly");
    dx = ow_doc_feed_shift(doc, 11906, OW_FEED_RIGHT);
    CHECK(dx == 11906 - c6->width && doc->page.margin_right == before.margin_right, "against the right edge");
    ow_doc_feed_unshift(doc, 11906, before.width, dx);
    dx = ow_doc_feed_shift(doc, 11906, OW_FEED_LEFT);
    CHECK(dx == 0 && doc->page.width == 11906 && doc->page.margin_right == before.margin_right + 11906 - c6->width,
          "against the left edge: the paper wider on the right");
    ow_doc_feed_unshift(doc, 11906, before.width, dx);
    CHECK(!memcmp(&doc->page, &before, sizeof before), "back");
    CHECK(ow_doc_feed_shift(doc, 11906, OW_FEED_AS_SET) == -1 && !memcmp(&doc->page, &before, sizeof before),
          "as set: untouched");
    ow_editor_envelope(ed, dl, "J Bloggs");
    before = doc->page;
    CHECK(ow_doc_feed_shift(doc, 11906, OW_FEED_CENTRE) == -1 && !memcmp(&doc->page, &before, sizeof before),
          "a DL is wider than A4: as it is");
    CHECK(ow_doc_feed_shift(NULL, 11906, OW_FEED_CENTRE) == -1, "no document");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

int main(void)
{
    test_sheet_check();
    test_sheet_text();
    test_custom_labels();
    test_range();
    test_merge_some();
    test_envelope();
    test_feed();
    printf("mailout: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
