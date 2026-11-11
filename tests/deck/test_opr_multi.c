/*
 * test_opr_multi: several objects at once (src/deck/opr_multi.c and the
 * groups in src/deck/opr_edit.c):
 *
 *   - moved together, held on the slide;
 *   - aligned to the box round them, one alone to the slide;
 *   - spaced evenly across and down;
 *   - grouped (drawing order kept, the group's box round them), moved and
 *     aligned as one, ungrouped; deleted with what is inside;
 *   - picked by a rectangle;
 *   - each change one Undo step, and Redo;
 *   - a group saved as ODP and read back, and saved as PPTX;
 *   - refusals.
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

static int add_shape(opr_undo *u, owf_deck *d, int slide, owf_shape shape, int x, int y, int w, int h)
{
    opr_shape_spec spec;
    int obj = -1;
    memset(&spec, 0, sizeof spec);
    spec.shape = shape;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.look.fill = OWF_SHADE(0x3366CC);
    if (opr_obj_insert_shape(u, d, slide, &spec, &obj) < 0)
        return -1;
    return obj;
}

/* A slide with a title and body, and three shapes after them: A, B, C. */
static owf_deck *deck_of(opr_undo *u, int *a, int *b, int *c)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    int sel = 0;
    if (!d)
        return NULL;
    opr_slide_insert(u, d, 0, CONTENT, &sel);
    *a = add_shape(u, d, 0, OWF_SHAPE_RECT, 1000, 1000, 1000, 500);
    *b = add_shape(u, d, 0, OWF_SHAPE_ELLIPSE, 3000, 2000, 2000, 1000);
    *c = add_shape(u, d, 0, OWF_SHAPE_TRIANGLE, 7000, 1500, 500, 2000);
    opr_undo_clear(u);
    return d;
}

static const owf_obj *O(const owf_deck *d, int i)
{
    return &d->slides[0].objs[i];
}

