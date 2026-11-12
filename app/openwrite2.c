/*
 * GFOpenWrite - the native OpenAmigaWriter shell.
 *
 * First build: GFOpenGadTools chrome, libowf documents, a lightweight native
 * page canvas, DOCX/ODT/classic-Amiga Open/Save, navigator, inspector and
 * status line.  GFOpenRTG is used by GFOpenGadTools automatically on true-colour
 * screens; the page renderer remains portable to AGA.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <utility/tagitem.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <dos/var.h>
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
#include "ogt_openfont.h"
#include "ow_clipboard.h"
#include <proto/diskfont.h>
#include <proto/layers.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <clib/alib_protos.h>

#include "openwrite_core.h"
#include "owf_open.h"
#include "ow_layout.h"
#include "ow_stack.h"
#include "lastdir.h"
#include "ow_print.h"
#include "ow_spell.h"
#include "ow_fontwidths.h"
#include "ow_autosave.h"
#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_iconset.h"
#include "ogt_font.h"
#include "ogt_list.h"
#include "ogt_pageview.h"
#include "ogt_ruler.h"
#include "ogt_sections.h"
#include "ogt_status.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *OpenFontTextBase = NULL, *LayersBase = NULL;
struct Library *DataTypesBase = NULL, *CyberGfxBase = NULL;
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "GFOpenWrite 2.0.32 (12.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " MIT, Copyright (c) 2026 AmiCradle Team";

enum {
    C_NEW = 1, C_OPEN, C_SAVE, C_PRINT, C_UNDO, C_REDO,
    C_IMAGE, C_TABLE, C_PDF
};
enum { GID_TOOLBAR = 100 };
/* 2.0: the shell's gadgets: the format row, the navigator, the page view, the inspector */
enum { GID_STYLE = 300, GID_FONT, GID_SIZE, GID_ZOOM, GID_NAV = 400, GID_VIEW = 410,
       GID_I_STYLE = 500, GID_I_ALIGN, GID_I_BEFORE, GID_I_AFTER, GID_I_SPACING, GID_I_FONT, GID_I_SIZE, GID_I_BOLD, GID_I_ITALIC,
       GID_I_PAPER, GID_I_MARGIN };

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVE_AS, M_PRINT, M_PDF, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_SELECT_ALL,
    M_FIND, M_FIND_NEXT, M_FIND_PREV, M_REPLACE,
    M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_100, M_ZOOM_FIT_PAGE, M_ZOOM_FIT_WIDTH,
    M_PAGE_FIRST, M_PAGE_PREV, M_PAGE_NEXT, M_PAGE_LAST,
    M_NAV, M_INSPECTOR,
    M_IMAGE, M_IMAGE_FLOAT, M_TEXT_BOX, M_TABLE, M_LINK, M_CONTENTS, M_PAGE_BREAK,
    M_FIELD_PAGE, M_FIELD_PAGES, M_FIELD_DATE, M_FIELD_TIME,
    M_PARAGRAPH, M_BOLD, M_ITALIC, M_UNDERLINE,
    M_TABLE_NEXT, M_TABLE_PREV, M_TABLE_STYLE, M_TABLE_ROW_ADD, M_TABLE_ROW_DEL, M_TABLE_COL_ADD, M_TABLE_COL_DEL,
    M_ALIGN_LEFT, M_ALIGN_CENTRE, M_ALIGN_RIGHT, M_ALIGN_JUSTIFY,
    M_STYLE_BODY, M_STYLE_H1, M_STYLE_H2, M_STYLE_H3, M_BULLETS, M_NUMBERING,
    M_PAGE_SETUP, M_COLUMNS, M_FRAME, M_HEADER, M_FOOTER, M_HEADER_LEFT, M_FOOTER_LEFT,
    M_DATATYPES, M_OPENPRINT, M_SPELL,
    M_THEME_OPEN, M_THEME_GRAPHITE, M_THEME_EMBER, M_THEME_CLEAR, M_THEME_CLASSIC,
    M_TB_BOTH, M_TB_ICONS, M_TB_TEXT,
    M_TRACK, M_CHANGE_NEXT, M_CHANGE_PREV, M_CHANGE_ACCEPT, M_CHANGE_REJECT, M_CHANGE_ACCEPT_ALL,
    M_CHANGE_REJECT_ALL, M_REVIEW_AUTHOR,
    M_COMMENT, M_COMMENT_NEXT, M_COMMENT_PREV, M_COMMENT_REPLY, M_COMMENT_EDIT, M_COMMENT_DONE, M_COMMENT_DELETE,
    M_FIELD_TITLE, M_FIELD_AUTHOR, M_FIELD_SUBJECT, M_FIELD_FILENAME, M_FIELD_WORDS, M_FIELD_CHAPTER, M_FIELD_USER,
    M_CROSS_REF, M_DOC_INFO, M_USER_FIELDS, M_UPDATE_FIELDS,
    M_FOOTNOTE, M_ENDNOTE, M_NOTE_NEXT, M_NOTE_PREV, M_NOTE_EDIT, M_NOTE_CONVERT, M_NOTE_DELETE,
    M_MERGE_SOURCE, M_MERGE_FIELD, M_MERGE_NEXT, M_MERGE_PREV, M_MERGE_NAMES, M_MERGE_DOC, M_MERGE_FILES,
    M_INDEX_ENTRY, M_INDEX
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", NULL, 0, 0, (APTR)M_SAVE_AS },
    { NM_ITEM, "Document Info...", NULL, 0, 0, (APTR)M_DOC_INFO },
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
    { NM_SUB, "Icons and text", NULL, CHECKIT, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons only", NULL, CHECKIT | CHECKED, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text only", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },
    { NM_ITEM, "Theme", NULL, 0, 0, NULL },
    { NM_SUB, "Open", NULL, 0, 0, (APTR)M_THEME_OPEN },
    { NM_SUB, "Graphite", NULL, 0, 0, (APTR)M_THEME_GRAPHITE },
    { NM_SUB, "Ember", NULL, 0, 0, (APTR)M_THEME_EMBER },
    { NM_SUB, "Clear", NULL, 0, 0, (APTR)M_THEME_CLEAR },
    { NM_SUB, "Classic", NULL, 0, 0, (APTR)M_THEME_CLASSIC },

    { NM_TITLE, "Insert", NULL, 0, 0, NULL },
    { NM_ITEM, "Image...", NULL, 0, 0, (APTR)M_IMAGE },
    { NM_ITEM, "Floating Image...", NULL, 0, 0, (APTR)M_IMAGE_FLOAT },
    { NM_ITEM, "Text Box...", NULL, 0, 0, (APTR)M_TEXT_BOX },
    { NM_ITEM, "Table...", NULL, 0, 0, (APTR)M_TABLE },
    { NM_ITEM, "Link...", NULL, 0, 0, (APTR)M_LINK },
    { NM_ITEM, "Table of Contents", NULL, 0, 0, (APTR)M_CONTENTS },
    { NM_ITEM, "Alphabetical Index", NULL, 0, 0, (APTR)M_INDEX },          /* 2.0.32 (core 1.7) */
    { NM_ITEM, "Index Entry...", NULL, 0, 0, (APTR)M_INDEX_ENTRY },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Page Break", NULL, 0, 0, (APTR)M_PAGE_BREAK },
    { NM_ITEM, "Comment...", NULL, 0, 0, (APTR)M_COMMENT },
    { NM_ITEM, "Footnote...", NULL, 0, 0, (APTR)M_FOOTNOTE },     /* 2.0.30 (core 1.5) */
    { NM_ITEM, "Endnote...", NULL, 0, 0, (APTR)M_ENDNOTE },
    { NM_ITEM, "Field", NULL, 0, 0, NULL },
    { NM_SUB, "Page Number", NULL, 0, 0, (APTR)M_FIELD_PAGE },
    { NM_SUB, "Page Count", NULL, 0, 0, (APTR)M_FIELD_PAGES },
    { NM_SUB, "Date", NULL, 0, 0, (APTR)M_FIELD_DATE },
    { NM_SUB, "Time", NULL, 0, 0, (APTR)M_FIELD_TIME },
    /* 2.0.29: fields with a value (core 1.4) */
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Title", NULL, 0, 0, (APTR)M_FIELD_TITLE },
    { NM_SUB, "Author", NULL, 0, 0, (APTR)M_FIELD_AUTHOR },
    { NM_SUB, "Subject", NULL, 0, 0, (APTR)M_FIELD_SUBJECT },
    { NM_SUB, "File Name", NULL, 0, 0, (APTR)M_FIELD_FILENAME },
    { NM_SUB, "Word Count", NULL, 0, 0, (APTR)M_FIELD_WORDS },
    { NM_SUB, "Chapter", NULL, 0, 0, (APTR)M_FIELD_CHAPTER },
    { NM_SUB, "User Field...", NULL, 0, 0, (APTR)M_FIELD_USER },
    { NM_ITEM, "Cross-reference...", NULL, 0, 0, (APTR)M_CROSS_REF },

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
    { NM_ITEM, "Cell Style...", NULL, 0, 0, (APTR)M_TABLE_STYLE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Insert Row Below", NULL, 0, 0, (APTR)M_TABLE_ROW_ADD },
    { NM_ITEM, "Delete Row", NULL, 0, 0, (APTR)M_TABLE_ROW_DEL },
    { NM_ITEM, "Insert Column Right", NULL, 0, 0, (APTR)M_TABLE_COL_ADD },
    { NM_ITEM, "Delete Column", NULL, 0, 0, (APTR)M_TABLE_COL_DEL },

    { NM_TITLE, "Layout", NULL, 0, 0, NULL },
    { NM_ITEM, "Page Setup...", NULL, 0, 0, (APTR)M_PAGE_SETUP },
    { NM_ITEM, "Document Columns...", NULL, 0, 0, (APTR)M_COLUMNS },
    { NM_ITEM, "Frame...", NULL, 0, 0, (APTR)M_FRAME },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Header...", NULL, 0, 0, (APTR)M_HEADER },
    { NM_ITEM, "Footer...", NULL, 0, 0, (APTR)M_FOOTER },
    { NM_ITEM, "Left-Page Header...", NULL, 0, 0, (APTR)M_HEADER_LEFT },
    { NM_ITEM, "Left-Page Footer...", NULL, 0, 0, (APTR)M_FOOTER_LEFT },

    { NM_TITLE, "Datatypes", NULL, 0, 0, NULL },
    { NM_ITEM, "GFOpenDatatypes...", NULL, 0, 0, (APTR)M_DATATYPES },

    { NM_TITLE, "Tools", NULL, 0, 0, NULL },
    { NM_ITEM, "Spell Check...", NULL, 0, 0, (APTR)M_SPELL },
    /* 2.0.27: tracked changes (core 1.2), in Tools so the menu bar still fits a 640 screen */
    { NM_ITEM, "Track Changes", NULL, 0, 0, NULL },
    { NM_SUB, "Record Changes", NULL, CHECKIT | MENUTOGGLE, 0, (APTR)M_TRACK },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Next Change", NULL, 0, 0, (APTR)M_CHANGE_NEXT },
    { NM_SUB, "Previous Change", NULL, 0, 0, (APTR)M_CHANGE_PREV },
    { NM_SUB, "Accept Change", NULL, 0, 0, (APTR)M_CHANGE_ACCEPT },
    { NM_SUB, "Reject Change", NULL, 0, 0, (APTR)M_CHANGE_REJECT },
    { NM_SUB, "Accept All", NULL, 0, 0, (APTR)M_CHANGE_ACCEPT_ALL },
    { NM_SUB, "Reject All", NULL, 0, 0, (APTR)M_CHANGE_REJECT_ALL },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Review Author...", NULL, 0, 0, (APTR)M_REVIEW_AUTHOR },
    /* 2.0.28: comments (core 1.3) */
    { NM_ITEM, "Comments", NULL, 0, 0, NULL },
    { NM_SUB, "Next Comment", NULL, 0, 0, (APTR)M_COMMENT_NEXT },
    { NM_SUB, "Previous Comment", NULL, 0, 0, (APTR)M_COMMENT_PREV },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Reply...", NULL, 0, 0, (APTR)M_COMMENT_REPLY },
    { NM_SUB, "Edit Comment...", NULL, 0, 0, (APTR)M_COMMENT_EDIT },
    { NM_SUB, "Mark Done", NULL, 0, 0, (APTR)M_COMMENT_DONE },
    { NM_SUB, "Delete Comment", NULL, 0, 0, (APTR)M_COMMENT_DELETE },
    /* 2.0.30: footnotes and endnotes (core 1.5) */
    { NM_ITEM, "Notes", NULL, 0, 0, NULL },
    { NM_SUB, "Next Note", NULL, 0, 0, (APTR)M_NOTE_NEXT },
    { NM_SUB, "Previous Note", NULL, 0, 0, (APTR)M_NOTE_PREV },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Edit Note...", NULL, 0, 0, (APTR)M_NOTE_EDIT },
    { NM_SUB, "Footnote <-> Endnote", NULL, 0, 0, (APTR)M_NOTE_CONVERT },
    { NM_SUB, "Delete Note", NULL, 0, 0, (APTR)M_NOTE_DELETE },
    /* 2.0.31: mail merge (core 1.6) */
    { NM_ITEM, "Mail Merge", NULL, 0, 0, NULL },
    { NM_SUB, "Data Source...", NULL, 0, 0, (APTR)M_MERGE_SOURCE },
    { NM_SUB, "Insert Merge Field...", NULL, 0, 0, (APTR)M_MERGE_FIELD },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Next Record", NULL, 0, 0, (APTR)M_MERGE_NEXT },
    { NM_SUB, "Previous Record", NULL, 0, 0, (APTR)M_MERGE_PREV },
    { NM_SUB, "Show Field Names", NULL, 0, 0, (APTR)M_MERGE_NAMES },
    { NM_SUB, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_SUB, "Merge to Document...", NULL, 0, 0, (APTR)M_MERGE_DOC },
    { NM_SUB, "Merge to Files...", NULL, 0, 0, (APTR)M_MERGE_FILES },
    { NM_ITEM, "User Fields...", NULL, 0, 0, (APTR)M_USER_FIELDS },
    { NM_ITEM, "Update Fields", NULL, 0, 0, (APTR)M_UPDATE_FIELDS },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "GFOpenPrint...", NULL, 0, 0, (APTR)M_OPENPRINT },

    { NM_TITLE, "Help", NULL, 0, 0, NULL },
    { NM_ITEM, "About GFOpenWrite...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* 2.0.5: the pictures are the Chrome icon set's (gfopengadtools docs/style/README.md; Images/Toolbar/), by name;
 * the vector icons are drawn when the set can't be (no strips, four colours, the Classic theme). */
static const ogt_tool tools[] = {
    { C_NEW,   "New",    OGT_ICON_FILE,    0, 0, 0, "new" },
    { C_OPEN,  "Open...",OGT_ICON_OPEN,    0, 0, 0, "open" },
    { C_SAVE,  "Save",   OGT_ICON_DISK,    0, 0, 0, "save" },
    { C_PRINT, "Print...",OGT_ICON_TEST,    1, 0, 0, "print" },
    { C_UNDO,  "Undo",   OGT_ICON_PARENT,  1, 1, 0, "undo" },
    { C_REDO,  "Redo",   OGT_ICON_FORWARD, 0, 1, 0, "redo" },
    { C_IMAGE, "Image...",OGT_ICON_FILE,    1, 0, 0, "picture" },
    { C_TABLE, "Table...",OGT_ICON_FOLDER,  0, 0, 0, "table" },
    { C_PDF,   "PDF...",  OGT_ICON_SENT,    1, 0, 0, "pdf" }
};

typedef struct box { int x, y, w, h; } box;

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *chain;                         /* the toolbar's gadgets */
static struct Gadget *glist2;                        /* 2.0: the GadTools gadgets: the format row and the inspector */
static ogt_pageview *pv;                             /* 2.0: the pages, one under another */
static ogt_list *nav;                                /* 2.0: the navigator: a thumbnail a page */
static ogt_sections isec;                            /* 2.0: the inspector */
static ogt_ruler ruler;                              /* 2.0 */
static ogt_status sbar;                              /* 2.0 */
static struct Gadget *fmt_gad[4];                    /* 2.0: style, font, size, zoom */
static int inspector_live;                           /* 2.0: the inspector's gadgets exist */
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;

#define DOC_FONT_CACHE 24                             /* 2.0.3: room for the sizes a zoom's text is fitted at */
typedef struct doc_font_slot {
    char file[72];
    int pixels;
    struct TextFont *font;
} doc_font_slot;
static doc_font_slot doc_fonts[DOC_FONT_CACHE];
/* The fonts that didn't open, not tried again until the cache is cleared. */
#define DOC_FONT_MISSES 16
static struct { char file[72]; int pixels; } doc_font_misses[DOC_FONT_MISSES];
static int doc_font_miss_next;
static int doc_font_next;                            /* 2.1: the slot a new size replaces when all are used */
static int quit_now;

/* ARexx (amicradle-teams ARexx-RULE.md): while a command runs no requester shows; what it would have said is
 * kept, and a failing command's reason is LASTERROR's answer. */
static int rexx_busy, rexx_noted;
static char rexx_noting[300], rexx_lasterror[300];

/* src into dst (n bytes), cut short to fit (no snprintf: GCC 16's format-truncation at -Werror) */
static void rexx_copy(char *dst, size_t n, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (!n) return;
    if (len >= n) len = n - 1;
    if (len) memcpy(dst, src, len);
    dst[len] = 0;
}

static int rexx_quiet(const char *text)
{
    if (!rexx_busy) return 0;
    if (!rexx_noted) { rexx_copy(rexx_noting, sizeof rexx_noting, text); rexx_noted = 1; }
    return 1;
}

static ogt_theme theme;
static ogt_ctx ctx;
static ogt_toolbar tb;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS;     /* icons only to start (the Team's rule, 10 October 2026) */

static owf_doc *doc;
static ow_editor *editor;
static ow_spell *spell;
static ow_autosave *autosave;
static struct MsgPort *rexx_port;
#define REXX_PORT_NAME "GFOPENWRITE"
static ow_recovery recovery;                         /* the autosave's recovery copy (ow_autosave.h) */
static char current_path[512];
static char current_format[32] = "ODT";
static char status[256] = "Ready.";
static char last_find[160] = "", last_replace[160] = "";
static int zoom = 100;
static int show_nav = 1, show_inspector = 1;
static int page_index;
static int selected_frame = -1;                      /* a floating, non-text frame selected on the page */

static box area, toolbar_box, format_box, navigator_box, canvas_box;
static box ruler_box, page_box, page_view_box, inspector_box, status_box;
#define PAGE_GAP 16
#define PAGE_MARGIN 12

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
/* Where a position is drawn, even off the visible part of the page. */
static int track_on, track_y, track_h;
static ow_position track_pos;
/* 2.0.10: pictures decoded once, at IMAGE_MAX_PIXELS at most (a cover's page-sized JPEG is read from its
 * datatype band by band and averaged down to it), IMAGE_CACHE_BYTES of them at most, the least recently
 * drawn going first; one that couldn't be read is remembered (argb NULL) and not tried again. */
#define IMAGE_CACHE_SLOTS 16
#define IMAGE_MAX_PIXELS (1024UL * 1024UL)           /* 4 MB of ARGB: a whole A4 page at 100% is 794 x 1123 */
#define IMAGE_CACHE_BYTES (8UL * 1024UL * 1024UL)
typedef struct { int index; UBYTE *argb; UWORD w,h; ULONG used; } ow_image_cache;
static ow_image_cache image_cache[IMAGE_CACHE_SLOTS];
static int image_cache_n;
static ULONG image_cache_tick;
static int mouse_selecting;
static int frame_dragging, frame_drag_frame = -1, frame_drag_page, frame_drag_mode;
static int frame_drag_start_x, frame_drag_start_y, frame_drag_x, frame_drag_y;
static ol_rect frame_drag_rect;
static int defer_repaint, repaint_pending;

/* 2.1: the pages are GFOpenLayout's: the body laid out once at full resolution (units, twips x 4) and drawn
 * at the screen's pixels through one scale, so line and page breaks never move with the zoom. Laid out
 * again when the document changes (ow_editor_changes). */
static ow_layout *pages_layout;
static const ow_editor *pages_editor;
static size_t pages_room;                            /* 2.0.13: ow_layout_new_reserved's, 0 before the first */
static unsigned long pages_changes;

static void draw_all(void);
static void redraw_document_area(void);
static void layout(void);
static void pages_changed(void);
static void pages_free(void);
static void fields_update(void);
static int page_hit(int mx, int my, ow_position *out);
static int page_frame_hit(int mx, int my, int *frame_index, ol_rect *rect);
static int frame_drag_begin(int mx, int my);
static void frame_drag_update(int mx, int my);
static void frame_drag_finish(int mx, int my);
static void follow_caret_page(void);
static void set_page_view(int page);
static void scroll_page(int dx, int dy);
static void request_document_redraw(void);
static void draw_status(void);
static void relayout(void);
static void menu_action(ULONG id);
static void doc_font_cache_clear(void);
static void image_cache_clear(void);
static void editor_repaint(const char *message);
static int editor_result(int rc, const char *message);
static void review_author_init(void);
static void sync_track_menu(void);
static char review_author[64];                       /* 2.0.27: the name tracked changes are made under */
static int review_author_set;
static int mm_to_twips(int mm);

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

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
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)title, (UBYTE *)"%s", (UBYTE *)"OK"
    };
    if (rexx_quiet(text)) return;
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




static owf_doc *blank_document(void)
{
    return owf_doc_new();
}


static void update_window_title(void)
{
    static char title[sizeof current_path + 16];   /* "*", the name and " - GFOpenWrite"; static: Intuition keeps the pointer (2.0.1) */
    const char *name = current_path[0] ? leaf(current_path) : "Untitled";
    snprintf(title, sizeof title, "%s%s - GFOpenWrite",
             editor && ow_editor_is_dirty(editor) ? "*" : "", name);
    if (win) SetWindowTitles(win, (STRPTR)title, (STRPTR)-1);
}

static void update_edit_tools(void)
{
    if (!editor) return;
    ogt_toolbar_enable(&tb, C_UNDO, ow_editor_can_undo(editor));
    ogt_toolbar_enable(&tb, C_REDO, ow_editor_can_redo(editor));
}

/* ---- 2.0: the format row's choices ------------------------------------------------------------- */
static const char *const style_labels[] = { "Body text", "Heading 1", "Heading 2", "Heading 3", NULL };
static const char *const font_labels[] = { "CGTriumvirate", "Times New Roman", "Courier New", NULL };
static const owf_font_kind font_kinds[] = { OWF_FONT_SANS, OWF_FONT_SERIF, OWF_FONT_MONO };
/* 2.0.11: 25% and 200% too, the zoom's ends, and the keys step through these (zoom_step), so the format row's
 * Zoom always says what the pages are at; it stayed at 50% while + and - went 10% at a time */
static const char *const zoom_labels[] = { "25%", "50%", "75%", "100%", "125%", "150%", "200%", "Fit page", "Fit width", NULL };
static const int zoom_values[] = { 25, 50, 75, 100, 125, 150, 200, -1, -2 };
#define ZOOM_STEPS 7                                 /* the percentages before Fit page and Fit width */
#define ZOOM_FIT_PAGE 7
#define ZOOM_FIT_WIDTH 8
static const char *const align_labels[] = { "Left", "Centre", "Right", "Justify", NULL };
static const char *const spacing_labels[] = { "Single", "1.5 lines", "Double", NULL };
static const char *const paper_labels[] = { "A4", "Letter", "Legal", "Custom", NULL };
static int zoom_choice = 3;

static int font_choice(const owf_charfmt *cf)
{
    const char *name = NULL;
    int i;
    if (doc && cf->font >= 0 && cf->font < doc->nfonts) name = doc->fonts[cf->font].name;
    else if (doc && doc->base.font >= 0 && doc->base.font < doc->nfonts) name = doc->fonts[doc->base.font].name;
    for (i = 0; i < 3; ++i) if (name && !strcmp(name, font_labels[i])) return i;
    return 0;
}

static int paper_choice(void)
{
    int sw = doc ? (doc->page.width < doc->page.height ? doc->page.width : doc->page.height) : 0;
    int sh = doc ? (doc->page.width > doc->page.height ? doc->page.width : doc->page.height) : 0;
    if (abs(sw - 11906) < 140 && abs(sh - 16838) < 140) return 0;
    if (abs(sw - 12240) < 140 && abs(sh - 15840) < 140) return 1;
    if (abs(sw - 12240) < 140 && abs(sh - 20160) < 140) return 2;
    return 3;
}

/* The format row and the inspector say what the caret's paragraph and run are. */
static void show_formats(void)
{
    owf_charfmt cf;
    owf_parafmt pf;
    struct Gadget *g;
    int twips, pts, lsp;
    if (!win || !editor) return;
    memset(&cf, 0, sizeof cf);
    memset(&pf, 0, sizeof pf);
    ow_editor_current_charfmt(editor, &cf);
    ow_editor_current_parafmt(editor, &pf);
    twips = cf.size ? cf.size : (doc && doc->base.size ? doc->base.size : 240);
    pts = (twips + 10) / 20;
    lsp = pf.line_spacing >= 200 ? 2 : pf.line_spacing >= 150 ? 1 : 0;
    if (fmt_gad[0]) GT_SetGadgetAttrs(fmt_gad[0], win, NULL, GTCY_Active, (ULONG)(pf.heading > 3 ? 3 : pf.heading), TAG_DONE);
    if (fmt_gad[1]) GT_SetGadgetAttrs(fmt_gad[1], win, NULL, GTCY_Active, (ULONG)font_choice(&cf), TAG_DONE);
    if (fmt_gad[2]) GT_SetGadgetAttrs(fmt_gad[2], win, NULL, GTIN_Number, (ULONG)pts, TAG_DONE);
    if (fmt_gad[3]) GT_SetGadgetAttrs(fmt_gad[3], win, NULL, GTCY_Active, (ULONG)zoom_choice, TAG_DONE);
    if (inspector_live) {
        if ((g = ogt_sections_gadget(&isec, GID_I_STYLE))) GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)(pf.heading > 3 ? 3 : pf.heading), TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_ALIGN))) GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)(pf.align <= OWF_ALIGN_JUSTIFY ? pf.align : 0), TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_BEFORE))) GT_SetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)((pf.space_before + 10) / 20), TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_AFTER))) GT_SetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)((pf.space_after + 10) / 20), TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_SPACING))) GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)lsp, TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_FONT))) GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)font_choice(&cf), TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_SIZE))) GT_SetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)pts, TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_BOLD))) GT_SetGadgetAttrs(g, win, NULL, GTCB_Checked, (cf.flags & OWF_BOLD) != 0, TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_ITALIC))) GT_SetGadgetAttrs(g, win, NULL, GTCB_Checked, (cf.flags & OWF_ITALIC) != 0, TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_PAPER))) GT_SetGadgetAttrs(g, win, NULL, GTCY_Active, (ULONG)paper_choice(), TAG_DONE);
        if ((g = ogt_sections_gadget(&isec, GID_I_MARGIN)) && doc) GT_SetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)(doc->page.margin_left * 254 / 14400), TAG_DONE);
    }
    ogt_ruler_set_indents(&ruler, pf.indent_left, pf.indent_first, pf.indent_right);
}

