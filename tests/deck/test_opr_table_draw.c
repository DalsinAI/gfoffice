/*
 * test_opr_table_draw: a slide table's merged cells and row heights drawn
 * (src/deck/opr_layout.c, through GFOpenLayout 0.21's row_span and a
 * row's least height), and kept right by the table edits (opr_edit.c):
 *
 *   - a cell merged across two columns is as wide as both, the cell it
 *     covers not drawn;
 *   - a cell merged down two rows is as tall as both, the cell under it
 *     not drawn, the next cell in that row in the next column;
 *   - a row is at least its height;
 *   - a row or column put in before a merge moves it, one put in across it
 *     grows it; one taken out shrinks it, and a merge left one cell goes;
 *     row heights follow their rows;
 *   - Undo puts the merges and heights back, Redo takes them again.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_deck_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#define COL 2400        /* twips a column, before the frame's insets */
#define ROW 1440        /* row 1's height */

/* a slide with a 3 by 3 table: (0,0) across two columns, (1,0) down two
 * rows, row 1 an inch tall */
static owf_deck *table_deck(opr_undo *u, int *obj)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    opr_table_spec spec;
    owf_obj *o;
    int sel = 0;
    if (!d)
        return NULL;
    if (opr_slide_insert(u, d, 0, -1, &sel) < 0)
        return d;
    memset(&spec, 0, sizeof spec);
    spec.x = 720;
    spec.y = 720;
    spec.width = 3 * COL;
    spec.height = 3 * 400;
    spec.rows = 3;
    spec.columns = 3;
    if (opr_obj_insert_table(u, d, 0, &spec, obj) < 0)
        return d;
    o = &d->slides[0].objs[*obj];
    owf_table_add_merge(o, 0, 0, 1, 2);
    owf_table_add_merge(o, 1, 0, 2, 1);
    owf_table_set_row_height(o, 1, ROW);
    owf_table_set_row_height(o, 2, 0);     /* three rows, as the readers leave them */
    opr_undo_clear(u);
    return d;
}

static const ol_node *nth(const ol_node *parent, ol_role role, int k)
{
    const ol_node *c;
    for (c = parent ? ol_node_first_child(parent) : NULL; c; c = ol_node_next_sibling(c))
        if (ol_node_role(c) == role && k-- == 0)
            return c;
    return NULL;
}

static const ol_node *table_of(const opr_slide *s, int obj)
{
    int frame = opr_slide_frame_for(s, obj);
    ol_id id;
    if (frame < 0)
        return NULL;
    for (id = s->frames[frame].first; id < s->frames[frame].end; id++) {
        const ol_node *n = ol_node_by_id(s->doc, id);
        if (n && ol_node_role(n) == OL_ROLE_TABLE)
            return n;
    }
    return NULL;
}

static const ol_node *cell_at(const ol_node *table, int row, int col)
{
    return nth(nth(table, OL_ROLE_ROW, row), OL_ROLE_CELL, col);
}

static int hidden(const ol_node *n)
{
    return n && ol_node_style(n)->display == OL_DISPLAY_NONE;
}

static void drawing(void)
{
    opr_undo *u = opr_undo_new();
    int obj = -1;
    owf_deck *d = table_deck(u, &obj);
    opr_slide *s;
    const ol_node *t;
    ol_rect a, b, c, r1, one;

    CHECK(d && obj >= 0, "a deck with a table");
    if (!d || obj < 0)
        goto out;
    s = opr_slide_layout(d, 0, NULL, NULL, "1/1/2026");
    t = s ? table_of(s, obj) : NULL;
    CHECK(t != NULL, "the table laid out");
    if (t) {
        /* the frame's insets narrow the columns alike: one column is (1,1)'s width */
        one = ol_node_bounds(cell_at(t, 1, 1));
        a = ol_node_bounds(cell_at(t, 0, 0));
        CHECK(one.width > 0 && a.width >= 2 * one.width - 2 && a.width <= 2 * one.width + 2,
              "(0,0) across two columns: %ld of %ld", (long)a.width, (long)one.width);
        CHECK(hidden(cell_at(t, 0, 1)), "(0,1) covered");
        CHECK(!hidden(cell_at(t, 0, 2)) && ol_node_bounds(cell_at(t, 0, 2)).x == a.x + a.width,
              "(0,2) after the merge");
        r1 = ol_node_bounds(nth(t, OL_ROLE_ROW, 1));
        CHECK(r1.height >= (ol_unit)ROW * OL_UNITS_PER_TWIP, "row 1 at least an inch: %ld", (long)r1.height);
        b = ol_node_bounds(cell_at(t, 1, 0));
        c = ol_node_bounds(nth(t, OL_ROLE_ROW, 2));
        CHECK(b.y == r1.y && b.height == r1.height + c.height,
              "(1,0) down rows 1 and 2: %ld of %ld + %ld", (long)b.height, (long)r1.height, (long)c.height);
        CHECK(hidden(cell_at(t, 2, 0)), "(2,0) covered");
        CHECK(ol_node_bounds(cell_at(t, 2, 1)).x == one.x && one.x == b.x + b.width, "(2,1) in column 1");
        CHECK(!hidden(cell_at(t, 1, 1)) && !hidden(cell_at(t, 2, 2)), "the other cells drawn");
    }
    opr_slide_free(s);
out:
    owf_deck_free(d);
    opr_undo_free(u);
}

