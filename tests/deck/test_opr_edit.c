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
 *   - authored text boxes and embedded pictures, transactional validation,
 *     object-presence undo/redo and ODP round trips;
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
    CHECK(opr_grid_snap(710, 720, 20) == 720 &&
          opr_grid_snap(690, 720, 20) == 690 &&
          opr_grid_snap(1435, 720, 20) == 1440 &&
          opr_grid_snap(-10, 720, 20) == 0 &&
          opr_grid_snap(360, 720, 400) == 0 &&
          opr_grid_snap(710, 0, 20) == 710,
          "portable grid snapping handles thresholds, ties and negative coordinates");

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

static void textbox_insertion(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    opr_textbox_spec spec, invalid, tail;
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    char *text = NULL;
    opr_selection selection;
    int selected = -1, slide = -1, obj = -1;
    int initial_font;
    int frame_inset[4] = {200, 150, 300, 250};
    int bad_inset[4] = {4200, 0, 0, 0};
    int shallow_inset[4] = {120, 80, 120, 80};
    CHECK(d && s && u, "a deck for text-box insertion");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    memset(&spec, 0, sizeof spec);
    spec.x = 1000; spec.y = 1200; spec.width = 4200; spec.height = 1800;
    spec.inset[0] = spec.inset[2] = 120;
    spec.inset[1] = spec.inset[3] = 80;
    spec.valign = OWF_VALIGN_CENTRE;
    spec.look.fill = OWF_SHADE(0xffffdd);
    spec.look.line = OWF_SHADE(0x303030);
    spec.look.line_width = 20;
    spec.look.line_style = OWF_LINE_THIN;
    spec.text = "Publisher story";
    CHECK(!opr_obj_insert_textbox(u, d, 0, &spec, &selected) && selected == 0 &&
          s->nobjs == 1 && s->objs[0].kind == OWF_OBJ_TEXT,
          "insert and select an editable text box");
    text = opr_text_get(d, 0, 0);
    CHECK(text && !strcmp(text, "Publisher story") && s->objs[0].x == 1000 &&
          s->objs[0].width == 4200 && s->objs[0].valign == OWF_VALIGN_CENTRE &&
          s->objs[0].inset[0] == 120 && s->objs[0].look.fill == spec.look.fill,
          "the text box owns its story, geometry, insets and look");
    free(text);
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && slide == 0 && obj == -1 && !s->nobjs,
          "undo text-box insertion removes it");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == 0 && s->nobjs == 1 &&
          s->objs[0].kind == OWF_OBJ_TEXT,
          "redo text-box insertion restores and selects it");
    CHECK(!opr_text_toggle_flags(u, d, 0, 0, 0, 0, OWF_BOLD) &&
          (s->objs[0].text.paras[0].runs[0].fmt.flags & OWF_BOLD),
          "bold the current paragraph as one reusable rich-text edit");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == 0 &&
          !(s->objs[0].text.paras[0].runs[0].fmt.flags & OWF_BOLD),
          "undo restores the paragraph's character formatting");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) &&
          (s->objs[0].text.paras[0].runs[0].fmt.flags & OWF_BOLD),
          "redo restores the bold formatting");
    CHECK(!opr_text_toggle_flags(u, d, 0, 0, 0, 0, OWF_BOLD) &&
          !(s->objs[0].text.paras[0].runs[0].fmt.flags & OWF_BOLD),
          "toggling an entirely bold range clears it");
    CHECK(!opr_text_toggle_flags(u, d, 0, 0, 0, 0, OWF_ITALIC | OWF_UNDERLINE) &&
          (s->objs[0].text.paras[0].runs[0].fmt.flags & (OWF_ITALIC | OWF_UNDERLINE)) ==
          (OWF_ITALIC | OWF_UNDERLINE), "italic and underline can be applied together");
    CHECK(!opr_text_set_size(u, d, 0, 0, 0, 0, 360) &&
          s->objs[0].text.paras[0].runs[0].fmt.size == 360,
          "set the paragraph to 18 point text");
    CHECK(!opr_text_set_colour(u, d, 0, 0, 0, 0, 0x336699UL) &&
          s->objs[0].text.paras[0].runs[0].fmt.colour == 0x336699UL,
          "set the paragraph text colour in the shared rich-text core");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].runs[0].fmt.colour == OWF_COLOUR_AUTO &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].runs[0].fmt.colour == 0x336699UL,
          "text colour participates in Undo and Redo");
    initial_font = s->objs[0].text.paras[0].runs[0].fmt.font;
    CHECK(!opr_text_set_font(u, d, 0, 0, 0, 0, "Liberation Serif") &&
          s->objs[0].text.paras[0].runs[0].fmt.font >= 0 &&
          !strcmp(d->res->fonts[s->objs[0].text.paras[0].runs[0].fmt.font].name,
                  "Liberation Serif"),
          "set the paragraph font family through the shared deck resources");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].runs[0].fmt.font == initial_font &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].runs[0].fmt.font >= 0,
          "font family participates in Undo and Redo");
    selection.anchor.para = selection.focus.para = 0;
    selection.anchor.offset = 0;
    selection.focus.offset = 9;
    CHECK(!opr_text_toggle_flags_selection(u, d, 0, 0, &selection, OWF_BOLD) &&
          s->objs[0].text.paras[0].nruns >= 2 &&
          (s->objs[0].text.paras[0].runs[0].fmt.flags & OWF_BOLD) &&
          !(s->objs[0].text.paras[0].runs[s->objs[0].text.paras[0].nruns - 1].fmt.flags & OWF_BOLD),
          "character selection formats Publisher without formatting story");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].nruns == 1 &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].nruns >= 2,
          "exact-range formatting is one Undo and Redo step");
    selection.anchor.offset = 10;
    selection.focus.offset = 15;
    CHECK(!opr_text_set_size_selection(u, d, 0, 0, &selection, 480) &&
          !opr_text_set_colour_selection(u, d, 0, 0, &selection, 0xcc3300UL) &&
          !opr_text_set_font_selection(u, d, 0, 0, &selection, "DejaVu Sans") &&
          s->objs[0].text.paras[0].runs[s->objs[0].text.paras[0].nruns - 1].fmt.size == 480 &&
          s->objs[0].text.paras[0].runs[s->objs[0].text.paras[0].nruns - 1].fmt.colour == 0xcc3300UL,
          "size, colour and font family apply to the exact selected characters");
    CHECK(!opr_undo_push(u, d, 0, 0), "snapshot selected-text deletion");
    selection.anchor.offset = 0;
    selection.focus.offset = 10;
    CHECK(!opr_text_delete_selection(d, 0, 0, &selection) &&
          opr_text_selection_empty(&selection) &&
          (text = opr_text_get(d, 0, 0)) != NULL && !strcmp(text, "story"),
          "typing replacement can delete an exact selection");
    free(text); text = NULL;
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          (text = opr_text_get(d, 0, 0)) != NULL && !strcmp(text, "Publisher story"),
          "Undo restores selected text and its split formatting");
    free(text); text = NULL;
    CHECK(!opr_text_set_alignment(u, d, 0, 0, 0, 0, OWF_ALIGN_RIGHT) &&
          s->objs[0].text.paras[0].fmt.align == OWF_ALIGN_RIGHT,
          "right-align the current paragraph");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].fmt.align != OWF_ALIGN_RIGHT &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].fmt.align == OWF_ALIGN_RIGHT,
          "paragraph alignment participates in Undo and Redo");
    CHECK(!opr_text_set_indents(u, d, 0, 0, 0, 0, 360, -180, 240) &&
          s->objs[0].text.paras[0].fmt.indent_left == 360 &&
          s->objs[0].text.paras[0].fmt.indent_first == -180 &&
          s->objs[0].text.paras[0].fmt.indent_right == 240,
          "set left, hanging first-line and right paragraph indents");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].fmt.indent_left == 0 &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].fmt.indent_left == 360,
          "paragraph indents participate in Undo and Redo");
    opr_undo_clear(u);
    CHECK(!opr_text_set_indents(u, d, 0, 0, 0, 0, 360, -180, 240) &&
          !opr_undo_can(u), "setting existing paragraph indents is not an edit");
    CHECK(opr_text_toggle_flags(u, d, 0, 0, 0, 1, OWF_BOLD) == -2 &&
          opr_text_set_size(u, d, 0, 0, 0, 0, 0) == -2 &&
          opr_text_set_colour(u, d, 0, 0, 0, 0, 0x1000000UL) == -2 &&
          opr_text_set_font(u, d, 0, 0, 0, 0, "") == -2 &&
          opr_text_set_alignment(u, d, 0, 0, 0, 0, (owf_align)99) == -2 &&
          opr_text_set_indents(u, d, 0, 0, 0, 0, 100, -101, 0) == -2 &&
          opr_text_set_indents(u, d, 0, 0, 0, 0, -1, 0, 0) == -2 &&
          opr_text_set_indents(u, d, 0, 0, 0, 0, 4100, 0, 100) == -2,
          "invalid formatting ranges and values are rejected");
    opr_undo_clear(u);
    CHECK(!opr_obj_set_text_columns(u, d, 0, 0, 2, 240) &&
          s->objs[0].text_columns == 2 && s->objs[0].text_column_gap == 240,
          "set two columns and their gap on the shared text-frame model");
    {
        opr_slide *laid = opr_slide_layout(d, 0, NULL, NULL, NULL);
        int frame = laid ? opr_slide_frame_for(laid, 0) : -1;
        const ol_box_style *box = frame >= 0 ?
            ol_node_box(ol_node_by_id(laid->doc, laid->frames[frame].node)) : NULL;
        CHECK(box && box->text_columns == 2 &&
              box->text_column_gap == (ol_unit)240 * OL_UNITS_PER_TWIP,
              "GFOpenLayout receives the deck object's columns once");
        opr_slide_free(laid);
    }
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == 0 && !s->objs[0].text_columns &&
          !opr_undo_redo(u, d, &slide, &obj) && obj == 0 && s->objs[0].text_columns == 2,
          "text columns participate in Undo and Redo");
    CHECK(!opr_obj_set_text_frame(u, d, 0, 0, 3, 160, frame_inset) &&
          s->objs[0].text_columns == 3 && s->objs[0].text_column_gap == 160 &&
          s->objs[0].inset[0] == 200 && s->objs[0].inset[3] == 250,
          "set columns, custom gap and four frame margins atomically");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == 0 &&
          s->objs[0].text_columns == 2 && s->objs[0].text_column_gap == 240 &&
          s->objs[0].inset[0] == 120 &&
          !opr_undo_redo(u, d, &slide, &obj) && obj == 0 &&
          s->objs[0].text_columns == 3 && s->objs[0].inset[3] == 250,
          "complete text-frame settings are one object-focused Undo and Redo step");
    CHECK(opr_obj_set_text_columns(u, d, 0, 0, 17, 0) == -2 &&
          opr_obj_set_text_columns(u, d, 0, 0, 3, spec.width) == -2 &&
          opr_obj_set_text_frame(u, d, 0, 0, 2, 100, bad_inset) == -2,
          "invalid text-column and margin geometry is rejected");
    opr_undo_clear(u);
    invalid = spec; invalid.width = 0;
    CHECK(opr_obj_insert_textbox(u, d, 0, &invalid, &selected) == -2 && s->nobjs == 1,
          "an empty text box is refused without changing the slide");
    invalid = spec; invalid.inset[0] = spec.width;
    CHECK(opr_obj_insert_textbox(u, d, 0, &invalid, &selected) == -2 && s->nobjs == 1,
          "text-box insets must leave room for its story");
    invalid = spec; invalid.valign = 99;
    CHECK(opr_obj_insert_textbox(u, d, 0, &invalid, &selected) == -2,
          "an invalid text-box alignment is refused");

    tail = spec;
    tail.y = 4000;
    tail.height = 1200;
    tail.text = NULL;
    CHECK(!opr_obj_insert_textbox(u, d, 0, &tail, &selected) && selected == 1 && s->nobjs == 2,
          "insert an empty continuation text box");
    opr_undo_clear(u);
    CHECK(!opr_obj_link_text_frame(u, d, 0, 0, 0, 1) &&
          s->objs[0].flow_story >= 0 && s->objs[1].flow_story == s->objs[0].flow_story,
          "link a Publisher story to the next empty frame");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == -1 &&
          s->objs[0].flow_story < 0 && s->objs[1].flow_story < 0 &&
          !opr_undo_redo(u, d, &slide, &obj) && obj == -1 &&
          s->objs[0].flow_story >= 0 && s->objs[1].flow_story == s->objs[0].flow_story,
          "linked text frames participate atomically in Undo and Redo");
    CHECK(opr_obj_link_text_frame(u, d, 0, 0, 0, 1) == -2 &&
          opr_obj_unlink_text_frame(u, d, 0, 0) == -2,
          "a linked target cannot be reused and the story-owning first frame stays attached");

    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the authored text box saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the authored text box reads back");
    if (back) {
        const owf_obj *saved = &back->slides[0].objs[0];
        const owf_textlevel *level = owf_deck_para_level(back,
                                      owf_deck_slide_master(back, 0), saved, 0);
        int effective_size = saved->text.paras[0].runs[0].fmt.size > 0 ?
                             saved->text.paras[0].runs[0].fmt.size :
                             level ? level->chars.size : 0;
        int saved_font = saved->text.paras[0].runs[0].fmt.font >= 0 ?
                         saved->text.paras[0].runs[0].fmt.font :
                         level ? level->chars.font : -1;
        int selected_font = saved->text.paras[0].nruns > 0 ?
                            saved->text.paras[0].runs[saved->text.paras[0].nruns - 1].fmt.font : -1;
        text = opr_text_get(back, 0, 0);
        CHECK(back->slides[0].nobjs == 2 && back->slides[0].objs[0].kind == OWF_OBJ_TEXT &&
              text && !strcmp(text, "Publisher story") &&
              back->slides[0].objs[0].x == spec.x &&
              back->slides[0].objs[0].inset[0] == frame_inset[0] &&
              back->slides[0].objs[0].inset[3] == frame_inset[3] &&
              back->slides[0].objs[0].look.fill == spec.look.fill &&
              back->slides[0].objs[0].text_columns == 3 &&
              back->slides[0].objs[0].text_column_gap == 160 &&
              back->slides[0].objs[0].flow_story >= 0 &&
              back->slides[0].objs[1].flow_story == back->slides[0].objs[0].flow_story &&
              back->slides[0].objs[0].text.paras[0].fmt.align == OWF_ALIGN_RIGHT &&
              back->slides[0].objs[0].text.paras[0].fmt.indent_left == 360 &&
              back->slides[0].objs[0].text.paras[0].fmt.indent_first == -180 &&
              back->slides[0].objs[0].text.paras[0].fmt.indent_right == 240 &&
              effective_size == 360 &&
              back->slides[0].objs[0].text.paras[0].runs[0].fmt.colour == 0x336699UL &&
              saved_font >= 0 && saved_font < back->res->nfonts &&
              !strcmp(back->res->fonts[saved_font].name, "Liberation Serif") &&
              saved->text.paras[0].nruns >= 3 &&
              saved->text.paras[0].runs[saved->text.paras[0].nruns - 1].fmt.size == 480 &&
              saved->text.paras[0].runs[saved->text.paras[0].nruns - 1].fmt.colour == 0xcc3300UL &&
              selected_font >= 0 && selected_font < back->res->nfonts &&
              !strcmp(back->res->fonts[selected_font].name, "DejaVu Sans") &&
              (back->slides[0].objs[0].text.paras[0].runs[0].fmt.flags &
               (OWF_ITALIC | OWF_UNDERLINE)) == (OWF_ITALIC | OWF_UNDERLINE),
              "the complete rich text box survives the ODP round trip: align %d size %d flags %u",
              back->slides[0].objs[0].text.paras[0].fmt.align,
              effective_size,
              back->slides[0].objs[0].text.paras[0].runs[0].fmt.flags);
        free(text);
    }
    owf_deck_free(back);
    free(data);
    opr_undo_clear(u);
    CHECK(!opr_text_set(d, 0, 0,
          "This intentionally long Publisher story must continue well beyond a shallow frame. "
          "It repeats enough ordinary words to require several lines and expose overset text. "
          "The layout service, not the application, decides the exact line and frame cut. "
          "More text follows to force another group of lines past the bottom edge. "
          "Publisher must mark that hidden continuation without guessing character counts.") &&
          !opr_obj_set_box(u, d, 0, 0, 1000, 1200, 900, 300) &&
          !opr_obj_set_text_frame(u, d, 0, 0, 1, 0, shallow_inset),
          "prepare a shallow linked frame with an overflowing story");
    CHECK(!opr_obj_unlink_text_frame(u, d, 0, 1) &&
          s->objs[0].flow_story < 0 && s->objs[1].flow_story < 0,
          "removing the tail returns continuation text to the preceding frame");
    {
        opr_slide *laid = opr_slide_layout(d, 0, NULL, NULL, NULL);
        int frame = laid ? opr_slide_frame_for(laid, 0) : -1;
        CHECK(frame >= 0 && opr_frame_text_overflow(laid, frame, NULL, NULL) > 0,
              "the laid-out frame reports visible overflow after its tail is removed");
        opr_slide_free(laid);
    }
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].flow_story >= 0 && s->objs[1].flow_story == s->objs[0].flow_story,
          "undo restores the linked continuation after overflow inspection");
    opr_undo_clear(u);
    tail.y = 6000;
    tail.height = 1000;
    CHECK(!opr_obj_insert_textbox(u, d, 0, &tail, &selected) && selected == 2,
          "insert a third empty frame for extending an existing flow");
    opr_undo_clear(u);
    CHECK(!opr_obj_link_text_frame(u, d, 0, 1, 0, 2) &&
          s->objs[2].flow_story == s->objs[0].flow_story,
          "append an empty frame at the current flow tail");
    CHECK(opr_obj_link_text_frame(u, d, 0, 0, 0, 2) == -2,
          "an existing linked target cannot be reused");
    CHECK(!opr_obj_unlink_text_frame(u, d, 0, 1) && s->objs[1].flow_story < 0 &&
          s->objs[0].flow_story >= 0 && s->objs[2].flow_story == s->objs[0].flow_story,
          "detaching a middle frame closes the remaining flow around it");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[1].flow_story == s->objs[0].flow_story &&
          s->objs[2].flow_story == s->objs[0].flow_story,
          "undo restores the detached middle frame atomically");
    CHECK(!opr_obj_unlink_text_frame(u, d, 0, 1) &&
          !opr_obj_link_text_frame(u, d, 0, 0, 0, 1) &&
          s->objs[1].flow_story == s->objs[0].flow_story &&
          s->objs[2].flow_story == s->objs[0].flow_story,
          "an empty frame can be inserted before the source's current successor");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && s->objs[1].flow_story < 0 &&
          s->objs[2].flow_story == s->objs[0].flow_story &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          s->objs[1].flow_story == s->objs[0].flow_story,
          "middle insertion participates atomically in Undo and Redo");
    opr_undo_free(u);
    owf_deck_free(d);
}

