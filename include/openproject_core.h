/*
 * GFOpenProject's editing core and views (src/project): a plan being edited, with one Undo step per edit
 * and the plan scheduled again after each; the task sheet's cells as text and back; and the Gantt view
 * (the task table on the left, bars on a timescale on the right) drawn through GFOpenLayout's drawing
 * calls (gfopenlayout_draw.h), with hit tests for the pointer. Plain C99, no Amiga headers, integer
 * arithmetic only. Design: amicradle-teams designs/GFOPENPROJECT-20261113.md, step 2.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPROJECT_CORE_H
#define OPENPROJECT_CORE_H

#include "owf_project.h"
#include "gfopenlayout_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The plan being edited ---- */

/* Saved plans kept for Undo and Redo, at most this many bytes together; the oldest go first. */
#define OPJ_UNDO_BUDGET (512L * 1024L)

typedef struct opj_state opj_state;

typedef struct {
    owf_project *plan;                   /* scheduled after every edit */
    int schedule_error;                  /* the last owf_project_schedule's: OWF_ERR_FORMAT for a loop */
    unsigned long changes;               /* counts every edit, undo and redo */
    int dirty;                           /* changed since opj_doc_saved */
    opj_state *undo, *redo;              /* newest first */
    long undo_bytes;
    int undo_lost;                       /* the oldest steps were dropped for the budget */
} opj_doc;

/* A document for plan, which it takes and frees; plan NULL: a new empty plan starting at start. NULL for
 * memory (plan is freed). */
opj_doc *opj_doc_new(owf_project *plan, owf_ptime start);
void opj_doc_free(opj_doc *doc);
void opj_doc_saved(opj_doc *doc);
int opj_can_undo(const opj_doc *doc);
int opj_can_redo(const opj_doc *doc);
/* OWF_OK, or OWF_ERR_FORMAT when there is nothing to undo (redo), or OWF_ERR_MEMORY. */
int opj_undo(opj_doc *doc);
int opj_redo(opj_doc *doc);

/* ---- Edits: each is one Undo step and schedules the plan again ----
 * Rows are task indexes. OWF_OK; OWF_ERR_FORMAT when the edit can't be made (out of range, text that
 * isn't a duration, a link that would make a loop: the plan is left as it was); OWF_ERR_MEMORY. */

/* A new task before row at (at == ntasks: at the end), at row at's level (at the end: the last row's), one
 * day long and estimated, as MS Project's new tasks are. */
int opj_insert_task(opj_doc *doc, int at, const char *name);
/* Rows first..first+count-1, with every subtask of a summary among them; links to them go too. */
int opj_delete_tasks(opj_doc *doc, int first, int count);
/* Indents (by 1) or outdents (by -1) the rows and their subtasks. A row can't go deeper than one level
 * below the task above it, nor above level 1. */
int opj_indent(opj_doc *doc, int first, int count, int by);
/* Links each row to the next, finish to start (MS Project's Link Tasks); count >= 2. */
int opj_link_rows(opj_doc *doc, int first, int count);
/* Removes every link between the rows (Unlink Tasks), or each row's links when count is 1. */
int opj_unlink_rows(opj_doc *doc, int first, int count);
/* Percent complete, 0..100. */
int opj_set_percent(opj_doc *doc, int row, int percent);
/* A constraint (OWF_CON_) and its date (ignored for ASAP and ALAP). */
int opj_set_constraint(opj_doc *doc, int row, int constraint, owf_ptime date);
int opj_set_notes(opj_doc *doc, int row, const char *notes);
/* Set Baseline (each row's scheduled dates, duration, work and cost) or Clear Baseline for rows
 * first..first+count-1, count -1: the whole plan. */
int opj_set_baseline(opj_doc *doc, int first, int count);
int opj_clear_baseline(opj_doc *doc, int first, int count);

/* ---- The task sheet ---- */

enum {
    OPJ_COL_ID,                          /* the row number */
    OPJ_COL_NAME,
    OPJ_COL_DURATION,
    OPJ_COL_START,
    OPJ_COL_FINISH,
    OPJ_COL_PREDECESSORS,
    OPJ_COL_PERCENT,
    OPJ_COL_WBS,                         /* the outline number, 1.2.3 */
    OPJ_COL_RESOURCES,                   /* Resource Names: "Dale,Bob[50%]" */
    OPJ_COL_BASELINE_START,              /* "NA" with no baseline, as MS Project shows it */
    OPJ_COL_BASELINE_FINISH,
    OPJ_COL_START_VARIANCE,              /* working time later than the baseline: "2d", "-4h", "0d" */
    OPJ_COL_FINISH_VARIANCE,
    OPJ_NCOLS
};

