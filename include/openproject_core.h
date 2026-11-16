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
/* A finish-to-start link from task pred to task succ (a link dragged between bars); OWF_ERR_FORMAT when
 * they are the same, out of range, linked already, or the link would make a loop (the plan unchanged). */
int opj_link_tasks(opj_doc *doc, int pred, int succ);
/* A task's duration in working minutes (a bar stretched), no longer estimated; not a summary's. */
int opj_set_duration(opj_doc *doc, int row, long minutes);
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
/* The same for Baseline n, 0..10 (0 is the one above, MS Project's Baseline; 1..10 Baseline 1 to 10);
 * n out of range: OWF_ERR_FORMAT. */
int opj_set_baseline_n(opj_doc *doc, int n, int first, int count);
int opj_clear_baseline_n(opj_doc *doc, int n, int first, int count);
/* Which baselines some task has saved: bit n for Baseline n. */
unsigned opj_baselines_saved(const opj_doc *doc);
/* "3", "Baseline 3", "baseline3", "Baseline" or "" (Baseline 0) to n, 0..10; -1 when it is none of these. */
int opj_baseline_parse(const char *text);
/* The saved ones as "0 3 10" ("none" with none) into out. */
void opj_baselines_text(const opj_doc *doc, char *out, size_t size);
/* Tools > Tracking > Update Project: work complete through status (owf_project_update; whole: 0% or 100%
 * only), as one edit; how many tasks changed in *changed (when not NULL). */
int opj_update_project(opj_doc *doc, owf_ptime status, int whole, int *changed);

/* ---- Change Working Time: calendars (one Undo step each, the plan scheduled again) ----
 * cal is an index into plan->calendars. */

/* Working hours as typed: spans "08:00-12:00, 13:00-17:00" (commas or spaces between them, "8-12" whole
 * hours too), or "off" (or "") for no working time. 1 and *out set, or 0 when it isn't hours. */
int opj_hours_parse(const char *text, owf_pday *out);
/* The other way: "08:00-12:00, 13:00-17:00", or "off". out holds at least 80 bytes. */
void opj_hours_text(const owf_pday *h, char *out);
/* A calendar by its name (case ignored) or its number from 1; -1 when there is none. */
int opj_find_calendar(const owf_project *p, const char *name);
/* A new base calendar called name, a copy of calendar from (its week and its exceptions; -1: Standard's
 * hours), its index in *index. OWF_ERR_FORMAT for an empty name or one a calendar has already. */
int opj_add_calendar(opj_doc *doc, const char *name, int from, int *index);
/* The hours of one weekday (0 Sunday) in every week of the calendar. */
int opj_set_week_hours(opj_doc *doc, int cal, int weekday, const char *hours);
/* The hours of the days first..last (days since 1 Jan 2000, at most 3660 of them): hours, "off" for
 * holidays, or NULL to give them back the week's hours. */
int opj_set_day_hours(opj_doc *doc, int cal, long first, long last, const char *hours);
/* The days a Change Working Time line names: a weekday ("Mon", "friday": 1, *weekday 0 Sunday..6), or a
 * date or two ("24/12/26", "24/12/26 - 1/1/27", "24/12/26 to 1/1/27": 2, *first and *last in days since
 * 1 Jan 2000, at most 3660 days). 0 when it is neither. */
int opj_days_parse(const char *text, int *weekday, long *first, long *last);
/* What a Change Working Time line's days work now: a weekday's hours, or the first day's (out at least
 * 80 bytes, "" when text names no days or cal is out of range). */
void opj_days_hours_text(const owf_project *p, int cal, const char *text, char *out);
/* Sets the hours of the days text names (as opj_days_parse) on calendar cal: hours as opj_hours_parse,
 * or "default" for dates to have the week's hours again. One Undo step; OWF_ERR_FORMAT for days or hours
 * that aren't. */
