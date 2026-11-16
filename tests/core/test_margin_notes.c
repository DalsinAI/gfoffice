/*
 * GFOpenWrite's comment notes in the margin (2.0.40): where each note goes on its page, stacked, a reply
 * indented; the note under a point; and a note picked to make its comment (a reply's too) the one at the
 * caret, so it can be changed, answered or deleted from the margin. make margin-notes-test,
 * make margin-notes-check (ASan).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openwrite_core.h"
#include "ow_layout.h"
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

/* half the size a character */
static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * style->font_size / 2;
    m->ascent = style->font_size * 4 / 5;
    m->descent = style->font_size / 5;
    return 1;
}

static owf_doc *doc_of(const char *const *paras, int n)
{
    owf_doc *doc = owf_doc_new();
    int i;
    owf_parafmt pf;
    owf_charfmt cf;
    owf_parafmt_init(&pf);
    owf_charfmt_init(&cf);
    for (i = 0; doc && i < n; ++i) {
        owf_para *p = owf_story_add(&doc->body, &pf);
        if (!p || (paras[i][0] && owf_para_add_text(p, &cf, paras[i], strlen(paras[i])) != 0)) {
            owf_doc_free(doc);
            return NULL;
        }
    }
    return doc;
}

/* The body's text, each comment's place as [its text], a reply's as [^text] */
static const char *dump(const owf_doc *doc)
{
    static char out[1024];
    size_t used = 0;
    int i, j;
    out[0] = 0;
    for (i = 0; i < doc->body.nparas; ++i) {
        const owf_para *p = &doc->body.paras[i];
        for (j = 0; j < p->nruns; ++j) {
            const owf_run *r = &p->runs[j];
            char piece[160] = "";
            if (r->comment > 0) {
                const owf_annotation *c = &doc->comments[r->comment - 1];
                snprintf(piece, sizeof piece, "[%s%s]", c->parent >= 0 ? "^" : "", c->text);
            } else if (r->comment == 0 && r->kind == OWF_RUN_TEXT && r->text) {
                snprintf(piece, sizeof piece, "%s", r->text);
            }
            if (used + strlen(piece) + 2 < sizeof out) { strcpy(out + used, piece); used += strlen(piece); }
        }
        if (i + 1 < doc->body.nparas && used + 2 < sizeof out) { strcpy(out + used, "|"); ++used; }
    }
    return out;
}

static void caret(ow_editor *ed, int paragraph, size_t offset)
{
    ow_selection sel;
    memset(&sel, 0, sizeof sel);
    sel.anchor.paragraph = paragraph;
    sel.anchor.byte_offset = offset;
    sel.focus = sel.anchor;
    ow_editor_set_selection(ed, &sel);
}

static void scale_at(ol_scale *s, int num_twips_per_pixel)
{
    ol_scale_set(s, 1, num_twips_per_pixel * OL_UNITS_PER_TWIP);
    s->origin_x = 10;
    s->origin_y = 20;
}

