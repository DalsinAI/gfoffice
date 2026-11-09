/*
 * GFOpenPresentation - the GFOffice Suite's slides, on AmigaOS 3.2.
 *
 * P2's program, with P3's first editing (Design-OpenPresentation.md, 12):
 * opens ODP and PPTX (libowf's deck formats), shows the slide at the
 * window's size with a strip of the slides beside it, lets a text box's
 * text be typed in (src/deck/opr_edit.c: the caret, backspace and delete,
 * new paragraphs, undo), saves ODP, exports PDF and prints through
 * GFOpenPrint (the deck's PDF to T:, then C:GFOpenPrint on it, as GFOpenWrite
 * does).
 *
 * The slide is laid out by GFOpenLayout through the core (opr_slide_layout),
 * measured with the PDF's standard fonts' widths, and drawn here from its
 * display list at the screen's pixels: each glyph where the layout put it,
 * so the screen, the caret and the PDF agree. Shapes and crop are consumed
 * from OpenLayout's shared display metadata. Pictures go through datatypes,
 * as GFOpenWrite's do.
 *
 * Chrome: GFOpenGadTools' toolbar with the Chrome icon set
 * (ogt_iconset_attach), status bar and list, in the theme
 * (gfopengadtools docs/style/README.md). 68040, no FPU needed.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/regions.h>
#include <hardware/blit.h>
#include <devices/inputevent.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <cybergraphx/cybergraphics.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/datatypes.h>
#include <proto/cybergraphics.h>
#include <proto/diskfont.h>
#include <proto/layers.h>
#include "ogt_openfont.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <clib/alib_protos.h>

#include "openpresentation_core.h"
#include "ow_clipboard.h"
#ifdef OPENPUBLISHER
#include "openpublisher_core.h"
#endif
#include "ow_layout.h"
#include "owf_pdf.h"
#include "ow_stack.h"
#include "lastdir.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_iconset.h"
#include "ogt_font.h"
#include "ogt_list.h"
#include "ogt_status.h"
#include "ogt_ruler.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *OpenFontTextBase = NULL, *LayersBase = NULL;
struct Library *DataTypesBase = NULL, *CyberGfxBase = NULL, *DiskfontBase = NULL;

#ifdef OPENPUBLISHER
#define PROGRAM "GFOpenPublisher"
#define VERSION_TEXT "GFOpenPublisher 0.3.11 (9.11.2026)"
#define UNIT "Page"
#define UNIT_LOWER "page"
#define UNITS_LOWER "pages"
#define DOCUMENT_LOWER "publication"
#define TRANSITION_TITLE "Page Transition"
#define SHOW_TITLE "Page Preview"
#else
#define PROGRAM "GFOpenPresentation"
#define VERSION_TEXT "GFOpenPresentation 0.6.17 (9.11.2026)"
#define UNIT "Slide"
#define UNIT_LOWER "slide"
#define UNITS_LOWER "slides"
#define DOCUMENT_LOWER "presentation"
#define TRANSITION_TITLE "Slide Transition"
#define SHOW_TITLE "Slide Show"
#endif
#define GRID_TWIPS 720
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " MIT, Copyright (c) 2026 AmiCradle Team";

enum { C_NEW = 1, C_OPEN, C_SAVE, C_PRINT, C_PDF, C_UNDO, C_REDO };
enum { GID_TOOLBAR = 100, GID_SLIDES = 400, GID_HSCROLL = 500, GID_VSCROLL = 501 };
enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_PRINT, M_PDF, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_FIND, M_FIND_NEXT, M_REPLACE, M_REPLACE_ALL, M_SELECT_ALL, M_STOP,
    M_TEXT_BOLD, M_TEXT_ITALIC, M_TEXT_UNDERLINE,
    M_TEXT_SIZE_10, M_TEXT_SIZE_12, M_TEXT_SIZE_14, M_TEXT_SIZE_18, M_TEXT_SIZE_24, M_TEXT_SIZE_36,
    M_TEXT_FONT, M_TEXT_COLOUR,
    M_TEXT_ALIGN_LEFT, M_TEXT_ALIGN_CENTRE, M_TEXT_ALIGN_RIGHT, M_TEXT_ALIGN_JUSTIFY,
    M_TEXT_COLUMNS_1, M_TEXT_COLUMNS_2, M_TEXT_COLUMNS_3, M_TEXT_FRAME,
    M_INSERT_TEXTBOX, M_INSERT_PICTURE, M_INSERT_RECT, M_INSERT_ROUNDRECT, M_INSERT_ELLIPSE, M_INSERT_TRIANGLE,
    M_INSERT_DIAMOND, M_INSERT_ARROW, M_INSERT_LINE,
    M_ROTATE_LEFT, M_ROTATE_RIGHT,
    M_BRING_FRONT, M_BRING_FORWARD, M_SEND_BACKWARD, M_SEND_BACK,
    M_ALIGN_LEFT, M_ALIGN_CENTRE, M_ALIGN_RIGHT,
    M_ALIGN_TOP, M_ALIGN_MIDDLE, M_ALIGN_BOTTOM,
    M_POSITION_SIZE, M_APPEARANCE, M_PICTURE_CROP, M_DUPLICATE_OBJECT, M_DELETE_OBJECT,
    M_WRAP_NONE, M_WRAP_SQUARE, M_WRAP_TIGHT, M_WRAP_TOP_BOTTOM, M_WRAP_DISTANCE,
    M_LINK_TEXT_NEXT, M_UNLINK_TEXT,
    M_NEW_SLIDE, M_DUPLICATE_SLIDE, M_RENAME_SLIDE, M_DELETE_SLIDE, M_HIDE_SLIDE, M_SHOW_SLIDE, M_TRANSITION,
    M_MOVE_SLIDE_UP, M_MOVE_SLIDE_DOWN,
    M_FIRST, M_PREV, M_NEXT, M_LAST, M_SHOW_GRID, M_SNAP_GRID,
    M_TB_BOTH, M_TB_ICONS, M_TB_TEXT,
    M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_FIT,
    M_SHOW_FIRST, M_SHOW_CURRENT, M_SHOW_LOOP
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", NULL, 0, 0, (APTR)M_SAVE_AS },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Print...", "P", 0, 0, (APTR)M_PRINT },
    { NM_ITEM, "Export PDF...", NULL, 0, 0, (APTR)M_PDF },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },

    { NM_TITLE, "Edit", NULL, 0, 0, NULL },
    { NM_ITEM, "Undo", "Z", 0, 0, (APTR)M_UNDO },
    { NM_ITEM, "Redo", NULL, 0, 0, (APTR)M_REDO },
    { NM_ITEM, "Cut", "X", 0, 0, (APTR)M_CUT },
    { NM_ITEM, "Copy", "C", 0, 0, (APTR)M_COPY },
    { NM_ITEM, "Paste", "V", 0, 0, (APTR)M_PASTE },
    { NM_ITEM, "Find...", "F", 0, 0, (APTR)M_FIND },
    { NM_ITEM, "Find Next", "G", 0, 0, (APTR)M_FIND_NEXT },
    { NM_ITEM, "Replace...", NULL, 0, 0, (APTR)M_REPLACE },
    { NM_ITEM, "Replace All in Text Box...", NULL, 0, 0, (APTR)M_REPLACE_ALL },
    { NM_ITEM, "Select All Text", "A", 0, 0, (APTR)M_SELECT_ALL },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Stop Typing", NULL, 0, 0, (APTR)M_STOP },

    { NM_TITLE, "Format", NULL, 0, 0, NULL },
    { NM_ITEM, "Bold", "B", 0, 0, (APTR)M_TEXT_BOLD },
    { NM_ITEM, "Italic", "I", 0, 0, (APTR)M_TEXT_ITALIC },
    { NM_ITEM, "Underline", "U", 0, 0, (APTR)M_TEXT_UNDERLINE },
    { NM_ITEM, "Font Size", NULL, 0, 0, NULL },
    { NM_SUB, "10 pt", NULL, 0, 0, (APTR)M_TEXT_SIZE_10 },
    { NM_SUB, "12 pt", NULL, 0, 0, (APTR)M_TEXT_SIZE_12 },
    { NM_SUB, "14 pt", NULL, 0, 0, (APTR)M_TEXT_SIZE_14 },
    { NM_SUB, "18 pt", NULL, 0, 0, (APTR)M_TEXT_SIZE_18 },
    { NM_SUB, "24 pt", NULL, 0, 0, (APTR)M_TEXT_SIZE_24 },
    { NM_SUB, "36 pt", NULL, 0, 0, (APTR)M_TEXT_SIZE_36 },
    { NM_ITEM, "Font Family...", NULL, 0, 0, (APTR)M_TEXT_FONT },
    { NM_ITEM, "Text Colour...", NULL, 0, 0, (APTR)M_TEXT_COLOUR },
    { NM_ITEM, "Paragraph Alignment", NULL, 0, 0, NULL },
    { NM_SUB, "Left", NULL, 0, 0, (APTR)M_TEXT_ALIGN_LEFT },
    { NM_SUB, "Centre", NULL, 0, 0, (APTR)M_TEXT_ALIGN_CENTRE },
    { NM_SUB, "Right", NULL, 0, 0, (APTR)M_TEXT_ALIGN_RIGHT },
    { NM_SUB, "Justify", NULL, 0, 0, (APTR)M_TEXT_ALIGN_JUSTIFY },
    { NM_ITEM, "Text Columns", NULL, 0, 0, NULL },
    { NM_SUB, "One", NULL, 0, 0, (APTR)M_TEXT_COLUMNS_1 },
    { NM_SUB, "Two", NULL, 0, 0, (APTR)M_TEXT_COLUMNS_2 },
    { NM_SUB, "Three", NULL, 0, 0, (APTR)M_TEXT_COLUMNS_3 },
    { NM_ITEM, "Text Frame...", NULL, 0, 0, (APTR)M_TEXT_FRAME },

    { NM_TITLE, "Insert", NULL, 0, 0, NULL },
    { NM_ITEM, "Text Box", NULL, 0, 0, (APTR)M_INSERT_TEXTBOX },
    { NM_ITEM, "Picture...", NULL, 0, 0, (APTR)M_INSERT_PICTURE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Rectangle", NULL, 0, 0, (APTR)M_INSERT_RECT },
    { NM_ITEM, "Rounded Rectangle", NULL, 0, 0, (APTR)M_INSERT_ROUNDRECT },
    { NM_ITEM, "Ellipse", NULL, 0, 0, (APTR)M_INSERT_ELLIPSE },
    { NM_ITEM, "Triangle", NULL, 0, 0, (APTR)M_INSERT_TRIANGLE },
    { NM_ITEM, "Diamond", NULL, 0, 0, (APTR)M_INSERT_DIAMOND },
    { NM_ITEM, "Right Arrow", NULL, 0, 0, (APTR)M_INSERT_ARROW },
    { NM_ITEM, "Line", NULL, 0, 0, (APTR)M_INSERT_LINE },

    { NM_TITLE, "Object", NULL, 0, 0, NULL },
    { NM_ITEM, "Rotate Left 15 Degrees", NULL, 0, 0, (APTR)M_ROTATE_LEFT },
    { NM_ITEM, "Rotate Right 15 Degrees", NULL, 0, 0, (APTR)M_ROTATE_RIGHT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Bring to Front", NULL, 0, 0, (APTR)M_BRING_FRONT },
    { NM_ITEM, "Bring Forward", NULL, 0, 0, (APTR)M_BRING_FORWARD },
    { NM_ITEM, "Send Backward", NULL, 0, 0, (APTR)M_SEND_BACKWARD },
    { NM_ITEM, "Send to Back", NULL, 0, 0, (APTR)M_SEND_BACK },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Align Left", NULL, 0, 0, (APTR)M_ALIGN_LEFT },
    { NM_ITEM, "Align Centre", NULL, 0, 0, (APTR)M_ALIGN_CENTRE },
    { NM_ITEM, "Align Right", NULL, 0, 0, (APTR)M_ALIGN_RIGHT },
    { NM_ITEM, "Align Top", NULL, 0, 0, (APTR)M_ALIGN_TOP },
    { NM_ITEM, "Align Middle", NULL, 0, 0, (APTR)M_ALIGN_MIDDLE },
    { NM_ITEM, "Align Bottom", NULL, 0, 0, (APTR)M_ALIGN_BOTTOM },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Position and Size...", NULL, 0, 0, (APTR)M_POSITION_SIZE },
    { NM_ITEM, "Appearance...", NULL, 0, 0, (APTR)M_APPEARANCE },
    { NM_ITEM, "Picture Crop...", NULL, 0, 0, (APTR)M_PICTURE_CROP },
#ifdef OPENPUBLISHER
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Text Wrapping", NULL, 0, 0, NULL },
    { NM_SUB, "None", NULL, 0, 0, (APTR)M_WRAP_NONE },
    { NM_SUB, "Square", NULL, 0, 0, (APTR)M_WRAP_SQUARE },
    { NM_SUB, "Tight", NULL, 0, 0, (APTR)M_WRAP_TIGHT },
    { NM_SUB, "Top and Bottom", NULL, 0, 0, (APTR)M_WRAP_TOP_BOTTOM },
    { NM_ITEM, "Text Wrap Distance...", NULL, 0, 0, (APTR)M_WRAP_DISTANCE },
    { NM_ITEM, "Link to Next Empty Text Box", NULL, 0, 0, (APTR)M_LINK_TEXT_NEXT },
    { NM_ITEM, "Remove Box from Text Flow", NULL, 0, 0, (APTR)M_UNLINK_TEXT },
#endif
    { NM_ITEM, "Duplicate", NULL, 0, 0, (APTR)M_DUPLICATE_OBJECT },
    { NM_ITEM, "Delete", NULL, 0, 0, (APTR)M_DELETE_OBJECT },

    { NM_TITLE, UNIT, NULL, 0, 0, NULL },
    { NM_ITEM, "New " UNIT, "M", 0, 0, (APTR)M_NEW_SLIDE },
    { NM_ITEM, "Duplicate " UNIT, NULL, 0, 0, (APTR)M_DUPLICATE_SLIDE },
    { NM_ITEM, "Rename " UNIT "...", NULL, 0, 0, (APTR)M_RENAME_SLIDE },
    { NM_ITEM, "Delete " UNIT, NULL, 0, 0, (APTR)M_DELETE_SLIDE },
    { NM_ITEM, "Hide " UNIT, NULL, 0, 0, (APTR)M_HIDE_SLIDE },
    { NM_ITEM, "Show " UNIT, NULL, 0, 0, (APTR)M_SHOW_SLIDE },
    { NM_ITEM, UNIT " Transition...", NULL, 0, 0, (APTR)M_TRANSITION },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Move " UNIT " Up", NULL, 0, 0, (APTR)M_MOVE_SLIDE_UP },
    { NM_ITEM, "Move " UNIT " Down", NULL, 0, 0, (APTR)M_MOVE_SLIDE_DOWN },

    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "First " UNIT, NULL, 0, 0, (APTR)M_FIRST },
    { NM_ITEM, "Previous " UNIT, "[", 0, 0, (APTR)M_PREV },
    { NM_ITEM, "Next " UNIT, "]", 0, 0, (APTR)M_NEXT },
    { NM_ITEM, "Last " UNIT, NULL, 0, 0, (APTR)M_LAST },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Zoom In", "+", 0, 0, (APTR)M_ZOOM_IN },
    { NM_ITEM, "Zoom Out", "-", 0, 0, (APTR)M_ZOOM_OUT },
    { NM_ITEM, "Fit " UNIT " to Window", "0", 0, 0, (APTR)M_ZOOM_FIT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Show 1/2 Inch Grid", NULL, CHECKIT, 0, (APTR)M_SHOW_GRID },
    { NM_ITEM, "Snap to 1/2 Inch Grid", NULL, CHECKIT, 0, (APTR)M_SNAP_GRID },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Buttons", NULL, 0, 0, NULL },
    { NM_SUB, "Icons and Text", NULL, CHECKIT, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons", NULL, CHECKIT | CHECKED, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },

    { NM_TITLE, "Show", NULL, 0, 0, NULL },
    { NM_ITEM, "Start from First", NULL, 0, 0, (APTR)M_SHOW_FIRST },
    { NM_ITEM, "Start from Current", NULL, 0, 0, (APTR)M_SHOW_CURRENT },
    { NM_ITEM, "Loop from First", NULL, 0, 0, (APTR)M_SHOW_LOOP },

    { NM_TITLE, "Help", NULL, 0, 0, NULL },
    { NM_ITEM, "About " PROGRAM "...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* The Chrome icon set's pictures by name, the vector icons as their fallback. The set has no Show or New
 * Slide picture yet, so the first player is reached through its menu and F5. */
static const ogt_tool tools[] = {
    { C_NEW,   "New",       OGT_ICON_FILE,    0, 0, 0, "new" },
    { C_OPEN,  "Open...",   OGT_ICON_OPEN,    0, 0, 0, "open" },
    { C_SAVE,  "Save",      OGT_ICON_DISK,    0, 0, 0, "save" },
    { C_PRINT, "Print...",  OGT_ICON_TEST,    1, 0, 0, "print" },
    { C_PDF,   "PDF...",    OGT_ICON_SENT,    0, 0, 0, "pdf" },
    { C_UNDO,  "Undo",      OGT_ICON_PARENT,  1, 1, 0, "undo" },
    { C_REDO,  "Redo",      OGT_ICON_FORWARD, 0, 1, 0, "redo" }
};

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;
static ogt_list *strip;                              /* the slides, a thumbnail each */
static ogt_status sbar;
static ogt_ruler ruler;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;
static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS;
static int quit_now;

static box area, toolbar_box, strip_box, ruler_box, view_box, slide_box, status_box;
/* Zoom: the slide at a percent of its size fitted to the view; larger than the view, it scrolls (pan, in
 * pixels) with the scrollers along the view's right and bottom edges, and is drawn only inside slide_area. */
static const int zooms[] = { 100, 150, 200, 300, 400 };
static int zoom = 100, pan_x, pan_y;
static box slide_area;
static struct Gadget *hscroll, *vscroll;
#define SCROLLER 14

static owf_deck *deck;
static int current;                                  /* the slide shown */
static char current_path[512];
static char current_format[16] = "ODP";
static int dirty;
static char status[320] = "Ready.";
static opr_undo *undo;

/* The selected slide object, and typing within it. */
static int selected_obj = -1;
static int edit_obj = -1;
static opr_pos caret;
static opr_selection text_selection;
static int text_selecting;
static int edit_grouped;
static int show_grid;
static int snap_grid;
static char find_text[160];
static char replacement_text[160];

enum {
    DRAG_NONE, DRAG_MOVE, DRAG_NW, DRAG_N, DRAG_NE, DRAG_E,
    DRAG_SE, DRAG_S, DRAG_SW, DRAG_W
};
static struct {
    int active, moved, mode, obj, mouse_x, mouse_y;
    owf_obj original, preview;
} object_drag;

static void draw_all(void);
static void draw_slide(void);
static void draw_status(void);
static void layout(void);
static void insert_picture(void);
static void update_ruler(void);
static void delete_selected_object(void);

/* ---- Small helpers ---- */

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

static int ask(const char *title, const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)buttons };
    return (int)EasyRequest(win, &es, NULL, (ULONG)text);
}

static void set_status(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n >= sizeof status) n = sizeof status - 1;
    if (n) memcpy(status, s, n);
    status[n] = 0;
}

static const char *leaf(const char *path)
{
    const char *s = FilePart((STRPTR)path);
    return s && *s ? s : path;
}

static int screen_depth(void)
{
    if (((struct Library *)GfxBase)->lib_Version >= 39)
        return (int)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    return scr->RastPort.BitMap->Depth;
}

static LONG argb_pen(uint32_t argb)
{
    return ogt_pen_rgb(&ctx, (ogt_rgb){ (UBYTE)(argb >> 16), (UBYTE)(argb >> 8), (UBYTE)argb });
}

/* Text and lines need a real pen, and on an RTG screen the shared palette
   runs out (a slide's red came out olive). So one pen of our own, set to
   each colour just before it draws: on a true-colour screen that changes
   nothing already drawn. Fills are drawn in exact colours already. */
static LONG own_pen = -1;
static int own_pen_tried;

static LONG exact_pen(LONG pen)
{
    ULONG rgb;
    if (!(pen & OGT_DIRECT) || !scr) return pen;
    if (own_pen < 0 && !own_pen_tried) {
        own_pen_tried = 1;
        if (((struct Library *)GfxBase)->lib_Version >= 39)
            own_pen = ObtainPen(scr->ViewPort.ColorMap, (ULONG)-1, 0, 0, 0, PEN_EXCLUSIVE | PEN_NO_SETCOLOR);
    }
    if (own_pen < 0) return pen;
    rgb = (ULONG)pen & 0xffffffUL;
    SetRGB32(&scr->ViewPort, (ULONG)own_pen, ((rgb >> 16) & 255) * 0x01010101UL,
             ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL);
    return own_pen;
}

static void own_pen_free(void)
{
    if (own_pen >= 0 && scr) ReleasePen(scr->ViewPort.ColorMap, (ULONG)own_pen);
    own_pen = -1;
    own_pen_tried = 0;
}

/* UTF-8 to the Amiga fonts' ISO-8859-1: a character as the bytes drawn for it, at most 4 (*n of them; 0 for
 * one that draws nothing). Its own byte; the nearest for the typography PDF's WinAnsi has beyond ISO-8859-1
 * (quotes, dashes, "..." for the ellipsis, "TM"); else the PDF writer's own stand-in (owf_pdf_fallback:
 * "->" for an arrow, "v" for a tick, "<=" for less or equal), so the screen draws what the PDF prints, in
 * the room the layout measured for it; '?' for the rest. */
static void latin1_of(unsigned long cp, unsigned char out[4], int *n)
{
    const char *s = NULL;
    *n = 1;
    if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) {
        out[0] = (unsigned char)cp;
        return;
    }
    switch (cp) {
    case 0x2022: case 0x25CF: case 0x25AA: case 0x2219: case 0x25A0: case 0x25E6: case 0x27A2: out[0] = 0xB7; return;
    case 0x2018: case 0x2019: case 0x201A: case 0x2039: s = "'"; break;
    case 0x201C: case 0x201D: case 0x201E: s = "\""; break;
    case 0x203A: s = "'"; break;
    case 0x2013: case 0x2212: s = "-"; break;
    case 0x2014: s = "--"; break;                    /* drawn as wide as the PDF's dash */
    case 0x2026: s = "..."; break;
    case 0x2122: s = "TM"; break;
    case 0x20AC: s = "E"; break;
    case 0x2020: case 0x2021: s = "+"; break;
    case 0x2030: s = "%"; break;
    case 0x0152: s = "OE"; break;
    case 0x0153: s = "oe"; break;
    case 0x0160: s = "S"; break;
    case 0x0161: s = "s"; break;
    case 0x017D: s = "Z"; break;
    case 0x017E: s = "z"; break;
    case 0x0178: s = "Y"; break;
    case 0x0192: s = "f"; break;
    case 0x02C6: s = "^"; break;
    case 0x02DC: s = "~"; break;
    case 0x00AD: *n = 0; return;
    default: s = owf_pdf_fallback(cp); break;
    }
    if (!s) {
        out[0] = '?';
        return;
    }
    /* the stand-ins are ASCII or ISO-8859-1 (x as U+00D7), in UTF-8 */
    for (*n = 0; *s && *n < 4; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == 0xC2 || c == 0xC3) {
            out[(*n)++] = (unsigned char)(((c & 3) << 6) | ((unsigned char)s[1] & 0x3F));
            if (s[1]) ++s;
        } else {
            out[(*n)++] = c;
        }
    }
}

/* The code point at *at (moved on past it); a broken byte is U+FFFD. */
static unsigned long utf8_next(const char *s, size_t n, size_t *at)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned long cp = p[*at];
    int len = cp < 0x80 ? 1 : (cp & 0xE0) == 0xC0 ? 2 : (cp & 0xF0) == 0xE0 ? 3 : (cp & 0xF8) == 0xF0 ? 4 : 0, k;
    if (!len || *at + (size_t)len > n) {
        (*at)++;
        return 0xFFFD;
    }
    if (len > 1) {
        cp &= 0x3F >> (len - 1);
        for (k = 1; k < len; k++) {
            if ((p[*at + k] & 0xC0) != 0x80) {
                (*at)++;
                return 0xFFFD;
            }
            cp = cp << 6 | (p[*at + k] & 0x3F);
        }
    }
    *at += (size_t)len;
    return cp;
}

/* Slide names use UTF-8 in the model and ISO-8859-1 in an Amiga string
 * gadget, as the text painter does. */
static void name_to_latin1(const char *utf8, char *out, size_t cap)
{
    size_t at = 0, n = utf8 ? strlen(utf8) : 0, used = 0;
    if (!cap) return;
    while (at < n && used + 1 < cap) {
        unsigned char bytes[4];
        int k, count;
        latin1_of(utf8_next(utf8, n, &at), bytes, &count);
        for (k = 0; k < count && used + 1 < cap; k++)
            out[used++] = (char)bytes[k];
    }
    out[used] = 0;
}

static char *name_from_latin1(const char *latin)
{
    size_t i, n = latin ? strlen(latin) : 0, at = 0;
    char *utf8 = owf_malloc(n * 2 + 1);
    if (!utf8) return NULL;
    for (i = 0; i < n; i++) {
        unsigned c = (unsigned char)latin[i];
        if (c < 0x80) utf8[at++] = (char)c;
        else {
            utf8[at++] = (char)(0xC0 | (c >> 6));
            utf8[at++] = (char)(0x80 | (c & 0x3F));
        }
    }
    utf8[at] = 0;
    return utf8;
}

