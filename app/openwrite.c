/*
 * OpenWrite - the native OpenAmigaWriter shell.
 *
 * First build: OpenGadTools chrome, libowf documents, a lightweight native
 * page canvas, DOCX/ODT/classic-Amiga Open/Save, navigator, inspector and
 * status line.  OpenRTG is used by OpenGadTools automatically on true-colour
 * screens; the page renderer remains portable to AGA.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <graphics/text.h>
#include <graphics/gfx.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/rexxsyslib.h>
#include <graphics/regions.h>
#include <devices/inputevent.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/datatypes.h>
#include <proto/cybergraphics.h>
#include <devices/clipboard.h>
#include <exec/io.h>
#include <exec/ports.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/layers.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <clib/alib_protos.h>

#include "openwrite_core.h"
#include "ow_stack.h"
#include "ow_print.h"
#include "ow_spell.h"
#include "ow_autosave.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_font.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *DiskfontBase = NULL, *LayersBase = NULL;
struct Library *DataTypesBase = NULL, *CyberGfxBase = NULL;
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "OpenWrite 1.0.1 (7.10.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " MIT, Copyright (c) 2026 Dalsin Limited";

enum {
    C_NEW = 1, C_OPEN, C_SAVE, C_PRINT, C_UNDO, C_REDO,
    C_IMAGE, C_TABLE, C_PDF
};
enum { GID_TOOLBAR = 100 };

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_PRINT, M_PDF, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_SELECT_ALL,
    M_FIND, M_FIND_NEXT, M_FIND_PREV, M_REPLACE,
    M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_100, M_ZOOM_FIT_PAGE, M_ZOOM_FIT_WIDTH,
    M_PAGE_FIRST, M_PAGE_PREV, M_PAGE_NEXT, M_PAGE_LAST,
    M_NAV, M_INSPECTOR,
    M_IMAGE, M_TABLE, M_LINK, M_PAGE_BREAK, M_FIELD_PAGE, M_FIELD_PAGES, M_FIELD_DATE, M_FIELD_TIME,
    M_PARAGRAPH, M_BOLD, M_ITALIC, M_UNDERLINE,
    M_TABLE_NEXT, M_TABLE_PREV, M_TABLE_ROW_ADD, M_TABLE_ROW_DEL, M_TABLE_COL_ADD, M_TABLE_COL_DEL,
    M_ALIGN_LEFT, M_ALIGN_CENTRE, M_ALIGN_RIGHT, M_ALIGN_JUSTIFY,
    M_STYLE_BODY, M_STYLE_H1, M_STYLE_H2, M_STYLE_H3, M_BULLETS, M_NUMBERING,
    M_PAGE_SETUP, M_HEADER, M_FOOTER,
    M_DATATYPES, M_OPENPRINT, M_SPELL,
    M_THEME_OPEN, M_THEME_GRAPHITE, M_THEME_EMBER, M_THEME_CLEAR, M_THEME_CLASSIC,
    M_TB_BOTH, M_TB_ICONS, M_TB_TEXT
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
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Cut", "X", 0, 0, (APTR)M_CUT },
    { NM_ITEM, "Copy", "C", 0, 0, (APTR)M_COPY },
    { NM_ITEM, "Paste", "V", 0, 0, (APTR)M_PASTE },
    { NM_ITEM, "Select All", "A", 0, 0, (APTR)M_SELECT_ALL },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Find...", "F", 0, 0, (APTR)M_FIND },
    { NM_ITEM, "Find Again", "G", 0, 0, (APTR)M_FIND_NEXT },
    { NM_ITEM, "Find Previous", NULL, 0, 0, (APTR)M_FIND_PREV },
    { NM_ITEM, "Replace...", NULL, 0, 0, (APTR)M_REPLACE },

    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "First Page", NULL, 0, 0, (APTR)M_PAGE_FIRST },
    { NM_ITEM, "Previous Page", "[", 0, 0, (APTR)M_PAGE_PREV },
    { NM_ITEM, "Next Page", "]", 0, 0, (APTR)M_PAGE_NEXT },
    { NM_ITEM, "Last Page", NULL, 0, 0, (APTR)M_PAGE_LAST },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Navigator", NULL, CHECKIT | CHECKED, 0, (APTR)M_NAV },
    { NM_ITEM, "Inspector", NULL, CHECKIT | CHECKED, 0, (APTR)M_INSPECTOR },
    { NM_ITEM, "Zoom in", "+", 0, 0, (APTR)M_ZOOM_IN },
    { NM_ITEM, "Zoom out", "-", 0, 0, (APTR)M_ZOOM_OUT },
    { NM_ITEM, "Actual size (100%)", "0", 0, 0, (APTR)M_ZOOM_100 },
    { NM_ITEM, "Fit Page", NULL, 0, 0, (APTR)M_ZOOM_FIT_PAGE },
    { NM_ITEM, "Fit Width", NULL, 0, 0, (APTR)M_ZOOM_FIT_WIDTH },
    { NM_ITEM, "Toolbar", NULL, 0, 0, NULL },
    { NM_SUB, "Icons and text", NULL, CHECKIT | CHECKED, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons only", NULL, CHECKIT, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text only", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },
    { NM_ITEM, "Theme", NULL, 0, 0, NULL },
    { NM_SUB, "Open", NULL, 0, 0, (APTR)M_THEME_OPEN },
    { NM_SUB, "Graphite", NULL, 0, 0, (APTR)M_THEME_GRAPHITE },
    { NM_SUB, "Ember", NULL, 0, 0, (APTR)M_THEME_EMBER },
    { NM_SUB, "Clear", NULL, 0, 0, (APTR)M_THEME_CLEAR },
    { NM_SUB, "Classic", NULL, 0, 0, (APTR)M_THEME_CLASSIC },

    { NM_TITLE, "Insert", NULL, 0, 0, NULL },
    { NM_ITEM, "Image...", NULL, 0, 0, (APTR)M_IMAGE },
    { NM_ITEM, "Table...", NULL, 0, 0, (APTR)M_TABLE },
    { NM_ITEM, "Link...", NULL, 0, 0, (APTR)M_LINK },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Page Break", NULL, 0, 0, (APTR)M_PAGE_BREAK },
    { NM_ITEM, "Field", NULL, 0, 0, NULL },
    { NM_SUB, "Page Number", NULL, 0, 0, (APTR)M_FIELD_PAGE },
    { NM_SUB, "Page Count", NULL, 0, 0, (APTR)M_FIELD_PAGES },
    { NM_SUB, "Date", NULL, 0, 0, (APTR)M_FIELD_DATE },
    { NM_SUB, "Time", NULL, 0, 0, (APTR)M_FIELD_TIME },

    { NM_TITLE, "Format", NULL, 0, 0, NULL },
    { NM_ITEM, "Bold", "B", 0, 0, (APTR)M_BOLD },
    { NM_ITEM, "Italic", "I", 0, 0, (APTR)M_ITALIC },
    { NM_ITEM, "Underline", "U", 0, 0, (APTR)M_UNDERLINE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Style", NULL, 0, 0, NULL },
    { NM_SUB, "Body Text", NULL, 0, 0, (APTR)M_STYLE_BODY },
    { NM_SUB, "Heading 1", NULL, 0, 0, (APTR)M_STYLE_H1 },
    { NM_SUB, "Heading 2", NULL, 0, 0, (APTR)M_STYLE_H2 },
    { NM_SUB, "Heading 3", NULL, 0, 0, (APTR)M_STYLE_H3 },
    { NM_ITEM, "Alignment", NULL, 0, 0, NULL },
    { NM_SUB, "Left", NULL, 0, 0, (APTR)M_ALIGN_LEFT },
    { NM_SUB, "Centre", NULL, 0, 0, (APTR)M_ALIGN_CENTRE },
    { NM_SUB, "Right", NULL, 0, 0, (APTR)M_ALIGN_RIGHT },
    { NM_SUB, "Justify", NULL, 0, 0, (APTR)M_ALIGN_JUSTIFY },
    { NM_ITEM, "Bullets", NULL, 0, 0, (APTR)M_BULLETS },
    { NM_ITEM, "Numbering", NULL, 0, 0, (APTR)M_NUMBERING },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Paragraph...", NULL, 0, 0, (APTR)M_PARAGRAPH },

    { NM_TITLE, "Table", NULL, 0, 0, NULL },
    { NM_ITEM, "Next Cell", NULL, 0, 0, (APTR)M_TABLE_NEXT },
    { NM_ITEM, "Previous Cell", NULL, 0, 0, (APTR)M_TABLE_PREV },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Insert Row Below", NULL, 0, 0, (APTR)M_TABLE_ROW_ADD },
    { NM_ITEM, "Delete Row", NULL, 0, 0, (APTR)M_TABLE_ROW_DEL },
    { NM_ITEM, "Insert Column Right", NULL, 0, 0, (APTR)M_TABLE_COL_ADD },
    { NM_ITEM, "Delete Column", NULL, 0, 0, (APTR)M_TABLE_COL_DEL },

    { NM_TITLE, "Layout", NULL, 0, 0, NULL },
    { NM_ITEM, "Page Setup...", NULL, 0, 0, (APTR)M_PAGE_SETUP },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Header...", NULL, 0, 0, (APTR)M_HEADER },
    { NM_ITEM, "Footer...", NULL, 0, 0, (APTR)M_FOOTER },

    { NM_TITLE, "Datatypes", NULL, 0, 0, NULL },
    { NM_ITEM, "OpenDatatypes...", NULL, 0, 0, (APTR)M_DATATYPES },

    { NM_TITLE, "Tools", NULL, 0, 0, NULL },
    { NM_ITEM, "Spell Check...", NULL, 0, 0, (APTR)M_SPELL },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "OpenPrint...", NULL, 0, 0, (APTR)M_OPENPRINT },

    { NM_TITLE, "Help", NULL, 0, 0, NULL },
    { NM_ITEM, "About OpenWrite...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static const ogt_tool tools[] = {
    { C_NEW,   "New",    OGT_ICON_FILE,    0, 0, 0 },
    { C_OPEN,  "Open...",OGT_ICON_OPEN,    0, 0, 0 },
    { C_SAVE,  "Save",   OGT_ICON_DISK,    0, 0, 0 },
    { C_PRINT, "Print...",OGT_ICON_TEST,    1, 0, 0 },
    { C_UNDO,  "Undo",   OGT_ICON_PARENT,  1, 1, 0 },
    { C_REDO,  "Redo",   OGT_ICON_FORWARD, 0, 1, 0 },
    { C_IMAGE, "Image...",OGT_ICON_FILE,    1, 0, 0 },
    { C_TABLE, "Table...",OGT_ICON_FOLDER,  0, 0, 0 },
    { C_PDF,   "PDF...",  OGT_ICON_SENT,    1, 0, 0 }
};

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;

#define DOC_FONT_CACHE 16
typedef struct doc_font_slot {
    char file[72];
    int pixels;
    struct TextFont *font;
} doc_font_slot;
static doc_font_slot doc_fonts[DOC_FONT_CACHE];
static int quit_now;

static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS_TEXT;

static owf_doc *doc;
static ow_editor *editor;
static ow_spell *spell;
static ow_autosave *autosave;
static struct MsgPort *rexx_port;
#define REXX_PORT_NAME "OPENWRITE"
#define RECOVERY_FILE "T:OpenWrite-Recovery.odt"
#define RECOVERY_META "T:OpenWrite-Recovery.meta"
static char current_path[512];
static char current_format[32] = "ODT";
static char status[256] = "Ready.";
static char last_find[160] = "", last_replace[160] = "";
static int zoom = 100;
static int show_nav = 1, show_inspector = 1;
static int page_index;

static box area, toolbar_box, format_box, navigator_box, canvas_box;
static box ruler_box, page_box, page_view_box, inspector_box, status_box;
static int page_full_w, page_full_h, page_max_scroll_x, page_max_scroll_y;
static int scroll_x, scroll_y;
static box fmt_style_box, fmt_font_box, fmt_size_box, fmt_button_box[9];
static box nav_page_box[4];
static int nav_page_number[4], nav_page_slots;

#define HIT_MAX 1024
typedef struct hit_span {
    int paragraph, run;
    size_t start, end;
    int x, y, w, h;
} hit_span;

static int render_x0, render_y0, render_num = 1, render_den = 1;
static int render_bottom, render_para = -1;
static int render_rule_bottom, render_tbl_id = -1, render_tbl_row = -1, render_row_y, render_para_prev = -1, render_cell_col = -1, render_cell_bottom, render_shift;
static int render_line_x, render_line_y, render_left_x, render_max_x, render_line_h;
static hit_span hits[HIT_MAX];
static int hit_count;
typedef struct { int index; UBYTE *argb; UWORD w,h; } ow_image_cache;
static ow_image_cache image_cache[16];
static int image_cache_n;
static int mouse_selecting;
static int defer_repaint, repaint_pending;

static void draw_all(void);
static void redraw_document_area(void);
static void request_document_redraw(void);
static void draw_status(void);
static void relayout(void);
static void menu_action(ULONG id);
static void doc_font_cache_clear(void);
static void image_cache_clear(void);
static void editor_repaint(const char *message);
static int editor_result(int rc, const char *message);

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

#define OW_ID(a,b,c,d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))
#define OW_ID_FORM OW_ID('F','O','R','M')
#define OW_ID_FTXT OW_ID('F','T','X','T')
#define OW_ID_CHRS OW_ID('C','H','R','S')
#define OW_MAX_CLIP (1UL << 20)

typedef struct ow_clipboard {
    struct MsgPort *port;
    struct IOClipReq *io;
} ow_clipboard;

static int clipboard_open(ow_clipboard *c)
{
    memset(c, 0, sizeof(*c));
    c->port = CreateMsgPort();
    if (!c->port) return 0;
    c->io = (struct IOClipReq *)CreateIORequest(c->port, sizeof(*c->io));
    if (!c->io) return 0;
    if (OpenDevice((STRPTR)"clipboard.device", PRIMARY_CLIP,
                   (struct IORequest *)c->io, 0)) {
        c->io->io_Device = NULL;
        return 0;
    }
    return 1;
}

static void clipboard_close(ow_clipboard *c)
{
    if (!c) return;
    if (c->io) {
        if (c->io->io_Device) CloseDevice((struct IORequest *)c->io);
        DeleteIORequest((struct IORequest *)c->io);
    }
    if (c->port) DeleteMsgPort(c->port);
    memset(c, 0, sizeof(*c));
}

static LONG clipboard_read_bytes(ow_clipboard *c, void *buf, LONG len)
{
    c->io->io_Command = CMD_READ;
    c->io->io_Data = (STRPTR)buf;
    c->io->io_Length = len;
    DoIO((struct IORequest *)c->io);
    return c->io->io_Error ? -1 : (LONG)c->io->io_Actual;
}

static void clipboard_read_done(ow_clipboard *c)
{
    UBYTE dummy[64];
    while (clipboard_read_bytes(c, dummy, sizeof dummy) > 0) ;
}

static char *clipboard_get_text(size_t *out_len)
{
    ow_clipboard c;
    ULONG head[3], chunk[2], total = 0;
    char *text = NULL;
    if (out_len) *out_len = 0;
    if (!clipboard_open(&c)) { clipboard_close(&c); return NULL; }
    c.io->io_Offset = 0;
    c.io->io_ClipID = 0;
    if (clipboard_read_bytes(&c, head, 12) != 12 || head[0] != OW_ID_FORM ||
        head[2] != OW_ID_FTXT || head[1] > OW_MAX_CLIP + 64) {
        clipboard_read_done(&c);
        clipboard_close(&c);
        return NULL;
    }
    text = (char *)malloc((size_t)head[1] + 1);
    if (!text) { clipboard_read_done(&c); clipboard_close(&c); return NULL; }
    while (clipboard_read_bytes(&c, chunk, 8) == 8) {
        ULONG size = chunk[1], padded = size + (size & 1);
        if (chunk[0] == OW_ID_CHRS && total + size <= head[1]) {
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
    text[total] = 0;
    if (out_len) *out_len = total;
    return text;
}

static int clipboard_set_text(const char *text, size_t len)
{
    ow_clipboard c;
    ULONG form[3], chunk[2];
    UBYTE pad = 0;
    if (!text || len > OW_MAX_CLIP) return 0;
    if (!clipboard_open(&c)) { clipboard_close(&c); return 0; }
    form[0] = OW_ID_FORM;
    form[1] = 4 + 8 + (ULONG)len + ((ULONG)len & 1);
    form[2] = OW_ID_FTXT;
    chunk[0] = OW_ID_CHRS;
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
    return 1;
write_fail:
#undef CLIP_WRITE
    clipboard_close(&c);
    return 0;
}

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
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)"OK"
    };
    EasyRequest(win, &es, NULL, (ULONG)text);
}

static void set_status(const char *s)
{
    snprintf(status, sizeof status, "%s", s ? s : "");
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

static void field(struct RastPort *rp, const char *fill, int x, int y, int w, int h)
{
    if (w < 3 || h < 3) return;
    ogt_fill(&ctx, rp, fill, x + 1, y + 1, w - 2, h - 2);
    ogt_bevel(rp, ogt_pen(&ctx, "string.shadow"), ogt_pen(&ctx, "string.shine"),
              x, y, w, h);
}

static void raised(struct RastPort *rp, int x, int y, int w, int h)
{
    ogt_fill(&ctx, rp, "button", x + 1, y + 1, w - 2, h - 2);
    ogt_bevel(rp, ogt_pen(&ctx, "button.shine"), ogt_pen(&ctx, "button.shadow"),
              x, y, w, h);
}

static void format_button_draw(struct RastPort *rp, const box *b,
                               const char *label, int active, int bold)
{
    if (active) {
        ogt_box(rp, ogt_pen(&ctx, "selection.inactive"), b->x + 1, b->y + 1,
                b->w - 2, b->h - 2);
        ogt_frame(rp, ogt_pen(&ctx, "accent"), b->x, b->y, b->w, b->h);
    } else raised(rp, b->x, b->y, b->w, b->h);
    if (bold) ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "label"),
             b->x + (b->w - ogt_text_width(rp, label)) / 2,
             b->y + (b->h - fh) / 2, label, b->w - 4);
    if (bold) ogt_bold(rp, 0);
}

static owf_doc *blank_document(void)
{
    return owf_doc_new();
}


static void update_window_title(void)
{
    char title[256];
    const char *name = current_path[0] ? leaf(current_path) : "Untitled";
    snprintf(title, sizeof title, "%s%s - OpenWrite",
             editor && ow_editor_is_dirty(editor) ? "*" : "", name);
    if (win) SetWindowTitles(win, (STRPTR)title, (STRPTR)-1);
}

static void update_edit_tools(void)
{
    if (!editor) return;
    ogt_toolbar_enable(&tb, C_UNDO, ow_editor_can_undo(editor));
    ogt_toolbar_enable(&tb, C_REDO, ow_editor_can_redo(editor));
}

static void image_cache_clear(void)
{
    int i;for(i=0;i<image_cache_n;++i)free(image_cache[i].argb);memset(image_cache,0,sizeof image_cache);image_cache_n=0;
}

static ow_image_cache *image_cache_get(int index)
{
    int i;char tmp[96];BPTR f;Object *o=NULL;struct BitMapHeader *bmh=NULL;ow_image_cache *c;const owf_image *im;
    if(!doc||index<0||index>=doc->nimages||!DataTypesBase)return NULL;
    for(i=0;i<image_cache_n;++i)if(image_cache[i].index==index)return &image_cache[i];
    if(image_cache_n>=16)return NULL;
    im=&doc->images[index];
    snprintf(tmp,sizeof tmp,"T:OpenWrite-Image-%ld-%d.tmp",(long)FindTask(NULL),index);
    f=Open((STRPTR)tmp,MODE_NEWFILE);if(!f)return NULL;if(Write(f,(APTR)im->data,(LONG)im->length)!=(LONG)im->length){Close(f);DeleteFile((STRPTR)tmp);return NULL;}Close(f);
    o=NewDTObject((APTR)tmp,DTA_GroupID,GID_PICTURE,PDTA_DestMode,PMODE_V43,PDTA_Remap,FALSE,TAG_DONE);if(!o){DeleteFile((STRPTR)tmp);return NULL;}
    GetDTAttrs(o,PDTA_BitMapHeader,(ULONG)&bmh,TAG_DONE);DoMethod(o,DTM_PROCLAYOUT,NULL,1);
    if(!bmh||!bmh->bmh_Width||!bmh->bmh_Height){DisposeDTObject(o);DeleteFile((STRPTR)tmp);return NULL;}
    c=&image_cache[image_cache_n];memset(c,0,sizeof(*c));c->index=index;c->w=bmh->bmh_Width;c->h=bmh->bmh_Height;c->argb=(UBYTE*)malloc((ULONG)c->w*c->h*4);
    if(!c->argb||!DoMethod(o,PDTM_READPIXELARRAY,(ULONG)c->argb,PBPAFMT_ARGB,c->w*4,0,0,c->w,c->h)){free(c->argb);memset(c,0,sizeof(*c));DisposeDTObject(o);DeleteFile((STRPTR)tmp);return NULL;}
    /* ScalePixelArray draws ARGB without blending: a picture with an alpha
     * channel (a PNG logo) showed black where it is see-through. Blend it
     * onto the paper (white) once here. A picture whose alpha bytes are all
     * zero has no alpha channel (some datatypes leave them so): kept as it is. */
    {
        ULONG k,n=(ULONG)c->w*c->h;int any=0,partial=0;
        for(k=0;k<n;++k){UBYTE a=c->argb[k*4];if(a)any=1;if(a!=255)partial=1;}
        if(any&&partial)for(k=0;k<n;++k){UBYTE *px=c->argb+k*4;unsigned a=px[0];
            if(a!=255){px[1]=(UBYTE)((px[1]*a+255*(255-a))/255);px[2]=(UBYTE)((px[2]*a+255*(255-a))/255);px[3]=(UBYTE)((px[3]*a+255*(255-a))/255);px[0]=255;}}
    }
    ++image_cache_n;DisposeDTObject(o);DeleteFile((STRPTR)tmp);return c;
}

