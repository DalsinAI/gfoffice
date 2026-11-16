/*
 * test_chartedit: a slide's chart edited on GFOpenCalc's core (src/deckchart/opr_chartedit.c, Presentation gap 2):
 *
 *   - opened, its data is on the sheet "Data" (names in row 1, categories down A, a series a column, a gap empty)
 *     and the chart on that sheet, selected, pointing at those cells;
 *   - taken back unedited, it is the same chart, with no cells of its own;
 *   - its type, title and legend changed through oc_core_update_chart, and undone;
 *   - its data changed through the cells: a value typed, a row added (a point), a column added (a series),
 *     a value cleared (a gap);
 *   - a scatter's x values down column A;
 *   - the chart given is not changed.
 *
 * Prints what fails; exit 1 when anything did.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opr_chartedit.h"
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

#include "../deck/chart_sales.h"

static const char *cell(oc_core *core, unsigned row, unsigned col)
{
    static char buf[64];
    const owf_book *b = oc_core_book(core);
    const owf_cell *c = b && b->nsheets ? owf_sheet_find(&b->sheets[0], row, col) : NULL;
    if (!c || c->type == OWF_VALUE_EMPTY) return "";
    if (c->type == OWF_VALUE_TEXT) return c->text ? c->text : "";
    if (c->type == OWF_VALUE_NUMBER) { snprintf(buf, sizeof buf, "%g", c->number); return buf; }
    return "?";
}

static void enter(oc_core *core, const char *where, const char *text)
{
    CHECK(oc_core_goto(core, where) == OWF_OK, "go to %s", where);
    oc_edit_begin(core, 0);
    if (*text) oc_edit_type(core, text, strlen(text));
    CHECK(oc_edit_commit(core, OC_MOVE_NONE) == OWF_OK, "enter %s at %s", text, where);
}

static int no_cells(const owf_chart *c)
{
    int i;
    for (i = 0; i < c->nseries; i++)
        if (c->series[i].values || c->series[i].categories || c->series[i].name_cell || c->series[i].x_values)
            return 0;
    return 1;
}

static void test_open_take(void)
{
    owf_chart src, out;
    oc_core *core;
    const owf_book *b;
    memset(&src, 0, sizeof src);
    CHECK(owf_chart_from_part(chart_xml, strlen(chart_xml), &src) == OWF_OK, "the chart");
    core = opr_chart_edit_open(&src);
    CHECK(core != NULL, "opened");
    if (!core) { owf_chart_clear(&src); return; }
    b = oc_core_book(core);
    CHECK(b && b->nsheets == 1 && b->sheets[0].name && !strcmp(b->sheets[0].name, "Data"), "one sheet, Data");
    CHECK(!strcmp(cell(core, 0, 0), "") && !strcmp(cell(core, 0, 1), "North") && !strcmp(cell(core, 0, 2), "South"),
          "the names in row 1");
    CHECK(!strcmp(cell(core, 1, 0), "Q1") && !strcmp(cell(core, 3, 0), "Q3"), "the categories down A");
    CHECK(!strcmp(cell(core, 1, 1), "4.3") && !strcmp(cell(core, 2, 2), "") && !strcmp(cell(core, 3, 2), "1.8"),
          "the values, a gap empty");
    CHECK(oc_core_selected_chart(core) == 0, "the chart selected (%d)", oc_core_selected_chart(core));
    CHECK(src.series[0].values && !strcmp(src.series[0].values, "Sheet1!$B$2:$B$4"), "the chart given not changed");

    memset(&out, 0, sizeof out);
    CHECK(opr_chart_edit_take(core, &out) == OWF_OK, "taken back");
    check_sales(&out, "taken back unedited");
    CHECK(no_cells(&out), "no cells of its own");
    owf_chart_clear(&out);

    /* the look, through GFOpenCalc's chart edit, undoable */
    {
        oc_chart_edit_spec e;
        memset(&e, 0, sizeof e);
        e.set = OC_CHART_SET_KIND | OC_CHART_SET_TITLE | OC_CHART_SET_LEGEND;
        e.kind = OWF_CHART_LINE;
        e.title = "Sales by quarter";
        e.legend = 'r';
        CHECK(oc_core_update_chart(core, -1, &e) == OWF_OK, "kind, title, legend");
        memset(&out, 0, sizeof out);
        CHECK(opr_chart_edit_take(core, &out) == OWF_OK && out.kind == OWF_CHART_LINE && out.title &&
              !strcmp(out.title, "Sales by quarter") && out.legend == 'r', "edited: kind %d legend %c", out.kind,
              out.legend ? out.legend : '-');
        CHECK(out.nseries == 2 && out.series[0].npoints == 3, "the data as it was");
        owf_chart_clear(&out);
        CHECK(oc_core_undo(core) == OWF_OK, "undone");
        memset(&out, 0, sizeof out);
        CHECK(opr_chart_edit_take(core, &out) == OWF_OK, "taken after undo");
        check_sales(&out, "after undo");
        owf_chart_clear(&out);
    }

    /* the data, through the cells */
    enter(core, "B3", "9");                  /* North's Q2 */
    enter(core, "A5", "Q4");                 /* a point */
    enter(core, "B5", "5");
    enter(core, "C5", "6");
    enter(core, "D1", "West");               /* a series */
    enter(core, "D2", "1");
    enter(core, "D4", "3");
    enter(core, "C2", "");                   /* South's Q1 cleared: a gap */
    memset(&out, 0, sizeof out);
    CHECK(opr_chart_edit_take(core, &out) == OWF_OK, "taken after typing");
    CHECK(out.nseries == 3, "%d series", out.nseries);
    if (out.nseries == 3) {
        CHECK(out.series[0].npoints == 4 && out.series[0].points[1].has_value && out.series[0].points[1].value == 9 &&
              out.series[0].points[3].value == 5, "North's values typed");
        CHECK(out.series[0].points[3].label && !strcmp(out.series[0].points[3].label, "Q4"), "the new category");
        CHECK(!out.series[1].points[0].has_value && out.series[1].points[3].value == 6, "South: a gap, a new point");
        CHECK(out.series[2].name && !strcmp(out.series[2].name, "West") && out.series[2].npoints == 4 &&
              out.series[2].points[0].value == 1 && !out.series[2].points[1].has_value &&
              out.series[2].points[2].value == 3, "the new series");
        CHECK(out.series[2].index == 2, "its index");
        CHECK(out.title && !strcmp(out.title, "Sales") && out.legend == 'b', "the look kept");
        CHECK(no_cells(&out), "still no cells of its own");
    }
    owf_chart_clear(&out);
    oc_core_free(core);
    owf_chart_clear(&src);
}