static void character_selection_ranges(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    opr_textbox_spec spec;
    opr_selection selection;
    opr_find_result found;
    opr_pos find_start = {0, 0};
    char *plain = NULL;
    int selected = -1, slide = -1, obj = -1, replacements = 0;
    CHECK(d && s && u, "a deck for cross-paragraph text selection");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    memset(&spec, 0, sizeof spec);
    spec.x = spec.y = 400;
    spec.width = 5000;
    spec.height = 3000;
    spec.valign = OWF_VALIGN_TOP;
    spec.text = NULL;
    CHECK(!opr_obj_insert_textbox(u, d, 0, &spec, &selected) && selected == 0,
          "insert a text box for a range spanning separators");
    CHECK(!opr_text_set(d, 0, 0, "One\nTwo\nThree") &&
          s->objs[0].text.nparas == 3,
          "make three paragraphs for the cross-paragraph range");
    CHECK(opr_text_find(d, 0, 0, find_start, "two", 0, &found) == 1 &&
          found.slide == 0 && found.obj == 0 &&
          found.selection.anchor.para == 1 && found.selection.anchor.offset == 0 &&
          found.selection.focus.offset == 3,
          "case-insensitive Find returns an exact deck selection");
    find_start = found.selection.focus;
    CHECK(opr_text_find(d, 0, 0, find_start, "One", 1, &found) == 1 &&
          found.selection.anchor.para == 0 && found.selection.anchor.offset == 0,
          "Find Next wraps once through the deck");
    opr_undo_clear(u);
    selection.anchor.para = 0; selection.anchor.offset = 2;
    selection.focus.para = 2; selection.focus.offset = 2;
    plain = opr_text_selection_get(d, 0, 0, &selection, NULL);
    CHECK(plain && !strcmp(plain, "e\nTwo\nTh"),
          "extract exact cross-paragraph selection for clipboard exchange");
    free(plain); plain = NULL;
    CHECK(!opr_text_toggle_flags_selection(u, d, 0, 0, &selection, OWF_BOLD) &&
          s->objs[0].text.paras[0].nruns == 2 &&
          !(s->objs[0].text.paras[0].runs[0].fmt.flags & OWF_BOLD) &&
          (s->objs[0].text.paras[0].runs[1].fmt.flags & OWF_BOLD) &&
          (s->objs[0].text.paras[1].runs[0].fmt.flags & OWF_BOLD) &&
          s->objs[0].text.paras[2].nruns == 2 &&
          (s->objs[0].text.paras[2].runs[0].fmt.flags & OWF_BOLD) &&
          !(s->objs[0].text.paras[2].runs[1].fmt.flags & OWF_BOLD),
          "a cross-paragraph selection formats only its two partial ends and full middle");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          s->objs[0].text.paras[0].nruns == 1 && s->objs[0].text.paras[2].nruns == 1 &&
          !opr_undo_redo(u, d, &slide, &obj),
          "cross-paragraph range formatting has one Undo and Redo snapshot");
    CHECK(!opr_undo_push(u, d, 0, 0) &&
          !opr_text_delete_selection(d, 0, 0, &selection) &&
          opr_text_selection_empty(&selection) && selection.focus.para == 0 &&
          selection.focus.offset == 2 &&
          (plain = opr_text_get(d, 0, 0)) != NULL && !strcmp(plain, "Onree"),
          "deleting across paragraphs joins the two unselected tails once");
    free(plain); plain = NULL;
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          (plain = opr_text_get(d, 0, 0)) != NULL && !strcmp(plain, "One\nTwo\nThree"),
          "Undo restores cross-paragraph text and formatting");
    free(plain); plain = NULL;
    selection.anchor.para = selection.focus.para = 1;
    selection.anchor.offset = selection.focus.offset = 1;
    CHECK(opr_text_toggle_flags_selection(u, d, 0, 0, &selection, OWF_BOLD) == -2,
          "an empty character selection is left to the caller's typing format policy");
    opr_undo_clear(u);
    CHECK(!opr_text_set(d, 0, 0, "One two TWO two"), "set Replace All source text");
    plain = opr_text_get(d, 0, 0);
    CHECK(plain && !strcmp(plain, "One two TWO two"),
          "replacing prior formatted content leaves no stale run bytes");
    free(plain); plain = NULL;
    CHECK(!opr_text_replace_all(u, d, 0, 0, "two", "second", 0,
                                &replacements, &find_start) &&
          replacements == 3 && find_start.para == 0 && find_start.offset == 24 &&
          (plain = opr_text_get(d, 0, 0)) != NULL &&
          !strcmp(plain, "One second second second"),
          "Replace All folds ASCII case and reports the final caret");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) &&
          (free(plain), plain = opr_text_get(d, 0, 0)) != NULL &&
          !strcmp(plain, "One two TWO two") &&
          !opr_undo_redo(u, d, &slide, &obj) &&
          (free(plain), plain = opr_text_get(d, 0, 0)) != NULL &&
          !strcmp(plain, "One second second second"),
          "all replacements share one Undo and Redo snapshot");
    CHECK(opr_text_replace_all(u, d, 0, 0, "", "x", 0,
                               &replacements, &find_start) == -2,
          "Replace All rejects an empty search without changing text");
    free(plain);
    opr_undo_free(u);
    owf_deck_free(d);
}