static void set_document(owf_doc *newdoc, const char *path, const char *fmt)
{
    doc_font_cache_clear();
    image_cache_clear();
    if (editor) ow_editor_free(editor);
    if (doc) owf_doc_free(doc);
    doc = newdoc;
    editor = doc ? ow_editor_new(doc) : NULL;
    if (editor) {
        ow_editor_set_zoom(editor, zoom);
        ow_editor_layout(editor);
        ow_editor_mark_saved(editor);
    }
    page_index = 0;
    scroll_x = scroll_y = 0;
    snprintf(current_path, sizeof current_path, "%s", path ? path : "");
    snprintf(current_format, sizeof current_format, "%s", fmt && *fmt ? fmt : "ODT");
    update_window_title();
}

static void recovery_clear(void)
{
    DeleteFile((STRPTR)RECOVERY_FILE);
    DeleteFile((STRPTR)RECOVERY_META);
}

static void recovery_save(void)
{
    FILE *f;
    owf_report *report;
    int rc;
    if (!doc || !editor || !ow_editor_is_dirty(editor)) return;
    report = owf_report_new();
    rc = owf_export_file(doc, RECOVERY_FILE, "odt", report);
    owf_report_free(report);
    if (rc != OWF_OK) return;
    f = fopen(RECOVERY_META, "w");
    if (f) {
        fprintf(f, "%s\n", current_path[0] ? current_path : "Untitled");
        fclose(f);
    }
    set_status("Autosave recovery copy updated.");
    draw_status();
}

static int recovery_exists(void)
{
    BPTR lock = Lock((STRPTR)RECOVERY_FILE, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

static void recovery_offer(void)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)"OpenWrite Recovery",
        (UBYTE *)"OpenWrite found a recovery document from an interrupted session.\n\nRecover it?",
        (UBYTE *)"Recover|Discard"
    };
    owf_doc *recovered = NULL;
    owf_report *report;
    const owf_format *used = NULL;
    int answer, rc;
    if (!recovery_exists()) return;
    answer = EasyRequest(win, &es, NULL);
    if (!answer) { recovery_clear(); return; }
    report = owf_report_new();
    rc = owf_import_file(RECOVERY_FILE, "odt", &recovered, report, &used);
    owf_report_free(report);
    if (rc != OWF_OK || !recovered) {
        tell("OpenWrite Recovery", "The recovery copy could not be opened.");
        recovery_clear();
        return;
    }
    set_document(recovered, "", used ? used->name : "ODT");
    if (editor) editor_result(ow_editor_insert_utf8(editor, "", 0), NULL);
    /* Import creates a clean editor; mark it dirty so Save cannot silently
     * lose the recovered work. */
    if (editor) {
        ow_editor_insert_utf8(editor, " ", 1);
        ow_editor_backspace(editor);
    }
    set_status("Recovered document. Save it to keep the recovered work.");
    relayout();
}

static int ask_file(int save, char *out, size_t out_size)
{
    struct FileRequester *fr;
    int ok = 0;
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(save ? "Save OpenWrite document" : "Open document"),
        ASLFR_Window, (ULONG)win,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save ? TRUE : FALSE,
        ASLFR_InitialFile, (ULONG)(save ? (current_path[0] ? leaf(current_path) : "Untitled.odt") : ""),
        ASLFR_PositiveText, (ULONG)(save ? "Save" : "Open"),
        TAG_DONE);
    if (!fr) return 0;
    if (AslRequestTags(fr, TAG_DONE) && fr->fr_File[0]) {
        snprintf(out, out_size, "%s", fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

static void open_document(const char *path)
{
    owf_doc *newdoc = NULL;
    owf_report *report = owf_report_new();
    const owf_format *used = NULL;
    char msg[256];
    int rc = owf_import_file(path, NULL, &newdoc, report, &used);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "OpenWrite could not open the document.\n\n%s", owf_error_text(rc));
        tell("Open document", msg);
        owf_report_free(report);
        return;
    }
    set_document(newdoc, path, used ? used->name : "document");
    recovery_clear();
    snprintf(msg, sizeof msg, "%s opened%s%s.",
             leaf(path),
             report && owf_report_count(report) ? "; compatibility notes: " : "",
             report && owf_report_count(report) ? "see import report" : "");
    set_status(msg);
    owf_report_free(report);
}

static void save_document_as(const char *path)
{
    owf_report *report = owf_report_new();
    char msg[256];
    int rc;
    if (!doc) return;
    rc = owf_export_file(doc, path, NULL, report);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "OpenWrite could not save this document.\n\n%s", owf_error_text(rc));
        tell("Save document", msg);
        owf_report_free(report);
        return;
    }
    snprintf(current_path, sizeof current_path, "%s", path);
    {
        const owf_format *f = owf_format_for_filename(path);
        if (f) snprintf(current_format, sizeof current_format, "%s", f->name);
    }
    snprintf(msg, sizeof msg, "%s saved.", leaf(path));
    set_status(msg);
    if (editor) ow_editor_mark_saved(editor);
    recovery_clear();
    update_window_title();
    owf_report_free(report);
}

static void doc_font_cache_clear(void)
{
    int i;
    for (i = 0; i < DOC_FONT_CACHE; ++i) {
        if (doc_fonts[i].font) CloseFont(doc_fonts[i].font);
        memset(&doc_fonts[i], 0, sizeof doc_fonts[i]);
    }
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

static void document_font_file(int font_index, char *out, size_t out_size)
{
    const char *name = NULL;
    owf_font_kind kind = OWF_FONT_ANY;
    if (doc && font_index >= 0 && font_index < doc->nfonts) {
        name = doc->fonts[font_index].name;
        kind = doc->fonts[font_index].kind;
    }
    if (kind == OWF_FONT_MONO || name_has(name, "courier") || name_has(name, "mono") || name_has(name, "lettergothic"))
        snprintf(out, out_size, "%s", "courier.font");
    else if (kind == OWF_FONT_SERIF || name_has(name, "times") || name_has(name, "serif"))
        snprintf(out, out_size, "%s", "CGTimes.font");
    else if (name_has(name, "helvetica"))
        snprintf(out, out_size, "%s", "helvetica.font");
    else
        snprintf(out, out_size, "%s", "CGTriumvirate.font");
}

static int run_twips(int paragraph, const owf_charfmt *fmt)
{
    int twips = fmt && fmt->size ? fmt->size : (doc && doc->base.size ? doc->base.size : 240);
    int heading = 0;
    static const int heading_size[] = { 0, 360, 320, 280, 260, 240, 240 };
    if (doc && paragraph >= 0 && paragraph < doc->body.nparas)
        heading = doc->body.paras[paragraph].fmt.heading;
    if ((!fmt || !fmt->size) && heading >= 1 && heading <= 6 && twips < heading_size[heading])
        twips = heading_size[heading];
    return twips;
}

static struct TextFont *document_font(int paragraph, const owf_charfmt *fmt)
{
    char file[72];
    int pixels, i, empty = -1;
    struct TextAttr ta;
    struct TextFont *f;
    int font_index = fmt ? fmt->font : -1;
    if (font_index < 0 && doc) font_index = doc->base.font;
    document_font_file(font_index, file, sizeof file);
    pixels = run_twips(paragraph, fmt) * render_num / render_den;
    if (pixels < 8) pixels = 8;
    if (pixels > 72) pixels = 72;
    for (i = 0; i < DOC_FONT_CACHE; ++i) {
        if (doc_fonts[i].font && doc_fonts[i].pixels == pixels && !strcmp(doc_fonts[i].file, file))
            return doc_fonts[i].font;
        if (!doc_fonts[i].font && empty < 0) empty = i;
    }
    if (!DiskfontBase || empty < 0) return font;
    memset(&ta, 0, sizeof ta);
    ta.ta_Name = (STRPTR)file;
    ta.ta_YSize = (UWORD)pixels;
    ta.ta_Style = FS_NORMAL;
    /* An outline font (FONTS:name.otag) is made at the exact size asked
     * for; asked for as FPF_DESIGNED, diskfont returns a size made before
     * (CGTimes: 10 pixels for any size from 8 to 14). A bitmap font keeps
     * its designed sizes, since scaling one looks worse than the nearest. */
    {
        char otag[96];
        BPTR lk;
        int outline = 0;
        snprintf(otag, sizeof otag, "FONTS:%s", file);
        if (strlen(otag) > 5 && !strcmp(otag + strlen(otag) - 5, ".font")) {
            strcpy(otag + strlen(otag) - 5, ".otag");
            if ((lk = Lock((STRPTR)otag, ACCESS_READ))) { outline = 1; UnLock(lk); }
        }
        ta.ta_Flags = outline ? FPF_DISKFONT : FPF_DISKFONT | FPF_DESIGNED;
        f = OpenDiskFont(&ta);
        if (!f) {
            ta.ta_Flags = outline ? FPF_DISKFONT | FPF_DESIGNED : FPF_DISKFONT;
            f = OpenDiskFont(&ta);
        }
    }
    if (!f) return font;
    snprintf(doc_fonts[empty].file, sizeof doc_fonts[empty].file, "%s", file);
    doc_fonts[empty].pixels = pixels;
    doc_fonts[empty].font = f;
    return f;
}

static int text_width_n(struct RastPort *rp, const char *s, size_t n);

static int paragraph_single_line_width(struct RastPort *rp, int paragraph)
{
    const owf_para *p;
    int i, total = 0;
    if (!doc || paragraph < 0 || paragraph >= doc->body.nparas) return -1;
    p = &doc->body.paras[paragraph];
    for (i = 0; i < p->nruns; ++i) {
        const owf_run *r = &p->runs[i];
        if (r->kind == OWF_RUN_TEXT && r->text) {
            if (strchr(r->text, '\n')) { SetFont(rp, font); return -1; }
            SetFont(rp, document_font(paragraph, &r->fmt));
            total += text_width_n(rp, r->text, strlen(r->text));
        } else if (r->kind == OWF_RUN_TAB) total += 24;
        else if (r->kind == OWF_RUN_LINEBREAK) { SetFont(rp, font); return -1; }
    }
    SetFont(rp, font);
    return total;
}

static int word_count(void)
{
    int i, j, words = 0, inword = 0;
    if (!doc) return 0;
    for (i = 0; i < doc->body.nparas; ++i) {
        for (j = 0; j < doc->body.paras[i].nruns; ++j) {
            const char *s = doc->body.paras[i].runs[j].text;
            if (!s) continue;
            while (*s) {
                if (isspace((unsigned char)*s)) inword = 0;
                else if (!inword) { ++words; inword = 1; }
                ++s;
            }
            inword = 0;
        }
    }
    return words;
}

static int pos_compare(ow_position a, ow_position b)
{
    if (a.paragraph != b.paragraph) return a.paragraph < b.paragraph ? -1 : 1;
    if (a.run != b.run) return a.run < b.run ? -1 : 1;
    if (a.byte_offset != b.byte_offset) return a.byte_offset < b.byte_offset ? -1 : 1;
    return 0;
}

static size_t utf8_next_local(const char *s, size_t off, size_t len)
{
    size_t p = off < len ? off + 1 : len;
    while (p < len && (((unsigned char)s[p] & 0xc0) == 0x80)) ++p;
    return p;
}

/* The document is UTF-8; Amiga fonts are ISO-8859-1. Latin-1 code points
 * map to their byte; common typography outside it to the nearest Latin-1
 * (bullets to a middle dot, quotes and dashes to ASCII, an ellipsis to
 * three dots); anything else to '?'. Returns the bytes written. */
static size_t to_latin1(const char *s, size_t n, char *out, size_t cap)
{
    size_t i = 0, o = 0;
    if (!cap) return 0;
    while (i < n && s[i] && o + 1 < cap) {
        unsigned c = (unsigned char)s[i], cp;
        int len;
        if (c < 0x80) { out[o++] = (char)c; ++i; continue; }
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { out[o++] = '?'; ++i; continue; }
        if (i + (size_t)len > n) { out[o++] = '?'; break; }
        {
            int k;
            for (k = 1; k < len; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        }
        i += (size_t)len;
        if (cp >= 0xA0 && cp <= 0xFF) out[o++] = (char)cp;
        else if (cp == 0x2022 || cp == 0x25CF || cp == 0x25AA || cp == 0x2219) out[o++] = (char)0xB7;
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A || cp == 0x2032) out[o++] = '\'';
        else if (cp == 0x201C || cp == 0x201D || cp == 0x201E || cp == 0x2033) out[o++] = '"';
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212 || cp == 0x2010 || cp == 0x2011) out[o++] = '-';
        else if (cp == 0x2026) { out[o++] = '.'; if (o + 1 < cap) out[o++] = '.'; if (o + 1 < cap) out[o++] = '.'; }
        else if (cp == 0x00A0 || cp == 0x2002 || cp == 0x2003 || cp == 0x2009 || cp == 0x202F) out[o++] = ' ';
        else if (cp == 0x20AC) { out[o++] = 'E'; if (o + 1 < cap) out[o++] = 'U'; if (o + 1 < cap) out[o++] = 'R'; }
        else if (cp == 0xFEFF || cp == 0x200B || cp == 0x200D || cp == 0x00AD) ;
        else out[o++] = '?';
    }
    out[o] = 0;
    return o;
}