/* A column's heading. */
const char *opj_column_title(int column);
/* A cell as the sheet shows it, into out (cut to size, NUL-terminated):
 * a duration "5d", "3ed", "1d?"; a date "Mon 16/11/26" (day/month/year); predecessors "2,3SS+1d". */
void opj_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size);
/* Typed text into a cell, as one edit. Name; Duration (owf_pduration_parse's text); Start (sets Start No
 * Earlier Than, as MS Project does), Finish (Finish No Earlier Than); Predecessors (replaces the row's
 * links); % Complete; Resource Names (replaces the row's assignments; a name not in the plan is added as
 * a work resource). ID and WBS can't be typed into (OWF_ERR_FORMAT). */
int opj_set_cell(opj_doc *doc, int row, int column, const char *text);
/* The outline number of a row, "1.2.3". */
void opj_outline_number(const opj_doc *doc, int row, char *out, size_t size);
/* "Mon 16/11/26" (with_time: "Mon 16/11/26 08:00"). out holds at least 24 bytes. */
void opj_date_text(owf_ptime t, int with_time, char *out);
/* "16/11/26", "16/11/2026", "Mon 16/11/26 13:00", "2026-11-16", "2026-11-16 13:00". A date with no time
 * gets default_minute (the start or end of a working day). 1 when it is one. */
int opj_date_parse(const char *s, int default_minute, owf_ptime *t);

/* ---- The resource sheet ---- */

enum {
    OPJ_RCOL_ID,                         /* the row number; "!" after it when over-allocated */
    OPJ_RCOL_NAME,
    OPJ_RCOL_TYPE,                       /* Work, Material, Cost */
    OPJ_RCOL_INITIALS,
    OPJ_RCOL_GROUP,
    OPJ_RCOL_MAX,                        /* Max. Units: "100%" (work only) */
    OPJ_RCOL_RATE,                       /* Std. Rate: "20.00/h" for work, "5.00" a unit for material */
    OPJ_RCOL_WORK,                       /* worked out: "40h" */
    OPJ_RCOL_COST,                       /* worked out: "800.00" */
    OPJ_NRCOLS
};

const char *opj_resource_column_title(int column);
void opj_resource_cell_text(const opj_doc *doc, int resource, int column, char *out, size_t size);
/* Typed text into a resource's cell, as one edit. Name (not empty, not another resource's, no ",;[]");
 * Type (Work, Material, Cost, or their first letter); Initials; Group; Max. Units ("50%" or "50", work
 * only); Std. Rate ("20", "20.00/h", "160/d" by the project's day, a currency sign first skipped; a
 * material's is a unit's, no "/"). ID, Work and Cost can't be typed into (OWF_ERR_FORMAT). */
int opj_set_resource_cell(opj_doc *doc, int resource, int column, const char *text);
/* A new work resource at the end, its index in *index (may be NULL). The name must not be empty or
 * another resource's. */
int opj_add_resource(opj_doc *doc, const char *name, int *index);
/* Resources first..first+count-1 and their assignments. */
int opj_delete_resources(opj_doc *doc, int first, int count);
/* Units of resource on task: 0 removes the assignment. Units in percent (hundredths for a cost). */
int opj_set_assignment(opj_doc *doc, int task, int resource, long units);

/* "12h", "12.5h", "0h": minutes of work as hours, to two places when not whole. out >= 16 bytes. */
void opj_work_text(long minutes, char *out);
/* "1234.50": hundredths of money. out >= 16 bytes. */
void opj_money_text(long hundredths, char *out);

/* ---- Resource usage: each resource's work in each period ---- */

/* The start of the period holding t, at a zoom (a day, a Monday, the 1st of a month or quarter); the next
 * period's start. */
owf_ptime opj_period_start(int zoom, owf_ptime t);
owf_ptime opj_period_next(int zoom, owf_ptime start);
/* Work of a resource (assign -1) or of one of its assignments in [from, to), as opj_work_text puts it,
 * "" when none. Over-allocated: *over set when the resource has more work in that span than its max
 * units allow there (may be NULL). */
void opj_usage_text(const opj_doc *doc, int resource, int assign, owf_ptime from, owf_ptime to, char *out,
                    size_t size, int *over);

/* ---- The Gantt view ---- */

enum { OPJ_ZOOM_DAYS, OPJ_ZOOM_WEEKS, OPJ_ZOOM_MONTHS, OPJ_ZOOM_QUARTERS, OPJ_NZOOMS };

