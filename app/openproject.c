/*
 * GFOpenProject - the project planner of the GFOffice Suite (amicradle-teams designs/GFOPENPROJECT-20261113.md).
 *
 * The window: the task sheet on the left and the Gantt chart on the right, as GFOpenProject's portable core
 * (src/project, include/openproject_core.h) draws them through GFOpenLayout's drawing calls; a Resources
 * window (the resource sheet); and the GFOPENPROJECT ARexx port. Everything about the plan is the core's:
 * this file only shows it and hands it what the user types. Files open and save through the core's ARexx
 * commands (OPEN, SAVE, SAVEAS, EXPORT), so a script and the menus do the same thing. Plain GadTools in the
 * screen's font, so it works on any Workbench screen; 68040 code, no FPU needed.
 *
 *   GFOpenProject [FILE]        FILE: a plan (.gfproj, MSPDI XML, CSV, MPX or .gan)
 *
 * Keys: cursor keys move the selected cell, Shift with left and right moves the timescale, Return edits the
 * cell, Del deletes the task, Tab indents it and Shift-Tab outdents it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <devices/printer.h>
#include <graphics/text.h>
#include <graphics/gfxmacros.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/rexxsyslib.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openproject_core.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL;
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "GFOpenProject 0.1.23 (17.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " GFOpenProject, the project planner of the GFOffice Suite. MIT, Copyright (c) 2026 AmiCradle Team";

/* ---- Menus ---- */

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXPORT_XML, M_EXPORT_CSV, M_EXPORT_PDF, M_PRINT, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_EDIT, M_INSERT, M_DELETE, M_INDENT, M_OUTDENT, M_LINK, M_UNLINK,
    M_DAYS, M_WEEKS, M_MONTHS, M_QUARTERS, M_SHOW_BASELINE, M_RESOURCES, M_START, M_GANTT, M_USAGE, M_NETWORK, M_TASK_USAGE, M_GRAPH, M_TRACKING,
    M_SET_BASELINE, M_CLEAR_BASELINE, M_ASSIGN, M_LEVEL, M_CLEAR_LEVELING, M_UPDATE_PROJECT, M_WORKTIME, M_PLAN_CALENDAR, M_TASK_CALENDAR,
    M_INFO,
    M_FIND, M_FIND_NEXT, M_REPLACE, M_SORT_START, M_SORT_FINISH, M_SORT_NAME, M_SORT_DURATION, M_SORT_BY,
    M_AUTOFILTER_ROW, M_AUTOFILTER, M_FILTER_SUMMARIES, M_GROUP_COLUMN, M_GROUP_DESC,
    M_UPDATE_TASKS, M_PROGRESS_LINES,
    M_COLUMN                                 /* View > Columns: M_COLUMN + the column (OPJ_COL_) */
};
#define M_FILTER 200                         /* View > Filter: M_FILTER + an OPJ_FILTER_ (All .. Unstarted) */
#define M_GROUP 220                          /* View > Group By: M_GROUP + an index into group_cols */
#define DATA_ITEM 20                         /* View's item number of Sort; Filter and Group By follow */
#define PROGRESS_ITEM 8                      /* Tools' item number of Progress Lines */
#define NFILTER_ITEMS 7                      /* Filter's items 0 to 6: the filters, one ticked at a time */
#define NGROUP_ITEMS 7                       /* Group By's items 0 to 6: No Group and six columns */
static const int group_cols[NGROUP_ITEMS] = {
    -1, OPJ_COL_DURATION, OPJ_COL_START, OPJ_COL_FINISH, OPJ_COL_RESOURCES, OPJ_COL_PERCENT, OPJ_COL_CONSTRAINT_TYPE
};
/* View's items 10 to 15 are the views, one ticked at a time: the others an item excludes. */
#define VIEWS_BUT(item) (0xFC00L & ~(1L << (item)))
#define COLUMNS_ITEM 17                      /* View's item number of Columns; More Columns follows */
#define MORE_COLUMNS OPJ_COL_WORK            /* the first column in More Columns */

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", "A", 0, 0, (APTR)M_SAVEAS },
    { NM_ITEM, "Export MSPDI XML...", NULL, 0, 0, (APTR)M_EXPORT_XML },
    { NM_ITEM, "Export CSV...", NULL, 0, 0, (APTR)M_EXPORT_CSV },
    { NM_ITEM, "Export PDF...", NULL, 0, 0, (APTR)M_EXPORT_PDF },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Print...", "P", 0, 0, (APTR)M_PRINT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", "?", 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, "Edit", NULL, 0, 0, NULL },
    { NM_ITEM, "Undo", "Z", 0, 0, (APTR)M_UNDO },
    { NM_ITEM, "Redo", "Y", 0, 0, (APTR)M_REDO },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Edit Cell...", "E", 0, 0, (APTR)M_EDIT },
    { NM_ITEM, "Task Information...", "T", 0, 0, (APTR)M_INFO },
    { NM_ITEM, "Insert Task", "I", 0, 0, (APTR)M_INSERT },
    { NM_ITEM, "Delete Task", NULL, 0, 0, (APTR)M_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Indent", ">", 0, 0, (APTR)M_INDENT },
    { NM_ITEM, "Outdent", "<", 0, 0, (APTR)M_OUTDENT },
    { NM_ITEM, "Link to Next Task", "L", 0, 0, (APTR)M_LINK },
    { NM_ITEM, "Unlink", "U", 0, 0, (APTR)M_UNLINK },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Find...", "F", 0, 0, (APTR)M_FIND },
    { NM_ITEM, "Find Next", "X", 0, 0, (APTR)M_FIND_NEXT },
    { NM_ITEM, "Replace...", NULL, 0, 0, (APTR)M_REPLACE },
    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "Days", "1", CHECKIT | CHECKED, 2 | 4 | 8, (APTR)M_DAYS },
    { NM_ITEM, "Weeks", "2", CHECKIT, 1 | 4 | 8, (APTR)M_WEEKS },
    { NM_ITEM, "Months", "3", CHECKIT, 1 | 2 | 8, (APTR)M_MONTHS },
    { NM_ITEM, "Quarters", "4", CHECKIT, 1 | 2 | 4, (APTR)M_QUARTERS },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Show Baseline", "B", CHECKIT | MENUTOGGLE, 0, (APTR)M_SHOW_BASELINE },
    { NM_ITEM, "Go to Project Start", NULL, 0, 0, (APTR)M_START },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Resources...", "R", 0, 0, (APTR)M_RESOURCES },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Gantt Chart", "G", CHECKIT | CHECKED, VIEWS_BUT(10), (APTR)M_GANTT },
    { NM_ITEM, "Resource Usage", "W", CHECKIT, VIEWS_BUT(11), (APTR)M_USAGE },
    { NM_ITEM, "Network Diagram", "D", CHECKIT, VIEWS_BUT(12), (APTR)M_NETWORK },
    { NM_ITEM, "Task Usage", "K", CHECKIT, VIEWS_BUT(13), (APTR)M_TASK_USAGE },
    { NM_ITEM, "Resource Graph", "H", CHECKIT, VIEWS_BUT(14), (APTR)M_GRAPH },
    { NM_ITEM, "Tracking Gantt", "J", CHECKIT, VIEWS_BUT(15), (APTR)M_TRACKING },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Columns", NULL, 0, 0, NULL },
    { NM_SUB, "ID", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)(M_COLUMN + OPJ_COL_ID) },
    { NM_SUB, "Task Name", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)(M_COLUMN + OPJ_COL_NAME) },
    { NM_SUB, "Duration", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)(M_COLUMN + OPJ_COL_DURATION) },
    { NM_SUB, "Start", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)(M_COLUMN + OPJ_COL_START) },
    { NM_SUB, "Finish", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)(M_COLUMN + OPJ_COL_FINISH) },
    { NM_SUB, "Predecessors", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)(M_COLUMN + OPJ_COL_PREDECESSORS) },
    { NM_SUB, "% Complete", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_PERCENT) },
    { NM_SUB, "WBS", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_WBS) },
    { NM_SUB, "Resource Names", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_RESOURCES) },
    { NM_SUB, "Baseline Start", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_BASELINE_START) },
    { NM_SUB, "Baseline Finish", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_BASELINE_FINISH) },
    { NM_SUB, "Start Var.", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_START_VARIANCE) },
    { NM_SUB, "Finish Var.", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_FINISH_VARIANCE) },
    { NM_SUB, "Constraint Type", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_CONSTRAINT_TYPE) },
    { NM_SUB, "Constraint Date", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_CONSTRAINT_DATE) },
    { NM_SUB, "Notes", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_NOTES) },
    { NM_SUB, "Actual Start", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_ACTUAL_START) },
    { NM_SUB, "Actual Finish", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_ACTUAL_FINISH) },
    { NM_SUB, "Actual Duration", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_ACTUAL_DURATION) },
    { NM_SUB, "Remaining Dur.", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_REMAINING_DURATION) },
    { NM_SUB, "BCWS", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_BCWS) },
    { NM_SUB, "BCWP", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_BCWP) },
    { NM_SUB, "ACWP", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_ACWP) },
    { NM_SUB, "SV", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_SV) },
    { NM_SUB, "CV", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_CV) },
    { NM_SUB, "Leveling Delay", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_LEVELING_DELAY) },
    { NM_ITEM, "More Columns", NULL, 0, 0, NULL },            /* a submenu takes 32: work, cost and type here */
    { NM_SUB, "Work", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_WORK) },
    { NM_SUB, "Actual Work", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_ACTUAL_WORK) },
    { NM_SUB, "Remaining Work", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_REMAINING_WORK) },
    { NM_SUB, "Cost", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_COST) },
    { NM_SUB, "Actual Cost", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_ACTUAL_COST) },
    { NM_SUB, "Type", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_TYPE) },
    { NM_SUB, "Effort Driven", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_EFFORT_DRIVEN) },
    { NM_SUB, "Priority", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_PRIORITY) },
    { NM_SUB, "Overallocated", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)(M_COLUMN + OPJ_COL_OVERALLOCATED) },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Sort", NULL, 0, 0, NULL },
    { NM_SUB, "by Start Date", NULL, 0, 0, (APTR)M_SORT_START },
    { NM_SUB, "by Finish Date", NULL, 0, 0, (APTR)M_SORT_FINISH },
    { NM_SUB, "by Name", NULL, 0, 0, (APTR)M_SORT_NAME },
    { NM_SUB, "by Duration", NULL, 0, 0, (APTR)M_SORT_DURATION },
    { NM_SUB, "Sort By...", NULL, 0, 0, (APTR)M_SORT_BY },
    { NM_ITEM, "Filter", NULL, 0, 0, NULL },
    { NM_SUB, "All Tasks", NULL, CHECKIT | CHECKED, 0x7F & ~1, (APTR)(M_FILTER + OPJ_FILTER_ALL) },
    { NM_SUB, "Critical", NULL, CHECKIT, 0x7F & ~2, (APTR)(M_FILTER + OPJ_FILTER_CRITICAL) },
    { NM_SUB, "Milestones", NULL, CHECKIT, 0x7F & ~4, (APTR)(M_FILTER + OPJ_FILTER_MILESTONES) },
    { NM_SUB, "Incomplete Tasks", NULL, CHECKIT, 0x7F & ~8, (APTR)(M_FILTER + OPJ_FILTER_INCOMPLETE) },
    { NM_SUB, "Completed Tasks", NULL, CHECKIT, 0x7F & ~16, (APTR)(M_FILTER + OPJ_FILTER_COMPLETED) },
    { NM_SUB, "Summary Tasks", NULL, CHECKIT, 0x7F & ~32, (APTR)(M_FILTER + OPJ_FILTER_SUMMARY) },
    { NM_SUB, "Unstarted Tasks", NULL, CHECKIT, 0x7F & ~64, (APTR)(M_FILTER + OPJ_FILTER_UNSTARTED) },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "AutoFilter Row", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)M_AUTOFILTER_ROW },
    { NM_SUB, "AutoFilter on Column...", NULL, 0, 0, (APTR)M_AUTOFILTER },
    { NM_SUB, "Show Related Summaries", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)M_FILTER_SUMMARIES },
    { NM_ITEM, "Group By", NULL, 0, 0, NULL },
    { NM_SUB, "No Group", NULL, CHECKIT | CHECKED, 0x7F & ~1, (APTR)(M_GROUP + 0) },
    { NM_SUB, "Duration", NULL, CHECKIT, 0x7F & ~2, (APTR)(M_GROUP + 1) },
    { NM_SUB, "Start", NULL, CHECKIT, 0x7F & ~4, (APTR)(M_GROUP + 2) },
    { NM_SUB, "Finish", NULL, CHECKIT, 0x7F & ~8, (APTR)(M_GROUP + 3) },
    { NM_SUB, "Resource Names", NULL, CHECKIT, 0x7F & ~16, (APTR)(M_GROUP + 4) },
    { NM_SUB, "% Complete", NULL, CHECKIT, 0x7F & ~32, (APTR)(M_GROUP + 5) },
    { NM_SUB, "Constraint Type", NULL, CHECKIT, 0x7F & ~64, (APTR)(M_GROUP + 6) },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Group By Column...", NULL, 0, 0, (APTR)M_GROUP_COLUMN },
    { NM_SUB, "Descending", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)M_GROUP_DESC },
    { NM_TITLE, "Tools", NULL, 0, 0, NULL },
    { NM_ITEM, "Set Baseline...", NULL, 0, 0, (APTR)M_SET_BASELINE },
    { NM_ITEM, "Clear Baseline...", NULL, 0, 0, (APTR)M_CLEAR_BASELINE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Assign Resources...", NULL, 0, 0, (APTR)M_ASSIGN },
    { NM_ITEM, "Level Resources...", NULL, 0, 0, (APTR)M_LEVEL },
    { NM_ITEM, "Clear Leveling", NULL, 0, 0, (APTR)M_CLEAR_LEVELING },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Update Tasks...", NULL, 0, 0, (APTR)M_UPDATE_TASKS },
    { NM_ITEM, "Update Project...", NULL, 0, 0, (APTR)M_UPDATE_PROJECT },
    { NM_ITEM, "Progress Lines", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)M_PROGRESS_LINES },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Change Working Time...", NULL, 0, 0, (APTR)M_WORKTIME },
    { NM_ITEM, "Plan Calendar...", NULL, 0, 0, (APTR)M_PLAN_CALENDAR },
    { NM_ITEM, "Task Calendar...", NULL, 0, 0, (APTR)M_TASK_CALENDAR },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct TextFont *font;
