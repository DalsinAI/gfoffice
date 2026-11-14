/*
 * GFOpenProject's editing core and Gantt view on the PC (src/project, openproject_core.h). Every expected
 * date, pixel and point below was worked out by hand: the Standard calendar (Monday to Friday, 08:00-12:00
 * and 13:00-17:00), the plan starting on Monday 16 November 2026 at 08:00, rows 16 pixels high, the Days
 * zoom (24 pixels a day). The view draws onto a host target of 0xRRGGBB pixels through GFOpenLayout's
 * drawing calls; text is a block font, 6 pixels a byte. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "openproject_core.h"

static int fails, checks;

#define AT(d, h, m) owf_ptime_make(2026, 11, d, h, m)
#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_X(got, want) do { unsigned long g_ = (unsigned long)(got), w_ = (unsigned long)(want); checks++; \
    if (g_ != w_) { fails++; printf("FAIL %s:%d: %s is %06lX, want %06lX\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

static char cell[256];

static const char *cell_of(const opj_doc *doc, int row, int column)
{
    opj_cell_text(doc, row, column, cell, sizeof cell);
    return cell;
}

/* ---- A host target ---- */

#define W 800
#define H 200
static unsigned long px[H][W];
static oldraw *cur;
static int fills_left_of;               /* fills that reached left of this x, when set */
static int fills_left;
static char texts[64][64];
static int ntexts;

static void t_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    int i, j;
    unsigned long v = oldraw_resolve(cur, c) & 0xFFFFFFUL;
    (void)t;
    if (fills_left_of && x < fills_left_of) fills_left++;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            if (i < 0 || j < 0 || i >= W || j >= H) {
                fails++;
                printf("FAIL fill outside the target at %d,%d\n", i, j);
                return;
            }
            px[j][i] = v;
        }
}

static void t_invert(void *t, int x, int y, int w, int h)
{
    (void)t; (void)x; (void)y; (void)w; (void)h;
}

static void t_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    (void)t; (void)img; (void)sx; (void)sy; (void)x; (void)y; (void)w; (void)h; (void)flags;
}

static int t_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    (void)t; (void)s; (void)f;
    size->width = (int)n * 6;
    size->ascent = 7;
    size->descent = 2;
    return 1;
}

static int t_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    (void)t; (void)x; (void)y; (void)f; (void)c; (void)st; (void)clip;
    if (ntexts < 64) {
        size_t k = n < 63 ? n : 63;
        memcpy(texts[ntexts], s, k);
        texts[ntexts][k] = 0;
        ntexts++;
    }
    return 1;
}

static void t_flush(void *t)
{
    (void)t;
}

