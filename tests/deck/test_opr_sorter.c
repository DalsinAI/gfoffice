/*
 * test_opr_sorter: the slide sorter's core (src/deck/opr_sorter.c) and the
 * one-step Undo groups under it (opr_edit.c):
 *
 *   - picking: a click, Shift ranges from the anchor, toggles, all, and a
 *     pick that follows the deck's count;
 *   - the grid: columns that fit, each cell's place, hits on thumbnails and
 *     misses in the gaps, drop gaps across rows, the insertion mark, scroll
 *     to a slide and clamped scrolling, the keys;
 *   - several slides moved forwards, backwards, scattered and onto
 *     themselves; copied; deleted (never all); hidden and shown; each one
 *     Undo step that puts every slide back with its content, and redoes;
 *   - an Undo group failing part way puts the deck back;
 *   - the rearranged deck saved as ODP and read back in the new order.
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

/* The deck's slides by name's first letter: "ABCDEF". */
static const char *order(const owf_deck *d)
{
    static char buf[64];
    int i;
    for (i = 0; i < d->nslides && i < 63; i++)
        buf[i] = d->slides[i].name ? d->slides[i].name[0] : '?';
    buf[i] = 0;
    return buf;
}

static const char *title(const owf_deck *d, int slide)
{
    static char buf[256];
    char *t = opr_text_get(d, slide, 0);
    snprintf(buf, sizeof buf, "%s", t ? t : "(null)");
    owf_free(t);
    return buf;
}

static owf_deck *lettered(int n, opr_undo *u)
{
    owf_deck *d = owf_deck_new(OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT);
    int i, sel = 0;
    char name[2] = { 0, 0 }, words[32];
    if (!d)
        return NULL;
    for (i = 0; i < n; i++) {
        if (opr_slide_insert(u, d, i, d->nlayouts > 1 ? 1 : -1, &sel) < 0)
            break;
        name[0] = (char)('A' + i);
        owf_deck_set_text(&d->slides[i].name, name);
        snprintf(words, sizeof words, "Title %c", 'A' + i);
        opr_text_set(d, i, 0, words);
    }
    opr_undo_clear(u);
    return d;
}

static void picking(void)
{
    opr_pick p;
    int list[8];
    memset(&p, 0, sizeof p);
    p.anchor = p.focus = -1;
    CHECK(!opr_pick_resize(&p, 6) && p.count == 6 && !opr_pick_number(&p), "a pick of six, nothing picked");
    CHECK(!opr_pick_click(&p, 2, OPR_PICK_ONE) && opr_pick_number(&p) == 1 && opr_pick_has(&p, 2) &&
          p.anchor == 2 && p.focus == 2, "a click picks one");
    CHECK(!opr_pick_click(&p, 4, OPR_PICK_RANGE) && opr_pick_list(&p, list, 8) == 3 &&
          list[0] == 2 && list[2] == 4 && p.anchor == 2 && p.focus == 4, "Shift picks from the anchor");
    CHECK(!opr_pick_click(&p, 0, OPR_PICK_RANGE) && opr_pick_list(&p, list, 8) == 3 &&
          list[0] == 0 && list[2] == 2, "Shift the other way replaces the range, anchor kept");
    CHECK(!opr_pick_click(&p, 5, OPR_PICK_TOGGLE) && opr_pick_number(&p) == 4 && opr_pick_has(&p, 5) &&
          p.anchor == 5, "a toggle adds one and moves the anchor");
    CHECK(!opr_pick_click(&p, 1, OPR_PICK_TOGGLE) && !opr_pick_has(&p, 1) && opr_pick_number(&p) == 3,
          "a toggle takes one out");
    CHECK(!opr_pick_click(&p, 3, OPR_PICK_ADD_RANGE) && opr_pick_number(&p) == 5 && !opr_pick_has(&p, 1),
          "Shift with a toggle adds the range to the rest");
    CHECK(opr_pick_click(&p, 6, OPR_PICK_ONE) == -1 && opr_pick_click(&p, -1, OPR_PICK_ONE) == -1,
          "no slide outside the deck");
    opr_pick_all(&p);
    CHECK(opr_pick_number(&p) == 6, "all");
    CHECK(!opr_pick_resize(&p, 3) && p.count == 3 && opr_pick_number(&p) == 3 && p.focus < 3 && p.anchor < 3,
          "fewer slides: the pick shrinks with them");
    CHECK(!opr_pick_resize(&p, 5) && opr_pick_number(&p) == 3 && !opr_pick_has(&p, 4), "more: the new ones unpicked");
    opr_pick_clear(&p);
    CHECK(!opr_pick_number(&p) && p.focus == -1, "clear");
    CHECK(!opr_pick_resize(&p, 0) && p.count == 0 && opr_pick_only(&p, 0) == -1, "an empty deck's pick");
    opr_pick_free(&p);
}