/* A modal one-line GadTools requester. */
static int text_requester(const char *title, const char *label, const char *initial, char *out, size_t out_size)
{
    struct Gadget *list = NULL, *last, *gtext, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    STRPTR value = NULL;
    int done = 0, ok = 0, ww = 430, wh, lw;
    if (!scr || !vi || !win || !out || out_size < 2) return 0;
    lw = TextLength(win->RPort, (STRPTR)label, strlen(label)) + 16;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    last = CreateContext(&list);
    ng.ng_LeftEdge = lw; ng.ng_TopEdge = 15; ng.ng_Width = ww - lw - 18; ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (STRPTR)label; ng.ng_GadgetID = 1; ng.ng_Flags = PLACETEXT_LEFT;
    last = gtext = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)(initial ? initial : ""),
                                GTST_MaxChars, (ULONG)(out_size - 1), GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_GadgetText = (STRPTR)"OK"; ng.ng_GadgetID = 2;
    ng.ng_LeftEdge = ww - 188; ng.ng_TopEdge = 48; ng.ng_Width = 78; ng.ng_Height = fh + 8;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 3; ng.ng_LeftEdge = ww - 98;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
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

static int transition_requester(owf_transition *transition, int *apply_all)
{
    static STRPTR effect_labels[] = {
        (STRPTR)"None", (STRPTR)"Fade", (STRPTR)"Fade through Black", (STRPTR)"Wipe", (STRPTR)"Push",
        (STRPTR)"Cover", (STRPTR)"Uncover", (STRPTR)"Split", (STRPTR)"Blinds", (STRPTR)"Dissolve",
        (STRPTR)"Other (kept)", NULL
    };
    static STRPTR direction_labels[] = {
        (STRPTR)"From Left", (STRPTR)"From Right", (STRPTR)"From Top", (STRPTR)"From Bottom", NULL
    };
    struct Gadget *list = NULL, *last, *geffect, *gdirection, *gduration, *gauto, *gdelay, *gall, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    LONG v;
    int done = 0, ok = 0, ww = 500, wh = 230;
    if (!transition || !apply_all || !scr || !vi) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    last = CreateContext(&list);

    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_LeftEdge = 185; ng.ng_TopEdge = 14; ng.ng_Width = 230; ng.ng_Height = 18;
    ng.ng_GadgetText = (STRPTR)"Effect"; ng.ng_GadgetID = 1;
    last = geffect = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)effect_labels,
                                  GTCY_Active, (ULONG)(transition->kind <= OWF_TRANS_OTHER ? transition->kind : 0),
                                  TAG_DONE);
    ng.ng_TopEdge = 46; ng.ng_GadgetText = (STRPTR)"Direction"; ng.ng_GadgetID = 2;
    last = gdirection = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)direction_labels,
                                     GTCY_Active, (ULONG)(transition->direction <= OWF_DIR_DOWN ? transition->direction : 0),
                                     TAG_DONE);
    ng.ng_TopEdge = 78; ng.ng_Width = 100; ng.ng_GadgetText = (STRPTR)"Duration ms (0 default)"; ng.ng_GadgetID = 3;
    last = gduration = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)transition->duration,
                                    GTIN_MaxChars, 6, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_LeftEdge = 185; ng.ng_TopEdge = 112; ng.ng_Width = 22; ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"Advance automatically"; ng.ng_GadgetID = 4;
    last = gauto = CreateGadget(CHECKBOX_KIND, last, &ng, GTCB_Checked, transition->advance_after >= 0, TAG_DONE);
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_LeftEdge = 185; ng.ng_TopEdge = 142; ng.ng_Width = 100; ng.ng_Height = 18;
    ng.ng_GadgetText = (STRPTR)"Advance after (ms)"; ng.ng_GadgetID = 5;
    last = gdelay = CreateGadget(INTEGER_KIND, last, &ng,
                                 GTIN_Number, (LONG)(transition->advance_after >= 0 ? transition->advance_after : 5000),
                                 GTIN_MaxChars, 8, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_LeftEdge = 185; ng.ng_TopEdge = 175; ng.ng_Width = 22; ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"Apply to all " UNITS_LOWER; ng.ng_GadgetID = 6;
    last = gall = CreateGadget(CHECKBOX_KIND, last, &ng, GTCB_Checked, FALSE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 198; ng.ng_Height = 20; ng.ng_Width = 78; ng.ng_LeftEdge = 330;
    ng.ng_GadgetText = (STRPTR)"Apply"; ng.ng_GadgetID = 7;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 414; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 8;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!geffect || !gdirection || !gduration || !gauto || !gdelay || !gall || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)TRANSITION_TITLE, WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP | CYCLEIDCMP | CHECKBOXIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
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
                if (id == 7) { ok = 1; done = 1; }
                else if (id == 8) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(geffect, rw, NULL, GTCY_Active, (ULONG)&v, TAG_DONE); transition->kind = (owf_transition_kind)v;
        GT_GetGadgetAttrs(gdirection, rw, NULL, GTCY_Active, (ULONG)&v, TAG_DONE); transition->direction = (unsigned char)v;
        GT_GetGadgetAttrs(gduration, rw, NULL, GTIN_Number, (ULONG)&v, TAG_DONE); transition->duration = (int)v;
        GT_GetGadgetAttrs(gauto, rw, NULL, GTCB_Checked, (ULONG)&v, TAG_DONE);
        if (v) {
            GT_GetGadgetAttrs(gdelay, rw, NULL, GTIN_Number, (ULONG)&v, TAG_DONE);
            transition->advance_after = (int)v;
        } else transition->advance_after = -1;
        GT_GetGadgetAttrs(gall, rw, NULL, GTCB_Checked, (ULONG)&v, TAG_DONE); *apply_all = v != 0;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

static void shade_text(unsigned long shade, char out[7])
{
    snprintf(out, 7, "%06lx", OWF_SHADE_SET(shade) ? OWF_SHADE_RGB(shade) : 0UL);
}

static int shade_from_text(const char *text, unsigned long *shade)
{
    char *end;
    unsigned long rgb;
    size_t n;
    if (!text || !shade) return 0;
    if (*text == '#') text++;
    n = strlen(text);
    if (n != 6) return 0;
    rgb = strtoul(text, &end, 16);
    if (end != text + 6 || *end) return 0;
    *shade = OWF_SHADE(rgb);
    return 1;
}

static int appearance_style_index(unsigned char style)
{
    if (style == OWF_LINE_DASHED || style == OWF_LINE_MEDIUM_DASHED) return 1;
    if (style == OWF_LINE_DOTTED) return 2;
    if (style == OWF_LINE_DASH_DOT || style == OWF_LINE_MEDIUM_DASH_DOT ||
        style == OWF_LINE_DASH_DOT_DOT || style == OWF_LINE_MEDIUM_DASH_DOT_DOT ||
        style == OWF_LINE_SLANTED_DASH_DOT) return 3;
    return 0;
}

static int geometry_requester(owf_obj *box)
{
    struct Gadget *list = NULL, *last, *gx, *gy, *gw, *gh, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    LONG x, y, width, height;
    int done = 0, ok = 0, ww = 430, wh = 196;
    if (!box || !scr || !vi) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = 176;
    ng.ng_Width = 122;
    ng.ng_Height = 18;
    last = CreateContext(&list);

    ng.ng_TopEdge = 14; ng.ng_GadgetText = (STRPTR)"Left (twips)"; ng.ng_GadgetID = 1;
    last = gx = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)box->x,
                             GTIN_MaxChars, 8, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 48; ng.ng_GadgetText = (STRPTR)"Top (twips)"; ng.ng_GadgetID = 2;
    last = gy = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)box->y,
                             GTIN_MaxChars, 8, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 82; ng.ng_GadgetText = (STRPTR)"Width (twips)"; ng.ng_GadgetID = 3;
    last = gw = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)box->width,
                             GTIN_MaxChars, 8, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 116; ng.ng_GadgetText = (STRPTR)"Height (twips)"; ng.ng_GadgetID = 4;
    last = gh = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)box->height,
                             GTIN_MaxChars, 8, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 154; ng.ng_Height = 20; ng.ng_Width = 78; ng.ng_LeftEdge = 252;
    ng.ng_GadgetText = (STRPTR)"Apply"; ng.ng_GadgetID = 5;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 336; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 6;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gx || !gy || !gw || !gh || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"Position and Size", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
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
                if (id == 5) { ok = 1; done = 1; }
                else if (id == 6) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(gx, rw, NULL, GTIN_Number, (ULONG)&x, TAG_DONE);
        GT_GetGadgetAttrs(gy, rw, NULL, GTIN_Number, (ULONG)&y, TAG_DONE);
        GT_GetGadgetAttrs(gw, rw, NULL, GTIN_Number, (ULONG)&width, TAG_DONE);
        GT_GetGadgetAttrs(gh, rw, NULL, GTIN_Number, (ULONG)&height, TAG_DONE);
        box->x = (int)x; box->y = (int)y;
        box->width = (int)width; box->height = (int)height;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

static int text_frame_requester(int *columns, int *gap, int inset[4])
{
    struct Gadget *list = NULL, *last, *gcolumns, *ggap, *gleft, *gtop, *gright, *gbottom, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    LONG values[6];
    int done = 0, ok = 0, ww = 460, wh = 274;
    if (!columns || !gap || !inset || !scr || !vi) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = 222;
    ng.ng_Width = 110;
    ng.ng_Height = 18;
    last = CreateContext(&list);

    ng.ng_TopEdge = 14; ng.ng_GadgetText = (STRPTR)"Columns (1-16)"; ng.ng_GadgetID = 1;
    last = gcolumns = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)*columns,
                                   GTIN_MaxChars, 2, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 48; ng.ng_GadgetText = (STRPTR)"Column gap (twips)"; ng.ng_GadgetID = 2;
    last = ggap = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)*gap,
                               GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 82; ng.ng_GadgetText = (STRPTR)"Left margin (twips)"; ng.ng_GadgetID = 3;
    last = gleft = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)inset[0],
                                GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 116; ng.ng_GadgetText = (STRPTR)"Top margin (twips)"; ng.ng_GadgetID = 4;
    last = gtop = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)inset[1],
                               GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 150; ng.ng_GadgetText = (STRPTR)"Right margin (twips)"; ng.ng_GadgetID = 5;
    last = gright = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)inset[2],
                                 GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 184; ng.ng_GadgetText = (STRPTR)"Bottom margin (twips)"; ng.ng_GadgetID = 6;
    last = gbottom = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)inset[3],
                                  GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 226; ng.ng_Height = 20; ng.ng_Width = 78; ng.ng_LeftEdge = 282;
    ng.ng_GadgetText = (STRPTR)"Apply"; ng.ng_GadgetID = 7;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 366; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 8;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gcolumns || !ggap || !gleft || !gtop || !gright || !gbottom || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"Text Frame", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
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
                if (id == 7) { ok = 1; done = 1; }
                else if (id == 8) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(gcolumns, rw, NULL, GTIN_Number, (ULONG)&values[0], TAG_DONE);
        GT_GetGadgetAttrs(ggap, rw, NULL, GTIN_Number, (ULONG)&values[1], TAG_DONE);
        GT_GetGadgetAttrs(gleft, rw, NULL, GTIN_Number, (ULONG)&values[2], TAG_DONE);
        GT_GetGadgetAttrs(gtop, rw, NULL, GTIN_Number, (ULONG)&values[3], TAG_DONE);
        GT_GetGadgetAttrs(gright, rw, NULL, GTIN_Number, (ULONG)&values[4], TAG_DONE);
        GT_GetGadgetAttrs(gbottom, rw, NULL, GTIN_Number, (ULONG)&values[5], TAG_DONE);
        *columns = (int)values[0]; *gap = (int)values[1];
        inset[0] = (int)values[2]; inset[1] = (int)values[3];
        inset[2] = (int)values[4]; inset[3] = (int)values[5];
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

#ifdef OPENPUBLISHER
static int wrap_distance_requester(int gap[4])
{
    struct Gadget *list = NULL, *last, *gtop, *gright, *gbottom, *gleft, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    LONG top, right, bottom, left;
    int done = 0, ok = 0, ww = 430, wh = 196;
    if (!gap || !scr || !vi) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = 205;
    ng.ng_Width = 110;
    ng.ng_Height = 18;
    last = CreateContext(&list);

    ng.ng_TopEdge = 14; ng.ng_GadgetText = (STRPTR)"Top distance (twips)"; ng.ng_GadgetID = 1;
    last = gtop = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)gap[0],
                               GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 48; ng.ng_GadgetText = (STRPTR)"Right distance (twips)"; ng.ng_GadgetID = 2;
    last = gright = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)gap[1],
                                 GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 82; ng.ng_GadgetText = (STRPTR)"Bottom distance (twips)"; ng.ng_GadgetID = 3;
    last = gbottom = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)gap[2],
                                  GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 116; ng.ng_GadgetText = (STRPTR)"Left distance (twips)"; ng.ng_GadgetID = 4;
    last = gleft = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)gap[3],
                                GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 154; ng.ng_Height = 20; ng.ng_Width = 78; ng.ng_LeftEdge = 252;
    ng.ng_GadgetText = (STRPTR)"Apply"; ng.ng_GadgetID = 5;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 336; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 6;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gtop || !gright || !gbottom || !gleft || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"Text Wrap Distance", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
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
                if (id == 5) { ok = 1; done = 1; }
                else if (id == 6) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(gtop, rw, NULL, GTIN_Number, (ULONG)&top, TAG_DONE);
        GT_GetGadgetAttrs(gright, rw, NULL, GTIN_Number, (ULONG)&right, TAG_DONE);
        GT_GetGadgetAttrs(gbottom, rw, NULL, GTIN_Number, (ULONG)&bottom, TAG_DONE);
        GT_GetGadgetAttrs(gleft, rw, NULL, GTIN_Number, (ULONG)&left, TAG_DONE);
        gap[0] = (int)top; gap[1] = (int)right;
        gap[2] = (int)bottom; gap[3] = (int)left;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}
#endif

static int picture_crop_requester(int crop[4])
{
    struct Gadget *list = NULL, *last, *gleft, *gtop, *gright, *gbottom, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    LONG values[4];
    int done = 0, ok = 0, ww = 450, wh = 196;
    if (!crop || !scr || !vi) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = 220;
    ng.ng_Width = 100;
    ng.ng_Height = 18;
    last = CreateContext(&list);

    ng.ng_TopEdge = 14; ng.ng_GadgetText = (STRPTR)"Left crop (0-999)"; ng.ng_GadgetID = 1;
    last = gleft = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)crop[0],
                                GTIN_MaxChars, 3, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 48; ng.ng_GadgetText = (STRPTR)"Top crop (0-999)"; ng.ng_GadgetID = 2;
    last = gtop = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)crop[1],
                               GTIN_MaxChars, 3, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 82; ng.ng_GadgetText = (STRPTR)"Right crop (0-999)"; ng.ng_GadgetID = 3;
    last = gright = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)crop[2],
                                 GTIN_MaxChars, 3, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 116; ng.ng_GadgetText = (STRPTR)"Bottom crop (0-999)"; ng.ng_GadgetID = 4;
    last = gbottom = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)crop[3],
                                  GTIN_MaxChars, 3, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 154; ng.ng_Height = 20; ng.ng_Width = 78; ng.ng_LeftEdge = 272;
    ng.ng_GadgetText = (STRPTR)"Apply"; ng.ng_GadgetID = 5;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 356; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 6;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gleft || !gtop || !gright || !gbottom || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"Picture Crop", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | INTEGERIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
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
                if (id == 5) { ok = 1; done = 1; }
                else if (id == 6) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(gleft, rw, NULL, GTIN_Number, (ULONG)&values[0], TAG_DONE);
        GT_GetGadgetAttrs(gtop, rw, NULL, GTIN_Number, (ULONG)&values[1], TAG_DONE);
        GT_GetGadgetAttrs(gright, rw, NULL, GTIN_Number, (ULONG)&values[2], TAG_DONE);
        GT_GetGadgetAttrs(gbottom, rw, NULL, GTIN_Number, (ULONG)&values[3], TAG_DONE);
        crop[0] = (int)values[0]; crop[1] = (int)values[1];
        crop[2] = (int)values[2]; crop[3] = (int)values[3];
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

static int appearance_requester(owf_look *look)
{
    static STRPTR style_labels[] = {
        (STRPTR)"Solid", (STRPTR)"Dashed", (STRPTR)"Dotted", (STRPTR)"Dash-dot", NULL
    };
    static STRPTR arrow_labels[] = {
        (STRPTR)"None", (STRPTR)"Start", (STRPTR)"End", (STRPTR)"Both", NULL
    };
    static const unsigned char styles[] = {
        OWF_LINE_THIN, OWF_LINE_DASHED, OWF_LINE_DOTTED, OWF_LINE_DASH_DOT
    };
    struct Gadget *list = NULL, *last, *gfill, *gnofill, *gline, *gnoline;
    struct Gadget *gwidth, *gstyle, *garrows, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    STRPTR fill_value = NULL, line_value = NULL;
    char fill_text[7], line_text[7];
    LONG v;
    int done = 0, ok = 0, invalid = 0, ww = 520, wh = 238;
    if (!look || !scr || !vi) return 0;
    shade_text(look->fill, fill_text);
    shade_text(look->line, line_text);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    last = CreateContext(&list);

    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_LeftEdge = 190; ng.ng_TopEdge = 14;
    ng.ng_Width = 105; ng.ng_Height = 18; ng.ng_GadgetText = (STRPTR)"Fill colour (RRGGBB)"; ng.ng_GadgetID = 1;
    last = gfill = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)fill_text,
                                GTST_MaxChars, 6, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_LeftEdge = 330; ng.ng_Width = 22; ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"No fill"; ng.ng_GadgetID = 2;
    last = gnofill = CreateGadget(CHECKBOX_KIND, last, &ng, GTCB_Checked, !OWF_SHADE_SET(look->fill), TAG_DONE);

    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_LeftEdge = 190; ng.ng_TopEdge = 48;
    ng.ng_Width = 105; ng.ng_Height = 18; ng.ng_GadgetText = (STRPTR)"Line colour (RRGGBB)"; ng.ng_GadgetID = 3;
    last = gline = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)line_text,
                                GTST_MaxChars, 6, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = PLACETEXT_RIGHT; ng.ng_LeftEdge = 330; ng.ng_Width = 22; ng.ng_Height = 14;
    ng.ng_GadgetText = (STRPTR)"No line"; ng.ng_GadgetID = 4;
    last = gnoline = CreateGadget(CHECKBOX_KIND, last, &ng, GTCB_Checked, !OWF_SHADE_SET(look->line), TAG_DONE);

    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_LeftEdge = 190; ng.ng_TopEdge = 82;
    ng.ng_Width = 105; ng.ng_Height = 18; ng.ng_GadgetText = (STRPTR)"Line width (twips)"; ng.ng_GadgetID = 5;
    last = gwidth = CreateGadget(INTEGER_KIND, last, &ng, GTIN_Number, (LONG)look->line_width,
                                 GTIN_MaxChars, 5, GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 116; ng.ng_Width = 180; ng.ng_GadgetText = (STRPTR)"Line style"; ng.ng_GadgetID = 6;
    last = gstyle = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)style_labels,
                                 GTCY_Active, appearance_style_index(look->line_style), TAG_DONE);
    ng.ng_TopEdge = 150; ng.ng_GadgetText = (STRPTR)"Arrowheads"; ng.ng_GadgetID = 7;
    last = garrows = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)arrow_labels,
                                  GTCY_Active, (ULONG)(look->arrows & 3), TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 194; ng.ng_Height = 20; ng.ng_Width = 78; ng.ng_LeftEdge = 340;
    ng.ng_GadgetText = (STRPTR)"Apply"; ng.ng_GadgetID = 8;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 424; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 9;
    gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gfill || !gnofill || !gline || !gnoline || !gwidth || !gstyle || !garrows || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"Object Appearance", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | STRINGIDCMP |
                  INTEGERIDCMP | CYCLEIDCMP | CHECKBOXIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
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
                if (id == 8) { ok = 1; done = 1; }
                else if (id == 9) done = 1;
            }
        }
    }
    if (ok) {
        GT_GetGadgetAttrs(gnofill, rw, NULL, GTCB_Checked, (ULONG)&v, TAG_DONE);
        if (v) look->fill = 0;
        else {
            GT_GetGadgetAttrs(gfill, rw, NULL, GTST_String, (ULONG)&fill_value, TAG_DONE);
            if (!shade_from_text((char *)fill_value, &look->fill)) invalid = 1;
        }
        GT_GetGadgetAttrs(gnoline, rw, NULL, GTCB_Checked, (ULONG)&v, TAG_DONE);
        if (v) look->line = 0;
        else {
            GT_GetGadgetAttrs(gline, rw, NULL, GTST_String, (ULONG)&line_value, TAG_DONE);
            if (!shade_from_text((char *)line_value, &look->line)) invalid = 1;
        }
        GT_GetGadgetAttrs(gwidth, rw, NULL, GTIN_Number, (ULONG)&v, TAG_DONE); look->line_width = (int)v;
        GT_GetGadgetAttrs(gstyle, rw, NULL, GTCY_Active, (ULONG)&v, TAG_DONE);
        look->line_style = styles[v >= 0 && v < 4 ? v : 0];
        GT_GetGadgetAttrs(garrows, rw, NULL, GTCY_Active, (ULONG)&v, TAG_DONE);
        look->arrows = (unsigned char)(v >= 0 && v < 4 ? v : 0);
        if (look->line_width < 0 || look->line_width > 10000) invalid = 1;
        if (invalid) ok = 0;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    if (invalid)
        tell("Object Appearance", "Colours must be six hexadecimal digits (RRGGBB). Line width must be 0 to 10000 twips.");
    return ok;
}

/* ---- Fonts ----
 * A deck's faces drawn with the Amiga's: sans as CGTriumvirate, serif as CGTimes, mono as courier (OS 3.2's
 * Intellifont outlines and bitmaps, as GFOpenWrite maps them), at the pixel size the scale gives, through
 * gfopenfont.library, else diskfont (which scales and draws outlines), else the fonts in memory. Kept open in a
 * small cache. */

#define FONT_CACHE 20
static struct { char file[40]; int pixels, style; struct TextFont *font; } fonts[FONT_CACHE];
static int font_next;
static struct { char file[40]; int pixels, style; } font_misses[16];
static int font_miss_next;
static int diskfont_tried;

static void font_cache_clear(void)
{
    int i;
    for (i = 0; i < FONT_CACHE; ++i) {
        if (fonts[i].font) CloseFont(fonts[i].font);
        memset(&fonts[i], 0, sizeof fonts[i]);
    }
    memset(font_misses, 0, sizeof font_misses);
    font_next = font_miss_next = 0;
}

static struct TextFont *open_route(int route, struct TextAttr *ta, UBYTE flags)
{
    ta->ta_Flags = flags;
    switch (route) {
    case 0: return OpenFontTextBase ? ogt_openfont_font(OpenFontTextBase, ta) : NULL;
    case 1:
        if (!DiskfontBase && !diskfont_tried) {
            diskfont_tried = 1;
            DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
        }
        return DiskfontBase ? OpenDiskFont(ta) : NULL;
    default: return OpenFont(ta);
    }
}

/* style: FSF_BOLD and FSF_ITALIC asked of the font itself (diskfont has an outline drawn bold or slanted by
 * its engine, cleaner than graphics' smear at small sizes); what the font comes without is the caller's. */
static struct TextFont *font_px(const char *file, int pixels, int style)
{
    struct TextAttr ta;
    struct TextFont *f = NULL;
    int i, slot = -1, route;
    if (pixels < 4) pixels = 4;
    if (pixels > 160) pixels = 160;
    for (i = 0; i < FONT_CACHE; ++i) {
        if (fonts[i].font && fonts[i].pixels == pixels && fonts[i].style == style && !strcmp(fonts[i].file, file))
            return fonts[i].font;
        if (!fonts[i].font && slot < 0) slot = i;
    }
    for (i = 0; i < 16; ++i)
        if (font_misses[i].pixels == pixels && font_misses[i].style == style && !strcmp(font_misses[i].file, file))
            return NULL;
    memset(&ta, 0, sizeof ta);
    ta.ta_Name = (STRPTR)file;
    ta.ta_YSize = (UWORD)pixels;
    ta.ta_Style = (UBYTE)style;
    for (route = 0; !f && route < 2; ++route)
        if ((f = open_route(route, &ta, FPF_DISKFONT)) && abs(f->tf_YSize - pixels) > pixels / 8 + 1) {
            CloseFont(f);
            f = NULL;
        }
    for (route = 0; !f && route < 3; ++route)
        f = open_route(route, &ta, route == 2 ? 0 : FPF_DISKFONT | FPF_DESIGNED);
    if (!f) {
        snprintf(font_misses[font_miss_next].file, sizeof font_misses[0].file, "%s", file);
        font_misses[font_miss_next].pixels = pixels;
        font_misses[font_miss_next].style = style;
        font_miss_next = (font_miss_next + 1) % 16;
        return NULL;
    }
    if (slot < 0) {
        slot = font_next;
        font_next = (font_next + 1) % FONT_CACHE;
        CloseFont(fonts[slot].font);
    }
    snprintf(fonts[slot].file, sizeof fonts[slot].file, "%s", file);
    fonts[slot].pixels = pixels;
    fonts[slot].style = style;
    fonts[slot].font = f;
    return f;
}

/* The font for a run at a pixel size, asked bold or italic as the run is (style: FSF_ bits). */
static struct TextFont *slide_font(const ol_style *st, int pixels, int style)
{
    int index = OW_LAYOUT_FONT_INDEX(OL_TEXT_FONT_OF(st->text_flags));
    owf_font_kind kind = (st->text_flags & OL_TEXT_MONO) ? OWF_FONT_MONO : opr_font_kind(deck, index);
    const char *file = kind == OWF_FONT_MONO ? "courier.font" : kind == OWF_FONT_SERIF ? "CGTimes.font"
                     : "CGTriumvirate.font";
    struct TextFont *f = font_px(file, pixels, style);
    if (!f && kind != OWF_FONT_SANS) f = font_px("CGTriumvirate.font", pixels, style);
    if (!f) f = font_px("helvetica.font", pixels, style);
    return f ? f : font;
}

