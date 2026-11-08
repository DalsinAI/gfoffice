/*
 * test_opr_edit: typing in a slide's text (src/deck/opr_edit.c), on the
 * PPTX fixture (test_opr_edit FIXTURE.pptx) and on a new deck:
 *
 *   - text inserted into the run at the caret keeps that run's look (bold
 *     red stays bold red); tabs, line breaks and new paragraphs, which keep
 *     their list level; backspace and delete, joining paragraphs at their
 *     ends; moving by characters across paragraphs, UTF-8 whole;
 *   - an empty placeholder takes its first paragraph at its kind's level;
 *   - undo and redo, sixteen steps, including inserting, deleting and moving
 *     whole slides without losing their content;
 *   - the caret and the pointer through the laid-out slide: the caret moves
 *     right as the offset grows, and a click on the caret gives the same
 *     position back, in a bulleted paragraph too;
 *   - the edited deck saved as ODP and read back with the new text.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
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

static void structural_slides(void)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    opr_undo *u = opr_undo_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    owf_obj resolved;
    owf_transition transition;
    int selected = -1, slide = -2, obj = -2, shape_obj = -1, duplicate_obj = -1;
    int old_x, old_y, old_width, old_height;
    unsigned old_flags;

    CHECK(d && u, "a deck and structural undo");
    if (!d || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    CHECK(!opr_slide_insert(u, d, 0, 1, &selected) && d->nslides == 1 && selected == 0,
          "insert the first slide");
    owf_deck_set_text(&d->slides[0].name, "Opening");
    owf_deck_set_text(&d->slides[0].footer, "Kept footer");
    CHECK(!opr_text_set(d, 0, 0, "Opening title"), "give it content");

    CHECK(!opr_slide_insert(u, d, 1, 2, &selected) && d->nslides == 2 && selected == 1,
          "insert a content slide");
    owf_deck_set_text(&d->slides[1].name, "Content");
    CHECK(!opr_text_set(d, 1, 0, "Second title"), "give the second slide content");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->nslides == 1 && slide == 0 && obj == -1,
          "undo an insertion and select the previous slide");
    CHECK(!strcmp(d->slides[0].name, "Opening") && !strcmp(text(d, 0, 0), "Opening title"),
          "the preceding slide is untouched");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->nslides == 2 && slide == 1 && obj == -1 &&
          !strcmp(d->slides[1].name, "Content") && !strcmp(text(d, 1, 0), "Second title"),
          "redo restores the inserted slide and all its content");

    selected = 0;
    CHECK(!opr_slide_delete(u, d, 0, &selected) && d->nslides == 1 && selected == 0 &&
          !strcmp(d->slides[0].name, "Content"), "delete a slide");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->nslides == 2 && slide == 0 &&
          !strcmp(d->slides[0].name, "Opening") && !strcmp(d->slides[0].footer, "Kept footer") &&
          !strcmp(text(d, 0, 0), "Opening title"), "undo restores the complete deleted slide");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->nslides == 1 && !strcmp(d->slides[0].name, "Content"),
          "redo deletes it again");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->nslides == 2, "restore it for moving");

    selected = 0;
    CHECK(!opr_slide_move(u, d, 0, 1, &selected) && selected == 1 && !strcmp(d->slides[0].name, "Content") &&
          !strcmp(d->slides[1].name, "Opening"), "move a slide and follow it");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && !strcmp(d->slides[0].name, "Opening"),
          "undo the move");
    CHECK(opr_redo_can(u), "the move can be redone");
    selected = 0;
    CHECK(!opr_slide_insert(u, d, 1, 0, &selected) && !opr_redo_can(u),
          "a new structural edit clears redo");

    selected = 0;
    CHECK(!opr_slide_duplicate(u, d, 0, 1, &selected) && selected == 1 && d->nslides == 4 &&
          d->slides[1].name && !strcmp(d->slides[1].name, "Opening Copy") &&
          !strcmp(text(d, 1, 0), "Opening title"), "duplicate a complete slide with a unique name");
    CHECK(d->slides[1].objs != d->slides[0].objs &&
          d->slides[1].objs[0].text.paras[0].runs[0].text != d->slides[0].objs[0].text.paras[0].runs[0].text,
          "the duplicate owns its objects and text");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->nslides == 3 && slide == 0,
          "undo removes the duplicate");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->nslides == 4 && slide == 1 &&
          !strcmp(d->slides[1].name, "Opening Copy"), "redo restores the duplicate");

    selected = 1;
    CHECK(!opr_slide_rename(u, d, 1, "Opening Copy Renamed", &selected) && selected == 1 &&
          !strcmp(d->slides[1].name, "Opening Copy Renamed"), "rename a slide");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 1 && !strcmp(d->slides[1].name, "Opening Copy"),
          "undo the rename");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && !strcmp(d->slides[1].name, "Opening Copy Renamed"),
          "redo the rename");
    CHECK(opr_slide_rename(u, d, 1, "Opening", &selected) == -2 &&
          opr_slide_rename(u, d, 1, "", &selected) == -2 &&
          !strcmp(d->slides[1].name, "Opening Copy Renamed"), "empty and duplicate slide names are refused");

    CHECK(!opr_slide_set_hidden(u, d, 1, 1, &selected) && selected == 1 && d->slides[1].hidden,
          "hide a slide");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 1 && !d->slides[1].hidden,
          "undo hiding a slide");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->slides[1].hidden,
          "redo hiding a slide");
    CHECK(!opr_slide_set_hidden(u, d, 1, 0, &selected) && !d->slides[1].hidden,
          "show a slide");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->slides[1].hidden,
          "undo showing a slide");
    CHECK(!opr_slide_set_hidden(u, d, 1, 1, &selected) && d->slides[1].hidden,
          "setting the same visibility is harmless");

    old_x = d->slides[1].objs[0].x;
    old_y = d->slides[1].objs[0].y;
    old_width = d->slides[1].objs[0].width;
    old_height = d->slides[1].objs[0].height;
    old_flags = d->slides[1].objs[0].flags;
    CHECK(owf_deck_resolve(d, 1, 0, &resolved, NULL) > 0 && resolved.width > 0 && resolved.height > 0,
          "resolve an inherited object box before moving it");
    CHECK(!opr_obj_set_box(u, d, 1, 0, 100, 200, 3000, 1200) &&
          d->slides[1].objs[0].x == 100 && d->slides[1].objs[0].y == 200 &&
          d->slides[1].objs[0].width == 3000 && d->slides[1].objs[0].height == 1200 &&
          !(d->slides[1].objs[0].flags & OWF_OBJ_INHERIT_BOX),
          "moving an inherited object makes its new box explicit");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 1 && obj == 0 &&
          d->slides[1].objs[0].x == old_x && d->slides[1].objs[0].y == old_y &&
          d->slides[1].objs[0].width == old_width && d->slides[1].objs[0].height == old_height &&
          d->slides[1].objs[0].flags == old_flags, "undo restores the object's inherited box");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == 0 && d->slides[1].objs[0].x == 100 &&
          d->slides[1].objs[0].width == 3000 && !(d->slides[1].objs[0].flags & OWF_OBJ_INHERIT_BOX),
          "redo restores the explicit object box");
    CHECK(opr_obj_set_box(u, d, 1, 0, -1, 0, 100, 100) == -2 &&
          opr_obj_set_box(u, d, 1, 0, d->width - 10, 0, 20, 100) == -2 &&
          opr_obj_set_box(u, d, 1, 0, 0, 0, 0, 100) == -2,
          "empty and out-of-slide object boxes are refused");

    {
        opr_shape_spec spec;
        int before = d->slides[1].nobjs;
        memset(&spec, 0, sizeof spec);
        spec.shape = OWF_SHAPE_ROUNDRECT;
        spec.x = 4000; spec.y = 3000; spec.width = 2400; spec.height = 1200;
        spec.radius = 200;
        spec.look.fill = OWF_SHADE(0x729fcf);
        spec.look.line = OWF_SHADE(0x203040);
        spec.look.line_width = 20;
        CHECK(!opr_obj_insert_shape(u, d, 1, &spec, &shape_obj) && shape_obj == before &&
              d->slides[1].nobjs == before + 1 && d->slides[1].objs[shape_obj].shape == OWF_SHAPE_ROUNDRECT &&
              d->slides[1].objs[shape_obj].radius == 200,
              "insert and select a styled shape");
        CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->slides[1].nobjs == before && obj == -1,
              "undo shape insertion");
        CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->slides[1].nobjs == before + 1 &&
              obj == shape_obj && d->slides[1].objs[shape_obj].look.fill == spec.look.fill,
              "redo shape insertion with its style");
        if (shape_obj >= 0) {
            owf_obj *shape = &d->slides[1].objs[shape_obj];
            CHECK(!opr_obj_set_rotation(u, d, 1, shape_obj, 150) && shape->rotation == 150,
                  "rotate a shape anticlockwise");
            CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 1 && obj == shape_obj &&
                  shape->rotation == 0, "undo shape rotation");
            CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == shape_obj &&
                  shape->rotation == 150, "redo shape rotation");
            CHECK(!opr_obj_set_rotation(u, d, 1, shape_obj, -150) && shape->rotation == 3450,
                  "normalize a clockwise shape rotation");
            CHECK(!opr_obj_delete(u, d, 1, shape_obj) && d->slides[1].nobjs == before,
                  "delete a shape as one edit");
            CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == shape_obj &&
                  d->slides[1].nobjs == before + 1 && d->slides[1].objs[shape_obj].rotation == 3450,
                  "undo restores the complete deleted shape");
        }
    }
    CHECK(opr_obj_set_rotation(u, d, 1, 0, 150) == -2,
          "refuse rotation for a non-shape object");
    CHECK(opr_obj_delete(u, d, 1, -1) == -1, "refuse deletion of a missing object");
    {
        int before = d->slides[0].nobjs;
        owf_obj resolved;
        char *source_text;
        CHECK(owf_deck_resolve(d, 0, 0, &resolved, NULL) >= 0,
              "resolve an inherited object before duplicating it");
        source_text = d->slides[0].objs[0].text.paras[0].runs[0].text;
        CHECK(owf_obj_copy(NULL, &d->slides[0].objs[0]) < 0 &&
              owf_obj_copy(&d->slides[0].objs[0], &d->slides[0].objs[0]) < 0,
              "the reusable object copier refuses invalid destinations");
        CHECK(!opr_obj_duplicate(u, d, 0, 0, 240, 240, &duplicate_obj) &&
              duplicate_obj == before && d->slides[0].nobjs == before + 1 &&
              !strcmp(text(d, 0, duplicate_obj), "Opening title") &&
              d->slides[0].objs[duplicate_obj].text.paras[0].runs[0].text != source_text &&
              d->slides[0].objs[duplicate_obj].id == 0 &&
              !(d->slides[0].objs[duplicate_obj].flags & OWF_OBJ_INHERIT_BOX) &&
              d->slides[0].objs[duplicate_obj].width == resolved.width,
              "duplicate an object with independent owned text and a resolved box");
        CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == -1 &&
              d->slides[0].nobjs == before, "undo removes the object duplicate");
        CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == duplicate_obj &&
              d->slides[0].nobjs == before + 1 &&
              !strcmp(text(d, 0, duplicate_obj), "Opening title"),
              "redo restores the complete independent duplicate");
    }
    {
        int before = d->slides[0].nobjs;
        CHECK(!opr_obj_delete(u, d, 0, 0) && d->slides[0].nobjs == before - 1,
              "delete an object that owns text");
        CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == 0 &&
              d->slides[0].nobjs == before && !strcmp(text(d, 0, 0), "Opening title"),
              "undo restores an object's owned text");
    }

    memset(&transition, 0, sizeof transition);
    transition.kind = OWF_TRANS_WIPE;
    transition.direction = OWF_DIR_RIGHT;
    transition.duration = 500;
    transition.advance_after = 2500;
    transition.keep = 99;
    CHECK(!opr_slide_set_transition(u, d, 1, &transition, 0, &selected) && selected == 1 &&
          d->slides[1].transition.kind == OWF_TRANS_WIPE &&
          d->slides[1].transition.direction == OWF_DIR_RIGHT &&
          d->slides[1].transition.duration == 500 && d->slides[1].transition.advance_after == 2500 &&
          d->slides[1].transition.keep == -1, "edit one slide's transition and discard kept foreign attributes");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 1 &&
          d->slides[1].transition.kind == OWF_TRANS_NONE && d->slides[1].transition.advance_after == -1,
          "undo a transition edit");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->slides[1].transition.kind == OWF_TRANS_WIPE &&
          d->slides[1].transition.advance_after == 2500, "redo a transition edit");

    transition.kind = OWF_TRANS_DISSOLVE;
    transition.direction = OWF_DIR_LEFT;
    transition.duration = 1000;
    transition.advance_after = -1;
    CHECK(!opr_slide_set_transition(u, d, 1, &transition, 1, &selected) &&
          d->slides[0].transition.kind == OWF_TRANS_DISSOLVE &&
          d->slides[d->nslides - 1].transition.kind == OWF_TRANS_DISSOLVE,
          "apply a transition to every slide as one edit");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->slides[0].transition.kind == OWF_TRANS_NONE &&
          d->slides[1].transition.kind == OWF_TRANS_WIPE &&
          d->slides[d->nslides - 1].transition.kind == OWF_TRANS_NONE,
          "undo apply-to-all restores every slide's own transition");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->slides[0].transition.kind == OWF_TRANS_DISSOLVE &&
          d->slides[d->nslides - 1].transition.kind == OWF_TRANS_DISSOLVE,
          "redo apply-to-all");
    transition.kind = OWF_TRANS_OTHER;
    CHECK(opr_slide_set_transition(u, d, 1, &transition, 0, &selected) == -2,
          "an uneditable kept transition cannot be invented");
    transition.kind = OWF_TRANS_PUSH;
    transition.duration = 600001;
    CHECK(opr_slide_set_transition(u, d, 1, &transition, 0, &selected) == -2,
          "an invalid transition duration is refused");
    transition.duration = 1000;
    transition.advance_after = 86400000;
    CHECK(opr_slide_set_transition(u, d, 1, &transition, 0, &selected) == -2,
          "an invalid automatic delay is refused");

    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the deck with edited transitions saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the deck with edited transitions reads back");
    if (back) {
        CHECK(back->nslides == d->nslides && back->slides[0].transition.kind == OWF_TRANS_DISSOLVE &&
              back->slides[0].transition.duration == 1000 && back->slides[0].transition.advance_after == -1 &&
              back->slides[back->nslides - 1].transition.kind == OWF_TRANS_DISSOLVE,
              "edited transitions survive the ODP round trip");
        CHECK(back->slides[1].objs[0].x == 100 && back->slides[1].objs[0].y == 200 &&
              back->slides[1].objs[0].width == 3000 && back->slides[1].objs[0].height == 1200 &&
              !(back->slides[1].objs[0].flags & OWF_OBJ_INHERIT_BOX),
              "the explicit object box survives the ODP round trip");
        if (shape_obj >= 0)
            CHECK(back->slides[1].objs[shape_obj].kind == OWF_OBJ_SHAPE &&
                  back->slides[1].objs[shape_obj].rotation == 3450,
                  "edited shape rotation survives the ODP round trip");
        if (duplicate_obj >= 0)
            CHECK(duplicate_obj < back->slides[0].nobjs &&
                  !strcmp(text(back, 0, duplicate_obj), "Opening title") &&
                  !(back->slides[0].objs[duplicate_obj].flags & OWF_OBJ_INHERIT_BOX),
                  "the duplicated object survives the ODP round trip");
    }

    owf_deck_free(back);
    free(data);
    opr_undo_free(u);
    owf_deck_free(d);
}

static int named_at(const owf_slide *s, int index, const char *name)
{
    return s && index >= 0 && index < s->nobjs && s->objs[index].name &&
           !strcmp(s->objs[index].name, name);
}

static void object_order(void)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    owf_obj *o;
    int selected = -1, slide = -1, obj = -1;
    CHECK(d && s && u, "a deck for object arranging");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    o = owf_slide_add(s, OWF_OBJ_TEXT); if (o) owf_deck_set_text(&o->name, "A");
    o = owf_slide_add(s, OWF_OBJ_TEXT); if (o) owf_deck_set_text(&o->name, "B");
    o = owf_slide_add(s, OWF_OBJ_GROUP); if (o) owf_deck_set_text(&o->name, "G");
    o = owf_slide_add(s, OWF_OBJ_TEXT);
    if (o) { owf_deck_set_text(&o->name, "C"); o->parent = 2; }
    o = owf_slide_add(s, OWF_OBJ_TEXT); if (o) owf_deck_set_text(&o->name, "D");
    CHECK(s->nobjs == 5 && named_at(s, 0, "A") && named_at(s, 2, "G") &&
          named_at(s, 3, "C") && s->objs[3].parent == 2, "the initial drawing order and group");
    CHECK(!opr_obj_arrange(u, d, 0, 0, OPR_ARRANGE_BACK, &selected) && selected == 0 &&
          !opr_undo_can(u), "arranging an object already at that layer is not an edit");

    CHECK(!opr_obj_arrange(u, d, 0, 0, OPR_ARRANGE_FORWARD, &selected) && selected == 1 &&
          named_at(s, 0, "B") && named_at(s, 1, "A") && named_at(s, 2, "G") &&
          named_at(s, 3, "C") && s->objs[3].parent == 2,
          "bring an object forward one top-level layer");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == 0 &&
          named_at(s, 0, "A") && named_at(s, 1, "B"), "undo object arranging");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == 1 &&
          named_at(s, 0, "B") && named_at(s, 1, "A"), "redo object arranging");

    CHECK(!opr_obj_arrange(u, d, 0, 1, OPR_ARRANGE_FRONT, &selected) && selected == 4 &&
          named_at(s, 0, "B") && named_at(s, 1, "G") && named_at(s, 2, "C") &&
          s->objs[2].parent == 1 && named_at(s, 4, "A"),
          "bring to front across a complete group block");
    CHECK(!opr_obj_arrange(u, d, 0, 4, OPR_ARRANGE_BACKWARD, &selected) && selected == 3 &&
          named_at(s, 1, "G") && named_at(s, 2, "C") && s->objs[2].parent == 1 &&
          named_at(s, 3, "A") && named_at(s, 4, "D"), "send backward one layer");
    CHECK(!opr_obj_arrange(u, d, 0, 3, OPR_ARRANGE_BACK, &selected) && selected == 0 &&
          named_at(s, 0, "A") && named_at(s, 1, "B") && named_at(s, 2, "G") &&
          named_at(s, 3, "C") && s->objs[3].parent == 2, "send an object to the back");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == 3 && named_at(s, 3, "A") &&
          named_at(s, 1, "G") && named_at(s, 2, "C") && s->objs[2].parent == 1,
          "undo send to back without breaking the crossed group");
    CHECK(opr_obj_arrange(u, d, 0, 1, OPR_ARRANGE_FRONT, &selected) == -2 &&
          opr_obj_arrange(u, d, 0, 2, OPR_ARRANGE_FRONT, &selected) == -2,
          "groups and their children wait for whole-group arranging");
    CHECK(opr_obj_arrange(u, d, 0, 0, (opr_arrange)99, &selected) == -2,
          "an invalid arrange command is refused");
    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the rearranged drawing order saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the rearranged drawing order reads back");
    if (back) {
        const owf_slide *saved = &back->slides[0];
        CHECK(saved->nobjs == 5 && named_at(saved, 0, "B") && named_at(saved, 1, "G") &&
              named_at(saved, 2, "C") && saved->objs[2].parent == 1 &&
              named_at(saved, 3, "A") && named_at(saved, 4, "D"),
              "drawing order and the intact group survive the ODP round trip");
    }
    owf_deck_free(back);
    free(data);
    opr_undo_free(u);
    owf_deck_free(d);
}

static void object_alignment(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    owf_obj *o;
    int slide = -1, obj = -1;
    CHECK(d && s && u, "a deck for object alignment");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    o = owf_slide_add(s, OWF_OBJ_SHAPE);
    CHECK(o != NULL, "an object to align");
    if (!o) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    o->shape = OWF_SHAPE_RECT;
    o->x = 100; o->y = 200; o->width = 2000; o->height = 1000;
    o->look.fill = OWF_SHADE(0x729fcf);

    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_LEFT) && o->x == 0 && o->y == 200,
          "align an object left");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == 0 &&
          o->x == 100 && o->y == 200, "undo alignment");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == 0 && o->x == 0,
          "redo alignment");
    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_RIGHT) && o->x == 8000,
          "align an object right");
    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_CENTRE) && o->x == 4000,
          "centre an object horizontally");
    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_TOP) && o->y == 0,
          "align an object to the top");
    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_BOTTOM) && o->y == 7000,
          "align an object to the bottom");
    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_MIDDLE) && o->y == 3500,
          "centre an object vertically");
    opr_undo_clear(u);
    CHECK(!opr_obj_align(u, d, 0, 0, OPR_ALIGN_MIDDLE) && !opr_undo_can(u),
          "alignment already satisfied is not an edit");
    CHECK(opr_obj_align(u, d, 0, 0, (opr_align)99) == -2,
          "an invalid alignment is refused");
    o = owf_slide_add(s, OWF_OBJ_GROUP);
    CHECK(o && opr_obj_align(u, d, 0, 1, OPR_ALIGN_LEFT) == -2,
          "whole-group alignment waits for group editing");

    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the aligned object saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the aligned object reads back");
    if (back)
        CHECK(back->slides[0].nobjs >= 1 && back->slides[0].objs[0].x == 4000 &&
              back->slides[0].objs[0].y == 3500 && back->slides[0].objs[0].width == 2000 &&
              back->slides[0].objs[0].height == 1000,
              "the aligned box survives the ODP round trip");
    owf_deck_free(back);
    free(data);
    opr_undo_free(u);
    owf_deck_free(d);
}

static void object_appearance(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    owf_obj *o;
    owf_look look, invalid;
    int slide = -1, obj = -1;
    CHECK(d && s && u, "a deck for object appearance");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    o = owf_slide_add(s, OWF_OBJ_SHAPE);
    CHECK(o != NULL, "a shape whose appearance can change");
    if (!o) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    o->shape = OWF_SHAPE_ARROW;
    o->x = 100; o->y = 200; o->width = 2000; o->height = 1000;
    memset(&look, 0, sizeof look);
    look.fill = OWF_SHADE(0x000000);
    look.line = OWF_SHADE(0xc03020);
    look.line_width = 60;
    look.line_style = OWF_LINE_DASHED;
    look.arrows = OWF_ARROW_START | OWF_ARROW_END;
    CHECK(!opr_obj_set_look(u, d, 0, 0, &look) && !memcmp(&o->look, &look, sizeof look),
          "set a shape's complete fill and stroke appearance");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == 0 &&
          !o->look.fill && !o->look.line, "undo object appearance");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == 0 &&
          !memcmp(&o->look, &look, sizeof look), "redo object appearance");
    opr_undo_clear(u);
    CHECK(!opr_obj_set_look(u, d, 0, 0, &look) && !opr_undo_can(u),
          "setting the existing appearance is not an edit");
    invalid = look; invalid.line_width = 10001;
    CHECK(opr_obj_set_look(u, d, 0, 0, &invalid) == -2,
          "an excessive line width is refused");
    invalid = look; invalid.fill = 0x2000000UL;
    CHECK(opr_obj_set_look(u, d, 0, 0, &invalid) == -2,
          "an invalid fill shade is refused");
    o = owf_slide_add(s, OWF_OBJ_GROUP);
    CHECK(o && opr_obj_set_look(u, d, 0, 1, &look) == -2,
          "group appearance waits for whole-group editing");

    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the styled object saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the styled object reads back");
    if (back) {
        const owf_look *saved = &back->slides[0].objs[0].look;
        CHECK(back->slides[0].nobjs >= 1 && saved->fill == look.fill && saved->line == look.line &&
              saved->line_width == look.line_width && saved->line_style == OWF_LINE_DASHED &&
              saved->arrows == look.arrows,
              "fill, stroke, dash and arrowheads survive the ODP round trip");
    }
    owf_deck_free(back);
    free(data);
    opr_undo_free(u);
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
    structural_slides();
    object_order();
    object_alignment();
    object_appearance();
    printf("test_opr_edit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