static void moving(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, two[2], box[4];
    owf_deck *d = deck_of(u, &a, &b, &c);
    two[0] = a;
    two[1] = c;
    CHECK(!opr_objs_box(d, 0, two, 2, box) && box[0] == 1000 && box[1] == 1000 && box[2] == 6500 && box[3] == 2500,
          "the box round A and C: %d,%d %dx%d", box[0], box[1], box[2], box[3]);
    CHECK(!opr_objs_move(u, d, 0, two, 2, 100, -200) && O(d, a)->x == 1100 && O(d, a)->y == 800 &&
          O(d, c)->x == 7100 && O(d, c)->y == 1300 && O(d, b)->x == 3000, "A and C moved, B not");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && O(d, a)->x == 1000 && O(d, c)->y == 1500, "one Undo");
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && O(d, a)->x == 1100 && O(d, c)->y == 1300, "Redo");
    CHECK(!opr_objs_move(u, d, 0, two, 2, -99999, 0) && O(d, a)->x == 0 && O(d, c)->x == 6000,
          "held at the left edge: %d %d", O(d, a)->x, O(d, c)->x);
    CHECK(opr_objs_move(u, d, 0, two, 0, 1, 1) == -1, "no objects");
    two[1] = a;
    CHECK(opr_objs_move(u, d, 0, two, 2, 1, 1) == -1, "the same one twice");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void aligning(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, three[3];
    owf_deck *d = deck_of(u, &a, &b, &c);
    three[0] = a;
    three[1] = b;
    three[2] = c;
    CHECK(!opr_objs_align(u, d, 0, three, 3, OPR_ALIGN_LEFT) && O(d, a)->x == 1000 && O(d, b)->x == 1000 &&
          O(d, c)->x == 1000, "left");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && O(d, b)->x == 3000 && O(d, c)->x == 7000, "one Undo");
    CHECK(!opr_objs_align(u, d, 0, three, 3, OPR_ALIGN_RIGHT) && O(d, a)->x == 6500 && O(d, b)->x == 5500 &&
          O(d, c)->x == 7000, "right: %d %d %d", O(d, a)->x, O(d, b)->x, O(d, c)->x);
    opr_undo_undo(u, d, NULL, NULL);
    CHECK(!opr_objs_align(u, d, 0, three, 3, OPR_ALIGN_CENTRE) && O(d, a)->x == 3750 && O(d, b)->x == 3250 &&
          O(d, c)->x == 4000, "centre: %d %d %d", O(d, a)->x, O(d, b)->x, O(d, c)->x);
    opr_undo_undo(u, d, NULL, NULL);
    CHECK(!opr_objs_align(u, d, 0, three, 3, OPR_ALIGN_TOP) && O(d, a)->y == 1000 && O(d, b)->y == 1000 &&
          O(d, c)->y == 1000 && O(d, b)->x == 3000, "top, x kept");
    opr_undo_undo(u, d, NULL, NULL);
    CHECK(!opr_objs_align(u, d, 0, three, 3, OPR_ALIGN_BOTTOM) && O(d, a)->y == 3000 && O(d, b)->y == 2500 &&
          O(d, c)->y == 1500, "bottom: %d %d %d", O(d, a)->y, O(d, b)->y, O(d, c)->y);
    opr_undo_undo(u, d, NULL, NULL);
    CHECK(!opr_objs_align(u, d, 0, three, 3, OPR_ALIGN_MIDDLE) && O(d, a)->y == 2000 && O(d, b)->y == 1750 &&
          O(d, c)->y == 1250, "middle: %d %d %d", O(d, a)->y, O(d, b)->y, O(d, c)->y);
    opr_undo_undo(u, d, NULL, NULL);
    CHECK(!opr_objs_align(u, d, 0, &b, 1, OPR_ALIGN_RIGHT) && O(d, b)->x == d->width - 2000, "one alone: the slide");
    CHECK(opr_objs_align(u, d, 0, three, 3, (opr_align)99) == -2, "no such alignment");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void distributing(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, three[3], two[2];
    owf_deck *d = deck_of(u, &a, &b, &c);
    three[0] = c;                /* any order: they are taken left to right */
    three[1] = a;
    three[2] = b;
    /* A 1000..2000, B 3000..5000, C 7000..7500: 6500 across less 3500 of objects, gaps of 1500 */
    CHECK(!opr_objs_distribute(u, d, 0, three, 3, 0) && O(d, a)->x == 1000 && O(d, b)->x == 3500 &&
          O(d, c)->x == 7000, "across: %d %d %d", O(d, a)->x, O(d, b)->x, O(d, c)->x);
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && O(d, b)->x == 3000, "one Undo");
    /* down: A 1000..1500, C 1500..3500, B 2000..3000; sorted by top A, C, B; first A and last B stay */
    CHECK(!opr_objs_distribute(u, d, 0, three, 3, 1) && O(d, a)->y == 1000 && O(d, b)->y == 2000,
          "down: the ends stay");
    /* 2000 down less 3500 of objects: gaps of -750, so C overlaps both by as much */
    CHECK(O(d, c)->y == 750, "down: C between them, the gaps equal: %d", O(d, c)->y);
    two[0] = a;
    two[1] = b;
    CHECK(opr_objs_distribute(u, d, 0, two, 2, 0) == -2, "two: nothing to space");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void grouping(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, two[2], g = -1, first = -1, count = 0, n, title, picked[8];
    owf_deck *d = deck_of(u, &a, &b, &c);
    n = d->slides[0].nobjs;
    two[0] = c;
    two[1] = a;
    CHECK(!opr_obj_group(u, d, 0, two, 2, &g) && d->slides[0].nobjs == n + 1, "A and C grouped: %d objects",
          d->slides[0].nobjs);
    /* B goes behind; the group, then A, then C, at the end */
    CHECK(g == n - 2 && O(d, g)->kind == OWF_OBJ_GROUP && O(d, g)->parent < 0, "the group at %d", g);
    CHECK(O(d, g + 1)->shape == OWF_SHAPE_RECT && O(d, g + 2)->shape == OWF_SHAPE_TRIANGLE &&
          O(d, g + 1)->parent == g && O(d, g + 2)->parent == g, "A then C inside it");
    CHECK(O(d, g - 1)->shape == OWF_SHAPE_ELLIPSE && O(d, g - 1)->parent < 0, "B behind them");
    CHECK(O(d, g)->x == 1000 && O(d, g)->y == 1000 && O(d, g)->width == 6500 && O(d, g)->height == 2500,
          "the group's box round them");
    CHECK(opr_obj_block_end(d, 0, g) == g + 3 && opr_obj_top(d, 0, g + 2) == g && opr_obj_top(d, 0, g - 1) == g - 1,
          "its block");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->slides[0].nobjs == n && O(d, a)->shape == OWF_SHAPE_RECT &&
          O(d, b)->shape == OWF_SHAPE_ELLIPSE && O(d, c)->shape == OWF_SHAPE_TRIANGLE && O(d, a)->parent < 0 &&
          O(d, c)->parent < 0, "one Undo: as it was");
    CHECK(!opr_undo_redo(u, d, NULL, NULL) && O(d, g)->kind == OWF_OBJ_GROUP && O(d, g + 2)->parent == g, "Redo");

    /* the group moves with what is in it */
    CHECK(!opr_objs_move(u, d, 0, &g, 1, 500, 500) && O(d, g + 1)->x == 1500 && O(d, g + 2)->y == 2000 &&
          O(d, g)->x == 1500 && O(d, g - 1)->x == 3000, "moved as one");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && O(d, g + 1)->x == 1000 && O(d, g)->x == 1000, "one Undo");
    two[0] = g;
    two[1] = g - 1;
    CHECK(!opr_objs_align(u, d, 0, two, 2, OPR_ALIGN_RIGHT) && O(d, g + 2)->x == 7000 && O(d, g + 1)->x == 1000 &&
          O(d, g - 1)->x == 5500, "the group and B aligned right: the group as one box");
    opr_undo_undo(u, d, NULL, NULL);

    /* picked by a rectangle: whole top-level objects only */
    CHECK(opr_objs_in_rect(d, 0, 0, 0, d->width, d->height, picked, 8) >= 2, "all of it: the group and B");
    CHECK(opr_objs_in_rect(d, 0, 2900, 1900, 2200, 1200, picked, 8) == 1 && picked[0] == g - 1, "B alone");
    CHECK(opr_objs_in_rect(d, 0, 900, 900, 1200, 700, picked, 8) == 0, "A alone is inside a group: nothing");

    /* refusals */
    CHECK(opr_obj_group(u, d, 0, &g, 1, NULL) == -1, "one object is no group");
    two[0] = g;
    two[1] = g - 1;
    CHECK(opr_obj_group(u, d, 0, two, 2, NULL) == -2, "a group in a group: refused");
    title = opr_outline_title_obj(d, 0);
    two[0] = title;
    two[1] = g - 1;
    CHECK(title >= 0 && opr_obj_group(u, d, 0, two, 2, NULL) == -2, "a placeholder: refused");
    CHECK(opr_obj_ungroup(u, d, 0, g - 1, NULL, NULL) == -2, "B is no group");
    CHECK(opr_obj_ungroup(u, d, 0, g + 1, NULL, NULL) == -2, "A is inside one");

    /* ungrouped */
    CHECK(!opr_obj_ungroup(u, d, 0, g, &first, &count) && first == g && count == 2 && d->slides[0].nobjs == n &&
          O(d, g)->parent < 0 && O(d, g + 1)->parent < 0 && O(d, g)->shape == OWF_SHAPE_RECT &&
          O(d, g)->x == 1000, "ungrouped: A and C top-level where they were");
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && O(d, g)->kind == OWF_OBJ_GROUP && O(d, g + 1)->parent == g,
          "one Undo groups them again");

    /* deleted with what is in it */
    two[0] = g;
    two[1] = g - 1;
    CHECK(!opr_objs_delete(u, d, 0, two, 2) && d->slides[0].nobjs == n - 3, "the group, A, C and B deleted: %d",
          d->slides[0].nobjs);
    CHECK(!opr_undo_undo(u, d, NULL, NULL) && d->slides[0].nobjs == n + 1 && O(d, g)->kind == OWF_OBJ_GROUP &&
          O(d, g + 2)->parent == g && O(d, g - 1)->shape == OWF_SHAPE_ELLIPSE, "one Undo puts them back");
    owf_deck_free(d);
    opr_undo_free(u);
}