static void grid(void)
{
    opr_sorter_grid g;
    int x, y, h, x2, y2, s;
    /* a 16:9 deck of 10 in a 700 x 400 view, labels 14 high */
    CHECK(!opr_sorter_layout(&g, 700, 400, 16000, 9000, 10, 0, 14), "lay out");
    CHECK(g.columns == 3 && g.rows == 4, "columns about 160 wide: %d columns, %d rows", g.columns, g.rows);
    CHECK(g.thumb_w * g.columns + g.gap * (g.columns - 1) + 2 * g.margin <= 700, "they fit across");
    CHECK(g.thumb_h == (int)((long)g.thumb_w * 9000 / 16000), "the slide's shape kept");
    CHECK(g.content_h == 2 * g.margin + 4 * g.cell_h + 3 * g.gap, "the height to scroll");
    CHECK(!opr_sorter_cell(&g, 0, 0, &x, &y) && x == g.margin && y == g.margin, "the first cell");
    CHECK(!opr_sorter_cell(&g, 4, 0, &x, &y) && x == g.margin + g.cell_w + g.gap && y == g.margin + g.cell_h + g.gap,
          "the fifth: second row, second column");
    CHECK(!opr_sorter_cell(&g, 4, 30, &x2, &y2) && x2 == x && y2 == y - 30, "scrolled up 30");
    CHECK(opr_sorter_cell(&g, 10, 0, &x, &y) == -1, "no eleventh");
    opr_sorter_cell(&g, 4, 0, &x, &y);
    CHECK(opr_sorter_hit(&g, x + 1, y + 1, 0) == 4 && opr_sorter_hit(&g, x + g.cell_w - 1, y + g.cell_h - 1, 0) == 4,
          "a hit on the thumbnail and on its label");
    CHECK(opr_sorter_hit(&g, x - 2, y + 5, 0) == -1 && opr_sorter_hit(&g, x + 5, y + g.cell_h + 2, 0) == -1,
          "the gaps between are no slide");
    CHECK(opr_sorter_hit(&g, x + 1, y + 1 - 50, 50) == 4, "a hit scrolled");
    opr_sorter_cell(&g, 9, 0, &x, &y);
    CHECK(opr_sorter_hit(&g, x + g.cell_w + g.gap + 3, y + 3, 0) == -1, "past the last slide is nothing");
    CHECK(opr_sorter_hit(&g, 2, 2, 0) == -1, "the margin is nothing");

    opr_sorter_cell(&g, 4, 0, &x, &y);
    CHECK(opr_sorter_gap(&g, x + 2, y + 5, 0) == 4, "the left of a thumbnail: before it");
    CHECK(opr_sorter_gap(&g, x + g.cell_w - 2, y + 5, 0) == 5, "its right: after it");
    CHECK(opr_sorter_gap(&g, 0, y + 5, 0) == 3, "the row's left edge: before its first");
    CHECK(opr_sorter_gap(&g, 699, y + 5, 0) == 6, "the row's right edge: after its last");
    opr_sorter_cell(&g, 9, 0, &x, &y);
    CHECK(opr_sorter_gap(&g, 699, y + 5, 0) == 10, "the last row's right: the end");
    CHECK(opr_sorter_gap(&g, 300, 5000, 0) == 10, "below everything: the end");
    CHECK(opr_sorter_gap(&g, 0, -40, 0) == 0, "above everything: the start");
    CHECK(!opr_sorter_gap_mark(&g, 4, 0, &x2, &y2, &h) && h == g.thumb_h, "a mark");
    opr_sorter_cell(&g, 4, 0, &x, &y);
    CHECK(x2 == x - g.gap / 2 && y2 == y, "the mark before slide 5 sits in the gap left of it");
    opr_sorter_cell(&g, 9, 0, &x, &y);
    CHECK(!opr_sorter_gap_mark(&g, 10, 0, &x2, &y2, &h) && x2 == x + g.cell_w + g.gap / 2 && y2 == y,
          "the end's mark is right of the last slide");
    CHECK(opr_sorter_gap_mark(&g, 11, 0, &x2, &y2, &h) == -1, "no mark past the end");

    CHECK(opr_sorter_clamp_scroll(&g, -5) == 0 && opr_sorter_clamp_scroll(&g, 100000) == g.content_h - 400,
          "scrolling kept inside");
    s = opr_sorter_scroll_to(&g, 9, 0);
    opr_sorter_cell(&g, 9, s, &x, &y);
    CHECK(s > 0 && y + g.cell_h <= 400 && y >= 0, "scroll to the last slide shows it (%d)", s);
    CHECK(opr_sorter_scroll_to(&g, 0, s) == 0, "and back to the first");
    CHECK(opr_sorter_scroll_to(&g, 1, 0) == 0, "a slide in view: no scroll");

    CHECK(opr_sorter_step(&g, 4, OPR_SORTER_LEFT) == 3 && opr_sorter_step(&g, 4, OPR_SORTER_RIGHT) == 5,
          "left and right");
    CHECK(opr_sorter_step(&g, 4, OPR_SORTER_UP) == 1 && opr_sorter_step(&g, 4, OPR_SORTER_DOWN) == 7,
          "up and down a row");
    CHECK(opr_sorter_step(&g, 1, OPR_SORTER_UP) == 1 && opr_sorter_step(&g, 8, OPR_SORTER_DOWN) == 8,
          "no row there: stay");
    CHECK(opr_sorter_step(&g, 0, OPR_SORTER_LEFT) == 0 && opr_sorter_step(&g, 9, OPR_SORTER_RIGHT) == 9,
          "kept inside");
    CHECK(opr_sorter_step(&g, 5, OPR_SORTER_HOME) == 0 && opr_sorter_step(&g, 5, OPR_SORTER_END) == 9,
          "home and end");
    CHECK(opr_sorter_step(&g, 9, OPR_SORTER_PAGE_UP) < 9 && opr_sorter_step(&g, 0, OPR_SORTER_PAGE_DOWN) > 0,
          "page keys");

    CHECK(!opr_sorter_layout(&g, 700, 400, 16000, 9000, 10, 5, 14) && g.columns == 5, "five columns asked for");
    CHECK(!opr_sorter_layout(&g, 200, 400, 16000, 9000, 10, 12, 14) && g.columns < 12 && g.thumb_w >= 32,
          "too many for the width: fewer (%d)", g.columns);
    CHECK(!opr_sorter_layout(&g, 20, 30, 16000, 9000, 3, 0, 14) && g.columns == 1 && g.thumb_w >= 1,
          "a tiny view still has a column");
    CHECK(!opr_sorter_layout(&g, 700, 400, 16000, 9000, 0, 0, 14) && g.rows == 0 && opr_sorter_hit(&g, 20, 20, 0) == -1 &&
          opr_sorter_gap(&g, 20, 20, 0) == 0 && opr_sorter_step(&g, 0, OPR_SORTER_RIGHT) == -1, "no slides");
    CHECK(opr_sorter_layout(&g, 0, 400, 16000, 9000, 1, 0, 14) == -1 &&
          opr_sorter_layout(&g, 700, 400, 16000, 9000, 1, 13, 14) == -1, "bad input refused");
}