static int t_copy(void *t, int sx, int sy, int dx, int dy, int w, int h)
{
    (void)t; (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h;
    return 1;
}

static const oldraw_ops ops = { t_fill, t_invert, t_image, t_text, t_measure, t_flush, NULL, t_copy };

static int drew(const char *s)
{
    int i;
    for (i = 0; i < ntexts; i++)
        if (strcmp(texts[i], s) == 0) return 1;
    return 0;
}

/* Plan (2d), Build (3d, after Plan), Ship (a milestone after Build). */
static opj_doc *three(void)
{
    opj_doc *doc = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(doc != NULL);
    CHECK_N(opj_insert_task(doc, 0, "Plan"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_DURATION, "2d"), OWF_OK);
    CHECK_N(opj_insert_task(doc, 1, "Build"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_DURATION, "3 days"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, "1"), OWF_OK);
    CHECK_N(opj_insert_task(doc, 2, "Ship"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 2, OPJ_COL_DURATION, "0d"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 2, OPJ_COL_PREDECESSORS, "2"), OWF_OK);
    return doc;
}

static void test_dates(void)
{
    owf_ptime t = 0;
    char out[32];
    opj_date_text(AT(16, 8, 0), 0, out);
    CHECK_S(out, "Mon 16/11/26");
    opj_date_text(AT(20, 17, 0), 1, out);
    CHECK_S(out, "Fri 20/11/26 17:00");
    CHECK(opj_date_parse("16/11/26", 480, &t) && t == AT(16, 8, 0));
    CHECK(opj_date_parse("Mon 16/11/2026 08:30", 0, &t) && t == AT(16, 8, 30));
    CHECK(opj_date_parse(" 2026-11-16 13:00", 0, &t) && t == AT(16, 13, 0));
    CHECK(opj_date_parse("2026-11-16T13:00:00", 0, &t) && t == AT(16, 13, 0));
    CHECK(opj_date_parse("16.11.26", 1020, &t) && t == AT(16, 17, 0));
    CHECK(!opj_date_parse("32/11/26", 0, &t));
    CHECK(!opj_date_parse("16/11", 0, &t));
    CHECK(!opj_date_parse("16/11/26 25:00", 0, &t));
    CHECK(!opj_date_parse("16/11/26 08:00 x", 0, &t));
    CHECK(!opj_date_parse("soon", 0, &t));
}

static void test_sheet(void)
{
    opj_doc *doc = opj_doc_new(NULL, AT(16, 8, 0));
    owf_project *p;
    unsigned long changes;
    CHECK_N(opj_insert_task(doc, 0, "Plan"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_DURATION), "1d?");             /* a new task: a day, estimated */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_DURATION, "2d"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_DURATION), "2d");
    opj_doc_free(doc);

    doc = three();
    p = doc->plan;
    CHECK_N(p->ntasks, 3);
    CHECK_S(cell_of(doc, 0, OPJ_COL_ID), "1");
    CHECK_S(cell_of(doc, 0, OPJ_COL_NAME), "Plan");
    CHECK_S(cell_of(doc, 0, OPJ_COL_START), "Mon 16/11/26");
    CHECK_S(cell_of(doc, 0, OPJ_COL_FINISH), "Tue 17/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_START), "Wed 18/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_FINISH), "Fri 20/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_PREDECESSORS), "1");
    CHECK_S(cell_of(doc, 2, OPJ_COL_DURATION), "0d");
    CHECK(doc->plan->tasks[2].milestone);
    CHECK_S(cell_of(doc, 2, OPJ_COL_START), "Fri 20/11/26");
    CHECK_S(cell_of(doc, 2, OPJ_COL_PERCENT), "0%");
    CHECK(doc->plan->tasks[0].critical && doc->plan->tasks[1].critical && doc->plan->tasks[2].critical);

    /* Edits that can't be made leave the plan as it was, and are no Undo step. */
    changes = doc->changes;
    p = doc->plan;
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PREDECESSORS, "3"), OWF_ERR_FORMAT);     /* a loop */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PREDECESSORS, "9"), OWF_ERR_FORMAT);     /* no row 9 */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PREDECESSORS, "1"), OWF_ERR_FORMAT);     /* itself */
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, "1xx"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_DURATION, "abc"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_DURATION, "-1d"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PERCENT, "150"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_START, "someday"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_ID, "4"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 7, OPJ_COL_NAME, "x"), OWF_ERR_FORMAT);
    CHECK(doc->plan == p);
    CHECK_N(doc->changes, changes);
    CHECK_N(doc->plan->tasks[0].nlinks, 0);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PREDECESSORS), "1");

    /* Links with types and lags, as typed. */
    CHECK_N(opj_set_cell(doc, 2, OPJ_COL_PREDECESSORS, "2FF+1d, 1SS"), OWF_OK);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2FF+1d,1SS");
    CHECK_S(cell_of(doc, 2, OPJ_COL_START), "Mon 23/11/26");                     /* a day after Build's finish */
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2");
    CHECK_N(opj_redo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2FF+1d,1SS");
    CHECK_N(opj_redo(doc), OWF_ERR_FORMAT);                                       /* nothing to redo */
    CHECK_N(opj_undo(doc), OWF_OK);

    /* Start typed: Start No Earlier Than, as MS Project does; Plan then has slack. */
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_START, "Mon 23/11/26"), OWF_OK);
    CHECK_N(doc->plan->tasks[1].constraint, OWF_CON_SNET);
    CHECK_N(doc->plan->tasks[1].constraint_date, AT(23, 8, 0));
    CHECK_S(cell_of(doc, 1, OPJ_COL_FINISH), "Wed 25/11/26");
    CHECK_S(cell_of(doc, 2, OPJ_COL_START), "Wed 25/11/26");
    CHECK(!doc->plan->tasks[0].critical && doc->plan->tasks[1].critical);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_FINISH, "27/11/26"), OWF_OK);            /* FNET Friday 17:00 */
    CHECK_N(doc->plan->tasks[1].constraint, OWF_CON_FNET);
    CHECK_N(doc->plan->tasks[1].early_finish, AT(27, 17, 0));
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(doc->plan->tasks[1].constraint, OWF_CON_ASAP);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PERCENT, " 50 %"), OWF_OK);
    CHECK_N(doc->plan->tasks[1].percent, 50);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_NAME, "Build it"), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_NAME), "Build it");
    CHECK_N(opj_set_notes(doc, 1, "Ask first"), OWF_OK);
    CHECK_S(doc->plan->tasks[1].notes, "Ask first");
    CHECK_N(opj_set_constraint(doc, 0, OWF_CON_MSO, AT(17, 8, 0)), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_START), "Tue 17/11/26");
    CHECK_N(opj_set_constraint(doc, 0, 99, 0), OWF_ERR_FORMAT);
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK(doc->dirty);
    opj_doc_saved(doc);
    CHECK(!doc->dirty);
    opj_doc_free(doc);
}