/* ---- Pictures, through datatypes (as GFOpenWrite's image cache) ---- */

#define IMAGE_SLOTS 12
#define IMAGE_MAX_PIXELS (1024UL * 1024UL)
#define IMAGE_BYTES (8UL * 1024UL * 1024UL)
typedef struct { int index; UBYTE *argb; UWORD w, h; ULONG used; } picture;
static picture pictures[IMAGE_SLOTS];
static int npictures;
static ULONG picture_tick;

static void pictures_clear(void)
{
    int i;
    for (i = 0; i < npictures; ++i) free(pictures[i].argb);
    memset(pictures, 0, sizeof pictures);
    npictures = 0;
}

static void pictures_room(ULONG need)
{
    for (;;) {
        ULONG total = 0;
        int i, old = -1;
        for (i = 0; i < npictures; ++i) {
            total += (ULONG)pictures[i].w * pictures[i].h * 4;
            if (old < 0 || pictures[i].used < pictures[old].used) old = i;
        }
        if (old < 0 || (npictures < IMAGE_SLOTS && total + need <= IMAGE_BYTES)) return;
        free(pictures[old].argb);
        pictures[old] = pictures[--npictures];
        memset(&pictures[npictures], 0, sizeof pictures[npictures]);
    }
}

static ULONG isqrt_ul(ULONG v)
{
    ULONG r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* The picture's pixels at tw x th from the datatype's sw x sh, each the nearest source pixel, a band of rows
 * at a time. */
static int read_scaled(Object *o, UBYTE *out, int sw, int sh, int tw, int th)
{
    int rows = 65536 / (sw * 4) > 0 ? 65536 / (sw * 4) : 1, y0, ty, ok = 1;
    UBYTE *band = (UBYTE *)malloc((size_t)sw * 4 * (rows < sh ? rows : sh));
    if (!band) return 0;
    for (y0 = 0, ty = 0; ok && y0 < sh && ty < th; y0 += rows) {
        int n = sh - y0 < rows ? sh - y0 : rows;
        if (!DoMethod(o, PDTM_READPIXELARRAY, (ULONG)band, PBPAFMT_ARGB, (ULONG)sw * 4, 0, (ULONG)y0, (ULONG)sw,
                      (ULONG)n)) {
            ok = 0;
            break;
        }
        for (; ty < th; ++ty) {
            int sy = (int)((ULONG)ty * sh / th), k;
            const ULONG *src;
            ULONG *dst = (ULONG *)(out + (size_t)ty * tw * 4);
            if (sy >= y0 + n) break;
            src = (const ULONG *)(band + (size_t)(sy - y0) * sw * 4);
            for (k = 0; k < tw; ++k) dst[k] = src[(ULONG)k * sw / tw];
        }
    }
    free(band);
    return ok;
}

static picture *picture_get(int index)
{
    int i, sw = 0, sh = 0, tw = 0, th = 0, ok;
    char tmp[96];
    BPTR f;
    Object *o = NULL;
    struct BitMapHeader *bmh = NULL;
    picture *c;
    const owf_image *im;
    if (!deck || index < 0 || index >= deck->res->nimages || !DataTypesBase) return NULL;
    for (i = 0; i < npictures; ++i)
        if (pictures[i].index == index) {
            pictures[i].used = ++picture_tick;
            return pictures[i].argb ? &pictures[i] : NULL;
        }
    im = &deck->res->images[index];
    snprintf(tmp, sizeof tmp, "T:GFOpenPresentation-Picture-%ld-%d.tmp", (long)FindTask(NULL), index);
    f = im->data && im->length ? Open((STRPTR)tmp, MODE_NEWFILE) : 0;
    if (f) {
        ok = Write(f, (APTR)im->data, (LONG)im->length) == (LONG)im->length;
        Close(f);
        if (ok) o = NewDTObject((APTR)tmp, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE,
                                TAG_DONE);
    }
    if (o) {
        GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
        DoMethod(o, DTM_PROCLAYOUT, NULL, 1);
        sw = bmh ? bmh->bmh_Width : 0;
        sh = bmh ? bmh->bmh_Height : 0;
        tw = sw;
        th = sh;
        if (sw > 0 && sh > 0 && (ULONG)sw * sh > IMAGE_MAX_PIXELS) {   /* the same shape, a megapixel */
            ULONG root = isqrt_ul((ULONG)sw * sh);
            tw = (int)((ULONG)sw * 1024UL / root);
            th = (int)((ULONG)sh * 1024UL / root);
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
        }
    }
    /* a slot, and room for the pixels, before anything is read: one that can't be read is remembered too */
    pictures_room(sw > 0 && sh > 0 ? (ULONG)tw * th * 4 : 0);
    c = &pictures[npictures++];
    memset(c, 0, sizeof *c);
    c->index = index;
    c->used = ++picture_tick;
    if (o && sw > 0 && sh > 0 && (c->argb = (UBYTE *)malloc((ULONG)tw * th * 4)) != NULL) {
        ok = tw == sw && th == sh
             ? DoMethod(o, PDTM_READPIXELARRAY, (ULONG)c->argb, PBPAFMT_ARGB, (ULONG)sw * 4, 0, 0, (ULONG)sw,
                        (ULONG)sh) != 0
             : read_scaled(o, c->argb, sw, sh, tw, th);
        if (ok) {
            c->w = (UWORD)tw;
            c->h = (UWORD)th;
        } else {
            free(c->argb);
            c->argb = NULL;
        }
    }
    if (o) DisposeDTObject(o);
    if (f) DeleteFile((STRPTR)tmp);
    if (!c->argb) return NULL;
    {   /* see-through pixels onto white: WritePixelArray doesn't blend */
        ULONG k, n = (ULONG)c->w * c->h;
        int any = 0, partial = 0;
        for (k = 0; k < n; ++k) { UBYTE a = c->argb[k * 4]; if (a) any = 1; if (a != 255) partial = 1; }
        if (any && partial)
            for (k = 0; k < n; ++k) {
                UBYTE *px = c->argb + k * 4;
                unsigned a = px[0];
                px[1] = (UBYTE)((px[1] * a + 255 * (255 - a)) / 255);
                px[2] = (UBYTE)((px[2] * a + 255 * (255 - a)) / 255);
                px[3] = (UBYTE)((px[3] * a + 255 * (255 - a)) / 255);
                px[0] = 255;
            }
    }
    return c;
}

/* A picture into the box, only the part inside lim, a band of rows at a time. */
static void draw_picture(struct RastPort *rp, const picture *c, int bx, int by, int bw, int bh, const ol_pixel_rect *lim)
{
    int x0 = bx > lim->x ? bx : lim->x, y0 = by > lim->y ? by : lim->y;
    int x1 = bx + bw < lim->x + lim->width ? bx + bw : lim->x + lim->width;
    int y1 = by + bh < lim->y + lim->height ? by + bh : lim->y + lim->height;
    int w = x1 - x0, rows, y, k;
    ULONG *band;
    UWORD *xs;
    if (w <= 0 || y1 <= y0 || bw <= 0 || bh <= 0 || x0 < 0 || y0 < 0) return;
    rows = 16384 / w > 0 ? 16384 / w : 1;
    if (rows > y1 - y0) rows = y1 - y0;
    band = (ULONG *)malloc((size_t)w * rows * 4);
    xs = (UWORD *)malloc((size_t)w * sizeof(UWORD));
    if (band && xs) {
        for (k = 0; k < w; ++k) xs[k] = (UWORD)((ULONG)(x0 + k - bx) * c->w / (ULONG)bw);
        for (y = y0; y < y1; y += rows) {
            int n = y1 - y < rows ? y1 - y : rows, r;
            for (r = 0; r < n; ++r) {
                const ULONG *src = (const ULONG *)c->argb + (ULONG)(y + r - by) * c->h / (ULONG)bh * c->w;
                ULONG *dst = band + (size_t)r * w;
                for (k = 0; k < w; ++k) dst[k] = src[xs[k]];
            }
            WritePixelArray(band, 0, 0, (UWORD)(w * 4), rp, (UWORD)x0, (UWORD)y, (UWORD)w, (UWORD)n, RECTFMT_ARGB);
        }
    }
    free(xs);
    free(band);
}

/* ---- Slides laid out ----
 * A few slides kept laid out (the one shown and the thumbnails in view), each by its slide's change count,
 * so a change lays out that slide again and no other. */

#define TW(v) ((ol_unit)(v) * OL_UNITS_PER_TWIP)
#define LAID_MAX 8
static struct { int slide; unsigned long version; opr_slide *s; ULONG used; } laid[LAID_MAX];
static unsigned long *versions;                      /* a count of each slide's changes */
static int nversions;
static ULONG laid_tick;

static void laid_clear(void)
{
    int i;
    for (i = 0; i < LAID_MAX; ++i) {
        opr_slide_free(laid[i].s);
        memset(&laid[i], 0, sizeof laid[i]);
    }
}

static unsigned long slide_version(int slide)
{
    if (deck && deck->nslides > nversions) {
        unsigned long *v = realloc(versions, (size_t)deck->nslides * sizeof *v);
        if (v) {
            memset(v + nversions, 0, (size_t)(deck->nslides - nversions) * sizeof *v);
            versions = v;
            nversions = deck->nslides;
        }
    }
    return slide >= 0 && slide < nversions ? versions[slide] : 0;
}

static void slide_changed(int slide)
{
    slide_version(slide);
    if (slide >= 0 && slide < nversions) versions[slide]++;
}

static opr_slide *laid_get(int slide)
{
    int i, slot = 0;
    unsigned long v = slide_version(slide);
    if (!deck || slide < 0 || slide >= deck->nslides) return NULL;
    for (i = 0; i < LAID_MAX; ++i)
        if (laid[i].s && laid[i].slide == slide && laid[i].version == v) {
            laid[i].used = ++laid_tick;
            return laid[i].s;
        }
    for (i = 0; i < LAID_MAX; ++i) {
        if (!laid[i].s) { slot = i; break; }
        if (laid[i].used < laid[slot].used) slot = i;
    }
    opr_slide_free(laid[slot].s);
    laid[slot].s = opr_slide_layout(deck, slide, NULL, NULL, NULL);
    laid[slot].slide = slide;
    laid[slot].version = v;
    laid[slot].used = ++laid_tick;
    return laid[slot].s;
}

/* ---- Drawing a slide from its display list ---- */

typedef struct {
    struct RastPort *rp;
    const opr_slide *s;
    ol_scale scale;
    ol_pixel_rect lim;           /* the slide, and of it what is being drawn */
    int thumb;                   /* a thumbnail: text as lines */
} painter;

static UWORD shape_pattern(unsigned style)
{
    switch (style) {
    case OL_STROKE_DASHED: return 0xF0F0u;
    case OL_STROKE_DOTTED: return 0xCCCCu;
    case OL_STROKE_DASH_DOT: return 0xFF18u;
    default: return 0xFFFFu;
    }
}

static void outline(struct RastPort *rp, LONG pen, const int *xy, int n, int width, int closed, unsigned style)
{
    int i, k;
    UWORD pattern = shape_pattern(style);
    if (n < 2) return;
    pen = exact_pen(pen);
    ogt_set_apen(rp, pen);
    SetDrPt(rp, pattern);
    Move(rp, xy[0], xy[1]);
    for (i = 1; i < n; i++) Draw(rp, xy[i * 2], xy[i * 2 + 1]);
    if (closed) Draw(rp, xy[0], xy[1]);
    for (k = 1; k < width && k < 4; ++k) {   /* a thicker line: drawn again, a pixel along */
        int moved[2 * OL_MAX_SHAPE_POINTS];
        for (i = 0; i < n && i < OL_MAX_SHAPE_POINTS; ++i) {
            moved[i * 2] = xy[i * 2] + (k & 1);
            moved[i * 2 + 1] = xy[i * 2 + 1] + (k >> 1 ? 1 : 0) + (k == 2);
        }
        Move(rp, moved[0], moved[1]);
        for (i = 1; i < n; i++) Draw(rp, moved[i * 2], moved[i * 2 + 1]);
        if (closed) Draw(rp, moved[0], moved[1]);
    }
    SetDrPt(rp, 0xFFFFu);
}

static void arrowhead(struct RastPort *rp, LONG pen, int x1, int y1, int x2, int y2, int size)
{
    long dx = x2 - x1, dy = y2 - y1, len;
    int pts[6];
    len = (long)isqrt_ul((ULONG)(dx * dx + dy * dy));
    if (len <= 0) return;
    pts[0] = x2;
    pts[1] = y2;
    pts[2] = (int)(x2 - dx * size / len - dy * size / (2 * len));
    pts[3] = (int)(y2 - dy * size / len + dx * size / (2 * len));
    pts[4] = (int)(x2 - dx * size / len + dy * size / (2 * len));
    pts[5] = (int)(y2 - dy * size / len - dx * size / (2 * len));
    ogt_fill_poly(rp, pen, pts, 3);
}

/* The same normalized OpenLayout geometry that the PDF renderer consumes. */
static void draw_shape(const painter *p, const ol_device_op *d)
{
    const ol_graphic *g = d->op->graphic;
    const ol_shape_style *s;
    int x = d->box.x, y = d->box.y, w = d->box.width, h = d->box.height;
    ol_rect bounds;
    ol_point path[OL_MAX_SHAPE_POINTS];
    int xy[2 * OL_MAX_SHAPE_POINTS], k, n, closed = 0;
    int width = d->thickness > 0 ? d->thickness : 1;
    LONG fill, line;
    if (!g || g->shape.type == OL_SHAPE_NONE) return;
    s = &g->shape;
    fill = (s->fill_argb >> 24) ? argb_pen(s->fill_argb) : -1;
    line = (s->stroke_argb >> 24) && s->stroke_width > 0 ? argb_pen(s->stroke_argb) : -1;
    if (w < 1 && h < 1) return;
    bounds.x = x;
    bounds.y = y;
    /* RastPort's last pixel is inclusive; preserve the old screen bounds. */
    bounds.width = w > 0 ? w - 1 : 0;
    bounds.height = h > 0 ? h - 1 : 0;
    n = ol_shape_path(g, &bounds, path, OL_MAX_SHAPE_POINTS, &closed);
    if (n <= 0) return;
    for (k = 0; k < n; ++k) {
        xy[k * 2] = path[k].x;
        xy[k * 2 + 1] = path[k].y;
    }
    if (fill >= 0 && closed && n >= 3) ogt_fill_poly(p->rp, fill, xy, n);
    if (line >= 0) outline(p->rp, line, xy, n, width, closed, s->stroke_style);
    if (line >= 0 && n >= 2 && (s->flags & OL_SHAPE_ARROW_END))
        arrowhead(p->rp, line, xy[(n - 2) * 2], xy[(n - 2) * 2 + 1], xy[(n - 1) * 2], xy[(n - 1) * 2 + 1],
                  width * 3 > 6 ? width * 3 : 6);
    if (line >= 0 && n >= 2 && (s->flags & OL_SHAPE_ARROW_START))
        arrowhead(p->rp, line, xy[2], xy[3], xy[0], xy[1], width * 3 > 6 ? width * 3 : 6);
}

/* The list bullets drawn as shapes: 1 a dot, 2 a square, 3 a ring, 4 a big
   disc (U+25CF, LibreOffice's, drawn small by its relative size); 0 not one. */
static int bullet_shape(unsigned long cp)
{
    switch (cp) {
    case 0x2022: case 0x2219: case 0xF0B7: return 1;
    case 0x25AA: case 0x25A0: case 0x25FC: case 0x25FE: case 0xF0A7: case 0xF06E: return 2;
    case 0x25CB: case 0x25E6: case 0xF06F: return 3;
    case 0x25CF: case 0xF06C: return 4;
    default: return 0;
    }
}

static void draw_bullet(struct RastPort *rp, LONG pen, int shape, int x, int baseline, int font_pixels)
{
    int r = font_pixels * (shape == 4 ? 30 : 22) / 100;
    int cy = baseline - font_pixels * 33 / 100, dy;
    if (r < 2) r = font_pixels >= 6 ? 2 : 1;
    if (shape == 2) {
        ogt_box(rp, pen, x, cy - r, 2 * r + 1, 2 * r + 1);
        return;
    }
    for (dy = -r; dy <= r; ++dy) {
        int hw = r;
        while (hw > 0 && hw * hw + dy * dy > r * r + r / 2) --hw;
        if (shape == 3 && dy > -r && dy < r) {
            int in = r - 1, iw = in;
            while (iw > 0 && iw * iw + dy * dy > in * in) --iw;
            if (abs(dy) < in) {
                ogt_box(rp, pen, x + r - hw, cy + dy, hw - iw > 0 ? hw - iw : 1, 1);
                ogt_box(rp, pen, x + r + iw + 1, cy + dy, hw - iw > 0 ? hw - iw : 1, 1);
                continue;
            }
        }
        ogt_box(rp, pen, x + r - hw, cy + dy, 2 * hw + 1, 1);
    }
}

/* A text run's words, measured before they are drawn: where the layout put each (x0 and x1, layout units from
 * the run's start) and its bytes in the font's ISO-8859-1 (latin1_of). */
#define RUN_WORDS 48
typedef struct { int x0, x1, shape; unsigned short n; char text[96]; } run_word;
static run_word run_words[RUN_WORDS];

/* Text smaller than this many pixels is drawn as a line where its words are (View > Zoom In to read it). */
#define GREEK_BELOW 5

/* A space between words (no-break spaces keep words together). */
static int is_gap(unsigned long cp)
{
    return cp == ' ' || cp == '\t' || (cp >= 0x2002 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

/* Text's width as Text() draws it: graphics' bold moves each letter on by the font's smear, which TextLength
 * can leave out (as GFOpenWrite's text_length has it). */
static int text_w(struct RastPort *rp, const char *s, int n)
{
    int w = TextLength(rp, (STRPTR)s, (UWORD)n);
    if (rp->AlgoStyle & FSF_BOLD) {
        int smear = rp->Font && rp->Font->tf_BoldSmear ? rp->Font->tf_BoldSmear : 1;
        int plain;
        SetSoftStyle(rp, rp->AlgoStyle & ~FSF_BOLD, FSF_BOLD);
        plain = TextLength(rp, (STRPTR)s, (UWORD)n) + n * smear;
        SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
        if (plain > w) w = plain;
    }
    return w;
}

/* The run's font at px pixels: bold and italic asked of the font (its engine draws them), what it can't
 * do added by graphics, but not graphics' bold below 12 pixels, where its smear closes the letters up. */
static void use_font(struct RastPort *rp, const ol_style *st, int px, int style)
{
    struct TextFont *f = slide_font(st, px, style);
    int soft = style & ~(f->tf_Style & (FSF_BOLD | FSF_ITALIC));
    if (px < 12) soft &= ~FSF_BOLD;
    SetFont(rp, f);
    SetSoftStyle(rp, soft, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

/* A word at x, as wide as the layout made it (fit pixels, no wider than room, the way to the next word):
 * each letter where the font puts it, the gaps between letters a little wider or narrower. The font's own
 * spacing is kept inside the word, and the spaces between words are the layout's. */
static void draw_word(struct RastPort *rp, int x, int baseline, const char *s, int n, int fit, int room)
{
    int w = text_w(rp, s, n), k;
    if (fit > room) fit = room;
    if (n < 2 || fit <= 0 || w <= 0 || (w - fit <= 1 && fit - w <= 1)) {
        Move(rp, x, baseline);
        Text(rp, (STRPTR)s, (WORD)n);
        return;
    }
    for (k = 0; k < n; ++k) {
        int at = k ? text_w(rp, s, k) : 0;
        Move(rp, x + (int)((long)at * fit / w), baseline);
        Text(rp, (STRPTR)s + k, 1);
    }
}

static void paint_text(painter *p, const ol_device_op *d)
{
    struct RastPort *rp = p->rp;
    const ol_text_op *t = &d->op->u.text;
    const ol_node *n = ol_node_by_id(p->s->doc, t->node_id);
    const char *s = n ? ol_node_text(n) : NULL;
    size_t len = t->text_length, at = 0;
    LONG pen;
    int style = FS_NORMAL, baseline = d->baseline, px0 = d->font_pixels;
    if (!s || !len) return;
    s += t->text_offset;
    pen = (t->style.foreground >> 24) ? argb_pen(t->style.foreground) : argb_pen(0xff000000u);
    if (p->thumb || px0 < GREEK_BELOW) {
        /* too small to read: a line where the words are; but a bullet at a
           small relative size (LibreOffice's 45%) is still drawn */
        size_t k = len;
        int hh = px0 / 3 > 0 ? px0 / 3 : 1;
        if (!p->thumb) {
            size_t b = 0;
            int shape = bullet_shape(utf8_next(s, len, &b));
            if (shape && (b >= len || s[b] == ' ' || s[b] == '\t')) {
                draw_bullet(rp, pen, shape, d->box.x, baseline, px0);
                return;
            }
        }
        while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) --k;
        if (k) ogt_box(rp, pen, d->box.x, baseline - px0 / 3 - hh / 2, (int)(d->box.width * (long)k / (long)len) + 1, hh);
        return;
    }
    if (t->style.text_flags & OL_TEXT_BOLD) style |= FSF_BOLD;
    if (t->style.text_flags & OL_TEXT_ITALIC) style |= FSF_ITALIC;
    if (t->style.text_flags & OL_TEXT_SUPER) baseline -= px0 / 2;
    else if (t->style.text_flags & OL_TEXT_SUB) baseline += px0 / 4;
    ogt_set_apen(rp, exact_pen(pen));
    SetDrMd(rp, JAM1);
    /* Each word starts where the layout put it. The layout measures with the print's (PDF) widths, and an
       Amiga font's letters at a size aren't those: wider at small sizes, narrower at large ones. So the font is
       taken at the size whose words are as wide as the layout's (60% to 140% of the run's), and a word still
       wider than its room is drawn closer, so the spaces between words stay. */
    while (at < len) {
        int nw = 0, k, px = px0, lo = px0 * 6 / 10 > GREEK_BELOW ? px0 * 6 / 10 : GREEK_BELOW, tries;
        long layout_w = 0, screen_w = 0, want;
        while (at < len && nw < RUN_WORDS) {
            run_word *w = &run_words[nw];
            size_t start = at;
            unsigned long cp = utf8_next(s, len, &at);
            unsigned char b[4];
            int nb, i;
            ol_text_metrics m;
            if (is_gap(cp) || cp < ' ' || cp == 0x7F) continue;
            w->n = 0;
            w->shape = bullet_shape(cp);
            if (w->shape && (at >= len || s[at] == ' ' || s[at] == '\t')) {
                w->x0 = w->x1 = 0;
            } else {
                w->shape = 0;
                for (;;) {
                    latin1_of(cp, b, &nb);
                    for (i = 0; i < nb && w->n < sizeof w->text; ++i) w->text[w->n++] = (char)b[i];
                    if (at >= len || w->n + 4 > sizeof w->text) break;
                    {
                        size_t here = at;
                        cp = utf8_next(s, len, &at);
                        if (is_gap(cp) || cp < ' ' || cp == 0x7F) { at = here; break; }
                    }
                }
            }
            m.width = 0;
            if (start) opr_measure((void *)deck, s, start, &t->style, &m);
            w->x0 = m.width;
            opr_measure((void *)deck, s, at, &t->style, &m);
            w->x1 = m.width;
            nw++;
        }
        use_font(rp, &t->style, px, style);
        for (k = 0; k < nw; ++k)
            if (!run_words[k].shape && run_words[k].n) {
                layout_w += run_words[k].x1 - run_words[k].x0;
                screen_w += text_w(rp, run_words[k].text, run_words[k].n);
            }
        want = ol_scale_length(&p->scale, (int)layout_w);
        if (screen_w > 0 && want > 0) {
            long target = (long)px0 * want / screen_w;
            if (target < lo) target = lo;
            if (target > (long)px0 * 14 / 10) target = (long)px0 * 14 / 10;
            for (tries = 0; tries < 4 && target != px; ++tries) {
                long was = screen_w;
                px = (int)target;
                use_font(rp, &t->style, px, style);
                for (k = 0, screen_w = 0; k < nw; ++k)
                    if (!run_words[k].shape && run_words[k].n) screen_w += text_w(rp, run_words[k].text, run_words[k].n);
                /* fonts don't grow evenly: a step down while still more than 3% too wide */
                if (screen_w * 100 > want * 103 && px > lo) target = px - 1;
                else if (screen_w == was) break;
            }
        }
        for (k = 0; k < nw; ++k) {
            const run_word *w = &run_words[k];
            int x = ol_scale_x(&p->scale, t->bounds.x + w->x0), room, gap = px / 5 > 1 ? px / 5 : 1;
            if (w->shape) {
                /* a bullet on its own: drawn, as the fonts' middle dot is too small to see */
                draw_bullet(rp, pen, w->shape, x, baseline, px0);
                ogt_set_apen(rp, exact_pen(pen));
                continue;
            }
            if (!w->n) continue;
            room = k + 1 < nw ? ol_scale_x(&p->scale, t->bounds.x + run_words[k + 1].x0) - x - gap
                              : ol_scale_x(&p->scale, t->bounds.x + w->x1) - x + gap;
            draw_word(rp, x, baseline, w->text, w->n, ol_scale_x(&p->scale, t->bounds.x + w->x1) - x, room);
        }
    }
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (t->style.text_flags & OL_TEXT_UNDERLINE) ogt_hline(rp, pen, d->box.x, baseline + 1, d->box.width);
    if (t->style.text_flags & OL_TEXT_STRIKE) ogt_hline(rp, pen, d->box.x, baseline - px0 / 3, d->box.width);
}

static void paint_op(void *userdata, const ol_device_op *d)
{
    painter *p = (painter *)userdata;
    struct RastPort *rp = p->rp;
    switch (d->op->type) {
    case OL_OP_TEXT:
        paint_text(p, d);
        break;
    case OL_OP_FILL_RECT:
        if (d->op->u.fill_rect.argb >> 24)
            ogt_box(rp, argb_pen(d->op->u.fill_rect.argb), d->box.x, d->box.y, d->box.width, d->box.height);
        break;
    case OL_OP_RULE: {
        LONG pen = argb_pen(d->op->u.rule.argb);
        int x1 = d->x1 < d->x2 ? d->x1 : d->x2, x2 = d->x1 < d->x2 ? d->x2 : d->x1;
        int y1 = d->y1 < d->y2 ? d->y1 : d->y2, y2 = d->y1 < d->y2 ? d->y2 : d->y1;
        if (y1 == y2) ogt_box(rp, pen, x1, y1 - (d->thickness - 1) / 2, x2 - x1 + 1, d->thickness);
        else if (x1 == x2) ogt_box(rp, pen, x1 - (d->thickness - 1) / 2, y1, d->thickness, y2 - y1 + 1);
        else { ogt_set_apen(rp, exact_pen(pen)); Move(rp, d->x1, d->y1); Draw(rp, d->x2, d->y2); }
        break;
    }
    case OL_OP_IMAGE: {
        int index = d->op->u.image.image_id;
        const ol_image_crop *crop = d->op->graphic ? &d->op->graphic->crop : NULL;
        picture *c = p->thumb ? NULL : picture_get(index);
        if (d->box.width < 1 || d->box.height < 1) break;
        if (c && CyberGfxBase && screen_depth() > 8) {
            ol_pixel_rect lim = p->lim;
            int bx = d->box.x, by = d->box.y, bw = d->box.width, bh = d->box.height;
            /* the box's part of the slide only, and a cropped picture larger behind it */
            if (lim.x < d->box.x) { lim.width -= d->box.x - lim.x; lim.x = d->box.x; }
            if (lim.y < d->box.y) { lim.height -= d->box.y - lim.y; lim.y = d->box.y; }
            if (lim.x + lim.width > d->box.x + d->box.width) lim.width = d->box.x + d->box.width - lim.x;
            if (lim.y + lim.height > d->box.y + d->box.height) lim.height = d->box.y + d->box.height - lim.y;
            if (crop && (crop->left || crop->top || crop->right || crop->bottom)) {
                bw = (int)((long)d->box.width * OL_SHAPE_COORDS /
                           (OL_SHAPE_COORDS - crop->left - crop->right));
                bh = (int)((long)d->box.height * OL_SHAPE_COORDS /
                           (OL_SHAPE_COORDS - crop->top - crop->bottom));
                bx = d->box.x - (int)((long)bw * crop->left / OL_SHAPE_COORDS);
                by = d->box.y - (int)((long)bh * crop->top / OL_SHAPE_COORDS);
            }
            draw_picture(rp, c, bx, by, bw, bh, &lim);
        } else {
            ogt_frame(rp, ogt_pen(&ctx, "group.line"), d->box.x, d->box.y, d->box.width, d->box.height);
            if (!p->thumb && d->box.width > 40 && d->box.height > fh + 8) {
                SetFont(rp, font);
                ogt_text(rp, ogt_pen(&ctx, "muted"), d->box.x + 6, d->box.y + 6, "Picture", d->box.width - 12);
            }
        }
        break;
    }
    case OL_OP_SHAPE:
        draw_shape(p, d);
        break;
    }
}

/* The slide at x, y, w wide (its height from the deck's shape), what crosses clip (NULL: all of it). */
static void paint_slide(struct RastPort *rp, const opr_slide *s, int x, int y, int w, const ol_pixel_rect *clip,
                        int thumb, ol_scale *out)
{
    painter p;
    int h = (int)((long)w * deck->height / deck->width);
    memset(&p, 0, sizeof p);
    p.rp = rp;
    p.s = s;
    p.thumb = thumb;
    ol_scale_fit(&p.scale, TW(deck->width), w);
    p.scale.origin_x = x;
    p.scale.origin_y = y;
    p.lim.x = x;
    p.lim.y = y;
    p.lim.width = w;
    p.lim.height = h;
    if (clip) {
        int x1 = x + w < clip->x + clip->width ? x + w : clip->x + clip->width;
        int y1 = y + h < clip->y + clip->height ? y + h : clip->y + clip->height;
        if (p.lim.x < clip->x) p.lim.x = clip->x;
        if (p.lim.y < clip->y) p.lim.y = clip->y;
        p.lim.width = x1 - p.lim.x;
        p.lim.height = y1 - p.lim.y;
    }
    if (out) *out = p.scale;
    if (!s) {
        ogt_box(rp, argb_pen(0xffffffffu), x, y, w, h);
        return;
    }
    ol_draw_page(s->doc, 0, &p.scale, &p.lim, paint_op, &p);
    SetFont(rp, font);
}

/* ---- The window ---- */

static void update_window_title(void)
{
    static char title[sizeof current_path + 32];     /* static: Intuition keeps the pointer */
    const char *name = current_path[0] ? leaf(current_path) : "Untitled";
    snprintf(title, sizeof title, "%s%s - " PROGRAM, dirty ? "*" : "", name);
    if (win) SetWindowTitles(win, (STRPTR)title, (STRPTR)-1);
}

static void update_edit_tools(void)
{
    ogt_toolbar_enable(&tb, C_UNDO, opr_undo_can(undo));
    ogt_toolbar_enable(&tb, C_REDO, opr_redo_can(undo));
}

/* Where the slide goes in the view: as large as fits, its shape kept, centred; zoomed, that size times the
 * zoom, scrolled by the pan when larger than the view. */
static void place_slide(void)
{
    int mw, mh, w, h;
    slide_area = view_box;
    if (zoom > 100) {
        slide_area.w -= SCROLLER;
        slide_area.h -= SCROLLER;
    }
    mw = view_box.w - 24;
    mh = view_box.h - 24;
    if (!deck || mw < 16 || mh < 16 || slide_area.w < 16 || slide_area.h < 16) {
        slide_box = (box){ view_box.x, view_box.y, 0, 0 };
        return;
    }
    w = mw;
    h = (int)((long)w * deck->height / deck->width);
    if (h > mh) {
        h = mh;
        w = (int)((long)h * deck->width / deck->height);
    }
    w = (int)((long)w * zoom / 100);
    h = (int)((long)h * zoom / 100);
    if (w + 24 <= slide_area.w) {
        pan_x = 0;
        slide_box.x = slide_area.x + (slide_area.w - w) / 2;
    } else {
        int most = w + 24 - slide_area.w;
        if (pan_x > most) pan_x = most;
        if (pan_x < 0) pan_x = 0;
        slide_box.x = slide_area.x + 12 - pan_x;
    }
    if (h + 24 <= slide_area.h) {
        pan_y = 0;
        slide_box.y = slide_area.y + (slide_area.h - h) / 2;
    } else {
        int most = h + 24 - slide_area.h;
        if (pan_y > most) pan_y = most;
        if (pan_y < 0) pan_y = 0;
        slide_box.y = slide_area.y + 12 - pan_y;
    }
    slide_box.w = w;
    slide_box.h = h;
}

/* The ruler is the existing OpenGadTools control. In text-editing mode its
 * scale covers the selected frame's inner width, so its values are exactly
 * owf_parafmt indents rather than page-relative approximations. */
static void update_ruler(void)
{
    int page_x = slide_box.x, page_w = deck ? deck->width : 11906;
    int left = 0, first = 0, right = 0;
    int num = slide_box.w > 0 ? slide_box.w : 1;
    int den = deck && deck->width > 0 ? deck->width : 1;
    if (deck && current >= 0 && current < deck->nslides && edit_obj >= 0 &&
        edit_obj < deck->slides[current].nobjs && slide_box.w > 0) {
        owf_obj resolved;
        owf_obj *o = &deck->slides[current].objs[edit_obj];
        int para = caret.para;
        if (owf_deck_resolve(deck, current, edit_obj, &resolved, NULL) >= 0 &&
            opr_obj_editable(deck, current, edit_obj) && o->text.nparas > 0) {
            int content_x = resolved.x + resolved.inset[0];
            int content_w = resolved.width - resolved.inset[0] - resolved.inset[2];
            if (para < 0) para = 0;
            if (para >= o->text.nparas) para = o->text.nparas - 1;
            if (content_w > 0) {
                page_x = slide_box.x + (int)((long)content_x * num / den);
                page_w = content_w;
            }
            left = o->text.paras[para].fmt.indent_left;
            first = o->text.paras[para].fmt.indent_first;
            right = o->text.paras[para].fmt.indent_right;
        }
    }
    ogt_ruler_layout(&ruler, ruler_box.x, ruler_box.y, ruler_box.w, ruler_box.h, page_x);
    ogt_ruler_set_page(&ruler, page_w, 0, 0, num, den, OGT_RULER_CM);
    ogt_ruler_set_indents(&ruler, left, first, right);
}

static void free_scrollers(void)
{
    if (hscroll) { RemoveGadget(win, hscroll); DisposeObject(hscroll); hscroll = NULL; }
    if (vscroll) { RemoveGadget(win, vscroll); DisposeObject(vscroll); vscroll = NULL; }
}

/* The scrollers of a zoomed slide (propgclass, telling the window as it moves). */
static void make_scrollers(void)
{
    free_scrollers();
    if (zoom <= 100 || slide_box.w <= 0) return;
    hscroll = (struct Gadget *)NewObject(NULL, (UBYTE *)"propgclass",
        GA_Left, slide_area.x, GA_Top, slide_area.y + slide_area.h + 1, GA_Width, slide_area.w, GA_Height, SCROLLER - 2,
        GA_ID, GID_HSCROLL, GA_RelVerify, TRUE, GA_Immediate, TRUE, PGA_Freedom, FREEHORIZ, PGA_NewLook, TRUE,
        PGA_Borderless, TRUE, PGA_Total, (ULONG)(slide_box.w + 24), PGA_Visible, (ULONG)slide_area.w,
        PGA_Top, (ULONG)pan_x, ICA_TARGET, ICTARGET_IDCMP, TAG_DONE);
    vscroll = (struct Gadget *)NewObject(NULL, (UBYTE *)"propgclass",
        GA_Left, slide_area.x + slide_area.w + 1, GA_Top, slide_area.y, GA_Width, SCROLLER - 2, GA_Height, slide_area.h,
        GA_ID, GID_VSCROLL, GA_RelVerify, TRUE, GA_Immediate, TRUE, PGA_Freedom, FREEVERT, PGA_NewLook, TRUE,
        PGA_Borderless, TRUE, PGA_Total, (ULONG)(slide_box.h + 24), PGA_Visible, (ULONG)slide_area.h,
        PGA_Top, (ULONG)pan_y, ICA_TARGET, ICTARGET_IDCMP, TAG_DONE);
    if (hscroll) AddGadget(win, hscroll, (UWORD)~0);
    if (vscroll) AddGadget(win, vscroll, (UWORD)~0);
}

static int thumb_height(void *user, int index)
{
    int w = strip_box.w - 32;
    (void)user; (void)index;
    if (!deck || w < 8) return fh + 8;
    return (int)((long)w * deck->height / deck->width) + fh + 14;
}

static void thumb_draw(void *user, int index, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    int tw = w - 20, th, tx = x + 10, ty = y + 4;
    char num[32];
    opr_slide *s;
    ol_pixel_rect clip;
    (void)user; (void)h;
    if (!deck || index >= deck->nslides || tw < 8) return;
    th = (int)((long)tw * deck->height / deck->width);
    ogt_box(rp, ogt_pen(&ctx, "track"), tx + 2, ty + 2, tw, th);
    s = laid_get(index);
    clip.x = tx;
    clip.y = ty;
    clip.width = tw;
    clip.height = th;
    paint_slide(rp, s, tx, ty, tw, &clip, 1, NULL);
    ogt_frame(rp, ogt_pen(&ctx, selected || index == current ? "accent" : "group.line"), tx, ty, tw, th);
    if (selected || index == current) ogt_frame(rp, ogt_pen(&ctx, "accent"), tx - 1, ty - 1, tw + 2, th + 2);
    snprintf(num, sizeof num, "%d%s", index + 1, deck->slides[index].hidden ? " (hidden)" : "");
    SetFont(rp, font);
    ogt_text(rp, ogt_pen(&ctx, "muted"), x + (w - ogt_text_width(rp, num)) / 2, ty + th + 3, num, 0);
}

static void free_gadgets(void)
{
    if (!win) return;
    free_scrollers();
    if (chain) { RemoveGList(win, chain, -1); chain = NULL; }
    if (strip) { ogt_list_free(strip); strip = NULL; }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    int tbh, stripw, body_y, body_h, view_x, view_w, ruler_h;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);
    ogt_toolbar_set(&tb, tools, sizeof tools / sizeof tools[0], area.w < 600 ? OGT_TB_ICONS : tb_style);
    update_edit_tools();
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 5, area.y + 3, area.w - 10);
    toolbar_box = (box){ area.x, area.y, area.w, tbh + 7 };
    status_box = (box){ area.x, area.y + area.h - fh - 10, area.w, fh + 10 };
    body_y = toolbar_box.y + toolbar_box.h;
    body_h = status_box.y - body_y;
    /* the slides beside the slide, hidden in a narrow window (as GFOpenWrite's navigator) */
    stripw = area.w >= 480 ? (area.w < 1000 ? 140 : area.w / 7) : 0;
    strip_box = (box){ area.x, body_y, stripw, body_h };
    view_x = area.x + stripw + (stripw ? 1 : 0);
    view_w = area.w - stripw - (stripw ? 1 : 0);
    ruler_h = fh + 12;
    if (ruler_h > body_h / 3) ruler_h = body_h / 3;
    ruler_box = (box){ view_x, body_y, view_w, ruler_h };
    view_box = (box){ view_x, body_y + ruler_h, view_w, body_h - ruler_h };
    place_slide();
    update_ruler();
    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);
    make_scrollers();
    if (stripw && (strip = ogt_list_new(win, &ctx, GID_SLIDES, thumb_height, thumb_draw, NULL, NULL, "list"))) {
        ogt_list_layout(strip, strip_box.x + 4, strip_box.y + 4, strip_box.w - 8, strip_box.h - 8);
        ogt_list_set_count(strip, deck ? deck->nslides : 0);
        ogt_list_select(strip, current);
    }
    ogt_status_init(&sbar, win, &ctx, 5);
    ogt_status_layout(&sbar, status_box.x, status_box.y, status_box.w, status_box.h);
}

/* A layout's name in words. The file's own name, but for names that are codes: LibreOffice's ODP layouts
 * ("AL1T0": the few with plain names get them, the rest nothing rather than a code), PresentationML's layout
 * types that some programs use as names ("titleOnly"), and Google Slides' ("TITLE_AND_BODY"). */
static const char *layout_words(const char *name)
{
    static const struct { const char *code, *words; } codes[] = {
        { "blank", "Blank" }, { "title", "Title Slide" }, { "obj", "Title and Content" },
        { "twoObj", "Two Content" }, { "titleOnly", "Title Only" }, { "secHead", "Section Header" },
        { "tx", "Title and Text" }, { "twoTxTwoObj", "Comparison" }, { "objTx", "Content with Caption" },
        { "picTx", "Picture with Caption" }, { "vertTx", "Title and Vertical Text" }, { "cust", "Custom" },
        { "TITLE", "Title Slide" }, { "CAPTION_ONLY", "Caption" }
    };
    static char words[64];
    const char *p = name;
    long k;
    size_t i;
    int lower = 0, under = 0;
    if (!p || !*p) return NULL;
    for (i = 0; i < sizeof codes / sizeof codes[0]; ++i)
        if (!strcmp(p, codes[i].code)) return codes[i].words;
    if (p[0] == 'A' && p[1] == 'L' && isdigit((unsigned char)p[2])) {
        for (p += 2; isdigit((unsigned char)*p); ++p) ;
        if (*p == 'T' && isdigit((unsigned char)p[1])) {
            k = strtol(p + 1, (char **)&p, 10);
            if (!*p) {
                switch (k) {
                case 0: return "Title Slide";
                case 1: return "Title, Content";
                case 3: return "Title and 2 Content";
                case 18: return "Title, 4 Content";
                case 19: return "Title Only";
                case 20: return "Blank";
                default: return NULL;
                }
            }
        }
        p = name;
    }
    /* SHOUTED_WITH_UNDERSCORES: "Title and body" */
    for (i = 0; p[i]; ++i) {
        if (islower((unsigned char)p[i])) lower = 1;
        if (p[i] == '_') under = 1;
    }
    if (!lower && (under || strlen(p) > 1) && strlen(p) < sizeof words) {
        for (i = 0; p[i]; ++i)
            words[i] = p[i] == '_' ? ' ' : (char)(i ? tolower((unsigned char)p[i]) : p[i]);
        words[i] = 0;
        return words;
    }
    return name;
}

static void draw_status(void)
{
    char s[96], detail[128] = "", slide_name[80] = "";
    const char *lname = NULL;
    if (!win) return;
    if (deck && deck->nslides) {
        int l = deck->slides[current].layout;
        snprintf(s, sizeof s, UNIT " %d of %d", current + 1, deck->nslides);
        if (deck->slides[current].name)
            name_to_latin1(deck->slides[current].name, slide_name, sizeof slide_name);
        if (l >= 0 && l < deck->nlayouts) lname = layout_words(deck->layouts[l].name);
    } else {
        snprintf(s, sizeof s, "No " UNITS_LOWER);
    }
    ogt_status_set(&sbar, 0, s);
    if (slide_name[0] && lname) snprintf(detail, sizeof detail, "%s - %s", slide_name, lname);
    else if (slide_name[0]) snprintf(detail, sizeof detail, "%s", slide_name);
    else if (lname) snprintf(detail, sizeof detail, "%s", lname);
    ogt_status_set(&sbar, 1, detail);
    ogt_status_set(&sbar, 2, status);
    {
        char z[16];
        if (zoom == 100) snprintf(z, sizeof z, "Fit");
        else snprintf(z, sizeof z, "%d%%", zoom);
        ogt_status_set(&sbar, 3, z);
    }
    ogt_status_set(&sbar, 4, current_format);
}

/* The caret and the frame being typed in, over the slide. */
static int object_pixel_box(int obj, box *b)
{
    owf_obj resolved;
    ol_scale sc;
    int x2, y2;
    if (!deck || current < 0 || current >= deck->nslides || obj < 0 ||
        obj >= deck->slides[current].nobjs || owf_deck_resolve(deck, current, obj, &resolved, NULL) < 0)
        return 0;
    ol_scale_fit(&sc, TW(deck->width), slide_box.w);
    sc.origin_x = slide_box.x;
    sc.origin_y = slide_box.y;
    b->x = ol_scale_x(&sc, TW(resolved.x));
    b->y = ol_scale_y(&sc, TW(resolved.y));
    x2 = ol_scale_x(&sc, TW(resolved.x + resolved.width));
    y2 = ol_scale_y(&sc, TW(resolved.y + resolved.height));
    b->w = x2 - b->x;
    b->h = y2 - b->y;
    return b->w > 0 && b->h > 0;
}

static void selection_points(const box *b, int x[8], int y[8])
{
    int cx = b->x + b->w / 2, cy = b->y + b->h / 2;
    x[0] = b->x; x[1] = cx; x[2] = b->x + b->w; x[3] = b->x + b->w;
    x[4] = b->x + b->w; x[5] = cx; x[6] = b->x; x[7] = b->x;
    y[0] = b->y; y[1] = b->y; y[2] = b->y; y[3] = cy;
    y[4] = b->y + b->h; y[5] = b->y + b->h; y[6] = b->y + b->h; y[7] = cy;
}

static void draw_selection(const opr_slide *s)
{
    box b;
    int x[8], y[8], i, frame;
    LONG accent;
    if (selected_obj < 0 || !object_pixel_box(selected_obj, &b)) return;
    accent = ogt_pen(&ctx, "accent");
    ogt_frame(win->RPort, accent, b.x - 2, b.y - 2, b.w + 4, b.h + 4);
    selection_points(&b, x, y);
    for (i = 0; i < 8; i++) {
        ogt_box(win->RPort, ogt_pen(&ctx, "window"), x[i] - 4, y[i] - 4, 9, 9);
        ogt_frame(win->RPort, accent, x[i] - 4, y[i] - 4, 9, 9);
    }
    frame = s ? opr_slide_frame_for(s, selected_obj) : -1;
    if (frame >= 0 && opr_frame_text_overflow((opr_slide *)s, frame, NULL, NULL) > 0) {
        int ox = b.x + b.w - 20, oy = b.y + b.h - 12;
        ogt_box(win->RPort, ogt_pen(&ctx, "window"), ox, oy, 18, 10);
        ogt_frame(win->RPort, accent, ox, oy, 18, 10);
        ogt_box(win->RPort, accent, ox + 4, oy + 5, 2, 2);
        ogt_box(win->RPort, accent, ox + 8, oy + 5, 2, 2);
        ogt_box(win->RPort, accent, ox + 12, oy + 5, 2, 2);
    }
}

static int app_compare_pos(opr_pos a, opr_pos b)
{
    if (a.para != b.para) return a.para < b.para ? -1 : 1;
    if (a.offset != b.offset) return a.offset < b.offset ? -1 : 1;
    return 0;
}

/* Complement each selected character cell after the slide is painted. The
 * caret mapping comes from the same OpenLayout plan as the glyphs, so wrapping,
 * columns and object exclusions need no second geometry implementation. */
static void draw_text_selection(const opr_slide *s, const ol_scale *sc)
{
    opr_pos at, end;
    int frame, guard = 0;
    struct RastPort *rp = win ? win->RPort : NULL;
    if (!rp || edit_obj < 0 || !s || opr_text_selection_empty(&text_selection) ||
        (frame = opr_slide_frame_for(s, edit_obj)) < 0)
        return;
    at = text_selection.anchor;
    end = text_selection.focus;
    if (app_compare_pos(at, end) > 0) {
        opr_pos swap = at;
        at = end;
        end = swap;
    }
    SetAPen(rp, (ULONG)ogt_pen(&ctx, "accent"));
    SetDrMd(rp, COMPLEMENT);
    while (app_compare_pos(at, end) < 0 && guard++ < 1000000) {
        opr_pos next = at;
        ol_rect a, b;
        int x, y, w, h;
        if (opr_text_move(deck, current, edit_obj, &next, OPR_MOVE_RIGHT) < 0 ||
            app_compare_pos(next, at) <= 0)
            break;
        if (app_compare_pos(next, end) > 0)
            next = end;
        if (!opr_caret_box(s, frame, at, NULL, NULL, &a) ||
            !opr_caret_box(s, frame, next, NULL, NULL, &b)) {
            at = next;
            continue;
        }
        x = ol_scale_x(sc, a.x);
        y = ol_scale_y(sc, a.y);
        h = ol_scale_length(sc, a.height);
        if (h < 2) h = 2;
        if (a.y == b.y) {
            int bx = ol_scale_x(sc, b.x);
            w = bx - x;
            if (w < 0) { x = bx; w = -w; }
        } else {
            w = h / 2;                 /* paragraph mark or a wrapped final glyph */
        }
        if (w < 1) w = 1;
        RectFill(rp, x, y, x + w - 1, y + h - 1);
        at = next;
    }
    SetDrMd(rp, JAM1);
}

static void draw_caret(const opr_slide *s, const ol_scale *sc)
{
    int frame;
    ol_rect b;
    if (edit_obj < 0 || !s || (frame = opr_slide_frame_for(s, edit_obj)) < 0) return;
    if (opr_caret_box(s, frame, caret, NULL, NULL, &b)) {
        int x = ol_scale_x(sc, b.x), y = ol_scale_y(sc, b.y), h = ol_scale_length(sc, b.height);
        ogt_box(win->RPort, ogt_pen(&ctx, "accent"), x, y, 2, h > 2 ? h : 2);
    }
}

static void draw_grid(const ol_scale *sc)
{
    struct RastPort *rp = win ? win->RPort : NULL;
    int x, y;
    if (!rp || !show_grid || !deck || GRID_TWIPS <= 0)
        return;
    SetAPen(rp, (ULONG)ogt_pen(&ctx, "group.line"));
    for (y = GRID_TWIPS; y < deck->height; y += GRID_TWIPS) {
        int sy = ol_scale_y(sc, (ol_unit)y * OL_UNITS_PER_TWIP);
        for (x = GRID_TWIPS; x < deck->width; x += GRID_TWIPS) {
            int sx = ol_scale_x(sc, (ol_unit)x * OL_UNITS_PER_TWIP);
            WritePixel(rp, sx, sy);
        }
    }
}

/* The slide, or only what of it is in the rectangle (the frame being typed in: no flicker elsewhere), drawn
 * through a clip region, so nothing a slide holds is drawn outside it. */
static void draw_slide_part(const box *part)
{
    struct RastPort *rp;
    struct Region *region = NULL, *old = NULL;
    struct Rectangle r;
    opr_slide *s;
    ol_scale sc;
    ol_pixel_rect clip;
    if (!win) return;
    rp = win->RPort;
    if (!part) {
        /* the window's own colour around the slide, so the slide stands out */
        ogt_fill(&ctx, rp, "window", view_box.x, view_box.y, view_box.w, view_box.h);
        if (!deck || !deck->nslides || slide_box.w <= 0) {
            ogt_text(rp, ogt_pen(&ctx, "muted"), view_box.x + 16, view_box.y + 16, "No " UNITS_LOWER ".", view_box.w - 32);
            return;
        }
        if (slide_box.x > slide_area.x && slide_box.y > slide_area.y &&
            slide_box.x + slide_box.w + 4 < slide_area.x + slide_area.w &&
            slide_box.y + slide_box.h + 4 < slide_area.y + slide_area.h) {
            ogt_box(rp, ogt_pen(&ctx, "track"), slide_box.x + 4, slide_box.y + 4, slide_box.w, slide_box.h);
            ogt_frame(rp, ogt_pen(&ctx, "group.line"), slide_box.x - 1, slide_box.y - 1, slide_box.w + 2, slide_box.h + 2);
        }
        if (hscroll || vscroll) {
            ogt_box(rp, ogt_pen(&ctx, "track"), slide_area.x + slide_area.w, slide_area.y + slide_area.h, SCROLLER, SCROLLER);
            if (hscroll) RefreshGList(hscroll, win, NULL, 1);
            if (vscroll) RefreshGList(vscroll, win, NULL, 1);
        }
    }
    if (!deck || !deck->nslides || slide_box.w <= 0) return;
    /* the slide, what of it is in the slide area */
    r.MinX = (WORD)(slide_box.x > slide_area.x ? slide_box.x : slide_area.x);
    r.MinY = (WORD)(slide_box.y > slide_area.y ? slide_box.y : slide_area.y);
    r.MaxX = (WORD)(slide_box.x + slide_box.w < slide_area.x + slide_area.w ? slide_box.x + slide_box.w - 1
                                                                            : slide_area.x + slide_area.w - 1);
    r.MaxY = (WORD)(slide_box.y + slide_box.h < slide_area.y + slide_area.h ? slide_box.y + slide_box.h - 1
                                                                            : slide_area.y + slide_area.h - 1);
    if (r.MaxX < r.MinX || r.MaxY < r.MinY) return;
    if (part) {
        if (part->x > r.MinX) r.MinX = (WORD)part->x;
        if (part->y > r.MinY) r.MinY = (WORD)part->y;
        if (part->x + part->w - 1 < r.MaxX) r.MaxX = (WORD)(part->x + part->w - 1);
        if (part->y + part->h - 1 < r.MaxY) r.MaxY = (WORD)(part->y + part->h - 1);
        if (r.MaxX < r.MinX || r.MaxY < r.MinY) return;
    }
    if (LayersBase && (region = NewRegion()) != NULL) {
        if (OrRectRegion(region, &r)) {
            old = InstallClipRegion(win->WLayer, region);
        } else {
            DisposeRegion(region);
            region = NULL;
        }
    }
    clip.x = r.MinX;
    clip.y = r.MinY;
    clip.width = r.MaxX - r.MinX + 1;
    clip.height = r.MaxY - r.MinY + 1;
    s = laid_get(current);
    paint_slide(rp, s, slide_box.x, slide_box.y, slide_box.w, &clip, 0, &sc);
    if (!s)
        ogt_text(rp, ogt_pen(&ctx, "muted"), slide_box.x + 8, slide_box.y + 8, "Not enough memory to show this " UNIT_LOWER ".",
                 slide_box.w - 16);
    draw_grid(&sc);
    draw_selection(s);
    draw_text_selection(s, &sc);
    draw_caret(s, &sc);
    if (region) {
        InstallClipRegion(win->WLayer, old);
        DisposeRegion(region);
    }
    update_ruler();
    ogt_ruler_draw(&ruler);
}

static void draw_slide(void)
{
    draw_slide_part(NULL);
}

static void draw_all(void)
{
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    SetFont(rp, font);
    ogt_fill(&ctx, rp, "window", area.x, area.y, area.w, area.h);
    ogt_toolbar_draw(&tb, &ctx, rp, "window");
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), toolbar_box.x, toolbar_box.y + toolbar_box.h - 1, toolbar_box.w);
    if (strip) {
        ogt_list_draw(strip);
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), strip_box.x + strip_box.w, strip_box.y, strip_box.h);
    }
    draw_slide();
    draw_status();
}

