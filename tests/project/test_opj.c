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

/* Set Baseline, a slip, the variance columns, Undo, and the baseline bars. */
static void test_baseline(void)
{
    opj_doc *doc = three();
    opj_view v;
    oldraw_rect r;
    opj_bar b;
    oldraw *d;
    CHECK_S(cell_of(doc, 0, OPJ_COL_BASELINE_START), "NA");
    CHECK_S(cell_of(doc, 0, OPJ_COL_FINISH_VARIANCE), "0d");
    CHECK_S(opj_column_title(OPJ_COL_START_VARIANCE), "Start Var.");
    CHECK_N(opj_set_baseline(doc, 0, 9), OWF_ERR_FORMAT);
    CHECK_N(opj_set_baseline(doc, 0, -1), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_BASELINE_START), "Wed 18/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_BASELINE_FINISH), "Fri 20/11/26");
    /* Plan takes 3 days: Build starts and finishes a day late; Plan back to 1 day: a day early. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_DURATION, "3d"), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_START_VARIANCE), "1d");
    CHECK_S(cell_of(doc, 2, OPJ_COL_FINISH_VARIANCE), "1d");
    CHECK_S(cell_of(doc, 0, OPJ_COL_START_VARIANCE), "0d");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_DURATION, "1d"), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_START_VARIANCE), "-1d");
    CHECK_N(opj_clear_baseline(doc, 1, 1), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_BASELINE_START), "NA");
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_BASELINE_START), "Wed 18/11/26");
    CHECK_N(opj_clear_baseline(doc, 3, 1), OWF_ERR_FORMAT);

    /* The baseline bar: under the bar, Wed 08:00 to Fri 17:00 at the baseline, a sixth of a row high. */
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    CHECK_N(opj_baseline_bar(&v, doc, 1, &r), 0);                         /* not shown yet */
    v.show_baseline = 1;
    CHECK(opj_baseline_bar(&v, doc, 1, &r));
    CHECK_N(r.x, opj_view_x(&v, AT(18, 8, 0)));
    CHECK_N(r.x + r.w, opj_view_x(&v, AT(20, 17, 0)));
    CHECK_N(r.y, opj_row_y(&v, 1) + 13);
    CHECK_N(r.h, 2);
    CHECK(opj_bar_of(&v, doc, 1, &b) && b.bar.y + b.bar.h <= r.y);         /* clear of the bar */
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    memset(px, 0, sizeof px);
    opj_draw(d, &v, doc, NULL);
    CHECK_X(px[r.y][r.x + r.w - 2], 0x909090);
    oldraw_free(d);
    opj_doc_free(doc);
}

/* Assign Resources: units typed for each kind, the dialog's lines, Assign, Remove, Replace and Undo. */
static void test_assign_dialog(void)
{
    opj_doc *doc = three();
    opj_assign_info info;
    long u = -1;
    char text[80];
    int uid;
    CHECK_N(opj_add_resource(doc, "Dale", NULL), OWF_OK);                 /* 0 */
    CHECK_N(opj_set_resource_cell(doc, 0, OPJ_RCOL_RATE, "20/h"), OWF_OK);
    CHECK_N(opj_add_resource(doc, "Bob", NULL), OWF_OK);                  /* 1 */
    CHECK_N(opj_add_resource(doc, "Steel", NULL), OWF_OK);                /* 2, a material at 5.00 */
    CHECK_N(opj_set_resource_cell(doc, 2, OPJ_RCOL_TYPE, "m"), OWF_OK);
    CHECK_N(opj_set_resource_cell(doc, 2, OPJ_RCOL_RATE, "5"), OWF_OK);
    CHECK_N(opj_add_resource(doc, "Travel", NULL), OWF_OK);               /* 3, a cost */
    CHECK_N(opj_set_resource_cell(doc, 3, OPJ_RCOL_TYPE, "c"), OWF_OK);

    /* what the Units box takes */
    CHECK_N(opj_units_parse(doc, 0, "50%", &u), OWF_OK);
    CHECK_N(u, 50);
    CHECK_N(opj_units_parse(doc, 0, " 150 ", &u), OWF_OK);
    CHECK_N(u, 150);
    CHECK_N(opj_units_parse(doc, 0, "50.5%", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 0, "0%", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 0, "", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 0, "200000", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 0, "half", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 0, "50%%", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 2, "2.5", &u), OWF_OK);
    CHECK_N(u, 250);
    CHECK_N(opj_units_parse(doc, 2, "2.5%", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(doc, 3, "$120", &u), OWF_OK);
    CHECK_N(u, 12000);
    CHECK_N(opj_units_parse(doc, 3, "120.00", &u), OWF_OK);
    CHECK_N(u, 12000);
    CHECK_N(opj_units_parse(doc, 9, "1", &u), OWF_ERR_FORMAT);
    CHECK_N(opj_units_parse(NULL, 0, "1", &u), OWF_ERR_FORMAT);
    opj_units_text(doc, 0, 50, text, sizeof text);
    CHECK_S(text, "50%");
    opj_units_text(doc, 2, 250, text, sizeof text);
    CHECK_S(text, "2.5");
    opj_units_text(doc, 3, 12000, text, sizeof text);
    CHECK_S(text, "120.00");
    opj_units_text(doc, 9, 1, text, sizeof text);
    CHECK_S(text, "");

    /* Assign: 100% when no units are typed; Build is 3 days, 24h at 20.00 */
    CHECK(opj_assign_get(doc, 1, 0, &info) && !info.assigned);
    CHECK(!opj_assign_get(doc, 9, 0, &info) && !opj_assign_get(doc, 1, 9, &info));
    opj_assign_line(doc, 1, 0, text, sizeof text);
    CHECK_S(text, "  Dale                                    ");
    CHECK_N(opj_assign_resource(doc, 1, 0, NULL), OWF_OK);
    CHECK(opj_assign_get(doc, 1, 0, &info) && info.assigned);
    CHECK_N(info.units, 100);
    CHECK_N(info.work, 24 * 60);
    CHECK_N(info.cost, 48000);
    opj_assign_line(doc, 1, 0, text, sizeof text);
    CHECK_S(text, "* Dale                     100%     480.00");
    CHECK_N(opj_assign_resource(doc, 1, 0, "50%"), OWF_OK);
    CHECK(opj_assign_get(doc, 1, 0, &info) && info.units == 50 && info.cost == 24000);
    CHECK_N(opj_assign_resource(doc, 1, 0, ""), OWF_OK);                   /* its own units stay */
    CHECK(opj_assign_get(doc, 1, 0, &info) && info.units == 50);
    CHECK_N(opj_assign_resource(doc, 1, 0, "lots"), OWF_ERR_FORMAT);
    CHECK_N(opj_assign_resource(doc, 1, 3, NULL), OWF_ERR_FORMAT);          /* a cost needs its amount */
    CHECK_N(opj_assign_resource(doc, 1, 3, "120"), OWF_OK);
    CHECK_N(opj_assign_resource(doc, 7, 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_assign_resource(doc, 1, 7, NULL), OWF_ERR_FORMAT);
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Dale[50%],Travel[120.00]");

    /* Replace: Bob takes Dale's place, units and uid */
    uid = doc->plan->assigns[0].uid;
    CHECK_N(opj_replace_resource(doc, 1, 0, 1, NULL), OWF_OK);
    CHECK(opj_assign_get(doc, 1, 0, &info) && !info.assigned);
    CHECK(opj_assign_get(doc, 1, 1, &info) && info.assigned && info.units == 50);
    CHECK_N(doc->plan->assigns[0].uid, uid);
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Bob[50%],Travel[120.00]");
    CHECK_N(opj_replace_resource(doc, 1, 1, 2, NULL), OWF_OK);             /* another kind: one unit */
    opj_assign_line(doc, 1, 2, text, sizeof text);
    CHECK_S(text, "* Steel                       1       5.00");
    CHECK_N(opj_replace_resource(doc, 1, 2, 0, "75%"), OWF_OK);
    CHECK(opj_assign_get(doc, 1, 0, &info) && info.units == 75);
    CHECK_N(opj_replace_resource(doc, 1, 1, 2, NULL), OWF_ERR_FORMAT);     /* Bob isn't assigned now */
    CHECK_N(opj_replace_resource(doc, 1, 0, 3, NULL), OWF_ERR_FORMAT);     /* Travel already is */
    CHECK_N(opj_replace_resource(doc, 1, 0, 0, NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_replace_resource(doc, 1, 0, 9, NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_replace_resource(doc, 1, 0, 1, "0"), OWF_ERR_FORMAT);
    CHECK(opj_assign_get(doc, 1, 0, &info) && info.units == 75);

    /* Remove, and Undo brings it back */
    CHECK_N(opj_remove_resource(doc, 1, 3), OWF_OK);
    CHECK_N(opj_remove_resource(doc, 1, 3), OWF_ERR_FORMAT);
    CHECK_N(opj_remove_resource(doc, 1, 9), OWF_ERR_FORMAT);
    CHECK_N(opj_remove_resource(doc, 1, -1), OWF_ERR_FORMAT);
    /* the name Replace... asks for */
    CHECK_N(opj_find_resource(doc, "dale"), 0);
    CHECK_N(opj_find_resource(doc, "  STEEL "), 2);
    CHECK_N(opj_find_resource(doc, "4"), 3);
    CHECK_N(opj_find_resource(doc, "5"), -1);
    CHECK_N(opj_find_resource(doc, "0"), -1);
    CHECK_N(opj_find_resource(doc, "Dal"), -1);
    CHECK_N(opj_find_resource(doc, ""), -1);
    CHECK_N(opj_find_resource(doc, NULL), -1);
    CHECK_N(opj_find_resource(doc, "99999999"), -1);
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Dale[75%]");
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_RESOURCES), "Dale[75%],Travel[120.00]");
    opj_doc_free(doc);
}

/* Baselines 1 to 10 in the core: set, clear, Undo, which are saved, and the names the window takes. */
static void test_baseline_n(void)
{
    opj_doc *doc = three();
    owf_pbaseline b;
    char text[40];
    CHECK_N(opj_baselines_saved(doc), 0);
    opj_baselines_text(doc, text, sizeof text);
    CHECK_S(text, "none");
    CHECK_N(opj_set_baseline_n(doc, 11, 0, -1), OWF_ERR_FORMAT);
    CHECK_N(opj_set_baseline_n(doc, -1, 0, -1), OWF_ERR_FORMAT);
    CHECK_N(opj_clear_baseline_n(doc, 11, 0, -1), OWF_ERR_FORMAT);
    CHECK_N(opj_set_baseline_n(doc, 2, 0, 9), OWF_ERR_FORMAT);
    CHECK_N(opj_set_baseline_n(doc, 2, 1, 1), OWF_OK);                    /* Build alone */
    CHECK(owf_ptask_baseline(&doc->plan->tasks[1], 2, &b));
    CHECK_N(b.start, AT(18, 8, 0));
    CHECK(!owf_ptask_baseline(&doc->plan->tasks[0], 2, &b));
    CHECK_S(cell_of(doc, 1, OPJ_COL_BASELINE_START), "NA");                /* Baseline (0) is another */
    CHECK_N(opj_baselines_saved(doc), 1u << 2);
    CHECK_N(opj_set_baseline(doc, 0, -1), OWF_OK);
    CHECK_N(opj_set_baseline_n(doc, 10, 0, -1), OWF_OK);
    opj_baselines_text(doc, text, sizeof text);
    CHECK_S(text, "0 2 10");
    opj_baselines_text(doc, text, 4);                                      /* cut short, still a string */
    CHECK_S(text, "0 2");
    CHECK_N(opj_clear_baseline_n(doc, 10, 0, -1), OWF_OK);
    CHECK_N(opj_baselines_saved(doc), (1u << 0) | (1u << 2));
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(opj_baselines_saved(doc), (1u << 0) | (1u << 2) | (1u << 10));
    CHECK_N(opj_clear_baseline(doc, 0, -1), OWF_OK);                       /* 0 is the old call */
    CHECK_N(opj_baselines_saved(doc), (1u << 2) | (1u << 10));
    /* what Tools > Set Baseline... takes */
    CHECK_N(opj_baseline_parse(""), 0);
    CHECK_N(opj_baseline_parse("Baseline"), 0);
    CHECK_N(opj_baseline_parse(" 3 "), 3);
    CHECK_N(opj_baseline_parse("Baseline 10"), 10);
    CHECK_N(opj_baseline_parse("baseline7"), 7);
    CHECK_N(opj_baseline_parse("BASELINE 1"), 1);
    CHECK_N(opj_baseline_parse("11"), -1);
    CHECK_N(opj_baseline_parse("0010"), -1);
    CHECK_N(opj_baseline_parse("two"), -1);
    CHECK_N(opj_baseline_parse("Baseline 3x"), -1);
    CHECK_N(opj_baseline_parse(NULL), 0);
    opj_doc_free(doc);
}

/* Printing: pages down then across; the table only on the first pages across; a footer on each. */
static void test_print(void)
{
    opj_doc *doc = three();
    opj_view proto, v;
    opj_print pr;
    oldraw *d;
    unsigned long bg;
    doc->plan->title = owf_malloc(8);
    if (doc->plan->title) strcpy(doc->plan->title, "Kitchen");
    opj_view_init(&proto, doc, 0, 0, 16, OPJ_ZOOM_DAYS);
    CHECK_N(opj_chart_left(&proto), 548);
    /* 800 by 80: two rows a page (80 less the timescale's 32 and the footer's 16), 10 days of chart on the
     * first page across, and the plan's 6 days fit: two pages. */
    CHECK_N(opj_print_layout(&pr, &proto, doc, 800, 80), 2);
    CHECK(pr.rows_per_page == 2 && pr.down == 2 && pr.across == 1 && pr.first_days == 10 && pr.more_days == 33);
    CHECK(opj_print_page_view(&pr, 1, &v));
    CHECK(v.first_row == 2 && v.height == 64 && v.width == 800 && v.origin == proto.origin);
    CHECK_N(opj_print_page_view(&pr, 2, &v), 0);
    v = proto;                                   /* a row height of 0 or less is refused, not divided by */
    v.row_height = 0;
    CHECK_N(opj_print_layout(&pr, &v, doc, 800, 80), 0);
    v.row_height = -16;
    CHECK_N(opj_print_layout(&pr, &v, doc, 800, 80), 0);
    /* 600 wide: 2 days of chart on the first page across, 24 on the next: four pages. */
    CHECK_N(opj_print_layout(&pr, &proto, doc, 600, 80), 4);
    CHECK(pr.across == 2 && pr.first_days == 2 && pr.more_days == 24);
    CHECK(opj_print_page_view(&pr, 2, &v));                                /* down 0, across 1 */
    CHECK_N(v.first_row, 0);
    CHECK_N(opj_chart_left(&v), 4);                                        /* no table */
    CHECK_N(v.origin, AT(18, 0, 0));
    CHECK_N(opj_view_x(&v, AT(18, 8, 0)), 12);
    /* Too small: no row of tasks, or no day of chart. */
    CHECK_N(opj_print_layout(&pr, &proto, doc, 600, 60), 0);
    CHECK_N(opj_print_layout(&pr, &proto, doc, 560, 80), 0);
    /* Drawing page 3 of 4: Build's bar from x 12 (Wed 08:00) to 69 (Fri 17:00), no table, the footer. */
    CHECK_N(opj_print_layout(&pr, &proto, doc, 600, 80), 4);
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    bg = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND)) & 0xFFFFFFUL;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_print_draw(d, &pr, doc, 2);
    CHECK_X(px[56][30], 0xD03030);
    CHECK_X(px[56][150], bg);                                             /* past the bars and lines */
    CHECK_X(px[70][599], bg);                                              /* the page is cleared */
    CHECK(drew("Page 3 of 4") && drew("Kitchen") && !drew("Task Name") && !drew("Build"));
    ntexts = 0;
    opj_print_draw(d, &pr, doc, 1);                                        /* down 1: Ship's row, table */
    CHECK(drew("Page 2 of 4") && drew("Task Name") && drew("Ship") && !drew("Plan"));
    ntexts = 0;
    opj_print_draw(d, &pr, doc, 9);
    CHECK_N(ntexts, 0);
    oldraw_free(d);
    opj_doc_free(doc);
}