static struct TextAttr font_attr;
static char font_name[64];
static int fh, fw;                 /* the font's height and an average character's width */
static int quit_now;

static opj_rexx rx;                /* the plan (rx.doc) and its file (rx.file), shared with the ARexx port */
static unsigned long shown;         /* rx.docs + 1 for the doc the view was made for, 0 none yet (OPEN and NEW bump rx.docs) */
static opj_view view;
static opj_lines lines;              /* View > Filter and Group By: the Gantt view's lines (rx.lines too) */
static int autofilter_row;           /* View > Filter > AutoFilter Row: a column's title filters on a click */
static char find_text[80];           /* Edit > Find...: what, and in which column */
static int find_col = OPJ_COL_NAME;
static oldraw *od;
static int sel_col = OPJ_COL_NAME;
/* The Resource Usage view in place of the Gantt view (View menu): it shares the zoom and the timescale's
 * place, and keeps its own top row and selected row (a resource or an assignment). Read only. */
static int usage_mode, usage_first, usage_sel = -1;   /* usage_mode: 0, OPJ_VIEW_USAGE, OPJ_VIEW_TASK_USAGE or OPJ_VIEW_GRAPH (whose usage_sel is the resource) */
/* The Network Diagram in place of the Gantt view (View menu): the same selected task, its boxes laid out
 * again at every redraw, so every edit shows. */
static int network_mode;
static opj_network net;
static char status[256];
static char title_text[300];

#define DOC (rx.doc)

/* The rows of the usage view showing (Task Usage or Resource Usage). */
static int usage_rows(void)
{
    if (usage_mode == OPJ_VIEW_GRAPH) return DOC->plan->nresources;
    return usage_mode == OPJ_VIEW_TASK_USAGE ? opj_task_usage_rows(DOC) : opj_usage_rows(DOC);
}

/* Shows a usage view (OPJ_VIEW_USAGE, OPJ_VIEW_TASK_USAGE or OPJ_VIEW_GRAPH), its selection kept to its rows. */
static void show_usage(int mode)
{
    if (usage_mode != mode) usage_sel = -1, usage_first = 0;
    usage_mode = mode;
    if (usage_sel < 0 && usage_rows()) usage_sel = 0;
}

/* ---- Small helpers ---- */

static void put_text(char *dst, size_t n, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (!n) return;
    if (len >= n) len = n - 1;
    if (len) memcpy(dst, src, len);
    dst[len] = 0;
}

/* ARexx (amicradle-teams ARexx-RULE.md): while a command runs no requester shows. */
static int rexx_busy;

static void tell(const char *text)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenProject", (UBYTE *)"%s", (UBYTE *)"OK" };
    if (rexx_busy) return;
    EasyRequest(win, &es, NULL, (ULONG)text);
}

/* The button picked, counted from 1; the last button is 0 (Cancel). */
static int ask(const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenProject", (UBYTE *)"%s", (UBYTE *)buttons };
    if (rexx_busy) return 0;
    return EasyRequest(win, &es, NULL, (ULONG)text);
}

static void set_status(const char *s)
{
    put_text(status, sizeof status, s);
}

static const char *leaf(const char *path)
{
    const char *s = (const char *)FilePart((STRPTR)path);
    return s && *s ? s : path;
}

/* A file name from the ASL requester; 0 when cancelled. */
static int ask_file(const char *title, int save, const char *pattern, char *out, size_t size)
{
    struct FileRequester *fr;
    char dir[400] = "", file[108] = "";
    int ok = 0;
    if (rx.file[0]) {
        const char *f = leaf(rx.file);
        size_t n = (size_t)(f - rx.file);
        if (n < sizeof dir) { memcpy(dir, rx.file, n); dir[n] = 0; }
        if (save) put_text(file, sizeof file, f);
    }
    fr = (struct FileRequester *)AllocAslRequestTags(ASL_FileRequest,
        ASLFR_Window, (ULONG)win, ASLFR_TitleText, (ULONG)title,
        ASLFR_DoSaveMode, save, ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern, (ULONG)pattern,
        ASLFR_InitialDrawer, (ULONG)dir, ASLFR_InitialFile, (ULONG)file, TAG_DONE);
    if (!fr) return 0;
    if (AslRequest(fr, NULL) && fr->fr_File && fr->fr_File[0]) {
        put_text(out, size, (const char *)fr->fr_Drawer);
        ok = AddPart((STRPTR)out, fr->fr_File, size) != 0;
    }
    FreeAslRequest(fr);
    return ok;
}