static void relayout(void)
{
    layout();
    draw_all();
}

/* ---- Slides and typing ---- */

static void stop_typing(void)
{
    edit_obj = -1;
    text_selecting = 0;
    edit_grouped = 0;
}

static void clear_object_selection(void)
{
    stop_typing();
    selected_obj = -1;
    memset(&object_drag, 0, sizeof object_drag);
}

static void show_slide(int index)
{
    if (!deck || !deck->nslides) return;
    if (index < 0) index = 0;
    if (index >= deck->nslides) index = deck->nslides - 1;
    if (index != current) clear_object_selection();
    current = index;
    if (strip) {
        ogt_list_select(strip, current);
        ogt_list_show(strip, current);
        ogt_list_draw(strip);
    }
    {
        char msg[64];
        snprintf(msg, sizeof msg, UNIT " %d.", current + 1);
        set_status(msg);
    }
    draw_slide();
    draw_status();
}

/* A structural edit changes slide indexes as well as the selected slide, so
 * every cached layout and version belongs to the old structure. */
static void structure_changed(int selected, const char *message)
{
    clear_object_selection();
    laid_clear();
    free(versions);
    versions = NULL;
    nversions = 0;
    if (!deck || !deck->nslides) current = 0;
    else if (selected < 0) current = 0;
    else if (selected >= deck->nslides) current = deck->nslides - 1;
    else current = selected;
    dirty = 1;
    update_window_title();
    set_status(message);
    relayout();
}