/* The Resource Usage view: each resource, its assignments under it, and the work in each period. Dale
 * full time on Plan (Mon-Tue) and on Build, which no longer follows Plan, so both from Monday: 16h on an 8
 * hour Monday, over-allocated. Bob half time on Plan. */
static void test_usage(void)
{
    opj_doc *doc = three();
    opj_view v, u;
    oldraw *d;
    char out[64];
    int r = -1, a = -2;
    unsigned long bg;
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_RESOURCES, "Dale,Bob[50%]"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_RESOURCES, "Dale"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, ""), OWF_OK);
    /* Rows: Dale, his Plan and Build; Bob, his Plan. */
    CHECK_N(opj_usage_rows(doc), 5);
    CHECK(opj_usage_row(doc, 0, &r, &a) && r == 0 && a == -1);
    CHECK(opj_usage_row(doc, 2, &r, &a) && r == 0 && a >= 0 && doc->plan->assigns[a].task == 1);
    CHECK(opj_usage_row(doc, 3, &r, &a) && r == 1 && a == -1);
    CHECK(opj_usage_row(doc, 4, &r, &a) && r == 1 && doc->plan->assigns[a].task == 0);
    CHECK_N(opj_usage_row(doc, 5, &r, &a), 0);
    CHECK_N(opj_usage_row(doc, -1, &r, &a), 0);
    CHECK_N(opj_usage_row(NULL, 0, &r, &a), 0);
    CHECK_N(opj_usage_rows(NULL), 0);
    /* The table: the resource sheet's ID, name and work; an assignment's task and work. */
    CHECK_S(opj_usage_column_title(OPJ_UCOL_NAME), "Resource Name");
    CHECK_S(opj_usage_column_title(9), "");
    opj_usage_cell_text(doc, 0, OPJ_UCOL_ID, out, sizeof out);
    CHECK_S(out, "1!");
    opj_usage_cell_text(doc, 0, OPJ_UCOL_WORK, out, sizeof out);
    CHECK_S(out, "40h");                                                   /* Plan's 16h and Build's 24h */
    opj_usage_cell_text(doc, 1, OPJ_UCOL_NAME, out, sizeof out);
    CHECK_S(out, "Plan");
    opj_usage_cell_text(doc, 1, OPJ_UCOL_ID, out, sizeof out);
    CHECK_S(out, "");
    opj_usage_cell_text(doc, 2, OPJ_UCOL_WORK, out, sizeof out);
    CHECK_S(out, "24h");
    opj_usage_cell_text(doc, 4, OPJ_UCOL_WORK, out, sizeof out);
    CHECK_S(out, "8h");                                                    /* half of Plan's 16h */
    opj_usage_cell_text(doc, 9, OPJ_UCOL_NAME, out, sizeof out);
    CHECK_S(out, "");
    /* Its places: a table of 32 + 180 + 72, then the split, then the Days chart from Monday. */
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    opj_usage_view(&v, &u);
    CHECK_N(opj_chart_left(&u), 288);
    CHECK_N(opj_view_x(&u, AT(17, 0, 0)), 312);
    /* Drawn: titles, names, each day's work; a resource's row shaded across the chart. */
    v.selected = 1;
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    bg = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND)) & 0xFFFFFFUL;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_usage_draw(d, &v, doc, NULL);
    CHECK(drew("Resource Name") && drew("Work") && drew("Dale") && drew("Bob") && drew("Build") && drew("1!"));
    CHECK(drew("16h") && drew("8h") && drew("4h"));                         /* Dale's, his Plan's, Bob's Monday */
    CHECK(!drew("Task Name"));                                              /* not the Gantt view's table */
    CHECK_X(px[40][300], 0xF0F0F0);                                        /* Dale's row */
    CHECK_X(px[56][300], bg);                                              /* his Plan's */
    CHECK_X(px[40][311], 0xC8C8C8);                                        /* Monday's right edge */
    CHECK_X(px[56][10], oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_FILL)) & 0xFFFFFFUL);   /* row 1 selected */
    /* Scrolled to Bob: only his rows. */
    v.first_row = 3;
    v.selected = -1;
    ntexts = 0;
    opj_usage_draw(d, &v, doc, NULL);
    CHECK(drew("Bob") && !drew("Dale") && drew("Plan") && !drew("Build"));
    oldraw_free(d);
    opj_doc_free(doc);
}