/* A line of text from a small window; 0 when cancelled. */
static int ask_text(const char *title, const char *prompt, char *buf, size_t size)
{
    struct Window *aw;
    struct Gadget *eg = NULL, *g, *sg;
    struct NewGadget ng;
    int done = 0, okd = 0, w = fw * 50, h = (fh + 8) * 3 + 12, lw;
    if (rexx_busy) return 0;
    lw = (int)strlen(prompt) * fw + 12;
    if (lw > w / 2) lw = w / 2;
    aw = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 60, WA_Top, win->TopEdge + 60, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!aw) return 0;
    if (!(g = CreateContext(&eg))) { CloseWindow(aw); return 0; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_LeftEdge = aw->BorderLeft + 8 + lw;
    ng.ng_TopEdge = aw->BorderTop + 8;
    ng.ng_Width = w - 16 - lw;
    ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (UBYTE *)prompt;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = 1;
    g = sg = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)buf, GTST_MaxChars, (ULONG)(size - 1), TAG_DONE);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = aw->Height - aw->BorderBottom - 6 - (fh + 6);
    ng.ng_LeftEdge = aw->BorderLeft + 8;
    ng.ng_Width = fw * 10 + 16;
    ng.ng_GadgetText = (UBYTE *)"OK";
    ng.ng_GadgetID = 2;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = aw->Width - aw->BorderRight - 8 - ng.ng_Width;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = 3;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(aw); return 0; }
    AddGList(aw, eg, ~0, -1, NULL);
    RefreshGList(eg, aw, NULL, -1);
    GT_RefreshWindow(aw, NULL);
    ActivateGadget(sg, aw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(aw->UserPort);
        while ((m = GT_GetIMsg(aw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) done = 1;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(aw); GT_EndRefresh(aw, TRUE); }
            else if (class == IDCMP_GADGETUP && (gg->GadgetID == 1 || gg->GadgetID == 2)) done = okd = 1;
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 3) done = 1;
        }
    }
    if (okd) {
        STRPTR str = NULL;
        GT_GetGadgetAttrs(sg, aw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
        put_text(buf, size, str ? (const char *)str : "");
    }
    RemoveGList(aw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(aw);
    return okd;
}

/* A core command (opj_rexx.c), as a script would send it; its reason in rx.lasterror when it fails. */
static int run(const char *command, char *result, size_t size)
{
    char scratch[64];
    if (!result) { result = scratch; size = sizeof scratch; }
    return opj_rexx_command(&rx, command, result, size);
}

/* A command with a file name: VERB "name" [more]. A name with a quote in it is refused. */
static int run_file(const char *verb, const char *name, const char *more)
{
    char line[600];
    if (strchr(name, '"')) {
        snprintf(rx.lasterror, sizeof rx.lasterror, "A file name with a quote in it can't be used");
        return OPJ_RX_ERROR;
    }
    snprintf(line, sizeof line, "%.15s \"%.500s\"%s%.40s", verb, name, more ? " " : "", more ? more : "");
    return run(line, NULL, 0);
}

/* ---- The view ---- */

static int inner_w(void) { return win ? win->GZZWidth : 0; }
static int inner_h(void) { return win ? win->GZZHeight : 0; }

/* The view for the doc shown: a new doc gets a new view, the same doc keeps its scroll and choices. */
static void view_for_doc(void)
{
    int zoom = view.zoom, base = view.show_baseline, tracking = view.tracking, progress = view.progress_lines;
    if (shown != rx.docs + 1) {
        opj_view_init(&view, DOC, inner_w(), inner_h(), fh + 5, shown ? zoom : OPJ_ZOOM_DAYS);
        view.show_baseline = base;
        view.tracking = tracking;
        view.progress_lines = progress;     /* Tools > Progress Lines stays ticked for the next plan */
        view.selected = DOC->plan->ntasks ? 0 : -1;
        shown = rx.docs + 1;
    }
    view.width = inner_w();
    view.height = inner_h();
    if (view.selected >= DOC->plan->ntasks) view.selected = DOC->plan->ntasks - 1;
    /* the lines for the plan as it is now (after any edit, NEW or OPEN); none on no memory: every task */
    view.lines = opj_lines_update(&lines, DOC) == OWF_OK ? &lines : NULL;
    if (view.lines && view.selected >= 0 && opj_view_task_line(&view, DOC, view.selected) < 0) {
        int line;                                       /* the selection hidden: the first task shown */
        view.selected = -1;
        for (line = 0; line < lines.n && view.selected < 0; line++) view.selected = opj_view_line_task(&view, DOC, line);
    }
    if (view.first_row > 0 && view.first_row >= opj_view_lines(&view, DOC)) view.first_row = 0;
    if (usage_sel >= usage_rows()) usage_sel = usage_rows() - 1;
    if (usage_first > usage_sel && usage_first > 0) usage_first = usage_sel > 0 ? usage_sel : 0;
}

static void update_title(void)
{
    const char *name = rx.file[0] ? leaf(rx.file) : "Untitled";
    if (!win) return;
    if (status[0]) snprintf(title_text, sizeof title_text, "GFOpenProject - %.100s%s - %.150s", name, DOC->dirty ? " *" : "", status);
    else snprintf(title_text, sizeof title_text, "GFOpenProject - %.100s%s", name, DOC->dirty ? " *" : "");
    SetWindowTitles(win, (UBYTE *)title_text, (UBYTE *)~0);
}

/* The selected cell's box, or 0 when it isn't on show. */
static int cell_box(oldraw_rect *r)
{
    int c, x = 0, line = opj_view_task_line(&view, DOC, view.selected);
    if (line < view.first_row || line >= view.first_row + opj_rows_shown(&view)) return 0;
    if (!view.columns[sel_col]) return 0;
    for (c = 0; c < sel_col; c++) x += view.columns[c];
    if (x >= opj_chart_left(&view)) return 0;
    r->x = x;
    r->y = opj_row_y(&view, view.selected);
    r->w = view.columns[sel_col];
    if (r->x + r->w > opj_chart_left(&view)) r->w = opj_chart_left(&view) - r->x;
    r->h = view.row_height;
    return 1;
}

static void draw_cb(void *user, oldraw *d, const oldraw_rect *area)
{
    oldraw_rect c;
    (void)user;
    if (usage_mode) {
        opj_view u = view;
        u.first_row = usage_first;
        u.selected = usage_sel;
        if (usage_mode == OPJ_VIEW_GRAPH) opj_graph_draw(d, &view, DOC, usage_sel, area);
        else if (usage_mode == OPJ_VIEW_TASK_USAGE) opj_task_usage_draw(d, &u, DOC, area);
        else opj_usage_draw(d, &u, DOC, area);
        return;
    }
    if (network_mode) {
        opj_network_draw(d, &view, &net, DOC, area);
        return;
    }
    opj_draw(d, &view, DOC, area);
    if (cell_box(&c)) {
        oldraw_rect t;
        t = c; t.h = 1; oldraw_fill(d, &t, OLDRAW_PEN(OLDRAW_PEN_TEXT));
        t.y = c.y + c.h - 1; oldraw_fill(d, &t, OLDRAW_PEN(OLDRAW_PEN_TEXT));
        t = c; t.w = 1; oldraw_fill(d, &t, OLDRAW_PEN(OLDRAW_PEN_TEXT));
        t.x = c.x + c.w - 1; oldraw_fill(d, &t, OLDRAW_PEN(OLDRAW_PEN_TEXT));
    }
}

static void redraw(void)
{
    if (!win) return;
    view_for_doc();
    if (!od) od = oldraw_new_rastport(win->RPort, scr, inner_w(), inner_h());
    if (!od) return;
    oldraw_resize(od, inner_w(), inner_h());
    if (network_mode) {
        opj_network_free(&net);
        opj_network_layout(DOC, &net);                  /* none on no memory: an empty diagram */
        opj_network_scroll(&view, &net, 0, 0);          /* kept to the diagram as it is now */
    }
    oldraw_damage_all(od);
    oldraw_redraw(od, draw_cb, NULL);
    update_title();
}

/* The selection on show: rows scrolled to it. */
static void keep_selection_shown(void)
{
    int shown_rows = opj_rows_shown(&view), line = opj_view_task_line(&view, DOC, view.selected);
    if (line < 0) return;
    if (line < view.first_row) view.first_row = line;
    if (shown_rows > 1 && line >= view.first_row + shown_rows - 1) view.first_row = line - shown_rows + 2;
    if (view.first_row < 0) view.first_row = 0;
}

/* Up and down the Resource Usage view's rows, scrolled to keep the selection on show. */
static void move_usage(int rows)
{
    int n = usage_rows(), shown_rows = opj_rows_shown(&view), s;
    if (!n) return;
    s = usage_sel < 0 ? 0 : usage_sel + rows;
    usage_sel = s < 0 ? 0 : s >= n ? n - 1 : s;
    if (usage_sel < usage_first) usage_first = usage_sel;
    if (shown_rows > 1 && usage_sel >= usage_first + shown_rows - 1) usage_first = usage_sel - shown_rows + 2;
    if (usage_first < 0) usage_first = 0;
    redraw();
}

static void move_selection(int rows, int cols)
{
    int n = DOC->plan->ntasks;
    if (usage_mode) {
        if (rows) move_usage(rows);
        return;
    }
    if (network_mode) {                                 /* up and down the tasks; left and right scroll */
        if (rows && n) {                                /* over summaries, which have no box */
            int s = view.selected < 0 ? 0 : view.selected + rows, step = rows > 0 ? 1 : -1;
            oldraw_rect b;
            s = s < 0 ? 0 : s >= n ? n - 1 : s;
            while (s >= 0 && s < n && !opj_network_box(&view, &net, s, &b)) s += step;
            if (s >= 0 && s < n) view.selected = s;
            opj_network_reveal(&view, &net, view.selected);
        }
        if (cols) opj_network_scroll(&view, &net, cols * (OPJ_NET_BOX_W + OPJ_NET_GAP_X) / 2, 0);
        redraw();
        return;
    }
    if (rows && n) {                                    /* up and down the lines, over group headings */
        int nl = opj_view_lines(&view, DOC), line = opj_view_task_line(&view, DOC, view.selected);
        int step = rows > 0 ? 1 : -1, t = -1;
        line = line < 0 ? 0 : line + rows;
        line = line < 0 ? 0 : line >= nl ? nl - 1 : line;
        while (line >= 0 && line < nl && (t = opj_view_line_task(&view, DOC, line)) < 0) line += step;
        while (t < 0 && line - step >= 0 && line - step < nl) {      /* none that way: the nearest back */
            line -= step;
            t = opj_view_line_task(&view, DOC, line);
        }
        if (t >= 0) view.selected = t;
        keep_selection_shown();
    }
    while (cols) {
        int c = sel_col + (cols > 0 ? 1 : -1);
        if (c < 0 || c >= OPJ_NCOLS) break;
        sel_col = c;
        if (view.columns[c]) cols += cols > 0 ? -1 : 1;
    }
    redraw();
}

/* The timescale moved a period at its zoom (back when by < 0). */
static void scroll_time(int by)
{
    if (network_mode) {
        opj_network_scroll(&view, &net, by * (view.width * 3 / 4), 0);
        redraw();
        return;
    }
    long step = opj_period_next(view.zoom, opj_period_start(view.zoom, view.origin)) - opj_period_start(view.zoom, view.origin);
    if (step <= 0) step = 24L * 60L;
    if (view.zoom == OPJ_ZOOM_DAYS) step = 24L * 60L;
    view.origin += by * step;
    redraw();
}

/* ---- Edits ---- */

static void edit_failed(int rc, const char *what)
{
    char msg[300];
    if (rc == OWF_OK) return;
    if (rc == OWF_ERR_MEMORY) snprintf(msg, sizeof msg, "%.60s: not enough memory", what);
    else snprintf(msg, sizeof msg, "%.60s: not possible (out of range, text it doesn't take, or a loop of links)", what);
    tell(msg);
}

/* 0, with the status line saying why, when edit can't run in the view shown (opj_edit_refused) */
static int edit_allowed(int edit)
{
    const char *why = opj_edit_refused(usage_mode, edit);
    if (!why) return 1;
    set_status(why);
    redraw();
    return 0;
}

/* ---- Task Information (Edit > Task Information..., or a double-click on a bar) ---- */

enum { TI_NAME, TI_DURATION, TI_PERCENT, TI_PREDECESSORS, TI_RESOURCES, TI_CONSTRAINT, TI_DATE, TI_NOTES, TI_TYPE,
       TI_EFFORT, TI_PRIORITY, TI_N };

static void task_info(void)
{
    static const int cols[TI_N] = { OPJ_COL_NAME, OPJ_COL_DURATION, OPJ_COL_PERCENT, OPJ_COL_PREDECESSORS,
                                    OPJ_COL_RESOURCES, OPJ_COL_CONSTRAINT_TYPE, OPJ_COL_CONSTRAINT_DATE, OPJ_COL_NOTES,
                                    OPJ_COL_TYPE, OPJ_COL_EFFORT_DRIVEN, OPJ_COL_PRIORITY };
    static const char *const labels[TI_N] = { "Name", "Duration", "% Complete", "Predecessors", "Resources",
                                              "Constraint", "Date", "Notes", "Task Type", "Effort Driven",
                                              "Priority" };
    static STRPTR kinds[9];
    static STRPTR types[] = { (STRPTR)"Fixed Units", (STRPTR)"Fixed Duration", (STRPTR)"Fixed Work", NULL };
    static char was[TI_N][400], now[TI_N][400];
    struct Window *aw;
    struct Gadget *eg = NULL, *g, *gad[TI_N];
    struct NewGadget ng;
    char title[80], why[160];
    int i, done = 0, row = view.selected, lw = fw * 13 + 12, w = fw * 64, h, kind, kind_was, type, type_was, summary;
    if (rexx_busy || !edit_allowed(OPJ_EDIT_INFO)) return;
    if (row < 0 || row >= DOC->plan->ntasks) { set_status("Task Information: choose a task first"); redraw(); return; }
    for (i = 0; i < 8; i++) kinds[i] = (STRPTR)opj_constraint_name(i);
    kinds[8] = NULL;
    for (i = 0; i < TI_N; i++) opj_cell_text(DOC, row, cols[i], was[i], sizeof was[i]);
    kind = kind_was = DOC->plan->tasks[row].constraint;
    type = type_was = DOC->plan->tasks[row].type;            /* a summary's are blank: its gadgets are off */
    summary = DOC->plan->tasks[row].summary;
    snprintf(title, sizeof title, "Task Information: %.50s", was[TI_NAME]);
    h = (fh + 8) * (TI_N + 1) + 16;
    aw = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP |
                  BUTTONIDCMP | CYCLEIDCMP,
        TAG_DONE);
    if (!aw) return;
    if (!(g = CreateContext(&eg))) { CloseWindow(aw); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_Height = fh + 6;
    ng.ng_Flags = PLACETEXT_LEFT;
    for (i = 0; i < TI_N; i++) {
        ng.ng_LeftEdge = aw->BorderLeft + 8 + lw;
        ng.ng_TopEdge = aw->BorderTop + 8 + i * (fh + 8);
        ng.ng_Width = w - 16 - lw;
        ng.ng_GadgetText = (UBYTE *)labels[i];
        ng.ng_GadgetID = 10 + i;
        if (i == TI_CONSTRAINT)
            g = gad[i] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)kinds, GTCY_Active, (ULONG)kind, TAG_DONE);
        else if (i == TI_TYPE)
            g = gad[i] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)types, GTCY_Active, (ULONG)type,
                                      GA_Disabled, (ULONG)summary, TAG_DONE);
        else if (i == TI_EFFORT)
            g = gad[i] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)!strcmp(was[i], "Yes"),
                                      GA_Disabled, (ULONG)summary, TAG_DONE);
        else if (i == TI_PRIORITY)                   /* 0..1000, 1000 Do Not Level; a summary has none */
            g = gad[i] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)was[i], GTST_MaxChars, 4,
                                      GA_Disabled, (ULONG)summary, TAG_DONE);
        else
            g = gad[i] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)was[i], GTST_MaxChars, 399, TAG_DONE);
    }
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = aw->Height - aw->BorderBottom - 6 - (fh + 6);
    ng.ng_LeftEdge = aw->BorderLeft + 8;
    ng.ng_Width = fw * 10 + 16;
    ng.ng_GadgetText = (UBYTE *)"OK";
    ng.ng_GadgetID = 2;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = aw->Width - aw->BorderRight - 8 - ng.ng_Width;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = 3;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(aw); return; }
    AddGList(aw, eg, ~0, -1, NULL);
    RefreshGList(eg, aw, NULL, -1);
    GT_RefreshWindow(aw, NULL);
    ActivateGadget(gad[TI_NAME], aw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        int ok = 0;
        WaitPort(aw->UserPort);
        while ((m = GT_GetIMsg(aw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) done = 1;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(aw); GT_EndRefresh(aw, TRUE); }
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 10 + TI_CONSTRAINT) kind = code;
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 10 + TI_TYPE) {
                type = code;                                 /* Fixed Work is always effort driven */
                if (type == OWF_TT_FIXED_WORK)
                    GT_SetGadgetAttrs(gad[TI_EFFORT], aw, NULL, GTCB_Checked, TRUE, TAG_DONE);
            }
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 2) ok = 1;
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 3) done = 1;
        }
        if (ok && !done) {
            /* only the fields changed are typed, the constraint's type before its date, as one edit */
            int use[TI_N], n = 0, bad = -1, rc;
            const char *texts[TI_N];
            for (i = 0; i < TI_N; i++) {
                STRPTR str = NULL;
                if (i == TI_CONSTRAINT) {
                    if (kind == kind_was) continue;
                    put_text(now[i], sizeof now[i], opj_constraint_name(kind));
                } else if (i == TI_TYPE) {
                    if (summary || type == type_was) continue;
                    put_text(now[i], sizeof now[i], (const char *)types[type]);
                } else if (i == TI_EFFORT) {
                    ULONG on = 0;
                    if (summary) continue;
                    GT_GetGadgetAttrs(gad[i], aw, NULL, GTCB_Checked, (ULONG)&on, TAG_DONE);
                    put_text(now[i], sizeof now[i], on ? "Yes" : "No");
                    if (!strcmp(now[i], was[i])) continue;
                } else {
                    if (i == TI_PRIORITY && summary) continue;
                    GT_GetGadgetAttrs(gad[i], aw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                    put_text(now[i], sizeof now[i], str ? (const char *)str : "");
                    if (!strcmp(now[i], was[i])) continue;
                }
                use[n] = cols[i];
                texts[n++] = now[i];
            }
            if (!n) { done = 1; break; }
            if ((rc = opj_set_cells(DOC, row, use, texts, n, &bad)) == OWF_OK) {
                if (DOC->schedule_error) set_status("The links make a loop: the plan can't be scheduled");
                done = 1;
            } else {
                for (i = 0; i < TI_N && bad >= 0; i++)
                    if (cols[i] == use[bad]) break;
                snprintf(why, sizeof why, rc == OWF_ERR_MEMORY ? "Task Information: out of memory"
                                                                : "Task Information: %s was not taken",
                         i < TI_N ? labels[i] : "a field");
                SetWindowTitles(aw, (UBYTE *)why, (UBYTE *)~0);   /* stays open to put it right */
                if (i < TI_N && i != TI_CONSTRAINT && i != TI_TYPE && i != TI_EFFORT) ActivateGadget(gad[i], aw, NULL);
            }
        }
    }
    RemoveGList(aw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(aw);
    redraw();
}

static void edit_cell(void)
{
    char text[400], prompt[64];
    int rc;
    if (!edit_allowed(OPJ_EDIT_CELL)) return;
    if (view.selected < 0) {
        if ((rc = opj_insert_task(DOC, DOC->plan->ntasks, "")) != OWF_OK) { edit_failed(rc, "New task"); return; }
        view.selected = DOC->plan->ntasks - 1;
    }
    if (!opj_column_typed(sel_col)) {
        set_status("That column is worked out, not typed");
        redraw();
        return;
    }
    opj_cell_text(DOC, view.selected, sel_col, text, sizeof text);
    snprintf(prompt, sizeof prompt, "%.60s", opj_column_title(sel_col));
    if (!ask_text("Edit Cell", prompt, text, sizeof text)) return;
    rc = opj_set_cell(DOC, view.selected, sel_col, text);
    edit_failed(rc, opj_column_title(sel_col));
    if (rc == OWF_OK && DOC->schedule_error) set_status("The links make a loop: the plan can't be scheduled");
    redraw();
}

static void insert_task(void)
{
    int at = view.selected < 0 ? DOC->plan->ntasks : view.selected, rc;
    char name[200] = "";
    if (!ask_text("Insert Task", "Name", name, sizeof name)) return;
    if ((rc = opj_insert_task(DOC, at, name)) != OWF_OK) { edit_failed(rc, "Insert Task"); return; }
    view.selected = at;
    keep_selection_shown();
    redraw();
}

static void delete_task(void)
{
    int rc;
    if (!edit_allowed(OPJ_EDIT_DELETE)) return;
    if (view.selected < 0) return;
    if ((rc = opj_delete_tasks(DOC, view.selected, 1)) != OWF_OK) edit_failed(rc, "Delete Task");
    redraw();
}

static void undo_redo(int redo)
{
    if (!(redo ? opj_can_redo(DOC) : opj_can_undo(DOC))) {
        set_status(redo ? "Nothing to redo" : "Nothing to undo");
    } else if ((redo ? opj_redo(DOC) : opj_undo(DOC)) != OWF_OK) {
        tell("Not enough memory to undo");
    }
    redraw();
}

/* ---- Files ---- */

/* 1 when the plan may be dropped: saved, or the user said so. */
static int may_drop(void)
{
    int k;
    if (!DOC->dirty) return 1;
    k = ask("The plan has changes that are not saved.", "Save|Don't Save|Cancel");
    if (k == 0) return 0;
    if (k == 2) return 1;
    if (!rx.file[0]) {
        char path[512];
        if (!ask_file("Save the plan as", TRUE, "#?.gfproj", path, sizeof path)) return 0;
        if (run_file("SAVEAS", path, NULL) != OPJ_RX_OK) { tell(rx.lasterror); return 0; }
        return 1;
    }
    if (run("SAVE", NULL, 0) != OPJ_RX_OK) { tell(rx.lasterror); return 0; }
    return 1;
}

