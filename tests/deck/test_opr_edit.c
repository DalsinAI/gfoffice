/*
 * test_opr_edit: typing in a slide's text (src/deck/opr_edit.c), on the
 * PPTX fixture (test_opr_edit FIXTURE.pptx) and on a new deck:
 *
 *   - text inserted into the run at the caret keeps that run's look (bold
 *     red stays bold red); tabs, line breaks and new paragraphs, which keep
 *     their list level; backspace and delete, joining paragraphs at their
 *     ends; moving by characters across paragraphs, UTF-8 whole;
 *   - an empty placeholder takes its first paragraph at its kind's level;
 *   - undo and redo, sixteen steps;
 *   - the caret and the pointer through the laid-out slide: the caret moves
 *     right as the offset grows, and a click on the caret gives the same
 *     position back, in a bulleted paragraph too;
 *   - the edited deck saved as ODP and read back with the new text.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "openpresentation_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static char *text(const owf_deck *d, int slide, int obj)
{
    static char buf[4096];
    char *t = opr_text_get(d, slide, obj);
    snprintf(buf, sizeof buf, "%s", t ? t : "(null)");
    owf_free(t);
    return buf;
}

static int obj_named(const owf_deck *d, int slide, const char *name)
{
    int i;
    for (i = 0; i < d->slides[slide].nobjs; i++)
        if (d->slides[slide].objs[i].name && !strcmp(d->slides[slide].objs[i].name, name))
            return i;
    return -1;
}

static void typing(owf_deck *d)
{
    opr_pos at;
    const owf_obj *o;
    int body = obj_named(d, 1, "Content Placeholder 2"), note = obj_named(d, 2, "Note");

    CHECK(body >= 0 && note >= 0, "the fixture's objects");
    CHECK(opr_obj_editable(d, 1, body) && !opr_obj_editable(d, 3, obj_named(d, 3, "Table 3")) &&
          !opr_obj_editable(d, 2, obj_named(d, 2, "Line")), "what can be typed in");
    CHECK(!strcmp(text(d, 1, body), "Titles and bullet lists\nA second level\nA third level\nText boxes and shapes\n"
                                    "Pictures and tables"), "the body's text: %s", text(d, 1, body));
    /* into a word */
    at.para = 0;
    at.offset = 6;
    CHECK(!opr_text_insert(d, 1, body, &at, ", subtitles", 11), "insert");
    CHECK(at.para == 0 && at.offset == 17, "the caret after it: %d/%lu", at.para, (unsigned long)at.offset);
    CHECK(!strncmp(text(d, 1, body), "Titles, subtitles and bullet lists\n", 35), "%s", text(d, 1, body));
    /* a new paragraph keeps its level */
    at.para = 1;
    at.offset = 0;
    opr_text_move(d, 1, body, &at, OPR_MOVE_PARA_END);
    CHECK(at.offset == 14, "the end of the second paragraph: %lu", (unsigned long)at.offset);
    CHECK(!opr_text_insert(d, 1, body, &at, "\nTyped on the Amiga", 19), "a new paragraph");
    o = &d->slides[1].objs[body];
    CHECK(o->text.nparas == 6 && at.para == 2 && owf_obj_level(o, 2) == 2 && owf_obj_level(o, 3) == 3 &&
          owf_obj_level(o, 4) == 1, "levels after it: %d %d %d", owf_obj_level(o, 2), owf_obj_level(o, 3),
          owf_obj_level(o, 4));
    CHECK(!strcmp(text(d, 1, body), "Titles, subtitles and bullet lists\nA second level\nTyped on the Amiga\n"
                                    "A third level\nText boxes and shapes\nPictures and tables"), "%s", text(d, 1, body));
    /* backspace at a paragraph's start joins it to the one before */
    at.offset = 0;
    CHECK(!opr_text_backspace(d, 1, body, &at) && at.para == 1 && at.offset == 14, "join back: %d/%lu", at.para,
          (unsigned long)at.offset);
    CHECK(!strncmp(text(d, 1, body) + 35, "A second levelTyped on the Amiga\nA third level", 46), "%s", text(d, 1, body));
    CHECK(d->slides[1].objs[body].text.nparas == 5 && owf_obj_level(&d->slides[1].objs[body], 2) == 3,
          "the levels after a join");
    /* delete at the end joins the next one on */
    opr_text_move(d, 1, body, &at, OPR_MOVE_PARA_END);
    CHECK(!opr_text_delete(d, 1, body, &at), "delete");
    CHECK(strstr(text(d, 1, body), "AmigaA third level") != NULL, "%s", text(d, 1, body));
    /* backspace and delete take whole UTF-8 characters */
    opr_text_insert(d, 1, body, &at, "\xE2\x80\x94", 3);
    at.offset -= 3;
    opr_text_move(d, 1, body, &at, OPR_MOVE_RIGHT);
    CHECK(at.offset == 35, "right over an em dash: %lu", (unsigned long)at.offset);
    CHECK(!opr_text_backspace(d, 1, body, &at) && at.offset == 32 && strstr(text(d, 1, body), "AmigaA third"),
          "backspace over an em dash: %lu %s", (unsigned long)at.offset, text(d, 1, body));
    /* left at a paragraph's start goes to the end of the one before */
    at.para = 2;
    at.offset = 0;
    opr_text_move(d, 1, body, &at, OPR_MOVE_LEFT);
    CHECK(at.para == 1 && at.offset == opr_para_length(&d->slides[1].objs[body].text.paras[1]), "left");

    /* into bold red: it stays bold red */
    at.para = 0;
    at.offset = 20;                  /* "A text box with bold| red" */
    CHECK(!opr_text_insert(d, 2, note, &at, "est", 3), "insert into bold red");
    o = &d->slides[2].objs[note];
    CHECK(o->text.paras[0].nruns == 3 && !strcmp(o->text.paras[0].runs[1].text, "boldest red") &&
          (o->text.paras[0].runs[1].fmt.flags & OWF_BOLD) && o->text.paras[0].runs[1].fmt.colour == 0xC00000,
          "bold red: %s", o->text.paras[0].nruns > 1 ? o->text.paras[0].runs[1].text : "?");
    /* a tab and a line break */
    at.offset = 0;
    CHECK(!opr_text_insert(d, 2, note, &at, "\t>\v", 3), "a tab and a line break");
    o = &d->slides[2].objs[note];
    CHECK(o->text.paras[0].runs[0].kind == OWF_RUN_TAB && o->text.paras[0].runs[2].kind == OWF_RUN_LINEBREAK &&
          at.offset == 3, "the tab's and the break's runs");
    CHECK(!strncmp(text(d, 2, note), "\t>\vA text box with boldest red words\n", 37), "%s", text(d, 2, note));
}