static void image_cache_clear(void)
{
    int i;for(i=0;i<image_cache_n;++i)free(image_cache[i].argb);memset(image_cache,0,sizeof image_cache);image_cache_n=0;
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

/* The cached picture, or NULL when it isn't decoded (thumbnails don't decode). */
static ow_image_cache *image_cache_peek(int index)
{
    int i;
    for (i = 0; i < image_cache_n; ++i)
        if (image_cache[i].index == index) return image_cache[i].argb ? &image_cache[i] : NULL;
    return NULL;
}

/* Room for need more bytes: the least recently drawn pictures go. */
static void image_cache_room(ULONG need)
{
    for (;;) {
        ULONG total = 0;
        int i, old = -1;
        for (i = 0; i < image_cache_n; ++i) {
            total += (ULONG)image_cache[i].w * image_cache[i].h * 4;
            if (old < 0 || image_cache[i].used < image_cache[old].used) old = i;
        }
        if (old < 0 || (image_cache_n < IMAGE_CACHE_SLOTS && total + need <= IMAGE_CACHE_BYTES)) return;
        free(image_cache[old].argb);
        image_cache[old] = image_cache[--image_cache_n];
        memset(&image_cache[image_cache_n], 0, sizeof image_cache[image_cache_n]);
    }
}

/* A target row of the averages: each the sum of its source pixels over how many there were. */
static void image_row_out(UBYTE *dst, int tw, ULONG *acc, const UWORD *cols, int rc)
{
    int k, b;
    for (k = 0; k < tw; ++k) {
        ULONG cnt = (ULONG)cols[k] * rc;
        for (b = 0; b < 4; ++b) dst[k * 4 + b] = (UBYTE)(cnt ? acc[k * 4 + b] / cnt : 255);
    }
    memset(acc, 0, (size_t)tw * 4 * sizeof(ULONG));
}

/* The picture's pixels at tw x th (no more than sw x sh), from the datatype's sw x sh: each the average of
 * the source pixels that fall in it, the source read 64 KB of rows at a time. */
static int image_read_scaled(Object *o, UBYTE *out, int sw, int sh, int tw, int th)
{
    ULONG *acc = (ULONG *)calloc((size_t)tw * 4, sizeof(ULONG));
    UWORD *xmap = (UWORD *)malloc((size_t)sw * sizeof(UWORD)), *cols = (UWORD *)calloc((size_t)tw, sizeof(UWORD));
    int rows = 65536 / (sw * 4) > 0 ? 65536 / (sw * 4) : 1, y0, r, sx, ty = 0, rc = 0, ok = 1;
    UBYTE *band;
    if (rows > sh) rows = sh;
    band = (UBYTE *)malloc((size_t)sw * 4 * rows);
    if (!acc || !xmap || !cols || !band) ok = 0;
    for (sx = 0; ok && sx < sw; ++sx) {
        xmap[sx] = (UWORD)((ULONG)sx * tw / sw);
        ++cols[xmap[sx]];
    }
    for (y0 = 0; ok && y0 < sh; y0 += rows) {
        int n = sh - y0 < rows ? sh - y0 : rows;
        if (!DoMethod(o, PDTM_READPIXELARRAY, (ULONG)band, PBPAFMT_ARGB, (ULONG)sw * 4, 0, (ULONG)y0, (ULONG)sw,
                      (ULONG)n)) {
            ok = 0;
            break;
        }
        for (r = 0; r < n; ++r) {
            int row_ty = (int)((ULONG)(y0 + r) * th / sh);
            const UBYTE *src = band + (size_t)r * sw * 4;
            if (row_ty != ty && rc) {
                image_row_out(out + (size_t)ty * tw * 4, tw, acc, cols, rc);
                rc = 0;
            }
            ty = row_ty;
            for (sx = 0; sx < sw; ++sx, src += 4) {
                ULONG *a = acc + xmap[sx] * 4;
                a[0] += src[0]; a[1] += src[1]; a[2] += src[2]; a[3] += src[3];
            }
            ++rc;
        }
    }
    if (ok && rc) image_row_out(out + (size_t)ty * tw * 4, tw, acc, cols, rc);
    free(band);
    free(cols);
    free(xmap);
    free(acc);
    return ok;
}

/* A slot for a picture, a slot made free first; argb NULL until it is read. */
static ow_image_cache *image_cache_add(int index)
{
    ow_image_cache *c;
    image_cache_room(0);
    c = &image_cache[image_cache_n++];
    memset(c, 0, sizeof(*c));
    c->index = index;
    c->used = ++image_cache_tick;
    return c;
}

static ow_image_cache *image_cache_get(int index)
{
    int i, sw, sh, tw, th, ok;
    char tmp[96];
    BPTR f;
    Object *o = NULL;
    struct BitMapHeader *bmh = NULL;
    ow_image_cache *c;
    const owf_image *im;
    if (!doc || index < 0 || index >= doc->nimages || !DataTypesBase) return NULL;
    for (i = 0; i < image_cache_n; ++i)
        if (image_cache[i].index == index) {
            image_cache[i].used = ++image_cache_tick;
            return image_cache[i].argb ? &image_cache[i] : NULL;
        }
    im = &doc->images[index];
    snprintf(tmp, sizeof tmp, "T:GFOpenWrite-Image-%ld-%d.tmp", (long)FindTask(NULL), index);
    f = im->data && im->length ? Open((STRPTR)tmp, MODE_NEWFILE) : 0;
    if (!f) {
        image_cache_add(index);                      /* not tried again */
        return NULL;
    }
    ok = Write(f, (APTR)im->data, (LONG)im->length) == (LONG)im->length;
    Close(f);
    if (ok) o = NewDTObject((APTR)tmp, DTA_GroupID, GID_PICTURE, PDTA_DestMode, PMODE_V43, PDTA_Remap, FALSE, TAG_DONE);
    if (!o) {
        DeleteFile((STRPTR)tmp);
        image_cache_add(index);
        return NULL;
    }
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
        if (tw > sw) tw = sw;
        if (th > sh) th = sh;
    }
    if (sw > 0 && sh > 0) image_cache_room((ULONG)tw * th * 4);
    c = image_cache_add(index);
    if (sw > 0 && sh > 0 && (c->argb = (UBYTE *)malloc((ULONG)tw * th * 4)) != NULL) {
        if (tw == sw && th == sh)
            ok = DoMethod(o, PDTM_READPIXELARRAY, (ULONG)c->argb, PBPAFMT_ARGB, (ULONG)sw * 4, 0, 0, (ULONG)sw,
                          (ULONG)sh) != 0;
        else
            ok = image_read_scaled(o, c->argb, sw, sh, tw, th);
        if (ok) {
            c->w = (UWORD)tw;
            c->h = (UWORD)th;
        } else {
            free(c->argb);
            c->argb = NULL;
        }
    }
    DisposeDTObject(o);
    DeleteFile((STRPTR)tmp);
    if (!c->argb) return NULL;
    /* ScalePixelArray and WritePixelArray draw ARGB without blending: a picture with an alpha channel (a PNG
     * logo) showed black where it is see-through. Blend it onto the paper (white) once here. A picture whose
     * alpha bytes are all zero has no alpha channel (some datatypes leave them so): kept as it is. */
    {
        ULONG k, n = (ULONG)c->w * c->h;
        int any = 0, partial = 0;
        for (k = 0; k < n; ++k) { UBYTE a = c->argb[k * 4]; if (a) any = 1; if (a != 255) partial = 1; }
        if (any && partial)
            for (k = 0; k < n; ++k) {
                UBYTE *px = c->argb + k * 4;
                unsigned a = px[0];
                if (a != 255) {
                    px[1] = (UBYTE)((px[1] * a + 255 * (255 - a)) / 255);
                    px[2] = (UBYTE)((px[2] * a + 255 * (255 - a)) / 255);
                    px[3] = (UBYTE)((px[3] * a + 255 * (255 - a)) / 255);
                    px[0] = 255;
                }
            }
    }
    return c;
}

static void set_document(owf_doc *newdoc, const char *path, const char *fmt)
{
    pages_free();                                    /* 2.1: the pages point into the document */
    doc_font_cache_clear();
    image_cache_clear();
    if (editor) ow_editor_free(editor);
    if (doc) owf_doc_free(doc);
    doc = newdoc;
    editor = doc ? ow_editor_new(doc) : NULL;
    if (editor) {
        review_author_init();
        ow_editor_set_review_author(editor, review_author);
        ow_editor_set_zoom(editor, zoom);
        ow_editor_layout(editor);
        ow_editor_mark_saved(editor);
    }
    page_index = 0;
    selected_frame = -1;
    frame_dragging = 0;
    frame_drag_frame = -1;
    pages_changed();
    if (pv) ogt_pageview_scroll_to(pv, 0);
    snprintf(current_path, sizeof current_path, "%s", path ? path : "");
    snprintf(current_format, sizeof current_format, "%s", fmt && *fmt ? fmt : "ODT");
    recovery.made = 0;                               /* a new editor counts its changes from 0 */
    if (editor) {                                    /* 2.0.29: the fields as this file and its pages make them */
        ow_editor_set_file_name(editor, current_path[0] ? current_path : NULL);
        fields_update();
    }
    sync_track_menu();
    update_window_title();
}

static void recovery_clear(void)
{
    ow_recovery_clear(&recovery);
}

/* Every minute: the recovery copy, when the document has changed since the last one (ow_autosave.h). */
static void recovery_save(void)
{
    owf_report *report;
    int rc;
    if (!doc || !editor || !ow_recovery_due(&recovery, ow_editor_is_dirty(editor), ow_editor_changes(editor))) return;
    if (!ow_recovery_begin(&recovery)) return;
    report = owf_report_new();
    rc = owf_export_file(doc, recovery.name, "odt", report);
    owf_report_free(report);
    if (rc != OWF_OK) return;
    ow_recovery_done(&recovery, current_path[0] ? current_path : "Untitled", ow_editor_changes(editor));
    set_status("Autosave recovery copy updated.");
    draw_status();
}

/* A recovery copy an interrupted session left (not another running GFOpenWrite's): offered, one after another. */
static void recovery_offer(void)
{
    struct EasyStruct es = {
        sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenWrite Recovery",
        (UBYTE *)"GFOpenWrite found a recovery document from an interrupted session.\n\nRecover it?",
        (UBYTE *)"Recover|Discard"
    };
    char found[80];
    int asked;
    for (asked = 0; asked < 8 && ow_recovery_find(&recovery, found, sizeof found); asked++) {
        owf_doc *recovered = NULL;
        owf_report *report;
        const owf_format *used = NULL;
        int rc;
        if (!EasyRequest(win, &es, NULL)) {
            if (!ow_recovery_discard(found)) return;    /* it can't go: not asked about again */
            continue;
        }
        report = owf_report_new();
        rc = owf_import_file(found, "odt", &recovered, report, &used);
        owf_report_free(report);
        if (rc != OWF_OK || !recovered) {
            tell("GFOpenWrite Recovery", "The recovery copy could not be opened.");
            ow_recovery_discard(found);
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
        recovery_save();                             /* this run's own copy of it, so the interrupted one's can go */
        if (recovery.made) ow_recovery_discard(found);
        set_status("Recovered document. Save it to keep the recovered work.");
        relayout();
        return;
    }
}

/* 2.0.31: a file, with a requester titled for what it is for (title NULL: a document's) */
static int ask_file_for(int save, const char *title, char *out, size_t out_size)
{
    struct FileRequester *fr;
    char last[512];
    int ok = 0;
    if (rexx_quiet("A file is needed")) return 0;
    lastdir_get("GFOpenWrite", last, sizeof last);         /* 1.0.4: where the last document was */
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(title ? title : save ? "Save GFOpenWrite document" : "Open document"),
        ASLFR_InitialDrawer, (ULONG)last,
        ASLFR_Window, (ULONG)win,
        ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, save ? TRUE : FALSE,
        ASLFR_InitialFile, (ULONG)(save ? (current_path[0] ? leaf(current_path) : "Untitled.odt") : ""),
        ASLFR_PositiveText, (ULONG)(save ? "Save" : "Open"),
        TAG_DONE);
    if (!fr) return 0;
    if (AslRequestTags(fr, TAG_DONE) && fr->fr_File[0]) {
        snprintf(out, out_size, "%s", fr->fr_Drawer);
        lastdir_put("GFOpenWrite", (const char *)fr->fr_Drawer);
        AddPart((STRPTR)out, fr->fr_File, out_size);
        ok = 1;
    }
    FreeAslRequest(fr);
    return ok;
}

static int ask_file(int save, char *out, size_t out_size)
{
    return ask_file_for(save, NULL, out, out_size);
}