/* Task Usage, from test_usage's plan: each task, then who works on it, and the work each day. */
static void test_task_usage(void)
{
    opj_doc *doc = three();
    opj_view v;
    oldraw *d;
    char out[64];
    int t = -1, a = -2, e;
    unsigned long bg;
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_RESOURCES, "Dale,Bob[50%]"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_RESOURCES, "Dale"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, ""), OWF_OK);
    /* Rows: Plan, its Dale and Bob; Build, its Dale; Ship. */
    CHECK_N(opj_task_usage_rows(doc), 6);
    CHECK(opj_task_usage_row(doc, 0, &t, &a) && t == 0 && a == -1);
    CHECK(opj_task_usage_row(doc, 2, &t, &a) && t == 0 && a >= 0 && doc->plan->assigns[a].resource == 1);
    CHECK(opj_task_usage_row(doc, 3, &t, &a) && t == 1 && a == -1);
    CHECK(opj_task_usage_row(doc, 4, &t, &a) && t == 1 && doc->plan->assigns[a].resource == 0);
    CHECK(opj_task_usage_row(doc, 5, &t, &a) && t == 2 && a == -1);
    CHECK_N(opj_task_usage_row(doc, 6, &t, &a), 0);
    CHECK_N(opj_task_usage_row(doc, -1, &t, &a), 0);
    CHECK_N(opj_task_usage_rows(NULL), 0);
    /* The table: a task's ID, name and work; an assignment's resource and work. */
    CHECK_S(opj_task_usage_column_title(OPJ_UCOL_NAME), "Task Name");
    CHECK_S(opj_task_usage_column_title(-1), "");
    opj_task_usage_cell_text(doc, 0, OPJ_UCOL_ID, out, sizeof out);
    CHECK_S(out, "1");
    opj_task_usage_cell_text(doc, 0, OPJ_UCOL_WORK, out, sizeof out);
    CHECK_S(out, "24h");                                                   /* Dale's 16h and Bob's 8h */
    opj_task_usage_cell_text(doc, 1, OPJ_UCOL_NAME, out, sizeof out);
    CHECK_S(out, "Dale");
    opj_task_usage_cell_text(doc, 1, OPJ_UCOL_ID, out, sizeof out);
    CHECK_S(out, "");
    opj_task_usage_cell_text(doc, 2, OPJ_UCOL_WORK, out, sizeof out);
    CHECK_S(out, "8h");
    opj_task_usage_cell_text(doc, 5, OPJ_UCOL_NAME, out, sizeof out);
    CHECK_S(out, "Ship");
    opj_task_usage_cell_text(doc, 9, OPJ_UCOL_NAME, out, sizeof out);
    CHECK_S(out, "");
    /* Work on Monday the 16th: Plan's 8h and 4h; Build's 8h; Ship none */
    opj_task_usage_text(doc, 0, -1, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out);
    CHECK_S(out, "12h");
    opj_task_usage_text(doc, 1, -1, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out);
    CHECK_S(out, "8h");
    CHECK(opj_task_usage_row(doc, 2, &t, &a));
    opj_task_usage_text(doc, 0, a, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out);
    CHECK_S(out, "4h");
    opj_task_usage_text(doc, 1, a, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out);   /* not Build's */
    CHECK_S(out, "");
    opj_task_usage_text(doc, 2, -1, AT(16, 0, 0), AT(30, 0, 0), out, sizeof out);
    CHECK_S(out, "");
    opj_task_usage_text(doc, 0, -1, AT(17, 0, 0), AT(16, 0, 0), out, sizeof out);   /* backwards */
    CHECK_S(out, "");
    /* Drawn: its own titles, names, each day's work; a task's row shaded across the chart. */
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    bg = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND)) & 0xFFFFFFUL;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_task_usage_draw(d, &v, doc, NULL);
    CHECK(drew("Task Name") && drew("Work") && drew("Plan") && drew("Dale") && drew("Bob") && drew("Ship"));
    CHECK(drew("12h") && drew("4h"));
    CHECK(!drew("Resource Name"));
    CHECK_X(px[40][300], 0xF0F0F0);                                        /* Plan's row */
    CHECK_X(px[56][300], bg);                                              /* its Dale's */
    /* a summary's row holds its subtasks' work */
    CHECK_N(opj_insert_task(doc, 0, "Phase"), OWF_OK);
    CHECK_N(opj_indent(doc, 1, 3, 1), OWF_OK);
    CHECK_N(opj_task_usage_rows(doc), 7);
    opj_task_usage_text(doc, 0, -1, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out);
    CHECK_S(out, "20h");
    opj_task_usage_text(doc, 1, -1, AT(16, 0, 0), AT(17, 0, 0), out, sizeof out);
    CHECK_S(out, "12h");
    ntexts = 0;
    opj_task_usage_draw(d, &v, doc, NULL);
    CHECK(drew("Phase") && drew("20h"));
    /* read only, with its own reason */
    for (e = OPJ_EDIT_CELL; e < OPJ_NEDITS; ++e)
        CHECK(opj_edit_refused(OPJ_VIEW_TASK_USAGE, e) != NULL &&
              !strcmp(opj_edit_refused(OPJ_VIEW_TASK_USAGE, e), "Task Usage is read only: Gantt Chart (G) edits tasks"));
    CHECK(opj_edit_refused(OPJ_VIEW_TASK_USAGE, OPJ_EDIT_NONE) == NULL);
    oldraw_free(d);
    opj_doc_free(doc);
}

/* The Resource Graph, from test_usage's plan: Dale 200% on Monday and Tuesday (Plan and Build), 100% on
 * Wednesday (Build); Bob 50% on Monday and Tuesday. */
static void test_graph(void)
{
    opj_doc *doc = three();
    opj_view v;
    oldraw_rect in, ov;
    oldraw *d;
    unsigned long bg;
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_RESOURCES, "Dale,Bob[50%]"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_RESOURCES, "Dale"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, ""), OWF_OK);
    CHECK_N(opj_graph_percent(doc, 0, AT(16, 0, 0), AT(17, 0, 0)), 200);
    CHECK_N(opj_graph_percent(doc, 0, AT(18, 0, 0), AT(19, 0, 0)), 100);
    CHECK_N(opj_graph_percent(doc, 0, AT(19, 0, 0), AT(20, 0, 0)), 0);
    CHECK_N(opj_graph_percent(doc, 1, AT(16, 0, 0), AT(17, 0, 0)), 50);
    CHECK_N(opj_graph_percent(doc, 0, AT(21, 0, 0), AT(23, 0, 0)), -1);   /* the weekend: no working time */
    CHECK_N(opj_graph_percent(doc, 0, AT(17, 0, 0), AT(16, 0, 0)), -1);
    CHECK_N(opj_graph_percent(doc, 5, AT(16, 0, 0), AT(17, 0, 0)), -1);
    CHECK_N(opj_graph_percent(doc, 0, AT(16, 0, 0), AT(23, 0, 0)), 100);  /* the week: 40h of 40h */
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    CHECK_N(opj_graph_scale(&v, doc, 0), 200);
    CHECK_N(opj_graph_scale(&v, doc, 1), 100);
    /* Dale's Monday: 0-100% blue, 100-200% red, the plot from y 48 down to 184 */
    CHECK(opj_graph_bar(&v, doc, 0, AT(16, 10, 0), 200, &in, &ov));
    CHECK_N(in.x, 289); CHECK_N(in.w, 22);
    CHECK_N(in.y, 116); CHECK_N(in.h, 68);
    CHECK_N(ov.y, 48); CHECK_N(ov.h, 68);
    CHECK(opj_graph_bar(&v, doc, 0, AT(18, 0, 0), 200, &in, &ov));   /* Wednesday: at its max */
    CHECK_N(in.y, 116); CHECK_N(ov.h, 0);
    CHECK(!opj_graph_bar(&v, doc, 0, AT(19, 0, 0), 200, &in, &ov));  /* Thursday: nothing */
    CHECK(opj_graph_bar(&v, doc, 1, AT(16, 0, 0), 100, &in, &ov));   /* Bob: half */
    CHECK_N(in.y, 116); CHECK_N(in.h, 68); CHECK_N(ov.h, 0);
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    bg = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND)) & 0xFFFFFFUL;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_graph_draw(d, &v, doc, 0, NULL);
    CHECK_X(px[100][300], 0xD03030);                       /* Monday over */
    CHECK_X(px[150][300], 0x4A7BD0);                       /* and within */
    CHECK_X(px[100][340], bg);                             /* Wednesday: not over */
    CHECK_X(px[150][340], 0x4A7BD0);
    CHECK_X(px[116][500], 0x202020);                       /* the max units line */
    CHECK_X(px[184][500], 0x202020);                       /* 0% */
    CHECK_X(px[150][500], bg);
    CHECK(drew("Dale") && drew("Peak: 200%") && drew("Max Units: 100%") && drew("200%") && drew("0%"));
    CHECK(drew("16 Nov '26"));
    ntexts = 0;
    opj_graph_draw(d, &v, doc, 9, NULL);
    CHECK(drew("No resource: Resources... adds them"));
    CHECK(opj_edit_refused(OPJ_VIEW_GRAPH, OPJ_EDIT_INFO) != NULL);
    oldraw_free(d);
    opj_doc_free(doc);
}