static void tick_view(void);

static void open_file(const char *path)
{
    if (run_file("OPEN", path, "FORCE") != OPJ_RX_OK) tell(rx.lasterror);
    else set_status("");
    tick_view();                        /* the new plan's Filter and Group By (All Tasks, No Group) */
    redraw();
}

static void save(int as)
{
    char path[512];
    int rc;
    if (as || !rx.file[0]) {
        if (!ask_file("Save the plan as", TRUE, "#?.(gfproj|xml|csv)", path, sizeof path)) return;
        rc = run_file("SAVEAS", path, NULL);
    } else {
        rc = run("SAVE", NULL, 0);
    }
    if (rc != OPJ_RX_OK) tell(rx.lasterror);
    else set_status("Saved");
    redraw();
}

static void export_as(const char *format, const char *pattern)
{
    char path[512], more[24];
    if (!ask_file("Export the plan as", TRUE, pattern, path, sizeof path)) return;
    snprintf(more, sizeof more, "FORMAT %.10s", format);
    if (run_file("EXPORT", path, more) != OPJ_RX_OK) tell(rx.lasterror);
    else set_status("Exported");
    redraw();
}

/* ---- Printing: each page drawn into a bitmap like the screen's, dumped through printer.device ---- */

#define PRINT_W 1600
#define PRINT_H 1100

typedef struct { const opj_print *pr; int page; } print_job;

static void print_cb(void *user, oldraw *d, const oldraw_rect *area)
{
    const print_job *j = (const print_job *)user;
    (void)area;
    opj_print_draw(d, j->pr, DOC, j->page);
}

/* The plan printed; 0 with the reason in why when it can't be. */
static int print_plan(char *why, size_t size)
{
    opj_print pr;
    struct MsgPort *port = NULL;
    struct IODRPReq *io = NULL;
    struct BitMap *bm = NULL;
    struct RastPort rp;
    oldraw *pd = NULL;
    int pages, page, ok = 0, opened = 0;
    if (!DOC->plan->ntasks) { put_text(why, size, "The plan has no tasks to print"); return 0; }
    if (!(pages = opj_print_layout(&pr, &view, DOC, PRINT_W, PRINT_H))) {
        put_text(why, size, "The page is too small for the plan");
        return 0;
    }
    if (!(port = CreateMsgPort()) || !(io = (struct IODRPReq *)CreateIORequest(port, sizeof *io))) {
        put_text(why, size, "Not enough memory to print");
        goto done;
    }
    if (OpenDevice((STRPTR)"printer.device", 0, (struct IORequest *)io, 0)) {
        put_text(why, size, "printer.device would not open (check the Printer preferences)");
        goto done;
    }
    opened = 1;
    if (!(bm = AllocBitMap(PRINT_W, PRINT_H, GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH), BMF_CLEAR,
                           scr->RastPort.BitMap))) {
        put_text(why, size, "Not enough memory for a page");
        goto done;
    }
    InitRastPort(&rp);
    rp.BitMap = bm;
    SetFont(&rp, font);
    if (!(pd = oldraw_new_rastport(&rp, scr, PRINT_W, PRINT_H))) { put_text(why, size, "Not enough memory to print"); goto done; }
    for (page = 0; page < pages; page++) {
        print_job j;
        j.pr = &pr;
        j.page = page;
        SetRast(&rp, 0);
        oldraw_damage_all(pd);
        oldraw_redraw(pd, print_cb, &j);
        io->io_Command = PRD_DUMPRPORT;
        io->io_RastPort = &rp;
        io->io_ColorMap = scr->ViewPort.ColorMap;
        io->io_Modes = GetVPModeID(&scr->ViewPort);
        io->io_SrcX = 0;
        io->io_SrcY = 0;
        io->io_SrcWidth = PRINT_W;
        io->io_SrcHeight = PRINT_H;
        io->io_DestCols = 0;
        io->io_DestRows = 0;
        io->io_Special = SPECIAL_FULLCOLS | SPECIAL_ASPECT;
        if (DoIO((struct IORequest *)io)) {
            snprintf(why, size, "The printer stopped at page %d of %d", page + 1, pages);
            goto done;
        }
        if (page + 1 < pages) {            /* a form feed between pages */
            struct IOStdReq *s = (struct IOStdReq *)io;
            s->io_Command = CMD_WRITE;
            s->io_Data = (APTR)"\014";
            s->io_Length = 1;
            DoIO((struct IORequest *)s);
        }
    }
    snprintf(why, size, "Printed %d page%s", pages, pages == 1 ? "" : "s");
    ok = 1;
done:
    if (pd) oldraw_free(pd);
    if (bm) { WaitBlit(); FreeBitMap(bm); }
    if (opened) CloseDevice((struct IORequest *)io);
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
    return ok;
}

/* ---- The Resources window: the resource sheet, one line a resource ---- */

static struct List res_list;
static struct Node *res_nodes;
static char (*res_text)[160];
static int res_count;

static void res_free(void)
{
    free(res_nodes);
    free(res_text);
    res_nodes = NULL;
    res_text = NULL;
    res_count = 0;
    NewList(&res_list);
}

static void res_build(void)
{
    int i, c;
    res_free();
    res_count = DOC->plan->nresources;
    if (!res_count) return;
    res_nodes = (struct Node *)calloc((size_t)res_count, sizeof *res_nodes);
    res_text = (char (*)[160])calloc((size_t)res_count, sizeof *res_text);
    if (!res_nodes || !res_text) { res_free(); return; }
    for (i = 0; i < res_count; i++) {
        size_t k = 0;
        for (c = 0; c < OPJ_NRCOLS; c++) {
            char cell[64];
            static const int width[OPJ_NRCOLS] = { 4, 16, 9, 5, 10, 6, 10, 8, 10, 12 };
            size_t l;
            opj_resource_cell_text(DOC, i, c, cell, sizeof cell);
            l = strlen(cell);
            if ((int)l > width[c]) l = (size_t)width[c];
            if (k + (size_t)width[c] + 2 >= sizeof res_text[i]) break;
            memcpy(res_text[i] + k, cell, l);
            memset(res_text[i] + k + l, ' ', (size_t)width[c] + 1 - l);
            k += (size_t)width[c] + 1;
        }
        res_text[i][k] = 0;
        res_nodes[i].ln_Name = res_text[i];
        AddTail(&res_list, &res_nodes[i]);
    }
}

static STRPTR res_columns[OPJ_NRCOLS + 1];

static void resources_window(void)
{
    enum { R_LIST = 1, R_COLUMN, R_VALUE, R_ADD, R_DELETE, R_SET };
    struct Window *rw;
    struct Gadget *eg = NULL, *g, *g_list, *g_col, *g_val;
    struct NewGadget ng;
    int done = 0, sel = -1, col = OPJ_RCOL_NAME, w = fw * 99, lines = 12, h, i, by;
    char head[200];
    size_t k = 0;
    static const int width[OPJ_NRCOLS] = { 4, 16, 9, 5, 10, 6, 10, 8, 10, 12 };
    for (i = 0; i < OPJ_NRCOLS; i++) {
        const char *t = opj_resource_column_title(i);
        size_t l = strlen(t);
        res_columns[i] = (STRPTR)t;
        if ((int)l > width[i]) l = (size_t)width[i];
        memcpy(head + k, t, l);
        memset(head + k + l, ' ', (size_t)width[i] + 1 - l);
        k += (size_t)width[i] + 1;
    }
    head[k] = 0;
    res_columns[OPJ_NRCOLS] = NULL;
    res_build();
    h = (fh + 2) * (lines + 1) + (fh + 8) * 3 + 24;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"GFOpenProject Resources", WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 30, WA_Top, win->TopEdge + 30, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | CYCLEIDCMP | STRINGIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!rw) { res_free(); return; }
    SetFont(rw->RPort, font);
    if (!(g = CreateContext(&eg))) { CloseWindow(rw); res_free(); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_LeftEdge = rw->BorderLeft + 8;
    ng.ng_TopEdge = rw->BorderTop + 2;
    ng.ng_Width = w - 16;
    ng.ng_Height = fh + 4;
    g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)head, GTTX_CopyText, TRUE, TAG_DONE);   /* the column heading */
    ng.ng_TopEdge = rw->BorderTop + 6 + fh + 2;
    ng.ng_Width = w - 16;
    ng.ng_Height = (fh + 2) * lines + 4;
    ng.ng_GadgetID = R_LIST;
    g = g_list = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&res_list, GTLV_ShowSelected, 0, TAG_DONE);
    by = ng.ng_TopEdge + ng.ng_Height + 6;
    ng.ng_TopEdge = by;
    ng.ng_Width = fw * 16 + 24;
    ng.ng_Height = fh + 6;
    ng.ng_GadgetID = R_COLUMN;
    g = g_col = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)res_columns, GTCY_Active, col, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 6;
    ng.ng_Width = w - 16 - (ng.ng_LeftEdge - rw->BorderLeft - 8);
    ng.ng_GadgetID = R_VALUE;
    g = g_val = CreateGadget(STRING_KIND, g, &ng, GTST_MaxChars, 120, TAG_DONE);
    ng.ng_TopEdge = by + fh + 12;
    ng.ng_LeftEdge = rw->BorderLeft + 8;
    ng.ng_Width = fw * 14 + 16;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_GadgetText = (UBYTE *)"Add...";
    ng.ng_GadgetID = R_ADD;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 6;
    ng.ng_GadgetText = (UBYTE *)"Delete";
    ng.ng_GadgetID = R_DELETE;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = rw->Width - rw->BorderRight - 8 - ng.ng_Width;
    ng.ng_GadgetText = (UBYTE *)"Set Cell";
    ng.ng_GadgetID = R_SET;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(rw); res_free(); return; }
    AddGList(rw, eg, ~0, -1, NULL);
    RefreshGList(eg, rw, NULL, -1);
    GT_RefreshWindow(rw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(rw->UserPort);
        while ((m = GT_GetIMsg(rw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int changed = 0, rc = OWF_OK;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; continue; }
            if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); continue; }
            if (class != IDCMP_GADGETUP) continue;
            switch (gg->GadgetID) {
            case R_LIST:
            case R_COLUMN: {
                char cell[128] = "";
                if (gg->GadgetID == R_LIST) sel = code;
                else col = code;
                if (sel >= 0 && sel < DOC->plan->nresources) opj_resource_cell_text(DOC, sel, col, cell, sizeof cell);
                GT_SetGadgetAttrs(g_val, rw, NULL, GTST_String, (ULONG)cell, TAG_DONE);
                break;
            }
            case R_ADD: {
                char name[100] = "";
                int at = -1;
                if (ask_text("Add Resource", "Name", name, sizeof name)) {
                    if ((rc = opj_add_resource(DOC, name, &at)) == OWF_OK) sel = at;
                    changed = 1;
                }
                break;
            }
            case R_DELETE:
                if (sel >= 0 && sel < DOC->plan->nresources) {
                    rc = opj_delete_resources(DOC, sel, 1);
                    if (sel >= DOC->plan->nresources) sel = DOC->plan->nresources - 1;
                    changed = 1;
                }
                break;
            case R_VALUE:
            case R_SET:
                if (sel >= 0 && sel < DOC->plan->nresources) {
                    STRPTR str = NULL;
                    GT_GetGadgetAttrs(g_val, rw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                    rc = opj_set_resource_cell(DOC, sel, col, str ? (const char *)str : "");
                    changed = 1;
                }
                break;
            }
            if (rc != OWF_OK) {
                struct Window *keep = win;
                win = rw;
                edit_failed(rc, opj_resource_column_title(col));
                win = keep;
            }
            if (changed) {
                GT_SetGadgetAttrs(g_list, rw, NULL, GTLV_Labels, ~0, TAG_DONE);
                res_build();
                GT_SetGadgetAttrs(g_list, rw, NULL, GTLV_Labels, (ULONG)&res_list,
                                  GTLV_Selected, sel < 0 ? ~0 : (ULONG)sel, TAG_DONE);
                redraw();
            }
        }
    }
    RemoveGList(rw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(rw);
    res_free();
}

/* ---- Tools > Assign Resources... (MS Project's dialog): every resource against the selected task ---- */

static struct List as_list;
static struct Node *as_nodes;
static char (*as_text)[80];

static void as_free(void)
{
    free(as_nodes);
    free(as_text);
    as_nodes = NULL;
    as_text = NULL;
    NewList(&as_list);
}

static void as_build(int task)
{
    int i, n = DOC->plan->nresources;
    as_free();
    if (!n) return;
    as_nodes = (struct Node *)calloc((size_t)n, sizeof *as_nodes);
    as_text = (char (*)[80])calloc((size_t)n, sizeof *as_text);
    if (!as_nodes || !as_text) { as_free(); return; }
    for (i = 0; i < n; i++) {
        opj_assign_line(DOC, task, i, as_text[i], sizeof as_text[i]);
        as_nodes[i].ln_Name = as_text[i];
        AddTail(&as_list, &as_nodes[i]);
    }
}