int opj_set_days_hours(opj_doc *doc, int cal, const char *days, const char *hours);
/* The plan's calendar (Project > Project Information's), or a task's own (-1: the plan's). */
int opj_set_plan_calendar(opj_doc *doc, int cal);
int opj_set_task_calendar(opj_doc *doc, int row, int cal);

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
    OPJ_COL_CONSTRAINT_TYPE,             /* "Start No Earlier Than", as MS Project names them */
    OPJ_COL_CONSTRAINT_DATE,             /* "NA" for As Soon / Late As Possible */
    OPJ_COL_NOTES,
    OPJ_COL_ACTUAL_START,                /* "NA" until the task has started */
    OPJ_COL_ACTUAL_FINISH,
    OPJ_COL_ACTUAL_DURATION,             /* duration times % complete, worked out */
    OPJ_COL_REMAINING_DURATION,
    OPJ_COL_BCWS,                        /* earned value at the status date (NA without one), money */
    OPJ_COL_BCWP,
    OPJ_COL_ACWP,
    OPJ_COL_SV,                          /* NA without a status date */
    OPJ_COL_CV,
    OPJ_NCOLS
};

/* A column's heading. */
const char *opj_column_title(int column);
/* A constraint type's name, "As Soon As Possible" .. "Finish No Later Than" (OWF_CON_), "" out of range. */
const char *opj_constraint_name(int constraint);
/* A cell as the sheet shows it, into out (cut to size, NUL-terminated):
 * a duration "5d", "3ed", "1d?"; a date "Mon 16/11/26" (day/month/year); predecessors "2,3SS+1d". */
void opj_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size);
/* Typed text into a cell, as one edit. Name; Duration (owf_pduration_parse's text); Start (sets Start No
 * Earlier Than, as MS Project does), Finish (Finish No Earlier Than); Predecessors (replaces the row's
 * links); % Complete; Resource Names (replaces the row's assignments; a name not in the plan is added as
 * a work resource); Constraint Type (its name, any case, or a short one: ASAP, ALAP, MSO, MFO, SNET, SNLT, FNET,
 * FNLT; a type with a date and none yet takes the task's start, or finish for the finish types); Constraint
 * Date (NA makes it As Soon As Possible; a date on an ASAP or ALAP task makes it Start No Earlier Than);
 * Notes; Actual Start and Actual Finish (a date, or NA to clear it: an actual finish with no actual start
 * gives the task its scheduled start as one, and clearing the start clears the finish too). % Complete
 * above 0 gives a task with no actual start its scheduled start, 100 its scheduled finish as actual
 * finish, below 100 clears an actual finish and 0 both, as MS Project does. ID, WBS, the baseline,
 * variance, Actual Duration and Remaining Duration columns can't be typed into (OWF_ERR_FORMAT);
 * opj_column_typed says which can. */
int opj_set_cell(opj_doc *doc, int row, int column, const char *text);
/* 1 when a column can be typed into, 0 when it is worked out. */
int opj_column_typed(int column);
/* Task Information's OK: several of row's cells typed at once, in the order given, as one edit (one Undo
 * step), scheduled once at the end. Nothing changes when any cell is refused: its place in columns goes
 * in *bad (when not NULL; -1 when none was), with OWF_ERR_FORMAT, or OWF_ERR_MEMORY. A Constraint Date
 * after a Constraint Type of As Soon or As Late As Possible is left alone, so the type chosen stands. */
int opj_set_cells(opj_doc *doc, int row, const int *columns, const char *const *texts, int n, int *bad);
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