static int text_width_n(struct RastPort *rp, const char *s, size_t n)
{
    char buf[512];
    size_t m;
    if (!n) return 0;
    m = to_latin1(s, n, buf, sizeof buf);
    return m ? (int)TextLength(rp, (STRPTR)buf, (ULONG)m) : 0;
}

static int selection_for_run(int paragraph, int run, size_t len,
                             size_t *from, size_t *to)
{
    ow_selection sel;
    ow_position a, b, rs, re;
    if (paragraph < 0 || !editor || ow_editor_selection_empty(editor)) return 0;
    sel = ow_editor_selection(editor);
    if (pos_compare(sel.anchor, sel.focus) <= 0) { a = sel.anchor; b = sel.focus; }
    else { a = sel.focus; b = sel.anchor; }
    rs.paragraph = re.paragraph = paragraph;
    rs.run = re.run = run;
    rs.byte_offset = 0;
    re.byte_offset = len;
    if (pos_compare(b, rs) <= 0 || pos_compare(a, re) >= 0) return 0;
    *from = (a.paragraph == paragraph && a.run == run) ? a.byte_offset : 0;
    *to = (b.paragraph == paragraph && b.run == run) ? b.byte_offset : len;
    if (*from > len) *from = len;
    if (*to > len) *to = len;
    return *to > *from;
}

static void add_hit(int paragraph, int run, size_t start, size_t end,
                    int x, int y, int w, int h)
{
    hit_span *hs;
    if (hit_count >= HIT_MAX) return;
    if (w > 0 && h > 0 && (x + w < page_view_box.x || x >= page_view_box.x + page_view_box.w ||
        y + h < page_view_box.y || y >= page_view_box.y + page_view_box.h)) return;
    if (paragraph < 0) return;
    if (doc && paragraph < doc->body.nparas && run >= 0 && run < doc->body.paras[paragraph].nruns &&
        doc->body.paras[paragraph].runs[run].kind != OWF_RUN_TEXT) return;
    hs = &hits[hit_count++];
    hs->paragraph = paragraph;
    hs->run = run;
    hs->start = start;
    hs->end = end;
    hs->x = x; hs->y = y; hs->w = w > 1 ? w : 2; hs->h = h;
}

static void draw_caret_if_here(struct RastPort *rp, int paragraph, int run,
                               size_t segment_start, size_t segment_end,
                               const char *text, int x, int y)
{
    ow_selection sel;
    ow_position c;
    int cx;
    if (paragraph < 0 || !editor || !ow_editor_selection_empty(editor)) return;
    sel = ow_editor_selection(editor);
    c = sel.focus;
    if (c.paragraph != paragraph || c.run != run) return;
    if (c.byte_offset < segment_start || c.byte_offset > segment_end) return;
    cx = x + text_width_n(rp, text + segment_start,
                          c.byte_offset - segment_start);
    ogt_vline(rp, ogt_pen(&ctx, "accent"), cx, y, render_line_h > 1 ? render_line_h - 1 : 1);
}

static void render_begin(void *ud, const ow_page_info *p)
{
    (void)ud; (void)p;
    hit_count = 0;
    render_para = -1;
    render_bottom = page_box.y;
    render_rule_bottom = page_box.y;
    render_tbl_id = render_tbl_row = -1;
    render_para_prev = -1;
    render_shift = 0;
}

static void render_end(void *ud, const ow_page_info *p)
{
    struct RastPort *rp = (struct RastPort *)ud;
    (void)p;
    if (rp && font) SetFont(rp, font);
}