static void picture_insertion(void)
{
    static const unsigned char png[] = {
        0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R',
        0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0, 0x90, 0x77, 0x53, 0xde,
        0, 0, 0, 12, 'I', 'D', 'A', 'T', 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0,
        0, 0, 0x03, 0x01, 0x01, 0, 0x18, 0xdd, 0x8d, 0xb0,
        0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82
    };
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    const owf_deck_format *odp = owf_deck_format_named("odp");
    opr_picture_spec spec, invalid;
    int edited_crop[4] = {133, 67, 200, 200};
    int bad_crop[4] = {600, 0, 400, 0};
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    int selected = -1, slide = -1, obj = -1;
    CHECK(d && s && u, "a deck for picture insertion");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    memset(&spec, 0, sizeof spec);
    spec.x = 900; spec.y = 700; spec.width = 4800; spec.height = 3200;
    /* Multiples representable by this deliberately tiny 1px fixture. */
    spec.crop[0] = 67; spec.crop[1] = 67; spec.crop[2] = 67; spec.crop[3] = 133;
    spec.name = "tiny.png"; spec.mime = "image/png"; spec.alt = "Tiny test picture";
    spec.data = png; spec.length = sizeof png;
    spec.natural_width = 1440; spec.natural_height = 960;
    CHECK(!opr_obj_insert_picture(u, d, 0, &spec, &selected) && selected == 0 &&
          s->nobjs == 1 && d->res->nimages == 1 && s->objs[0].kind == OWF_OBJ_PICTURE,
          "insert, embed and select a picture");
    CHECK(s->objs[0].image == 0 && s->objs[0].x == spec.x &&
          s->objs[0].width == spec.width && s->objs[0].crop[3] == 133 &&
          s->objs[0].name && !strcmp(s->objs[0].name, spec.name) &&
          s->objs[0].alt && !strcmp(s->objs[0].alt, spec.alt) &&
          d->res->images[0].length == sizeof png &&
          !memcmp(d->res->images[0].data, png, sizeof png),
          "the picture owns its geometry, crop, description and resource bytes");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == -1 && !s->nobjs,
          "undo picture insertion removes the object");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && obj == 0 && s->nobjs == 1 &&
          s->objs[0].kind == OWF_OBJ_PICTURE && s->objs[0].image == 0,
          "redo picture insertion restores and selects it");
    opr_undo_clear(u);
    CHECK(!opr_obj_set_picture_crop(u, d, 0, 0, edited_crop) &&
          !memcmp(s->objs[0].crop, edited_crop, sizeof edited_crop),
          "edit all four picture crop edges as one operation");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == 0 &&
          s->objs[0].crop[0] == spec.crop[0] && s->objs[0].crop[3] == spec.crop[3] &&
          !opr_undo_redo(u, d, &slide, &obj) && obj == 0 &&
          !memcmp(s->objs[0].crop, edited_crop, sizeof edited_crop),
          "picture crop has one Undo and Redo snapshot");
    CHECK(opr_obj_set_picture_crop(u, d, 0, 0, bad_crop) == -2 &&
          !memcmp(s->objs[0].crop, edited_crop, sizeof edited_crop),
          "a crop that removes the source area is rejected transactionally");
    invalid = spec; invalid.data = NULL;
    CHECK(opr_obj_insert_picture(u, d, 0, &invalid, &selected) == -2 &&
          s->nobjs == 1 && d->res->nimages == 1,
          "missing picture bytes are refused without orphaning a resource");
    invalid = spec; invalid.crop[0] = 950; invalid.crop[2] = 50;
    CHECK(opr_obj_insert_picture(u, d, 0, &invalid, &selected) == -2 &&
          s->nobjs == 1 && d->res->nimages == 1,
          "a crop that removes the picture is refused transactionally");
    invalid = spec; invalid.x = d->width - spec.width + 1;
    CHECK(opr_obj_insert_picture(u, d, 0, &invalid, &selected) == -2 &&
          s->nobjs == 1 && d->res->nimages == 1,
          "an out-of-page picture is refused transactionally");

    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the authored picture saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the authored picture reads back");
    if (back) {
        const owf_obj *saved = &back->slides[0].objs[0];
        CHECK(back->slides[0].nobjs == 1 && saved->kind == OWF_OBJ_PICTURE &&
              saved->image >= 0 && saved->image < back->res->nimages,
              "the picture object and resource survive the ODP round trip");
        CHECK(saved->x == spec.x && saved->width == spec.width,
              "picture geometry survives the ODP round trip: %d/%d, %d/%d",
              saved->x, spec.x, saved->width, spec.width);
        CHECK(saved->crop[0] == edited_crop[0] && saved->crop[3] == edited_crop[3],
              "picture crop survives the ODP round trip: %d/%d, %d/%d",
              saved->crop[0], edited_crop[0], saved->crop[3], edited_crop[3]);
        CHECK(saved->alt && !strcmp(saved->alt, spec.alt),
              "picture description survives the ODP round trip: %s",
              saved->alt ? saved->alt : "(null)");
        CHECK(saved->image >= 0 && saved->image < back->res->nimages &&
              back->res->images[saved->image].length == sizeof png,
              "picture bytes survive the ODP round trip");
    }
    owf_deck_free(back);
    free(data);
    opr_undo_free(u);
    owf_deck_free(d);
}