static void test_outline(void)
{
    opj_doc *doc = three();
    char out[32];
    /* A summary above the three: the links move down a row with the tasks. */
    CHECK_N(opj_insert_task(doc, 0, "Phase"), OWF_OK);
    CHECK_N(doc->plan->ntasks, 4);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2");
    CHECK_S(cell_of(doc, 3, OPJ_COL_PREDECESSORS), "3");
    CHECK_N(opj_indent(doc, 0, 1, 1), OWF_ERR_FORMAT);                           /* nothing above it */
    CHECK_N(opj_indent(doc, 1, 3, 1), OWF_OK);
    CHECK(doc->plan->tasks[0].summary);
    opj_outline_number(doc, 0, out, sizeof out);
    CHECK_S(out, "1");
    CHECK_S(cell_of(doc, 3, OPJ_COL_WBS), "1.3");
    CHECK_S(cell_of(doc, 0, OPJ_COL_START), "Mon 16/11/26");
    CHECK_S(cell_of(doc, 0, OPJ_COL_FINISH), "Fri 20/11/26");
    CHECK_S(cell_of(doc, 0, OPJ_COL_DURATION), "5d");                            /* its span's work */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_DURATION, "2d"), OWF_ERR_FORMAT);       /* a summary's is worked out */
    CHECK_N(opj_indent(doc, 2, 1, 1), OWF_ERR_FORMAT);       /* Build under Plan, its predecessor: a loop */
    CHECK_N(opj_insert_task(doc, 2, "Note"), OWF_OK);
    CHECK_N(opj_indent(doc, 2, 1, 1), OWF_OK);                                    /* Note under Plan */
    CHECK_S(cell_of(doc, 2, OPJ_COL_WBS), "1.1.1");
    CHECK_S(cell_of(doc, 3, OPJ_COL_WBS), "1.2");
    CHECK_N(opj_indent(doc, 2, 1, 1), OWF_ERR_FORMAT);                           /* no deeper than below Plan */
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(opj_indent(doc, 3, 1, -1), OWF_OK);                                   /* Ship out to level 1 */
    CHECK_S(cell_of(doc, 3, OPJ_COL_WBS), "2");
    CHECK_N(opj_indent(doc, 3, 1, -1), OWF_ERR_FORMAT);
    CHECK_N(opj_undo(doc), OWF_OK);
    /* Deleting a summary takes its subtasks; Undo brings them back. */
    CHECK_N(opj_delete_tasks(doc, 0, 1), OWF_OK);
    CHECK_N(doc->plan->ntasks, 0);
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(doc->plan->ntasks, 4);
    /* Deleting Plan: Build's link to it goes, Ship's to Build moves up a row. */
    CHECK_N(opj_delete_tasks(doc, 1, 1), OWF_OK);
    CHECK_N(doc->plan->ntasks, 3);
    CHECK_S(cell_of(doc, 1, OPJ_COL_NAME), "Build");
    CHECK_S(cell_of(doc, 1, OPJ_COL_PREDECESSORS), "");
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2");
    CHECK_N(opj_undo(doc), OWF_OK);
    /* Unlink one row, then link the rows again. */
    CHECK_N(opj_unlink_rows(doc, 2, 1), OWF_OK);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "");
    CHECK_S(cell_of(doc, 3, OPJ_COL_PREDECESSORS), "");
    CHECK_S(cell_of(doc, 3, OPJ_COL_START), "Mon 16/11/26");
    CHECK_N(opj_link_rows(doc, 1, 3), OWF_OK);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2");
    CHECK_S(cell_of(doc, 3, OPJ_COL_PREDECESSORS), "3");
    CHECK_N(opj_unlink_rows(doc, 1, 3), OWF_OK);
    CHECK_S(cell_of(doc, 3, OPJ_COL_PREDECESSORS), "");
    CHECK_N(opj_link_rows(doc, 0, 2), OWF_ERR_FORMAT);                           /* a summary to its own subtask */
    CHECK_N(opj_link_rows(doc, 1, 1), OWF_ERR_FORMAT);
    CHECK_N(opj_delete_tasks(doc, 3, 2), OWF_ERR_FORMAT);
    opj_doc_free(doc);
}

