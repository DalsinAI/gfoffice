/*
 * GFOpenCalc - the spreadsheet of the GFOffice Suite, beside GFOpenWrite.
 *
 * The window: GFOpenGadTools chrome as GFOpenWrite 2's (app/openwrite2.c), a
 * formula bar (the cell's name and what it holds), the grid, sheet tabs
 * and a status line. Everything about the sheet is GFOpenCalc's portable
 * core (src/calc, include/opencalc_core.h): this file maps keys, the
 * pointer and the menus to it and paints the list of things it says to
 * draw, into an off-screen bitmap when there is memory for one (so the
 * grid never flickers), else straight into the window. Workbooks are
 * libowf's (owf_book) and their formulas the engine's (owf_calc.h).
 * 68040 code, no FPU needed (libowf's doubles are soft maths).
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <utility/tagitem.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/rexxsyslib.h>
#include <devices/inputevent.h>
#include <devices/clipboard.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>
#include <exec/io.h>
#include <exec/ports.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include "ogt_openfont.h"
#include <proto/layers.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <clib/alib_protos.h>

#include "opencalc_core.h"
#include "owf_open.h"
#include "ow_stack.h"
#include "lastdir.h"
#include "ow_autosave.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_iconset.h"
#include "ogt_font.h"
#include "ogt_status.h"
#include "ow_fontwidths.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *OpenFontTextBase = NULL, *LayersBase = NULL;
struct Library *CyberGfxBase = NULL, *DiskfontBase = NULL;
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "GFOpenCalc 0.1.65 (9.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " GFOpenCalc, the spreadsheet of the GFOffice Suite. MIT, Copyright (c) 2026 AmiCradle Team";

#ifndef IDCMP_EXTENDEDMOUSE                      /* OS 3.2's wheel messages (intuition/intuition.h V47) */
#define IDCMP_EXTENDEDMOUSE 0x80000000
#define IMSGCODE_INTUIWHEELDATA 0x0001
struct IntuiWheelData { UWORD Version; WORD WheelX; WORD WheelY; };
#endif
#define WHEEL_UP 0x7a
#define WHEEL_DOWN 0x7b

enum { C_NEW = 1, C_OPEN, C_SAVE, C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_BOLD, C_ITALIC };
enum { GID_TOOLBAR = 100, GID_NAME = 300, GID_INPUT, GID_VSCROLL, GID_HSCROLL };

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_EXPORT_CSV, M_EXPORT_PDF, M_PRINT, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_FIND, M_FIND_NEXT,
    M_FILL_DOWN, M_FILL_RIGHT, M_CLEAR, M_CLEAR_FORMATS, M_CLEAR_ALL,
    M_SELECT_ALL, M_SELECT_ROWS, M_SELECT_COLS, M_GOTO,
    M_FREEZE, M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_100,
    M_THEME_OPEN, M_THEME_GRAPHITE, M_THEME_EMBER, M_THEME_CLEAR, M_THEME_CLASSIC,
    M_INS_ROWS, M_INS_COLS, M_INS_SHEET, M_DEL_ROWS, M_DEL_COLS, M_COMMENT, M_CHART, M_CHART_DELETE, M_CHART_FORMAT,
    M_CHART_DATA, M_CHART_AXIS_TITLES, M_CHART_SERIES_STYLE, M_CHART_POINT_COLOUR, M_CHART_AXIS_FORMATS,
    M_CHART_SERIES_LABELS, M_CHART_PLOT_OPTIONS, M_CHART_TRENDLINE, M_CHART_ERROR_BARS, M_CHART_SERIES_AXIS,
    M_BOLD, M_ITALIC, M_UNDERLINE,
    M_AL_GENERAL, M_AL_LEFT, M_AL_CENTRE, M_AL_RIGHT,
    M_NUMFMT = 100,                                  /* + oc_number_formats' index */
    M_FILL_NONE = 150, M_FILL_YELLOW, M_FILL_GREEN, M_FILL_BLUE, M_FILL_GREY, M_FILL_RED,
    M_COL_AUTO = 170, M_COL_RED, M_COL_BLUE, M_COL_GREEN,
    M_BORDER_ALL = 180, M_BORDER_OUTLINE, M_BORDER_BOTTOM, M_BORDER_NONE,
    M_COL_WIDTH = 190, M_ROW_HEIGHT, M_HIDE_COLS, M_SHOW_COLS, M_HIDE_ROWS, M_SHOW_ROWS,
    M_SHEET_RENAME = 200, M_SHEET_DELETE, M_SHEET_DUPLICATE, M_SHEET_LEFT, M_SHEET_RIGHT, M_SHEET_HIDE, M_SHEET_SHOW,
    M_NAME_DEFINE, M_NAME_DELETE, M_SHEET_NEXT, M_SHEET_PREV,
    M_SORT_ASC, M_SORT_DESC, M_MERGE, M_UNMERGE, M_CONDFMT, M_CONDFMT_CLEAR,
    M_AUTOFILTER, M_FILTER_ACTIVE, M_FILTER_CLEAR, M_VALIDITY, M_VALIDITY_CLEAR, M_GOAL_SEEK, M_SUBTOTALS,
    M_PIVOT_SUMMARY, M_PIVOT_REFRESH, M_MULTIPLE_OPS
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", NULL, 0, 0, (APTR)M_SAVE_AS },
    { NM_ITEM, "Export CSV...", NULL, 0, 0, (APTR)M_EXPORT_CSV },
    { NM_ITEM, "Export PDF...", NULL, 0, 0, (APTR)M_EXPORT_PDF },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Print...", "P", 0, 0, (APTR)M_PRINT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },

    { NM_TITLE, "Edit", NULL, 0, 0, NULL },
    { NM_ITEM, "Undo", "Z", 0, 0, (APTR)M_UNDO },
    { NM_ITEM, "Redo", NULL, 0, 0, (APTR)M_REDO },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Cut", "X", 0, 0, (APTR)M_CUT },
    { NM_ITEM, "Copy", "C", 0, 0, (APTR)M_COPY },
    { NM_ITEM, "Paste", "V", 0, 0, (APTR)M_PASTE },
    { NM_ITEM, "Find...", "F", 0, 0, (APTR)M_FIND },
    { NM_ITEM, "Find Next", "G", 0, 0, (APTR)M_FIND_NEXT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Fill Down", "D", 0, 0, (APTR)M_FILL_DOWN },
    { NM_ITEM, "Fill Right", "R", 0, 0, (APTR)M_FILL_RIGHT },
    { NM_ITEM, "Clear", NULL, 0, 0, NULL },
    { NM_SUB, "Contents", NULL, 0, 0, (APTR)M_CLEAR },
    { NM_SUB, "Formats", NULL, 0, 0, (APTR)M_CLEAR_FORMATS },
    { NM_SUB, "All", NULL, 0, 0, (APTR)M_CLEAR_ALL },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Select All", "A", 0, 0, (APTR)M_SELECT_ALL },
    { NM_ITEM, "Select Rows", NULL, 0, 0, (APTR)M_SELECT_ROWS },
    { NM_ITEM, "Select Columns", NULL, 0, 0, (APTR)M_SELECT_COLS },
    { NM_ITEM, "Go To...", NULL, 0, 0, (APTR)M_GOTO },

    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "Freeze Panes", NULL, 0, 0, (APTR)M_FREEZE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Zoom in", "+", 0, 0, (APTR)M_ZOOM_IN },
    { NM_ITEM, "Zoom out", "-", 0, 0, (APTR)M_ZOOM_OUT },
    { NM_ITEM, "Actual size (100%)", "0", 0, 0, (APTR)M_ZOOM_100 },
    { NM_ITEM, "Theme", NULL, 0, 0, NULL },
    { NM_SUB, "Open", NULL, 0, 0, (APTR)M_THEME_OPEN },
    { NM_SUB, "Graphite", NULL, 0, 0, (APTR)M_THEME_GRAPHITE },
    { NM_SUB, "Ember", NULL, 0, 0, (APTR)M_THEME_EMBER },
    { NM_SUB, "Clear", NULL, 0, 0, (APTR)M_THEME_CLEAR },
    { NM_SUB, "Classic", NULL, 0, 0, (APTR)M_THEME_CLASSIC },

    { NM_TITLE, "Insert", NULL, 0, 0, NULL },
    { NM_ITEM, "Rows", NULL, 0, 0, (APTR)M_INS_ROWS },
    { NM_ITEM, "Columns", NULL, 0, 0, (APTR)M_INS_COLS },
    { NM_ITEM, "Sheet", NULL, 0, 0, (APTR)M_INS_SHEET },
    { NM_ITEM, "Comment...", NULL, 0, 0, (APTR)M_COMMENT },
    { NM_ITEM, "Chart...", NULL, 0, 0, (APTR)M_CHART },
    { NM_ITEM, "Delete Selected Chart", NULL, 0, 0, (APTR)M_CHART_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Delete Rows", NULL, 0, 0, (APTR)M_DEL_ROWS },
    { NM_ITEM, "Delete Columns", NULL, 0, 0, (APTR)M_DEL_COLS },

    { NM_TITLE, "Format", NULL, 0, 0, NULL },
    { NM_ITEM, "Bold", "B", 0, 0, (APTR)M_BOLD },
    { NM_ITEM, "Italic", "I", 0, 0, (APTR)M_ITALIC },
    { NM_ITEM, "Underline", "U", 0, 0, (APTR)M_UNDERLINE },
    { NM_ITEM, "Alignment", NULL, 0, 0, NULL },
    { NM_SUB, "General", NULL, 0, 0, (APTR)M_AL_GENERAL },
    { NM_SUB, "Left", NULL, 0, 0, (APTR)M_AL_LEFT },
    { NM_SUB, "Centre", NULL, 0, 0, (APTR)M_AL_CENTRE },
    { NM_SUB, "Right", NULL, 0, 0, (APTR)M_AL_RIGHT },
    { NM_ITEM, "Number Format", NULL, 0, 0, NULL },
    { NM_SUB, "General", NULL, 0, 0, (APTR)(M_NUMFMT + 0) },
    { NM_SUB, "Number", NULL, 0, 0, (APTR)(M_NUMFMT + 1) },
    { NM_SUB, "Number, 2 decimals", NULL, 0, 0, (APTR)(M_NUMFMT + 2) },
    { NM_SUB, "Thousands", NULL, 0, 0, (APTR)(M_NUMFMT + 3) },
    { NM_SUB, "Thousands, 2 decimals", NULL, 0, 0, (APTR)(M_NUMFMT + 4) },
    { NM_SUB, "Percent", NULL, 0, 0, (APTR)(M_NUMFMT + 5) },
    { NM_SUB, "Percent, 2 decimals", NULL, 0, 0, (APTR)(M_NUMFMT + 6) },
    { NM_SUB, "Scientific", NULL, 0, 0, (APTR)(M_NUMFMT + 7) },
    { NM_SUB, "Currency", NULL, 0, 0, (APTR)(M_NUMFMT + 8) },
    { NM_SUB, "Date", NULL, 0, 0, (APTR)(M_NUMFMT + 9) },
    { NM_SUB, "Date, month name", NULL, 0, 0, (APTR)(M_NUMFMT + 10) },
    { NM_SUB, "Time", NULL, 0, 0, (APTR)(M_NUMFMT + 11) },
    { NM_SUB, "Text", NULL, 0, 0, (APTR)(M_NUMFMT + 12) },
    { NM_ITEM, "Fill", NULL, 0, 0, NULL },
    { NM_SUB, "None", NULL, 0, 0, (APTR)M_FILL_NONE },
    { NM_SUB, "Yellow", NULL, 0, 0, (APTR)M_FILL_YELLOW },
    { NM_SUB, "Green", NULL, 0, 0, (APTR)M_FILL_GREEN },
    { NM_SUB, "Blue", NULL, 0, 0, (APTR)M_FILL_BLUE },
    { NM_SUB, "Grey", NULL, 0, 0, (APTR)M_FILL_GREY },
    { NM_SUB, "Red", NULL, 0, 0, (APTR)M_FILL_RED },
    { NM_ITEM, "Text Colour", NULL, 0, 0, NULL },
    { NM_SUB, "Automatic", NULL, 0, 0, (APTR)M_COL_AUTO },
    { NM_SUB, "Red", NULL, 0, 0, (APTR)M_COL_RED },
    { NM_SUB, "Blue", NULL, 0, 0, (APTR)M_COL_BLUE },
    { NM_SUB, "Green", NULL, 0, 0, (APTR)M_COL_GREEN },
    { NM_ITEM, "Borders", NULL, 0, 0, NULL },
    { NM_SUB, "All", NULL, 0, 0, (APTR)M_BORDER_ALL },
    { NM_SUB, "Outline", NULL, 0, 0, (APTR)M_BORDER_OUTLINE },
    { NM_SUB, "Bottom", NULL, 0, 0, (APTR)M_BORDER_BOTTOM },
    { NM_SUB, "None", NULL, 0, 0, (APTR)M_BORDER_NONE },
    { NM_ITEM, "Merge Cells...", NULL, 0, 0, (APTR)M_MERGE },
    { NM_ITEM, "Unmerge Cells", NULL, 0, 0, (APTR)M_UNMERGE },
    { NM_ITEM, "Chart...", NULL, 0, 0, (APTR)M_CHART_FORMAT },
    { NM_ITEM, "Chart Data...", NULL, 0, 0, (APTR)M_CHART_DATA },
    { NM_ITEM, "Chart Axis Titles...", NULL, 0, 0, (APTR)M_CHART_AXIS_TITLES },
    { NM_ITEM, "Chart Series Style...", NULL, 0, 0, (APTR)M_CHART_SERIES_STYLE },
    { NM_ITEM, "Chart Data Point Colour...", NULL, 0, 0, (APTR)M_CHART_POINT_COLOUR },
    { NM_ITEM, "Chart Axis Number Formats...", NULL, 0, 0, (APTR)M_CHART_AXIS_FORMATS },
    { NM_ITEM, "Chart Series Data Labels...", NULL, 0, 0, (APTR)M_CHART_SERIES_LABELS },
    { NM_ITEM, "Chart Plot Options...", NULL, 0, 0, (APTR)M_CHART_PLOT_OPTIONS },
    { NM_ITEM, "Chart Series Trendline...", NULL, 0, 0, (APTR)M_CHART_TRENDLINE },
    { NM_ITEM, "Chart Series Y Error Bars...", NULL, 0, 0, (APTR)M_CHART_ERROR_BARS },
    { NM_ITEM, "Chart Series Value Axis...", NULL, 0, 0, (APTR)M_CHART_SERIES_AXIS },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Conditional Format...", NULL, 0, 0, (APTR)M_CONDFMT },
    { NM_ITEM, "Clear Conditional Formatting", NULL, 0, 0, (APTR)M_CONDFMT_CLEAR },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Column Width...", NULL, 0, 0, (APTR)M_COL_WIDTH },
    { NM_ITEM, "Row Height...", NULL, 0, 0, (APTR)M_ROW_HEIGHT },
    { NM_ITEM, "Hide Columns", NULL, 0, 0, (APTR)M_HIDE_COLS },
    { NM_ITEM, "Show Columns", NULL, 0, 0, (APTR)M_SHOW_COLS },
    { NM_ITEM, "Hide Rows", NULL, 0, 0, (APTR)M_HIDE_ROWS },
    { NM_ITEM, "Show Rows", NULL, 0, 0, (APTR)M_SHOW_ROWS },

    { NM_TITLE, "Data", NULL, 0, 0, NULL },
    { NM_ITEM, "Sort Ascending...", NULL, 0, 0, (APTR)M_SORT_ASC },
    { NM_ITEM, "Sort Descending...", NULL, 0, 0, (APTR)M_SORT_DESC },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "AutoFilter", NULL, 0, 0, (APTR)M_AUTOFILTER },
    { NM_ITEM, "Filter by Active Cell", NULL, 0, 0, (APTR)M_FILTER_ACTIVE },
    { NM_ITEM, "Show All Rows", NULL, 0, 0, (APTR)M_FILTER_CLEAR },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Goal Seek...", NULL, 0, 0, (APTR)M_GOAL_SEEK },
    { NM_ITEM, "Multiple Operations...", NULL, 0, 0, (APTR)M_MULTIPLE_OPS },
    { NM_ITEM, "Subtotals...", NULL, 0, 0, (APTR)M_SUBTOTALS },
    { NM_ITEM, "Pivot Summary...", NULL, 0, 0, (APTR)M_PIVOT_SUMMARY },
    { NM_ITEM, "Refresh Pivot Summary", NULL, 0, 0, (APTR)M_PIVOT_REFRESH },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Validity...", NULL, 0, 0, (APTR)M_VALIDITY },
    { NM_ITEM, "Clear Validity", NULL, 0, 0, (APTR)M_VALIDITY_CLEAR },

    { NM_TITLE, "Sheet", NULL, 0, 0, NULL },
    { NM_ITEM, "Rename...", NULL, 0, 0, (APTR)M_SHEET_RENAME },
    { NM_ITEM, "Delete", NULL, 0, 0, (APTR)M_SHEET_DELETE },
    { NM_ITEM, "Duplicate...", NULL, 0, 0, (APTR)M_SHEET_DUPLICATE },
    { NM_ITEM, "Move Left", NULL, 0, 0, (APTR)M_SHEET_LEFT },
    { NM_ITEM, "Move Right", NULL, 0, 0, (APTR)M_SHEET_RIGHT },
    { NM_ITEM, "Hide Sheet", NULL, 0, 0, (APTR)M_SHEET_HIDE },
    { NM_ITEM, "Show Sheet...", NULL, 0, 0, (APTR)M_SHEET_SHOW },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Define Name...", NULL, 0, 0, (APTR)M_NAME_DEFINE },
    { NM_ITEM, "Delete Name...", NULL, 0, 0, (APTR)M_NAME_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Next Sheet", "]", 0, 0, (APTR)M_SHEET_NEXT },
    { NM_ITEM, "Previous Sheet", "[", 0, 0, (APTR)M_SHEET_PREV },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* 0.1.12: the pictures are the Chrome icon set's (gfopengadtools docs/style/README.md; Images/Toolbar/), by name;
 * the vector icons are drawn when the set can't be (no strips, four colours, the Classic theme). */
static const ogt_tool tools[] = {
    { C_NEW,   "New",     OGT_ICON_FILE,    0, 0, 0, "new" },
    { C_OPEN,  "Open...", OGT_ICON_OPEN,    0, 0, 0, "open" },
    { C_SAVE,  "Save",    OGT_ICON_DISK,    0, 0, 0, "save" },
    { C_UNDO,  "Undo",    OGT_ICON_PARENT,  1, 1, 0, "undo" },
    { C_REDO,  "Redo",    OGT_ICON_FORWARD, 0, 1, 0, "redo" },
    { C_CUT,   "Cut",     OGT_ICON_DELETE,  1, 0, 0, "cut" },
    { C_COPY,  "Copy",    OGT_ICON_ARCHIVE, 0, 0, 0, "copy" },
    { C_PASTE, "Paste",   OGT_ICON_EXTRACT, 0, 0, 0, "paste" }
};

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;                         /* the toolbar's gadgets */
static struct Gadget *glist2;                        /* the GadTools gadgets: the formula bar, the scrollers */
static struct Gadget *g_name, *g_input, *g_vscroll, *g_hscroll;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;
static struct RastPort mrp;                          /* measures text in the window's font */
static struct RastPort crp;                          /* measures cells' text for the core, in their fonts */
static int quit_now;

static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static ogt_status sbar;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS;                  /* icons only to start (the Team's rule, 10 October 2026) */

static oc_core *core;
static oc_draw_list dl;
static ow_autosave *autosave;
static struct MsgPort *rexx_port;
static char rexx_name[16];                           /* GFOPENCALC, or GFOPENCALC.2 for a second copy */
#define REXX_PORT_NAME "GFOPENCALC"
static ow_recovery recovery;                         /* the autosave's recovery copy (ow_autosave.h) */
static char current_path[512];
static char find_text[256];
static char current_format[32] = "";
static char status[256] = "Ready.";

static box area, toolbar_box, bar_box, grid_box, tabs_box, status_box;
static int sw;                                       /* a scroller's width */
static unsigned sc_first[2], sc_vis[2], sc_total[2]; /* what the scrollers show */
static char shown_name[40], shown_input[1024];       /* what the formula bar shows */
static int bar_active;                               /* the formula bar's gadget is being typed in */

/* the off-screen bitmap the grid is painted in */
static struct BitMap *buf_bm;
static struct RastPort buf_rp;
static int buf_w, buf_h;

/* the pointer */
static int mouse_down, resizing, resize_axis;
/* A chart being moved or resized: where it and the pointer began, the handle
 * (-1 moves), and the outline showing (its box, -1 none). */
#define CHART_HANDLE 7
#define CHART_MIN_W 32
#define CHART_MIN_H 24
static int chart_drag, chart_handle = -1, chart_from_x, chart_from_y;
static int chart_start_x, chart_start_y, chart_start_w, chart_start_h;
static int outline_x = -1, outline_y, outline_w, outline_h;
static unsigned resize_index;
static int resize_from, resize_size;
static ULONG click_secs, click_micros;
static unsigned click_row, click_col;
static int defer_repaint, repaint_pending;

/* The sheet tabs' strip: every place a click means something (a sheet's tab, "+", the two arrows), and
 * which sheets show (the core's oc_tab_strip). */
#define MAX_TABS 64
enum { TAB_PLUS = -1, TAB_LEFT = -2, TAB_RIGHT = -3 };
static int tab_n, tab_x[MAX_TABS + 3], tab_w[MAX_TABS + 3], tab_sheet[MAX_TABS + 3];
static int tab_first, tab_end, tab_scroll, tab_shown_sheet = -1;

static void draw_all(void);
static void refresh(void);
static void pick_from_list(int bx, int by, int bw, int bh);
static void relayout(void);
static void menu_action(ULONG id);

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

/* ---- Text: the model is UTF-8, Amiga fonts and gadgets ISO-8859-1 ---- */

/* Latin-1 for the screen: common typography outside it to the nearest
 * Latin-1, as GFOpenWrite shows it, anything else '?'. */
static size_t to_latin1(const char *s, size_t n, char *out, size_t cap)
{
    size_t i = 0, o = 0;
    if (!cap) return 0;
    while (i < n && s[i] && o + 1 < cap) {
        unsigned c = (unsigned char)s[i], cp;
        int len, k;
        if (c < 0x80) { out[o++] = (char)c; ++i; continue; }
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { out[o++] = '?'; ++i; continue; }
        if (i + (size_t)len > n) { out[o++] = '?'; break; }
        for (k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        i += (size_t)len;
        if (cp >= 0xA0 && cp <= 0xFF) out[o++] = (char)cp;
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A) out[o++] = '\'';
        else if (cp == 0x201C || cp == 0x201D || cp == 0x201E) out[o++] = '"';
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) out[o++] = '-';
        else if (cp == 0x2022) out[o++] = (char)0xB7;
        else if (cp == 0x20AC) { out[o++] = 'E'; if (o + 1 < cap) out[o++] = 'U'; if (o + 1 < cap) out[o++] = 'R'; }
        else out[o++] = '?';
    }
    out[o] = 0;
    return o;
}

/* A gadget's Latin-1 text as UTF-8 (owf_malloc'd). */
static char *from_latin1(const char *s)
{
    return oc_text_from_latin1(s, strlen(s), NULL);
}

/* TextLength with a system-made bold's smear counted (as GFOpenWrite 2 measures). */
static int text_length(struct RastPort *rp, const char *s, size_t m)
{
    struct TextFont *f = rp->Font;
    UBYTE algo = rp->AlgoStyle;
    LONG w = TextLength(rp, (STRPTR)s, (ULONG)m), plain;
    if (!m || !(algo & FSF_BOLD) || !f || (f->tf_Style & FSF_BOLD)) return (int)w;
    rp->AlgoStyle = algo & ~FSF_BOLD;
    plain = TextLength(rp, (STRPTR)s, (ULONG)m) + (LONG)m * (f->tf_BoldSmear ? f->tf_BoldSmear : 1);
    rp->AlgoStyle = algo;
    return (int)(plain > w ? plain : w);
}

static ULONG soft_style(unsigned flags)
{
    return (flags & OWF_BOLD ? FSF_BOLD : 0) | (flags & OWF_ITALIC ? FSF_ITALIC : 0) |
           (flags & OWF_UNDERLINE ? FSF_UNDERLINED : 0);
}

/* ---- Cells' fonts ----
 * A cell's text is drawn and measured in its own font at its own size (the core's oc_font_px: 11 points is a
 * 15-pixel em at 100%). The book's font is taken to an Amiga family by its name and kind (CGTriumvirate for
 * Arial, Calibri, Helvetica and the other sans fonts, CGTimes for serif ones, courier for fixed ones); the face
 * that draws it is GFOpenWrite's choice (app/ow_fontwidths.c, ow_fw_face): that family when it opens, else the
 * first of its kind's that does (gfopenfont.library can't read OS 3.2's Intellifont CGTriumvirate, which has no
 * bitmap sizes: DejaVu Sans, Liberation Sans, helvetica...), else the window's font. openfont makes a face at
 * a size of its own reckoning (the lab's DejaVu Sans asked for 15 drew capitals 13 high, as large as its 13
 * points of the window's font): the face is made again at the size whose capitals are 0.716 em, Arial's, when
 * that comes nearer. Bold and italic are made from it. CELL_FONTS sizes stay open; the oldest makes way. */
#define CELL_FONTS 12
typedef struct cell_font_entry {
    char file[40];                                   /* the family asked for (cell_font_file) */
    char face[OW_FW_NAME];                           /* the face that draws it; "" the window's font */
    int pixels, owned;                               /* the em asked for, in pixels */
    int ysize, cap;                                  /* the size it was made at (ta_YSize), its capitals' height */
    const char *by;                                  /* openfont, diskfont or the window's font */
    struct TextFont *font;
    int ink_top, ink_bottom;                         /* the rows text marks, from the font's top: ascenders to descenders */
} cell_font_entry;
static cell_font_entry cell_fonts[CELL_FONTS];
static int cell_font_next;
static ow_fontwidths cell_faces;                     /* which face opens, for each family */
static int cell_faces_ready;
#define FONT_PROBES 8
static struct TextFont *font_probes[FONT_PROBES];    /* faces ow_fw_face opened to see that they open */
static int font_probe_next;
static const char *font_opened_by = "";             /* the library that opened the last font: FONTCHECK says */

static void close_cell_fonts(void)
{
    int i;
    for (i = 0; i < CELL_FONTS; ++i) {
        if (cell_fonts[i].font && cell_fonts[i].owned) CloseFont(cell_fonts[i].font);
        memset(&cell_fonts[i], 0, sizeof cell_fonts[i]);
    }
    cell_font_next = 0;
    for (i = 0; i < FONT_PROBES; ++i) {
        if (font_probes[i]) CloseFont(font_probes[i]);
        font_probes[i] = NULL;
    }
    font_probe_next = 0;
    cell_faces_ready = 0;                            /* the window's font (the last face to try) may change */
}

static int name_has(const char *name, const char *part)
{
    size_t i, j, n, p;
    if (!name || !part) return 0;
    n = strlen(name); p = strlen(part);
    if (!p || p > n) return 0;
    for (i = 0; i + p <= n; ++i) {
        for (j = 0; j < p; ++j)
            if (tolower((unsigned char)name[i + j]) != tolower((unsigned char)part[j])) break;
        if (j == p) return 1;
    }
    return 0;
}

/* The book's font (-1 the book's own) and its name. */
static int cell_font_index(int index, const char **name, owf_font_kind *kind)
{
    owf_book *book = core ? oc_core_book(core) : NULL;
    *name = NULL;
    *kind = OWF_FONT_ANY;
    if (book && index < 0 && book->nstyles) index = book->styles[0].font.font;
    if (book && index >= 0 && index < book->nfonts) {
        *name = book->fonts[index].name;
        *kind = book->fonts[index].kind;
    }
    return index;
}

static const char *cell_font_file(int index)
{
    const char *name;
    owf_font_kind kind;
    cell_font_index(index, &name, &kind);
    if (kind == OWF_FONT_MONO || name_has(name, "courier") || name_has(name, "mono") || name_has(name, "consolas") ||
        name_has(name, "lettergothic"))
        return "courier.font";
    if (kind != OWF_FONT_SANS && !name_has(name, "sans") &&
        (kind == OWF_FONT_SERIF || name_has(name, "times") || name_has(name, "serif") || name_has(name, "cambria") ||
         name_has(name, "georgia") || name_has(name, "garamond")))
        return "CGTimes.font";
    return "CGTriumvirate.font";
}

/* An Amiga font at a size: an outline font (FONTS:name.otag) made at exactly that size, a bitmap one at its
 * nearest designed size (scaled only when it has none). Through gfopenfont.library, then diskfont.library (the
 * lab's openfont can't read OS 3.2's Intellifont outlines, which diskfont makes through bullet.library, and
 * finds none of SYS:Fonts' bitmap families). NULL when neither has it. */
static struct TextFont *open_font_px(const char *file, int pixels)
{
    struct TextAttr ta;
    struct TextFont *f = NULL;
    char otag[96];
    BPTR lk;
    int outline = 0;
    if (!file || !file[0]) return NULL;
    snprintf(otag, sizeof otag, "FONTS:%s", file);
    if (strlen(otag) > 5 && !strcmp(otag + strlen(otag) - 5, ".font")) {
        strcpy(otag + strlen(otag) - 5, ".otag");
        if ((lk = Lock((STRPTR)otag, ACCESS_READ))) { outline = 1; UnLock(lk); }
    }
    memset(&ta, 0, sizeof ta);
    ta.ta_Name = (STRPTR)file;
    ta.ta_YSize = (UWORD)pixels;
    ta.ta_Style = FS_NORMAL;
    font_opened_by = "openfont";
    if (OpenFontTextBase) {
        ta.ta_Flags = outline ? FPF_DISKFONT : FPF_DISKFONT | FPF_DESIGNED;
        if ((f = ogt_openfont_font(OpenFontTextBase, &ta))) return f;
        ta.ta_Flags = outline ? FPF_DISKFONT | FPF_DESIGNED : FPF_DISKFONT;
        if ((f = ogt_openfont_font(OpenFontTextBase, &ta))) return f;
    }
    font_opened_by = "diskfont";
    if (DiskfontBase) {
        ta.ta_Flags = outline ? FPF_DISKFONT : FPF_DISKFONT | FPF_DESIGNED;
        if ((f = OpenDiskFont(&ta))) return f;
        ta.ta_Flags = FPF_DISKFONT;
        f = OpenDiskFont(&ta);
    }
    return f;
}

/* ow_fw_face's backend: a face opened to see that it opens (kept until the next FONT_PROBES), and its letters'
 * advances. */
static void *probe_open(void *user, const char *file, int pixels, int *ysize, int *ascent)
{
    struct TextFont *f = open_font_px(file, pixels);
    (void)user;
    if (!f) return NULL;
    *ysize = f->tf_YSize;
    *ascent = f->tf_Baseline + 1;
    if (font_probes[font_probe_next]) CloseFont(font_probes[font_probe_next]);
    font_probes[font_probe_next] = f;
    font_probe_next = (font_probe_next + 1) % FONT_PROBES;
    return f;
}

static int probe_advance(void *user, void *f, unsigned char c, int bold)
{
    struct RastPort rp;
    char ch = (char)c;
    (void)user;
    InitRastPort(&rp);
    SetFont(&rp, (struct TextFont *)f);
    SetSoftStyle(&rp, bold ? FSF_BOLD : 0, FSF_BOLD);
    return text_length(&rp, &ch, 1);
}

/* The rows of the font's box (from its top) that text s marks with ink, drawn into a small bitmap: what
 * places a cell's text, since a font's height (tf_YSize) holds space above and below its letters that
 * differs from font to font. 0 when there is no memory for the bitmap. */
static int font_ink(struct TextFont *f, const char *s, int *top, int *bottom)
{
    struct RastPort rp;
    struct BitMap *bm;
    int len = (int)strlen(s), w, h, x, y, first = -1, last = -1;
    InitRastPort(&rp);
    SetFont(&rp, f);
    w = TextLength(&rp, (STRPTR)s, (ULONG)len) + 8;
    h = f->tf_YSize + 4;
    if (((struct Library *)GfxBase)->lib_Version < 39 || !(bm = AllocBitMap((ULONG)w, (ULONG)h, 1, BMF_CLEAR, NULL)))
        return 0;
    rp.BitMap = bm;
    SetAPen(&rp, 1);
    SetDrMd(&rp, JAM1);
    Move(&rp, 2, 2 + f->tf_Baseline);
    Text(&rp, (STRPTR)s, (ULONG)len);
    WaitBlit();
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (ReadPixel(&rp, x, y)) {
                if (first < 0) first = y;
                last = y;
                break;
            }
    FreeBitMap(bm);
    if (first < 0) return 0;
    *top = first - 2;
    *bottom = last - 2;
    return 1;
}