/* ---- Assign Resources (MS Project's dialog): every resource against one task ---- */
typedef struct {
    int assigned;                        /* 1 when the task has the resource */
    long units;                          /* the assignment's units, as opj_set_assignment takes them; 0 when none */
    long work, cost;                     /* its work (minutes) and cost (hundredths), as scheduled */
} opj_assign_info;
/* The resource against the task into *out; 0 (and *out zeroed) when either is out of range. */
int opj_assign_get(const opj_doc *doc, int task, int resource, opj_assign_info *out);
/* Units as the dialog types them for the resource: a work resource "50%" or "50" (whole percent, 1 to
 * 100000), a material "2.5" (units, to two places), a cost "120.00" (a currency sign before it is fine).
 * OWF_OK with *units as opj_set_assignment takes them; OWF_ERR_FORMAT for 0, nothing, or text it doesn't take. */
int opj_units_parse(const opj_doc *doc, int resource, const char *text, long *units);
/* The other way: "50%", "2.5", "120.00"; "" for a resource out of range. out holds at least 24 bytes. */
void opj_units_text(const opj_doc *doc, int resource, long units, char *out, size_t size);
/* The dialog's line for the resource: "*" when assigned (else " "), the name, then for an assignment its
 * units and cost, in columns. */
void opj_assign_line(const opj_doc *doc, int task, int resource, char *out, size_t size);
/* Assign: the resource on the task at units (NULL or "": the assignment's own units, else 100%, or one
 * unit of a material; a cost resource needs its amount), as one edit. */
int opj_assign_resource(opj_doc *doc, int task, int resource, const char *units);
/* The resource named (any case, blanks around it skipped), or a row number from 1; -1 when there is none. */
int opj_find_resource(const opj_doc *doc, const char *name);
/* Remove: the resource's assignment on the task, as one edit; OWF_ERR_FORMAT when there is none. */
int opj_remove_resource(opj_doc *doc, int task, int resource);
/* Replace: the task's assignment of from goes to to, keeping its place and, with units NULL or "", its
 * units (one unit of to's kind when the two kinds differ), as one edit. OWF_ERR_FORMAT when from isn't
 * assigned, to already is, or they are the same. */
int opj_replace_resource(opj_doc *doc, int task, int from, int to, const char *units);

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
    int tracking;                        /* the Tracking Gantt: each task's bar in its row's upper half, its
                                          * baseline as high in the lower half, the done part solid, and
                                          * the % Complete beside each bar in place of the resource names */
    int scroll_x, scroll_y;              /* the Network Diagram's scroll, in pixels */
    oldraw_font font;
} opj_view;

/* A view of doc, width by height pixels, rows row_height high, at a zoom, starting at the Monday on or
 * before the plan's start. Shows ID, Name, Duration, Start, Finish and Predecessors. */
void opj_view_init(opj_view *v, const opj_doc *doc, int width, int height, int row_height, int zoom);
/* The column chooser (View > Columns, ARexx COLUMN). A column's width when shown: 32 ID, 180 Task Name, 56
 * Duration, 96 Start and Finish, 84 Predecessors, 64 % Complete, 48 WBS, 140 Resource Names, 96 the baseline
 * dates, 72 the variances, 140 Constraint Type, 96 Constraint Date, 160 Notes; 0 out of range. */
int opj_column_width(int column);
/* 1 when v's table shows column. */
int opj_view_column_shown(const opj_view *v, int column);
/* Shows column at its width (a shown column keeps its own) or hides it; the columns stay in the sheet's
 * order. 1 done (nothing to do counts), 0 out of range or when it would hide the last column shown. */
int opj_view_show_column(opj_view *v, int column, int show);
/* column when shown, else the next shown column after it, else the last one before it; -1 when none is. */
int opj_view_near_column(const opj_view *v, int column);
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

/* Row's baseline bar (under its bar, a sixth of a row high; the Tracking Gantt's lower half); 0 when the
 * row has none or the view shows no baselines (the Tracking Gantt always does). */
int opj_baseline_bar(const opj_view *v, const opj_doc *doc, int row, oldraw_rect *bar);
#define OPJ_COLOUR_BASELINE OLDRAW_RGB(0x909090)