static void test_undo_budget(void)
{
    opj_doc *doc = three();
    static char notes[20000];
    int i, steps = 0;
    memset(notes, 'n', sizeof notes - 1);
    CHECK_N(opj_set_notes(doc, 0, notes), OWF_OK);
    for (i = 0; i < 60; i++) {
        CHECK_N(opj_set_percent(doc, 0, i % 100), OWF_OK);
    }
    CHECK(doc->undo_lost);
    CHECK(doc->undo_bytes <= OPJ_UNDO_BUDGET);
    while (opj_can_undo(doc) && steps < 1000) {
        opj_undo(doc);
        steps++;
    }
    CHECK(steps > 10 && steps < 60);
    CHECK(opj_can_redo(doc));
    opj_doc_free(doc);
}

static void test_geometry(void)
{
    opj_doc *doc = three();
    opj_view v;
    opj_bar b;
    opj_path path;
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    CHECK_N(v.origin, AT(16, 0, 0));
    CHECK_N(opj_chart_left(&v), 548);                                              /* 32+180+56+96+96+84, then 4 */
    CHECK_N(opj_view_x(&v, AT(16, 8, 0)), 556);
    CHECK_N(opj_view_x(&v, AT(17, 17, 0)), 589);
    CHECK_N(opj_view_time(&v, 556), AT(16, 8, 0));
    CHECK_N(opj_view_time(&v, 572), AT(17, 0, 0));
    CHECK_N(opj_view_x(&v, AT(9, 0, 0)), 548 - 7 * 24);                           /* before the left edge */
    CHECK_N(opj_rows_shown(&v), 11);
    CHECK_N(opj_row_y(&v, 0), 32);
    CHECK(opj_bar_of(&v, doc, 0, &b));
    CHECK(b.kind == OPJ_BAR_TASK && b.critical);
    CHECK(b.bar.x == 556 && b.bar.w == 33 && b.bar.y == 36 && b.bar.h == 8 && b.done.w == 0);
    CHECK(opj_bar_of(&v, doc, 1, &b));
    CHECK(b.bar.x == 604 && b.bar.w == 57 && b.bar.y == 52);
    CHECK(opj_bar_of(&v, doc, 2, &b));
    CHECK(b.kind == OPJ_BAR_MILESTONE && b.bar.x == 657 && b.bar.y == 67 && b.bar.w == 9 && b.bar.h == 9);
    CHECK(!opj_bar_of(&v, doc, 3, &b));
    /* Plan to Build: out of Plan's end, down, into Build's start. */
    CHECK(opj_link_path(&v, doc, 1, 0, &path));
    CHECK_N(path.n, 4);
    CHECK(path.x[0] == 589 && path.y[0] == 40 && path.x[1] == 595 && path.y[1] == 40);
    CHECK(path.x[2] == 595 && path.y[2] == 56 && path.x[3] == 603 && path.y[3] == 56);
    /* Build to Ship: no room to come straight in, so round along the top of Ship's row. */
    CHECK(opj_link_path(&v, doc, 2, 0, &path));
    CHECK_N(path.n, 6);
    CHECK(path.x[0] == 661 && path.y[0] == 56 && path.x[1] == 667 && path.y[2] == 64);
    CHECK(path.x[3] == 650 && path.y[3] == 64 && path.x[4] == 650 && path.y[4] == 72);
    CHECK(path.x[5] == 656 && path.y[5] == 72);
    CHECK(!opj_link_path(&v, doc, 0, 0, &path));
    /* Half done: the middle third of the bar, half its width. */
    CHECK_N(opj_set_percent(doc, 1, 50), OWF_OK);
    CHECK(opj_bar_of(&v, doc, 1, &b));
    CHECK(b.done.x == 604 && b.done.w == 28 && b.done.y == 54 && b.done.h == 4);
    /* The other zooms. */
    v.zoom = OPJ_ZOOM_WEEKS;
    CHECK_N(opj_view_x(&v, AT(17, 0, 0)), 554);
    CHECK_N(opj_zoom_day_width(OPJ_ZOOM_MONTHS), 2);
    CHECK_N(opj_zoom_day_width(OPJ_ZOOM_QUARTERS), 1);
    /* A summary's bar. */
    CHECK_N(opj_insert_task(doc, 0, "Phase"), OWF_OK);
    CHECK_N(opj_indent(doc, 1, 3, 1), OWF_OK);
    v.zoom = OPJ_ZOOM_DAYS;
    CHECK(opj_bar_of(&v, doc, 0, &b));
    CHECK(b.kind == OPJ_BAR_SUMMARY && b.bar.x == 556 && b.bar.w == 661 - 556);
    opj_doc_free(doc);
}