static void render_text_run(void *ud, int paragraph, int run_index,
                            int x, int y, const char *utf8,
                            const owf_charfmt *fmt)
{
    struct RastPort *rp = (struct RastPort *)ud;
    size_t len = utf8 ? strlen(utf8) : 0, off = 0;
    size_t sel_from = 0, sel_to = 0;
    int has_selection, style = FS_NORMAL;
    struct TextFont *df;
    int run_h;

    if (!utf8) return;
    df = document_font(paragraph, fmt);
    if (df) SetFont(rp, df);
    run_h = (df ? df->tf_YSize : fh) + 2;
    if (paragraph != render_para) {
        int wanted_y = render_y0 + y * render_num / render_den;
        int content_right;
        if (paragraph >= 0 && doc && paragraph < doc->body.nparas && doc->body.paras[paragraph].table_id >= 0) {
            const owf_para *tp=&doc->body.paras[paragraph];
            int cols=tp->table_cols>0?tp->table_cols:1;
            int cw=(doc->page.width-doc->page.margin_left-doc->page.margin_right)/cols;
            content_right=render_x0+(doc->page.margin_left+(tp->table_col+1)*cw-120)*render_num/render_den;
        } else content_right = render_x0 + (doc->page.width - doc->page.margin_right) * render_num / render_den;
        int para_width, available;
        owf_align align = (doc && paragraph >= 0 && paragraph < doc->body.nparas)
            ? doc->body.paras[paragraph].fmt.align : OWF_ALIGN_LEFT;
        render_para_prev = render_para;
        render_para = paragraph;
        render_left_x = render_x0 + x * render_num / render_den;
        available = content_right - render_left_x;
        if (align == OWF_ALIGN_CENTRE || align == OWF_ALIGN_RIGHT) {
            para_width = paragraph_single_line_width(rp, paragraph);
            if (para_width >= 0 && para_width <= available) {
                if (align == OWF_ALIGN_CENTRE) render_left_x += (available - para_width) / 2;
                else render_left_x += available - para_width;
            }
            if (df) SetFont(rp, df);
        }
        render_line_x = render_left_x;
        render_line_h = run_h;
        {
            /* The engine places paragraphs from estimated heights; the
             * text is measured here. A table row's cells share one line,
             * below what came before the row. Another paragraph follows
             * what was drawn before it, with its spacing, so an estimate
             * that was too tall leaves no gap; one that was too short
             * is never drawn over. */
            const owf_para *pp = (doc && paragraph >= 0 && paragraph < doc->body.nparas) ? &doc->body.paras[paragraph] : NULL;
            int above = render_bottom > render_rule_bottom ? render_bottom : render_rule_bottom;
            if (pp && pp->table_id >= 0) {
                if (pp->table_id != render_tbl_id || pp->table_row != render_tbl_row) {
                    render_tbl_id = pp->table_id;
                    render_tbl_row = pp->table_row;
                    render_row_y = wanted_y + render_shift < render_bottom + 2 ? render_bottom + 2 : wanted_y + render_shift;
                    render_cell_col = -1;
                }
                if (pp->table_col != render_cell_col) {   /* the cell's first paragraph: the row's line */
                    render_cell_col = pp->table_col;
                    render_line_y = render_row_y;
                } else                                   /* a cell's next paragraph: below the last */
                    render_line_y = render_cell_bottom + 2;
                render_cell_bottom = render_line_y;
            } else {
                int was_para = render_para_prev >= 0 && doc && render_para_prev < doc->body.nparas;
                render_tbl_id = render_tbl_row = -1;
                if (was_para && pp) {
                    const owf_para *prev = &doc->body.paras[render_para_prev];
                    int gap = ((prev->table_id >= 0 ? 0 : prev->fmt.space_after) + pp->fmt.space_before) * render_num / render_den;
                    int flow;
                    if (pp->fmt.heading >= 1 && pp->fmt.heading <= 3 && !pp->fmt.space_before) gap += run_h / 2;
                    flow = above + 2 + gap;
                    render_line_y = flow < wanted_y ? flow : (wanted_y < above + 2 ? above + 2 : wanted_y);
                    render_shift = render_line_y - wanted_y;
                } else
                    render_line_y = wanted_y < render_bottom + 2 ? render_bottom + 2 : wanted_y;
            }
        }
    } else if (run_h > render_line_h) render_line_h = run_h;
    render_max_x = page_box.x + page_box.w - 9;
    if (render_line_x < render_left_x) render_line_x = render_left_x;

    if (fmt && (fmt->flags & OWF_BOLD)) style |= FSF_BOLD;
    if (fmt && (fmt->flags & OWF_ITALIC)) style |= FSF_ITALIC;
    if (fmt && (fmt->flags & OWF_UNDERLINE)) style |= FSF_UNDERLINED;
    if (paragraph >= 0 && doc && paragraph < doc->body.nparas && run_index >= 0 &&
        run_index < doc->body.paras[paragraph].nruns && doc->body.paras[paragraph].runs[run_index].href)
        style |= FSF_UNDERLINED;
    SetSoftStyle(rp, style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    has_selection = selection_for_run(paragraph, run_index, len, &sel_from, &sel_to);

    if (!len) {
        add_hit(paragraph, run_index, 0, 0, render_line_x, render_line_y, 2, render_line_h);
        draw_caret_if_here(rp, paragraph, run_index, 0, 0, utf8,
                           render_line_x, render_line_y);
        if (render_bottom < render_line_y + render_line_h)
            render_bottom = render_line_y + render_line_h;
        SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        return;
    }

    while (off < len && render_line_y + render_line_h < page_box.y + page_box.h - 7) {
        char line[256];
        size_t p = off, best = off, last_space = (size_t)-1;
        size_t seg_end, a, b;
        int available, width, overflow = 0, newline = 0;

        if (render_line_x >= render_max_x - 4) {
            render_line_x = render_left_x;
            render_line_y += render_line_h;
        }
        available = render_max_x - render_line_x;
        while (p < len) {
            size_t next;
            int w;
            if (utf8[p] == '\n') { newline = 1; break; }
            next = utf8_next_local(utf8, p, len);
            if (next - off >= sizeof(line) - 1) break;
            w = text_width_n(rp, utf8 + off, next - off);
            if (w > available) { overflow = 1; break; }
            best = next;
            if (utf8[p] == ' ' || utf8[p] == '\t') last_space = best;
            p = next;
        }
        if (overflow && last_space != (size_t)-1 && last_space > off)
            best = last_space;
        if (best == off && off < len && utf8[off] != '\n')
            best = utf8_next_local(utf8, off, len);
        seg_end = best;

        if (seg_end > off) {
            size_t n = seg_end - off;
            if (n >= sizeof(line)) n = sizeof(line) - 1;
            to_latin1(utf8 + off, n, line, sizeof line);
            width = text_width_n(rp, utf8 + off, n);

            if (has_selection) {
                a = sel_from > off ? sel_from : off;
                b = sel_to < seg_end ? sel_to : seg_end;
                if (b > a) {
                    int sx = render_line_x + text_width_n(rp, utf8 + off, a - off);
                    int sw = text_width_n(rp, utf8 + a, b - a);
                    if (sw < 2) sw = 2;
                    ogt_box(rp, ogt_pen(&ctx, "selection.inactive"),
                            sx, render_line_y, sw, render_line_h);
                }
            }
            ogt_text(rp, (paragraph >= 0 && doc && paragraph < doc->body.nparas &&
                     run_index >= 0 && run_index < doc->body.paras[paragraph].nruns &&
                     doc->body.paras[paragraph].runs[run_index].href) ? ogt_pen(&ctx, "accent")
                     : (fmt && fmt->colour != OWF_COLOUR_AUTO && fmt->colour != 0)
                     ? ogt_pen_rgb(&ctx, (ogt_rgb){ (UBYTE)((fmt->colour >> 16) & 255), (UBYTE)((fmt->colour >> 8) & 255), (UBYTE)(fmt->colour & 255) })
                     : ogt_pen(&ctx, "fill.text"), render_line_x,
                     render_line_y, line, available);
            add_hit(paragraph, run_index, off, seg_end, render_line_x,
                    render_line_y, width, render_line_h);
            draw_caret_if_here(rp, paragraph, run_index, off, seg_end,
                               utf8, render_line_x, render_line_y);
            render_line_x += width;
            off = seg_end;
        }

        if (off < len && utf8[off] == '\n') {
            ++off;
            render_line_x = render_left_x;
            render_line_y += render_line_h;
        } else if (overflow) {
            render_line_x = render_left_x;
            render_line_y += render_line_h;
            while (off < len && utf8[off] == ' ') ++off;
        } else if (newline) {
            ++off;
            render_line_x = render_left_x;
            render_line_y += render_line_h;
        } else if (seg_end == off && off < len) {
            ++off;
        }
        if (render_bottom < render_line_y + render_line_h)
            render_bottom = render_line_y + render_line_h;
        if (render_cell_bottom < render_line_y + render_line_h)
            render_cell_bottom = render_line_y + render_line_h;
    }
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
}

static int point_in_box(const box *b, int x, int y)
{
    return b && x >= b->x && y >= b->y && x < b->x + b->w && y < b->y + b->h;
}

static int hit_position(int mx, int my, ow_position *out)
{
    int i, best = -1, best_dist = 0x7fffffff;
    struct RastPort *rp;
    if (!win || !editor || !out || !hit_count) return 0;
    rp = win->RPort;
    for (i = 0; i < hit_count; ++i) {
        hit_span *h = &hits[i];
        int dy = my < h->y ? h->y - my : my >= h->y + h->h ? my - (h->y + h->h - 1) : 0;
        int dx = mx < h->x ? h->x - mx : mx > h->x + h->w ? mx - (h->x + h->w) : 0;
        int d = dy * 8 + dx;
        if (d < best_dist) { best_dist = d; best = i; }
    }
    if (best < 0) return 0;
    {
        hit_span *h = &hits[best];
        const owf_run *run = &doc->body.paras[h->paragraph].runs[h->run];
        size_t p = h->start;
        out->paragraph = h->paragraph;
        out->run = h->run;
        if (mx <= h->x || h->start == h->end) { out->byte_offset = h->start; return 1; }
        if (mx >= h->x + h->w) { out->byte_offset = h->end; return 1; }
        while (p < h->end) {
            size_t next = utf8_next_local(run->text, p, strlen(run->text));
            int left = h->x + text_width_n(rp, run->text + h->start, p - h->start);
            int right = h->x + text_width_n(rp, run->text + h->start, next - h->start);
            if (mx < (left + right) / 2) { out->byte_offset = p; return 1; }
            p = next;
        }
        out->byte_offset = h->end;
        return 1;
    }
}

static void mouse_set_selection(int mx, int my, int extend)
{
    ow_position p;
    ow_selection sel;
    if (!point_in_box(&page_box, mx, my) || !hit_position(mx, my, &p)) return;
    sel = ow_editor_selection(editor);
    if (!extend) sel.anchor = p;
    sel.focus = p;
    ow_editor_set_selection(editor, &sel);
    request_document_redraw();
}

static void draw_format_bar(void)
{
    struct RastPort *rp = win->RPort;
    int x = format_box.x + 6, y = format_box.y + 4;
    int h = format_box.h - 8, gap = 5;
    int stylew = 104, fontw = 140, sizew = 46;
    LONG textpen = ogt_pen(&ctx, "label");
    owf_charfmt cf;
    owf_parafmt pf;
    char style_name[32] = "Body Text  v";
    char font_name[96] = "Default  v";
    char size_name[24] = "12";
    const char *labels[] = { "B", "I", "U", "L", "C", "R", "J", "\xb7", "1." };
    int active[9] = { 0,0,0,0,0,0,0,0,0 };
    int i, bw = fh + 11;

    memset(&cf, 0, sizeof cf);
    memset(&pf, 0, sizeof pf);
    if (editor) {
        ow_editor_current_charfmt(editor, &cf);
        ow_editor_current_parafmt(editor, &pf);
        active[0] = (cf.flags & OWF_BOLD) != 0;
        active[1] = (cf.flags & OWF_ITALIC) != 0;
        active[2] = (cf.flags & OWF_UNDERLINE) != 0;
        active[3 + (pf.align >= OWF_ALIGN_LEFT && pf.align <= OWF_ALIGN_JUSTIFY ? pf.align : 0)] = 1;
        if (pf.heading > 0) snprintf(style_name, sizeof style_name, "Heading %d  v", pf.heading);
        if (cf.font >= 0 && doc && cf.font < doc->nfonts && doc->fonts[cf.font].name)
            snprintf(font_name, sizeof font_name, "%s  v", doc->fonts[cf.font].name);
        else if (doc && doc->base.font >= 0 && doc->base.font < doc->nfonts && doc->fonts[doc->base.font].name)
            snprintf(font_name, sizeof font_name, "%s  v", doc->fonts[doc->base.font].name);
        {
            int twips = cf.size ? cf.size : (doc && doc->base.size ? doc->base.size : 240);
            snprintf(size_name, sizeof size_name, "%d", (twips + 10) / 20);
        }
    }

    fmt_style_box = (box){ x, y, stylew, h };
    field(rp, "string", x, y, stylew, h);
    ogt_text(rp, textpen, x + 7, y + (h - fh) / 2, style_name, stylew - 12);
    x += stylew + gap;
    fmt_font_box = (box){ x, y, fontw, h };
    field(rp, "string", x, y, fontw, h);
    ogt_text(rp, textpen, x + 7, y + (h - fh) / 2, font_name, fontw - 12);
    x += fontw + gap;
    fmt_size_box = (box){ x, y, sizew, h };
    field(rp, "string", x, y, sizew, h);
    ogt_text(rp, textpen, x + 8, y + (h - fh) / 2, size_name, sizew - 12);
    x += sizew + gap;

    for (i = 0; i < 9; ++i) {
        fmt_button_box[i] = (box){0,0,0,0};
        if (x + bw >= format_box.x + format_box.w - 4) continue;
        fmt_button_box[i] = (box){ x, y, bw, h };
        format_button_draw(rp, &fmt_button_box[i], labels[i], active[i], i == 0);
        x += bw + 3;
    }
}

static void draw_tabs(struct RastPort *rp, int x, int y, int w, const char **labels, int n, int active)
{
    int i, tw = w / n;
    for (i = 0; i < n; ++i) {
        if (i == active) ogt_fill(&ctx, rp, "button", x + i * tw, y, tw, fh + 8);
        ogt_hline(rp, ogt_pen(&ctx, "group.line"), x + i * tw, y + fh + 7, tw);
        if (i) ogt_vline(rp, ogt_pen(&ctx, "group.line"), x + i * tw, y + 2, fh + 4);
        ogt_text(rp, ogt_pen(&ctx, "label"), x + i * tw + 5, y + 4, labels[i], tw - 10);
    }
}

static void draw_navigator(void)
{
    struct RastPort *rp = win->RPort;
    const char *tabs[] = { "Pages", "Outline", "Styles", "Assets" };
    int top, i, pages = editor ? ow_editor_page_count(editor) : 1;
    int start_page = 0;
    if (!navigator_box.w) return;
    nav_page_slots = 0;
    if (pages > 4) {
        start_page = page_index > 1 ? page_index - 1 : 0;
        if (start_page + 4 > pages) start_page = pages - 4;
    }
    ogt_fill(&ctx, rp, "window", navigator_box.x, navigator_box.y, navigator_box.w, navigator_box.h);
    if (navigator_box.w < 210) {
        field(rp, "string", navigator_box.x + 5, navigator_box.y + 3, navigator_box.w - 10, fh + 8);
        ogt_text(rp, ogt_pen(&ctx, "label"), navigator_box.x + 12, navigator_box.y + 7, "Pages  v", navigator_box.w - 24);
    } else draw_tabs(rp, navigator_box.x, navigator_box.y, navigator_box.w, tabs, 4, 0);
    top = navigator_box.y + fh + 13;
    field(rp, "list", navigator_box.x + 5, top, navigator_box.w - 10, navigator_box.h - (top - navigator_box.y) - 5);
    for (i = 0; i < pages - start_page && i < 4; ++i) {
        int pn = start_page + i;
        int pw = (navigator_box.w - 34) * 2 / 3;
        int ph = pw * 141 / 100;
        int px = navigator_box.x + (navigator_box.w - pw) / 2;
        int py = top + 9 + i * (ph + fh + 11);
        if (py + ph + fh > navigator_box.y + navigator_box.h - 8) break;
        nav_page_box[nav_page_slots] = (box){ px, py, pw, ph };
        nav_page_number[nav_page_slots] = pn;
        ++nav_page_slots;
        ogt_box(rp, ogt_pen(&ctx, "track"), px + 2, py + 2, pw, ph);
        ogt_box(rp, ogt_pen_rgb(&ctx, (ogt_rgb){255,255,255}), px, py, pw, ph);
        ogt_frame(rp, ogt_pen(&ctx, pn == page_index ? "accent" : "group.line"), px, py, pw, ph);
        {
            char s[16];
            snprintf(s, sizeof s, "%d", pn + 1);
            ogt_text(rp, ogt_pen(&ctx, "muted"), navigator_box.x + (navigator_box.w - ogt_text_width(rp, s)) / 2,
                     py + ph + 3, s, 0);
        }
    }
}

static void draw_inspector(void)
{
    struct RastPort *rp = win->RPort;
    const char *tabs[] = { "Properties", "Styles", "Datatypes", "OpenPrint" };
    int x, y, fw;
    char buf[96];
    if (!inspector_box.w) return;
    ogt_fill(&ctx, rp, "window", inspector_box.x, inspector_box.y, inspector_box.w, inspector_box.h);
    draw_tabs(rp, inspector_box.x, inspector_box.y, inspector_box.w, tabs, 4, 0);
    x = inspector_box.x + 8;
    y = inspector_box.y + fh + 18;
    fw = inspector_box.w - 16;
    ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "label"), x, y, "Document", fw);
    ogt_bold(rp, 0);
    y += fh + 7;
    snprintf(buf, sizeof buf, "Format     %s", current_format);
    field(rp, "string", x, y, fw, fh + 8);
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 6, y + 4, buf, fw - 12);
    y += fh + 13;
    field(rp, "string", x, y, fw, fh + 8);
    {
        const char *paper = "Custom";
        const char *orient = doc && doc->page.width > doc->page.height ? "landscape" : "portrait";
        int sw = doc ? (doc->page.width < doc->page.height ? doc->page.width : doc->page.height) : 0;
        int sh = doc ? (doc->page.width > doc->page.height ? doc->page.width : doc->page.height) : 0;
        if (abs(sw - 11906) < 140 && abs(sh - 16838) < 140) paper = "A4";
        else if (abs(sw - 12240) < 140 && abs(sh - 15840) < 140) paper = "Letter";
        else if (abs(sw - 12240) < 140 && abs(sh - 20160) < 140) paper = "Legal";
        snprintf(buf, sizeof buf, "Page       %s %s", paper, orient);
        ogt_text(rp, ogt_pen(&ctx, "label"), x + 6, y + 4, buf, fw - 12);
    }
    y += fh + 13;
    snprintf(buf, sizeof buf, "Zoom       %d%%", zoom);
    field(rp, "string", x, y, fw, fh + 8);
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 6, y + 4, buf, fw - 12);
    y += fh + 18;

    ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "label"), x, y, "Open stack", fw);
    ogt_bold(rp, 0);
    y += fh + 7;
    snprintf(buf, sizeof buf, "OpenRTG    %s", screen_depth() > 8 ? "true-colour" : "AGA");
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 3, y, buf, fw - 6);
    y += fh + 5;
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 3, y, "Datatypes  ready", fw - 6);
    y += fh + 5;
    ogt_text(rp, ogt_pen(&ctx, "label"), x + 3, y, "OpenPrint  ready", fw - 6);
    y += fh + 13;
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), x, y, fw);
    y += 7;
    ogt_text(rp, ogt_pen(&ctx, "muted"), x, y, "DOCX / ODT / Amiga", fw);
    y += fh + 3;
    ogt_text(rp, ogt_pen(&ctx, "muted"), x, y, "via libowf", fw);
}

static void draw_ruler(void)
{
    struct RastPort *rp = win->RPort;
    int i, base = ruler_box.y + ruler_box.h - 3;
    ogt_fill(&ctx, rp, "window", ruler_box.x, ruler_box.y, ruler_box.w, ruler_box.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), ruler_box.x, base, ruler_box.w);
    for (i = 0; i <= 10; ++i) {
        int x = ruler_box.x + 8 + (ruler_box.w - 16) * i / 10;
        int h = (i % 5 == 0) ? 7 : 4;
        ogt_vline(rp, ogt_pen(&ctx, "muted"), x, base - h + 1, h);
    }
}

static void render_image(void *ud,int image_index,int x,int y,int width,int height)
{
    struct RastPort *rp=(struct RastPort*)ud;ow_image_cache *c=image_cache_get(image_index);int px=render_x0+x*render_num/render_den,py=render_y0+y*render_num/render_den+render_shift;int pw=width*render_num/render_den,ph=height*render_num/render_den;char label[96];
    if(pw<8)pw=8;
    if(ph<8)ph=8;
    if(c&&CyberGfxBase&&screen_depth()>8)ScalePixelArray(c->argb,c->w,c->h,c->w*4,rp,(UWORD)px,(UWORD)py,(UWORD)pw,(UWORD)ph,RECTFMT_ARGB);
    else {ogt_frame(rp,ogt_pen(&ctx,"group.line"),px,py,pw,ph);snprintf(label,sizeof label,"Image: %s",(doc&&image_index<doc->nimages&&doc->images[image_index].name)?doc->images[image_index].name:"embedded");ogt_text(rp,ogt_pen(&ctx,"muted"),px+6,py+6,label,pw-12);}
    /* what follows goes below the picture, not over it (a floating picture
     * is placed in its paragraph; text does not wrap round it yet) */
    if(py+ph>render_bottom)render_bottom=py+ph;
    if(py+ph>render_cell_bottom)render_cell_bottom=py+ph;
}

static void render_rule(void *ud, int x1, int y1, int x2, int y2, unsigned long rgb)
{
    struct RastPort *rp=(struct RastPort*)ud;
    int px1=render_x0+x1*render_num/render_den, py1=render_y0+y1*render_num/render_den+render_shift;
    int px2=render_x0+x2*render_num/render_den, py2=render_y0+y2*render_num/render_den+render_shift;
    LONG pen=ogt_pen_rgb(&ctx,(ogt_rgb){(UBYTE)((rgb>>16)&255),(UBYTE)((rgb>>8)&255),(UBYTE)(rgb&255)});
    SetAPen(rp,pen);Move(rp,px1,py1);Draw(rp,px2,py2);
    if(py1>render_rule_bottom)render_rule_bottom=py1;
    if(py2>render_rule_bottom)render_rule_bottom=py2;
}

static void draw_page(void)
{
    struct RastPort *rp = win->RPort;
    int pw, ph, scale_num, scale_den;
    int vx, vy, vw, vh;
    ow_renderer r;
    LONG paper = ogt_pen_rgb(&ctx, (ogt_rgb){255,255,255});
    LONG shadow = ogt_pen(&ctx, "track");
    LONG edge = ogt_pen(&ctx, "group.line");
    struct Region *clip = NULL, *oldclip = NULL;
    struct Rectangle rect;
    if (!doc || !editor) return;

    vx = canvas_box.x + 5;
    vy = ruler_box.y + ruler_box.h + 5;
    vw = canvas_box.w - 10;
    vh = canvas_box.y + canvas_box.h - vy - 5;
    if (vw < 80 || vh < 80) return;
    page_view_box = (box){ vx, vy, vw, vh };
    /* 100% means an approximately 96-dpi document surface: 1440 twips/inch
     * divided by 96 pixels/inch is exactly 15 twips/pixel. Fit Page/Width
     * compute a zoom percentage separately instead of redefining 100%. */
    scale_num = zoom;
    scale_den = 1500;
    pw = doc->page.width * scale_num / scale_den;
    ph = doc->page.height * scale_num / scale_den;
    if (pw < 40) pw = 40;
    if (ph < 40) ph = 40;
    page_full_w = pw;
    page_full_h = ph;
    page_max_scroll_x = pw > vw ? pw - vw : 0;
    page_max_scroll_y = ph > vh ? ph - vh : 0;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_x > page_max_scroll_x) scroll_x = page_max_scroll_x;
    if (scroll_y > page_max_scroll_y) scroll_y = page_max_scroll_y;

    page_box.w = pw;
    page_box.h = ph;
    page_box.x = vx + (pw < vw ? (vw - pw) / 2 : 0) - scroll_x;
    page_box.y = vy + (ph < vh ? (vh - ph) / 2 : 0) - scroll_y;

    if (LayersBase && win->WLayer) {
        clip = NewRegion();
        if (clip) {
            rect.MinX = (WORD)vx; rect.MinY = (WORD)vy;
            rect.MaxX = (WORD)(vx + vw - 1); rect.MaxY = (WORD)(vy + vh - 1);
            if (OrRectRegion(clip, &rect)) oldclip = InstallClipRegion(win->WLayer, clip);
            else { DisposeRegion(clip); clip = NULL; }
        }
    }

    ogt_box(rp, shadow, page_box.x + 4, page_box.y + 4, pw, ph);
    ogt_box(rp, paper, page_box.x, page_box.y, pw, ph);
    ogt_frame(rp, edge, page_box.x, page_box.y, pw, ph);

    render_x0 = page_box.x;
    render_y0 = page_box.y;
    render_bottom = page_box.y;
    render_para = -1;
    render_num = scale_num;
    render_den = scale_den;
    memset(&r, 0, sizeof r);
    r.userdata = rp;
    r.begin_page = render_begin;
    r.end_page = render_end;
    r.text_run = render_text_run;
    r.rule = render_rule;
    r.image = render_image;
    ow_editor_render_page(editor, page_index, &r);

    if (clip && LayersBase && win->WLayer) {
        InstallClipRegion(win->WLayer, oldclip);
        DisposeRegion(clip);
    }
    if (font) SetFont(rp, font);
}