static void open_document(const char *path)
{
    owf_doc *newdoc = NULL;
    owf_report *report = owf_report_new();
    const owf_format *used = NULL;
    char msg[256];
    int rc = owf_import_file(path, NULL, &newdoc, report, &used);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "GFOpenWrite could not open the document.\n\n%s", owf_error_text(rc));
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
    if (editor) {                                    /* 2.0.29: the fields as they will be in the file */
        ow_editor_set_file_name(editor, path);
        fields_update();
    }
    rc = owf_export_file(doc, path, NULL, report);
    if (rc != OWF_OK) {
        snprintf(msg, sizeof msg, "GFOpenWrite could not save this document.\n\n%s", owf_error_text(rc));
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
    doc_font_next = 0;
    memset(doc_font_misses, 0, sizeof doc_font_misses);   /* 2.0.3 */
    doc_font_miss_next = 0;
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
    /* 2.0.5: more of today's families by their names (Roboto Slab, Cambria, Consolas...) */
    static const char *const serifs[] = { "times", "serif", "slab", "cambria", "georgia", "garamond", "palatino",
                                          "baskerville", "bookman", "book antiqua", "century", "caslon", "minion",
                                          "constantia", "rockwell", "caladea", NULL };
    static const char *const monos[] = { "courier", "mono", "lettergothic", "consolas", "code", "menlo", "monaco",
                                         "fixed", NULL };
    int i, serif = kind == OWF_FONT_SERIF, mono = kind == OWF_FONT_MONO;
    for (i = 0; !mono && monos[i]; ++i) mono = name_has(name, monos[i]);
    for (i = 0; !serif && serifs[i]; ++i) serif = name_has(name, serifs[i]);
    if (mono)
        snprintf(out, out_size, "%s", "courier.font");
    else if (serif)
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

/* 2.0.3: a font through one route: gfopenfont.library (route 0), diskfont.library (1, opened when openfont
 * can't give the font: diskfont scales bitmap families and draws outlines through bullet.library, OS 3.2's
 * Intellifont included) or graphics' GFOpenFont (2: the fonts in memory, the ROM's and the screen's). */
struct Library *DiskfontBase = NULL;
static int diskfont_tried;

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

/* A document font at a pixel size, kept open for the next drawing (DOC_FONT_CACHE sizes; the oldest makes
 * way). NULL when no route has such a font.
 *
 * 2.0.3: at that very size whenever a route can give it: what a word is laid out with must be what it is
 * drawn with (ow_fontwidths.h). A bitmap family's designed size when it has that one (FPF_DESIGNED), else
 * the family scaled to the size (an outline is drawn at it); openfont first, then diskfont. Only when none
 * can: the nearest size there is, whose widths ow_fontwidths scales. In the lab (25 October 2026)
 * openfont opened nothing of OS 3.2's Intellifont CGTriumvirate, which has no bitmap sizes, and the old
 * FPF_DESIGNED request could come back at another size than asked. */
static struct TextFont *document_font_px(const char *file, int pixels)
{
    int i, slot = -1, route, outline = 0;
    struct TextAttr ta;
    struct TextFont *f = NULL;
    if (!file || !file[0]) return NULL;
    if (pixels < 4) pixels = 4;
    if (pixels > 128) pixels = 128;                  /* an outline font's glyphs at 128 pixels: about 450 KB */
    for (i = 0; i < DOC_FONT_CACHE; ++i) {
        if (doc_fonts[i].font && doc_fonts[i].pixels == pixels && !strcmp(doc_fonts[i].file, file))
            return doc_fonts[i].font;
        if (!doc_fonts[i].font && slot < 0) slot = i;
    }
    for (i = 0; i < DOC_FONT_MISSES; ++i)
        if (doc_font_misses[i].pixels == pixels && !strcmp(doc_font_misses[i].file, file)) return NULL;
    memset(&ta, 0, sizeof ta);
    ta.ta_Name = (STRPTR)file;
    ta.ta_YSize = (UWORD)pixels;
    ta.ta_Style = FS_NORMAL;
    {
        char otag[96];
        BPTR lk;
        snprintf(otag, sizeof otag, "FONTS:%s", file);
        if (strlen(otag) > 5 && !strcmp(otag + strlen(otag) - 5, ".font")) {
            strcpy(otag + strlen(otag) - 5, ".otag");
            if ((lk = Lock((STRPTR)otag, ACCESS_READ))) { outline = 1; UnLock(lk); }
        }
    }
    /* the size itself: a designed bitmap, else scaled or drawn at it */
    for (route = 0; !f && route < 2; ++route) {
        if (!outline && (f = open_route(route, &ta, FPF_DISKFONT | FPF_DESIGNED)) && f->tf_YSize != pixels) {
            CloseFont(f);
            f = NULL;
        }
        if (!f && (f = open_route(route, &ta, FPF_DISKFONT)) && f->tf_YSize != pixels) {
            CloseFont(f);
            f = NULL;
        }
    }
    /* else the nearest there is */
    for (route = 0; !f && route < 3; ++route)
        f = open_route(route, &ta, route == 2 ? 0 : FPF_DISKFONT | FPF_DESIGNED);
    if (f && route == 3 && strcmp((const char *)f->tf_Message.mn_Node.ln_Name, file)) {
        CloseFont(f);                                /* GFOpenFont's another family: not this one */
        f = NULL;
    }
    if (!f) {
        snprintf(doc_font_misses[doc_font_miss_next].file, sizeof doc_font_misses[0].file, "%s", file);
        doc_font_misses[doc_font_miss_next].pixels = pixels;
        doc_font_miss_next = (doc_font_miss_next + 1) % DOC_FONT_MISSES;
        return NULL;
    }
    if (slot < 0) {                                  /* every slot used: the oldest goes (the caller sets the new one) */
        slot = doc_font_next;
        doc_font_next = (doc_font_next + 1) % DOC_FONT_CACHE;
        CloseFont(doc_fonts[slot].font);
    }
    snprintf(doc_fonts[slot].file, sizeof doc_fonts[slot].file, "%s", file);
    doc_fonts[slot].pixels = pixels;
    doc_fonts[slot].font = f;
    return f;
}

static struct TextFont *document_font(int paragraph, const owf_charfmt *fmt)
{
    char file[72];
    int pixels;
    struct TextFont *f;
    int font_index = fmt ? fmt->font : -1;
    if (font_index < 0 && doc) font_index = doc->base.font;
    document_font_file(font_index, file, sizeof file);
    pixels = run_twips(paragraph, fmt) * render_num / render_den;
    if (pixels < 8) pixels = 8;
    if (pixels > 72) pixels = 72;
    f = document_font_px(file, pixels);
    return f ? f : font;
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

/* TextLength with the bold smear counted. Text() in a font made bold by the system moves each letter on by
 * the font's bold smear, which TextLength can leave out; so a bold word is measured plain, plus the smear a
 * letter, or as TextLength has it bold, whichever is wider. */
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

static int text_width_n(struct RastPort *rp, const char *s, size_t n)
{
    char buf[512];
    size_t m;
    if (!n) return 0;
    m = to_latin1(s, n, buf, sizeof buf);
    return m ? text_length(rp, buf, m) : 0;
}

static int selection_for_run(int paragraph, int run, size_t len,
                             size_t *from, size_t *to)
{
    ow_selection sel;
    ow_position a, b, rs, re;
    if (paragraph < 0 || !editor || ow_editor_selection_empty(editor)) return 0;
    sel = ow_editor_selection(editor);
    if (sel.focus.box) return 0;                     /* 2.0.15: in a text box, not the body's */
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
    if (track_on && track_y < 0 && paragraph == track_pos.paragraph && run == track_pos.run &&
        track_pos.byte_offset >= start && track_pos.byte_offset <= end) { track_y = y; track_h = h; }
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
    if (c.box || c.paragraph != paragraph || c.run != run) return;
    if (c.byte_offset < segment_start || c.byte_offset > segment_end) return;
    cx = x + text_width_n(rp, text + segment_start,
                          c.byte_offset - segment_start);
    ogt_vline(rp, ogt_pen(&ctx, "accent"), cx, y, render_line_h > 1 ? render_line_h - 1 : 1);
}

static void render_begin(void *ud, const ow_page_info *p)
{
    (void)ud; (void)p;
    render_para = -1;                                /* 2.0: hit_count is cleared once a drawing, in draw_pages */
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
    memset(out, 0, sizeof(*out));                    /* the body's (box 0) */
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
    {   /* 2.0: on any page that shows in the view */
        int cx, cy;
        if (!pv || !ogt_pageview_to_content(pv, mx, my, &cx, &cy) || ogt_pageview_page_at(pv, cy) < 0) return;
    }
    if (!extend) {
        int frame_index;
        if (page_frame_hit(mx, my, &frame_index, NULL) && frame_index >= 0 &&
            frame_index < doc->nframes && doc->frames[frame_index].story.nparas == 0) {
            selected_frame = frame_index;
            set_status("Floating frame selected; use Layout / Frame to move or resize it.");
            request_document_redraw();
            return;
        }
        selected_frame = -1;
    }
    if (!page_hit(mx, my, &p) && !hit_position(mx, my, &p)) return;
    sel = ow_editor_selection(editor);
    if (!extend) sel.anchor = p;
    sel.focus = p;
    ow_editor_set_selection(editor, &sel);
    request_document_redraw();
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
    /* the pen may be a direct colour (true-colour screens): SetAPen alone took
     * its low bits as a palette pen, and table lines came out green */
    if(py1==py2)ogt_hline(rp,pen,px1<px2?px1:px2,py1,(px1<px2?px2-px1:px1-px2)+1);
    else if(px1==px2)ogt_vline(rp,pen,px1,py1<py2?py1:py2,(py1<py2?py2-py1:py1-py2)+1);
    else{ogt_set_apen(rp,pen);Move(rp,px1,py1);Draw(rp,px2,py2);}
    if(py1>render_rule_bottom)render_rule_bottom=py1;
    if(py2>render_rule_bottom)render_rule_bottom=py2;
}

/* ---- 2.0: the shell on GFOpenGadTools' kinds --------------------------------------------------------
 *
 * The page view holds every page one under another (PAGE_MARGIN round, PAGE_GAP between); the view's draw
 * callback renders each page that shows, through the same renderer as before, so the hit spans, the caret
 * and the selection work as they did. page_index is the page the caret is on. */

static int page_scale_num(void) { return zoom; }
static int page_scale_den(void) { return 1500; }   /* 100%: 15 twips a pixel (96 dpi) */

static void page_size(int *pw, int *ph)
{
    *pw = doc ? doc->page.width * page_scale_num() / page_scale_den() : 40;
    *ph = doc ? doc->page.height * page_scale_num() / page_scale_den() : 40;
    if (*pw < 40) *pw = 40;
    if (*ph < 40) *ph = 40;
}

/* ---- 2.1: the pages drawn by GFOpenLayout --------------------------------------------------------------
 *
 * 2.0.3: text is measured for the layout from each font's advance widths at the size it is drawn at 100%
 * (WIDTH_UNITS units a pixel: 96 dpi), from the very font that draws it there, so at 100% each word is as
 * wide as the layout made it. The layout is still the same at every zoom. Before, the widths were taken at
 * 64 pixels and scaled to the text's size; the screen's fonts are not that proportional (a bitmap font's
 * small sizes run wide, CGTriumvirate's sizes come from its bitmaps since openfont can't read its
 * Intellifont outline, each advance and the size itself are rounded to pixels, the bold smear is a pixel at
 * any size), so words drew wider than their place and, glyph by glyph where the layout put them, ran into
 * each other. At other zooms a style whose font draws wider than the layout is drawn a size or two smaller
 * (ow_fw_fit). A page is drawn straight at the screen's pixels; nothing is drawn big and shrunk. */

#define WIDTH_UNITS (OL_UNITS_PER_TWIP * 15)        /* units a pixel at 100% (page_scale_den: 15 twips) */
#define TAB_UNITS (OL_UNITS_PER_INCH / 2)

typedef ow_fw_table width_table;
static ow_fontwidths fontw;                          /* the faces and widths (app/ow_fontwidths.c) */
static int fontw_ready;
static const width_table *width_memo;               /* the last style measured: most words share it */
static uint32_t width_memo_key;
static ol_unit width_memo_size;

/* The font file for a GFOpenLayout font number: the document's font index plus one, 0 its default. */
static void layout_font_file(uint32_t flags, char *out, size_t size)
{
    unsigned number = OL_TEXT_FONT_OF(flags);
    if (!number && (flags & OL_TEXT_MONO)) {
        snprintf(out, size, "%s", "courier.font");
        return;
    }
    document_font_file(number ? OW_LAYOUT_FONT_INDEX(number) : (doc ? doc->base.font : -1), out, size);
}

/* ow_fontwidths' fonts are document_font_px's (kept open in its cache); advances through a RastPort of
 * its own. */
static void *fw_open(void *user, const char *file, int pixels, int *ysize, int *ascent)
{
    struct TextFont *f = document_font_px(file, pixels);
    (void)user;
    if (!f) return NULL;
    *ysize = f->tf_YSize;
    *ascent = f->tf_Baseline + 1;
    return f;
}

/* 2.0.5: bold is drawn by GFOpenWrite itself, each glyph plain and again the font's bold smear further on,
 * its advance that much wider (smear_of: 0 for a font made bold, which has its own widths). What the layout
 * measures for bold is then just what is drawn, whatever graphics' own bold would advance: in the lab (25
 * October 2026) bold words drew narrower than they were laid out, and the gaps between them grew (14 point
 * bold paragraphs, bold contents entries). */
static int smear_of(const struct TextFont *f, int bold)
{
    if (!bold || !f || (f->tf_Style & FSF_BOLD)) return 0;
    return f->tf_BoldSmear > 0 ? f->tf_BoldSmear : 1;
}

static int fw_advance(void *user, void *f, unsigned char c, int bold)
{
    static struct RastPort trp;
    static struct TextFont *set;
    static int ready;
    char ch = (char)c;
    (void)user;
    if (!ready) { InitRastPort(&trp); ready = 1; }
    if (set != (struct TextFont *)f) {
        SetFont(&trp, (struct TextFont *)f);
        SetSoftStyle(&trp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        set = (struct TextFont *)f;
    }
    return (int)TextLength(&trp, (STRPTR)&ch, 1) + smear_of((struct TextFont *)f, bold);
}

/* The theme's font as a file name, as GFOpenGadTools opens it: "DejaVu Sans" is DejaVuSans.font. */
static const char *theme_face_file(void)
{
    static char file[72];
    const char *s;
    char *d = file;
    for (s = theme.font; *s && d < file + sizeof file - 6; s++)
        if (*s != ' ') *d++ = *s;
    strcpy(d, ".font");
    return file[0] != '.' ? file : NULL;
}

/* The faces and widths, ready for the window's font (again after a theme change: font_widths_reset). The
 * faces a document font falls back to end with the theme's and then the window's own (on an Amiga with only
 * bitmap fonts, the screen's, which diskfont scales). */
static ow_fontwidths *fonts_w(void)
{
    if (!fontw_ready || fontw.be.fallback != (void *)font) {   /* first, or a new window font */
        ow_fw_backend be;
        memset(&be, 0, sizeof be);
        be.open = fw_open;
        be.advance = fw_advance;
        be.fallback = font;
        be.fallback_ysize = font ? font->tf_YSize : 8;
        be.fallback_ascent = font ? font->tf_Baseline + 1 : 7;
        ow_fw_init(&fontw, &be, theme_face_file(), font_attr.ta_Name ? (const char *)font_attr.ta_Name : NULL);
        fontw_ready = 1;
    }
    return &fontw;
}

static void font_widths_reset(void)
{
    fontw_ready = 0;
    width_memo = NULL;
}

/* A style's advance widths, at the size it is drawn at at 100%, from the face that draws it. */
static const width_table *font_widths(uint32_t flags, ol_unit size)
{
    char file[72];
    uint32_t key = flags & (0xffff0000u | OL_TEXT_MONO | OL_TEXT_BOLD);
    if (width_memo && key == width_memo_key && size == width_memo_size) return width_memo;
    layout_font_file(flags, file, sizeof file);
    width_memo = ow_fw_widths(fonts_w(), file, (flags & OL_TEXT_BOLD) != 0, ow_fw_pixels(size));
    width_memo_key = key;
    width_memo_size = size;
    return width_memo;
}

/* A sum of a table's widths, in units. */
static ol_unit table_units(ULONG sum)
{
    return (ol_unit)sum * WIDTH_UNITS;
}

/* GFOpenLayout's measure callback: text in units, from the width tables. */
static int measure_units(void *userdata, const char *utf8, size_t length, const ol_style *style,
                         ol_text_metrics *m)
{
    ol_unit size = style->font_size > 0 ? style->font_size : OL_POINT(12), tabs = 0;
    const width_table *t = font_widths(style->text_flags, size);
    ULONG sum = 0;
    size_t at = 0;
    char buf[256];
    (void)userdata;
    while (at < length) {
        size_t n = length - at, k, o;
        if (n > 200) {                               /* a long run in pieces, cut between characters */
            n = 200;
            while (n > 1 && ((unsigned char)utf8[at + n] & 0xc0u) == 0x80u) --n;
        }
        o = to_latin1(utf8 + at, n, buf, sizeof buf);
        for (k = 0; k < o; ++k) {
            unsigned char c = (unsigned char)buf[k];
            if (c == '\t') tabs += TAB_UNITS;
            else sum += t->w[c];
        }
        at += n;
    }
    m->width = table_units(sum) + tabs;
    m->ascent = (ol_unit)t->ascent * WIDTH_UNITS;
    m->descent = (ol_unit)t->descent * WIDTH_UNITS;
    return 1;
}

/* The editor owns TOC semantics and undo; this adapter asks GFOpenLayout for
 * the real physical page of each heading after the entries have been inserted. */
static int resolve_toc_pages(void *userdata, const owf_doc *document,
                             const int *paragraphs, int count, int *page_numbers)
{
    ow_layout *layout;
    int i, ok = 1;
    (void)userdata;
    layout = ow_layout_new(document, measure_units, NULL);
    if (!layout) return 0;
    for (i = 0; i < count; ++i) {
        ol_rect box;
        if (!ow_layout_paragraph_box(layout, paragraphs[i], &page_numbers[i], &box)) {
            ok = 0;
            break;
        }
    }
    ow_layout_free(layout);
    return ok;
}

static void pages_free(void)
{
    if (pages_layout) ow_layout_free(pages_layout);
    pages_layout = NULL;
    pages_editor = NULL;
    width_memo = NULL;                               /* font numbers are the document's */
}

/* The pages for the document as it is now; NULL when memory ran out (the 2.0 renderer draws then). */
static ow_layout *pages(void)
{
    if (!editor) return NULL;
    if (pages_layout && pages_editor == editor && pages_changes == ow_editor_changes(editor))
        return pages_layout;
    {
        /* 2.0.13: the display list's room, the same each time while the document's list fits it (a quarter
         * more than it needs, to the next 1024), taken before anything else as the last layout is freed: it
         * then gets back the very block the last one had. Without it each edit left about 3 MB of libnix's
         * pools behind (MEMCHECK) on a 20-page document. */
        size_t had = pages_layout ? ol_display_count(ow_layout_body(pages_layout)) : 0;
        if (had && (had > pages_room || had < pages_room / 4)) pages_room = (had + had / 4 + 1023) / 1024 * 1024;
        /* 2.0.22: a first layout's room from a guess at its ops (ow_layout_estimate_ops, a little more than it
         * makes), not the last document's or none: its list is made once, not doubled from 64, and the next
         * layouts keep that room while the list fits it */
        if (!had) pages_room = (ow_layout_estimate_ops(ow_editor_document(editor)) + 1023) / 1024 * 1024;
    }
    pages_free();
    pages_layout = ow_layout_new_reserved(ow_editor_document(editor), measure_units, NULL, pages_room);
    pages_editor = editor;
    pages_changes = ow_editor_changes(editor);
    return pages_layout;
}

static int page_total(void)
{
    ow_layout *l = pages();
    int n = l ? ow_layout_page_count(l) : editor ? ow_editor_page_count(editor) : 1;
    return n < 1 ? 1 : n;
}

/* The page the caret is on. */
static int caret_page(void)
{
    ow_layout *l = pages();
    ol_rect box;
    int page;
    if (l && ow_layout_caret(l, ow_editor_selection(editor).focus, &page, &box)) return page;
    return editor ? ow_editor_current_page(editor) : 0;
}

/* A page's scale (zoom 100: 15 twips, 60 units, a pixel, as the ruler), its top left corner at ox, oy. */
static void page_scale(ol_scale *s, int ox, int oy)
{
    ol_scale_set(s, page_scale_num(), page_scale_den() * OL_UNITS_PER_TWIP);
    s->origin_x = ox;
    s->origin_y = oy;
}

typedef struct {
    struct RastPort *rp;
    ow_layout *layout;
    const ol_scale *scale;
    const ol_document *od;      /* the body, or a header or footer */
    int body;                   /* od is the body: its text is the document's runs */
    int thumb;                  /* a navigator thumbnail: text as grey lines */
    int page;
    int has_sel;
    ow_position sel_a, sel_b;   /* the selection, in order */
    int caret_on, caret_page, caret_done;
    ow_position caret;
    ol_rect caret_box;          /* units, on caret_page */
    int caret_x, caret_y, caret_h;
    ol_pixel_rect lim;          /* 2.0.10: the page, and of it what is being drawn: pictures stay inside */
} page_paint;

/* 2.0.10: a picture into the box bx, by, bw, bh, only what of it is inside lim: each row of pixels picked from
 * the cached picture and written a band at a time, so a page-sized picture scrolled half out of the window
 * costs what shows, and nothing is drawn at a negative place. */
static void draw_picture(struct RastPort *rp, const ow_image_cache *c, int bx, int by, int bw, int bh,
                         const ol_pixel_rect *lim)
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

static LONG argb_pen(uint32_t argb)
{
    return ogt_pen_rgb(&ctx, (ogt_rgb){ (UBYTE)(argb >> 16), (UBYTE)(argb >> 8), (UBYTE)argb });
}

/* Where the first n bytes of a word end, in pixels from its left: in the screen font, or, for a word whose
 * glyphs are placed one by one, the layout's. */
static int word_x(const page_paint *pp, const ol_device_op *d, const char *s, size_t n, int placed)
{
    ol_text_metrics m;
    if (!n) return 0;
    if (!placed) return text_width_n(pp->rp, s, n);
    measure_units(NULL, s, n, &d->op->u.text.style, &m);
    return ol_scale_length(pp->scale, m.width);
}

/* 2.0.4: a tab's leader (GFOpenLayout 0.3) across its space: dots, hyphens, underscores or middle dots in the
 * word's font, each where a run of them from the page's left edge would put it, so the leaders of a contents
 * page line up from line to line; a heavy one is a rule. A little room is left at each end. */
static void paint_leader(page_paint *pp, const ol_device_op *d, unsigned leader, LONG pen, int baseline)
{
    struct RastPort *rp = pp->rp;
    char row[96];
    int cw, from, to, x, k;
    char c = leader == OL_LEADER_HYPHEN ? '-' : leader == OL_LEADER_UNDERSCORE ? '_'
           : leader == OL_LEADER_MIDDLE_DOT ? (char)0xB7 : '.';
    if (leader == OL_LEADER_HEAVY) {
        int h = d->font_pixels / 8 > 1 ? d->font_pixels / 8 : 1, gap = d->font_pixels / 4;
        if (d->box.width > 2 * gap) ogt_box(rp, pen, d->box.x + gap, baseline - h + 1, d->box.width - 2 * gap, h);
        return;
    }
    cw = text_length(rp, &c, 1);
    if (cw < 1) return;
    from = d->box.x + cw / 2;
    to = d->box.x + d->box.width - cw / 2;
    x = pp->scale->origin_x + (from - pp->scale->origin_x + cw - 1) / cw * cw;   /* on the page's grid */
    if (x + cw > to) return;
    memset(row, c, sizeof row);
    SetDrMd(rp, JAM1);
    while (x + cw <= to) {
        k = (to - x) / cw;
        if (k > (int)sizeof row) k = (int)sizeof row;
        Move(rp, x, baseline);
        Text(rp, (STRPTR)row, (ULONG)k);
        x += k * cw;
    }
}

static void paint_text(page_paint *pp, const ol_device_op *d)
{
    struct RastPort *rp = pp->rp;
    const ol_text_op *t = &d->op->u.text;
    const char *s = ol_node_text(ol_node_by_id(pp->od, t->node_id));
    const width_table *wt;
    ol_unit size = t->style.font_size > 0 ? t->style.font_size : OL_POINT(12), tabs;
    char buf[256], file[72];
    size_t n, k, drawn;
    int baseline = d->baseline, style = FS_NORMAL, para = -1, run = -1, nbox = 0, target, placed, smear = 0;
    ULONG sum;
    struct TextFont *f;
    LONG pen;
    if (!s || !t->text_length) return;
    s += t->text_offset;
    if (pp->thumb || d->font_pixels < 5) {           /* too small to read: a grey line where the words are */
        int h = d->font_pixels / 2 > 0 ? d->font_pixels / 2 : 1;
        k = t->text_length;
        while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) --k;
        if (k) ogt_box(rp, ogt_pen(&ctx, "muted"), d->box.x, baseline - h,
                       (int)(d->box.width * (int32_t)k / (int32_t)t->text_length) + 1, h);
        return;
    }
    if (t->style.text_flags & OL_TEXT_BOLD) style |= FSF_BOLD;
    if (t->style.text_flags & OL_TEXT_ITALIC) style |= FSF_ITALIC;
    if (t->style.text_flags & OL_TEXT_UNDERLINE) style |= FSF_UNDERLINED;
    if (t->style.text_flags & OL_TEXT_SUPER) baseline -= d->font_pixels / 2;
    else if (t->style.text_flags & OL_TEXT_SUB) baseline += d->font_pixels / 4;

    /* The word as the layout measured it: its font's widths at its size at 100%, where that font draws it
     * at just that width. At another zoom a style whose font runs wider than the layout's widths scaled is
     * drawn a size or two smaller (ow_fw_fit); a word that is still wider (the bold smear, a pixel a letter
     * at any size) has each glyph where the layout put it, so it keeps its width and the space after it.
     * The widths first: they may open fonts, and the drawing font is set after them. */
    n = to_latin1(s, t->text_length, buf, sizeof buf);
    drawn = n;
    while (drawn && (buf[drawn - 1] == ' ' || buf[drawn - 1] == '\t')) --drawn;
    wt = font_widths(t->style.text_flags, size);
    snprintf(file, sizeof file, "%s", wt->face);      /* the face the widths are from draws it ("": the window's) */
    for (k = 0, sum = 0, tabs = 0; k < drawn; ++k) {
        if (buf[k] == '\t') tabs += TAB_UNITS;
        else sum += wt->w[(unsigned char)buf[k]];
    }
    target = ol_scale_length(pp->scale, table_units(sum) + tabs);
    for (k = 0; k < n; ++k)
        if ((unsigned char)buf[k] < 32) buf[k] = ' ';   /* a tab: its space is the layout's */
    f = file[0] ? document_font_px(file, d->font_pixels) : NULL;
    SetFont(rp, f ? f : font);
    smear = smear_of(f ? f : font, (style & FSF_BOLD) != 0);
    SetSoftStyle(rp, smear ? style & ~FSF_BOLD : style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (f && drawn > 1 && (int)TextLength(rp, (STRPTR)buf, drawn) + (int)drawn * smear > target + 1) {
        int fit = ow_fw_fit(fonts_w(), wt, d->font_pixels, ol_scale_length(pp->scale, table_units(ow_fw_sample_sum(wt))));
        f = document_font_px(file, fit);             /* again: trying sizes may have closed the first */
        SetFont(rp, f ? f : font);
        smear = smear_of(f ? f : font, (style & FSF_BOLD) != 0);
        SetSoftStyle(rp, smear ? style & ~FSF_BOLD : style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    }
    /* bold of our own is always placed glyph by glyph; plain text when the font runs wider than the layout */
    placed = drawn && (smear || (int)TextLength(rp, (STRPTR)buf, drawn) > target + 1);

    /* the selection under the text, and where the caret goes, measured as the text is drawn: the body's, or a
     * text box's (2.0.15) */
    if (pp->body && ow_layout_node_ref(pp->layout, t->node_id, &nbox, &para, &run) && run >= 0 &&
        nbox == pp->caret.box && (nbox ? &doc->frames[nbox - 1].story : &doc->body)->paras[para].runs[run].kind ==
        OWF_RUN_TEXT) {
        ow_position rs, re;
        memset(&rs, 0, sizeof rs);
        rs.box = nbox;
        re = rs;
        rs.paragraph = re.paragraph = para;
        rs.run = re.run = run;
        rs.byte_offset = t->text_offset;
        re.byte_offset = t->text_offset + t->text_length;
        if (pp->has_sel && pos_compare(pp->sel_b, rs) > 0 && pos_compare(pp->sel_a, re) < 0) {
            size_t from = pos_compare(pp->sel_a, rs) > 0 ? pp->sel_a.byte_offset : rs.byte_offset;
            size_t to = pos_compare(pp->sel_b, re) < 0 ? pp->sel_b.byte_offset : re.byte_offset;
            int sx = d->box.x + word_x(pp, d, s, from - rs.byte_offset, placed);
            int sw = d->box.x + word_x(pp, d, s, to - rs.byte_offset, placed) - sx;
            ogt_box(rp, ogt_pen(&ctx, "selection.inactive"), sx, d->box.y, sw > 2 ? sw : 2, d->box.height);
        }
        if (pp->caret_on && !pp->caret_done && pp->caret_page == pp->page &&
            pp->caret.paragraph == para && pp->caret.run == run &&
            pp->caret.byte_offset >= rs.byte_offset && pp->caret.byte_offset <= re.byte_offset &&
            t->bounds.y == pp->caret_box.y) {
            pp->caret_x = d->box.x + word_x(pp, d, s, pp->caret.byte_offset - rs.byte_offset, placed);
            pp->caret_y = d->box.y;
            pp->caret_h = d->box.height;
            pp->caret_done = 1;
        }
    }

    if (OL_TEXT_LEADER_OF(t->style.text_flags) && t->text_length == 1 && s[0] == '\t' && d->box.width > 0) {
        pen = (t->style.foreground >> 24) ? argb_pen(t->style.foreground) : ogt_pen(&ctx, "fill.text");
        ogt_set_apen(rp, pen);
        SetSoftStyle(rp, style & ~(FSF_UNDERLINED | FSF_BOLD), FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
        paint_leader(pp, d, OL_TEXT_LEADER_OF(t->style.text_flags), pen, baseline);
        SetSoftStyle(rp, smear ? style & ~FSF_BOLD : style, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    }
    if (!(style & FSF_UNDERLINED)) n = drawn;        /* an underline runs on under the spaces */
    if (!n) return;
    pen = (t->style.foreground >> 24) ? argb_pen(t->style.foreground) : ogt_pen(&ctx, "fill.text");
    ogt_set_apen(rp, pen);
    SetDrMd(rp, JAM1);
    if (!drawn) {                                      /* spaces or a tab: only their underline */
        ogt_hline(rp, pen, d->box.x, baseline + 1, d->box.width);
        return;
    }
    if (!placed) {
        Move(rp, d->box.x, baseline);
        Text(rp, (STRPTR)buf, n);
        if (t->style.text_flags & OL_TEXT_STRIKE)
            ogt_hline(rp, pen, d->box.x, baseline - (rp->TxBaseline + 1) / 3, text_length(rp, buf, drawn));
        return;
    }
    SetSoftStyle(rp, style & ~(FSF_UNDERLINED | (smear ? FSF_BOLD : 0)), FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    to_latin1(s, t->text_length, buf, sizeof buf);    /* the tabs again, for their widths */
    for (k = 0, sum = 0, tabs = 0; k < drawn; ++k) {
        unsigned char c = (unsigned char)buf[k];
        if (c > ' ') {
            int gx = d->box.x + (sum || tabs ? ol_scale_length(pp->scale, table_units(sum) + tabs) : 0);
            Move(rp, gx, baseline);
            Text(rp, (STRPTR)&buf[k], 1);
            if (smear) {                             /* bold: the glyph again, the smear on */
                Move(rp, gx + smear, baseline);
                Text(rp, (STRPTR)&buf[k], 1);
            }
        }
        if (c == '\t') tabs += TAB_UNITS;
        else sum += wt->w[c];
    }
    if (style & FSF_UNDERLINED)
        ogt_hline(rp, pen, d->box.x, baseline + 1, n > drawn ? d->box.width : target);
    if (t->style.text_flags & OL_TEXT_STRIKE)
        ogt_hline(rp, pen, d->box.x, baseline - (rp->TxBaseline + 1) / 3, target);
}

static void paint_op(void *userdata, const ol_device_op *d)
{
    page_paint *pp = (page_paint *)userdata;
    struct RastPort *rp = pp->rp;
    switch (d->op->type) {
    case OL_OP_TEXT:
        paint_text(pp, d);
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
        else { ogt_set_apen(rp, pen); Move(rp, d->x1, d->y1); Draw(rp, d->x2, d->y2); }
        break;
    }
    case OL_OP_IMAGE: {
        int index = d->op->u.image.image_id;
        ow_image_cache *c;
        if (index == OW_LAYOUT_NO_IMAGE) break;       /* 2.0.17: a text box's room in its line */
        c = pp->thumb ? image_cache_peek(index) : image_cache_get(index);   /* 2.0.10 */
        if (d->box.width < 1 || d->box.height < 1) break;
        if (c && CyberGfxBase && screen_depth() > 8)
            draw_picture(rp, c, d->box.x, d->box.y, d->box.width, d->box.height, &pp->lim);
        else {
            char label[96];
            ogt_frame(rp, ogt_pen(&ctx, "group.line"), d->box.x, d->box.y, d->box.width, d->box.height);
            if (!pp->thumb && d->box.width > 24 && d->box.height > fh + 8) {
                SetFont(rp, font);
                SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
                snprintf(label, sizeof label, "Image: %s",
                         (doc && index >= 0 && index < doc->nimages && doc->images[index].name)
                         ? doc->images[index].name : "embedded");
                ogt_text(rp, ogt_pen(&ctx, "muted"), d->box.x + 6, d->box.y + 6, label, d->box.width - 12);
            }
        }
        break;
    }
    case OL_OP_SHAPE:
        /* OpenWrite does not currently put drawing nodes in its document. */
        break;
    }
}

/* Draws one page of the pages at s (its top left corner at s's origin), what crosses clip: the body, then
 * its header and footer; the caret and the selection when it is the document's page. */
/* 2.0.28: comments in the page's right margin, each beside its line, stacked so they do not overlap; the one
 * at the caret outlined in the accent colour, replies under their comment and indented, done ones muted. Only
 * what fits inside clip is drawn. */
static void clip_box(struct RastPort *rp, LONG pen, int x, int y, int w, int h, const ol_pixel_rect *clip)
{
    int x1 = x + w, y1 = y + h;
    if (clip) {
        if (x < clip->x) x = clip->x;
        if (y < clip->y) y = clip->y;
        if (x1 > clip->x + clip->width) x1 = clip->x + clip->width;
        if (y1 > clip->y + clip->height) y1 = clip->y + clip->height;
    }
    if (x1 > x && y1 > y) ogt_box(rp, pen, x, y, x1 - x, y1 - y);
}

static int line_inside(int y, int h, const ol_pixel_rect *clip)
{
    return !clip || (y >= clip->y && y + h <= clip->y + clip->height);
}

static void paint_comments(struct RastPort *rp, ow_layout *l, int page, const ol_scale *s,
                           const ol_pixel_rect *clip)
{
    const ol_page *pg = ow_layout_page(l);
    int n = ow_editor_comment_count(editor), k, current = -1, bottom = -100000;
    int x0, x1, fh = font ? font->tf_YSize : 8, line = fh + 2;
    ow_comment_info info;
    if (!n || !pg) return;
    if (ow_editor_current_comment(editor, &info)) current = info.index;
    x0 = ol_scale_x(s, pg->width - pg->margin_right) + 6;
    x1 = ol_scale_x(s, pg->width) - 4;
    if (x1 - x0 < 24) return;                        /* no room: the highlighted text shows it */
    if (font) SetFont(rp, font);
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    for (k = 0; k < n; ++k) {
        int at_page, x, y, w, h, i, top;
        ol_rect at;
        char head[96], body[160];
        const char *who, *nl;
        size_t len;
        LONG edge, ink;
        if (!ow_editor_comment_info(editor, k, &info) || !ow_layout_caret(l, info.end, &at_page, &at) ||
            at_page != page)
            continue;
        x = x0 + (info.reply_to >= 0 ? 8 : 0);
        w = x1 - x;
        if (w < 16) continue;
        h = 2 * line + 4;
        top = ol_scale_y(s, at.y);
        y = top > bottom + 3 ? top : bottom + 3;
        bottom = y + h;
        edge = ogt_pen(&ctx, k == current ? "accent" : "group.line");
        ink = ogt_pen(&ctx, info.done ? "muted" : "fill.text");
        /* the place in the text, and a line from the margin to the note */
        if (line_inside(top, ol_scale_length(s, at.height), clip))
            ogt_vline(rp, edge, ol_scale_x(s, at.x), top, ol_scale_length(s, at.height) > 1 ?
                      ol_scale_length(s, at.height) : 2);
        clip_box(rp, edge, x0 - 6, top, 6 + (info.reply_to >= 0 ? 8 : 0), 1, clip);
        clip_box(rp, ogt_pen_rgb(&ctx, info.done ? (ogt_rgb){236,236,236} : (ogt_rgb){255,246,190}), x, y, w, h, clip);
        clip_box(rp, edge, x, y, w, 1, clip);
        clip_box(rp, edge, x, y + h - 1, w, 1, clip);
        clip_box(rp, edge, x, y, 1, h, clip);
        clip_box(rp, edge, x + w - 1, y, 1, h, clip);
        who = info.initials && *info.initials ? info.initials : info.author && *info.author ? info.author : "?";
        len = to_latin1(who, strlen(who), head, sizeof head - 24);
        if (info.date && strlen(info.date) >= 10) {
            snprintf(head + len, sizeof head - len, " %.2s/%.2s", info.date + 8, info.date + 5);
        }
        nl = info.text ? strchr(info.text, '\n') : NULL;
        len = info.text ? (nl ? (size_t)(nl - info.text) : strlen(info.text)) : 0;
        i = (int)to_latin1(info.text ? info.text : "", len, body, sizeof body - 4);
        if (nl && i < (int)sizeof body - 4) strcpy(body + i, " ...");
        if (line_inside(y + 2, fh, clip)) {
            ogt_bold(rp, 1);
            ogt_text(rp, ink, x + 3, y + 2, head, w - 6);
            ogt_bold(rp, 0);
        }
        if (line_inside(y + 2 + line, fh, clip)) ogt_text(rp, ink, x + 3, y + 2 + line, body, w - 6);
    }
}

static void paint_page(struct RastPort *rp, ow_layout *l, int page, const ol_scale *s,
                       const ol_pixel_rect *clip, int thumb)
{
    page_paint pp;
    int k;
    memset(&pp, 0, sizeof pp);
    pp.rp = rp;
    pp.layout = l;
    pp.scale = s;
    pp.od = ow_layout_body(l);
    pp.body = 1;
    pp.thumb = thumb;
    pp.page = page;
    {   /* 2.0.10: the page, cut to clip */
        int x0 = ol_scale_x(s, 0), y0 = ol_scale_y(s, 0);
        int x1 = ol_scale_x(s, ow_layout_page(l)->width), y1 = ol_scale_y(s, ow_layout_page(l)->height);
        if (clip) {
            if (x0 < clip->x) x0 = clip->x;
            if (y0 < clip->y) y0 = clip->y;
            if (x1 > clip->x + clip->width) x1 = clip->x + clip->width;
            if (y1 > clip->y + clip->height) y1 = clip->y + clip->height;
        }
        pp.lim.x = x0;
        pp.lim.y = y0;
        pp.lim.width = x1 > x0 ? x1 - x0 : 0;
        pp.lim.height = y1 > y0 ? y1 - y0 : 0;
    }
    if (!thumb && editor) {
        ow_selection sel = ow_editor_selection(editor);
        if (pos_compare(sel.anchor, sel.focus) <= 0) { pp.sel_a = sel.anchor; pp.sel_b = sel.focus; }
        else { pp.sel_a = sel.focus; pp.sel_b = sel.anchor; }
        pp.has_sel = pos_compare(pp.sel_a, pp.sel_b) != 0;
        pp.caret = sel.focus;
        pp.caret_on = !pp.has_sel && ow_layout_caret(l, sel.focus, &pp.caret_page, &pp.caret_box);
    }
    /* 2.0.16: the header and the footer first, under the body (and its frames behind the text), as Word draws
     * them: a header's picture over the page stays behind the body's text */
    for (k = 0; k < 2; ++k) {
        char date[32] = "", time_text[16] = "";
        time_t now = time(NULL);
        struct tm *tmv = localtime(&now);
        ol_unit sx, sy;
        ol_scale ss = *s;
        ol_document *story;
        if (tmv) {
            strftime(date, sizeof date, "%d/%m/%Y", tmv);
            strftime(time_text, sizeof time_text, "%H:%M", tmv);
        }
        story = ow_layout_story(l, k ? OW_STORY_FOOTER : OW_STORY_HEADER, page, date, time_text, &sx, &sy);
        if (!story) continue;
        ss.origin_x = ol_scale_x(s, sx);
        ss.origin_y = ol_scale_y(s, sy);
        pp.od = story;
        pp.body = 0;
        ol_draw_page(story, 0, &ss, clip, paint_op, &pp);
        ol_document_free(story);
    }
    pp.od = ow_layout_body(l);
    pp.body = 1;
    ol_draw_page(pp.od, page, s, clip, paint_op, &pp);
    {
        /* 2.0.30: the page's footnotes at the foot of its body, a short rule above them */
        ol_unit fx, fy;
        ol_document *feet = ow_layout_footnotes(l, page, &fx, &fy);
        if (feet) {
            ol_scale fs = *s;
            const ol_page *pg = ow_layout_page(l);
            int ry = ol_scale_y(s, fy - OW_LAYOUT_NOTE_SEPARATOR / 2);
            int rx = ol_scale_x(s, fx);
            int rw = ol_scale_length(s, (pg->width - pg->margin_left - pg->margin_right) / 3);
            ogt_hline(rp, ogt_pen(&ctx, "fill.text"), rx, ry, rw > 1 ? rw : 1);
            fs.origin_x = ol_scale_x(s, fx);
            fs.origin_y = ol_scale_y(s, fy);
            pp.od = feet;
            pp.body = 0;
            ol_draw_page(feet, 0, &fs, clip, paint_op, &pp);
            ol_document_free(feet);
            pp.od = ow_layout_body(l);
            pp.body = 1;
        }
    }
    SetSoftStyle(rp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    if (pp.caret_on && pp.caret_page == page) {
        if (!pp.caret_done) {                        /* not in a word (an empty paragraph): where the layout says */
            pp.caret_x = ol_scale_x(s, pp.caret_box.x);
            pp.caret_y = ol_scale_y(s, pp.caret_box.y);
            pp.caret_h = ol_scale_length(s, pp.caret_box.height);
        }
        ogt_vline(rp, ogt_pen(&ctx, "accent"), pp.caret_x, pp.caret_y, pp.caret_h > 1 ? pp.caret_h - 1 : 1);
    }
    if (!thumb && selected_frame >= 0) {
        int fp;
        ol_rect at;
        if (ow_layout_frame_rect(l, selected_frame, &fp, &at) && fp == page) {
            ol_pixel_rect box = ol_scale_rect(s, at);
            int hs = 5;
            LONG pen = ogt_pen(&ctx, "accent");
            if (frame_dragging && frame_drag_frame == selected_frame && frame_drag_page == page) {
                int dx = frame_drag_x - frame_drag_start_x;
                int dy = frame_drag_y - frame_drag_start_y;
                ow_frame_adjust_rect(&box.x, &box.y, &box.width, &box.height,
                                     (ow_frame_handle)frame_drag_mode, dx, dy, 8, 8);
            }
            ogt_frame(rp, pen, box.x, box.y, box.width, box.height);
            ogt_set_apen(rp, pen);
            RectFill(rp, box.x - hs / 2, box.y - hs / 2,
                     box.x + hs / 2, box.y + hs / 2);
            RectFill(rp, box.x + box.width - hs / 2, box.y - hs / 2,
                     box.x + box.width + hs / 2, box.y + hs / 2);
            RectFill(rp, box.x - hs / 2, box.y + box.height - hs / 2,
                     box.x + hs / 2, box.y + box.height + hs / 2);
            RectFill(rp, box.x + box.width - hs / 2, box.y + box.height - hs / 2,
                     box.x + box.width + hs / 2, box.y + box.height + hs / 2);
        }
    }    if (!thumb && editor) paint_comments(rp, l, page, s, clip);
}

/* The editor position under the pointer (window coordinates). */
static int page_hit(int mx, int my, ow_position *out)
{
    ow_layout *l = pages();
    int cx, cy, page;
    ol_scale s;
    if (!l || !pv || !ogt_pageview_to_content(pv, mx, my, &cx, &cy)) return 0;
    page = ogt_pageview_page_at(pv, cy);
    if (page < 0) return 0;
    page_scale(&s, PAGE_MARGIN, ogt_pageview_page_top(pv, page));
    return ow_layout_hit(l, page, ol_unscale_x(&s, cx), ol_unscale_y(&s, cy), out);
}

/* The topmost authored frame under the pointer. GFOpenLayout resolves the
 * anchor, wrapping and pagination; the application only converts coordinates. */
static int page_frame_hit(int mx, int my, int *frame_index, ol_rect *rect)
{
    ow_layout *l = pages();
    int cx, cy, page;
    ol_scale s;
    if (!l || !pv || !ogt_pageview_to_content(pv, mx, my, &cx, &cy)) return 0;
    page = ogt_pageview_page_at(pv, cy);
    if (page < 0) return 0;
    page_scale(&s, PAGE_MARGIN, ogt_pageview_page_top(pv, page));
    return ow_layout_hit_frame(l, page, ol_unscale_x(&s, cx), ol_unscale_y(&s, cy),
                               frame_index, rect);
}

/* A drag previews in page-view pixels and commits once on release, so a move
 * or resize is one core edit and therefore one Undo step. */
static int frame_drag_begin(int mx, int my)
{
    ow_layout *l = pages();
    int cx, cy, page, frame_index, near = 7;
    ol_scale s;
    ol_rect at;
    ol_pixel_rect box;
    if (!l || !doc || !pv || !ogt_pageview_to_content(pv, mx, my, &cx, &cy)) return 0;
    page = ogt_pageview_page_at(pv, cy);
    if (page < 0) return 0;
    page_scale(&s, PAGE_MARGIN, ogt_pageview_page_top(pv, page));
    if (!ow_layout_hit_frame(l, page, ol_unscale_x(&s, cx), ol_unscale_y(&s, cy),
                             &frame_index, &at) || frame_index != selected_frame ||
        frame_index < 0 || frame_index >= doc->nframes ||
        doc->frames[frame_index].story.nparas != 0)
        return 0;
    box = ol_scale_rect(&s, at);
    frame_drag_mode = OW_FRAME_MOVE;
    if (abs(cx - box.x) <= near && abs(cy - box.y) <= near) frame_drag_mode = OW_FRAME_RESIZE_NW;
    else if (abs(cx - (box.x + box.width)) <= near && abs(cy - box.y) <= near) frame_drag_mode = OW_FRAME_RESIZE_NE;
    else if (abs(cx - box.x) <= near && abs(cy - (box.y + box.height)) <= near) frame_drag_mode = OW_FRAME_RESIZE_SW;
    else if (abs(cx - (box.x + box.width)) <= near &&
             abs(cy - (box.y + box.height)) <= near) frame_drag_mode = OW_FRAME_RESIZE_SE;
    frame_dragging = 1;
    frame_drag_frame = frame_index;
    frame_drag_page = page;
    frame_drag_start_x = frame_drag_x = cx;
    frame_drag_start_y = frame_drag_y = cy;
    frame_drag_rect = at;
    return 1;
}

static void frame_drag_update(int mx, int my)
{
    int cx, cy;
    if (!frame_dragging || !pv || !ogt_pageview_to_content(pv, mx, my, &cx, &cy)) return;
    frame_drag_x = cx;
    frame_drag_y = cy;
    request_document_redraw();
}

static void frame_drag_finish(int mx, int my)
{
    ol_scale s;
    int dx, dy, x, y, width, height, min_size = mm_to_twips(10), rc;
    owf_frame_wrap wrap;
    if (!frame_dragging) return;
    frame_drag_update(mx, my);
    page_scale(&s, PAGE_MARGIN, ogt_pageview_page_top(pv, frame_drag_page));
    dx = (int)(ol_unscale_x(&s, s.origin_x + frame_drag_x - frame_drag_start_x) /
               OL_UNITS_PER_TWIP);
    dy = (int)(ol_unscale_y(&s, s.origin_y + frame_drag_y - frame_drag_start_y) /
               OL_UNITS_PER_TWIP);
    x = (int)(frame_drag_rect.x / OL_UNITS_PER_TWIP);
    y = (int)(frame_drag_rect.y / OL_UNITS_PER_TWIP);
    width = (int)(frame_drag_rect.width / OL_UNITS_PER_TWIP);
    height = (int)(frame_drag_rect.height / OL_UNITS_PER_TWIP);
    ow_frame_adjust_rect(&x, &y, &width, &height,
                         (ow_frame_handle)frame_drag_mode, dx, dy, min_size, min_size);
    wrap = (owf_frame_wrap)doc->frames[frame_drag_frame].wrap;
    frame_dragging = 0;
    frame_drag_mode = 0;
    if (!dx && !dy) { request_document_redraw(); return; }
    rc = ow_editor_apply_frame_geometry(editor, frame_drag_frame, x, y, width, height, wrap);
    if (rc == OWF_OK) {
        pages_changed();
        editor_repaint("Frame moved or resized; Undo restores its previous geometry.");
    } else if (rc == OWF_ERR_MEMORY) tell("Frame", "There is not enough memory to update the frame.");
    else tell("Frame", "The frame geometry is not valid.");
}

/* Up and down a line, from where the caret is drawn: the nearest place on the line above or below, on
 * this page or the one before or after. 0 when there are no pages (the editor's own move then). */
static int caret_line_move(int dir, int extend)
{
    ow_layout *l = pages();
    ow_selection sel;
    ow_position to;
    ol_rect box, nb;
    ol_unit y, step, height;
    int page, np, found, i, count;
    if (!l || !editor) return 0;
    sel = ow_editor_selection(editor);
    if (!ow_layout_caret(l, sel.focus, &page, &box)) return 0;
    count = ow_layout_page_count(l);
    height = ow_layout_page(l)->height;
    step = box.height / 2 > OL_POINT(1) ? box.height / 2 : OL_POINT(1);
    y = dir < 0 ? box.y : box.y + box.height;
    np = page;
    for (i = 0; i < 512; ++i) {
        y += dir < 0 ? -step : step;
        if (y < 0) {
            if (np == 0) return 1;                   /* the first line: the caret stays */
            --np;
            y = height - 1;
        } else if (y >= height) {
            if (np + 1 >= count) return 1;           /* the last line */
            ++np;
            y = 0;
        }
        /* 2.0.15: in the story the caret is in (a text box's lines, not the body's beside it) */
        if (ow_layout_hit(l, np, box.x, y, &to) && to.box == sel.focus.box && ow_layout_caret(l, to, &found, &nb) &&
            (found != page || nb.y != box.y))
            break;
    }
    if (i >= 512) return 1;
    if (!extend) sel.anchor = to;
    sel.focus = to;
    ow_editor_set_selection(editor, &sel);
    return 1;
}


/* The view's content: the pages at this zoom. */
static void pages_changed(void)
{
    int count = page_total(), pw, ph;
    page_size(&pw, &ph);
    if (pv) ogt_pageview_set_pages(pv, count, pw, ph, PAGE_GAP, PAGE_MARGIN);
    if (nav) ogt_list_set_count(nav, count);
}

static void draw_pages(void *user, struct RastPort *rp, int x, int y, int w, int h, int cx, int cy)
{
    int p, count = page_total(), pw, ph;
    LONG paper = ogt_pen_rgb(&ctx, (ogt_rgb){255,255,255});
    LONG shadow = ogt_pen(&ctx, "track");
    LONG edge = ogt_pen(&ctx, "group.line");
    ow_renderer r;
    ow_layout *l = pages();
    (void)user;
    if (!doc || !editor) return;
    page_size(&pw, &ph);
    hit_count = 0;
    for (p = 0; p < count; ++p) {
        int top = ogt_pageview_page_top(pv, p) - cy;
        if (top + ph + 4 < 0 || top > h) continue;
        page_box = (box){ x + PAGE_MARGIN - cx, y + top, pw, ph };
        ogt_box(rp, shadow, page_box.x + 4, page_box.y + 4, pw, ph);
        ogt_box(rp, paper, page_box.x, page_box.y, pw, ph);
        ogt_frame(rp, edge, page_box.x, page_box.y, pw, ph);
        if (l) {                                     /* 2.1: GFOpenLayout's page, at the screen's pixels */
            ol_scale s;
            ol_pixel_rect clip;
            page_scale(&s, page_box.x, page_box.y);
            clip.x = x;
            clip.y = y;
            clip.width = w;
            clip.height = h;
            paint_page(rp, l, p, &s, &clip, 0);
            continue;
        }
        render_x0 = page_box.x;
        render_y0 = page_box.y;
        render_bottom = page_box.y;
        render_para = -1;
        render_num = page_scale_num();
        render_den = page_scale_den();
        memset(&r, 0, sizeof r);
        r.userdata = rp;
        r.begin_page = render_begin;
        r.end_page = render_end;
        r.text_run = render_text_run;
        r.rule = render_rule;
        r.image = render_image;
        ow_editor_render_page(editor, p, &r);         /* its spans join the pages' before (render_begin keeps them) */
    }
    if (font) SetFont(rp, font);
}

static void draw_page(void)
{
    if (pv) ogt_pageview_draw(pv);
}

/* The navigator: a thumbnail a page, the caret's page marked. */
static int nav_height(void *user, int index) { (void)user; (void)index; return navigator_box.w * 2 / 3 * 141 / 100 + fh + 14; }

static void nav_draw(void *user, int index, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    int pw = w * 2 / 3 - 8, ph = pw * 141 / 100, px = x + (w - pw) / 2, py = y + 6;
    char s[16];
    (void)user; (void)h;
    ogt_box(rp, ogt_pen(&ctx, "track"), px + 2, py + 2, pw, ph);
    ogt_box(rp, ogt_pen_rgb(&ctx, (ogt_rgb){255,255,255}), px, py, pw, ph);
    {   /* 2.1: the page itself, small: GFOpenLayout's ops at the thumbnail's scale, words as grey lines */
        ow_layout *l = pages();
        if (l && index < ow_layout_page_count(l) && pw > 8) {
            ol_scale s;
            ol_pixel_rect clip;
            memset(&s, 0, sizeof s);
            ol_scale_fit(&s, ow_layout_page(l)->width, pw);
            s.origin_x = px;
            s.origin_y = py;
            clip.x = px;
            clip.y = py;
            clip.width = pw;
            clip.height = ph;
            paint_page(rp, l, index, &s, &clip, 1);
            SetFont(rp, font);
        }
    }
    ogt_frame(rp, ogt_pen(&ctx, selected || index == page_index ? "accent" : "group.line"), px, py, pw, ph);
    if (selected || index == page_index) ogt_frame(rp, ogt_pen(&ctx, "accent"), px - 1, py - 1, pw + 2, ph + 2);
    snprintf(s, sizeof s, "%d", index + 1);
    ogt_text(rp, ogt_pen(&ctx, "muted"), x + (w - ogt_text_width(rp, s)) / 2, py + ph + 3, s, 0);
}

static void draw_status(void)
{
    char s[96];
    if (!win) return;
    snprintf(s, sizeof s, "Page %d of %d", page_index + 1, page_total());
    ogt_status_set(&sbar, 0, s);
    snprintf(s, sizeof s, "%d words", word_count());
    ogt_status_set(&sbar, 1, s);
    ogt_status_set(&sbar, 2, status);
    snprintf(s, sizeof s, "%s  %d%%%s", current_format, zoom, editor && ow_editor_track_changes(editor) ? "  Rec" : "");
    ogt_status_set(&sbar, 3, s);
}

static void draw_ruler(void)
{
    if (!doc || !win) return;
    ogt_ruler_set_page(&ruler, doc->page.width, doc->page.margin_left, doc->page.margin_right, page_scale_num(), page_scale_den(), OGT_RULER_CM);
    ogt_ruler_draw(&ruler);
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
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), format_box.x, format_box.y + format_box.h - 1, format_box.w);
    if (glist2) { RefreshGList(glist2, win, NULL, -1); GT_RefreshWindow(win, NULL); }
    show_formats();
    if (navigator_box.w) {
        ogt_list_draw(nav);
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), navigator_box.x + navigator_box.w, navigator_box.y, navigator_box.h);
    }
    draw_ruler();
    draw_page();
    if (inspector_box.w) {
        ogt_vline(rp, ogt_pen(&ctx, "group.line"), inspector_box.x - 1, inspector_box.y, inspector_box.h);
        ogt_sections_draw(&isec, win, &ctx);
    }
    draw_status();
}

static void redraw_document_area(void)
{
    if (!win) return;
    SetFont(win->RPort, font);
    pages_changed();
    show_formats();
    draw_ruler();
    draw_page();
    if (nav) ogt_list_draw(nav);
    draw_status();
}

static void free_gadgets(void)
{
    if (!win) return;
    if (chain) { RemoveGList(win, chain, -1); chain = NULL; }
    if (glist2) { RemoveGList(win, glist2, -1); FreeGadgets(glist2); glist2 = NULL; }
    memset(fmt_gad, 0, sizeof fmt_gad);
    inspector_live = 0;
    if (pv) { ogt_pageview_free(pv); pv = NULL; }
    if (nav) { ogt_list_free(nav); nav = NULL; }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    struct NewGadget ng;
    struct Gadget *g;
    int tbh, use_style, navw, inspw, body_y, body_h, x, y, lh = fh + 6, pages;
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
    format_box = (box){ area.x, toolbar_box.y + toolbar_box.h, area.w, lh + 10 };
    status_box = (box){ area.x, area.y + area.h - fh - 10, area.w, fh + 10 };

    body_y = format_box.y + format_box.h;
    body_h = status_box.y - body_y;
    navw = show_nav && area.w >= 560 ? (area.w < 1000 ? 124 : area.w / 8) : 0;
    inspw = show_inspector && area.w >= 700 ? (area.w < 1200 ? 210 : area.w / 5) : 0;
    navigator_box = (box){ area.x, body_y, navw, body_h };
    inspector_box = (box){ area.x + area.w - inspw, body_y, inspw, body_h };
    canvas_box = (box){ area.x + navw + (navw ? 1 : 0), body_y,
                        area.w - navw - inspw - (navw ? 1 : 0) - (inspw ? 1 : 0), body_h };
    ruler_box = (box){ canvas_box.x + 4, canvas_box.y + 4, canvas_box.w - 8, fh + 10 };
    page_view_box = (box){ canvas_box.x + 4, ruler_box.y + ruler_box.h + 4, canvas_box.w - 8, canvas_box.h - ruler_box.h - 12 };

    if ((chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR)))
        AddGList(win, chain, ~0, -1, NULL);

    /* the format row: style, font, size, zoom, as GadTools gadgets */
    g = CreateContext(&glist2);
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;
    x = format_box.x + 6; y = format_box.y + 4;
#define FG(kind, id, lab, w, ...) \
    (ng.ng_LeftEdge = x, ng.ng_TopEdge = y, ng.ng_Width = (w), ng.ng_Height = lh, ng.ng_GadgetText = (STRPTR)(lab), \
     ng.ng_Flags = PLACETEXT_LEFT, ng.ng_GadgetID = (id), g = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE), x += (w) + 10)
    x += ogt_text_width(rp, "Style") + 8;
    FG(CYCLE_KIND, GID_STYLE, "Style", 120, GTCY_Labels, (ULONG)style_labels, GTCY_Active, 0); fmt_gad[0] = g;
    x += ogt_text_width(rp, "Font") + 8;
    FG(CYCLE_KIND, GID_FONT, "Font", 150, GTCY_Labels, (ULONG)font_labels, GTCY_Active, 0); fmt_gad[1] = g;
    x += ogt_text_width(rp, "Size") + 8;
    FG(INTEGER_KIND, GID_SIZE, "Size", 50, GTIN_Number, 12, GTIN_MaxChars, 3); fmt_gad[2] = g;
    x += ogt_text_width(rp, "Zoom") + 8;
    FG(CYCLE_KIND, GID_ZOOM, "Zoom", 110, GTCY_Labels, (ULONG)zoom_labels, GTCY_Active, (ULONG)zoom_choice); fmt_gad[3] = g;
#undef FG

    /* the inspector: three sections of GadTools gadgets */
    inspector_live = 0;
    if (inspw) {
        ogt_row para[] = {
            { OGT_ROW_CYCLE, GID_I_STYLE, "Style", 0, style_labels, NULL, 0, 0, 0, 0 },
            { OGT_ROW_CYCLE, GID_I_ALIGN, "Align", 0, align_labels, NULL, 0, 0, 0, 0 },
            { OGT_ROW_INTEGER, GID_I_BEFORE, "Before", 56, NULL, NULL, 0, 0, 0, 0 },
            { OGT_ROW_INTEGER, GID_I_AFTER, "After", 56, NULL, NULL, 0, 0, 0, 0 },
            { OGT_ROW_CYCLE, GID_I_SPACING, "Leading", 0, spacing_labels, NULL, 0, 0, 0, 0 },
        };
        ogt_row chr[] = {
            { OGT_ROW_CYCLE, GID_I_FONT, "Font", 0, font_labels, NULL, 0, 0, 0, 0 },
            { OGT_ROW_INTEGER, GID_I_SIZE, "Size", 56, NULL, NULL, 12, 0, 0, 0 },
            { OGT_ROW_CHECKBOX, GID_I_BOLD, "Bold", 0, NULL, NULL, 0, 0, 0, 0 },
            { OGT_ROW_CHECKBOX, GID_I_ITALIC, "Italic", 0, NULL, NULL, 0, 0, 0, 0 },
        };
        ogt_row page[] = {
            { OGT_ROW_CYCLE, GID_I_PAPER, "Paper", 0, paper_labels, NULL, 0, 0, 0, 0 },
            { OGT_ROW_INTEGER, GID_I_MARGIN, "Margin mm", 56, NULL, NULL, 20, 0, 0, 0 },
        };
        /* 2.0.11: only the sections that fit above the status bar. ogt_sections lays a section out as its title
         * (a row and 4) and its rows (a row and its 4 gap each), 10 between sections; a window too low for the
         * Page section had its gadgets drawn over the status bar and the window's border. */
        int room = inspector_box.h - 8, need;
        ogt_sections_init(&isec, inspector_box.x + 10, inspector_box.y + 8, inspector_box.w - 20,
                          ogt_text_width(rp, "Margin mm") + 6, lh, &font_attr, vi);
        if ((need = 6 * (lh + 4)) <= room) { ogt_sections_add(&isec, "Paragraph", para, 5); room -= need + 10; }
        if ((need = 5 * (lh + 4)) <= room) { ogt_sections_add(&isec, "Character", chr, 4); room -= need + 10; }
        if ((need = 3 * (lh + 4)) <= room) ogt_sections_add(&isec, "Page", page, 2);
        g = isec.n ? ogt_sections_gadgets(&isec, g) : g;
        inspector_live = g != NULL;
    }
    if (g && glist2) AddGList(win, glist2, ~0, -1, NULL);
    else if (glist2) { FreeGadgets(glist2); glist2 = NULL; }

    /* the navigator, the ruler, the page view, the status bar */
    pages = page_total();
    if (navw) {
        nav = ogt_list_new(win, &ctx, GID_NAV, nav_height, nav_draw, NULL, NULL, "list");
        if (nav) {
            ogt_list_layout(nav, navigator_box.x + 4, navigator_box.y + 4, navigator_box.w - 8, navigator_box.h - 8);
            ogt_list_set_count(nav, pages < 1 ? 1 : pages);
            ogt_list_select(nav, page_index);
        }
    }
    ogt_ruler_init(&ruler, win, &ctx);
    pv = ogt_pageview_new(win, &ctx, GID_VIEW, draw_pages, NULL, "list");
    if (pv) {
        ogt_pageview_layout(pv, page_view_box.x, page_view_box.y, page_view_box.w, page_view_box.h);
        pages_changed();
        ogt_pageview_show_page(pv, page_index);
    }
    if (pv) {   /* the ruler's zero is the page's left edge, where the view puts it */
        int bx, by, bw, bh;
        ogt_pageview_box(pv, &bx, &by, &bw, &bh);
        ogt_ruler_layout(&ruler, ruler_box.x, ruler_box.y, ruler_box.w, ruler_box.h,
                         bx + (pv->cw < bw ? (bw - pv->cw) / 2 : 0) + PAGE_MARGIN);
    }
    ogt_status_init(&sbar, win, &ctx, 4);
    ogt_status_layout(&sbar, status_box.x, status_box.y, status_box.w, status_box.h);
}

static void set_page_view(int page)
{
    int pages = page_total();
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;
    page_index = page;
    if (pv) ogt_pageview_show_page(pv, page);
    if (nav) { ogt_list_select(nav, page); ogt_list_show(nav, page); }
    { char msg[64]; snprintf(msg, sizeof msg, "Page %d of %d.", page_index + 1, pages); set_status(msg); }
    request_document_redraw();
}

/* After Find or a caret move: the caret's page, scrolled so the caret shows (found among the spans the
 * last drawing laid out). */
static void bring_focus_into_view(void)
{
    int y, h, bx, by, bw, bh;
    ow_layout *l = pages();
    if (!editor || !pv) return;
    follow_caret_page();
    if (l) {                                         /* 2.1: the caret's line from the layout, in content pixels */
        ol_rect box;
        ol_scale s;
        int page;
        if (ow_layout_caret(l, ow_editor_selection(editor).focus, &page, &box)) {
            page_scale(&s, 0, ogt_pageview_page_top(pv, page));
            y = ol_scale_y(&s, box.y);
            h = ol_scale_length(&s, box.height);
            ogt_pageview_box(pv, &bx, &by, &bw, &bh);
            if (y < pv->top || y + h > pv->top + bh) ogt_pageview_scroll_by(pv, y - (pv->top + bh / 3));
        }
        redraw_document_area();
        return;
    }
    track_pos = ow_editor_selection(editor).focus;
    track_y = -1; track_h = 0; track_on = 1;
    redraw_document_area();
    track_on = 0;
    y = track_y; h = track_h;
    if (y < 0) { ogt_pageview_show_page(pv, page_index); return; }
    ogt_pageview_box(pv, &bx, &by, &bw, &bh);
    if (y >= by && y + h <= by + bh) return;
    ogt_pageview_scroll_by(pv, y - (by + bh / 3));
    redraw_document_area();
}

static void scroll_page(int dx, int dy)
{
    (void)dx;
    if (pv) ogt_pageview_scroll_by(pv, dy);
}

static int fit_zoom(int width_only)
{
    int vw, vh, zw, zh, z;
    if (!doc) return 100;
    vw = page_view_box.w - 2 * PAGE_MARGIN - 20;
    vh = page_view_box.h - 2 * PAGE_MARGIN - 8;
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
    int i;
    if (z < 25) z = 25;
    if (z > 200) z = 200;
    zoom = z;
    if (editor) ow_editor_set_zoom(editor, z);
    for (i = 0; i < ZOOM_STEPS; ++i) if (zoom_values[i] == z) zoom_choice = i;
    relayout();
}

/* 2.0.11: the next of the Zoom gadget's percentages above (dir > 0) or below the zoom. */
static int zoom_step(int dir)
{
    int i;
    if (dir > 0) {
        for (i = 0; i < ZOOM_STEPS; ++i) if (zoom_values[i] > zoom) return zoom_values[i];
        return zoom_values[ZOOM_STEPS - 1];
    }
    for (i = ZOOM_STEPS - 1; i >= 0; --i) if (zoom_values[i] < zoom) return zoom_values[i];
    return zoom_values[0];
}

/* The format row's and the inspector's gadgets: what was picked, applied to the caret's text. */
static int format_gadget(struct Gadget *g, UWORD code)
{
    owf_charfmt cf;
    owf_parafmt pf;
    LONG n = 0;
    if (!editor || !g) return 0;
    memset(&cf, 0, sizeof cf);
    memset(&pf, 0, sizeof pf);
    ow_editor_current_charfmt(editor, &cf);
    ow_editor_current_parafmt(editor, &pf);
    switch (g->GadgetID) {
    case GID_STYLE: case GID_I_STYLE:
        pf.heading = code;
        editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_HEADING), code ? "Heading style." : "Body text style.");
        return 1;
    case GID_FONT: case GID_I_FONT: {
        int f = owf_doc_font(doc, font_labels[code], font_kinds[code]);
        if (f < 0) { tell("Typeface", "There is not enough memory to add that typeface."); return 1; }
        cf.font = f;
        editor_result(ow_editor_apply_charfmt(editor, &cf, OW_CHARFMT_FONT), "Typeface changed.");
        return 1;
    }
    case GID_SIZE: case GID_I_SIZE:
        GT_GetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)&n, TAG_DONE);
        if (n < 4) n = 4;
        if (n > 144) n = 144;
        cf.size = n * OWF_TWIPS_PER_POINT;
        editor_result(ow_editor_apply_charfmt(editor, &cf, OW_CHARFMT_SIZE), "Type size changed.");
        return 1;
    case GID_ZOOM:
        zoom_choice = code;
        set_zoom(zoom_values[code] == -1 ? fit_zoom(0) : zoom_values[code] == -2 ? fit_zoom(1) : zoom_values[code]);
        return 1;
    case GID_I_ALIGN:
        pf.align = (owf_align)code;
        editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_ALIGN), "Paragraph alignment changed.");
        return 1;
    case GID_I_BEFORE: case GID_I_AFTER:
        GT_GetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)&n, TAG_DONE);
        if (g->GadgetID == GID_I_BEFORE) pf.space_before = n * 20; else pf.space_after = n * 20;
        editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_SPACING), "Paragraph spacing changed.");
        return 1;
    case GID_I_SPACING:
        pf.line_spacing = code == 2 ? 200 : code == 1 ? 150 : 100;
        editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_SPACING), "Line spacing changed.");
        return 1;
    case GID_I_BOLD:
        editor_result(ow_editor_toggle_char_flags(editor, OWF_BOLD), "Bold.");
        return 1;
    case GID_I_ITALIC:
        editor_result(ow_editor_toggle_char_flags(editor, OWF_ITALIC), "Italic.");
        return 1;
    case GID_I_PAPER: case GID_I_MARGIN: {
        owf_page page;
        static const int papers[3][2] = { { 11906, 16838 }, { 12240, 15840 }, { 12240, 20160 } };
        ow_editor_page_setup(editor, &page);
        if (g->GadgetID == GID_I_PAPER && code < 3) { page.width = papers[code][0]; page.height = papers[code][1]; }
        if (g->GadgetID == GID_I_MARGIN) {
            GT_GetGadgetAttrs(g, win, NULL, GTIN_Number, (ULONG)&n, TAG_DONE);
            page.margin_left = page.margin_right = page.margin_top = page.margin_bottom = n * 14400 / 254;
        }
        editor_result(ow_editor_apply_page_setup(editor, &page), "Page changed.");
        pages_changed();
        return 1;
    }
    }
    return 0;
}