static int font_cap(struct TextFont *f)
{
    int top, bottom;
    return font_ink(f, "H", &top, &bottom) ? bottom - top + 1 : 0;
}

/* face at an em of pixels, at the size whose capitals are 0.716 em high (the core's oc_font_size_for_caps):
 * the sizes it tries made and measured, the chosen kept, the others closed. *ysize: the size it was made
 * at; *cap: its capitals' height (0 not known). */
typedef struct { const char *face; int n; int size[OC_CAP_TRIES + 1]; struct TextFont *font[OC_CAP_TRIES + 1]; } cap_trial;

static int cap_at_size(void *user, int size)
{
    cap_trial *t = (cap_trial *)user;
    struct TextFont *f;
    if (t->n > OC_CAP_TRIES || !(f = open_font_px(t->face, size))) return 0;
    t->size[t->n] = size;
    t->font[t->n++] = f;
    return font_cap(f);
}

static struct TextFont *open_cell_font(const char *face, int pixels, int *ysize, int *cap)
{
    cap_trial t;
    struct TextFont *keep = NULL;
    int k;
    memset(&t, 0, sizeof t);
    t.face = face;
    *ysize = oc_font_size_for_caps(pixels, cap_at_size, &t, cap);
    for (k = 0; k < t.n; k++) {
        if (!keep && t.size[k] == *ysize) keep = t.font[k];
        else CloseFont(t.font[k]);
    }
    return keep;
}

/* The font for a cell's text: the book's font index (-1 the book's own) at size twips (0 the default). */
static cell_font_entry *cell_font(int index, int size)
{
    const char *file = cell_font_file(index), *face;
    int i, slot = -1, pixels = core ? oc_font_px(core, size) : fh, owned = 1, ysize = 0, cap = 0;
    struct TextFont *f = NULL;
    cell_font_entry *e;
    for (i = 0; i < CELL_FONTS; ++i) {
        if (cell_fonts[i].font && cell_fonts[i].pixels == pixels && !strcmp(cell_fonts[i].file, file))
            return &cell_fonts[i];
        if (!cell_fonts[i].font && slot < 0) slot = i;
    }
    if (!cell_faces_ready) {
        ow_fw_backend be;
        memset(&be, 0, sizeof be);
        be.open = probe_open;
        be.advance = probe_advance;
        ow_fw_init(&cell_faces, &be, font_attr.ta_Name ? (const char *)font_attr.ta_Name : "",
                   font_attr.ta_Name ? (const char *)font_attr.ta_Name : "");   /* the window's font is the theme's */
        cell_faces_ready = 1;
    }
    face = ow_fw_face(&cell_faces, file);
    if (face[0]) f = open_cell_font(face, pixels, &ysize, &cap);
    if (!f) {                                        /* the window's: remembered, so it isn't looked for again */
        f = font;
        owned = 0;
        face = "";
        ysize = fh;
        cap = f ? font_cap(f) : 0;
        font_opened_by = "the window's font";
    }
    if (!f) return NULL;
    if (slot < 0) {
        slot = cell_font_next;
        cell_font_next = (cell_font_next + 1) % CELL_FONTS;
        if (cell_fonts[slot].owned) CloseFont(cell_fonts[slot].font);
    }
    e = &cell_fonts[slot];
    snprintf(e->file, sizeof e->file, "%s", file);
    snprintf(e->face, sizeof e->face, "%s", face);
    e->pixels = pixels;
    e->owned = owned;
    e->ysize = ysize;
    e->cap = cap;
    e->by = font_opened_by;
    e->font = f;
    if (!font_ink(f, "Hdgjpqy", &e->ink_top, &e->ink_bottom)) {
        e->ink_top = 0;
        e->ink_bottom = f->tf_YSize - 1;
    }
    return e;
}

/* The core's measure: a cell's text in its font. */
static int measure(void *user, const char *s, size_t n, unsigned flags, int fnt, int size)
{
    char buf[512];
    size_t m;
    cell_font_entry *e;
    (void)user;
    if (!n || !(e = cell_font(fnt, size))) return 0;
    m = to_latin1(s, n, buf, sizeof buf);
    SetFont(&crp, e->font);
    SetSoftStyle(&crp, soft_style(flags), FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    return text_length(&crp, buf, m);
}

/* ---- The clipboard: FTXT, its CHRS in ISO-8859-1 ---- */

#define OC_ID(a,b,c,d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#define OC_ID_FORM OC_ID('F','O','R','M')
#define OC_ID_FTXT OC_ID('F','T','X','T')
#define OC_ID_CHRS OC_ID('C','H','R','S')
#define OC_MAX_CLIP (1UL << 20)

typedef struct oc_clipboard {
    struct MsgPort *port;
    struct IOClipReq *io;
} oc_clipboard;

static int clipboard_open(oc_clipboard *c)
{
    memset(c, 0, sizeof(*c));
    c->port = CreateMsgPort();
    if (!c->port) return 0;
    c->io = (struct IOClipReq *)CreateIORequest(c->port, sizeof(*c->io));
    if (!c->io) return 0;
    if (OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP, (struct IORequest *)c->io, 0)) {
        c->io->io_Device = NULL;
        return 0;
    }
    return 1;
}

static void clipboard_close(oc_clipboard *c)
{
    if (c->io) {
        if (c->io->io_Device) CloseDevice((struct IORequest *)c->io);
        DeleteIORequest((struct IORequest *)c->io);
    }
    if (c->port) DeleteMsgPort(c->port);
    memset(c, 0, sizeof(*c));
}

static LONG clipboard_read_bytes(oc_clipboard *c, void *buf, LONG len)
{
    c->io->io_Command = CMD_READ;
    c->io->io_Data = (STRPTR)buf;
    c->io->io_Length = len;
    DoIO((struct IORequest *)c->io);
    return c->io->io_Error ? -1 : (LONG)c->io->io_Actual;
}

static void clipboard_read_done(oc_clipboard *c)
{
    UBYTE dummy[64];
    while (clipboard_read_bytes(c, dummy, sizeof dummy) > 0) ;
}

/* The clipboard's text as UTF-8 (owf_malloc'd), or NULL. */
static char *clipboard_get_text(size_t *out_len)
{
    oc_clipboard c;
    ULONG head[3], chunk[2], total = 0;
    char *text = NULL, *utf8;
    if (out_len) *out_len = 0;
    if (!clipboard_open(&c)) { clipboard_close(&c); return NULL; }
    c.io->io_Offset = 0;
    c.io->io_ClipID = 0;
    if (clipboard_read_bytes(&c, head, 12) != 12 || head[0] != OC_ID_FORM || head[2] != OC_ID_FTXT ||
        head[1] > OC_MAX_CLIP + 64) {
        clipboard_read_done(&c);
        clipboard_close(&c);
        return NULL;
    }
    text = (char *)malloc((size_t)head[1] + 1);
    if (!text) { clipboard_read_done(&c); clipboard_close(&c); return NULL; }
    while (clipboard_read_bytes(&c, chunk, 8) == 8) {
        ULONG size = chunk[1], padded = size + (size & 1);
        if (chunk[0] == OC_ID_CHRS && total + size <= head[1]) {
            if (clipboard_read_bytes(&c, text + total, (LONG)size) != (LONG)size) break;
            total += size;
            if (size & 1) { UBYTE pad; clipboard_read_bytes(&c, &pad, 1); }
        } else {
            UBYTE skip[64];
            while (padded) {
                LONG n = padded < sizeof skip ? (LONG)padded : (LONG)sizeof skip;
                if (clipboard_read_bytes(&c, skip, n) != n) break;
                padded -= (ULONG)n;
            }
        }
    }
    clipboard_read_done(&c);
    clipboard_close(&c);
    utf8 = oc_text_from_latin1(text, total, out_len);
    free(text);
    return utf8;
}

/* UTF-8 text onto the clipboard as FTXT, Latin-1 ('?' for what it hasn't). */
static int clipboard_set_text(const char *utf8, size_t length)
{
    oc_clipboard c;
    ULONG form[3], chunk[2];
    UBYTE pad = 0;
    size_t len = 0;
    char *text;
    if (!utf8 || length > OC_MAX_CLIP) return 0;
    if (!(text = oc_text_to_latin1(utf8, length, &len))) return 0;
    if (!clipboard_open(&c)) { clipboard_close(&c); owf_free(text); return 0; }
    form[0] = OC_ID_FORM;
    form[1] = 4 + 8 + (ULONG)len + ((ULONG)len & 1);
    form[2] = OC_ID_FTXT;
    chunk[0] = OC_ID_CHRS;
    chunk[1] = (ULONG)len;
    c.io->io_Offset = 0;
    c.io->io_ClipID = 0;
    c.io->io_Error = 0;
#define CLIP_WRITE(ptr,n) do { \
    c.io->io_Command = CMD_WRITE; c.io->io_Data = (STRPTR)(ptr); \
    c.io->io_Length = (n); DoIO((struct IORequest *)c.io); \
    if (c.io->io_Error) goto write_fail; \
} while (0)
    CLIP_WRITE(form, 12);
    CLIP_WRITE(chunk, 8);
    if (len) CLIP_WRITE(text, (ULONG)len);
    if (len & 1) CLIP_WRITE(&pad, 1);
    c.io->io_Command = CMD_UPDATE;
    DoIO((struct IORequest *)c.io);
    if (c.io->io_Error) goto write_fail;
#undef CLIP_WRITE
    clipboard_close(&c);
    owf_free(text);
    return 1;
write_fail:
    clipboard_close(&c);
    owf_free(text);
    return 0;
}

/* ---- The theme, messages ---- */

static char *read_file(const char *path)
{
    BPTR f = Open((STRPTR)path, MODE_OLDFILE);
    LONG n, cap = 32768;
    char *buf;
    if (!f) return NULL;
    buf = malloc(cap);
    if (!buf) { Close(f); return NULL; }
    n = Read(f, buf, cap - 1);
    Close(f);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

static void load_theme(void)
{
    char path[160], err[120];
    char *text;
    snprintf(path, sizeof path, "PROGDIR:Themes/%s.theme", theme_name);
    text = read_file(path);
    if (!text) {
        snprintf(path, sizeof path, "SYS:Prefs/Presets/Themes/%s.theme", theme_name);
        text = read_file(path);
    }
    if (theme.text) ogt_theme_free(&theme);
    if (!text || !ogt_theme_parse(&theme, text, err, sizeof err)) {
        ogt_theme_parse(&theme, classic_theme, err, sizeof err);
        strcpy(theme_name, "Classic");
    }
    free(text);
    /* GFOpenGadTools' status bar (ogt_status) fills with "panel", a key no theme has (THEME_SPEC.md 5): ogt_pen
     * then gives pen 1, black, under the status line's dark text. Until GFOpenGadTools takes a key themes
     * have, "panel" is the window's colour here. */
    ogt_theme_override(&theme, "panel", NULL);
    {
        int mode = theme_mode == OGT_DARK && theme.has_dark ? OGT_DARK : OGT_LIGHT;
        const char *window = ogt_theme_get(&theme, mode, "window");
        if (!ogt_theme_get(&theme, mode, "panel") && window) ogt_theme_override(&theme, "panel", window);
    }
}

static void read_theme_choice(void)
{
    char *s = read_file("ENV:GFOpenGadTools/Theme");
    if (!s) return;
    {
        char *nl = strchr(s, '\n');
        if (nl) *nl = 0;
        if (*s && strlen(s) < sizeof theme_name) strcpy(theme_name, s);
        theme_mode = nl && !strncmp(nl + 1, "dark", 4) ? OGT_DARK : OGT_LIGHT;
    }
    free(s);
}

static void tell(const char *title, const char *text)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)"OK" };
    EasyRequest(win, &es, NULL, (ULONG)text);
}

/* src into dst (n bytes), cut short to fit and always ended: what snprintf("%s") did, without GCC 16's
 * format-truncation error at -Werror on a status or a file name that is meant to be cut short. */
static void put_text(char *dst, size_t n, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (!n) return;
    if (len >= n) len = n - 1;
    if (len) memcpy(dst, src, len);
    dst[len] = 0;
}

static void set_status(const char *s)
{
    put_text(status, sizeof status, s);
}

static const char *leaf(const char *path)
{
    const char *s = FilePart((STRPTR)path);
    return s && *s ? s : path;
}

static int result(int rc, const char *done);
static int ask_window(const char *title, const char *text, const char *buttons);

/* A typed value its cell's rule refused: the rule's alert, as Excel shows it. Stop: Retry goes on typing,
 * Cancel puts the cell back. Warning ("Continue?"): Yes puts it in, No goes on typing, Cancel puts the cell
 * back. Information: OK puts it in, Cancel puts the cell back. */
static void validation_alert(void)
{
    oc_alert a;
    char title[128], text[640];
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, NULL, (UBYTE *)"%s", NULL };
    size_t n;
    LONG pick;
    if (!core || !oc_core_invalid(core, &a)) return;
    to_latin1(a.title, strlen(a.title), title, sizeof title);
    n = to_latin1(a.message, strlen(a.message), text, sizeof text - 16);
    if (a.style == OWF_DV_WARNING) memcpy(text + n, "\n\nContinue?", 12);
    es.es_Title = (UBYTE *)title;
    es.es_GadgetFormat = (UBYTE *)(a.style == OWF_DV_WARNING ? "Yes|No|Cancel" : a.style == OWF_DV_INFORMATION ? "OK|Cancel"
                                                                                                         : "Retry|Cancel");
    pick = ask_window(title, text, (const char *)es.es_GadgetFormat);   /* EasyRequest cuts lines short on RTG */
    if (pick < 0) pick = EasyRequest(win, &es, NULL, (ULONG)text);
    ActivateWindow(win);                             /* typing goes on in the window, not in the Workbench */
    if (a.style == OWF_DV_STOP) {
        if (pick == 0) oc_edit_cancel(core);
    } else if (a.style == OWF_DV_WARNING) {
        if (pick == 1) result(oc_edit_commit_anyway(core, a.move), NULL);
        else if (pick == 0) oc_edit_cancel(core);
    } else {
        if (pick == 1) result(oc_edit_commit_anyway(core, a.move), NULL);
        else oc_edit_cancel(core);
    }
}

/* What a core result means to the user; 1 when it went well. */
static int result(int rc, const char *done)
{
    switch (rc) {
    case OC_INVALID:
        validation_alert();
        break;
    case OWF_OK:
        if (done) set_status(done);
        if (core && oc_core_undo_lost(core)) set_status("Done; it was too big to keep for Undo.");
        return 1;
    case OWF_ERR_MEMORY:
        tell("GFOpenCalc", "There is not enough memory to complete that.");
        break;
    case OWF_ERR_FORMAT:
        set_status("The formula has an error: it is kept, and shows #NAME?.");
        return 1;
    case OWF_ERR_UNSUPPORTED:
        set_status("That can't be done here.");
        break;
    default:
        set_status(owf_error_text(rc));
        break;
    }
    return 0;
}

/* ---- Painting the core's draw list ---- */

static LONG rgb_pen(unsigned long rgb)
{
    ogt_rgb c;
    c.r = (unsigned char)(rgb >> 16);
    c.g = (unsigned char)(rgb >> 8);
    c.b = (unsigned char)rgb;
    return ogt_pen_rgb(&ctx, c);
}

/* The accent, four fifths of the way to white: behind selected cells. */
static LONG selection_pen(void)
{
    ogt_rgb a = ogt_colour(&ctx, "accent");
    a.r = (unsigned char)(255 - (255 - a.r) / 5);
    a.g = (unsigned char)(255 - (255 - a.g) / 5);
    a.b = (unsigned char)(255 - (255 - a.b) / 5);
    return ogt_pen_rgb(&ctx, a);
}

/* The sheet's own colours, the same in every theme and mode, as Excel's and LibreOffice's: white paper,
 * light grey grid lines, light grey row numbers and column letters with dark text (those the selection
 * crosses a little darker). Exact on a true-colour screen; on a palette screen each is one shared pen
 * (ObtainBestPen), the nearest the screen has when it has no pen to spare. */
#define RGB_GRID 0xD4D4D4UL
#define RGB_HEADER 0xEBEBEBUL
#define RGB_HEADER_SELECTED 0xD2D2D2UL
#define RGB_HEADER_LINE 0xC0C0C0UL
#define RGB_HEADER_TEXT 0x333333UL
#define RGB_HEADER_TEXT_SELECTED 0x111111UL
#define RGB_TAB_TEXT 0x1C242DUL                      /* the chosen sheet's tab: white, as the sheet */

static LONG role_pen(int role, unsigned long rgb)
{
    switch (role) {
    case OC_ROLE_CELL_FILL:
    case OC_ROLE_CHART: return rgb_pen(rgb);             /* a chart's area, bars and lines */
    case OC_ROLE_SELECTION: return selection_pen();
    case OC_ROLE_GRID: return rgb_pen(RGB_GRID);
    case OC_ROLE_BORDER:
    case OC_ROLE_TEXT: return rgb == OWF_COLOUR_AUTO ? rgb_pen(0) : rgb_pen(rgb);
    case OC_ROLE_HEADER: return rgb_pen(RGB_HEADER);
    case OC_ROLE_HEADER_SELECTED: return rgb_pen(RGB_HEADER_SELECTED);
    case OC_ROLE_HEADER_LINE: return rgb_pen(RGB_HEADER_LINE);
    case OC_ROLE_CURSOR: return ogt_pen(&ctx, "accent");
    case OC_ROLE_FREEZE: return rgb_pen(0x707070UL);
    case OC_ROLE_COMMENT: return rgb_pen(0xD01010UL);        /* Excel's red mark */
    case OC_ROLE_NOTE: return rgb_pen(0xFFFFE1UL);           /* and its comment's pale yellow */
    case OC_ROLE_NOTE_LINE: return rgb_pen(0x808080UL);
    }
    return rgb_pen(0xFFFFFFUL);                      /* the paper, behind typing */
}

typedef struct { struct RastPort *rp; int ox, oy, w, h, limit_h; } canvas;

/* A box, cut to a clip box (grid coordinates) and the canvas. */
static void fill_cut(const canvas *cv, LONG pen, int x, int y, int w, int h, int cx, int cy, int cw, int ch)
{
    int x1 = x + w, y1 = y + h;
    if (cx < 0) { cw += cx; cx = 0; }
    if (cy < 0) { ch += cy; cy = 0; }
    if (cx + cw > cv->w) cw = cv->w - cx;
    if (cy + ch > cv->h) ch = cv->h - cy;
    if (x < cx) x = cx;
    if (y < cy) y = cy;
    if (x1 > cx + cw) x1 = cx + cw;
    if (y1 > cy + ch) y1 = cy + ch;
    if (x1 <= x || y1 <= y) return;
    ogt_box(cv->rp, pen, cv->ox + x, cv->oy + y, x1 - x, y1 - y);
}

static void fill_box(const canvas *cv, LONG pen, int x, int y, int w, int h)
{
    fill_cut(cv, pen, x, y, w, h, 0, 0, cv->w, cv->h);
}

/* A line: grid and header lines 1 pixel; a cell's border at its weight and in its dashes (the core's
 * oc_line_weight and oc_line_pattern), a medium or thick one centred on the cells' edge. */
static void paint_line(const canvas *cv, const oc_draw_item *it)
{
    LONG pen = role_pen(it->role, it->rgb);
    int border = it->role == OC_ROLE_BORDER, t = border ? oc_line_weight(it->line) : 1, k;
    UWORD pattern = border ? (UWORD)oc_line_pattern(it->line) : 0xFFFF;
    if (t <= 0) return;
    if (border && it->line == OWF_LINE_DOUBLE) {
        if (it->w) { fill_box(cv, pen, it->x, it->y - 1, it->w, 1); fill_box(cv, pen, it->x, it->y + 1, it->w, 1); }
        else { fill_box(cv, pen, it->x - 1, it->y, 1, it->h); fill_box(cv, pen, it->x + 1, it->y, 1, it->h); }
        return;
    }
    if (pattern != 0xFFFF) {
        /* a pattern: Draw, its ends cut to the canvas (the line's last pixel is x + w - 1) */
        int x0 = it->x, y0 = it->y, x1 = it->x + (it->w ? it->w - 1 : 0), y1 = it->y + (it->h ? it->h - 1 : 0);
        if (it->w) y0 = y1 = it->y - (t - 1) / 2;
        else x0 = x1 = it->x - (t - 1) / 2;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > cv->w - 1) x1 = cv->w - 1;
        if (y1 > cv->h - 1) y1 = cv->h - 1;
        if (x0 > x1 || y0 > y1) return;
        ogt_set_apen(cv->rp, pen);
        SetDrPt(cv->rp, pattern);
        for (k = 0; k < t; k++) {
            int dx = it->w ? 0 : k, dy = it->w ? k : 0;
            if (x0 + dx > cv->w - 1 || y0 + dy > cv->h - 1) break;
            Move(cv->rp, cv->ox + x0 + dx, cv->oy + y0 + dy);
            Draw(cv->rp, cv->ox + (it->w ? x1 : x0 + dx), cv->oy + (it->w ? y0 + dy : y1));
        }
        SetDrPt(cv->rp, 0xFFFF);
        return;
    }
    if (it->w)
        fill_box(cv, pen, it->x, it->y - (t - 1) / 2, it->w, t);
    else
        fill_box(cv, pen, it->x - (t - 1) / 2, it->y, t, it->h);
}

/* Text from x, cut to the clip box: letters wholly left of it skipped, those past it not drawn. th: the
 * rastport's font's height. */
static void text_cut(const canvas *cv, LONG pen, const char *utf8, unsigned flags, int x, int y, int cx, int cw, int th)
{
    char buf[512];
    struct TextExtent te;
    size_t n = to_latin1(utf8, strlen(utf8), buf, sizeof buf), start = 0;
    struct RastPort *rp = cv->rp;
    LONG fit;
    if (!n) return;
    if (cx < 0) { cw += cx; cx = 0; }
    if (cx + cw > cv->w) cw = cv->w - cx;
    if (cw <= 0 || y < 0 || y + th > cv->limit_h) return;
    SetSoftStyle(rp, soft_style(flags), FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    while (start < n && x < cx) {
        x += text_length(rp, buf + start, 1);
        start++;
    }
    if (start >= n || x >= cx + cw) return;
    fit = TextFit(rp, (STRPTR)buf + start, (ULONG)(n - start), &te, NULL, 1, (ULONG)(cx + cw - x), (ULONG)(th + 8));
    if (fit <= 0) return;
    ogt_set_apen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, cv->ox + x, cv->oy + y + rp->TxBaseline);
    Text(rp, (STRPTR)buf + start, (ULONG)fit);
}

static void paint_item(const canvas *cv, const oc_draw_item *it)
{
    switch (it->kind) {
    case OC_DRAW_FILL:
        fill_box(cv, role_pen(it->role, it->rgb), it->x, it->y, it->w, it->h);
        break;
    case OC_DRAW_LINE:
        paint_line(cv, it);
        break;
    case OC_DRAW_TEXT: {
        /* in the cell's font, placed in its box (y, h: the cell less its grid line, or one wrapped line) by
           its letters' ink, as Excel places text: bottom, its descenders 1 pixel above the grid line; top, its
           ascenders 1 pixel below the cell's top; centre, halfway */
        cell_font_entry *e = cell_font(it->font, it->size);
        struct TextFont *f = e ? e->font : font;
        int th = f ? f->tf_YSize : fh, ink_top = e ? e->ink_top : 0, ink_bottom = e ? e->ink_bottom : th - 1, y;
        if (!it->text) break;
        if (f) SetFont(cv->rp, f);
        if (it->valign == OWF_VALIGN_TOP) y = it->y + 1 - ink_top;
        else if (it->valign == OWF_VALIGN_CENTRE) y = it->y + (it->h - (ink_bottom - ink_top + 1)) / 2 - ink_top;
        else y = it->y + it->h - 2 - ink_bottom;
        text_cut(cv, role_pen(OC_ROLE_TEXT, it->rgb), it->text, it->flags, it->text_x, y, it->clip_x, it->clip_w, th);
        SetFont(cv->rp, font);
        break;
    }
    case OC_DRAW_HEADER: {
        int tw;
        char buf[24];
        fill_box(cv, role_pen(it->role, 0), it->x, it->y, it->w, it->h);
        if (!it->text) break;
        to_latin1(it->text, strlen(it->text), buf, sizeof buf);
        SetSoftStyle(cv->rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        tw = text_length(cv->rp, buf, strlen(buf));
        /* row numbers left-aligned a small inset in (the core's text_x), column letters centred */
        text_cut(cv, rgb_pen(it->flags ? RGB_HEADER_TEXT_SELECTED : RGB_HEADER_TEXT), it->text, 0,
                 it->align == OC_ALIGN_LEFT ? it->x + it->text_x : it->x + (it->w - tw) / 2, it->y + (it->h - fh) / 2, it->x,
                 it->w - 1, fh);
        break;
    }
    case OC_DRAW_FRAME: {
        LONG pen = role_pen(it->role, 0);
        int t = it->line > 0 ? it->line : 1;
        fill_cut(cv, pen, it->x, it->y, it->w, t, it->clip_x, it->clip_y, it->clip_w, it->clip_h);
        fill_cut(cv, pen, it->x, it->y + it->h - t, it->w, t, it->clip_x, it->clip_y, it->clip_w, it->clip_h);
        fill_cut(cv, pen, it->x, it->y, t, it->h, it->clip_x, it->clip_y, it->clip_w, it->clip_h);
        fill_cut(cv, pen, it->x + it->w - t, it->y, t, it->h, it->clip_x, it->clip_y, it->clip_w, it->clip_h);
        break;
    }
    case OC_DRAW_CARET:
        fill_box(cv, rgb_pen(0), it->x, it->y, 1, it->h);
        break;
    case OC_DRAW_FILTER: {                           /* an AutoFilter's button: Excel's grey box with its arrow */
        int xy[6], a = it->w / 4 > 1 ? it->w / 4 : 1, cx = it->x + it->w / 2, cy = it->y + it->h / 2;
        if (it->x < 0 || it->y < 0 || it->x + it->w >= cv->w || it->y + it->h >= cv->h) break;
        fill_box(cv, rgb_pen(0xF3F3F3UL), it->x, it->y, it->w, it->h);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x, it->y, it->w, 1);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x, it->y + it->h - 1, it->w, 1);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x, it->y, 1, it->h);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x + it->w - 1, it->y, 1, it->h);
        xy[0] = cv->ox + cx - a - 1; xy[1] = cv->oy + cy - a / 2;
        xy[2] = cv->ox + cx + a + 1; xy[3] = cv->oy + cy - a / 2;
        xy[4] = cv->ox + cx; xy[5] = cv->oy + cy + a;
        ogt_fill_poly(cv->rp, it->flags ? ogt_pen(&ctx, "accent") : rgb_pen(0x404040UL), xy, 3);   /* filtering: the accent */
        break;
    }
    case OC_DRAW_SEGMENT: {                          /* a chart's line: 1 pixel, cut to its pane by the core */
        int x0 = it->x, y0 = it->y, x1 = it->x + it->w, y1 = it->y + it->h;
        if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 || x0 >= cv->w || x1 >= cv->w || y0 >= cv->h || y1 >= cv->h) break;
        ogt_set_apen(cv->rp, role_pen(it->role, it->rgb));
        Move(cv->rp, cv->ox + x0, cv->oy + y0);
        Draw(cv->rp, cv->ox + x1, cv->oy + y1);
        break;
    }
    case OC_DRAW_POLY: {                             /* a chart's slice or marker: filled, cut to its pane by the core */
        int xy[2 * 256], k, n = it->npoints;
        if (!it->points || n < 3 || n > 256) break;
        for (k = 0; k < n; k++) {
            int px = it->points[2 * k], py = it->points[2 * k + 1];
            if (px < 0 || py < 0 || px > cv->w || py > cv->h) break;   /* its right and bottom edges are outside it */
            xy[2 * k] = cv->ox + px;
            xy[2 * k + 1] = cv->oy + py;
        }
        if (k == n) ogt_fill_poly(cv->rp, role_pen(it->role, it->rgb), xy, n);
        break;
    }
    case OC_DRAW_DROPDOWN: {                         /* a list's button beside the active cell: Excel's grey box, an arrow */
        int xy[6], a = it->w / 4 > 1 ? it->w / 4 : 1, cx = it->x + it->w / 2, cy = it->y + it->h / 2;
        if (it->x < 0 || it->y < 0 || it->x + it->w > cv->w || it->y + it->h > cv->h) break;
        fill_box(cv, rgb_pen(0xF3F3F3UL), it->x, it->y, it->w, it->h);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x, it->y, it->w, 1);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x, it->y + it->h - 1, it->w, 1);
        fill_box(cv, rgb_pen(0xA6A6A6UL), it->x + it->w - 1, it->y, 1, it->h);
        xy[0] = cv->ox + cx - a - 1; xy[1] = cv->oy + cy - a / 2;
        xy[2] = cv->ox + cx + a + 1; xy[3] = cv->oy + cy - a / 2;
        xy[4] = cv->ox + cx; xy[5] = cv->oy + cy + a;
        ogt_fill_poly(cv->rp, rgb_pen(0x404040UL), xy, 3);
        break;
    }
    case OC_DRAW_MARK: {                             /* a comment's: the box's top right half, when it is all in view */
        int xy[6];
        if (it->x < 0 || it->y < 0 || it->x + it->w >= cv->w || it->y + it->h >= cv->h || it->w < 1) break;
        xy[0] = cv->ox + it->x; xy[1] = cv->oy + it->y;
        xy[2] = cv->ox + it->x + it->w; xy[3] = cv->oy + it->y;
        xy[4] = cv->ox + it->x + it->w; xy[5] = cv->oy + it->y + it->h;
        ogt_fill_poly(cv->rp, role_pen(it->role, 0), xy, 3);
        break;
    }
    }
}

/* An off-screen bitmap like the window's, a little taller than the grid
 * (a row's text that goes past its foot lands there, not in memory that
 * isn't ours). 0 when there is no memory or no AllocBitMap. */
static int buffer_for(int w, int h)
{
    int want_h = h + fh + 8;
    if (buf_bm && buf_w >= w && buf_h >= want_h) return 1;
    if (buf_bm) { WaitBlit(); FreeBitMap(buf_bm); buf_bm = NULL; }
    if (((struct Library *)GfxBase)->lib_Version < 39) return 0;
    buf_bm = AllocBitMap((ULONG)w, (ULONG)want_h, GetBitMapAttr(win->RPort->BitMap, BMA_DEPTH), BMF_MINPLANES,
                         win->RPort->BitMap);
    if (!buf_bm) return 0;
    buf_w = w;
    buf_h = want_h;
    InitRastPort(&buf_rp);
    buf_rp.BitMap = buf_bm;
    return 1;
}