static void draw_status(void)
{
    struct RastPort *rp = win->RPort;
    char left[260], right[64];
    int pages = editor ? ow_editor_page_count(editor) : 1;
    snprintf(left, sizeof left, " Page %d of %d  |  %d words  |  %s  |  %s",
             page_index + 1, pages, word_count(), current_format, status);
    snprintf(right, sizeof right, "%s  |  %d%% ",
             screen_depth() > 8 ? "OpenRTG" : "AGA", zoom);
    field(rp, "string", status_box.x, status_box.y, status_box.w, status_box.h);
    ogt_text(rp, ogt_pen(&ctx, "label"), status_box.x + 4,
             status_box.y + (status_box.h - fh) / 2, left,
             status_box.w - ogt_text_width(rp, right) - 14);
    ogt_text(rp, ogt_pen(&ctx, "label"),
             status_box.x + status_box.w - ogt_text_width(rp, right) - 5,
             status_box.y + (status_box.h - fh) / 2, right, 0);
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
    draw_format_bar();
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), format_box.x, format_box.y + format_box.h - 1, format_box.w);

    if (navigator_box.w) {
        draw_navigator();
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), navigator_box.x + navigator_box.w, navigator_box.y, navigator_box.h);
    }
    ogt_fill(&ctx, rp, "list", canvas_box.x, canvas_box.y, canvas_box.w, canvas_box.h);
    draw_ruler();
    draw_page();
    if (inspector_box.w) {
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), inspector_box.x - 1, inspector_box.y, inspector_box.h);
        draw_inspector();
    }
    draw_status();
}

static void redraw_document_area(void)
{
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    SetFont(rp, font);
    draw_format_bar();
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), format_box.x,
              format_box.y + format_box.h - 1, format_box.w);
    ogt_fill(&ctx, rp, "list", canvas_box.x, canvas_box.y, canvas_box.w, canvas_box.h);
    draw_ruler();
    draw_page();
    draw_status();
}

static void request_document_redraw(void)
{
    if (defer_repaint) repaint_pending = 1;
    else redraw_document_area();
}

static void free_gadgets(void)
{
    if (chain) {
        RemoveGList(win, chain, -1);
        chain = NULL;
    }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    int tbh, use_style, navw, inspw, body_y, body_h;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);

    use_style = area.w < 780 ? OGT_TB_ICONS : tb_style;
    ogt_toolbar_set(&tb, tools, sizeof tools / sizeof tools[0], use_style);
    update_edit_tools();
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 5, area.y + 3, area.w - 10);
    toolbar_box = (box){ area.x, area.y, area.w, tbh + 7 };
    format_box = (box){ area.x, toolbar_box.y + toolbar_box.h, area.w, fh + 16 };
    status_box = (box){ area.x + 5, area.y + area.h - fh - 11, area.w - 10, fh + 7 };

    body_y = format_box.y + format_box.h;
    body_h = status_box.y - body_y - 5;
    navw = show_nav && area.w >= 560 ? (area.w < 850 ? 138 : area.w / 7) : 0;
    inspw = show_inspector && area.w >= 900 ? (area.w < 1200 ? 205 : area.w / 5) : 0;
    navigator_box = (box){ area.x, body_y, navw, body_h };
    inspector_box = (box){ area.x + area.w - inspw, body_y, inspw, body_h };
    canvas_box = (box){ area.x + navw + (navw ? 1 : 0), body_y,
                        area.w - navw - inspw - (navw ? 1 : 0) - (inspw ? 1 : 0), body_h };
    ruler_box = (box){ canvas_box.x, canvas_box.y, canvas_box.w, fh + 10 };

    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);
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
            if (font) CloseFont(font);
            font = f;
            fh = font->tf_YSize;
        }
    }
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    if (win) relayout();
}

static int open_window(void)
{
    int w, h;
    if (!(scr = LockPubScreen(NULL))) return 0;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return 0;
    load_theme();
    if (!(font = ogt_open_font(&theme, scr, &font_attr))) return 0;
    fh = font->tf_YSize;
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);

    w = scr->Width >= 1200 ? scr->Width * 9 / 10 : scr->Width - 12;
    h = scr->Height >= 700 ? (scr->Height - scr->BarHeight) * 9 / 10 : scr->Height - scr->BarHeight - 8;
    if (w < 520) w = scr->Width;
    if (h < 330) h = scr->Height - scr->BarHeight;

    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"OpenWrite — OpenAmigaWriter",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, (scr->Width - w) / 2,
        WA_Top, scr->BarHeight + (scr->Height - scr->BarHeight - h) / 2,
        WA_MinWidth, 520, WA_MinHeight, 330,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                  IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW |
                  IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                  IDCMP_MOUSEMOVE,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    relayout();
    update_window_title();
    return 1;
}

static void close_window(void)
{
    doc_font_cache_clear();
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
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int text_requester(const char *title, const char *label,
                          const char *initial, char *out, size_t out_size)
{
    struct Gadget *list = NULL, *last, *gtext, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    STRPTR value = NULL;
    int done = 0, ok = 0, ww = 430, wh = 88;
    if (!scr || !vi || !out || out_size < 2) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr; ng.ng_VisualInfo = vi;
    last = CreateContext(&list);
    ng.ng_LeftEdge = 82; ng.ng_TopEdge = 15; ng.ng_Width = 330; ng.ng_Height = 18;
    ng.ng_GadgetText = (STRPTR)label; ng.ng_GadgetID = 1; ng.ng_Flags = PLACETEXT_LEFT;
    last = gtext = CreateGadget(STRING_KIND, last, &ng,
        GTST_String, (ULONG)(initial ? initial : ""), GTST_MaxChars, (ULONG)(out_size - 1),
        GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_GadgetText = (STRPTR)"OK"; ng.ng_GadgetID = 2;
    ng.ng_LeftEdge = 242; ng.ng_TopEdge = 48; ng.ng_Width = 78; ng.ng_Height = 20;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 3; ng.ng_LeftEdge = 332;
    last = gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gtext || !gok || !gcancel) goto out;
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

static int replace_requester(char *find, size_t find_size,
                             char *repl, size_t repl_size)
{
    struct Gadget *list = NULL, *last, *gf, *gr, *gone, *gall, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    STRPTR v = NULL;
    int done = 0, mode = 0, ww = 470, wh = 122;
    if (!scr || !vi) return 0;
    memset(&ng, 0, sizeof ng); ng.ng_TextAttr = &font_attr; ng.ng_VisualInfo = vi;
    last = CreateContext(&list);
    ng.ng_Flags = PLACETEXT_LEFT; ng.ng_Height = 18; ng.ng_Width = 340; ng.ng_LeftEdge = 110;
    ng.ng_TopEdge = 15; ng.ng_GadgetText = (STRPTR)"Find"; ng.ng_GadgetID = 1;
    last = gf = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)find,
        GTST_MaxChars, (ULONG)(find_size - 1), GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_TopEdge = 45; ng.ng_GadgetText = (STRPTR)"Replace with"; ng.ng_GadgetID = 2;
    last = gr = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)repl,
        GTST_MaxChars, (ULONG)(repl_size - 1), GA_TabCycle, TRUE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_TopEdge = 78; ng.ng_Height = 20; ng.ng_Width = 92;
    ng.ng_LeftEdge = 170; ng.ng_GadgetText = (STRPTR)"Replace"; ng.ng_GadgetID = 3;
    last = gone = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 270; ng.ng_GadgetText = (STRPTR)"Replace All"; ng.ng_GadgetID = 4;
    last = gall = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = 370; ng.ng_Width = 80; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 5;
    last = gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!gf || !gr || !gone || !gall || !gcancel) goto out;
    rw = OpenWindowTags(NULL, WA_Title, (ULONG)"Replace", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh,
        WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL); ActivateGadget(gf, rw, NULL);
    while (!done) {
        struct IntuiMessage *m; Wait(1UL << rw->UserPort->mp_SigBit);
        while ((m = GT_GetIMsg(rw->UserPort))) {
            ULONG cls = m->Class; UWORD id = m->IAddress ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { done = 1; break; }
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                if (id == 3 || id == 4) { mode = id == 3 ? 1 : 2; done = 1; }
                else if (id == 5) done = 1;
            }
        }
    }
    if (mode) {
        GT_GetGadgetAttrs(gf, rw, NULL, GTST_String, (ULONG)&v, TAG_DONE);
        snprintf(find, find_size, "%s", v ? (char *)v : "");
        GT_GetGadgetAttrs(gr, rw, NULL, GTST_String, (ULONG)&v, TAG_DONE);
        snprintf(repl, repl_size, "%s", v ? (char *)v : "");
        if (!find[0]) mode = 0;
    }
out:
    if (rw) CloseWindow(rw); if (list) FreeGadgets(list); return mode;
}

static int spell_requester(const char *word, char suggestions[][48], int nsuggest,
                           char *replacement, size_t replacement_size)
{
    struct Gadget *list=NULL,*last,*gtext,*grep,*gignore,*gadd,*gdone;
    struct NewGadget ng; struct Window *rw=NULL; STRPTR v=NULL;
    int done=0, mode=0, ww=500, wh=118; char label[180];
    if(!scr||!vi||!word)return 0;
    if(nsuggest>0) snprintf(label,sizeof label,"%s  (suggestion: %s)",word,suggestions[0]);
    else snprintf(label,sizeof label,"Not in dictionary: %s",word);
    memset(&ng,0,sizeof ng);ng.ng_TextAttr=&font_attr;ng.ng_VisualInfo=vi;last=CreateContext(&list);
    ng.ng_Flags=PLACETEXT_ABOVE;ng.ng_LeftEdge=18;ng.ng_TopEdge=28;ng.ng_Width=464;ng.ng_Height=18;
    ng.ng_GadgetText=(STRPTR)label;ng.ng_GadgetID=1;
    last=gtext=CreateGadget(STRING_KIND,last,&ng,GTST_String,(ULONG)(nsuggest?suggestions[0]:word),GTST_MaxChars,(ULONG)(replacement_size-1),GA_TabCycle,TRUE,TAG_DONE);
    ng.ng_Flags=0;ng.ng_TopEdge=70;ng.ng_Height=22;ng.ng_Width=94;ng.ng_GadgetID=2;ng.ng_LeftEdge=84;ng.ng_GadgetText=(STRPTR)"Replace";
    last=grep=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    ng.ng_LeftEdge=184;ng.ng_GadgetID=3;ng.ng_GadgetText=(STRPTR)"Ignore";last=gignore=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    ng.ng_LeftEdge=284;ng.ng_GadgetID=4;ng.ng_GadgetText=(STRPTR)"Add";last=gadd=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    ng.ng_LeftEdge=384;ng.ng_GadgetID=5;ng.ng_GadgetText=(STRPTR)"Done";last=gdone=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    if(!gtext||!grep||!gignore||!gadd||!gdone)goto out;
    rw=OpenWindowTags(NULL,WA_Title,(ULONG)"Spell Check",WA_PubScreen,(ULONG)scr,WA_InnerWidth,ww,WA_InnerHeight,wh,
        WA_Left,(scr->Width-ww)/2,WA_Top,(scr->Height-wh)/2,WA_Gadgets,(ULONG)list,WA_DragBar,TRUE,WA_DepthGadget,TRUE,
        WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|STRINGIDCMP|BUTTONIDCMP,TAG_DONE);
    if(!rw)goto out;GT_RefreshWindow(rw,NULL);ActivateGadget(gtext,rw,NULL);
    while(!done){struct IntuiMessage*m;Wait(1UL<<rw->UserPort->mp_SigBit);while((m=GT_GetIMsg(rw->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget*)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(rw);GT_EndRefresh(rw,TRUE);}else if(cls==IDCMP_GADGETUP&&id>=2&&id<=5){mode=(int)id-1;done=1;}}}
    if(mode==1){GT_GetGadgetAttrs(gtext,rw,NULL,GTST_String,(ULONG)&v,TAG_DONE);snprintf(replacement,replacement_size,"%s",v?(char*)v:"");if(!replacement[0])mode=2;}
out:
    if(rw)CloseWindow(rw);if(list)FreeGadgets(list);return mode;
}

static int ensure_spell(void)
{
    char msg[160];
    static const char *paths[]={"PROGDIR:Dictionaries/en_GB.words","PROGDIR:Dictionaries/en_US.words","SYS:Dictionaries/en_GB.words",NULL};
    int i;
    if(spell)return 1;
    for(i=0;paths[i];++i){spell=ow_spell_open(paths[i],"PROGDIR:Dictionaries/user.words",msg,sizeof msg);if(spell){set_status(msg);return 1;}}
    tell("Spell Check","No OpenWrite dictionary was found. Install Dictionaries/en_GB.words beside OpenWrite.");
    return 0;
}

static void do_spell_check(void)
{
    char word[96], repl[96], sug[4][48];
    int rc, ns, mode;
    if(!ensure_spell())return;
    for(;;){
        rc=ow_spell_next(spell,editor,word,sizeof word,1);
        if(rc<0){tell("Spell Check","The document could not be checked.");return;}
        if(!rc){set_status("Spell check complete: no more unknown words.");request_document_redraw();return;}
        request_document_redraw();
        ns=ow_spell_suggest(spell,word,sug,4);
        repl[0]=0;mode=spell_requester(word,sug,ns,repl,sizeof repl);
        if(mode==0||mode==4){set_status("Spell check stopped.");request_document_redraw();return;}
        if(mode==1){if(editor_result(ow_editor_insert_utf8(editor,repl,strlen(repl)),"Spelling replaced.")){} }
        else if(mode==3){if(!ow_spell_add(spell,word))tell("Spell Check","The word could not be added to the user dictionary.");}
        /* mode 2 ignore: selection focus already sits after the word */
    }
}

static int twips_to_mm(int twips)
{
    if (twips >= 0) return (twips * 127 + 3600) / 7200;
    return -((-twips * 127 + 3600) / 7200);
}

static int mm_to_twips(int mm)
{
    if (mm >= 0) return (mm * 7200 + 63) / 127;
    return -((-mm * 7200 + 63) / 127);
}

static int paragraph_requester(owf_parafmt *fmt)
{
    static STRPTR align_labels[] = { (STRPTR)"Left", (STRPTR)"Centre", (STRPTR)"Right", (STRPTR)"Justify", NULL };
    static STRPTR style_labels[] = { (STRPTR)"Body Text", (STRPTR)"Heading 1", (STRPTR)"Heading 2", (STRPTR)"Heading 3", NULL };
    struct Gadget *list=NULL,*last,*gstyle,*galign,*gbefore,*gafter,*gleft,*gright,*gfirst,*gline,*gok,*gcancel;
    struct NewGadget ng; struct Window *rw=NULL;
    LONG v=0; int done=0,ok=0,ww=500,wh=250;
    if(!fmt||!scr||!vi)return 0;
    memset(&ng,0,sizeof ng); ng.ng_TextAttr=&font_attr; ng.ng_VisualInfo=vi;
    last=CreateContext(&list);

#define ADD_INT(var,id,label,top,value) do { \
    ng.ng_Flags=PLACETEXT_LEFT; ng.ng_LeftEdge=180; ng.ng_TopEdge=(top); ng.ng_Width=90; ng.ng_Height=18; \
    ng.ng_GadgetText=(STRPTR)(label); ng.ng_GadgetID=(id); \
    last=(var)=CreateGadget(INTEGER_KIND,last,&ng,GTIN_Number,(LONG)(value),GTIN_MaxChars,7,GA_TabCycle,TRUE,TAG_DONE); \
} while(0)

    ng.ng_Flags=PLACETEXT_LEFT; ng.ng_LeftEdge=180; ng.ng_TopEdge=14; ng.ng_Width=170; ng.ng_Height=18;
    ng.ng_GadgetText=(STRPTR)"Style"; ng.ng_GadgetID=1;
    last=gstyle=CreateGadget(CYCLE_KIND,last,&ng,GTCY_Labels,(ULONG)style_labels,GTCY_Active,(ULONG)(fmt->heading>=1&&fmt->heading<=3?fmt->heading:0),TAG_DONE);
    ng.ng_TopEdge=44; ng.ng_GadgetText=(STRPTR)"Alignment"; ng.ng_GadgetID=2;
    last=galign=CreateGadget(CYCLE_KIND,last,&ng,GTCY_Labels,(ULONG)align_labels,GTCY_Active,(ULONG)fmt->align,TAG_DONE);
    ADD_INT(gbefore,3,"Space before (pt)",74,(fmt->space_before+10)/20);
    ADD_INT(gafter,4,"Space after (pt)",100,(fmt->space_after+10)/20);
    ADD_INT(gleft,5,"Left indent (mm)",126,twips_to_mm(fmt->indent_left));
    ADD_INT(gright,6,"Right indent (mm)",152,twips_to_mm(fmt->indent_right));
    ADD_INT(gfirst,7,"First line (mm)",178,twips_to_mm(fmt->indent_first));
    ADD_INT(gline,8,"Line spacing (%)",204,fmt->line_spacing?fmt->line_spacing:100);