/* Where the resource names go beside row's bar (to its right, MS Project's way); 0 when the row has no
 * bar or no assignments. The Tracking Gantt puts every bar's % Complete there. */
int opj_label_box(const opj_view *v, const opj_doc *doc, int row, oldraw_rect *box);
/* What goes there: the resource names ("Dale,Bob[50%]"), or "40%" in the Tracking Gantt. */
void opj_label_text(const opj_view *v, const opj_doc *doc, int row, char *out, size_t size);

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

/* ---- The Resource Usage view (MS Project's): each resource's row, its assignments' rows under it, and
 * the work of each in every period of the timescale's lower tier (a day, week, month or quarter, by the
 * zoom); a period over the resource's max units is in red, and its ID has "!" (the resource sheet's). It
 * uses a view's size, row height, zoom, origin, first row, selected row and font; its table is its own:
 * ID, Resource Name and Work. ---- */
#define OPJ_USAGE_ID_W 32
#define OPJ_USAGE_NAME_W 180
#define OPJ_USAGE_WORK_W 72
enum { OPJ_UCOL_ID, OPJ_UCOL_NAME, OPJ_UCOL_WORK, OPJ_NUCOLS };
/* The rows: every resource, each followed by its assignments in the order they were made. */
int opj_usage_rows(const opj_doc *doc);
/* Row's resource, and its assignment (-1: the resource's own row). 0 when there is no such row. */
int opj_usage_row(const opj_doc *doc, int row, int *resource, int *assign);
const char *opj_usage_column_title(int column);
/* A table cell: the resource's ID, name and work (the resource sheet's), or for an assignment "", its
 * task's name and its work. "" out of range. */
void opj_usage_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size);
/* v as the usage view draws it: its table's width in place of the Gantt view's columns, so opj_view_x,
 * opj_view_time, opj_chart_left and opj_row_y give the usage view's places. */
void opj_usage_view(const opj_view *v, opj_view *usage);
void opj_usage_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area);

/* ---- The Task Usage view (MS Project's): Resource Usage turned round. Each task's row (indented by its
 * outline level, a summary's name bold), its assignments' rows under it, and the work of each in every
 * period; a task's row holds its own and its subtasks' work and is shaded. Its table: ID, Task Name and
 * Work, as wide as Resource Usage's; it uses the same view fields (opj_usage_view gives its places).
 * Read only. ---- */
int opj_task_usage_rows(const opj_doc *doc);
/* Row's task, and its assignment (-1: the task's own row). 0 when there is no such row. */
int opj_task_usage_row(const opj_doc *doc, int row, int *task, int *assign);
const char *opj_task_usage_column_title(int column);
/* A table cell: the task's ID, name and work, or for an assignment "", its resource's name and its work. */
void opj_task_usage_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size);
/* Work of a task (assign -1: its own assignments' and its subtasks') or of one of its assignments in
 * [from, to), as opj_work_text puts it, "" when none. */
void opj_task_usage_text(const opj_doc *doc, int task, int assign, owf_ptime from, owf_ptime to, char *out,
                         size_t size);
void opj_task_usage_draw(oldraw *d, const opj_view *v, const opj_doc *doc, const oldraw_rect *area);

/* ---- The Resource Graph (MS Project's): one work resource's allocation in each period of the timescale's
 * lower tier, as a bar of percent (its work there over its calendar's working time there): blue up to
 * its max units, red above. A line marks its max units; the scale runs from 0 to opj_graph_scale. Its
 * table's place (Resource Usage's width) holds the resource's name, its peak in the periods shown and its
 * max units, with the scale's ends beside the chart. The plot runs from three rows down to a row above
 * the bottom. It uses the same view fields as Resource Usage. Read only. ---- */
/* A resource's percent allocated in [from, to); -1 for a material or cost resource, or no working time. */
long opj_graph_percent(const opj_doc *doc, int resource, owf_ptime from, owf_ptime to);
/* The scale's top: the larger of its max units and its peak in the periods v shows, at least 100,
 * rounded up to 50. */