/* The Tracking Gantt: Build baselined at 3 days, then made 5, half done. */
static void test_tracking(void)
{
    opj_doc *doc = three();
    opj_view v;
    opj_bar b;
    oldraw_rect r, base;
    oldraw *d;
    char text[64];
    CHECK_N(opj_set_baseline(doc, 0, -1), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_DURATION, "5d"), OWF_OK);
    CHECK_N(opj_set_percent(doc, 1, 50), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_RESOURCES, "Dale"), OWF_OK);
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    /* the plain Gantt chart: no baseline unless asked for, resource names beside the bars */
    CHECK(!opj_baseline_bar(&v, doc, 1, &r));
    CHECK(opj_bar_of(&v, doc, 1, &b));
    CHECK_N(b.bar.y, 52); CHECK_N(b.bar.h, 8);
    CHECK(!opj_label_box(&v, doc, 2, &r));                                /* Ship: nobody on it */
    opj_label_text(&v, doc, 0, text, sizeof text);
    CHECK_S(text, "Dale");
    /* tracking: the bar in the upper half, the baseline as high under it, the done part solid */
    v.tracking = 1;
    CHECK(opj_bar_of(&v, doc, 1, &b));
    CHECK_N(b.bar.y, 50); CHECK_N(b.bar.h, 5);
    CHECK_N(b.done.y, 50); CHECK_N(b.done.h, 5);
    CHECK_N(b.done.w, b.bar.w / 2);
    CHECK(opj_baseline_bar(&v, doc, 1, &base));
    CHECK_N(base.y, 56); CHECK_N(base.h, 5);
    CHECK_N(base.x, b.bar.x);                                             /* both from Wednesday */
    CHECK_N(base.w, 57);                                                  /* Wed 08:00 to Fri 17:00 */
    CHECK(b.bar.w > base.w);                                              /* now to Tuesday */
    CHECK(opj_label_box(&v, doc, 2, &r));                                 /* every bar has its % */
    opj_label_text(&v, doc, 1, text, sizeof text);
    CHECK_S(text, "50%");
    opj_label_text(&v, doc, 2, text, sizeof text);
    CHECK_S(text, "0%");
    opj_label_text(&v, doc, 7, text, sizeof text);
    CHECK_S(text, "");
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_draw(d, &v, doc, NULL);
    CHECK_X(px[52][b.bar.x + b.bar.w - 3], 0xD03030);                    /* Build's undone part, critical */
    CHECK_X(px[52][b.bar.x + 2], 0x1A1A1A);                               /* its done part, the bar's height */
    CHECK_X(px[50][b.bar.x + 2], 0x1A1A1A);
    CHECK_X(px[58][base.x + 2], 0x909090);                                /* the baseline under it */
    CHECK(drew("50%") && drew("0%") && !drew("Dale"));
    oldraw_free(d);
    opj_doc_free(doc);
}

static int has_bytes(const unsigned char *data, size_t n, const char *what)
{
    size_t k = strlen(what), i;
    for (i = 0; i + k <= n; i++)
        if (memcmp(data + i, what, k) == 0) return 1;
    return 0;
}

/* The printed Gantt view as a PDF (opj_pdf.c): a page each opj_print page, text searchable, the footer. */
/* Lisette's review of #240: the task edits act on the Gantt chart's selection, which Resource Usage
 * hides, so none runs while it shows, from the menu or a key; all run on the Gantt chart. */
static void test_usage_edits(void)
{
    int e;
    CHECK_N(opj_edit_for_key(13, 0), OPJ_EDIT_CELL);
    CHECK_N(opj_edit_for_key(127, 0), OPJ_EDIT_DELETE);
    CHECK_N(opj_edit_for_key(9, 0), OPJ_EDIT_INDENT);
    CHECK_N(opj_edit_for_key(9, 1), OPJ_EDIT_OUTDENT);
    CHECK_N(opj_edit_for_key('g', 0), OPJ_EDIT_NONE);
    CHECK_N(opj_edit_for_key(27, 1), OPJ_EDIT_NONE);
    for (e = OPJ_EDIT_CELL; e < OPJ_NEDITS; ++e) {
        CHECK(opj_edit_refused(0, e) == NULL);
        CHECK(opj_edit_refused(1, e) != NULL &&
              !strcmp(opj_edit_refused(1, e),
                      "Resource Usage is read only: Gantt Chart (G) edits tasks, Resources... edits resources"));
    }
    CHECK(opj_edit_refused(1, OPJ_EDIT_NONE) == NULL);                    /* other keys and menu items */
    CHECK(opj_edit_refused(1, OPJ_NEDITS) == NULL);
}

/* The column chooser: widths, showing and hiding in the sheet's order, the table keeping one column, a
 * selection moved off a hidden column, and the chart's edge following the table. */
static void test_columns(void)
{
    opj_view v;
    int c, left;
    opj_view_init(&v, NULL, 800, 400, 16, OPJ_ZOOM_DAYS);
    for (c = 0; c < OPJ_NCOLS; c++)
        CHECK_N(opj_view_column_shown(&v, c), c <= OPJ_COL_PREDECESSORS);
    CHECK_N(opj_column_width(OPJ_COL_RESOURCES), 140);
    CHECK_N(opj_column_width(OPJ_COL_PERCENT), 64);
    CHECK_N(opj_column_width(-1), 0);
    CHECK_N(opj_column_width(OPJ_NCOLS), 0);
    left = opj_chart_left(&v);
    CHECK(opj_view_show_column(&v, OPJ_COL_RESOURCES, 1));
    CHECK_N(v.columns[OPJ_COL_RESOURCES], 140);
    CHECK_N(opj_chart_left(&v), left + 140);
    v.columns[OPJ_COL_NAME] = 250;                                        /* a width of its own stays */
    CHECK(opj_view_show_column(&v, OPJ_COL_NAME, 1));
    CHECK_N(v.columns[OPJ_COL_NAME], 250);
    CHECK(opj_view_show_column(&v, OPJ_COL_DURATION, 0));
    CHECK_N(opj_view_column_shown(&v, OPJ_COL_DURATION), 0);
    CHECK(opj_view_show_column(&v, OPJ_COL_DURATION, 0));                 /* already hidden */
    CHECK_N(opj_view_near_column(&v, OPJ_COL_DURATION), OPJ_COL_START);
    CHECK_N(opj_view_near_column(&v, OPJ_COL_NAME), OPJ_COL_NAME);
    CHECK_N(opj_view_near_column(&v, OPJ_COL_WBS), OPJ_COL_RESOURCES);
    CHECK_N(opj_view_near_column(&v, OPJ_COL_FINISH_VARIANCE), OPJ_COL_RESOURCES);
    CHECK(!opj_view_show_column(&v, -1, 1) && !opj_view_show_column(&v, OPJ_NCOLS, 0));
    CHECK(!opj_view_show_column(NULL, OPJ_COL_NAME, 1));
    CHECK(opj_view_show_column(&v, OPJ_COL_WBS, 1));
    for (c = 0; c < OPJ_NCOLS; c++)
        if (c != OPJ_COL_WBS) CHECK(opj_view_show_column(&v, c, 0));
    CHECK(!opj_view_show_column(&v, OPJ_COL_WBS, 0));                     /* the table keeps one column */
    CHECK_N(v.columns[OPJ_COL_WBS], 48);
    CHECK_N(opj_view_near_column(&v, OPJ_COL_ID), OPJ_COL_WBS);
    v.columns[OPJ_COL_WBS] = 0;
    CHECK_N(opj_view_near_column(&v, OPJ_COL_ID), -1);
    CHECK_N(opj_view_near_column(NULL, 0), -1);
}

/* Task Information's fields: Constraint Type, Constraint Date and Notes as cells, and several cells typed
 * as one edit (one Undo step, nothing changed when one is refused). Build is Wed 18 to Fri 20 Nov. */
static void test_task_info(void)
{
    opj_doc *doc = three();
    static const int cols[3] = { OPJ_COL_CONSTRAINT_TYPE, OPJ_COL_CONSTRAINT_DATE, OPJ_COL_NOTES };
    static const char *const ok[3] = { "Start No Earlier Than", "23/11/26", "Two crews" };
    static const int cols2[2] = { OPJ_COL_DURATION, OPJ_COL_CONSTRAINT_TYPE };
    static const char *const bad2[2] = { "5d", "Sideways" };
    int bad = 9;
    long changes;
    CHECK_S(opj_column_title(OPJ_COL_CONSTRAINT_TYPE), "Constraint Type");
    CHECK_S(opj_column_title(OPJ_COL_NOTES), "Notes");
    CHECK_S(opj_constraint_name(OWF_CON_FNLT), "Finish No Later Than");
    CHECK_S(opj_constraint_name(99), "");
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "As Soon As Possible");
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_DATE), "NA");
    CHECK_S(cell_of(doc, 1, OPJ_COL_NOTES), "");
    /* OK: all three at once, one step, scheduled once */
    changes = doc->changes;
    CHECK_N(opj_set_cells(doc, 1, cols, ok, 3, &bad), OWF_OK);
    CHECK_N(bad, -1);
    CHECK_N(doc->changes, changes + 1);
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "Start No Earlier Than");
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_DATE), "Mon 23/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_NOTES), "Two crews");
    CHECK_S(cell_of(doc, 1, OPJ_COL_START), "Mon 23/11/26");
    CHECK_S(cell_of(doc, 2, OPJ_COL_FINISH), "Wed 25/11/26");
    CHECK_N(opj_undo(doc), OWF_OK);                                       /* one step takes it all back */
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "As Soon As Possible");
    CHECK_S(cell_of(doc, 1, OPJ_COL_NOTES), "");
    CHECK_S(cell_of(doc, 1, OPJ_COL_START), "Wed 18/11/26");
    CHECK_N(opj_redo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_NOTES), "Two crews");
    /* one refused: nothing changes, and bad says which */
    changes = doc->changes;
    CHECK_N(opj_set_cells(doc, 1, cols2, bad2, 2, &bad), OWF_ERR_FORMAT);
    CHECK_N(bad, 1);
    CHECK_N(doc->changes, changes);
    CHECK_S(cell_of(doc, 1, OPJ_COL_DURATION), "3d");
    CHECK_N(opj_set_cells(doc, 1, cols, ok, 0, &bad), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cells(doc, 1, NULL, ok, 1, NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cells(doc, 7, cols, ok, 1, &bad), OWF_ERR_FORMAT);
    {   /* As Soon As Possible chosen with a date edited too: the type stands, the date is left alone */
        static const char *const asap[3] = { "As Soon As Possible", "30/11/26", "Two crews" };
        CHECK_N(opj_set_cells(doc, 1, cols, asap, 3, &bad), OWF_OK);
        CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "As Soon As Possible");
        CHECK_S(cell_of(doc, 1, OPJ_COL_START), "Wed 18/11/26");
        CHECK_N(opj_undo(doc), OWF_OK);
        CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "Start No Earlier Than");
    }
    /* a type with a date and none yet: where the task is now; short names, any case */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, "mso"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_CONSTRAINT_TYPE), "Must Start On");
    CHECK_S(cell_of(doc, 0, OPJ_COL_CONSTRAINT_DATE), "Mon 16/11/26");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, "Must  finish on"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_CONSTRAINT_DATE), "Mon 16/11/26");   /* the date it had stays */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_DATE, "NA"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_CONSTRAINT_TYPE), "As Soon As Possible");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, "alap"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_CONSTRAINT_DATE), "NA");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_DATE, "17/11/26"), OWF_OK);   /* a date: SNET */
    CHECK_S(cell_of(doc, 0, OPJ_COL_CONSTRAINT_TYPE), "Start No Earlier Than");
    CHECK_S(cell_of(doc, 0, OPJ_COL_START), "Tue 17/11/26");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_DATE, "the 30th"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, ""), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_NOTES, ""), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_BASELINE_START, "16/11/26"), OWF_ERR_FORMAT);
    /* a summary takes only As Soon As Possible, Start No Earlier Than and Finish No Later Than */
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, ""), OWF_OK);     /* a link to its summary is a loop */
    CHECK_N(opj_indent(doc, 1, 1, 1), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, "Must Start On"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, "FNLT"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_CONSTRAINT_TYPE, "ASAP"), OWF_OK);
    opj_doc_free(doc);
}