static void assign_window(void)
{
    enum { A_LIST = 1, A_UNITS, A_ASSIGN, A_REMOVE, A_REPLACE, A_ADD, A_CLOSE };
    static const char *const labels[] = { "Assign", "Remove", "Replace...", "Add Resource...", "Close" };
    struct Window *aw;
    struct Gadget *eg = NULL, *g, *g_list, *g_units, *g_info;
    struct NewGadget ng;
    char title[80], head[80], info[120] = "Choose a resource, then Assign, Remove or Replace";
    int task = view.selected, done = 0, sel = -1, w = fw * 52, lines = 10, h, i, by, bw;
    if (rexx_busy || !edit_allowed(OPJ_EDIT_INFO)) return;
    if (task < 0 || task >= DOC->plan->ntasks) { set_status("Assign Resources: choose a task first"); redraw(); return; }
    snprintf(title, sizeof title, "Assign Resources: %.50s", DOC->plan->tasks[task].name ? DOC->plan->tasks[task].name : "");
    snprintf(head, sizeof head, "  %-20s %8s %10s", "Resource Name", "Units", "Cost");
    NewList(&as_list);
    as_build(task);
    h = (fh + 2) * (lines + 1) + (fh + 8) * 3 + 24;
    aw = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 40, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | STRINGIDCMP | BUTTONIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!aw) { as_free(); return; }
    SetFont(aw->RPort, font);
    if (!(g = CreateContext(&eg))) { CloseWindow(aw); as_free(); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_LeftEdge = aw->BorderLeft + 8;
    ng.ng_TopEdge = aw->BorderTop + 2;
    ng.ng_Width = w - 16;
    ng.ng_Height = fh + 4;
    g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)head, GTTX_CopyText, TRUE, TAG_DONE);
    ng.ng_TopEdge = aw->BorderTop + 6 + fh + 2;
    ng.ng_Height = (fh + 2) * lines + 4;
    ng.ng_GadgetID = A_LIST;
    g = g_list = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&as_list, GTLV_ShowSelected, 0, TAG_DONE);
    by = ng.ng_TopEdge + ng.ng_Height + 6;
    ng.ng_TopEdge = by;
    ng.ng_LeftEdge = aw->BorderLeft + 8 + fw * 6 + 8;
    ng.ng_Width = fw * 12 + 12;
    ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (UBYTE *)"Units";
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = A_UNITS;
    g = g_units = CreateGadget(STRING_KIND, g, &ng, GTST_MaxChars, 20, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 8;
    ng.ng_Width = w - 16 - (ng.ng_LeftEdge - aw->BorderLeft - 8);
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = 0;
    g = g_info = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)info, GTTX_CopyText, TRUE, TAG_DONE);
    ng.ng_TopEdge = by + fh + 12;
    ng.ng_LeftEdge = aw->BorderLeft + 8;
    bw = (w - 16 - 4 * 6) / 5;
    ng.ng_Width = bw;
    ng.ng_Flags = PLACETEXT_IN;
    for (i = 0; i < 5; i++) {
        ng.ng_GadgetText = (UBYTE *)labels[i];
        ng.ng_GadgetID = A_ASSIGN + i;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        ng.ng_LeftEdge += bw + 6;
    }
    if (!g) { FreeGadgets(eg); CloseWindow(aw); as_free(); return; }
    AddGList(aw, eg, ~0, -1, NULL);
    RefreshGList(eg, aw, NULL, -1);
    GT_RefreshWindow(aw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(aw->UserPort);
        while ((m = GT_GetIMsg(aw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int changed = 0, rc = OWF_OK;
            STRPTR units = NULL;
            const char *what = "Assign";
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; continue; }
            if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(aw); GT_EndRefresh(aw, TRUE); continue; }
            if (class != IDCMP_GADGETUP) continue;
            GT_GetGadgetAttrs(g_units, aw, NULL, GTST_String, (ULONG)&units, TAG_DONE);
            switch (gg->GadgetID) {
            case A_LIST: {
                opj_assign_info ai;
                char u[32] = "";
                sel = code;
                if (opj_assign_get(DOC, task, sel, &ai) && ai.assigned) opj_units_text(DOC, sel, ai.units, u, sizeof u);
                GT_SetGadgetAttrs(g_units, aw, NULL, GTST_String, (ULONG)u, TAG_DONE);
                break;
            }
            case A_UNITS:
            case A_ASSIGN:
                if (sel < 0) break;
                rc = opj_assign_resource(DOC, task, sel, units ? (const char *)units : "");
                if (rc == OWF_OK) snprintf(info, sizeof info, "%.40s assigned", DOC->plan->resources[sel].name);
                changed = 1;
                break;
            case A_REMOVE:
                if (sel < 0) break;
                what = "Remove";
                rc = opj_remove_resource(DOC, task, sel);
                if (rc == OWF_OK) snprintf(info, sizeof info, "%.40s removed", DOC->plan->resources[sel].name);
                changed = 1;
                break;
            case A_REPLACE: {
                char name[100] = "";
                int to, kind;
                if (sel < 0) break;
                what = "Replace";
                if (!ask_text("Replace", "With (a resource's name)", name, sizeof name)) break;
                if ((to = opj_find_resource(DOC, name)) < 0) {
                    snprintf(info, sizeof info, "No resource %.40s", name);
                    changed = 1;
                    break;
                }
                kind = DOC->plan->resources[sel].type != DOC->plan->resources[to].type;
                rc = opj_replace_resource(DOC, task, sel, to, units ? (const char *)units : "");
                if (rc == OWF_OK) {
                    snprintf(info, sizeof info, kind && !(units && *units) ? "%.30s replaced: one unit, as it is another kind"
                                                                           : "%.30s replaced", DOC->plan->resources[sel].name);
                    sel = to;
                }
                changed = 1;
                break;
            }
            case A_ADD: {
                char name[100] = "";
                int at = -1;
                what = "Add Resource";
                if (!ask_text("Add Resource", "Name", name, sizeof name)) break;
                if ((rc = opj_add_resource(DOC, name, &at)) == OWF_OK) sel = at;
                changed = 1;
                break;
            }
            case A_CLOSE: done = 1; break;
            }
            if (rc != OWF_OK) {
                struct Window *keep = win;
                win = aw;
                edit_failed(rc, what);
                win = keep;
            }
            if (changed) {
                GT_SetGadgetAttrs(g_list, aw, NULL, GTLV_Labels, ~0, TAG_DONE);
                as_build(task);
                GT_SetGadgetAttrs(g_list, aw, NULL, GTLV_Labels, (ULONG)&as_list,
                                  GTLV_Selected, sel < 0 ? ~0 : (ULONG)sel, TAG_DONE);
                GT_SetGadgetAttrs(g_info, aw, NULL, GTTX_Text, (ULONG)info, TAG_DONE);
                redraw();
            }
        }
    }
    RemoveGList(aw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(aw);
    as_free();
}

/* ---- Menus, mouse and keys ---- */

/* ---- Tools > Level Resources... ---- */

/* The Level Resources requester, as ProjectLibre's and MS Project's Resource Leveling dialog: the leveling
 * order, "Level only within available slack", then Level Now, Clear Leveling or Cancel. The choices stay
 * for the next time. 1 Level Now, 2 Clear Leveling, 0 Cancel. */
static int level_order = OWF_LEVEL_STANDARD, level_within;

static int level_dialog(void)
{
    static STRPTR orders[OWF_LEVEL_NORDERS + 1];
    struct Window *aw;
    struct Gadget *eg = NULL, *g, *slack_g;
    struct NewGadget ng;
    int i, done = 0, answer = 0, order = level_order, lw = fw * 15 + 12, w = fw * 56, h = (fh + 8) * 4 + 16;
    for (i = 0; i < OWF_LEVEL_NORDERS; i++) orders[i] = (STRPTR)opj_level_order_name(i);
    orders[OWF_LEVEL_NORDERS] = NULL;
    aw = OpenWindowTags(NULL, WA_Title, (ULONG)"Level Resources", WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | BUTTONIDCMP |
                  CYCLEIDCMP | CHECKBOXIDCMP,
        TAG_DONE);
    if (!aw) return 0;
    if (!(g = CreateContext(&eg))) { CloseWindow(aw); return 0; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_Height = fh + 6;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = aw->BorderLeft + 8 + lw;
    ng.ng_TopEdge = aw->BorderTop + 8;
    ng.ng_Width = w - 16 - lw;
    ng.ng_GadgetText = (UBYTE *)"Leveling order";
    ng.ng_GadgetID = 10;
    g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)orders, GTCY_Active, (ULONG)order, TAG_DONE);
    ng.ng_TopEdge += fh + 8;
    ng.ng_Flags = PLACETEXT_RIGHT;
    ng.ng_LeftEdge = aw->BorderLeft + 8;
    ng.ng_Width = 26;
    ng.ng_GadgetText = (UBYTE *)"Level only within available slack";
    ng.ng_GadgetID = 11;
    g = slack_g = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, (ULONG)level_within, TAG_DONE);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = aw->Height - aw->BorderBottom - 6 - (fh + 6);
    ng.ng_Width = (w - 32) / 3;
    ng.ng_GadgetText = (UBYTE *)"Level Now";
    ng.ng_GadgetID = 1;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 8;
    ng.ng_GadgetText = (UBYTE *)"Clear Leveling";
    ng.ng_GadgetID = 2;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 8;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = 3;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(aw); return 0; }
    AddGList(aw, eg, ~0, -1, NULL);
    RefreshGList(eg, aw, NULL, -1);
    GT_RefreshWindow(aw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(aw->UserPort);
        while ((m = GT_GetIMsg(aw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) done = 1;
            else if (class == IDCMP_VANILLAKEY && code == 13) answer = 1, done = 1;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(aw); GT_EndRefresh(aw, TRUE); }
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 10) order = code;
            else if (class == IDCMP_GADGETUP && gg->GadgetID >= 1 && gg->GadgetID <= 3)
                answer = gg->GadgetID == 3 ? 0 : gg->GadgetID, done = 1;
        }
    }
    if (answer) {
        ULONG on = 0;
        GT_GetGadgetAttrs(slack_g, aw, NULL, GTCB_Checked, (ULONG)&on, TAG_DONE);
        level_within = on != 0;
        level_order = order >= 0 && order < OWF_LEVEL_NORDERS ? order : OWF_LEVEL_STANDARD;
    }
    RemoveGList(aw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(aw);
    return answer;
}

/* Levels the plan in the requester's order, the whole of it or only within slack, with the busy pointer
 * while it works (each move schedules the plan again, which takes a while on a large one). */
static void level_resources(void)
{
    char msg[96];
    int how, delayed = 0, rc, over = 0, i;
    if (rexx_busy) return;
    how = level_dialog();
    redraw();
    if (!how) return;
    if (how == 2) {
        rc = opj_clear_leveling(DOC);
        if (rc == OWF_OK) set_status("Leveling cleared");
        else edit_failed(rc, "Clear Leveling");
        return;
    }
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    rc = opj_level_by(DOC, level_within, level_order, &delayed);
    SetWindowPointer(win, TAG_DONE);
    if (rc != OWF_OK) { edit_failed(rc, "Level Resources"); return; }
    for (i = 0; i < DOC->plan->nresources; i++) over += DOC->plan->resources[i].overallocated != 0;
    snprintf(msg, sizeof msg, "Level Resources: %d task%s delayed%s", delayed, delayed == 1 ? "" : "s",
             over ? "; some over-allocation no move can mend" : "");
    set_status(msg);
}

/* ---- Tools > Update Tasks... (the selected task's progress in one Undo) ---- */

static void update_tasks(void)
{
    static const int cols[5] = { OPJ_COL_PERCENT, OPJ_COL_ACTUAL_DURATION, OPJ_COL_REMAINING_DURATION,
                                 OPJ_COL_ACTUAL_START, OPJ_COL_ACTUAL_FINISH };
    static const char *const labels[5] = { "% Complete", "Actual duration", "Remaining duration",
                                           "Actual start (NA: none)", "Actual finish (NA: none)" };
    char was[5][80], now[5][80], msg[200];
    const char *given[5];
    int row = view.selected, i, rc, summary;
    if (rexx_busy) return;
    if (row < 0 || row >= DOC->plan->ntasks) { set_status("Update Tasks: select a task first"); return; }
    summary = DOC->plan->tasks[row].summary;
    for (i = 0; i < 5; i++) {
        char *text = opj_cell_text_dup(DOC, row, cols[i]);
        put_text(was[i], sizeof was[i], text ? text : "");
        free(text);
        put_text(now[i], sizeof now[i], was[i]);
        given[i] = NULL;
    }
    for (i = 0; i < (summary ? 1 : 5); i++) {    /* a summary rolls up its subtasks: % Complete only */
        if (!ask_text("Update Tasks", labels[i], now[i], sizeof now[i])) return;
        if (strcmp(now[i], was[i])) given[i] = now[i];
    }
    if (!given[0] && !given[1] && !given[2] && !given[3] && !given[4]) return;
    rc = opj_update_task(DOC, row, given[0], given[1], given[2], given[3], given[4]);
    if (rc != OWF_OK) { edit_failed(rc, "Update Tasks"); return; }
    {
        char *pc = opj_cell_text_dup(DOC, row, OPJ_COL_PERCENT);
        snprintf(msg, sizeof msg, "Update Tasks: %.80s now %s complete", DOC->plan->tasks[row].name
                 ? DOC->plan->tasks[row].name : "the task", pc ? pc : "?");
        free(pc);
    }
    set_status(msg);
}

/* ---- Tools > Update Project... ---- */

static owf_ptime today(void);

/* The status date (the last one, or today at the end of its working day) and how to count work done. */
static void update_project(void)
{
    char date[40], msg[96];
    owf_ptime status;
    int how, changed = 0, rc;
    if (rexx_busy) return;
    opj_date_text(DOC->plan->has_status_date ? DOC->plan->status_date : today() + 9L * 60L, 1, date);
    for (;;) {
        if (!ask_text("Update Project", "Work complete through", date, sizeof date)) return;
        if (opj_date_parse(date, 17 * 60, &status)) break;
        tell("That is not a date: type it as 20/11/26 or 20/11/26 17:00");
    }
    how = ask("Set tasks started by then to", "0% - 100% complete|0% or 100% only|Cancel");
    if (!how) return;
    rc = opj_update_project(DOC, status, how == 2, &changed);
    if (rc != OWF_OK) { edit_failed(rc, "Update Project"); return; }
    snprintf(msg, sizeof msg, "Update Project: %d task%s brought up to date", changed, changed == 1 ? "" : "s");
    set_status(msg);
}