static int has_merge(const owf_obj *o, int row, int col, int rows, int cols)
{
    int i;
    for (i = 0; i < o->nmerges; i++)
        if (o->merges[i].row == row && o->merges[i].col == col &&
            o->merges[i].rows == rows && o->merges[i].cols == cols)
            return 1;
    return 0;
}

static void editing(void)
{
    opr_undo *u = opr_undo_new();
    int obj = -1;
    owf_deck *d = table_deck(u, &obj);
    owf_obj *o;

    if (!d || obj < 0) {
        CHECK(0, "a deck with a table");
        owf_deck_free(d);
        opr_undo_free(u);
        return;
    }
    o = &d->slides[0].objs[obj];

    /* a row before row 1: the down merge moves, row 1's height with it */
    CHECK(opr_table_insert_row(u, d, 0, obj, 1) == 0, "insert row 1");
    CHECK(o->nmerges == 2 && has_merge(o, 0, 0, 1, 2) && has_merge(o, 2, 0, 2, 1), "the down merge moved");
    CHECK(o->nrows == 4 && o->row_heights[2] == ROW, "row 1's height now row 2's");
    /* a row inside the down merge: it grows */
    CHECK(opr_table_insert_row(u, d, 0, obj, 3) == 0, "insert row 3");
    CHECK(has_merge(o, 2, 0, 3, 1), "the down merge grew");
    /* Undo twice: as it was */
    CHECK(opr_undo_undo(u, d, NULL, NULL) == 0 && opr_undo_undo(u, d, NULL, NULL) == 0, "undo both");
    o = &d->slides[0].objs[obj];
    CHECK(o->nmerges == 2 && has_merge(o, 0, 0, 1, 2) && has_merge(o, 1, 0, 2, 1), "merges back");
    CHECK(o->nrows == 3 && o->row_heights[1] == ROW, "heights back");
    CHECK(opr_undo_redo(u, d, NULL, NULL) == 0, "redo");
    o = &d->slides[0].objs[obj];
    CHECK(has_merge(o, 2, 0, 2, 1) && o->nrows == 4, "redone");
    CHECK(opr_undo_undo(u, d, NULL, NULL) == 0, "undo again");
    o = &d->slides[0].objs[obj];

    /* a column inside the across merge grows it; one before it moves it */
    CHECK(opr_table_insert_column(u, d, 0, obj, 1) == 0, "insert column 1");
    CHECK(has_merge(o, 0, 0, 1, 3) && has_merge(o, 1, 0, 2, 1), "the across merge grew");
    CHECK(opr_table_insert_column(u, d, 0, obj, 0) == 0, "insert column 0");
    CHECK(has_merge(o, 0, 1, 1, 3) && has_merge(o, 1, 1, 2, 1), "both moved");
    /* columns out: both move back, the across merge shrinks, then goes */
    CHECK(opr_table_delete_column(u, d, 0, obj, 0) == 0, "delete column 0");
    CHECK(has_merge(o, 0, 0, 1, 3) && has_merge(o, 1, 0, 2, 1), "both moved back");
    CHECK(opr_table_delete_column(u, d, 0, obj, 2) == 0, "delete column 2");
    CHECK(has_merge(o, 0, 0, 1, 2), "the across merge shrank");
    CHECK(opr_table_delete_column(u, d, 0, obj, 1) == 0, "delete column 1");
    CHECK(o->nmerges == 1 && has_merge(o, 1, 0, 2, 1), "the across merge went, the down one stayed");
    /* the down merge's first row out: it shrinks to one cell and goes; row 1's height goes with it */
    CHECK(opr_table_delete_row(u, d, 0, obj, 1) == 0, "delete row 1");
    CHECK(o->nmerges == 0 && o->merges == NULL, "no merges left");
    CHECK(o->nrows == 2 && o->row_heights[0] == 0 && o->row_heights[1] == 0, "row 1's height gone");
    while (opr_undo_undo(u, d, NULL, NULL) == 0)
        ;
    o = &d->slides[0].objs[obj];
    CHECK(o->nmerges == 2 && has_merge(o, 0, 0, 1, 2) && has_merge(o, 1, 0, 2, 1) && o->ncols == 3,
          "all undone");
    owf_deck_free(d);
    opr_undo_free(u);
}

int main(void)
{
    drawing();
    editing();
    printf("test_opr_table_draw: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