/* Dragging on the chart, at Days zoom (24 pixels a day). Plan Mon 16-Tue 17, Build Wed 18-Fri 20, Ship
 * (a milestone) Fri 20 17:00. */
static int mid_y(const opj_view *v, int row)
{
    return opj_row_y(v, row) + v->row_height / 2;
}

static void test_drag(void)
{
    opj_doc *doc = three();
    opj_view v;
    opj_hit h;
    opj_drag d;
    opj_bar b;
    char text[80], want[40];
    int x, y;
    unsigned long changes;
    opj_view_init(&v, doc, 800, 400, 16, OPJ_ZOOM_DAYS);
    /* move: Build's middle two days on, to Fri 20 (Start No Earlier Than) */
    CHECK(opj_bar_of(&v, doc, 1, &b));
    x = b.bar.x + b.bar.w / 2;
    y = mid_y(&v, 1);
    opj_hit_test(&v, doc, x, y, &h);
    CHECK(opj_drag_begin(&v, doc, &h, x, y, &d));
    CHECK_N(d.kind, OPJ_DRAG_MOVE);
    CHECK_N(d.row, 1);
    CHECK(!opj_drag_text(&v, doc, &d, x + 5, y, text, sizeof text));     /* under half a day: nothing */
    CHECK_S(text, "");
    CHECK_N(opj_drag_end(doc, &v, &d, x + 5, y), OWF_ERR_FORMAT);
    CHECK(opj_drag_text(&v, doc, &d, x + 48, y, text, sizeof text));
    CHECK_S(text, "Start: Fri 20/11/26");
    CHECK(opj_drag_text(&v, doc, &d, x + 72, y, text, sizeof text));     /* Saturday: the next working day */
    CHECK_S(text, "Start: Mon 23/11/26");
    CHECK(!opj_drag_text(&v, doc, &d, x + 48, mid_y(&v, 0) - 20, text, sizeof text));   /* off its row */
    changes = doc->changes;
    CHECK_N(opj_drag_end(doc, &v, &d, x + 48, y), OWF_OK);
    CHECK_N(doc->changes, changes + 1);
    CHECK_S(cell_of(doc, 1, OPJ_COL_START), "Fri 20/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "Start No Earlier Than");
    CHECK_S(cell_of(doc, 2, OPJ_COL_FINISH), "Tue 24/11/26");
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_START), "Wed 18/11/26");
    CHECK_S(cell_of(doc, 1, OPJ_COL_CONSTRAINT_TYPE), "As Soon As Possible");
    /* stretch: Build's right end three days on, to Mon 23: 4 days */
    CHECK(opj_bar_of(&v, doc, 1, &b));
    x = b.bar.x + b.bar.w - 1;
    opj_hit_test(&v, doc, x, y, &h);
    CHECK_N(h.part, 1);
    CHECK(opj_drag_begin(&v, doc, &h, x, y, &d));
    CHECK_N(d.kind, OPJ_DRAG_STRETCH);
    CHECK(!opj_drag_text(&v, doc, &d, x + 24, y, text, sizeof text));    /* to Saturday: still 3 days */
    CHECK(opj_drag_text(&v, doc, &d, x + 72, y, text, sizeof text));
    CHECK_S(text, "Duration: 4d");
    CHECK_N(opj_drag_end(doc, &v, &d, x + 72, y), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_DURATION), "4d");
    CHECK_S(cell_of(doc, 1, OPJ_COL_FINISH), "Mon 23/11/26");
    CHECK(!opj_drag_text(&v, doc, &d, x - 200, y, text, sizeof text));   /* its finish before its start */
    CHECK_N(opj_drag_end(doc, &v, &d, x - 200, y), OWF_ERR_FORMAT);
    CHECK_S(cell_of(doc, 1, OPJ_COL_DURATION), "4d");
    /* % Complete: from the left end to the middle */
    CHECK(opj_bar_of(&v, doc, 1, &b));
    x = b.bar.x + 1;
    opj_hit_test(&v, doc, x, y, &h);
    CHECK(opj_drag_begin(&v, doc, &h, x, y, &d));
    CHECK_N(d.kind, OPJ_DRAG_PROGRESS);
    changes = doc->changes;                     /* Lisette's review of #346: a click is not a drag */
    CHECK(!opj_drag_text(&v, doc, &d, x, y, text, sizeof text));
    CHECK_N(opj_drag_end(doc, &v, &d, x, y), OWF_ERR_FORMAT);
    CHECK_N(opj_drag_end(doc, &v, &d, x + OPJ_DRAG_SLOP, y - OPJ_DRAG_SLOP), OWF_ERR_FORMAT);   /* a shake */
    CHECK_N(doc->changes, changes);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), "0%");
    CHECK(opj_drag_text(&v, doc, &d, b.bar.x + b.bar.w / 4, y, text, sizeof text));
    sprintf(want, "%% Complete: %d%%", (b.bar.w / 4 * 100 + b.bar.w / 2) / b.bar.w);   /* a quarter, rounded */
    CHECK_S(text, want);
    CHECK_N(opj_drag_end(doc, &v, &d, b.bar.x + b.bar.w / 4, y), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), want + 12);
    CHECK_N(opj_drag_end(doc, &v, &d, b.bar.x + b.bar.w + 40, y), OWF_OK);   /* past the end: 100 */
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), "100%");
    CHECK(opj_bar_of(&v, doc, 1, &b));          /* a click on the done part (all of it now) */
    x = b.bar.x + b.bar.w / 2;
    opj_hit_test(&v, doc, x, y, &h);
    CHECK_N(h.part, 2);
    CHECK(opj_drag_begin(&v, doc, &h, x, y, &d));
    CHECK_N(d.kind, OPJ_DRAG_PROGRESS);
    changes = doc->changes;
    CHECK_N(opj_drag_end(doc, &v, &d, x, y), OWF_ERR_FORMAT);
    CHECK_N(doc->changes, changes);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), "100%");
    /* a link: Plan's bar dropped on Ship's diamond */
    CHECK(opj_bar_of(&v, doc, 0, &b));
    x = b.bar.x + b.bar.w / 2;
    opj_hit_test(&v, doc, x, mid_y(&v, 0), &h);
    CHECK(opj_drag_begin(&v, doc, &h, x, mid_y(&v, 0), &d));
    CHECK(opj_bar_of(&v, doc, 2, &b));
    CHECK(opj_drag_text(&v, doc, &d, b.bar.x + b.bar.w / 2, mid_y(&v, 2), text, sizeof text));
    CHECK_S(text, "Link 1 to 3, finish to start");
    CHECK_N(opj_drag_end(doc, &v, &d, b.bar.x + b.bar.w / 2, mid_y(&v, 2)), OWF_OK);
    CHECK_S(cell_of(doc, 2, OPJ_COL_PREDECESSORS), "2,1");
    CHECK_N(opj_drag_end(doc, &v, &d, b.bar.x + b.bar.w / 2, mid_y(&v, 2)), OWF_ERR_FORMAT);   /* linked already */
    /* Ship's diamond on Plan's bar would be a loop: refused, the plan as it was */
    x = b.bar.x + b.bar.w / 2;
    opj_hit_test(&v, doc, x, mid_y(&v, 2), &h);
    CHECK(opj_drag_begin(&v, doc, &h, x, mid_y(&v, 2), &d));
    CHECK_N(d.kind, OPJ_DRAG_MOVE);
    CHECK(!opj_drag_text(&v, doc, &d, x + 48, mid_y(&v, 2), text, sizeof text));   /* a milestone isn't moved */
    CHECK(opj_bar_of(&v, doc, 0, &b));
    changes = doc->changes;
    CHECK_N(opj_drag_end(doc, &v, &d, b.bar.x + 4, mid_y(&v, 0)), OWF_ERR_FORMAT);
    CHECK_N(doc->changes, changes);
    CHECK_S(cell_of(doc, 0, OPJ_COL_PREDECESSORS), "");
    /* not on a bar; the core's own refusals */
    opj_hit_test(&v, doc, 790, mid_y(&v, 0), &h);
    CHECK(!opj_drag_begin(&v, doc, &h, 790, mid_y(&v, 0), &d));
    CHECK(!opj_drag_begin(NULL, doc, &h, 0, 0, &d));
    CHECK_N(opj_link_tasks(doc, 1, 1), OWF_ERR_FORMAT);
    CHECK_N(opj_link_tasks(doc, 0, 9), OWF_ERR_FORMAT);
    CHECK_N(opj_set_duration(doc, 1, -1), OWF_ERR_FORMAT);
    CHECK_N(opj_set_duration(doc, 1, 960), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_DURATION), "2d");
    opj_doc_free(doc);
}