static void test_scatter(void)
{
    owf_chart src, out;
    oc_core *core;
    int k;
    memset(&src, 0, sizeof src);
    CHECK(owf_chart_from_part(chart_xml, strlen(chart_xml), &src) == OWF_OK, "the chart");
    src.kind = OWF_CHART_SCATTER;
    for (k = 0; k < 3; k++) {
        free(src.series[0].points[k].label); src.series[0].points[k].label = NULL;
        free(src.series[1].points[k].label); src.series[1].points[k].label = NULL;
        src.series[0].points[k].x = src.series[1].points[k].x = k * 2 + 0.5;
        src.series[0].points[k].has_x = src.series[1].points[k].has_x = 1;
    }
    core = opr_chart_edit_open(&src);
    CHECK(core && !strcmp(cell(core, 3, 0), "4.5"), "x values down A");
    memset(&out, 0, sizeof out);
    CHECK(core && opr_chart_edit_take(core, &out) == OWF_OK && out.kind == OWF_CHART_SCATTER && out.nseries == 2 &&
          out.series[1].npoints == 3 && out.series[1].points[2].has_x && out.series[1].points[2].x == 4.5 &&
          out.series[1].points[2].value == 1.8 && !out.series[1].points[1].has_value, "a scatter back");
    CHECK(no_cells(&out), "no cells");
    owf_chart_clear(&out);
    if (core) oc_core_free(core);
    owf_chart_clear(&src);

    CHECK(opr_chart_edit_take(NULL, &out) == OWF_ERR_FORMAT, "no core");
}

int main(void)
{
    test_open_take();
    test_scatter();
    printf("test_chartedit: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