static void paint_grid(void)
{
    canvas cv;
    int i;
    if (!win || !core || grid_box.w <= 0 || grid_box.h <= 0) return;
    oc_view_set_size(core, grid_box.w, grid_box.h);
    if (oc_view_draw(core, &dl) != OWF_OK) set_status("There is not enough memory to draw all of the sheet.");
    if (buffer_for(grid_box.w, grid_box.h)) {
        cv.rp = &buf_rp;
        cv.ox = cv.oy = 0;
        cv.limit_h = buf_h;
    } else {
        cv.rp = win->RPort;
        cv.ox = grid_box.x;
        cv.oy = grid_box.y;
        cv.limit_h = grid_box.h;
    }
    cv.w = grid_box.w;
    cv.h = grid_box.h;
    SetFont(cv.rp, font);
    fill_box(&cv, rgb_pen(0xFFFFFFUL), 0, 0, cv.w, cv.h);
    for (i = 0; i < dl.n; i++)
        paint_item(&cv, &dl.items[i]);
    SetSoftStyle(cv.rp, 0, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (cv.rp == &buf_rp)
        BltBitMapRastPort(buf_bm, 0, 0, win->RPort, grid_box.x, grid_box.y, grid_box.w, grid_box.h, 0xC0);
}

/* ---- The sheet tabs ---- */

/* A hit area of the strip. */
static void tab_area(int x, int w, int what)
{
    if (tab_n >= MAX_TABS + 3) return;
    tab_x[tab_n] = x;
    tab_w[tab_n] = w;
    tab_sheet[tab_n] = what;
    tab_n++;
}

/* A scroll arrow's button: a small triangle pointing left or right, dimmed when there is nothing that way. */
static void draw_tab_arrow(struct RastPort *rp, int x, int y, int w, int h, int right, int live)
{
    int cx = x + w / 2, cy = y + h / 2, a = h / 4 > 2 ? h / 4 : 2, xy[6];
    ogt_fill(&ctx, rp, "tab", x, y, w, h);
    ogt_frame(rp, ogt_pen(&ctx, "group.line"), x, y - 1, w, h + 1);
    xy[0] = right ? cx - a / 2 : cx + a / 2;
    xy[1] = cy - a;
    xy[2] = right ? cx - a / 2 : cx + a / 2;
    xy[3] = cy + a;
    xy[4] = right ? cx + a / 2 + 1 : cx - a / 2 - 1;
    xy[5] = cy;
    /* dimmed: the theme's muted (the style guide, gfopengadtools #85, 4.2): in the Open theme #4a5663 on the tabs'
       #dde2e8 in the light mode (5.8:1, against tab.text's 12:1), #a9b4bf on #313c48 in the dark (5.3:1 against
       9.2:1); in Chrome 4.4:1 light and 5.3:1 dark, against tab.text's 8.3 and 8.0: dimmer than the arrow that
       works in both */
    ogt_fill_poly(rp, ogt_pen(&ctx, live ? "tab.text" : "muted"), xy, 3);
}

/* The tabs: as many as fit from tab_first (the core's oc_tab_strip), two arrows before them when not all
 * fit, "+" after them. The chosen sheet's tab is brought into view when the sheet changes (a click, the
 * keys, the menu), not while the arrows or the wheel scroll the strip. */
static void draw_tabs(void)
{
    struct RastPort *rp;
    owf_book *book;
    int x, s, h = tabs_box.h - 3, y = tabs_box.y + 1, cur, plus_w, arrow_w, avail, maxtab, *width;
    if (!win || !core || tabs_box.w <= 0) return;
    rp = win->RPort;
    book = oc_core_book(core);
    cur = oc_core_sheet(core);
    SetFont(rp, font);
    ogt_fill(&ctx, rp, "window", tabs_box.x, tabs_box.y, tabs_box.w, tabs_box.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), tabs_box.x, tabs_box.y, tabs_box.w);
    tab_n = 0;
    plus_w = ogt_text_width(rp, "+") + 16;
    arrow_w = fh + 4;
    avail = tabs_box.w - 4 - (plus_w + 2) - 4;
    maxtab = avail / 3 > 60 ? avail / 3 : 60;        /* a long name is cut short ("...") to a third of the strip */
    if (!(width = malloc((size_t)(book->nsheets > 0 ? book->nsheets : 1) * sizeof *width))) return;
    ogt_bold(rp, 1);                                 /* each tab as wide as when chosen: none moves on a click */
    for (s = 0; s < book->nsheets; s++) {
        char name[64];
        if (book->sheets[s].hidden && s != cur) { width[s] = 0; continue; }
        to_latin1(book->sheets[s].name, strlen(book->sheets[s].name), name, sizeof name);
        width[s] = ogt_text_width(rp, name) + 16;
        if (width[s] > maxtab) width[s] = maxtab;
        width[s] += 2;                               /* the gap after it */
    }
    ogt_bold(rp, 0);
    tab_end = oc_tab_strip(width, book->nsheets, avail, 2 * (arrow_w + 2), cur != tab_shown_sheet ? cur : -1,
                           &tab_first, &tab_scroll);
    tab_shown_sheet = cur;
    x = tabs_box.x + 4;
    if (tab_scroll) {
        int k, more_left = 0, more_right = 0;
        for (k = 0; k < tab_first; k++) if (width[k]) more_left = 1;
        for (k = tab_end; k < book->nsheets; k++) if (width[k]) more_right = 1;
        draw_tab_arrow(rp, x, y, arrow_w, h, 0, more_left);
        tab_area(x, arrow_w, TAB_LEFT);
        x += arrow_w + 2;
        draw_tab_arrow(rp, x, y, arrow_w, h, 1, more_right);
        tab_area(x, arrow_w, TAB_RIGHT);
        x += arrow_w + 2;
    }
    for (s = tab_first; s < tab_end && s < book->nsheets && tab_n < MAX_TABS; s++) {
        char name[64];
        int on = s == cur, tw = width[s] - 2;
        if (!width[s]) continue;
        to_latin1(book->sheets[s].name, strlen(book->sheets[s].name), name, sizeof name);
        ogt_bold(rp, on);
        /* the chosen sheet's tab white, as the sheet; the others the theme's tabs (THEME_SPEC.md 5) */
        if (on) ogt_box(rp, rgb_pen(0xFFFFFFUL), x, y, tw, h);
        else ogt_fill(&ctx, rp, "tab", x, y, tw, h);
        ogt_frame(rp, ogt_pen(&ctx, "group.line"), x, y - 1, tw, h + 1);
        if (on) ogt_hline(rp, rgb_pen(0xFFFFFFUL), x + 1, y - 1, tw - 2);
        ogt_text(rp, on ? rgb_pen(RGB_TAB_TEXT) : ogt_pen(&ctx, "tab.text"), x + 8, y + (h - fh) / 2, name, tw - 16);
        ogt_bold(rp, 0);
        tab_area(x, tw, s);
        x += tw + 2;
    }
    free(width);
    /* "+": a new sheet */
    ogt_fill(&ctx, rp, "tab", x, y, plus_w, h);
    ogt_frame(rp, ogt_pen(&ctx, "group.line"), x, y - 1, plus_w, h + 1);
    ogt_text(rp, ogt_pen(&ctx, "tab.text"), x + 8, y + (h - fh) / 2, "+", 0);
    tab_area(x, plus_w, TAB_PLUS);
}

/* The strip scrolled a tab left (way < 0) or right. */
static void scroll_tabs(int way)
{
    owf_book *book = oc_core_book(core);
    int s, cur = oc_core_sheet(core);
    if (way < 0) {
        for (s = tab_first - 1; s >= 0; s--)
            if (!book->sheets[s].hidden || s == cur) { tab_first = s; break; }
    } else if (tab_end < book->nsheets) {
        for (s = tab_first + 1; s < book->nsheets; s++)
            if (!book->sheets[s].hidden || s == cur) { tab_first = s; break; }
    }
    draw_tabs();
}

/* Which hit area is at x, y (a tab, "+" or an arrow: tab_sheet says which), or -1. */
static int tab_at(int mx, int my)
{
    int i;
    if (my < tabs_box.y || my >= tabs_box.y + tabs_box.h) return -1;
    for (i = 0; i < tab_n; i++)
        if (mx >= tab_x[i] && mx < tab_x[i] + tab_w[i]) return i;
    return -1;
}

/* ---- The formula bar, scrollers, status, title ---- */

static void update_bar(void)
{
    char name[40], latin[1024];
    const char *in;
    if (!win || !core) return;
    oc_core_name_box(core, name, sizeof name);
    if (g_name && strcmp(name, shown_name)) {
        strcpy(shown_name, name);
        GT_SetGadgetAttrs(g_name, win, NULL, GTST_String, (ULONG)shown_name, TAG_DONE);
    }
    if (bar_active) return;                          /* the user is typing there */
    in = oc_core_input_text(core);
    to_latin1(in, strlen(in), latin, sizeof latin);
    if (g_input && strcmp(latin, shown_input)) {
        strcpy(shown_input, latin);
        GT_SetGadgetAttrs(g_input, win, NULL, GTST_String, (ULONG)shown_input, TAG_DONE);
    }
}

static void update_scrollers(void)
{
    int axis;
    for (axis = 0; axis < 2; axis++) {
        struct Gadget *g = axis ? g_hscroll : g_vscroll;
        unsigned first, vis, total;
        if (!g) continue;
        oc_view_scroller(core, axis, &first, &vis, &total);
        if (total > 32767) {                         /* a scroller counts in words: scale the sheet's million rows */
            unsigned div = total / 32767 + 1;
            first /= div;
            vis = vis / div ? vis / div : 1;
            total /= div;
        }
        if (first == sc_first[axis] && vis == sc_vis[axis] && total == sc_total[axis]) continue;
        sc_first[axis] = first;
        sc_vis[axis] = vis;
        sc_total[axis] = total;
        GT_SetGadgetAttrs(g, win, NULL, GTSC_Top, first, GTSC_Total, total, GTSC_Visible, vis, TAG_DONE);
    }
}

/* A scroller moved to top (in its units). */
static void scrolled(int axis, unsigned top)
{
    unsigned first, vis, total, t, l, frozen;
    owf_sheet *s = &oc_core_book(core)->sheets[oc_core_sheet(core)];
    oc_view_scroller(core, axis, &first, &vis, &total);
    if (total > 32767) top *= total / 32767 + 1;
    oc_view_scroll_pos(core, &t, &l);
    frozen = axis ? s->freeze_cols : s->freeze_rows;
    if (axis) oc_view_scroll_to(core, t, frozen + top);
    else oc_view_scroll_to(core, frozen + top, l);
}

static void draw_status(void)
{
    char s[128], m[160];
    unsigned row, col;
    if (!win || !core) return;
    oc_core_active(core, &row, &col);
    oc_cell_name(row, col, s);
    if (oc_core_editing(core)) strcat(s, oc_core_editing(core) == OC_EDIT_ENTER ? "  Enter" : "  Edit");
    ogt_status_set(&sbar, 0, s);
    oc_core_status(core, m, sizeof m);
    to_latin1(m, strlen(m), s, sizeof s);
    ogt_status_set(&sbar, 1, s);
    ogt_status_set(&sbar, 2, status);
    snprintf(s, sizeof s, "%s  %d%%", current_format[0] ? current_format : "New", oc_view_zoom(core));
    ogt_status_set(&sbar, 3, s);
}

static void update_window_title(void)
{
    static char title[sizeof current_path + 16];  /* static: Intuition keeps the pointer */
    const char *name = current_path[0] ? leaf(current_path) : "Untitled";
    snprintf(title, sizeof title, "%s%s - GFOpenCalc", core && oc_core_is_dirty(core) ? "*" : "", name);
    if (win) SetWindowTitles(win, (STRPTR)title, (STRPTR)-1);
}

static void update_tools(void)
{
    if (!core) return;
    ogt_toolbar_enable(&tb, C_UNDO, oc_core_can_undo(core));
    ogt_toolbar_enable(&tb, C_REDO, oc_core_can_redo(core));
}

/* After anything: the grid, the bar, the tabs, the status and the title. */
static void refresh(void)
{
    if (!win) return;
    if (defer_repaint) { repaint_pending = 1; return; }
    update_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    paint_grid();
    update_scrollers();
    update_bar();
    draw_tabs();
    draw_status();
    update_window_title();
}

static void draw_all(void)
{
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    SetFont(rp, font);
    ogt_fill(&ctx, rp, "window", area.x, area.y, area.w, area.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), toolbar_box.x, toolbar_box.y + toolbar_box.h - 1, toolbar_box.w);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), bar_box.x, bar_box.y + bar_box.h - 1, bar_box.w);
    if (glist2) { RefreshGList(glist2, win, NULL, -1); GT_RefreshWindow(win, NULL); }
    if (g_name && g_input)                           /* "=" between the name and what the cell holds */
        ogt_text(rp, ogt_pen(&ctx, "muted"), g_name->LeftEdge + g_name->Width + 8, bar_box.y + (bar_box.h - fh) / 2, "=", 0);
    sc_first[0] = sc_first[1] = sc_vis[0] = sc_vis[1] = sc_total[0] = sc_total[1] = (unsigned)-1;
    refresh();
}

/* ---- The window ---- */

static void free_gadgets(void)
{
    if (!win) return;
    if (chain) { RemoveGList(win, chain, -1); chain = NULL; }
    if (glist2) { RemoveGList(win, glist2, -1); FreeGadgets(glist2); glist2 = NULL; }
    g_name = g_input = g_vscroll = g_hscroll = NULL;
    strcpy(shown_name, "\1");                       /* differs from anything: the bar is filled again */
    strcpy(shown_input, "\1");
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    struct NewGadget ng;
    struct Gadget *g;
    int tbh, use_style, lh = fh + 6, namew, x, tabs_y, grid_y;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);
    sw = fh + 4 < 16 ? 16 : fh + 4;

    use_style = area.w < 600 ? OGT_TB_ICONS : tb_style;
    ogt_toolbar_set(&tb, tools, sizeof tools / sizeof tools[0], use_style);
    update_tools();
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 5, area.y + 3, area.w - 10);
    toolbar_box = (box){ area.x, area.y, area.w, tbh + 7 };
    bar_box = (box){ area.x, toolbar_box.y + toolbar_box.h, area.w, lh + 8 };
    status_box = (box){ area.x, area.y + area.h - fh - 10, area.w, fh + 10 };
    tabs_y = status_box.y - (fh + 8);
    grid_y = bar_box.y + bar_box.h;
    grid_box = (box){ area.x, grid_y, area.w - sw, tabs_y - grid_y };
    tabs_box = (box){ area.x, tabs_y, area.w * 3 / 5, fh + 8 };

    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);

    g = CreateContext(&glist2);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    namew = ogt_text_width(rp, "XFD1048576") + 16;
    x = bar_box.x + 6;
    ng.ng_LeftEdge = x; ng.ng_TopEdge = bar_box.y + 4; ng.ng_Width = namew; ng.ng_Height = lh;
    ng.ng_GadgetID = GID_NAME;
    g = g_name = CreateGadget(STRING_KIND, g, &ng, GTST_MaxChars, 32, GA_TabCycle, FALSE, TAG_DONE);
    x += namew + 8 + ogt_text_width(rp, "=") + 8;
    ng.ng_LeftEdge = x; ng.ng_Width = bar_box.x + bar_box.w - 6 - x; ng.ng_GadgetID = GID_INPUT;
    g = g_input = CreateGadget(STRING_KIND, g, &ng, GTST_MaxChars, 1000, GA_TabCycle, FALSE, GA_Immediate, TRUE, TAG_DONE);
    ng.ng_LeftEdge = grid_box.x + grid_box.w; ng.ng_TopEdge = grid_box.y; ng.ng_Width = sw; ng.ng_Height = grid_box.h;
    ng.ng_GadgetID = GID_VSCROLL;
    g = g_vscroll = CreateGadget(SCROLLER_KIND, g, &ng, GTSC_Top, 0, GTSC_Total, 100, GTSC_Visible, 10, GTSC_Arrows, sw,
                                 PGA_Freedom, LORIENT_VERT, GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
    ng.ng_LeftEdge = tabs_box.x + tabs_box.w; ng.ng_TopEdge = tabs_y + 1; ng.ng_Width = area.w - tabs_box.w - sw;
    ng.ng_Height = tabs_box.h - 2; ng.ng_GadgetID = GID_HSCROLL;
    g = g_hscroll = CreateGadget(SCROLLER_KIND, g, &ng, GTSC_Top, 0, GTSC_Total, 100, GTSC_Visible, 10, GTSC_Arrows, sw,
                                 PGA_Freedom, LORIENT_HORIZ, GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
    if (g) AddGList(win, glist2, ~0, -1, NULL);
    else if (glist2) { FreeGadgets(glist2); glist2 = NULL; g_name = g_input = g_vscroll = g_hscroll = NULL; }

    /* the core's view: column letters a line high; the row numbers' bar as wide as the largest number showing
       (Dale, 25 October 2026: it starts narrow and stretches as the numbers grow), 6 pixels each side */
    {
        int dw = 0, k;
        for (k = 0; k < 10; k++) {
            char digit[2] = { (char)('0' + k), 0 };
            int w = ogt_text_width(rp, digit);
            if (w > dw) dw = w;
        }
        oc_view_set_headers(core, ogt_text_width(rp, "1048576") + 10, fh + 4);
        oc_view_set_row_numbers(core, dw, 12);
    }
    oc_view_set_size(core, grid_box.w, grid_box.h);
    ogt_status_init(&sbar, win, &ctx, 4);
    ogt_status_layout(&sbar, status_box.x, status_box.y, status_box.w, status_box.h);
}

static void relayout(void)
{
    layout();
    draw_all();
}

static void apply_theme(const char *name)
{
    if (name && *name) snprintf(theme_name, sizeof theme_name, "%s", name);
    ogt_ctx_free(&ctx);
    load_theme();
    {
        struct TextFont *f = ogt_open_font(&theme, scr, &font_attr);
        if (f) {
            close_cell_fonts();                      /* some may be the window's font, or the theme's */
            if (font) CloseFont(font);
            font = f;
            fh = font->tf_YSize;
            SetFont(&mrp, font);
            SetFont(&crp, font);
        }
    }
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    if (win) relayout();
}

/* The part of the screen a full-size window may have (GFOpenWrite 2's free_area()): below the title bar,
 * less GFOpenDock's strip; ENV:GFOpenDock/Free wins when the dock publishes one. */
static void free_area(int *l, int *t, int *w, int *h)
{
    char buf[48];
    struct Window *dw;
    ULONG lock;
    LONG got;
    int top = scr->BarHeight + 1, bottom = scr->Height, left = 0, right = scr->Width, a, b, c, d;
    got = GetVar((STRPTR)"GFOpenDock/Free", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (got > 0 && sscanf(buf, "%d %d %d %d", &a, &b, &c, &d) == 4 && c >= 400 && d >= 200 && a >= 0 && b >= 0 &&
        a + c <= scr->Width && b + d <= scr->Height) {
        *l = a;
        *t = b < top ? top : b;
        *w = c;
        *h = b + d - *t;
        return;
    }
    lock = LockIBase(0);
    for (dw = scr->FirstWindow; dw; dw = dw->NextWindow) {
        if (!dw->ScreenTitle || strncmp((const char *)dw->ScreenTitle, "GFOpenDock", 10) != 0)
            continue;
        if (dw->Width >= dw->Height) {
            if (dw->TopEdge + dw->Height / 2 > scr->Height / 2) {
                if (dw->TopEdge < bottom) bottom = dw->TopEdge;
            } else if (dw->TopEdge + dw->Height > top)
                top = dw->TopEdge + dw->Height;
        } else {
            if (dw->LeftEdge + dw->Width / 2 > scr->Width / 2) {
                if (dw->LeftEdge < right) right = dw->LeftEdge;
            } else if (dw->LeftEdge + dw->Width > left)
                left = dw->LeftEdge + dw->Width;
        }
    }
    UnlockIBase(lock);
    if (right - left < 400 || bottom - top < 200) {
        left = 0;
        right = scr->Width;
        top = scr->BarHeight + 1;
        bottom = scr->Height;
    }
    *l = left;
    *t = top;
    *w = right - left;
    *h = bottom - top;
}

/* The first size, every GFOpen app's (10 October 2026): 800 x 600 centred in the free area, never bigger. */
#define START_W 800
#define START_H 600
static void start_box(int *l, int *t, int *w, int *h)
{
    int al, at, aw, ah;
    free_area(&al, &at, &aw, &ah);
    *w = aw < START_W ? aw : START_W;
    *h = ah < START_H ? ah : START_H;
    *l = al + (aw - *w) / 2;
    *t = at + (ah - *h) / 2;
}

static int open_window(void)
{
    int l, t, w, h;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    load_theme();
    if (!(font = ogt_open_font(&theme, scr, &font_attr))) return 0;
    fh = font->tf_YSize;
    InitRastPort(&mrp);
    SetFont(&mrp, font);
    InitRastPort(&crp);
    SetFont(&crp, font);
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    start_box(&l, &t, &w, &h);
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenCalc",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, l, WA_Top, t,
        WA_MinWidth, w < 400 ? w : 400, WA_MinHeight, h < 260 ? h : 260,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                  IDCMP_INTUITICKS | IDCMP_EXTENDEDMOUSE,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    oc_view_set_measure(core, measure, NULL);
    relayout();
    update_window_title();
    return 1;
}

static void close_window(void)
{
    if (buf_bm) { WaitBlit(); FreeBitMap(buf_bm); buf_bm = NULL; }
    if (win) {
        ClearMenuStrip(win);
        free_gadgets();
        CloseWindow(win);
        win = NULL;
    }
    ogt_ctx_free(&ctx);
    if (menu) { FreeMenus(menu); menu = NULL; }
    close_cell_fonts();                              /* before the window's font: some may stand in for it */
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

/* A one-line requester (GFOpenWrite 2's): the text in Latin-1, as typed. 1 for OK. */
static int text_requester(const char *title, const char *label, const char *initial, char *out, size_t out_size)
{
    struct Gadget *list = NULL, *last, *gtext, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    STRPTR value = NULL;
    int done = 0, ok = 0, ww = 430, wh = 88, lw;
    if (!scr || !vi || !out || out_size < 2) return 0;
    lw = TextLength(&mrp, (STRPTR)label, strlen(label)) + 16;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr; ng.ng_VisualInfo = vi;
    last = CreateContext(&list);
    ng.ng_LeftEdge = lw; ng.ng_TopEdge = 15; ng.ng_Width = ww - lw - 18; ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (STRPTR)label; ng.ng_GadgetID = 1; ng.ng_Flags = PLACETEXT_LEFT;
    last = gtext = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)(initial ? initial : ""),
                                GTST_MaxChars, (ULONG)(out_size - 1), GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_GadgetText = (STRPTR)"OK"; ng.ng_GadgetID = 2;
    ng.ng_LeftEdge = ww - 188; ng.ng_TopEdge = 48; ng.ng_Width = 78; ng.ng_Height = fh + 8;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 3; ng.ng_LeftEdge = ww - 98;
    last = gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gtext || !gok || !gcancel) goto out;
    wh = 48 + fh + 8 + 14;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh,
        WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
    ActivateGadget(gtext, rw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        Wait(1UL << rw->UserPort->mp_SigBit);
        while ((m = GT_GetIMsg(rw->UserPort))) {
            ULONG cls = m->Class;
            UWORD id = m->IAddress ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { done = 1; break; }
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                if (id == 1 || id == 2) { ok = 1; done = 1; }
                else if (id == 3) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(gtext, rw, NULL, GTST_String, (ULONG)&value, TAG_DONE);
        snprintf(out, out_size, "%s", value ? (char *)value : "");
        if (!out[0]) ok = 0;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

/* ---- Files ---- */

static int confirm_discard_changes(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenCalc",
                             (UBYTE *)"The workbook has unsaved changes.\n\nDiscard them?", (UBYTE *)"Discard|Cancel" };
    if (!core || !oc_core_is_dirty(core)) return 1;
    return EasyRequest(win, &es, NULL) != 0;
}

static int ask_file(int save, const char *title, const char *initial, char *out, size_t out_size)
{
    struct FileRequester *fr;
    char last[512];
    int ok = 0;
    lastdir_get("GFOpenCalc", last, sizeof last);
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)title,
        ASLFR_InitialDrawer, (ULONG)last,
        ASLFR_Window, (ULONG)win,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save ? TRUE : FALSE,
        ASLFR_InitialFile, (ULONG)(initial ? initial : ""),
        ASLFR_PositiveText, (ULONG)(save ? "Save" : "Open"),
        TAG_DONE);
    if (!fr) return 0;
    if (AslRequestTags(fr, TAG_DONE) && fr->fr_File[0]) {
        snprintf(out, out_size, "%s", fr->fr_Drawer);
        lastdir_put("GFOpenCalc", (const char *)fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

/* The file's leaf with another extension. */
static void with_extension(const char *path, const char *ext, char *out, size_t size)
{
    char *dot;
    put_text(out, size, path[0] ? leaf(path) : "Untitled");
    dot = strrchr(out, '.');
    if (dot) *dot = 0;
    if (strlen(out) + strlen(ext) + 2 < size) {
        strcat(out, ".");
        strcat(out, ext);
    }
}

/* The formats that can be written, for a message: "CSV (.csv)". */
static void writable_formats(char *out, size_t size)
{
    int i, n = owf_book_format_count();
    out[0] = 0;
    for (i = 0; i < n; i++) {
        const owf_book_format *f = owf_book_format_at(i);
        char one[96], ext[24];
        if (!f || !f->export) continue;
        snprintf(ext, sizeof ext, "%s", f->extensions);
        if (strchr(ext, ' ')) *strchr(ext, ' ') = 0;
        snprintf(one, sizeof one, "%s%s (.%s)", out[0] ? ", " : "", f->description, ext);
        if (strlen(out) + strlen(one) + 1 < size) strcat(out, one);
    }
}

/* ---- The notes requester ----
 * What a file's import or export left out, in GFOpenCalc's own window: EasyRequest cut its lines short on the
 * lab's RTG screen ("Formatting of whole rows is kept on their cel"). The window is as wide as its widest
 * line in the window's font, up to three quarters of the screen (longer lines wrap at spaces), and as tall
 * as its lines, up to the screen (the rest left out, and said so); the OK button below the last line. A
 * line starting with NOTE_BULLET is a bulleted note: a round dot, its text (and the lines it wraps onto)
 * indented after it. Return, Escape, OK or the close gadget close it. ask_window is the same window with
 * other buttons ("Retry|Cancel", as EasyRequest's), side by side under the text. */
#define NOTE_LINES 48
#define NOTE_TEXT 4096
#define NOTE_BULLET '\001'

typedef struct { char *text; int indent, bullet; } note_line;

/* The text's paragraphs (Latin-1, '\n' between them) broken to maxw pixels in rp's font into buf's
 * NUL-ended lines. The count. */
static int break_note_lines(struct RastPort *rp, const char *text, int maxw, int indent, char *buf, size_t size,
                            note_line *line, int max)
{
    size_t o = 0;
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        const char *end = strchr(p, '\n'), *q;
        size_t len = end ? (size_t)(end - p) : strlen(p);
        int bullet = len && *p == NOTE_BULLET, first = 1;
        if (bullet) { p++; len--; }
        if (!len) {                                  /* an empty line */
            if (o + 1 > size) return n;
            buf[o] = 0;
            line[n].text = buf + o++;
            line[n].indent = line[n].bullet = 0;
            n++;
        }
        while (len && n < max) {
            size_t take = len, fit;
            struct TextExtent te;
            int room = maxw - (bullet ? indent : 0);
            fit = (size_t)TextFit(rp, (STRPTR)p, (ULONG)len, &te, NULL, 1, (ULONG)(room > 0 ? room : 1), 32767);
            if (fit < len) {                         /* back to the last space that fits; else a cut word */
                for (q = p + fit; q > p && *q != ' '; q--) ;
                take = q > p ? (size_t)(q - p) : (fit ? fit : 1);
            }
            if (o + take + 1 > size) return n;
            memcpy(buf + o, p, take);
            buf[o + take] = 0;
            line[n].text = buf + o;
            line[n].indent = bullet ? indent : 0;
            line[n].bullet = bullet && first;
            n++;
            first = 0;
            o += take + 1;
            p += take;
            len -= take;
            while (len && *p == ' ') { p++; len--; }
        }
        if (!end) break;
        p = end + 1;
    }
    return n;
}

#define ASK_BUTTONS 4

/* The window with buttons, as EasyRequest's gadget format gives them ("Yes|No|Cancel"): which was chosen as
 * EasyRequest says it (the first 1, the next 2..., the last 0). Return chooses the first; Escape and the close
 * gadget the last. -1 when the window could not be opened (the caller asks otherwise). */
static int ask_window(const char *title, const char *text, const char *buttons)
{
    struct Gadget *list = NULL, *gok = NULL;
    struct NewGadget ng;
    struct Window *rw;
    struct RastPort trp;
    char *buf, names[ASK_BUTTONS][32];
    note_line *line;
    int n, i, ww = 0, wh, okw = 0, okh = fh + 8, lh = fh + 2, maxw, maxlines, cut = 0, done = 0, text_h, indent;
    int nb = 0, row_w, chosen = 0;
    const char *b = buttons;
    while (*b && nb < ASK_BUTTONS) {                 /* "Retry|Cancel": the buttons' names */
        size_t k = strcspn(b, "|");
        snprintf(names[nb], sizeof names[nb], "%.*s", (int)(k < 31 ? k : 31), b);
        nb++;
        b += k;
        if (*b == '|') b++;
    }
    if (!nb || !scr || !vi || !font) return -1;
    buf = malloc(NOTE_TEXT);
    line = malloc(NOTE_LINES * sizeof *line);
    if (!buf || !line) { free(buf); free(line); return -1; }
    InitRastPort(&trp);
    SetFont(&trp, font);
    indent = fh;                                     /* the dot and a space */
    maxw = scr->Width * 3 / 4 - 32;
    n = break_note_lines(&trp, text, maxw, indent, buf, NOTE_TEXT, line, NOTE_LINES);
    maxlines = (scr->Height - scr->BarHeight - 40 - okh - 40) / lh;
    if (maxlines < 3) maxlines = 3;
    if (n > maxlines) { n = maxlines - 1; cut = 1; }
    for (i = 0; i < n; i++) {
        int w = line[i].indent + TextLength(&trp, (STRPTR)line[i].text, strlen(line[i].text));
        if (w > ww) ww = w;
    }
    for (i = 0; i < nb; i++) {                       /* the buttons alike, each as wide as the widest name */
        int w = TextLength(&trp, (STRPTR)names[i], strlen(names[i])) + 48;
        if (w > okw) okw = w;
    }
    row_w = nb * okw + (nb - 1) * 16;
    ww += 32;
    if (ww < row_w + 32) ww = row_w + 32;
    if (ww < 280) ww = 280;
    text_h = 14 + (n + cut) * lh;                    /* every line drawn, the "(and more)" too */
    wh = text_h + 12 + okh + 10;
    /* the window first, then its button from the window's own borders (a gadget's place counts from the
       window's corner, its title bar included) */
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh,
        WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | BUTTONIDCMP,
        TAG_DONE);
    if (rw) {
        memset(&ng, 0, sizeof ng);
        ng.ng_TextAttr = &font_attr;
        ng.ng_VisualInfo = vi;
        gok = CreateContext(&list);
        for (i = 0; i < nb && gok; i++) {
            ng.ng_LeftEdge = rw->BorderLeft + (ww - row_w) / 2 + i * (okw + 16);
            ng.ng_TopEdge = rw->BorderTop + text_h + 12;
            ng.ng_Width = okw;
            ng.ng_Height = okh;
            ng.ng_GadgetText = (STRPTR)names[i];
            ng.ng_GadgetID = (UWORD)(i + 1);
            gok = CreateGadget(BUTTON_KIND, gok, &ng, TAG_DONE);
        }
        if (gok) AddGList(rw, list, (UWORD)~0, -1, NULL);
    }
    if (!rw || !gok) {
        if (rw) CloseWindow(rw);
        if (list) FreeGadgets(list);
        free(buf);
        free(line);
        return -1;
    }
    while (!done) {
        struct IntuiMessage *m;
        int x = rw->BorderLeft, y = rw->BorderTop, k, r = fh / 5 > 2 ? fh / 5 : 2;
        struct RastPort *rp = rw->RPort;
        SetFont(rp, font);
        ogt_fill(&ctx, rp, "window", x, y, ww, text_h + 6);
        for (k = 0; k < n; k++) {
            int ly = y + 14 + k * lh;
            if (line[k].bullet)                      /* a round dot on the text's middle */
                ogt_fill_circle(rp, ogt_pen(&ctx, "label"), x + 16 + r, ly + fh / 2, r);
            ogt_text(rp, ogt_pen(&ctx, "label"), x + 16 + line[k].indent, ly, line[k].text, 0);
        }
        if (cut)
            ogt_text(rp, ogt_pen(&ctx, "muted"), x + 16, y + 14 + n * lh, "(and more)", 0);
        ogt_fill(&ctx, rp, "window", x, y + text_h + 6, ww, wh - text_h - 6);
        RefreshGList(list, rw, NULL, -1);
        GT_RefreshWindow(rw, NULL);
        for (;;) {
            int redraw = 0;
            Wait(1UL << rw->UserPort->mp_SigBit);
            while ((m = GT_GetIMsg(rw->UserPort))) {
                ULONG cls = m->Class;
                UWORD code = m->Code;
                int id = cls == IDCMP_GADGETUP && m->IAddress ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
                GT_ReplyIMsg(m);
                if (cls == IDCMP_GADGETUP && id >= 1 && id <= nb) { chosen = id == nb ? 0 : id; done = 1; }
                else if (cls == IDCMP_CLOSEWINDOW || (cls == IDCMP_VANILLAKEY && code == 27)) { chosen = 0; done = 1; }
                else if (cls == IDCMP_VANILLAKEY && code == 13) { chosen = nb > 1 ? 1 : 0; done = 1; }
                else if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); redraw = 1; }
            }
            if (done || redraw) break;
        }
    }
    RemoveGList(rw, list, -1);
    CloseWindow(rw);
    FreeGadgets(list);
    free(buf);
    free(line);
    return chosen;
}