static void test_pdf(void)
{
    opj_doc *doc = three(), *empty;
    opj_view proto;
    unsigned char *data = NULL;
    size_t n = 0;
    int pages = -1;
    doc->plan->title = owf_malloc(8);
    if (doc->plan->title) strcpy(doc->plan->title, "Kitchen");
    opj_view_init(&proto, doc, 0, 0, 16, OPJ_ZOOM_DAYS);
    /* A4 landscape less 1 cm round it, 12 twips a pixel: 1308 by 897. Rows 17 high, 49 a page; 31 days of chart on
     * the first page across (1308 - 548 = 760, 24 a day): the plan's 6 days fit on one page. */
    CHECK_N(opj_pdf_write(doc, &proto, &data, &n, &pages), OWF_OK);
    CHECK_N(pages, 1);
    CHECK(data && n > 5 && memcmp(data, "%PDF-", 5) == 0);
    CHECK(has_bytes(data, n, "(Kitchen) Tj") && has_bytes(data, n, "(Page 1 of 1) Tj"));
    CHECK(has_bytes(data, n, "(Task Name) Tj") && has_bytes(data, n, "(Plan) Tj") && has_bytes(data, n, "(Build) Tj"));
    CHECK(has_bytes(data, n, "/Title "));                                  /* the plan's title names the PDF */
    CHECK(has_bytes(data, n, "/Count 1"));
    owf_free(data);
    /* Build at 60 working days runs the plan past 31 days and 31 + 54: two more pages across (4 pixels of
     * split, then 54 days a page). */
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_DURATION, "60d"), OWF_OK);
    CHECK_N(opj_pdf_write(doc, &proto, &data, &n, &pages), OWF_OK);
    CHECK_N(pages, 3);
    CHECK(has_bytes(data, n, "(Page 3 of 3) Tj") && has_bytes(data, n, "/Count 3"));
    owf_free(data);
    /* Refused: a plan with no tasks, a missing argument; nothing is handed back. */
    empty = opj_doc_new(NULL, AT(16, 8, 0));
    CHECK(empty != NULL);
    data = (unsigned char *)"x";
    pages = 9;
    CHECK_N(opj_pdf_write(empty, &proto, &data, &n, &pages), OWF_ERR_FORMAT);
    CHECK(data == NULL && n == 0 && pages == 0);
    CHECK_N(opj_pdf_write(doc, NULL, &data, &n, NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_pdf_write(NULL, &proto, &data, &n, NULL), OWF_ERR_FORMAT);
    opj_doc_free(empty);
    opj_doc_free(doc);
}

/* The Network Diagram: Plan, then Build and Docs (both after Plan), then Ship (after Build). */
static void test_network(void)
{
    opj_doc *doc = three();
    opj_network net;
    opj_view v;
    opj_path path;
    oldraw_rect b;
    oldraw *d;
    char text[80];
    int w, h;
    unsigned long bg;
    CHECK_N(opj_insert_task(doc, 3, "Docs"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 3, OPJ_COL_DURATION, "1d"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 3, OPJ_COL_PREDECESSORS, "1"), OWF_OK);
    CHECK_N(opj_network_layout(doc, &net), OWF_OK);
    CHECK_N(net.ntasks, 4);
    CHECK_N(net.columns, 3);
    CHECK_N(net.lines, 2);
    CHECK_N(net.column[0], 0); CHECK_N(net.line[0], 0);
    CHECK_N(net.column[1], 1); CHECK_N(net.line[1], 0);
    CHECK_N(net.column[2], 2); CHECK_N(net.line[2], 0);
    CHECK_N(net.column[3], 1); CHECK_N(net.line[3], 1);
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    CHECK(opj_network_box(&v, &net, 3, &b));
    CHECK_N(b.x, 216); CHECK_N(b.y, 100); CHECK_N(b.w, 160); CHECK_N(b.h, 68);
    CHECK(!opj_network_box(&v, &net, 4, &b));
    opj_network_size(&v, &net, &w, &h);
    CHECK_N(w, 592);
    CHECK_N(h, 184);
    CHECK_N(opj_network_hit(&v, &net, 20, 20), 0);
    CHECK_N(opj_network_hit(&v, &net, 200, 20), -1);         /* between the columns */
    CHECK_N(opj_network_hit(&v, &net, 220, 110), 3);
    CHECK_N(opj_network_hit(&v, &net, -1, 20), -1);
    /* links: straight along a line, or down in the gap before the successor's column */
    CHECK(opj_network_link_path(&v, &net, doc, 1, 0, &path));
    CHECK_N(path.n, 2);
    CHECK_N(path.x[0], 176); CHECK_N(path.y[0], 50); CHECK_N(path.x[1], 215); CHECK_N(path.y[1], 50);
    CHECK(opj_network_link_path(&v, &net, doc, 3, 0, &path));
    CHECK_N(path.n, 4);
    CHECK_N(path.x[1], 195); CHECK_N(path.y[2], 134); CHECK_N(path.x[3], 215);
    CHECK(!opj_network_link_path(&v, &net, doc, 0, 0, &path));   /* Plan has no predecessor */
    CHECK(!opj_network_link_path(&v, &net, doc, 1, 1, &path));
    /* the boxes' text */
    opj_network_line_text(doc, 1, 0, text, sizeof text);
    CHECK_S(text, "Build");
    opj_network_line_text(doc, 1, 1, text, sizeof text);
    CHECK_S(text, "ID: 2  Dur: 3d");
    opj_network_line_text(doc, 1, 2, text, sizeof text);
    CHECK_S(text, "Start: Wed 18/11/26");
    opj_network_line_text(doc, 1, 3, text, sizeof text);
    CHECK_S(text, "Finish: Fri 20/11/26");
    opj_network_line_text(doc, 9, 0, text, sizeof text);
    CHECK_S(text, "");
    /* scrolling stays on the diagram; reveal shows a box whole */
    v.width = 300;
    v.height = 120;
    opj_network_reveal(&v, &net, 2);
    CHECK_N(v.scroll_x, 292);
    CHECK_N(v.scroll_y, 0);
    CHECK_N(opj_network_hit(&v, &net, 130, 20), 2);
    opj_network_reveal(&v, &net, 0);
    CHECK_N(v.scroll_x, 0);
    opj_network_scroll(&v, &net, 1000, 1000);
    CHECK_N(v.scroll_x, 292);
    CHECK_N(v.scroll_y, 64);
    opj_network_scroll(&v, &net, -5000, -5);
    CHECK_N(v.scroll_x, 0);
    CHECK_N(v.scroll_y, 59);
    /* drawing: Plan critical and finished (crossed), Docs not critical, Build selected */
    CHECK_N(opj_set_percent(doc, 0, 100), OWF_OK);
    opj_view_init(&v, doc, W, H, 16, OPJ_ZOOM_DAYS);
    v.selected = 1;
    d = cur = oldraw_new(&ops, NULL, W, H, NULL);
    CHECK(d != NULL);
    bg = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND)) & 0xFFFFFFUL;
    memset(px, 0, sizeof px);
    ntexts = 0;
    opj_network_draw(d, &v, &net, doc, NULL);
    CHECK_X(px[16][16], 0xD03030);                        /* Plan's edge: critical */
    CHECK_X(px[17][17], 0xD03030);                        /* two pixels thick */
    CHECK_X(px[18][18], 0xC8C8C8);                        /* finished: crossed, down */
    CHECK_X(px[81][18], 0xC8C8C8);                        /* and up */
    CHECK_X(px[100][216], 0x4A7BD0);                      /* Docs's edge: not critical */
    CHECK_X(px[40][300], oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_FILL)) & 0xFFFFFFUL);   /* Build: selected */
    CHECK_X(px[50][190], 0xD03030);                       /* Plan to Build: both critical */
    CHECK_X(px[90][195], 0x3050A0);                       /* Plan to Docs */
    CHECK_X(px[60][200], bg);                             /* beside Docs's line, where it turns down */
    CHECK_X(px[195][700], bg);
    CHECK(drew("Plan") && drew("Build") && drew("Docs") && drew("Ship"));
    CHECK(drew("ID: 2  Dur: 3d") && drew("Start: Wed 18/11/26"));
    oldraw_free(d);
    cur = NULL;
    opj_network_free(&net);
    CHECK(net.column == NULL && net.ntasks == 0);
    /* a summary has no box, and the boxes under it keep their order */
    CHECK_N(opj_insert_task(doc, 0, "Phase"), OWF_OK);
    CHECK_N(opj_indent(doc, 1, 4, 1), OWF_OK);
    CHECK_N(opj_network_layout(doc, &net), OWF_OK);
    CHECK_N(net.column[0], -1);
    CHECK(!opj_network_box(&v, &net, 0, &b));
    CHECK_N(net.column[1], 0);
    CHECK_N(net.column[4], 1);
    CHECK_N(net.line[4], 1);
    opj_network_free(&net);
    CHECK_N(opj_network_layout(NULL, &net), OWF_ERR_FORMAT);
    opj_doc_free(doc);
}

/* Change Working Time: hours as text, base calendars, a weekday's hours, holidays and a task's own calendar,
 * each one Undo step with the plan scheduled again. A 2-day task from Mon 16 Nov 2026 08:00. */