/* ---- Tools > Change Working Time..., Plan Calendar..., Task Calendar... ---- */

static const char *cal_name(int cal)
{
    const owf_project *p = DOC->plan;
    return cal >= 0 && cal < p->ncalendars && p->calendars[cal].name ? p->calendars[cal].name : "Standard";
}

/* Tools > Set Baseline... and Clear Baseline...: which one, Baseline (0) or Baseline 1 to 10, for the whole plan. */
static void baseline_n(int clear)
{
    const char *title = clear ? "Clear Baseline" : "Set Baseline";
    char saved[40], prompt[80], which[24] = "0", done[64];
    int n, rc;
    opj_baselines_text(DOC, saved, sizeof saved);
    snprintf(prompt, sizeof prompt, "Baseline, 0 to 10 (saved: %s)", saved);
    for (;;) {
        if (!ask_text(title, prompt, which, sizeof which)) return;
        if ((n = opj_baseline_parse(which)) >= 0) break;
        tell("The baseline is 0 (Baseline) or 1 to 10.");
    }
    rc = clear ? opj_clear_baseline_n(DOC, n, 0, -1) : opj_set_baseline_n(DOC, n, 0, -1);
    edit_failed(rc, title);
    if (rc != OWF_OK) return;
    if (!clear && n == 0) view.show_baseline = 1;
    if (n) snprintf(done, sizeof done, clear ? "Baseline %d cleared" : "Baseline %d set", n);
    else snprintf(done, sizeof done, clear ? "Baseline cleared" : "Baseline set");
    set_status(done);
}

/* A calendar by the name typed (default: def's); one not in the plan is made, a copy of the plan's, when
 * the user says so (make set). -1 when cancelled. */
static int ask_calendar(const char *title, int def, int make)
{
    char name[64], msg[160];
    int cal, rc;
    put_text(name, sizeof name, cal_name(def));
    for (;;) {
        if (!ask_text(title, "Calendar", name, sizeof name)) return -1;
        if ((cal = opj_find_calendar(DOC->plan, name)) >= 0) return cal;
        if (!make) {
            snprintf(msg, sizeof msg, "There is no calendar %.60s", name);
            tell(msg);
            continue;
        }
        snprintf(msg, sizeof msg, "There is no calendar %.40s.\nMake it, a copy of %.40s?", name, cal_name(DOC->plan->calendar));
        if (!ask(msg, "Make it|Cancel")) continue;
        rc = opj_add_calendar(DOC, name, DOC->plan->calendar, &cal);
        if (rc == OWF_OK) return cal;
        tell(rc == OWF_ERR_MEMORY ? "Out of memory" : "A calendar's name can't be empty or a number");
    }
}

static void change_working_time(void)
{
    static char days[48] = "Mon";
    char title[96], hours[96];
    int cal, rc;
    if (rexx_busy || (cal = ask_calendar("Change Working Time", DOC->plan->calendar, 1)) < 0) return;
    snprintf(title, sizeof title, "Change Working Time: %.60s", cal_name(cal));
    for (;;) {
        if (!ask_text(title, "Days (Mon, or 24/12/26 - 1/1/27)", days, sizeof days)) return;
        if (opj_days_parse(days, NULL, NULL, NULL)) break;
        tell("Type a weekday (Mon, Friday) or dates (24/12/26, or 24/12/26 - 1/1/27)");
    }
    opj_days_hours_text(DOC->plan, cal, days, hours);
    for (;;) {
        if (!ask_text(title, "Hours (08:00-12:00, 13:00-17:00, off, default)", hours, sizeof hours)) return;
        if ((rc = opj_set_days_hours(DOC, cal, days, hours)) == OWF_OK) break;
        if (rc == OWF_ERR_MEMORY) { edit_failed(rc, "Change Working Time"); return; }
        tell("Hours are spans in order, 08:00-12:00, 13:00-17:00, or off;\ndefault gives dates their week's hours again");
    }
    {
        char done[160];                  /* 60 + 24 + 60 and the words between */
        snprintf(done, sizeof done, "%.60s: %.24s now %.60s", cal_name(cal), days, hours);
        set_status(done);
    }
}

static void plan_calendar(void)
{
    int cal;
    if (rexx_busy || (cal = ask_calendar("Plan Calendar", DOC->plan->calendar, 0)) < 0) return;
    edit_failed(opj_set_plan_calendar(DOC, cal), "Plan Calendar");
}

/* The selected task's own calendar; an empty name gives it the plan's again. */
static void task_calendar(void)
{
    char name[64], prompt[96];
    int row = view.selected, cal;
    if (rexx_busy) return;
    if (row < 0 || row >= DOC->plan->ntasks) { set_status("Task Calendar: choose a task first"); return; }
    if (DOC->plan->tasks[row].summary) { set_status("Task Calendar: a summary works its subtasks' calendars"); return; }
    put_text(name, sizeof name, DOC->plan->tasks[row].calendar < 0 ? "" : cal_name(DOC->plan->tasks[row].calendar));
    snprintf(prompt, sizeof prompt, "Calendar (empty: the plan's)");
    for (;;) {
        if (!ask_text("Task Calendar", prompt, name, sizeof name)) return;
        cal = name[0] ? opj_find_calendar(DOC->plan, name) : -1;
        if (!name[0] || cal >= 0) break;
        tell("There is no such calendar: Change Working Time... makes one");
    }
    edit_failed(opj_set_task_calendar(DOC, row, cal), "Task Calendar");
}

/* ---- Sort, Filter, Group By, Find and Replace (View and Edit menus; opj_core.c, opj_filter.c) ---- */

/* Sorts by keys, keeping the selected task selected. */
static void sort_by(const opj_sort_key *keys, int n)
{
    int uid = view.selected >= 0 && view.selected < DOC->plan->ntasks ? DOC->plan->tasks[view.selected].uid : 0;
    int rc = opj_sort_tasks(DOC, keys, n), i;
    if (rc != OWF_OK) {
        edit_failed(rc, "Sort");
        return;
    }
    for (i = 0; uid && i < DOC->plan->ntasks; i++)
        if (DOC->plan->tasks[i].uid == uid) view.selected = i;
    set_status("Sorted");
}

static void sort_one(int column)
{
    opj_sort_key k;
    k.column = column;
    k.descending = 0;
    sort_by(&k, 1);
}

/* Sort By...: up to three columns typed, "Start, Duration desc, Name". */
static void sort_dialog(void)
{
    static char keys[120] = "Start, Name";
    opj_sort_key k[3];
    int n;
    for (;;) {
        if (!ask_text("Sort By", "Columns (desc after one: down)", keys, sizeof keys)) return;
        if (opj_sort_parse(keys, k, &n) == OWF_OK) break;
        tell("Sort By takes up to three column titles, each with desc after it to sort down,\nas \"Start, Duration desc, Name\"");
    }
    sort_by(k, n);
}

/* The lines' filter or group changed: back to the top, the selection on show. */
static void lines_changed(const char *what)
{
    char msg[120];
    view.first_row = 0;
    view_for_doc();                     /* works the lines out once; none (every task shown) on no memory */
    if (!view.lines) {
        edit_failed(OWF_ERR_MEMORY, what);
        return;
    }
    keep_selection_shown();
    if (lines.group >= 0) {
        int i, groups = 0;
        for (i = 0; i < lines.n; i++) groups += lines.task[i] < 0;
        snprintf(msg, sizeof msg, "Grouped by %s: %d group%s", opj_column_title(lines.group), groups, groups == 1 ? "" : "s");
    } else if (lines.filter.kind != OPJ_FILTER_ALL) {
        snprintf(msg, sizeof msg, "%s: %d of %d tasks", lines.filter.kind == OPJ_FILTER_CELL ? "AutoFilter"
                 : opj_filter_name(lines.filter.kind), lines.n, DOC->plan->ntasks);
    } else msg[0] = 0;
    set_status(msg);
}

/* The AutoFilter on a column: tasks whose cell there contains what is typed (nothing: every task). */
static void autofilter(int column)
{
    char text[sizeof lines.filter.text], prompt[80];
    if (column < 0 || column >= OPJ_NCOLS) return;
    put_text(text, sizeof text, lines.filter.kind == OPJ_FILTER_CELL && lines.filter.column == column ? lines.filter.text : "");
    snprintf(prompt, sizeof prompt, "%.40s contains (empty: all)", opj_column_title(column));
    if (!ask_text("AutoFilter", prompt, text, sizeof text)) return;
    if (text[0]) {
        lines.filter.kind = OPJ_FILTER_CELL;
        lines.filter.column = column;
        lines.filter.how = OPJ_FIND_CONTAINS;
        put_text(lines.filter.text, sizeof lines.filter.text, text);
    } else lines.filter.kind = OPJ_FILTER_ALL;
    lines_changed("AutoFilter");
}

/* Group By Column...: any column by its title. */
static void group_dialog(void)
{
    char name[60];
    int col;
    put_text(name, sizeof name, lines.group >= 0 ? opj_column_title(lines.group) : "");
    for (;;) {
        if (!ask_text("Group By", "Column (empty: no group)", name, sizeof name)) return;
        if (!name[0] || (col = opj_column_parse(name, 0)) >= 0) break;
        tell("There is no such column: View > Columns lists them");
    }
    lines.group = name[0] ? col : -1;
    lines_changed("Group By");
}

/* Find Next: the next task after the selection whose find_col cell has find_text, among the tasks shown
 * (going round the end); in the Resource Usage view, the next resource by name. */
static void find_next(void)
{
    char msg[160];
    int from, k, r;
    if (!find_text[0]) {
        set_status("Find: Edit > Find... first");
        return;
    }
    if (usage_mode == OPJ_VIEW_USAGE) {
        int res = -1, a, row;
        if (usage_sel >= 0) opj_usage_row(DOC, usage_sel, &res, &a);
        if ((r = opj_find_resource_row(DOC, OPJ_RCOL_NAME, find_text, OPJ_FIND_CONTAINS, res, 0)) < 0) {
            snprintf(msg, sizeof msg, "Find: no resource has %.60s", find_text);
            set_status(msg);
            return;
        }
        for (row = 0; row < usage_rows(); row++)
            if (opj_usage_row(DOC, row, &res, &a) && res == r && a < 0) { move_usage(row - (usage_sel < 0 ? 0 : usage_sel)); break; }
        return;
    }
    if (usage_mode) {
        set_status("Find: in the Gantt chart, the Network Diagram or Resource Usage");
        return;
    }
    from = view.selected;
    for (k = 0; k < DOC->plan->ntasks; k++) {           /* past the matches the filter hides */
        r = opj_find_task(DOC, find_col, find_text, OPJ_FIND_CONTAINS, from, 0);
        if (r < 0 || (k && r == view.selected)) break;
        if (network_mode || opj_view_task_line(&view, DOC, r) >= 0) {
            view.selected = r;
            if (network_mode) opj_network_reveal(&view, &net, r);
            else keep_selection_shown();
            snprintf(msg, sizeof msg, "Found %.60s in task %d", find_text, r + 1);
            set_status(msg);
            return;
        }
        from = r;
    }
    snprintf(msg, sizeof msg, "Find: no %.40s %.60s%s", opj_column_title(find_col), find_text,
             opj_lines_active(&lines) ? " among the tasks shown" : "");
    set_status(msg);
}

/* Find...: what to look for, in the selected column (Task Name when none), then the first one. */
static void find_dialog(void)
{
    char prompt[80];
    if (!usage_mode) find_col = view.columns[sel_col] ? sel_col : OPJ_COL_NAME;
    snprintf(prompt, sizeof prompt, "Find in %.40s", usage_mode == OPJ_VIEW_USAGE ? "Resource Name" : opj_column_title(find_col));
    if (!ask_text("Find", prompt, find_text, sizeof find_text) || !find_text[0]) return;
    find_next();
}

/* Replace...: in the selected column of every task (shown or not), as Replace All; one Undo step. */
static void replace_dialog(void)
{
    static char with[80];
    char prompt[80], msg[200];
    int changed = 0, refused = 0, rc;
    find_col = view.columns[sel_col] ? sel_col : OPJ_COL_NAME;
    if (!opj_column_typed(find_col)) {
        set_status("Replace: that column is worked out; choose one you can type in");
        return;
    }
    snprintf(prompt, sizeof prompt, "Find in %.40s", opj_column_title(find_col));
    if (!ask_text("Replace", prompt, find_text, sizeof find_text) || !find_text[0]) return;
    if (!ask_text("Replace", "Replace with", with, sizeof with)) return;
    rc = opj_replace_text(DOC, find_col, find_text, with, 0, -1, &changed, &refused);
    if (rc == OWF_ERR_FORMAT && refused) {
        snprintf(msg, sizeof msg, "Replace: %d cell%s wouldn't take the new text, so nothing changed", refused,
                 refused == 1 ? "" : "s");
        tell(msg);
        return;
    }
    if (rc != OWF_OK) {
        edit_failed(rc, "Replace");
        return;
    }
    snprintf(msg, sizeof msg, changed ? "Replaced in %d cell%s" : "Replace: nothing to replace", changed, changed == 1 ? "" : "s");
    set_status(msg);
}

static void set_zoom(int zoom)
{
    owf_ptime mid = opj_view_time(&view, opj_chart_left(&view));
    view.zoom = zoom;
    view.origin = opj_period_start(zoom, mid);
    redraw();
}


