/*
 * GFOpenWrite's labels (core 1.9): the stock sheets fit their pages; a sheet of labels from the mail merge (one
 * record a label, text boxes at the sheet's pitch, a new page when one is full, a range of records) and from a
 * document with no data source (one page of it); what is refused; the labels through ODT and DOCX.
 * make labels-test, make labels-check (ASan).
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

static const char csv[] = "Name,Town\nAnn,Leeds\nBob,York\nCy,Bath\n";

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
            const char *t = r->kind == OWF_RUN_TEXT ? r->text : r->kind == OWF_RUN_FIELD ? r->text : NULL;
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

static owf_doc *two_lines(void)
{
    owf_doc *doc = owf_doc_new();
    owf_parafmt pf;
    owf_charfmt cf;
    int i;
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < 2; ++i) {
        owf_para *p;
        owf_parafmt_init(&pf);
        if (!(p = owf_story_add(&doc->body, &pf)) || owf_para_add_text(p, &cf, i ? "Town: " : "To ", i ? 6 : 3) != OWF_OK) {
            owf_doc_free(doc);
            return NULL;
        }
    }
    return doc;
}

static void caret_end(ow_editor *ed, int paragraph)
{
    owf_doc *doc = ow_editor_document(ed);
    owf_para *p = &doc->body.paras[paragraph];
    ow_selection sel;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = paragraph;
    sel.anchor.run = p->nruns - 1;
    sel.anchor.byte_offset = p->runs[p->nruns - 1].text ? strlen(p->runs[p->nruns - 1].text) : 0;
    sel.focus = sel.anchor;
    ow_editor_set_selection(ed, &sel);
}

static ow_editor *address(owf_doc **doc, int with_source)
{
    ow_editor *ed;
    *doc = two_lines();
    ed = *doc ? ow_editor_new(*doc) : NULL;
    if (!ed || !with_source) return ed;
    if (ow_editor_merge_load_text(ed, "RAM:people.csv", csv, sizeof csv - 1) != OWF_OK) return ed;
    caret_end(ed, 0);
    ow_editor_insert_merge_field(ed, "Name");
    caret_end(ed, 1);
    ow_editor_insert_merge_field(ed, "Town");
    return ed;
}

static void test_sheets(void)
{
    int i, n = ow_label_sheet_count();
    CHECK(n >= 6 && !ow_label_sheet_at(-1) && !ow_label_sheet_at(n), "the sheets");
    for (i = 0; i < n; ++i) {
        const ow_label_sheet *s = ow_label_sheet_at(i);
        char msg[120];
        snprintf(msg, sizeof msg, "%s fits its page", s ? s->name : "?");
        CHECK(s && s->name && s->across >= s->label_width && s->down >= s->label_height &&
              s->left + (s->columns - 1) * s->across + s->label_width <= s->page_width &&
              s->top + (s->rows - 1) * s->down + s->label_height <= s->page_height, msg);
    }
}

static void test_merged(void)
{
    owf_doc *doc = NULL, *out = NULL, *back = NULL;
    ow_editor *ed = address(&doc, 1);
    ow_label_sheet small;
    const ow_label_sheet *s = ow_label_sheet_at(0);
    int i;
    if (!ed || !s) { CHECK(0, "editor"); ow_editor_free(ed); owf_doc_free(doc); return; }
    CHECK(ow_editor_labels(ed, s, -1, -1, &out) == OWF_OK && out, "labels made");
    if (out) {
        CHECK(out->nframes == 3 && out->body.nparas == 1, "three labels on one page");
        CHECK(!strcmp(story_text(&out->frames[0].story), "To Ann|Town: Leeds") &&
              !strcmp(story_text(&out->frames[2].story), "To Cy|Town: Bath"), story_text(&out->frames[0].story));
        CHECK(out->frames[1].x == s->left + s->across && out->frames[1].y == s->top &&
              out->frames[1].width == s->label_width && out->frames[1].height == s->label_height &&
              out->frames[1].x_from == OWF_FROM_PAGE && out->frames[1].y_from == OWF_FROM_PAGE, "placed at the pitch");
        CHECK(out->page.width == s->page_width && out->page.height == s->page_height && !out->page.margin_left, "the page");
        {
            int frames = 0;
            for (i = 0; i < out->body.paras[0].nruns; ++i)
                frames += out->body.paras[0].runs[i].kind == OWF_RUN_FRAME && out->body.paras[0].runs[i].image == frames;
            CHECK(frames == 3, "anchored in the page's paragraph");
        }
        CHECK(owf_export_file(out, "/tmp/openwrite-labels.odt", "odt", NULL) == OWF_OK &&
              owf_import_file("/tmp/openwrite-labels.odt", NULL, &back, NULL, NULL) == OWF_OK && back &&
              back->nframes == 3 && !strcmp(story_text(&back->frames[1].story), "To Bob|Town: York"), "ODT");
        owf_doc_free(back);
        back = NULL;
        CHECK(owf_export_file(out, "/tmp/openwrite-labels.docx", "docx", NULL) == OWF_OK &&
              owf_import_file("/tmp/openwrite-labels.docx", NULL, &back, NULL, NULL) == OWF_OK && back &&
              back->nframes == 3 && !strcmp(story_text(&back->frames[2].story), "To Cy|Town: Bath"), "DOCX");
        owf_doc_free(back);
        owf_doc_free(out);
        out = NULL;
    }
    CHECK(!strcmp(story_text(&doc->body), "To \xc2\xab" "Name\xc2\xbb|Town: \xc2\xab" "Town\xc2\xbb"), "the document as it was");
    small = *s;
    small.columns = 1;
    small.rows = 2;
    CHECK(ow_editor_labels(ed, &small, -1, -1, &out) == OWF_OK && out && out->nframes == 3 && out->body.nparas == 2 &&
          out->body.paras[1].fmt.page_break_before && out->frames[2].y == s->top, "a new page when one is full");
    owf_doc_free(out);
    out = NULL;
    CHECK(ow_editor_labels(ed, s, 1, 1, &out) == OWF_OK && out && out->nframes == 1 &&
          !strcmp(story_text(&out->frames[0].story), "To Bob|Town: York"), "one record");
    owf_doc_free(out);
    out = NULL;
    CHECK(ow_editor_labels(ed, s, 5, 9, &out) == OWF_ERR_UNSUPPORTED && !out, "no records there");
    small.rows = 0;
    CHECK(ow_editor_labels(ed, &small, -1, -1, &out) == OWF_ERR_FORMAT, "a sheet with no rows");
    CHECK(ow_editor_labels(ed, NULL, -1, -1, &out) == OWF_ERR_FORMAT, "no sheet");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

static void test_plain_and_refused(void)
{
    owf_doc *doc = NULL, *out = NULL;
    ow_editor *ed = address(&doc, 0);
    const ow_label_sheet *s = ow_label_sheet_at(1);
    if (!ed || !s) { CHECK(0, "editor"); ow_editor_free(ed); owf_doc_free(doc); return; }
    CHECK(ow_editor_labels(ed, s, -1, -1, &out) == OWF_OK && out && out->nframes == s->columns * s->rows &&
          out->body.nparas == 1, "a page of the same label");
    if (out) CHECK(!strcmp(story_text(&out->frames[13].story), "To |Town: "), story_text(&out->frames[13].story));
    owf_doc_free(out);
    out = NULL;
    caret_end(ed, 1);
    CHECK(ow_editor_insert_table(ed, 1, 2) == OWF_OK, "a table");
    CHECK(ow_editor_labels(ed, s, -1, -1, &out) == OWF_ERR_UNSUPPORTED && !out, "not with a table");
    ow_editor_free(ed);
    owf_doc_free(doc);
}

int main(void)
{
    test_sheets();
    test_merged();
    test_plain_and_refused();
    printf("labels: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