static void object_clipboard_paste(void)
{
    static const unsigned char image_bytes[] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    owf_deck *source = owf_deck_new(10000, 8000);
    owf_deck *dest = owf_deck_new(10000, 8000);
    owf_deck *round = NULL;
    owf_slide *ss = source ? owf_deck_add_slide(source, -1, -1) : NULL;
    owf_slide *ds = dest ? owf_deck_add_slide(dest, -1, -1) : NULL;
    opr_undo *su = opr_undo_new(), *du = opr_undo_new();
    opr_textbox_spec text_spec;
    opr_picture_spec picture_spec;
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t data_length = 0;
    int selected = -1, font, slide = -1, obj = -1;
    CHECK(source && dest && ss && ds && su && du, "decks for cross-document object paste");
    if (!source || !dest || !ss || !ds || !su || !du) goto out;
    memset(&text_spec, 0, sizeof text_spec);
    text_spec.x = 600; text_spec.y = 700; text_spec.width = 3200; text_spec.height = 1200;
    text_spec.inset[0] = text_spec.inset[2] = 100;
    text_spec.inset[1] = text_spec.inset[3] = 60;
    text_spec.text = "Clipboard object";
    CHECK(!opr_obj_insert_textbox(su, source, 0, &text_spec, &selected) && selected == 0,
          "prepare a source text object");
    font = owf_doc_font(source->res, "Clipboard Serif", OWF_FONT_SERIF);
    CHECK(font >= 0, "prepare a source-only font resource");
    if (font >= 0) ss->objs[0].text.paras[0].runs[0].fmt.font = font;
    memset(&picture_spec, 0, sizeof picture_spec);
    picture_spec.x = 4200; picture_spec.y = 900; picture_spec.width = 1800; picture_spec.height = 1400;
    picture_spec.name = "clipboard.png"; picture_spec.mime = "image/png";
    picture_spec.data = image_bytes; picture_spec.length = sizeof image_bytes;
    picture_spec.natural_width = 1800; picture_spec.natural_height = 1400;
    CHECK(!opr_obj_insert_picture(su, source, 0, &picture_spec, &selected) && selected == 1,
          "prepare a source picture object");
    CHECK(odp && odp->export && odp->export(source, &data, &data_length, NULL) == OWF_OK && data_length > 0 &&
          owf_import_deck_memory(data, data_length, "odp", &round, NULL, NULL) == OWF_OK && round,
          "round-trip the object clipboard's ODP transport");
    if (!round) goto out;
    selected = -1;
    CHECK(!opr_obj_paste(du, dest, 0, round, 0, 0, 240, 240, &selected) && selected == 0 &&
          ds->nobjs == 1 && ds->objs[0].x == text_spec.x + 240 &&
          ds->objs[0].text.paras[0].runs[0].fmt.font >= 0 &&
          !strcmp(dest->res->fonts[ds->objs[0].text.paras[0].runs[0].fmt.font].name,
                  "Clipboard Serif"),
          "paste text with an independently remapped font resource");
    CHECK(!opr_obj_paste(du, dest, 0, round, 0, 1, 240, 240, &selected) && selected == 1 &&
          ds->nobjs == 2 && ds->objs[1].image >= 0 &&
          dest->res->images[ds->objs[1].image].length == sizeof image_bytes &&
          !memcmp(dest->res->images[ds->objs[1].image].data, image_bytes, sizeof image_bytes),
          "paste a picture with independent embedded resource bytes");
    CHECK(!opr_undo_undo(du, dest, &slide, &obj) && ds->nobjs == 1 && obj == -1 &&
          !opr_undo_redo(du, dest, &slide, &obj) && ds->nobjs == 2 && obj == 1,
          "pasted object presence participates in Undo and Redo");
    CHECK(opr_obj_paste(du, dest, 0, round, 0, 99, 0, 0, &selected) == -1,
          "object paste rejects an invalid source index");
out:
    opr_undo_free(su);
    opr_undo_free(du);
    free(data);
    owf_deck_free(round);
    owf_deck_free(source);
    owf_deck_free(dest);
}