static void test_drawing(void)
{
    opj_doc *doc = three();
    opj_view v;
    oldraw *d;
    oldraw_rect chart;
    unsigned long bg;
    CHECK_N(opj_set_percent(doc, 1, 50), OWF_OK);
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    bg = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND)) & 0xFFFFFFUL;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_draw(d, &v, doc, NULL);
    CHECK_X(px[40][572], 0xD03030);                       /* Plan: critical, red */
    CHECK_X(px[53][640], 0xD03030);                       /* Build's undone half */
    CHECK_X(px[55][610], 0x1A1A1A);                       /* its done half */
    CHECK_X(px[71][661], 0xD03030);                       /* Ship's diamond, its middle */
    CHECK_X(px[67][661], 0xD03030);                       /* its top point */
    CHECK_X(px[67][660], bg);                             /* beside the point: not the diamond */
    CHECK_X(px[48][595], 0x3050A0);                       /* Plan to Build's line, going down */
    CHECK_X(px[56][603], 0x3050A0);                       /* its arrow's tip */
    CHECK_X(px[53][600], 0x3050A0);                       /* the arrow's back */
    CHECK_X(px[64][660], 0x3050A0);                       /* Build to Ship, along Ship's row's top */
    CHECK_X(px[150][680], 0xE4E4E4);                      /* Saturday 21st: a day off */
    CHECK_X(px[150][560], bg);                            /* Monday: working */
    CHECK(drew("Task Name") && drew("Duration") && drew("Predecessors"));
    CHECK(drew("Plan") && drew("Build") && drew("Ship") && drew("2d") && drew("Mon 16/11/26"));
    CHECK(drew("16 Nov '26") && drew("M") && drew("F"));
    CHECK_X(px[47][100], 0xC8C8C8);                       /* the line under row 1 in the table */

    /* Only the chart is drawn when only it needs drawing. */
    chart.x = opj_chart_left(&v);
    chart.y = 0;
    chart.w = W - chart.x;
    chart.h = H;
    fills_left_of = chart.x;
    fills_left = 0;
    opj_draw(d, &v, doc, &chart);
    CHECK_N(fills_left, 0);
    fills_left_of = 0;

    /* A selected row is filled in the table; Weeks shades no days off (too narrow to see). */
    v.selected = 0;
    v.zoom = OPJ_ZOOM_WEEKS;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_draw(d, &v, doc, NULL);
    CHECK_X(px[40][100], oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_FILL)) & 0xFFFFFFUL);
    CHECK(drew("Nov 2026") && drew("16"));
    v.zoom = OPJ_ZOOM_QUARTERS;
    ntexts = 0;
    opj_draw(d, &v, doc, NULL);
    CHECK(drew("2026") && drew("Q4"));
    v.zoom = OPJ_ZOOM_MONTHS;
    ntexts = 0;
    opj_draw(d, &v, doc, NULL);
    CHECK(drew("Q4 2026") && drew("Nov"));
    oldraw_free(d);
    opj_doc_free(doc);
}