static void undo_redo(owf_deck *d)
{
    opr_undo *u = opr_undo_new();
    opr_pos at;
    int title = obj_named(d, 0, "Title 1"), slide = -1, obj = -1, i;
    char before[256];
    CHECK(u != NULL, "undo");
    if (!u)
        return;
    snprintf(before, sizeof before, "%s", text(d, 0, title));
    CHECK(!opr_undo_can(u), "nothing to undo yet");
    at.para = 0;
    at.offset = 0;
    for (i = 0; i < 20; i++) {
        CHECK(!opr_undo_push(u, d, 0, title), "push %d", i);
        opr_text_insert(d, 0, title, &at, "x", 1);
    }
    CHECK(!strncmp(text(d, 0, title), "xxxxxxxxxxxxxxxxxxxxGFOffice", 28), "%s", text(d, 0, title));
    for (i = 0; i < 16; i++)
        CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == title, "undo %d", i);
    CHECK(!opr_undo_can(u) && opr_undo_undo(u, d, NULL, NULL) < 0, "sixteen steps at most");
    CHECK(!strncmp(text(d, 0, title), "xxxxGFOffice", 12), "after sixteen undos: %s", text(d, 0, title));
    CHECK(opr_redo_can(u) && !opr_undo_redo(u, d, NULL, NULL), "redo");
    CHECK(!strncmp(text(d, 0, title), "xxxxxGFOffice", 13), "after a redo: %s", text(d, 0, title));
    /* a new change forgets what could be redone */
    opr_undo_push(u, d, 0, title);
    opr_text_set(d, 0, title, before);
    CHECK(!opr_redo_can(u) && !strcmp(text(d, 0, title), before), "set the text back: %s", text(d, 0, title));
    opr_undo_free(u);
}

static void empty_placeholders(void)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, 2) : NULL;    /* Title, Content */
    opr_pos at;
    CHECK(s && s->nobjs == 2 && (s->objs[0].flags & OWF_OBJ_EMPTY) && (s->objs[1].flags & OWF_OBJ_EMPTY),
          "a new slide's empty placeholders");
    if (!s) {
        owf_deck_free(d);
        return;
    }
    at.para = 0;
    at.offset = 0;
    CHECK(!opr_text_insert(d, 0, 0, &at, "A title", 7) && !(d->slides[0].objs[0].flags & OWF_OBJ_EMPTY) &&
          owf_obj_level(&d->slides[0].objs[0], 0) == 0, "typing into the title");
    at.offset = 0;
    CHECK(!opr_text_insert(d, 0, 1, &at, "One\nTwo", 7) && owf_obj_level(&d->slides[0].objs[1], 0) == 1 &&
          owf_obj_level(&d->slides[0].objs[1], 1) == 1, "typing into the body: list paragraphs");
    CHECK(!strcmp(text(d, 0, 1), "One\nTwo"), "%s", text(d, 0, 1));
    owf_deck_free(d);
}