static int text_crosses(const opr_slide *laid, int frame,
                        ol_unit x0, ol_unit y0, ol_unit x1, ol_unit y1)
{
    size_t i;
    for (i = 0; laid && i < ol_display_count(laid->doc); ++i) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        ol_rect r;
        if (op->type != OL_OP_TEXT || opr_slide_frame_of(laid, op) != frame)
            continue;
        r = op->u.text.bounds;
        if (r.x < x1 && r.x + r.width > x0 && r.y < y1 && r.y + r.height > y0)
            return 1;
    }
    return 0;
}

static ol_unit text_left_segment_edge(const opr_slide *laid, int frame,
                                      ol_unit y0, ol_unit y1, ol_unit xlimit)
{
    ol_unit right = 0;
    ol_unit first_y = INT32_MAX;
    size_t i;
    for (i = 0; laid && i < ol_display_count(laid->doc); ++i) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        ol_rect r;
        if (op->type != OL_OP_TEXT || opr_slide_frame_of(laid, op) != frame)
            continue;
        r = op->u.text.bounds;
        if (r.y < y1 && r.y + r.height > y0 && r.y < first_y)
            first_y = r.y;
    }
    for (i = 0; laid && i < ol_display_count(laid->doc); ++i) {
        const ol_display_op *op = ol_display_get(laid->doc, i);
        ol_rect r;
        if (op->type != OL_OP_TEXT || opr_slide_frame_of(laid, op) != frame)
            continue;
        r = op->u.text.bounds;
        if (r.y == first_y && r.x < xlimit &&
            r.x + r.width > right)
            right = r.x + r.width;
    }
    return right;
}