#undef ADD_INT
    ng.ng_Flags=0; ng.ng_TopEdge=218; ng.ng_Height=20; ng.ng_Width=78; ng.ng_LeftEdge=330;
    ng.ng_GadgetText=(STRPTR)"Apply"; ng.ng_GadgetID=9; last=gok=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    ng.ng_LeftEdge=414; ng.ng_GadgetText=(STRPTR)"Cancel"; ng.ng_GadgetID=10; last=gcancel=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    if(!gstyle||!galign||!gbefore||!gafter||!gleft||!gright||!gfirst||!gline||!gok||!gcancel)goto out;
    rw=OpenWindowTags(NULL,WA_Title,(ULONG)"Paragraph",WA_PubScreen,(ULONG)scr,
        WA_InnerWidth,ww,WA_InnerHeight,wh,WA_Left,(scr->Width-ww)/2,WA_Top,(scr->Height-wh)/2,
        WA_Gadgets,(ULONG)list,WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,
        WA_Activate,TRUE,WA_SimpleRefresh,TRUE,
        WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|BUTTONIDCMP|INTEGERIDCMP|CYCLEIDCMP,TAG_DONE);
    if(!rw)goto out; GT_RefreshWindow(rw,NULL);
    while(!done){struct IntuiMessage*m;Wait(1UL<<rw->UserPort->mp_SigBit);while((m=GT_GetIMsg(rw->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget*)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(rw);GT_EndRefresh(rw,TRUE);}else if(cls==IDCMP_GADGETUP){if(id==9){ok=1;done=1;}else if(id==10)done=1;}}}
    if(ok){
        GT_GetGadgetAttrs(gstyle,rw,NULL,GTCY_Active,(ULONG)&v,TAG_DONE); fmt->heading=(int)v;
        GT_GetGadgetAttrs(galign,rw,NULL,GTCY_Active,(ULONG)&v,TAG_DONE); fmt->align=(owf_align)v;
        GT_GetGadgetAttrs(gbefore,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->space_before=(int)v*20;
        GT_GetGadgetAttrs(gafter,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->space_after=(int)v*20;
        GT_GetGadgetAttrs(gleft,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->indent_left=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gright,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->indent_right=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gfirst,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->indent_first=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gline,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->line_spacing=(int)v;
        if (fmt->line_spacing < 50) fmt->line_spacing = 50;
        if (fmt->line_spacing > 400) fmt->line_spacing = 400;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

static int page_setup_requester(owf_page *page)
{
    static STRPTR paper_labels[]={(STRPTR)"A4",(STRPTR)"Letter",(STRPTR)"Legal",NULL};
    static STRPTR orient_labels[]={(STRPTR)"Portrait",(STRPTR)"Landscape",NULL};
    struct Gadget *list=NULL,*last,*gpaper,*gorient,*gtop,*gright,*gbottom,*gleft,*gstart,*gok,*gcancel;
    struct NewGadget ng; struct Window *rw=NULL; LONG v=0; int done=0,ok=0,paper=0,orient=0,ww=480,wh=226;
    int w,h;
    if(!page||!scr||!vi)return 0;
    w=page->width;h=page->height;orient=w>h;
    if(orient){int t=w;w=h;h=t;}
    if(abs(w-12240)<120 && abs(h-15840)<120)paper=1; else if(abs(w-12240)<120 && abs(h-20160)<120)paper=2; else paper=0;
    memset(&ng,0,sizeof ng);ng.ng_TextAttr=&font_attr;ng.ng_VisualInfo=vi;last=CreateContext(&list);
    ng.ng_Flags=PLACETEXT_LEFT;ng.ng_LeftEdge=180;ng.ng_TopEdge=15;ng.ng_Width=180;ng.ng_Height=18;ng.ng_GadgetText=(STRPTR)"Paper";ng.ng_GadgetID=1;
    last=gpaper=CreateGadget(CYCLE_KIND,last,&ng,GTCY_Labels,(ULONG)paper_labels,GTCY_Active,paper,TAG_DONE);
    ng.ng_TopEdge=45;ng.ng_GadgetText=(STRPTR)"Orientation";ng.ng_GadgetID=2;
    last=gorient=CreateGadget(CYCLE_KIND,last,&ng,GTCY_Labels,(ULONG)orient_labels,GTCY_Active,orient,TAG_DONE);
#define ADD_PINT(var,id,label,top,value) do { ng.ng_Flags=PLACETEXT_LEFT;ng.ng_LeftEdge=180;ng.ng_TopEdge=(top);ng.ng_Width=90;ng.ng_Height=18;ng.ng_GadgetText=(STRPTR)(label);ng.ng_GadgetID=(id);last=(var)=CreateGadget(INTEGER_KIND,last,&ng,GTIN_Number,(LONG)(value),GTIN_MaxChars,5,GA_TabCycle,TRUE,TAG_DONE);} while(0)
    ADD_PINT(gtop,3,"Top margin (mm)",75,twips_to_mm(page->margin_top));
    ADD_PINT(gright,4,"Right margin (mm)",101,twips_to_mm(page->margin_right));
    ADD_PINT(gbottom,5,"Bottom margin (mm)",127,twips_to_mm(page->margin_bottom));
    ADD_PINT(gleft,6,"Left margin (mm)",153,twips_to_mm(page->margin_left));
    ADD_PINT(gstart,7,"Start page",179,page->start_page>0?page->start_page:1);
#undef ADD_PINT
    ng.ng_Flags=0;ng.ng_TopEdge=194;ng.ng_Height=20;ng.ng_Width=78;ng.ng_LeftEdge=310;ng.ng_GadgetText=(STRPTR)"Apply";ng.ng_GadgetID=8;last=gok=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    ng.ng_LeftEdge=394;ng.ng_GadgetText=(STRPTR)"Cancel";ng.ng_GadgetID=9;last=gcancel=CreateGadget(BUTTON_KIND,last,&ng,TAG_DONE);
    if(!gpaper||!gorient||!gtop||!gright||!gbottom||!gleft||!gstart||!gok||!gcancel)goto out;
    rw=OpenWindowTags(NULL,WA_Title,(ULONG)"Page Setup",WA_PubScreen,(ULONG)scr,WA_InnerWidth,ww,WA_InnerHeight,wh,
        WA_Left,(scr->Width-ww)/2,WA_Top,(scr->Height-wh)/2,WA_Gadgets,(ULONG)list,WA_DragBar,TRUE,WA_DepthGadget,TRUE,WA_CloseGadget,TRUE,WA_Activate,TRUE,WA_SimpleRefresh,TRUE,
        WA_IDCMP,IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW|BUTTONIDCMP|INTEGERIDCMP|CYCLEIDCMP,TAG_DONE);
    if(!rw)goto out;GT_RefreshWindow(rw,NULL);
    while(!done){struct IntuiMessage*m;Wait(1UL<<rw->UserPort->mp_SigBit);while((m=GT_GetIMsg(rw->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget*)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(rw);GT_EndRefresh(rw,TRUE);}else if(cls==IDCMP_GADGETUP){if(id==8){ok=1;done=1;}else if(id==9)done=1;}}}
    if(ok){int pw=11906,ph=16838;
        GT_GetGadgetAttrs(gpaper,rw,NULL,GTCY_Active,(ULONG)&v,TAG_DONE);paper=(int)v;if(paper==1){pw=12240;ph=15840;}else if(paper==2){pw=12240;ph=20160;}
        GT_GetGadgetAttrs(gorient,rw,NULL,GTCY_Active,(ULONG)&v,TAG_DONE);if(v){int t=pw;pw=ph;ph=t;}page->width=pw;page->height=ph;
        GT_GetGadgetAttrs(gtop,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE);page->margin_top=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gright,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE);page->margin_right=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gbottom,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE);page->margin_bottom=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gleft,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE);page->margin_left=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gstart,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE);page->start_page=(int)v<1?1:(int)v;
    }
out:
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return ok;
}

static int confirm_discard_changes(void)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0,
        (UBYTE *)"OpenWrite",
        (UBYTE *)"The document has unsaved changes.\n\nDiscard them?",
        (UBYTE *)"Discard|Cancel"
    };
    if (!editor || !ow_editor_is_dirty(editor)) return 1;
    return EasyRequest(win, &es, NULL) != 0;
}

static void new_document(void)
{
    owf_doc *d;
    if (!confirm_discard_changes()) return;
    d = blank_document();
    if (!d) { tell("New document", "There is not enough memory for a new document."); return; }
    set_document(d, "", "ODT");
    recovery_clear();
    set_status("New document.");
    relayout();
}

static void do_open(void)
{
    char path[512];
    if (!confirm_discard_changes()) return;
    if (ask_file(0, path, sizeof path)) {
        open_document(path);
        relayout();
    }
}

static void do_save_as(void)
{
    char path[512];
    if (ask_file(1, path, sizeof path)) {
        save_document_as(path);
        draw_status();
    }
}

static void do_save(void)
{
    if (current_path[0]) {
        save_document_as(current_path);
        draw_status();
    } else do_save_as();
}

static void do_export_pdf(void)
{
    struct FileRequester *fr;
    char path[512], initial[160] = "Untitled.pdf", msg[256];
    const char *name;
    owf_report *report;
    int rc;
    if (!doc) return;
    if (current_path[0]) {
        const char *src = leaf(current_path);
        const char *dot;
        snprintf(initial, sizeof initial, "%s", src);
        dot = strrchr(initial, '.');
        if (dot) initial[(size_t)(dot - initial)] = 0;
        if (strlen(initial) + 4 < sizeof initial) strcat(initial, ".pdf");
    }
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)"Export OpenWrite PDF",
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, TRUE, ASLFR_InitialFile, (ULONG)initial,
        ASLFR_PositiveText, (ULONG)"Export", TAG_DONE);
    if (!fr) { tell("Export PDF", "The file requester could not be opened."); return; }
    if (!AslRequestTags(fr, TAG_DONE) || !fr->fr_File[0]) { FreeAslRequest(fr); return; }
    snprintf(path, sizeof path, "%s", fr->fr_Drawer);
    AddPart((STRPTR)path, fr->fr_File, sizeof path);
    FreeAslRequest(fr);
    name = FilePart((STRPTR)path);
    if (!strrchr(name, '.')) AddPart((STRPTR)path, (STRPTR)"", sizeof path);
    if (!strrchr(FilePart((STRPTR)path), '.')) {
        if (strlen(path) + 4 < sizeof path) strcat(path, ".pdf");
    }
    report = owf_report_new();
    rc = owf_export_file(doc, path, "pdf", report);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "OpenWrite could not export the PDF.\n\n%s", owf_error_text(rc));
        tell("Export PDF", msg);
    } else {
        snprintf(msg, sizeof msg, "%s exported as searchable PDF.", leaf(path));
        set_status(msg); request_document_redraw();
    }
    owf_report_free(report);
}

static void about(void)
{
    tell("OpenWrite",
         VERSION_TEXT "\n\n"
         "A lightweight native word processor for AmigaOS.\n\n"
         "OpenGadTools interface\n"
         "libowf DOCX/ODT/Amiga compatibility\n"
         "OpenRTG-ready page canvas\n"
         "OpenDatatypes and OpenPrint integration points\n\n"
         "MIT licence, Copyright (c) 2026 Dalsin Limited.");
}


static void set_page_view(int page)
{
    int pages = editor ? ow_editor_page_count(editor) : 1;
    if (pages < 1) pages = 1;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    if (page_index != page) scroll_x = scroll_y = 0;
    page_index = page;
    { char msg[64]; snprintf(msg, sizeof msg, "Page %d of %d.", page_index + 1, pages); set_status(msg); }
    draw_all();
}

static void follow_caret_page(void)
{
    int p = editor ? ow_editor_current_page(editor) : 0;
    if (p >= 0 && editor && p < ow_editor_page_count(editor)) page_index = p;
}

static void scroll_page(int dx, int dy)
{
    scroll_x += dx; scroll_y += dy;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_x > page_max_scroll_x) scroll_x = page_max_scroll_x;
    if (scroll_y > page_max_scroll_y) scroll_y = page_max_scroll_y;
    request_document_redraw();
}

static int fit_zoom(int width_only)
{
    int vw, vh, zw, zh, z;
    if (!doc) return 100;
    vw = canvas_box.w - 30;
    vh = canvas_box.h - ruler_box.h - 30;
    if (vw < 80 || vh < 80) return 100;
    zw = vw * 1500 / doc->page.width;
    zh = vh * 1500 / doc->page.height;
    z = width_only ? zw : (zw < zh ? zw : zh);
    if (z < 25) z = 25;
    if (z > 200) z = 200;
    return z;
}

static void set_zoom(int z)
{
    if (z < 25) z = 25;
    if (z > 200) z = 200;
    zoom = z;
    scroll_x = scroll_y = 0;
    if (editor) ow_editor_set_zoom(editor, z);
    relayout();
}

static void editor_repaint(const char *message)
{
    if (message) set_status(message);
    follow_caret_page();
    update_edit_tools();
    if (defer_repaint) { repaint_pending = 1; return; }
    update_window_title();
    ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
    redraw_document_area();
}

static int editor_result(int rc, const char *message)
{
    if (rc == OWF_OK) {
        editor_repaint(message);
        return 1;
    }
    if (rc == OWF_ERR_MEMORY)
        tell("OpenWrite", "There is not enough memory to complete that edit.");
    else
        tell("OpenWrite", owf_error_text(rc));
    return 0;
}

static int copy_selection_to_clipboard(void)
{
    char *text;
    size_t len = 0;
    if (!editor || ow_editor_selection_empty(editor)) {
        set_status("Nothing selected.");
        request_document_redraw();
        return 0;
    }
    text = ow_editor_selection_text(editor, &len);
    if (!text) {
        tell("Clipboard", "OpenWrite could not copy the selected text.");
        return 0;
    }
    if (!clipboard_set_text(text, len)) {
        free(text);
        tell("Clipboard", "clipboard.device could not accept the selected text.");
        return 0;
    }
    free(text);
    set_status("Copied to clipboard.");
    request_document_redraw();
    return 1;
}

static void do_cut(void)
{
    if (!copy_selection_to_clipboard()) return;
    editor_result(ow_editor_delete_forward(editor), "Cut to clipboard.");
}

static void do_copy(void)
{
    copy_selection_to_clipboard();
}

static void do_paste(void)
{
    char *text;
    size_t len = 0;
    text = clipboard_get_text(&len);
    if (!text) {
        set_status("Clipboard has no FTXT text.");
        request_document_redraw();
        return;
    }
    editor_result(ow_editor_insert_text_block(editor, text, len), "Pasted from clipboard.");
    free(text);
}

static void do_select_all(void)
{
    if (!editor) return;
    ow_editor_select_all(editor);
    set_status("Select All.");
    request_document_redraw();
}