static void saving(void)
{
    opr_undo *u = opr_undo_new();
    int a, b, c, two[2], g = -1, i, groups = 0, inside = 0;
    owf_deck *d = deck_of(u, &a, &b, &c), *back = NULL;
    const owf_deck_format *odp = owf_deck_format_named("odp");
    unsigned char *data = NULL;
    size_t n = 0;
    owf_report *report = owf_report_new();
    two[0] = a;
    two[1] = c;
    opr_obj_group(u, d, 0, two, 2, &g);
    CHECK(odp && odp->export && odp->export(d, &data, &n, NULL) == OWF_OK, "saved as ODP");
    CHECK(data && owf_import_deck_memory(data, n, "odp", &back, NULL, NULL) == OWF_OK && back, "read back");
    if (back) {
        for (i = 0; i < back->slides[0].nobjs; i++) {
            if (back->slides[0].objs[i].kind == OWF_OBJ_GROUP)
                groups++;
            else if (back->slides[0].objs[i].parent >= 0)
                inside++;
        }
        CHECK(groups == 1 && inside == 2, "a group with two objects in it: %d, %d", groups, inside);
    }
    owf_free(data);
    data = NULL;
    CHECK(opr_export_pptx(d, &data, &n, report) == OWF_OK && data && owf_report_count(report) > 0,
          "saved as PPTX, the group reported");
    owf_free(data);
    owf_report_free(report);
    owf_deck_free(back);
    owf_deck_free(d);
    opr_undo_free(u);
}

int main(void)
{
    moving();
    aligning();
    distributing();
    grouping();
    saving();
    printf("test_opr_multi: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