static void new_slide(void)
{
    int layout = -1, selected = current;
    if (!deck) return;
    if (deck->nslides && current >= 0 && current < deck->nslides)
        layout = deck->slides[current].layout;
    if (layout < 0 || layout >= deck->nlayouts)
        layout = deck->nlayouts > 2 ? 2 : (deck->nlayouts ? 0 : -1);
    if (opr_slide_insert(undo, deck, deck->nslides ? current + 1 : 0, layout, &selected) < 0) {
        tell(PROGRAM, "There is not enough memory for a new " UNIT_LOWER ".");
        return;
    }
    structure_changed(selected, "New " UNIT_LOWER " inserted.");
}

static void duplicate_current_slide(void)
{
    int selected = current;
    if (!deck || current < 0 || current >= deck->nslides) return;
    if (opr_slide_duplicate(undo, deck, current, current + 1, &selected) < 0) {
        tell(PROGRAM, "There is not enough memory to duplicate the " UNIT_LOWER ".");
        return;
    }
    structure_changed(selected, UNIT " duplicated.");
}

static void rename_current_slide(void)
{
    char initial[256], entered[256];
    char *utf8;
    int rc, selected = current;
    if (!deck || current < 0 || current >= deck->nslides) return;
    if (deck->slides[current].name)
        name_to_latin1(deck->slides[current].name, initial, sizeof initial);
    else
        snprintf(initial, sizeof initial, UNIT " %d", current + 1);
    if (!text_requester("Rename " UNIT, "Name", initial, entered, sizeof entered)) return;
    utf8 = name_from_latin1(entered);
    if (!utf8) {
        tell(PROGRAM, "There is not enough memory to rename the " UNIT_LOWER ".");
        return;
    }
    rc = opr_slide_rename(undo, deck, current, utf8, &selected);
    owf_free(utf8);
    if (rc == -2) {
        tell("Rename " UNIT, "A " UNIT_LOWER " name must be unique and between 1 and 255 UTF-8 bytes.");
        return;
    }
    if (rc < 0) {
        tell(PROGRAM, "The " UNIT_LOWER " could not be renamed.");
        return;
    }
    structure_changed(selected, UNIT " renamed.");
}

static void delete_slide(void)
{
    int selected = current;
    if (!deck || !deck->nslides) return;
    if (deck->nslides == 1) {
        tell(PROGRAM, "A " DOCUMENT_LOWER " must keep at least one " UNIT_LOWER ".");
        return;
    }
    if (opr_slide_delete(undo, deck, current, &selected) < 0) {
        tell(PROGRAM, "The " UNIT_LOWER " could not be deleted.");
        return;
    }
    structure_changed(selected, UNIT " deleted.");
}

static void move_slide(int direction)
{
    int selected = current, to = current + direction;
    if (!deck || to < 0 || to >= deck->nslides) {
        set_status(direction < 0 ? "This is already the first " UNIT_LOWER "." : "This is already the last " UNIT_LOWER ".");
        draw_status();
        return;
    }
    if (opr_slide_move(undo, deck, current, to, &selected) < 0) {
        tell(PROGRAM, "The " UNIT_LOWER " could not be moved.");
        return;
    }
    structure_changed(selected, direction < 0 ? UNIT " moved up." : UNIT " moved down.");
}

static void set_current_hidden(int hidden)
{
    int selected = current;
    if (!deck || current < 0 || current >= deck->nslides) return;
    hidden = hidden != 0;
    if (deck->slides[current].hidden == hidden) {
        set_status(hidden ? "This " UNIT_LOWER " is already hidden." : "This " UNIT_LOWER " is already shown.");
        draw_status();
        return;
    }
    if (opr_slide_set_hidden(undo, deck, current, hidden, &selected) < 0) {
        tell(PROGRAM, "The " UNIT_LOWER "'s show setting could not be changed.");
        return;
    }
    structure_changed(selected, hidden ? UNIT " hidden from the show." : UNIT " included in the show.");
}

static void edit_current_transition(void)
{
    owf_transition transition;
    int apply_all = 0, selected = current, rc;
    if (!deck || current < 0 || current >= deck->nslides) return;
    transition = deck->slides[current].transition;
    if (!transition_requester(&transition, &apply_all)) return;
    rc = opr_slide_set_transition(undo, deck, current, &transition, apply_all, &selected);
    if (rc == -2) {
        tell(TRANSITION_TITLE, "Duration must be 0 to 600000 ms and automatic advance 0 to 86399999 ms.");
        return;
    }
    if (rc < 0) {
        tell(PROGRAM, "There is not enough memory to change the " UNIT_LOWER " transition.");
        return;
    }
    structure_changed(selected, apply_all ? "Transition applied to all " UNITS_LOWER "." : UNIT " transition changed.");
}

/* ---- Full-screen show ----
 *
 * A borderless window on the presentation's public screen is the first
 * player backend. It uses the same laid-out slides and painter as the editor,
 * and opr_show owns all navigation/timing decisions. A later own-screen
 * backend can therefore change display setup without changing show rules. */
static void show_paint(struct RastPort *rp, int width, int height, const opr_show *show, int blank, int number)
{
    ol_pixel_rect clip;
    int w, h, x, y;
    char digits[24];
    LONG background = blank == 2 ? argb_pen(0xffffffffu) : argb_pen(0xff000000u);
    ogt_box(rp, background, 0, 0, width, height);
    if (!blank && show && show->deck) {
        w = width;
        h = (int)((long)w * show->deck->height / show->deck->width);
        if (h > height) {
            h = height;
            w = (int)((long)h * show->deck->width / show->deck->height);
        }
        x = (width - w) / 2;
        y = (height - h) / 2;
        clip.x = x;
        clip.y = y;
        clip.width = w;
        clip.height = h;
        paint_slide(rp, laid_get(show->slide), x, y, w, &clip, 0, NULL);
    }
    if (number > 0) {
        snprintf(digits, sizeof digits, "%d", number);
        SetFont(rp, font);
        ogt_text(rp, blank == 2 ? argb_pen(0xff000000u) : argb_pen(0xffffffffu),
                 width - ogt_text_width(rp, digits) - 16, 12, digits, 0);
    }
}

static void show_draw(struct Window *sw, const opr_show *show, int blank, int number)
{
    show_paint(sw->RPort, sw->Width, sw->Height, show, blank, number);
}

typedef struct {
    struct BitMap *old_bitmap, *new_bitmap, *mask_bitmap;
    struct RastPort old_rp, new_rp, mask_rp;
} show_buffers;

static int show_buffers_open(show_buffers *b, int width, int height)
{
    int depth = screen_depth();
    memset(b, 0, sizeof *b);
    b->old_bitmap = AllocBitMap((ULONG)width, (ULONG)height, (ULONG)depth, BMF_CLEAR | BMF_DISPLAYABLE,
                                scr->RastPort.BitMap);
    b->new_bitmap = AllocBitMap((ULONG)width, (ULONG)height, (ULONG)depth, BMF_CLEAR | BMF_DISPLAYABLE,
                                scr->RastPort.BitMap);
    if (!b->old_bitmap || !b->new_bitmap) {
        if (b->old_bitmap) FreeBitMap(b->old_bitmap);
        if (b->new_bitmap) FreeBitMap(b->new_bitmap);
        memset(b, 0, sizeof *b);
        return 0;
    }
    InitRastPort(&b->old_rp);
    InitRastPort(&b->new_rp);
    b->old_rp.BitMap = b->old_bitmap;
    b->new_rp.BitMap = b->new_bitmap;
    b->mask_bitmap = AllocBitMap((ULONG)width, (ULONG)height, 1, BMF_CLEAR, scr->RastPort.BitMap);
    if (b->mask_bitmap) {
        InitRastPort(&b->mask_rp);
        b->mask_rp.BitMap = b->mask_bitmap;
    }
    return 1;
}

static void show_buffers_close(show_buffers *b)
{
    if (!b) return;
    if (b->old_bitmap) FreeBitMap(b->old_bitmap);
    if (b->new_bitmap) FreeBitMap(b->new_bitmap);
    if (b->mask_bitmap) FreeBitMap(b->mask_bitmap);
    memset(b, 0, sizeof *b);
}

static int show_pause(const struct DateStamp *started, int duration, int steps)
{
    struct DateStamp now;
    int milliseconds = duration > 0 ? duration : 500;
    ULONG ticks = (ULONG)((milliseconds + steps * 10) / (steps * 20));
    LONG elapsed;
    if (!ticks) ticks = 1;
    DateStamp(&now);
    elapsed = (now.ds_Days - started->ds_Days) * 24L * 60L * TICKS_PER_SECOND * 60L +
              (now.ds_Minute - started->ds_Minute) * TICKS_PER_SECOND * 60L +
              now.ds_Tick - started->ds_Tick;
    if (elapsed >= (LONG)ticks)
        return 0;                           /* this machine needs a cut, not a crawl */
    Delay(ticks - (ULONG)(elapsed > 0 ? elapsed : 0));
    return 1;
}

static void show_blit(struct BitMap *bitmap, struct RastPort *rp, const opr_blit *part)
{
    if (part->width > 0 && part->height > 0)
        BltBitMapRastPort(bitmap, part->source_x, part->source_y, rp, part->dest_x, part->dest_y,
                          part->width, part->height, 0xC0);
}

static void show_plan(struct Window *sw, const show_buffers *b, const opr_transition_plan *plan,
                      int width, int height)
{
    int i;
    BltBitMapRastPort(plan->base_new ? b->new_bitmap : b->old_bitmap, 0, 0, sw->RPort, 0, 0,
                      width, height, 0xC0);
    for (i = 0; i < plan->old_count; i++) show_blit(b->old_bitmap, sw->RPort, &plan->old_parts[i]);
    for (i = 0; i < plan->new_count; i++) show_blit(b->new_bitmap, sw->RPort, &plan->new_parts[i]);
}

static int show_mask(show_buffers *b, int width, int height, int step, int steps)
{
    unsigned short pattern[4];
    if (!b->mask_bitmap || opr_transition_pattern_at(step, steps, pattern) < 0)
        return 0;
    SetRast(&b->mask_rp, 0);
    SetAPen(&b->mask_rp, 1);
    SetDrMd(&b->mask_rp, JAM1);
    SetAfPt(&b->mask_rp, pattern, 2);
    RectFill(&b->mask_rp, 0, 0, width - 1, height - 1);
    SetAfPt(&b->mask_rp, NULL, 0);
    return 1;
}

static void show_masked_new(struct Window *sw, const show_buffers *b, int width, int height)
{
    BltMaskBitMapRastPort(b->new_bitmap, 0, 0, sw->RPort, 0, 0, width, height, ABC | ABNC | ANBC,
                          b->mask_bitmap->Planes[0]);
}

static void show_fade_black(struct Window *sw, int width, int height, int step, int steps)
{
    unsigned short pattern[4];
    if (opr_transition_pattern_at(step, steps, pattern) < 0) return;
    ogt_set_apen(sw->RPort, argb_pen(0xff000000u));
    SetDrMd(sw->RPort, JAM1);
    SetAfPt(sw->RPort, pattern, 2);
    RectFill(sw->RPort, 0, 0, width - 1, height - 1);
    SetAfPt(sw->RPort, NULL, 0);
}

/* Animate the incoming slide's transition. Rendering remains shared with the
 * editor; when two screen-sized friend bitmaps do not fit, this deliberately
 * falls back to a cut rather than making a low-memory Amiga crawl. */
static void show_transition(struct Window *sw, show_buffers *b, const opr_show *show, int old_slide)
{
    const owf_transition *transition;
    opr_show old_show;
    int kind, direction, duration, steps = 12, i, width = sw->Width, height = sw->Height;
    if (!b->old_bitmap || !b->new_bitmap || old_slide < 0 || old_slide == show->slide) {
        show_draw(sw, show, 0, 0);
        return;
    }
    transition = &show->deck->slides[show->slide].transition;
    kind = transition->kind;
    if (kind < OWF_TRANS_FADE || kind > OWF_TRANS_DISSOLVE ||
        ((kind == OWF_TRANS_FADE || kind == OWF_TRANS_FADE_BLACK) && !b->mask_bitmap)) {
        show_draw(sw, show, 0, 0);
        return;
    }
    direction = transition->direction;
    duration = transition->duration;
    old_show = *show;
    old_show.slide = old_slide;
    show_paint(&b->old_rp, width, height, &old_show, 0, 0);
    show_paint(&b->new_rp, width, height, show, 0, 0);
    BltBitMapRastPort(b->old_bitmap, 0, 0, sw->RPort, 0, 0, width, height, 0xC0);

    if (kind >= OWF_TRANS_WIPE && kind <= OWF_TRANS_BLINDS) {
        for (i = 1; i <= steps; i++) {
            opr_transition_plan plan;
            struct DateStamp started;
            DateStamp(&started);
            if (opr_transition_plan_at(kind, direction, width, height, i, steps, &plan) < 0) break;
            show_plan(sw, b, &plan, width, height);
            if (!show_pause(&started, duration, steps)) break;
        }
    } else if (kind == OWF_TRANS_FADE) {
        for (i = 1; i <= steps; i++) {
            struct DateStamp started;
            DateStamp(&started);
            if (!show_mask(b, width, height, i, steps)) break;
            show_masked_new(sw, b, width, height);
            if (!show_pause(&started, duration, steps)) break;
        }
    } else if (kind == OWF_TRANS_FADE_BLACK) {
        int half = steps / 2;
        for (i = 1; i <= half; i++) {
            struct DateStamp started;
            DateStamp(&started);
            show_fade_black(sw, width, height, i, half);
            if (!show_pause(&started, duration, steps)) break;
        }
        if (i > half)
            for (i = 1; i <= steps - half; i++) {
                struct DateStamp started;
                DateStamp(&started);
                if (!show_mask(b, width, height, i, steps - half)) break;
                show_masked_new(sw, b, width, height);
                if (!show_pause(&started, duration, steps)) break;
            }
    } else { /* dissolve */
        int total = opr_transition_tile_count(width, height), done = 0;
        for (i = 1; i <= steps; i++) {
            struct DateStamp started;
            int until = total * i / steps;
            DateStamp(&started);
            while (done < until) {
                opr_blit tile;
                if (opr_transition_tile_at(width, height, done, &tile) == 0)
                    show_blit(b->new_bitmap, sw->RPort, &tile);
                done++;
            }
            if (!show_pause(&started, duration, steps)) break;
        }
    }
    BltBitMapRastPort(b->new_bitmap, 0, 0, sw->RPort, 0, 0, width, height, 0xC0);
}

static ULONG show_tick_delta(ULONG seconds, ULONG micros, ULONG *last_seconds, ULONG *last_micros, int *have_time)
{
    ULONG delta;
    if (!*have_time) {
        *have_time = 1;
        *last_seconds = seconds;
        *last_micros = micros;
        return 100;                         /* one Intuition tick, approximately */
    }
    if (seconds < *last_seconds) delta = 0;
    else {
        delta = (seconds - *last_seconds) * 1000UL;
        if (micros >= *last_micros) delta += (micros - *last_micros) / 1000UL;
        else if (delta >= 1000) delta = delta - 1000UL + (1000000UL + micros - *last_micros) / 1000UL;
    }
    *last_seconds = seconds;
    *last_micros = micros;
    return delta;
}

static void run_show(int from, int loop)
{
    struct Window *sw;
    struct IntuiMessage *im;
    opr_show show;
    show_buffers buffers;
    int running = 1, blank = 0, number = 0, have_time = 0, drawn_slide, drawn_blank = 0;
    ULONG last_seconds = 0, last_micros = 0;
    if (!deck || opr_show_begin(&show, deck, from, loop) < 0) {
        tell(SHOW_TITLE, "There is no visible " UNIT_LOWER " to show.");
        return;
    }
    stop_typing();
    sw = OpenWindowTags(NULL,
        WA_PubScreen, (ULONG)scr,
        WA_Left, 0, WA_Top, 0, WA_Width, scr->Width, WA_Height, scr->Height,
        WA_Borderless, TRUE, WA_Backdrop, TRUE, WA_Activate, TRUE,
        WA_RMBTrap, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY |
                  IDCMP_RAWKEY | IDCMP_INTUITICKS,
        TAG_DONE);
    if (!sw) {
        tell(SHOW_TITLE, "The full-screen show window could not open.");
        return;
    }
    show_buffers_open(&buffers, sw->Width, sw->Height);
    show_draw(sw, &show, blank, number);
    drawn_slide = show.slide;
    while (running) {
        ULONG signals = Wait((1UL << sw->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) break;
        while ((im = (struct IntuiMessage *)GetMsg(sw->UserPort)) != NULL) {
            ULONG class = im->Class, seconds = im->Seconds, micros = im->Micros;
            UWORD code = im->Code;
            int redraw = 0, rc = 0;
            ReplyMsg((struct Message *)im);
            if (class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(sw);
                show_draw(sw, &show, blank, number);
                EndRefresh(sw, TRUE);
                continue;
            }
            if (class == IDCMP_INTUITICKS) {
                rc = opr_show_tick(&show, show_tick_delta(seconds, micros, &last_seconds, &last_micros, &have_time));
                if (rc < 0) running = 0;
                else if (rc > 0) { blank = number = 0; redraw = 1; }
            } else if (class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                rc = opr_show_next(&show);
                if (rc < 0) running = 0;
                else { blank = number = 0; redraw = 1; }
            } else if (class == IDCMP_RAWKEY && !(code & 0x80)) {
                if (code == 0x4e || code == 0x4d || code == 0x49) {
                    rc = opr_show_next(&show);
                    if (rc < 0) running = 0;
                    else { blank = number = 0; redraw = 1; }
                } else if (code == 0x4f || code == 0x4c || code == 0x48) {
                    opr_show_previous(&show); blank = number = 0; redraw = 1;
                } else if (code == 0x70) {
                    opr_show_first(&show); blank = number = 0; redraw = 1;
                } else if (code == 0x71) {
                    opr_show_last(&show); blank = number = 0; redraw = 1;
                }
            } else if (class == IDCMP_VANILLAKEY) {
                if (code == 27) running = 0;
                else if (code >= '0' && code <= '9') {
                    if (number < 100000) number = number * 10 + code - '0';
                    redraw = 1;
                } else if (code == 13) {
                    if (number > 0) opr_show_goto(&show, number - 1);
                    else if (opr_show_next(&show) < 0) running = 0;
                    blank = number = 0; redraw = 1;
                } else if (code == ' ') {
                    if (opr_show_next(&show) < 0) running = 0;
                    blank = number = 0; redraw = 1;
                } else if (code == 8) {
                    if (number > 0) number /= 10;
                    else { opr_show_previous(&show); blank = 0; }
                    redraw = 1;
                } else if (code == 'b' || code == 'B') {
                    blank = blank == 1 ? 0 : 1; number = 0; redraw = 1;
                } else if (code == 'w' || code == 'W') {
                    blank = blank == 2 ? 0 : 2; number = 0; redraw = 1;
                } else if (code == 'h' || code == 'H') {
                    opr_show_next_hidden(&show); blank = number = 0; redraw = 1;
                }
            }
            if (running && redraw) {
                if (!blank && !drawn_blank && drawn_slide != show.slide && number == 0)
                    show_transition(sw, &buffers, &show, drawn_slide);
                else
                    show_draw(sw, &show, blank, number);
                drawn_slide = show.slide;
                drawn_blank = blank;
            }
        }
    }
    show_buffers_close(&buffers);
    CloseWindow(sw);
    show_slide(show.slide);
    ActivateWindow(win);
    draw_all();
}

/* The pixel box of the object typed in, and around it, as laid out now: what a keystroke redraws. */
static int edit_area(box *b)
{
    opr_slide *s;
    int frame;
    ol_scale sc;
    ol_rect r;
    if (edit_obj < 0 || !deck || !(s = laid_get(current)) || (frame = opr_slide_frame_for(s, edit_obj)) < 0) return 0;
    ol_scale_fit(&sc, TW(deck->width), slide_box.w);
    sc.origin_x = slide_box.x;
    sc.origin_y = slide_box.y;
    r = ol_node_bounds(ol_node_by_id(s->doc, s->frames[frame].node));
    {
        owf_obj resolved;
        const owf_obj *o = &deck->slides[current].objs[edit_obj];
        int x, y, x2, y2;
        if (owf_deck_resolve(deck, current, edit_obj, &resolved, NULL) >= 0) o = &resolved;
        x = ol_scale_x(&sc, TW(o->x));
        y = ol_scale_y(&sc, TW(o->y));
        x2 = ol_scale_x(&sc, TW(o->x + o->width));
        y2 = ol_scale_y(&sc, TW(o->y + o->height));
        int ry2 = ol_scale_y(&sc, r.y + r.height);
        if (ry2 > y2) y2 = ry2;
        /* the text may have run below its box, and the caret and frame are drawn around it */
        b->x = x - 4;
        b->y = y - 4;
        b->w = x2 - x + 8;
        b->h = y2 - y + 8 + slide_box.h / 4;
    }
    return 1;
}

static void union_box(box *a, const box *b)
{
    int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
    if (b->x < a->x) a->x = b->x;
    if (b->y < a->y) a->y = b->y;
    a->w = x1 - a->x;
    a->h = y1 - a->y;
}

/* After a change to the object typed in: laid out again, and its part of the slide drawn. */
static void text_changed(const box *before)
{
    box after;
    slide_changed(current);
    if (!dirty) {
        dirty = 1;
        update_window_title();
    }
    if (edit_area(&after)) {
        if (before) union_box(&after, before);
        draw_slide_part(&after);
    } else {
        draw_slide();
    }
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
}

/* Before a change: an undo step, once for each run of typing. */
static void before_change(box *b)
{
    if (!edit_area(b)) b->w = 0;
    if (!edit_grouped) {
        opr_undo_push(undo, deck, current, edit_obj);
        edit_grouped = 1;
    }
}

static void collapse_text_selection(void)
{
    text_selection.anchor = text_selection.focus = caret;
}

static void caret_moved(int extend_selection)
{
    box b;
    edit_grouped = 0;
    text_selection.focus = caret;
    if (!extend_selection)
        text_selection.anchor = caret;
    if (edit_area(&b)) draw_slide_part(&b);
}

static void type_text(const char *utf8, size_t n)
{
    box b;
    if (edit_obj < 0) return;
    before_change(&b);
    if (!opr_text_selection_empty(&text_selection)) {
        if (opr_text_delete_selection(deck, current, edit_obj, &text_selection) < 0) {
            tell(PROGRAM, "There is not enough memory for that.");
            return;
        }
        caret = text_selection.focus;
    }
    if (opr_text_insert(deck, current, edit_obj, &caret, utf8, n) < 0) {
        tell(PROGRAM, "There is not enough memory for that.");
        return;
    }
    collapse_text_selection();
    text_changed(b.w ? &b : NULL);
}

static void type_key(int key)
{
    box b;
    int rc = 0;
    if (edit_obj < 0) return;
    before_change(&b);
    if (!opr_text_selection_empty(&text_selection)) {
        rc = opr_text_delete_selection(deck, current, edit_obj, &text_selection);
        caret = text_selection.focus;
        if (!rc && key == 13)
            rc = opr_text_newline(deck, current, edit_obj, &caret);
    } else if (key == 8) rc = opr_text_backspace(deck, current, edit_obj, &caret);
    else if (key == 127) rc = opr_text_delete(deck, current, edit_obj, &caret);
    else if (key == 13) rc = opr_text_newline(deck, current, edit_obj, &caret);
    if (rc < 0) tell(PROGRAM, "There is not enough memory for that.");
    collapse_text_selection();
    text_changed(b.w ? &b : NULL);
}

static int copy_text_selection(void)
{
    char *text;
    size_t length = 0;
    if (edit_obj < 0 || opr_text_selection_empty(&text_selection)) {
        set_status("Select text to copy.");
        draw_status();
        return 0;
    }
    text = opr_text_selection_get(deck, current, edit_obj, &text_selection, &length);
    if (!text) {
        tell("Clipboard", "There is not enough memory to copy that text.");
        return 0;
    }
    if (!ow_clipboard_set_text(text, length)) {
        free(text);
        tell("Clipboard", "clipboard.device could not accept the selected text.");
        return 0;
    }
    free(text);
    set_status("Copied selected text to the clipboard.");
    draw_status();
    return 1;
}

static int copy_object_selection(void)
{
    const owf_deck_format *format = owf_deck_format_named("odp");
    const owf_obj *o;
    unsigned char *data = NULL;
    size_t length = 0;
    int ok;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select one object to copy.");
        draw_status();
        return 0;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->parent >= 0 || o->kind == OWF_OBJ_GROUP || o->kind == OWF_OBJ_CHART || o->keep >= 0) {
        set_status("That object still depends on unsupported grouped or imported package data.");
        draw_status();
        return 0;
    }
    if (!format || !format->export ||
        format->export(deck, &data, &length, NULL) != OWF_OK || !data) {
        tell("Clipboard", "The object could not be prepared as an ODP clipboard fragment.");
        free(data);
        return 0;
    }
    ok = ow_clipboard_set_deck_object(data, length, current, selected_obj);
    free(data);
    if (!ok) {
        tell("Clipboard", "clipboard.device could not accept the selected object.");
        return 0;
    }
    set_status("Copied the selected object to the clipboard.");
    draw_status();
    return 1;
}