static int format_click(int mx, int my)
{
    owf_charfmt cf;
    owf_parafmt pf;
    int i;
    if (!editor || !point_in_box(&format_box, mx, my)) return 0;
    memset(&cf, 0, sizeof cf);
    memset(&pf, 0, sizeof pf);
    ow_editor_current_charfmt(editor, &cf);
    ow_editor_current_parafmt(editor, &pf);

    if (point_in_box(&fmt_style_box, mx, my)) {
        pf.heading = pf.heading >= 3 ? 0 : pf.heading + 1;
        editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_HEADING),
                      pf.heading ? "Heading style." : "Body Text style.");
        return 1;
    }
    if (point_in_box(&fmt_font_box, mx, my)) {
        static const char *names[] = { "CGTriumvirate", "Times New Roman", "Courier New" };
        static const owf_font_kind kinds[] = { OWF_FONT_SANS, OWF_FONT_SERIF, OWF_FONT_MONO };
        int current = -1, next, f;
        const char *name = NULL;
        if (cf.font >= 0 && cf.font < doc->nfonts) name = doc->fonts[cf.font].name;
        for (i = 0; i < 3; ++i) if (name && !strcmp(name, names[i])) current = i;
        next = (current + 1) % 3;
        f = owf_doc_font(doc, names[next], kinds[next]);
        if (f < 0) { tell("Typeface", "There is not enough memory to add that typeface."); return 1; }
        cf.font = f;
        editor_result(ow_editor_apply_charfmt(editor, &cf, OW_CHARFMT_FONT), "Typeface changed.");
        return 1;
    }
    if (point_in_box(&fmt_size_box, mx, my)) {
        static const int pts[] = { 10, 12, 14, 18, 24, 36 };
        int twips = cf.size ? cf.size : (doc->base.size ? doc->base.size : 240);
        int pt = (twips + 10) / 20, next = 0;
        for (i = 0; i < 6; ++i) if (pts[i] <= pt) next = (i + 1) % 6;
        cf.size = pts[next] * OWF_TWIPS_PER_POINT;
        editor_result(ow_editor_apply_charfmt(editor, &cf, OW_CHARFMT_SIZE), "Type size changed.");
        return 1;
    }
    for (i = 0; i < 9; ++i) {
        if (!fmt_button_box[i].w || !point_in_box(&fmt_button_box[i], mx, my)) continue;
        if (i == 0) editor_result(ow_editor_toggle_char_flags(editor, OWF_BOLD), "Bold.");
        else if (i == 1) editor_result(ow_editor_toggle_char_flags(editor, OWF_ITALIC), "Italic.");
        else if (i == 2) editor_result(ow_editor_toggle_char_flags(editor, OWF_UNDERLINE), "Underline.");
        else if (i >= 3 && i <= 6) {
            pf.align = (owf_align)(i - 3);
            editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_ALIGN), "Paragraph alignment changed.");
        } else if (i == 7) {
            editor_result(ow_editor_toggle_list(editor, 0), "Bullets toggled.");
        } else if (i == 8) {
            editor_result(ow_editor_toggle_list(editor, 1), "Numbering toggled.");
        }
        return 1;
    }
    return 1;
}

static int vanilla_utf8(UWORD code, char out[3])
{
    unsigned c = code & 255;
    if (c < 0x80) { out[0] = (char)c; out[1] = 0; return 1; }
    out[0] = (char)(0xc0 | (c >> 6));
    out[1] = (char)(0x80 | (c & 0x3f));
    out[2] = 0;
    return 2;
}

static const char *image_mime_name(const char *path)
{
    const char *e=strrchr(path,'.');if(!e)return "application/octet-stream";
    if(!strcasecmp(e,".png"))return "image/png";
    if(!strcasecmp(e,".jpg")||!strcasecmp(e,".jpeg"))return "image/jpeg";
    if(!strcasecmp(e,".gif"))return "image/gif";
    if(!strcasecmp(e,".webp"))return "image/webp";
    if(!strcasecmp(e,".bmp"))return "image/bmp";
    if(!strcasecmp(e,".iff")||!strcasecmp(e,".ilbm"))return "image/iff";
    return "application/octet-stream";
}

static int ask_image_file(char *out,size_t out_size)
{
    struct FileRequester *fr;int ok=0;
    fr=AllocAslRequestTags(ASL_FileRequest,ASLFR_TitleText,(ULONG)"Insert Image",ASLFR_Window,(ULONG)win,ASLFR_SleepWindow,TRUE,ASLFR_DoSaveMode,FALSE,ASLFR_PositiveText,(ULONG)"Insert",TAG_DONE);
    if(!fr)return 0;
    if(AslRequestTags(fr,TAG_DONE)&&fr->fr_File[0]){snprintf(out,out_size,"%s",fr->fr_Drawer);AddPart((STRPTR)out,fr->fr_File,out_size);ok=1;}
    FreeAslRequest(fr);return ok;
}

static void do_image(void)
{
    char path[512],alt[160]="";BPTR f;LONG size,got;unsigned char *data;int width=4320,height=2880,idx,maxw;Object *o=NULL;struct BitMapHeader *bmh=NULL;
    if(!doc||!editor||!ask_image_file(path,sizeof path))return;
    f=Open((STRPTR)path,MODE_OLDFILE);if(!f){tell("Insert Image","The image file could not be opened.");return;}Seek(f,0,OFFSET_END);size=Seek(f,0,OFFSET_BEGINNING);if(size<=0||size>32*1024*1024){Close(f);tell("Insert Image","The image is empty or too large to embed.");return;}data=(unsigned char*)malloc((size_t)size);if(!data){Close(f);tell("Insert Image","There is not enough memory to embed the image.");return;}got=Read(f,data,size);Close(f);if(got!=size){free(data);tell("Insert Image","The image could not be read completely.");return;}
    if(DataTypesBase){o=NewDTObject((APTR)path,DTA_GroupID,GID_PICTURE,PDTA_DestMode,PMODE_V43,PDTA_Remap,FALSE,TAG_DONE);if(o){GetDTAttrs(o,PDTA_BitMapHeader,(ULONG)&bmh,TAG_DONE);if(bmh&&bmh->bmh_Width&&bmh->bmh_Height){width=(int)bmh->bmh_Width*15;height=(int)bmh->bmh_Height*15;}DisposeDTObject(o);}}
    maxw=doc->page.width-doc->page.margin_left-doc->page.margin_right;if(width>maxw&&width>0){height=(int)((long)height*maxw/width);width=maxw;}
    snprintf(alt,sizeof alt,"%s",leaf(path));idx=owf_doc_add_image(doc,leaf(path),image_mime_name(path),data,(size_t)size,width,height,alt);free(data);if(idx<0){tell("Insert Image","There is not enough memory to add the image.");return;}
    image_cache_clear();if(editor_result(ow_editor_insert_image(editor,idx),"Image inserted through OpenDatatypes."))relayout();
}

static void do_table(void)
{
    char rs[16]="3",cs[16]="3";int rows,cols;
    if(!text_requester("Insert Table","Rows",rs,rs,sizeof rs))return;
    if(!text_requester("Insert Table","Columns",cs,cs,sizeof cs))return;
    rows=atoi(rs);cols=atoi(cs);
    if(rows<1||rows>64||cols<1||cols>32){tell("Insert Table","Rows must be 1-64 and columns 1-32.");return;}
    if(editor_result(ow_editor_insert_table(editor,rows,cols),"Table inserted."))relayout();
}

static void do_link(void)
{
    char url[256]="https://", text[160]="Link";
    if(!editor)return;
    if(!text_requester("Hyperlink","URL",url,url,sizeof url))return;
    if(ow_editor_selection_empty(editor)){
        if(!text_requester("Hyperlink","Text",text,text,sizeof text))return;
        editor_result(ow_editor_insert_link(editor,text,url),"Hyperlink inserted.");
    } else editor_result(ow_editor_set_link(editor,url),"Hyperlink applied.");
}

static void do_story_edit(ow_story_kind which)
{
    char *current = ow_editor_story_text(editor, which);
    char value[512];
    const char *title = which == OW_STORY_HEADER ? "Header" : "Footer";
    int i;
    if (!current) current = (char *)calloc(1, 1);
    snprintf(value, sizeof value, "%s", current ? current : "");
    free(current);
    for (i = 0; value[i]; ++i) if (value[i] == '\n' || value[i] == '\r') value[i] = ' ';
    if (!text_requester(title, "Text / {PAGE} {PAGES} {DATE} {TIME}", value, value, sizeof value)) return;
    if (editor_result(ow_editor_set_story_text(editor, which, value), which == OW_STORY_HEADER ? "Header updated." : "Footer updated.")) relayout();
}

static void do_command(int id)
{
    switch (id) {
    case C_NEW: new_document(); break;
    case C_OPEN: do_open(); break;
    case C_SAVE: do_save(); break;
    case C_PRINT: {
        char msg[220];
        if (ow_print_document(doc, msg, sizeof msg)) {
            set_status(msg); request_document_redraw();
        } else tell("Print", msg);
        break;
    }
    case C_UNDO:
        if (editor && ow_editor_undo(editor) > 0) editor_repaint("Undo.");
        break;
    case C_REDO:
        if (editor && ow_editor_redo(editor) > 0) editor_repaint("Redo.");
        break;
    case C_IMAGE: do_image(); break;
    case C_TABLE: do_table(); break;
    case C_PDF: do_export_pdf(); break;
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

static void do_find_again(int backwards)
{
    int rc;
    if (!editor) return;
    if (!last_find[0]) {
        if (!text_requester("Find", "Find", "", last_find, sizeof last_find)) return;
    }
    rc = ow_editor_find(editor, last_find, backwards, 0, 1);
    if (rc > 0) {
        follow_caret_page();
        set_status(backwards ? "Previous match." : "Match found.");
        request_document_redraw();
    } else if (!rc) {
        char msg[220]; snprintf(msg, sizeof msg, "Cannot find: %s", last_find);
        set_status(msg); request_document_redraw(); DisplayBeep(scr);
    } else tell("Find", "There is not enough memory to search this document.");
}

static void do_find_prompt(void)
{
    if (text_requester("Find", "Find", last_find, last_find, sizeof last_find))
        do_find_again(0);
}

static void do_replace(void)
{
    int mode = replace_requester(last_find, sizeof last_find,
                                 last_replace, sizeof last_replace);
    if (!mode || !editor) return;
    if (mode == 1) {
        int rc = ow_editor_find(editor, last_find, 0, 0, 1);
        if (rc > 0) editor_result(ow_editor_insert_text_block(editor, last_replace,
                                  strlen(last_replace)), "Replaced one match.");
        else if (!rc) { set_status("No match to replace."); request_document_redraw(); DisplayBeep(scr); }
        else tell("Replace", "There is not enough memory to search this document.");
    } else {
        ow_selection start;
        int count = 0, rc;
        memset(&start, 0, sizeof start);
        ow_editor_set_selection(editor, &start);
        while (count < 10000 && (rc = ow_editor_find(editor, last_find, 0, 0, 0)) > 0) {
            if (ow_editor_insert_text_block(editor, last_replace, strlen(last_replace)) != OWF_OK) {
                tell("Replace All", "There is not enough memory to continue replacing.");
                break;
            }
            ++count;
        }
        {
            char msg[96]; snprintf(msg, sizeof msg, "Replaced %d occurrence%s.", count, count == 1 ? "" : "s");
            editor_repaint(msg);
        }
    }
}

static void set_heading_level(int level)
{
    owf_parafmt pf;
    if (!editor || !ow_editor_current_parafmt(editor, &pf)) return;
    pf.heading = level;
    editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_HEADING),
                  level ? "Heading style." : "Body Text style.");
}

static void set_paragraph_alignment(owf_align align)
{
    owf_parafmt pf;
    if (!editor || !ow_editor_current_parafmt(editor, &pf)) return;
    pf.align = align;
    editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_ALIGN),
                  "Paragraph alignment changed.");
}

static void menu_action(ULONG id)
{
    switch (id) {
    case M_NEW: new_document(); break;
    case M_OPEN: do_open(); break;
    case M_SAVE: do_save(); break;
    case M_SAVE_AS: do_save_as(); break;
    case M_PRINT: do_command(C_PRINT); break;
    case M_PDF: do_command(C_PDF); break;
    case M_ABOUT: about(); break;
    case M_QUIT: if (confirm_discard_changes()) quit_now = 1; break;
    case M_UNDO: do_command(C_UNDO); break;
    case M_REDO: do_command(C_REDO); break;
    case M_CUT: do_cut(); break;
    case M_COPY: do_copy(); break;
    case M_PASTE: do_paste(); break;
    case M_SELECT_ALL: do_select_all(); break;
    case M_FIND: do_find_prompt(); break;
    case M_FIND_NEXT: do_find_again(0); break;
    case M_FIND_PREV: do_find_again(1); break;
    case M_REPLACE: do_replace(); break;
    case M_PAGE_FIRST: set_page_view(0); break;
    case M_PAGE_PREV: set_page_view(page_index - 1); break;
    case M_PAGE_NEXT: set_page_view(page_index + 1); break;
    case M_PAGE_LAST: set_page_view(editor ? ow_editor_page_count(editor) - 1 : 0); break;
    case M_ZOOM_IN: set_zoom(zoom + 10); break;
    case M_ZOOM_OUT: set_zoom(zoom - 10); break;
    case M_ZOOM_100: set_zoom(100); break;
    case M_ZOOM_FIT_PAGE: set_zoom(fit_zoom(0)); break;
    case M_ZOOM_FIT_WIDTH: set_zoom(fit_zoom(1)); break;
    case M_NAV: show_nav = !show_nav; relayout(); break;
    case M_INSPECTOR: show_inspector = !show_inspector; relayout(); break;
    case M_IMAGE: do_command(C_IMAGE); break;
    case M_TABLE: do_command(C_TABLE); break;
    case M_LINK: do_link(); break;
    case M_PAGE_BREAK: editor_result(ow_editor_insert_page_break(editor), "Page break inserted."); break;
    case M_BOLD: editor_result(ow_editor_toggle_char_flags(editor, OWF_BOLD), "Bold."); break;
    case M_ITALIC: editor_result(ow_editor_toggle_char_flags(editor, OWF_ITALIC), "Italic."); break;
    case M_UNDERLINE: editor_result(ow_editor_toggle_char_flags(editor, OWF_UNDERLINE), "Underline."); break;
    case M_ALIGN_LEFT: set_paragraph_alignment(OWF_ALIGN_LEFT); break;
    case M_ALIGN_CENTRE: set_paragraph_alignment(OWF_ALIGN_CENTRE); break;
    case M_ALIGN_RIGHT: set_paragraph_alignment(OWF_ALIGN_RIGHT); break;
    case M_ALIGN_JUSTIFY: set_paragraph_alignment(OWF_ALIGN_JUSTIFY); break;
    case M_STYLE_BODY: set_heading_level(0); break;
    case M_STYLE_H1: set_heading_level(1); break;
    case M_STYLE_H2: set_heading_level(2); break;
    case M_STYLE_H3: set_heading_level(3); break;
    case M_BULLETS: editor_result(ow_editor_toggle_list(editor, 0), "Bullets toggled."); break;
    case M_NUMBERING: editor_result(ow_editor_toggle_list(editor, 1), "Numbering toggled."); break;
    case M_PARAGRAPH: {
        owf_parafmt pf;
        if (editor && ow_editor_current_parafmt(editor, &pf) && paragraph_requester(&pf))
            editor_result(ow_editor_apply_parafmt(editor, &pf,
                OW_PARAFMT_HEADING | OW_PARAFMT_ALIGN | OW_PARAFMT_INDENTS | OW_PARAFMT_SPACING),
                "Paragraph updated.");
        break;
    }
    case M_TABLE_NEXT: if(!ow_editor_table_move(editor,1))set_status("Last table cell."); request_document_redraw(); break;
    case M_TABLE_PREV: if(!ow_editor_table_move(editor,-1))set_status("First table cell."); request_document_redraw(); break;
    case M_TABLE_ROW_ADD: editor_result(ow_editor_table_insert_row(editor),"Table row inserted."); break;
    case M_TABLE_ROW_DEL: editor_result(ow_editor_table_delete_row(editor),"Table row deleted."); break;
    case M_TABLE_COL_ADD: editor_result(ow_editor_table_insert_column(editor),"Table column inserted."); break;
    case M_TABLE_COL_DEL: editor_result(ow_editor_table_delete_column(editor),"Table column deleted."); break;
    case M_PAGE_SETUP: {
        owf_page pg;
        if (editor) {
            ow_editor_page_setup(editor, &pg);
            if (page_setup_requester(&pg)) {
                int rc = ow_editor_apply_page_setup(editor, &pg);
                if (rc == OWF_OK) { scroll_x = scroll_y = 0; editor_repaint("Page setup updated."); }
                else if (rc == OWF_ERR_MEMORY) tell("Page Setup", "There is not enough memory to apply page setup.");
                else tell("Page Setup", "Those margins do not leave a usable page area.");
            }
        }
        break;
    }
    case M_HEADER: do_story_edit(OW_STORY_HEADER); break;
    case M_FOOTER: do_story_edit(OW_STORY_FOOTER); break;
    case M_FIELD_PAGE: editor_result(ow_editor_insert_field(editor, OWF_FIELD_PAGE), "Page field inserted."); break;
    case M_FIELD_PAGES: editor_result(ow_editor_insert_field(editor, OWF_FIELD_PAGES), "Page-count field inserted."); break;
    case M_FIELD_DATE: editor_result(ow_editor_insert_field(editor, OWF_FIELD_DATE), "Date field inserted."); break;
    case M_FIELD_TIME: editor_result(ow_editor_insert_field(editor, OWF_FIELD_TIME), "Time field inserted."); break;
    case M_DATATYPES: do_command(C_IMAGE); break;
    case M_OPENPRINT: do_command(C_PRINT); break;
    case M_SPELL: do_spell_check(); break;
    case M_THEME_OPEN: apply_theme("Open"); break;
    case M_THEME_GRAPHITE: apply_theme("Graphite"); break;
    case M_THEME_EMBER: apply_theme("Ember"); break;
    case M_THEME_CLEAR: apply_theme("Clear"); break;
    case M_THEME_CLASSIC: apply_theme("Classic"); break;
    case M_TB_BOTH: tb_style = OGT_TB_ICONS_TEXT; relayout(); break;
    case M_TB_ICONS: tb_style = OGT_TB_ICONS; relayout(); break;
    case M_TB_TEXT: tb_style = OGT_TB_TEXT; relayout(); break;
    }
}