static void test_notes(void)
{
    static const char *const text[] = { "The quick fox", "jumps over", "the lazy dog" };
    owf_doc *doc = doc_of(text, 3);
    ow_editor *ed = doc ? ow_editor_new(doc) : NULL;
    ow_layout *l;
    ow_margin_note notes[8];
    ow_comment_info info;
    ol_scale s;
    int n, x0;
    CHECK(ed != NULL, "editor");
    if (!ed) { owf_doc_free(doc); return; }
    ow_editor_set_review_author(ed, "Ann");
    caret(ed, 0, 9);
    CHECK(ow_editor_insert_comment(ed, "Which fox?") == OWF_OK, "Ann's");
    ow_editor_set_review_author(ed, "Bob");
    caret(ed, 0, 9);
    CHECK(ow_editor_reply_comment(ed, "The red one.") == OWF_OK, "Bob's reply");
    caret(ed, 2, 3);
    CHECK(ow_editor_insert_comment(ed, "Which dog?") == OWF_OK, "Bob's comment");
    CHECK(!strcmp(dump(doc), "The quick[Which fox?][^The red one.] fox|jumps over|the[Which dog?] lazy dog"), dump(doc));

    l = ow_layout_new(doc, measure, NULL);
    CHECK(l != NULL, "laid out");
    if (!l) { ow_editor_free(ed); owf_doc_free(doc); return; }
    scale_at(&s, 15);
    n = ow_layout_margin_notes(l, ed, 0, &s, 10, notes, 8);
    CHECK(n == 3, "three notes on the first page");
    x0 = ol_scale_x(&s, ow_layout_page(l)->width - ow_layout_page(l)->margin_right) + 6;
    CHECK(n == 3 && notes[0].index == 0 && notes[1].index == 1 && notes[2].index == 2, "in the comments' order");
    CHECK(n == 3 && notes[0].x == x0 && notes[1].x == x0 + 8 && notes[2].x == x0, "the reply indented");
    CHECK(n == 3 && notes[0].margin_x == x0 && notes[0].height == 24, "the margin's edge, two lines high");
    CHECK(n == 3 && notes[0].x + notes[0].width == ol_scale_x(&s, ow_layout_page(l)->width) - 4, "to the page's edge less 4");
    CHECK(n == 3 && notes[1].y == notes[0].y + notes[0].height + 3, "the reply under its comment, not over it");
    CHECK(n == 3 && notes[0].y == notes[0].place_y && notes[2].y >= notes[2].place_y, "beside its line, or under");
    CHECK(n == 3 && notes[2].y >= notes[1].y + notes[1].height + 3, "the third under the reply");
    CHECK(n == 3 && notes[0].place_x > ol_scale_x(&s, ow_layout_page(l)->margin_left) && notes[0].place_x < x0,
          "the place is after \"quick\", not at the line's start");
    CHECK(ow_layout_margin_notes(l, ed, 0, &s, 10, notes, 1) == 3 && notes[0].index == 0, "fills max, counts all");
    CHECK(ow_layout_margin_notes(l, ed, 1, &s, 10, notes, 8) == 0, "none on a page without them");

    /* the note under a point */
    CHECK(ow_margin_note_at(notes, 0, 0, 0) == -1, "no notes");
    n = ow_layout_margin_notes(l, ed, 0, &s, 10, notes, 8);
    CHECK(ow_margin_note_at(notes, n, notes[1].x + 2, notes[1].y + 2) == 1, "the reply's");
    CHECK(ow_margin_note_at(notes, n, notes[0].x + notes[0].width - 1, notes[0].y + notes[0].height - 1) == 0,
          "its last pixel");
    CHECK(ow_margin_note_at(notes, n, notes[0].x + 2, notes[0].y + notes[0].height + 1) == -1, "between notes");
    CHECK(ow_margin_note_at(notes, n, notes[0].x - 1, notes[0].y + 2) == -1, "left of the note");

    /* the author filter: Bob's alone, Ann's comment's note goes and Bob's reply's stays */
    ow_editor_set_comment_filter(ed, "Bob");
    CHECK(ow_layout_margin_notes(l, ed, 0, &s, 10, notes, 8) == 2 && notes[0].index == 1 && notes[1].index == 2,
          "only Bob's");
    ow_editor_set_comment_filter(ed, NULL);

    /* a narrow margin: none */
    scale_at(&s, 200);
    CHECK(ow_layout_margin_notes(l, ed, 0, &s, 10, notes, 8) == 0, "no room");
    CHECK(ow_layout_margin_notes(NULL, ed, 0, &s, 10, notes, 8) == 0 && ow_layout_margin_notes(l, NULL, 0, &s, 10, notes, 8) == 0 &&
          ow_layout_margin_notes(l, ed, 0, NULL, 10, notes, 8) == 0 && ow_layout_margin_notes(l, ed, 0, &s, 0, notes, 8) == 0,
          "no layout, editor, scale or line");
    ow_layout_free(l);

    /* picked by its note: the reply is the comment at the caret */
    CHECK(!ow_editor_select_comment(ed, 3) && !ow_editor_select_comment(ed, -1), "no such comment");
    caret(ed, 0, 9);
    CHECK(ow_editor_current_comment(ed, &info) && info.index == 0, "at the caret: Ann's comment, not the reply");
    CHECK(ow_editor_select_comment(ed, 1) && ow_editor_current_comment(ed, &info) && info.index == 1 &&
          !strcmp(info.text, "The red one."), "picked: the reply");
    CHECK(ow_editor_set_comment_text(ed, "The brown one.") == OWF_OK, "the reply changed");
    CHECK(!strcmp(dump(doc), "The quick[Which fox?][^The brown one.] fox|jumps over|the[Which dog?] lazy dog"), dump(doc));
    CHECK(ow_editor_current_comment(ed, &info) && info.index == 1, "still the reply after the change");
    ow_editor_undo(ed);
    CHECK(!strcmp(dump(doc), "The quick[Which fox?][^The red one.] fox|jumps over|the[Which dog?] lazy dog"), dump(doc));

    /* a reply picked and deleted: that reply alone; undo brings it back */
    CHECK(ow_editor_select_comment(ed, 1) && ow_editor_delete_comment(ed) == OWF_OK, "the reply deleted");
    CHECK(!strcmp(dump(doc), "The quick[Which fox?] fox|jumps over|the[Which dog?] lazy dog"), dump(doc));
    ow_editor_undo(ed);
    CHECK(!strcmp(dump(doc), "The quick[Which fox?][^The red one.] fox|jumps over|the[Which dog?] lazy dog"), dump(doc));

    /* the selection moved: the pick is gone, the caret's comment is the thread's again */
    CHECK(ow_editor_select_comment(ed, 1), "picked again");
    caret(ed, 0, 9);
    CHECK(ow_editor_current_comment(ed, &info) && info.index == 0, "moved: Ann's");
    /* Next selects a comment, not a pick */
    CHECK(ow_editor_select_comment(ed, 1) && ow_editor_next_comment(ed, 0) == 1 && ow_editor_current_comment(ed, &info) &&
          info.index == 2, "Next: Bob's comment");
    /* a comment picked: as before, its replies with it */
    CHECK(ow_editor_select_comment(ed, 0) && ow_editor_delete_comment(ed) == OWF_OK, "Ann's deleted");
    CHECK(!strcmp(dump(doc), "The quick fox|jumps over|the[Which dog?] lazy dog"), dump(doc));
    ow_editor_free(ed);
    owf_doc_free(doc);
}

int main(void)
{
    test_notes();
    printf("margin-notes: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