static void object_wrapping(void)
{
    owf_deck *d = owf_deck_new(10000, 8000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    opr_undo *u = opr_undo_new();
    opr_textbox_spec text_spec;
    opr_shape_spec shape_spec;
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t length = 0;
    owf_deck *back = NULL;
    int selected = -1, slide = -1, obj = -1;
    int gap[4] = {100, 100, 100, 100};
    int square_gap[4] = {400, 400, 400, 400};
    int tight_gap[4] = {0, 0, 0, 0};
    int edited_gap[4] = {250, 300, 350, 400};
    int bad_gap[4] = {100, -1, 100, 100};
    opr_slide *laid;
    int frame;
    ol_unit square_top_right = 0, tight_top_right;
    ol_unit x0 = (ol_unit)3400 * OL_UNITS_PER_TWIP;
    ol_unit y0 = (ol_unit)900 * OL_UNITS_PER_TWIP;
    ol_unit x1 = (ol_unit)5100 * OL_UNITS_PER_TWIP;
    ol_unit y1 = (ol_unit)2500 * OL_UNITS_PER_TWIP;

    CHECK(d && s && u, "a deck for object text wrapping");
    if (!d || !s || !u) {
        opr_undo_free(u);
        owf_deck_free(d);
        return;
    }
    memset(&text_spec, 0, sizeof text_spec);
    text_spec.x = 1000; text_spec.y = 1000;
    text_spec.width = 6000; text_spec.height = 3200;
    text_spec.text =
        "a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a "
        "a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a "
        "a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a";
    CHECK(!opr_obj_insert_textbox(u, d, 0, &text_spec, &selected) && selected == 0,
          "insert the text frame that will flow around an object");
    memset(&shape_spec, 0, sizeof shape_spec);
    shape_spec.shape = OWF_SHAPE_ELLIPSE;
    shape_spec.x = 3500; shape_spec.y = 1000;
    shape_spec.width = 1500; shape_spec.height = 1400;
    shape_spec.look.fill = OWF_SHADE(0x6699cc);
    CHECK(!opr_obj_insert_shape(u, d, 0, &shape_spec, &selected) && selected == 1,
          "insert the overlapping shape");
    opr_undo_clear(u);
    CHECK(!opr_obj_set_wrap(u, d, 0, 1, OWF_WRAP_SQUARE, square_gap) &&
          s->objs[1].wrap == OWF_WRAP_SQUARE && s->objs[1].wrap_gap[0] == 400,
          "make the shape a square exclusion");
    laid = opr_slide_layout(d, 0, NULL, NULL, NULL);
    frame = laid ? opr_slide_frame_for(laid, 0) : -1;
    CHECK(frame >= 0 && !text_crosses(laid, frame, x0, y0, x1, y1),
          "GFOpenLayout keeps frame text outside the square exclusion");
    square_top_right = text_left_segment_edge(laid, frame,
                                              (ol_unit)900 * OL_UNITS_PER_TWIP,
                                              (ol_unit)1500 * OL_UNITS_PER_TWIP,
                                              (ol_unit)4250 * OL_UNITS_PER_TWIP);
    opr_slide_free(laid);
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && obj == 1 && s->objs[1].wrap == OWF_WRAP_NONE &&
          !opr_undo_redo(u, d, &slide, &obj) && obj == 1 && s->objs[1].wrap == OWF_WRAP_SQUARE,
          "object wrapping participates in Undo and Redo");
    CHECK(!opr_obj_set_wrap(u, d, 0, 1, OWF_WRAP_SQUARE, edited_gap) &&
          s->objs[1].wrap_gap[0] == 250 && s->objs[1].wrap_gap[3] == 400,
          "change four wrap distances without changing the wrap kind");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && s->objs[1].wrap_gap[0] == 400 &&
          !opr_undo_redo(u, d, &slide, &obj) && s->objs[1].wrap_gap[3] == 400,
          "wrap-distance editing is one object-focused Undo and Redo step");
    CHECK(opr_obj_set_wrap(u, d, 0, 1, OWF_WRAP_SQUARE, bad_gap) == -2,
          "negative wrap distances are rejected");
    CHECK(!opr_obj_set_wrap(u, d, 0, 1, OWF_WRAP_TOP_BOTTOM, gap),
          "switch the same object to top-and-bottom wrapping");
    laid = opr_slide_layout(d, 0, NULL, NULL, NULL);
    frame = laid ? opr_slide_frame_for(laid, 0) : -1;
    CHECK(frame >= 0 && !text_crosses(laid, frame,
          (ol_unit)1000 * OL_UNITS_PER_TWIP, y0,
          (ol_unit)7000 * OL_UNITS_PER_TWIP, y1),
          "top-and-bottom wrapping leaves no frame text beside the shape");
    opr_slide_free(laid);

    CHECK(!opr_obj_set_wrap(u, d, 0, 1, OWF_WRAP_TIGHT, tight_gap),
          "switch the ellipse to tight contour wrapping");
    laid = opr_slide_layout(d, 0, NULL, NULL, NULL);
    frame = laid ? opr_slide_frame_for(laid, 0) : -1;
    tight_top_right = text_left_segment_edge(laid, frame,
                                             (ol_unit)900 * OL_UNITS_PER_TWIP,
                                             (ol_unit)1500 * OL_UNITS_PER_TWIP,
                                             (ol_unit)4250 * OL_UNITS_PER_TWIP);
    CHECK(frame >= 0 && tight_top_right > square_top_right,
          "tight wrapping uses more of the ellipse corner: %ld > %ld",
          (long)tight_top_right, (long)square_top_right);
    CHECK(frame >= 0 && !text_crosses(laid, frame,
          (ol_unit)4100 * OL_UNITS_PER_TWIP, (ol_unit)1200 * OL_UNITS_PER_TWIP,
          (ol_unit)4500 * OL_UNITS_PER_TWIP, (ol_unit)2200 * OL_UNITS_PER_TWIP),
          "tight wrapping still keeps text out of the ellipse contour");
    opr_slide_free(laid);

    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK,
          "the wrapped publication saves as ODP");
    CHECK(owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back,
          "the wrapped publication reads back");
    if (back)
        CHECK(back->slides[0].nobjs == 2 &&
              back->slides[0].objs[1].wrap == OWF_WRAP_TIGHT &&
              back->slides[0].objs[1].wrap_gap[0] == 0 &&
              back->slides[0].objs[1].wrap_gap[3] == 0,
              "tight wrap kind and distances survive the ODP round trip");
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
    textbox_insertion();
    character_selection_ranges();
    picture_insertion();
    object_clipboard_paste();
    object_wrapping();
    printf("test_opr_edit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
