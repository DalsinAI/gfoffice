/*
 * test_opr_outline: the outline's core (src/deck/opr_outline.c):
 *
 *   - the rows: each slide's title, then its outline box's paragraphs at
 *     their levels; a box still showing its prompt has none;
 *   - a row's text set, a title's and a paragraph's, other paragraphs kept;
 *   - Return after a title (a new slide, the same layout) and after body
 *     text (a new paragraph at its level);
 *   - demote and promote: a level down and up; a title demoted joins the
 *     slide before; a top-level paragraph promoted starts a slide, taking
 *     the paragraphs after it; the first title and a title promoted refuse;
 *   - delete: a paragraph, the last paragraph, a title's whole slide;
 *   - each change one Undo step that puts the deck back, and Redo;
 *   - the outline's result saved as ODP and read back.
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

#define CONTENT 2                /* owf_deck_new's "Title, Content" */

/* The whole outline as one string: rows as level digit + text, ";" between. */
static const char *dump(const owf_deck *d)
{
    static char buf[2048];
    opr_outline o;
    size_t at = 0;
    int i;
    memset(&o, 0, sizeof o);
    buf[0] = 0;
    if (opr_outline_build(d, &o) < 0)
        return "(no memory)";
    for (i = 0; i < o.count && at + 64 < sizeof buf; i++) {
        char *t = opr_outline_text(d, &o.rows[i]);
        at += (size_t)snprintf(buf + at, sizeof buf - at, "%s%d%s", i ? ";" : "", o.rows[i].level, t ? t : "?");
        owf_free(t);
    }
    opr_outline_free(&o);
    return buf;
}

static opr_outline_row row(const owf_deck *d, int index)
{
    opr_outline o;
    opr_outline_row r;
    memset(&o, 0, sizeof o);
    memset(&r, 0, sizeof r);
    r.slide = -1;
    if (opr_outline_build(d, &o) == 0 && index >= 0 && index < o.count)
        r = o.rows[index];
    opr_outline_free(&o);
    return r;
}

/* Two slides: "One" with a, b (level 2), c; "Two" with d. */
static owf_deck *deck_of(opr_undo *u)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    int sel = 0, body;
    if (!d)
        return NULL;
    opr_slide_insert(u, d, 0, CONTENT, &sel);
    opr_slide_insert(u, d, 1, CONTENT, &sel);
    opr_text_set(d, 0, opr_outline_title_obj(d, 0), "One");
    body = opr_outline_body_obj(d, 0);
    opr_text_set(d, 0, body, "a\nb\nc");
    owf_obj_set_level(&d->slides[0].objs[body], 1, 1);
    opr_text_set(d, 1, opr_outline_title_obj(d, 1), "Two");
    opr_text_set(d, 1, opr_outline_body_obj(d, 1), "d");
    opr_undo_clear(u);
    return d;
}

static void rows(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(u);
    int sel = 2;
    opr_outline o;
    CHECK(!strcmp(dump(d), "0One;1a;2b;1c;0Two;1d"), "the rows: %s", dump(d));
    opr_slide_insert(u, d, 2, CONTENT, &sel);
    CHECK(!strcmp(dump(d), "0One;1a;2b;1c;0Two;1d;0"), "a new slide: its empty title, no body rows: %s", dump(d));
    opr_slide_insert(u, d, 3, 0, &sel);
    CHECK(!strcmp(dump(d), "0One;1a;2b;1c;0Two;1d;0;0"), "a blank slide: one row: %s", dump(d));
    memset(&o, 0, sizeof o);
    opr_outline_build(d, &o);
    CHECK(o.count == 8 && o.rows[7].obj < 0 && o.rows[2].para == 1 && o.rows[2].slide == 0, "rows' places");
    CHECK(opr_outline_find(&o, 1, -1, 0) == 4 && opr_outline_find(&o, 0, o.rows[3].obj, 2) == 3 &&
          opr_outline_find(&o, 5, -1, 0) < 0, "rows found");
    opr_outline_free(&o);
    owf_deck_free(d);
    opr_undo_free(u);
}