static void notes_window(const char *title, const char *text)
{
    if (ask_window(title, text, "OK") < 0) tell(title, text);
}

/* The report's notes, when they say something was left out or shown otherwise. */
static void show_report(const char *title, owf_report *report)
{
    char *text;
    size_t used = 0;
    int i, n = report ? owf_report_count(report) : 0, any = 0;
    if (!n || !(text = malloc(NOTE_TEXT))) return;
    used = (size_t)snprintf(text, NOTE_TEXT, "%s",
                            "Some of the workbook can't be shown as it was made:\n\n");
    for (i = 0; i < n; i++) {
        owf_note_level level;
        int times;
        const char *note = owf_report_note(report, i, &level, &times);
        char latin[300];
        size_t k;
        if (level == OWF_NOTE_INFO || !note) continue;
        k = to_latin1(note, strlen(note), latin, sizeof latin);
        if (used + k + 3 >= NOTE_TEXT) break;
        text[used++] = NOTE_BULLET;
        memcpy(text + used, latin, k);
        used += k;
        text[used++] = '\n';
        text[used] = 0;
        any = 1;
    }
    if (any) notes_window(title, text);
    free(text);
}

static void open_book(const char *path)
{
    owf_report *report = owf_report_new();
    const owf_book_format *used = NULL;
    char msg[600];
    int rc = oc_core_open(core, path, NULL, report, &used);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "GFOpenCalc could not open the workbook.\n\n%s", owf_error_text(rc));
        tell("Open workbook", msg);
        owf_report_free(report);
        return;
    }
    snprintf(current_path, sizeof current_path, "%s", path);
    snprintf(current_format, sizeof current_format, "%s", used ? used->name : "");
    sc_first[0] = sc_first[1] = (unsigned)-1;
    snprintf(msg, sizeof msg, "%s opened.", leaf(path));
    set_status(msg);
    show_report("Open workbook", report);
    owf_report_free(report);
}

/* Saves (keep_name: as the workbook's file; else a copy, an export); format NULL: by the name. 1 when it
 * was written. */
static int save_book_as(const char *path, const char *format, int keep_name)
{
    owf_report *report = owf_report_new();
    char msg[600], list[300];
    int rc = keep_name ? oc_core_save(core, path, format, report) : oc_core_export(core, path, format, report);
    if (rc != OWF_OK) {
        writable_formats(list, sizeof list);
        if (rc == OWF_ERR_FORMAT || rc == OWF_ERR_UNSUPPORTED)
            snprintf(msg, sizeof msg, "GFOpenCalc can't write that kind of file yet.\n\nIt can write: %s.", list);
        else
            snprintf(msg, sizeof msg, "GFOpenCalc could not save the workbook.\n\n%s", owf_error_text(rc));
        tell("Save workbook", msg);
        owf_report_free(report);
        return 0;
    }
    if (keep_name) {
        const owf_book_format *f = format ? owf_book_format_named(format) : owf_book_format_for_filename(path);
        snprintf(current_path, sizeof current_path, "%s", path);
        if (f) snprintf(current_format, sizeof current_format, "%s", f->name);
    }
    snprintf(msg, sizeof msg, keep_name ? "%s saved." : "%s written.", leaf(path));
    set_status(msg);
    show_report("Save workbook", report);
    owf_report_free(report);
    return 1;
}

static void do_save_as(void)
{
    char path[512], initial[160];
    with_extension(current_path, "csv", initial, sizeof initial);
    if (ask_file(1, "Save GFOpenCalc workbook", initial, path, sizeof path))
        save_book_as(path, NULL, 1);
}

static void do_save(void)
{
    const owf_book_format *f = current_format[0] ? owf_book_format_named(current_format) : NULL;
    if (current_path[0] && f && f->export) save_book_as(current_path, NULL, 1);
    else do_save_as();                               /* new, or a kind of file that can be read but not written */
}

static void do_export_csv(void)
{
    char path[512], initial[160];
    with_extension(current_path, "csv", initial, sizeof initial);
    if (ask_file(1, "Export the sheet as CSV", initial, path, sizeof path))
        save_book_as(path, "csv", 0);
}

/* The same reusable core renderer backs Export PDF and Print. By default
 * all visible sheets are printed, fitted to one page wide with gridlines;
 * the core paginates long sheets and leaves this live view untouched. */
static int write_pdf(const char *path, const char *report_title)
{
    oc_pdf_options options;
    unsigned char *data = NULL;
    size_t length = 0;
    owf_report *report = owf_report_new();
    BPTR f = 0;
    int rc, ok = 0;
    oc_pdf_options_init(&options);
    rc = oc_core_pdf(core, &options, &data, &length, report);
    if (rc == OWF_OK && length <= 0x7FFFFFFFUL && (f = Open((STRPTR)path, MODE_NEWFILE))) {
        ok = Write(f, data, (LONG)length) == (LONG)length;
        Close(f);
    }
    if (report_title)
        show_report(report_title, report);
    owf_report_free(report);
    owf_free(data);
    return ok;
}

static void do_export_pdf(void)
{
    char path[512], initial[160], msg[600];
    with_extension(current_path, "pdf", initial, sizeof initial);
    if (!ask_file(1, "Export workbook as PDF", initial, path, sizeof path))
        return;
    if (write_pdf(path, "Export PDF"))
        snprintf(msg, sizeof msg, "%.200s written: all visible sheets, fitted one page wide.", leaf(path));
    else
        snprintf(msg, sizeof msg, "%.200s could not be written.", leaf(path));
    set_status(msg);
}

/* As the rest of GFOffice: make a temporary PDF, then let GFOpenPrint own
 * preview, printer selection and physical/network output. */
#define OPENPRINT "C:GFOpenPrint"
static void do_print(void)
{
    char path[200], name[160], cmd[360];
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR lock, in, out;
    me->pr_WindowPtr = (APTR)-1;
    lock = Lock((STRPTR)OPENPRINT, ACCESS_READ);
    me->pr_WindowPtr = was;
    if (!lock) {
        tell("Print", "Printing needs GFOpenPrint (C:GFOpenPrint).\n\nExport PDF still works.");
        return;
    }
    UnLock(lock);
    with_extension(current_path, "pdf", name, sizeof name);
    snprintf(path, sizeof path, "T:%s", name);
    if (!write_pdf(path, "Print")) {
        set_status("The workbook couldn't be made ready to print.");
        return;
    }
    snprintf(cmd, sizeof cmd, "%s \"%s\"", OPENPRINT, path);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out,
                                  NP_StackSize, 65536, TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        set_status("GFOpenPrint couldn't be started.");
    } else {
        set_status("GFOpenPrint's Print window is open for this workbook.");
    }
}

static void new_book(void)
{
    if (!confirm_discard_changes()) return;
    if (!result(oc_core_set_book(core, NULL), "New workbook.")) return;
    current_path[0] = 0;
    current_format[0] = 0;
}

static void do_open(void)
{
    char path[512];
    if (!confirm_discard_changes()) return;
    if (ask_file(0, "Open workbook", "", path, sizeof path)) open_book(path);
}

/* ---- Recovery: the workbook kept every minute while it has changes not yet kept (ow_autosave.h) ---- */

/* The writable format that keeps most: ODS, XLSX, XLS, then CSV. */
static const owf_book_format *recovery_format(void)
{
    static const char *const prefer[] = { "ods", "xlsx", "xls", "csv", NULL };
    int i;
    for (i = 0; prefer[i]; i++) {
        const owf_book_format *f = owf_book_format_named(prefer[i]);
        if (f && f->export) return f;
    }
    return NULL;
}

static void recovery_save(void)
{
    const owf_book_format *f = recovery_format();
    owf_report *report;
    if (!core || !f || oc_core_editing(core) ||
        !ow_recovery_due(&recovery, oc_core_is_dirty(core), oc_core_changes(core)) || !ow_recovery_begin(&recovery))
        return;
    report = owf_report_new();
    if (owf_export_book_file(oc_core_book(core), recovery.name, f->name, report) == OWF_OK) {
        ow_recovery_done(&recovery, current_path[0] ? current_path : "Untitled", oc_core_changes(core));
        set_status("Autosave recovery copy updated.");
        draw_status();
    }
    owf_report_free(report);
}

/* A recovery copy an interrupted session left (not another running GFOpenCalc's): offered, one after another. */
static void recovery_offer(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenCalc Recovery",
        (UBYTE *)"GFOpenCalc found a recovery copy of a workbook from an interrupted session.\n\nRecover it?",
        (UBYTE *)"Recover|Discard" };
    char found[80];
    int asked;
    for (asked = 0; asked < 8 && ow_recovery_find(&recovery, found, sizeof found); asked++) {
        if (!EasyRequest(win, &es, NULL)) {
            if (!ow_recovery_discard(found)) return;    /* it can't go: not asked about again */
            continue;
        }
        open_book(found);
        if (strcmp(current_path, found)) return;     /* not opened (open_book said why): it stays for another try */
        current_path[0] = 0;                         /* it is not the file it came from: Save asks */
        current_format[0] = 0;
        oc_core_set_dirty(core);                     /* so that it can't be lost without asking */
        recovery_save();                             /* this run's own copy of it, so the interrupted one's can go */
        if (recovery.made) ow_recovery_discard(found);
        set_status("Recovered workbook. Save it to keep the recovered work.");
        return;
    }
}

/* ---- Commands ---- */

static void about(void)
{
    tell("GFOpenCalc",
         VERSION_TEXT "\n\n"
         "GFOpenCalc, the spreadsheet of the GFOffice Suite.\n\n"
         "GFOpenGadTools interface\n"
         "libowf workbooks and formula engine\n"
         "Stable multi-key row sorting\n"
         "Workbook and sheet-local named ranges\n"
         "Duplicate, reorder and hide sheets\n"
         "Editable XLSX/ODS data validation\n"
         "Layered conditional formatting\n"
         "Value, compound Standard and Top/Bottom AutoFilters\n"
         "Searchable PDF export and GFOpenPrint printing\n\n"
         "MIT licence, Copyright (c) 2026 AmiCradle Team.");
}

static void do_copy(int cut)
{
    size_t n = 0;
    char *text = cut ? oc_core_cut(core, &n) : oc_core_copy(core, &n);
    if (!text) { set_status("Nothing to copy, or too much at once."); return; }
    if (!clipboard_set_text(text, n)) set_status("clipboard.device could not take the cells.");
    else set_status(cut ? "Cut to the clipboard." : "Copied to the clipboard.");
    owf_free(text);
}

static void do_paste(void)
{
    size_t n = 0;
    char *text = clipboard_get_text(&n);
    int rc = oc_core_paste(core, text, n);
    if (rc == OWF_ERR_UNSUPPORTED && !text) set_status("The clipboard has no text.");
    else result(rc, "Pasted.");
    owf_free(text);
}

static void do_goto(void)
{
    char name[96] = "";
    if (text_requester("Go To", "Cell or name", "", name, sizeof name) && oc_core_goto(core, name) != OWF_OK)
        set_status("That is not a cell, range, or navigable name.");
}

/* "12.5" as hundredths (no floating point): 1250. -1 when it isn't a number. */
static long hundredths(const char *s)
{
    long whole = 0, frac = 0;
    int digits = 0, any = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') { whole = whole * 10 + (*s++ - '0'); any = 1; if (whole > 100000) return -1; }
    if (*s == '.' || *s == ',') {
        s++;
        while (*s >= '0' && *s <= '9') {
            if (digits < 2) { frac = frac * 10 + (*s - '0'); digits++; }
            s++;
            any = 1;
        }
    }
    while (*s == ' ') s++;
    if (*s || !any) return -1;
    while (digits < 2) { frac *= 10; digits++; }
    return whole * 100 + frac;
}

static void do_col_width(void)
{
    char v[24];
    unsigned row, col;
    long chars;
    oc_core_active(core, &row, &col);
    /* in characters, as Excel: its default 8.43 is 960 twips */
    snprintf(v, sizeof v, "%ld.%02ld", (long)oc_core_col_width(core, col) * 843L / 960L / 100L,
             (long)oc_core_col_width(core, col) * 843L / 960L % 100L);
    if (!text_requester("Column Width", "Characters", v, v, sizeof v)) return;
    if ((chars = hundredths(v)) < 0 || chars > 25500) { set_status("A width is 0 to 255 characters."); return; }
    result(oc_core_set_col_width(core, chars ? (int)(chars * 960L / 843L) : 1), "Column width changed.");
}

static void do_row_height(void)
{
    char v[24];
    unsigned row, col;
    long pts;
    oc_core_active(core, &row, &col);
    snprintf(v, sizeof v, "%ld.%02ld", (long)oc_core_row_height(core, row) / 20L, (long)oc_core_row_height(core, row) % 20L * 5L);
    if (!text_requester("Row Height", "Points", v, v, sizeof v)) return;
    if ((pts = hundredths(v)) < 0 || pts > 40900) { set_status("A height is 0 to 409 points."); return; }
    result(oc_core_set_row_height(core, pts ? (int)(pts * 20L / 100L) : 1), "Row height changed.");
}

static void do_rename_sheet(int sheet)
{
    char name[96], latin[96];
    char *utf8;
    owf_book *book = oc_core_book(core);
    if (sheet < 0 || sheet >= book->nsheets) return;
    to_latin1(book->sheets[sheet].name, strlen(book->sheets[sheet].name), latin, sizeof latin);
    if (!text_requester("Rename Sheet", "Name", latin, name, sizeof name)) return;
    if (!(utf8 = from_latin1(name))) { result(OWF_ERR_MEMORY, NULL); return; }
    if (oc_core_rename_sheet(core, sheet, utf8) == OWF_ERR_FORMAT)
        tell("Rename Sheet", "A sheet's name is 1 to 31 characters, without [ ] : * ? / \\,\nand no other sheet may have it.");
    else
        set_status("Sheet renamed.");
    owf_free(utf8);
}

static void do_delete_sheet(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Delete Sheet",
                             (UBYTE *)"Delete the sheet \"%s\" and everything on it?", (UBYTE *)"Delete|Cancel" };
    owf_book *book = oc_core_book(core);
    char latin[96];
    int s = oc_core_sheet(core);
    if (book->nsheets < 2) { set_status("A workbook keeps at least one sheet."); return; }
    to_latin1(book->sheets[s].name, strlen(book->sheets[s].name), latin, sizeof latin);
    if (EasyRequest(win, &es, NULL, (ULONG)latin)) result(oc_core_delete_sheet(core, s), "Sheet deleted.");
}

static void do_show_sheet(void)
{
    owf_book *book = oc_core_book(core);
    char entered[96];
    char *utf8;
    int i, found = -1;
    for (i = 0; i < book->nsheets; i++)
        if (book->sheets[i].hidden) { found = i; break; }
    if (found < 0) { set_status("There are no hidden sheets."); return; }
    if (!text_requester("Show Sheet", "Hidden sheet name", "", entered, sizeof entered)) return;
    if (!(utf8 = from_latin1(entered))) { result(OWF_ERR_MEMORY, NULL); return; }
    found = -1;
    for (i = 0; i < book->nsheets; i++)
        if (book->sheets[i].hidden && !strcasecmp(book->sheets[i].name, utf8)) { found = i; break; }
    if (found < 0)
        set_status("There is no hidden sheet by that name.");
    else {
        int rc = oc_core_set_sheet_hidden(core, found, 0);
        if (rc == OWF_OK) rc = oc_core_set_sheet(core, found);
        result(rc, "Sheet shown.");
    }
    owf_free(utf8);
}

static void do_duplicate_sheet(void)
{
    owf_book *book = oc_core_book(core);
    int sheet = oc_core_sheet(core), rc;
    char initial[96], entered[96], latin[72];
    char *utf8;
    to_latin1(book->sheets[sheet].name, strlen(book->sheets[sheet].name), latin, sizeof latin);
    snprintf(initial, sizeof initial, "%s Copy", latin);
    if (!text_requester("Duplicate Sheet", "New name", initial, entered, sizeof entered)) return;
    if (!(utf8 = from_latin1(entered))) { result(OWF_ERR_MEMORY, NULL); return; }
    rc = oc_core_duplicate_sheet(core, sheet, utf8);
    if (rc == OWF_ERR_FORMAT)
        set_status("Use a new legal sheet name (1-31 characters, without [ ] : * ? / \\).");
    else if (rc == OWF_ERR_UNSUPPORTED)
        set_status("A sheet with pictures or shapes cannot yet be duplicated; supported charts can.");
    else
        result(rc, "Sheet duplicated.");
    owf_free(utf8);
}

static void do_define_name(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Define Name",
                             (UBYTE *)"Where can formulas use this name?", (UBYTE *)"Workbook|Sheet|Cancel" };
    char entered[96];
    char *utf8;
    int pick, scope, rc;
    if (!text_requester("Define Name", "Name for selection", "", entered, sizeof entered)) return;
    if (!(utf8 = from_latin1(entered))) { result(OWF_ERR_MEMORY, NULL); return; }
    pick = EasyRequest(win, &es, NULL);
    if (!pick) { owf_free(utf8); return; }
    scope = pick == 1 ? -1 : oc_core_sheet(core);
    rc = oc_core_define_selection(core, utf8, scope);
    if (rc == OWF_ERR_FORMAT)
        set_status("A name starts with a letter, underscore or backslash, contains no spaces, and cannot be a cell address.");
    else if (rc == OWF_ERR_UNSUPPORTED)
        set_status("Select one rectangular range before defining its name.");
    else
        result(rc, scope < 0 ? "Workbook name defined." : "Sheet name defined.");
    owf_free(utf8);
}

static void do_delete_name(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Delete Name",
                             (UBYTE *)"This name exists in both scopes. Which one?", (UBYTE *)"Sheet|Workbook|Cancel" };
    char entered[96];
    char *utf8;
    const owf_defname *name;
    int i, local = 0, global = 0, scope, pick;
    if (!text_requester("Delete Name", "Name", "", entered, sizeof entered)) return;
    if (!(utf8 = from_latin1(entered))) { result(OWF_ERR_MEMORY, NULL); return; }
    for (i = 0; i < oc_core_name_count(core); i++) {
        name = oc_core_name_at(core, i);
        if (!name || strcasecmp(name->name, utf8)) continue;
        if (name->sheet == oc_core_sheet(core)) local = 1;
        if (name->sheet == -1) global = 1;
    }
    if (!local && !global) {
        set_status("There is no workbook or current-sheet name by that name.");
        owf_free(utf8);
        return;
    }
    if (local && global) {
        pick = EasyRequest(win, &es, NULL);
        if (!pick) { owf_free(utf8); return; }
        scope = pick == 1 ? oc_core_sheet(core) : -1;
    } else
        scope = local ? oc_core_sheet(core) : -1;
    result(oc_core_delete_name(core, utf8, scope), scope < 0 ? "Workbook name deleted." : "Sheet name deleted.");
    owf_free(utf8);
}

static void do_comment(void)
{
    unsigned row, col;
    const owf_comment *comment;
    char initial[512], entered[512];
    char *utf8;
    oc_core_active(core, &row, &col);
    comment = oc_core_comment(core, oc_core_sheet(core), row, col);
    to_latin1(comment && comment->text ? comment->text : "",
              comment && comment->text ? strlen(comment->text) : 0, initial, sizeof initial);
    if (!text_requester("Cell Comment", "Comment (empty removes)", initial, entered, sizeof entered)) return;
    if (!(utf8 = from_latin1(entered))) { result(OWF_ERR_MEMORY, NULL); return; }
    result(oc_core_set_comment(core, comment && comment->author ? comment->author : "GFOpenCalc", utf8),
           *utf8 ? "Comment saved." : "Comment removed.");
    owf_free(utf8);
}

static void do_find(int ask)
{
    char initial[256], entered[256];
    char *utf8;
    unsigned row, col;
    int rc;
    if (ask || !find_text[0]) {
        to_latin1(find_text, strlen(find_text), initial, sizeof initial);
        if (!text_requester("Find", "Text", initial, entered, sizeof entered)) return;
        if (!(utf8 = from_latin1(entered))) { result(OWF_ERR_MEMORY, NULL); return; }
        snprintf(find_text, sizeof find_text, "%s", utf8);
        owf_free(utf8);
    }
    if (!find_text[0]) return;
    rc = oc_core_find(core, find_text, 0, 0, 0);
    if (rc == OWF_OK) {
        oc_core_active(core, &row, &col);
        oc_view_show_cell(core, row, col);
        set_status("Found.");
    } else if (rc == OWF_ERR_UNSUPPORTED)
        set_status("Text not found.");
    else
        result(rc, NULL);
}

static void do_command(int id)
{
    switch (id) {
    case C_NEW: new_book(); break;
    case C_OPEN: do_open(); break;
    case C_SAVE: do_save(); break;
    case C_UNDO: if (oc_core_undo(core) == OWF_OK) set_status("Undo."); break;
    case C_REDO: if (oc_core_redo(core) == OWF_OK) set_status("Redo."); break;
    case C_CUT: do_copy(1); break;
    case C_COPY: do_copy(0); break;
    case C_PASTE: do_paste(); break;
    }
}

static void toolbar_event(ULONG class, struct Gadget *g)
{
    int i = ogt_toolbar_index(&tb, g->GadgetID);
    if (i < 0) return;
    if (class == IDCMP_GADGETDOWN) {
        tb.pressed = i;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 1, "window");
    } else {
        tb.pressed = -1;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 0, "window");
        if (!tb.tool[i].disabled) do_command(tb.tool[i].id);
    }
}

static void set_zoom(int z)
{
    unsigned row, col;
    oc_view_set_zoom(core, z);
    oc_core_active(core, &row, &col);
    oc_view_show_cell(core, row, col);
}

static int sort_keys_text(const char *text, oc_sort_key *keys, unsigned capacity, unsigned *count)
{
    const char *p = text;
    unsigned n = 0;
    while (1) {
        unsigned column = 0, i;
        const char *word;
        size_t length;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || n == capacity || !isalpha((unsigned char)*p)) return 0;
        while (isalpha((unsigned char)*p)) {
            unsigned letter = (unsigned)(toupper((unsigned char)*p) - 'A' + 1);
            if (letter < 1 || letter > 26 || column > (OC_MAX_COLS - letter) / 26) return 0;
            column = column * 26 + letter;
            p++;
        }
        if (!column || column > OC_MAX_COLS) return 0;
        keys[n].col = column - 1;
        keys[n].direction = OC_SORT_ASCENDING;
        while (*p == ' ' || *p == '\t') p++;
        if (*p && *p != ',') {
            word = p;
            while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
            length = (size_t)(p - word);
            if ((length == 3 && !strncasecmp(word, "asc", 3)) ||
                (length == 9 && !strncasecmp(word, "ascending", 9)))
                keys[n].direction = OC_SORT_ASCENDING;
            else if ((length == 4 && !strncasecmp(word, "desc", 4)) ||
                     (length == 10 && !strncasecmp(word, "descending", 10)))
                keys[n].direction = OC_SORT_DESCENDING;
            else
                return 0;
            while (*p == ' ' || *p == '\t') p++;
        }
        for (i = 0; i < n; ++i)
            if (keys[i].col == keys[n].col) return 0;
        n++;
        if (!*p) break;
        if (*p++ != ',') return 0;
    }
    *count = n;
    return 1;
}

static void do_sort(int direction)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Sort Rows",
                             (UBYTE *)"Is the first selected row a header?",
                             (UBYTE *)"Yes|No|Cancel" };
    oc_sort_key keys[8];
    char entered[256], initial[32], column[16], message[64];
    unsigned row, col;
    unsigned nkeys;
    int pick = EasyRequest(win, &es, NULL);
    int rc;
    (void)row;
    if (!pick) return;
    oc_core_active(core, &row, &col);
    oc_col_name(col, column);
    snprintf(initial, sizeof initial, "%s %s", column,
             direction == OC_SORT_ASCENDING ? "asc" : "desc");
    if (!text_requester("Sort Rows", "Keys: B asc, A desc (up to 8)", initial, entered, sizeof entered))
        return;
    if (!sort_keys_text(entered, keys, sizeof keys / sizeof keys[0], &nkeys)) {
        set_status("Use column letters and directions, for example B asc, A desc.");
        return;
    }
    rc = oc_core_sort_keys(core, keys, nkeys, pick == 1);
    if (rc == OWF_ERR_UNSUPPORTED)
        set_status("Select one unmerged range containing every unique sort key and at least two data rows.");
    else {
        snprintf(message, sizeof message, "Rows sorted by %lu key%s.",
                 (unsigned long)nkeys, nkeys == 1 ? "" : "s");
        result(rc, message);
    }
}

static void do_merge(int merge)
{
    int rc = oc_core_merge(core, merge, 0);
    if (rc == OC_MERGE_CONTENT) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"Merge Cells",
                                 (UBYTE *)"Only the upper-left value is kept. Continue?",
                                 (UBYTE *)"Merge|Cancel" };
        if (EasyRequest(win, &es, NULL) == 1) rc = oc_core_merge(core, 1, 1);
        else return;
    }
    if (rc == OWF_ERR_UNSUPPORTED)
        set_status(merge ? "Select one unmerged rectangular range." : "Select merged cells to unmerge.");
    else
        result(rc, merge ? "Cells merged." : "Cells unmerged.");
}

static void do_filter_active(void)
{
    const owf_sheet *sheet = &oc_core_book(core)->sheets[oc_core_sheet(core)];
    const owf_cell *cell;
    const char *display;
    unsigned row, col;
    char value[256];
    oc_core_active(core, &row, &col);
    if (!sheet->has_autofilter || row <= sheet->autofilter.first_row || row > sheet->autofilter.last_row ||
        col < sheet->autofilter.first_col || col > sheet->autofilter.last_col) {
        set_status("Select a data cell inside an AutoFilter range.");
        return;
    }
    cell = owf_sheet_find(sheet, row, col);
    if (cell && cell->type == OWF_VALUE_TEXT)
        display = cell->text ? cell->text : "";
    else {
        oc_core_cell_text(core, oc_core_sheet(core), row, col, value, sizeof value);
        display = value;
    }
    result(oc_core_filter_value(core, col, display), display[0] ? "Rows filtered by the active value."
                                                                : "Rows filtered to blanks.");
}

static const char *valid_kind_name(int type)
{
    static const char *const names[] = { "any", "whole", "decimal", "list", "date", "time", "length", "custom" };
    return type >= OWF_DV_ANY && type <= OWF_DV_CUSTOM ? names[type] : "list";
}

static void validity_initial(const oc_validation_spec *v, char *out, size_t size)
{
    static const char *const ops[] = { "..", " not ", "=", "<>", ">", "<", ">=", "<=" };
    const char *a = v->formula[0] ? v->formula[0] : "", *b = v->formula[1] ? v->formula[1] : "";
    if (v->type == OWF_DV_LIST && a[0] == '"') {
        size_t n = strlen(a);
        snprintf(out, size, "list: %.*s", (int)(n > 1 && a[n - 1] == '"' ? n - 2 : n - 1), a + 1);
    } else if (v->type == OWF_DV_LIST)
        snprintf(out, size, "list: =%s", a);
    else if (v->type == OWF_DV_CUSTOM)
        snprintf(out, size, "custom: =%s", a);
    else if (v->op == OWF_CF_BETWEEN)
        snprintf(out, size, "%s: %s..%s", valid_kind_name(v->type), a, b);
    else if (v->op == OWF_CF_NOT_BETWEEN)
        snprintf(out, size, "%s: not %s..%s", valid_kind_name(v->type), a, b);
    else
        snprintf(out, size, "%s: %s%s", valid_kind_name(v->type),
                 v->op >= OWF_CF_EQUAL && v->op <= OWF_CF_LESS_EQUAL ? ops[v->op] : "=", a);
}

static char *trim_text(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
    return s;
}

static int validity_parse(char *text, oc_validation_spec *v, char *one, size_t one_size,
                          char *two, size_t two_size)
{
    static const char *const names[] = { "whole", "decimal", "list", "date", "time", "length", "custom" };
    static const int types[] = { OWF_DV_WHOLE, OWF_DV_DECIMAL, OWF_DV_LIST, OWF_DV_DATE,
                                 OWF_DV_TIME, OWF_DV_TEXT_LENGTH, OWF_DV_CUSTOM };
    char *colon = strchr(text, ':'), *value, *dots;
    int i;
    if (!colon) return 0;
    *colon = 0;
    text = trim_text(text);
    value = trim_text(colon + 1);
    for (i = 0; i < 7 && strcasecmp(text, names[i]); i++) ;
    if (i == 7 || !*value) return 0;
    v->type = types[i];
    v->op = OWF_CF_EQUAL;
    if (v->type == OWF_DV_LIST) {
        if (*value == '=') snprintf(one, one_size, "%s", value + 1);
        else snprintf(one, one_size, "\"%s\"", value);
    } else {
        if (*value == '=') value++;
        dots = strstr(value, "..");
        if (dots) {
            *dots = 0;
            v->op = OWF_CF_BETWEEN;
            value = trim_text(value);
            if (!strncasecmp(value, "not ", 4)) { v->op = OWF_CF_NOT_BETWEEN; value = trim_text(value + 4); }
            snprintf(two, two_size, "%s", trim_text(dots + 2));
            if (!*value || !*two) return 0;
        } else if (!strncmp(value, "<=", 2)) { v->op = OWF_CF_LESS_EQUAL; value += 2; }
        else if (!strncmp(value, ">=", 2)) { v->op = OWF_CF_GREATER_EQUAL; value += 2; }
        else if (!strncmp(value, "<>", 2) || !strncmp(value, "!=", 2)) { v->op = OWF_CF_NOT_EQUAL; value += 2; }
        else if (*value == '<') { v->op = OWF_CF_LESS; value++; }
        else if (*value == '>') { v->op = OWF_CF_GREATER; value++; }
        value = trim_text(value);
        if (!*value) return 0;
        snprintf(one, one_size, "%s", value);
    }
    v->formula[0] = one;
    v->formula[1] = two[0] ? two : NULL;
    return 1;
}

static void do_validity(void)
{
    oc_validation_spec v;
    char initial[256] = "list: Yes, No", entered[256], one[256] = "", two[256] = "";
    char *utf8 = NULL, *utf8b = NULL;
    int had = oc_core_get_validation(core, &v), rc;
    if (had) validity_initial(&v, initial, sizeof initial);
    else {
        memset(&v, 0, sizeof v);
        v.allow_blank = v.show_error = 1;
        v.error_style = OWF_DV_STOP;
    }
    if (!text_requester("Validity", "list/whole/decimal/date/time/length/custom", initial, entered, sizeof entered)) return;
    if (!validity_parse(entered, &v, one, sizeof one, two, sizeof two)) {
        set_status("Use list: Yes, No; whole: 1..10; decimal: >=0; or custom: =A1>0.");
        return;
    }
    utf8 = from_latin1(v.formula[0]);
    utf8b = v.formula[1] ? from_latin1(v.formula[1]) : NULL;
    if (!utf8 || (v.formula[1] && !utf8b)) rc = OWF_ERR_MEMORY;
    else { v.formula[0] = utf8; v.formula[1] = utf8b; rc = oc_core_set_validation(core, &v); }
    owf_free(utf8); owf_free(utf8b);
    result(rc, had ? "Validity changed." : "Validity added.");
}

static unsigned long conditional_colour(const char *name)
{
    static const char *const names[] = { "red", "green", "yellow", "blue", "grey", "orange", "white" };
    static const unsigned long rgb[] = { 0xF6C8C8UL, 0xCDEBC5UL, 0xFFF2A8UL, 0xC9DDF2UL,
                                         0xE0E0E0UL, 0xF4C7A1UL, 0xFFFFFFUL };
    int i;
    for (i = 0; i < 7; ++i) if (!strcasecmp(name, names[i])) return OWF_SHADE(rgb[i]);
    return 0;
}