static const char *skip_space(const char *s)
{
    while (s && *s && isspace((unsigned char)*s)) ++s;
    return s ? s : "";
}

static int starts_word_ci(const char *s, const char *word, const char **rest)
{
    size_t n=strlen(word),i;
    for(i=0;i<n;++i)if(!s[i]||tolower((unsigned char)s[i])!=tolower((unsigned char)word[i]))return 0;
    if(s[n]&&!isspace((unsigned char)s[n]))return 0;
    if(rest)*rest=skip_space(s+n);
    return 1;
}

static char *document_plain_text(void)
{
    size_t cap=4096,n=0;char *out=(char*)malloc(cap);int p,r;
    if(!out||!doc){free(out);return NULL;}out[0]=0;
    for(p=0;p<doc->body.nparas;++p){
        const owf_para *para=&doc->body.paras[p];
        for(r=0;r<para->nruns;++r){const owf_run *run=&para->runs[r];const char *t=NULL;char field[32];size_t add;
            if(run->kind==OWF_RUN_TEXT)t=run->text;
            else if(run->kind==OWF_RUN_TAB)t="\t";
            else if(run->kind==OWF_RUN_LINEBREAK)t="\n";
            else if(run->kind==OWF_RUN_FIELD){if(run->field==OWF_FIELD_PAGE)snprintf(field,sizeof field,"%d",ow_editor_current_page(editor)+1);else if(run->field==OWF_FIELD_PAGES)snprintf(field,sizeof field,"%d",ow_editor_page_count(editor));else field[0]=0;t=field;}
            if(!t)continue;
            add=strlen(t);
            if(n+add+2>cap){size_t want=(n+add+2)*2;char *q=(char*)realloc(out,want);if(!q){free(out);return NULL;}out=q;cap=want;}
            memcpy(out+n,t,add);n+=add;out[n]=0;
        }
        if(p+1<doc->body.nparas){if(n+2>cap){char*q=(char*)realloc(out,cap*2);if(!q){free(out);return NULL;}out=q;cap*=2;}out[n++]='\n';out[n]=0;}
    }
    return out;
}

static int rexx_do_command(const char *command, char **result_out)
{
    const char *arg="";char result[512];int ok=1;char *dynamic=NULL;
    result[0]=0;command=skip_space(command);
    if(starts_word_ci(command,"VERSION",&arg))snprintf(result,sizeof result,"%s",VERSION_TEXT);
    else if(starts_word_ci(command,"WORDCOUNT",&arg))snprintf(result,sizeof result,"%d",word_count());
    else if(starts_word_ci(command,"PAGECOUNT",&arg))snprintf(result,sizeof result,"%d",editor?ow_editor_page_count(editor):0);
    else if(starts_word_ci(command,"OPEN",&arg)){if(!*arg|| (editor&&ow_editor_is_dirty(editor))){ok=0;snprintf(result,sizeof result,"Unsaved changes or missing path");}else{open_document(arg);relayout();snprintf(result,sizeof result,"%s",status);}}
    else if(starts_word_ci(command,"SAVEAS",&arg)){if(!*arg){ok=0;snprintf(result,sizeof result,"Missing path");}else{save_document_as(arg);snprintf(result,sizeof result,"%s",status);}}
    else if(starts_word_ci(command,"SAVE",&arg)){if(current_path[0]){save_document_as(current_path);snprintf(result,sizeof result,"%s",status);}else{ok=0;snprintf(result,sizeof result,"Untitled document needs SAVEAS");}}
    else if(starts_word_ci(command,"EXPORT",&arg)){owf_report *rp;if(!*arg){ok=0;snprintf(result,sizeof result,"Missing export path");}else{rp=owf_report_new();ok=owf_export_file(doc,arg,NULL,rp)==OWF_OK;owf_report_free(rp);snprintf(result,sizeof result,"%s",ok?"Exported":"Export failed");}}
    else if(starts_word_ci(command,"PRINT",&arg)){ok=ow_print_document(doc,result,sizeof result);}
    else if(starts_word_ci(command,"INSERTTEXT",&arg)){ok=editor_result(ow_editor_insert_text_block(editor,arg,strlen(arg)),"ARexx text inserted.");snprintf(result,sizeof result,"%s",ok?"Inserted":"Insert failed");}
    else if(starts_word_ci(command,"FIND",&arg)){if(!*arg){ok=0;snprintf(result,sizeof result,"Missing search text");}else{int f=ow_editor_find(editor,arg,0,0,1);ok=f>0;request_document_redraw();snprintf(result,sizeof result,"%s",ok?"Found":"Not found");}}
    else if(starts_word_ci(command,"GETTEXT",&arg)){dynamic=document_plain_text();if(!dynamic){ok=0;snprintf(result,sizeof result,"Out of memory");}}
    else if(starts_word_ci(command,"COMMAND",&arg)){
        if(!strcasecmp(arg,"BOLD"))menu_action(M_BOLD);else if(!strcasecmp(arg,"ITALIC"))menu_action(M_ITALIC);else if(!strcasecmp(arg,"UNDERLINE"))menu_action(M_UNDERLINE);else if(!strcasecmp(arg,"PAGEBREAK"))menu_action(M_PAGE_BREAK);else if(!strcasecmp(arg,"UNDO"))menu_action(M_UNDO);else if(!strcasecmp(arg,"REDO"))menu_action(M_REDO);else{ok=0;snprintf(result,sizeof result,"Unknown COMMAND");}
        if(ok&&!result[0])snprintf(result,sizeof result,"OK");
    }
    else if(starts_word_ci(command,"QUIT",&arg)){if(editor&&ow_editor_is_dirty(editor)){ok=0;snprintf(result,sizeof result,"Unsaved changes");}else{quit_now=1;snprintf(result,sizeof result,"Quitting");}}
    else {ok=0;snprintf(result,sizeof result,"Unknown OpenWrite ARexx command");}
    if(result_out){if(dynamic)*result_out=dynamic;else{*result_out=(char*)malloc(strlen(result)+1);if(*result_out)strcpy(*result_out,result);else ok=0;}}
    return ok;
}

static void rexx_messages(void)
{
    struct RexxMsg *rm;
    while(rexx_port&&(rm=(struct RexxMsg*)GetMsg(rexx_port))!=NULL){char *result=NULL;int ok=rexx_do_command((const char*)rm->rm_Args[0],&result);rm->rm_Result1=ok?RC_OK:RC_ERROR;rm->rm_Result2=0;if((rm->rm_Action&RXFF_RESULT)&&RexxSysBase&&result)rm->rm_Result2=(LONG)CreateArgstring((STRPTR)result,(LONG)strlen(result));free(result);ReplyMsg((struct Message*)rm);}
}

static void rexx_open(void)
{
    struct MsgPort *p;if(!RexxSysBase)return;p=CreateMsgPort();if(!p)return;p->mp_Node.ln_Name=(char*)REXX_PORT_NAME;p->mp_Node.ln_Pri=0;Forbid();if(FindPort((STRPTR)REXX_PORT_NAME)){Permit();DeleteMsgPort(p);return;}AddPort(p);Permit();rexx_port=p;
}

static void rexx_close(void)
{
    struct RexxMsg *rm;if(!rexx_port)return;RemPort(rexx_port);while((rm=(struct RexxMsg*)GetMsg(rexx_port))!=NULL){rm->rm_Result1=RC_FATAL;rm->rm_Result2=0;ReplyMsg((struct Message*)rm);}DeleteMsgPort(rexx_port);rexx_port=NULL;
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
        GT_ReplyIMsg(im);
        switch (class) {
        case IDCMP_CLOSEWINDOW:
            if (confirm_discard_changes()) quit_now = 1;
            break;
        case IDCMP_NEWSIZE:
            relayout();
            break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win); draw_all(); GT_EndRefresh(win, TRUE);
            break;
        case IDCMP_GADGETDOWN:
        case IDCMP_GADGETUP:
            if (g && g->GadgetID >= GID_TOOLBAR) toolbar_event(class, g);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN && point_in_box(&navigator_box, mx, my)) {
                int ni;
                for (ni = 0; ni < nav_page_slots; ++ni)
                    if (point_in_box(&nav_page_box[ni], mx, my)) { set_page_view(nav_page_number[ni]); break; }
            } else if (code == SELECTDOWN && point_in_box(&format_box, mx, my)) {
                format_click(mx, my);
            } else if (code == SELECTDOWN && point_in_box(&page_box, mx, my)) {
                mouse_selecting = 1;
                mouse_set_selection(mx, my,
                    (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0);
            } else if (code == SELECTUP) {
                if (mouse_selecting) {
                    mouse_set_selection(mx, my, 1);
                    mouse_selecting = 0;
                }
                if (tb.pressed >= 0) {
                    ogt_toolbar_draw_one(&tb, &ctx, win->RPort,
                                         tb.pressed, 0, "window");
                    tb.pressed = -1;
                }
            }
            break;
        case IDCMP_MOUSEMOVE:
            if (mouse_selecting) mouse_set_selection(mx, my, 1);
            break;
        case IDCMP_RAWKEY:
            if (!(code & 0x80) && editor) {
                int extend = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
                int alt = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
                int ramiga = (qual & IEQUALIFIER_RCOMMAND) != 0;
                if (alt) {
                    if (code == 0x4f) scroll_page(-48, 0);
                    else if (code == 0x4e) scroll_page(48, 0);
                    else if (code == 0x4c) scroll_page(0, -48);
                    else if (code == 0x4d) scroll_page(0, 48);
                } else if (ramiga) {
                    if (code == 0x4f) set_page_view(0);
                    else if (code == 0x4e) set_page_view(ow_editor_page_count(editor) - 1);
                    else if (code == 0x4c) set_page_view(page_index - 1);
                    else if (code == 0x4d) set_page_view(page_index + 1);
                    else if (code == 0x44) editor_result(ow_editor_insert_page_break(editor), "Page break inserted.");
                } else switch (code) {
                case 0x4f: ow_editor_move_caret(editor, OW_MOVE_LEFT, extend); follow_caret_page(); request_document_redraw(); break;
                case 0x4e: ow_editor_move_caret(editor, OW_MOVE_RIGHT, extend); follow_caret_page(); request_document_redraw(); break;
                case 0x4c: ow_editor_move_caret(editor, OW_MOVE_UP, extend); follow_caret_page(); request_document_redraw(); break;
                case 0x4d: ow_editor_move_caret(editor, OW_MOVE_DOWN, extend); follow_caret_page(); request_document_redraw(); break;
                case 0x46: editor_result(ow_editor_delete_forward(editor), "Modified."); break;
                }
            }
            break;
        case IDCMP_VANILLAKEY:
            if (!editor) break;
            if (qual & (IEQUALIFIER_CONTROL | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
                break;
            if (code == 8) {
                editor_result(ow_editor_backspace(editor), "Modified.");
            } else if (code == 13) {
                if (ow_editor_in_table(editor)) editor_result(ow_editor_insert_utf8(editor, "\n", 1), "Table cell line break.");
                else editor_result(ow_editor_newline(editor), "Modified.");
            } else if (code == 127) {
                editor_result(ow_editor_delete_forward(editor), "Modified.");
            } else if (code == 9) {
                if(ow_editor_in_table(editor)){int backwards=(qual&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT))!=0;if(!ow_editor_table_move(editor,backwards?-1:1)&&!backwards)editor_result(ow_editor_table_insert_row(editor),"New table row.");else request_document_redraw();}
                else editor_result(ow_editor_insert_utf8(editor, "\t", 1), "Modified.");
            } else if (code >= 32 && code < 256) {
                char u[3];
                int n = vanilla_utf8(code, u);
                editor_result(ow_editor_insert_utf8(editor, u, (size_t)n), "Modified.");
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
    defer_repaint = 0;
    if (repaint_pending && win) {
        repaint_pending = 0;
        update_window_title();
        update_edit_tools();
        ogt_toolbar_draw(&tb, &ctx, win->RPort, "window");
        redraw_document_area();
    }
}

static int window_main(int argc, char **argv)
{
    int rc = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)"OpenWrite needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }

    DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36);
    LayersBase = OpenLibrary((STRPTR)"layers.library", 36);

    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
    read_theme_choice();
    set_document(blank_document(), "", "ODT");
    if (!doc || !editor) { rc = 20; goto out; }

    if (argc > 1) snprintf(first, sizeof first, "%s", argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 &&
            NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }

    if (!open_window()) {
        if (argc) PutStr((STRPTR)"OpenWrite: the window could not open\n");
        rc = 20;
        goto out;
    }
    rexx_open();
    autosave = ow_autosave_open(60);
    if (first[0]) { open_document(first); relayout(); }
    else recovery_offer();

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
    rexx_close();
    close_window();
    if (spell) { ow_spell_close(spell); spell = NULL; }
    if (editor) { ow_editor_free(editor); editor = NULL; }
    if (doc) { owf_doc_free(doc); doc = NULL; }
    if (theme.text) ogt_theme_free(&theme);
    if (LayersBase) { CloseLibrary(LayersBase); LayersBase = NULL; }
    if (DiskfontBase) { CloseLibrary(DiskfontBase); DiskfontBase = NULL; }
    image_cache_clear();
    if (CyberGfxBase) { CloseLibrary(CyberGfxBase); CyberGfxBase = NULL; }
    if (DataTypesBase) { CloseLibrary(DataTypesBase); DataTypesBase = NULL; }
    if (RexxSysBase) { CloseLibrary((struct Library *)RexxSysBase); RexxSysBase = NULL; }
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}

int main(int argc, char **argv)
{
    return ow_run_with_stack(65536, window_main, argc, argv);
}