static void editing(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(u);
    opr_outline_row r;
    int s = -1, p = -1, sel = 0;

    r = row(d, 2);
    CHECK(!opr_outline_set_text(u, d, &r, "bee") && !strcmp(dump(d), "0One;1a;2bee;1c;0Two;1d"), "b typed over: %s",
          dump(d));
    r = row(d, 0);
    CHECK(!opr_outline_set_text(u, d, &r, "First") && !strcmp(dump(d), "0First;1a;2bee;1c;0Two;1d"), "a title: %s",
          dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && !strcmp(dump(d), "0One;1a;2bee;1c;0Two;1d"), "Undo the title");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && !strcmp(dump(d), "0One;1a;2b;1c;0Two;1d"), "Undo the paragraph");

    r = row(d, 2);
    CHECK(!opr_outline_insert_after(u, d, &r, &s, &p) && s == 0 && p == 2 &&
          !strcmp(dump(d), "0One;1a;2b;2;1c;0Two;1d"), "Return after b: a paragraph at its level: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && !strcmp(dump(d), "0One;1a;2b;1c;0Two;1d"), "Undo it");
    r = row(d, 0);
    CHECK(!opr_outline_insert_after(u, d, &r, &s, &p) && s == 1 && d->nslides == 3 &&
          d->slides[1].layout == CONTENT && !strcmp(dump(d), "0One;1a;2b;1c;0;0Two;1d"),
          "Return after a title: a slide after it: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->nslides == 2, "Undo the new slide");

    /* levels */
    r = row(d, 1);
    CHECK(!opr_outline_demote(u, d, &r) && !strcmp(dump(d), "0One;2a;2b;1c;0Two;1d"), "a demoted: %s", dump(d));
    r = row(d, 1);
    CHECK(!opr_outline_promote(u, d, &r, &s) && s == 0 && !strcmp(dump(d), "0One;1a;2b;1c;0Two;1d"), "a promoted back");
    r = row(d, 0);
    CHECK(opr_outline_promote(u, d, &r, &s) == -2, "a title goes no higher");
    CHECK(opr_outline_demote(u, d, &r) == -2, "the first title joins nothing");

    /* a top-level paragraph promoted: a slide of its own, the rest under it */
    r = row(d, 1);
    CHECK(!opr_outline_promote(u, d, &r, &s) && s == 1 && d->nslides == 3 &&
          !strcmp(dump(d), "0One;0a;1b;1c;0Two;1d"), "a promoted to a slide, b and c under it: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->nslides == 2 && !strcmp(dump(d), "0One;1a;2b;1c;0Two;1d"),
          "one Undo: %s", dump(d));
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && d->nslides == 3 && !strcmp(dump(d), "0One;0a;1b;1c;0Two;1d"),
          "Redo: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL), "back");
    r = row(d, 3);
    CHECK(!opr_outline_promote(u, d, &r, &s) && s == 1 && !strcmp(dump(d), "0One;1a;2b;0c;0Two;1d"),
          "the last paragraph promoted: a slide with only a title: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL), "back");

    /* a title demoted: its slide joins the one before */
    r = row(d, 4);
    CHECK(!opr_outline_demote(u, d, &r) && d->nslides == 1 && !strcmp(dump(d), "0One;1a;2b;1c;1Two;2d"),
          "Two joins One: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->nslides == 2 && !strcmp(dump(d), "0One;1a;2b;1c;0Two;1d"),
          "one Undo puts Two back: %s", dump(d));
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && d->nslides == 1, "Redo");
    CHECK(!opr_undo_undo(u, d, NULL, NULL), "back");

    /* delete */
    r = row(d, 2);
    CHECK(!opr_outline_delete(u, d, &r) && !strcmp(dump(d), "0One;1a;1c;0Two;1d"), "b deleted: %s", dump(d));
    r = row(d, 1);
    CHECK(!opr_outline_delete(u, d, &r) && !strcmp(dump(d), "0One;1c;0Two;1d"), "a deleted: %s", dump(d));
    r = row(d, 4 - 1);
    CHECK(!opr_outline_delete(u, d, &r) && !strcmp(dump(d), "0One;1c;0Two"), "d, the only one: %s", dump(d));
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && !strcmp(dump(d), "0One;1c;0Two;1d"), "Undo: %s", dump(d));
    r = row(d, 2);
    CHECK(!opr_outline_delete(u, d, &r) && d->nslides == 1 && !strcmp(dump(d), "0One;1c"), "Two deleted: %s",
          dump(d));
    r = row(d, 0);
    CHECK(opr_outline_delete(u, d, &r) == -2, "the last slide stays");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->nslides == 2, "Undo");

    /* a slide with no title box */
    opr_slide_insert(u, d, 2, 0, &sel);
    r = row(d, 4);
    CHECK(r.obj < 0 && opr_outline_set_text(u, d, &r, "x") == -2, "no title box: -2");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void saving(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(u), *back = NULL;
    const owf_deck_format *odp = owf_deck_format_named("odp");
    opr_outline_row r;
    unsigned char *data = NULL;
    size_t n = 0;
    int s;
    r = row(d, 3);
    opr_outline_promote(u, d, &r, &s);
    r = row(d, 2);
    opr_outline_demote(u, d, &r);
    CHECK(!strcmp(dump(d), "0One;1a;3b;0c;0Two;1d"), "before saving: %s", dump(d));
    CHECK(odp && odp->export && odp->export(d, &data, &n, NULL) == OWF_OK, "saved as ODP");
    CHECK(data && owf_import_deck_memory(data, n, "odp", &back, NULL, NULL) == OWF_OK && back &&
          !strcmp(dump(back), "0One;1a;3b;0c;0Two;1d"), "read back: %s", back ? dump(back) : "(none)");
    owf_free(data);
    owf_deck_free(back);
    owf_deck_free(d);
    opr_undo_free(u);
}

int main(void)
{
    rows();
    editing();
    saving();
    printf("test_opr_outline: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