static void request_document_redraw(void)
{
    if (defer_repaint) repaint_pending = 1;
    else redraw_document_area();
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
    font_widths_reset();                             /* 2.0.3: the window's font is a face of last resort */
    pages_free();
    if (win) relayout();
}

/* The part of the screen a full-size window may have: below the title bar,
 * less the strip GFOpenDock takes along an edge (GFOpenFiles' free_area(), copied
 * here). GFOpenDock's window is the one whose screen title starts "GFOpenDock";
 * an ENV:GFOpenDock/Free of "left top width height" wins when the dock
 * publishes one. Without a dock it is the screen less its title bar. */
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
        if (dw->Width >= dw->Height) {              /* along the top or the bottom */
            if (dw->TopEdge + dw->Height / 2 > scr->Height / 2) {
                if (dw->TopEdge < bottom)
                    bottom = dw->TopEdge;
            } else if (dw->TopEdge + dw->Height > top)
                top = dw->TopEdge + dw->Height;
        } else {                                    /* down the left or the right */
            if (dw->LeftEdge + dw->Width / 2 > scr->Width / 2) {
                if (dw->LeftEdge < right)
                    right = dw->LeftEdge;
            } else if (dw->LeftEdge + dw->Width > left)
                left = dw->LeftEdge + dw->Width;
        }
    }
    UnlockIBase(lock);
    if (right - left < 400 || bottom - top < 200) { /* a dock that big: use the whole screen */
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

/* The first size (the user, 10 October 2026, as in GFOpenFiles 0.2.3): 800 x
 * 600, centred in the free area, and never bigger than it, so on a screen
 * smaller than 800 x 600 it is the free area itself. A size the user gives
 * the window is kept and given back by GFOpenWindows. */
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
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);

    start_box(&l, &t, &w, &h);     /* 800 x 600 in the free area, or the free area when smaller */

    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenWrite",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, l, WA_Top, t,
        WA_MinWidth, w < 520 ? w : 520, WA_MinHeight, h < 330 ? h : 330,
        WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE,
        WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                  IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW |
                  IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                  IDCMP_MOUSEMOVE | IDCMP_IDCMPUPDATE | IDCMP_EXTENDEDMOUSE,   /* 2.0.1: the wheel as OS 3.2 sends it */
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    relayout();
    zoom_choice = ZOOM_FIT_WIDTH;                    /* 2.0: the page as wide as the view to start */
    set_zoom(fit_zoom(1));
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
    if (rw) CloseWindow(rw);
    if (list) FreeGadgets(list);
    return mode;
}