static int copy_selection(void)
{
    if (edit_obj >= 0)
        return copy_text_selection();
    return copy_object_selection();
}

static void cut_text_selection(void)
{
    box before;
    if (!copy_text_selection())
        return;
    before_change(&before);
    if (opr_text_delete_selection(deck, current, edit_obj, &text_selection) < 0) {
        tell("Clipboard", "There is not enough memory to cut that text.");
        return;
    }
    caret = text_selection.focus;
    edit_grouped = 0;
    set_status("Cut selected text to the clipboard.");
    text_changed(before.w ? &before : NULL);
    draw_status();
}

static void cut_selection(void)
{
    if (edit_obj >= 0) {
        cut_text_selection();
        return;
    }
    if (!copy_object_selection())
        return;
    delete_selected_object();
    set_status("Cut the selected object to the clipboard.");
    draw_status();
}

static void paste_text(void)
{
    char *text;
    size_t length = 0;
    if (edit_obj < 0) {
        set_status("Click inside an editable text box before pasting text.");
        draw_status();
        return;
    }
    text = ow_clipboard_get_text(&length);
    if (!text) {
        set_status("The clipboard has no FTXT text.");
        draw_status();
        return;
    }
    type_text(text, length);
    free(text);
    edit_grouped = 0;
    set_status("Pasted text from the clipboard.");
    draw_status();
}