static void menu_action(ULONG id, struct MenuItem *item)
{
    char path[512], why[200];
    set_status("");
    {   /* the task edits act on the Gantt chart's selection, which the Resource Usage view hides */
        int edit = id == M_EDIT ? OPJ_EDIT_CELL : id == M_INSERT ? OPJ_EDIT_INSERT : id == M_DELETE ? OPJ_EDIT_DELETE
                 : id == M_INDENT ? OPJ_EDIT_INDENT : id == M_OUTDENT ? OPJ_EDIT_OUTDENT : id == M_LINK ? OPJ_EDIT_LINK
                 : id == M_UNLINK ? OPJ_EDIT_UNLINK : id == M_INFO ? OPJ_EDIT_INFO
                 : id == M_REPLACE || id == M_UPDATE_TASKS ? OPJ_EDIT_CELL : OPJ_EDIT_NONE;
        if (!edit_allowed(edit)) return;
    }
    if (id >= M_FILTER && id < M_FILTER + NFILTER_ITEMS) {     /* View > Filter: a filter, ticked alone */
        lines.filter.kind = (int)(id - M_FILTER);
        lines_changed("Filter");
        tick_view();
        redraw();
        return;
    }
    if (id >= M_GROUP && id < M_GROUP + NGROUP_ITEMS) {
        lines.group = group_cols[id - M_GROUP];
        lines_changed("Group By");
        tick_view();
        redraw();
        return;
    }
    if (id >= M_COLUMN && id < M_COLUMN + OPJ_NCOLS) {
        int column = (int)(id - M_COLUMN), show = item && (item->Flags & CHECKED) ? 1 : 0;
        if (!opj_view_show_column(&view, column, show)) set_status("The table keeps one column");
        sel_col = opj_view_near_column(&view, sel_col);
        tick_view();
        redraw();
        return;
    }
    switch (id) {
    case M_NEW:
        if (may_drop() && run("NEW FORCE", NULL, 0) != OPJ_RX_OK) tell(rx.lasterror);
        break;
    case M_OPEN:
        if (may_drop() && ask_file("Open a plan", FALSE, "#?.(gfproj|xml|csv|mpx|gan)", path, sizeof path)) {
            open_file(path);
            return;
        }
        break;
    case M_SAVE: save(0); return;
    case M_SAVEAS: save(1); return;
    case M_EXPORT_XML: export_as("MSPDI", "#?.xml"); return;
    case M_EXPORT_CSV: export_as("CSV", "#?.csv"); return;
    case M_EXPORT_PDF: export_as("PDF", "#?.pdf"); return;
    case M_PRINT:
        if (!print_plan(why, sizeof why)) tell(why);
        else set_status(why);
        break;
    case M_ABOUT:
        tell(VERSION_TEXT "\nThe project planner of the GFOffice Suite.\n\nPlans in .gfproj, MSPDI XML and CSV;\n"
             "MPX and .gan read; the Gantt chart exported as PDF;\nthe Tracking Gantt, Resource Usage, Task Usage, Resource Graph and\nNetwork Diagram views.\nMIT, Copyright (c) 2026 AmiCradle Team");
        break;
    case M_QUIT: if (may_drop()) quit_now = 1; break;
    case M_UNDO: undo_redo(0); return;
    case M_REDO: undo_redo(1); return;
    case M_EDIT: edit_cell(); return;
    case M_INFO: task_info(); return;
    case M_INSERT: insert_task(); return;
    case M_DELETE: delete_task(); return;
    case M_INDENT:
    case M_OUTDENT:
        if (view.selected >= 0) edit_failed(opj_indent(DOC, view.selected, 1, id == M_INDENT ? 1 : -1),
                                            id == M_INDENT ? "Indent" : "Outdent");
        break;
    case M_LINK:
        if (view.selected >= 0) edit_failed(opj_link_rows(DOC, view.selected, 2), "Link");
        break;
    case M_UNLINK:
        if (view.selected >= 0) edit_failed(opj_unlink_rows(DOC, view.selected, 1), "Unlink");
        break;
    case M_DAYS: set_zoom(OPJ_ZOOM_DAYS); return;
    case M_WEEKS: set_zoom(OPJ_ZOOM_WEEKS); return;
    case M_MONTHS: set_zoom(OPJ_ZOOM_MONTHS); return;
    case M_QUARTERS: set_zoom(OPJ_ZOOM_QUARTERS); return;
    case M_SHOW_BASELINE: view.show_baseline = item && (item->Flags & CHECKED) ? 1 : 0; break;
    case M_START: view.origin = opj_period_start(OPJ_ZOOM_WEEKS, DOC->plan->start); break;
    case M_RESOURCES: resources_window(); break;
    case M_ASSIGN: assign_window(); break;
    case M_GANTT: usage_mode = network_mode = view.tracking = 0; set_status(""); break;
    case M_TRACKING:
        usage_mode = network_mode = 0;
        view.tracking = 1;
        if (DOC->plan->ntasks && !DOC->plan->tasks[0].baselined) set_status("No baseline yet: Tools > Set Baseline keeps one");
        break;
    case M_NETWORK:
        usage_mode = 0;
        network_mode = 1;
        opj_network_free(&net);
        if (opj_network_layout(DOC, &net) == OWF_OK && view.selected >= 0) opj_network_reveal(&view, &net, view.selected);
        break;
    case M_TASK_USAGE: network_mode = 0; show_usage(OPJ_VIEW_TASK_USAGE); break;
    case M_GRAPH:                                       /* up and down choose the resource */
        network_mode = 0;
        show_usage(OPJ_VIEW_GRAPH);
        if (!DOC->plan->nresources) set_status("No resources yet: Resources... adds them");
        break;
    case M_USAGE:
        network_mode = 0;
        show_usage(OPJ_VIEW_USAGE);
        if (!DOC->plan->nresources) set_status("No resources yet: Resources... adds them");
        break;
    case M_SET_BASELINE: baseline_n(0); break;
    case M_CLEAR_BASELINE: baseline_n(1); break;
    case M_LEVEL: level_resources(); break;
    case M_CLEAR_LEVELING: {
        int rc = opj_clear_leveling(DOC);
        if (rc == OWF_OK) set_status("Leveling cleared");
        else edit_failed(rc, "Clear Leveling");
        break;
    }
    case M_FIND: find_dialog(); break;
    case M_FIND_NEXT: find_next(); break;
    case M_REPLACE: replace_dialog(); break;
    case M_SORT_START: sort_one(OPJ_COL_START); break;
    case M_SORT_FINISH: sort_one(OPJ_COL_FINISH); break;
    case M_SORT_NAME: sort_one(OPJ_COL_NAME); break;
    case M_SORT_DURATION: sort_one(OPJ_COL_DURATION); break;
    case M_SORT_BY: sort_dialog(); break;
    case M_AUTOFILTER_ROW:
        autofilter_row = item && (item->Flags & CHECKED) ? 1 : 0;
        set_status(autofilter_row ? "AutoFilter: click a column's title to filter on it" : "");
        break;
    case M_AUTOFILTER: autofilter(view.columns[sel_col] ? sel_col : OPJ_COL_NAME); break;
    case M_FILTER_SUMMARIES:
        lines.filter.summaries = item && (item->Flags & CHECKED) ? 1 : 0;
        lines_changed("Filter");
        break;
    case M_GROUP_COLUMN: group_dialog(); break;
    case M_GROUP_DESC:
        lines.group_descending = item && (item->Flags & CHECKED) ? 1 : 0;
        lines_changed("Group By");
        break;
    case M_UPDATE_PROJECT: update_project(); break;
    case M_UPDATE_TASKS: update_tasks(); break;
    case M_PROGRESS_LINES:
        view.progress_lines = item && (item->Flags & CHECKED) ? 1 : 0;
        if (view.progress_lines && !DOC->plan->has_status_date)
            set_status("Progress lines need a status date: Tools > Update Project... sets one");
        break;
    case M_WORKTIME: change_working_time(); break;
    case M_PLAN_CALENDAR: plan_calendar(); break;
    case M_TASK_CALENDAR: task_calendar(); break;
    }
    if (id == M_NEW) tick_view();       /* the new plan's Filter and Group By (All Tasks, No Group) */
    redraw();
}

/* The View menu's Gantt Chart and Resource Usage ticks, after ARexx VIEW changed the view. */
static void tick_view(void)
{
    struct MenuItem *g, *u, *n, *k, *h, *tg;
    int c;
    if (!win || !menu) return;
    g = ItemAddress(menu, FULLMENUNUM(2, 10, NOSUB));
    u = ItemAddress(menu, FULLMENUNUM(2, 11, NOSUB));
    n = ItemAddress(menu, FULLMENUNUM(2, 12, NOSUB));
    k = ItemAddress(menu, FULLMENUNUM(2, 13, NOSUB));
    h = ItemAddress(menu, FULLMENUNUM(2, 14, NOSUB));
    tg = ItemAddress(menu, FULLMENUNUM(2, 15, NOSUB));
    if (!g || !u || !n || !k || !h || !tg) return;
    ClearMenuStrip(win);
    g->Flags &= ~CHECKED;
    u->Flags &= ~CHECKED;
    n->Flags &= ~CHECKED;
    k->Flags &= ~CHECKED;
    h->Flags &= ~CHECKED;
    tg->Flags &= ~CHECKED;
    (usage_mode == OPJ_VIEW_GRAPH ? h : usage_mode == OPJ_VIEW_TASK_USAGE ? k : usage_mode ? u
     : network_mode ? n : view.tracking ? tg : g)->Flags |= CHECKED;
    for (c = 0; c < OPJ_NCOLS; c++) {                       /* View > Columns follows the table */
        struct MenuItem *it = ItemAddress(menu, c < MORE_COLUMNS ? FULLMENUNUM(2, COLUMNS_ITEM, c)
                                                : FULLMENUNUM(2, COLUMNS_ITEM + 1, c - MORE_COLUMNS));
        if (!it) continue;
        if (opj_view_column_shown(&view, c)) it->Flags |= CHECKED;
        else it->Flags &= ~CHECKED;
    }
    for (c = 0; c < NFILTER_ITEMS; c++) {                   /* View > Filter and Group By follow the lines */
        struct MenuItem *it = ItemAddress(menu, FULLMENUNUM(2, DATA_ITEM + 1, c));
        if (!it) continue;
        if (lines.filter.kind == c) it->Flags |= CHECKED;
        else it->Flags &= ~CHECKED;
    }
    for (c = 0; c < NGROUP_ITEMS; c++) {
        struct MenuItem *it = ItemAddress(menu, FULLMENUNUM(2, DATA_ITEM + 2, c));
        if (!it) continue;
        if (lines.group == group_cols[c]) it->Flags |= CHECKED;
        else it->Flags &= ~CHECKED;
    }
    {
        struct MenuItem *sum = ItemAddress(menu, FULLMENUNUM(2, DATA_ITEM + 1, NFILTER_ITEMS + 3));
        struct MenuItem *down = ItemAddress(menu, FULLMENUNUM(2, DATA_ITEM + 2, NGROUP_ITEMS + 2));
        if (sum) sum->Flags = (UWORD)(lines.filter.summaries ? sum->Flags | CHECKED : sum->Flags & ~CHECKED);
        if (down) down->Flags = (UWORD)(lines.group_descending ? down->Flags | CHECKED : down->Flags & ~CHECKED);
    }
    {   /* Tools > Progress Lines follows the view (ARexx PROGRESSLINES too) */
        struct MenuItem *pl = ItemAddress(menu, FULLMENUNUM(3, PROGRESS_ITEM, NOSUB));
        if (pl) pl->Flags = (UWORD)(view.progress_lines ? pl->Flags | CHECKED : pl->Flags & ~CHECKED);
    }
    ResetMenuStrip(win, menu);
}

/* ---- Dragging on the Gantt chart (opj_drag.c works out the edit) ---- */

static opj_drag drag;
static int dragging;
static char drag_shown[80];

static void drag_move(int x, int y)
{
    char text[80];
    if (!dragging) return;
    if (!opj_drag_text(&view, DOC, &drag, x, y, text, sizeof text)) text[0] = 0;
    if (!strcmp(text, drag_shown)) return;          /* redraw only when what the drop would do changes */
    put_text(drag_shown, sizeof drag_shown, text);
    set_status(text);
    redraw();
}

static void drag_drop(int x, int y)
{
    static const char *what[] = { "Move", "Stretch", "% Complete", "Link" };
    char text[80];
    int rc;
    if (!dragging) return;
    dragging = 0;
    drag_shown[0] = 0;
    if (!opj_drag_text(&view, DOC, &drag, x, y, text, sizeof text)) { set_status(""); redraw(); return; }
    rc = opj_drag_end(DOC, &view, &drag, x, y);
    set_status(rc == OWF_OK && DOC->schedule_error ? "The links make a loop: the plan can't be scheduled" : "");
    if (rc != OWF_OK) {
        int k = !strncmp(text, "Link", 4) ? OPJ_DRAG_LINK : drag.kind;
        edit_failed(rc, k >= 0 && k < (int)(sizeof what / sizeof what[0]) ? what[k] : "Drag");
    }
    redraw();
}

