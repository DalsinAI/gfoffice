/*
 * test_opr_setchart: a slide chart's data and look replaced as one undo step (opr_obj_set_chart in
 * src/deck/opr_edit.c, Presentation gap 2): what the chart editor gives back goes on the slide.
 *
 *   - the chart given is copied (the caller keeps its own), the object's old one freed;
 *   - Undo puts the old chart back and says which object; Redo the new one;
 *   - an object that is not a top-level chart, or no chart, is refused (-2) and nothing changes;
 *   - a chart object that had no data (NULL) gets one, and Undo takes it away again.
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

#include "chart_sales.h"

/* A deck of one slide: a text box (0), a chart (1) holding the sales chart, a chart with no data (2). */
static owf_deck *make_deck(void)
{
    owf_deck *d = owf_deck_new(9144000, 6858000);
    owf_slide *s = d ? owf_deck_add_slide(d, -1, -1) : NULL;
    owf_obj *o;
    if (!s) { owf_deck_free(d); return NULL; }
    if (!owf_slide_add(s, OWF_OBJ_TEXT)) { owf_deck_free(d); return NULL; }
    o = owf_slide_add(s, OWF_OBJ_CHART);
    if (!o || !(o->chart = calloc(1, sizeof *o->chart)) ||
        owf_chart_from_part(chart_xml, strlen(chart_xml), o->chart) != OWF_OK) {
        owf_deck_free(d);
        return NULL;
    }
    if (!owf_slide_add(s, OWF_OBJ_CHART)) { owf_deck_free(d); return NULL; }
    return d;
}

int main(void)
{
    owf_deck *d = make_deck();
    opr_undo *u = opr_undo_new();
    owf_chart edited;
    int slide = -1, obj = -1, i;
    CHECK(d && u, "the deck");
    if (!d || !u) return 1;
    memset(&edited, 0, sizeof edited);
    CHECK(owf_chart_copy(&edited, d->slides[0].objs[1].chart) == OWF_OK, "a copy to edit");
    edited.kind = OWF_CHART_LINE;
    free(edited.title);
    edited.title = malloc(7);
    if (edited.title) memcpy(edited.title, "Edited", 7);
    edited.legend = 'r';
    edited.series[1].points[1].has_value = 1;
    edited.series[1].points[1].value = 7;

    CHECK(opr_obj_set_chart(u, d, 0, 1, &edited) == 0, "set");
    {
        const owf_chart *c = d->slides[0].objs[1].chart;
        CHECK(c && c != &edited && c->kind == OWF_CHART_LINE && c->title && !strcmp(c->title, "Edited") &&
              c->legend == 'r' && c->series[1].points[1].has_value && c->series[1].points[1].value == 7, "the new chart");
        CHECK(c && c->title != edited.title, "copied, not taken");
    }
    CHECK(opr_undo_can(u), "an undo step");
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && slide == 0 && obj == 1, "undone (%d %d)", slide, obj);
    check_sales(d->slides[0].objs[1].chart, "after undo");
    CHECK(opr_undo_redo(u, d, &slide, &obj) == 0 && obj == 1, "redone");
    CHECK(d->slides[0].objs[1].chart && d->slides[0].objs[1].chart->kind == OWF_CHART_LINE, "the edit again");

    /* refused: nothing changes, no step */
    opr_undo_clear(u);
    CHECK(opr_obj_set_chart(u, d, 0, 0, &edited) == -2, "a text box");
    CHECK(opr_obj_set_chart(u, d, 0, 1, NULL) == -2, "no chart");
    CHECK(opr_obj_set_chart(u, d, 0, 9, &edited) == -2, "no such object");
    CHECK(opr_obj_set_chart(NULL, d, 0, 1, &edited) == -1, "no undo");
    CHECK(!opr_undo_can(u), "no step for those");
    CHECK(d->slides[0].objs[0].chart == NULL, "the text box has no chart");

    /* a chart object with no data gets one, and loses it on undo */
    CHECK(d->slides[0].objs[2].chart == NULL, "no data yet");
    CHECK(opr_obj_set_chart(u, d, 0, 2, &edited) == 0 && d->slides[0].objs[2].chart &&
          d->slides[0].objs[2].chart->nseries == 2, "given one");
    CHECK(opr_undo_undo(u, d, &slide, &obj) == 0 && obj == 2 && d->slides[0].objs[2].chart == NULL, "taken away");

    /* more steps than undo keeps: the oldest go, freed */
    for (i = 0; i < 20; i++) {
        edited.gap = i;
        CHECK(opr_obj_set_chart(u, d, 0, 1, &edited) == 0, "step %d", i);
    }
    CHECK(d->slides[0].objs[1].chart->gap == 19, "the last step");

    owf_chart_clear(&edited);
    opr_undo_free(u);
    owf_deck_free(d);
    printf("test_opr_setchart: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