long opj_graph_scale(const opj_view *v, const opj_doc *doc, int resource);
/* The bar of the period holding period: within (up to the max units) and over (above it; h 0 when not
 * over). 0 when the period has no allocation. */
int opj_graph_bar(const opj_view *v, const opj_doc *doc, int resource, owf_ptime period, long scale,
                  oldraw_rect *within, oldraw_rect *over);
void opj_graph_draw(oldraw *d, const opj_view *v, const opj_doc *doc, int resource, const oldraw_rect *area);
/* ---- The Network Diagram view (MS Project's, src/project/opj_network.c): a box for every task that
 * isn't a summary, holding its name, ID and duration, start and finish, four rows of the view's row height
 * high. A box stands in the column one past its furthest predecessor's (the first column: no
 * predecessors), and down its column in the sheet's order. A link is a line with an arrow from the
 * predecessor's box's right side to the successor's left side; red when both tasks are critical. A
 * critical task's box is edged in red; a milestone's edge is doubled; a task under way is struck through
 * once and a finished one crossed. It uses a view's size, row height, selected row, font and scroll. ---- */
#define OPJ_NET_BOX_W 160
#define OPJ_NET_GAP_X 40
#define OPJ_NET_GAP_Y 16
#define OPJ_NET_MARGIN 16
typedef struct {
    int ntasks;                          /* the plan's tasks when laid out */
    int *column, *line;                  /* each task's box's column and place down it; -1 for a summary */
    int columns, lines;                  /* how many columns, and the most boxes in one */
} opj_network;
/* Lays doc's boxes out (lay it out again after any edit). OWF_OK, OWF_ERR_MEMORY, OWF_ERR_FORMAT. */
int opj_network_layout(const opj_doc *doc, opj_network *net);
void opj_network_free(opj_network *net);
/* Row's box in the view (scrolled); 0 for a summary or out of range. */
int opj_network_box(const opj_view *v, const opj_network *net, int row, oldraw_rect *box);
/* The whole diagram's size in pixels, margins included; 0 by 0 with no boxes. */
void opj_network_size(const opj_view *v, const opj_network *net, int *width, int *height);
/* The row whose box is at x, y; -1 none. */
int opj_network_hit(const opj_view *v, const opj_network *net, int x, int y);
/* Scrolls by dx, dy pixels, kept to the diagram; scrolls just enough to show row's box whole. */
void opj_network_scroll(opj_view *v, const opj_network *net, int dx, int dy);
void opj_network_reveal(opj_view *v, const opj_network *net, int row);
/* The line of link (of row's links) to row's box: 2 or 4 points, the last at the arrow's tip. 0 when
 * either end is a summary or out of range. */
int opj_network_link_path(const opj_view *v, const opj_network *net, const opj_doc *doc, int row, int link,
                          opj_path *path);
/* A box's line of text, 0 to 3: the name; "ID: 2  Dur: 3d"; "Start: Mon 16/11/26"; "Finish: ...". */
void opj_network_line_text(const opj_doc *doc, int row, int line, char *out, size_t size);
void opj_network_draw(oldraw *d, const opj_view *v, const opj_network *net, const opj_doc *doc,
                      const oldraw_rect *area);

/* The task edits, which act on the Gantt chart's selected task, from the Edit menu or a key (OPJ_EDIT_INFO:
 * Task Information). Resource
 * Usage hides that selection, so none of them runs while it shows. */
enum { OPJ_EDIT_NONE = -1, OPJ_EDIT_CELL, OPJ_EDIT_INSERT, OPJ_EDIT_DELETE, OPJ_EDIT_INDENT, OPJ_EDIT_OUTDENT,
       OPJ_EDIT_LINK, OPJ_EDIT_UNLINK, OPJ_EDIT_INFO, OPJ_NEDITS };