static void click(int x, int y, ULONG secs, ULONG micros)
{
    static ULONG last_s, last_m;
    static int last_row = -1, last_col = -1;
    opj_hit hit;
    if (usage_mode) {
        int row = y >= 2 * view.row_height ? usage_first + (y - 2 * view.row_height) / view.row_height : -1;
        (void)secs; (void)micros;
        if (usage_mode == OPJ_VIEW_GRAPH) return;       /* its rows are resources chosen by up and down */
        if (row >= 0 && row < usage_rows()) { usage_sel = row; redraw(); }
        return;
    }
    if (network_mode) {                                 /* a box selects its task; a double-click opens it */
        int row = opj_network_hit(&view, &net, x, y), dbl;
        if (row < 0) return;
        dbl = row == last_row && last_col == -3 && DoubleClick(last_s, last_m, secs, micros);
        view.selected = row;
        last_row = row;
        last_col = -3;                                  /* a box */
        last_s = secs;
        last_m = micros;
        redraw();
        if (dbl) { last_row = -1; task_info(); }
        return;
    }
    opj_hit_test(&view, DOC, x, y, &hit);
    if (hit.what == OPJ_HIT_CELL) {
        int dbl = hit.row == last_row && hit.column == last_col && DoubleClick(last_s, last_m, secs, micros);
        view.selected = hit.row;
        sel_col = hit.column;
        last_row = hit.row;
        last_col = hit.column;
        last_s = secs;
        last_m = micros;
        redraw();
        if (dbl) { last_row = -1; edit_cell(); }
    } else if ((hit.what == OPJ_HIT_BAR || hit.what == OPJ_HIT_CHART) && hit.row >= 0) {
        int dbl = hit.what == OPJ_HIT_BAR && hit.row == last_row && last_col == -2 &&
                  DoubleClick(last_s, last_m, secs, micros);
        view.selected = hit.row;
        last_row = hit.row;
        last_col = -2;                                  /* a bar, not a cell */
        last_s = secs;
        last_m = micros;
        redraw();
        if (dbl) { last_row = -1; task_info(); }
        else if (hit.what == OPJ_HIT_BAR && !rexx_busy && edit_allowed(OPJ_EDIT_CELL))
            dragging = opj_drag_begin(&view, DOC, &hit, x, y, &drag);
    } else if (hit.what == OPJ_HIT_COLUMN_TITLE) {
        if (hit.column >= 0 && hit.column < OPJ_NCOLS) {
            sel_col = hit.column;
            if (autofilter_row && !rexx_busy) autofilter(hit.column);   /* the AutoFilter row: filter on it */
            tick_view();
            redraw();
        }
    }
}

static void events(void)
{
    struct IntuiMessage *m;
    while (win && (m = GT_GetIMsg(win->UserPort)) != NULL) {
        ULONG class = m->Class, secs = m->Seconds, micros = m->Micros;
        UWORD code = m->Code, qual = m->Qualifier;
        WORD mx = m->MouseX - win->BorderLeft, my = m->MouseY - win->BorderTop;   /* in the inner (GZZ) area */
        GT_ReplyIMsg(m);
        switch (class) {
        case IDCMP_CLOSEWINDOW: if (may_drop()) quit_now = 1; break;
        case IDCMP_NEWSIZE: redraw(); break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            GT_EndRefresh(win, TRUE);
            redraw();
            break;
        case IDCMP_MENUPICK:
            while (code != MENUNULL && win && !quit_now) {
                struct MenuItem *item = ItemAddress(menu, code);
                if (!item) break;
                menu_action((ULONG)GTMENUITEM_USERDATA(item), item);
                code = item->NextSelect;
            }
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN) click(mx, my, secs, micros);
            else if (code == SELECTUP) drag_drop(mx, my);
            break;
        case IDCMP_MOUSEMOVE: drag_move(mx, my); break;
        case IDCMP_VANILLAKEY: {
            int edit = opj_edit_for_key(code, (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0);
            if (!edit_allowed(edit)) break;
            if (edit == OPJ_EDIT_CELL) edit_cell();
            else if (edit == OPJ_EDIT_DELETE) delete_task();
            else if ((edit == OPJ_EDIT_INDENT || edit == OPJ_EDIT_OUTDENT) && view.selected >= 0) {
                edit_failed(opj_indent(DOC, view.selected, 1, edit == OPJ_EDIT_OUTDENT ? -1 : 1),
                            edit == OPJ_EDIT_OUTDENT ? "Outdent" : "Indent");
                redraw();
            }
            break;
        }
        case IDCMP_RAWKEY: {
            int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
            if (code == 0x4c) move_selection(shift ? -opj_rows_shown(&view) : -1, 0);
            else if (code == 0x4d) move_selection(shift ? opj_rows_shown(&view) : 1, 0);
            else if (code == 0x4f) { if (shift) scroll_time(-1); else move_selection(0, -1); }
            else if (code == 0x4e) { if (shift) scroll_time(1); else move_selection(0, 1); }
            break;
        }
        }
    }
}

/* ---- ARexx: the GFOPENPROJECT port ---- */

#define REXX_PORT_NAME "GFOPENPROJECT"
static struct MsgPort *rexx_port;
static char rexx_name[20];
static char app_lasterror[256];

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* s starts with word (any case) and then a space or its end; *rest is what follows */
static int starts_word_ci(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    while (is_space(*s)) s++;
    for (i = 0; i < n; ++i) if (!s[i] || lower((unsigned char)s[i]) != lower((unsigned char)word[i])) return 0;
    if (s[n] && !is_space(s[n])) return 0;
    if (rest) { s += n; while (is_space(*s)) s++; *rest = s; }
    return 1;
}

/* VERSION, ACTIVATE, DEACTIVATE, FAULT, PRINT and QUIT are the app's; the rest the core's (opj_rexx.c). */
static int rexx_do_command(const char *command, char *result, size_t size)
{
    const char *arg = "";
    int rc = OPJ_RX_OK, own = 1;
    result[0] = 0;
    if (starts_word_ci(command, "VERSION", &arg)) put_text(result, size, VERSION_TEXT);
    else if (starts_word_ci(command, "ACTIVATE", &arg)) { if (win) { WindowToFront(win); ActivateWindow(win); } }
    else if (starts_word_ci(command, "DEACTIVATE", &arg)) {
        rc = OPJ_RX_NOW; put_text(app_lasterror, sizeof app_lasterror, "DEACTIVATE: GFOpenProject has no icon to go to");
    } else if (starts_word_ci(command, "FAULT", &arg)) {
        if (!*arg) { rc = OPJ_RX_ERROR; put_text(app_lasterror, sizeof app_lasterror, "FAULT needs a code"); }
        else Fault(atol(arg), NULL, (STRPTR)result, (LONG)size);
    } else if (starts_word_ci(command, "PRINT", &arg)) {
        char why[200];
        if (!print_plan(why, sizeof why)) { rc = OPJ_RX_ERROR; put_text(app_lasterror, sizeof app_lasterror, why); }
    } else if (starts_word_ci(command, "VIEW", &arg)) {
        if (!*arg)
            put_text(result, size, usage_mode == OPJ_VIEW_GRAPH ? "GRAPH" : usage_mode == OPJ_VIEW_TASK_USAGE ? "TASKUSAGE"
                                   : usage_mode ? "USAGE" : network_mode ? "NETWORK" : view.tracking ? "TRACKING" : "GANTT");
        else if (starts_word_ci(arg, "GANTT", NULL) || starts_word_ci(arg, "USAGE", NULL) ||
                 starts_word_ci(arg, "NETWORK", NULL) || starts_word_ci(arg, "TASKUSAGE", NULL) ||
                 starts_word_ci(arg, "GRAPH", NULL) || starts_word_ci(arg, "TRACKING", NULL)) {
            network_mode = starts_word_ci(arg, "NETWORK", NULL);
            view.tracking = starts_word_ci(arg, "TRACKING", NULL);
            if (starts_word_ci(arg, "USAGE", NULL)) show_usage(OPJ_VIEW_USAGE);
            else if (starts_word_ci(arg, "TASKUSAGE", NULL)) show_usage(OPJ_VIEW_TASK_USAGE);
            else if (starts_word_ci(arg, "GRAPH", NULL)) show_usage(OPJ_VIEW_GRAPH);
            else usage_mode = 0;
            tick_view();
            redraw();
        } else {
            rc = OPJ_RX_ERROR;
            put_text(app_lasterror, sizeof app_lasterror, "VIEW: GANTT, TRACKING, USAGE, TASKUSAGE, GRAPH or NETWORK");
        }
    } else if (starts_word_ci(command, "QUIT", &arg)) {
        if (DOC->dirty && !starts_word_ci(arg, "FORCE", NULL)) {
            rc = OPJ_RX_NOW; put_text(app_lasterror, sizeof app_lasterror, "QUIT: the plan has unsaved changes (FORCE drops them)");
        } else quit_now = 1;
    } else if (starts_word_ci(command, "LASTERROR", &arg) && app_lasterror[0]) {
        put_text(result, size, app_lasterror);
    } else {
        own = 0;
        app_lasterror[0] = 0;
        rc = opj_rexx_command(&rx, command, result, size);
        if (starts_word_ci(command, "HELP", &arg) && !*arg && rc == OPJ_RX_OK) {
            /* the core's list already starts with the app's words */
        }
    }
    if (own && rc == OPJ_RX_OK && !starts_word_ci(command, "LASTERROR", NULL)) app_lasterror[0] = 0;
    return rc;
}

static void rexx_messages(void)
{
    struct RexxMsg *rm;
    while (rexx_port && (rm = (struct RexxMsg *)GetMsg(rexx_port)) != NULL) {
        static char res[2048];
        int rc;
        rexx_busy = 1;
        rc = rexx_do_command((const char *)rm->rm_Args[0], res, sizeof res);
        rexx_busy = 0;
        rm->rm_Result1 = rc;
        rm->rm_Result2 = 0;
        if (rc == OPJ_RX_OK && (rm->rm_Action & RXFF_RESULT) && RexxSysBase)
            rm->rm_Result2 = (LONG)CreateArgstring((STRPTR)res, (LONG)strlen(res));
        ReplyMsg((struct Message *)rm);
    }
    sel_col = opj_view_near_column(&view, sel_col);      /* COLUMN may have hidden it */
    tick_view();
    if (win) redraw();
}

static void rexx_open(void)
{
    struct MsgPort *p;
    int n;
    if (!RexxSysBase) return;
    if (!(p = CreateMsgPort())) return;
    p->mp_Node.ln_Pri = 0;
    Forbid();
    for (n = 1; n < 10; n++) {                       /* GFOPENPROJECT, then GFOPENPROJECT.2 for a second copy */
        if (n == 1) put_text(rexx_name, sizeof rexx_name, REXX_PORT_NAME);
        else snprintf(rexx_name, sizeof rexx_name, REXX_PORT_NAME ".%d", n);
        if (!FindPort((STRPTR)rexx_name)) break;
    }
    if (n == 10) { Permit(); DeleteMsgPort(p); return; }
    p->mp_Node.ln_Name = rexx_name;
    AddPort(p);
    Permit();
    rexx_port = p;
}

static void rexx_close(void)
{
    struct RexxMsg *rm;
    if (!rexx_port) return;
    RemPort(rexx_port);
    while ((rm = (struct RexxMsg *)GetMsg(rexx_port)) != NULL) {
        rm->rm_Result1 = RC_FATAL;
        rm->rm_Result2 = 0;
        ReplyMsg((struct Message *)rm);
    }
    DeleteMsgPort(rexx_port);
    rexx_port = NULL;
}

/* ---- The window ---- */

static int open_window(void)
{
    int w, h;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    put_text(font_name, sizeof font_name, (const char *)scr->Font->ta_Name);
    font_attr = *scr->Font;
    font_attr.ta_Name = (STRPTR)font_name;
    if (!(font = OpenFont(&font_attr))) return 0;
    fh = font->tf_YSize;
    {
        struct RastPort rp;
        InitRastPort(&rp);
        SetFont(&rp, font);
        fw = TextLength(&rp, (STRPTR)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", 62) / 62;
        if (fw < 4) fw = 8;
    }
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    w = scr->Width < 1000 ? scr->Width : 1000;
    h = scr->Height - scr->BarHeight - 1 < 600 ? scr->Height - scr->BarHeight - 1 : 600;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenProject",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, (scr->Width - w) / 2, WA_Top, scr->BarHeight + 1 + (scr->Height - scr->BarHeight - 1 - h) / 2,
        WA_MinWidth, 320, WA_MinHeight, 160, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_GimmeZeroZero, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE,
        WA_ReportMouse, TRUE,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    SetFont(win->RPort, font);
    return 1;
}

static void close_window(void)
{
    if (od) { oldraw_free(od); od = NULL; }
    if (win) {
        ClearMenuStrip(win);
        CloseWindow(win);
        win = NULL;
    }
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

/* Today at 08:00, in the plan's minutes from 2000 (DateStamp's days are from 1978: 8035 days earlier). */
static owf_ptime today(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ((long)ds.ds_Days - 8035L) * 1440L + 8L * 60L;
}

int main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    NewList(&res_list);
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)"GFOpenProject needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    if (argc > 1) put_text(first, sizeof first, argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    rx.view = &view;                       /* EXPORT FORMAT PDF prints the window's columns and zoom */
    opj_lines_init(&lines);
    rx.lines = &lines;                     /* FILTER and GROUP set the window's lines */
    if (!(rx.doc = opj_doc_new(NULL, today()))) {
        if (argc) PutStr((STRPTR)"GFOpenProject: not enough memory\n");
        rc = 20;
        goto out;
    }
    if (!open_window()) {
        if (argc) PutStr((STRPTR)"GFOpenProject: the window could not open\n");
        rc = 20;
        goto out;
    }
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    rexx_open();
    if (first[0]) open_file(first);
    redraw();
    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG rsig = rexx_port ? (1UL << rexx_port->mp_SigBit) : 0;
        ULONG got = Wait(sig | rsig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) quit_now = 1;
        if (got & rsig) rexx_messages();
        if (got & sig) events();
    }
out:
    rexx_close();
    if (RexxSysBase) { CloseLibrary((struct Library *)RexxSysBase); RexxSysBase = NULL; }
    close_window();
    res_free();
    opj_network_free(&net);
    opj_lines_free(&lines);
    if (rx.doc) opj_doc_free(rx.doc);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}