static int conditional_parse(char *text, oc_condfmt_spec *rule)
{
    char *p, *join = NULL, *value, *dots;
    memset(rule, 0, sizeof *rule);
    text = trim_text(text);
    if (!strncasecmp(text, "scale ", 6)) {
        char *words[3]; int n = 0;
        p = trim_text(text + 6);
        while (*p && n < 3) {
            words[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
            p = trim_text(p);
        }
        if (*p || n < 2) return 0;
        rule->type = OWF_CF_COLOUR_SCALE; rule->npoints = n;
        rule->points[0].kind = OWF_CFV_MIN;
        rule->points[n - 1].kind = OWF_CFV_MAX;
        if (n == 3) { rule->points[1].kind = OWF_CFV_PERCENTILE; rule->points[1].value = "50"; }
        for (n = 0; n < rule->npoints; ++n)
            if (!(rule->points[n].colour = conditional_colour(words[n]))) return 0;
        return 1;
    }
    for (p = text; *p; ++p) if (!strncasecmp(p, " if ", 4)) { join = p; break; }
    if (!join) return 0;
    *join = 0;
    if (!(rule->fill = conditional_colour(trim_text(text)))) return 0;
    value = trim_text(join + 4);
    if (!strncasecmp(value, "formula ", 8)) {
        value = trim_text(value + 8);
        if (*value == '=') value++;
        if (!*value) return 0;
        rule->type = OWF_CF_EXPRESSION; rule->formula[0] = value;
        return 1;
    }
    rule->type = OWF_CF_CELL_IS;
    dots = strstr(value, "..");
    if (dots) {
        *dots = 0; rule->op = OWF_CF_BETWEEN;
        value = trim_text(value);
        if (!strncasecmp(value, "not ", 4)) { rule->op = OWF_CF_NOT_BETWEEN; value = trim_text(value + 4); }
        rule->formula[1] = trim_text(dots + 2);
        if (!*value || !*rule->formula[1]) return 0;
    } else if (!strncmp(value, "<=", 2)) { rule->op = OWF_CF_LESS_EQUAL; value += 2; }
    else if (!strncmp(value, ">=", 2)) { rule->op = OWF_CF_GREATER_EQUAL; value += 2; }
    else if (!strncmp(value, "<>", 2) || !strncmp(value, "!=", 2)) { rule->op = OWF_CF_NOT_EQUAL; value += 2; }
    else if (*value == '<') { rule->op = OWF_CF_LESS; value++; }
    else if (*value == '>') { rule->op = OWF_CF_GREATER; value++; }
    else { rule->op = OWF_CF_EQUAL; if (*value == '=') value++; }
    value = trim_text(value);
    if (!*value) return 0;
    rule->formula[0] = value;
    return 1;
}

static void do_conditional_format(void)
{
    char entered[256], *utf8 = NULL, *utf8b = NULL;
    oc_condfmt_spec rule;
    int rc;
    if (!text_requester("Conditional Format", "red if >10; green if formula A1>0; scale red blue",
                        "red if > 10", entered, sizeof entered)) return;
    if (!conditional_parse(entered, &rule)) {
        set_status("Use red if > 10, green if formula A1>0, or scale red yellow green.");
        return;
    }
    if (rule.formula[0]) utf8 = from_latin1(rule.formula[0]);
    if (rule.formula[1]) utf8b = from_latin1(rule.formula[1]);
    if ((rule.formula[0] && !utf8) || (rule.formula[1] && !utf8b)) rc = OWF_ERR_MEMORY;
    else {
        if (utf8) rule.formula[0] = utf8;
        if (utf8b) rule.formula[1] = utf8b;
        rc = oc_core_add_condfmt(core, &rule);
    }
    owf_free(utf8); owf_free(utf8b);
    result(rc, "Conditional format added.");
}

/* "column Annual sales", "stacked bar Costs", "line Trend", "area",
 * "pie Share" or "scatter Measurements". The selected table's first row
 * names its series and its first column names its categories. */
static void do_insert_chart(void)
{
    char entered[192], *p, *word, *title;
    char *utf8 = NULL;
    oc_chart_spec spec;
    int rc;
    if (!text_requester("Insert Chart", "column/bar/line/area/pie/scatter [title]",
                        "column", entered, sizeof entered)) return;
    memset(&spec, 0, sizeof spec);
    spec.grouping = OWF_CHART_CLUSTERED;
    spec.first_row_labels = spec.first_col_categories = 1;
    spec.legend = spec.gridlines = 1;
    p = trim_text(entered);
    word = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
    p = trim_text(p);
    if (!strcasecmp(word, "stacked") || !strcasecmp(word, "percent")) {
        spec.grouping = !strcasecmp(word, "stacked") ? OWF_CHART_STACKED : OWF_CHART_PERCENT;
        word = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        p = trim_text(p);
    }
    if (!strcasecmp(word, "column")) spec.kind = OWF_CHART_COLUMN;
    else if (!strcasecmp(word, "bar")) spec.kind = OWF_CHART_BAR;
    else if (!strcasecmp(word, "line")) spec.kind = OWF_CHART_LINE;
    else if (!strcasecmp(word, "area")) spec.kind = OWF_CHART_AREA;
    else if (!strcasecmp(word, "pie")) spec.kind = OWF_CHART_PIE;
    else if (!strcasecmp(word, "scatter")) spec.kind = OWF_CHART_SCATTER;
    else {
        set_status("Use column, bar, line, area, pie or scatter; optionally start with stacked or percent.");
        return;
    }
    title = p;
    if (*title && !(utf8 = from_latin1(title))) { result(OWF_ERR_MEMORY, NULL); return; }
    spec.title = utf8;
    rc = oc_core_add_chart(core, &spec);
    if (rc == OWF_ERR_UNSUPPORTED)
        set_status("Select one table with headings and data; drawings with pictures or shapes are kept unchanged.");
    else
        result(rc, "Chart inserted; drag it or use Delete Selected Chart.");
    owf_free(utf8);
}

static void chart_initial(char *out, size_t size, const owf_chart *chart)
{
    static const char *const kinds[] = { "other", "column", "bar", "line", "area", "pie", "scatter" };
    static const char *const groups[] = { "clustered", "stacked", "percent" };
    char title[160], number[40];
    size_t n;
    unsigned labels = chart->nseries ? chart->series[0].labels : 0;
    int pos = chart->nseries ? chart->series[0].label_pos : OWF_LABEL_AUTO;
    to_latin1(chart->title ? chart->title : "", chart->title ? strlen(chart->title) : 0, title, sizeof title);
    n = (size_t)snprintf(out, size, "%s %s legend-%c %s ",
                         kinds[chart->kind >= OWF_CHART_COLUMN && chart->kind <= OWF_CHART_SCATTER ? chart->kind : 0],
                         groups[chart->grouping >= OWF_CHART_CLUSTERED && chart->grouping <= OWF_CHART_PERCENT ?
                                chart->grouping : OWF_CHART_CLUSTERED],
                         chart->legend == 'l' || chart->legend == 't' || chart->legend == 'b' ? chart->legend : 'r',
                         chart->gridlines ? "grid" : "nogrid");
    if (!chart->legend && n < size) n += (size_t)snprintf(out + n, size - n, "nolegend ");
    if (n < size) n += (size_t)snprintf(out + n, size - n, "nolabels ");
    if ((labels & OWF_LABEL_VALUE) && n < size) n += (size_t)snprintf(out + n, size - n, "values ");
    if ((labels & OWF_LABEL_PERCENT) && n < size) n += (size_t)snprintf(out + n, size - n, "percent-labels ");
    if ((labels & OWF_LABEL_CATEGORY) && n < size) n += (size_t)snprintf(out + n, size - n, "categories ");
    if ((labels & OWF_LABEL_SERIES) && n < size) n += (size_t)snprintf(out + n, size - n, "series ");
    if (labels && n < size) {
        static const char *const positions[] = { "auto", "centre", "inside-end", "inside-base", "outside", "above",
                                                 "below", "left", "right", "best" };
        n += (size_t)snprintf(out + n, size - n, "%s ", positions[pos >= OWF_LABEL_AUTO && pos <= OWF_LABEL_BEST_FIT ? pos : 0]);
    }
    if (chart->kind != OWF_CHART_PIE) {
        if (chart->has_min) { owf_number_text(chart->min, number); if (n < size) n += (size_t)snprintf(out + n, size - n, "min=%s ", number); }
        else if (n < size) n += (size_t)snprintf(out + n, size - n, "auto-min ");
        if (chart->has_max) { owf_number_text(chart->max, number); if (n < size) n += (size_t)snprintf(out + n, size - n, "max=%s ", number); }
        else if (n < size) n += (size_t)snprintf(out + n, size - n, "auto-max ");
    }
    if (chart->kind == OWF_CHART_SCATTER) {
        if (chart->has_xmin) { owf_number_text(chart->xmin, number); if (n < size) n += (size_t)snprintf(out + n, size - n, "xmin=%s ", number); }
        else if (n < size) n += (size_t)snprintf(out + n, size - n, "auto-xmin ");
        if (chart->has_xmax) { owf_number_text(chart->xmax, number); if (n < size) n += (size_t)snprintf(out + n, size - n, "xmax=%s ", number); }
        else if (n < size) n += (size_t)snprintf(out + n, size - n, "auto-xmax ");
        if (n < size) n += (size_t)snprintf(out + n, size - n, "%s ", chart->x_gridlines ? "xgrid" : "noxgrid");
    }
    if (n < size) snprintf(out + n, size - n, "title=%s", title);
}

static int chart_number(const char *text, double *value)
{
    char *end;
    *value = strtod(text, &end);
    return end != text && !*end && *value >= -1e200 && *value <= 1e200;
}

static int one_cell_ref(const char *text, unsigned *row, unsigned *col)
{
    owf_range range;
    if (!oc_parse_range(text, &range) || range.first_row != range.last_row ||
        range.first_col != range.last_col)
        return 0;
    *row = range.first_row;
    *col = range.first_col;
    return 1;
}

static void do_goal_seek(void)
{
    char result_ref[24], variable_ref[24], target_text[48];
    char solution_text[48], achieved_text[48], message[320];
    unsigned result_row, result_col, variable_row, variable_col;
    double target, solution, achieved;
    int iterations, rc, sheet = oc_core_sheet(core);

    rc = oc_edit_commit(core, OC_MOVE_NONE);
    if (rc == OC_INVALID) { validation_alert(); return; }
    if (rc == OWF_ERR_MEMORY) { result(rc, NULL); return; }
    oc_core_active(core, &result_row, &result_col);
    oc_cell_name(result_row, result_col, result_ref);
    if (!text_requester("Goal Seek", "Formula cell", result_ref, result_ref, sizeof result_ref)) return;
    if (!one_cell_ref(result_ref, &result_row, &result_col)) {
        set_status("Goal Seek needs one formula cell, such as C12.");
        return;
    }
    if (!text_requester("Goal Seek", "Target value", "0", target_text, sizeof target_text)) return;
    if (!chart_number(target_text, &target)) {
        set_status("The Goal Seek target must be a number.");
        return;
    }
    if (!text_requester("Goal Seek", "Variable cell", "", variable_ref, sizeof variable_ref)) return;
    if (!one_cell_ref(variable_ref, &variable_row, &variable_col)) {
        set_status("Goal Seek needs one variable cell, such as B12.");
        return;
    }
    rc = oc_core_goal_seek(core, sheet, result_row, result_col, target,
                           sheet, variable_row, variable_col, 0.0, 100,
                           &solution, &achieved, &iterations);
    if (rc == OWF_ERR_FORMAT) {
        tell("Goal Seek", "The formula cell must contain a numeric formula.\n\n"
                          "The variable cell must be empty or a number, not a formula.");
        return;
    }
    if (rc == OWF_ERR_UNSUPPORTED) {
        tell("Goal Seek", "No numeric solution was found in 100 iterations.\n\n"
                          "Check that the formula depends on the variable cell and can reach the target.");
        return;
    }
    if (rc != OWF_OK) { result(rc, NULL); return; }
    owf_number_text(solution, solution_text);
    owf_number_text(achieved, achieved_text);
    snprintf(message, sizeof message,
             "A solution was found in %d iteration%s.\n\n"
             "Variable %s = %s\nFormula %s = %s\n\nApply this value?",
             iterations, iterations == 1 ? "" : "s", variable_ref, solution_text,
             result_ref, achieved_text);
    if (ask_window("Goal Seek", message, "Apply|Cancel") == 1) {
        if (result(oc_core_set_input(core, sheet, variable_row, variable_col, solution_text),
                   "Goal Seek solution applied; Undo restores the old value.")) {
            oc_core_select_cell(core, variable_row, variable_col, 0, 0);
            oc_view_show_cell(core, variable_row, variable_col);
        }
    } else
        set_status("Goal Seek solution not applied.");
}

static void do_multiple_operations(void)
{
    oc_selection selection;
    char formula_ref[24] = "", input_ref[24] = "", column_input_ref[24] = "";
    unsigned formula_row, formula_col, input_row, input_col;
    unsigned column_input_row, column_input_col;
    int rc, sheet = oc_core_sheet(core);
    oc_core_selection(core, &selection);
    if (selection.nranges != 1) {
        set_status("Select one trial column or one row-and-column trial matrix first.");
        return;
    }
    if (selection.ranges[0].first_row == selection.ranges[0].last_row &&
        selection.ranges[0].first_col != selection.ranges[0].last_col) {
        set_status("A matrix needs a top trial row and a left trial column.");
        return;
    }
    if (!text_requester("Multiple Operations", "Formula cell", "", formula_ref, sizeof formula_ref)) return;
    if (!one_cell_ref(formula_ref, &formula_row, &formula_col)) {
        set_status("Multiple Operations needs one formula cell, such as D1.");
        return;
    }
    if (!text_requester("Multiple Operations",
                        selection.ranges[0].first_col == selection.ranges[0].last_col ?
                        "Input cell" : "Row input cell", "", input_ref, sizeof input_ref)) return;
    if (!one_cell_ref(input_ref, &input_row, &input_col)) {
        set_status("Multiple Operations needs one input cell, such as C1.");
        return;
    }
    if (selection.ranges[0].first_col == selection.ranges[0].last_col)
        rc = oc_core_multiple_operations(core, sheet, formula_row, formula_col,
                                         sheet, input_row, input_col);
    else {
        if (!text_requester("Multiple Operations", "Column input cell", "",
                            column_input_ref, sizeof column_input_ref)) return;
        if (!one_cell_ref(column_input_ref, &column_input_row, &column_input_col)) {
            set_status("Multiple Operations needs one column input cell, such as E1.");
            return;
        }
        rc = oc_core_multiple_operations_2d(core, sheet, formula_row, formula_col,
                                            sheet, input_row, input_col,
                                            sheet, column_input_row, column_input_col);
    }
    if (rc == OWF_ERR_FORMAT)
        set_status("Use distinct formula/input cells outside the selected trial table.");
    else if (rc == OWF_ERR_UNSUPPORTED)
        set_status("Every trial and result must be numeric; the output table may hold 262,144 cells.");
    else
        result(rc, selection.ranges[0].first_col == selection.ranges[0].last_col ?
               "Multiple Operations results filled the next column; Undo removes them." :
               "Multiple Operations filled the matrix interior; Undo restores it.");
}

static int column_word(const char *text, size_t length, unsigned *column)
{
    char cell[12];
    owf_range range;
    size_t i;
    if (!length || length + 2 >= sizeof cell)
        return 0;
    for (i = 0; i < length; ++i)
        if (!isalpha((unsigned char)text[i]))
            return 0;
        else
            cell[i] = (char)toupper((unsigned char)text[i]);
    cell[length] = '1';
    cell[length + 1] = 0;
    if (!oc_parse_range(cell, &range))
        return 0;
    *column = range.first_col;
    return 1;
}

static int column_list(const char *text, unsigned *columns, unsigned capacity, unsigned *count)
{
    const char *p = text;
    unsigned n = 0;
    while (*p) {
        const char *start;
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        start = p;
        while (isalpha((unsigned char)*p)) ++p;
        if (start == p || n == capacity || !column_word(start, (size_t)(p - start), &columns[n]))
            return 0;
        ++n;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p && *p != ',') return 0;
    }
    *count = n;
    return n != 0;
}

static int subtotal_function(const char *text)
{
    static const struct { const char *name; int function; } names[] = {
        { "average", OC_SUBTOTAL_AVERAGE }, { "count", OC_SUBTOTAL_COUNT },
        { "counta", OC_SUBTOTAL_COUNTA }, { "max", OC_SUBTOTAL_MAX },
        { "min", OC_SUBTOTAL_MIN }, { "product", OC_SUBTOTAL_PRODUCT },
        { "stdev", OC_SUBTOTAL_STDEV }, { "stdevp", OC_SUBTOTAL_STDEVP },
        { "sum", OC_SUBTOTAL_SUM }, { "var", OC_SUBTOTAL_VAR }, { "varp", OC_SUBTOTAL_VARP }
    };
    unsigned i;
    for (i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (!strcasecmp(text, names[i].name))
            return names[i].function;
    return 0;
}

static void do_subtotals(void)
{
    oc_selection selection;
    char group_text[12], values_text[256], function_text[24], name[8];
    unsigned group_col, columns[64], ncolumns = 0, col;
    int function, rc;
    oc_core_selection(core, &selection);
    if (selection.nranges != 1 || selection.ranges[0].first_row >= selection.ranges[0].last_row ||
        selection.ranges[0].first_col >= selection.ranges[0].last_col) {
        set_status("Select one table with a heading row and at least two columns.");
        return;
    }
    oc_col_name(selection.ranges[0].first_col, group_text);
    if (!text_requester("Subtotals", "Group column", group_text, group_text, sizeof group_text)) return;
    if (!column_word(group_text, strlen(group_text), &group_col) ||
        group_col < selection.ranges[0].first_col || group_col > selection.ranges[0].last_col) {
        set_status("The group column must be inside the selected table.");
        return;
    }
    values_text[0] = 0;
    for (col = selection.ranges[0].first_col; col <= selection.ranges[0].last_col; ++col)
        if (col != group_col) {
            size_t used = strlen(values_text), needed;
            oc_col_name(col, name);
            needed = strlen(name) + (used ? 1u : 0u);
            if (used + needed + 1 >= sizeof values_text)
                break;
            snprintf(values_text + used, sizeof values_text - used, "%s%s", used ? "," : "", name);
        }
    if (!text_requester("Subtotals", "Calculate columns", values_text, values_text, sizeof values_text)) return;
    if (!column_list(values_text, columns, 64, &ncolumns)) {
        set_status("List value columns as letters separated by commas, for example B,C.");
        return;
    }
    if (!text_requester("Subtotals", "sum/average/count/counta/max/min/...", "sum",
                        function_text, sizeof function_text)) return;
    function = subtotal_function(function_text);
    if (!function) {
        set_status("Use sum, average, count, counta, max, min, product, stdev, stdevp, var or varp.");
        return;
    }
    rc = oc_core_subtotals(core, group_col, columns, ncolumns, function, 1, 1);
    if (rc == OWF_ERR_FORMAT)
        set_status("Subtotals need heading, group and value columns inside the selected table.");
    else if (rc == OWF_ERR_UNSUPPORTED)
        set_status("Subtotals need one rectangular selection.");
    else
        result(rc, "Group subtotals and a grand total were inserted; Undo removes them.");
}

static void do_pivot_summary(void)
{
    oc_selection selection;
    char row_text[12], column_text[12], value_text[12], function_text[24];
    unsigned row_col, column_col = OC_PIVOT_NO_COLUMN, value_col, candidate;
    int function, rc;
    oc_core_selection(core, &selection);
    if (selection.nranges != 1 || selection.ranges[0].first_row >= selection.ranges[0].last_row ||
        selection.ranges[0].first_col >= selection.ranges[0].last_col) {
        set_status("Select one headed table with at least one data row and two columns.");
        return;
    }
    oc_col_name(selection.ranges[0].first_col, row_text);
    if (!text_requester("Pivot Summary", "Row field column", row_text,
                        row_text, sizeof row_text)) return;
    if (!column_word(row_text, strlen(row_text), &row_col) ||
        row_col < selection.ranges[0].first_col || row_col > selection.ranges[0].last_col) {
        set_status("The pivot row field must be a column inside the selected table.");
        return;
    }
    candidate = row_col == selection.ranges[0].first_col ? row_col + 1u : selection.ranges[0].first_col;
    if (selection.ranges[0].last_col - selection.ranges[0].first_col >= 2u)
        oc_col_name(candidate, column_text);
    else
        snprintf(column_text, sizeof column_text, "none");
    if (!text_requester("Pivot Summary", "Column field, or none", column_text,
                        column_text, sizeof column_text)) return;
    if (strcasecmp(column_text, "none")) {
        if (!column_word(column_text, strlen(column_text), &column_col) ||
            column_col < selection.ranges[0].first_col || column_col > selection.ranges[0].last_col ||
            column_col == row_col) {
            set_status("Use a different table column for the column field, or type none.");
            return;
        }
    }
    value_col = selection.ranges[0].last_col;
    if (value_col == row_col || value_col == column_col) {
        for (candidate = selection.ranges[0].first_col; candidate <= selection.ranges[0].last_col; ++candidate)
            if (candidate != row_col && candidate != column_col) {
                value_col = candidate;
                break;
            }
    }
    oc_col_name(value_col, value_text);
    if (!text_requester("Pivot Summary", "Value field column", value_text,
                        value_text, sizeof value_text)) return;
    if (!column_word(value_text, strlen(value_text), &value_col) ||
        value_col < selection.ranges[0].first_col || value_col > selection.ranges[0].last_col ||
        value_col == row_col || value_col == column_col) {
        set_status("The value field must be a different column inside the selected table.");
        return;
    }
    if (!text_requester("Pivot Summary", "sum/average/count/counta/max/min/...", "sum",
                        function_text, sizeof function_text)) return;
    function = subtotal_function(function_text);
    if (!function) {
        set_status("Use sum, average, count, counta, max, min, product, stdev, stdevp, var or varp.");
        return;
    }
    rc = oc_core_pivot_summary(core, row_col, column_col, value_col, function);
    if (rc == OWF_ERR_FORMAT)
        set_status("Pivot Summary needs distinct heading, row, column and value fields inside the selection.");
    else if (rc == OWF_ERR_UNSUPPORTED)
        set_status("That pivot would be too large, or the selection is not one rectangular table.");
    else
        result(rc, "Pivot summary created on a new sheet; Undo removes it.");
}

static void do_format_chart(void)
{
    owf_chart *charts = NULL;
    oc_chart_edit_spec edit;
    char initial[512], entered[512], *p, *token, *title = NULL, *utf8 = NULL;
    unsigned current_labels;
    int index = oc_core_selected_chart(core), n = 0, rc, bad = 0, current_pos, current_kind;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    current_labels = charts[index].nseries ? charts[index].series[0].labels : 0;
    current_pos = charts[index].nseries ? charts[index].series[0].label_pos : OWF_LABEL_AUTO;
    current_kind = charts[index].kind;
    chart_initial(initial, sizeof initial, &charts[index]);
    owf_charts_free(charts, n);
    if (!text_requester("Format Chart", "type/group legend grid min/max labels title=...", initial, entered, sizeof entered)) return;
    memset(&edit, 0, sizeof edit);
    edit.labels = current_labels;
    edit.label_pos = current_pos;
    p = trim_text(entered);
    while (*p && !bad) {
        char *end;
        token = p;
        if (!strncasecmp(token, "title=", 6)) { title = token + 6; edit.set |= OC_CHART_SET_TITLE; break; }
        end = p;
        while (*end && !isspace((unsigned char)*end)) end++;
        if (*end) *end++ = 0;
        p = trim_text(end);
        if (!strcasecmp(token, "column")) { edit.kind = OWF_CHART_COLUMN; edit.set |= OC_CHART_SET_KIND; }
        else if (!strcasecmp(token, "bar")) { edit.kind = OWF_CHART_BAR; edit.set |= OC_CHART_SET_KIND; }
        else if (!strcasecmp(token, "line")) { edit.kind = OWF_CHART_LINE; edit.set |= OC_CHART_SET_KIND; }
        else if (!strcasecmp(token, "area")) { edit.kind = OWF_CHART_AREA; edit.set |= OC_CHART_SET_KIND; }
        else if (!strcasecmp(token, "pie")) { edit.kind = OWF_CHART_PIE; edit.set |= OC_CHART_SET_KIND; }
        else if (!strcasecmp(token, "scatter")) { edit.kind = OWF_CHART_SCATTER; edit.set |= OC_CHART_SET_KIND; }
        else if (!strcasecmp(token, "clustered")) { edit.grouping = OWF_CHART_CLUSTERED; edit.set |= OC_CHART_SET_GROUPING; }
        else if (!strcasecmp(token, "stacked")) { edit.grouping = OWF_CHART_STACKED; edit.set |= OC_CHART_SET_GROUPING; }
        else if (!strcasecmp(token, "percent")) { edit.grouping = OWF_CHART_PERCENT; edit.set |= OC_CHART_SET_GROUPING; }
        else if (!strcasecmp(token, "legend-r") || !strcasecmp(token, "legend-right"))
            { edit.legend = 'r'; edit.set |= OC_CHART_SET_LEGEND; }
        else if (!strcasecmp(token, "legend-l") || !strcasecmp(token, "legend-left"))
            { edit.legend = 'l'; edit.set |= OC_CHART_SET_LEGEND; }
        else if (!strcasecmp(token, "legend-t") || !strcasecmp(token, "legend-top"))
            { edit.legend = 't'; edit.set |= OC_CHART_SET_LEGEND; }
        else if (!strcasecmp(token, "legend-b") || !strcasecmp(token, "legend-bottom"))
            { edit.legend = 'b'; edit.set |= OC_CHART_SET_LEGEND; }
        else if (!strcasecmp(token, "nolegend")) { edit.legend = 0; edit.set |= OC_CHART_SET_LEGEND; }
        else if (!strcasecmp(token, "grid")) { edit.gridlines = 1; edit.set |= OC_CHART_SET_GRIDLINES; }
        else if (!strcasecmp(token, "nogrid")) { edit.gridlines = 0; edit.set |= OC_CHART_SET_GRIDLINES; }
        else if (!strcasecmp(token, "xgrid")) { edit.x_gridlines = 1; edit.set |= OC_CHART_SET_X_GRIDLINES; }
        else if (!strcasecmp(token, "noxgrid")) { edit.x_gridlines = 0; edit.set |= OC_CHART_SET_X_GRIDLINES; }
        else if (!strncasecmp(token, "min=", 4)) {
            if (chart_number(token + 4, &edit.min)) { edit.has_min = 1; edit.set |= OC_CHART_SET_VALUE_AXIS; }
            else bad = 1;
        }
        else if (!strncasecmp(token, "max=", 4)) {
            if (chart_number(token + 4, &edit.max)) { edit.has_max = 1; edit.set |= OC_CHART_SET_VALUE_AXIS; }
            else bad = 1;
        }
        else if (!strcasecmp(token, "auto-min")) { edit.has_min = 0; edit.set |= OC_CHART_SET_VALUE_AXIS; }
        else if (!strcasecmp(token, "auto-max")) { edit.has_max = 0; edit.set |= OC_CHART_SET_VALUE_AXIS; }
        else if (!strncasecmp(token, "xmin=", 5)) {
            if (chart_number(token + 5, &edit.xmin)) { edit.has_xmin = 1; edit.set |= OC_CHART_SET_X_AXIS; }
            else bad = 1;
        }
        else if (!strncasecmp(token, "xmax=", 5)) {
            if (chart_number(token + 5, &edit.xmax)) { edit.has_xmax = 1; edit.set |= OC_CHART_SET_X_AXIS; }
            else bad = 1;
        }
        else if (!strcasecmp(token, "auto-xmin")) { edit.has_xmin = 0; edit.set |= OC_CHART_SET_X_AXIS; }
        else if (!strcasecmp(token, "auto-xmax")) { edit.has_xmax = 0; edit.set |= OC_CHART_SET_X_AXIS; }
        else if (!strcasecmp(token, "nolabels")) { edit.labels = 0; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "values")) { edit.labels |= OWF_LABEL_VALUE; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "percent-labels")) { edit.labels |= OWF_LABEL_PERCENT; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "categories")) { edit.labels |= OWF_LABEL_CATEGORY; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "series")) { edit.labels |= OWF_LABEL_SERIES; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "auto")) { edit.label_pos = OWF_LABEL_AUTO; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "centre")) { edit.label_pos = OWF_LABEL_CENTRE; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "inside-end")) { edit.label_pos = OWF_LABEL_INSIDE_END; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "inside-base")) { edit.label_pos = OWF_LABEL_INSIDE_BASE; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "outside")) { edit.label_pos = OWF_LABEL_OUTSIDE_END; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "above")) { edit.label_pos = OWF_LABEL_ABOVE; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "below")) { edit.label_pos = OWF_LABEL_BELOW; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "left")) { edit.label_pos = OWF_LABEL_LEFT; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "right")) { edit.label_pos = OWF_LABEL_RIGHT; edit.set |= OC_CHART_SET_LABELS; }
        else if (!strcasecmp(token, "best")) { edit.label_pos = OWF_LABEL_BEST_FIT; edit.set |= OC_CHART_SET_LABELS; }
        else bad = 1;
    }
    if (((edit.set & OC_CHART_SET_KIND) ? edit.kind : current_kind) != OWF_CHART_SCATTER)
        edit.set &= ~(OC_CHART_SET_X_AXIS | OC_CHART_SET_X_GRIDLINES);
    if (((edit.set & OC_CHART_SET_KIND) ? edit.kind : current_kind) == OWF_CHART_PIE)
        edit.set &= ~OC_CHART_SET_VALUE_AXIS;
    if (bad || !edit.set) {
        set_status("Use type/group, legend-*, grid, min=/max= (x equivalents), labels and title=...");
        return;
    }
    if ((edit.set & OC_CHART_SET_TITLE) && title && *title && !(utf8 = from_latin1(title))) {
        result(OWF_ERR_MEMORY, NULL);
        return;
    }
    edit.title = utf8;
    rc = oc_core_update_chart(core, index, &edit);
    owf_free(utf8);
    result(rc, "Chart formatted.");
}

static void do_chart_axis_titles(void)
{
    owf_chart *charts = NULL;
    oc_chart_edit_spec edit;
    char initial[384], entered[384], category[176], value[176], *separator, *category_text, *value_text;
    char *category_utf8 = NULL, *value_utf8 = NULL;
    int index = oc_core_selected_chart(core), n = 0, rc;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Axis Titles."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (charts[index].kind == OWF_CHART_PIE) {
        owf_charts_free(charts, n);
        set_status("Pie and doughnut charts do not have axes.");
        return;
    }
    to_latin1(charts[index].category_title ? charts[index].category_title : "",
              charts[index].category_title ? strlen(charts[index].category_title) : 0, category, sizeof category);
    to_latin1(charts[index].value_title ? charts[index].value_title : "",
              charts[index].value_title ? strlen(charts[index].value_title) : 0, value, sizeof value);
    owf_charts_free(charts, n);
    snprintf(initial, sizeof initial, "%s | %s", category, value);
    if (!text_requester("Chart Axis Titles", "Category/X title | Value/Y title", initial, entered, sizeof entered)) return;
    separator = strchr(entered, '|');
    if (!separator) {
        set_status("Separate the two titles with |; leave either side empty to clear it.");
        return;
    }
    *separator++ = 0;
    category_text = trim_text(entered);
    value_text = trim_text(separator);
    if (*category_text && !(category_utf8 = from_latin1(category_text))) {
        result(OWF_ERR_MEMORY, NULL);
        return;
    }
    if (*value_text && !(value_utf8 = from_latin1(value_text))) {
        owf_free(category_utf8);
        result(OWF_ERR_MEMORY, NULL);
        return;
    }
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_AXIS_TITLES;
    edit.category_title = category_utf8;
    edit.value_title = value_utf8;
    rc = oc_core_update_chart(core, index, &edit);
    owf_free(category_utf8);
    owf_free(value_utf8);
    result(rc, "Chart axis titles changed.");
}

