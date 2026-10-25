/*
 * OpenCalc - the spreadsheet of the GFOffice Suite, beside OpenWrite.
 *
 * The window: OpenGadTools chrome as OpenWrite 2's (app/openwrite2.c), a
 * formula bar (the cell's name and what it holds), the grid, sheet tabs
 * and a status line. Everything about the sheet is OpenCalc's portable
 * core (src/calc, include/opencalc_core.h): this file maps keys, the
 * pointer and the menus to it and paints the list of things it says to
 * draw, into an off-screen bitmap when there is memory for one (so the
 * grid never flickers), else straight into the window. Workbooks are
 * libowf's (owf_book) and their formulas the engine's (owf_calc.h).
 * 68040 code, no FPU needed (libowf's doubles are soft maths).
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <utility/tagitem.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
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
#include "ogt_font.h"
#include "ogt_status.h"
#include "ow_fontwidths.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *OpenFontTextBase = NULL, *LayersBase = NULL;
struct Library *CyberGfxBase = NULL, *DiskfontBase = NULL;
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "OpenCalc 0.1.13 (25.10.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " OpenCalc, the spreadsheet of the GFOffice Suite. MIT, Copyright (c) 2026 AmigaChrome Team";

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
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_EXPORT_CSV, M_PRINT, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_FILL_DOWN, M_FILL_RIGHT, M_CLEAR, M_CLEAR_FORMATS, M_CLEAR_ALL,
    M_SELECT_ALL, M_SELECT_ROWS, M_SELECT_COLS, M_GOTO,
    M_FREEZE, M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_100,
    M_THEME_OPEN, M_THEME_GRAPHITE, M_THEME_EMBER, M_THEME_CLEAR, M_THEME_CLASSIC,
    M_INS_ROWS, M_INS_COLS, M_INS_SHEET, M_DEL_ROWS, M_DEL_COLS,
    M_BOLD, M_ITALIC, M_UNDERLINE,
    M_AL_GENERAL, M_AL_LEFT, M_AL_CENTRE, M_AL_RIGHT,
    M_NUMFMT = 100,                                  /* + oc_number_formats' index */
    M_FILL_NONE = 150, M_FILL_YELLOW, M_FILL_GREEN, M_FILL_BLUE, M_FILL_GREY, M_FILL_RED,
    M_COL_AUTO = 170, M_COL_RED, M_COL_BLUE, M_COL_GREEN,
    M_BORDER_ALL = 180, M_BORDER_OUTLINE, M_BORDER_BOTTOM, M_BORDER_NONE,
    M_COL_WIDTH = 190, M_ROW_HEIGHT, M_HIDE_COLS, M_SHOW_COLS, M_HIDE_ROWS, M_SHOW_ROWS,
    M_SHEET_RENAME = 200, M_SHEET_DELETE, M_SHEET_NEXT, M_SHEET_PREV
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", NULL, 0, 0, (APTR)M_SAVE_AS },
    { NM_ITEM, "Export CSV...", NULL, 0, 0, (APTR)M_EXPORT_CSV },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Print...", "P", NM_ITEMDISABLED, 0, (APTR)M_PRINT },
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
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Column Width...", NULL, 0, 0, (APTR)M_COL_WIDTH },
    { NM_ITEM, "Row Height...", NULL, 0, 0, (APTR)M_ROW_HEIGHT },
    { NM_ITEM, "Hide Columns", NULL, 0, 0, (APTR)M_HIDE_COLS },
    { NM_ITEM, "Show Columns", NULL, 0, 0, (APTR)M_SHOW_COLS },
    { NM_ITEM, "Hide Rows", NULL, 0, 0, (APTR)M_HIDE_ROWS },
    { NM_ITEM, "Show Rows", NULL, 0, 0, (APTR)M_SHOW_ROWS },

    { NM_TITLE, "Sheet", NULL, 0, 0, NULL },
    { NM_ITEM, "Rename...", NULL, 0, 0, (APTR)M_SHEET_RENAME },
    { NM_ITEM, "Delete", NULL, 0, 0, (APTR)M_SHEET_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Next Sheet", "]", 0, 0, (APTR)M_SHEET_NEXT },
    { NM_ITEM, "Previous Sheet", "[", 0, 0, (APTR)M_SHEET_PREV },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static const ogt_tool tools[] = {
    { C_NEW,   "New",     OGT_ICON_FILE,    0, 0, 0 },
    { C_OPEN,  "Open...", OGT_ICON_OPEN,    0, 0, 0 },
    { C_SAVE,  "Save",    OGT_ICON_DISK,    0, 0, 0 },
    { C_UNDO,  "Undo",    OGT_ICON_PARENT,  1, 1, 0 },
    { C_REDO,  "Redo",    OGT_ICON_FORWARD, 0, 1, 0 },
    { C_CUT,   "Cut",     OGT_ICON_DELETE,  1, 0, 0 },
    { C_COPY,  "Copy",    OGT_ICON_ARCHIVE, 0, 0, 0 },
    { C_PASTE, "Paste",   OGT_ICON_EXTRACT, 0, 0, 0 }
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
static char rexx_name[16];                           /* OPENCALC, or OPENCALC.2 for a second copy */
#define REXX_PORT_NAME "OPENCALC"
#define RECOVERY_BASE "T:OpenCalc-Recovery"
#define RECOVERY_META "T:OpenCalc-Recovery.meta"
static char current_path[512];
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
static void relayout(void);
static void menu_action(ULONG id);

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

/* ---- Text: the model is UTF-8, Amiga fonts and gadgets ISO-8859-1 ---- */

/* Latin-1 for the screen: common typography outside it to the nearest
 * Latin-1, as OpenWrite shows it, anything else '?'. */
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

/* TextLength with a system-made bold's smear counted (as OpenWrite 2 measures). */
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
 * that draws it is OpenWrite's choice (app/ow_fontwidths.c, ow_fw_face): that family when it opens, else the
 * first of its kind's that does (openfont.library can't read OS 3.2's Intellifont CGTriumvirate, which has no
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
 * nearest designed size (scaled only when it has none). Through openfont.library, then diskfont.library (the
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
    /* OpenGadTools' status bar (ogt_status) fills with "panel", a key no theme has (THEME_SPEC.md 5): ogt_pen
     * then gives pen 1, black, under the status line's dark text. Until OpenGadTools takes a key themes
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
    char *s = read_file("ENV:OpenGadTools/Theme");
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

/* What a core result means to the user; 1 when it went well. */
static int result(int rc, const char *done)
{
    switch (rc) {
    case OWF_OK:
        if (done) set_status(done);
        if (core && oc_core_undo_lost(core)) set_status("Done; it was too big to keep for Undo.");
        return 1;
    case OWF_ERR_MEMORY:
        tell("OpenCalc", "There is not enough memory to complete that.");
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
    case OC_ROLE_CELL_FILL: return rgb_pen(rgb);
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
    /* dimmed: the theme's muted (the style guide, opengadtools #85, 4.2): in the Open theme #4a5663 on the tabs'
       #dde2e8 in the light mode (5.8:1, against tab.text's 12:1), #a9b4bf on #313c48 in the dark (5.3:1 against
       9.2:1): dimmer than the arrow that works in both */
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
    snprintf(title, sizeof title, "%s%s - OpenCalc", core && oc_core_is_dirty(core) ? "*" : "", name);
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

/* The part of the screen a full-size window may have (OpenWrite 2's free_area()): below the title bar,
 * less OpenDock's strip; ENV:OpenDock/Free wins when the dock publishes one. */
static void free_area(int *l, int *t, int *w, int *h)
{
    char buf[48];
    struct Window *dw;
    ULONG lock;
    LONG got;
    int top = scr->BarHeight + 1, bottom = scr->Height, left = 0, right = scr->Width, a, b, c, d;
    got = GetVar((STRPTR)"OpenDock/Free", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
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
        if (!dw->ScreenTitle || strncmp((const char *)dw->ScreenTitle, "OpenDock", 8) != 0)
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

/* The first size, every Open app's (10 October 2026): 800 x 600 centred in the free area, never bigger. */
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
        WA_Title, (ULONG)"OpenCalc",
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

/* A one-line requester (OpenWrite 2's): the text in Latin-1, as typed. 1 for OK. */
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
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"OpenCalc",
                             (UBYTE *)"The workbook has unsaved changes.\n\nDiscard them?", (UBYTE *)"Discard|Cancel" };
    if (!core || !oc_core_is_dirty(core)) return 1;
    return EasyRequest(win, &es, NULL) != 0;
}

static int ask_file(int save, const char *title, const char *initial, char *out, size_t out_size)
{
    struct FileRequester *fr;
    char last[512];
    int ok = 0;
    lastdir_get("OpenCalc", last, sizeof last);
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
        lastdir_put("OpenCalc", (const char *)fr->fr_Drawer);
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
 * What a file's import or export left out, in OpenCalc's own window: EasyRequest cut its lines short on the
 * lab's RTG screen ("Formatting of whole rows is kept on their cel"). The window is as wide as its widest
 * line in the window's font, up to three quarters of the screen (longer lines wrap at spaces), and as tall
 * as its lines, up to the screen (the rest left out, and said so); the OK button below the last line. A
 * line starting with NOTE_BULLET is a bulleted note: a round dot, its text (and the lines it wraps onto)
 * indented after it. Return, Escape, OK or the close gadget close it. */
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

static void notes_window(const char *title, const char *text)
{
    struct Gadget *list = NULL, *gok = NULL;
    struct NewGadget ng;
    struct Window *rw;
    struct RastPort trp;
    char *buf;
    note_line *line;
    int n, i, ww = 0, wh, okw, okh = fh + 8, lh = fh + 2, maxw, maxlines, cut = 0, done = 0, text_h, indent;
    if (!scr || !vi || !font) { tell(title, text); return; }
    buf = malloc(NOTE_TEXT);
    line = malloc(NOTE_LINES * sizeof *line);
    if (!buf || !line) { free(buf); free(line); tell(title, text); return; }
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
    okw = TextLength(&trp, (STRPTR)"OK", 2) + 48;
    ww += 32;
    if (ww < okw + 32) ww = okw + 32;
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
        ng.ng_LeftEdge = rw->BorderLeft + (ww - okw) / 2;
        ng.ng_TopEdge = rw->BorderTop + text_h + 12;
        ng.ng_Width = okw;
        ng.ng_Height = okh;
        ng.ng_GadgetText = (STRPTR)"OK";
        ng.ng_GadgetID = 1;
        gok = CreateGadget(BUTTON_KIND, gok, &ng, TAG_DONE);
        if (gok) AddGList(rw, list, (UWORD)~0, -1, NULL);
    }
    if (!rw || !gok) {
        if (rw) CloseWindow(rw);
        if (list) FreeGadgets(list);
        free(buf);
        free(line);
        tell(title, text);
        return;
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
                GT_ReplyIMsg(m);
                if (cls == IDCMP_CLOSEWINDOW || cls == IDCMP_GADGETUP) done = 1;
                else if (cls == IDCMP_VANILLAKEY && (code == 13 || code == 27)) done = 1;
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
        snprintf(msg, sizeof msg, "OpenCalc could not open the workbook.\n\n%s", owf_error_text(rc));
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
            snprintf(msg, sizeof msg, "OpenCalc can't write that kind of file yet.\n\nIt can write: %s.", list);
        else
            snprintf(msg, sizeof msg, "OpenCalc could not save the workbook.\n\n%s", owf_error_text(rc));
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
    if (ask_file(1, "Save OpenCalc workbook", initial, path, sizeof path))
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

/* ---- Recovery: the workbook kept in T: every minute while it has changes ---- */

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

static void recovery_path(const owf_book_format *f, char *out, size_t size)
{
    char ext[24];
    snprintf(ext, sizeof ext, "%s", f->extensions);
    if (strchr(ext, ' ')) *strchr(ext, ' ') = 0;
    snprintf(out, size, "%s.%s", RECOVERY_BASE, ext);
}

static void recovery_clear(void)
{
    int i, n = owf_book_format_count();
    char path[64];
    for (i = 0; i < n; i++) {
        const owf_book_format *f = owf_book_format_at(i);
        if (!f || !f->export) continue;
        recovery_path(f, path, sizeof path);
        DeleteFile((STRPTR)path);
    }
    DeleteFile((STRPTR)RECOVERY_META);
}

static void recovery_save(void)
{
    const owf_book_format *f = recovery_format();
    owf_report *report;
    char path[64];
    BPTR meta;
    int was_dirty;
    if (!core || !oc_core_is_dirty(core) || !f || oc_core_editing(core)) return;
    recovery_path(f, path, sizeof path);
    report = owf_report_new();
    was_dirty = oc_core_is_dirty(core);
    if (owf_export_book_file(oc_core_book(core), path, f->name, report) == OWF_OK) {
        if ((meta = Open((STRPTR)RECOVERY_META, MODE_NEWFILE))) {
            const char *name = current_path[0] ? current_path : "Untitled";
            Write(meta, (APTR)name, strlen(name));
            Write(meta, (APTR)"\n", 1);
            Close(meta);
        }
        set_status("Autosave recovery copy updated.");
        draw_status();
    }
    (void)was_dirty;
    owf_report_free(report);
}

static void recovery_offer(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"OpenCalc Recovery",
        (UBYTE *)"OpenCalc found a recovery copy of a workbook from an interrupted session.\n\nRecover it?",
        (UBYTE *)"Recover|Discard" };
    const owf_book_format *f = recovery_format();
    char path[64];
    BPTR lock;
    if (!f) return;
    recovery_path(f, path, sizeof path);
    if (!(lock = Lock((STRPTR)path, ACCESS_READ))) return;
    UnLock(lock);
    if (!EasyRequest(win, &es, NULL)) { recovery_clear(); return; }
    open_book(path);
    current_path[0] = 0;                             /* it is not the file it came from: Save asks */
    current_format[0] = 0;
    oc_core_set_dirty(core);                         /* so that it can't be lost without asking */
    set_status("Recovered workbook. Save it to keep the recovered work.");
}

/* ---- Commands ---- */

static void about(void)
{
    tell("OpenCalc",
         VERSION_TEXT "\n\n"
         "OpenCalc, the spreadsheet of the GFOffice Suite.\n\n"
         "OpenGadTools interface\n"
         "libowf workbooks and formula engine\n\n"
         "MIT licence, Copyright (c) 2026 AmigaChrome Team.");
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
    char name[40] = "";
    if (text_requester("Go To", "Cell", "", name, sizeof name) && oc_core_goto(core, name) != OWF_OK)
        set_status("That is not a cell or a range.");
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
    case M_ABOUT: about(); break;
    case M_QUIT: if (confirm_discard_changes()) quit_now = 1; break;
    case M_UNDO: do_command(C_UNDO); break;
    case M_REDO: do_command(C_REDO); break;
    case M_CUT: do_copy(1); break;
    case M_COPY: do_copy(0); break;
    case M_PASTE: do_paste(); break;
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
    case M_SHEET_RENAME: do_rename_sheet(oc_core_sheet(core)); break;
    case M_SHEET_DELETE: do_delete_sheet(); break;
    case M_SHEET_NEXT: oc_core_key(core, OC_KEY_PAGEDOWN, OC_MOD_CTRL); break;
    case M_SHEET_PREV: oc_core_key(core, OC_KEY_PAGEUP, OC_MOD_CTRL); break;
    }
}

/* ---- ARexx: the OPENCALC port ---- */

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
        snprintf(result_text, sizeof result_text, "VERSION HELP ACTIVATE ISDIRTY GETFILE OPEN SAVE SAVEAS EXPORT GETCELL GETFORMULA "
                 "SETCELL GOTO SHEET SHEETS ADDSHEET RECALC COMMAND FONTCHECK GETCOMMENT QUIT");
    else if (starts_word_ci(command, "ACTIVATE", &arg)) {
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
    } else { ok = 0; snprintf(result_text, sizeof result_text, "Unknown OpenCalc ARexx command"); }
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
    for (n = 1; n < 10; n++) {                       /* OPENCALC, then OPENCALC.2 for a second copy */
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

static void grid_press(int mx, int my, UWORD qual, ULONG secs, ULONG micros)
{
    oc_hit hit;
    unsigned m = mods_of(qual);
    int rc, from_bar = bar_active;
    if (bar_active) {                                /* typing in the bar: what it holds, then the click */
        bar_to_core();
        bar_active = 0;
    }
    oc_view_hit(core, mx - grid_box.x, my - grid_box.y, &hit);
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

static void grid_drag(int mx, int my)
{
    oc_hit hit;
    int x = mx - grid_box.x, y = my - grid_box.y;
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
                if (v && oc_core_goto(core, (const char *)v) != OWF_OK) set_status("That is not a cell or a range.");
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
            if (mouse_down && !resizing) {               /* held past the edge: keep scrolling */
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
        if (argc) PutStr((STRPTR)"OpenCalc needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    if (!owf_open()) {
        tell("OpenCalc", "OpenCalc needs owf.library 1 in LIBS:.");
        rc = 20;
        goto out;
    }
    OpenFontTextBase = ogt_openfont_open();
    DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);  /* cells' fonts openfont can't open */
    LayersBase = OpenLibrary((STRPTR)"layers.library", 36);
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
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
        if (argc) PutStr((STRPTR)"OpenCalc: the window could not open\n");
        rc = 20;
        goto out;
    }
    rexx_open();
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
    if (core && !oc_core_is_dirty(core)) recovery_clear();

out:
    if (autosave) { ow_autosave_close(autosave); autosave = NULL; }
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