static void several(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = u ? lettered(6, u) : NULL, *back = NULL;
    opr_pick p;
    int sel = -1, slide = -1, obj = -1;
    unsigned char *data = NULL;
    size_t length = 0;
    const owf_deck_format *odp = owf_deck_format_named("odp");

    memset(&p, 0, sizeof p);
    p.anchor = p.focus = -1;
    CHECK(d && d->nslides == 6 && !strcmp(order(d), "ABCDEF"), "six lettered slides");
    if (!d) {
        opr_undo_free(u);
        return;
    }
    opr_pick_resize(&p, d->nslides);
    CHECK(opr_sorter_move(u, d, &p, 0, &sel) == -2 && !opr_undo_can(u), "nothing picked: nothing moved");

    /* B and C to the end */
    opr_pick_click(&p, 1, OPR_PICK_ONE);
    opr_pick_click(&p, 2, OPR_PICK_RANGE);
    CHECK(!opr_sorter_move(u, d, &p, 6, &sel) && !strcmp(order(d), "ADEFBC"), "B and C to the end: %s", order(d));
    CHECK(opr_pick_number(&p) == 2 && opr_pick_has(&p, 4) && opr_pick_has(&p, 5) && sel == 5,
          "they stay picked where they went, focus on C");
    CHECK(!strcmp(title(d, 4), "Title B") && !strcmp(title(d, 5), "Title C"), "with their content");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF") && obj == -1,
          "one Undo puts both back: %s", order(d));
    CHECK(!opr_undo_can(u), "and it was one step");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && !strcmp(order(d), "ADEFBC"), "Redo moves both again");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF"), "undo again");

    /* scattered: B, D, F to the front */
    opr_pick_resize(&p, d->nslides);
    opr_pick_click(&p, 1, OPR_PICK_ONE);
    opr_pick_click(&p, 3, OPR_PICK_TOGGLE);
    opr_pick_click(&p, 5, OPR_PICK_TOGGLE);
    CHECK(!opr_sorter_move(u, d, &p, 0, &sel) && !strcmp(order(d), "BDFACE"), "B D F to the front: %s", order(d));
    CHECK(opr_pick_has(&p, 0) && opr_pick_has(&p, 2) && !opr_pick_has(&p, 3) && sel == 2, "picked at 0..2, F focused");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF"), "undo the scattered move");

    /* into the middle, from both sides: A and F between C and D */
    opr_pick_click(&p, 0, OPR_PICK_ONE);
    opr_pick_click(&p, 5, OPR_PICK_TOGGLE);
    CHECK(!opr_sorter_move(u, d, &p, 3, &sel) && !strcmp(order(d), "BCAFDE"), "A and F before D: %s", order(d));
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF"), "undo");

    /* onto themselves: nothing done, nothing to undo */
    while (opr_undo_can(u)) opr_undo_undo(u, d, NULL, NULL);
    opr_undo_clear(u);
    opr_pick_click(&p, 2, OPR_PICK_ONE);
    opr_pick_click(&p, 3, OPR_PICK_RANGE);
    CHECK(!opr_sorter_move(u, d, &p, 2, &sel) && !strcmp(order(d), "ABCDEF") && !opr_undo_can(u),
          "dropped before themselves: no change");
    CHECK(!opr_sorter_move(u, d, &p, 4, &sel) && !strcmp(order(d), "ABCDEF") && !opr_undo_can(u),
          "dropped after themselves: no change");
    CHECK(!opr_sorter_move(u, d, &p, 3, &sel) && !strcmp(order(d), "ABCDEF") && !opr_undo_can(u),
          "dropped between themselves: no change");
    CHECK(opr_sorter_move(u, d, &p, 7, &sel) == -1, "no gap past the end");

    /* one slide back by one, as Move Up did */
    opr_pick_click(&p, 4, OPR_PICK_ONE);
    CHECK(!opr_sorter_move(u, d, &p, 3, &sel) && !strcmp(order(d), "ABCEDF") && sel == 3, "E before D");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF"), "undo");

    /* copies after the last picked */
    opr_pick_click(&p, 0, OPR_PICK_ONE);
    opr_pick_click(&p, 2, OPR_PICK_TOGGLE);
    CHECK(!opr_sorter_duplicate(u, d, &p, &sel) && d->nslides == 8 && !strcmp(order(d), "ABCACDEF"),
          "copies of A and C after C: %s", order(d));
    CHECK(p.count == 8 && opr_pick_has(&p, 3) && opr_pick_has(&p, 4) && opr_pick_number(&p) == 2 && sel == 3,
          "the copies picked");
    CHECK(!strcmp(title(d, 3), "Title A") && !strcmp(title(d, 4), "Title C") && strcmp(d->slides[3].name, "A"),
          "the copies' content, and names of their own (%s)", d->slides[3].name);
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->nslides == 6 && !strcmp(order(d), "ABCDEF"),
          "one Undo takes both copies away");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && d->nslides == 8, "Redo makes them again");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && d->nslides == 6, "undo");

    /* delete */
    opr_pick_resize(&p, d->nslides);
    opr_pick_click(&p, 1, OPR_PICK_ONE);
    opr_pick_click(&p, 2, OPR_PICK_RANGE);
    opr_pick_click(&p, 4, OPR_PICK_TOGGLE);
    CHECK(!opr_sorter_delete(u, d, &p, &sel) && !strcmp(order(d), "ADF") && sel == 2 && opr_pick_has(&p, 2) &&
          p.count == 3, "B C E deleted; F, the one after E, picked (%s, %d)", order(d), sel);
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF") && !strcmp(title(d, 4), "Title E"),
          "one Undo brings all three back whole");
    CHECK(!opr_undo_redo(u, d, &slide, &obj) && !strcmp(order(d), "ADF"), "Redo deletes them again");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF"), "undo");
    opr_pick_resize(&p, d->nslides);
    opr_pick_click(&p, 4, OPR_PICK_ONE);
    opr_pick_click(&p, 5, OPR_PICK_RANGE);
    CHECK(!opr_sorter_delete(u, d, &p, &sel) && !strcmp(order(d), "ABCD") && sel == 3,
          "the last two deleted: the new last one picked");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCDEF"), "undo");
    opr_pick_resize(&p, d->nslides);
    opr_pick_all(&p);
    CHECK(opr_sorter_delete(u, d, &p, &sel) == -2 && d->nslides == 6, "never all of them");

    /* hide and show */
    opr_pick_click(&p, 1, OPR_PICK_ONE);
    opr_pick_click(&p, 3, OPR_PICK_TOGGLE);
    d->slides[3].hidden = 1;
    while (opr_undo_can(u)) opr_undo_undo(u, d, NULL, NULL);
    opr_undo_clear(u);
    CHECK(!opr_sorter_set_hidden(u, d, &p, 1, &sel) && d->slides[1].hidden && d->slides[3].hidden &&
          !d->slides[2].hidden, "B and D hidden (D already was)");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !d->slides[1].hidden && d->slides[3].hidden && !opr_undo_can(u),
          "one Undo: B shown again, D still hidden as before");
    CHECK(!opr_sorter_set_hidden(u, d, &p, 0, &sel) && !d->slides[3].hidden && opr_undo_can(u),
          "shown");
    d->slides[1].hidden = 0;
    opr_undo_clear(u);
    CHECK(!opr_sorter_set_hidden(u, d, &p, 0, &sel) && !opr_undo_can(u), "already shown: no step");

    /* the new order saved and read back */
    opr_pick_click(&p, 5, OPR_PICK_ONE);
    CHECK(!opr_sorter_move(u, d, &p, 0, &sel) && !strcmp(order(d), "FABCDE"), "F first");
    CHECK(odp && odp->export && odp->export(d, &data, &length, NULL) == OWF_OK && data,
          "saved as ODP");
    CHECK(data && owf_import_deck_memory(data, length, "odp", &back, NULL, NULL) == OWF_OK && back &&
          back->nslides == 6 && !strcmp(order(back), "FABCDE") && !strcmp(title(back, 0), "Title F"),
          "read back in the new order: %s", back ? order(back) : "(none)");
    owf_free(data);
    owf_deck_free(back);
    opr_pick_free(&p);
    opr_undo_free(u);
    owf_deck_free(d);
}