static int spell_requester(const char *word, char suggestions[][48], int nsuggest,
                           char *replacement, size_t replacement_size)
{
    struct Gadget *list=NULL,*last,*gtext,*grep,*gignore,*gadd,*gdone;
    struct NewGadget ng; struct Window *rw=NULL; STRPTR v=NULL;
    int done=0, mode=0, ww=500, wh=118; char label[180];
    if(!scr||!vi||!word)return 0;
    if(nsuggest>0) snprintf(label,sizeof label,"%s  (suggestion: %.*s)",word,(int)sizeof suggestions[0]-1,suggestions[0]);
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
    if(!rw)goto out;
    GT_RefreshWindow(rw,NULL);ActivateGadget(gtext,rw,NULL);
    while(!done){struct IntuiMessage*m;Wait(1UL<<rw->UserPort->mp_SigBit);while((m=GT_GetIMsg(rw->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget*)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(rw);GT_EndRefresh(rw,TRUE);}else if(cls==IDCMP_GADGETUP&&id>=2&&id<=5){mode=(int)id-1;done=1;}}}
    if(mode==1){GT_GetGadgetAttrs(gtext,rw,NULL,GTST_String,(ULONG)&v,TAG_DONE);snprintf(replacement,replacement_size,"%s",v?(char*)v:"");if(!replacement[0])mode=2;}
out:
    if(rw)CloseWindow(rw);
    if(list)FreeGadgets(list);
    return mode;
}

static int ensure_spell(void)
{
    char msg[160];
    static const char *paths[]={"PROGDIR:Dictionaries/en_GB.words","PROGDIR:Dictionaries/en_US.words","SYS:Dictionaries/en_GB.words",NULL};
    int i;
    if(spell)return 1;
    for(i=0;paths[i];++i){spell=ow_spell_open(paths[i],"PROGDIR:Dictionaries/user.words",msg,sizeof msg);if(spell){set_status(msg);return 1;}}
    tell("Spell Check","No GFOpenWrite dictionary was found. Install Dictionaries/en_GB.words beside GFOpenWrite.");
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
    /* 2.0.18: a height (Word's exactly or at least) shows as 100 and stays unless the number is changed */
    ADD_INT(gline,8,"Line spacing (%)",204,OWF_LINE_PERCENT(fmt->line_spacing));
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
    if(!rw)goto out;
    GT_RefreshWindow(rw,NULL);
    while(!done){struct IntuiMessage*m;Wait(1UL<<rw->UserPort->mp_SigBit);while((m=GT_GetIMsg(rw->UserPort))){ULONG cls=m->Class;UWORD id=m->IAddress?((struct Gadget*)m->IAddress)->GadgetID:0;GT_ReplyIMsg(m);if(cls==IDCMP_CLOSEWINDOW){done=1;break;}if(cls==IDCMP_REFRESHWINDOW){GT_BeginRefresh(rw);GT_EndRefresh(rw,TRUE);}else if(cls==IDCMP_GADGETUP){if(id==9){ok=1;done=1;}else if(id==10)done=1;}}}
    if(ok){
        GT_GetGadgetAttrs(gstyle,rw,NULL,GTCY_Active,(ULONG)&v,TAG_DONE); fmt->heading=(int)v;
        GT_GetGadgetAttrs(galign,rw,NULL,GTCY_Active,(ULONG)&v,TAG_DONE); fmt->align=(owf_align)v;
        GT_GetGadgetAttrs(gbefore,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->space_before=(int)v*20;
        GT_GetGadgetAttrs(gafter,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->space_after=(int)v*20;
        GT_GetGadgetAttrs(gleft,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->indent_left=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gright,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->indent_right=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gfirst,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE); fmt->indent_first=mm_to_twips((int)v);
        GT_GetGadgetAttrs(gline,rw,NULL,GTIN_Number,(ULONG)&v,TAG_DONE);
        if (!OWF_LINE_IS_HEIGHT(fmt->line_spacing) || (int)v != OWF_LINE_PERCENT(fmt->line_spacing)) {
            fmt->line_spacing=(int)v;
            if (fmt->line_spacing < 50) fmt->line_spacing = 50;
            if (fmt->line_spacing > 400) fmt->line_spacing = 400;
        }
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
    if(!rw)goto out;
    GT_RefreshWindow(rw,NULL);
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
        (UBYTE *)"GFOpenWrite",
        (UBYTE *)"The document has unsaved changes.\n\nDiscard them?",
        (UBYTE *)"Discard|Cancel"
    };
    if (!editor || !ow_editor_is_dirty(editor)) return 1;
    if (rexx_quiet("The document has unsaved changes")) return 0;
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
    static char pdf_last[512];
    const char *name;
    owf_report *report;
    int rc;
    if (!doc) return;
    if (current_path[0]) {
        const char *src = leaf(current_path);
        const char *dot;
        strlcpy(initial, src, sizeof initial);  /* a file name, cut to fit */
        dot = strrchr(initial, '.');
        if (dot) initial[(size_t)(dot - initial)] = 0;
        if (strlen(initial) + 4 < sizeof initial) strcat(initial, ".pdf");
    }
    lastdir_get("GFOpenWrite-PDF", pdf_last, sizeof pdf_last);
    fr = AllocAslRequestTags(ASL_FileRequest,
        ASLFR_TitleText, (ULONG)"Export GFOpenWrite PDF",
        ASLFR_InitialDrawer, (ULONG)pdf_last,
        ASLFR_Window, (ULONG)win, ASLFR_SleepWindow, TRUE,
        ASLFR_DoSaveMode, TRUE, ASLFR_InitialFile, (ULONG)initial,
        ASLFR_PositiveText, (ULONG)"Export", TAG_DONE);
    if (!fr) { tell("Export PDF", "The file requester could not be opened."); return; }
    if (!AslRequestTags(fr, TAG_DONE) || !fr->fr_File[0]) { FreeAslRequest(fr); return; }
    snprintf(path, sizeof path, "%s", fr->fr_Drawer);
    lastdir_put("GFOpenWrite-PDF", (const char *)fr->fr_Drawer);
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
        snprintf(msg, sizeof msg, "GFOpenWrite could not export the PDF.\n\n%s", owf_error_text(rc));
        tell("Export PDF", msg);
    } else {
        snprintf(msg, sizeof msg, "%s exported as searchable PDF.", leaf(path));
        set_status(msg); request_document_redraw();
    }
    owf_report_free(report);
}

static void about(void)
{
    tell("GFOpenWrite",
         VERSION_TEXT "\n\n"
         "A lightweight native word processor for AmigaOS.\n\n"
         "GFOpenGadTools interface\n"
         "libowf DOCX/ODT/Amiga compatibility\n"
         "GFOpenRTG-ready page canvas\n"
         "GFOpenDatatypes and GFOpenPrint integration points\n\n"
         "MIT licence, Copyright (c) 2026 AmiCradle Team.");
}



static void follow_caret_page(void)
{
    int p = caret_page();
    if (p >= 0 && editor && p < page_total()) page_index = p;
}

/* After Find: the match's page, then scrolled so the match is in view (a
 * third of the way down), found among the spans the last drawing laid out. */




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
        tell("GFOpenWrite", "There is not enough memory to complete that edit.");
    else
        tell("GFOpenWrite", owf_error_text(rc));
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
        tell("Clipboard", "GFOpenWrite could not copy the selected text.");
        return 0;
    }
    if (!ow_clipboard_set_text(text, len)) {
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
    text = ow_clipboard_get_text(&len);
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
    struct FileRequester *fr;int ok=0;static char last[512];
    lastdir_get("GFOpenWrite-images",last,sizeof last);
    fr=AllocAslRequestTags(ASL_FileRequest,ASLFR_InitialDrawer,(ULONG)last,ASLFR_TitleText,(ULONG)"Insert Image",ASLFR_Window,(ULONG)win,ASLFR_SleepWindow,TRUE,ASLFR_DoSaveMode,FALSE,ASLFR_PositiveText,(ULONG)"Insert",TAG_DONE);
    if(!fr)return 0;
    if(AslRequestTags(fr,TAG_DONE)&&fr->fr_File[0]){snprintf(out,out_size,"%s",fr->fr_Drawer);lastdir_put("GFOpenWrite-images",(const char *)fr->fr_Drawer);AddPart((STRPTR)out,fr->fr_File,out_size);ok=1;}
    FreeAslRequest(fr);return ok;
}

static void do_image(int floating)
{
    char path[512],alt[160]="";BPTR f;LONG size,got;unsigned char *data;int width=4320,height=2880,idx,maxw;Object *o=NULL;struct BitMapHeader *bmh=NULL;
    if(!doc||!editor||!ask_image_file(path,sizeof path))return;
    f=Open((STRPTR)path,MODE_OLDFILE);if(!f){tell("Insert Image","The image file could not be opened.");return;}Seek(f,0,OFFSET_END);size=Seek(f,0,OFFSET_BEGINNING);if(size<=0||size>32*1024*1024){Close(f);tell("Insert Image","The image is empty or too large to embed.");return;}data=(unsigned char*)malloc((size_t)size);if(!data){Close(f);tell("Insert Image","There is not enough memory to embed the image.");return;}got=Read(f,data,size);Close(f);if(got!=size){free(data);tell("Insert Image","The image could not be read completely.");return;}
    if(DataTypesBase){o=NewDTObject((APTR)path,DTA_GroupID,GID_PICTURE,PDTA_DestMode,PMODE_V43,PDTA_Remap,FALSE,TAG_DONE);if(o){GetDTAttrs(o,PDTA_BitMapHeader,(ULONG)&bmh,TAG_DONE);if(bmh&&bmh->bmh_Width&&bmh->bmh_Height){width=(int)bmh->bmh_Width*15;height=(int)bmh->bmh_Height*15;}DisposeDTObject(o);}}
    maxw=doc->page.width-doc->page.margin_left-doc->page.margin_right;if(width>maxw&&width>0){height=(int)((long)height*maxw/width);width=maxw;}
    strlcpy(alt,leaf(path),sizeof alt);idx=owf_doc_add_image(doc,leaf(path),image_mime_name(path),data,(size_t)size,width,height,alt);free(data);if(idx<0){tell("Insert Image","There is not enough memory to add the image.");return;}
    image_cache_clear();
    if(editor_result(floating ? ow_editor_insert_floating_image(editor,idx,OWF_WRAP_SQUARE)
                              : ow_editor_insert_image(editor,idx),
                     floating ? "Floating image inserted; text wraps around it."
                              : "Image inserted through GFOpenDatatypes.")) {
        selected_frame = floating ? doc->nframes - 1 : -1;
        relayout();
    }
}

static void do_text_box(void)
{
    char text[512]="Text box",ws[16]="76",hs[16]="25";
    int width,height;
    if(!editor)return;
    if(!text_requester("Insert Text Box","Initial text",text,text,sizeof text))return;
    if(!text_requester("Insert Text Box","Width (mm)",ws,ws,sizeof ws))return;
    if(!text_requester("Insert Text Box","Height (mm)",hs,hs,sizeof hs))return;
    width=mm_to_twips(atoi(ws));height=mm_to_twips(atoi(hs));
    if(width<mm_to_twips(10)||height<mm_to_twips(10)||
       width>mm_to_twips(500)||height>mm_to_twips(500)){
        tell("Insert Text Box","Width and height must be between 10 and 500 mm.");return;
    }
    if(editor_result(ow_editor_insert_text_box(editor,text,width,height,OWF_WRAP_SQUARE),
                     "Text box inserted; type in it or click it again to edit."))relayout();
}

static int current_frame_index(void)
{
    ow_selection sel;
    if (!editor || !doc) return -1;
    sel = ow_editor_selection(editor);
    if (sel.focus.box > 0 && sel.focus.box <= doc->nframes) return sel.focus.box - 1;
    if (selected_frame >= 0 && selected_frame < doc->nframes) return selected_frame;
    return -1;
}

static void do_frame_properties(void)
{
    static const char *wrap_name[] = { "none", "square", "tight", "top-bottom" };
    char xs[24], ys[24], ws[24], hs[24], wrap[24];
    int frame_index = current_frame_index(), frame_page, x, y, width, height, rc;
    owf_frame_wrap mode;
    ol_rect at;
    ow_layout *l;
    if (frame_index < 0) {
        tell("Frame", "Select a floating image, or put the caret in a text box, first.");
        return;
    }
    l = pages();
    if (!l || !ow_layout_frame_rect(l, frame_index, &frame_page, &at)) {
        tell("Frame", "This frame is not currently placed on a page.");
        return;
    }
    (void)frame_page;
    snprintf(xs, sizeof xs, "%d", twips_to_mm((int)(at.x / OL_UNITS_PER_TWIP)));
    snprintf(ys, sizeof ys, "%d", twips_to_mm((int)(at.y / OL_UNITS_PER_TWIP)));
    snprintf(ws, sizeof ws, "%d", twips_to_mm((int)(at.width / OL_UNITS_PER_TWIP)));
    snprintf(hs, sizeof hs, "%d", twips_to_mm((int)(at.height / OL_UNITS_PER_TWIP)));
    mode = (owf_frame_wrap)doc->frames[frame_index].wrap;
    if (mode > OWF_WRAP_TOP_BOTTOM) mode = OWF_WRAP_NONE;
    strlcpy(wrap, wrap_name[mode], sizeof wrap);
    if (!text_requester("Frame", "X from page (mm)", xs, xs, sizeof xs) ||
        !text_requester("Frame", "Y from page (mm)", ys, ys, sizeof ys) ||
        !text_requester("Frame", "Width (mm)", ws, ws, sizeof ws) ||
        !text_requester("Frame", "Height (mm)", hs, hs, sizeof hs) ||
        !text_requester("Frame", "Wrap: none / square / tight / top-bottom", wrap, wrap, sizeof wrap))
        return;
    x = mm_to_twips(atoi(xs)); y = mm_to_twips(atoi(ys));
    width = mm_to_twips(atoi(ws)); height = mm_to_twips(atoi(hs));
    if (width < mm_to_twips(10) || height < mm_to_twips(10) ||
        width > mm_to_twips(500) || height > mm_to_twips(500)) {
        tell("Frame", "Width and height must be between 10 and 500 mm.");
        return;
    }
    if (!strcasecmp(wrap, "none")) mode = OWF_WRAP_NONE;
    else if (!strcasecmp(wrap, "square")) mode = OWF_WRAP_SQUARE;
    else if (!strcasecmp(wrap, "tight")) mode = OWF_WRAP_TIGHT;
    else if (!strcasecmp(wrap, "top-bottom") || !strcasecmp(wrap, "top bottom"))
        mode = OWF_WRAP_TOP_BOTTOM;
    else {
        tell("Frame", "Wrap must be none, square, tight, or top-bottom.");
        return;
    }
    rc = ow_editor_apply_frame_geometry(editor, frame_index, x, y, width, height, mode);
    if (rc == OWF_OK) {
        selected_frame = frame_index;
        pages_changed();
        editor_repaint("Frame position, size and wrapping updated.");
    } else if (rc == OWF_ERR_MEMORY) tell("Frame", "There is not enough memory to update the frame.");
    else tell("Frame", "The frame geometry is not valid.");
}