static void do_chart_axis_formats(void)
{
    owf_chart *charts = NULL;
    oc_chart_edit_spec edit;
    char initial[384], entered[384], *separator, *value, *x;
    int index = oc_core_selected_chart(core), n = 0, rc, scatter;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Axis Number Formats."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (charts[index].kind == OWF_CHART_PIE) {
        owf_charts_free(charts, n);
        set_status("Pie and doughnut charts do not have value axes to format.");
        return;
    }
    scatter = charts[index].kind == OWF_CHART_SCATTER;
    snprintf(initial, sizeof initial, "%s | %s", charts[index].value_format ? charts[index].value_format : "",
             charts[index].x_format ? charts[index].x_format : "");
    owf_charts_free(charts, n);
    if (!text_requester("Chart Axis Number Formats", "Value/Y format | Scatter X format",
                        initial, entered, sizeof entered)) return;
    separator = strchr(entered, '|');
    if (!separator) {
        set_status("Separate the two formats with |; leave either side empty to inherit the data format.");
        return;
    }
    *separator++ = 0;
    value = trim_text(entered);
    x = trim_text(separator);
    if (!scatter && *x) {
        set_status("Only scatter charts have a numeric X-axis format; leave the right side empty.");
        return;
    }
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_AXIS_FORMATS;
    edit.value_format = value;
    edit.x_format = x;
    result(oc_core_update_chart(core, index, &edit), "Chart axis number formats changed.");
}

static int chart_series_colour(const char *text, unsigned long *colour)
{
    static const char *const names[] = { "red", "green", "blue", "orange", "yellow", "grey", "purple" };
    static const unsigned long rgb[] = { 0xC00000UL, 0x00A050UL, 0x4472C4UL, 0xED7D31UL,
                                         0xFFC000UL, 0xA5A5A5UL, 0x7030A0UL };
    char *end;
    unsigned long value;
    int i;
    if (!strcasecmp(text, "auto")) { *colour = 0; return 1; }
    for (i = 0; i < 7; i++)
        if (!strcasecmp(text, names[i])) { *colour = OWF_SHADE(rgb[i]); return 1; }
    if (*text != '#' || strlen(text) != 7)
        return 0;
    value = strtoul(text + 1, &end, 16);
    if (*end)
        return 0;
    *colour = OWF_SHADE(value);
    return 1;
}

static int chart_series_marker(const char *text, int *marker)
{
    static const char *const names[] = { "auto", "none", "circle", "square", "diamond", "triangle", "x", "star",
                                         "plus", "dash", "dot" };
    int i;
    for (i = OWF_MARKER_AUTO; i <= OWF_MARKER_DOT; i++)
        if (!strcasecmp(text, names[i])) { *marker = i; return 1; }
    return 0;
}

static void do_chart_series_style(void)
{
    static const char *const markers[] = { "auto", "none", "circle", "square", "diamond", "triangle", "x", "star",
                                           "plus", "dash", "dot" };
    owf_chart *charts = NULL;
    const owf_chart_series *series;
    oc_chart_edit_spec edit;
    char number[24] = "1", label[48], initial[160], entered[192], *p, *token, *end;
    long chosen, size;
    int index = oc_core_selected_chart(core), n = 0, rc, bad = 0;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Series Style."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (!charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("This chart has no series to format.");
        return;
    }
    snprintf(label, sizeof label, "Series number (1-%d)", charts[index].nseries);
    if (!text_requester("Chart Series", label, number, number, sizeof number)) {
        owf_charts_free(charts, n);
        return;
    }
    chosen = strtol(trim_text(number), &end, 10);
    if (*end || chosen < 1 || chosen > charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("Choose an existing series number.");
        return;
    }
    series = &charts[index].series[chosen - 1];
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_SERIES_STYLE;
    edit.series = (int)chosen - 1;
    edit.series_colour = series->colour;
    edit.series_line = !!series->line;
    edit.series_marker = series->marker;
    edit.series_marker_size = series->marker_size;
    if (OWF_SHADE_SET(series->colour))
        snprintf(initial, sizeof initial, "colour=#%06lx %s marker=%s size=%d", OWF_SHADE_RGB(series->colour),
                 series->line ? "line" : "noline", markers[series->marker >= OWF_MARKER_AUTO &&
                 series->marker <= OWF_MARKER_DOT ? series->marker : OWF_MARKER_AUTO], series->marker_size);
    else
        snprintf(initial, sizeof initial, "colour=auto %s marker=%s size=%d", series->line ? "line" : "noline",
                 markers[series->marker >= OWF_MARKER_AUTO && series->marker <= OWF_MARKER_DOT ?
                         series->marker : OWF_MARKER_AUTO], series->marker_size);
    owf_charts_free(charts, n);
    if (!text_requester("Chart Series Style", "colour=#RRGGBB; line/marker for line or scatter",
                        initial, entered, sizeof entered)) return;
    p = trim_text(entered);
    while (*p && !bad) {
        token = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        p = trim_text(p);
        if (!strncasecmp(token, "colour=", 7)) bad = !chart_series_colour(token + 7, &edit.series_colour);
        else if (!strcasecmp(token, "line")) edit.series_line = 1;
        else if (!strcasecmp(token, "noline")) edit.series_line = 0;
        else if (!strncasecmp(token, "marker=", 7)) bad = !chart_series_marker(token + 7, &edit.series_marker);
        else if (!strncasecmp(token, "size=", 5)) {
            size = strtol(token + 5, &end, 10);
            if (*end || size < 0 || size > 72) bad = 1;
            else edit.series_marker_size = (int)size;
        } else bad = 1;
    }
    if (bad) {
        set_status("Use colour=auto/#RRGGBB, line/noline, marker=auto/none/circle/... and size=0-72.");
        return;
    }
    result(oc_core_update_chart(core, index, &edit), "Chart series style changed.");
}

static void do_chart_point_colour(void)
{
    owf_chart *charts = NULL;
    const owf_chart_series *series;
    oc_chart_edit_spec edit;
    char entered[32] = "1", label[48], colour[32], *end;
    unsigned long chosen_colour;
    long chosen_series, chosen_point;
    int index = oc_core_selected_chart(core), n = 0, rc;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Data Point Colour."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (!charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("This chart has no series to format.");
        return;
    }
    snprintf(label, sizeof label, "Series number (1-%d)", charts[index].nseries);
    if (!text_requester("Chart Data Point", label, entered, entered, sizeof entered)) {
        owf_charts_free(charts, n);
        return;
    }
    chosen_series = strtol(trim_text(entered), &end, 10);
    if (*end || chosen_series < 1 || chosen_series > charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("Choose an existing series number.");
        return;
    }
    series = &charts[index].series[chosen_series - 1];
    if (!series->npoints) {
        owf_charts_free(charts, n);
        set_status("That series has no data points to format.");
        return;
    }
    strcpy(entered, "1");
    snprintf(label, sizeof label, "Point number (1-%d)", series->npoints);
    if (!text_requester("Chart Data Point", label, entered, entered, sizeof entered)) {
        owf_charts_free(charts, n);
        return;
    }
    chosen_point = strtol(trim_text(entered), &end, 10);
    if (*end || chosen_point < 1 || chosen_point > series->npoints) {
        owf_charts_free(charts, n);
        set_status("Choose an existing data point number.");
        return;
    }
    chosen_colour = series->points[chosen_point - 1].colour;
    if (OWF_SHADE_SET(chosen_colour))
        snprintf(colour, sizeof colour, "#%06lx", OWF_SHADE_RGB(chosen_colour));
    else
        strcpy(colour, "auto");
    owf_charts_free(charts, n);
    if (!text_requester("Chart Data Point Colour", "auto, #RRGGBB or a named colour",
                        colour, colour, sizeof colour)) return;
    if (!chart_series_colour(trim_text(colour), &chosen_colour)) {
        set_status("Use auto, #RRGGBB, red, green, blue, orange, yellow, grey or purple.");
        return;
    }
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_POINT_COLOUR;
    edit.series = (int)chosen_series - 1;
    edit.point = (int)chosen_point - 1;
    edit.point_colour = chosen_colour;
    result(oc_core_update_chart(core, index, &edit), "Chart data point colour changed.");
}

static void do_chart_series_labels(void)
{
    static const char *const positions[] = { "auto", "centre", "inside-end", "inside-base", "outside", "above",
                                             "below", "left", "right", "best" };
    owf_chart *charts = NULL;
    const owf_chart_series *series;
    oc_chart_edit_spec edit;
    char number[24] = "1", label[48], initial[384], entered[384], format[192];
    char *flags, *position, *fmt, *separator, *p, *token, *end, *utf8 = NULL;
    size_t at = 0;
    long chosen;
    int index = oc_core_selected_chart(core), n = 0, rc, i, bad = 0;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Series Data Labels."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (!charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("This chart has no series to format.");
        return;
    }
    snprintf(label, sizeof label, "Series number (1-%d)", charts[index].nseries);
    if (!text_requester("Chart Series Data Labels", label, number, number, sizeof number)) {
        owf_charts_free(charts, n);
        return;
    }
    chosen = strtol(trim_text(number), &end, 10);
    if (*end || chosen < 1 || chosen > charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("Choose an existing series number.");
        return;
    }
    series = &charts[index].series[chosen - 1];
    initial[0] = 0;
    if (!series->labels) at += (size_t)snprintf(initial + at, sizeof initial - at, "none ");
    if ((series->labels & OWF_LABEL_VALUE) && at < sizeof initial)
        at += (size_t)snprintf(initial + at, sizeof initial - at, "value ");
    if ((series->labels & OWF_LABEL_PERCENT) && at < sizeof initial)
        at += (size_t)snprintf(initial + at, sizeof initial - at, "percent ");
    if ((series->labels & OWF_LABEL_CATEGORY) && at < sizeof initial)
        at += (size_t)snprintf(initial + at, sizeof initial - at, "category ");
    if ((series->labels & OWF_LABEL_SERIES) && at < sizeof initial)
        at += (size_t)snprintf(initial + at, sizeof initial - at, "series ");
    to_latin1(series->label_format ? series->label_format : "",
              series->label_format ? strlen(series->label_format) : 0, format, sizeof format);
    if (at < sizeof initial)
        snprintf(initial + at, sizeof initial - at, "| %s | %s",
                 positions[series->label_pos >= OWF_LABEL_AUTO && series->label_pos <= OWF_LABEL_BEST_FIT ?
                           series->label_pos : OWF_LABEL_AUTO], *format ? format : "auto");
    owf_charts_free(charts, n);
    if (!text_requester("Chart Series Data Labels", "value/percent/category/series | position | format",
                        initial, entered, sizeof entered)) return;
    flags = entered;
    separator = strchr(flags, '|');
    if (!separator) bad = 1;
    else {
        *separator++ = 0;
        position = separator;
        separator = strchr(position, '|');
        if (!separator) bad = 1;
        else { *separator++ = 0; fmt = trim_text(separator); }
    }
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_SERIES_LABELS;
    edit.series = (int)chosen - 1;
    flags = trim_text(flags);
    p = flags;
    while (*p && !bad) {
        token = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        p = trim_text(p);
        if (!strcasecmp(token, "none")) edit.labels = 0;
        else if (!strcasecmp(token, "value")) edit.labels |= OWF_LABEL_VALUE;
        else if (!strcasecmp(token, "percent")) edit.labels |= OWF_LABEL_PERCENT;
        else if (!strcasecmp(token, "category")) edit.labels |= OWF_LABEL_CATEGORY;
        else if (!strcasecmp(token, "series")) edit.labels |= OWF_LABEL_SERIES;
        else bad = 1;
    }
    if (!*flags) bad = 1;
    if (!bad) {
        position = trim_text(position);
        for (i = OWF_LABEL_AUTO; i <= OWF_LABEL_BEST_FIT; i++)
            if (!strcasecmp(position, positions[i])) { edit.label_pos = i; break; }
        if (i > OWF_LABEL_BEST_FIT) bad = 1;
    }
    if (!bad && strcasecmp(fmt, "auto") && *fmt && !(utf8 = from_latin1(fmt))) {
        result(OWF_ERR_MEMORY, NULL);
        return;
    }
    if (bad) {
        set_status("Use label flags | auto/centre/inside-end/.../best | auto or a number format.");
        return;
    }
    edit.label_format = utf8;
    rc = oc_core_update_chart(core, index, &edit);
    owf_free(utf8);
    result(rc, "Chart series data labels changed.");
}

static void do_chart_plot_options(void)
{
    owf_chart *charts = NULL;
    oc_chart_edit_spec edit;
    char initial[128], entered[160], *p, *token, *end;
    long value;
    int index = oc_core_selected_chart(core), n = 0, rc, kind, bad = 0;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Plot Options."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    kind = charts[index].kind;
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_PLOT_OPTIONS;
    edit.gap = charts[index].gap;
    edit.first_angle = charts[index].first_angle;
    edit.hole = charts[index].hole;
    edit.vary = !!charts[index].vary;
    if (kind == OWF_CHART_COLUMN || kind == OWF_CHART_BAR)
        snprintf(initial, sizeof initial, "gap=%d", edit.gap);
    else if (kind == OWF_CHART_PIE)
        snprintf(initial, sizeof initial, "angle=%d hole=%d %s", edit.first_angle, edit.hole,
                 edit.vary ? "vary" : "same");
    else {
        owf_charts_free(charts, n);
        set_status("Plot options currently apply to column, bar, pie and doughnut charts.");
        return;
    }
    owf_charts_free(charts, n);
    if (!text_requester("Chart Plot Options",
                        kind == OWF_CHART_PIE ? "angle=0-359 hole=0-90 vary/same" : "gap=0-500 percent",
                        initial, entered, sizeof entered)) return;
    p = trim_text(entered);
    while (*p && !bad) {
        token = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        p = trim_text(p);
        if (!strncasecmp(token, "gap=", 4) && (kind == OWF_CHART_COLUMN || kind == OWF_CHART_BAR)) {
            value = strtol(token + 4, &end, 10);
            if (*end || value < 0 || value > 500) bad = 1;
            else edit.gap = (int)value;
        } else if (!strncasecmp(token, "angle=", 6) && kind == OWF_CHART_PIE) {
            value = strtol(token + 6, &end, 10);
            if (*end || value < 0 || value > 359) bad = 1;
            else edit.first_angle = (int)value;
        } else if (!strncasecmp(token, "hole=", 5) && kind == OWF_CHART_PIE) {
            value = strtol(token + 5, &end, 10);
            if (*end || value < 0 || value > 90) bad = 1;
            else edit.hole = (int)value;
        } else if (!strcasecmp(token, "vary") && kind == OWF_CHART_PIE) edit.vary = 1;
        else if (!strcasecmp(token, "same") && kind == OWF_CHART_PIE) edit.vary = 0;
        else bad = 1;
    }
    if (bad) {
        set_status(kind == OWF_CHART_PIE ? "Use angle=0-359 hole=0-90 and vary or same."
                                        : "Use gap=0-500.");
        return;
    }
    result(oc_core_update_chart(core, index, &edit), "Chart plot options changed.");
}

static void do_chart_trendline(void)
{
    static const char *const types[] = { "none", "linear", "exponential", "logarithmic", "polynomial", "power",
                                         "moving-average" };
    owf_chart *charts = NULL;
    const owf_chart_series *series;
    oc_chart_edit_spec edit;
    char number[24] = "1", label[48], initial[192], entered[224], colour[32], *p, *token, *end;
    long chosen, value;
    int index = oc_core_selected_chart(core), n = 0, rc, bad = 0, type;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Series Trendline."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL);
        else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (charts[index].kind == OWF_CHART_PIE || !charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("Trendlines need a non-pie chart with at least one series.");
        return;
    }
    snprintf(label, sizeof label, "Series number (1-%d)", charts[index].nseries);
    if (!text_requester("Chart Series Trendline", label, number, number, sizeof number)) {
        owf_charts_free(charts, n);
        return;
    }
    chosen = strtol(trim_text(number), &end, 10);
    if (*end || chosen < 1 || chosen > charts[index].nseries) {
        owf_charts_free(charts, n);
        set_status("Choose an existing series number.");
        return;
    }
    series = &charts[index].series[chosen - 1];
    type = series->trendline >= OWF_TREND_NONE && series->trendline <= OWF_TREND_MOVING_AVERAGE ?
           series->trendline : OWF_TREND_NONE;
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_TRENDLINE;
    edit.series = (int)chosen - 1;
    edit.trendline = type;
    edit.trend_order = series->trend_order >= 2 ? series->trend_order : 2;
    edit.trend_period = series->trend_period >= 2 ? series->trend_period : 2;
    edit.trend_display_equation = !!series->trend_display_equation;
    edit.trend_display_r2 = !!series->trend_display_r2;
    edit.trend_colour = series->trend_colour;
    if (edit.trend_colour) snprintf(colour, sizeof colour, "#%06lx", OWF_SHADE_RGB(edit.trend_colour));
    else strcpy(colour, "auto");
    snprintf(initial, sizeof initial, "%s order=%d period=%d colour=%s %s %s", types[type], edit.trend_order,
             edit.trend_period, colour,
             edit.trend_display_equation ? "equation" : "noequation", edit.trend_display_r2 ? "r2" : "nor2");
    owf_charts_free(charts, n);
    if (!text_requester("Chart Series Trendline", "none/linear/... order=2-6 period=2+ colour=...",
                        initial, entered, sizeof entered)) return;
    p = trim_text(entered);
    token = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
    for (type = OWF_TREND_NONE; type <= OWF_TREND_MOVING_AVERAGE; type++)
        if (!strcasecmp(token, types[type])) { edit.trendline = type; break; }
    if (type > OWF_TREND_MOVING_AVERAGE) bad = 1;
    p = trim_text(p);
    while (*p && !bad) {
        token = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        p = trim_text(p);
        if (!strncasecmp(token, "order=", 6)) {
            value = strtol(token + 6, &end, 10);
            if (*end || value < 2 || value > 6) bad = 1; else edit.trend_order = (int)value;
        } else if (!strncasecmp(token, "period=", 7)) {
            value = strtol(token + 7, &end, 10);
            if (*end || value < 2 || value > 4096) bad = 1; else edit.trend_period = (int)value;
        } else if (!strncasecmp(token, "colour=", 7))
            bad = !chart_series_colour(token + 7, &edit.trend_colour);
        else if (!strcasecmp(token, "equation")) edit.trend_display_equation = 1;
        else if (!strcasecmp(token, "noequation")) edit.trend_display_equation = 0;
        else if (!strcasecmp(token, "r2")) edit.trend_display_r2 = 1;
        else if (!strcasecmp(token, "nor2")) edit.trend_display_r2 = 0;
        else bad = 1;
    }
    if (bad) {
        set_status("Use a trend type, order=2-6, period=2+, colour=auto/#RRGGBB, equation/noequation and r2/nor2.");
        return;
    }
    result(oc_core_update_chart(core, index, &edit), "Chart series trendline changed.");
}

static void do_chart_error_bars(void)
{
    static const char *const types[] = { "none", "constant", "percentage", "standard-error", "variance",
                                         "standard-deviation", "error-margin", "cell-range" };
    owf_chart *charts = NULL;
    const owf_chart_series *series;
    oc_chart_edit_spec edit;
    char number[24] = "1", label[48], initial[512], entered[512], colour[32], *p, *token, *end;
    long chosen;
    double value;
    int index = oc_core_selected_chart(core), n = 0, rc, bad = 0, type;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Series Y Error Bars."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL); else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    if (charts[index].kind == OWF_CHART_PIE || !charts[index].nseries) {
        owf_charts_free(charts, n); set_status("Y error bars need a non-pie chart with at least one series."); return;
    }
    snprintf(label, sizeof label, "Series number (1-%d)", charts[index].nseries);
    if (!text_requester("Chart Series Y Error Bars", label, number, number, sizeof number)) {
        owf_charts_free(charts, n); return;
    }
    chosen = strtol(trim_text(number), &end, 10);
    if (*end || chosen < 1 || chosen > charts[index].nseries) {
        owf_charts_free(charts, n); set_status("Choose an existing series number."); return;
    }
    series = &charts[index].series[chosen - 1];
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_ERROR_BARS;
    edit.series = (int)chosen - 1;
    edit.error_kind = series->error_kind;
    edit.error_direction = series->error_direction;
    edit.error_cap = series->error_cap;
    edit.error_plus = series->error_plus;
    edit.error_minus = series->error_minus;
    edit.error_plus_range = series->error_plus_range;
    edit.error_minus_range = series->error_minus_range;
    edit.error_colour = series->error_colour;
    if (edit.error_colour) snprintf(colour, sizeof colour, "#%06lx", OWF_SHADE_RGB(edit.error_colour));
    else strcpy(colour, "auto");
    snprintf(initial, sizeof initial, "%s plus=%.15g minus=%.15g direction=%s %s colour=%s plus-range=%s minus-range=%s",
             types[edit.error_kind >= OWF_ERROR_NONE && edit.error_kind <= OWF_ERROR_CELL_RANGE ? edit.error_kind : 0],
             edit.error_plus, edit.error_minus,
             edit.error_direction == OWF_ERROR_PLUS ? "plus" : edit.error_direction == OWF_ERROR_MINUS ? "minus" : "both",
             edit.error_cap ? "cap" : "nocap", colour,
             edit.error_plus_range ? edit.error_plus_range : "-", edit.error_minus_range ? edit.error_minus_range : "-");
    owf_charts_free(charts, n);
    if (!text_requester("Chart Series Y Error Bars", "type plus= minus= direction= cap/nocap colour= ranges=",
                        initial, entered, sizeof entered)) return;
    p = trim_text(entered); token = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
    for (type = OWF_ERROR_NONE; type <= OWF_ERROR_CELL_RANGE; type++)
        if (!strcasecmp(token, types[type])) { edit.error_kind = type; break; }
    if (type > OWF_ERROR_CELL_RANGE) bad = 1;
    for (p = trim_text(p); *p && !bad; p = trim_text(p)) {
        token = p; while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        if (!strncasecmp(token, "plus=", 5) || !strncasecmp(token, "minus=", 6)) {
            int plus = tolower((unsigned char)token[0]) == 'p';
            value = strtod(token + (plus ? 5 : 6), &end);
            if (*end || !(value >= 0 && value <= 1e200)) bad = 1;
            else if (plus) edit.error_plus = value; else edit.error_minus = value;
        } else if (!strncasecmp(token, "direction=", 10)) {
            const char *d = token + 10;
            if (!strcasecmp(d, "both")) edit.error_direction = OWF_ERROR_BOTH;
            else if (!strcasecmp(d, "plus")) edit.error_direction = OWF_ERROR_PLUS;
            else if (!strcasecmp(d, "minus")) edit.error_direction = OWF_ERROR_MINUS; else bad = 1;
        } else if (!strcasecmp(token, "cap")) edit.error_cap = 1;
        else if (!strcasecmp(token, "nocap")) edit.error_cap = 0;
        else if (!strncasecmp(token, "colour=", 7)) bad = !chart_series_colour(token + 7, &edit.error_colour);
        else if (!strncasecmp(token, "plus-range=", 11)) edit.error_plus_range = token[11] == '-' && !token[12] ? NULL : token + 11;
        else if (!strncasecmp(token, "minus-range=", 12)) edit.error_minus_range = token[12] == '-' && !token[13] ? NULL : token + 12;
        else bad = 1;
    }
    if (bad || (edit.error_kind == OWF_ERROR_CELL_RANGE && (!edit.error_plus_range || !edit.error_minus_range))) {
        set_status("Use type, plus=/minus=, direction=both/plus/minus, cap/nocap, colour=auto/#RRGGBB and ranges.");
        return;
    }
    result(oc_core_update_chart(core, index, &edit), "Chart series Y error bars changed.");
}

static void do_chart_series_axis(void)
{
    owf_chart *charts = NULL;
    const owf_chart *chart;
    const owf_chart_series *series;
    oc_chart_edit_spec edit;
    char number[24] = "1", label[48], initial[384], entered[384], min[48], max[48];
    char *p, *token, *end, *split, *title;
    long chosen;
    double value;
    int index = oc_core_selected_chart(core), n = 0, rc, bad = 0, secondary;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Series Value Axis."); return; }
    rc = owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n);
    if (rc != OWF_OK || index >= n) {
        owf_charts_free(charts, n);
        if (rc != OWF_OK) result(rc, NULL); else set_status("The selected chart is no longer on this sheet.");
        return;
    }
    chart = &charts[index];
    if (chart->kind == OWF_CHART_PIE || chart->nseries < 2) {
        owf_charts_free(charts, n);
        set_status("A secondary value axis needs a non-pie chart with at least two series.");
        return;
    }
    snprintf(label, sizeof label, "Series number (1-%d)", chart->nseries);
    if (!text_requester("Chart Series Value Axis", label, number, number, sizeof number)) {
        owf_charts_free(charts, n); return;
    }
    chosen = strtol(trim_text(number), &end, 10);
    if (*end || chosen < 1 || chosen > chart->nseries) {
        owf_charts_free(charts, n); set_status("Choose an existing series number."); return;
    }
    series = &chart->series[chosen - 1];
    if (chart->has_secondary_min) owf_number_text(chart->secondary_min, min); else strcpy(min, "auto");
    if (chart->has_secondary_max) owf_number_text(chart->secondary_max, max); else strcpy(max, "auto");
    snprintf(initial, sizeof initial, "%s min=%s max=%s %s format=%s | %s",
             series->axis == OWF_AXIS_SECONDARY ? "secondary" : "primary", min, max,
             chart->secondary_gridlines ? "grid" : "nogrid",
             chart->secondary_format ? chart->secondary_format : "auto",
             chart->secondary_title ? chart->secondary_title : "");
    memset(&edit, 0, sizeof edit);
    edit.set = OC_CHART_SET_SERIES_AXIS;
    edit.series = (int)chosen - 1;
    edit.secondary_gridlines = chart->secondary_gridlines;
    edit.has_secondary_min = chart->has_secondary_min;
    edit.has_secondary_max = chart->has_secondary_max;
    edit.secondary_min = chart->secondary_min;
    edit.secondary_max = chart->secondary_max;
    owf_charts_free(charts, n);
    if (!text_requester("Chart Series Value Axis", "primary/secondary min=auto/# max=auto/# grid/nogrid format=... | title",
                        initial, entered, sizeof entered)) return;
    split = strchr(entered, '|');
    title = NULL;
    if (split) { *split++ = 0; title = trim_text(split); }
    p = trim_text(entered); token = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
    if (!strcasecmp(token, "primary")) secondary = 0;
    else if (!strcasecmp(token, "secondary")) secondary = 1;
    else secondary = bad = 1;
    edit.series_axis = secondary ? OWF_AXIS_SECONDARY : OWF_AXIS_PRIMARY;
    for (p = trim_text(p); *p && !bad; p = trim_text(p)) {
        token = p; while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = 0;
        if (!strncasecmp(token, "min=", 4)) {
            if (!strcasecmp(token + 4, "auto")) edit.has_secondary_min = 0;
            else if (chart_number(token + 4, &value)) { edit.secondary_min = value; edit.has_secondary_min = 1; }
            else bad = 1;
        } else if (!strncasecmp(token, "max=", 4)) {
            if (!strcasecmp(token + 4, "auto")) edit.has_secondary_max = 0;
            else if (chart_number(token + 4, &value)) { edit.secondary_max = value; edit.has_secondary_max = 1; }
            else bad = 1;
        } else if (!strcasecmp(token, "grid")) edit.secondary_gridlines = 1;
        else if (!strcasecmp(token, "nogrid")) edit.secondary_gridlines = 0;
        else if (!strncasecmp(token, "format=", 7))
            edit.secondary_format = !strcasecmp(token + 7, "auto") ? NULL : token + 7;
        else bad = 1;
    }
    if (edit.has_secondary_min && edit.has_secondary_max && !(edit.secondary_min < edit.secondary_max)) bad = 1;
    if (bad) {
        set_status("Use primary/secondary, min=/max=auto or a number, grid/nogrid, format=auto/text, then | title.");
        return;
    }
    if (secondary) {
        edit.set |= OC_CHART_SET_SECONDARY_AXIS;
        edit.secondary_title = title && *title ? title : NULL;
    }
    result(oc_core_update_chart(core, index, &edit), "Chart series value axis changed.");
}

static void do_chart_data(void)
{
    oc_chart_data_spec spec;
    owf_range range;
    char initial[128], entered[192], name[32], *p, *token;
    int index = oc_core_selected_chart(core), bad = 0, rc;
    if (index < 0) { set_status("Click a chart first, then choose Format > Chart Data."); return; }
    oc_core_name_box(core, name, sizeof name);
    snprintf(initial, sizeof initial, "%s columns headings categories", name);
    if (!text_requester("Chart Data", "range columns/rows headings categories", initial, entered, sizeof entered)) return;
    memset(&spec, 0, sizeof spec);
    spec.first_row_labels = spec.first_col_categories = 1;
    p = trim_text(entered);
    token = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
    if (!oc_parse_range(token, &range)) {
        set_status("Start with a cell range on this sheet, such as A1:D12.");
        return;
    }
    p = trim_text(p);
    while (*p && !bad) {
        char *end = p;
        token = p;
        while (*end && !isspace((unsigned char)*end)) end++;
        if (*end) *end++ = 0;
        p = trim_text(end);
        if (!strcasecmp(token, "columns")) spec.series_by_rows = 0;
        else if (!strcasecmp(token, "rows")) spec.series_by_rows = 1;
        else if (!strcasecmp(token, "headings")) spec.first_row_labels = 1;
        else if (!strcasecmp(token, "noheadings")) spec.first_row_labels = 0;
        else if (!strcasecmp(token, "categories")) spec.first_col_categories = 1;
        else if (!strcasecmp(token, "nocategories")) spec.first_col_categories = 0;
        else bad = 1;
    }
    if (bad) {
        set_status("After the range use columns/rows, headings/noheadings and categories/nocategories.");
        return;
    }
    rc = oc_core_set_chart_data(core, index, &range, &spec);
    if (rc == OWF_ERR_UNSUPPORTED)
        set_status("That range does not contain enough data for this chart type.");
    else
        result(rc, "Chart data changed.");
}

