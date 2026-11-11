/*
 * test_opr_pptx: a deck saved as PowerPoint (src/deck/opr_pptx.c) and read
 * back with the PPTX reader:
 *
 *   - the slide size and the slide count;
 *   - each slide's title and its outline paragraphs at their levels;
 *   - the speaker notes, a hidden slide, a slide's own background;
 *   - a shape with its text, where it was;
 *   - saved twice, the same bytes;
 *   - a deck after an outline change (a paragraph promoted to a slide).
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
    owf_obj_set_level(&d->slides[0].objs[body], 1, 2);
    opr_text_set(d, 1, opr_outline_title_obj(d, 1), "Two");
    opr_text_set(d, 1, opr_outline_body_obj(d, 1), "d");
    opr_undo_clear(u);
    return d;
}

/* Saved as PPTX and read back; NULL when either fails. */
static owf_deck *round_trip(const owf_deck *d, unsigned char **data, size_t *n)
{
    owf_deck *back = NULL;
    *data = NULL;
    *n = 0;
    if (opr_export_pptx(d, data, n, NULL) != OWF_OK || !*data)
        return NULL;
    if (owf_import_deck_memory(*data, *n, "pptx", &back, NULL, NULL) != OWF_OK)
        return NULL;
    return back;
}

/* The first object on a slide whose text is utf8, or -1. */
static int obj_with(const owf_deck *d, int slide, const char *utf8)
{
    int i;
    for (i = 0; i < d->slides[slide].nobjs; i++) {
        char *t = opr_text_get(d, slide, i);
        int same = t && !strcmp(t, utf8);
        owf_free(t);
        if (same)
            return i;
    }
    return -1;
}

static void basics(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(u), *back;
    unsigned char *data, *again = NULL;
    size_t n, m = 0;
    char *notes;

    opr_notes_set(u, d, 0, "Say hello\nThen the plan");
    d->slides[1].hidden = 1;
    d->slides[1].background = OWF_SHADE(0x336699);

    back = round_trip(d, &data, &n);
    CHECK(back != NULL, "saved and read back");
    if (!back)
        goto out;
    CHECK(back->width == d->width && back->height == d->height, "the slide size: %dx%d", back->width, back->height);
    CHECK(back->nslides == 2, "two slides: %d", back->nslides);
    CHECK(!strcmp(dump(back), "0One;1a;2b;1c;0Two;1d"), "the outline: %s", dump(back));
    notes = opr_notes_get(back, 0);
    CHECK(notes && !strcmp(notes, "Say hello\nThen the plan"), "the notes: %s", notes ? notes : "(none)");
    owf_free(notes);
    notes = opr_notes_get(back, 1);
    CHECK(!notes || !*notes, "no notes on Two: %s", notes ? notes : "");
    owf_free(notes);
    CHECK(!back->slides[0].hidden && back->slides[1].hidden, "Two hidden");
    CHECK(back->slides[1].background == OWF_SHADE(0x336699), "Two's background: %06lx",
          back->slides[1].background & 0xFFFFFFUL);
    CHECK(opr_export_pptx(d, &again, &m, NULL) == OWF_OK && m == n && !memcmp(again, data, n),
          "saved twice, the same bytes");
    owf_deck_free(back);
out:
    owf_free(data);
    owf_free(again);
    owf_deck_free(d);
    opr_undo_free(u);
}

static void shapes(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(u), *back;
    unsigned char *data;
    size_t n;
    int obj = -1, i;
    opr_shape_spec want;
    const owf_obj *o;

    memset(&want, 0, sizeof want);
    want.shape = OWF_SHAPE_ELLIPSE;
    want.x = 1440;
    want.y = 1440;
    want.width = 2880;
    want.height = 1440;
    want.look.fill = OWF_SHADE(0xFFCC00);
    if (opr_obj_insert_shape(u, d, 1, &want, &obj) == 0 && obj >= 0)
        opr_text_set(d, 1, obj, "Round");
    CHECK(obj >= 0, "a shape added");
    back = round_trip(d, &data, &n);
    CHECK(back != NULL, "saved and read back");
    if (back) {
        i = obj_with(back, 1, "Round");
        o = i >= 0 ? &back->slides[1].objs[i] : NULL;
        CHECK(o && o->kind == OWF_OBJ_SHAPE && o->shape == OWF_SHAPE_ELLIPSE, "the ellipse and its text");
        if (o) {
            CHECK(o->x == want.x && o->y == want.y && o->width == want.width && o->height == want.height,
                  "where it was: %d,%d %dx%d", o->x, o->y, o->width, o->height);
            CHECK(o->look.fill == OWF_SHADE(0xFFCC00), "its fill: %06lx", o->look.fill & 0xFFFFFFUL);
        }
    }
    owf_free(data);
    owf_deck_free(back);
    owf_deck_free(d);
    opr_undo_free(u);
}

static void after_outline(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = deck_of(u), *back;
    opr_outline o;
    opr_outline_row r;
    unsigned char *data;
    size_t n;
    int s;

    memset(&o, 0, sizeof o);
    opr_outline_build(d, &o);
    r = o.rows[3];
    opr_outline_free(&o);
    opr_outline_promote(u, d, &r, &s);
    CHECK(!strcmp(dump(d), "0One;1a;2b;0c;0Two;1d"), "c promoted: %s", dump(d));
    back = round_trip(d, &data, &n);
    CHECK(back && back->nslides == 3 && !strcmp(dump(back), "0One;1a;2b;0c;0Two;1d"), "read back: %s",
          back ? dump(back) : "(none)");
    owf_free(data);
    owf_deck_free(back);
    owf_deck_free(d);
    opr_undo_free(u);
}

int main(void)
{
    basics();
    shapes();
    after_outline();
    printf("test_opr_pptx: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