static void test_hits(void)
{
    opj_doc *doc = three();
    opj_view v;
    opj_hit h;
    CHECK_N(opj_set_percent(doc, 1, 50), OWF_OK);
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    opj_hit_test(&v, doc, 572, 40, &h);
    CHECK(h.what == OPJ_HIT_BAR && h.row == 0 && h.part == 0);
    opj_hit_test(&v, doc, 587, 40, &h);
    CHECK(h.what == OPJ_HIT_BAR && h.part == 1);           /* the right end: stretch */
    opj_hit_test(&v, doc, 610, 55, &h);
    CHECK(h.what == OPJ_HIT_BAR && h.row == 1 && h.part == 2);
    opj_hit_test(&v, doc, 100, 40, &h);
    CHECK(h.what == OPJ_HIT_CELL && h.row == 0 && h.column == OPJ_COL_NAME);
    opj_hit_test(&v, doc, 10, 5, &h);
    CHECK(h.what == OPJ_HIT_COLUMN_TITLE && h.column == OPJ_COL_ID);
    opj_hit_test(&v, doc, 546, 50, &h);
    CHECK_N(h.what, OPJ_HIT_SPLIT);
    opj_hit_test(&v, doc, 572, 5, &h);
    CHECK(h.what == OPJ_HIT_TIMESCALE && h.time == AT(17, 0, 0));
    opj_hit_test(&v, doc, 700, 40, &h);
    CHECK(h.what == OPJ_HIT_CHART && h.row == 0);
    opj_hit_test(&v, doc, 700, 150, &h);
    CHECK(h.what == OPJ_HIT_CHART && h.row == -1);
    opj_hit_test(&v, doc, 100, 150, &h);
    CHECK_N(h.what, OPJ_HIT_NONE);
    opj_hit_test(&v, doc, -1, 5, &h);
    CHECK_N(h.what, OPJ_HIT_NONE);
    opj_doc_free(doc);
}