static int selected_frame_key(UWORD code, UWORD qualifier)
{
    ow_layout *l;
    ol_rect at;
    int page, x, y, width, height, step, dx = 0, dy = 0, rc;
    owf_frame_wrap wrap;
    if (!doc || !editor || selected_frame < 0 || selected_frame >= doc->nframes ||
        doc->frames[selected_frame].story.nparas != 0)
        return 0;
    if (code == 0x45) {                              /* Escape: return to text editing */
        selected_frame = -1;
        request_document_redraw();
        return 1;
    }
    if (code == 0x46) {                              /* Delete */
        rc = ow_editor_delete_frame(editor, selected_frame);
        if (rc == OWF_OK) {
            selected_frame = -1;
            pages_changed();
            editor_repaint("Frame deleted; Undo restores it.");
        } else if (rc == OWF_ERR_MEMORY) tell("Frame", "There is not enough memory to delete the frame.");
        return 1;
    }
    step = mm_to_twips((qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? 5 : 1);
    if (code == 0x4f) dx = -step;
    else if (code == 0x4e) dx = step;
    else if (code == 0x4c) dy = -step;
    else if (code == 0x4d) dy = step;
    else return 0;
    l = pages();
    if (!l || !ow_layout_frame_rect(l, selected_frame, &page, &at)) return 1;
    (void)page;
    x = (int)(at.x / OL_UNITS_PER_TWIP);
    y = (int)(at.y / OL_UNITS_PER_TWIP);
    width = (int)(at.width / OL_UNITS_PER_TWIP);
    height = (int)(at.height / OL_UNITS_PER_TWIP);
    ow_frame_adjust_rect(&x, &y, &width, &height, OW_FRAME_MOVE, dx, dy, 1, 1);
    wrap = (owf_frame_wrap)doc->frames[selected_frame].wrap;
    rc = ow_editor_apply_frame_geometry(editor, selected_frame, x, y, width, height, wrap);
    if (rc == OWF_OK) {
        pages_changed();
        editor_repaint((qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
                       ? "Frame moved 5 mm." : "Frame moved 1 mm.");
    } else if (rc == OWF_ERR_MEMORY) tell("Frame", "There is not enough memory to move the frame.");
    return 1;
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

static int parse_style_colour(const char *text, unsigned long *shade)
{
    char *end;
    unsigned long rgb;
    if (!text || !shade) return 0;
    while (*text && isspace((unsigned char)*text)) ++text;
    if (!strcasecmp(text, "none") || !strcasecmp(text, "automatic")) {
        *shade = 0;
        return 1;
    }
    if (*text == '#') ++text;
    if (strlen(text) != 6) return 0;
    rgb = strtoul(text, &end, 16);
    if (*end) return 0;
    *shade = OWF_SHADE(rgb);
    return 1;
}

static void do_table_style(void)
{
    char scope_text[24] = "cell", fill_text[24] = "none", border_text[24] = "#000000";
    ow_table_scope scope;
    unsigned long fill, border;
    int borders, rc;
    if (!editor || !ow_editor_in_table(editor)) {
        tell("Cell Style", "Put the caret in a table cell first.");
        return;
    }
    if (!text_requester("Cell Style", "Apply to: cell / row / table",
                        scope_text, scope_text, sizeof scope_text) ||
        !text_requester("Cell Style", "Fill: #RRGGBB or none",
                        fill_text, fill_text, sizeof fill_text) ||
        !text_requester("Cell Style", "Border: #RRGGBB or none",
                        border_text, border_text, sizeof border_text))
        return;
    if (!strcasecmp(scope_text, "cell")) scope = OW_TABLE_CELL;
    else if (!strcasecmp(scope_text, "row")) scope = OW_TABLE_ROW;
    else if (!strcasecmp(scope_text, "table") || !strcasecmp(scope_text, "all")) scope = OW_TABLE_ALL;
    else { tell("Cell Style", "Scope must be cell, row, or table."); return; }
    if (!parse_style_colour(fill_text, &fill) || !parse_style_colour(border_text, &border)) {
        tell("Cell Style", "Colours must be #RRGGBB or none.");
        return;
    }
    borders = border ? OWF_BORDER_TOP | OWF_BORDER_LEFT | OWF_BORDER_BOTTOM | OWF_BORDER_RIGHT : 0;
    rc = ow_editor_table_apply_style(editor, scope, fill, borders, border);
    if (rc == OWF_OK) {
        pages_changed();
        editor_repaint("Table cell style updated.");
    } else if (rc == OWF_ERR_MEMORY) tell("Cell Style", "There is not enough memory to style the table.");
    else tell("Cell Style", "The table style could not be applied.");
}

static void do_document_columns(void)
{
    char columns_text[16], gap_text[24];
    char *end;
    long gap_mm;
    int columns = 1, gap = 0, i;
    if (!editor || !doc || doc->body.nparas < 1) return;
    if (doc->body.paras[0].section_columns > 0) {
        columns = doc->body.paras[0].section_columns;
        gap = doc->body.paras[0].section_gap;
    }
    snprintf(columns_text, sizeof columns_text, "%d", columns);
    snprintf(gap_text, sizeof gap_text, "%ld", ((long)gap * 254 + 7200) / 14400);
    if (!text_requester("Document Columns", "Columns (1-16)",
                        columns_text, columns_text, sizeof columns_text) ||
        !text_requester("Document Columns", "Gap in millimetres (0-100)",
                        gap_text, gap_text, sizeof gap_text))
        return;
    columns = (int)strtol(columns_text, &end, 10);
    if (*end || columns < 1 || columns > 16) {
        tell("Document Columns", "Columns must be a whole number from 1 to 16.");
        return;
    }
    gap_mm = strtol(gap_text, &end, 10);
    if (*end || gap_mm < 0 || gap_mm > 100) {
        tell("Document Columns", "The gap must be from 0 to 100 millimetres.");
        return;
    }
    for (i = 1; i < doc->body.nparas; ++i)
        if (doc->body.paras[i].section_columns) {
            struct EasyStruct es = {
                sizeof(struct EasyStruct), 0, (UBYTE *)"Document Columns",
                (UBYTE *)"This document has later section column settings.\n"
                         "Applying document columns replaces those settings.\n\nContinue?",
                (UBYTE *)"Apply|Cancel"
            };
            if (!EasyRequest(win, &es, NULL)) return;
            break;
        }
    gap = (int)((gap_mm * 14400 + 127) / 254);
    editor_result(ow_editor_apply_document_columns(editor, columns, gap),
                  columns == 1 ? "Document set to one column." : "Document columns updated.");
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

/* 2.0.23: the left pages' (even-numbered) own too; empty, left pages have the header or footer */
static void do_story_edit(ow_story_kind which)
{
    static const char *const title[5] = { "", "Header", "Footer", "Left-Page Header (empty: the header)",
                                          "Left-Page Footer (empty: the footer)" };
    static const char *const done[5] = { "", "Header updated.", "Footer updated.", "Left-page header updated.",
                                         "Left-page footer updated." };
    char *current = ow_editor_story_text(editor, which);
    char value[512], before[512];
    int i;
    if (!current) current = (char *)calloc(1, 1);
    snprintf(value, sizeof value, "%s", current ? current : "");
    free(current);
    for (i = 0; value[i]; ++i) if (value[i] == '\n' || value[i] == '\r') value[i] = ' ';
    memcpy(before, value, sizeof before);
    if (!text_requester(title[which], "Text / {PAGE} {PAGES} {DATE} {TIME}", value, value, sizeof value)) return;
    if (!strcmp(before, value)) return;          /* unchanged: a left page's "nothing" stays so */
    if (editor_result(ow_editor_set_story_text(editor, which, value), done[which])) relayout();
}

static void do_command(int id)
{
    switch (id) {
    case C_NEW: new_document(); break;
    case C_OPEN: do_open(); break;
    case C_SAVE: do_save(); break;
    case C_PRINT: {
        char msg[220];
        if (ow_print_document_named(doc, current_path[0] ? leaf(current_path) : NULL, msg, sizeof msg)) {
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
    case C_IMAGE: do_image(0); break;
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
        set_status(backwards ? "Previous match." : "Match found.");
        bring_focus_into_view();
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

/* ---- 2.0.27: tracked changes ---- */

/* The name changes are made under: the ENV USER (or USERNAME) variable until Review Author... names one. */
static void review_author_init(void)
{
    char buf[64];
    if (review_author_set) return;
    review_author_set = 1;
    if (GetVar((STRPTR)"USER", (STRPTR)buf, sizeof buf, 0) > 0 ||
        GetVar((STRPTR)"USERNAME", (STRPTR)buf, sizeof buf, 0) > 0)
        snprintf(review_author, sizeof review_author, "%s", buf);
}

/* Record Changes' tick follows the document's setting (a file can arrive recording) */
static void sync_track_menu(void)
{
    struct Menu *m;
    struct MenuItem *it, *sub;
    int on = editor && ow_editor_track_changes(editor);
    if (!menu) return;
    if (win) ClearMenuStrip(win);
    for (m = menu; m; m = m->NextMenu)
        for (it = m->FirstItem; it; it = it->NextItem)
            for (sub = it->SubItem; sub; sub = sub->NextItem)
                if ((ULONG)GTMENUITEM_USERDATA(sub) == M_TRACK) {
                    if (on) sub->Flags |= CHECKED; else sub->Flags &= ~CHECKED;
                }
    if (win) ResetMenuStrip(win, menu);
}

static void change_text(const ow_change_info *info, int count, char *out, size_t size)
{
    snprintf(out, size, "%s by %s%s%s (%d of %d)", info->kind == OWF_REV_DELETE ? "Deletion" : "Insertion",
             info->author && *info->author ? info->author : "unknown author", info->date && *info->date ? ", " : "",
             info->date ? info->date : "", info->index + 1, count);
}

static void do_next_change(int backwards)
{
    ow_change_info info;
    char msg[160];
    int rc;
    if (!editor) return;
    rc = ow_editor_next_change(editor, backwards);
    if (rc <= 0) { set_status("No changes."); request_document_redraw(); DisplayBeep(scr); return; }
    if (ow_editor_current_change(editor, &info)) {
        change_text(&info, ow_editor_change_count(editor), msg, sizeof msg);
        if (rc == 2) strncat(msg, backwards ? "; from the end" : "; from the start", sizeof msg - strlen(msg) - 1);
        set_status(msg);
    }
    bring_focus_into_view();
}

static void do_accept_change(int accept, int all)
{
    int rc;
    if (!editor) return;
    rc = all ? ow_editor_accept_all_changes(editor, accept) : ow_editor_accept_change(editor, accept);
    if (rc == OWF_ERR_UNSUPPORTED) {
        set_status(all ? "The document has no changes." : "No change at the caret.");
        request_document_redraw();
        DisplayBeep(scr);
        return;
    }
    editor_result(rc, all ? (accept ? "Every change accepted." : "Every change rejected.")
                          : (accept ? "Change accepted." : "Change rejected."));
}

static void do_review_author(void)
{
    char name[64];
    snprintf(name, sizeof name, "%s", review_author);
    if (!text_requester("Review Author", "Name", review_author, name, sizeof name)) return;
    snprintf(review_author, sizeof review_author, "%s", name);
    review_author_set = 1;
    if (editor) ow_editor_set_review_author(editor, review_author);
    set_status(review_author[0] ? "Changes are now made under that name." : "Changes now have no author.");
    request_document_redraw();
}

/* ---- 2.0.29: fields with a value ---- */

/* Pages of the bookmarked body paragraphs, from this layout, for page references (made before an update, as
 * the update changes the text the layout points into) */
static int *field_pages;
static int field_npages;

static int field_page_of(void *userdata, int paragraph)
{
    (void)userdata;
    return paragraph >= 0 && paragraph < field_npages && field_pages[paragraph] >= 0 ? field_pages[paragraph] : 0;
}

static void fields_update(void)
{
    ow_layout *l;
    owf_doc *d;
    int i;
    if (!editor || !(d = ow_editor_document(editor))) return;
    l = pages();
    field_npages = d->body.nparas;
    field_pages = field_npages ? (int *)malloc((size_t)field_npages * sizeof(int)) : NULL;
    for (i = 0; field_pages && i < field_npages; ++i) {
        ow_position pos;
        ol_rect box;
        int page = -1;
        field_pages[i] = -1;
        if (!l || !d->body.paras[i].bookmarks) continue;
        memset(&pos, 0, sizeof pos);
        pos.paragraph = i;
        if (ow_layout_caret(l, pos, &page, &box)) field_pages[i] = page;
    }
    if (field_pages) ow_editor_set_page_lookup(editor, field_page_of, NULL);
    if (ow_editor_update_fields(editor) > 0) pages_changed();
    ow_editor_set_page_lookup(editor, NULL, NULL);
    free(field_pages);
    field_pages = NULL;
    field_npages = 0;
}

/* UTF-8 from what a requester gives (Latin-1) */
static char *latin1_utf8(const char *latin1)
{
    size_t n = strlen(latin1), i, o = 0;
    char *out = (char *)malloc(2 * n + 1);
    if (!out) return NULL;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)latin1[i];
        if (c < 0x80) out[o++] = (char)c;
        else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    return out;
}

static int set_info(ow_doc_info which, const char *latin1)
{
    char *u = latin1_utf8(latin1);
    int rc = u ? ow_editor_set_doc_info(editor, which, u) : OWF_ERR_MEMORY;
    free(u);
    return rc;
}

/* Title, author and subject, one requester each (an empty answer leaves it as it is) */
static void do_doc_info(void)
{
    static const char *const labels[] = { "Title", "Author", "Subject" };
    char text[200];
    int k, changed = 0;
    if (!editor) return;
    for (k = 0; k < 3; ++k) {
        const char *now = ow_editor_doc_info(editor, (ow_doc_info)k);
        to_latin1(now ? now : "", now ? strlen(now) : 0, text, sizeof text);
        if (!text_requester("Document Info", labels[k], text, text, sizeof text)) continue;
        if (set_info((ow_doc_info)k, text) == OWF_OK) ++changed;
    }
    if (changed) { fields_update(); update_window_title(); set_status("Document information changed."); }
    request_document_redraw();
}

/* A user field: insert one (named, made when new), or (insert 0) set one's value or remove it ("-") */
static void do_user_field(int insert)
{
    char name[64], value[200];
    const char *first = NULL, *now = NULL;
    char *un, *uv;
    int i, rc = OWF_OK;
    if (!editor) return;
    ow_editor_user_field_at(editor, 0, &first, NULL);
    to_latin1(first ? first : "", first ? strlen(first) : 0, name, sizeof name);
    if (!text_requester(insert ? "Insert User Field" : "User Fields", "Name", name, name, sizeof name)) return;
    if (!(un = latin1_utf8(name))) return;
    for (i = 0; i < ow_editor_user_field_count(editor); ++i) {
        const char *n, *v;
        if (ow_editor_user_field_at(editor, i, &n, &v) && !strcmp(n, un)) now = v;
    }
    if (!insert || !now) {
        to_latin1(now ? now : "", now ? strlen(now) : 0, value, sizeof value);
        if (!text_requester(insert ? "Insert User Field" : "User Fields", insert ? "Value" : "Value (-: remove)",
                            value, value, sizeof value)) { free(un); return; }
        if (!insert && !strcmp(value, "-")) {
            rc = ow_editor_set_user_field(editor, un, NULL);
        } else if ((uv = latin1_utf8(value)) != NULL) {
            rc = ow_editor_set_user_field(editor, un, uv);
            free(uv);
        } else rc = OWF_ERR_MEMORY;
    }
    if (rc == OWF_OK && insert) rc = ow_editor_insert_field_arg(editor, OWF_FIELD_USER, un);
    free(un);
    if (rc == OWF_ERR_UNSUPPORTED) { set_status("No user field of that name."); DisplayBeep(scr); }
    else if (editor_result(rc, insert ? "User field inserted." : "User field changed.")) fields_update();
    request_document_redraw();
}

/* A heading to refer to, from a list, and whether to show its text or its page: its paragraph, -1: cancelled */
static int heading_requester(int *page)
{
    struct Gadget *glist = NULL, *last, *glv, *gpage, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    struct List items;
    struct Node *nodes = NULL;
    char (*labels)[72] = NULL;
    int *paras = NULL, n = 0, i, k, chosen = -1, picked = 0, done = 0, ww = 420, wh = 200;
    owf_doc *d = editor ? ow_editor_document(editor) : NULL;
    if (!scr || !vi || !d) return -1;
    for (i = 0; i < d->body.nparas; ++i) if (d->body.paras[i].fmt.heading > 0) ++n;
    if (!n) return -2;
    nodes = (struct Node *)calloc((size_t)n, sizeof *nodes);
    labels = (char (*)[72])calloc((size_t)n, sizeof *labels);
    paras = (int *)calloc((size_t)n, sizeof *paras);
    if (!nodes || !labels || !paras) goto out;
    NewList(&items);
    for (i = 0, k = 0; i < d->body.nparas && k < n; ++i) {
        const owf_para *p = &d->body.paras[i];
        size_t o;
        int j;
        if (p->fmt.heading <= 0) continue;
        o = (size_t)(p->fmt.heading > 1 ? 2 * (p->fmt.heading - 1) : 0);
        memset(labels[k], ' ', o);
        for (j = 0; j < p->nruns && o + 1 < sizeof labels[k]; ++j)
            if (p->runs[j].kind == OWF_RUN_TEXT && p->runs[j].text)
                o += to_latin1(p->runs[j].text, strlen(p->runs[j].text), labels[k] + o, sizeof labels[k] - o);
        labels[k][o < sizeof labels[k] ? o : sizeof labels[k] - 1] = 0;
        nodes[k].ln_Name = labels[k];
        AddTail(&items, &nodes[k]);
        paras[k++] = i;
    }
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr; ng.ng_VisualInfo = vi;
    last = CreateContext(&glist);
    ng.ng_LeftEdge = 12; ng.ng_TopEdge = 20; ng.ng_Width = 396; ng.ng_Height = 120;
    ng.ng_GadgetText = (STRPTR)"Refer to the heading"; ng.ng_GadgetID = 1; ng.ng_Flags = PLACETEXT_ABOVE;
    last = glv = CreateGadget(LISTVIEW_KIND, last, &ng, GTLV_Labels, (ULONG)&items, GTLV_ShowSelected, 0,
                              GTLV_Selected, 0, TAG_DONE);
    ng.ng_TopEdge = 146; ng.ng_Width = 26; ng.ng_Height = 14; ng.ng_LeftEdge = 12;
    ng.ng_GadgetText = (STRPTR)"Its page number, not its text"; ng.ng_GadgetID = 2; ng.ng_Flags = PLACETEXT_RIGHT;
    last = gpage = CreateGadget(CHECKBOX_KIND, last, &ng, GTCB_Checked, *page ? TRUE : FALSE, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_GadgetText = (STRPTR)"Insert"; ng.ng_GadgetID = 3;
    ng.ng_LeftEdge = 242; ng.ng_TopEdge = 170; ng.ng_Width = 78; ng.ng_Height = 20;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 4; ng.ng_LeftEdge = 330;
    last = gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!glv || !gpage || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Cross-reference", WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh,
        WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | LISTVIEWIDCMP | CHECKBOXIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        Wait(1UL << rw->UserPort->mp_SigBit);
        while ((m = GT_GetIMsg(rw->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD id = m->IAddress && (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN)
                     ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { done = 1; break; }
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                if (id == 1) picked = code;
                else if (id == 2) *page = !*page;
                else if (id == 3) { chosen = picked >= 0 && picked < n ? paras[picked] : -1; done = 1; }
                else if (id == 4) done = 1;
            }
        }
    }
out:
    if (rw) CloseWindow(rw);
    if (glist) FreeGadgets(glist);
    free(nodes);
    free(labels);
    free(paras);
    return chosen;
}

/* 2.0.32: an index entry at the caret (the selection's words by default; "Main:Sub" for a subentry), and the
 * alphabetical index from every entry, made like the table of contents */
static void do_index(int what)
{
    if (!editor) return;
    if (what == M_INDEX_ENTRY) {
        char entry[160], *u;
        size_t len = 0;
        int rc;
        char *sel = ow_editor_selection_empty(editor) ? NULL : ow_editor_selection_text(editor, &len);
        entry[0] = 0;
        if (sel) {
            size_t o = to_latin1(sel, len, entry, sizeof entry);
            entry[o < sizeof entry ? o : sizeof entry - 1] = 0;
            free(sel);
        }
        if (!text_requester("Index Entry", "Entry (Main:Sub)", entry, entry, sizeof entry) || !entry[0]) return;
        if (!(u = latin1_utf8(entry))) { tell("Index Entry", "There is not enough memory."); return; }
        rc = ow_editor_insert_index_entry(editor, u);
        free(u);
        if (rc == OWF_ERR_FORMAT) { tell("Index Entry", "An entry needs a word before any ':'."); return; }
        if (editor_result(rc, "Index entry marked; Insert > Alphabetical Index lists it.")) pages_changed();
        return;
    } else {
        int rc = ow_editor_insert_index(editor, resolve_toc_pages, NULL);
        if (rc == OWF_OK) {
            pages_changed();
            editor_repaint("Alphabetical index inserted; Undo removes it.");
        } else if (rc == OWF_ERR_UNSUPPORTED) {
            tell("Alphabetical Index", "This document has no index entries (Insert > Index Entry...).");
        } else if (rc == OWF_ERR_MEMORY) {
            tell("Alphabetical Index", "There is not enough memory to build the index.");
        } else {
            tell("Alphabetical Index", "Place the caret in an ordinary body paragraph, outside a table or text box.");
        }
    }
}

static void do_cross_reference(void)
{
    static int page_form;
    int para;
    if (!editor) return;
    para = heading_requester(&page_form);
    if (para == -2) { set_status("The document has no headings to refer to."); DisplayBeep(scr); return; }
    if (para < 0) return;
    if (editor_result(ow_editor_insert_cross_reference(editor, para, page_form), "Cross-reference inserted."))
        fields_update();
    request_document_redraw();
}

/* ---- 2.0.28: comments ---- */

/* Latin-1 (what the requester and ARexx give) as UTF-8, a typed "\n" as a paragraph break. NULL: no memory. */
static char *comment_utf8(const char *latin1)
{
    size_t n = strlen(latin1), i, o = 0;
    char *out = (char *)malloc(2 * n + 1);
    if (!out) return NULL;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)latin1[i];
        if (c == '\\' && latin1[i + 1] == 'n') { out[o++] = '\n'; ++i; }
        else if (c < 0x80) out[o++] = (char)c;
        else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    return out;
}

/* A comment's text for the requester: Latin-1, its paragraph breaks as "\n" */
static void comment_latin1(const char *utf8, char *out, size_t size)
{
    size_t o = 0;
    const char *p = utf8 ? utf8 : "";
    if (!size) return;
    while (*p && o + 3 < size) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        o += to_latin1(p, len, out + o, size - o);
        if (!nl) break;
        if (o + 3 < size) { out[o++] = '\\'; out[o++] = 'n'; }
        p = nl + 1;
    }
    out[o] = 0;
}

static void comment_text(const ow_comment_info *info, int count, char *out, size_t size)
{
    char who[64], words[160];
    const char *name = info->author && *info->author ? info->author : "unknown author";
    to_latin1(name, strlen(name), who, sizeof who);
    comment_latin1(info->text, words, sizeof words);
    snprintf(out, size, "%s%s by %s%s%.10s: %s (%d of %d)", info->reply_to >= 0 ? "Reply" : "Comment",
             info->done ? " (done)" : "", who, info->date && *info->date ? ", " : "", info->date ? info->date : "",
             words, info->index + 1, count);
}

/* The text asked for, then op on it: 1 done, 0 cancelled or failed (the status says) */
static int comment_with_text(const char *title, const char *initial, int (*op)(ow_editor *, const char *),
                             const char *done)
{
    char text[400];
    char *utf8;
    int rc;
    comment_latin1(initial, text, sizeof text);
    if (!text_requester(title, "Text (\\n: new paragraph)", text, text, sizeof text)) return 0;
    if (!(utf8 = comment_utf8(text))) { set_status("Out of memory."); return 0; }
    rc = op(editor, utf8);
    free(utf8);
    return editor_result(rc, done);
}

static int no_comment_here(int rc)
{
    if (rc != OWF_ERR_UNSUPPORTED) return 0;
    set_status("No comment at the caret.");
    request_document_redraw();
    DisplayBeep(scr);
    return 1;
}

static void do_comment(int what)
{
    ow_comment_info info;
    char msg[240];
    int rc, have;
    if (!editor) return;
    review_author_init();
    ow_editor_set_review_author(editor, review_author);
    have = ow_editor_current_comment(editor, &info);
    switch (what) {
    case M_COMMENT:
        comment_with_text("Insert Comment", "", ow_editor_insert_comment, "Comment added.");
        break;
    case M_COMMENT_NEXT: case M_COMMENT_PREV:
        rc = ow_editor_next_comment(editor, what == M_COMMENT_PREV);
        if (rc <= 0) { set_status("No comments."); request_document_redraw(); DisplayBeep(scr); return; }
        if (ow_editor_current_comment(editor, &info)) {
            comment_text(&info, ow_editor_comment_count(editor), msg, sizeof msg);
            if (rc == 2) strncat(msg, what == M_COMMENT_PREV ? "; from the end" : "; from the start",
                                 sizeof msg - strlen(msg) - 1);
            set_status(msg);
        }
        bring_focus_into_view();
        break;
    case M_COMMENT_REPLY:
        if (!have) { no_comment_here(OWF_ERR_UNSUPPORTED); return; }
        comment_with_text("Reply", "", ow_editor_reply_comment, "Reply added.");
        break;
    case M_COMMENT_EDIT:
        if (!have) { no_comment_here(OWF_ERR_UNSUPPORTED); return; }
        comment_with_text("Edit Comment", info.text, ow_editor_set_comment_text, "Comment changed.");
        break;
    case M_COMMENT_DONE:
        rc = ow_editor_resolve_comment(editor, have ? !info.done : 1);
        if (!no_comment_here(rc)) editor_result(rc, have && info.done ? "Comment open again." : "Comment marked done.");
        break;
    case M_COMMENT_DELETE:
        rc = ow_editor_delete_comment(editor);
        if (!no_comment_here(rc)) editor_result(rc, "Comment deleted.");
        break;
    }
}

/* ---- 2.0.30: footnotes and endnotes ---- */

static int insert_footnote(ow_editor *ed, const char *utf8) { return ow_editor_insert_note(ed, 0, utf8); }
static int insert_endnote(ow_editor *ed, const char *utf8) { return ow_editor_insert_note(ed, 1, utf8); }

static void note_text(const ow_note_info *info, int count, char *out, size_t size)
{
    char words[160];
    comment_latin1(info->text, words, sizeof words);
    snprintf(out, size, "%s %s: %s (%d of %d)", info->endnote ? "Endnote" : "Footnote", info->mark, words,
             info->index + 1, count);
}

static int no_note_here(int rc)
{
    if (rc != OWF_ERR_UNSUPPORTED) return 0;
    set_status("No note at the caret.");
    request_document_redraw();
    DisplayBeep(scr);
    return 1;
}

static void do_note(int what)
{
    ow_note_info info;
    char msg[240];
    int rc, have;
    if (!editor) return;
    have = ow_editor_current_note(editor, &info);
    switch (what) {
    case M_FOOTNOTE:
        comment_with_text("Insert Footnote", "", insert_footnote, "Footnote added.");
        break;
    case M_ENDNOTE:
        comment_with_text("Insert Endnote", "", insert_endnote, "Endnote added.");
        break;
    case M_NOTE_NEXT: case M_NOTE_PREV:
        rc = ow_editor_next_note(editor, what == M_NOTE_PREV);
        if (rc <= 0) { set_status("No notes."); request_document_redraw(); DisplayBeep(scr); return; }
        if (ow_editor_current_note(editor, &info)) {
            note_text(&info, ow_editor_note_count(editor), msg, sizeof msg);
            if (rc == 2) strncat(msg, what == M_NOTE_PREV ? "; from the end" : "; from the start",
                                 sizeof msg - strlen(msg) - 1);
            set_status(msg);
        }
        bring_focus_into_view();
        break;
    case M_NOTE_EDIT:
        if (!have) { no_note_here(OWF_ERR_UNSUPPORTED); return; }
        comment_with_text(info.endnote ? "Edit Endnote" : "Edit Footnote", info.text, ow_editor_set_note_text,
                          "Note changed.");
        break;
    case M_NOTE_CONVERT:
        rc = ow_editor_convert_note(editor);
        if (!no_note_here(rc)) editor_result(rc, have && info.endnote ? "Made a footnote." : "Made an endnote.");
        break;
    case M_NOTE_DELETE:
        rc = ow_editor_delete_note(editor);
        if (!no_note_here(rc)) editor_result(rc, "Note deleted.");
        break;
    }
}

/* ---- 2.0.31: mail merge (core 1.6) ---- */

/* One of n names (Latin-1) from a list: its index, -1: cancelled */
static int pick_requester(const char *title, const char *label, char (*names)[72], int n)
{
    struct Gadget *glist = NULL, *last, *glv, *gok, *gcancel;
    struct NewGadget ng;
    struct Window *rw = NULL;
    struct List items;
    struct Node *nodes;
    int i, chosen = -1, picked = 0, done = 0, ww = 320, wh = 190;
    if (!scr || !vi || n <= 0) return -1;
    if (!(nodes = (struct Node *)calloc((size_t)n, sizeof *nodes))) return -1;
    NewList(&items);
    for (i = 0; i < n; ++i) { nodes[i].ln_Name = names[i]; AddTail(&items, &nodes[i]); }
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &font_attr; ng.ng_VisualInfo = vi;
    last = CreateContext(&glist);
    ng.ng_LeftEdge = 12; ng.ng_TopEdge = 20; ng.ng_Width = 296; ng.ng_Height = 130;
    ng.ng_GadgetText = (STRPTR)label; ng.ng_GadgetID = 1; ng.ng_Flags = PLACETEXT_ABOVE;
    last = glv = CreateGadget(LISTVIEW_KIND, last, &ng, GTLV_Labels, (ULONG)&items, GTLV_ShowSelected, 0,
                              GTLV_Selected, 0, TAG_DONE);
    ng.ng_Flags = 0; ng.ng_GadgetText = (STRPTR)"Insert"; ng.ng_GadgetID = 2;
    ng.ng_LeftEdge = 142; ng.ng_TopEdge = 160; ng.ng_Width = 78; ng.ng_Height = 20;
    last = gok = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = 3; ng.ng_LeftEdge = 230;
    last = gcancel = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!glv || !gok || !gcancel) goto out;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh,
        WA_Left, (scr->Width - ww) / 2, WA_Top, (scr->Height - wh) / 2,
        WA_Gadgets, (ULONG)glist, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | LISTVIEWIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!rw) goto out;
    GT_RefreshWindow(rw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        Wait(1UL << rw->UserPort->mp_SigBit);
        while ((m = GT_GetIMsg(rw->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            UWORD id = m->IAddress && (cls == IDCMP_GADGETUP || cls == IDCMP_GADGETDOWN)
                     ? ((struct Gadget *)m->IAddress)->GadgetID : 0;
            GT_ReplyIMsg(m);
            if (cls == IDCMP_CLOSEWINDOW) { done = 1; break; }
            if (cls == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); }
            else if (cls == IDCMP_GADGETUP) {
                if (id == 1) picked = code;
                else if (id == 2) { chosen = picked >= 0 && picked < n ? picked : -1; done = 1; }
                else if (id == 3) done = 1;
            }
        }
    }
out:
    if (rw) CloseWindow(rw);
    if (glist) FreeGadgets(glist);
    free(nodes);
    return chosen;
}

/* Where the fields are: "Record 2 of 3" or "Field names" */
static void merge_status(const char *before)
{
    char msg[200];                                   /* set_status keeps 256 */
    int at = ow_editor_merge_shown(editor);
    if (at >= 0) snprintf(msg, sizeof msg, "%.150sRecord %d of %d.", before, at + 1, ow_editor_merge_records(editor));
    else snprintf(msg, sizeof msg, "%.150sField names shown.", before);
    set_status(msg);
}

static const char *merge_error(int rc)
{
    return rc == OWF_ERR_IO ? "The data source could not be read." :
           rc == OWF_ERR_FORMAT ? "The data source has no columns: its first line names them." :
           rc == OWF_ERR_UNSUPPORTED ? "The data source has no records." : owf_error_text(rc);
}

/* Merge records into one document, or a file each (files), at path: how many records, or -(an OWF_ERR_) */
static int merge_out(const char *path, int files)
{
    owf_doc *all = NULL;
    int rc, n = 0, records = ow_editor_merge_records(editor);
    if (!records) return -OWF_ERR_UNSUPPORTED;
    fields_update();                                 /* the other fields as they are now */
    if (files) {
        rc = ow_editor_merge_to_files(editor, path, 0, -1, &n);
    } else {
        rc = ow_editor_merge_document(editor, 0, -1, &all);
        if (rc == OWF_OK) {
            owf_report *report = owf_report_new();
            rc = owf_export_file(all, path, NULL, report);
            owf_report_free(report);
            owf_doc_free(all);
            n = records;
        }
    }
    pages_changed();
    return rc == OWF_OK ? n : -rc;
}

static void do_merge(int what)
{
    char path[512], msg[240];
    int rc, n, at;
    if (!editor) return;
    switch (what) {
    case M_MERGE_SOURCE:
        if (!ask_file_for(0, "Mail merge data source (CSV)", path, sizeof path)) return;
        rc = ow_editor_merge_load(editor, path);
        if (rc != OWF_OK) { tell("Mail Merge", merge_error(rc)); return; }
        snprintf(msg, sizeof msg, "%.100s: %d columns, %d records. ", leaf(path), ow_editor_merge_columns(editor),
                 ow_editor_merge_records(editor));
        pages_changed();
        merge_status(msg);
        break;
    case M_MERGE_FIELD: {
        char (*names)[72];
        int k, cols = ow_editor_merge_columns(editor);
        if (!cols) { tell("Mail Merge", "Choose a data source first (Tools > Mail Merge > Data Source...)."); return; }
        if (!(names = (char (*)[72])calloc((size_t)cols, sizeof *names))) return;
        for (k = 0; k < cols; ++k) {
            const char *c = ow_editor_merge_column(editor, k);
            size_t o = to_latin1(c, strlen(c), names[k], sizeof names[k]);
            names[k][o < sizeof names[k] ? o : sizeof names[k] - 1] = 0;
        }
        k = pick_requester("Insert Merge Field", "Column", names, cols);
        free(names);
        if (k < 0) return;
        if (editor_result(ow_editor_insert_merge_field(editor, ow_editor_merge_column(editor, k)), "Merge field inserted."))
            fields_update();
        break;
    }
    case M_MERGE_NEXT: case M_MERGE_PREV: case M_MERGE_NAMES:
        n = ow_editor_merge_records(editor);
        if (!n && what != M_MERGE_NAMES) { tell("Mail Merge", "The data source has no records, or there is none."); return; }
        at = ow_editor_merge_shown(editor);
        at = what == M_MERGE_NAMES ? -1 : what == M_MERGE_NEXT ? (at + 1 < n ? at + 1 : n - 1) : (at > 0 ? at - 1 : 0);
        rc = ow_editor_merge_show(editor, at);
        if (rc != OWF_OK) { editor_result(rc, NULL); return; }
        pages_changed();
        merge_status("");
        break;
    case M_MERGE_DOC: case M_MERGE_FILES:
        if (!ow_editor_merge_records(editor)) { tell("Mail Merge", "The data source has no records, or there is none."); return; }
        if (!ask_file_for(1, what == M_MERGE_DOC ? "Merge into one document" : "Merge into files (numbered)",
                          path, sizeof path)) return;
        n = merge_out(path, what == M_MERGE_FILES);
        if (n < 0) { tell("Mail Merge", owf_error_text(-n)); return; }
        snprintf(msg, sizeof msg, what == M_MERGE_DOC ? "%d records merged into %.100s." : "%d records merged into files from %.100s.",
                 n, leaf(path));
        set_status(msg);
        break;
    }
    request_document_redraw();
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
    case M_PAGE_LAST: set_page_view(page_total() - 1); break;
    case M_ZOOM_IN: set_zoom(zoom_step(1)); break;
    case M_ZOOM_OUT: set_zoom(zoom_step(-1)); break;
    case M_ZOOM_100: set_zoom(100); break;
    case M_ZOOM_FIT_PAGE: zoom_choice = ZOOM_FIT_PAGE; set_zoom(fit_zoom(0)); break;
    case M_ZOOM_FIT_WIDTH: zoom_choice = ZOOM_FIT_WIDTH; set_zoom(fit_zoom(1)); break;
    case M_NAV: show_nav = !show_nav; relayout(); break;
    case M_INSPECTOR: show_inspector = !show_inspector; relayout(); break;
    case M_IMAGE: do_command(C_IMAGE); break;
    case M_IMAGE_FLOAT: do_image(1); break;
    case M_TEXT_BOX: do_text_box(); break;
    case M_TABLE: do_command(C_TABLE); break;
    case M_LINK: do_link(); break;
    case M_CONTENTS: {
        int rc = ow_editor_insert_table_of_contents(editor, 3, resolve_toc_pages, NULL);
        if (rc == OWF_OK) {
            pages_changed();
            editor_repaint("Table of contents inserted from Heading 1-3; Undo removes it.");
        } else if (rc == OWF_ERR_UNSUPPORTED) {
            tell("Table of Contents", "This document has no Heading 1, Heading 2 or Heading 3 paragraphs.");
        } else if (rc == OWF_ERR_MEMORY) {
            tell("Table of Contents", "There is not enough memory to build the table of contents.");
        } else {
            tell("Table of Contents", "Place the caret in an ordinary body paragraph, outside a table or text box.");
        }
        break;
    }
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
    case M_TABLE_STYLE: do_table_style(); break;
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
                if (rc == OWF_OK) { pages_changed(); editor_repaint("Page setup updated."); }
                else if (rc == OWF_ERR_MEMORY) tell("Page Setup", "There is not enough memory to apply page setup.");
                else tell("Page Setup", "Those margins do not leave a usable page area.");
            }
        }
        break;
    }
    case M_COLUMNS: do_document_columns(); break;
    case M_FRAME: do_frame_properties(); break;
    case M_HEADER: do_story_edit(OW_STORY_HEADER); break;
    case M_FOOTER: do_story_edit(OW_STORY_FOOTER); break;
    case M_HEADER_LEFT: do_story_edit(OW_STORY_HEADER_LEFT); break;
    case M_FOOTER_LEFT: do_story_edit(OW_STORY_FOOTER_LEFT); break;
    case M_FIELD_PAGE: editor_result(ow_editor_insert_field(editor, OWF_FIELD_PAGE), "Page field inserted."); break;
    case M_FIELD_PAGES: editor_result(ow_editor_insert_field(editor, OWF_FIELD_PAGES), "Page-count field inserted."); break;
    case M_FIELD_DATE: editor_result(ow_editor_insert_field(editor, OWF_FIELD_DATE), "Date field inserted."); break;
    case M_FIELD_TITLE: case M_FIELD_AUTHOR: case M_FIELD_SUBJECT: case M_FIELD_FILENAME: case M_FIELD_WORDS:
    case M_FIELD_CHAPTER:
        if (editor && editor_result(ow_editor_insert_field(editor, (owf_field)(OWF_FIELD_TITLE + (id - M_FIELD_TITLE))),
                                    "Field inserted.")) fields_update();
        break;
    case M_FIELD_USER: do_user_field(1); break;
    case M_USER_FIELDS: do_user_field(0); break;
    case M_CROSS_REF: do_cross_reference(); break;
    case M_DOC_INFO: do_doc_info(); break;
    case M_UPDATE_FIELDS:
        if (editor) { fields_update(); set_status("Fields updated."); request_document_redraw(); }
        break;
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
    case M_TRACK: if (editor) { ow_editor_set_track_changes(editor, !ow_editor_track_changes(editor)); sync_track_menu(); editor_repaint(ow_editor_track_changes(editor) ? "Recording changes." : "Changes no longer recorded."); } break;
    case M_CHANGE_NEXT: do_next_change(0); break;
    case M_CHANGE_PREV: do_next_change(1); break;
    case M_CHANGE_ACCEPT: do_accept_change(1, 0); break;
    case M_CHANGE_REJECT: do_accept_change(0, 0); break;
    case M_CHANGE_ACCEPT_ALL: do_accept_change(1, 1); break;
    case M_CHANGE_REJECT_ALL: do_accept_change(0, 1); break;
    case M_REVIEW_AUTHOR: do_review_author(); break;
    case M_COMMENT: case M_COMMENT_NEXT: case M_COMMENT_PREV: case M_COMMENT_REPLY: case M_COMMENT_EDIT:
    case M_COMMENT_DONE: case M_COMMENT_DELETE: do_comment((int)id); break;
    case M_FOOTNOTE: case M_ENDNOTE: case M_NOTE_NEXT: case M_NOTE_PREV: case M_NOTE_EDIT: case M_NOTE_CONVERT:
    case M_NOTE_DELETE: do_note((int)id); break;
    case M_MERGE_SOURCE: case M_MERGE_FIELD: case M_MERGE_NEXT: case M_MERGE_PREV: case M_MERGE_NAMES:
    case M_MERGE_DOC: case M_MERGE_FILES: do_merge((int)id); break;
    case M_INDEX_ENTRY: case M_INDEX: do_index((int)id); break;
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
            if(run->kind==OWF_RUN_TEXT&&run->note>0){char m[16];ow_layout_note_mark(doc,run->note-1,m,sizeof m);snprintf(field,sizeof field,"[%s]",m);t=field;}   /* 2.0.30 */
            else if(run->kind==OWF_RUN_TEXT)t=run->text;
            else if(run->kind==OWF_RUN_TAB)t="\t";
            else if(run->kind==OWF_RUN_LINEBREAK)t="\n";
            else if(run->kind==OWF_RUN_FIELD){if(run->field==OWF_FIELD_PAGE)snprintf(field,sizeof field,"%d",caret_page()+1);else if(run->field==OWF_FIELD_PAGES)snprintf(field,sizeof field,"%d",page_total());else field[0]=0;t=run->field>OWF_FIELD_TIME?(run->text?run->text:""):field;}   /* 2.0.31: a value whole, not cut at 31 bytes */
            if(!t)continue;
            add=strlen(t);
            if(n+add+2>cap){size_t want=(n+add+2)*2;char *q=(char*)realloc(out,want);if(!q){free(out);return NULL;}out=q;cap=want;}
            memcpy(out+n,t,add);n+=add;out[n]=0;
        }
        if(p+1<doc->body.nparas){if(n+2>cap){char*q=(char*)realloc(out,cap*2);if(!q){free(out);return NULL;}out=q;cap*=2;}out[n++]='\n';out[n]=0;}
    }
    return out;
}