/* The edit a key in the window asks for: Return the cell, Del the task's deletion, Tab indent (shift:
 * outdent). OPJ_EDIT_NONE for any other key. */
int opj_edit_for_key(int key, int shift);
/* Why edit can't run with usage showing (OPJ_VIEW_: the usage views are read only), as the status line
 * says it; NULL when it may run (OPJ_EDIT_NONE always may). */
enum { OPJ_VIEW_GANTT, OPJ_VIEW_USAGE, OPJ_VIEW_TASK_USAGE, OPJ_VIEW_GRAPH };
const char *opj_edit_refused(int usage, int edit);

/* ---- Printing the Gantt view: pages down, then across, as MS Project prints ----
 * The first page across holds the table and the start of the chart; the pages after it hold more of the
 * chart (to a day past the plan's finish) with no table. Every page has the timescale at its top and a
 * footer row: the plan's title, and "Page n of m". */
typedef struct {
    opj_view proto;                      /* columns, row height, zoom, font and baselines to print with */
    int page_width, page_height;         /* the printable area, in the printer's pixels */
    int rows_per_page, down, across;
    long first_days, more_days;          /* days of chart on the first page across, and on each after it */
} opj_print;

/* Lays doc out on pages of page_width by page_height. The number of pages, or 0 when a page is too small
 * for a row of tasks or a day of chart. */
int opj_print_layout(opj_print *pr, const opj_view *proto, const opj_doc *doc, int page_width, int page_height);
/* The view a page (from 0) shows. 0 when there is no such page. */
int opj_print_page_view(const opj_print *pr, int page, opj_view *v);
/* Draws a page onto d (sized as the page). */
void opj_print_draw(oldraw *d, const opj_print *pr, const opj_doc *doc, int page);

/* ---- The printed Gantt view as a PDF (src/project/opj_pdf.c) ----
 * Each page opj_print_layout lays out, drawn through an oldraw target that writes into libowf's page PDF
 * writer (owf_pdf.h): A4 landscape, a 1 cm margin, OPJ_PDF_TWIPS twips a pixel, text in PDF's standard fonts,
 * so it stays searchable. proto gives the columns, zoom and baselines; the font is the paper's own, Helvetica
 * at OPJ_PDF_FONT_PX pixels in rows OPJ_PDF_ROW high, whatever the screen's. */
#define OPJ_PDF_PAGE_W 16838                  /* A4 landscape, twips */
#define OPJ_PDF_PAGE_H 11906
#define OPJ_PDF_MARGIN 567                    /* 1 cm */
#define OPJ_PDF_TWIPS 12                      /* a pixel: 0.6 pt */
#define OPJ_PDF_FONT_PX 12                    /* 7.2 pt */
#define OPJ_PDF_ROW 17
/* The PDF, from malloc (owf_free it), and its page count in *pages (may be NULL). OWF_OK; OWF_ERR_MEMORY;
 * OWF_ERR_FORMAT when the plan has no tasks or a page is too small for it. */
int opj_pdf_write(const opj_doc *doc, const opj_view *proto, unsigned char **data, size_t *length, int *pages);

/* What is at a point. */
enum { OPJ_HIT_NONE, OPJ_HIT_COLUMN_TITLE, OPJ_HIT_TIMESCALE, OPJ_HIT_CELL, OPJ_HIT_SPLIT, OPJ_HIT_BAR, OPJ_HIT_CHART };
typedef struct {
    int what;                            /* OPJ_HIT_ */
    int row, column;                     /* CELL: both; COLUMN_TITLE: column; BAR, CHART: row (-1 below the rows) */
    owf_ptime time;                      /* TIMESCALE, BAR, CHART: the time at the point */
    int part;                            /* BAR: 0 its middle (move), 1 its right end (stretch), 2 its done part */
} opj_hit;
void opj_hit_test(const opj_view *v, const opj_doc *doc, int x, int y, opj_hit *hit);