static void caret_and_pointer(owf_deck *d)
{
    int title = obj_named(d, 0, "Title 1"), body = obj_named(d, 1, "Content Placeholder 2"), f, k;
    opr_slide *s = opr_slide_layout(d, 0, NULL, NULL, "25/10/2026");
    opr_pos at, back;
    ol_rect a, b;
    CHECK(s != NULL, "slide 1 laid out");
    if (!s)
        return;
    f = opr_slide_frame_for(s, title);
    CHECK(f >= 0 && opr_frame_obj(s, f) == title, "the title's frame: %d", f);
    at.para = 0;
    at.offset = 0;
    CHECK(opr_caret_box(s, f, at, NULL, NULL, &a), "the caret at the start");
    at.offset = 10;
    CHECK(opr_caret_box(s, f, at, NULL, NULL, &b) && b.x > a.x && b.height > 0, "further right: %ld %ld",
          (long)a.x, (long)b.x);
    for (k = 0; k <= 12; k += 3) {
        at.offset = (size_t)k;
        opr_caret_box(s, f, at, NULL, NULL, &a);
        CHECK(opr_hit(s, a.x, a.y + a.height / 2, NULL, NULL, &back) == f && back.para == 0 && back.offset == (size_t)k,
              "a click on the caret at %d gives %d/%lu", k, back.para, (unsigned long)back.offset);
    }
    /* outside every frame of the slide's own */
    CHECK(opr_hit(s, 4, 4, NULL, NULL, &back) == -1, "a click on the background");
    opr_slide_free(s);

    /* a bulleted paragraph: its bullet and tab come before its text */
    s = opr_slide_layout(d, 1, NULL, NULL, NULL);
    CHECK(s != NULL, "slide 2 laid out");
    if (!s)
        return;
    f = opr_slide_frame_for(s, body);
    for (k = 0; k <= 6; k += 2) {
        at.para = 1;
        at.offset = (size_t)k;
        CHECK(opr_caret_box(s, f, at, NULL, NULL, &a), "caret in a list paragraph");
        CHECK(opr_hit_frame(s, f, a.x, a.y + a.height / 2, NULL, NULL, &back) && back.para == 1 &&
              back.offset == (size_t)k, "a click there at %d gives %d/%lu", k, back.para, (unsigned long)back.offset);
    }
    /* left of the bullet: the paragraph's start */
    at.offset = 0;
    opr_caret_box(s, f, at, NULL, NULL, &a);
    CHECK(opr_hit_frame(s, f, a.x - 2000, a.y + a.height / 2, NULL, NULL, &back) && back.para == 1 && back.offset == 0,
          "left of the bullet");
    opr_slide_free(s);
}

static void saved(owf_deck *d)
{
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    int body;
    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK, "saved as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back, "read back");
    if (back) {
        body = obj_named(back, 1, "Content Placeholder 2");
        CHECK(body >= 0 && strstr(text(back, 1, body), "Titles, subtitles and bullet lists") &&
              strstr(text(back, 1, body), "AmigaA third level"), "the edits kept: %s", body >= 0 ? text(back, 1, body) : "");
        CHECK(owf_obj_level(&back->slides[1].objs[body], 1) == 2, "their levels kept");
        CHECK(strstr(text(back, 2, obj_named(back, 2, "Note")), "boldest red") != NULL, "%s", text(back, 2, obj_named(back, 2, "Note")));
    }
    owf_deck_free(back);
    free(data);
}

int main(int argc, char **argv)
{
    owf_deck *d = NULL;
    const char *path = argc > 1 ? argv[1] : "tests/deck/fixtures/pptx-talk.pptx";
    if (owf_import_deck_file(path, NULL, &d, NULL, NULL) != OWF_OK || !d) {
        printf("FAIL: %s not read\n", path);
        return 1;
    }
    caret_and_pointer(d);
    typing(d);
    undo_redo(d);
    saved(d);
    owf_deck_free(d);
    empty_placeholders();
    printf("test_opr_edit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