static void menu_action(ULONG id)
{
    static const unsigned long fills[] = { 0, 0xFFF2A8UL, 0xCDEBC5UL, 0xC9DDF2UL, 0xE0E0E0UL, 0xF6C8C8UL };
    static const unsigned long colours[] = { OWF_COLOUR_AUTO, 0xC00000UL, 0x1F4E9CUL, 0x2E7D32UL };
    if (id >= M_NUMFMT && id < M_NUMFMT + 13 && oc_number_formats[id - M_NUMFMT]) {
        result(oc_core_set_number_format(core, oc_number_formats[id - M_NUMFMT]), "Number format changed.");
        return;
    }
    if (id >= M_FILL_NONE && id <= M_FILL_RED) {
        unsigned long f = fills[id - M_FILL_NONE];
        result(oc_core_set_fill(core, f ? OWF_SHADE(f) : 0), "Fill changed.");
        return;
    }
    if (id >= M_COL_AUTO && id <= M_COL_GREEN) {
        result(oc_core_set_font_colour(core, colours[id - M_COL_AUTO]), "Text colour changed.");
        return;
    }
    switch (id) {
    case M_NEW: new_book(); break;
    case M_OPEN: do_open(); break;
    case M_SAVE: do_save(); break;
    case M_SAVE_AS: do_save_as(); break;
    case M_EXPORT_CSV: do_export_csv(); break;
    case M_EXPORT_PDF: do_export_pdf(); break;
    case M_PRINT: do_print(); break;
    case M_ABOUT: about(); break;
    case M_QUIT: if (confirm_discard_changes()) quit_now = 1; break;
    case M_UNDO: do_command(C_UNDO); break;
    case M_REDO: do_command(C_REDO); break;
    case M_CUT: do_copy(1); break;
    case M_COPY: do_copy(0); break;
    case M_PASTE: do_paste(); break;
    case M_FIND: do_find(1); break;
    case M_FIND_NEXT: do_find(0); break;
    case M_FILL_DOWN: result(oc_core_fill(core, OC_FILL_DOWN), "Filled down."); break;
    case M_FILL_RIGHT: result(oc_core_fill(core, OC_FILL_RIGHT), "Filled right."); break;
    case M_CLEAR: result(oc_core_clear(core, OC_CLEAR_CONTENTS), "Cleared."); break;
    case M_CLEAR_FORMATS: result(oc_core_clear(core, OC_CLEAR_FORMATS), "Formats cleared."); break;
    case M_CLEAR_ALL: result(oc_core_clear(core, OC_CLEAR_ALL), "Cleared."); break;
    case M_SELECT_ALL: oc_core_select_all(core); break;
    case M_SELECT_ROWS: oc_core_select_rows(core); break;
    case M_SELECT_COLS: oc_core_select_cols(core); break;
    case M_GOTO: do_goto(); break;
    case M_FREEZE: result(oc_core_freeze(core), NULL); break;
    case M_ZOOM_IN: set_zoom(oc_view_zoom(core) + 10); break;
    case M_ZOOM_OUT: set_zoom(oc_view_zoom(core) - 10); break;
    case M_ZOOM_100: set_zoom(100); break;
    case M_THEME_OPEN: apply_theme("Open"); break;
    case M_THEME_GRAPHITE: apply_theme("Graphite"); break;
    case M_THEME_EMBER: apply_theme("Ember"); break;
    case M_THEME_CLEAR: apply_theme("Clear"); break;
    case M_THEME_CLASSIC: apply_theme("Classic"); break;
    case M_INS_ROWS:
        if (oc_core_insert(core, 0) == OWF_ERR_FORMAT) set_status("Rows can't go in: cells would fall off the sheet.");
        else set_status("Rows inserted.");
        break;
    case M_INS_COLS:
        if (oc_core_insert(core, 1) == OWF_ERR_FORMAT) set_status("Columns can't go in: cells would fall off the sheet.");
        else set_status("Columns inserted.");
        break;
    case M_INS_SHEET: if (oc_core_add_sheet(core, NULL) < 0) result(OWF_ERR_MEMORY, NULL); else set_status("Sheet added."); break;
    case M_COMMENT: do_comment(); break;
    case M_CHART: do_insert_chart(); break;
    case M_CHART_FORMAT: do_format_chart(); break;
    case M_CHART_DATA: do_chart_data(); break;
    case M_CHART_AXIS_TITLES: do_chart_axis_titles(); break;
    case M_CHART_SERIES_STYLE: do_chart_series_style(); break;
    case M_CHART_POINT_COLOUR: do_chart_point_colour(); break;
    case M_CHART_AXIS_FORMATS: do_chart_axis_formats(); break;
    case M_CHART_SERIES_LABELS: do_chart_series_labels(); break;
    case M_CHART_PLOT_OPTIONS: do_chart_plot_options(); break;
    case M_CHART_TRENDLINE: do_chart_trendline(); break;
    case M_CHART_ERROR_BARS: do_chart_error_bars(); break;
    case M_CHART_SERIES_AXIS: do_chart_series_axis(); break;
    case M_CHART_DELETE:
        if (oc_core_selected_chart(core) < 0) set_status("Click a chart first, then choose Delete Selected Chart.");
        else result(oc_core_delete_chart(core, -1), "Chart deleted.");
        break;
    case M_DEL_ROWS: result(oc_core_delete(core, 0), "Rows deleted."); break;
    case M_DEL_COLS: result(oc_core_delete(core, 1), "Columns deleted."); break;
    case M_BOLD: result(oc_core_toggle_flags(core, OWF_BOLD), NULL); break;
    case M_ITALIC: result(oc_core_toggle_flags(core, OWF_ITALIC), NULL); break;
    case M_UNDERLINE: result(oc_core_toggle_flags(core, OWF_UNDERLINE), NULL); break;
    case M_AL_GENERAL: result(oc_core_set_halign(core, OWF_HALIGN_GENERAL), NULL); break;
    case M_AL_LEFT: result(oc_core_set_halign(core, OWF_HALIGN_LEFT), NULL); break;
    case M_AL_CENTRE: result(oc_core_set_halign(core, OWF_HALIGN_CENTRE), NULL); break;
    case M_AL_RIGHT: result(oc_core_set_halign(core, OWF_HALIGN_RIGHT), NULL); break;
    case M_BORDER_ALL: result(oc_core_set_borders(core, OC_BORDERS_ALL, OWF_LINE_THIN, 0), NULL); break;
    case M_BORDER_OUTLINE: result(oc_core_set_borders(core, OC_BORDERS_OUTLINE, OWF_LINE_THIN, 0), NULL); break;
    case M_BORDER_BOTTOM: result(oc_core_set_borders(core, OC_BORDERS_BOTTOM, OWF_LINE_THIN, 0), NULL); break;
    case M_BORDER_NONE: result(oc_core_set_borders(core, OC_BORDERS_NONE, OWF_LINE_NONE, 0), NULL); break;
    case M_COL_WIDTH: do_col_width(); break;
    case M_ROW_HEIGHT: do_row_height(); break;
    case M_HIDE_COLS: result(oc_core_hide(core, 1, 1), "Columns hidden."); break;
    case M_SHOW_COLS: result(oc_core_hide(core, 1, 0), "Columns shown."); break;
    case M_HIDE_ROWS: result(oc_core_hide(core, 0, 1), "Rows hidden."); break;
    case M_SHOW_ROWS: result(oc_core_hide(core, 0, 0), "Rows shown."); break;
    case M_SORT_ASC: do_sort(OC_SORT_ASCENDING); break;
    case M_SORT_DESC: do_sort(OC_SORT_DESCENDING); break;
    case M_AUTOFILTER:
        if (oc_core_has_autofilter(core))
            result(oc_core_set_autofilter(core, 0), "AutoFilter removed; every row is shown.");
        else {
            int rc = oc_core_set_autofilter(core, 1);
            if (rc == OWF_ERR_UNSUPPORTED) set_status("Select a table with a heading row and at least one data row.");
            else result(rc, "AutoFilter added. Click a heading's arrow to choose a value.");
        }
        break;
    case M_FILTER_ACTIVE: do_filter_active(); break;
    case M_GOAL_SEEK: do_goal_seek(); break;
    case M_MULTIPLE_OPS: do_multiple_operations(); break;
    case M_SUBTOTALS: do_subtotals(); break;
    case M_PIVOT_SUMMARY: do_pivot_summary(); break;
    case M_PIVOT_REFRESH:
        {
            int rc = oc_core_refresh_pivot(core);
            if (rc == OWF_ERR_UNSUPPORTED)
                set_status("The active sheet is not a refreshable GFOpenCalc Pivot Summary.");
            else if (rc == OWF_ERR_FORMAT)
                set_status("The Pivot Summary source or fields are no longer valid.");
            else
                result(rc, "Pivot summary refreshed from its source; Undo restores the previous summary.");
        }
        break;
    case M_VALIDITY: do_validity(); break;
    case M_VALIDITY_CLEAR: result(oc_core_set_validation(core, NULL), "Validity cleared from the selection."); break;
    case M_FILTER_CLEAR:
        if (!oc_core_has_autofilter(core)) set_status("This sheet has no AutoFilter.");
        else result(oc_core_filter_clear(core), "Every AutoFilter row is shown.");
        break;
    case M_MERGE: do_merge(1); break;
    case M_UNMERGE: do_merge(0); break;
    case M_CONDFMT: do_conditional_format(); break;
    case M_CONDFMT_CLEAR: result(oc_core_clear_condfmt(core), "Conditional formatting cleared from the selection."); break;
    case M_SHEET_RENAME: do_rename_sheet(oc_core_sheet(core)); break;
    case M_SHEET_DELETE: do_delete_sheet(); break;
    case M_SHEET_DUPLICATE: do_duplicate_sheet(); break;
    case M_SHEET_LEFT:
        if (!oc_core_sheet(core)) set_status("This sheet is already first.");
        else result(oc_core_move_sheet(core, oc_core_sheet(core), oc_core_sheet(core) - 1), "Sheet moved left.");
        break;
    case M_SHEET_RIGHT:
        if (oc_core_sheet(core) + 1 >= oc_core_book(core)->nsheets) set_status("This sheet is already last.");
        else result(oc_core_move_sheet(core, oc_core_sheet(core), oc_core_sheet(core) + 1), "Sheet moved right.");
        break;
    case M_SHEET_HIDE:
        if (oc_core_set_sheet_hidden(core, oc_core_sheet(core), 1) == OWF_ERR_UNSUPPORTED)
            set_status("A workbook keeps at least one visible sheet.");
        else set_status("Sheet hidden.");
        break;
    case M_SHEET_SHOW: do_show_sheet(); break;
    case M_NAME_DEFINE: do_define_name(); break;
    case M_NAME_DELETE: do_delete_name(); break;
    case M_SHEET_NEXT: oc_core_key(core, OC_KEY_PAGEDOWN, OC_MOD_CTRL); break;
    case M_SHEET_PREV: oc_core_key(core, OC_KEY_PAGEUP, OC_MOD_CTRL); break;
    }
}

/* ---- ARexx: the GFOPENCALC port ---- */

static const char *skip_space(const char *s)
{
    while (s && *s && isspace((unsigned char)*s)) ++s;
    return s ? s : "";
}

static int starts_word_ci(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; ++i) if (!s[i] || tolower((unsigned char)s[i]) != tolower((unsigned char)word[i])) return 0;
    if (s[n] && !isspace((unsigned char)s[n])) return 0;
    if (rest) *rest = skip_space(s + n);
    return 1;
}

/* A cell's reference and what follows it: "B12 rest". 1 when it is one. */
static int rexx_cell(const char *arg, unsigned *row, unsigned *col, const char **rest)
{
    char ref[24];
    size_t n = 0;
    owf_range r;
    while (arg[n] && !isspace((unsigned char)arg[n]) && n < sizeof ref - 1) { ref[n] = arg[n]; n++; }
    ref[n] = 0;
    if (!oc_parse_range(ref, &r)) return 0;
    *row = r.first_row;
    *col = r.first_col;
    if (rest) *rest = skip_space(arg + n);
    return 1;
}

/* For the lab: FONTCHECK [cell] (the active cell without one): the cell's font as the book names it and
 * its size, the Amiga family it is taken to, the face that draws it and which library opened it, the em asked
 * for, the size it was made at and its height, whether that size was designed or scaled, its capitals' height
 * and the rows its letters mark (ascenders to descenders), all in pixels at the view's zoom. */
static int font_check(const char *arg, char *out, size_t size)
{
    owf_book *book = oc_core_book(core);
    const owf_sheet *sh = &book->sheets[oc_core_sheet(core)];
    const owf_cell *c;
    const owf_cellstyle *st;
    cell_font_entry *e;
    const char *name;
    owf_font_kind kind;
    char ref[16];
    unsigned row, col;
    int pt10;
    if (!*arg) oc_core_active(core, &row, &col);
    else if (!rexx_cell(arg, &row, &col, NULL)) { snprintf(out, size, "Not a cell"); return 0; }
    c = owf_sheet_find(sh, row, col);
    st = &book->styles[c && c->style >= 0 && c->style < book->nstyles ? c->style : 0];
    cell_font_index(st->font.font, &name, &kind);
    e = cell_font(st->font.font, st->font.size);
    oc_cell_name(row, col, ref);
    pt10 = (st->font.size > 0 ? st->font.size : OC_DEFAULT_FONT_TWIPS) / 2;
    if (!e) { snprintf(out, size, "%s: no font", ref); return 0; }
    snprintf(out, size, "%s: %s %d.%d pt%s%s at %d%%: family %s, face %s by %s, em %d px, made at %d (%s, %d high), "
             "capitals %d px, letters rows %d to %d",
             ref, name ? name : "(the book's)", pt10 / 10, pt10 % 10, st->font.flags & OWF_BOLD ? " bold" : "",
             st->font.flags & OWF_ITALIC ? " italic" : "", oc_view_zoom(core), e->file, e->face[0] ? e->face : "(none)",
             e->by, e->pixels, e->ysize, e->font && (e->font->tf_Flags & FPF_DESIGNED) ? "designed" : "scaled",
             e->font ? e->font->tf_YSize : 0, e->cap, e->ink_top, e->ink_bottom);
    return 1;
}

/* For the lab: CHARTCHECK: the charts of the sheet showing, as read from its drawing: each one's kind, the
 * cell it starts in, its series and the first one's points, the cells its values come from, and its title;
 * which is selected. */
static int chart_check(char *out, size_t size)
{
    static const char *const kinds[] = { "other", "columns", "bars", "lines", "areas", "pie", "scatter" };
    static const char *const groupings[] = { "", " stacked", " 100%" };
    char buf[1024], ref[16];
    owf_chart *charts;
    int n, i;
    size_t k;
    if (owf_sheet_charts(oc_core_book(core), oc_core_sheet(core), &charts, &n) != OWF_OK) {
        snprintf(out, size, "Out of memory");
        return 0;
    }
    k = (size_t)snprintf(buf, sizeof buf, "%d chart%s", n, n == 1 ? "" : "s");
    for (i = 0; i < n && k < sizeof buf; i++) {
        const owf_chart *c = &charts[i];
        oc_cell_name(c->from_row, c->from_col, ref);
        k += (size_t)snprintf(buf + k, sizeof buf - k, "; %s%s at %s, %d series, %d points from %s, \"%s\"",
                              c->kind == OWF_CHART_PIE && c->hole ? "doughnut" :
                              kinds[c->kind >= 0 && c->kind <= OWF_CHART_SCATTER ? c->kind : 0],
                              c->grouping >= 0 && c->grouping <= OWF_CHART_PERCENT && c->kind >= OWF_CHART_COLUMN &&
                              c->kind <= OWF_CHART_AREA ? groupings[c->grouping] : "", ref, c->nseries,
                              c->nseries ? c->series[0].npoints : 0,
                              c->nseries && c->series[0].values ? c->series[0].values : "(the file)", c->title ? c->title : "");
        if (k < sizeof buf && c->value_format)
            k += (size_t)snprintf(buf + k, sizeof buf - k, ", axis \"%s\"", c->value_format);
        if (k < sizeof buf && i == oc_core_selected_chart(core))
            k += (size_t)snprintf(buf + k, sizeof buf - k, ", selected");
        if (k < sizeof buf && c->nseries && c->series[0].labels)
            k += (size_t)snprintf(buf + k, sizeof buf - k, ", labels%s%s%s%s",
                                  c->series[0].labels & OWF_LABEL_SERIES ? " series" : "",
                                  c->series[0].labels & OWF_LABEL_CATEGORY ? " category" : "",
                                  c->series[0].labels & OWF_LABEL_VALUE ? " value" : "",
                                  c->series[0].labels & OWF_LABEL_PERCENT ? " percent" : "");
    }
    owf_charts_free(charts, n);
    to_latin1(buf, strlen(buf), out, size);
    return 1;
}

/* For the lab: VALIDCHECK: the sheet's data validation rules: each one's kind, its first range (and how many
 * more), its alert when a typed value is refused, and for a list whether it has its arrow. */
static int valid_check(char *out, size_t size)
{
    static const char *const kinds[] = { "any", "whole", "decimal", "list", "date", "time", "text length", "formula" };
    static const char *const alerts[] = { "Stop", "Warning", "Information" };
    const owf_book *b = oc_core_book(core);
    const owf_sheet *sh = &b->sheets[oc_core_sheet(core)];
    char buf[1024], a[16], z[16];
    int i;
    size_t k = (size_t)snprintf(buf, sizeof buf, "%d rule%s", sh->nvalidations, sh->nvalidations == 1 ? "" : "s");
    for (i = 0; i < sh->nvalidations && k < sizeof buf; i++) {
        const owf_validation *v = &sh->validations[i];
        const owf_range *r = v->nranges ? &v->ranges[0] : NULL;
        if (r) {
            oc_cell_name(r->first_row, r->first_col, a);
            oc_cell_name(r->last_row, r->last_col, z);
        }
        k += (size_t)snprintf(buf + k, sizeof buf - k, "; %s %s%s%s%s, %s%s", kinds[v->type >= 0 && v->type <= 7 ? v->type : 0],
                              r ? a : "?", r ? ":" : "", r ? z : "", v->nranges > 1 ? " +more" : "",
                              v->show_error ? alerts[v->error_style >= 0 && v->error_style <= 2 ? v->error_style : 0] : "no alert",
                              v->type == OWF_DV_LIST ? (v->no_dropdown ? ", no arrow" : ", arrow") : "");
    }
    to_latin1(buf, strlen(buf), out, size);
    return 1;
}

/* As the GFOffice ARexx conventions (docs/gfoffice/Design-Suite-Shared.md 6.3, proposed): 0 done, 5 not
 * possible now, 10 an error or an unknown command. */
#define RX_OK 0
#define RX_NOW 5
#define RX_ERROR 10

static int rexx_do_command(const char *command, char **result_out)
{
    const char *arg = "";
    char result_text[1024];
    int ok = 1, rc_fail = RX_ERROR;
    unsigned row, col;
    result_text[0] = 0;
    command = skip_space(command);
    if (starts_word_ci(command, "VERSION", &arg)) snprintf(result_text, sizeof result_text, "%s", VERSION_TEXT);
    else if (starts_word_ci(command, "HELP", &arg))
        snprintf(result_text, sizeof result_text, "VERSION HELP ACTIVATE FAULT ISDIRTY GETFILE OPEN SAVE SAVEAS EXPORT GETCELL GETFORMULA "
                 "SETCELL GOTO SHEET SHEETS ADDSHEET RENAMESHEET RECALC COMMAND FONTCHECK CHARTCHECK CHARTSELECT CHARTMOVE "
                 "VALIDCHECK GETCOMMENT QUIT");
    else if (starts_word_ci(command, "FAULT", &arg)) {   /* a DOS error code's text, as every Open app's port */
        if (!*arg) { ok = 0; snprintf(result_text, sizeof result_text, "FAULT needs a code"); }
        else Fault(atol(arg), NULL, (STRPTR)result_text, sizeof result_text);
    } else if (starts_word_ci(command, "ACTIVATE", &arg)) {
        if (win) { WindowToFront(win); ActivateWindow(win); }
    } else if (starts_word_ci(command, "ISDIRTY", &arg))
        snprintf(result_text, sizeof result_text, "%d", oc_core_is_dirty(core) ? 1 : 0);
    else if (starts_word_ci(command, "GETFILE", &arg))
        snprintf(result_text, sizeof result_text, "%s", current_path);
    else if (starts_word_ci(command, "OPEN", &arg)) {
        if (!*arg) { ok = 0; snprintf(result_text, sizeof result_text, "Missing path"); }
        else if (oc_core_is_dirty(core)) { ok = 0; rc_fail = RX_NOW; snprintf(result_text, sizeof result_text, "Unsaved changes"); }
        else { open_book(arg); snprintf(result_text, sizeof result_text, "%s", status); }
    } else if (starts_word_ci(command, "SAVEAS", &arg)) {
        if (!*arg) { ok = 0; snprintf(result_text, sizeof result_text, "Missing path"); }
        else { ok = save_book_as(arg, NULL, 1); snprintf(result_text, sizeof result_text, "%s", status); }
    } else if (starts_word_ci(command, "SAVE", &arg)) {
        const owf_book_format *f = current_format[0] ? owf_book_format_named(current_format) : NULL;
        if (current_path[0] && f && f->export) { ok = save_book_as(current_path, NULL, 1); snprintf(result_text, sizeof result_text, "%s", status); }
        else { ok = 0; rc_fail = RX_NOW; snprintf(result_text, sizeof result_text, "Needs SAVEAS"); }
    } else if (starts_word_ci(command, "EXPORT", &arg)) {
        owf_report *rp = owf_report_new();
        ok = *arg && owf_export_book_file(oc_core_book(core), arg, NULL, rp) == OWF_OK;
        owf_report_free(rp);
        snprintf(result_text, sizeof result_text, "%s", ok ? "Exported" : "Export failed");
    } else if (starts_word_ci(command, "FONTCHECK", &arg)) {
        if (!font_check(arg, result_text, sizeof result_text)) ok = 0;
    } else if (starts_word_ci(command, "CHARTSELECT", &arg)) {   /* n (1 the first, 0 none); none given: which */
        if (!*arg) snprintf(result_text, sizeof result_text, "%d", oc_core_selected_chart(core) + 1);
        else {
            int k = atoi(arg);
            oc_core_select_chart(core, k - 1);
            ok = k == 0 || oc_core_selected_chart(core) == k - 1;
            snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : "No such chart");
        }
    } else if (starts_word_ci(command, "CHARTMOVE", &arg)) {     /* the selected chart: to a cell, or BY rows columns */
        int rc2;
        const char *by;
        if (starts_word_ci(arg, "BY", &by)) {
            int dr = 0, dc = 0;
            ok = sscanf(by, "%d %d", &dr, &dc) == 2;
            rc2 = ok ? oc_core_step_chart(core, dr, dc) : OWF_ERR_FORMAT;
        } else {
            ok = rexx_cell(arg, &row, &col, NULL);
            rc2 = ok ? oc_core_place_chart(core, row, col) : OWF_ERR_FORMAT;
        }
        ok = rc2 == OWF_OK;
        snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : rc2 == OWF_ERR_UNSUPPORTED ? "No chart selected" : "Failed");
    } else if (starts_word_ci(command, "CHARTCHECK", &arg)) {
        if (!chart_check(result_text, sizeof result_text)) ok = 0;
    } else if (starts_word_ci(command, "VALIDCHECK", &arg)) {
        valid_check(result_text, sizeof result_text);
    } else if (starts_word_ci(command, "GETCOMMENT", &arg)) {
        const owf_comment *cm;
        if (!rexx_cell(arg, &row, &col, NULL)) { ok = 0; snprintf(result_text, sizeof result_text, "Not a cell"); }
        else if ((cm = oc_core_comment(core, oc_core_sheet(core), row, col)) != NULL)
            to_latin1(cm->text, strlen(cm->text), result_text, sizeof result_text);
    } else if (starts_word_ci(command, "GETCELL", &arg)) {
        if (!rexx_cell(arg, &row, &col, NULL)) { ok = 0; snprintf(result_text, sizeof result_text, "Not a cell"); }
        else {
            char shown[512];
            oc_core_cell_text(core, oc_core_sheet(core), row, col, shown, sizeof shown);
            to_latin1(shown, strlen(shown), result_text, sizeof result_text);
        }
    } else if (starts_word_ci(command, "GETFORMULA", &arg)) {
        if (!rexx_cell(arg, &row, &col, NULL)) { ok = 0; snprintf(result_text, sizeof result_text, "Not a cell"); }
        else {
            char *in = oc_core_cell_input(core, oc_core_sheet(core), row, col);
            if (in) to_latin1(in, strlen(in), result_text, sizeof result_text);
            else ok = 0;
            owf_free(in);
        }
    } else if (starts_word_ci(command, "SETCELL", &arg)) {
        const char *value;
        char *utf8;
        if (!rexx_cell(arg, &row, &col, &value)) { ok = 0; snprintf(result_text, sizeof result_text, "Not a cell"); }
        else if (!(utf8 = from_latin1(value))) { ok = 0; snprintf(result_text, sizeof result_text, "Out of memory"); }
        else {
            int rc = oc_core_set_input(core, oc_core_sheet(core), row, col, utf8);
            owf_free(utf8);
            ok = rc == OWF_OK;
            snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : rc == OWF_ERR_FORMAT ? "Formula error" : "Failed");
        }
    } else if (starts_word_ci(command, "GOTO", &arg)) {
        ok = oc_core_goto(core, arg) == OWF_OK;
        snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : "Not a cell");
    } else if (starts_word_ci(command, "SHEET", &arg)) {
        owf_book *book = oc_core_book(core);
        int i, n = atoi(arg);
        if (!*arg) snprintf(result_text, sizeof result_text, "%d", oc_core_sheet(core) + 1);
        else {
            ok = 0;
            for (i = 0; i < book->nsheets; i++) {
                char latin[96];
                to_latin1(book->sheets[i].name, strlen(book->sheets[i].name), latin, sizeof latin);
                if (!strcasecmp(latin, arg) || n == i + 1) { ok = oc_core_set_sheet(core, i) == OWF_OK; break; }
            }
            snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : "No such sheet");
        }
    } else if (starts_word_ci(command, "SHEETS", &arg)) {
        snprintf(result_text, sizeof result_text, "%d", oc_core_book(core)->nsheets);
    } else if (starts_word_ci(command, "ADDSHEET", &arg)) {
        char *utf8 = *arg ? from_latin1(arg) : NULL;
        ok = oc_core_add_sheet(core, utf8) >= 0;
        owf_free(utf8);
        snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : "Failed");
    } else if (starts_word_ci(command, "RENAMESHEET", &arg)) {   /* the sheet showing, as Rename... does */
        char *utf8 = *arg ? from_latin1(arg) : NULL;
        int rc = utf8 ? oc_core_rename_sheet(core, oc_core_sheet(core), utf8) : OWF_ERR_FORMAT;
        owf_free(utf8);
        ok = rc == OWF_OK;
        snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : rc == OWF_ERR_FORMAT ? "Not a sheet name" : "Failed");
    } else if (starts_word_ci(command, "RECALC", &arg)) {
        ok = owf_calc_recalc_all(oc_core_calc(core)) == OWF_OK;
        snprintf(result_text, sizeof result_text, "%s", ok ? "OK" : "Out of memory");
    } else if (starts_word_ci(command, "COMMAND", &arg)) {
        static const struct { const char *name; ULONG id; } cmds[] = {
            { "UNDO", M_UNDO }, { "REDO", M_REDO }, { "COPY", M_COPY }, { "CUT", M_CUT }, { "PASTE", M_PASTE },
            { "FILLDOWN", M_FILL_DOWN }, { "FILLRIGHT", M_FILL_RIGHT }, { "CLEAR", M_CLEAR }, { "BOLD", M_BOLD },
            { "ITALIC", M_ITALIC }, { "UNDERLINE", M_UNDERLINE }, { "INSERTROWS", M_INS_ROWS },
            { "INSERTCOLUMNS", M_INS_COLS }, { "DELETEROWS", M_DEL_ROWS }, { "DELETECOLUMNS", M_DEL_COLS },
            { "FREEZE", M_FREEZE }, { "SELECTALL", M_SELECT_ALL }, { NULL, 0 } };
        int i;
        for (i = 0; cmds[i].name && strcasecmp(cmds[i].name, arg); i++) ;
        if (cmds[i].name) { menu_action(cmds[i].id); snprintf(result_text, sizeof result_text, "OK"); }
        else { ok = 0; snprintf(result_text, sizeof result_text, "Unknown COMMAND"); }
    } else if (starts_word_ci(command, "QUIT", &arg)) {
        if (oc_core_is_dirty(core) && strcasecmp(arg, "FORCE")) { ok = 0; rc_fail = RX_NOW; snprintf(result_text, sizeof result_text, "Unsaved changes"); }
        else { quit_now = 1; snprintf(result_text, sizeof result_text, "Quitting"); }
    } else { ok = 0; snprintf(result_text, sizeof result_text, "Unknown GFOpenCalc ARexx command"); }
    if (result_out) {
        *result_out = (char *)malloc(strlen(result_text) + 1);
        if (*result_out) strcpy(*result_out, result_text);
        else ok = 0;
    }
    refresh();
    return ok ? RX_OK : rc_fail;
}

static void rexx_messages(void)
{
    struct RexxMsg *rm;
    while (rexx_port && (rm = (struct RexxMsg *)GetMsg(rexx_port)) != NULL) {
        char *res = NULL;
        int rc = rexx_do_command((const char *)rm->rm_Args[0], &res);
        rm->rm_Result1 = rc;
        rm->rm_Result2 = 0;
        if (rc == RX_OK && (rm->rm_Action & RXFF_RESULT) && RexxSysBase && res)
            rm->rm_Result2 = (LONG)CreateArgstring((STRPTR)res, (LONG)strlen(res));
        free(res);
        ReplyMsg((struct Message *)rm);
    }
}