/* ---- Dragging on the Gantt chart (opj_drag.c), MS Project's mouse ----
 * A bar's middle moves the task (Start No Earlier Than the day it is dropped on, at that day's first
 * working minute), its right end stretches it (its duration the working time to the end of the day it is
 * dropped on), its done part or its left 3 pixels set % Complete, and a bar dropped on another task's bar
 * links the two, finish to start. Moves and stretches go by whole days at every zoom; summaries and
 * milestones are only linked. Each drop is one edit, one Undo step. */
enum { OPJ_DRAG_MOVE, OPJ_DRAG_STRETCH, OPJ_DRAG_PROGRESS, OPJ_DRAG_LINK };
typedef struct {
    int kind;                            /* OPJ_DRAG_MOVE, _STRETCH or _PROGRESS (a link is found on the drop) */
    int row;                             /* the bar's task */
    int x0, y0;                          /* where the button went down */
} opj_drag;
/* A drag from where the button went down (hit, x, y): 1 when it is on a bar that can be dragged. Letting
 * go within OPJ_DRAG_SLOP pixels of there is a click, not a drag, and does nothing. */
#define OPJ_DRAG_SLOP 3
int opj_drag_begin(const opj_view *v, const opj_doc *doc, const opj_hit *hit, int x, int y, opj_drag *d);
/* What letting go at x, y would do, for the status line: "Start: Mon 23/11/26", "Duration: 4d",
 * "% Complete: 50%", "Link 2 to 3, finish to start"; 0 (out "") when it would do nothing. */
int opj_drag_text(const opj_view *v, const opj_doc *doc, const opj_drag *d, int x, int y, char *out, size_t size);
/* Lets go at x, y: OWF_OK, or OWF_ERR_FORMAT when it does nothing (a click, no change, off the row, a
 * link that would loop) and the plan is as it was. */
int opj_drag_end(opj_doc *doc, const opj_view *v, const opj_drag *d, int x, int y);

/* ---- ARexx (opj_rexx.c): the plan's commands of the GFOPENPROJECT port ----
 * The app answers VERSION, ACTIVATE, DEACTIVATE, FAULT, PRINT, VIEW and QUIT itself and hands every other
 * command here. Arguments as a ReadArgs template (quotes, KEYWORD=value or KEYWORD value, /S switches);
 * rows and resources are numbered from 1, as the sheets' ID columns. Return codes as every Open app's
 * port: 0 done, 5 not possible now, 10 an error, 20 an unknown command or out of memory; on any but 0,
 * LASTERROR gives the reason. No requester is ever shown. */
#define OPJ_RX_OK 0
#define OPJ_RX_NOW 5
#define OPJ_RX_ERROR 10
#define OPJ_RX_FAIL 20

typedef struct {
    opj_doc *doc;                        /* replaced by NEW and OPEN (the app redraws when it changes) */
    char file[256];                      /* the plan's file, "" when it has none */
    char lasterror[256];
    unsigned long docs;                  /* bumped each time NEW or OPEN replaces doc: a new plan, even at the old address */
    opj_view *view;                      /* the window's view: EXPORT FORMAT PDF prints its columns and zoom,
                                          * COLUMN shows and hides its columns; NULL: a fresh one at days zoom
                                          * for EXPORT, and COLUMN is not possible (5) */
} opj_rexx;

/* A command, its answer into result (cut to size, "" when it has none). */
int opj_rexx_command(opj_rexx *rx, const char *command, char *result, size_t size);
/* The file format a name's extension asks for: .xml MSPDI, .csv CSV, else .gfproj. */
int opj_rexx_format(const char *file);
/* EXPORT's format for the printed Gantt view as a PDF (FORMAT PDF, or a .pdf name): not a plan file, so
 * SAVE and SAVEAS never write it. */
#define OPJ_EXPORT_PDF 100

#ifdef __cplusplus
}
#endif

#endif