static void groups(void)
{
    opr_undo *u = opr_undo_new();
    owf_deck *d = u ? lettered(4, u) : NULL;
    int sel = 0, slide, obj;
    if (!d) {
        CHECK(0, "a deck");
        opr_undo_free(u);
        return;
    }
    CHECK(opr_undo_group_end(u) == -1, "no group to end");
    CHECK(opr_undo_group_begin(u, 0) == -1, "a group has room for one at least");
    CHECK(!opr_undo_group_begin(u, 3) && opr_undo_group_begin(u, 3) == -1, "one group at a time");
    CHECK(!opr_slide_move(u, d, 0, 3, &sel) && !opr_text_set(d, 0, 0, "x") && !opr_undo_push(u, d, 1, 0) &&
          !opr_text_set(d, 1, 0, "Changed"), "a move and a text change in the group");
    CHECK(opr_undo_undo(u, d, &slide, &obj) == -1, "no Undo while it is open");
    CHECK(!opr_undo_group_cancel(u, d) && !strcmp(order(d), "ABCD") && !opr_undo_can(u),
          "cancel: the move undone, nothing kept");
    /* the text change made after its push is put back too */
    CHECK(strcmp(title(d, 1), "Changed") != 0, "and the text put back: %s", title(d, 1));

    CHECK(!opr_undo_group_begin(u, 1), "a group of one");
    CHECK(!opr_slide_move(u, d, 0, 1, &sel) && !opr_undo_group_end(u) && !strcmp(order(d), "BACD"), "end it");
    CHECK(!opr_undo_undo(u, d, &slide, &obj) && !strcmp(order(d), "ABCD") && !opr_undo_can(u), "one step");

    CHECK(!opr_undo_group_begin(u, 2) && !opr_undo_group_end(u) && !opr_undo_can(u), "an empty group adds nothing");

    CHECK(!opr_slide_move(u, d, 0, 1, &sel) && opr_undo_can(u), "a step before");
    CHECK(!opr_undo_group_begin(u, 1), "room for one");
    CHECK(!opr_slide_move(u, d, 0, 1, &sel) && !opr_slide_move(u, d, 2, 3, &sel), "two made");
    CHECK(opr_undo_group_end(u) == -1 && !opr_undo_can(u) && !opr_redo_can(u),
          "more than room: refused, and the history cleared");
    opr_undo_free(u);
    owf_deck_free(d);
}

int main(void)
{
    picking();
    grid();
    several();
    groups();
    printf("test_opr_sorter: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