static void test_worktime(void)
{
    opj_doc *doc = opj_doc_new(NULL, AT(16, 8, 0));
    owf_pday h;
    char text[80];
    int cal = -9, night = -9;
    long thu = owf_ptime_make(2026, 11, 19, 0, 0) / 1440;
    CHECK(doc != NULL);
    if (!doc) return;
    /* Hours as typed and back. */
    CHECK(opj_hours_parse("08:00-12:00, 13:00-17:00", &h) && h.nspans == 2 && h.from[1] == 780 && h.to[1] == 1020);
    opj_hours_text(&h, text);
    CHECK_S(text, "08:00-12:00, 13:00-17:00");
    CHECK(opj_hours_parse("8-12 13:30-17", &h) && h.nspans == 2 && h.from[1] == 810);
    CHECK(opj_hours_parse("22:00-24:00", &h) && h.nspans == 1 && h.to[0] == 1440);
    CHECK(opj_hours_parse("OFF", &h) && h.nspans == 0);
    CHECK(opj_hours_parse("", &h) && h.nspans == 0);
    opj_hours_text(&h, text);
    CHECK_S(text, "off");
    CHECK(!opj_hours_parse("12-8", &h));                             /* backwards */
    CHECK(!opj_hours_parse("8-12, 11-13", &h));                      /* overlapping */
    CHECK(!opj_hours_parse("8-25", &h) && !opj_hours_parse("8:70-9", &h) && !opj_hours_parse("8", &h));
    CHECK(!opj_hours_parse("1-2 3-4 5-6 7-8 9-10 11-12", &h));       /* six spans */
    CHECK(!opj_hours_parse("8-12x", &h) && !opj_hours_parse(NULL, &h));

    CHECK_N(opj_insert_task(doc, 0, "Lay"), OWF_OK);
    CHECK_N(opj_set_duration(doc, 0, 2 * 480), OWF_OK);
    CHECK_N(doc->plan->tasks[0].early_finish, AT(17, 17, 0));
    /* Calendars by name (case ignored) or number. */
    CHECK_N(opj_find_calendar(doc->plan, "standard"), 0);
    CHECK_N(opj_find_calendar(doc->plan, "1"), 0);
    CHECK_N(opj_find_calendar(doc->plan, "2"), -1);
    CHECK_N(opj_find_calendar(doc->plan, "Night"), -1);
    /* Standard's Monday made a half day: Lay runs on to Wed noon; Undo puts it back. */
    CHECK_N(opj_set_week_hours(doc, 0, 1, "08:00-12:00"), OWF_OK);
    CHECK_N(doc->plan->tasks[0].early_finish, AT(18, 12, 0));
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_N(doc->plan->tasks[0].early_finish, AT(17, 17, 0));
    CHECK_N(opj_set_week_hours(doc, 0, 7, "8-12"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_week_hours(doc, 3, 1, "8-12"), OWF_ERR_FORMAT);  /* no calendar 3 */
    CHECK_N(opj_set_week_hours(doc, 0, 1, "nonsense"), OWF_ERR_FORMAT);
    /* Holidays Mon 16 and Tue 17: Lay moves to Wed and Thu. */
    CHECK_N(opj_set_day_hours(doc, 0, thu - 3, thu - 2, "off"), OWF_OK);
    CHECK_N(doc->plan->tasks[0].early_start, AT(18, 8, 0));
    CHECK_N(doc->plan->tasks[0].early_finish, AT(19, 17, 0));
    CHECK_N(doc->plan->calendars[0].nexceptions, 2);
    /* Tuesday back to the week's hours: Lay Tue and Wed. */
    CHECK_N(opj_set_day_hours(doc, 0, thu - 2, thu - 2, NULL), OWF_OK);
    CHECK_N(doc->plan->calendars[0].nexceptions, 1);
    CHECK_N(doc->plan->tasks[0].early_start, AT(17, 8, 0));
    CHECK_N(opj_set_day_hours(doc, 0, thu, thu - 1, "off"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_day_hours(doc, 0, thu, thu + 3660, "off"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_day_hours(doc, 0, thu, thu, "25-26"), OWF_ERR_FORMAT);
    /* A new calendar, a copy of Standard with its holiday; names must be new and not a number. */
    CHECK_N(opj_add_calendar(doc, "Night", 0, &night), OWF_OK);
    CHECK_N(night, 1);
    CHECK_N(doc->plan->calendars[night].nexceptions, 1);
    CHECK_N(doc->plan->calendars[night].week[1].nspans, 2);
    CHECK_N(opj_add_calendar(doc, "NIGHT", -1, &cal), OWF_ERR_FORMAT);
    CHECK_N(opj_add_calendar(doc, "  ", -1, &cal), OWF_ERR_FORMAT);
    CHECK_N(opj_add_calendar(doc, "24", -1, &cal), OWF_ERR_FORMAT);
    CHECK_N(opj_add_calendar(doc, "Odd", 5, &cal), OWF_ERR_FORMAT);
    CHECK_N(cal, -9);
    CHECK_N(opj_add_calendar(doc, "Plain", -1, &cal), OWF_OK);        /* Standard's hours, no holidays */
    CHECK_N(doc->plan->calendars[cal].nexceptions, 0);
    CHECK_N(opj_find_calendar(doc->plan, "night"), night);
    /* Night works 22:00-24:00 every day; Lay on its own Night calendar takes 4 days of 2 hours from Mon. */
    {
        int d;
        for (d = 0; d < 7; d++) CHECK_N(opj_set_week_hours(doc, night, d, "22:00-24:00"), OWF_OK);
    }
    CHECK_N(opj_set_day_hours(doc, night, thu - 3, thu - 3, NULL), OWF_OK);   /* Mon not a holiday there */
    CHECK_N(opj_set_task_calendar(doc, 0, night), OWF_OK);
    CHECK_N(doc->plan->tasks[0].calendar, night);
    CHECK_N(doc->plan->tasks[0].early_start, AT(16, 22, 0));
    CHECK_N(doc->plan->tasks[0].early_finish, owf_ptime_make(2026, 11, 24, 0, 0));   /* 8 nights of 2 hours */
    CHECK_N(opj_set_task_calendar(doc, 0, -1), OWF_OK);
    CHECK_N(doc->plan->tasks[0].calendar, -1);
    CHECK_N(opj_set_task_calendar(doc, 0, 9), OWF_ERR_FORMAT);
    CHECK_N(opj_set_task_calendar(doc, 4, 0), OWF_ERR_FORMAT);
    /* The plan's calendar: Night for every task without its own. */
    CHECK_N(opj_set_plan_calendar(doc, night), OWF_OK);
    CHECK_N(doc->plan->calendar, night);
    CHECK_N(doc->plan->tasks[0].early_start, AT(16, 22, 0));
    CHECK_N(opj_set_plan_calendar(doc, -1), OWF_ERR_FORMAT);
    {
        unsigned long before = doc->changes;
        CHECK_N(opj_set_plan_calendar(doc, 7), OWF_ERR_FORMAT);
        CHECK_N(doc->changes, before);                                /* a refused edit is no step */
    }
    opj_doc_free(doc);
    CHECK_N(opj_set_week_hours(NULL, 0, 1, "8-12"), OWF_ERR_FORMAT);
    CHECK_N(opj_find_calendar(NULL, "Standard"), -1);
}

/* Tracking in the sheet: Actual Start, Actual Finish, Actual and Remaining Duration, % Complete moving the
 * actual dates, and Update Project as one Undo step. Two 2-day tasks, A then B, from Mon 16 Nov 2026. */
static void test_actual_cells(void)
{
    opj_doc *doc = opj_doc_new(NULL, AT(16, 8, 0));
    int changed = -1;
    unsigned long before;
    CHECK(doc != NULL);
    if (!doc) return;
    CHECK_N(opj_insert_task(doc, 0, "A"), OWF_OK);
    CHECK_N(opj_insert_task(doc, 1, "B"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_DURATION, "2d"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_DURATION, "2d"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_PREDECESSORS, "1"), OWF_OK);
    CHECK_S(opj_column_title(OPJ_COL_ACTUAL_START), "Actual Start");
    CHECK_S(opj_column_title(OPJ_COL_REMAINING_DURATION), "Remaining Dur.");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_START), "NA");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_FINISH), "NA");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_DURATION), "0d");
    CHECK_S(cell_of(doc, 0, OPJ_COL_REMAINING_DURATION), "2d");
    /* Which columns are typed. */
    CHECK(opj_column_typed(OPJ_COL_ACTUAL_START) && opj_column_typed(OPJ_COL_NOTES) && opj_column_typed(OPJ_COL_CONSTRAINT_TYPE));
    CHECK(!opj_column_typed(OPJ_COL_ACTUAL_DURATION) && !opj_column_typed(OPJ_COL_ID) && !opj_column_typed(OPJ_COL_START_VARIANCE));
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_DURATION, "1d"), OWF_ERR_FORMAT);
    /* 50% gives A its scheduled start as its actual start; a day done, a day to go. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PERCENT, "50"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_START), "Mon 16/11/26");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_FINISH), "NA");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_DURATION), "1d");
    CHECK_S(cell_of(doc, 0, OPJ_COL_REMAINING_DURATION), "1d");
    /* 100% finishes it at its scheduled finish; 0% takes both away. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PERCENT, "100%"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_FINISH), "Tue 17/11/26");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PERCENT, "60"), OWF_OK);     /* not finished after all */
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_FINISH), "NA");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_START), "Mon 16/11/26");
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_PERCENT, "0"), OWF_OK);
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_START), "NA");
    /* A typed Actual Start a day late moves A and B; Actual Finish before it is refused. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_START, "17/11/26"), OWF_OK);
    CHECK_N(doc->plan->tasks[0].actual_start, AT(17, 8, 0));
    CHECK_N(doc->plan->tasks[1].early_start, AT(19, 8, 0));
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_FINISH, "16/11/26"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_FINISH, "someday"), OWF_ERR_FORMAT);
    /* Finished Wed at 17:00: 100%, 2 days, B from Thu. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_FINISH, "18/11/26"), OWF_OK);
    CHECK_N(doc->plan->tasks[0].actual_finish, AT(18, 17, 0));
    CHECK_S(cell_of(doc, 0, OPJ_COL_PERCENT), "100%");
    CHECK_S(cell_of(doc, 0, OPJ_COL_REMAINING_DURATION), "0d");
    /* An Actual Start later than the finish drops the finish. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_START, "19/11/26"), OWF_OK);
    CHECK(!doc->plan->tasks[0].has_actual_finish);
    CHECK_S(cell_of(doc, 0, OPJ_COL_PERCENT), "99%");                /* no longer finished */
    /* NA on the finish leaves it started, below 100%; NA on the start clears both and the percent. */
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_FINISH, "20/11/26"), OWF_OK);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_FINISH, "NA"), OWF_OK);
    CHECK(doc->plan->tasks[0].has_actual_start && doc->plan->tasks[0].percent == 99);
    CHECK_N(opj_set_cell(doc, 0, OPJ_COL_ACTUAL_START, "na"), OWF_OK);
    CHECK(!doc->plan->tasks[0].has_actual_start && doc->plan->tasks[0].percent == 0);
    /* An Actual Finish with no start: the scheduled start is taken. */
    CHECK_N(opj_set_cell(doc, 1, OPJ_COL_ACTUAL_FINISH, "18/11/26"), OWF_OK);
    CHECK_N(doc->plan->tasks[1].actual_start, AT(18, 8, 0));         /* B starts Wed, after A */
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK(!doc->plan->tasks[1].has_actual_start);
    /* Update Project through Tue 17 17:00: A done, B not started; one Undo step. */
    before = doc->changes;
    CHECK_N(opj_update_project(doc, AT(17, 17, 0), 0, &changed), OWF_OK);
    CHECK_N(changed, 1);
    CHECK_N(doc->changes, before + 1);
    CHECK_S(cell_of(doc, 0, OPJ_COL_PERCENT), "100%");
    CHECK_S(cell_of(doc, 0, OPJ_COL_ACTUAL_FINISH), "Tue 17/11/26");
    CHECK(doc->plan->has_status_date && doc->plan->status_date == AT(17, 17, 0));
    CHECK_N(opj_update_project(doc, AT(18, 12, 0), 0, &changed), OWF_OK);
    CHECK_N(changed, 1);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), "25%");
    CHECK_N(opj_undo(doc), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), "0%");
    CHECK_N(opj_update_project(doc, AT(18, 12, 0), 1, NULL), OWF_OK);
    CHECK_S(cell_of(doc, 1, OPJ_COL_PERCENT), "0%");                 /* 0% or 100% only */
    CHECK_S(cell_of(doc, 1, OPJ_COL_ACTUAL_START), "Wed 18/11/26");
    /* Earned value columns: NA for BCWS and SV until there is a status date; money otherwise. */
    {
        opj_doc *ev = opj_doc_new(NULL, AT(16, 8, 0));
        CHECK(ev != NULL);
        if (ev) {
            CHECK_N(opj_insert_task(ev, 0, "Dig"), OWF_OK);
            CHECK_N(opj_set_cell(ev, 0, OPJ_COL_DURATION, "2d"), OWF_OK);
            CHECK_N(opj_add_resource(ev, "Dale", NULL), OWF_OK);
            CHECK_N(opj_set_resource_cell(ev, 0, OPJ_RCOL_RATE, "10"), OWF_OK);
            CHECK_N(opj_set_cell(ev, 0, OPJ_COL_RESOURCES, "Dale"), OWF_OK);
            CHECK_N(opj_set_baseline(ev, 0, -1), OWF_OK);
            CHECK_S(opj_column_title(OPJ_COL_BCWS), "BCWS");
            CHECK_S(cell_of(ev, 0, OPJ_COL_BCWS), "NA");
            CHECK_S(cell_of(ev, 0, OPJ_COL_SV), "NA");
            CHECK_S(cell_of(ev, 0, OPJ_COL_BCWP), "0.00");
            CHECK(!opj_column_typed(OPJ_COL_CV));
            CHECK_N(opj_update_project(ev, AT(16, 17, 0), 0, NULL), OWF_OK);   /* a day of two done */
            CHECK_S(cell_of(ev, 0, OPJ_COL_BCWS), "80.00");
            CHECK_S(cell_of(ev, 0, OPJ_COL_BCWP), "80.00");
            CHECK_S(cell_of(ev, 0, OPJ_COL_ACWP), "80.00");
            CHECK_S(cell_of(ev, 0, OPJ_COL_SV), "0.00");
            CHECK_S(cell_of(ev, 0, OPJ_COL_CV), "0.00");
            opj_doc_free(ev);
        }
    }
    CHECK_N(opj_update_project(NULL, AT(18, 12, 0), 0, &changed), OWF_ERR_FORMAT);
    CHECK_N(changed, 0);
    opj_doc_free(doc);
}