/* Resources: Resource Names typed on the sheet, the resource sheet, usage by period, names by the bars. */
static void test_resources(void)
{
    opj_doc *doc = three();
    char out[64];
    int at = -1, over = -1;
    opj_view v;
    oldraw *d;
    oldraw_rect box;
    unsigned long changes;

    /* Typed Resource Names make the resources. Plan is 2 days: Dale 16h, Bob half time 8h. */
    CHECK_S(opj_column_title(OPJ_COL_RESOURCES), "Resource Names");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_RESOURCES, "Dale, Bob [50%]"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_RESOURCES), "Dale,Bob[50%]");
    CHECK_N(doc->plan->nresources, 2);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_RESOURCES, "Dale[x]"), OWF_ERR_FORMAT);
    CHECK_S(cell_of(doc, 0, OPJ_COL_RESOURCES), "Dale,Bob[50%]");          /* left as it was */
    opj_resource_cell_text(doc, 0, OPJ_RCOL_WORK, out, sizeof out);
    CHECK_S(out, "16h");
    opj_resource_cell_text(doc, 1, OPJ_RCOL_WORK, out, sizeof out);
    CHECK_S(out, "8h");
    opj_resource_cell_text(doc, 0, OPJ_RCOL_TYPE, out, sizeof out);
    CHECK_S(out, "Work");
    opj_resource_cell_text(doc, 0, OPJ_RCOL_MAX, out, sizeof out);
    CHECK_S(out, "100%");
    CHECK_S(opj_resource_column_title(OPJ_RCOL_RATE), "Std. Rate");

    /* Rates: 20 an hour makes Dale's 16 hours 320.00; 160 a day of 8 hours is 20 an hour. */
    CHECK_N(opj_set_resource_cell(doc, 0, OPJ_RCOL_RATE, "20"), OWF_OK);
    opj_resource_cell_text(doc, 0, OPJ_RCOL_COST, out, sizeof out);
    CHECK_S(out, "320.00");
    opj_resource_cell_text(doc, 0, OPJ_RCOL_RATE, out, sizeof out);
    CHECK_S(out, "20.00/h");
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_RATE, "160/d"), OWF_OK);
    CHECK_N(doc->plan->resources[1].rate, 2000);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_RATE, "\xC2\xA3" "25.50/hr"), OWF_OK);    /* a pound sign */
    CHECK_N(doc->plan->resources[1].rate, 2550);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_RATE, "25/week"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_RATE, "abc"), OWF_ERR_FORMAT);
    CHECK_N(doc->plan->resources[1].rate, 2550);

    /* Names: not empty, not another's (case ignored), nothing that breaks Resource Names. */
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_NAME, "dale"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_NAME, "Bob, Jr"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_NAME, ""), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_NAME, "Robert"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_RESOURCES), "Dale,Robert[50%]");
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_INITIALS, "R"), OWF_OK);
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_GROUP, "Build team"), OWF_OK);
    opj_resource_cell_text(doc, 1, OPJ_RCOL_GROUP, out, sizeof out);
    CHECK_S(out, "Build team");
    CHECK_N(opj_set_resource_cell(doc, 1, OPJ_RCOL_WORK, "5h"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 5, OPJ_RCOL_NAME, "X"), OWF_ERR_FORMAT);

    /* Max. Units, and over-allocation: Dale full time on Plan and on Build at once is 200%. */
    CHECK_N(opj_set_resource_cell(doc, 0, OPJ_RCOL_MAX, "150%"), OWF_OK);
    CHECK_N(doc->plan->resources[0].max_units, 150);
    CHECK_N(opj_set_resource_cell(doc, 0, OPJ_RCOL_MAX, "100"), OWF_OK);
    CHECK_N(opj_set_resource_cell(doc, 0, OPJ_RCOL_MAX, "lots"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_assignment(doc, 1, 0, 100), OWF_OK);
    opj_resource_cell_text(doc, 0, OPJ_RCOL_ID, out, sizeof out);
    CHECK_S(out, "1");                                                     /* Build follows Plan */
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, ""), OWF_OK);
    opj_resource_cell_text(doc, 0, OPJ_RCOL_ID, out, sizeof out);
    CHECK_S(out, "1!");
    CHECK_N(doc->plan->resources[0].peak, 200);

    /* Periods. */
    CHECK_N(opj_period_start(OPJ_ZOOM_DAYS, AT(18, 10, 0)), AT(18, 0, 0));
    CHECK_N(opj_period_start(OPJ_ZOOM_WEEKS, AT(18, 10, 0)), AT(16, 0, 0));
    CHECK_N(opj_period_start(OPJ_ZOOM_MONTHS, AT(18, 10, 0)), AT(1, 0, 0));
    CHECK_N(opj_period_start(OPJ_ZOOM_QUARTERS, AT(18, 10, 0)), owf_ptime_make(2026, 10, 1, 0, 0));
    CHECK_N(opj_period_next(OPJ_ZOOM_QUARTERS, owf_ptime_make(2026, 10, 1, 0, 0)), owf_ptime_make(2027, 1, 1, 0, 0));
    CHECK_N(opj_period_next(OPJ_ZOOM_MONTHS, AT(1, 0, 0)), owf_ptime_make(2026, 12, 1, 0, 0));
    CHECK_N(opj_period_next(OPJ_ZOOM_WEEKS, AT(16, 0, 0)), AT(23, 0, 0));

    /* Usage: on Monday Dale works 8h on Plan and 8h on Build, 16h of an 8 hour day. */
    opj_usage_text(doc, 0, -1, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out, &over);
    CHECK_S(out, "16h");
    CHECK_N(over, 1);
    opj_usage_text(doc, 0, 0, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out, NULL);
    CHECK_S(out, "8h");                                                    /* Plan's assignment alone */
    opj_usage_text(doc, 1, -1, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out, &over);
    CHECK_S(out, "4h");                                                    /* half of 8 hours */
    CHECK_N(over, 0);
    opj_usage_text(doc, 1, -1, AT(18, 0, 0), AT(19, 0, 0), out, sizeof out, &over);
    CHECK_S(out, "");                                                      /* Plan is over by then */
    opj_usage_text(doc, 0, -1, AT(16, 0, 0), AT(23, 0, 0), out, sizeof out, &over);
    CHECK_S(out, "40h");                                                   /* 16h + 24h in the week */
    CHECK_N(over, 0);                                                      /* of the week's 40 */

    /* A material: 3 tonnes of steel at 5.00 is 15.00, and no work. */
    CHECK_N(opj_add_resource(doc, "Steel", &at), OWF_OK);
    CHECK_N(at, 2);
    CHECK_N(opj_add_resource(doc, "STEEL", NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 2, OPJ_RCOL_TYPE, "m"), OWF_OK);
    CHECK_N(opj_set_resource_cell(doc, 2, OPJ_RCOL_RATE, "5/h"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_resource_cell(doc, 2, OPJ_RCOL_RATE, "5"), OWF_OK);
    CHECK_N(opj_set_resource_cell(doc, 2, OPJ_RCOL_MAX, "50%"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_RESOURCES, "Dale,Steel[3]"), OWF_OK);
    opj_resource_cell_text(doc, 2, OPJ_RCOL_COST, out, sizeof out);
    CHECK_S(out, "15.00");
    opj_resource_cell_text(doc, 2, OPJ_RCOL_RATE, out, sizeof out);
    CHECK_S(out, "5.00");
    opj_resource_cell_text(doc, 2, OPJ_RCOL_MAX, out, sizeof out);
    CHECK_S(out, "");
    opj_usage_text(doc, 2, -1, AT(16, 0, 0), AT(23, 0, 0), out, sizeof out, NULL);
    CHECK_S(out, "");                                                      /* no hours for a material */

    /* Deleting Dale takes his assignments; Undo brings both back. */
    changes = doc->changes;
    CHECK_N(opj_delete_resources(doc, 0, 1), OWF_OK);
    CHECK_N(doc->plan->nresources, 2);
    CHECK_S(cell_of(doc, 0, OPJ_COL_RESOURCES), "Robert[50%]");
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Steel[3.00]");
    CHECK_N(opj_delete_resources(doc, 1, 5), OWF_ERR_FORMAT);
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(doc->changes, changes + 2);
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Dale,Steel[3.00]");
    CHECK_N(opj_set_assignment(doc, 1, 0, 0), OWF_OK);                       /* unassigned */
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Steel[3.00]");
    CHECK_N(opj_set_assignment(doc, 1, 9, 100), OWF_ERR_FORMAT);

    /* Inserting a task above keeps the names on their tasks. */
    CHECK_N(opj_insert_task(doc, 0, "Survey"), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Dale,Robert[50%]");
    CHECK_S(cell_of(doc, 0, OPJ_COL_RESOURCES), "");

    /* Work and money as the sheet shows them. */
    opj_work_text(90, out);
    CHECK_S(out, "1.5h");
    opj_work_text(20, out);
    CHECK_S(out, "0.33h");
    opj_work_text(0, out);
    CHECK_S(out, "0h");
    opj_money_text(123450, out);
    CHECK_S(out, "1234.50");
    opj_money_text(5, out);
    CHECK_S(out, "0.05");

    /* The names go 6 pixels right of the bar; a row with none has no label. */
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    CHECK_N(opj_label_box(&v, doc, 0, &box), 0);                             /* Survey: no one */
    CHECK(opj_label_box(&v, doc, 1, &box));
    {
        opj_bar b;
        CHECK(opj_bar_of(&v, doc, 1, &b));
        CHECK_N(box.x, b.bar.x + b.bar.w + 6);
        CHECK_N(box.y, opj_row_y(&v, 1));
    }
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    ntexts = 0;
    opj_draw(d, &v, doc, NULL);
    CHECK(drew("Dale,Robert[50%]") && drew("Steel[3.00]"));
    oldraw_free(d);
    opj_doc_free(doc);
}

int main(void)
{
    test_dates();
    test_sheet();
    test_outline();
    test_undo_budget();
    test_geometry();
    test_drawing();
    test_hits();
    test_resources();
    if (fails) {
        printf("project view tests: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("project view tests passed (%d checks)\n", checks);
    return 0;
}