static void rexx_open(void)
{
    struct MsgPort *p;
    int n;
    if (!RexxSysBase) return;
    if (!(p = CreateMsgPort())) return;
    p->mp_Node.ln_Pri = 0;
    Forbid();
    for (n = 1; n < 10; n++) {                       /* GFOPENCALC, then GFOPENCALC.2 for a second copy */
        if (n == 1) snprintf(rexx_name, sizeof rexx_name, "%s", REXX_PORT_NAME);
        else snprintf(rexx_name, sizeof rexx_name, "%s.%d", REXX_PORT_NAME, n);
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

/* ---- Keys and the pointer ---- */

static void bar_to_core(void);

/* Keys reach the window: the bar's gadget isn't being typed in any more; what it holds goes to the cell. */
static void bar_done(void)
{
    if (!bar_active) return;
    bar_to_core();
    bar_active = 0;
}

static unsigned mods_of(UWORD qual)
{
    return ((qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? OC_MOD_SHIFT : 0) |
           ((qual & IEQUALIFIER_CONTROL) ? OC_MOD_CTRL : 0) |
           ((qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? OC_MOD_ALT : 0);
}

/* The keys that aren't characters: arrows, F2, Del, Help, and a PC keyboard's Home, End, PageUp, PageDown
 * (OS 3.2's raw codes). Alt+Up/Down are PageUp/PageDown and Alt+Left/Right Home and End, for keyboards
 * without them; Ctrl+Alt+Up/Down go to the first and last cells. */
static void raw_key(UWORD code, UWORD qual)
{
    unsigned m = mods_of(qual);
    int key = 0, rc;
    bar_done();
    switch (code) {
    case 0x4c: key = OC_KEY_UP; break;
    case 0x4d: key = OC_KEY_DOWN; break;
    case 0x4e: key = OC_KEY_RIGHT; break;
    case 0x4f: key = OC_KEY_LEFT; break;
    case 0x51: key = OC_KEY_F2; break;
    case 0x46: key = OC_KEY_DELETE; break;
    case 0x70: key = OC_KEY_HOME; break;
    case 0x71: key = OC_KEY_END; break;
    case 0x48: key = OC_KEY_PAGEUP; break;
    case 0x49: key = OC_KEY_PAGEDOWN; break;
    case WHEEL_UP: case WHEEL_DOWN: {
        unsigned t, l;
        oc_view_scroll_pos(core, &t, &l);
        oc_view_scroll_to(core, code == WHEEL_UP ? (t > 3 ? t - 3 : 0) : t + 3, l);
        refresh();
        return;
    }
    default: return;
    }
    if (oc_core_selected_chart(core) >= 0 && !oc_core_editing(core)) {
        if (key == OC_KEY_UP || key == OC_KEY_DOWN || key == OC_KEY_LEFT || key == OC_KEY_RIGHT) {
            /* the selected chart: a row or column that way, a pixel with Shift */
            int dr = key == OC_KEY_UP ? -1 : key == OC_KEY_DOWN ? 1 : 0, dc = key == OC_KEY_LEFT ? -1 : key == OC_KEY_RIGHT ? 1 : 0;
            result((m & OC_MOD_SHIFT) ? oc_core_move_chart(core, dc, dr) : oc_core_step_chart(core, dr, dc), "Chart moved.");
            refresh();
            return;
        }
        oc_core_select_chart(core, -1);              /* another key: the cells again */
        set_status("");
    }
    if ((m & OC_MOD_ALT) && !(m & OC_MOD_CTRL) && key == OC_KEY_DOWN && !oc_core_editing(core)) {
        int i;                                       /* Alt+Down: the active cell's list, when it has one */
        for (i = 0; i < dl.n; i++)
            if (dl.items[i].kind == OC_DRAW_DROPDOWN) {
                pick_from_list(grid_box.x + dl.items[i].x, grid_box.y + dl.items[i].y, dl.items[i].w, dl.items[i].h);
                return;
            }
    }
    if ((m & OC_MOD_ALT) && !oc_core_editing(core) && (key == OC_KEY_UP || key == OC_KEY_DOWN || key == OC_KEY_LEFT || key == OC_KEY_RIGHT)) {
        if (m & OC_MOD_CTRL) key = key == OC_KEY_UP || key == OC_KEY_LEFT ? OC_KEY_HOME : OC_KEY_END;
        else {
            key = key == OC_KEY_UP ? OC_KEY_PAGEUP : key == OC_KEY_DOWN ? OC_KEY_PAGEDOWN : key == OC_KEY_LEFT ? OC_KEY_HOME : OC_KEY_END;
            m &= ~(unsigned)OC_MOD_ALT;
        }
        if (key == OC_KEY_HOME || key == OC_KEY_END) m = (m & OC_MOD_SHIFT) | ((qual & IEQUALIFIER_CONTROL) ? OC_MOD_CTRL : 0);
    }
    rc = oc_core_key(core, key, m);
    if (rc != OWF_ERR_UNSUPPORTED) result(rc, NULL);
    refresh();
}

/* A character from the keymap (Latin-1) as UTF-8. */
static int vanilla_utf8(UWORD code, char out[3])
{
    unsigned c = code & 255;
    if (c < 0x80) { out[0] = (char)c; out[1] = 0; return 1; }
    out[0] = (char)(0xc0 | (c >> 6));
    out[1] = (char)(0x80 | (c & 0x3f));
    out[2] = 0;
    return 2;
}

static void vanilla_key(UWORD code, UWORD qual)
{
    unsigned m = mods_of(qual);
    int rc = OWF_ERR_UNSUPPORTED;
    if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) return;   /* menu shortcuts */
    bar_done();
    if (oc_core_selected_chart(core) >= 0) {        /* a chart selected: Esc leaves it, a key goes to the cells */
        oc_core_select_chart(core, -1);
        set_status("");
        if (code == 27) {
            refresh();
            return;
        }
    }
    switch (code) {
    case 8: rc = oc_core_key(core, OC_KEY_BACKSPACE, m); break;
    case 9: rc = oc_core_key(core, OC_KEY_TAB, m); break;
    case 13: rc = oc_core_key(core, OC_KEY_RETURN, m); break;
    case 27: rc = oc_core_key(core, OC_KEY_ESCAPE, m); break;
    case 127: rc = oc_core_key(core, OC_KEY_DELETE, m); break;
    case 1: if (m & OC_MOD_CTRL) { oc_core_select_all(core); rc = OWF_OK; } break;           /* Ctrl+A */
    case 4: if (m & OC_MOD_CTRL) rc = oc_core_fill(core, OC_FILL_DOWN); break;               /* Ctrl+D */
    case 18: if (m & OC_MOD_CTRL) rc = oc_core_fill(core, OC_FILL_RIGHT); break;             /* Ctrl+R */
    default:
        if (code == 32 && !oc_core_editing(core) && (m & (OC_MOD_CTRL | OC_MOD_SHIFT))) {
            if (m & OC_MOD_CTRL) oc_core_select_cols(core);                                 /* Ctrl+Space: columns */
            else oc_core_select_rows(core);                                                  /* Shift+Space: rows */
            rc = OWF_OK;
        } else if (code >= 32 && code < 256 && code != 127 && !(m & OC_MOD_CTRL)) {
            char u[3];
            int n = vanilla_utf8(code, u);
            rc = oc_edit_type(core, u, (size_t)n);
        }
        break;
    }
    if (rc != OWF_ERR_UNSUPPORTED) result(rc, NULL);
    refresh();
}

/* The formula bar's text into the cell being edited. */
static void bar_to_core(void)
{
    STRPTR v = NULL;
    char *utf8;
    if (!g_input) return;
    GT_GetGadgetAttrs(g_input, win, NULL, GTST_String, (ULONG)&v, TAG_DONE);
    if (!(utf8 = from_latin1(v ? (const char *)v : ""))) return;
    oc_edit_set(core, utf8, strlen(utf8));
    owf_free(utf8);
}

/* Choices in a box under a button (bx, by, bw, bh: in the window). Escape
 * or a click elsewhere returns -1. Up to 12 show at once, the rest by the
 * arrows and the wheel. */
#define LIST_SHOWN 12
static int choice_popup(int bx, int by, int bw, int bh, char **items, int n, const char *now)
{
    char latin[256];
    int i, w = 0, ih = fh + 4, shown, top = 0, at = 0, done = 0, chosen = -1, lw, lh, lx, ly;
    struct RastPort trp;
    struct Window *pw;
    if (!items || n <= 0) return -1;
    InitRastPort(&trp);
    SetFont(&trp, font);
    for (i = 0; i < n; i++) {
        size_t k = to_latin1(items[i], strlen(items[i]), latin, sizeof latin);
        int tw = TextLength(&trp, (STRPTR)latin, (ULONG)k);
        if (tw > w) w = tw;
    }
    for (i = 0; now && i < n; i++)
        if (!strcmp(items[i], now)) { at = i; break; }
    shown = n < LIST_SHOWN ? n : LIST_SHOWN;
    lw = w + 16 < 120 ? 120 : w + 16;
    lh = shown * ih + 2;
    lx = win->LeftEdge + bx + bw - lw;
    ly = win->TopEdge + by + bh;
    if (lx < 0) lx = 0;
    if (lx + lw > scr->Width) lx = scr->Width - lw;
    if (ly + lh > scr->Height) ly = win->TopEdge + by - lh;     /* no room below: above the cell */
    if (at >= shown) top = at - shown + 1;
    pw = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, lx, WA_Top, ly, WA_Width, lw, WA_Height, lh,
                        WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE,
                        WA_SimpleRefresh, TRUE,
                        WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_VANILLAKEY | IDCMP_RAWKEY |
                                  IDCMP_INACTIVEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_EXTENDEDMOUSE,
                        TAG_DONE);
    if (!pw) {
        return -1;
    }
    while (!done) {
        struct RastPort *rp = pw->RPort;
        struct IntuiMessage *m;
        SetFont(rp, font);
        ogt_fill(&ctx, rp, "window", 0, 0, lw, lh);
        ogt_box(rp, rgb_pen(0x808080UL), 0, 0, lw, 1);
        ogt_box(rp, rgb_pen(0x808080UL), 0, lh - 1, lw, 1);
        ogt_box(rp, rgb_pen(0x808080UL), 0, 0, 1, lh);
        ogt_box(rp, rgb_pen(0x808080UL), lw - 1, 0, 1, lh);
        for (i = 0; i < shown && top + i < n; i++) {
            int y = 1 + i * ih, sel = top + i == at;
            to_latin1(items[top + i], strlen(items[top + i]), latin, sizeof latin);
            if (sel) ogt_fill(&ctx, rp, "accent", 1, y, lw - 2, ih);
            ogt_text(rp, sel ? ogt_pen(&ctx, "accent.text") : ogt_pen(&ctx, "label"), 8, y + 2, latin, lw - 16);
        }
        for (;;) {
            int redraw = 0;
            Wait(1UL << pw->UserPort->mp_SigBit);
            while ((m = (struct IntuiMessage *)GetMsg(pw->UserPort))) {
                ULONG cls = m->Class;
                UWORD code = m->Code;
                int my = m->MouseY, mx = m->MouseX, row = my >= 1 ? (my - 1) / ih : -1, wheel_y = 0;
                if (cls == IDCMP_EXTENDEDMOUSE && code == IMSGCODE_INTUIWHEELDATA && m->IAddress)
                    wheel_y = ((struct IntuiWheelData *)m->IAddress)->WheelY;
                ReplyMsg((struct Message *)m);
                if (cls == IDCMP_INACTIVEWINDOW) done = 1;
                else if (cls == IDCMP_REFRESHWINDOW) { BeginRefresh(pw); EndRefresh(pw, TRUE); redraw = 1; }
                else if (cls == IDCMP_MOUSEMOVE && mx >= 0 && mx < lw && row >= 0 && row < shown && top + row < n &&
                         top + row != at) { at = top + row; redraw = 1; }
                else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTUP) {
                    /* let go over a choice: it; elsewhere (the click on the button that opened the list): the
                       list stays open, as Excel's does */
                    if (mx >= 0 && mx < lw && row >= 0 && row < shown && top + row < n) { chosen = top + row; done = 1; }
                } else if (cls == IDCMP_MOUSEBUTTONS && code == SELECTDOWN &&
                           (mx < 0 || mx >= lw || my < 0 || my >= lh)) done = 1;
                else if (cls == IDCMP_MOUSEBUTTONS && code == MENUDOWN) done = 1;
                else if (cls == IDCMP_VANILLAKEY && code == 13) { chosen = at; done = 1; }
                else if (cls == IDCMP_VANILLAKEY && code == 27) done = 1;
                else if ((cls == IDCMP_RAWKEY && (code == 0x4C || code == 0x4D)) || wheel_y) {
                    int down = cls == IDCMP_RAWKEY ? code == 0x4D : wheel_y > 0;
                    at = down ? (at + 1 < n ? at + 1 : at) : (at > 0 ? at - 1 : 0);
                    if (at < top) top = at;
                    if (at >= top + shown) top = at - shown + 1;
                    redraw = 1;
                }
            }
            if (done || redraw) break;
        }
    }
    CloseWindow(pw);
    ActivateWindow(win);
    return chosen;
}

/* The active cell's validation list: a choice is put in the cell as if typed. */
static void pick_from_list(int bx, int by, int bw, int bh)
{
    char **items = NULL;
    int n = 0, chosen;
    if (!core || oc_core_list_choices(core, &items, &n) != OWF_OK || !n) {
        oc_choices_free(items, n);
        set_status(n ? "Not enough memory for the list." : "The list has no choices.");
        return;
    }
    chosen = choice_popup(bx, by, bw, bh, items, n, oc_core_input_text(core));
    if (chosen >= 0) {
        unsigned row, col;
        oc_core_active(core, &row, &col);
        if (result(oc_core_set_input(core, oc_core_sheet(core), row, col, items[chosen]), NULL))
            set_status("Chosen from the list.");
    }
    oc_choices_free(items, n);
    refresh();
}

static int filter_condition_text(const char *text, int *op, const char **value)
{
    while (*text == ' ' || *text == '\t') text++;
    if (!strncmp(text, "<=", 2)) { *op = OC_FILTER_LESS_EQUAL; text += 2; }
    else if (!strncmp(text, ">=", 2)) { *op = OC_FILTER_GREATER_EQUAL; text += 2; }
    else if (!strncmp(text, "<>", 2) || !strncmp(text, "!=", 2)) { *op = OC_FILTER_NOT_EQUAL; text += 2; }
    else if (*text == '<') { *op = OC_FILTER_LESS; text++; }
    else if (*text == '>') { *op = OC_FILTER_GREATER; text++; }
    else { *op = OC_FILTER_EQUAL; if (*text == '=') text++; }
    while (*text == ' ' || *text == '\t') text++;
    *value = text;
    return *text != 0;
}

/* Split a Standard Filter into the two comparisons Excel permits for one
 * column. The spaces keep words inside values from being mistaken for joins. */
static int filter_conditions_text(char *text, int *op1, const char **value1,
                                  int *op2, const char **value2, int *use_or)
{
    char *p, *join = NULL;
    size_t join_len = 0;
    for (p = text; *p; p++) {
        if (!strncasecmp(p, " and ", 5)) {
            join = p;
            join_len = 5;
            *use_or = 0;
            break;
        }
        if (!strncasecmp(p, " or ", 4)) {
            join = p;
            join_len = 4;
            *use_or = 1;
            break;
        }
    }
    if (!join)
        return 0;
    *join = 0;
    return filter_condition_text(text, op1, value1) &&
           filter_condition_text(join + join_len, op2, value2) ? 1 : -1;
}

static int filter_rank_text(const char *text, unsigned *count, int *bottom, int *percent)
{
    char *end;
    unsigned long n;
    while (*text == ' ' || *text == '\t') text++;
    if (!strncasecmp(text, "top", 3) && (text[3] == ' ' || text[3] == '\t')) {
        *bottom = 0;
        text += 3;
    } else if (!strncasecmp(text, "bottom", 6) && (text[6] == ' ' || text[6] == '\t')) {
        *bottom = 1;
        text += 6;
    } else {
        return 0;
    }
    while (*text == ' ' || *text == '\t') text++;
    n = strtoul(text, &end, 10);
    if (end == text || !n)
        return 0;
    while (*end == ' ' || *end == '\t') end++;
    *percent = *end == '%';
    if (*percent) {
        end++;
        while (*end == ' ' || *end == '\t') end++;
    }
    if (*end || n > (*percent ? 100UL : 500UL))
        return 0;
    *count = (unsigned)n;
    return 1;
}

/* An AutoFilter heading's arrow: every distinct displayed value, plus All
 * and Standard and Top/Bottom filters.
 * A blank value has a visible label but remains an empty string in the core. */
static void pick_filter(unsigned col, int bx, int by, int bw, int bh)
{
    char **items = NULL, **shown = NULL;
    int n = 0, i, chosen, rc = oc_core_filter_choices(core, col, &items, &n);
    if (rc != OWF_OK) {
        oc_choices_free(items, n);
        result(rc, NULL);
        return;
    }
    shown = malloc((size_t)(n + 4) * sizeof *shown);
    if (!shown) {
        oc_choices_free(items, n);
        result(OWF_ERR_MEMORY, NULL);
        return;
    }
    shown[0] = (char *)"(All)";
    shown[1] = (char *)"(Standard Filter...)";
    shown[2] = (char *)"(Nonblanks)";
    shown[3] = (char *)"(Top/Bottom...)";
    for (i = 0; i < n; i++) shown[i + 4] = items[i][0] ? items[i] : (char *)"(Blanks)";
    chosen = choice_popup(bx, by, bw, bh, shown, n + 4, NULL);
    if (chosen >= 0) {
        if (chosen == 1) {
            char entered[256], *utf8, *utf8b;
            const char *value, *value2;
            int op, op2, use_or, pair;
            if (text_requester("Standard Filter", "Condition (* ? ~; and/or)", "= ", entered, sizeof entered)) {
                pair = filter_conditions_text(entered, &op, &value, &op2, &value2, &use_or);
                if (pair > 0) {
                    utf8 = from_latin1(value);
                    utf8b = from_latin1(value2);
                    if (!utf8 || !utf8b) result(OWF_ERR_MEMORY, NULL);
                    else result(oc_core_filter_conditions(core, col, op, utf8, op2, utf8b, use_or),
                                "Compound Standard Filter applied.");
                    owf_free(utf8);
                    owf_free(utf8b);
                } else if (!pair && filter_condition_text(entered, &op, &value)) {
                    if (!(utf8 = from_latin1(value))) result(OWF_ERR_MEMORY, NULL);
                    else {
                        result(oc_core_filter_condition(core, col, op, utf8), "Standard Filter applied.");
                        owf_free(utf8);
                    }
                } else {
                    set_status("Use >= 10 and <= 20, = A* or = *Ltd, or one comparison.");
                }
            }
        } else if (chosen == 2) {
            result(oc_core_filter_condition(core, col, OC_FILTER_NOT_EQUAL, ""), "Blank rows filtered out.");
        } else if (chosen == 3) {
            char entered[64];
            unsigned count;
            int bottom, percent;
            if (text_requester("Top/Bottom Filter", "Rank", "top 10", entered, sizeof entered)) {
                if (filter_rank_text(entered, &count, &bottom, &percent))
                    result(oc_core_filter_top(core, col, count, bottom, percent), "Top/Bottom Filter applied.");
                else
                    set_status("Use top 10, bottom 5, top 10% or bottom 20%.");
            }
        } else {
            rc = oc_core_filter_value(core, col, chosen ? items[chosen - 4] : NULL);
            result(rc, chosen ? "Rows filtered by value." : "This column's filter is cleared.");
        }
    }
    free(shown);
    oc_choices_free(items, n);
    refresh();
}

static void grid_press(int mx, int my, UWORD qual, ULONG secs, ULONG micros)
{
    oc_hit hit;
    unsigned m = mods_of(qual);
    int rc, from_bar = bar_active;
    if (bar_active) {                                /* typing in the bar: what it holds, then the click */
        bar_to_core();
        bar_active = 0;
    }
    if (!oc_core_editing(core)) {                   /* a selected handle resizes; a chart's body moves */
        int selected = oc_core_selected_chart(core), h = -1, k;
        if (selected >= 0 && oc_view_chart_box(core, selected, &chart_start_x, &chart_start_y,
                                               &chart_start_w, &chart_start_h)) {
            int px = mx - grid_box.x, py = my - grid_box.y, n;
            for (n = 0; n < 9; n++) {
                int hx, hy;
                if (n == 4) continue;
                hx = chart_start_x + (n % 3) * (chart_start_w - 1) / 2 - CHART_HANDLE / 2;
                hy = chart_start_y + (n / 3) * (chart_start_h - 1) / 2 - CHART_HANDLE / 2;
                if (px >= hx && px < hx + CHART_HANDLE && py >= hy && py < hy + CHART_HANDLE) {
                    h = n;
                    break;
                }
            }
        }
        if (h >= 0) {
            chart_drag = 1;
            chart_handle = h;
            chart_from_x = mx;
            chart_from_y = my;
            mouse_down = 1;
            set_status("Resize chart: drag the handle.");
            return;
        }
        k = oc_view_chart_at(core, mx - grid_box.x, my - grid_box.y);
        if (k >= 0) {
            oc_core_select_chart(core, k);
            chart_drag = 1;
            chart_handle = -1;
            chart_from_x = mx;
            chart_from_y = my;
            oc_view_chart_box(core, k, &chart_start_x, &chart_start_y, &chart_start_w, &chart_start_h);
            mouse_down = 1;
            set_status("Chart selected: drag it or its handles; arrow keys move it.");
            refresh();
            return;
        }
    }
    if (oc_core_selected_chart(core) >= 0) {        /* a click anywhere else: the chart no longer */
        oc_core_select_chart(core, -1);
        set_status("");
    }
    oc_view_hit(core, mx - grid_box.x, my - grid_box.y, &hit);
    if (hit.kind == OC_HIT_DROPDOWN) {               /* the list's button: its choices */
        pick_from_list(grid_box.x + hit.x, grid_box.y + hit.y, hit.w, hit.h);
        return;
    }
    if (hit.kind == OC_HIT_FILTER) {                 /* an AutoFilter heading's distinct values */
        pick_filter(hit.col, grid_box.x + hit.x, grid_box.y + hit.y, hit.w, hit.h);
        return;
    }
    if (hit.kind == OC_HIT_COL_EDGE || hit.kind == OC_HIT_ROW_EDGE) {
        resizing = 1;
        resize_axis = hit.kind == OC_HIT_COL_EDGE;
        resize_index = resize_axis ? hit.col : hit.row;
        resize_from = resize_axis ? mx : my;
        resize_size = resize_axis ? hit.w : hit.h;
        return;
    }
    if (hit.kind == OC_HIT_CELL && !oc_core_editing(core) && hit.row == click_row && hit.col == click_col &&
        DoubleClick(click_secs, click_micros, secs, micros)) {
        oc_edit_begin(core, 1);                      /* a double click edits the cell */
        click_secs = 0;
        refresh();
        return;
    }
    rc = oc_core_press(core, &hit, (m & OC_MOD_SHIFT) != 0, (m & OC_MOD_CTRL) != 0);
    if (rc != OWF_ERR_UNSUPPORTED) result(rc, NULL);
    if (from_bar && oc_core_editing(core) && g_input) {
        /* a reference typed into the formula: on typing in the bar */
        update_bar();
        ActivateGadget(g_input, win, NULL);
        bar_active = 1;
    }
    if (hit.kind == OC_HIT_CELL) {
        click_row = hit.row;
        click_col = hit.col;
        click_secs = secs;
        click_micros = micros;
    }
    mouse_down = 1;
    refresh();
}

/* The dragged chart's outline, drawn over the grid by complementing (drawn again, it goes); cut to the grid. */
static void outline(int x, int y, int w, int h)
{
    struct RastPort *rp = win ? win->RPort : NULL;
    int x0 = x, y0 = y, x1 = x + w - 1, y1 = y + h - 1;
    if (!rp || w < 2 || h < 2) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > grid_box.w - 1) x1 = grid_box.w - 1;
    if (y1 > grid_box.h - 1) y1 = grid_box.h - 1;
    if (x0 > x1 || y0 > y1) return;
    SetDrMd(rp, COMPLEMENT);
    if (y == y0) { Move(rp, grid_box.x + x0, grid_box.y + y0); Draw(rp, grid_box.x + x1, grid_box.y + y0); }
    if (y + h - 1 == y1 && y1 > y0) { Move(rp, grid_box.x + x0, grid_box.y + y1); Draw(rp, grid_box.x + x1, grid_box.y + y1); }
    if (x == x0 && y1 - y0 > 1) { Move(rp, grid_box.x + x0, grid_box.y + y0 + 1); Draw(rp, grid_box.x + x0, grid_box.y + y1 - 1); }
    if (x + w - 1 == x1 && x1 > x0 && y1 - y0 > 1) {
        Move(rp, grid_box.x + x1, grid_box.y + y0 + 1);
        Draw(rp, grid_box.x + x1, grid_box.y + y1 - 1);
    }
    SetDrMd(rp, JAM1);
}

static void outline_off(void)
{
    if (outline_x == -1) return;
    outline(outline_x, outline_y, outline_w, outline_h);
    outline_x = -1;
}

static void chart_drag_to(int mx, int my)
{
    int dx = mx - chart_from_x, dy = my - chart_from_y;
    int x = chart_start_x, y = chart_start_y, w = chart_start_w, h = chart_start_h;
    outline_off();
    if (oc_core_selected_chart(core) < 0 || w < 1 || h < 1) return;
    if (chart_handle < 0) {
        x += dx;
        y += dy;
    } else {
        int col = chart_handle % 3, row = chart_handle / 3;
        if (col == 0) { x += dx; w -= dx; }
        else if (col == 2) w += dx;
        if (row == 0) { y += dy; h -= dy; }
        else if (row == 2) h += dy;
        if (w < CHART_MIN_W) {
            if (col == 0) x = chart_start_x + chart_start_w - CHART_MIN_W;
            w = CHART_MIN_W;
        }
        if (h < CHART_MIN_H) {
            if (row == 0) y = chart_start_y + chart_start_h - CHART_MIN_H;
            h = CHART_MIN_H;
        }
    }
    outline_x = x;
    outline_y = y;
    outline_w = w;
    outline_h = h;
    outline(outline_x, outline_y, outline_w, outline_h);
}

static void grid_drag(int mx, int my)
{
    oc_hit hit;
    int x = mx - grid_box.x, y = my - grid_box.y;
    if (chart_drag) {
        chart_drag_to(mx, my);
        return;
    }
    if (resizing) return;
    /* past the grid's edge: scroll a line that way, then select to the edge */
    if (y >= grid_box.h || x >= grid_box.w || y < 0 || x < 0) {
        unsigned t, l;
        oc_view_scroll_pos(core, &t, &l);
        if (y >= grid_box.h) t++;
        else if (y < 0 && t > 0) t--;
        if (x >= grid_box.w) l++;
        else if (x < 0 && l > 0) l--;
        oc_view_scroll_to(core, t, l);
        if (y >= grid_box.h) y = grid_box.h - 2;
        if (x >= grid_box.w) x = grid_box.w - 2;
    }
    if (y < 0) y = 0;
    if (x < 0) x = 0;
    if (oc_view_hit(core, x, y, &hit) != OC_HIT_NONE && oc_core_drag(core, &hit) == OWF_OK) refresh();
}

static void grid_release(int mx, int my)
{
    if (chart_drag) {                                /* where the chart was dropped: move/resize there */
        int x, y, w, h, dx, dy, handle = chart_handle;
        chart_drag_to(mx, my);
        x = outline_x; y = outline_y; w = outline_w; h = outline_h;
        outline_off();
        chart_drag = 0;
        chart_handle = -1;
        mouse_down = 0;
        if (x == -1) return;
        dx = x - chart_start_x;
        dy = y - chart_start_y;
        if (handle >= 0 && (dx || dy || w != chart_start_w || h != chart_start_h)) {
            result(oc_core_transform_chart(core, dx, dy, w, h), "Chart resized.");
            refresh();
        } else if (handle < 0 && dx * dx + dy * dy >= 9) {
            result(oc_core_move_chart(core, dx, dy), "Chart moved.");
            refresh();
        }
        return;
    }
    if (resizing) {
        int d = (resize_axis ? mx : my) - resize_from, px = resize_size + d;
        int tw = oc_view_px_to_twips(core, px < 0 ? 0 : px);
        resizing = 0;
        if (d) {
            if (resize_axis) result(oc_core_set_cols_width(core, resize_index, resize_index, tw ? tw : 1), "Column width changed.");
            else result(oc_core_set_rows_height(core, resize_index, resize_index, tw ? tw : 1), "Row height changed.");
            refresh();
        }
    }
    mouse_down = 0;
}

static void tabs_press(int mx, int my, ULONG secs, ULONG micros)
{
    static ULONG last_secs, last_micros;
    static int last_tab = -2;
    int t = tab_at(mx, my);
    if (t < 0) return;
    if (tab_sheet[t] == TAB_LEFT || tab_sheet[t] == TAB_RIGHT) {
        scroll_tabs(tab_sheet[t] == TAB_LEFT ? -1 : 1);
        last_tab = -2;
        return;
    }
    if (tab_sheet[t] == TAB_PLUS) {
        if (oc_core_add_sheet(core, NULL) < 0) result(OWF_ERR_MEMORY, NULL);
        else set_status("Sheet added.");
    } else {
        oc_core_set_sheet(core, tab_sheet[t]);
        if (t == last_tab && DoubleClick(last_secs, last_micros, secs, micros)) {
            do_rename_sheet(tab_sheet[t]);
            last_tab = -2;
            refresh();
            return;
        }
    }
    last_tab = t;
    last_secs = secs;
    last_micros = micros;
    refresh();
}

static void events(void)
{
    struct IntuiMessage *im;
    defer_repaint = 1;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code, qual = im->Qualifier;
        APTR ia = im->IAddress;
        int mx = im->MouseX, my = im->MouseY;
        struct Gadget *g = (struct Gadget *)ia;
        ULONG secs = im->Seconds, micros = im->Micros;
        struct IntuiWheelData wheel;
        int have_wheel = 0;
        if (class == IDCMP_EXTENDEDMOUSE && code == IMSGCODE_INTUIWHEELDATA && ia) {
            wheel = *(const struct IntuiWheelData *)ia;      /* copied before the reply */
            have_wheel = 1;
        }
        GT_ReplyIMsg(im);
        switch (class) {
        case IDCMP_CLOSEWINDOW:
            if (confirm_discard_changes()) quit_now = 1;
            break;
        case IDCMP_NEWSIZE:
            relayout();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            defer_repaint = 0;
            draw_all();
            defer_repaint = 1;
            GT_EndRefresh(win, TRUE);
            break;
        case IDCMP_GADGETDOWN:
            if (g == g_input) {
                bar_active = 1;
                if (!oc_core_editing(core)) oc_edit_begin(core, 1);
            } else if (g == g_vscroll || g == g_hscroll) {
                scrolled(g == g_hscroll, code);
                refresh();
            } else if (g && g->GadgetID >= GID_TOOLBAR && g->GadgetID < GID_NAME)
                toolbar_event(class, g);
            break;
        case IDCMP_GADGETUP:
            if (g == g_input) {
                bar_to_core();
                bar_active = 0;
                result(oc_edit_commit(core, OC_MOVE_DOWN), NULL);
                refresh();
            } else if (g == g_name) {
                STRPTR v = NULL;
                GT_GetGadgetAttrs(g_name, win, NULL, GTST_String, (ULONG)&v, TAG_DONE);
                if (v && oc_core_goto(core, (const char *)v) != OWF_OK) set_status("That is not a cell, range, or navigable name.");
                shown_name[0] = 1;
                refresh();
            } else if (g == g_vscroll || g == g_hscroll) {
                scrolled(g == g_hscroll, code);
                refresh();
            } else if (g && g->GadgetID >= GID_TOOLBAR && g->GadgetID < GID_NAME) {
                toolbar_event(class, g);
                refresh();
            }
            break;
        case IDCMP_MOUSEMOVE:
            if (g == g_vscroll || g == g_hscroll) {
                scrolled(g == g_hscroll, code);
                refresh();
            } else if (mouse_down)
                grid_drag(mx, my);
            break;
        case IDCMP_INTUITICKS:
            if (mouse_down && !resizing && !chart_drag) {   /* held past the edge: keep scrolling */
                int x = mx - grid_box.x, y = my - grid_box.y;
                if (x < 0 || y < 0 || x >= grid_box.w || y >= grid_box.h) grid_drag(mx, my);
            }
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN) {
                if (mx >= grid_box.x && my >= grid_box.y && mx < grid_box.x + grid_box.w && my < grid_box.y + grid_box.h)
                    grid_press(mx, my, qual, secs, micros);
                else if (tab_at(mx, my) >= 0)
                    tabs_press(mx, my, secs, micros);
            } else if (code == SELECTUP) {
                if (mouse_down || resizing) grid_release(mx, my);
                if (tb.pressed >= 0) {
                    ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                    tb.pressed = -1;
                }
            }
            break;
        case IDCMP_RAWKEY:
            if (!(code & 0x80)) raw_key(code, qual);
            break;
        case IDCMP_EXTENDEDMOUSE:
            if (have_wheel && (wheel.WheelY || wheel.WheelX) && mx >= tabs_box.x && mx < tabs_box.x + tabs_box.w &&
                my >= tabs_box.y && my < tabs_box.y + tabs_box.h) {
                scroll_tabs((wheel.WheelY ? wheel.WheelY : wheel.WheelX) < 0 ? -1 : 1);   /* over the tabs: they scroll */
            } else if (have_wheel && (wheel.WheelY || wheel.WheelX)) {
                unsigned t, l;
                oc_view_scroll_pos(core, &t, &l);
                if (wheel.WheelY) t = wheel.WheelY < 0 ? (t > 3 ? t - 3 : 0) : t + 3;
                if (wheel.WheelX) l = wheel.WheelX < 0 ? (l > 1 ? l - 1 : 0) : l + 1;
                oc_view_scroll_to(core, t, l);
                refresh();
            }
            break;
        case IDCMP_VANILLAKEY:
            vanilla_key(code, qual);
            break;
        case IDCMP_MENUPICK:
            while (code != MENUNULL && win) {
                struct MenuItem *item = ItemAddress(menu, code);
                if (!item) break;
                menu_action((ULONG)GTMENUITEM_USERDATA(item));
                code = item->NextSelect;
            }
            refresh();
            break;
        }
    }
    defer_repaint = 0;
    if (repaint_pending && win) {
        repaint_pending = 0;
        refresh();
    }
}

static int window_main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)"GFOpenCalc needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    if (!owf_open()) {
        tell("GFOpenCalc", "GFOpenCalc needs " OWF_LIBRARY_NAME " in LIBS:.");
        rc = 20;
        goto out;
    }
    OpenFontTextBase = ogt_openfont_open();
    DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);  /* cells' fonts openfont can't open */
    LayersBase = OpenLibrary((STRPTR)"layers.library", 36);
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
    ogt_iconset_attach();   /* the toolbar's pictures come from the Chrome set (Images/Toolbar/) */
    read_theme_choice();
    oc_draw_list_init(&dl);
    if (!(core = oc_core_new(NULL))) { rc = 20; goto out; }

    if (argc > 1) snprintf(first, sizeof first, "%s", argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    if (!open_window()) {
        if (argc) PutStr((STRPTR)"GFOpenCalc: the window could not open\n");
        rc = 20;
        goto out;
    }
    rexx_open();
    {
        const owf_book_format *f = recovery_format();
        char ext[8];
        snprintf(ext, sizeof ext, "%s", f ? f->extensions : "ods");
        if (strchr(ext, ' ')) *strchr(ext, ' ') = 0;   /* the first of its names */
        ow_recovery_init(&recovery, "GFOpenCalc", ext);
    }
    autosave = ow_autosave_open(60);
    if (first[0]) open_book(first);
    else recovery_offer();
    refresh();

    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG asig = ow_autosave_signal(autosave);
        ULONG rsig = rexx_port ? (1UL << rexx_port->mp_SigBit) : 0;
        ULONG got = Wait(sig | asig | rsig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) {
            if (confirm_discard_changes()) quit_now = 1;
        }
        if (got & sig) events();
        if (got & rsig) rexx_messages();
        if ((got & asig) && ow_autosave_tick(autosave)) recovery_save();
    }

out:
    if (autosave) { ow_autosave_close(autosave); autosave = NULL; }
    ow_recovery_end(&recovery, core && oc_core_is_dirty(core));        /* unsaved: the copy waits for the next start */
    rexx_close();
    close_window();
    oc_draw_list_free(&dl);
    if (core) { oc_core_free(core); core = NULL; }
    if (theme.text) ogt_theme_free(&theme);
    if (LayersBase) { CloseLibrary(LayersBase); LayersBase = NULL; }
    if (OpenFontTextBase) { CloseLibrary(OpenFontTextBase); OpenFontTextBase = NULL; }
    if (DiskfontBase) { CloseLibrary(DiskfontBase); DiskfontBase = NULL; }
    if (CyberGfxBase) { CloseLibrary(CyberGfxBase); CyberGfxBase = NULL; }
    if (RexxSysBase) { CloseLibrary((struct Library *)RexxSysBase); RexxSysBase = NULL; }
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    owf_close();
    return rc;
}

int main(int argc, char **argv)
{
    return ow_run_with_stack(65536, window_main, argc, argv);
}