static void paste_object(void)
{
    unsigned char *data;
    size_t length = 0;
    int source_slide = -1, source_obj = -1, selected = -1, rc;
    owf_deck *source = NULL;
    if (!deck || !deck->nslides) return;
    data = ow_clipboard_get_deck_object(&length, &source_slide, &source_obj);
    if (!data) {
        set_status("The clipboard has no GFOffice deck object.");
        draw_status();
        return;
    }
    rc = owf_import_deck_memory(data, length, "odp", &source, NULL, NULL);
    free(data);
    if (rc != OWF_OK || !source) {
        owf_deck_free(source);
        set_status("The clipboard's deck object is damaged or unsupported.");
        draw_status();
        return;
    }
    rc = opr_obj_paste(undo, deck, current, source, source_slide, source_obj,
                       240, 240, &selected);
    owf_deck_free(source);
    if (rc < 0) {
        set_status(rc == -2 ? "That clipboard object still has unsupported package dependencies." :
                             "There is not enough memory to paste that object.");
        draw_status();
        return;
    }
    stop_typing();
    selected_obj = selected;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Pasted the object from the clipboard.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void paste_selection(void)
{
    if (edit_obj >= 0)
        paste_text();
    else
        paste_object();
}

static int find_next_text(int ask_text)
{
    opr_find_result found;
    opr_pos start;
    char entered[sizeof find_text];
    int start_obj, rc;
    if (!deck || !deck->nslides)
        return 0;
    if (ask_text) {
        if (!text_requester("Find", "Text", find_text, entered, sizeof entered))
            return 0;
        snprintf(find_text, sizeof find_text, "%s", entered);
    }
    if (!find_text[0]) {
        set_status("Enter text to find.");
        draw_status();
        return 0;
    }
    start_obj = edit_obj >= 0 ? edit_obj : selected_obj >= 0 ? selected_obj : 0;
    if (edit_obj >= 0)
        start = text_selection.focus;
    else {
        start.para = 0;
        start.offset = 0;
    }
    rc = opr_text_find(deck, current, start_obj, start, find_text, 0, &found);
    if (rc < 0) {
        tell("Find", "There is not enough memory to search this " DOCUMENT_LOWER ".");
        return 0;
    }
    if (!rc) {
        set_status("Text not found.");
        draw_status();
        return 0;
    }
    show_slide(found.slide);
    selected_obj = edit_obj = found.obj;
    text_selection = found.selection;
    caret = found.selection.focus;
    edit_grouped = 0;
    set_status("Found text; Find Next continues through the " DOCUMENT_LOWER ".");
    draw_slide();
    draw_status();
    return 1;
}

static void replace_text_command(int all)
{
    char entered[sizeof find_text], replacement[sizeof replacement_text];
    if (!deck || !deck->nslides) return;
    if (all && edit_obj < 0) {
        set_status("Click inside an editable text box before Replace All.");
        draw_status();
        return;
    }
    if (!text_requester(all ? "Replace All in Text Box" : "Replace", "Find", find_text,
                        entered, sizeof entered))
        return;
    if (!entered[0]) {
        set_status("Enter non-empty text to replace.");
        draw_status();
        return;
    }
    if (!text_requester(all ? "Replace All in Text Box" : "Replace", "Replace with",
                        replacement_text, replacement, sizeof replacement))
        return;
    snprintf(find_text, sizeof find_text, "%s", entered);
    snprintf(replacement_text, sizeof replacement_text, "%s", replacement);
    if (!all) {
        if (!find_next_text(0)) return;
        type_text(replacement_text, strlen(replacement_text));
        edit_grouped = 0;
        set_status("Replaced one match; Undo restores it.");
        draw_status();
        return;
    }
    {
        box before;
        opr_pos after;
        int count = 0, rc;
        if (!edit_area(&before)) before.w = 0;
        edit_grouped = 0;
        rc = opr_text_replace_all(undo, deck, current, edit_obj, find_text,
                                  replacement_text, 0, &count, &after);
        if (rc < 0) {
            tell("Replace All", rc == -1 ? "There is not enough memory to replace the text."
                                           : "That text box cannot be edited.");
            return;
        }
        if (count) {
            char message[96];
            caret = after;
            text_selection.anchor = text_selection.focus = after;
            text_changed(before.w ? &before : NULL);
            snprintf(message, sizeof message, "Replaced %d match%s in this text box; one Undo restores them.",
                     count, count == 1 ? "" : "es");
            set_status(message);
        } else {
            set_status("Text not found in this text box.");
        }
        draw_status();
    }
}

/* Up or down a line: the place nearest the caret on the line above or below, in the same text. */
static void caret_line(int dir, int extend_selection)
{
    opr_slide *s = laid_get(current);
    int frame;
    ol_rect b;
    opr_pos to;
    if (!s || (frame = opr_slide_frame_for(s, edit_obj)) < 0 || !opr_caret_box(s, frame, caret, NULL, NULL, &b))
        return;
    if (opr_hit_frame(s, frame, b.x, dir < 0 ? b.y - b.height / 2 : b.y + b.height + b.height / 2, NULL, NULL, &to))
        caret = to;
    caret_moved(extend_selection);
}

static int format_begin(box *before)
{
    if (edit_obj < 0) {
        set_status("Click inside an editable text box before formatting text.");
        draw_status();
        return 0;
    }
    edit_grouped = 0;
    if (!edit_area(before)) before->w = 0;
    return 1;
}

static void select_all_text(void)
{
    owf_obj *o;
    box b;
    if (edit_obj < 0 || !deck) {
        set_status("Click inside an editable text box before selecting text.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[edit_obj];
    if (!o->text.nparas)
        return;
    text_selection.anchor.para = 0;
    text_selection.anchor.offset = 0;
    text_selection.focus.para = o->text.nparas - 1;
    text_selection.focus.offset = opr_para_length(&o->text.paras[o->text.nparas - 1]);
    caret = text_selection.focus;
    edit_grouped = 0;
    set_status("All text in the current frame is selected.");
    if (edit_area(&b)) draw_slide_part(&b);
    draw_status();
}

static void format_finish(int rc, const box *before, const char *message)
{
    if (rc < 0) {
        set_status(rc == -2 ? "That text formatting cannot be applied here." :
                             "There is not enough memory to format the text.");
        draw_status();
        return;
    }
    text_changed(before && before->w ? before : NULL);
    set_status(message);
    draw_status();
}

static void format_text_flags(unsigned flags, const char *message)
{
    box before;
    int rc;
    if (!format_begin(&before)) return;
    if (opr_text_selection_empty(&text_selection))
        rc = opr_text_toggle_flags(undo, deck, current, edit_obj,
                                   caret.para, caret.para, flags);
    else
        rc = opr_text_toggle_flags_selection(undo, deck, current, edit_obj,
                                             &text_selection, flags);
    format_finish(rc, &before, message);
}

static void format_text_size(int points)
{
    box before;
    char message[64];
    int rc;
    if (!format_begin(&before)) return;
    if (opr_text_selection_empty(&text_selection))
        rc = opr_text_set_size(undo, deck, current, edit_obj,
                               caret.para, caret.para, points * OWF_TWIPS_PER_POINT);
    else
        rc = opr_text_set_size_selection(undo, deck, current, edit_obj,
                                         &text_selection, points * OWF_TWIPS_PER_POINT);
    snprintf(message, sizeof message, "%s set to %d point text.",
             opr_text_selection_empty(&text_selection) ? "Current paragraph" : "Selection", points);
    format_finish(rc, &before, message);
}

static void format_text_font(void)
{
    box before;
    owf_obj *o;
    int font = -1, rc;
    char initial[96] = "Liberation Sans", entered[96];
    if (!format_begin(&before)) return;
    o = &deck->slides[current].objs[edit_obj];
    if (caret.para >= 0 && caret.para < o->text.nparas &&
        o->text.paras[caret.para].nruns > 0)
        font = o->text.paras[caret.para].runs[0].fmt.font;
    if (font < 0 && deck->res) font = deck->res->base.font;
    if (deck->res && font >= 0 && font < deck->res->nfonts &&
        deck->res->fonts[font].name)
        snprintf(initial, sizeof initial, "%s", deck->res->fonts[font].name);
    if (!text_requester("Font Family", "Family", initial,
                        entered, sizeof entered))
        return;
    if (opr_text_selection_empty(&text_selection))
        rc = opr_text_set_font(undo, deck, current, edit_obj,
                               caret.para, caret.para, entered);
    else
        rc = opr_text_set_font_selection(undo, deck, current, edit_obj,
                                         &text_selection, entered);
    format_finish(rc, &before, opr_text_selection_empty(&text_selection) ?
                  "Current paragraph font family changed." : "Selection font family changed.");
}

static void format_text_colour(void)
{
    box before;
    owf_obj *o;
    unsigned long colour = 0, shade;
    char initial[8], entered[16];
    int rc;
    if (!format_begin(&before)) return;
    o = &deck->slides[current].objs[edit_obj];
    if (caret.para >= 0 && caret.para < o->text.nparas &&
        o->text.paras[caret.para].nruns > 0 &&
        o->text.paras[caret.para].runs[0].fmt.colour != OWF_COLOUR_AUTO)
        colour = o->text.paras[caret.para].runs[0].fmt.colour;
    snprintf(initial, sizeof initial, "%06lx", colour & 0xFFFFFFUL);
    if (!text_requester("Text Colour", "RGB (RRGGBB)", initial,
                        entered, sizeof entered))
        return;
    if (!shade_from_text(entered, &shade)) {
        tell("Text Colour", "Enter exactly six hexadecimal digits, for example 336699.");
        return;
    }
    if (opr_text_selection_empty(&text_selection))
        rc = opr_text_set_colour(undo, deck, current, edit_obj,
                                 caret.para, caret.para, OWF_SHADE_RGB(shade));
    else
        rc = opr_text_set_colour_selection(undo, deck, current, edit_obj,
                                           &text_selection, OWF_SHADE_RGB(shade));
    format_finish(rc, &before, opr_text_selection_empty(&text_selection) ?
                  "Current paragraph text colour changed." : "Selection text colour changed.");
}

static void format_text_alignment(owf_align alignment, const char *message)
{
    box before;
    int rc, first, last;
    if (!format_begin(&before)) return;
    first = text_selection.anchor.para;
    last = text_selection.focus.para;
    if (first > last) { int swap = first; first = last; last = swap; }
    rc = opr_text_set_alignment(undo, deck, current, edit_obj,
                                first, last, alignment);
    format_finish(rc, &before, message);
}

static void apply_ruler_change(int marker)
{
    box before;
    int first_para, last_para, rc;
    const char *message = marker == OGT_RULER_LEFT ? "Left paragraph indent changed." :
                          marker == OGT_RULER_FIRST ? "First-line paragraph indent changed." :
                                                    "Right paragraph indent changed.";
    if (edit_obj < 0 || !deck) {
        update_ruler();
        ogt_ruler_draw(&ruler);
        return;
    }
    if (!edit_area(&before)) before.w = 0;
    if (opr_text_selection_empty(&text_selection)) {
        first_para = last_para = caret.para;
    } else {
        first_para = text_selection.anchor.para;
        last_para = text_selection.focus.para;
        if (first_para > last_para) {
            int swap = first_para;
            first_para = last_para;
            last_para = swap;
        }
    }
    edit_grouped = 0;
    rc = opr_text_set_indents(undo, deck, current, edit_obj,
                              first_para, last_para,
                              ruler.left_twips, ruler.first_twips,
                              ruler.right_twips);
    if (rc < 0) {
        set_status(rc == -2 ? "That indent would leave no usable text width." :
                             "There is not enough memory to change the paragraph indent.");
        update_ruler();
        ogt_ruler_draw(&ruler);
        draw_status();
        return;
    }
    text_changed(before.w ? &before : NULL);
    set_status(message);
    draw_status();
}

static int on_visible_slide(int mx, int my)
{
    return deck && deck->nslides && mx >= slide_box.x && my >= slide_box.y &&
           mx < slide_box.x + slide_box.w && my < slide_box.y + slide_box.h &&
           mx >= slide_area.x && my >= slide_area.y &&
           mx < slide_area.x + slide_area.w && my < slide_area.y + slide_area.h;
}

static void current_slide_scale(ol_scale *sc)
{
    ol_scale_fit(sc, TW(deck->width), slide_box.w);
    sc->origin_x = slide_box.x;
    sc->origin_y = slide_box.y;
}

static int selection_handle(int mx, int my)
{
    static const int mode[8] = { DRAG_NW, DRAG_N, DRAG_NE, DRAG_E, DRAG_SE, DRAG_S, DRAG_SW, DRAG_W };
    box b;
    int x[8], y[8], i;
    if (selected_obj < 0 || !object_pixel_box(selected_obj, &b)) return DRAG_NONE;
    selection_points(&b, x, y);
    for (i = 0; i < 8; i++)
        if (mx >= x[i] - 5 && mx <= x[i] + 5 && my >= y[i] - 5 && my <= y[i] + 5)
            return mode[i];
    return DRAG_NONE;
}

static int hit_slide_object(int mx, int my, opr_pos *at)
{
    opr_slide *s = laid_get(current);
    ol_scale sc;
    int frame;
    if (!s || !on_visible_slide(mx, my)) return -1;
    current_slide_scale(&sc);
    frame = opr_hit(s, ol_unscale_x(&sc, mx), ol_unscale_y(&sc, my), NULL, NULL, at);
    return frame >= 0 ? opr_frame_obj(s, frame) : -1;
}

static int hit_edit_text(int mx, int my, opr_pos *at)
{
    opr_slide *s = laid_get(current);
    ol_scale sc;
    int frame;
    if (!s || edit_obj < 0 || !on_visible_slide(mx, my) ||
        (frame = opr_slide_frame_for(s, edit_obj)) < 0)
        return 0;
    current_slide_scale(&sc);
    return opr_hit_frame(s, frame, ol_unscale_x(&sc, mx), ol_unscale_y(&sc, my),
                         NULL, NULL, at);
}

static void drag_text_selection(int mx, int my)
{
    opr_pos at;
    box b;
    if (!text_selecting || !hit_edit_text(mx, my, &at) ||
        app_compare_pos(at, text_selection.focus) == 0)
        return;
    caret = text_selection.focus = at;
    if (edit_area(&b)) draw_slide_part(&b);
}

static void start_object_drag(int obj, int mode, int mx, int my)
{
    owf_obj resolved;
    if (obj < 0 || obj >= deck->slides[current].nobjs ||
        owf_deck_resolve(deck, current, obj, &resolved, NULL) < 0)
        return;
    memset(&object_drag, 0, sizeof object_drag);
    object_drag.active = 1;
    object_drag.mode = mode;
    object_drag.obj = obj;
    object_drag.mouse_x = mx;
    object_drag.mouse_y = my;
    object_drag.original = deck->slides[current].objs[obj];
    object_drag.preview = resolved;
}

static void slide_mouse_down(int mx, int my)
{
    opr_pos at;
    int obj, handle;
    if (!on_visible_slide(mx, my)) return;
    if (edit_obj >= 0 && hit_edit_text(mx, my, &at)) {
        caret = at;
        text_selection.anchor = text_selection.focus = at;
        text_selecting = 1;
        edit_grouped = 0;
        set_status("Drag to select text; Shift+Arrow extends the selection.");
        caret_moved(1);
        draw_status();
        return;
    }
    handle = selection_handle(mx, my);
    if (handle != DRAG_NONE) {
        stop_typing();
        start_object_drag(selected_obj, handle, mx, my);
        set_status("Resize the selected object.");
        draw_status();
        return;
    }
    obj = hit_slide_object(mx, my, &at);
    stop_typing();
    if (obj < 0) {
        selected_obj = -1;
        memset(&object_drag, 0, sizeof object_drag);
        set_status("Ready.");
        draw_slide();
        draw_status();
        return;
    }
    selected_obj = obj;
    start_object_drag(obj, DRAG_MOVE, mx, my);
    set_status("Drag to move; release over text to type.");
    draw_slide();
    draw_status();
}

static int twip_delta_x(const ol_scale *sc, int from, int to)
{
    return (int)((ol_unscale_x(sc, to) - ol_unscale_x(sc, from)) / OL_UNITS_PER_TWIP);
}

static int twip_delta_y(const ol_scale *sc, int from, int to)
{
    return (int)((ol_unscale_y(sc, to) - ol_unscale_y(sc, from)) / OL_UNITS_PER_TWIP);
}

static void preview_object_drag(int mx, int my)
{
    owf_obj *o;
    ol_scale sc;
    int dx, dy, left, top, right, bottom;
    int minimum_w, minimum_h, snap_x, snap_y;
    if (!object_drag.active || object_drag.obj < 0 || object_drag.obj >= deck->slides[current].nobjs)
        return;
    if (!object_drag.moved && abs(mx - object_drag.mouse_x) < 3 && abs(my - object_drag.mouse_y) < 3)
        return;
    object_drag.moved = 1;
    current_slide_scale(&sc);
    dx = twip_delta_x(&sc, object_drag.mouse_x, mx);
    dy = twip_delta_y(&sc, object_drag.mouse_y, my);
    snap_x = abs(twip_delta_x(&sc, 0, 6));
    snap_y = abs(twip_delta_y(&sc, 0, 6));
    if (snap_x < 1) snap_x = 1;
    if (snap_y < 1) snap_y = 1;
    left = object_drag.preview.x;
    top = object_drag.preview.y;
    right = left + object_drag.preview.width;
    bottom = top + object_drag.preview.height;
    minimum_w = deck->width < 120 ? deck->width : 120;
    minimum_h = deck->height < 120 ? deck->height : 120;
    if (object_drag.mode == DRAG_MOVE) {
        int width = right - left, height = bottom - top;
        left += dx;
        top += dy;
        if (snap_grid) {
            left = opr_grid_snap(left, GRID_TWIPS, snap_x);
            top = opr_grid_snap(top, GRID_TWIPS, snap_y);
        }
        right = left + width;
        bottom = top + height;
        if (left < 0) { right -= left; left = 0; }
        if (top < 0) { bottom -= top; top = 0; }
        if (right > deck->width) { left -= right - deck->width; right = deck->width; }
        if (bottom > deck->height) { top -= bottom - deck->height; bottom = deck->height; }
    } else {
        if (object_drag.mode == DRAG_NW || object_drag.mode == DRAG_W || object_drag.mode == DRAG_SW) left += dx;
        if (object_drag.mode == DRAG_NE || object_drag.mode == DRAG_E || object_drag.mode == DRAG_SE) right += dx;
        if (object_drag.mode == DRAG_NW || object_drag.mode == DRAG_N || object_drag.mode == DRAG_NE) top += dy;
        if (object_drag.mode == DRAG_SW || object_drag.mode == DRAG_S || object_drag.mode == DRAG_SE) bottom += dy;
        if (snap_grid) {
            if (object_drag.mode == DRAG_NW || object_drag.mode == DRAG_W || object_drag.mode == DRAG_SW)
                left = opr_grid_snap(left, GRID_TWIPS, snap_x);
            if (object_drag.mode == DRAG_NE || object_drag.mode == DRAG_E || object_drag.mode == DRAG_SE)
                right = opr_grid_snap(right, GRID_TWIPS, snap_x);
            if (object_drag.mode == DRAG_NW || object_drag.mode == DRAG_N || object_drag.mode == DRAG_NE)
                top = opr_grid_snap(top, GRID_TWIPS, snap_y);
            if (object_drag.mode == DRAG_SW || object_drag.mode == DRAG_S || object_drag.mode == DRAG_SE)
                bottom = opr_grid_snap(bottom, GRID_TWIPS, snap_y);
        }
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right > deck->width) right = deck->width;
        if (bottom > deck->height) bottom = deck->height;
        if (right - left < minimum_w) {
            if (object_drag.mode == DRAG_NW || object_drag.mode == DRAG_W || object_drag.mode == DRAG_SW)
                left = right - minimum_w;
            else right = left + minimum_w;
        }
        if (bottom - top < minimum_h) {
            if (object_drag.mode == DRAG_NW || object_drag.mode == DRAG_N || object_drag.mode == DRAG_NE)
                top = bottom - minimum_h;
            else bottom = top + minimum_h;
        }
        /* Enforcing the minimum can move the opposite edge back outside a
         * very small slide. Keep the final preview inside the model's box
         * invariant before handing it to opr_obj_set_box(). */
        if (left < 0) { left = 0; right = minimum_w; }
        if (top < 0) { top = 0; bottom = minimum_h; }
        if (right > deck->width) { right = deck->width; left = right - minimum_w; }
        if (bottom > deck->height) { bottom = deck->height; top = bottom - minimum_h; }
    }
    o = &deck->slides[current].objs[object_drag.obj];
    o->x = left;
    o->y = top;
    o->width = right - left;
    o->height = bottom - top;
    o->flags &= ~(unsigned)OWF_OBJ_INHERIT_BOX;
    slide_changed(current);
    {
        char message[96];
        snprintf(message, sizeof message, "%s: %d, %d; %d x %d twips.",
                 object_drag.mode == DRAG_MOVE ? "Position" : "Size", o->x, o->y, o->width, o->height);
        set_status(message);
    }
    draw_slide();
    draw_status();
}

static void finish_object_drag(int mx, int my)
{
    int obj, rc;
    owf_obj target, original;
    opr_pos at;
    if (!object_drag.active) return;
    obj = object_drag.obj;
    target = deck->slides[current].objs[obj];
    original = object_drag.original;
    if (object_drag.moved) {
        owf_obj *o = &deck->slides[current].objs[obj];
        o->x = original.x; o->y = original.y; o->width = original.width; o->height = original.height;
        o->flags = original.flags;
        rc = opr_obj_set_box(undo, deck, current, obj, target.x, target.y, target.width, target.height);
        if (rc < 0) {
            set_status("The object could not be moved or resized.");
        } else {
            dirty = 1;
            update_window_title();
            set_status(object_drag.mode == DRAG_MOVE ? "Object moved." : "Object resized.");
        }
        slide_changed(current);
        if (strip) ogt_list_draw_row(strip, current);
        update_edit_tools();
        ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    } else if (obj == hit_slide_object(mx, my, &at) && opr_obj_editable(deck, current, obj)) {
        edit_obj = obj;
        caret = at;
        collapse_text_selection();
        edit_grouped = 0;
        set_status("Typing: Esc stops.");
    } else {
        set_status("Object selected.");
    }
    memset(&object_drag, 0, sizeof object_drag);
    draw_slide();
    draw_status();
}

static void nudge_selected(int dx, int dy, int pixels)
{
    owf_obj box, before;
    ol_scale sc;
    int tx, ty;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs ||
        owf_deck_resolve(deck, current, selected_obj, &box, NULL) < 0)
        return;
    before = box;
    current_slide_scale(&sc);
    tx = twip_delta_x(&sc, slide_box.x, slide_box.x + pixels);
    ty = twip_delta_y(&sc, slide_box.y, slide_box.y + pixels);
    if (tx < 1) tx = 1;
    if (ty < 1) ty = 1;
    box.x += dx * tx;
    box.y += dy * ty;
    if (box.x < 0) box.x = 0;
    if (box.y < 0) box.y = 0;
    if (box.x + box.width > deck->width) box.x = deck->width - box.width;
    if (box.y + box.height > deck->height) box.y = deck->height - box.height;
    if (box.x == before.x && box.y == before.y)
        return;
    if (opr_obj_set_box(undo, deck, current, selected_obj, box.x, box.y, box.width, box.height) < 0)
        return;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Object nudged.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void rotate_selected(int delta_tenths)
{
    owf_obj *o;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select a shape to rotate.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->kind != OWF_OBJ_SHAPE) {
        set_status("Only shape objects can be rotated.");
        draw_status();
        return;
    }
    stop_typing();
    if (opr_obj_set_rotation(undo, deck, current, selected_obj,
                             o->rotation + delta_tenths) < 0) {
        set_status("The shape could not be rotated.");
        draw_status();
        return;
    }
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status(delta_tenths > 0 ? "Shape rotated left 15 degrees."
                                : "Shape rotated right 15 degrees.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void set_selected_text_columns(int columns)
{
    owf_obj *o;
    int gap, rc;
    char message[96];
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select a text frame for columns.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (!opr_obj_editable(deck, current, selected_obj)) {
        set_status("Only text frames and text-bearing shapes can have columns.");
        draw_status();
        return;
    }
    gap = columns == 1 ? 0 : o->text_column_gap > 0 ? o->text_column_gap : 180;
    stop_typing();
    rc = opr_obj_set_text_columns(undo, deck, current, selected_obj, columns, gap);
    if (rc < 0) {
        set_status(rc == -2 ? "Those columns do not fit inside the text frame."
                            : "The text columns could not be changed.");
        draw_status();
        return;
    }
    slide_changed(current);
    dirty = 1;
    update_window_title();
    if (columns == 1)
        snprintf(message, sizeof message, "Text frame set to one column.");
    else
        snprintf(message, sizeof message, "Text frame set to %d columns.", columns);
    set_status(message);
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void edit_selected_text_frame(void)
{
    owf_obj *o;
    int columns, gap, inset[4], rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select a text frame to format.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (!opr_obj_editable(deck, current, selected_obj) || o->parent >= 0) {
        set_status("Only a top-level text frame or text-bearing shape can be formatted.");
        draw_status();
        return;
    }
    columns = o->text_columns >= 2 ? o->text_columns : 1;
    gap = columns == 1 ? 0 : o->text_column_gap;
    memcpy(inset, o->inset, sizeof inset);
    if (!text_frame_requester(&columns, &gap, inset)) return;
    if (o->text_columns == (columns == 1 ? 0 : columns) &&
        o->text_column_gap == (columns == 1 ? 0 : gap) &&
        !memcmp(o->inset, inset, sizeof inset)) {
        set_status("Text-frame settings are unchanged.");
        draw_status();
        return;
    }
    stop_typing();
    rc = opr_obj_set_text_frame(undo, deck, current, selected_obj,
                                columns, gap, inset);
    if (rc < 0) {
        if (rc == -2)
            tell("Text Frame", "Use 1 to 16 columns and non-negative margins and gap that leave room for text.");
        else
            set_status("The text-frame settings could not be changed.");
        draw_status();
        return;
    }
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Text-frame columns, gap and margins changed.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

#ifdef OPENPUBLISHER
static void set_selected_wrap(owf_frame_wrap wrap)
{
    owf_obj *o;
    int gaps[4] = {120, 120, 120, 120};
    int rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select the object that text should wrap around.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->wrap != OWF_WRAP_NONE)
        memcpy(gaps, o->wrap_gap, sizeof gaps);
    stop_typing();
    rc = opr_obj_set_wrap(undo, deck, current, selected_obj, wrap,
                          wrap == OWF_WRAP_NONE ? NULL : gaps);
    if (rc < 0) {
        set_status(rc == -2 ? "That object cannot be used for text wrapping."
                            : "Text wrapping could not be changed.");
        draw_status();
        return;
    }
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status(wrap == OWF_WRAP_NONE ? "Text wrapping removed."
               : wrap == OWF_WRAP_TOP_BOTTOM ? "Text now stays above and below the object."
                                             : "Text now wraps around the object.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void edit_selected_wrap_distance(void)
{
    owf_obj *o;
    int before[4], gaps[4], rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select an object whose text wrap distance should change.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->wrap == OWF_WRAP_NONE) {
        set_status("Choose Square, Tight or Top and Bottom wrapping first.");
        draw_status();
        return;
    }
    memcpy(before, o->wrap_gap, sizeof before);
    memcpy(gaps, o->wrap_gap, sizeof gaps);
    if (!wrap_distance_requester(gaps)) return;
    if (!memcmp(before, gaps, sizeof gaps)) {
        set_status("Text wrap distances are unchanged.");
        draw_status();
        return;
    }
    stop_typing();
    rc = opr_obj_set_wrap(undo, deck, current, selected_obj,
                          (owf_frame_wrap)o->wrap, gaps);
    if (rc < 0) {
        if (rc == -2)
            tell("Text Wrap Distance", "Each distance must be between 0 and 20000 twips.");
        else
            set_status("Text wrap distances could not be changed.");
        draw_status();
        return;
    }
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Text wrap distances changed.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static int empty_text_frame(int slide, int obj)
{
    char *text = opr_text_get(deck, slide, obj);
    int empty = text && !*text;
    owf_free(text);
    return empty;
}

static void link_selected_text_frame(void)
{
    owf_obj *source;
    int page, obj, target_page = -1, target_obj = -1, rc;
    char message[96];
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select the text box that should flow first.");
        draw_status();
        return;
    }
    source = &deck->slides[current].objs[selected_obj];
    if (source->kind != OWF_OBJ_TEXT || source->parent >= 0) {
        set_status("Only a top-level text box can start or extend a text flow.");
        draw_status();
        return;
    }
    for (page = current; page < deck->nslides && target_page < 0; ++page) {
        int first = page == current ? selected_obj + 1 : 0;
        for (obj = first; obj < deck->slides[page].nobjs; ++obj) {
            const owf_obj *candidate = &deck->slides[page].objs[obj];
            if (candidate->kind == OWF_OBJ_TEXT && candidate->parent < 0 &&
                !(candidate->flags & OWF_OBJ_HIDDEN) && candidate->flow_story < 0 &&
                empty_text_frame(page, obj)) {
                target_page = page;
                target_obj = obj;
                break;
            }
        }
    }
    if (target_page < 0) {
        set_status("No later empty text box is available; insert one after this box.");
        draw_status();
        return;
    }
    stop_typing();
    rc = opr_obj_link_text_frame(undo, deck, current, selected_obj, target_page, target_obj);
    if (rc < 0) {
        set_status(rc == -2 ? "The empty box must be the next box after the selected flow position."
                            : "The text boxes could not be linked.");
        draw_status();
        return;
    }
    laid_clear();
    dirty = 1;
    update_window_title();
    snprintf(message, sizeof message, "Text now continues into page %d's empty box.", target_page + 1);
    set_status(message);
    draw_slide();
    if (strip) {
        ogt_list_draw_row(strip, current);
        if (target_page != current) ogt_list_draw_row(strip, target_page);
    }
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void unlink_selected_text_frame(void)
{
    int rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select a non-first text box in a linked flow.");
        draw_status();
        return;
    }
    stop_typing();
    rc = opr_obj_unlink_text_frame(undo, deck, current, selected_obj);
    if (rc < 0) {
        set_status(rc == -2 ? "The first box owns the story; select a later box to remove."
                            : "The text box could not be unlinked.");
        draw_status();
        return;
    }
    laid_clear();
    dirty = 1;
    update_window_title();
    set_status("Text box removed from the flow; the remaining boxes close the gap.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}
#endif

static void insert_shape(owf_shape kind)
{
    opr_shape_spec spec;
    int selected = -1;
    if (!deck || current < 0 || current >= deck->nslides)
        return;
    memset(&spec, 0, sizeof spec);
    spec.shape = kind;
    spec.width = deck->width / 3;
    spec.height = kind == OWF_SHAPE_LINE ? deck->height / 5 : deck->height / 3;
    spec.x = (deck->width - spec.width) / 2;
    spec.y = (deck->height - spec.height) / 2;
    spec.look.fill = kind == OWF_SHAPE_LINE ? 0 : OWF_SHADE(0x729fcf);
    spec.look.line = OWF_SHADE(0x203040);
    spec.look.line_width = 20;
    if (kind == OWF_SHAPE_ROUNDRECT)
        spec.radius = (spec.width < spec.height ? spec.width : spec.height) / 6;
    if (opr_obj_insert_shape(undo, deck, current, &spec, &selected) < 0) {
        set_status("The shape could not be inserted.");
        draw_status();
        return;
    }
    stop_typing();
    selected_obj = selected;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Shape inserted; drag it or use Object to rotate it.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void insert_textbox(void)
{
    opr_textbox_spec spec;
    int selected = -1;
    if (!deck || current < 0 || current >= deck->nslides)
        return;
    memset(&spec, 0, sizeof spec);
    spec.width = deck->width / 2;
    spec.height = deck->height / 4;
    spec.x = (deck->width - spec.width) / 2;
    spec.y = (deck->height - spec.height) / 2;
    spec.inset[0] = spec.inset[2] = 120;
    spec.inset[1] = spec.inset[3] = 80;
    spec.valign = OWF_VALIGN_TOP;
    if (opr_obj_insert_textbox(undo, deck, current, &spec, &selected) < 0) {
        set_status("The text box could not be inserted.");
        draw_status();
        return;
    }
    stop_typing();
    selected_obj = selected;
    edit_obj = selected;
    caret.para = 0;
    caret.offset = 0;
    collapse_text_selection();
    edit_grouped = 0;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Text box inserted. Type its text; Esc stops.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void delete_selected_object(void)
{
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select an object to delete.");
        draw_status();
        return;
    }
    if (opr_obj_delete(undo, deck, current, selected_obj) < 0) {
        set_status("That grouped object cannot be deleted yet.");
        draw_status();
        return;
    }
    stop_typing();
    selected_obj = -1;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Object deleted.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void duplicate_selected_object(void)
{
    int selected = -1;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select an object to duplicate.");
        draw_status();
        return;
    }
    if (opr_obj_duplicate(undo, deck, current, selected_obj, 240, 240, &selected) < 0) {
        set_status("That grouped object cannot be duplicated yet.");
        draw_status();
        return;
    }
    stop_typing();
    selected_obj = selected;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Object duplicated; the copy is selected.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void arrange_selected_object(opr_arrange how, const char *message)
{
    int selected = selected_obj;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select an object to arrange.");
        draw_status();
        return;
    }
    if (opr_obj_arrange(undo, deck, current, selected_obj, how, &selected) < 0) {
        set_status("That grouped object cannot be arranged yet.");
        draw_status();
        return;
    }
    if (selected == selected_obj) {
        set_status("The object is already at that layer.");
        draw_status();
        return;
    }
    stop_typing();
    selected_obj = selected;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status(message);
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void align_selected_object(opr_align how, const char *message)
{
    owf_obj before, after;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs ||
        owf_deck_resolve(deck, current, selected_obj, &before, NULL) < 0) {
        set_status("Select an object to align.");
        draw_status();
        return;
    }
    if (opr_obj_align(undo, deck, current, selected_obj, how) < 0) {
        set_status("That grouped object cannot be aligned yet.");
        draw_status();
        return;
    }
    if (owf_deck_resolve(deck, current, selected_obj, &after, NULL) >= 0 &&
        before.x == after.x && before.y == after.y &&
        before.width == after.width && before.height == after.height) {
        set_status("The object is already aligned there.");
        draw_status();
        return;
    }
    stop_typing();
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status(message);
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void edit_selected_appearance(void)
{
    owf_look before, edited;
    owf_obj *o;
    int rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select a text box or shape to format.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->parent >= 0 || (o->kind != OWF_OBJ_TEXT && o->kind != OWF_OBJ_SHAPE)) {
        set_status("Appearance editing currently supports top-level text boxes and shapes.");
        draw_status();
        return;
    }
    before = edited = o->look;
    if (!appearance_requester(&edited)) return;
    rc = opr_obj_set_look(undo, deck, current, selected_obj, &edited);
    if (rc < 0) {
        set_status(rc == -2 ? "That appearance is not valid for this object."
                            : "The object's appearance could not be changed.");
        draw_status();
        return;
    }
    if (!memcmp(&before, &edited, sizeof before)) {
        set_status("The object's appearance is unchanged.");
        draw_status();
        return;
    }
    stop_typing();
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Object appearance changed.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void edit_selected_picture_crop(void)
{
    owf_obj *o;
    int before[4], crop[4], rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select a picture to crop.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->parent >= 0 || o->kind != OWF_OBJ_PICTURE) {
        set_status("Picture Crop supports one top-level picture.");
        draw_status();
        return;
    }
    memcpy(before, o->crop, sizeof before);
    memcpy(crop, o->crop, sizeof crop);
    if (!picture_crop_requester(crop)) return;
    rc = opr_obj_set_picture_crop(undo, deck, current, selected_obj, crop);
    if (rc < 0) {
        if (rc == -2)
            tell("Picture Crop", "Use values from 0 to 999; opposite sides together must be less than 1000.");
        else
            set_status("The picture crop could not be changed.");
        draw_status();
        return;
    }
    if (!memcmp(before, crop, sizeof before)) {
        set_status("The picture crop is unchanged.");
        draw_status();
        return;
    }
    stop_typing();
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Picture crop changed.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void edit_selected_geometry(void)
{
    owf_obj before, edited, *o;
    int rc;
    if (!deck || selected_obj < 0 || selected_obj >= deck->slides[current].nobjs) {
        set_status("Select an object to position or size.");
        draw_status();
        return;
    }
    o = &deck->slides[current].objs[selected_obj];
    if (o->parent >= 0 || o->kind == OWF_OBJ_GROUP ||
        owf_deck_resolve(deck, current, selected_obj, &before, NULL) < 0) {
        set_status("Position and Size currently supports one top-level object.");
        draw_status();
        return;
    }
    edited = before;
    if (!geometry_requester(&edited)) return;
    rc = opr_obj_set_box(undo, deck, current, selected_obj,
                         edited.x, edited.y, edited.width, edited.height);
    if (rc < 0) {
        if (rc == -2)
            tell("Position and Size", "The object must have a positive size and remain wholly inside the slide or page.");
        else
            set_status("The object's position and size could not be changed.");
        draw_status();
        return;
    }
    if (before.x == edited.x && before.y == edited.y &&
        before.width == edited.width && before.height == edited.height &&
        !(before.flags & OWF_OBJ_INHERIT_BOX)) {
        set_status("The object's position and size are unchanged.");
        draw_status();
        return;
    }
    stop_typing();
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Object position and size changed.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

static void do_undo(int redo)
{
    int slide = -1, obj = -1;
    int was_typing = edit_obj >= 0;
    if ((redo ? opr_undo_redo(undo, deck, &slide, &obj) : opr_undo_undo(undo, deck, &slide, &obj)) < 0) return;
    if (obj < 0) {
        structure_changed(slide, redo ? UNIT " change redone." : UNIT " change undone.");
        return;
    }
    slide_changed(slide);
    dirty = 1;
    update_window_title();
    if (slide != current) show_slide(slide);
    selected_obj = obj;
    edit_obj = was_typing && opr_obj_editable(deck, current, obj) ? obj : -1;
    edit_grouped = 0;
    if (edit_obj >= 0)
        opr_text_move(deck, current, edit_obj, &caret, OPR_MOVE_TEXT_END);
    collapse_text_selection();
    set_status(redo ? "Redone." : "Undone.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

/* ---- Files ---- */

static void set_deck(owf_deck *d, const char *path, const char *format)
{
    laid_clear();
    pictures_clear();
    if (undo) opr_undo_clear(undo);
    if (deck) owf_deck_free(deck);
    deck = d;
    free(versions);
    versions = NULL;
    nversions = 0;
    current = 0;
    dirty = 0;
    clear_object_selection();
    snprintf(current_path, sizeof current_path, "%s", path ? path : "");
    snprintf(current_format, sizeof current_format, "%s", format ? format : "ODP");
    update_window_title();
}

static int confirm_discard(void)
{
    if (!dirty) return 1;
    return ask(PROGRAM, "The " DOCUMENT_LOWER " has changes that are not saved.\n\nLose them?", "Lose Them|Cancel") == 1;
}

static int ask_file(int save, const char *title, const char *initial, char *out, size_t out_size)
{
    struct FileRequester *fr;
    char last[512];
    int ok = 0;
    lastdir_get(PROGRAM, last, sizeof last);
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
        lastdir_put(PROGRAM, (const char *)fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

static const char *picture_mime(const char *path)
{
    const char *e = path ? strrchr(path, '.') : NULL;
    if (!e) return "application/octet-stream";
    if (!strcasecmp(e, ".png")) return "image/png";
    if (!strcasecmp(e, ".jpg") || !strcasecmp(e, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(e, ".gif")) return "image/gif";
    if (!strcasecmp(e, ".webp")) return "image/webp";
    if (!strcasecmp(e, ".bmp")) return "image/bmp";
    if (!strcasecmp(e, ".iff") || !strcasecmp(e, ".ilbm") || !strcasecmp(e, ".lbm"))
        return "image/iff";
    return "application/octet-stream";
}

static int picture_file(const char *path, unsigned char **data, size_t *length)
{
    BPTR f;
    LONG size, got;
    unsigned char *p;
    *data = NULL;
    *length = 0;
    f = Open((STRPTR)path, MODE_OLDFILE);
    if (!f) return 0;
    Seek(f, 0, OFFSET_END);
    size = Seek(f, 0, OFFSET_BEGINNING);
    if (size <= 0 || size > 32 * 1024 * 1024) {
        Close(f);
        return 0;
    }
    p = (unsigned char *)malloc((size_t)size);
    if (!p) {
        Close(f);
        return 0;
    }
    got = Read(f, p, size);
    Close(f);
    if (got != size) {
        free(p);
        return 0;
    }
    *data = p;
    *length = (size_t)size;
    return 1;
}

static void insert_picture(void)
{
    char path[512];
    unsigned char *data = NULL;
    size_t length = 0;
    Object *dto = NULL;
    struct BitMapHeader *bmh = NULL;
    opr_picture_spec spec;
    int natural_w = 4320, natural_h = 2880, max_w, max_h, selected = -1;
    if (!deck || current < 0 || current >= deck->nslides)
        return;
    if (!ask_file(0, "Insert a picture", NULL, path, sizeof path))
        return;
    if (!picture_file(path, &data, &length)) {
        tell("Insert Picture", "The picture is empty, larger than 32 MB, or could not be read.");
        return;
    }
    if (DataTypesBase) {
        dto = NewDTObject((APTR)path, DTA_GroupID, GID_PICTURE,
                          PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE, TAG_DONE);
        if (dto) {
            GetDTAttrs(dto, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE);
            if (bmh && bmh->bmh_Width && bmh->bmh_Height) {
                natural_w = (int)bmh->bmh_Width * 15;
                natural_h = (int)bmh->bmh_Height * 15;
            }
            DisposeDTObject(dto);
        }
    }
    memset(&spec, 0, sizeof spec);
    spec.width = natural_w;
    spec.height = natural_h;
    max_w = deck->width * 2 / 3;
    max_h = deck->height * 2 / 3;
    if (spec.width > max_w) {
        spec.height = (int)((long long)spec.height * max_w / spec.width);
        spec.width = max_w;
    }
    if (spec.height > max_h) {
        spec.width = (int)((long long)spec.width * max_h / spec.height);
        spec.height = max_h;
    }
    if (spec.width < 1) spec.width = 1;
    if (spec.height < 1) spec.height = 1;
    spec.x = (deck->width - spec.width) / 2;
    spec.y = (deck->height - spec.height) / 2;
    spec.name = leaf(path);
    spec.mime = picture_mime(path);
    spec.alt = leaf(path);
    spec.data = data;
    spec.length = length;
    spec.natural_width = natural_w;
    spec.natural_height = natural_h;
    if (opr_obj_insert_picture(undo, deck, current, &spec, &selected) < 0) {
        free(data);
        tell("Insert Picture", "The picture could not be embedded in this " DOCUMENT_LOWER ".");
        return;
    }
    free(data);
    stop_typing();
    selected_obj = selected;
    slide_changed(current);
    dirty = 1;
    update_window_title();
    set_status("Picture embedded; drag it or use Object to position and size it.");
    draw_slide();
    if (strip) ogt_list_draw_row(strip, current);
    update_edit_tools();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    draw_status();
}

/* The file's name with another extension: "Talk.pptx" as "Talk.odp". */
static void with_extension(const char *path, const char *ext, char *out, size_t size)
{
    const char *name = path && *path ? leaf(path) : "Untitled";
    const char *dot = strrchr(name, '.');
    int n = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    snprintf(out, size, "%.*s.%s", n > 100 ? 100 : n, name, ext);
}

static void open_deck(const char *path)
{
    owf_deck *d = NULL;
    owf_report *report = owf_report_new();
#ifdef OPENPUBLISHER
    int version = OWF_PUB_UNKNOWN;
#else
    const owf_deck_format *used = NULL;
#endif
    char msg[600];
#ifdef OPENPUBLISHER
    int rc = opp_import_pub_file(path, &d, report, &version);
#else
    int rc = owf_import_deck_file(path, NULL, &d, report, &used);
#endif
    if (rc != OWF_OK || !d) {
        snprintf(msg, sizeof msg, PROGRAM " could not open %.200s.\n\n%.200s", leaf(path),
#ifdef OPENPUBLISHER
                 rc == OWF_ERR_FORMAT ? "It is not a Microsoft Publisher file GFOpenPublisher reads." : owf_error_text(rc));
#else
                 rc == OWF_ERR_FORMAT ? "It is not a presentation GFOpenPresentation reads (ODP or PPTX)." : owf_error_text(rc));
#endif
        tell("Open", msg);
        owf_report_free(report);
        return;
    }
#ifdef OPENPUBLISHER
    set_deck(d, path, "PUB");
    snprintf(msg, sizeof msg, "%s, %d page%s%s.", owf_pub_version_name(version), d->nslides,
             d->nslides == 1 ? "" : "s", owf_report_count(report) ? "; some parts shown simpler" : "");
#else
    set_deck(d, path, used && !strcmp(used->name, "pptx") ? "PPTX" : "ODP");
    snprintf(msg, sizeof msg, "%d slide%s%s.", d->nslides, d->nslides == 1 ? "" : "s",
             owf_report_count(report) ? "; some parts shown simpler" : "");
#endif
    set_status(msg);
    owf_report_free(report);
}

static void new_deck(void)
{
    owf_deck *d;
    if (!confirm_discard()) return;
    d = owf_deck_new(
#ifdef OPENPUBLISHER
                     11906, 16838
#else
                     OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT
#endif
                     );
    if (!d || !owf_deck_add_slide(d, -1, 1)) {      /* a title slide */
        owf_deck_free(d);
        tell(PROGRAM, "There is not enough memory for a new " DOCUMENT_LOWER ".");
        return;
    }
    set_deck(d, "", "ODP");
    set_status("A new " DOCUMENT_LOWER ": click a box to type in it.");
    relayout();
}

static void do_open(void)
{
    char path[512];
    if (!confirm_discard()) return;
    if (!ask_file(0, "Open a " DOCUMENT_LOWER, NULL, path, sizeof path)) return;
    open_deck(path);
    relayout();
}

static int save_to(const char *path)
{
    owf_report *report = owf_report_new();
    char msg[600];
    int rc = owf_export_deck_file(deck, path, "odp", report);
    owf_report_free(report);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, PROGRAM " could not save %.200s.\n\n%.200s", leaf(path), owf_error_text(rc));
        tell("Save", msg);
        return 0;
    }
    snprintf(current_path, sizeof current_path, "%s", path);
    snprintf(current_format, sizeof current_format, "ODP");
    dirty = 0;
    snprintf(msg, sizeof msg, "%.200s saved.", leaf(path));
    set_status(msg);
    update_window_title();
    draw_status();
    return 1;
}

static void do_save_as(void)
{
    char path[512], name[128];
    if (!deck) return;
    with_extension(current_path, "odp", name, sizeof name);
    if (ask_file(1, "Save the " DOCUMENT_LOWER " as ODP", name, path, sizeof path)) {
        size_t n = strlen(path);
        if (n < 4 || (strcmp(path + n - 4, ".odp") && strcmp(path + n - 4, ".ODP")))
            snprintf(path + n, sizeof path - n, ".odp");
        save_to(path);
    }
}

static void do_save(void)
{
    size_t n = strlen(current_path);
    /* PPTX is read only for now: a PowerPoint file is saved as ODP beside it, under a name asked for */
    if (!n || strcmp(current_format, "ODP") || n < 4 || (strcmp(current_path + n - 4, ".odp") &&
                                                         strcmp(current_path + n - 4, ".ODP"))) {
        do_save_as();
        return;
    }
    save_to(current_path);
}

static int write_pdf(const char *path)
{
    unsigned char *data = NULL;
    size_t length = 0;
    owf_report *report = owf_report_new();
    BPTR f;
    int rc = opr_export_pdf(deck, NULL, &data, &length, report), ok = 0;
    owf_report_free(report);
    if (rc == OWF_OK && (f = Open((STRPTR)path, MODE_NEWFILE))) {
        ok = Write(f, data, (LONG)length) == (LONG)length;
        Close(f);
    }
    free(data);
    return ok;
}

static void do_export_pdf(void)
{
    char path[512], name[128], msg[600];
    if (!deck) return;
    with_extension(current_path, "pdf", name, sizeof name);
    if (!ask_file(1, "Export the " UNITS_LOWER " as PDF", name, path, sizeof path)) return;
    if (write_pdf(path)) snprintf(msg, sizeof msg, "%.200s written: one PDF page per %s.", leaf(path), UNIT_LOWER);
    else snprintf(msg, sizeof msg, "%.200s could not be written.", leaf(path));
    set_status(msg);
    draw_status();
}

/* Print: the slides as a PDF in T:, and GFOpenPrint's Print window on it, started on its own (GFOpenWrite's way,
 * app/ow_print.c). GFOpenPrint chooses the printer, or saves the PDF. */
#define OPENPRINT "C:GFOpenPrint"
static void do_print(void)
{
    char path[160], name[128], cmd[300];
    struct Process *me = (struct Process *)FindTask(NULL);
    APTR was = me->pr_WindowPtr;
    BPTR lock, in, out;
    if (!deck) return;
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
    if (!write_pdf(path)) {
        set_status("The " UNITS_LOWER " couldn't be made ready to print.");
        draw_status();
        return;
    }
    snprintf(cmd, sizeof cmd, "%s \"%s\"", OPENPRINT, path);
    in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out || SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, in, SYS_Output, out, NP_StackSize, 65536,
                                  TAG_DONE) == -1) {
        if (in) Close(in);
        if (out) Close(out);
        set_status("GFOpenPrint couldn't be started.");
    } else {
        set_status("GFOpenPrint's Print window is open for these " UNITS_LOWER ".");
    }
    draw_status();
}

static void about(void)
{
    char text[400];
#ifdef OPENPUBLISHER
    snprintf(text, sizeof text,
             VERSION_TEXT "\nThe GFOffice Suite's desktop publisher.\n\nOpens Microsoft Publisher files as positioned pages,\n"
             "saves ODP, exports PDF and prints through GFOpenPrint.\n\nPublisher core %s, presentation core %s, libowf %s, GFOpenLayout %s.\n"
             "MIT, Copyright (c) 2026 AmiCradle Team.",
             OPP_CORE_VERSION, OPR_CORE_VERSION, OWF_VERSION, OL_VERSION);
#else
    snprintf(text, sizeof text,
             VERSION_TEXT "\nThe GFOffice Suite's slides.\n\nOpens ODP and PPTX, saves ODP, exports PDF,\n"
             "prints through GFOpenPrint, and runs a full-screen show.\n"
             "F5: first; Shift-F5: current; Esc: end.\n\nCore %s, libowf %s, GFOpenLayout %s.\n"
             "MIT, Copyright (c) 2026 AmiCradle Team.",
             OPR_CORE_VERSION, OWF_VERSION, OL_VERSION);
#endif
    tell("About " PROGRAM, text);
}

/* ---- Commands ---- */

/* Zoom to z percent of the fitted size, the middle of what is shown kept in the middle. */
static void set_zoom(int z)
{
    long fx = 500, fy = 500;                         /* the view's middle, in thousandths of the slide */
    if (z == zoom) return;
    if (slide_box.w > 0 && slide_box.h > 0) {
        fx = (long)(slide_area.x + slide_area.w / 2 - slide_box.x) * 1000 / slide_box.w;
        fy = (long)(slide_area.y + slide_area.h / 2 - slide_box.y) * 1000 / slide_box.h;
    }
    zoom = z;
    place_slide();
    pan_x = (int)(fx * slide_box.w / 1000) + 12 - slide_area.w / 2;
    pan_y = (int)(fy * slide_box.h / 1000) + 12 - slide_area.h / 2;
    relayout();
}

static void zoom_step(int dir)
{
    int i, n = (int)(sizeof zooms / sizeof zooms[0]), at = 0;
    for (i = 0; i < n; ++i)
        if (zooms[i] <= zoom) at = i;
    at += dir;
    if (at < 0) at = 0;
    if (at >= n) at = n - 1;
    set_zoom(zooms[at]);
}

/* The scrollers moved: the slide follows. */
static void scrolled(void)
{
    ULONG top;
    int moved = 0;
    if (hscroll && GetAttr(PGA_Top, (Object *)hscroll, &top) && (int)top != pan_x) { pan_x = (int)top; moved = 1; }
    if (vscroll && GetAttr(PGA_Top, (Object *)vscroll, &top) && (int)top != pan_y) { pan_y = (int)top; moved = 1; }
    if (moved) {
        place_slide();
        draw_slide();
    }
}

static void set_buttons(int style)
{
    tb_style = style;
    relayout();
}

static void menu_action(ULONG id)
{
    switch (id) {
    case M_NEW: new_deck(); break;
    case M_OPEN: do_open(); break;
    case M_SAVE: do_save(); break;
    case M_SAVE_AS: do_save_as(); break;
    case M_PRINT: do_print(); break;
    case M_PDF: do_export_pdf(); break;
    case M_ABOUT: about(); break;
    case M_QUIT: if (confirm_discard()) quit_now = 1; break;
    case M_UNDO: do_undo(0); break;
    case M_REDO: do_undo(1); break;
    case M_CUT: cut_selection(); break;
    case M_COPY: copy_selection(); break;
    case M_PASTE: paste_selection(); break;
    case M_FIND: find_next_text(1); break;
    case M_FIND_NEXT: find_next_text(0); break;
    case M_REPLACE: replace_text_command(0); break;
    case M_REPLACE_ALL: replace_text_command(1); break;
    case M_SELECT_ALL: select_all_text(); break;
    case M_STOP:
        stop_typing();
        draw_slide();
        break;
    case M_TEXT_BOLD: format_text_flags(OWF_BOLD, "Bold toggled for the selection or current paragraph."); break;
    case M_TEXT_ITALIC: format_text_flags(OWF_ITALIC, "Italic toggled for the selection or current paragraph."); break;
    case M_TEXT_UNDERLINE: format_text_flags(OWF_UNDERLINE, "Underline toggled for the selection or current paragraph."); break;
    case M_TEXT_SIZE_10: format_text_size(10); break;
    case M_TEXT_SIZE_12: format_text_size(12); break;
    case M_TEXT_SIZE_14: format_text_size(14); break;
    case M_TEXT_SIZE_18: format_text_size(18); break;
    case M_TEXT_SIZE_24: format_text_size(24); break;
    case M_TEXT_SIZE_36: format_text_size(36); break;
    case M_TEXT_FONT: format_text_font(); break;
    case M_TEXT_COLOUR: format_text_colour(); break;
    case M_TEXT_ALIGN_LEFT: format_text_alignment(OWF_ALIGN_LEFT, "Current paragraph aligned left."); break;
    case M_TEXT_ALIGN_CENTRE: format_text_alignment(OWF_ALIGN_CENTRE, "Current paragraph centred."); break;
    case M_TEXT_ALIGN_RIGHT: format_text_alignment(OWF_ALIGN_RIGHT, "Current paragraph aligned right."); break;
    case M_TEXT_ALIGN_JUSTIFY: format_text_alignment(OWF_ALIGN_JUSTIFY, "Current paragraph justified."); break;
    case M_TEXT_COLUMNS_1: set_selected_text_columns(1); break;
    case M_TEXT_COLUMNS_2: set_selected_text_columns(2); break;
    case M_TEXT_COLUMNS_3: set_selected_text_columns(3); break;
    case M_TEXT_FRAME: edit_selected_text_frame(); break;
    case M_INSERT_TEXTBOX: insert_textbox(); break;
    case M_INSERT_PICTURE: insert_picture(); break;
    case M_INSERT_RECT: insert_shape(OWF_SHAPE_RECT); break;
    case M_INSERT_ROUNDRECT: insert_shape(OWF_SHAPE_ROUNDRECT); break;
    case M_INSERT_ELLIPSE: insert_shape(OWF_SHAPE_ELLIPSE); break;
    case M_INSERT_TRIANGLE: insert_shape(OWF_SHAPE_TRIANGLE); break;
    case M_INSERT_DIAMOND: insert_shape(OWF_SHAPE_DIAMOND); break;
    case M_INSERT_ARROW: insert_shape(OWF_SHAPE_ARROW); break;
    case M_INSERT_LINE: insert_shape(OWF_SHAPE_LINE); break;
    case M_ROTATE_LEFT: rotate_selected(150); break;
    case M_ROTATE_RIGHT: rotate_selected(-150); break;
    case M_BRING_FRONT: arrange_selected_object(OPR_ARRANGE_FRONT, "Object brought to front."); break;
    case M_BRING_FORWARD: arrange_selected_object(OPR_ARRANGE_FORWARD, "Object brought forward."); break;
    case M_SEND_BACKWARD: arrange_selected_object(OPR_ARRANGE_BACKWARD, "Object sent backward."); break;
    case M_SEND_BACK: arrange_selected_object(OPR_ARRANGE_BACK, "Object sent to back."); break;
    case M_ALIGN_LEFT: align_selected_object(OPR_ALIGN_LEFT, "Object aligned left."); break;
    case M_ALIGN_CENTRE: align_selected_object(OPR_ALIGN_CENTRE, "Object centred horizontally."); break;
    case M_ALIGN_RIGHT: align_selected_object(OPR_ALIGN_RIGHT, "Object aligned right."); break;
    case M_ALIGN_TOP: align_selected_object(OPR_ALIGN_TOP, "Object aligned top."); break;
    case M_ALIGN_MIDDLE: align_selected_object(OPR_ALIGN_MIDDLE, "Object centred vertically."); break;
    case M_ALIGN_BOTTOM: align_selected_object(OPR_ALIGN_BOTTOM, "Object aligned bottom."); break;
    case M_POSITION_SIZE: edit_selected_geometry(); break;
    case M_APPEARANCE: edit_selected_appearance(); break;
    case M_PICTURE_CROP: edit_selected_picture_crop(); break;
#ifdef OPENPUBLISHER
    case M_WRAP_NONE: set_selected_wrap(OWF_WRAP_NONE); break;
    case M_WRAP_SQUARE: set_selected_wrap(OWF_WRAP_SQUARE); break;
    case M_WRAP_TIGHT: set_selected_wrap(OWF_WRAP_TIGHT); break;
    case M_WRAP_TOP_BOTTOM: set_selected_wrap(OWF_WRAP_TOP_BOTTOM); break;
    case M_WRAP_DISTANCE: edit_selected_wrap_distance(); break;
    case M_LINK_TEXT_NEXT: link_selected_text_frame(); break;
    case M_UNLINK_TEXT: unlink_selected_text_frame(); break;
#endif
    case M_DUPLICATE_OBJECT: duplicate_selected_object(); break;
    case M_DELETE_OBJECT: delete_selected_object(); break;
    case M_NEW_SLIDE: new_slide(); break;
    case M_DUPLICATE_SLIDE: duplicate_current_slide(); break;
    case M_RENAME_SLIDE: rename_current_slide(); break;
    case M_DELETE_SLIDE: delete_slide(); break;
    case M_HIDE_SLIDE: set_current_hidden(1); break;
    case M_SHOW_SLIDE: set_current_hidden(0); break;
    case M_TRANSITION: edit_current_transition(); break;
    case M_MOVE_SLIDE_UP: move_slide(-1); break;
    case M_MOVE_SLIDE_DOWN: move_slide(1); break;
    case M_FIRST: show_slide(0); break;
    case M_PREV: show_slide(current - 1); break;
    case M_NEXT: show_slide(current + 1); break;
    case M_LAST: show_slide(deck ? deck->nslides - 1 : 0); break;
    case M_ZOOM_IN: zoom_step(1); break;
    case M_ZOOM_OUT: zoom_step(-1); break;
    case M_ZOOM_FIT: set_zoom(100); break;
    case M_SHOW_GRID:
        show_grid = !show_grid;
        set_status(show_grid ? "Half-inch grid shown." : "Grid hidden.");
        draw_slide(); draw_status();
        break;
    case M_SNAP_GRID:
        snap_grid = !snap_grid;
        set_status(snap_grid ? "Move and resize snap to the half-inch grid." : "Grid snapping off.");
        draw_status();
        break;
    case M_TB_BOTH: set_buttons(OGT_TB_ICONS_TEXT); break;
    case M_TB_ICONS: set_buttons(OGT_TB_ICONS); break;
    case M_TB_TEXT: set_buttons(OGT_TB_TEXT); break;
    case M_SHOW_FIRST: run_show(0, 0); break;
    case M_SHOW_CURRENT: run_show(current, 0); break;
    case M_SHOW_LOOP: run_show(0, 1); break;
    }
}

static void tool_command(int id)
{
    switch (id) {
    case C_NEW: menu_action(M_NEW); break;
    case C_OPEN: menu_action(M_OPEN); break;
    case C_SAVE: menu_action(M_SAVE); break;
    case C_PRINT: menu_action(M_PRINT); break;
    case C_PDF: menu_action(M_PDF); break;
    case C_UNDO: menu_action(M_UNDO); break;
    case C_REDO: menu_action(M_REDO); break;
    }
}

static void toolbar_event(ULONG class, struct Gadget *g)
{
    int i = ogt_toolbar_index(&tb, g->GadgetID);
    if (i < 0) return;
    if (class == IDCMP_GADGETDOWN) {
        tb.pressed = i;
        ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 1, "window");
        return;
    }
    ogt_toolbar_draw_one(&tb, &ctx, win->RPort, i, 0, "window");
    tb.pressed = -1;
    if (!tb.tool[i].disabled) tool_command(tb.tool[i].id);
}

/* A typed character as UTF-8 (the keymap gives ISO-8859-1). */
static int vanilla_utf8(UWORD code, char out[3])
{
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    out[0] = (char)(0xC0 | (code >> 6));
    out[1] = (char)(0x80 | (code & 0x3F));
    return 2;
}

static void events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class, secs = im->Seconds, micros = im->Micros;
        UWORD code = im->Code, qual = im->Qualifier;
        APTR ia = im->IAddress;
        int mx = im->MouseX, my = im->MouseY, row = -1, r, handled = 0;
        struct Gadget *g = (struct Gadget *)ia;
        if (strip) {
            r = ogt_list_event(strip, class, code, ia, mx, my, secs, micros, &row);
            if (r == OGT_LIST_PICKED || r == OGT_LIST_OPENED) { show_slide(row); handled = 1; }
            else if (r == OGT_LIST_SCROLLED) handled = 1;
        }
        if (!handled && (edit_obj >= 0 || ruler.dragging) &&
            (class == IDCMP_MOUSEBUTTONS || class == IDCMP_MOUSEMOVE)) {
            int was_dragging = ruler.dragging;
            r = ogt_ruler_event(&ruler, class, code, mx, my);
            if (was_dragging || ruler.dragging || r != OGT_RULER_NONE) handled = 1;
            if (r != OGT_RULER_NONE) apply_ruler_change(r);
        }
        GT_ReplyIMsg(im);
        if (handled) continue;
        switch (class) {
        case IDCMP_CLOSEWINDOW:
            if (confirm_discard()) quit_now = 1;
            break;
        case IDCMP_NEWSIZE:
            relayout();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            draw_all();
            GT_EndRefresh(win, TRUE);
            break;
        case IDCMP_GADGETDOWN:
        case IDCMP_GADGETUP:
            if (g && (g == hscroll || g == vscroll)) scrolled();
            else if (g && g->GadgetID >= GID_TOOLBAR && g->GadgetID < GID_SLIDES) toolbar_event(class, g);
            break;
        case IDCMP_IDCMPUPDATE:
            scrolled();
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN) slide_mouse_down(mx, my);
            else if (code == SELECTUP) {
                if (text_selecting) {
                    drag_text_selection(mx, my);
                    text_selecting = 0;
                    set_status(opr_text_selection_empty(&text_selection) ?
                               "Typing: Esc stops." : "Text selected; type to replace or choose a format.");
                    draw_status();
                } else if (object_drag.active) finish_object_drag(mx, my);
                if (tb.pressed >= 0) {
                    ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                    tb.pressed = -1;
                }
            }
            break;
        case IDCMP_MOUSEMOVE:
            if (text_selecting) drag_text_selection(mx, my);
            else if (object_drag.active) preview_object_drag(mx, my);
            break;
        case IDCMP_RAWKEY:
            if (code & 0x80) break;
            if (code == 0x54) {                 /* F5; Shift-F5 starts here */
                run_show(qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT) ? current : 0, 0);
            } else if (edit_obj >= 0) {
                int extend = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
                switch (code) {
                case 0x4f: opr_text_move(deck, current, edit_obj, &caret, OPR_MOVE_LEFT); caret_moved(extend); break;
                case 0x4e: opr_text_move(deck, current, edit_obj, &caret, OPR_MOVE_RIGHT); caret_moved(extend); break;
                case 0x4c: caret_line(-1, extend); break;
                case 0x4d: caret_line(1, extend); break;
                }                               /* Del comes as a vanilla key (127) */
            } else if (selected_obj >= 0 &&
                       (code == 0x4c || code == 0x4d || code == 0x4e || code == 0x4f)) {
                int pixels = qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT) ? 10 : 1;
                nudge_selected(code == 0x4f ? -1 : code == 0x4e ? 1 : 0,
                               code == 0x4c ? -1 : code == 0x4d ? 1 : 0, pixels);
            } else if (code == 0x4c || code == 0x4f) {
                show_slide(current - 1);     /* up, left, or with Shift while typing: the slide before */
            } else if (code == 0x4d || code == 0x4e) {
                show_slide(current + 1);
            }
            break;
        case IDCMP_VANILLAKEY:
            if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) break;
            if (code == 27) {
                if (edit_obj >= 0) {
                    box b;
                    int had = edit_area(&b);
                    stop_typing();
                    set_status("Ready.");
                    if (had) draw_slide_part(&b);
                    draw_status();
                } else if (selected_obj >= 0) {
                    selected_obj = -1;
                    set_status("Ready.");
                    draw_slide();
                    draw_status();
                }
                break;
            }
            if (edit_obj < 0) {
                if (code == 127 && selected_obj >= 0) delete_selected_object();
                else if (code == ' ') show_slide(current + 1);
                else if (code == 8) show_slide(current - 1);
                else if (code == '+' || code == '=') zoom_step(1);
                else if (code == '-') zoom_step(-1);
                else if (code == '0') set_zoom(100);
                break;
            }
            if (code == 8 || code == 127 || code == 13) type_key(code);
            else if (code == 9 || (code >= 32 && code < 256 && code != 127)) {
                char u[3];
                int n = vanilla_utf8(code, u);
                type_text(u, (size_t)n);
            }
            break;
        case IDCMP_MENUPICK:
            while (code != MENUNULL && win) {
                struct MenuItem *item = ItemAddress(menu, code);
                if (!item) break;
                menu_action((ULONG)GTMENUITEM_USERDATA(item));
                code = item->NextSelect;
            }
            break;
        }
    }
}

/* ---- Opening and closing ---- */

/* The part of the screen a window may have: below the title bar, less GFOpenDock's strip when it publishes
 * one (ENV:GFOpenDock/Free), as GFOpenWrite's free_area. */
static void free_area(int *l, int *t, int *w, int *h)
{
    char buf[48];
    int a, b, c, d;
    LONG got = GetVar((STRPTR)"GFOpenDock/Free", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (got > 0 && sscanf(buf, "%d %d %d %d", &a, &b, &c, &d) == 4 && c >= 400 && d >= 200 && a >= 0 && b >= 0 &&
        a + c <= scr->Width && b + d <= scr->Height) {
        *l = a;
        *t = b < scr->BarHeight + 1 ? scr->BarHeight + 1 : b;
        *w = c;
        *h = b + d - *t;
        return;
    }
    *l = 0;
    *t = scr->BarHeight + 1;
    *w = scr->Width;
    *h = scr->Height - *t;
}

static WORD zoom_box[4];

static int open_window(void)
{
    int l, t, w, h, aw, ah;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    load_theme();
    if (!(font = ogt_open_font(&theme, scr, &font_attr))) return 0;
    fh = font->tf_YSize;
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    free_area(&l, &t, &aw, &ah);
    /* 800 x 600 to start (the GFOpen apps' rule), centred, never bigger than the screen allows */
    w = aw < 800 ? aw : 800;
    h = ah < 600 ? ah : 600;
    /* the zoom gadget: the whole free area, and back */
    zoom_box[0] = (WORD)l;
    zoom_box[1] = (WORD)t;
    zoom_box[2] = (WORD)aw;
    zoom_box[3] = (WORD)ah;
    l += (aw - w) / 2;
    t += (ah - h) / 2;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)PROGRAM,
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Left, l, WA_Top, t, WA_Width, w, WA_Height, h,
        WA_MinWidth, w < 400 ? w : 400, WA_MinHeight, h < 280 ? h : 280, WA_Zoom, (ULONG)zoom_box,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                  IDCMP_IDCMPUPDATE,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    ogt_ruler_init(&ruler, win, &ctx);
    update_window_title();
    relayout();
    return 1;
}

static void close_window(void)
{
    font_cache_clear();
    if (win) {
        ClearMenuStrip(win);
        free_gadgets();
        CloseWindow(win);
        win = NULL;
    }
    ogt_ctx_free(&ctx);
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    own_pen_free();
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int window_main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)PROGRAM " needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    OpenFontTextBase = ogt_openfont_open();
    LayersBase = OpenLibrary((STRPTR)"layers.library", 36);
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
    ogt_iconset_attach();   /* the toolbar's pictures from the Chrome set (Images/Toolbar/) */
    read_theme_choice();
    undo = opr_undo_new();
    if (argc > 1) snprintf(first, sizeof first, "%s", argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    {
        owf_deck *d = owf_deck_new(
#ifdef OPENPUBLISHER
                                      11906, 16838
#else
                                      OWF_DECK_16_9_WIDTH, OWF_DECK_16_9_HEIGHT
#endif
                                      );
        if (!d || !owf_deck_add_slide(d, -1, 1) || !undo) {
            owf_deck_free(d);
            rc = 20;
            goto out;
        }
        set_deck(d, "", "ODP");
    }
    if (!open_window()) {
        if (argc) PutStr((STRPTR)PROGRAM ": the window could not open\n");
        rc = 20;
        goto out;
    }
    if (first[0]) {
        open_deck(first);
        relayout();
    }
    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(sig | SIGBREAKF_CTRL_C);
        if ((got & SIGBREAKF_CTRL_C) && confirm_discard()) quit_now = 1;
        if (got & sig) events();
    }
out:
    close_window();
    laid_clear();
    pictures_clear();
    free(versions);
    if (deck) owf_deck_free(deck);
    opr_undo_free(undo);
    if (theme.text) ogt_theme_free(&theme);
    if (LayersBase) CloseLibrary(LayersBase);
    if (OpenFontTextBase) CloseLibrary(OpenFontTextBase);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}

int main(int argc, char **argv)
{
    return ow_run_with_stack(65536, window_main, argc, argv);
}