/* 2.0.5, for FONTCHECK: what graphics' Text() advances for n bytes in f, with its own bold or without,
 * drawn into a bitmap of our own and read off the RastPort's cp_x; -1 when it can't be drawn. */
static int drawn_advance(struct TextFont *f, const char *s, int n, int algo_bold)
{
    struct BitMap *bm;
    struct RastPort trp;
    int w;
    if (!f || n <= 0 || n > 32) return -1;
    InitRastPort(&trp);
    SetFont(&trp, f);
    SetSoftStyle(&trp, FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    w = (int)TextLength(&trp, (STRPTR)s, (ULONG)n);    /* room for it twice over and more: no layer clips it */
    bm = AllocBitMap((ULONG)(2 * w + n * (2 * f->tf_XSize + 8) + 64), (ULONG)(f->tf_YSize + 4), 1, BMF_CLEAR, NULL);
    if (!bm) return -1;
    trp.BitMap = bm;
    SetSoftStyle(&trp, algo_bold ? FSF_BOLD : FS_NORMAL, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    SetAPen(&trp, 1);
    SetDrMd(&trp, JAM1);
    Move(&trp, 0, f->tf_Baseline);
    Text(&trp, (STRPTR)s, (ULONG)n);
    w = trp.cp_x;
    WaitBlit();
    FreeBitMap(bm);
    return w;
}

/* 2.0.3, for the lab: FONTCHECK [points [zoom [bold]]]: the document's own font at a size (11 points, the window's
 * zoom): the routes there are (gfopenfont.library and diskfont.library), the window's font, the face that
 * draws the text (the document's, or the first of its kind that opens), where its widths come from (its own
 * at the size, scaled from another, or the window font's), and the sample of letters and digits laid out and
 * drawn: at every zoom drawn is no wider than laid out, and at 100% just as wide. 2.0.5: with bold, one word
 * as well: plain, in graphics' own bold (its TextLength, and how far its Text() moves on), and ours (plain
 * and the smear a letter, as GFOpenWrite lays out and draws bold). */
static void font_check(const char *arg, char *out, size_t size)
{
    static const char *const from[] = { "own", "scaled", "window font" };
    char file[72], *end;
    long pt = strtol(arg, &end, 10), z = strtol(end, &end, 10), bold = strtol(end, NULL, 10);
    uint32_t flags = doc && doc->base.font >= 0 ? OL_TEXT_FONT(OW_LAYOUT_FONT(doc->base.font)) : 0;
    ow_fw_check c;
    const ow_fw_table *t;
    char of[16] = "none", df[16] = "none";
    if (pt < 4 || pt > 96) pt = 11;
    if (z < 10 || z > 800) z = zoom;
    layout_font_file(flags, file, sizeof file);
    ow_fw_check_sample(fonts_w(), file, bold != 0, OL_POINT(pt), (int)z, &c);
    t = ow_fw_widths(fonts_w(), file, bold != 0, c.px);   /* 2.0.4: a space's and an e's widths too */
    if (OpenFontTextBase) snprintf(of, sizeof of, "%d.%d", OpenFontTextBase->lib_Version, OpenFontTextBase->lib_Revision);
    if (DiskfontBase) snprintf(df, sizeof df, "%d.%d", DiskfontBase->lib_Version, DiskfontBase->lib_Revision);
    snprintf(out, size, "%s %ldpt%s: openfont %s, diskfont %s, window font %s %d; face %s, widths %s, measured at "
             "%dpx (space %d, e %d); at %ld%%: %dpx, drawn at %dpx (%d tall), sample %ld px drawn, %ld px laid out",
             file, pt, bold ? " bold" : "", of, df,
             font_attr.ta_Name ? (const char *)font_attr.ta_Name : "?", font ? font->tf_YSize : 0,
             c.face[0] ? c.face : "(none)", from[c.from >= 0 && c.from <= 2 ? c.from : 0], c.px, t->w[' '], t->w['e'],
             z, c.zoom_px, c.draw_px, c.draw_ysize, c.drawn, c.laid);
    if (bold && c.face[0]) {
        /* 2.0.5: graphics' own bold against ours, on one word at the size drawn */
        struct TextFont *bf = document_font_px(c.face, c.draw_px);
        if (bf) {
            static const char word[] = "Contents";
            struct RastPort trp;
            int n = (int)sizeof word - 1, plain, tl_bold, d_plain = drawn_advance(bf, word, n, 0);
            int d_bold = drawn_advance(bf, word, n, 1);
            size_t used = strlen(out);
            InitRastPort(&trp);
            SetFont(&trp, bf);
            plain = (int)TextLength(&trp, (STRPTR)word, (ULONG)n);
            SetSoftStyle(&trp, FSF_BOLD, FSF_BOLD);
            tl_bold = (int)TextLength(&trp, (STRPTR)word, (ULONG)n);
            snprintf(out + used, size - used, "; \"%s\": plain %d (Text %d), graphics bold TextLength %d, Text %d, "
                     "smear %d; ours %d", word, plain, d_plain, tl_bold, d_bold, bf->tf_BoldSmear,
                     plain + n * smear_of(bf, 1));
        }
    }
}

/* 2.0.12, for the lab: MEMCHECK: where GFOpenWrite's memory is. Free memory (and the largest block); libnix's
 * malloc pools (its puddles from AllocMem: how much, how much of it free inside them, which Avail counts as
 * used); and what we hold: the document's paragraphs and runs, the undo and redo snapshots, the pages laid
 * out (GFOpenLayout's ops and nodes, about), the decoded pictures, and the document fonts open (their bitmaps,
 * about). A drop in Avail that none of ours explains is the pools' or the system's (diskfont keeps a font
 * it made until memory runs short; Avail FLUSH frees it). */
extern struct MinList __memorylist;                  /* libnix's malloc (stdlib/malloc.c) */

static void mem_check(char *out, size_t size)
{
    const struct MinNode *n;
    unsigned long pool = 0, pool_free = 0, layout_ops = 0, layout_nodes = 0, pictures = 0, font_bytes = 0;
    unsigned long snap, docb;
    int puddles = 0, nundo = 0, nredo = 0, fonts = 0, i;
    ol_document *od = pages_layout ? ow_layout_body(pages_layout) : NULL;
    Forbid();
    for (n = __memorylist.mlh_Head; n && n->mln_Succ; n = n->mln_Succ) {
        const struct MemHeader *mh = (const struct MemHeader *)n;
        pool += (unsigned long)((const char *)mh->mh_Upper - (const char *)mh);
        pool_free += mh->mh_Free;
        ++puddles;
    }
    Permit();
    if (od) {
        layout_ops = (unsigned long)ol_display_count(od);
        while (ol_node_by_id(od, (ol_id)(layout_nodes + 1))) ++layout_nodes;
    }
    for (i = 0; i < image_cache_n; ++i) pictures += (unsigned long)image_cache[i].w * image_cache[i].h * 4;
    for (i = 0; i < DOC_FONT_CACHE; ++i)
        if (doc_fonts[i].font) {
            const struct TextFont *f = doc_fonts[i].font;
            ++fonts;
            font_bytes += (unsigned long)f->tf_YSize * f->tf_Modulo + (unsigned long)(f->tf_HiChar - f->tf_LoChar + 2) * 8;
        }
    snap = ow_editor_snapshot_bytes(editor, &nundo, &nredo);
    docb = ow_editor_document_bytes(editor);
    snprintf(out, size, "avail %lu (largest %lu); malloc pools %lu in %d, %lu of it free; document %lu; "
             "undo %d + redo %d snapshots, %lu; pages %lu ops (room %lu), %lu nodes, about %lu; pictures %d, %lu; "
             "fonts %d open, about %lu",
             (unsigned long)AvailMem(MEMF_ANY), (unsigned long)AvailMem(MEMF_ANY | MEMF_LARGEST), pool, puddles,
             pool_free, docb, nundo, nredo, snap, layout_ops, (unsigned long)pages_room, layout_nodes,
             layout_ops * (unsigned long)sizeof(ol_display_op) + layout_nodes * 160UL, image_cache_n, pictures,
             fonts, font_bytes);
}

/* RC as every Open app's port: 0 done, 5 not done but nothing wrong, 10 an error, 20 an unknown command. */
#define RX_OK 0
#define RX_WARN 5
#define RX_ERROR 10
#define RX_UNKNOWN 20

static int rexx_do_command(const char *command, char **result_out)
{
    const char *arg="";char result[768];int ok=1,rc_fail=RX_ERROR;char *dynamic=NULL;
    result[0]=0;command=skip_space(command);
    if(starts_word_ci(command,"VERSION",&arg))snprintf(result,sizeof result,"%s",VERSION_TEXT);
    /* ACTIVATE, DEACTIVATE, HELP, FAULT and LASTERROR: with VERSION and QUIT, the commands every Open app's port has */
    else if(starts_word_ci(command,"ACTIVATE",&arg)){if(win){ScreenToFront(win->WScreen);WindowToFront(win);ActivateWindow(win);}}
    else if(starts_word_ci(command,"DEACTIVATE",&arg)){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"DEACTIVATE: GFOpenWrite has no icon to go to");}
    else if(starts_word_ci(command,"LASTERROR",&arg))rexx_copy(result,sizeof result,rexx_lasterror);
    else if(starts_word_ci(command,"HELP",&arg))snprintf(result,sizeof result,"VERSION ACTIVATE DEACTIVATE HELP FAULT LASTERROR WORDCOUNT PAGECOUNT FONTCHECK MEMCHECK "
        "OPEN SAVEAS SAVE EXPORT PRINT INSERTTEXT FIND GETTEXT COMMAND TRACKCHANGES CHANGES NEXTCHANGE PREVCHANGE "
        "ACCEPTCHANGE REJECTCHANGE ACCEPTALL REJECTALL REVIEWAUTHOR ADDCOMMENT COMMENTS NEXTCOMMENT PREVCOMMENT GETCOMMENT "
        "REPLYCOMMENT EDITCOMMENT RESOLVECOMMENT DELETECOMMENT INSERTFIELD DOCINFO USERFIELD CROSSREF UPDATEFIELDS "
        "ADDFOOTNOTE ADDENDNOTE NOTES NEXTNOTE PREVNOTE GETNOTE EDITNOTE CONVERTNOTE DELETENOTE "
        "MERGESOURCE MERGEFIELDS MERGERECORDS MERGERECORD INSERTMERGEFIELD MERGE INDEXENTRY INDEXENTRIES INSERTINDEX QUIT");
    else if(starts_word_ci(command,"FAULT",&arg)){if(!*arg){ok=0;snprintf(result,sizeof result,"FAULT needs a code");}else Fault(atol(arg),NULL,(STRPTR)result,sizeof result);}
    else if(starts_word_ci(command,"WORDCOUNT",&arg))snprintf(result,sizeof result,"%d",word_count());
    else if(starts_word_ci(command,"PAGECOUNT",&arg))snprintf(result,sizeof result,"%d",editor?page_total():0);
    else if(starts_word_ci(command,"FONTCHECK",&arg))font_check(arg,result,sizeof result);
    else if(starts_word_ci(command,"MEMCHECK",&arg))mem_check(result,sizeof result);
    else if(starts_word_ci(command,"OPEN",&arg)){if(!*arg){ok=0;snprintf(result,sizeof result,"OPEN needs a file");}else if(editor&&ow_editor_is_dirty(editor)){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"The document has unsaved changes");}else{open_document(arg);relayout();if(rexx_noted)ok=0;snprintf(result,sizeof result,"%s",status);}}
    else if(starts_word_ci(command,"SAVEAS",&arg)){if(!*arg){ok=0;snprintf(result,sizeof result,"SAVEAS needs a file");}else{save_document_as(arg);if(rexx_noted)ok=0;snprintf(result,sizeof result,"%s",status);}}
    else if(starts_word_ci(command,"SAVE",&arg)){if(current_path[0]){save_document_as(current_path);if(rexx_noted)ok=0;snprintf(result,sizeof result,"%s",status);}else{ok=0;snprintf(result,sizeof result,"Untitled document needs SAVEAS");}}
    else if(starts_word_ci(command,"EXPORT",&arg)){owf_report *rp;if(!*arg){ok=0;snprintf(result,sizeof result,"Missing export path");}else{rp=owf_report_new();ok=owf_export_file(doc,arg,NULL,rp)==OWF_OK;owf_report_free(rp);snprintf(result,sizeof result,"%s",ok?"Exported":"Export failed");}}
    else if(starts_word_ci(command,"PRINT",&arg)){ok=ow_print_document_named(doc,current_path[0]?leaf(current_path):NULL,result,sizeof result);}
    else if(starts_word_ci(command,"INSERTTEXT",&arg)){ok=editor_result(ow_editor_insert_text_block(editor,arg,strlen(arg)),"ARexx text inserted.");snprintf(result,sizeof result,"%s",ok?"Inserted":"Insert failed");}
    else if(starts_word_ci(command,"FIND",&arg)){if(!*arg){ok=0;snprintf(result,sizeof result,"Missing search text");}else{int f=ow_editor_find(editor,arg,0,0,1);ok=f>0;if(ok)bring_focus_into_view();else{request_document_redraw();rc_fail=RX_WARN;}snprintf(result,sizeof result,"%s",ok?"Found":"Not found");}}
    else if(starts_word_ci(command,"GETTEXT",&arg)){dynamic=document_plain_text();if(!dynamic){ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Out of memory");}}
    else if(starts_word_ci(command,"COMMAND",&arg)){
        if(!strcasecmp(arg,"BOLD"))menu_action(M_BOLD);else if(!strcasecmp(arg,"ITALIC"))menu_action(M_ITALIC);else if(!strcasecmp(arg,"UNDERLINE"))menu_action(M_UNDERLINE);else if(!strcasecmp(arg,"PAGEBREAK"))menu_action(M_PAGE_BREAK);else if(!strcasecmp(arg,"UNDO"))menu_action(M_UNDO);else if(!strcasecmp(arg,"REDO"))menu_action(M_REDO);else{ok=0;snprintf(result,sizeof result,"Unknown COMMAND");}
        if(ok&&!result[0])snprintf(result,sizeof result,"OK");
    }
    /* 2.0.27: tracked changes. TRACKCHANGES [ON|OFF] answers 1 or 0; CHANGES the count; NEXTCHANGE and PREVCHANGE
     * the change they selected; ACCEPT/REJECT ... RC 5 when there was nothing to do */
    else if(starts_word_ci(command,"TRACKCHANGES",&arg)){if(!editor){ok=0;snprintf(result,sizeof result,"No document");}else{if(!strcasecmp(arg,"ON")||!strcmp(arg,"1"))ow_editor_set_track_changes(editor,1);else if(!strcasecmp(arg,"OFF")||!strcmp(arg,"0"))ow_editor_set_track_changes(editor,0);else if(*arg){ok=0;snprintf(result,sizeof result,"TRACKCHANGES takes ON or OFF");}if(ok){sync_track_menu();draw_status();snprintf(result,sizeof result,"%d",ow_editor_track_changes(editor));}}}
    else if(starts_word_ci(command,"CHANGES",&arg))snprintf(result,sizeof result,"%d",editor?ow_editor_change_count(editor):0);
    else if(starts_word_ci(command,"NEXTCHANGE",&arg)||starts_word_ci(command,"PREVCHANGE",&arg)){ow_change_info ci;int back=tolower((unsigned char)command[0])=='p';if(!editor||ow_editor_next_change(editor,back)<=0||!ow_editor_current_change(editor,&ci)){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No changes");}else{bring_focus_into_view();change_text(&ci,ow_editor_change_count(editor),result,sizeof result);}}
    else if(starts_word_ci(command,"ACCEPTCHANGE",&arg)||starts_word_ci(command,"REJECTCHANGE",&arg)||starts_word_ci(command,"ACCEPTALL",&arg)||starts_word_ci(command,"REJECTALL",&arg)){int acc=tolower((unsigned char)command[0])=='a',all=tolower((unsigned char)command[6])=='a'&&tolower((unsigned char)command[7])=='l';int rc=!editor?OWF_ERR_UNSUPPORTED:all?ow_editor_accept_all_changes(editor,acc):ow_editor_accept_change(editor,acc);if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"%s",all?"The document has no changes":"No change at the caret");}else{ok=editor_result(rc,acc?"Accepted.":"Rejected.");snprintf(result,sizeof result,"%s",ok?(acc?"Accepted":"Rejected"):"Failed");}}
    else if(starts_word_ci(command,"REVIEWAUTHOR",&arg)){if(*arg){snprintf(review_author,sizeof review_author,"%s",arg);review_author_set=1;if(editor)ow_editor_set_review_author(editor,review_author);}snprintf(result,sizeof result,"%s",review_author);}
    /* 2.0.28: comments. ADDCOMMENT, REPLYCOMMENT and EDITCOMMENT take the text ("\n": a new paragraph); COMMENTS
     * the count; NEXTCOMMENT, PREVCOMMENT and GETCOMMENT the comment (at the caret); RESOLVECOMMENT [OFF];
     * RC 5 when there is no comment there */
    else if(starts_word_ci(command,"ADDCOMMENT",&arg)||starts_word_ci(command,"REPLYCOMMENT",&arg)||starts_word_ci(command,"EDITCOMMENT",&arg)){char c0=(char)tolower((unsigned char)command[0]);char *u;int rc;
        if(!editor){ok=0;snprintf(result,sizeof result,"No document");}else if(!*arg){ok=0;snprintf(result,sizeof result,"The comment needs its text");}else if(!(u=comment_utf8(arg))){ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Out of memory");}
        else{review_author_init();ow_editor_set_review_author(editor,review_author);rc=c0=='a'?ow_editor_insert_comment(editor,u):c0=='r'?ow_editor_reply_comment(editor,u):ow_editor_set_comment_text(editor,u);free(u);
            if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No comment at the caret");}else{ok=editor_result(rc,c0=='a'?"Comment added.":c0=='r'?"Reply added.":"Comment changed.");snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}}
    else if(starts_word_ci(command,"COMMENTS",&arg))snprintf(result,sizeof result,"%d",editor?ow_editor_comment_count(editor):0);
    else if(starts_word_ci(command,"NEXTCOMMENT",&arg)||starts_word_ci(command,"PREVCOMMENT",&arg)||starts_word_ci(command,"GETCOMMENT",&arg)){ow_comment_info ci;char c0=(char)tolower((unsigned char)command[0]);
        if(!editor||(c0!='g'&&ow_editor_next_comment(editor,c0=='p')<=0)||!ow_editor_current_comment(editor,&ci)){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"%s",c0=='g'?"No comment at the caret":"No comments");}
        else{if(c0!='g')bring_focus_into_view();comment_text(&ci,ow_editor_comment_count(editor),result,sizeof result);}}
    else if(starts_word_ci(command,"RESOLVECOMMENT",&arg)||starts_word_ci(command,"DELETECOMMENT",&arg)){int del=tolower((unsigned char)command[0])=='d';int off=!strcasecmp(arg,"OFF")||!strcmp(arg,"0");int rc=!editor?OWF_ERR_UNSUPPORTED:del?ow_editor_delete_comment(editor):ow_editor_resolve_comment(editor,!off);
        if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No comment at the caret");}else{ok=editor_result(rc,del?"Comment deleted.":off?"Comment open again.":"Comment marked done.");snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}
    /* 2.0.30: notes. ADDFOOTNOTE text, ADDENDNOTE text ("\n": a new paragraph) at the caret; NOTES answers how
     * many; NEXTNOTE, PREVNOTE and GETNOTE answer "Footnote 1: text (1 of 2)" (RC 5: none); EDITNOTE text,
     * CONVERTNOTE and DELETENOTE act on the note at the caret (RC 5: none there) */
    else if(starts_word_ci(command,"ADDFOOTNOTE",&arg)||starts_word_ci(command,"ADDENDNOTE",&arg)||starts_word_ci(command,"EDITNOTE",&arg)){
        int add=toupper((unsigned char)command[0])=='A';char *u=NULL;int rc;
        if(!editor){ok=0;snprintf(result,sizeof result,"No document");}
        else if(!*arg){ok=0;snprintf(result,sizeof result,"The note needs its text");}
        else if(!(u=comment_utf8(arg))){ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Out of memory");}
        else{rc=add?ow_editor_insert_note(editor,toupper((unsigned char)command[3])=='E',u):ow_editor_set_note_text(editor,u);free(u);
            if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No note at the caret");}
            else{ok=editor_result(rc,add?"Note added.":"Note changed.");snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}}
    else if(starts_word_ci(command,"NOTES",&arg))snprintf(result,sizeof result,"%d",editor?ow_editor_note_count(editor):0);
    else if(starts_word_ci(command,"NEXTNOTE",&arg)||starts_word_ci(command,"PREVNOTE",&arg)||starts_word_ci(command,"GETNOTE",&arg)){ow_note_info ni;char c0=(char)tolower((unsigned char)command[0]);
        if(!editor||(c0!='g'&&ow_editor_next_note(editor,c0=='p')<=0)||!ow_editor_current_note(editor,&ni)){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"%s",c0=='g'?"No note at the caret":"No notes");}
        else{if(c0!='g')bring_focus_into_view();note_text(&ni,ow_editor_note_count(editor),result,sizeof result);}}
    else if(starts_word_ci(command,"CONVERTNOTE",&arg)||starts_word_ci(command,"DELETENOTE",&arg)){int del=toupper((unsigned char)command[0])=='D';int rc=!editor?OWF_ERR_UNSUPPORTED:del?ow_editor_delete_note(editor):ow_editor_convert_note(editor);
        if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No note at the caret");}else{ok=editor_result(rc,del?"Note deleted.":"Note changed kind.");snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}
    /* 2.0.31: mail merge. MERGESOURCE [file|-] reads a data source ("-": none) or answers the document's (RC 5:
     * none); MERGEFIELDS answers the columns between '|'; MERGERECORDS how many records; MERGERECORD [n|0] shows
     * record n (from 1; 0: the names) or answers the one shown; INSERTMERGEFIELD column at the caret; MERGE file
     * [FILES] merges every record into one document, or a file each (numbered), and answers how many */
    else if(starts_word_ci(command,"MERGESOURCE",&arg)){int rc;
        if(!editor){ok=0;snprintf(result,sizeof result,"No document");}
        else if(!*arg){const char *src=ow_editor_merge_source(editor);if(!src){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No data source");}else{size_t o=to_latin1(src,strlen(src),result,sizeof result);result[o<sizeof result?o:sizeof result-1]=0;}}
        else{rc=ow_editor_merge_load(editor,strcmp(arg,"-")?arg:NULL);if(rc!=OWF_OK){ok=0;snprintf(result,sizeof result,"%s",merge_error(rc));}else{pages_changed();snprintf(result,sizeof result,"%d",ow_editor_merge_records(editor));}}}
    else if(starts_word_ci(command,"MERGEFIELDS",&arg)){int k,cols=editor?ow_editor_merge_columns(editor):0;size_t o=0;result[0]=0;
        if(!cols){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No data source");}
        else for(k=0;k<cols&&o+2<sizeof result;++k){const char *c=ow_editor_merge_column(editor,k);if(k)result[o++]='|';o+=to_latin1(c,strlen(c),result+o,sizeof result-o);if(o>=sizeof result)o=sizeof result-1;result[o]=0;}}
    else if(starts_word_ci(command,"MERGERECORDS",&arg))snprintf(result,sizeof result,"%d",editor?ow_editor_merge_records(editor):0);
    else if(starts_word_ci(command,"MERGERECORD",&arg)){int rc;
        if(!editor){ok=0;snprintf(result,sizeof result,"No document");}
        else if(!*arg)snprintf(result,sizeof result,"%d",ow_editor_merge_shown(editor)+1);
        else{rc=ow_editor_merge_show(editor,atoi(arg)-1);if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No such record");}else{ok=editor_result(rc,NULL);if(ok)pages_changed();snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}}
    else if(starts_word_ci(command,"INSERTMERGEFIELD",&arg)){char *u;int rc;
        if(!editor||!*arg){ok=0;snprintf(result,sizeof result,"INSERTMERGEFIELD needs a column");}
        else if(!(u=latin1_utf8(arg))){ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Out of memory");}
        else{rc=ow_editor_insert_merge_field(editor,u);free(u);ok=editor_result(rc,"Merge field inserted.");if(ok)fields_update();snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}
    else if(starts_word_ci(command,"MERGE",&arg)){char path[400];const char *rest=arg;int n=0,files;
        while(*rest&&*rest!=' '&&n<399){path[n++]=*rest++;}path[n]=0;rest=skip_space(rest);files=starts_word_ci(rest,"FILES",NULL);
        if(!editor||!path[0]){ok=0;snprintf(result,sizeof result,"MERGE needs a file");}
        else if((n=merge_out(path,files))<0){ok=0;if(n==-OWF_ERR_UNSUPPORTED)rc_fail=RX_WARN;snprintf(result,sizeof result,"%s",-n==OWF_ERR_UNSUPPORTED?"No records":owf_error_text(-n));}
        else snprintf(result,sizeof result,"%d",n);}
    /* 2.0.32: the alphabetical index. INDEXENTRY entry marks one at the caret ("Main:Sub" for a subentry);
     * INDEXENTRIES answers how many; INSERTINDEX puts the index at the caret (RC 5: no entries) */
    else if(starts_word_ci(command,"INDEXENTRY",&arg)){char *u;int rc;
        if(!editor||!*arg){ok=0;snprintf(result,sizeof result,"INDEXENTRY needs an entry");}
        else if(!(u=latin1_utf8(arg))){ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Out of memory");}
        else{rc=ow_editor_insert_index_entry(editor,u);free(u);ok=editor_result(rc,"Index entry marked.");if(ok)pages_changed();snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}
    else if(starts_word_ci(command,"INDEXENTRIES",&arg))snprintf(result,sizeof result,"%d",editor?ow_editor_index_entry_count(editor):0);
    else if(starts_word_ci(command,"INSERTINDEX",&arg)){int rc=editor?ow_editor_insert_index(editor,resolve_toc_pages,NULL):OWF_ERR_FORMAT;
        if(rc==OWF_ERR_UNSUPPORTED){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No index entries");}
        else{ok=editor_result(rc,"Alphabetical index inserted.");if(ok)pages_changed();snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}
    /* 2.0.29: fields. INSERTFIELD kind [name] (PAGE PAGES DATE TIME TITLE AUTHOR SUBJECT FILENAME WORDS CHAPTER,
     * USER name, REF name, REFPAGE name: a bookmark's); DOCINFO TITLE|AUTHOR|SUBJECT [value] answers the value;
     * USERFIELD name [value|-] answers the value (RC 5: none; "-" removes it); CROSSREF n [PAGE] refers to the
     * n-th heading (from 1); UPDATEFIELDS answers how many changed */
    else if(starts_word_ci(command,"INSERTFIELD",&arg)){static const char *const kinds[]={"PAGE","PAGES","DATE","TIME","TITLE","AUTHOR","SUBJECT","FILENAME","WORDS","CHAPTER","USER","REF","REFPAGE"};char kind[16];const char *rest=arg;int k,n=0,rc;char *u=NULL;
        while(*rest&&*rest!=' '&&n<15){kind[n++]=(char)toupper((unsigned char)*rest++);}kind[n]=0;rest=skip_space(rest);
        for(k=0;k<13&&strcmp(kinds[k],kind);++k);
        if(!editor||k==13){ok=0;snprintf(result,sizeof result,"INSERTFIELD takes PAGE PAGES DATE TIME TITLE AUTHOR SUBJECT FILENAME WORDS CHAPTER, or USER, REF or REFPAGE and a name");}
        else{if(*rest)u=comment_utf8(rest);rc=ow_editor_insert_field_arg(editor,(owf_field)k,u&&*u?u:NULL);free(u);if(rc==OWF_ERR_FORMAT){ok=0;snprintf(result,sizeof result,"%s",k>=10?"That field needs a name":"That field takes no name");}else{ok=editor_result(rc,"Field inserted.");if(ok)fields_update();snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}}
    else if(starts_word_ci(command,"DOCINFO",&arg)){char what[16];const char *rest=arg;int n=0,k;while(*rest&&*rest!=' '&&n<15){what[n++]=(char)toupper((unsigned char)*rest++);}what[n]=0;rest=skip_space(rest);
        k=!strcmp(what,"TITLE")?0:!strcmp(what,"AUTHOR")?1:!strcmp(what,"SUBJECT")?2:-1;
        if(!editor||k<0){ok=0;snprintf(result,sizeof result,"DOCINFO takes TITLE, AUTHOR or SUBJECT, and a value to set");}
        else{const char *v;if(*rest){char *u=comment_utf8(!strcmp(rest,"\"\"")?"":rest);ok=u&&ow_editor_set_doc_info(editor,(ow_doc_info)k,u)==OWF_OK;free(u);if(ok){fields_update();update_window_title();request_document_redraw();}}
            v=ow_editor_doc_info(editor,(ow_doc_info)k);if(ok)to_latin1(v?v:"",v?strlen(v):0,result,sizeof result);else snprintf(result,sizeof result,"Failed");}}
    else if(starts_word_ci(command,"USERFIELD",&arg)){char name[64];const char *rest=arg,*now=NULL;int n=0,i,rc=OWF_OK;char *un;while(*rest&&*rest!=' '&&n<63){name[n++]=*rest++;}name[n]=0;rest=skip_space(rest);
        if(!editor||!name[0]){ok=0;snprintf(result,sizeof result,"USERFIELD needs a name");}
        else if(!(un=comment_utf8(name))){ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Out of memory");}
        else{if(*rest){if(!strcmp(rest,"-"))rc=ow_editor_set_user_field(editor,un,NULL);else{char *uv=comment_utf8(rest);rc=uv?ow_editor_set_user_field(editor,un,uv):OWF_ERR_MEMORY;free(uv);}if(rc==OWF_OK){fields_update();request_document_redraw();}}
            for(i=0;i<ow_editor_user_field_count(editor);++i){const char *nn,*vv;if(ow_editor_user_field_at(editor,i,&nn,&vv)&&!strcmp(nn,un))now=vv;}free(un);
            if(rc!=OWF_OK&&rc!=OWF_ERR_UNSUPPORTED){ok=0;snprintf(result,sizeof result,"Failed");}else if(!now&&strcmp(rest,"-")){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No user field of that name");}else to_latin1(now?now:"",now?strlen(now):0,result,sizeof result);}}
    else if(starts_word_ci(command,"CROSSREF",&arg)){owf_doc *d=editor?ow_editor_document(editor):NULL;int want=atoi(arg),i,k=0,para=-1,page=strstr(arg,"PAGE")||strstr(arg,"page");
        for(i=0;d&&i<d->body.nparas&&para<0;++i)if(d->body.paras[i].fmt.heading>0&&++k==want)para=i;
        if(para<0){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"No heading %d",want);}
        else{ok=editor_result(ow_editor_insert_cross_reference(editor,para,page),"Cross-reference inserted.");if(ok)fields_update();snprintf(result,sizeof result,"%s",ok?"OK":"Failed");}}
    else if(starts_word_ci(command,"UPDATEFIELDS",&arg)){int before=editor?(int)ow_editor_changes(editor):0;fields_update();request_document_redraw();snprintf(result,sizeof result,"%d",editor&&(int)ow_editor_changes(editor)!=before?1:0);}
    else if(starts_word_ci(command,"QUIT",&arg)){if(editor&&ow_editor_is_dirty(editor)&&strcasecmp(arg,"FORCE")){ok=0;rc_fail=RX_WARN;snprintf(result,sizeof result,"Unsaved changes (QUIT FORCE quits)");}else{quit_now=1;snprintf(result,sizeof result,"Quitting");}}
    else {ok=0;rc_fail=RX_UNKNOWN;snprintf(result,sizeof result,"Unknown GFOpenWrite ARexx command; HELP lists them");}
    if(!ok){free(dynamic);dynamic=NULL;rexx_copy(rexx_lasterror,sizeof rexx_lasterror,rexx_noted?rexx_noting:result);}
    if(result_out){if(dynamic)*result_out=dynamic;else{*result_out=(char*)malloc(strlen(result)+1);if(*result_out)strcpy(*result_out,result);else if(ok){ok=0;rc_fail=RX_UNKNOWN;}}}
    return ok?RX_OK:rc_fail;
}

static void rexx_messages(void)
{
    struct RexxMsg *rm;
    while(rexx_port&&(rm=(struct RexxMsg*)GetMsg(rexx_port))!=NULL){
        char *result=NULL;int rc;
        rexx_busy=1;rexx_noted=0;
        rc=rexx_do_command((const char*)rm->rm_Args[0],&result);
        rexx_busy=0;
        rm->rm_Result1=rc;rm->rm_Result2=0;
        /* RESULT only with RC 0 (a secondary result with an error RC is an error number, not a string) */
        if(rc==RX_OK&&(rm->rm_Action&RXFF_RESULT)&&RexxSysBase&&result)rm->rm_Result2=(LONG)CreateArgstring((STRPTR)result,(LONG)strlen(result));
        free(result);ReplyMsg((struct Message*)rm);
    }
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
        ULONG secs = im->Seconds, micros = im->Micros;
        int handled = 0, cx = 0, cy = 0, row = -1, r;
        /* 2.0: the kinds see every message first, before it is replied (the scroller's tags live in it) */
        if (pv) {
            r = ogt_pageview_event(pv, class, code, ia, mx, my, &cx, &cy);
            if (r == OGT_PV_SCROLLED) { page_index = ogt_pageview_page(pv); if (nav) { ogt_list_select(nav, page_index); ogt_list_show(nav, page_index); } draw_status(); handled = 1; }
            else if (r == OGT_PV_PRESS) {
                mouse_set_selection(mx, my, (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0);
                if (frame_drag_begin(mx, my)) mouse_selecting = 0;
                else mouse_selecting = 1;
                handled = 1;
            }
            else if (r == OGT_PV_DRAG) {
                if (frame_dragging) frame_drag_update(mx, my);
                else if (mouse_selecting) mouse_set_selection(mx, my, 1);
                handled = 1;
            }
            else if (r == OGT_PV_RELEASE) {
                if (frame_dragging) frame_drag_finish(mx, my);
                else if (mouse_selecting) { mouse_set_selection(mx, my, 1); mouse_selecting = 0; }
                handled = 1;
            }
        }
        if (!handled && nav) {
            r = ogt_list_event(nav, class, code, ia, mx, my, secs, micros, &row);
            if (r == OGT_LIST_PICKED || r == OGT_LIST_OPENED) { set_page_view(row); handled = 1; }
            else if (r == OGT_LIST_SCROLLED) handled = 1;
        }
        if (!handled && doc && (r = ogt_ruler_event(&ruler, class, code, mx, my)) != OGT_RULER_NONE) {
            owf_parafmt pf;
            memset(&pf, 0, sizeof pf);
            ow_editor_current_parafmt(editor, &pf);
            pf.indent_left = ruler.left_twips; pf.indent_first = ruler.first_twips; pf.indent_right = ruler.right_twips;
            editor_result(ow_editor_apply_parafmt(editor, &pf, OW_PARAFMT_INDENTS), "Indents changed.");
            handled = 1;
        }
        GT_ReplyIMsg(im);
        if (handled) {
            if (class == IDCMP_MOUSEBUTTONS && code == SELECTUP && tb.pressed >= 0) {
                ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                tb.pressed = -1;
            }
            continue;
        }
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
            if (g && g->GadgetID >= GID_TOOLBAR && g->GadgetID < GID_STYLE) toolbar_event(class, g);
            else if (g && class == IDCMP_GADGETUP) format_gadget(g, code);
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTUP) {
                if (frame_dragging) frame_drag_finish(mx, my);
                else if (mouse_selecting) { mouse_set_selection(mx, my, 1); mouse_selecting = 0; }
                if (tb.pressed >= 0) {
                    ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window");
                    tb.pressed = -1;
                }
            }
            break;
        case IDCMP_MOUSEMOVE:
            if (frame_dragging) frame_drag_update(mx, my);
            else if (mouse_selecting) mouse_set_selection(mx, my, 1);
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
                    else if (code == 0x4e) set_page_view(page_total() - 1);
                    else if (code == 0x4c) set_page_view(page_index - 1);
                    else if (code == 0x4d) set_page_view(page_index + 1);
                    else if (code == 0x44) editor_result(ow_editor_insert_page_break(editor), "Page break inserted.");
                } else if (selected_frame_key(code, qual)) {
                    /* A selected floating frame takes arrows; Escape returns them to the caret. */
                } else switch (code) {
                case 0x4f: ow_editor_move_caret(editor, OW_MOVE_LEFT, extend); bring_focus_into_view(); break;
                case 0x4e: ow_editor_move_caret(editor, OW_MOVE_RIGHT, extend); bring_focus_into_view(); break;
                case 0x4c: if (!caret_line_move(-1, extend)) ow_editor_move_caret(editor, OW_MOVE_UP, extend); bring_focus_into_view(); break;
                case 0x4d: if (!caret_line_move(1, extend)) ow_editor_move_caret(editor, OW_MOVE_DOWN, extend); bring_focus_into_view(); break;
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
        if (argc) PutStr((STRPTR)"GFOpenWrite needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    if (!owf_open()) {
        tell("GFOpenWrite", "GFOpenWrite needs " OWF_LIBRARY_NAME " in LIBS:.");
        rc = 20;
        goto out;
    }

    OpenFontTextBase = ogt_openfont_open();
    LayersBase = OpenLibrary((STRPTR)"layers.library", 36);

    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39);
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 0);
    ogt_iconset_attach();   /* the toolbar's pictures come from the Chrome set (Images/Toolbar/) */
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
        if (argc) PutStr((STRPTR)"GFOpenWrite: the window could not open\n");
        rc = 20;
        goto out;
    }
    rexx_open();
    ow_recovery_init(&recovery, "GFOpenWrite", "odt");
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
    ow_recovery_end(&recovery, editor && ow_editor_is_dirty(editor));   /* unsaved: the copy waits for the next start */
    rexx_close();
    close_window();
    if (spell) { ow_spell_close(spell); spell = NULL; }
    pages_free();
    if (editor) { ow_editor_free(editor); editor = NULL; }
    if (doc) { owf_doc_free(doc); doc = NULL; }
    if (theme.text) ogt_theme_free(&theme);
    if (LayersBase) { CloseLibrary(LayersBase); LayersBase = NULL; }
    if (OpenFontTextBase) { CloseLibrary(OpenFontTextBase); OpenFontTextBase = NULL; }
    if (DiskfontBase) { CloseLibrary(DiskfontBase); DiskfontBase = NULL; }
    image_cache_clear();
    if (CyberGfxBase) { CloseLibrary(CyberGfxBase); CyberGfxBase = NULL; }
    if (DataTypesBase) { CloseLibrary(DataTypesBase); DataTypesBase = NULL; }
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