/* Change Working Time's days line: weekdays, a date, a range; the hours shown for it; setting them. */
static void test_worktime_days(void)
{
    opj_doc *doc = opj_doc_new(NULL, AT(16, 8, 0));
    int wd = -1;
    long a = -1, b = -1, mon = AT(16, 0, 0) / 1440;
    char text[80];
    CHECK(doc != NULL);
    if (!doc) return;
    CHECK_N(opj_days_parse("Mon", &wd, &a, &b), 1);
    CHECK_N(wd, 1);
    CHECK_N(opj_days_parse(" saturday ", &wd, NULL, NULL), 1);
    CHECK_N(wd, 6);
    CHECK_N(opj_days_parse("WED", &wd, NULL, NULL), 1);
    CHECK_N(wd, 3);
    CHECK_N(opj_days_parse("Mo", &wd, NULL, NULL), 0);                /* too short */
    CHECK_N(opj_days_parse("Mond", &wd, NULL, NULL), 0);              /* neither three letters nor the name */
    CHECK_N(opj_days_parse("Fridays", &wd, NULL, NULL), 0);
    CHECK_N(opj_days_parse("24/12/26", &wd, &a, &b), 2);
    CHECK_N(a, mon + 38);
    CHECK_N(b, a);
    CHECK_N(opj_days_parse("24/12/26 - 1/1/27", NULL, &a, &b), 2);
    CHECK_N(b - a, 8);
    CHECK_N(opj_days_parse("16/11/26 to 18/11/26", NULL, &a, &b), 2);
    CHECK_N(a, mon);
    CHECK_N(b, mon + 2);
    CHECK_N(opj_days_parse("2026-11-16 TO 2026-11-17", NULL, &a, &b), 2);   /* ISO dates keep their dashes */
    CHECK_N(b, mon + 1);
    CHECK_N(opj_days_parse("18/11/26 - 16/11/26", NULL, &a, &b), 0);  /* backwards */
    CHECK_N(opj_days_parse("16/11/26 13:00", NULL, &a, &b), 0);       /* a time is not a day */
    CHECK_N(opj_days_parse("1/1/26 - 1/1/40", NULL, &a, &b), 0);      /* over 3660 days */
    CHECK_N(opj_days_parse("", NULL, NULL, NULL), 0);
    CHECK_N(opj_days_parse(NULL, NULL, NULL, NULL), 0);
    CHECK_N(opj_days_parse("someday", NULL, NULL, NULL), 0);
    /* The hours they work now. */
    opj_days_hours_text(doc->plan, 0, "Mon", text);
    CHECK_S(text, "08:00-12:00, 13:00-17:00");
    opj_days_hours_text(doc->plan, 0, "Sun", text);
    CHECK_S(text, "off");
    opj_days_hours_text(doc->plan, 0, "21/11/26", text);              /* a Saturday */
    CHECK_S(text, "off");
    opj_days_hours_text(doc->plan, 3, "Mon", text);
    CHECK_S(text, "");
    opj_days_hours_text(doc->plan, 0, "nonsense", text);
    CHECK_S(text, "");
    /* Setting them: a weekday, a range of holidays, back to default; default is for dates only. */
    CHECK_N(opj_set_days_hours(doc, 0, "Fri", "08:00-12:00"), OWF_OK);
    opj_days_hours_text(doc->plan, 0, "Friday", text);
    CHECK_S(text, "08:00-12:00");
    CHECK_N(opj_set_days_hours(doc, 0, "24/12/26 - 1/1/27", "off"), OWF_OK);
    CHECK_N(doc->plan->calendars[0].nexceptions, 9);
    opj_days_hours_text(doc->plan, 0, "28/12/26", text);
    CHECK_S(text, "off");
    CHECK_N(opj_set_days_hours(doc, 0, "28/12/26", " Default "), OWF_OK);
    CHECK_N(doc->plan->calendars[0].nexceptions, 8);
    opj_days_hours_text(doc->plan, 0, "28/12/26", text);
    CHECK_S(text, "08:00-12:00, 13:00-17:00");
    CHECK_N(opj_set_days_hours(doc, 0, "Mon", "default"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_days_hours(doc, 0, "someday", "off"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_days_hours(doc, 0, "Mon", "12-8"), OWF_ERR_FORMAT);
    CHECK_N(opj_set_days_hours(doc, 0, "Mon", NULL), OWF_ERR_FORMAT);
    CHECK_N(opj_set_days_hours(doc, 5, "Mon", "off"), OWF_ERR_FORMAT);
    CHECK_N(opj_undo(doc), OWF_OK);                                   /* the default undone */
    CHECK_N(doc->plan->calendars[0].nexceptions, 9);
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
    test_baseline();
    test_baseline_n();
    test_assign_dialog();
    test_print();
    test_pdf();
    test_usage();
    test_usage_edits();
    test_task_usage();
    test_graph();
    test_tracking();
    test_columns();
    test_task_info();
    test_network();
    test_drag();
    test_worktime();
    test_actual_cells();
    test_worktime_days();
    if (fails) {
        printf("project view tests: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("project view tests passed (%d checks)\n", checks);
    return 0;
}