typedef struct {
    int width, height;                   /* the view's size in pixels */
    int row_height;                      /* a task row; the timescale has two tiers this high */
    int columns[OPJ_NCOLS];              /* each column's width; 0 hides it */
    int sheet_width;                     /* the table's width (the rest is the chart); 0: the columns' */
    int zoom;                            /* OPJ_ZOOM_ */
    owf_ptime origin;                    /* the time at the chart's left edge */
    int first_row;                       /* the top row shown */
    int selected;                        /* a selected row, -1 none */
    int show_baseline;                   /* a thin grey bar under each bar, at its baseline dates */
    oldraw_font font;
} opj_view;

/* A view of doc, width by height pixels, rows row_height high, at a zoom, starting at the Monday on or
 * before the plan's start. Shows ID, Name, Duration, Start, Finish and Predecessors. */
void opj_view_init(opj_view *v, const opj_doc *doc, int width, int height, int row_height, int zoom);
/* Pixels a day takes at a zoom: 24, 6, 2, 1. */
int opj_zoom_day_width(int zoom);
/* The chart's left edge (where origin is). */
int opj_chart_left(const opj_view *v);
/* A time's x in the view, and the time at an x. */
long opj_view_x(const opj_view *v, owf_ptime t);
owf_ptime opj_view_time(const opj_view *v, long x);
/* A row's top y (rows above first_row are above the view); rows shown. */
int opj_row_y(const opj_view *v, int row);
int opj_rows_shown(const opj_view *v);

/* What a task's bar looks like. */
enum { OPJ_BAR_TASK, OPJ_BAR_SUMMARY, OPJ_BAR_MILESTONE };
typedef struct {
    int kind;                            /* OPJ_BAR_ */
    int critical;
    oldraw_rect bar;                     /* the whole bar; a milestone's diamond's box */
    oldraw_rect done;                    /* the part done (percent complete), w 0 when none */
} opj_bar;
/* Row's bar in the view; 0 when the row has none (out of range). */
int opj_bar_of(const opj_view *v, const opj_doc *doc, int row, opj_bar *bar);

/* Row's baseline bar (under its bar, a sixth of a row high); 0 when the row has none or the view doesn't
 * show baselines. */
int opj_baseline_bar(const opj_view *v, const opj_doc *doc, int row, oldraw_rect *bar);
#define OPJ_COLOUR_BASELINE OLDRAW_RGB(0x909090)

/* Where the resource names go beside row's bar (to its right, MS Project's way); 0 when the row has no
 * bar or no assignments. */
int opj_label_box(const opj_view *v, const opj_doc *doc, int row, oldraw_rect *box);

/* A link's line: up to 6 points joined by straight lines, the last at the arrow's tip. */
typedef struct {
    int n;
    int x[6], y[6];
} opj_path;
/* The line from link (of row's links) to row. 0 when there is none. */
int opj_link_path(const opj_view *v, const opj_doc *doc, int row, int link, opj_path *path);

/* Colours used. */
#define OPJ_COLOUR_BAR OLDRAW_RGB(0x4A7BD0)
#define OPJ_COLOUR_CRITICAL OLDRAW_RGB(0xD03030)
#define OPJ_COLOUR_DONE OLDRAW_RGB(0x1A1A1A)
#define OPJ_COLOUR_SUMMARY OLDRAW_RGB(0x202020)
#define OPJ_COLOUR_LINK OLDRAW_RGB(0x3050A0)
#define OPJ_COLOUR_NONWORKING OLDRAW_RGB(0xE4E4E4)
#define OPJ_COLOUR_GRID OLDRAW_RGB(0xC8C8C8)

/* Draws what of the view crosses area (the view's own pixels) onto d. */
void opj_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area);

/* What is at a point. */
enum { OPJ_HIT_NONE, OPJ_HIT_COLUMN_TITLE, OPJ_HIT_TIMESCALE, OPJ_HIT_CELL, OPJ_HIT_SPLIT, OPJ_HIT_BAR, OPJ_HIT_CHART };
typedef struct {
    int what;                            /* OPJ_HIT_ */
    int row, column;                     /* CELL: both; COLUMN_TITLE: column; BAR, CHART: row (-1 below the rows) */
    owf_ptime time;                      /* TIMESCALE, BAR, CHART: the time at the point */
    int part;                            /* BAR: 0 its middle (move), 1 its right end (stretch), 2 its done part */
} opj_hit;
void opj_hit_test(const opj_view *v, const opj_doc *doc, int x, int y, opj_hit *hit);

#ifdef __cplusplus
}
#endif

#endif
