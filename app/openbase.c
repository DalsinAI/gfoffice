/*
 * GFOpenBase - the database of the GFOffice Suite (docs/gfoffice/Design-OpenBase.md).
 *
 * B0c, the first window: the project's tables on the left, the chosen
 * table's rows as a datasheet on the right, and two windows of their own:
 * the table designer (fields, their types and rules) and the row editor (one
 * line a field). Everything about the project is GFOpenBase's portable core
 * (src/base, include/openbase_core.h); this file only shows it and hands it
 * what the user types. Plain GadTools in the screen's font, so it works on
 * any Workbench screen; 68040 code, no FPU needed.
 *
 *   GFOpenBase [FILE] [NEW] [TABLE=name] [ROW=key]
 *                               FILE: a project (.oadb); NEW makes it if there is none;
 *                               TABLE and ROW: the table shown, and the row chosen in it
 *
 * ARexx: the GFOPENBASE port (GFOPENBASE.2 for a second copy), as docs/gfoffice/Design-Suite-Shared.md
 * section 6 has it: VERSION HELP ACTIVATE DEACTIVATE FAULT LASTERROR ISDIRTY GETFILE OPEN TABLES TABLE
 * ROW QUIT. OPEN FILE "x" TABLE "t" ROW n is how ACFind and GFOpenBench open a row hit (amicradle-teams
 * designs/ACFIND-EVERY-DRIVE-DESIGN-20261111.md section 9). No requester shows while a command runs.
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

#include "openbase_core.h"
#include "ob_reportwin.h"
#include "ow_stack.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL;
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "GFOpenBase 0.14.2 (16.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " GFOpenBase, the database of the GFOffice Suite. MIT, Copyright (c) 2026 AmiCradle Team";

/* ---- Menus and gadgets ---- */

enum {
    M_NEW = 1, M_OPEN, M_CLOSE, M_COPY, M_COMPACT, M_CHECK, M_ABOUT, M_QUIT,
    M_T_NEW, M_T_DESIGN, M_T_DELETE, M_T_RELATIONS, M_R_ADD, M_R_EDIT, M_R_DELETE, M_R_FIRST, M_R_LAST,
    M_T_VIEW, M_T_ALL, M_T_IMPORT, M_T_EXPORT, M_UNDO, M_T_FROMCSV,
    M_Q_NEW, M_Q_OPEN, M_Q_DESIGN, M_Q_AGAIN, M_T_FORM, M_T_FORMS, M_T_FROMDBF, M_T_IMPORTDBF, M_T_EXPORTDBF, M_T_FDESIGN, M_Q_VIEW
};

/* 4.3: the Report window (app/ob_reportwin.c) */
enum { M_REPORTS = 100 };

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New...", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, "Close", NULL, 0, 0, (APTR)M_CLOSE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Save a Copy...", NULL, 0, 0, (APTR)M_COPY },
    { NM_ITEM, "Compact", NULL, 0, 0, (APTR)M_COMPACT },
    { NM_ITEM, "Check", NULL, 0, 0, (APTR)M_CHECK },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", "?", 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, "Edit", NULL, 0, 0, NULL },
    { NM_ITEM, "Undo", "Z", 0, 0, (APTR)M_UNDO },
    { NM_TITLE, "Table", NULL, 0, 0, NULL },
    { NM_ITEM, "New Table...", "T", 0, 0, (APTR)M_T_NEW },
    { NM_ITEM, "Design...", "D", 0, 0, (APTR)M_T_DESIGN },
    { NM_ITEM, "Delete Table...", NULL, 0, 0, (APTR)M_T_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Relationships...", "R", 0, 0, (APTR)M_T_RELATIONS },
    { NM_ITEM, "Open as Form...", "K", 0, 0, (APTR)M_T_FORM },
    { NM_ITEM, "Open Form...", NULL, 0, 0, (APTR)M_T_FORMS },
    { NM_ITEM, "Design Form...", NULL, 0, 0, (APTR)M_T_FDESIGN },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Sort and Filter...", "F", 0, 0, (APTR)M_T_VIEW },
    { NM_ITEM, "Show All Rows", NULL, 0, 0, (APTR)M_T_ALL },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "New Table from CSV...", NULL, 0, 0, (APTR)M_T_FROMCSV },
    { NM_ITEM, "Import CSV...", "I", 0, 0, (APTR)M_T_IMPORT },
    { NM_ITEM, "Export CSV...", NULL, 0, 0, (APTR)M_T_EXPORT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "New Table from dBASE...", NULL, 0, 0, (APTR)M_T_FROMDBF },
    { NM_ITEM, "Import dBASE...", NULL, 0, 0, (APTR)M_T_IMPORTDBF },
    { NM_ITEM, "Export dBASE...", NULL, 0, 0, (APTR)M_T_EXPORTDBF },
    { NM_TITLE, "Query", NULL, 0, 0, NULL },
    { NM_ITEM, "New Query...", "Y", 0, 0, (APTR)M_Q_NEW },
    { NM_ITEM, "Open Query...", "U", 0, 0, (APTR)M_Q_OPEN },
    { NM_ITEM, "Design Query...", NULL, 0, 0, (APTR)M_Q_DESIGN },
    { NM_ITEM, "Run Again", NULL, 0, 0, (APTR)M_Q_AGAIN },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Export Results as CSV...", NULL, 0, 0, (APTR)M_T_EXPORT },
    { NM_ITEM, "Save as View...", NULL, 0, 0, (APTR)M_Q_VIEW },
    { NM_TITLE, "Row", NULL, 0, 0, NULL },
    { NM_ITEM, "Add Row...", "A", 0, 0, (APTR)M_R_ADD },
    { NM_ITEM, "Edit Row...", "E", 0, 0, (APTR)M_R_EDIT },
    { NM_ITEM, "Delete Row...", NULL, 0, 0, (APTR)M_R_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "First Row", NULL, 0, 0, (APTR)M_R_FIRST },
    { NM_ITEM, "Last Row", NULL, 0, 0, (APTR)M_R_LAST },
    { NM_TITLE, "Report", NULL, 0, 0, NULL },
    { NM_ITEM, "Reports...", "P", 0, 0, (APTR)M_REPORTS },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

enum {
    GID_TABLES = 1, GID_T_NEW, GID_T_DESIGN, GID_T_DELETE,
    GID_R_ADD, GID_R_EDIT, GID_R_DELETE, GID_SCROLL
};

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *glist, *g_tables, *g_scroll;
static struct TextFont *font;
static struct TextAttr font_attr;
static char font_name[64];
static int fh, fw;                 /* the font's height and an average character's width */
static int quit_now;

static ob_project *proj;
static char proj_path[512];
static char status[256];

/* The tables, as a GadTools list. */
static struct List table_list;
static struct Node *table_nodes;
static int table_count, table_sel = -1;

/* The chosen table's design and the datasheet. */
static ob_table cur;
static int have_cur;
static ob_int total_rows;          /* the rows the view shows */
static ob_int all_rows;            /* the table's rows */
/* The datasheet's sort and filter: the chosen table's, cleared for another */
static ob_view view;
static char view_table[OB_NAME_MAX];
static ob_int top_row;             /* the first row shown, by position */
static int sel_row = -1;           /* by position; -1 none */
static int left_col;               /* the first field shown */
#define PAGE_MAX 128
static int page_n;
static ob_int page_keys[PAGE_MAX];
static char *page_cells[PAGE_MAX][OB_MAX_FIELDS];

/* A query's rows in the datasheet (core 0.5, design 5): read only */
static ob_query qy;                /* the query shown, or the last one designed */
static int q_on;                   /* the datasheet shows qy's rows, not cur's */
static ob_qresult q_cols[OB_QUERY_OUT];
static int q_ncols;
static char q_answers[OB_QUERY_PARAMS][OB_VALUE_MAX];
static const char *q_pv[OB_QUERY_PARAMS];

/* The datasheet's box in the window. */
static int ds_x, ds_y, ds_w, ds_h, ds_rows;

/* ---- Small helpers ---- */

static void put_text(char *dst, size_t n, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    if (!n) return;
    if (len >= n) len = n - 1;
    if (len) memcpy(dst, src, len);
    dst[len] = 0;
}

/* ARexx (amicradle-teams ARexx-RULE.md): while a command runs no requester shows; what it would have said is
 * kept, and a failing command's reason is LASTERROR's answer. */
static int rexx_busy, rexx_noted;
static char rexx_noting[300], rexx_lasterror[300];

static int rexx_quiet(const char *text)
{
    if (!rexx_busy) return 0;
    if (!rexx_noted) { put_text(rexx_noting, sizeof rexx_noting, text); rexx_noted = 1; }
    return 1;
}

static void tell(const char *text)
{
    if (rexx_quiet(text)) return;
    {
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
    EasyRequest(win, &es, NULL, (ULONG)text);
    }
}

static int ask(const char *text, const char *buttons)
{
    if (rexx_quiet(text)) return 0;                  /* the last button: Cancel */
    {
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)buttons };
    return EasyRequest(win, &es, NULL, (ULONG)text);
    }
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
    if (proj_path[0]) {
        const char *f = leaf(proj_path);
        size_t n = (size_t)(f - proj_path);
        if (n < sizeof dir) { memcpy(dir, proj_path, n); dir[n] = 0; }
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

/* ---- The tables and the datasheet ---- */

static void free_page(void)
{
    int i, j;
    for (i = 0; i < page_n; i++)
        for (j = 0; j < OB_MAX_FIELDS; j++) {
            free(page_cells[i][j]);
            page_cells[i][j] = NULL;
        }
    page_n = 0;
}

static int page_cb(void *user, ob_int key, int n, const char *const *values)
{
    int j;
    (void)user;
    if (page_n >= PAGE_MAX) return 1;
    page_keys[page_n] = key;
    for (j = 0; j < n && j < OB_MAX_FIELDS; j++) {
        if (values[j]) {
            size_t len = strlen(values[j]);
            if (len > 120) len = 120;
            if ((page_cells[page_n][j] = malloc(len + 1)) != NULL) {
                memcpy(page_cells[page_n][j], values[j], len);
                page_cells[page_n][j][len] = 0;
            }
        }
    }
    page_n++;
    return 0;
}

static void load_page(void)
{
    free_page();
    if (proj && q_on) {
        total_rows = all_rows = ob_query_total(proj, &qy, q_pv);
        if (total_rows < 0) {
            static char why[OB_ERR_MAX];
            put_text(why, sizeof why, ob_error(proj));
            q_on = 0;
            total_rows = all_rows = 0;
            set_status(why);
        } else {
            if (top_row > total_rows - ds_rows) top_row = total_rows - ds_rows;
            if (top_row < 0) top_row = 0;
            if (sel_row >= total_rows) sel_row = (int)total_rows - 1;
            ob_query_rows(proj, &qy, q_pv, top_row, ds_rows < PAGE_MAX ? ds_rows : PAGE_MAX, page_cb, NULL);
            return;
        }
    }
    if (!proj || !have_cur) return;
    if (strcmp(view_table, cur.name)) {
        ob_view_init(&view);
        put_text(view_table, sizeof view_table, cur.name);
    }
    all_rows = ob_row_total(proj, cur.name);
    if (all_rows < 0) all_rows = 0;
    total_rows = ob_view_total(proj, cur.name, &view);
    if (total_rows < 0) {
        /* a field it sorted or filtered by has gone or changed type */
        ob_view_init(&view);
        set_status("The sort or filter no longer fits the table: every row is shown");
        total_rows = all_rows;
    }
    if (top_row > total_rows - ds_rows) top_row = total_rows - ds_rows;
    if (top_row < 0) top_row = 0;
    if (sel_row >= total_rows) sel_row = (int)total_rows - 1;
    ob_view_rows(proj, cur.name, &view, top_row, ds_rows < PAGE_MAX ? ds_rows : PAGE_MAX, page_cb, NULL);
}

static void free_tables(void)
{
    free(table_nodes);
    table_nodes = NULL;
    table_count = 0;
    NewList(&table_list);
}

/* The table list read again; the chosen table kept by name when it is still there. */
static void load_tables(const char *choose)
{
    int i;
    char keep[OB_NAME_MAX] = "";
    if (choose) put_text(keep, sizeof keep, choose);
    else if (have_cur) put_text(keep, sizeof keep, cur.name);
    if (win && g_tables) GT_SetGadgetAttrs(g_tables, win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    free_tables();
    table_sel = -1;
    have_cur = 0;
    if (proj) {
        table_count = ob_table_count(proj);
        if (table_count > 0 && (table_nodes = calloc((size_t)table_count, sizeof *table_nodes)) != NULL) {
            for (i = 0; i < table_count; i++) {
                table_nodes[i].ln_Name = (char *)ob_table_name(proj, i);
                AddTail(&table_list, &table_nodes[i]);
                if (keep[0] && !strcmp(keep, ob_table_name(proj, i))) table_sel = i;
            }
        } else {
            table_count = 0;
        }
        if (table_sel < 0 && table_count > 0) table_sel = 0;
        if (table_sel >= 0) have_cur = ob_table_get(proj, ob_table_name(proj, table_sel), &cur);
    }
    if (win && g_tables)
        GT_SetGadgetAttrs(g_tables, win, NULL, GTLV_Labels, (ULONG)&table_list,
                          GTLV_Selected, table_sel >= 0 ? (ULONG)table_sel : ~0UL, TAG_DONE);
    if (!have_cur || !choose) { top_row = 0; sel_row = -1; left_col = 0; }
    load_page();
}

/* A field's width in the datasheet, in characters. */
static int col_chars(const ob_field *f)
{
    switch (f->type) {
    case OB_COUNTER: return 6;
    case OB_INTEGER: case OB_LOOKUP: return 8;
    case OB_NUMBER: return 10;
    case OB_CURRENCY: return 11;
    case OB_DATE: return 10;
    case OB_TIME: return 8;
    case OB_DATETIME: return 19;
    case OB_YESNO: return 4;
    case OB_CHOICE: return 10;
    case OB_OBJECT: return 12;
    case OB_TEXT: return f->max_len > 0 && f->max_len < 24 ? (f->max_len < 6 ? 6 : f->max_len) : 24;
    default: return 24;
    }
}

static int col_width(const ob_field *f)
{
    int n = col_chars(f), l = (int)strlen(f->label[0] ? f->label : f->name);
    if (l > n) n = l < 24 ? l : 24;
    return n * fw + 8;
}

/* Text cut to fit w pixels, drawn at x, y (the top). */
static void text_fit(struct RastPort *rp, int x, int y, int w, const char *s)
{
    struct TextExtent te;
    ULONG n;
    if (!s || !*s || w <= 0) return;
    n = TextFit(rp, (STRPTR)s, strlen(s), &te, NULL, 1, w, fh + 1);
    if (!n) return;
    Move(rp, x, y + rp->TxBaseline);
    Text(rp, (STRPTR)s, n);
}

/* The datasheet's columns: the table's fields, or the query's result. */
static int ds_ncols(void)
{
    if (q_on) return q_ncols < OB_MAX_FIELDS ? q_ncols : OB_MAX_FIELDS;
    return have_cur ? cur.nfields : 0;
}

static ob_field q_tmpf;            /* a result column's width, worked out as a field's */

static int ds_colw(int j)
{
    if (!q_on) return col_width(&cur.fields[j]);
    memset(&q_tmpf, 0, sizeof q_tmpf);
    q_tmpf.type = q_cols[j].type >= 0 ? (ob_type)q_cols[j].type : OB_TEXT;
    if (q_cols[j].type < 0) q_tmpf.max_len = 14;
    put_text(q_tmpf.name, sizeof q_tmpf.name, q_cols[j].heading);
    return col_width(&q_tmpf);
}

static const char *ds_head(int j)
{
    if (q_on) return q_cols[j].heading;
    return cur.fields[j].label[0] ? cur.fields[j].label : cur.fields[j].name;
}

static UWORD pen(int which)
{
    struct DrawInfo *di = GetScreenDrawInfo(scr);
    UWORD p = di ? di->dri_Pens[which] : 1;
    if (di) FreeScreenDrawInfo(scr, di);
    return p;
}

static void draw_datasheet(void)
{
    struct RastPort *rp;
    int row_h = fh + 3, x, i, j, y;
    UWORD text_pen, back_pen, fill_pen, filltext_pen, shine_pen, shadow_pen;
    char line[128];
    if (!win) return;
    rp = win->RPort;
    SetFont(rp, font);
    text_pen = pen(TEXTPEN);
    back_pen = pen(BACKGROUNDPEN);
    fill_pen = pen(FILLPEN);
    filltext_pen = pen(FILLTEXTPEN);
    shine_pen = pen(SHINEPEN);
    shadow_pen = pen(SHADOWPEN);
    SetDrMd(rp, JAM1);
    SetAPen(rp, back_pen);
    RectFill(rp, ds_x, ds_y, ds_x + ds_w - 1, ds_y + ds_h - 1);
    /* the frame */
    SetAPen(rp, shadow_pen);
    Move(rp, ds_x, ds_y + ds_h - 1); Draw(rp, ds_x, ds_y); Draw(rp, ds_x + ds_w - 1, ds_y);
    SetAPen(rp, shine_pen);
    Draw(rp, ds_x + ds_w - 1, ds_y + ds_h - 1); Draw(rp, ds_x, ds_y + ds_h - 1);
    if (!proj) {
        SetAPen(rp, text_pen);
        text_fit(rp, ds_x + 8, ds_y + 8, ds_w - 16, "No project open: Project > New... or Open...");
        goto status_line;
    }
    if (!have_cur && !q_on) {
        SetAPen(rp, text_pen);
        text_fit(rp, ds_x + 8, ds_y + 8, ds_w - 16, table_count ? "Choose a table" : "No tables yet: Table > New Table...");
        goto status_line;
    }
    /* the heading */
    x = ds_x + 4;
    y = ds_y + 2;
    SetAPen(rp, fill_pen);
    RectFill(rp, ds_x + 1, ds_y + 1, ds_x + ds_w - 2, ds_y + row_h);
    for (j = left_col; j < ds_ncols() && x < ds_x + ds_w - 4; j++) {
        int w = ds_colw(j);
        SetAPen(rp, filltext_pen);
        if (!q_on && !strcmp(view.sort, cur.fields[j].name)) {
            /* the column the rows are sorted by: its name and an arrow */
            snprintf(line, sizeof line, "%.100s %s", ds_head(j), view.desc ? "v" : "^");
            text_fit(rp, x, y, (x + w - 6 < ds_x + ds_w - 4 ? w - 6 : ds_x + ds_w - 4 - x), line);
        } else {
            text_fit(rp, x, y, (x + w - 6 < ds_x + ds_w - 4 ? w - 6 : ds_x + ds_w - 4 - x), ds_head(j));
        }
        x += w;
    }
    /* the rows */
    for (i = 0; i < page_n && i < ds_rows; i++) {
        int sel = top_row + i == sel_row;
        y = ds_y + 2 + row_h * (i + 1);
        if (sel) {
            SetAPen(rp, fill_pen);
            RectFill(rp, ds_x + 1, y - 1, ds_x + ds_w - 2, y + row_h - 2);
        }
        x = ds_x + 4;
        for (j = left_col; j < ds_ncols() && x < ds_x + ds_w - 4; j++) {
            int w = ds_colw(j);
            SetAPen(rp, sel ? filltext_pen : text_pen);
            text_fit(rp, x, y, (x + w - 6 < ds_x + ds_w - 4 ? w - 6 : ds_x + ds_w - 4 - x),
                     page_cells[i][j] ? page_cells[i][j] : "");
            x += w;
        }
        SetAPen(rp, shine_pen);
        Move(rp, ds_x + 1, y + row_h - 2);
        Draw(rp, ds_x + ds_w - 2, y + row_h - 2);
    }
    if (!page_n) {
        SetAPen(rp, text_pen);
        text_fit(rp, ds_x + 8, ds_y + 2 + row_h + 4, ds_w - 16,
                 q_on ? "The query gives no rows" :
                 all_rows ? "No rows pass the filter: Table > Show All Rows" : "No rows yet: Row > Add Row...");
    }
status_line:
    /* the status line, under the datasheet */
    SetAPen(rp, back_pen);
    RectFill(rp, ds_x, ds_y + ds_h + 2, ds_x + ds_w - 1, ds_y + ds_h + 3 + fh);
    SetAPen(rp, text_pen);
    if (status[0]) {
        text_fit(rp, ds_x + 2, ds_y + ds_h + 3, ds_w - 4, status);
    } else if (q_on) {
        snprintf(line, sizeof line, "Query %.40s: %lld rows, read only", qy.name[0] ? qy.name : "(not saved)", total_rows);
        if (sel_row >= 0) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof line - n, ", row %d", sel_row + 1);
        }
        text_fit(rp, ds_x + 2, ds_y + ds_h + 3, ds_w - 4, line);
    } else if (have_cur) {
        if (ob_view_filtered(&view))
            snprintf(line, sizeof line, "%.40s: %lld of %lld rows (filtered), %d fields", cur.name, total_rows, all_rows, cur.nfields);
        else
            snprintf(line, sizeof line, "%.40s: %lld rows, %d fields", cur.name, total_rows, cur.nfields);
        if (sel_row >= 0) {
            size_t n = strlen(line);
            snprintf(line + n, sizeof line - n, ", row %d", sel_row + 1);
        }
        text_fit(rp, ds_x + 2, ds_y + ds_h + 3, ds_w - 4, line);
    }
}

static void update_scroller(void)
{
    if (!g_scroll || !win) return;
    GT_SetGadgetAttrs(g_scroll, win, NULL, GTSC_Top, (ULONG)top_row, GTSC_Total, (ULONG)total_rows,
                      GTSC_Visible, (ULONG)ds_rows, TAG_DONE);
}

static void update_title(void)
{
    static char title[160];
    if (!win) return;
    if (proj) {
        const char *t = ob_title(proj);
        snprintf(title, sizeof title, "GFOpenBase - %.140s", t && *t ? t : leaf(proj_path));
    } else {
        put_text(title, sizeof title, "GFOpenBase");
    }
    SetWindowTitles(win, (UBYTE *)title, (UBYTE *)~0);
}

static void refresh(void)
{
    update_scroller();
    draw_datasheet();
}

/* ---- The main window's gadgets ---- */

static void free_gadgets(void)
{
    if (glist) {
        if (win) RemoveGList(win, glist, -1);
        FreeGadgets(glist);
        glist = NULL;
    }
    g_tables = g_scroll = NULL;
}

static int make_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    int l = win->BorderLeft + 6, t = win->BorderTop + 6;
    int r = win->Width - win->BorderRight - 6, b = win->Height - win->BorderBottom - 6;
    int bh = fh + 6, lw = fw * 22 + 16, sw = 18, bw;
    if (lw > (r - l) / 3) lw = (r - l) / 3;
    free_gadgets();
    if (!(g = CreateContext(&glist))) return 0;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;

    /* the tables, with their three buttons under them */
    ng.ng_LeftEdge = l;
    ng.ng_TopEdge = t + fh + 4;
    ng.ng_Width = lw;
    ng.ng_Height = b - (t + fh + 4) - 3 * (bh + 4);
    ng.ng_GadgetText = (UBYTE *)"Tables";
    ng.ng_Flags = PLACETEXT_ABOVE;
    ng.ng_GadgetID = GID_TABLES;
    g = g_tables = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&table_list,
                                GTLV_ShowSelected, 0, GTLV_Selected, table_sel >= 0 ? (ULONG)table_sel : ~0UL, TAG_DONE);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_Height = bh;
    ng.ng_TopEdge = b - 3 * (bh + 4) + 4;
    ng.ng_GadgetText = (UBYTE *)"New Table...";
    ng.ng_GadgetID = GID_T_NEW;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_TopEdge += bh + 4;
    ng.ng_GadgetText = (UBYTE *)"Design...";
    ng.ng_GadgetID = GID_T_DESIGN;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_TopEdge += bh + 4;
    ng.ng_GadgetText = (UBYTE *)"Delete Table...";
    ng.ng_GadgetID = GID_T_DELETE;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);

    /* the datasheet, its scroller and the row buttons */
    ds_x = l + lw + 10;
    ds_y = t + fh + 4;
    ds_w = r - ds_x - sw;
    ds_h = b - ds_y - bh - 8 - fh - 6;
    ds_rows = (ds_h - 4) / (fh + 3) - 1;
    if (ds_rows < 1) ds_rows = 1;
    if (ds_rows > PAGE_MAX) ds_rows = PAGE_MAX;
    ng.ng_LeftEdge = ds_x + ds_w;
    ng.ng_TopEdge = ds_y;
    ng.ng_Width = sw;
    ng.ng_Height = ds_h;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = GID_SCROLL;
    g = g_scroll = CreateGadget(SCROLLER_KIND, g, &ng, GTSC_Top, (ULONG)top_row, GTSC_Total, (ULONG)total_rows,
                                GTSC_Visible, (ULONG)ds_rows, GTSC_Arrows, fh + 2, PGA_Freedom, LORIENT_VERT,
                                GA_RelVerify, TRUE, GA_Immediate, TRUE, TAG_DONE);
    bw = (ds_w + sw - 2 * 8) / 3;
    ng.ng_TopEdge = b - bh;
    ng.ng_Height = bh;
    ng.ng_Width = bw;
    ng.ng_LeftEdge = ds_x;
    ng.ng_GadgetText = (UBYTE *)"Add Row...";
    ng.ng_GadgetID = GID_R_ADD;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += bw + 8;
    ng.ng_GadgetText = (UBYTE *)"Edit Row...";
    ng.ng_GadgetID = GID_R_EDIT;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += bw + 8;
    ng.ng_GadgetText = (UBYTE *)"Delete Row...";
    ng.ng_GadgetID = GID_R_DELETE;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) return 0;
    AddGList(win, glist, ~0, -1, NULL);
    RefreshGList(glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
    return 1;
}

static void relayout(void)
{
    free_gadgets();
    SetAPen(win->RPort, pen(BACKGROUNDPEN));
    RectFill(win->RPort, win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1,
             win->Height - win->BorderBottom - 1);
    make_gadgets();
    RefreshWindowFrame(win);
    load_page();
    refresh();
}

/* ---- Projects ---- */

static void close_project(void)
{
    free_page();
    if (win && g_tables) GT_SetGadgetAttrs(g_tables, win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
    free_tables();
    if (win && g_tables) GT_SetGadgetAttrs(g_tables, win, NULL, GTLV_Labels, (ULONG)&table_list, TAG_DONE);
    have_cur = 0;
    q_on = 0;
    ob_query_init(&qy, "");
    table_sel = -1;
    if (proj) ob_close(proj);
    proj = NULL;
    proj_path[0] = 0;
    total_rows = top_row = 0;
    sel_row = -1;
}

static int open_project(const char *path, int create)
{
    char err[OB_ERR_MAX], full[520];
    ob_project *p;
    put_text(full, sizeof full, path);
    if (create && !strchr(leaf(full), '.') && strlen(full) + 6 < sizeof full) strcat(full, ".oadb");
    p = ob_open(full, create ? OB_OPEN_CREATE : 0, err, sizeof err);
    if (!p && !create && strstr(err, "is an SQLite database")) {
        char q[400];
        snprintf(q, sizeof q, "%.250s\n\nOpen it as a GFOpenBase project? Its tables stay as they are;\n"
                 "GFOpenBase adds its own design tables beside them.", err);
        if (ask(q, "Open as a project|Cancel") == 1) p = ob_open(full, OB_OPEN_ADOPT, err, sizeof err);
        else return 0;
    }
    if (!p) {
        tell(err);
        return 0;
    }
    close_project();
    proj = p;
    put_text(proj_path, sizeof proj_path, full);
    load_tables(NULL);
    update_title();
    set_status("");
    refresh();
    return 1;
}

/* ---- The row editor: one line a field ---- */

static int whole_cb(void *user, ob_int key, int n, const char *const *v)
{
    char **values = user;
    int i;
    (void)key;
    for (i = 0; i < n; i++) put_text(values[i], 1024, v[i] ? v[i] : "");
    return 1;
}

#define ED_MAX 24          /* fields on one page of the editor */

/* The row editor for table tb: the datasheet's (tb == &cur), or a form's
 * subform, whose Lookup rel->field holds the form's record `parent`: shown,
 * not typed. */
static void edit_row_of(const ob_table *tb, ob_int key, int adding, const ob_relation *rel, ob_int parent)
{
    struct Window *ew;
    struct Gadget *eg = NULL, *g, *fields_g[ED_MAX] = { 0 };
    struct NewGadget ng;
    char *values[OB_MAX_FIELDS], *before[OB_MAX_FIELDS], title[96], pk[24];
    int i, page = 0, pages, per, done = 0, lw = 0, vw, h, row_h = fh + 8, nshow, first;
    memset(values, 0, sizeof values);
    memset(before, 0, sizeof before);
    snprintf(pk, sizeof pk, "%lld", (long long)parent);
    if (rel) snprintf(title, sizeof title, adding ? "Add Row to %.63s" : "Edit Row of %.63s", tb->name);
    else put_text(title, sizeof title, adding ? "Add Row" : "Edit Row");
    for (i = 0; i < tb->nfields; i++) {
        const char *v = "";
        int n = (int)strlen(tb->fields[i].label[0] ? tb->fields[i].label : tb->fields[i].name);
        if (n > lw) lw = n;
        if (adding && rel && !strcmp(tb->fields[i].name, rel->field)) v = pk;
        else if (adding && tb->fields[i].has_default) v = tb->fields[i].deflt;
        values[i] = malloc(1024);
        before[i] = malloc(1024);
        if (!values[i] || !before[i]) goto out;
        put_text(values[i], 1024, v);
        put_text(before[i], 1024, adding ? "" : v);
    }
    /* the datasheet keeps values cut short: read the row whole */
    if (!adding) {
        ob_rows(proj, tb->name, key - 1, 1, whole_cb, values);
        for (i = 0; i < tb->nfields; i++) put_text(before[i], 1024, values[i]);
    }
    per = (scr->Height - 120) / row_h;
    if (per > ED_MAX) per = ED_MAX;
    if (per < 4) per = 4;
    pages = (tb->nfields + per - 1) / per;
    lw = (lw > 24 ? 24 : lw) * fw + 16;
    vw = fw * 40;
    if (lw + vw + 40 > scr->Width) vw = scr->Width - lw - 40;
    nshow = tb->nfields < per ? tb->nfields : per;
    h = nshow * row_h + fh * 2 + 40;
    ew = OpenWindowTags(NULL,
        WA_Title, (ULONG)title,
        WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30,
        WA_InnerWidth, lw + vw + 20, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!ew) goto out;
    SetFont(ew->RPort, font);
    while (!done) {
        /* the page's gadgets */
        first = page * per;
        nshow = tb->nfields - first < per ? tb->nfields - first : per;
        if (!(g = CreateContext(&eg))) break;
        memset(&ng, 0, sizeof ng);
        ng.ng_VisualInfo = vi;
        ng.ng_TextAttr = &font_attr;
        for (i = 0; i < nshow; i++) {
            const ob_field *f = &tb->fields[first + i];
            ng.ng_LeftEdge = ew->BorderLeft + 10 + lw;
            ng.ng_TopEdge = ew->BorderTop + 8 + i * row_h;
            ng.ng_Width = vw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)(f->label[0] ? f->label : f->name);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_GadgetID = 100 + i;
            if (f->type == OB_COUNTER || f->type == OB_OBJECT || (rel && !strcmp(f->name, rel->field)))
                g = fields_g[i] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text,
                                               (ULONG)(f->type == OB_COUNTER && adding ? "(new)" : values[first + i]),
                                               GTTX_Border, TRUE, TAG_DONE);
            else
                g = fields_g[i] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)values[first + i],
                                               GTST_MaxChars, 1023, GA_TabCycle, TRUE, TAG_DONE);
        }
        ng.ng_Flags = PLACETEXT_IN;
        ng.ng_TopEdge = ew->BorderTop + 8 + nshow * row_h + 8;
        ng.ng_Height = fh + 6;
        ng.ng_Width = fw * 10 + 16;
        ng.ng_LeftEdge = ew->BorderLeft + 10;
        ng.ng_GadgetText = (UBYTE *)(adding ? "Add" : "Save");
        ng.ng_GadgetID = 1;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        if (pages > 1) {
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_GadgetText = (UBYTE *)"< Fields";
            ng.ng_GadgetID = 3;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, page == 0, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_GadgetText = (UBYTE *)"Fields >";
            ng.ng_GadgetID = 4;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, page == pages - 1, TAG_DONE);
        }
        ng.ng_LeftEdge = ew->Width - ew->BorderRight - 10 - ng.ng_Width;
        ng.ng_GadgetText = (UBYTE *)"Cancel";
        ng.ng_GadgetID = 2;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        if (!g) break;
        SetAPen(ew->RPort, pen(BACKGROUNDPEN));
        RectFill(ew->RPort, ew->BorderLeft, ew->BorderTop, ew->Width - ew->BorderRight - 1, ew->Height - ew->BorderBottom - 1);
        AddGList(ew, eg, ~0, -1, NULL);
        RefreshGList(eg, ew, NULL, -1);
        GT_RefreshWindow(ew, NULL);
        for (i = 0; i < nshow; i++)
            if (fields_g[i] && tb->fields[first + i].type != OB_COUNTER && tb->fields[first + i].type != OB_OBJECT
                && !(rel && !strcmp(tb->fields[first + i].name, rel->field))) {
                ActivateGadget(fields_g[i], ew, NULL);
                break;
            }
        {
            int action = 0;     /* 1 save, 2 cancel, 3 back, 4 on */
            while (!action) {
                struct IntuiMessage *m;
                WaitPort(ew->UserPort);
                while (!action && (m = GT_GetIMsg(ew->UserPort)) != NULL) {
                    ULONG class = m->Class;
                    UWORD code = m->Code;
                    struct Gadget *gad = (struct Gadget *)m->IAddress;
                    GT_ReplyIMsg(m);
                    if (class == IDCMP_CLOSEWINDOW) action = 2;
                    else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(ew); GT_EndRefresh(ew, TRUE); }
                    else if (class == IDCMP_VANILLAKEY && code == 27) action = 2;
                    else if (class == IDCMP_GADGETUP && gad->GadgetID >= 1 && gad->GadgetID <= 4) action = gad->GadgetID;
                }
            }
            /* keep what was typed on this page */
            for (i = 0; i < nshow; i++) {
                const ob_field *f = &tb->fields[first + i];
                if (f->type != OB_COUNTER && f->type != OB_OBJECT && !(rel && !strcmp(f->name, rel->field))) {
                    STRPTR s = NULL;
                    GT_GetGadgetAttrs(fields_g[i], ew, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                    put_text(values[first + i], 1024, s ? (const char *)s : "");
                }
            }
            RemoveGList(ew, eg, -1);
            FreeGadgets(eg);
            eg = NULL;
            if (action == 3 && page > 0) page--;
            else if (action == 4 && page < pages - 1) page++;
            else if (action == 2) done = 1;
            else if (action == 1) {
                const char *names[OB_MAX_FIELDS], *vals[OB_MAX_FIELDS];
                int n = 0, good;
                for (i = 0; i < tb->nfields; i++) {
                    const ob_field *f = &tb->fields[i];
                    if (f->type == OB_COUNTER || f->type == OB_OBJECT) continue;
                    if (rel && !strcmp(f->name, rel->field)) continue;     /* the record's */
                    if (adding ? values[i][0] == 0 && !f->has_default : !strcmp(values[i], before[i])) continue;
                    names[n] = f->name;
                    vals[n] = values[i];
                    n++;
                }
                if (adding && rel) good = ob_child_add(proj, rel, parent, n, names, vals, &key);
                else if (adding) good = ob_row_add(proj, tb->name, n, names, vals, &key);
                else good = ob_row_change(proj, tb->name, key, n, names, vals);
                if (good) {
                    done = 1;
                    set_status(adding ? "Row added" : "Row saved");
                    if (adding && tb == &cur) {
                        load_page();
                        sel_row = (int)total_rows - 1;
                        top_row = total_rows - ds_rows;
                    }
                } else {
                    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s",
                                             (UBYTE *)"OK" };
                    EasyRequest(ew, &es, NULL, (ULONG)ob_error(proj));
                }
            }
        }
    }
    CloseWindow(ew);
    if (win && tb == &cur) ActivateWindow(win);     /* keys go to the main window again */
out:
    if (eg) FreeGadgets(eg);
    for (i = 0; i < OB_MAX_FIELDS; i++) { free(values[i]); free(before[i]); }
    load_page();
    refresh();
}

static void edit_row(int adding)
{
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    if (!proj || !have_cur) return;
    if (!adding && (sel_row < 0 || sel_row < top_row || sel_row >= top_row + page_n)) {
        tell("Choose a row first.");
        return;
    }
    edit_row_of(&cur, adding ? 0 : page_keys[sel_row - top_row], adding, NULL, 0);
}

static void delete_row(void)
{
    char q[200];
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    if (!proj || !have_cur) return;
    if (sel_row < top_row || sel_row >= top_row + page_n || sel_row < 0) { tell("Choose a row first."); return; }
    snprintf(q, sizeof q, "Delete row %d of %.63s?", sel_row + 1, cur.name);
    if (ask(q, "Delete|Cancel") != 1) return;
    if (!ob_row_delete(proj, cur.name, page_keys[sel_row - top_row])) tell(ob_error(proj));
    else set_status("Row deleted");
    load_page();
    refresh();
}

/* ---- The table designer ---- */

enum {
    D_TNAME = 1, D_FIELDS, D_FNAME, D_TYPE, D_REQUIRED, D_UNIQUE, D_KEY, D_MAXLEN, D_DEFAULT, D_LABEL, D_CHOICES,
    D_LOOKUP, D_ONDELETE, D_ADD, D_REMOVE, D_UP, D_DOWN, D_SQL, D_INDEXES, D_SAVE, D_CANCEL, D_COUNT
};

static const char *type_labels[OB_TYPE_COUNT + 1];
static const char *delete_labels[] = { "Refuse", "Delete them too", "Clear them", NULL };

/* The designer's working copy and its list. */
static ob_table dt;
static struct List dl;
static struct Node dnodes[OB_MAX_FIELDS];
static char dtext[OB_MAX_FIELDS][OB_NAME_MAX + 48];
static int dsel;

static void designer_list(void)
{
    int i;
    NewList(&dl);
    for (i = 0; i < dt.nfields; i++) {
        snprintf(dtext[i], sizeof dtext[i], "%s (%s%s%s)", dt.fields[i].name, ob_type_name(dt.fields[i].type),
                 dt.fields[i].key ? ", key" : "", dt.fields[i].required ? ", required" : "");
        dnodes[i].ln_Name = dtext[i];
        AddTail(&dl, &dnodes[i]);
    }
}

/* ---- Indexes (on the designer's working copy) ---- */

enum { X_LIST = 1, X_NAME, X_FIELDS, X_UNIQUE, X_ADD, X_REMOVE, X_DONE, X_COUNT };

static struct List xl;
static struct Node xnodes[OB_MAX_INDEXES];
static char xtext[OB_MAX_INDEXES][OB_NAME_MAX + 160];

static void index_list(void)
{
    int i;
    NewList(&xl);
    for (i = 0; i < dt.nindexes; i++) {
        char f[140];
        ob_index_fields_text(&dt.indexes[i], f, sizeof f);
        snprintf(xtext[i], sizeof xtext[i], "%s%s: %s", dt.indexes[i].name, dt.indexes[i].unique ? " (unique)" : "", f);
        xnodes[i].ln_Name = xtext[i];
        AddTail(&xl, &xnodes[i]);
    }
}

/* "Surname, First name" into x's fields; 0 when there are too many. */
static int index_set_fields(ob_index *x, const char *text)
{
    const char *a = text;
    int n = 0;
    char names[OB_INDEX_FIELDS][OB_NAME_MAX];
    for (;;) {
        const char *b = strchr(a, ','), *e;
        size_t len = b ? (size_t)(b - a) : strlen(a);
        while (len && (*a == ' ' || *a == '\t')) { a++; len--; }
        e = a + len;
        while (len && (e[-1] == ' ' || e[-1] == '\t')) { e--; len--; }
        if (len) {
            if (n >= OB_INDEX_FIELDS) return 0;
            if (len >= OB_NAME_MAX) len = OB_NAME_MAX - 1;
            memcpy(names[n], a, len);
            names[n++][len] = 0;
        }
        if (!b) break;
        a = b + 1;
    }
    memcpy(x->fields, names, sizeof names[0] * (size_t)n);
    x->nfields = n;
    return 1;
}

static void edit_indexes(struct Window *parent)
{
    struct Window *xw;
    struct Gadget *eg = NULL, *g, *gad[X_COUNT];
    struct NewGadget ng;
    int done = 0, rebuild = 1, xsel = dt.nindexes ? 0 : -1, rowh = fh + 8, l, t, w, h, lw = fw * 8;
    w = fw * 58;
    h = rowh * 10 + 16;
    if (w > scr->Width - 20) w = scr->Width - 20;
    xw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Indexes",
        WA_PubScreen, (ULONG)scr,
        WA_Left, parent->LeftEdge + 40, WA_Top, parent->TopEdge + 30,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | STRINGIDCMP | CHECKBOXIDCMP,
        TAG_DONE);
    if (!xw) return;
    SetFont(xw->RPort, font);
    l = xw->BorderLeft + 8;
    t = xw->BorderTop + 6;
    while (!done) {
        if (rebuild) {
            ob_index *x = xsel >= 0 && xsel < dt.nindexes ? &dt.indexes[xsel] : NULL;
            char ftext[OB_INDEX_FIELDS * (OB_NAME_MAX + 2)];
            int iw = xw->Width - xw->BorderLeft - xw->BorderRight - 16;
            if (eg) { RemoveGList(xw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            index_list();
            ftext[0] = 0;
            if (x) ob_index_fields_text(x, ftext, sizeof ftext);
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = vi;
            ng.ng_TextAttr = &font_attr;
            ng.ng_LeftEdge = l;
            ng.ng_TopEdge = t;
            ng.ng_Width = iw;
            ng.ng_Height = rowh * 5;
            ng.ng_GadgetID = X_LIST;
            g = gad[X_LIST] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&xl, GTLV_ShowSelected, 0,
                                           GTLV_Selected, xsel >= 0 ? (ULONG)xsel : ~0UL, TAG_DONE);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_Width = iw - lw;
            ng.ng_Height = fh + 6;
            ng.ng_TopEdge = t + rowh * 5 + 8;
            ng.ng_GadgetText = (UBYTE *)"Name";
            ng.ng_GadgetID = X_NAME;
            g = gad[X_NAME] = CreateGadget(STRING_KIND, g, &ng, GA_Disabled, x == NULL,
                                           GTST_String, (ULONG)(x ? x->name : ""), GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            ng.ng_TopEdge += rowh;
            ng.ng_GadgetText = (UBYTE *)"Fields";
            ng.ng_GadgetID = X_FIELDS;
            g = gad[X_FIELDS] = CreateGadget(STRING_KIND, g, &ng, GA_Disabled, x == NULL,
                                             GTST_String, (ULONG)ftext, GTST_MaxChars, (ULONG)sizeof ftext - 1, TAG_DONE);
            ng.ng_TopEdge += rowh;
            ng.ng_Width = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Unique";
            ng.ng_GadgetID = X_UNIQUE;
            g = gad[X_UNIQUE] = CreateGadget(CHECKBOX_KIND, g, &ng, GA_Disabled, x == NULL,
                                             GTCB_Checked, x ? x->unique : 0, GTCB_Scaled, TRUE, TAG_DONE);
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = xw->Height - xw->BorderBottom - 6 - (fh + 6);
            ng.ng_LeftEdge = l;
            ng.ng_Width = fw * 10 + 16;
            ng.ng_GadgetText = (UBYTE *)"Add";
            ng.ng_GadgetID = X_ADD;
            g = gad[X_ADD] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, dt.nindexes >= OB_MAX_INDEXES, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_GadgetText = (UBYTE *)"Remove";
            ng.ng_GadgetID = X_REMOVE;
            g = gad[X_REMOVE] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, x == NULL, TAG_DONE);
            ng.ng_LeftEdge = xw->Width - xw->BorderRight - 8 - ng.ng_Width;
            ng.ng_GadgetText = (UBYTE *)"Done";
            ng.ng_GadgetID = X_DONE;
            g = gad[X_DONE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(xw->RPort, pen(BACKGROUNDPEN));
            RectFill(xw->RPort, xw->BorderLeft, xw->BorderTop, xw->Width - xw->BorderRight - 1,
                     xw->Height - xw->BorderBottom - 1);
            AddGList(xw, eg, ~0, -1, NULL);
            RefreshGList(eg, xw, NULL, -1);
            GT_RefreshWindow(xw, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(xw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(xw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                ob_index *x = xsel >= 0 && xsel < dt.nindexes ? &dt.indexes[xsel] : NULL;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(xw); GT_EndRefresh(xw, TRUE); continue; }
                if (class != IDCMP_GADGETUP && class != IDCMP_GADGETDOWN) continue;
                switch (gg->GadgetID) {
                case X_LIST:
                    xsel = code;
                    rebuild = 1;
                    break;
                case X_NAME: case X_FIELDS: {
                    STRPTR s = NULL;
                    if (!x) break;
                    GT_GetGadgetAttrs(gg, xw, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                    if (!s) s = (STRPTR)"";
                    if (gg->GadgetID == X_NAME) put_text(x->name, sizeof x->name, (const char *)s);
                    else if (!index_set_fields(x, (const char *)s)) {
                        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase",
                                                 (UBYTE *)"An index has at most %ld fields.", (UBYTE *)"OK" };
                        EasyRequest(xw, &es, NULL, (ULONG)OB_INDEX_FIELDS);
                    }
                    rebuild = 1;
                    break;
                }
                case X_UNIQUE:
                    if (x) { x->unique = (gg->Flags & GFLG_SELECTED) != 0; rebuild = 1; }
                    break;
                case X_ADD: {
                    char name[OB_NAME_MAX];
                    const char *field = dsel >= 0 && dsel < dt.nfields && dt.fields[dsel].type != OB_COUNTER
                                        ? dt.fields[dsel].name : dt.nfields > 1 ? dt.fields[1].name : dt.fields[0].name;
                    int k = dt.nindexes + 1, j, clash;
                    do {
                        snprintf(name, sizeof name, "%.38s by %.20s", dt.name, field);
                        if (k > dt.nindexes + 1) snprintf(name, sizeof name, "%.40s index %d", dt.name, k);
                        for (clash = 0, j = 0; j < dt.nindexes; j++) if (!strcmp(dt.indexes[j].name, name)) clash = 1;
                        k++;
                    } while (clash);
                    if (ob_table_add_index(&dt, name, 0, field)) xsel = dt.nindexes - 1;
                    rebuild = 1;
                    break;
                }
                case X_REMOVE:
                    if (x) {
                        ob_table_remove_index(&dt, xsel);
                        if (xsel >= dt.nindexes) xsel = dt.nindexes - 1;
                        rebuild = 1;
                    }
                    break;
                case X_DONE:
                    done = 1;
                    break;
                }
            }
        }
    }
    if (eg) { RemoveGList(xw, eg, -1); FreeGadgets(eg); }
    CloseWindow(xw);
}

/* ---- The relationships view ----
 * Each table a box (its name, then its fields, the key's in bold), each
 * relationship a line from the Lookup field to the table it looks up.
 * Drawn here too (core 0.11.0): drag a table's key onto another table's
 * field to make that field look it up, or onto the table's name to add a
 * field that does; click a line to change what deleting does, or to
 * remove it. The table designer's Looks up does the same. */

#define RV_FIELDS 10        /* fields shown in a box */

static ob_table rv_t;       /* one table's design at a time (too big for the stack) */
static struct TextExtent rv_te;   /* TextFit writes its answer here: never pass it NULL */

static int same_name(const char *a, const char *b)
{
    while (*a && *b) {
        int x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == *b;
}

struct rv_box {
    char name[OB_NAME_MAX];
    int x, y, w, h, shown;
    int nfields;
    char fields[RV_FIELDS][OB_NAME_MAX];
    unsigned char keyed[RV_FIELDS];     /* in the key (drawn bold) */
    unsigned char counter[RV_FIELDS];   /* the Counter: what a Lookup holds */
};

/* A relationship's line as drawn: four points, three straight parts */
struct rv_line { int rel; WORD x[4], y[4]; };

struct rv_view {
    struct Window *w;
    struct rv_box *box;
    ob_relation *rel;
    struct rv_line *line;
    int nt, nr, nl, hidden, row_h, gap;
};

static void rv_free(struct rv_view *v)
{
    free(v->box);
    free(v->rel);
    free(v->line);
    v->box = NULL;
    v->rel = NULL;
    v->line = NULL;
    v->nt = v->nr = v->nl = 0;
}

/* The tables and relationships read again, and laid out in rows with room
 * between the columns for the lines; 0 with the reason told. */
static int rv_load(struct rv_view *v)
{
    struct Window *rw = v->w;
    int i, j, bw = fw * 18, cols, x, y, rowmax;
    rv_free(v);
    v->nt = ob_table_count(proj);
    v->nr = ob_relations(proj, NULL, 0);
    if (v->nr < 0) { v->nr = 0; tell(ob_error(proj)); return 0; }
    v->box = calloc((size_t)(v->nt ? v->nt : 1), sizeof *v->box);
    v->rel = calloc((size_t)(v->nr ? v->nr : 1), sizeof *v->rel);
    v->line = calloc((size_t)(v->nr ? v->nr : 1), sizeof *v->line);
    if (!v->box || !v->rel || !v->line) { rv_free(v); tell("Not enough memory."); return 0; }
    ob_relations(proj, v->rel, v->nr);
    v->gap = fw * 12;
    cols = (rw->Width - rw->BorderLeft - rw->BorderRight - v->gap) / (bw + v->gap);
    if (cols < 1) cols = 1;
    x = rw->BorderLeft + v->gap;
    y = rw->BorderTop + 10;
    rowmax = 0;
    v->hidden = 0;
    for (i = 0; i < v->nt; i++) {
        struct rv_box *b = &v->box[i];
        put_text(b->name, sizeof b->name, ob_table_name(proj, i));
        if (ob_table_get(proj, b->name, &rv_t)) {
            for (j = 0; j < rv_t.nfields && b->nfields < RV_FIELDS; j++) {
                put_text(b->fields[b->nfields], OB_NAME_MAX, rv_t.fields[j].name);
                b->keyed[b->nfields] = rv_t.fields[j].type == OB_COUNTER || rv_t.fields[j].key;
                b->counter[b->nfields] = rv_t.fields[j].type == OB_COUNTER;
                b->nfields++;
            }
            if (rv_t.nfields > RV_FIELDS) {
                put_text(b->fields[RV_FIELDS - 1], OB_NAME_MAX, "...");
                b->keyed[RV_FIELDS - 1] = b->counter[RV_FIELDS - 1] = 0;
            }
        }
        b->w = bw;
        b->h = v->row_h * (b->nfields + 1) + 6;
        if (i && i % cols == 0) { x = rw->BorderLeft + v->gap; y += rowmax + v->row_h * 2; rowmax = 0; }
        b->x = x;
        b->y = y;
        b->shown = y + b->h < rw->Height - rw->BorderBottom - v->row_h - 8;
        if (!b->shown) v->hidden++;
        if (b->h > rowmax) rowmax = b->h;
        x += bw + v->gap;
    }
    return 1;
}

static void rv_paint(struct rv_view *v)
{
    struct Window *rw = v->w;
    struct RastPort *rp = rw->RPort;
    int i, j, row_h = v->row_h, gap = v->gap;
    char line[200];
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    SetAPen(rp, pen(BACKGROUNDPEN));
    RectFill(rp, rw->BorderLeft, rw->BorderTop, rw->Width - rw->BorderRight - 1, rw->Height - rw->BorderBottom - 1);
    /* the boxes first */
    for (i = 0; i < v->nt; i++) {
        struct rv_box *b = &v->box[i];
        if (!b->shown) continue;
        SetAPen(rp, pen(BACKGROUNDPEN));
        RectFill(rp, b->x, b->y, b->x + b->w - 1, b->y + b->h - 1);
        SetAPen(rp, pen(FILLPEN));
        RectFill(rp, b->x, b->y, b->x + b->w - 1, b->y + row_h + 1);
        SetAPen(rp, pen(SHADOWPEN));
        Move(rp, b->x, b->y); Draw(rp, b->x + b->w - 1, b->y); Draw(rp, b->x + b->w - 1, b->y + b->h - 1);
        Draw(rp, b->x, b->y + b->h - 1); Draw(rp, b->x, b->y);
        SetAPen(rp, pen(FILLTEXTPEN));
        SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
        Move(rp, b->x + 4, b->y + 1 + rp->TxBaseline);
        Text(rp, (STRPTR)b->name, (ULONG)TextFit(rp, (STRPTR)b->name, strlen(b->name), &rv_te, NULL, 1,
                                                 b->w - 8, row_h + 2));
        SetSoftStyle(rp, 0, FSF_BOLD);
        SetAPen(rp, pen(TEXTPEN));
        for (j = 0; j < b->nfields; j++) {
            if (b->keyed[j]) SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
            Move(rp, b->x + 6, b->y + 3 + row_h * (j + 1) + rp->TxBaseline);
            Text(rp, (STRPTR)b->fields[j], (ULONG)TextFit(rp, (STRPTR)b->fields[j], strlen(b->fields[j]), &rv_te, NULL, 1,
                                                          b->w - 10, row_h));
            if (b->keyed[j]) SetSoftStyle(rp, 0, FSF_BOLD);
        }
    }
    /* then the lines, from box edge to box edge, each with its rule in the gap */
    v->nl = 0;
    for (i = 0; i < v->nr; i++) {
        struct rv_box *from = NULL, *to = NULL;
        struct rv_line *ln = &v->line[v->nl];
        int fy, tx, fx, ty, lx, ly, f = 0;
        for (j = 0; j < v->nt; j++) {
            if (same_name(v->box[j].name, v->rel[i].table)) from = &v->box[j];
            if (same_name(v->box[j].name, v->rel[i].lookup)) to = &v->box[j];
        }
        if (!from || !to || !from->shown || !to->shown) continue;
        for (j = 0; j < from->nfields; j++) if (same_name(from->fields[j], v->rel[i].field)) f = j + 1;
        fy = f ? from->y + 3 + row_h * f + row_h / 2 : from->y + from->h / 2;
        ty = to->y + row_h / 2 + 1;
        ln->rel = i;
        if (from == to) {
            /* a table that looks itself up: a loop on its right */
            fx = from->x + from->w;
            ln->x[0] = (WORD)fx; ln->y[0] = (WORD)fy;
            ln->x[1] = (WORD)(fx + gap / 2); ln->y[1] = (WORD)fy;
            ln->x[2] = (WORD)(fx + gap / 2); ln->y[2] = (WORD)ty;
            ln->x[3] = (WORD)fx; ln->y[3] = (WORD)ty;
            lx = fx + gap / 2 + 3;
            ly = (fy + ty) / 2 + rp->TxBaseline / 2;
        } else {
            if (to->x >= from->x + from->w) { fx = from->x + from->w; tx = to->x; }
            else if (to->x + to->w <= from->x) { fx = from->x; tx = to->x + to->w; }
            else { fx = from->x + from->w; tx = to->x + to->w; }
            ln->x[0] = (WORD)fx; ln->y[0] = (WORD)fy;
            ln->x[3] = (WORD)tx; ln->y[3] = (WORD)ty;
            if (fx == tx || (fx == from->x + from->w && tx == to->x + to->w)) {
                /* one above the other: out to the side and back */
                int side = (fx > tx ? fx : tx) + gap / 2;
                ln->x[1] = (WORD)side; ln->y[1] = (WORD)fy;
                ln->x[2] = (WORD)side; ln->y[2] = (WORD)ty;
                lx = side + 3;
                ly = (fy + ty) / 2 + rp->TxBaseline / 2;
            } else {
                int mid = (fx + tx) / 2;
                ln->x[1] = (WORD)mid; ln->y[1] = (WORD)fy;
                ln->x[2] = (WORD)mid; ln->y[2] = (WORD)ty;
                lx = -1;
                ly = ty - 3;
            }
        }
        SetAPen(rp, pen(FILLPEN));
        Move(rp, ln->x[0], ln->y[0]);
        for (j = 1; j < 4; j++) Draw(rp, ln->x[j], ln->y[j]);
        v->nl++;
        /* the rule, in the gap, just above the line at the looked-up table */
        SetAPen(rp, pen(TEXTPEN));
        {
            const char *rule = v->rel[i].on_delete == OB_DELETE_CASCADE ? "cascade"
                             : v->rel[i].on_delete == OB_DELETE_CLEAR ? "clear" : "refuse";
            int tw = TextLength(rp, (STRPTR)rule, strlen(rule));
            Move(rp, lx >= 0 ? lx : (ln->x[0] + ln->x[3]) / 2 - tw / 2, ly);
            Text(rp, (STRPTR)rule, strlen(rule));
        }
    }
    if (!v->nt) snprintf(line, sizeof line, "No tables yet: Table > New Table...");
    else if (!v->nr) snprintf(line, sizeof line, "No relationships yet: drag a table's key (bold) onto a field of another table, or onto its name");
    else snprintf(line, sizeof line, "%d table%s, %d relationship%s. Drag a key (bold) onto a field to relate them; click a line to change it.",
                  v->nt, v->nt == 1 ? "" : "s", v->nr, v->nr == 1 ? "" : "s");
    if (v->hidden) {
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, " %d don't fit.", v->hidden);
    }
    SetAPen(rp, pen(TEXTPEN));
    Move(rp, rw->BorderLeft + 8, rw->Height - rw->BorderBottom - 6 - fh + rp->TxBaseline);
    Text(rp, (STRPTR)line, (ULONG)TextFit(rp, (STRPTR)line, strlen(line), &rv_te, NULL, 1,
                                          rw->Width - rw->BorderLeft - rw->BorderRight - 16, fh + 2));
}

/* What is at x, y: a box's field (*field from 0), its name (*field -1), or
 * nothing (-1 returned). "..." (fields not shown) is nothing. */
static int rv_box_at(const struct rv_view *v, int x, int y, int *field)
{
    int i;
    for (i = 0; i < v->nt; i++) {
        const struct rv_box *b = &v->box[i];
        int row;
        if (!b->shown || x < b->x || x >= b->x + b->w || y < b->y || y >= b->y + b->h) continue;
        row = y < b->y + v->row_h + 2 ? -1 : (y - b->y - 3) / v->row_h - 1;
        if (row >= b->nfields) row = b->nfields - 1;
        if (row >= 0 && !strcmp(b->fields[row], "...")) return -1;
        *field = row;
        return i;
    }
    return -1;
}

/* The relationship whose line passes within 3 pixels of x, y; -1 none */
static int rv_line_at(const struct rv_view *v, int x, int y)
{
    int i, j;
    for (i = 0; i < v->nl; i++)
        for (j = 0; j < 3; j++) {
            int x0 = v->line[i].x[j], y0 = v->line[i].y[j], x1 = v->line[i].x[j + 1], y1 = v->line[i].y[j + 1];
            int lo, hi;
            if (y0 == y1) {
                lo = x0 < x1 ? x0 : x1; hi = x0 < x1 ? x1 : x0;
                if (x >= lo - 3 && x <= hi + 3 && y >= y0 - 3 && y <= y0 + 3) return v->line[i].rel;
            } else {
                lo = y0 < y1 ? y0 : y1; hi = y0 < y1 ? y1 : y0;
                if (y >= lo - 3 && y <= hi + 3 && x >= x0 - 3 && x <= x0 + 3) return v->line[i].rel;
            }
        }
    return -1;
}

static void rv_rubber(struct Window *rw, int x0, int y0, int x1, int y1)
{
    struct RastPort *rp = rw->RPort;
    SetDrMd(rp, COMPLEMENT);
    Move(rp, x0, y0);
    Draw(rp, x1, y1);
    SetDrMd(rp, JAM1);
}

/* A key dropped: which field looks up which table, the rule asked, made. 1 when it changed. */
static int rv_drop(struct rv_view *v, int fb, int ff, int tb, int tf)
{
    const struct rv_box *key, *holder;
    const char *field;
    char q[400], made[OB_NAME_MAX];
    int r;
    if (tb < 0) return 0;
    if (ff >= 0 && v->box[fb].counter[ff] && !(tb == fb && tf == ff)) {
        key = &v->box[fb]; holder = &v->box[tb]; field = tf >= 0 ? holder->fields[tf] : NULL;
    } else if (tf >= 0 && v->box[tb].counter[tf] && ff >= 0) {
        key = &v->box[tb]; holder = &v->box[fb]; field = holder->fields[ff];
    } else {
        tell("Drag a table's key (in bold) onto the field that will hold it, or onto a table's name to add one.");
        return 0;
    }
    if (field) snprintf(q, sizeof q, "%.60s.%.60s will hold a row of %.60s.\n\nWhen a row of %.60s is deleted, its rows in %.60s are:",
                        holder->name, field, key->name, key->name, holder->name);
    else snprintf(q, sizeof q, "%.60s gets a field that holds a row of %.60s.\n\nWhen a row of %.60s is deleted, its rows in %.60s are:",
                  holder->name, key->name, key->name, holder->name);
    r = ask(q, "Refused|Deleted Too|Cleared|Cancel");
    if (!r) return 0;
    if (!ob_relation_add(proj, holder->name, field, key->name,
                         r == 2 ? OB_DELETE_CASCADE : r == 3 ? OB_DELETE_CLEAR : OB_DELETE_REFUSE, made, sizeof made)) {
        tell(ob_error(proj));
        return 0;
    }
    return 1;
}

/* A line clicked: its rule changed, or the relationship removed. 1 when it changed. */
static int rv_edit(struct rv_view *v, int i)
{
    const ob_relation *r = &v->rel[i];
    char q[400], table[OB_NAME_MAX], field[OB_NAME_MAX];
    int a, good;
    snprintf(q, sizeof q, "%.60s.%.60s holds a row of %.60s. When that row is deleted, its rows are %s.\n\nChange that to:",
             r->table, r->field, r->lookup,
             r->on_delete == OB_DELETE_CASCADE ? "deleted too" : r->on_delete == OB_DELETE_CLEAR ? "cleared" : "refused");
    a = ask(q, "Refused|Deleted Too|Cleared|Remove It|Cancel");
    if (!a) return 0;
    put_text(table, sizeof table, r->table);
    put_text(field, sizeof field, r->field);
    if (a == 4) {
        snprintf(q, sizeof q, "%.60s.%.60s will hold %.60s's keys as plain numbers, looking nothing up.", table, field, r->lookup);
        if (ask(q, "Remove|Cancel") != 1) return 0;
        good = ob_relation_remove(proj, table, field);
    } else
        good = ob_relation_set_rule(proj, table, field, a == 2 ? OB_DELETE_CASCADE : a == 3 ? OB_DELETE_CLEAR : OB_DELETE_REFUSE);
    if (!good) { tell(ob_error(proj)); return 0; }
    return 1;
}

static void show_relationships(void)
{
    struct rv_view v;
    struct Window *rw;
    int done = 0, w, h, changed = 0, dragging = 0, fb = -1, ff = -1, sx = 0, sy = 0, lx = 0, ly = 0;
    if (!proj) { tell("Open or make a project first."); return; }
    memset(&v, 0, sizeof v);
    v.row_h = fh + 2;
    w = scr->Width * 4 / 5;
    h = scr->Height * 3 / 4;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Relationships",
        WA_PubScreen, (ULONG)scr,
        WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2,
        WA_Width, w, WA_Height, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE, WA_ReportMouse, TRUE, WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE,
        TAG_DONE);
    if (!rw) { tell("The window could not be opened."); return; }
    v.w = rw;
    if (rv_load(&v)) rv_paint(&v);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(rw->UserPort);
        while ((m = GT_GetIMsg(rw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            int mx = m->MouseX, my = m->MouseY, again = 0;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && (code == 27 || code == 13))) done = 1;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); }
            else if (class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
                int bi = rv_box_at(&v, mx, my, &ff), li;
                if (bi >= 0 && ff >= 0) {           /* a field: drag it */
                    dragging = 1;
                    fb = bi;
                    sx = lx = mx;
                    sy = ly = my;
                    rv_rubber(rw, sx, sy, lx, ly);
                } else if (bi < 0 && (li = rv_line_at(&v, mx, my)) >= 0)
                    again = rv_edit(&v, li);
            } else if (class == IDCMP_MOUSEMOVE && dragging) {
                rv_rubber(rw, sx, sy, lx, ly);
                lx = mx;
                ly = my;
                rv_rubber(rw, sx, sy, lx, ly);
            } else if (class == IDCMP_MOUSEBUTTONS && code == SELECTUP && dragging) {
                int tf = -1, tb;
                rv_rubber(rw, sx, sy, lx, ly);
                dragging = 0;
                tb = rv_box_at(&v, mx, my, &tf);
                if (tb >= 0 && !(tb == fb && tf == ff)) again = rv_drop(&v, fb, ff, tb, tf);
            }
            if (again) {
                changed = 1;
                if (rv_load(&v)) rv_paint(&v);
            }
        }
    }
    CloseWindow(rw);
    rv_free(&v);
    if (changed) {         /* the table shown may have a Lookup now, or none */
        load_tables(NULL);
        load_page();
        refresh();
    }
    if (win) ActivateWindow(win);     /* keys go to the main window again */
}


static void design_table(int creating)
{
    struct Window *dw;
    struct Gadget *eg = NULL, *g, *gad[D_COUNT];
    struct NewGadget ng;
    char old_name[OB_NAME_MAX] = "";
    int done = 0, i, lw, l, t, rowh = fh + 8, colw, h, w, rebuild = 1;
    if (!proj) { tell("Open or make a project first."); return; }
    if (!creating && !have_cur) { tell("Choose a table first."); return; }
    for (i = 0; i < OB_TYPE_COUNT; i++) type_labels[i] = ob_type_name((ob_type)i);
    type_labels[OB_TYPE_COUNT] = NULL;
    if (creating) {
        char name[OB_NAME_MAX];
        snprintf(name, sizeof name, "Table%d", table_count + 1);
        ob_table_init(&dt, name);
    } else {
        dt = cur;
        put_text(old_name, sizeof old_name, cur.name);
    }
    dsel = dt.nfields > 1 ? 1 : 0;
    lw = fw * 12 + 8;
    colw = fw * 26;
    w = lw + colw + 20 + fw * 30 + 30;
    if (w > scr->Width - 20) w = scr->Width - 20;
    h = rowh * 15 + 20;
    if (h > scr->Height - 40) h = scr->Height - 40;
    dw = OpenWindowTags(NULL,
        WA_Title, (ULONG)(creating ? "New Table" : "Design Table"),
        WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 30, WA_Top, win->TopEdge + 20,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | STRINGIDCMP | CYCLEIDCMP | CHECKBOXIDCMP | INTEGERIDCMP,
        TAG_DONE);
    if (!dw) return;
    SetFont(dw->RPort, font);
    l = dw->BorderLeft + 8;
    t = dw->BorderTop + 6;
    while (!done) {
        if (rebuild) {
            const ob_field *f = dsel >= 0 && dsel < dt.nfields ? &dt.fields[dsel] : NULL;
            int px = l + lw + colw + 20 + fw * 10, pw = dw->Width - dw->BorderRight - 8 - px, y;
            if (eg) { RemoveGList(dw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            designer_list();
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = vi;
            ng.ng_TextAttr = &font_attr;
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_TopEdge = t;
            ng.ng_Width = colw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Table name";
            ng.ng_GadgetID = D_TNAME;
            g = gad[D_TNAME] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)dt.name, GTST_MaxChars, OB_NAME_MAX - 1,
                                            TAG_DONE);
            ng.ng_TopEdge = t + rowh + fh + 4;
            ng.ng_LeftEdge = l;
            ng.ng_Width = lw + colw;
            ng.ng_Height = h - (ng.ng_TopEdge - dw->BorderTop) - 2 * rowh - 20;
            ng.ng_GadgetText = (UBYTE *)"Fields";
            ng.ng_Flags = PLACETEXT_ABOVE;
            ng.ng_GadgetID = D_FIELDS;
            g = gad[D_FIELDS] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&dl, GTLV_ShowSelected, 0,
                                             GTLV_Selected, dsel >= 0 ? (ULONG)dsel : ~0UL,
                                             GTLV_MakeVisible, dsel >= 0 ? (ULONG)dsel : 0, TAG_DONE);
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge += ng.ng_Height + 6;
            ng.ng_Height = fh + 6;
            ng.ng_Width = (lw + colw - 3 * 6) / 4;
            {
                static const char *const names[4] = { "Add", "Remove", "Up", "Down" };
                int k;
                for (k = 0; k < 4; k++) {
                    ng.ng_GadgetText = (UBYTE *)names[k];
                    ng.ng_GadgetID = D_ADD + k;
                    g = gad[D_ADD + k] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled,
                                                      k == 1 ? (f == NULL || dt.nfields <= 1) : k >= 2 ? f == NULL : FALSE,
                                                      TAG_DONE);
                    ng.ng_LeftEdge += ng.ng_Width + 6;
                }
            }
            /* the chosen field's properties */
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = px;
            ng.ng_Width = pw;
            y = t + rowh + fh + 4;
#define PROP(id, kind, text, ...) do { ng.ng_TopEdge = y; ng.ng_GadgetText = (UBYTE *)(text); ng.ng_GadgetID = (id); \
            g = gad[id] = CreateGadget(kind, g, &ng, GA_Disabled, f == NULL, __VA_ARGS__, TAG_DONE); y += rowh; } while (0)
            PROP(D_FNAME, STRING_KIND, "Name", GTST_String, (ULONG)(f ? f->name : ""), GTST_MaxChars, OB_NAME_MAX - 1);
            PROP(D_TYPE, CYCLE_KIND, "Type", GTCY_Labels, (ULONG)type_labels, GTCY_Active, f ? (ULONG)f->type : 0);
            PROP(D_LABEL, STRING_KIND, "Label", GTST_String, (ULONG)(f ? f->label : ""), GTST_MaxChars, OB_LABEL_MAX - 1);
            ng.ng_Width = fh + 6;
            PROP(D_REQUIRED, CHECKBOX_KIND, "Required", GTCB_Checked, f ? f->required : 0, GTCB_Scaled, TRUE);
            PROP(D_UNIQUE, CHECKBOX_KIND, "Unique", GTCB_Checked, f ? f->unique : 0, GTCB_Scaled, TRUE);
            PROP(D_KEY, CHECKBOX_KIND, "Key", GTCB_Checked, f ? f->key : 0, GTCB_Scaled, TRUE);
            ng.ng_Width = pw;
            PROP(D_MAXLEN, INTEGER_KIND, "Max length", GTIN_Number, f ? (ULONG)f->max_len : 0, GTIN_MaxChars, 5);
            PROP(D_DEFAULT, STRING_KIND, "Default", GTST_String, (ULONG)(f && f->has_default ? f->deflt : ""),
                 GTST_MaxChars, OB_VALUE_MAX - 1);
            PROP(D_CHOICES, STRING_KIND, "Choices", GTST_String, (ULONG)(f ? f->choices : ""), GTST_MaxChars, OB_VALUE_MAX - 1);
            PROP(D_LOOKUP, STRING_KIND, "Looks up", GTST_String, (ULONG)(f ? f->lookup : ""), GTST_MaxChars, OB_NAME_MAX - 1);
            PROP(D_ONDELETE, CYCLE_KIND, "On delete", GTCY_Labels, (ULONG)delete_labels,
                 GTCY_Active, f ? (ULONG)f->on_delete : 0);
#undef PROP
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = dw->Height - dw->BorderBottom - 6 - (fh + 6);
            ng.ng_Width = fw * 10 + 16;
            ng.ng_LeftEdge = l;
            ng.ng_GadgetText = (UBYTE *)(creating ? "Create" : "Save");
            ng.ng_GadgetID = D_SAVE;
            g = gad[D_SAVE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_GadgetText = (UBYTE *)"Show SQL";
            ng.ng_GadgetID = D_SQL;
            g = gad[D_SQL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_GadgetText = (UBYTE *)"Indexes...";
            ng.ng_GadgetID = D_INDEXES;
            g = gad[D_INDEXES] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = dw->Width - dw->BorderRight - 8 - ng.ng_Width;
            ng.ng_GadgetText = (UBYTE *)"Cancel";
            ng.ng_GadgetID = D_CANCEL;
            g = gad[D_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(dw->RPort, pen(BACKGROUNDPEN));
            RectFill(dw->RPort, dw->BorderLeft, dw->BorderTop, dw->Width - dw->BorderRight - 1,
                     dw->Height - dw->BorderBottom - 1);
            AddGList(dw, eg, ~0, -1, NULL);
            RefreshGList(eg, dw, NULL, -1);
            GT_RefreshWindow(dw, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(dw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(dw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                ob_field *f = dsel >= 0 && dsel < dt.nfields ? &dt.fields[dsel] : NULL;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(dw); GT_EndRefresh(dw, TRUE); continue; }
                if (class == IDCMP_VANILLAKEY && code == 27) { done = 1; break; }
                if (class != IDCMP_GADGETUP && class != IDCMP_GADGETDOWN) continue;
                switch (gg->GadgetID) {
                case D_TNAME: {
                    STRPTR s = NULL;
                    GT_GetGadgetAttrs(gg, dw, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                    put_text(dt.name, sizeof dt.name, s ? (const char *)s : "");
                    break;
                }
                case D_FIELDS:
                    dsel = code;
                    rebuild = 1;
                    break;
                case D_FNAME: case D_LABEL: case D_DEFAULT: case D_CHOICES: case D_LOOKUP: {
                    STRPTR s = NULL;
                    if (!f) break;
                    GT_GetGadgetAttrs(gg, dw, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                    if (!s) s = (STRPTR)"";
                    if (gg->GadgetID == D_FNAME) {
                        char n[OB_NAME_MAX];
                        put_text(n, sizeof n, (const char *)s);
                        ob_table_rename_field(&dt, dsel, n);   /* its indexes follow */
                        rebuild = 1;
                    }
                    else if (gg->GadgetID == D_LABEL) put_text(f->label, sizeof f->label, (const char *)s);
                    else if (gg->GadgetID == D_DEFAULT) { put_text(f->deflt, sizeof f->deflt, (const char *)s); f->has_default = s[0] != 0; }
                    else if (gg->GadgetID == D_CHOICES) put_text(f->choices, sizeof f->choices, (const char *)s);
                    else put_text(f->lookup, sizeof f->lookup, (const char *)s);
                    break;
                }
                case D_TYPE:
                    if (f) { f->type = (ob_type)code; rebuild = 1; }
                    break;
                case D_ONDELETE:
                    if (f) f->on_delete = (ob_on_delete)code;
                    break;
                case D_REQUIRED:
                    if (f) { f->required = (gg->Flags & GFLG_SELECTED) != 0; rebuild = 1; }
                    break;
                case D_UNIQUE:
                    if (f) f->unique = (gg->Flags & GFLG_SELECTED) != 0;
                    break;
                case D_KEY:
                    if (f) { f->key = (gg->Flags & GFLG_SELECTED) != 0; rebuild = 1; }
                    break;
                case D_INDEXES:
                    edit_indexes(dw);
                    break;
                case D_MAXLEN: {
                    LONG n = 0;
                    if (!f) break;
                    GT_GetGadgetAttrs(gg, dw, NULL, GTIN_Number, (ULONG)&n, TAG_DONE);
                    f->max_len = (int)n;
                    break;
                }
                case D_ADD: {
                    char name[OB_NAME_MAX];
                    int k = dt.nfields + 1;
                    do snprintf(name, sizeof name, "Field%d", k++); while (ob_table_find_field(&dt, name) >= 0);
                    if (ob_table_add_field(&dt, name, OB_TEXT)) dsel = dt.nfields - 1;
                    rebuild = 1;
                    break;
                }
                case D_REMOVE:
                    if (f && dt.nfields > 1) {
                        ob_table_remove_field(&dt, dsel);   /* and from its indexes */
                        if (dsel >= dt.nfields) dsel = dt.nfields - 1;
                        rebuild = 1;
                    }
                    break;
                case D_UP: case D_DOWN: {
                    int to = dsel + (gg->GadgetID == D_UP ? -1 : 1);
                    if (f && to >= 0 && to < dt.nfields) {
                        ob_field tmp = dt.fields[to];
                        dt.fields[to] = dt.fields[dsel];
                        dt.fields[dsel] = tmp;
                        dsel = to;
                        rebuild = 1;
                    }
                    break;
                }
                case D_SQL: {
                    static char sql[4096];
                    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"SQL", (UBYTE *)"%s", (UBYTE *)"OK" };
                    if (!ob_table_check(proj, &dt)) put_text(sql, sizeof sql, ob_error(proj));
                    else if (ob_table_sql(&dt, NULL, sql, sizeof sql) < 0) put_text(sql, sizeof sql, "(too long to show)");
                    EasyRequest(dw, &es, NULL, (ULONG)sql);
                    break;
                }
                case D_SAVE: {
                    int good = creating ? ob_table_create(proj, &dt) : ob_table_change(proj, old_name, &dt);
                    if (good) {
                        done = 2;
                    } else {
                        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s",
                                                 (UBYTE *)"OK" };
                        EasyRequest(dw, &es, NULL, (ULONG)ob_error(proj));
                    }
                    break;
                }
                case D_CANCEL:
                    done = 1;
                    break;
                }
            }
        }
    }
    if (eg) { RemoveGList(dw, eg, -1); FreeGadgets(eg); }
    CloseWindow(dw);
    if (win) ActivateWindow(win);     /* keys go to the main window again */
    if (done == 2) {
        set_status(creating ? "Table created" : "Table saved");
        load_tables(dt.name);
    }
    refresh();
}

static void delete_table(void)
{
    char q[200];
    if (!proj || !have_cur) return;
    snprintf(q, sizeof q, "Delete the table %.63s and its %lld rows?\nThis can't be undone.", cur.name, total_rows);
    if (ask(q, "Delete|Cancel") != 1) return;
    if (!ob_table_drop(proj, cur.name)) tell(ob_error(proj));
    else { set_status("Table deleted"); load_tables(NULL); }
    refresh();
}

/* ---- Commands ---- */

/* ---- Sort and filter, CSV ---- */

enum { V_SORT = 1, V_DESC, V_FIELD, V_OP, V_VALUE, V_APPLY, V_ALL, V_CANCEL, V_COUNT };

static STRPTR v_sort_labels[OB_MAX_FIELDS + 2], v_field_labels[OB_MAX_FIELDS + 1], v_op_labels[OB_FILTER_COUNT + 1];

static void view_changed(void)
{
    top_row = 0;
    sel_row = -1;
    load_page();
}

/* Table > Sort and Filter...: the datasheet's view of the chosen table. */
static void edit_view(void)
{
    struct Window *vw;
    struct Gadget *eg = NULL, *g, *gad[V_COUNT];
    struct NewGadget ng;
    ob_view v = view;
    int done = 0, rebuild = 1, rowh = fh + 8, l, t, w, h, lw = fw * 9, i, sort_i = 0, field_i = 0;
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    if (!proj || !have_cur) return;
    for (i = 0; i < cur.nfields; i++) {
        STRPTR name = (STRPTR)(cur.fields[i].label[0] ? cur.fields[i].label : cur.fields[i].name);
        v_sort_labels[i + 1] = name;
        v_field_labels[i] = name;
        if (!strcmp(v.sort, cur.fields[i].name)) sort_i = i + 1;
        if (!strcmp(v.field, cur.fields[i].name)) field_i = i;
    }
    v_sort_labels[0] = (STRPTR)"Key order";
    v_sort_labels[cur.nfields + 1] = NULL;
    v_field_labels[cur.nfields] = NULL;
    for (i = 0; i < OB_FILTER_COUNT; i++) v_op_labels[i] = (STRPTR)ob_filter_name((ob_filter_op)i);
    v_op_labels[OB_FILTER_COUNT] = NULL;
    if (!v.field[0] && cur.nfields) {
        /* the first field after the Counter */
        field_i = cur.nfields > 1 ? 1 : 0;
        put_text(v.field, sizeof v.field, cur.fields[field_i].name);
    }
    w = fw * 46;
    h = rowh * 7 + 12;
    if (w > scr->Width - 20) w = scr->Width - 20;
    vw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Sort and Filter",
        WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  CYCLEIDCMP | STRINGIDCMP | CHECKBOXIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!vw) return;
    SetFont(vw->RPort, font);
    l = vw->BorderLeft + 8;
    t = vw->BorderTop + 6;
    while (!done) {
        if (rebuild) {
            int iw = vw->Width - vw->BorderLeft - vw->BorderRight - 16;
            int needs_value = v.op != OB_FILTER_NONE && v.op != OB_FILTER_EMPTY && v.op != OB_FILTER_NOT_EMPTY;
            if (eg) { RemoveGList(vw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = vi;
            ng.ng_TextAttr = &font_attr;
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_TopEdge = t;
            ng.ng_Width = iw - lw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Sort by";
            ng.ng_GadgetID = V_SORT;
            g = gad[V_SORT] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)v_sort_labels, GTCY_Active, (ULONG)sort_i, TAG_DONE);
            ng.ng_TopEdge += rowh;
            ng.ng_Width = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Z to A";
            ng.ng_GadgetID = V_DESC;
            g = gad[V_DESC] = CreateGadget(CHECKBOX_KIND, g, &ng, GA_Disabled, sort_i == 0,
                                           GTCB_Checked, v.desc, GTCB_Scaled, TRUE, TAG_DONE);
            ng.ng_TopEdge += rowh + 6;
            ng.ng_Width = iw - lw;
            ng.ng_GadgetText = (UBYTE *)"Show";
            ng.ng_GadgetID = V_OP;
            g = gad[V_OP] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)v_op_labels, GTCY_Active, (ULONG)v.op, TAG_DONE);
            ng.ng_TopEdge += rowh;
            ng.ng_GadgetText = (UBYTE *)"Field";
            ng.ng_GadgetID = V_FIELD;
            g = gad[V_FIELD] = CreateGadget(CYCLE_KIND, g, &ng, GA_Disabled, v.op == OB_FILTER_NONE,
                                            GTCY_Labels, (ULONG)v_field_labels, GTCY_Active, (ULONG)field_i, TAG_DONE);
            ng.ng_TopEdge += rowh;
            ng.ng_GadgetText = (UBYTE *)"Value";
            ng.ng_GadgetID = V_VALUE;
            g = gad[V_VALUE] = CreateGadget(STRING_KIND, g, &ng, GA_Disabled, !needs_value,
                                            GTST_String, (ULONG)v.value, GTST_MaxChars, OB_VALUE_MAX - 1, TAG_DONE);
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = vw->Height - vw->BorderBottom - 6 - (fh + 6);
            ng.ng_LeftEdge = l;
            ng.ng_Width = fw * 10 + 16;
            ng.ng_GadgetText = (UBYTE *)"Apply";
            ng.ng_GadgetID = V_APPLY;
            g = gad[V_APPLY] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = l + (iw - ng.ng_Width) / 2;
            ng.ng_GadgetText = (UBYTE *)"Show All";
            ng.ng_GadgetID = V_ALL;
            g = gad[V_ALL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = vw->Width - vw->BorderRight - 8 - ng.ng_Width;
            ng.ng_GadgetText = (UBYTE *)"Cancel";
            ng.ng_GadgetID = V_CANCEL;
            g = gad[V_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(vw->RPort, pen(BACKGROUNDPEN));
            RectFill(vw->RPort, vw->BorderLeft, vw->BorderTop, vw->Width - vw->BorderRight - 1,
                     vw->Height - vw->BorderBottom - 1);
            AddGList(vw, eg, ~0, -1, NULL);
            RefreshGList(eg, vw, NULL, -1);
            GT_RefreshWindow(vw, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(vw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(vw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(vw); GT_EndRefresh(vw, TRUE); continue; }
                if (class != IDCMP_GADGETUP && class != IDCMP_GADGETDOWN) continue;
                switch (gg->GadgetID) {
                case V_SORT:
                    sort_i = code;
                    put_text(v.sort, sizeof v.sort, sort_i > 0 && sort_i <= cur.nfields ? cur.fields[sort_i - 1].name : "");
                    if (!sort_i) v.desc = 0;
                    rebuild = 1;
                    break;
                case V_DESC:
                    v.desc = (gg->Flags & GFLG_SELECTED) != 0;
                    break;
                case V_OP:
                    v.op = (ob_filter_op)code;
                    rebuild = 1;
                    break;
                case V_FIELD:
                    field_i = code;
                    if (field_i < cur.nfields) put_text(v.field, sizeof v.field, cur.fields[field_i].name);
                    break;
                case V_VALUE: {
                    STRPTR str = NULL;
                    GT_GetGadgetAttrs(gg, vw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                    put_text(v.value, sizeof v.value, str ? (const char *)str : "");
                    break;
                }
                case V_APPLY: {
                    STRPTR str = NULL;
                    /* the value as typed, Return pressed or not */
                    if (gad[V_VALUE]) GT_GetGadgetAttrs(gad[V_VALUE], vw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                    if (str) put_text(v.value, sizeof v.value, (const char *)str);
                    if (!ob_view_check(proj, cur.name, &v)) {
                        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
                        EasyRequest(vw, &es, NULL, (ULONG)ob_error(proj));
                        break;
                    }
                    view = v;
                    view_changed();
                    done = 1;
                    break;
                }
                case V_ALL:
                    ob_view_init(&view);
                    view_changed();
                    done = 1;
                    break;
                case V_CANCEL:
                    done = 1;
                    break;
                }
            }
        }
    }
    if (eg) { RemoveGList(vw, eg, -1); FreeGadgets(eg); }
    CloseWindow(vw);
    if (win) ActivateWindow(win);     /* keys go to the main window again */
}

/* A click on a column's heading: sort by it, then Z to A, then key order. */
static void sort_by_column(int mx)
{
    int x = ds_x + 4, j;
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    for (j = left_col; j < cur.nfields; j++) {
        int w = col_width(&cur.fields[j]);
        if (mx >= x && mx < x + w) {
            if (strcmp(view.sort, cur.fields[j].name)) { put_text(view.sort, sizeof view.sort, cur.fields[j].name); view.desc = 0; }
            else if (!view.desc) view.desc = 1;
            else { view.sort[0] = 0; view.desc = 0; }
            view_changed();
            return;
        }
        x += w;
    }
}

static ob_csv_opts csv_o;

/* What an import left out, as a requester. */
static void show_report(ob_int added, const ob_csv_opts *o)
{
    static char text[sizeof o->report + 120];
    snprintf(text, sizeof text, "Added %lld rows. %d left out:\n\n%s", added, o->bad, o->report);
    tell(text);
}

static void import_csv(void)
{
    char path[512], msg[160];
    ob_int n;
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    if (!proj || !have_cur) return;
    snprintf(msg, sizeof msg, "Import CSV into %.60s", cur.name);
    if (!ask_file(msg, FALSE, "#?.(csv|txt|tab)", path, sizeof path)) return;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    ob_csv_opts_init(&csv_o);
    n = ob_csv_import_with(proj, cur.name, path, &csv_o);
    SetWindowPointer(win, TAG_DONE);
    if (n < 0) {
        char q[OB_ERR_MAX + 120];
        if (strncmp(ob_error(proj), "Line ", 5) || !strncmp(ob_error(proj), "Line 1", 6)) { tell(ob_error(proj)); return; }
        /* a bad row: the others can go in, with the bad ones listed */
        snprintf(q, sizeof q, "%s\n\nNothing was added. Add the other rows and list the bad ones?", ob_error(proj));
        if (!ask(q, "Add the Others|Cancel")) return;
        ob_csv_opts_init(&csv_o);
        csv_o.skip_bad = 1;
        SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
        n = ob_csv_import_with(proj, cur.name, path, &csv_o);
        SetWindowPointer(win, TAG_DONE);
        if (n < 0) { tell(ob_error(proj)); return; }
    }
    snprintf(msg, sizeof msg, "Added %lld rows from %.60s", n, leaf(path));
    load_page();
    set_status(msg);
    if (csv_o.bad) show_report(n, &csv_o);
}

static void export_csv(void)
{
    char path[512], msg[160];
    ob_int n;
    BPTR l;
    if (!proj || (!q_on && !have_cur)) return;
    if (q_on) snprintf(msg, sizeof msg, "Export %.60s's results as CSV", qy.name[0] ? qy.name : "the query");
    else snprintf(msg, sizeof msg, "Export %.60s as CSV", cur.name);
    if (!ask_file(msg, TRUE, "#?.csv", path, sizeof path)) return;
    if ((l = Lock((STRPTR)path, ACCESS_READ)) != 0) {
        UnLock(l);
        snprintf(msg, sizeof msg, "%.100s is already there.\nReplace it?", leaf(path));
        if (!ask(msg, "Replace|Cancel")) return;
    }
    {
        int c = ask("Write the text as ISO-8859-1 (for Amiga programs)\nor UTF-8 (for PC and Mac programs)?", "ISO-8859-1|UTF-8|Cancel");
        if (!c) return;
        ob_csv_opts_init(&csv_o);
        csv_o.sep = ',';
        csv_o.charset = c == 2 ? OB_CHARSET_UTF8 : OB_CHARSET_LATIN1;
    }
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    /* a query's rows with the answers it was run with */
    if (q_on) n = ob_query_csv_export(proj, &qy, q_pv, path, &csv_o);
    else n = ob_csv_export_with(proj, cur.name, &view, path, &csv_o);
    SetWindowPointer(win, TAG_DONE);
    if (n < 0) { tell(ob_error(proj)); return; }
    snprintf(msg, sizeof msg, "Wrote %lld rows to %.60s%s", n, leaf(path),
             !q_on && ob_view_filtered(&view) ? " (the filtered rows)" : "");
    set_status(msg);
}

/* ---- Table > Import dBASE..., Export dBASE..., New Table from dBASE... ----
 * dBASE III and IV tables (.dbf, memos in the .dbt beside it); the text is
 * taken byte for byte, as ISO-8859-1. */

/* Rows from path into the table: all or none, and if a record is bad, the
 * others on request with the bad ones listed. -1 with the reason told. */
static ob_int dbf_rows_in(const char *table, const char *path)
{
    ob_int n;
    ob_csv_opts_init(&csv_o);
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    n = ob_dbf_import(proj, table, path, &csv_o);
    SetWindowPointer(win, TAG_DONE);
    if (n < 0) {
        char q[OB_ERR_MAX + 120];
        if (strncmp(ob_error(proj), "Record ", 7)) { tell(ob_error(proj)); return -1; }
        snprintf(q, sizeof q, "%s\n\nNothing was added. Add the other records and list the bad ones?", ob_error(proj));
        if (!ask(q, "Add the Others|Cancel")) return -1;
        ob_csv_opts_init(&csv_o);
        csv_o.skip_bad = 1;
        SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
        n = ob_dbf_import(proj, table, path, &csv_o);
        SetWindowPointer(win, TAG_DONE);
        if (n < 0) { tell(ob_error(proj)); return -1; }
    }
    return n;
}

static void dbf_done(ob_int n, const char *path)
{
    char msg[200];
    snprintf(msg, sizeof msg, "Added %lld rows from %.60s", (long long)n, leaf(path));
    if (csv_o.deleted) snprintf(msg + strlen(msg), sizeof msg - strlen(msg), " (%lld deleted records left out)", (long long)csv_o.deleted);
    load_page();
    set_status(msg);
    if (csv_o.bad) show_report(n, &csv_o);
    else if (csv_o.report[0]) tell(csv_o.report);     /* the file's code page */
}

static void import_dbf(void)
{
    char path[512], msg[160];
    ob_int n;
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    if (!proj || !have_cur) return;
    snprintf(msg, sizeof msg, "Import dBASE into %.60s", cur.name);
    if (!ask_file(msg, FALSE, "#?.dbf", path, sizeof path)) return;
    if ((n = dbf_rows_in(cur.name, path)) >= 0) dbf_done(n, path);
}

static void export_dbf(void)
{
    char path[512], msg[160];
    ob_int n;
    BPTR l;
    if (q_on) { set_status("A query's rows are read only: choose a table to change its rows"); return; }
    if (!proj || !have_cur) return;
    snprintf(msg, sizeof msg, "Export %.60s as dBASE", cur.name);
    if (!ask_file(msg, TRUE, "#?.dbf", path, sizeof path)) return;
    if ((l = Lock((STRPTR)path, ACCESS_READ)) != 0) {
        UnLock(l);
        snprintf(msg, sizeof msg, "%.100s is already there.\nReplace it (and its .dbt)?", leaf(path));
        if (!ask(msg, "Replace|Cancel")) return;
    }
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    n = ob_dbf_export(proj, cur.name, &view, path);
    SetWindowPointer(win, TAG_DONE);
    if (n < 0) { tell(ob_error(proj)); return; }
    snprintf(msg, sizeof msg, "Wrote %lld rows to %.60s%s", (long long)n, leaf(path),
             ob_view_filtered(&view) ? " (the filtered rows)" : "");
    set_status(msg);
}

static int table_named(const char *name);
static ob_table dbf_t;      /* the new table's design (too big for the stack) */

static void new_table_from_dbf(void)
{
    char path[512], msg[600];
    size_t k;
    ob_int n;
    int i, made = 0;
    if (!proj) return;
    if (!ask_file("New table from a dBASE file", FALSE, "#?.dbf", path, sizeof path)) return;
    SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
    i = ob_dbf_guess(proj, path, NULL, &dbf_t);
    SetWindowPointer(win, TAG_DONE);
    if (!i) { tell(ob_error(proj)); return; }
    if (table_named(dbf_t.name) >= 0) {     /* Sales there already: Sales 2 */
        char base[OB_NAME_MAX];
        put_text(base, sizeof base, dbf_t.name);
        for (i = 2; i < 100; i++) {
            snprintf(dbf_t.name, sizeof dbf_t.name, "%.*s %d", (int)sizeof base - 5, base, i);
            if (table_named(dbf_t.name) < 0) break;
        }
    }
    snprintf(msg, sizeof msg, "Make the table %.60s from %.100s, with these fields?\n\n", dbf_t.name, leaf(path));
    for (i = 0; i < dbf_t.nfields; i++) {
        k = strlen(msg);
        if (k + 60 >= sizeof msg) { put_text(msg + k, sizeof msg - k, "..."); break; }
        snprintf(msg + k, sizeof msg - k, "%s%.30s %.12s", i ? (i % 3 ? ", " : ",\n") : "", dbf_t.fields[i].name, ob_type_name(dbf_t.fields[i].type));
    }
    if (!ask(msg, "Make|Cancel")) return;
    /* the table and its rows together, or neither */
    if (!ob_begin(proj)) { tell(ob_error(proj)); return; }
    if (!ob_table_create(proj, &dbf_t)) { char e[OB_ERR_MAX]; put_text(e, sizeof e, ob_error(proj)); ob_rollback(proj); tell(e); return; }
    if ((n = dbf_rows_in(dbf_t.name, path)) >= 0 && ob_commit(proj)) made = 1;
    else {
        if (n >= 0) tell(ob_error(proj));
        ob_rollback(proj);
    }
    if (!made) return;
    load_tables(dbf_t.name);
    top_row = 0;
    sel_row = -1;
    dbf_done(n, path);
}

/* ---- Table > New Table from CSV... ---- */

enum { W_NAME = 1, W_SEP, W_CHARSET, W_HEADER, W_FIELDS, W_TYPE, W_SKIP, W_IMPORT, W_CANCEL, W_COUNT };

static ob_table wt;         /* the new table's design (too big for the stack) */
static struct List wl;
static struct Node wnodes[OB_MAX_FIELDS];
static char wtext[OB_MAX_FIELDS][OB_NAME_MAX + 24];
static const ob_type w_types[] = { OB_TEXT, OB_MEMO, OB_INTEGER, OB_NUMBER, OB_CURRENCY, OB_DATE, OB_TIME,
                                   OB_DATETIME, OB_YESNO };
#define W_NTYPES ((int)(sizeof w_types / sizeof w_types[0]))
static STRPTR w_type_labels[W_NTYPES + 1];
static char w_sep_found[40], w_cs_found[40], w_hd_found[40];
static STRPTR w_sep_labels[] = { (STRPTR)w_sep_found, (STRPTR)"Comma", (STRPTR)"Semicolon", (STRPTR)"Tab", NULL };
static STRPTR w_cs_labels[] = { (STRPTR)w_cs_found, (STRPTR)"ISO-8859-1", (STRPTR)"UTF-8", NULL };
static STRPTR w_hd_labels[] = { (STRPTR)w_hd_found, (STRPTR)"Field names", (STRPTR)"Data", NULL };

static void wizard_list(void)
{
    int i;
    NewList(&wl);
    for (i = 0; i < wt.nfields; i++) {
        snprintf(wtext[i], sizeof wtext[i], "%s: %s", wt.fields[i].name, ob_type_name(wt.fields[i].type));
        wnodes[i].ln_Name = wtext[i];
        AddTail(&wl, &wnodes[i]);
    }
}

static void new_table_from_csv(void)
{
    char path[512], tname[OB_NAME_MAX], msg[200];
    struct Window *ww;
    struct Gadget *eg = NULL, *g, *gad[W_COUNT];
    struct NewGadget ng;
    ob_csv_opts o;
    int done = 0, rebuild = 1, regues = 1, rowh = fh + 8, l, t, w, h, lw = fw * 12, i;
    int sep_i = 0, cs_i = 0, hd_i = 0, fsel = -1, skip = 0, made = 0;
    ob_int n = 0;
    if (!proj) return;
    if (!ask_file("New table from a CSV file", FALSE, "#?.(csv|txt|tab)", path, sizeof path)) return;
    tname[0] = 0;
    for (i = 0; i < W_NTYPES; i++) w_type_labels[i] = (STRPTR)ob_type_name(w_types[i]);
    w_type_labels[W_NTYPES] = NULL;
    w = fw * 56;
    h = rowh * 14 + 16;
    if (w > scr->Width - 20) w = scr->Width - 20;
    if (h > scr->Height - 40) h = scr->Height - 40;
    ww = OpenWindowTags(NULL,
        WA_Title, (ULONG)"New Table from CSV",
        WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 30, WA_Top, win->TopEdge + 20,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | CYCLEIDCMP | STRINGIDCMP | CHECKBOXIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!ww) return;
    SetFont(ww->RPort, font);
    l = ww->BorderLeft + 8;
    t = ww->BorderTop + 6;
    while (!done) {
        if (regues) {
            ob_csv_opts_init(&o);
            o.sep = sep_i == 1 ? ',' : sep_i == 2 ? ';' : sep_i == 3 ? '\t' : 0;
            o.charset = cs_i == 1 ? OB_CHARSET_LATIN1 : cs_i == 2 ? OB_CHARSET_UTF8 : OB_CHARSET_AUTO;
            o.header = hd_i == 1 ? 1 : hd_i == 2 ? 0 : -1;
            SetWindowPointer(ww, WA_BusyPointer, TRUE, TAG_DONE);
            if (!ob_csv_guess(proj, path, tname[0] ? tname : NULL, &o, &wt)) {
                SetWindowPointer(ww, TAG_DONE);
                {
                    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
                    EasyRequest(ww, &es, NULL, (ULONG)ob_error(proj));
                }
                break;
            }
            SetWindowPointer(ww, TAG_DONE);
            put_text(tname, sizeof tname, wt.name);
            snprintf(w_sep_found, sizeof w_sep_found, "Found: %s", o.found_sep == ';' ? "semicolon" : o.found_sep == '\t' ? "tab" : "comma");
            snprintf(w_cs_found, sizeof w_cs_found, "Found: %s", o.found_charset == OB_CHARSET_UTF8 ? "UTF-8" : "ISO-8859-1");
            snprintf(w_hd_found, sizeof w_hd_found, "Found: %s", o.found_header ? "field names" : "data");
            fsel = -1;
            regues = 0;
            rebuild = 1;
        }
        if (rebuild) {
            int iw = ww->Width - ww->BorderLeft - ww->BorderRight - 16, ti = 0, y;
            ob_field *f = fsel >= 0 && fsel < wt.nfields ? &wt.fields[fsel] : NULL;
            if (f) for (i = 0; i < W_NTYPES; i++) if (w_types[i] == f->type) ti = i;
            if (eg) { RemoveGList(ww, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            wizard_list();
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = vi;
            ng.ng_TextAttr = &font_attr;
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_TopEdge = y = t;
            ng.ng_Width = iw - lw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Table name";
            ng.ng_GadgetID = W_NAME;
            g = gad[W_NAME] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)tname, GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_GadgetText = (UBYTE *)"Separator";
            ng.ng_GadgetID = W_SEP;
            g = gad[W_SEP] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)w_sep_labels, GTCY_Active, (ULONG)sep_i, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_GadgetText = (UBYTE *)"Characters";
            ng.ng_GadgetID = W_CHARSET;
            g = gad[W_CHARSET] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)w_cs_labels, GTCY_Active, (ULONG)cs_i, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_GadgetText = (UBYTE *)"First line";
            ng.ng_GadgetID = W_HEADER;
            g = gad[W_HEADER] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)w_hd_labels, GTCY_Active, (ULONG)hd_i, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_Height = rowh * 5;
            ng.ng_GadgetText = (UBYTE *)"Fields";
            ng.ng_GadgetID = W_FIELDS;
            g = gad[W_FIELDS] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&wl, GTLV_ShowSelected, 0,
                                             GTLV_Selected, fsel >= 0 ? (ULONG)fsel : ~0UL, TAG_DONE);
            ng.ng_TopEdge = y += rowh * 5 + 6;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Type";
            ng.ng_GadgetID = W_TYPE;
            g = gad[W_TYPE] = CreateGadget(CYCLE_KIND, g, &ng, GA_Disabled, !f || f->type == OB_COUNTER,
                                           GTCY_Labels, (ULONG)w_type_labels, GTCY_Active, (ULONG)ti, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_Width = fh + 6;
            ng.ng_Flags = PLACETEXT_RIGHT; /* the label is longer than the left column */
            ng.ng_GadgetText = (UBYTE *)"Leave out bad rows";
            ng.ng_GadgetID = W_SKIP;
            g = gad[W_SKIP] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, skip, GTCB_Scaled, TRUE, TAG_DONE);
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = ww->Height - ww->BorderBottom - 6 - (fh + 6);
            ng.ng_LeftEdge = l;
            ng.ng_Width = fw * 10 + 16;
            ng.ng_GadgetText = (UBYTE *)"Import";
            ng.ng_GadgetID = W_IMPORT;
            g = gad[W_IMPORT] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = ww->Width - ww->BorderRight - 8 - ng.ng_Width;
            ng.ng_GadgetText = (UBYTE *)"Cancel";
            ng.ng_GadgetID = W_CANCEL;
            g = gad[W_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(ww->RPort, pen(BACKGROUNDPEN));
            RectFill(ww->RPort, ww->BorderLeft, ww->BorderTop, ww->Width - ww->BorderRight - 1,
                     ww->Height - ww->BorderBottom - 1);
            AddGList(ww, eg, ~0, -1, NULL);
            RefreshGList(eg, ww, NULL, -1);
            GT_RefreshWindow(ww, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(ww->UserPort);
            while (!rebuild && !regues && !done && (m = GT_GetIMsg(ww->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(ww); GT_EndRefresh(ww, TRUE); continue; }
                if (class != IDCMP_GADGETUP && class != IDCMP_GADGETDOWN) continue;
                switch (gg->GadgetID) {
                case W_NAME: {
                    STRPTR str = NULL;
                    GT_GetGadgetAttrs(gg, ww, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                    put_text(tname, sizeof tname, str ? (const char *)str : "");
                    put_text(wt.name, sizeof wt.name, tname);
                    break;
                }
                case W_SEP: sep_i = code; regues = 1; break;
                case W_CHARSET: cs_i = code; regues = 1; break;
                case W_HEADER: hd_i = code; regues = 1; break;
                case W_FIELDS: fsel = code; rebuild = 1; break;
                case W_TYPE:
                    if (fsel >= 0 && fsel < wt.nfields && wt.fields[fsel].type != OB_COUNTER && code < W_NTYPES) {
                        wt.fields[fsel].type = w_types[code];
                        rebuild = 1;
                    }
                    break;
                case W_SKIP: skip = (gg->Flags & GFLG_SELECTED) != 0; break;
                case W_IMPORT: {
                    STRPTR str = NULL;
                    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
                    if (gad[W_NAME]) GT_GetGadgetAttrs(gad[W_NAME], ww, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                    if (str) { put_text(tname, sizeof tname, (const char *)str); put_text(wt.name, sizeof wt.name, tname); }
                    o.skip_bad = skip;
                    o.bad = 0;
                    o.report[0] = 0;
                    SetWindowPointer(ww, WA_BusyPointer, TRUE, TAG_DONE);
                    /* the table and its rows together, or neither */
                    n = -1;
                    if (ob_begin(proj)) {
                        if (ob_table_create(proj, &wt) && (n = ob_csv_import_with(proj, wt.name, path, &o)) >= 0 && ob_commit(proj)) {
                            made = 1;
                        } else {
                            char e[OB_ERR_MAX];
                            put_text(e, sizeof e, ob_error(proj));
                            ob_rollback(proj);
                            n = -1;
                            EasyRequest(ww, &es, NULL, (ULONG)e);
                        }
                    } else {
                        EasyRequest(ww, &es, NULL, (ULONG)ob_error(proj));
                    }
                    SetWindowPointer(ww, TAG_DONE);
                    if (made) done = 1;
                    break;
                }
                case W_CANCEL:
                    done = 1;
                    break;
                }
            }
        }
    }
    if (eg) { RemoveGList(ww, eg, -1); FreeGadgets(eg); }
    CloseWindow(ww);
    if (win) ActivateWindow(win);     /* keys go to the main window again */
    if (made) {
        load_tables(wt.name);
        top_row = 0;
        sel_row = -1;
        load_page();
        snprintf(msg, sizeof msg, "Made %.60s with %lld rows from %.60s", wt.name, n, leaf(path));
        set_status(msg);
        if (o.bad) show_report(n, &o);
    }
}

/* ---- Queries (core 0.5, design 5) ---- */

/* A line of text from the user in a small window; 0 when cancelled. */
static int ask_text(const char *title, const char *prompt, char *buf, size_t size)
{
    struct Window *aw;
    struct Gadget *eg = NULL, *g, *sg;
    struct NewGadget ng;
    int done = 0, okd = 0, w = fw * 44, h = (fh + 8) * 3 + 12, lw;
    if (rexx_quiet(prompt)) return 0;
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

/* qy's rows into the datasheet, asking its questions first; 0 when it can't run. */
static int run_query(int ask_again)
{
    char names[OB_QUERY_PARAMS][OB_VALUE_MAX], msg[OB_ERR_MAX + 80];
    int n, i;
    if (!proj) return 0;
    n = ob_query_params(proj, &qy, names, OB_QUERY_PARAMS);
    if (n < 0) { tell(ob_error(proj)); return 0; }
    for (i = 0; i < n && i < OB_QUERY_PARAMS; i++) {
        if (ask_again || !q_answers[i][0]) {
            if (!ask_text(qy.name[0] ? qy.name : "Query", names[i], q_answers[i], sizeof q_answers[i])) return 0;
        }
        q_pv[i] = q_answers[i];
    }
    q_ncols = ob_query_columns(proj, &qy, q_cols, OB_QUERY_OUT);
    if (q_ncols < 0) { tell(ob_error(proj)); q_ncols = 0; return 0; }
    if (q_ncols > OB_QUERY_OUT) q_ncols = OB_QUERY_OUT;
    if (ob_query_total(proj, &qy, q_pv) < 0) {
        snprintf(msg, sizeof msg, "%s", ob_error(proj));
        tell(msg);
        return 0;
    }
    q_on = 1;
    top_row = 0;
    sel_row = -1;
    left_col = 0;
    load_page();
    set_status("");
    return 1;
}

/* The designer: one window for the grid (tables, columns, criteria) or the
 * SQL, with Run, Save and Close. */
enum {
    Q_NAME = 1, Q_SQLONLY, Q_SQL, Q_TABLES, Q_ADDTABLE, Q_REMTABLE, Q_JOINS, Q_COLS, Q_ADDFIELD, Q_ADDEXPR, Q_REMCOL,
    Q_EXPR, Q_HEAD, Q_SORT, Q_TOTAL, Q_SHOW, Q_CRIT0, Q_CRIT1, Q_CRIT2, Q_SQLTEXT, Q_RUN, Q_SAVE, Q_CLOSE, Q_COUNT
};

#define QF_MAX 240                 /* fields offered to add */
static ob_table q_t;               /* one table's design at a time */
static ob_query q_ed;              /* the query being designed */
static struct List q_tl, q_cl;
static struct Node q_tnodes[OB_QUERY_TABLES], q_cnodes[OB_QUERY_COLUMNS];
static char q_ctext[OB_QUERY_COLUMNS][2 * OB_NAME_MAX + 48];
static char q_ftext[QF_MAX][2 * OB_NAME_MAX + 4];
static STRPTR q_flabels[QF_MAX + 2], q_tlabels[OB_MAX_FIELDS + 2];
static char q_tname_text[OB_MAX_FIELDS][OB_NAME_MAX];
static STRPTR q_sort_labels[] = { (STRPTR)"No sort", (STRPTR)"A to Z", (STRPTR)"Z to A", NULL };
static STRPTR q_total_labels[OB_TOTALS + 1];
static char q_sqltext[OB_SQL_MAX];
static int q_nf;

static void q_lists(void)
{
    int i, j, n = 0;
    NewList(&q_tl);
    for (i = 0; i < q_ed.ntables; i++) {
        q_tnodes[i].ln_Name = q_ed.tables[i];
        AddTail(&q_tl, &q_tnodes[i]);
    }
    NewList(&q_cl);
    for (i = 0; i < q_ed.ncolumns; i++) {
        const ob_qcolumn *c = &q_ed.columns[i];
        if (c->table[0]) snprintf(q_ctext[i], sizeof q_ctext[i], "%s%s.%s", c->show ? "" : "(", c->table, c->field);
        else snprintf(q_ctext[i], sizeof q_ctext[i], "%s%.60s", c->show ? "" : "(", c->expr);
        if (c->total) {
            size_t len = strlen(q_ctext[i]);
            snprintf(q_ctext[i] + len, sizeof q_ctext[i] - len, " %s", ob_total_name(c->total));
        }
        if (c->sort) {
            size_t len = strlen(q_ctext[i]);
            snprintf(q_ctext[i] + len, sizeof q_ctext[i] - len, " %s", c->sort == OB_SORT_UP ? "A-Z" : "Z-A");
        }
        if (c->criteria[0][0] || c->criteria[1][0] || c->criteria[2][0]) {
            size_t len = strlen(q_ctext[i]);
            snprintf(q_ctext[i] + len, sizeof q_ctext[i] - len, " [%.40s]", c->criteria[0][0] ? c->criteria[0] : "or...");
        }
        if (!c->show) {
            size_t len = strlen(q_ctext[i]);
            snprintf(q_ctext[i] + len, sizeof q_ctext[i] - len, ")");
        }
        q_cnodes[i].ln_Name = q_ctext[i];
        AddTail(&q_cl, &q_cnodes[i]);
    }
    /* the fields that can be added: each table's *, then its fields */
    q_flabels[0] = (STRPTR)"Add a field...";
    q_nf = 0;
    for (i = 0; i < q_ed.ntables && q_nf < QF_MAX; i++) {
        if (!ob_table_get(proj, q_ed.tables[i], &q_t)) continue;
        snprintf(q_ftext[q_nf], sizeof q_ftext[q_nf], "%s.*", q_t.name);
        q_flabels[q_nf + 1] = (STRPTR)q_ftext[q_nf];
        q_nf++;
        for (j = 0; j < q_t.nfields && q_nf < QF_MAX; j++) {
            snprintf(q_ftext[q_nf], sizeof q_ftext[q_nf], "%s.%s", q_t.name, q_t.fields[j].name);
            q_flabels[q_nf + 1] = (STRPTR)q_ftext[q_nf];
            q_nf++;
        }
    }
    q_flabels[q_nf + 1] = NULL;
    /* the tables that can be added */
    q_tlabels[0] = (STRPTR)"Add a table...";
    n = ob_table_count(proj);
    if (n > OB_MAX_FIELDS) n = OB_MAX_FIELDS;
    for (i = 0; i < n; i++) {
        put_text(q_tname_text[i], sizeof q_tname_text[i], ob_table_name(proj, i));
        q_tlabels[i + 1] = (STRPTR)q_tname_text[i];
    }
    q_tlabels[n + 1] = NULL;
    for (i = 0; i < OB_TOTALS; i++) q_total_labels[i] = (STRPTR)(i ? ob_total_name((ob_total)i) : "No total");
    q_total_labels[OB_TOTALS] = NULL;
    /* the SQL it makes, or why it can't run yet */
    if (q_ed.sql_only) {
        put_text(q_sqltext, sizeof q_sqltext, "");
    } else if (ob_query_sql(proj, &q_ed, q_sqltext, sizeof q_sqltext) < 0) {
        snprintf(q_sqltext, sizeof q_sqltext, "(%s)", ob_error(proj));
    }
}

/* What the string gadgets hold, into q_ed (a string gadget tells only when
 * Return is pressed, so read them before anything else happens). */
static void q_read_strings(struct Window *qw, struct Gadget **gad, int csel)
{
    STRPTR str;
    ob_qcolumn *c = csel >= 0 && csel < q_ed.ncolumns ? &q_ed.columns[csel] : NULL;
#define QSTR(id, dst) do { str = NULL; if (gad[id]) GT_GetGadgetAttrs(gad[id], qw, NULL, GTST_String, (ULONG)&str, TAG_DONE); \
    if (str) put_text((dst), sizeof(dst), (const char *)str); } while (0)
    QSTR(Q_NAME, q_ed.name);
    if (q_ed.sql_only) QSTR(Q_SQL, q_ed.sql);
    if (c && !q_ed.sql_only) {
        if (!c->table[0]) QSTR(Q_EXPR, c->expr);
        QSTR(Q_HEAD, c->heading);
        QSTR(Q_CRIT0, c->criteria[0]);
        QSTR(Q_CRIT1, c->criteria[1]);
        QSTR(Q_CRIT2, c->criteria[2]);
    }
#undef QSTR
}

/* ---- Joins (the query designer's Joins... window) ----
 * The query's tables as boxes of their fields, its joins as lines between
 * fields. Drag a field onto a field of another table, or click one and then
 * the other, to join them; click a line to choose it; Inner, Left and Right
 * set what it keeps; Remove (or Del) takes it away. With no join drawn yet
 * the window starts from the tables' relationships. */

#define JV_FIELDS 24

struct jv_box {
    int x, y, w, h, nf, more;
    char fields[JV_FIELDS][OB_NAME_MAX];
};

static struct jv_box jv[OB_QUERY_TABLES];
static int jv_seg[OB_QUERY_JOINS][8];       /* each join's line: four points */
static int jv_rowh, jv_sel, jv_pt, jv_pf;    /* row height, chosen join, picked table and field */
static int jv_cx, jv_cy, jv_cw, jv_ch, jv_sty;

static int jv_table(const char *name)
{
    int i;
    for (i = 0; i < q_ed.ntables; i++) if (same_name(q_ed.tables[i], name)) return i;
    return -1;
}

static int jv_field(int t, const char *name)
{
    int i;
    for (i = 0; i < jv[t].nf; i++) if (same_name(jv[t].fields[i], name)) return i;
    return -1;
}

/* The boxes in rows across the canvas, each as tall as its fields (up to
 * what fits). */
static void jv_layout(void)
{
    int bw = fw * 16, gap = fw * 6, cols, rows, maxh, i, j, show;
    cols = (jv_cw + gap) / (bw + gap);
    if (cols < 1) cols = 1;
    rows = (q_ed.ntables + cols - 1) / cols;
    if (rows < 1) rows = 1;
    maxh = (jv_ch - (rows - 1) * jv_rowh) / rows;
    show = (maxh - 6) / jv_rowh - 1;
    if (show < 1) show = 1;
    if (show > JV_FIELDS) show = JV_FIELDS;
    for (i = 0; i < q_ed.ntables; i++) {
        struct jv_box *b = &jv[i];
        b->nf = b->more = 0;
        if (ob_table_get(proj, q_ed.tables[i], &q_t))
            for (j = 0; j < q_t.nfields; j++) {
                if (b->nf < show) put_text(b->fields[b->nf++], OB_NAME_MAX, q_t.fields[j].name);
                else b->more++;
            }
        b->w = bw;
        b->h = jv_rowh * (b->nf + 1 + (b->more ? 1 : 0)) + 6;
        b->x = jv_cx + (i % cols) * (bw + gap);
        b->y = jv_cy + (i / cols) * (maxh + jv_rowh);
    }
}

static int jv_field_y(int t, int f)
{
    return f >= 0 ? jv[t].y + 3 + jv_rowh * (f + 1) + jv_rowh / 2 : jv[t].y + jv[t].h / 2;
}

static void jv_draw(struct Window *jw)
{
    struct RastPort *rp = jw->RPort;
    char line[3 * OB_NAME_MAX + 120];
    int i, j;
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    SetAPen(rp, pen(BACKGROUNDPEN));
    RectFill(rp, jv_cx, jv_cy, jv_cx + jv_cw - 1, jv_cy + jv_ch - 1);
    RectFill(rp, jv_cx, jv_sty, jv_cx + jv_cw - 1, jv_sty + fh + 1);
    for (i = 0; i < q_ed.ntables; i++) {
        struct jv_box *b = &jv[i];
        SetAPen(rp, pen(FILLPEN));
        RectFill(rp, b->x, b->y, b->x + b->w - 1, b->y + jv_rowh + 1);
        SetAPen(rp, pen(SHADOWPEN));
        Move(rp, b->x, b->y); Draw(rp, b->x + b->w - 1, b->y); Draw(rp, b->x + b->w - 1, b->y + b->h - 1);
        Draw(rp, b->x, b->y + b->h - 1); Draw(rp, b->x, b->y);
        SetAPen(rp, pen(FILLTEXTPEN));
        SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
        Move(rp, b->x + 4, b->y + 1 + rp->TxBaseline);
        Text(rp, (STRPTR)q_ed.tables[i], (ULONG)TextFit(rp, (STRPTR)q_ed.tables[i], strlen(q_ed.tables[i]), &rv_te, NULL, 1,
                                                        b->w - 8, jv_rowh + 2));
        SetSoftStyle(rp, 0, FSF_BOLD);
        for (j = 0; j < b->nf + (b->more ? 1 : 0); j++) {
            const char *t = j < b->nf ? b->fields[j] : "...";
            int ry = b->y + 3 + jv_rowh * (j + 1);
            if (i == jv_pt && j == jv_pf) {
                SetAPen(rp, pen(FILLPEN));
                RectFill(rp, b->x + 2, ry, b->x + b->w - 3, ry + jv_rowh - 1);
                SetAPen(rp, pen(FILLTEXTPEN));
            } else {
                SetAPen(rp, pen(TEXTPEN));
            }
            Move(rp, b->x + 6, ry + (jv_rowh - fh) / 2 + rp->TxBaseline);
            Text(rp, (STRPTR)t, (ULONG)TextFit(rp, (STRPTR)t, strlen(t), &rv_te, NULL, 1, b->w - 10, jv_rowh));
        }
    }
    /* the joins: out of one box's side, across the gap, into the other's */
    for (i = 0; i < q_ed.njoins; i++) {
        const ob_qjoin *k = &q_ed.joins[i];
        int a = jv_table(k->left), b = jv_table(k->right), ax, bx, ay, by, mid, *sg = jv_seg[i];
        memset(sg, 0, sizeof jv_seg[i]);
        if (a < 0 || b < 0) continue;
        ay = jv_field_y(a, jv_field(a, k->lfield));
        by = jv_field_y(b, jv_field(b, k->rfield));
        if (jv[b].x >= jv[a].x + jv[a].w) { ax = jv[a].x + jv[a].w; bx = jv[b].x; }
        else if (jv[b].x + jv[b].w <= jv[a].x) { ax = jv[a].x; bx = jv[b].x + jv[b].w; }
        else { ax = jv[a].x + jv[a].w; bx = jv[b].x + jv[b].w; }
        mid = ax == bx || (ax == jv[a].x + jv[a].w && bx == jv[b].x + jv[b].w)
            ? (ax > bx ? ax : bx) + fw * 3 : (ax + bx) / 2;
        sg[0] = ax; sg[1] = ay; sg[2] = mid; sg[3] = ay; sg[4] = mid; sg[5] = by; sg[6] = bx; sg[7] = by;
        SetAPen(rp, pen(i == jv_sel ? FILLPEN : SHADOWPEN));
        for (j = 0; j < (i == jv_sel ? 2 : 1); j++) {
            Move(rp, ax, ay + j); Draw(rp, mid + j, ay + j); Draw(rp, mid + j, by + j); Draw(rp, bx, by + j);
        }
        /* a block at the end whose every row is kept */
        if (k->kind == OB_JOIN_LEFT) RectFill(rp, ax + (ax < mid ? 0 : -4), ay - 2, ax + (ax < mid ? 4 : 0), ay + 2);
        if (k->kind == OB_JOIN_RIGHT) RectFill(rp, bx + (bx < mid ? 0 : -4), by - 2, bx + (bx < mid ? 4 : 0), by + 2);
    }
    if (jv_sel >= 0 && jv_sel < q_ed.njoins) {
        const ob_qjoin *k = &q_ed.joins[jv_sel];
        snprintf(line, sizeof line, "%s.%s = %s.%s: %s", k->left, k->lfield, k->right, k->rfield,
                 k->kind == OB_JOIN_INNER ? "Inner, only the rows that match"
                 : k->kind == OB_JOIN_LEFT ? "Left, every row of the first" : "Right, every row of the second");
    } else if (jv_pt >= 0) {
        snprintf(line, sizeof line, "%s.%s: now click or drop on a field of another table", q_ed.tables[jv_pt], jv[jv_pt].fields[jv_pf]);
    } else {
        snprintf(line, sizeof line, "%d join%s. Drag a field onto another table's field to join them.",
                 q_ed.njoins, q_ed.njoins == 1 ? "" : "s");
    }
    SetAPen(rp, pen(TEXTPEN));
    text_fit(rp, jv_cx, jv_sty, jv_cw, line);
}

/* The field under x, y: table and field, or 0. */
static int jv_hit_field(int x, int y, int *t, int *f)
{
    int i;
    for (i = 0; i < q_ed.ntables; i++) {
        const struct jv_box *b = &jv[i];
        int row = (y - b->y - 3) / jv_rowh - 1;
        if (x < b->x || x >= b->x + b->w || y < b->y + 3 + jv_rowh || row < 0 || row >= b->nf) continue;
        *t = i;
        *f = row;
        return 1;
    }
    return 0;
}

/* The join whose line passes within 3 pixels of x, y; -1 for none. */
static int jv_hit_join(int x, int y)
{
    int i, s;
    for (i = q_ed.njoins - 1; i >= 0; i--)
        for (s = 0; s < 3; s++) {
            int x1 = jv_seg[i][2 * s], y1 = jv_seg[i][2 * s + 1], x2 = jv_seg[i][2 * s + 2], y2 = jv_seg[i][2 * s + 3];
            int lo = x1 < x2 ? x1 : x2, hi = x1 < x2 ? x2 : x1, top = y1 < y2 ? y1 : y2, bot = y1 < y2 ? y2 : y1;
            if (!x1 && !x2) continue;
            if (x >= lo - 3 && x <= hi + 3 && y >= top - 3 && y <= bot + 3) return i;
        }
    return -1;
}

/* Joins the picked field to table t's field f. */
static void jv_join(int t, int f)
{
    int i;
    if (jv_pt < 0 || t == jv_pt) return;
    i = ob_query_add_join(&q_ed, q_ed.tables[jv_pt], jv[jv_pt].fields[jv_pf], q_ed.tables[t], jv[t].fields[f], OB_JOIN_INNER);
    if (i < 0) tell(q_ed.njoins >= OB_QUERY_JOINS ? "A query has at most 12 joins" : "Those two fields are joined already");
    else jv_sel = i;
    jv_pt = jv_pf = -1;
}

enum { JV_INNER = 1, JV_LEFT, JV_RIGHT, JV_REMOVE, JV_RELS, JV_CLOSE, JV_NBUTTONS = JV_CLOSE };

static void join_window(void)
{
    static const char *const btext[JV_NBUTTONS] = { "Inner", "Left", "Right", "Remove", "Relationships", "Close" };
    struct Window *jw;
    struct Gadget *eg = NULL, *g;
    struct NewGadget ng;
    int w, h, i, bw, bh = fh + 6, done = 0, down = 0;
    if (q_ed.ntables < 2) { tell("Add a second table first."); return; }
    if (q_ed.njoins == 0 && ob_query_relation_joins(proj, &q_ed) < 0) { tell(ob_error(proj)); return; }
    w = fw * 80;
    h = scr->Height * 3 / 4;
    if (w > scr->Width - 20) w = scr->Width - 20;
    jw = OpenWindowTags(NULL, WA_Title, (ULONG)"Joins", WA_PubScreen, (ULONG)scr,
        WA_Left, (scr->Width - w) / 2, WA_Top, scr->BarHeight + 1, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS | BUTTONIDCMP,
        TAG_DONE);
    if (!jw) { tell("The window could not be opened."); return; }
    w = jw->Width - jw->BorderLeft - jw->BorderRight;
    h = jw->Height - jw->BorderTop - jw->BorderBottom;
    jv_rowh = fh + 2;
    jv_cx = jw->BorderLeft + 8;
    jv_cy = jw->BorderTop + 6;
    jv_cw = w - 16;
    jv_ch = h - 6 - (fh + 6) - (bh + 8) - 4;
    jv_sty = jv_cy + jv_ch + 4;
    jv_sel = q_ed.njoins ? 0 : -1;
    jv_pt = jv_pf = -1;
    jv_layout();
    if (!(g = CreateContext(&eg))) { CloseWindow(jw); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_Height = bh;
    ng.ng_TopEdge = jw->Height - jw->BorderBottom - 6 - bh;
    bw = (jv_cw - 5 * 4) / JV_NBUTTONS;
    for (i = 0; i < JV_NBUTTONS && g; i++) {
        ng.ng_LeftEdge = jv_cx + i * (bw + 4);
        ng.ng_Width = bw;
        ng.ng_GadgetText = (UBYTE *)btext[i];
        ng.ng_GadgetID = i + 1;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    }
    if (!g) { FreeGadgets(eg); CloseWindow(jw); tell("Not enough memory."); return; }
    AddGList(jw, eg, ~0, -1, NULL);
    RefreshGList(eg, jw, NULL, -1);
    GT_RefreshWindow(jw, NULL);
    jv_draw(jw);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(jw->UserPort);
        while (!done && (m = GT_GetIMsg(jw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            WORD mx = m->MouseX, my = m->MouseY;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int redraw = 1, t, f;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
            if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(jw); GT_EndRefresh(jw, TRUE); continue; }
            if (class == IDCMP_VANILLAKEY) {
                if (code == 9 && q_ed.njoins) jv_sel = (jv_sel + 1) % q_ed.njoins;
                else if ((code == 127 || code == 8) && jv_sel >= 0) { ob_query_remove_join(&q_ed, jv_sel); jv_sel = -1; }
                else redraw = 0;
            } else if (class == IDCMP_MOUSEBUTTONS) {
                if (code == SELECTDOWN) {
                    if (jv_hit_field(mx, my, &t, &f)) {
                        if (jv_pt >= 0 && t != jv_pt) jv_join(t, f);       /* the second click */
                        else { jv_pt = t; jv_pf = f; jv_sel = -1; down = 1; }
                    } else {
                        jv_sel = jv_hit_join(mx, my);
                        jv_pt = jv_pf = -1;
                    }
                } else if (code == SELECTUP && down) {                       /* a drag ends */
                    down = 0;
                    if (jv_hit_field(mx, my, &t, &f) && t != jv_pt) jv_join(t, f);
                    else redraw = 0;
                } else redraw = 0;
            } else if (class == IDCMP_GADGETUP) {
                ob_qjoin *k = jv_sel >= 0 && jv_sel < q_ed.njoins ? &q_ed.joins[jv_sel] : NULL;
                switch (gg->GadgetID) {
                case JV_INNER: case JV_LEFT: case JV_RIGHT:
                    if (k) k->kind = gg->GadgetID == JV_LEFT ? OB_JOIN_LEFT : gg->GadgetID == JV_RIGHT ? OB_JOIN_RIGHT : OB_JOIN_INNER;
                    else tell("Click a join's line first.");
                    break;
                case JV_REMOVE:
                    if (k) { ob_query_remove_join(&q_ed, jv_sel); jv_sel = -1; }
                    else tell("Click a join's line first.");
                    break;
                case JV_RELS:
                    if ((i = ob_query_relation_joins(proj, &q_ed)) < 0) tell(ob_error(proj));
                    else if (!i) tell("Every relationship between these tables is drawn already.");
                    break;
                case JV_CLOSE: done = 1; break;
                }
                if (!done) ActivateWindow(jw);
            } else redraw = 0;
            if (!done && redraw) jv_draw(jw);
        }
    }
    RemoveGList(jw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(jw);
}

static void design_query(int fresh)
{
    struct Window *qw;
    struct Gadget *eg = NULL, *g, *gad[Q_COUNT];
    struct NewGadget ng;
    int done = 0, rebuild = 1, rowh = fh + 8, l, t, w, h, lw = fw * 10, csel = -1, tsel = -1, run_it = 0, i;
    if (!proj) return;
    if (fresh) {
        ob_query_init(&q_ed, "");
        if (have_cur) {
            ob_query_add_table(&q_ed, cur.name);
            ob_query_add_column(&q_ed, cur.name, "*");
        }
    } else {
        q_ed = qy;
    }
    w = fw * 72;
    h = rowh * 21 + 16;
    if (w > scr->Width - 20) w = scr->Width - 20;
    if (h > scr->Height - 40) h = scr->Height - 40;
    qw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Query",
        WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 20, WA_Top, win->TopEdge + 16,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  LISTVIEWIDCMP | CYCLEIDCMP | STRINGIDCMP | CHECKBOXIDCMP | BUTTONIDCMP | TEXTIDCMP,
        TAG_DONE);
    if (!qw) return;
    SetFont(qw->RPort, font);
    l = qw->BorderLeft + 8;
    t = qw->BorderTop + 6;
    while (!done) {
        if (rebuild) {
            int iw = qw->Width - qw->BorderLeft - qw->BorderRight - 16, y, half, bw = fw * 16 + 16;
            ob_qcolumn *c = csel >= 0 && csel < q_ed.ncolumns ? &q_ed.columns[csel] : NULL;
            int nc = !c || q_ed.sql_only;
            if (eg) { RemoveGList(qw, eg, -1); FreeGadgets(eg); eg = NULL; }
            memset(gad, 0, sizeof gad);
            q_lists();
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = vi;
            ng.ng_TextAttr = &font_attr;
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_LeftEdge = l + lw;
            ng.ng_TopEdge = y = t;
            ng.ng_Width = iw - lw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Name";
            ng.ng_GadgetID = Q_NAME;
            g = gad[Q_NAME] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)q_ed.name, GTST_MaxChars, OB_NAME_MAX - 1, TAG_DONE);
            ng.ng_TopEdge = y += rowh;
            ng.ng_Width = fh + 6;
            ng.ng_Flags = PLACETEXT_RIGHT;
            ng.ng_GadgetText = (UBYTE *)"Type the SQL";
            ng.ng_GadgetID = Q_SQLONLY;
            g = gad[Q_SQLONLY] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, q_ed.sql_only, GTCB_Scaled, TRUE, TAG_DONE);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_TopEdge = y += rowh;
            if (q_ed.sql_only) {
                ng.ng_Width = iw - lw;
                ng.ng_GadgetText = (UBYTE *)"SQL";
                ng.ng_GadgetID = Q_SQL;
                g = gad[Q_SQL] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)q_ed.sql, GTST_MaxChars, OB_SQL_MAX - 1, TAG_DONE);
                y += rowh;
            } else {
                half = (iw - lw) / 2 - 4;
                /* the tables, and adding or taking one away */
                ng.ng_Width = half;
                ng.ng_Height = rowh * 3;
                ng.ng_GadgetText = (UBYTE *)"Tables";
                ng.ng_GadgetID = Q_TABLES;
                g = gad[Q_TABLES] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&q_tl, GTLV_ShowSelected, 0,
                                                 GTLV_Selected, tsel >= 0 ? (ULONG)tsel : ~0UL, TAG_DONE);
                ng.ng_LeftEdge = l + lw + half + 8;
                ng.ng_Height = fh + 6;
                ng.ng_GadgetText = NULL;
                ng.ng_GadgetID = Q_ADDTABLE;
                g = gad[Q_ADDTABLE] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)q_tlabels, GTCY_Active, 0, TAG_DONE);
                ng.ng_TopEdge = y + rowh;
                ng.ng_Flags = PLACETEXT_IN;
                ng.ng_GadgetText = (UBYTE *)"Take Table Out";
                ng.ng_GadgetID = Q_REMTABLE;
                g = gad[Q_REMTABLE] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, tsel < 0, TAG_DONE);
                ng.ng_TopEdge = y + rowh * 2;
                ng.ng_GadgetText = (UBYTE *)(q_ed.njoins ? "Joins (drawn)..." : "Joins...");
                ng.ng_GadgetID = Q_JOINS;
                g = gad[Q_JOINS] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, q_ed.ntables < 2, TAG_DONE);
                ng.ng_Flags = PLACETEXT_LEFT;
                ng.ng_LeftEdge = l + lw;
                ng.ng_TopEdge = y += rowh * 3 + 4;
                /* the columns */
                ng.ng_Width = half;
                ng.ng_Height = rowh * 5;
                ng.ng_GadgetText = (UBYTE *)"Columns";
                ng.ng_GadgetID = Q_COLS;
                g = gad[Q_COLS] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&q_cl, GTLV_ShowSelected, 0,
                                               GTLV_Selected, csel >= 0 ? (ULONG)csel : ~0UL, TAG_DONE);
                ng.ng_LeftEdge = l + lw + half + 8;
                ng.ng_Height = fh + 6;
                ng.ng_GadgetText = NULL;
                ng.ng_GadgetID = Q_ADDFIELD;
                g = gad[Q_ADDFIELD] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)q_flabels, GTCY_Active, 0,
                                                   GA_Disabled, q_nf == 0, TAG_DONE);
                ng.ng_Flags = PLACETEXT_IN;
                ng.ng_TopEdge = y + rowh;
                ng.ng_GadgetText = (UBYTE *)"Add an Expression";
                ng.ng_GadgetID = Q_ADDEXPR;
                g = gad[Q_ADDEXPR] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
                ng.ng_TopEdge = y + rowh * 2;
                ng.ng_GadgetText = (UBYTE *)"Take Column Out";
                ng.ng_GadgetID = Q_REMCOL;
                g = gad[Q_REMCOL] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, c == NULL, TAG_DONE);
                ng.ng_Flags = PLACETEXT_LEFT;
                ng.ng_LeftEdge = l + lw;
                ng.ng_TopEdge = y += rowh * 5 + 4;
                /* the chosen column */
                ng.ng_Width = iw - lw;
                ng.ng_GadgetText = (UBYTE *)"Expression";
                ng.ng_GadgetID = Q_EXPR;
                g = gad[Q_EXPR] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(c && !c->table[0] ? c->expr : ""),
                                               GTST_MaxChars, OB_EXPR_MAX - 1, GA_Disabled, nc || c->table[0], TAG_DONE);
                ng.ng_TopEdge = y += rowh;
                ng.ng_GadgetText = (UBYTE *)"Heading";
                ng.ng_GadgetID = Q_HEAD;
                g = gad[Q_HEAD] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(c ? c->heading : ""),
                                               GTST_MaxChars, OB_NAME_MAX - 1, GA_Disabled, nc, TAG_DONE);
                ng.ng_TopEdge = y += rowh;
                ng.ng_Width = half;
                ng.ng_GadgetText = (UBYTE *)"Sort";
                ng.ng_GadgetID = Q_SORT;
                g = gad[Q_SORT] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)q_sort_labels,
                                               GTCY_Active, (ULONG)(c ? c->sort : 0), GA_Disabled, nc, TAG_DONE);
                ng.ng_LeftEdge = l + lw + half + 8 + fw * 6;
                ng.ng_Width = half - fw * 6;
                ng.ng_GadgetText = (UBYTE *)"Total";
                ng.ng_GadgetID = Q_TOTAL;
                g = gad[Q_TOTAL] = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)q_total_labels,
                                                GTCY_Active, (ULONG)(c ? c->total : 0), GA_Disabled, nc, TAG_DONE);
                ng.ng_LeftEdge = l + lw;
                ng.ng_TopEdge = y += rowh;
                ng.ng_Width = fh + 6;
                ng.ng_Flags = PLACETEXT_RIGHT;
                ng.ng_GadgetText = (UBYTE *)"Show";
                ng.ng_GadgetID = Q_SHOW;
                g = gad[Q_SHOW] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, c ? c->show : 0, GTCB_Scaled, TRUE,
                                               GA_Disabled, nc, TAG_DONE);
                ng.ng_Flags = PLACETEXT_LEFT;
                ng.ng_Width = iw - lw;
                for (i = 0; i < OB_QUERY_OR; i++) {
                    ng.ng_TopEdge = y += rowh;
                    ng.ng_GadgetText = (UBYTE *)(i ? "or" : "Criteria");
                    ng.ng_GadgetID = Q_CRIT0 + i;
                    g = gad[Q_CRIT0 + i] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(c ? c->criteria[i] : ""),
                                                        GTST_MaxChars, OB_VALUE_MAX - 1, GA_Disabled, nc, TAG_DONE);
                }
                ng.ng_TopEdge = y += rowh;
                ng.ng_GadgetText = (UBYTE *)"SQL";
                ng.ng_GadgetID = Q_SQLTEXT;
                g = gad[Q_SQLTEXT] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)q_sqltext, GTTX_Border, TRUE, TAG_DONE);
            }
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = qw->Height - qw->BorderBottom - 6 - (fh + 6);
            ng.ng_LeftEdge = l;
            ng.ng_Width = bw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)"Run";
            ng.ng_GadgetID = Q_RUN;
            g = gad[Q_RUN] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = (qw->Width - bw) / 2;
            ng.ng_GadgetText = (UBYTE *)"Save";
            ng.ng_GadgetID = Q_SAVE;
            g = gad[Q_SAVE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = qw->Width - qw->BorderRight - 8 - bw;
            ng.ng_GadgetText = (UBYTE *)"Close";
            ng.ng_GadgetID = Q_CLOSE;
            g = gad[Q_CLOSE] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(qw->RPort, pen(BACKGROUNDPEN));
            RectFill(qw->RPort, qw->BorderLeft, qw->BorderTop, qw->Width - qw->BorderRight - 1,
                     qw->Height - qw->BorderBottom - 1);
            AddGList(qw, eg, ~0, -1, NULL);
            RefreshGList(eg, qw, NULL, -1);
            GT_RefreshWindow(qw, NULL);
            if (q_ed.sql_only && gad[Q_SQL]) ActivateGadget(gad[Q_SQL], qw, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(qw->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(qw->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                ob_qcolumn *c = csel >= 0 && csel < q_ed.ncolumns ? &q_ed.columns[csel] : NULL;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(qw); GT_EndRefresh(qw, TRUE); continue; }
                if (class != IDCMP_GADGETUP && class != IDCMP_GADGETDOWN) continue;
                q_read_strings(qw, gad, csel);
                switch (gg->GadgetID) {
                case Q_NAME: case Q_SQL: break;
                case Q_EXPR: case Q_HEAD: case Q_CRIT0: case Q_CRIT1: case Q_CRIT2: rebuild = 1; break;
                case Q_SQLONLY:
                    q_ed.sql_only = (gg->Flags & GFLG_SELECTED) != 0;
                    /* typing SQL starts from what the grid makes */
                    if (q_ed.sql_only && !q_ed.sql[0] && q_sqltext[0] && q_sqltext[0] != '(')
                        put_text(q_ed.sql, sizeof q_ed.sql, q_sqltext);
                    rebuild = 1;
                    break;
                case Q_TABLES: tsel = code; rebuild = 1; break;
                case Q_ADDTABLE:
                    if (code > 0 && q_tlabels[code]) {
                        if (ob_query_add_table(&q_ed, (const char *)q_tlabels[code]) < 0)
                            tell(q_ed.ntables >= OB_QUERY_TABLES ? "A query has at most 8 tables" : "That table is in the query already");
                        else if (q_ed.njoins > 0 && ob_query_relation_joins(proj, &q_ed) < 0)   /* drawn: draw its relationships too */
                            tell(ob_error(proj));
                        rebuild = 1;
                    }
                    break;
                case Q_REMTABLE:
                    if (tsel >= 0 && tsel < q_ed.ntables) { ob_query_remove_table(&q_ed, tsel); tsel = -1; csel = -1; rebuild = 1; }
                    break;
                case Q_JOINS: join_window(); rebuild = 1; break;
                case Q_COLS: csel = code; rebuild = 1; break;
                case Q_ADDFIELD:
                    if (code > 0 && code <= q_nf) {
                        char tn[2 * OB_NAME_MAX + 4], *dot;
                        put_text(tn, sizeof tn, q_ftext[code - 1]);
                        dot = strrchr(tn, '.');
                        if (dot) {
                            *dot = 0;
                            if (!ob_query_add_column(&q_ed, tn, dot + 1)) tell("A query has at most 16 columns");
                            else csel = q_ed.ncolumns - 1;
                        }
                        rebuild = 1;
                    }
                    break;
                case Q_ADDEXPR:
                    if (!ob_query_add_column(&q_ed, NULL, "")) tell("A query has at most 16 columns");
                    else csel = q_ed.ncolumns - 1;
                    rebuild = 1;
                    break;
                case Q_REMCOL:
                    if (c) { ob_query_remove_column(&q_ed, csel); csel = -1; rebuild = 1; }
                    break;
                case Q_SORT: if (c) { c->sort = code; rebuild = 1; } break;
                case Q_TOTAL: if (c && code < OB_TOTALS) { c->total = (ob_total)code; rebuild = 1; } break;
                case Q_SHOW: if (c) { c->show = (gg->Flags & GFLG_SELECTED) != 0; rebuild = 1; } break;
                case Q_RUN:
                    if (!ob_query_check(proj, &q_ed)) {
                        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
                        EasyRequest(qw, &es, NULL, (ULONG)ob_error(proj));
                    } else {
                        run_it = done = 1;
                    }
                    break;
                case Q_SAVE: {
                    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
                    if (!q_ed.name[0]) EasyRequest(qw, &es, NULL, (ULONG)"Give the query a name first");
                    else if (!ob_query_save(proj, &q_ed)) EasyRequest(qw, &es, NULL, (ULONG)ob_error(proj));
                    else {
                        char msg[OB_NAME_MAX + 20];
                        snprintf(msg, sizeof msg, "Saved %s", q_ed.name);
                        set_status(msg);
                        SetWindowTitles(qw, (UBYTE *)"Query (saved)", (UBYTE *)~0);
                    }
                    break;
                }
                case Q_CLOSE: done = 1; break;
                }
            }
        }
    }
    if (eg) { RemoveGList(qw, eg, -1); FreeGadgets(eg); }
    CloseWindow(qw);
    if (win) ActivateWindow(win);     /* keys go to the main window again */
    if (run_it) {
        int k;
        qy = q_ed;
        for (k = 0; k < OB_QUERY_PARAMS; k++) q_answers[k][0] = 0;
        run_query(1);
    }
}

/* The saved queries: run one, design it, or delete it. */
enum { O_LIST = 1, O_RUN, O_DESIGN, O_DELETE, O_CANCEL, O_COUNT };

static struct List o_list;
static struct Node *o_nodes;
static char (*o_names)[OB_NAME_MAX];

static void open_query(void)
{
    struct Window *ow;
    struct Gadget *eg = NULL, *g, *gad[O_COUNT];
    struct NewGadget ng;
    int done = 0, rebuild = 1, sel = -1, n = 0, i, w = fw * 44, h = (fh + 8) * 12 + 12, bw, act = 0;
    if (!proj) return;
    ow = OpenWindowTags(NULL, WA_Title, (ULONG)"Open Query", WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | LISTVIEWIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!ow) return;
    while (!done) {
        if (rebuild) {
            if (eg) { RemoveGList(ow, eg, -1); FreeGadgets(eg); eg = NULL; }
            free(o_nodes); o_nodes = NULL;
            free(o_names); o_names = NULL;
            NewList(&o_list);
            n = ob_query_count(proj);
            if (n > 0 && (o_nodes = calloc((size_t)n, sizeof *o_nodes)) != NULL
                && (o_names = calloc((size_t)n, sizeof *o_names)) != NULL) {
                for (i = 0; i < n; i++) {
                    const char *nm = ob_query_name(proj, i);
                    put_text(o_names[i], OB_NAME_MAX, nm ? nm : "");
                    o_nodes[i].ln_Name = o_names[i];
                    AddTail(&o_list, &o_nodes[i]);
                }
            } else {
                n = 0;
            }
            if (sel >= n) sel = -1;
            memset(gad, 0, sizeof gad);
            if (!(g = CreateContext(&eg))) break;
            memset(&ng, 0, sizeof ng);
            ng.ng_VisualInfo = vi;
            ng.ng_TextAttr = &font_attr;
            ng.ng_LeftEdge = ow->BorderLeft + 8;
            ng.ng_TopEdge = ow->BorderTop + 6;
            ng.ng_Width = w - 16;
            ng.ng_Height = h - (fh + 6) - 24;
            ng.ng_GadgetID = O_LIST;
            g = gad[O_LIST] = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&o_list, GTLV_ShowSelected, 0,
                                           GTLV_Selected, sel >= 0 ? (ULONG)sel : ~0UL, TAG_DONE);
            bw = (w - 16 - 3 * 6) / 4;
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = ow->Height - ow->BorderBottom - 6 - (fh + 6);
            ng.ng_Height = fh + 6;
            ng.ng_Width = bw;
            ng.ng_GadgetText = (UBYTE *)"Run";
            ng.ng_GadgetID = O_RUN;
            g = gad[O_RUN] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, sel < 0, TAG_DONE);
            ng.ng_LeftEdge += bw + 6;
            ng.ng_GadgetText = (UBYTE *)"Design...";
            ng.ng_GadgetID = O_DESIGN;
            g = gad[O_DESIGN] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, sel < 0, TAG_DONE);
            ng.ng_LeftEdge += bw + 6;
            ng.ng_GadgetText = (UBYTE *)"Delete...";
            ng.ng_GadgetID = O_DELETE;
            g = gad[O_DELETE] = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, sel < 0, TAG_DONE);
            ng.ng_LeftEdge += bw + 6;
            ng.ng_GadgetText = (UBYTE *)"Cancel";
            ng.ng_GadgetID = O_CANCEL;
            g = gad[O_CANCEL] = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            if (!g) break;
            SetAPen(ow->RPort, pen(BACKGROUNDPEN));
            RectFill(ow->RPort, ow->BorderLeft, ow->BorderTop, ow->Width - ow->BorderRight - 1,
                     ow->Height - ow->BorderBottom - 1);
            if (!n) {
                SetAPen(ow->RPort, pen(TEXTPEN));
                SetFont(ow->RPort, font);
                text_fit(ow->RPort, ow->BorderLeft + 16, ow->BorderTop + 14, w - 32, "No saved queries yet: Query > New Query...");
            }
            AddGList(ow, eg, ~0, -1, NULL);
            RefreshGList(eg, ow, NULL, -1);
            GT_RefreshWindow(ow, NULL);
            rebuild = 0;
        }
        {
            struct IntuiMessage *m;
            WaitPort(ow->UserPort);
            while (!rebuild && !done && (m = GT_GetIMsg(ow->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gg = (struct Gadget *)m->IAddress;
                GT_ReplyIMsg(m);
                if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; break; }
                if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(ow); GT_EndRefresh(ow, TRUE); continue; }
                if (class != IDCMP_GADGETUP) continue;
                switch (gg->GadgetID) {
                case O_LIST: sel = code; rebuild = 1; break;
                case O_RUN: act = 1; done = 1; break;
                case O_DESIGN: act = 2; done = 1; break;
                case O_DELETE:
                    if (sel >= 0 && sel < n) {
                        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"Delete the query %s?",
                                                 (UBYTE *)"Delete|Cancel" };
                        if (EasyRequest(ow, &es, NULL, (ULONG)o_names[sel]) == 1) {
                            if (!ob_query_delete(proj, o_names[sel])) {
                                struct EasyStruct e2 = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
                                EasyRequest(ow, &e2, NULL, (ULONG)ob_error(proj));
                            }
                            sel = -1;
                            rebuild = 1;
                        }
                    }
                    break;
                case O_CANCEL: done = 1; break;
                }
            }
        }
    }
    if (act && sel >= 0 && sel < n) {
        static ob_query got;
        if (!ob_query_get(proj, o_names[sel], &got)) {
            tell(ob_error(proj));
            act = 0;
        } else {
            int k;
            qy = got;
            for (k = 0; k < OB_QUERY_PARAMS; k++) q_answers[k][0] = 0;
        }
    }
    if (eg) { RemoveGList(ow, eg, -1); FreeGadgets(eg); }
    CloseWindow(ow);
    if (win) ActivateWindow(win);     /* keys go to the main window again */
    free(o_nodes); o_nodes = NULL;
    free(o_names); o_names = NULL;
    if (act == 1) run_query(1);
    else if (act == 2) design_query(0);
}

/* ---- Forms: the table one record at a time (design 6.1, 6.3, 6.4) ----
 * An auto form of the current table, in the datasheet's sort and filter:
 * a row a field, a bar to move between records, edits kept for the record
 * and written with one change when you leave it, Save or Close; Undo Record
 * puts them back. A table that other tables look up shows their rows for
 * the record (a subform). */

enum {
    F_FIRST = 1, F_PREV, F_POS, F_NEXT, F_LAST, F_NEW, F_DELETE, F_FIND, F_SAVE, F_UNDO, F_CLOSE,
    F_BACK, F_ON, F_SUBPICK, F_SUBLIST, F_SUBCOUNT,
    F_FILTER, F_FROW, F_FHINT, F_FCLEAR, F_FCANCEL,     /* Filter by Form */
    F_SUBADD, F_SUBEDIT, F_SUBDEL, F_SUBOPEN,           /* the subform's rows */
    F_FIELD0
};

#define FORM_SUB_MAX 40
#define FORM_SUBS 8

static char *f_vals[OB_MAX_FIELDS], *f_before[OB_MAX_FIELDS];
static ob_int f_pos, f_key, f_total;         /* f_pos -1: a new record */
static ob_relation f_subs[FORM_SUBS];
static int f_nsubs, f_sub;
static STRPTR f_sub_labels[FORM_SUBS + 1];
static char f_sub_text[FORM_SUBS][2 * OB_NAME_MAX + 8];
static ob_table f_child;                     /* the subform's table */
static struct List f_sl;
static struct Node f_snodes[FORM_SUB_MAX];
static char f_stext[FORM_SUB_MAX][160];
static int f_sn;
static char f_count[64], f_scount[80];
static int f_ssel = -1, f_ssub = -1;        /* the subform row chosen, for record f_skey */
static ob_int f_skey = -2;
static int f_slines = 7;                     /* the subform list's lines */
static ob_relation f_go_rel;                 /* a subform row to open in its own form */
static ob_form *f_design;                    /* the designed form shown, NULL: the auto form */
#define FG_MAX 64                            /* field gadgets on one designed form */
static ob_int f_go_parent, f_go_key;
static int f_go;

/* Filter by Form (design 6.3): the form's fields take what to look for, a
 * row at a time (Look for, Or, Or); Apply Filter makes it the view's. */
static int f_filtering, f_frow;
static ob_view f_fview;                      /* the criteria being typed */
static STRPTR f_frow_labels[] = { (STRPTR)"Look for", (STRPTR)"Or", (STRPTR)"Or (2)", NULL };
static char f_fhint[96];

static int form_row_cb(void *user, ob_int key, int n, const char *const *v)
{
    int i;
    (void)user;
    f_key = key;
    for (i = 0; i < n && i < OB_MAX_FIELDS; i++) {
        put_text(f_vals[i], 1024, v[i] ? v[i] : "");
        put_text(f_before[i], 1024, f_vals[i]);
    }
    return 1;
}

/* Reads record f_pos, or a new record's defaults when f_pos is -1 (or the
 * view is empty). */
static void form_read(void)
{
    int i;
    f_total = ob_view_total(proj, cur.name, &view);
    if (f_total < 0) f_total = 0;
    if (f_pos >= f_total) f_pos = f_total - 1;
    for (i = 0; i < cur.nfields; i++) { f_vals[i][0] = 0; f_before[i][0] = 0; }
    f_key = -1;
    if (f_pos >= 0) {
        ob_view_rows(proj, cur.name, &view, f_pos, 1, form_row_cb, NULL);
    } else {
        for (i = 0; i < cur.nfields; i++)
            if (cur.fields[i].has_default) put_text(f_vals[i], 1024, cur.fields[i].deflt);
    }
}

static int form_sub_cb(void *user, ob_int key, int n, const char *const *v)
{
    int i;
    size_t len = 0;
    const char *skip = user;
    char *out = f_stext[f_sn];
    (void)key;
    if (f_sn >= FORM_SUB_MAX) return 1;
    out[0] = 0;
    for (i = 0; i < n && i < f_child.nfields; i++) {
        if (!strcmp(f_child.fields[i].name, skip) || f_child.fields[i].type == OB_COUNTER) continue;
        len = strlen(out);
        snprintf(out + len, sizeof f_stext[0] - len, "%s%s", len ? " | " : "", v[i] ? v[i] : "");
    }
    f_snodes[f_sn].ln_Name = out;
    AddTail(&f_sl, &f_snodes[f_sn]);
    f_sn++;
    return 0;
}

/* The subform's rows for this record. */
static void form_subrows(void)
{
    ob_view kids;
    ob_int n = 0;
    NewList(&f_sl);
    f_sn = 0;
    f_scount[0] = 0;
    if (f_key != f_skey || f_sub != f_ssub) { f_ssel = -1; f_skey = f_key; f_ssub = f_sub; }
    if (!f_nsubs) return;
    if (f_key <= 0) { put_text(f_scount, sizeof f_scount, "Save the record to see its rows"); return; }
    if (!ob_table_get(proj, f_subs[f_sub].table, &f_child)) { put_text(f_scount, sizeof f_scount, ob_error(proj)); return; }
    ob_view_init(&kids);
    put_text(kids.field, sizeof kids.field, f_subs[f_sub].field);
    kids.op = OB_FILTER_EQUALS;
    snprintf(kids.value, sizeof kids.value, "%lld", (long long)f_key);
    n = ob_view_total(proj, f_child.name, &kids);
    if (n < 0) { put_text(f_scount, sizeof f_scount, ob_error(proj)); return; }
    ob_view_rows(proj, f_child.name, &kids, 0, FORM_SUB_MAX, form_sub_cb, f_subs[f_sub].field);
    if (n > FORM_SUB_MAX) snprintf(f_scount, sizeof f_scount, "The first %d of %lld rows", FORM_SUB_MAX, (long long)n);
    else snprintf(f_scount, sizeof f_scount, "%lld row%s", (long long)n, n == 1 ? "" : "s");
    if (f_ssel >= f_sn) f_ssel = -1;
}

static int form_changed(void)
{
    int i;
    for (i = 0; i < cur.nfields; i++)
        if (strcmp(f_vals[i], f_before[i])) return 1;
    return 0;
}

/* Writes the record's edits: 1 when done (or nothing to do), 0 with the reason told. */
static int form_save(struct Window *fw_)
{
    const char *names[OB_MAX_FIELDS], *vals[OB_MAX_FIELDS];
    int i, n = 0, good;
    ob_int key = f_key;
    if (f_key > 0 && !form_changed()) return 1;
    for (i = 0; i < cur.nfields; i++) {
        const ob_field *f = &cur.fields[i];
        if (f->type == OB_COUNTER || f->type == OB_OBJECT) continue;
        if (f_key < 0 ? f_vals[i][0] == 0 && !f->has_default : !strcmp(f_vals[i], f_before[i])) continue;
        names[n] = f->name;
        vals[n] = f_vals[i];
        n++;
    }
    if (f_key < 0) good = ob_row_add(proj, cur.name, n, names, vals, &key);
    else good = ob_row_change(proj, cur.name, f_key, n, names, vals);
    if (!good) {
        struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
        EasyRequest(fw_, &es, NULL, (ULONG)ob_error(proj));
        return 0;
    }
    if (f_key < 0) {
        ob_int pos = ob_view_position(proj, cur.name, &view, key);
        f_total = ob_view_total(proj, cur.name, &view);
        f_pos = pos >= 0 ? pos : f_total - 1;
        set_status(pos >= 0 ? "Record added" : "Record added; the filter leaves it out");
    } else {
        set_status("Record saved");
    }
    return 1;
}

static STRPTR f_find_labels[OB_MAX_FIELDS + 1];
static char f_find_text[OB_VALUE_MAX];
static int f_find_field;

/* Find: a field and the text; 1 next, -1 back, 0 cancel. */
static int form_ask_find(struct Window *parent)
{
    struct Window *aw;
    struct Gadget *eg = NULL, *g, *sg, *cg;
    struct NewGadget ng;
    int i, k = 0, act = 0, w = fw * 48, h = (fh + 8) * 4 + 12, lw = fw * 7 + 12;
    for (i = 0; i < cur.nfields; i++)
        if (cur.fields[i].type != OB_OBJECT) f_find_labels[k++] = (STRPTR)(cur.fields[i].label[0] ? cur.fields[i].label : cur.fields[i].name);
    f_find_labels[k] = NULL;
    if (!k) return 0;
    if (f_find_field >= k) f_find_field = 0;
    aw = OpenWindowTags(NULL, WA_Title, (ULONG)"Find", WA_PubScreen, (ULONG)scr,
        WA_Left, parent->LeftEdge + 40, WA_Top, parent->TopEdge + 40, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP | BUTTONIDCMP | CYCLEIDCMP,
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
    ng.ng_GadgetText = (UBYTE *)"Field";
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_GadgetID = 1;
    g = cg = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)f_find_labels, GTCY_Active, f_find_field, TAG_DONE);
    ng.ng_TopEdge += fh + 10;
    ng.ng_GadgetText = (UBYTE *)"Find";
    ng.ng_GadgetID = 2;
    g = sg = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)f_find_text, GTST_MaxChars, OB_VALUE_MAX - 1, TAG_DONE);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = aw->Height - aw->BorderBottom - 6 - (fh + 6);
    ng.ng_LeftEdge = aw->BorderLeft + 8;
    ng.ng_Width = fw * 10 + 16;
    ng.ng_GadgetText = (UBYTE *)"Find Next";
    ng.ng_GadgetID = 3;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 8;
    ng.ng_GadgetText = (UBYTE *)"Find Back";
    ng.ng_GadgetID = 4;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = aw->Width - aw->BorderRight - 8 - ng.ng_Width;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = 5;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(aw); return 0; }
    AddGList(aw, eg, ~0, -1, NULL);
    RefreshGList(eg, aw, NULL, -1);
    GT_RefreshWindow(aw, NULL);
    ActivateGadget(sg, aw, NULL);
    while (!act) {
        struct IntuiMessage *m;
        WaitPort(aw->UserPort);
        while ((m = GT_GetIMsg(aw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) act = 9;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(aw); GT_EndRefresh(aw, TRUE); }
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 1) f_find_field = code;
            else if (class == IDCMP_GADGETUP && (gg->GadgetID == 2 || gg->GadgetID == 3)) act = 1;
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 4) act = -1;
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 5) act = 9;
        }
    }
    {
        STRPTR str = NULL;
        GT_GetGadgetAttrs(sg, aw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
        put_text(f_find_text, sizeof f_find_text, str ? (const char *)str : "");
    }
    RemoveGList(aw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(aw);
    return act == 9 ? 0 : act;
}

/* The field the Find window's choice names (Datatype objects are not listed). */
static const char *form_find_name(void)
{
    int i, k = 0;
    for (i = 0; i < cur.nfields; i++) {
        if (cur.fields[i].type == OB_OBJECT) continue;
        if (k++ == f_find_field) return cur.fields[i].name;
    }
    return cur.nfields ? cur.fields[0].name : "";
}

static int form_window(void);

/* Text from x on the baseline y, only the part between minx and maxx. */
static void text_clip(struct RastPort *rp, int x, int y, const char *t, int len, int minx, int maxx)
{
    while (len > 0 && x < minx) { x += TextLength(rp, (STRPTR)t, 1); t++; len--; }
    while (len > 0 && x + TextLength(rp, (STRPTR)t, len) > maxx) len--;
    if (len <= 0) return;
    Move(rp, x, y);
    Text(rp, (STRPTR)t, len);
}

/* A designed form's labels, lines and boxes, at x0, y0 in cells of fw by
 * row_h, kept inside minx, miny to maxx, maxy. fields: its field items too,
 * as raised boxes with the field's name (the designer; a shown form has
 * gadgets there). */
static void form_draw_clip(struct Window *w_, const ob_form *f, int x0, int y0, int row_h,
                           int minx, int miny, int maxx, int maxy, int fields)
{
    struct RastPort *rp = w_->RPort;
    int i;
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    for (i = 0; i < f->nitems; i++) {
        const ob_fitem *it = &f->items[i];
        int x = x0 + it->x * fw, y = y0 + it->y * row_h, w = it->w * fw, h = it->h * row_h;
        int cx1 = x < minx ? minx : x, cy1 = y < miny ? miny : y;
        int cx2 = x + w > maxx ? maxx : x + w, cy2 = y + h > maxy ? maxy : y + h;
        if (cx1 >= cx2 || cy1 >= cy2) continue;
        if (it->kind == OB_FI_FIELD) {
            if (!fields) continue;
            if (cx2 - cx1 > 2 && cy2 - cy1 > 2)
                DrawBevelBox(rp, cx1, cy1, cx2 - cx1, cy2 - cy1, GT_VisualInfo, (ULONG)vi, TAG_DONE);
            SetAPen(rp, pen(TEXTPEN));
            if (y + (row_h - fh) / 2 >= miny && y + (row_h + fh) / 2 <= maxy)
                text_clip(rp, x + fw / 2, y + (row_h - fh) / 2 + font->tf_Baseline, it->field,
                          (int)strlen(it->field), minx, cx2 - fw / 2);
        } else if (it->kind == OB_FI_LABEL || (it->kind == OB_FI_BOX && it->text[0])) {
            int len = (int)strlen(it->text), room = (w - (it->kind == OB_FI_BOX ? 2 * fw : 0)) / fw;
            int ty = it->kind == OB_FI_BOX ? y + font->tf_Baseline / 2 : y + (row_h - fh) / 2 + font->tf_Baseline;
            if (len > room) len = room;
            if (it->kind == OB_FI_BOX) {
                int by = y + fh / 2 < miny ? miny : y + fh / 2;
                if (cx2 - cx1 > 2 && cy2 - by > 2)
                    DrawBevelBox(rp, cx1, by, cx2 - cx1, cy2 - by, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, TRUE, TAG_DONE);
            }
            if (len <= 0 || ty - font->tf_Baseline < miny || ty - font->tf_Baseline + fh > maxy) continue;
            SetAPen(rp, pen(it->kind == OB_FI_BOX ? HIGHLIGHTTEXTPEN : TEXTPEN));
            if (it->kind == OB_FI_BOX) {
                SetBPen(rp, pen(BACKGROUNDPEN));
                SetDrMd(rp, JAM2);
            }
            text_clip(rp, x + (it->kind == OB_FI_BOX ? fw : 0), ty, it->text, len, minx, maxx);
            SetDrMd(rp, JAM1);
        } else if (it->kind == OB_FI_BOX) {
            if (cx2 - cx1 > 2 && cy2 - cy1 > 2)
                DrawBevelBox(rp, cx1, cy1, cx2 - cx1, cy2 - cy1, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, TRUE, TAG_DONE);
        } else if (it->kind == OB_FI_LINE) {
            int across = it->h == 1, ly = y + row_h / 2, lx = x + fw / 2;
            if (across ? ly < miny || ly + 1 >= maxy : lx < minx || lx + 1 >= maxx) continue;
            SetAPen(rp, pen(SHADOWPEN));
            if (across) { Move(rp, cx1, ly); Draw(rp, cx2 - 1, ly); }
            else { Move(rp, lx, cy1); Draw(rp, lx, cy2 - 1); }
            SetAPen(rp, pen(SHINEPEN));
            if (across) { Move(rp, cx1, ly + 1); Draw(rp, cx2 - 1, ly + 1); }
            else { Move(rp, lx + 1, cy1); Draw(rp, lx + 1, cy2 - 1); }
        }
    }
}

static void form_draw(struct Window *w_, const ob_form *f, int x0, int y0, int row_h)
{
    form_draw_clip(w_, f, x0, y0, row_h, w_->BorderLeft, w_->BorderTop,
                   w_->Width - w_->BorderRight - 2, w_->Height - w_->BorderBottom - 2, 0);
}

/* Table > Open as Form. A subform row opened from it (Open Form, Return or
 * a double-click on the row) closes this form and opens its table's, in
 * the view of that record's rows, at the row. */
static void open_form(void)
{
    while (form_window()) {
        ob_int at;
        f_design = NULL;                       /* its table's auto form (the caller frees its own) */
        load_tables(f_go_rel.table);
        if (!have_cur || strcmp(cur.name, f_go_rel.table)) { tell("The subform's table isn't there any more."); break; }
        ob_child_view(&view, &f_go_rel, f_go_parent);
        put_text(view_table, sizeof view_table, cur.name);
        at = ob_view_position(proj, cur.name, &view, f_go_key);
        top_row = 0;
        sel_row = at >= 0 ? (int)at : 0;
        load_page();
        refresh();
        set_status("The record's rows: Table > Show All Rows shows every row");
    }
}

/* A list to choose from, Return or the first button taking the one chosen;
 * its index, -1 on Cancel. */
#define PICK_MAX 128
static int pick_list(const char *title, const char *const *labels, int n, const char *ok_text)
{
    static struct Node nodes[PICK_MAX];
    static struct List list;
    struct Window *pw;
    struct Gadget *eg = NULL, *g;
    struct NewGadget ng;
    int i, sel = 0, act = 0, w = fw * 40, h = (fh + 2) * 10 + fh + 30;
    if (n > PICK_MAX) n = PICK_MAX;
    if (n <= 0) return -1;
    NewList(&list);
    for (i = 0; i < n; i++) { nodes[i].ln_Name = (char *)labels[i]; AddTail(&list, &nodes[i]); }
    pw = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | LISTVIEWIDCMP | BUTTONIDCMP,
        TAG_DONE);
    if (!pw) return -1;
    if (!(g = CreateContext(&eg))) { CloseWindow(pw); return -1; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_LeftEdge = pw->BorderLeft + 8;
    ng.ng_TopEdge = pw->BorderTop + 6;
    ng.ng_Width = w - 16;
    ng.ng_Height = (fh + 2) * 10;
    ng.ng_GadgetID = 1;
    g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&list, GTLV_ShowSelected, 0, GTLV_Selected, 0, TAG_DONE);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = pw->Height - pw->BorderBottom - 6 - (fh + 6);
    ng.ng_Height = fh + 6;
    ng.ng_Width = fw * 10 + 12;
    ng.ng_GadgetText = (UBYTE *)ok_text;
    ng.ng_GadgetID = 2;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = pw->Width - pw->BorderRight - 8 - ng.ng_Width;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = 3;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(pw); return -1; }
    AddGList(pw, eg, ~0, -1, NULL);
    RefreshGList(eg, pw, NULL, -1);
    GT_RefreshWindow(pw, NULL);
    while (!act) {
        struct IntuiMessage *m;
        WaitPort(pw->UserPort);
        while ((m = GT_GetIMsg(pw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) act = 3;
            else if (class == IDCMP_VANILLAKEY && code == 13) act = 2;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(pw); GT_EndRefresh(pw, TRUE); }
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 1) sel = code;
            else if (class == IDCMP_GADGETUP && (gg->GadgetID == 2 || gg->GadgetID == 3)) act = gg->GadgetID;
        }
    }
    RemoveGList(pw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(pw);
    return act == 2 && sel >= 0 && sel < n ? sel : -1;
}

/* Table > Open Form...: the table's auto form or one of its designed forms. */
#define PICK_FORMS 32
static void open_designed(void)
{
    static char names[PICK_FORMS][OB_NAME_MAX];
    const char *labels[PICK_FORMS + 1];
    int n, i, sel;
    if (q_on || !proj || !have_cur) { set_status("Choose a table first"); return; }
    n = ob_forms_of(proj, cur.name, names, PICK_FORMS);
    if (n < 0) { tell(ob_error(proj)); return; }
    if (n > PICK_FORMS) n = PICK_FORMS;
    labels[0] = "(Auto form)";
    for (i = 0; i < n; i++) labels[i + 1] = names[i];
    sel = pick_list("Open Form", labels, n + 1, "Open");
    if (sel < 0) { ActivateWindow(win); return; }
    if (sel == 0) { open_form(); return; }
    {
        ob_form *mine = malloc(sizeof *mine);    /* open_form may move on to an auto form */
        if (!mine) { tell("Out of memory"); return; }
        if (!ob_form_get(proj, names[sel - 1], mine) || !ob_form_check(proj, mine)) {
            tell(ob_error(proj));
        } else {
            f_design = mine;
            open_form();
        }
        f_design = NULL;
        free(mine);
    }
}

/* ---- The form designer (Table > Design Form...) ---- */

static ob_form *d_form;         /* the form being designed */
static int d_sel = -1;          /* its chosen item */
static int d_ox, d_oy;          /* the cell at the canvas's top left */
static int d_cols, d_rows;      /* the cells the canvas shows */
static int d_changed;
static int d_cx, d_cy, d_cw, d_ch, d_rowh, d_sty;   /* the canvas, a cell's height, the status line */

static const char *const d_kinds[OB_FI_KINDS] = { "Field", "Label", "Line", "Box" };

/* Scrolls the canvas so the chosen item's top left is in sight. */
static void design_follow(void)
{
    const ob_fitem *it;
    if (d_sel < 0 || d_sel >= d_form->nitems) return;
    it = &d_form->items[d_sel];
    if (it->x < d_ox) d_ox = it->x;
    else if (it->x + (it->w < d_cols ? it->w : 1) > d_ox + d_cols) d_ox = it->x + (it->w < d_cols ? it->w : 1) - d_cols;
    if (it->y < d_oy) d_oy = it->y;
    else if (it->y + (it->h < d_rows ? it->h : 1) > d_oy + d_rows) d_oy = it->y + (it->h < d_rows ? it->h : 1) - d_rows;
    if (d_ox < 0) d_ox = 0;
    if (d_oy < 0) d_oy = 0;
}

static void design_draw(struct Window *dw)
{
    struct RastPort *rp = dw->RPort;
    char st[OB_NAME_MAX + OB_LABEL_MAX + 64];
    int x, y, x0 = d_cx - d_ox * fw, y0 = d_cy - d_oy * d_rowh;
    design_follow();
    x0 = d_cx - d_ox * fw;
    y0 = d_cy - d_oy * d_rowh;
    SetAPen(rp, pen(BACKGROUNDPEN));
    RectFill(rp, d_cx, d_cy, d_cx + d_cw - 1, d_cy + d_ch - 1);
    SetAPen(rp, pen(SHADOWPEN));                    /* a dot every 4 cells */
    for (y = (4 - d_oy % 4) % 4; y < d_rows; y += 4)
        for (x = (4 - d_ox % 4) % 4; x < d_cols; x += 4)
            WritePixel(rp, d_cx + x * fw, d_cy + y * d_rowh);
    form_draw_clip(dw, d_form, x0, y0, d_rowh, d_cx, d_cy, d_cx + d_cw, d_cy + d_ch, 1);
    if (d_sel >= 0 && d_sel < d_form->nitems) {
        const ob_fitem *it = &d_form->items[d_sel];
        int x1 = x0 + it->x * fw - 1, y1 = y0 + it->y * d_rowh - 1;
        int x2 = x0 + (it->x + it->w) * fw, y2 = y0 + (it->y + it->h) * d_rowh;
        if (x1 < d_cx) x1 = d_cx;
        if (y1 < d_cy) y1 = d_cy;
        if (x2 > d_cx + d_cw - 1) x2 = d_cx + d_cw - 1;
        if (y2 > d_cy + d_ch - 1) y2 = d_cy + d_ch - 1;
        if (x1 < x2 && y1 < y2) {
            SetAPen(rp, pen(FILLPEN));
            Move(rp, x1, y1); Draw(rp, x2, y1); Draw(rp, x2, y2); Draw(rp, x1, y2); Draw(rp, x1, y1);
        }
        snprintf(st, sizeof st, "%s %s at %d,%d, %d x %d%s", d_kinds[it->kind],
                 it->kind == OB_FI_FIELD ? it->field : it->text, it->x, it->y, it->w, it->h,
                 d_changed ? " (changed)" : "");
    } else {
        snprintf(st, sizeof st, "%d items%s: click one, or Tab; cursor keys move it, Shift sizes it",
                 d_form->nitems, d_changed ? " (changed)" : "");
    }
    SetAPen(rp, pen(BACKGROUNDPEN));
    RectFill(rp, d_cx, d_sty, d_cx + d_cw - 1, d_sty + fh);
    SetAPen(rp, pen(TEXTPEN));
    SetDrMd(rp, JAM1);
    SetFont(rp, font);
    text_fit(rp, d_cx, d_sty, d_cw, st);
}

/* Room for a new item: the first free row, or a word why not. */
static int design_row(void)
{
    int r = ob_form_free_row(d_form);
    if (r < 0) tell("The form is full to the bottom: move something up first.");
    return r;
}

static int design_save(const char *name)
{
    if (name) put_text(d_form->name, sizeof d_form->name, name);
    if (!ob_form_check(proj, d_form) || !ob_form_save(proj, d_form)) { tell(ob_error(proj)); return 0; }
    d_changed = 0;
    return 1;
}

/* Whether a form of that name is kept (on any table). */
static int form_taken(const char *name)
{
    int i, n = ob_form_count(proj);
    for (i = 0; i < n; i++) {
        const char *s = ob_form_name(proj, i);
        if (s && same_name(s, name)) return 1;
    }
    return 0;
}

enum { FD_FIELD = 1, FD_LABEL, FD_LINE, FD_BOX, FD_TEXT, FD_REMOVE, FD_SAVE, FD_SAVEAS, FD_CLOSE, FD_NBUTTONS = FD_CLOSE };

static void design_button(struct Window *dw, int id)
{
    static char names[PICK_MAX][OB_NAME_MAX];
    const char *labels[PICK_MAX];
    char buf[OB_LABEL_MAX];
    ob_fitem *it;
    int n, i, r;
    switch (id) {
    case FD_FIELD:
        n = ob_form_unplaced(proj, d_form, names, PICK_MAX);
        if (n < 0) { tell(ob_error(proj)); break; }
        if (n == 0) { tell("Every field of the table is on the form."); break; }
        if (n > PICK_MAX) n = PICK_MAX;
        for (i = 0; i < n; i++) labels[i] = names[i];
        i = pick_list("Add Field", labels, n, "Add");
        if (i < 0 || (r = design_row()) < 0) break;
        if ((i = ob_form_add_field(proj, d_form, names[i], 0, r)) < 0) { tell(ob_error(proj)); break; }
        d_sel = i;
        d_changed = 1;
        break;
    case FD_LABEL:
    case FD_BOX:
        buf[0] = 0;
        if (!ask_text(id == FD_LABEL ? "Add Label" : "Add Box", id == FD_LABEL ? "Text:" : "Title:", buf, sizeof buf)) break;
        if (id == FD_LABEL && !buf[0]) break;
        if ((r = design_row()) < 0) break;
        n = (int)strlen(buf);
        it = id == FD_LABEL ? ob_form_add(d_form, OB_FI_LABEL, 0, r, n, 1)
                           : ob_form_add(d_form, OB_FI_BOX, 0, r, n + 4 > 30 ? n + 4 : 30, 5);
        if (!it) { tell("The form is full."); break; }
        put_text(it->text, sizeof it->text, buf);
        d_sel = d_form->nitems - 1;
        d_changed = 1;
        break;
    case FD_LINE:
        if ((r = design_row()) < 0) break;
        if (!ob_form_add(d_form, OB_FI_LINE, 0, r, 20, 1)) { tell("The form is full."); break; }
        d_sel = d_form->nitems - 1;
        d_changed = 1;
        break;
    case FD_TEXT:
        if (d_sel < 0) { tell("Choose a label or a box first."); break; }
        it = &d_form->items[d_sel];
        if (it->kind != OB_FI_LABEL && it->kind != OB_FI_BOX) {
            tell(it->kind == OB_FI_FIELD ? "A field's words are the label beside it: choose that." : "A line has no words.");
            break;
        }
        put_text(buf, sizeof buf, it->text);
        if (!ask_text(it->kind == OB_FI_LABEL ? "Label" : "Box", it->kind == OB_FI_LABEL ? "Text:" : "Title:", buf, sizeof buf)) break;
        if (it->kind == OB_FI_LABEL && !buf[0]) break;
        put_text(it->text, sizeof it->text, buf);
        d_changed = 1;
        break;
    case FD_REMOVE:
        if (d_sel < 0) { tell("Choose an item first."); break; }
        ob_form_remove(d_form, d_sel);
        if (d_sel >= d_form->nitems) d_sel = d_form->nitems - 1;
        d_changed = 1;
        break;
    case FD_SAVE:
        design_save(NULL);
        break;
    case FD_SAVEAS:
        put_text(buf, OB_NAME_MAX, d_form->name);
        if (!ask_text("Save Form As", "Name:", buf, OB_NAME_MAX) || !buf[0]) break;
        if (!same_name(buf, d_form->name) && form_taken(buf)
            && ask("There is a form of that name already.", "Replace|Cancel") != 1) break;
        design_save(buf);
        SetWindowTitles(dw, (UBYTE *)d_form->name, (UBYTE *)~0);
        break;
    }
    ActivateWindow(dw);
}

/* The designer window on f; changed when f isn't kept as it is yet. */
static void design_window(ob_form *f, int changed)
{
    static const char *const btext[FD_NBUTTONS] = {
        "Field...", "Label...", "Line", "Box...", "Words...", "Remove", "Save", "Save As...", "Close"
    };
    struct Window *dw;
    struct Gadget *eg = NULL, *g;
    struct NewGadget ng;
    int iw, ih, bw, bh = fh + 6, bottom, i, done = 0, wt, cols, rows;
    int down = 0, dx0 = 0, dy0 = 0;
    d_form = f;
    d_sel = f->nitems ? 0 : -1;
    d_ox = d_oy = 0;
    d_changed = changed;
    d_rowh = fh + 8;                                /* as the shown form */
    ob_form_extent(f, &cols, &rows);
    bw = fw * 10 + 12;
    iw = (cols + 4) * fw + 16;
    if (iw < 5 * (bw + 4) + 12) iw = 5 * (bw + 4) + 12;
    if (iw > scr->Width - 40) iw = scr->Width - 40;
    bottom = (fh + 4) + 2 * (bh + 4) + 8;
    ih = 6 + (rows + 3) * d_rowh + bottom;
    wt = scr->BarHeight + 1;
    if (ih > scr->Height - wt - scr->WBorTop - fh - 1 - scr->WBorBottom)
        ih = scr->Height - wt - scr->WBorTop - fh - 1 - scr->WBorBottom;
    dw = OpenWindowTags(NULL, WA_Title, (ULONG)f->name, WA_ScreenTitle, (ULONG)"GFOpenBase: Design Form",
        WA_PubScreen, (ULONG)scr, WA_Left, 20, WA_Top, wt, WA_InnerWidth, iw, WA_InnerHeight, ih,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY
                  | IDCMP_MOUSEBUTTONS | BUTTONIDCMP,
        TAG_DONE);
    if (!dw) { tell("The designer window didn't open."); return; }
    iw = dw->Width - dw->BorderLeft - dw->BorderRight;
    ih = dw->Height - dw->BorderTop - dw->BorderBottom;
    d_cx = dw->BorderLeft + 6;
    d_cy = dw->BorderTop + 4;
    d_cw = iw - 12;
    d_cols = d_cw / fw;
    d_rows = (ih - bottom - 4) / d_rowh;
    if (d_rows < 1) d_rows = 1;
    d_ch = d_rows * d_rowh;
    d_sty = d_cy + d_ch + 4;
    if (!(g = CreateContext(&eg))) { CloseWindow(dw); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_Height = bh;
    bw = (d_cw - 4 * 4) / 5;
    for (i = 0; i < FD_NBUTTONS && g; i++) {
        ng.ng_LeftEdge = d_cx + (i % 5) * (bw + 4);
        ng.ng_TopEdge = d_sty + fh + 4 + (i / 5) * (bh + 4);
        ng.ng_Width = bw;
        ng.ng_GadgetText = (UBYTE *)btext[i];
        ng.ng_GadgetID = i + 1;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    }
    if (!g) { FreeGadgets(eg); CloseWindow(dw); tell("Out of memory"); return; }
    AddGList(dw, eg, ~0, -1, NULL);
    RefreshGList(eg, dw, NULL, -1);
    GT_RefreshWindow(dw, NULL);
    design_draw(dw);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(dw->UserPort);
        while (!done && (m = GT_GetIMsg(dw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code, qual = m->Qualifier;
            WORD mx = m->MouseX, my = m->MouseY;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int redraw = 1;
            GT_ReplyIMsg(m);
            switch (class) {
            case IDCMP_CLOSEWINDOW: done = 1; break;
            case IDCMP_REFRESHWINDOW: GT_BeginRefresh(dw); GT_EndRefresh(dw, TRUE); break;
            case IDCMP_GADGETUP:
                if (gg->GadgetID == FD_CLOSE) done = 1;
                else design_button(dw, gg->GadgetID);
                break;
            case IDCMP_MOUSEBUTTONS:
                if (mx < d_cx || my < d_cy || mx >= d_cx + d_cw || my >= d_cy + d_ch) { down = 0; redraw = 0; break; }
                if (code == SELECTDOWN) {
                    dx0 = d_ox + (mx - d_cx) / fw;
                    dy0 = d_oy + (my - d_cy) / d_rowh;
                    d_sel = ob_form_at(d_form, dx0, dy0);
                    down = d_sel >= 0;
                } else if (code == SELECTUP && down) {          /* a drag moves it */
                    int ddx = d_ox + (mx - d_cx) / fw - dx0, ddy = d_oy + (my - d_cy) / d_rowh - dy0;
                    if ((ddx || ddy) && ob_form_nudge(d_form, d_sel, ddx, ddy, 0, 0)) d_changed = 1;
                    down = 0;
                } else redraw = 0;
                break;
            case IDCMP_VANILLAKEY:
                if (code == 27) done = 1;
                else if (code == 9 && d_form->nitems)                  /* Tab: the next item */
                    d_sel = (d_sel + 1) % d_form->nitems;
                else if (code == 127 || code == 8) { if (d_sel >= 0) design_button(dw, FD_REMOVE); }
                else if (code == 13 && d_sel >= 0) design_button(dw, FD_TEXT);
                else redraw = 0;
                break;
            case IDCMP_RAWKEY: {
                int ddx = 0, ddy = 0, step = (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) ? 4 : 1;
                if (code == 0x4c) ddy = -step;
                else if (code == 0x4d) ddy = step;
                else if (code == 0x4e) ddx = step;
                else if (code == 0x4f) ddx = -step;
                if ((!ddx && !ddy) || d_sel < 0) { redraw = 0; break; }
                if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ob_form_nudge(d_form, d_sel, 0, 0, ddx, ddy);
                else ob_form_nudge(d_form, d_sel, ddx, ddy, 0, 0);
                d_changed = 1;
                break;
            }
            default: redraw = 0;
            }
            if (done && d_changed) {
                int a = ask("Save the changes to the form?", "Save|Don't Save|Cancel");
                if (a == 0 || (a == 1 && !design_save(NULL))) { done = 0; ActivateWindow(dw); }
            }
            if (!done && redraw) design_draw(dw);
        }
    }
    RemoveGList(dw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(dw);
    d_form = NULL;
}

/* Table > Design Form...: a new form on the table, or one of its forms. */
static void design_forms(void)
{
    static char names[PICK_FORMS][OB_NAME_MAX];
    const char *labels[PICK_FORMS + 1];
    char nm[OB_NAME_MAX];
    ob_form *f;
    int n, i, sel, changed = 0;
    if (q_on || !proj || !have_cur) { set_status("Choose a table first"); return; }
    n = ob_forms_of(proj, cur.name, names, PICK_FORMS);
    if (n < 0) { tell(ob_error(proj)); return; }
    if (n > PICK_FORMS) n = PICK_FORMS;
    labels[0] = "(New form)";
    for (i = 0; i < n; i++) labels[i + 1] = names[i];
    sel = pick_list("Design Form", labels, n + 1, "Design");
    if (sel < 0) { ActivateWindow(win); return; }
    if (!(f = malloc(sizeof *f))) { tell("Out of memory"); return; }
    if (sel == 0) {
        int a;
        put_text(nm, sizeof nm, cur.name);
        for (i = 2; form_taken(nm) && i < 100; i++) snprintf(nm, sizeof nm, "%.*s %d", OB_NAME_MAX - 5, cur.name, i);
        if (!ask_text("New Form", "Name:", nm, sizeof nm) || !nm[0]) { free(f); ActivateWindow(win); return; }
        if (form_taken(nm)) { tell("There is a form of that name already: choose it from the list."); free(f); return; }
        a = ask("Start the new form from:", "The Auto Form|Empty|Cancel");
        if (a == 0) { free(f); ActivateWindow(win); return; }
        if (a == 1) { if (!ob_form_auto(proj, cur.name, nm, f)) { tell(ob_error(proj)); free(f); return; } }
        else ob_form_init(f, nm, cur.name);
        changed = 1;
    } else if (!ob_form_get(proj, names[sel - 1], f)) {
        tell(ob_error(proj));
        free(f);
        return;
    }
    design_window(f, changed);
    free(f);
    ActivateWindow(win);
}

/* The form; 1 when it closed to open a subform row in its own form. */
static int form_window(void)
{
    struct Window *w_;
    struct Gadget *eg = NULL, *g, *fg[FG_MAX];
    int fidx[FG_MAX], fitem[FG_MAX], dcols = 0, drows = 0, top0;
    struct NewGadget ng;
    int i, page = 0, pages, per, lw = 0, vw, row_h = fh + 8, nshow, first, done = 0, bw, sub_h, top, x, iw, ih, full, wt, oh;
    int last = 0;                              /* the action before, for a double-click */
    ULONG l_secs = 0, l_mics = 0;
    if (q_on) { set_status("Forms show a table: choose one in the list"); return 0; }
    if (!proj || !have_cur) { set_status("Choose a table first"); return 0; }
    f_go = 0;
    for (i = 0; i < OB_MAX_FIELDS; i++) {
        if (!f_vals[i]) f_vals[i] = malloc(1024);
        if (!f_before[i]) f_before[i] = malloc(1024);
        if (!f_vals[i] || !f_before[i]) { tell("Out of memory"); return 0; }
    }
    f_nsubs = ob_children(proj, cur.name, f_subs, FORM_SUBS);
    if (f_nsubs < 0) f_nsubs = 0;
    if (f_nsubs > FORM_SUBS) f_nsubs = FORM_SUBS;
    for (i = 0; i < f_nsubs; i++) {
        snprintf(f_sub_text[i], sizeof f_sub_text[i], "%.63s (%.63s)", f_subs[i].table, f_subs[i].field);
        f_sub_labels[i] = (STRPTR)f_sub_text[i];
    }
    f_sub_labels[f_nsubs] = NULL;
    f_sub = 0;
    f_filtering = 0;
    f_pos = sel_row >= 0 ? sel_row : 0;
    form_read();
    for (i = 0; i < cur.nfields; i++) {
        int n = (int)strlen(cur.fields[i].label[0] ? cur.fields[i].label : cur.fields[i].name);
        if (n > lw) lw = n;
    }
    lw = (lw > 24 ? 24 : lw) * fw + 16;
    bw = fw * 6 + 12;                          /* a bar button */
    iw = lw + fw * 44 + 20;
    if (iw < 8 * (bw + 6) + fw * 16) iw = 8 * (bw + 6) + fw * 16;
    if (iw > scr->Width - 40) iw = scr->Width - 40;
    vw = iw - lw - 20;
    f_slines = 7;
    sub_h = f_nsubs ? (fh + 8) + f_slines * (fh + 2) + 8 + fh + 12 : 0;
    per = (scr->Height - 160 - sub_h - 3 * (fh + 12)) / row_h;
    if (per > ED_MAX) per = ED_MAX;
    if (per < 3) per = 3;
    pages = (cur.nfields + per - 1) / per;
    nshow = cur.nfields < per ? cur.nfields : per;
    if (f_design) {
        /* a designed form: one page, as big as its items; at least the bar's width */
        ob_form_extent(f_design, &dcols, &drows);
        pages = 1;
        per = OB_MAX_FIELDS;
        if (iw < 20 + dcols * fw) iw = 20 + dcols * fw;
        if (iw > scr->Width - 40) iw = scr->Width - 40;
    }
    ih = 8 + (f_design ? drows * row_h : nshow * row_h) + (pages > 1 ? fh + 12 : 0) + sub_h + 2 * (fh + 12) + 8;
    /* on a short screen the subform's list gives up lines first, down to 3 */
    oh = ih + scr->WBorTop + fh + 1 + scr->WBorBottom;     /* the window, borders and title */
    while (f_nsubs && f_slines > 3 && oh > scr->Height - scr->BarHeight - 1) {
        f_slines--;
        sub_h -= fh + 2;
        ih -= fh + 2;
        oh -= fh + 2;
    }
    wt = win->TopEdge + 20;                    /* below the main window's title, if it fits */
    if (wt + oh > scr->Height) wt = scr->Height - oh;
    if (wt < 0) wt = 0;
    w_ = OpenWindowTags(NULL,
        WA_Title, (ULONG)(f_design ? f_design->name : cur.name),
        WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 30, WA_Top, wt,
        WA_InnerWidth, iw, WA_InnerHeight, ih,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP
                  | BUTTONIDCMP | CYCLEIDCMP | LISTVIEWIDCMP,
        TAG_DONE);
    if (!w_) return 0;
    SetFont(w_->RPort, font);
    while (!done) {
        int action = 0;
        ULONG acode = 0;
        first = page * per;
        nshow = cur.nfields - first < per ? cur.nfields - first : per;
        if (nshow < 0) nshow = 0;
        if (nshow > FG_MAX) nshow = FG_MAX;
        for (i = 0; i < nshow; i++) { fidx[i] = first + i; fitem[i] = -1; }
        if (f_design) {
            /* its field items, in the order they were placed */
            int k, j;
            nshow = 0;
            for (k = 0; k < f_design->nitems && nshow < FG_MAX; k++)
                if (f_design->items[k].kind == OB_FI_FIELD
                    && (j = ob_table_find_field(&cur, f_design->items[k].field)) >= 0) {
                    fidx[nshow] = j;
                    fitem[nshow] = k;
                    nshow++;
                }
        }
        if (f_pos < 0) put_text(f_count, sizeof f_count, "New record");
        else snprintf(f_count, sizeof f_count, "%lld of %lld", (long long)f_pos + 1, (long long)f_total);
        form_subrows();
        memset(fg, 0, sizeof fg);
        if (!(g = CreateContext(&eg))) break;
        memset(&ng, 0, sizeof ng);
        ng.ng_VisualInfo = vi;
        ng.ng_TextAttr = &font_attr;
        top0 = top = w_->BorderTop + 8;
        for (i = 0; i < nshow; i++) {
            const ob_field *f = &cur.fields[fidx[i]];
            ng.ng_LeftEdge = w_->BorderLeft + 10 + lw;
            ng.ng_TopEdge = top + i * row_h;
            ng.ng_Width = vw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)(f->label[0] ? f->label : f->name);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_GadgetID = F_FIELD0 + i;
            if (fitem[i] >= 0) {
                /* where the designer put it, in character cells */
                const ob_fitem *it = &f_design->items[fitem[i]];
                ng.ng_LeftEdge = w_->BorderLeft + 10 + it->x * fw;
                ng.ng_TopEdge = top + it->y * row_h;
                ng.ng_Width = it->w * fw;
                ng.ng_Height = it->h > 1 ? it->h * row_h - 2 : fh + 6;
                ng.ng_GadgetText = NULL;
            }
            if (f_filtering)
                g = fg[i] = CreateGadget(STRING_KIND, g, &ng,
                                         GTST_String, (ULONG)ob_view_form_get(&f_fview, f->name, f_frow),
                                         GTST_MaxChars, OB_VALUE_MAX - 1, GA_TabCycle, TRUE, TAG_DONE);
            else if (f->type == OB_COUNTER || f->type == OB_OBJECT)
                g = fg[i] = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text,
                                         (ULONG)(f->type == OB_COUNTER && f_key < 0 ? "(new)" : f_vals[fidx[i]]),
                                         GTTX_Border, TRUE, TAG_DONE);
            else
                g = fg[i] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)f_vals[fidx[i]],
                                         GTST_MaxChars, 1023, GA_TabCycle, TRUE, TAG_DONE);
        }
        top += f_design ? drows * row_h : nshow * row_h;
        ng.ng_Flags = PLACETEXT_IN;
        ng.ng_Height = fh + 6;
        if (pages > 1) {
            ng.ng_TopEdge = top;
            ng.ng_LeftEdge = w_->BorderLeft + 10 + lw;
            ng.ng_Width = fw * 10 + 16;
            ng.ng_GadgetText = (UBYTE *)"< Fields";
            ng.ng_GadgetID = F_BACK;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, page == 0, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_GadgetText = (UBYTE *)"Fields >";
            ng.ng_GadgetID = F_ON;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, page == pages - 1, TAG_DONE);
            top += fh + 12;
        }
        if (f_filtering) {
            /* the filter's bar: which row, and how to write it */
            ng.ng_TopEdge = top + 4;
            ng.ng_LeftEdge = w_->BorderLeft + 10 + lw;
            ng.ng_Width = fw * 12 + 28;
            ng.ng_GadgetText = (UBYTE *)"Row";
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_GadgetID = F_FROW;
            g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)f_frow_labels, GTCY_Active, f_frow, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_Width = w_->Width - w_->BorderRight - 10 - ng.ng_LeftEdge;
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = F_FHINT;
            snprintf(f_fhint, sizeof f_fhint, "Leeds, > 10, Smith*, between 1 and 5, empty, not York");
            g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)f_fhint, TAG_DONE);
            ng.ng_Flags = PLACETEXT_IN;
            ng.ng_TopEdge = top + 4 + fh + 12;
            ng.ng_Width = fw * 12 + 12;
            ng.ng_LeftEdge = w_->BorderLeft + 10; ng.ng_GadgetText = (UBYTE *)"Apply Filter"; ng.ng_GadgetID = F_FILTER;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Clear All"; ng.ng_GadgetID = F_FCLEAR;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge = w_->Width - w_->BorderRight - 10 - ng.ng_Width; ng.ng_GadgetText = (UBYTE *)"Cancel";
            ng.ng_GadgetID = F_FCANCEL;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        }
        if (!f_filtering && f_nsubs) {
            ng.ng_TopEdge = top + 4;
            ng.ng_LeftEdge = w_->BorderLeft + 10 + lw;
            ng.ng_Width = vw / 2;
            ng.ng_GadgetText = (UBYTE *)"Rows in";
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_GadgetID = F_SUBPICK;
            g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)f_sub_labels, GTCY_Active, f_sub, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8;
            ng.ng_Width = vw - vw / 2 - 8;
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = F_SUBCOUNT;
            g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)f_scount, TAG_DONE);
            ng.ng_TopEdge = top + 4 + fh + 8;
            ng.ng_LeftEdge = w_->BorderLeft + 10;
            ng.ng_Width = iw - 20;
            ng.ng_Height = f_slines * (fh + 2);
            ng.ng_GadgetID = F_SUBLIST;
            ng.ng_Flags = 0;
            g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&f_sl, GTLV_ShowSelected, 0,
                             GTLV_Selected, f_ssel >= 0 ? (ULONG)f_ssel : ~0UL, TAG_DONE);
            ng.ng_Height = fh + 6;
            ng.ng_Flags = PLACETEXT_IN;
            /* change the subform's rows: Add for this record, Edit and Delete the one chosen */
            ng.ng_TopEdge = top + 4 + fh + 8 + f_slines * (fh + 2) + 4;
            ng.ng_Width = fw * 12 + 12;
            ng.ng_LeftEdge = w_->BorderLeft + 10; ng.ng_GadgetText = (UBYTE *)"Add Row..."; ng.ng_GadgetID = F_SUBADD;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_key <= 0, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Edit Row..."; ng.ng_GadgetID = F_SUBEDIT;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_ssel < 0, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Delete Row..."; ng.ng_GadgetID = F_SUBDEL;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_ssel < 0, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Open Form"; ng.ng_GadgetID = F_SUBOPEN;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_ssel < 0, TAG_DONE);
            top += sub_h;
        }
        /* the record bar */
        if (!f_filtering) {
            x = w_->BorderLeft + 10;
            ng.ng_TopEdge = top + 4;
            ng.ng_Width = bw;
            {
                static const char *const bar[] = { "|<", "<" };
                ng.ng_LeftEdge = x; ng.ng_GadgetText = (UBYTE *)bar[0]; ng.ng_GadgetID = F_FIRST;
                g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_pos == 0 || !f_total, TAG_DONE);
                x += bw + 4;
                ng.ng_LeftEdge = x; ng.ng_GadgetText = (UBYTE *)bar[1]; ng.ng_GadgetID = F_PREV;
                g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_pos == 0 || !f_total, TAG_DONE);
                x += bw + 4;
            }
            ng.ng_LeftEdge = x; ng.ng_Width = fw * 16; ng.ng_GadgetText = NULL; ng.ng_GadgetID = F_POS;
            g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)f_count, GTTX_Justification, GTJ_CENTER, GTTX_Border, TRUE, TAG_DONE);
            x += fw * 16 + 4;
            ng.ng_Width = bw;
            ng.ng_LeftEdge = x; ng.ng_GadgetText = (UBYTE *)">"; ng.ng_GadgetID = F_NEXT;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_pos < 0 || f_pos >= f_total - 1, TAG_DONE);
            x += bw + 4;
            ng.ng_LeftEdge = x; ng.ng_GadgetText = (UBYTE *)">|"; ng.ng_GadgetID = F_LAST;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_pos < 0 || f_pos >= f_total - 1, TAG_DONE);
            x += bw + 12;
            ng.ng_Width = fw * 7 + 12;
            ng.ng_LeftEdge = x; ng.ng_GadgetText = (UBYTE *)"New"; ng.ng_GadgetID = F_NEW;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_pos < 0, TAG_DONE);
            x += ng.ng_Width + 4;
            ng.ng_LeftEdge = x; ng.ng_GadgetText = (UBYTE *)"Delete..."; ng.ng_Width = fw * 9 + 12; ng.ng_GadgetID = F_DELETE;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, f_pos < 0, TAG_DONE);
            /* the second row: Find, Save, Undo Record, Close */
            ng.ng_TopEdge = top + 4 + fh + 12;
            ng.ng_Width = fw * 11 + 12;
            ng.ng_LeftEdge = w_->BorderLeft + 10; ng.ng_GadgetText = (UBYTE *)"Find..."; ng.ng_GadgetID = F_FIND;
            g = CreateGadget(BUTTON_KIND, g, &ng, GA_Disabled, !f_total, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Save"; ng.ng_GadgetID = F_SAVE;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Undo Record"; ng.ng_GadgetID = F_UNDO;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_LeftEdge += ng.ng_Width + 8; ng.ng_GadgetText = (UBYTE *)"Filter by Form"; ng.ng_GadgetID = F_FILTER;
            ng.ng_Width = fw * 14 + 12;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
            ng.ng_Width = fw * 11 + 12;
            ng.ng_LeftEdge = w_->Width - w_->BorderRight - 10 - ng.ng_Width; ng.ng_GadgetText = (UBYTE *)"Close";
            ng.ng_GadgetID = F_CLOSE;
            g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
        }
        if (!g) break;
        SetAPen(w_->RPort, pen(BACKGROUNDPEN));
        RectFill(w_->RPort, w_->BorderLeft, w_->BorderTop, w_->Width - w_->BorderRight - 1, w_->Height - w_->BorderBottom - 1);
        AddGList(w_, eg, ~0, -1, NULL);
        RefreshGList(eg, w_, NULL, -1);
        GT_RefreshWindow(w_, NULL);
        if (f_design) form_draw(w_, f_design, w_->BorderLeft + 10, top0, row_h);
        for (i = 0; i < nshow && last != F_SUBLIST; i++)
            if (fg[i] && (f_filtering || (cur.fields[fidx[i]].type != OB_COUNTER && cur.fields[fidx[i]].type != OB_OBJECT))) {
                ActivateGadget(fg[i], w_, NULL);
                break;
            }
        while (!action) {
            struct IntuiMessage *m;
            WaitPort(w_->UserPort);
            while (!action && (m = GT_GetIMsg(w_->UserPort)) != NULL) {
                ULONG class = m->Class;
                UWORD code = m->Code;
                struct Gadget *gad = (struct Gadget *)m->IAddress;
                ULONG secs = m->Seconds, mics = m->Micros;
                GT_ReplyIMsg(m);
                if (class == IDCMP_GADGETUP && gad->GadgetID == F_SUBLIST && (int)code == f_ssel && f_ssel >= 0
                    && last == F_SUBLIST && DoubleClick(l_secs, l_mics, secs, mics)) {
                    action = F_SUBOPEN;     /* a double-click on the chosen row */
                    continue;
                }
                if (class == IDCMP_GADGETUP) { l_secs = secs; l_mics = mics; }
                if (class == IDCMP_CLOSEWINDOW) action = F_CLOSE;
                else if (class == IDCMP_VANILLAKEY && code == 13 && !f_filtering && f_ssel >= 0) action = F_SUBOPEN;
                else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(w_); GT_EndRefresh(w_, TRUE); }
                else if (class == IDCMP_VANILLAKEY && code == 27) action = f_filtering ? F_FCANCEL : F_UNDO;
                else if (class == IDCMP_GADGETUP && gad->GadgetID >= F_FIRST && gad->GadgetID < F_FIELD0
                         && gad->GadgetID != F_POS && gad->GadgetID != F_SUBCOUNT
                         && gad->GadgetID != F_FHINT) {
                    action = gad->GadgetID;
                    acode = code;
                }
            }
        }
        /* keep what was typed */
        full = 0;
        for (i = 0; i < nshow; i++) {
            const ob_field *f = &cur.fields[fidx[i]];
            if (f_filtering && fg[i]) {
                STRPTR s = NULL;
                GT_GetGadgetAttrs(fg[i], w_, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                if (!ob_view_form_set(&f_fview, f->name, f_frow, s ? (const char *)s : "")) full = 1;
            } else if (fg[i] && f->type != OB_COUNTER && f->type != OB_OBJECT) {
                STRPTR s = NULL;
                GT_GetGadgetAttrs(fg[i], w_, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                put_text(f_vals[fidx[i]], 1024, s ? (const char *)s : "");
            }
        }
        RemoveGList(w_, eg, -1);
        FreeGadgets(eg);
        eg = NULL;
        if (full) tell("Filter by Form looks at 16 fields at most: what was typed past that is left out");
        if (f_filtering && action == F_CLOSE) action = F_FCANCEL;
        last = action;
        switch (action) {
        case F_FROW: f_frow = (int)acode < OB_FORM_OR ? (int)acode : 0; break;
        case F_FCLEAR: ob_view_form_clear(&f_fview); break;
        case F_FCANCEL: f_filtering = 0; set_status("Filter by Form cancelled"); break;
        case F_FILTER:
            if (!f_filtering) {
                if (!form_save(w_)) break;
                f_fview = view;
                f_frow = 0;
                f_filtering = 1;
                page = 0;
                set_status("Type what to look for, then Apply Filter");
            } else if (!ob_view_check(proj, cur.name, &f_fview)) {
                tell(ob_error(proj));
            } else {
                char msg[96];
                view = f_fview;
                f_filtering = 0;
                f_pos = 0;
                form_read();
                if (!f_total) f_pos = -1;
                snprintf(msg, sizeof msg, ob_view_filtered(&view) ? "Filter applied: %lld records" : "Filter removed: %lld records",
                         (long long)f_total);
                set_status(msg);
            }
            break;
        case F_BACK: if (page > 0) page--; break;
        case F_ON: if (page < pages - 1) page++; break;
        case F_SUBPICK: f_sub = (int)acode < f_nsubs ? (int)acode : 0; break;
        case F_SUBLIST: f_ssel = (int)acode; break;
        case F_SUBOPEN: {
            ob_int k;
            if (!form_save(w_)) break;
            form_read();
            k = ob_child_key(proj, &f_subs[f_sub], f_key, f_ssel);
            if (k == -2) { tell(ob_error(proj)); break; }
            if (k < 0) { tell("Choose a row in the list first."); break; }
            f_go_rel = f_subs[f_sub];
            f_go_parent = f_key;
            f_go_key = k;
            f_go = 1;
            done = 1;
            break;
        }
        case F_SUBADD: case F_SUBEDIT: case F_SUBDEL: {
            ob_int k = 0;
            if (!form_save(w_)) break;
            form_read();
            if (f_key <= 0) { tell("Save the record first: its rows need its key"); break; }
            if (action != F_SUBADD) {
                k = ob_child_key(proj, &f_subs[f_sub], f_key, f_ssel);
                if (k == -2) { tell(ob_error(proj)); break; }
                if (k < 0) { tell("Choose a row in the list first."); break; }
            }
            if (action == F_SUBDEL) {
                char q[160];
                snprintf(q, sizeof q, "Delete row %d of %.63s?", f_ssel + 1, f_child.name);
                if (ask(q, "Delete|Cancel") == 1) {
                    if (!ob_row_delete(proj, f_child.name, k)) tell(ob_error(proj));
                    else { set_status("Row deleted"); f_ssel = -1; }
                }
            } else {
                edit_row_of(&f_child, k, action == F_SUBADD, &f_subs[f_sub], f_key);
                ActivateWindow(w_);
            }
            break;
        }
        case F_UNDO:
            for (i = 0; i < cur.nfields; i++) put_text(f_vals[i], 1024, f_before[i]);
            if (f_pos < 0) { f_pos = f_total ? 0 : -1; form_read(); }
            set_status("Record put back");
            break;
        case F_SAVE:
            if (form_save(w_)) form_read();
            break;
        case F_FIRST: case F_PREV: case F_NEXT: case F_LAST: case F_NEW: {
            if (!form_save(w_)) break;
            f_total = ob_view_total(proj, cur.name, &view);
            if (action == F_FIRST) f_pos = 0;
            else if (action == F_PREV && f_pos > 0) f_pos--;
            else if (action == F_NEXT && f_pos < f_total - 1) f_pos++;
            else if (action == F_LAST) f_pos = f_total - 1;
            else if (action == F_NEW) f_pos = -1;
            form_read();
            if (action == F_NEW) page = 0;
            break;
        }
        case F_DELETE: {
            char q[160];
            snprintf(q, sizeof q, "Delete record %lld of %s?", (long long)f_pos + 1, cur.name);
            if (ask(q, "Delete|Cancel") == 1) {
                if (!ob_row_delete(proj, cur.name, f_key)) tell(ob_error(proj));
                else { set_status("Record deleted"); form_read(); }
            }
            break;
        }
        case F_FIND: {
            int dir;
            if (!form_save(w_)) break;
            dir = form_ask_find(w_);
            if (dir < 0 && f_pos <= 0) set_status("Not found");
            else if (dir) {
                ob_int at = ob_view_find(proj, cur.name, &view, form_find_name(), f_find_text,
                                         dir > 0 ? f_pos + 1 : f_pos - 1, dir < 0);
                if (at == -2) tell(ob_error(proj));
                else if (at == -1) set_status("Not found");
                else { f_pos = at; form_read(); set_status("Found"); }
            }
            break;
        }
        case F_CLOSE:
            if (form_save(w_)) done = 1;
            else if (ask("The record can't be saved. Close and lose the changes?", "Lose Them|Stay") == 1) done = 1;
            break;
        }
    }
    if (eg) FreeGadgets(eg);
    CloseWindow(w_);
    /* the datasheet shows where the form was */
    load_page();
    if (f_pos >= 0 && f_pos < total_rows) {
        sel_row = (int)f_pos;
        if (sel_row < top_row || sel_row >= top_row + ds_rows) top_row = sel_row - ds_rows / 2;
        load_page();
    }
    ActivateWindow(win);
    refresh();
    return f_go;
}

static void undo_last(void)
{
    char msg[OB_NAME_MAX + 80], name[OB_NAME_MAX + 48];
    if (!proj) return;
    if (!ob_undo_count(proj)) { set_status("Nothing to undo"); return; }
    put_text(name, sizeof name, ob_undo_name(proj));
    if (!ob_undo(proj)) { tell(ob_error(proj)); load_page(); return; }
    snprintf(msg, sizeof msg, "Undone: %s", name);
    load_page();
    set_status(msg);
}

/* ---- Query > Save as View... ---- */

static void save_view(void)
{
    char name[OB_NAME_MAX], msg[OB_NAME_MAX + 80];
    int i, n;
    if (!proj) return;
    if (!qy.ntables && !qy.sql_only) { set_status("No query yet: Query > New Query..."); return; }
    snprintf(name, sizeof name, "%s", qy.name[0] ? qy.name : "");
    if (!ask_text("Save as View", "View name (other SQLite programs can read it)", name, sizeof name)) return;
    n = ob_sqlview_count(proj);
    for (i = 0; i < n; i++) {
        const char *v = ob_sqlview_name(proj, i);
        if (v && same_name(v, name)) {
            snprintf(msg, sizeof msg, "There is a view named %.40s.\nReplace it?", v);
            if (!ask(msg, "Replace|Cancel")) return;
            break;
        }
    }
    if (!ob_query_save_view(proj, &qy, name)) { tell(ob_error(proj)); return; }
    snprintf(msg, sizeof msg, "Saved the view %.40s", name);
    set_status(msg);
}

/* The Report window over this one, its new report from the table or query shown. */
static void report_window(void)
{
    ob_rwin_env e;
    if (!proj) { set_status("Open a project first"); return; }
    memset(&e, 0, sizeof e);
    e.scr = scr;
    e.vi = vi;
    e.font = font;
    e.ta = &font_attr;
    e.fw = fw;
    e.fh = fh;
    e.parent = win;
    e.proj = proj;
    e.proj_path = proj_path;
    e.table = have_cur && !q_on ? cur.name : "";
    e.query = q_on ? qy.name : "";
    e.ask_text = ask_text;
    ob_report_window(&e);
}

static void menu_action(ULONG id)
{
    char path[512];
    set_status("");
    switch (id) {
    case M_NEW:
        if (ask_file("New project", TRUE, "#?.oadb", path, sizeof path)) open_project(path, 1);
        break;
    case M_OPEN:
        if (ask_file("Open a project", FALSE, "#?.oadb", path, sizeof path)) open_project(path, 0);
        break;
    case M_CLOSE:
        close_project();
        update_title();
        break;
    case M_COPY:
        if (proj && ask_file("Save a copy as", TRUE, "#?.oadb", path, sizeof path)) {
            if (!ob_copy(proj, path)) tell(ob_error(proj));
            else set_status("Copy saved");
        }
        break;
    case M_COMPACT:
        if (proj) { if (!ob_copy(proj, NULL)) tell(ob_error(proj)); else set_status("Project compacted"); }
        break;
    case M_CHECK:
        if (proj) set_status(ob_check(proj, 1) ? "The project file is whole" : ob_error(proj));
        break;
    case M_ABOUT:
        tell(VERSION_TEXT "\nThe database of the GFOffice Suite.\n\nSQLite projects (.oadb).\nMIT, Copyright (c) 2026 AmiCradle Team");
        break;
    case M_QUIT: quit_now = 1; break;
    case M_T_NEW: design_table(1); break;
    case M_T_DESIGN: design_table(0); break;
    case M_T_DELETE: delete_table(); break;
    case M_T_RELATIONS: show_relationships(); break;
    case M_T_FORM: open_form(); break;
    case M_T_FORMS: open_designed(); break;
    case M_T_FDESIGN: design_forms(); break;
    case M_R_ADD: edit_row(1); break;
    case M_R_EDIT: edit_row(0); break;
    case M_R_DELETE: delete_row(); break;
    case M_R_FIRST: top_row = 0; sel_row = total_rows ? 0 : -1; load_page(); break;
    case M_R_LAST: top_row = total_rows - ds_rows; sel_row = (int)total_rows - 1; load_page(); break;
    case M_T_VIEW: edit_view(); break;
    case M_T_ALL: if (have_cur) { ob_view_init(&view); view_changed(); } break;
    case M_T_IMPORT: import_csv(); break;
    case M_T_EXPORT: export_csv(); break;
    case M_T_FROMCSV: new_table_from_csv(); break;
    case M_T_FROMDBF: new_table_from_dbf(); break;
    case M_T_IMPORTDBF: import_dbf(); break;
    case M_T_EXPORTDBF: export_dbf(); break;
    case M_UNDO: undo_last(); break;
    case M_REPORTS: report_window(); break;
    case M_Q_NEW: design_query(1); break;
    case M_Q_OPEN: open_query(); break;
    case M_Q_DESIGN: design_query(!qy.ntables && !qy.sql_only); break;
    case M_Q_VIEW: save_view(); break;
    case M_Q_AGAIN:
        if (qy.ntables || qy.sql_only) run_query(1);
        else set_status("No query yet: Query > New Query...");
        break;
    }
    refresh();
}

static void move_selection(int delta)
{
    if (!have_cur || !total_rows) return;
    sel_row = sel_row < 0 ? 0 : sel_row + delta;
    if (sel_row < 0) sel_row = 0;
    if (sel_row >= total_rows) sel_row = (int)total_rows - 1;
    if (sel_row < top_row) top_row = sel_row;
    if (sel_row >= top_row + ds_rows) top_row = sel_row - ds_rows + 1;
    load_page();
    refresh();
}

static void events(void)
{
    struct IntuiMessage *m;
    static ULONG last_s, last_m;
    static int last_row = -1;
    while (win && (m = GT_GetIMsg(win->UserPort)) != NULL) {
        ULONG class = m->Class, secs = m->Seconds, micros = m->Micros;
        UWORD code = m->Code;
        WORD mx = m->MouseX, my = m->MouseY;
        struct Gadget *g = (struct Gadget *)m->IAddress;
        GT_ReplyIMsg(m);
        switch (class) {
        case IDCMP_CLOSEWINDOW: quit_now = 1; break;
        case IDCMP_NEWSIZE: relayout(); break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            GT_EndRefresh(win, TRUE);
            draw_datasheet();
            break;
        case IDCMP_MENUPICK:
            while (code != MENUNULL && win) {
                struct MenuItem *item = ItemAddress(menu, code);
                if (!item) break;
                menu_action((ULONG)GTMENUITEM_USERDATA(item));
                code = item->NextSelect;
            }
            break;
        case IDCMP_GADGETUP:
        case IDCMP_GADGETDOWN:
        case IDCMP_MOUSEMOVE:
            /* the scroller, dragged or clicked: Code is its new top */
            if (g && g == g_scroll) {
                top_row = code;
                load_page();
                draw_datasheet();
                break;
            }
            if (class != IDCMP_GADGETUP || !g) break;
            set_status("");
            switch (g->GadgetID) {
            case GID_TABLES:
                if (q_on && code < (UWORD)table_count) {
                    /* back from a query's rows to a table's */
                    q_on = 0;
                    table_sel = -1;
                }
                if ((int)code != table_sel && code < (UWORD)table_count) {
                    table_sel = code;
                    have_cur = ob_table_get(proj, ob_table_name(proj, table_sel), &cur);
                    top_row = 0;
                    sel_row = -1;
                    left_col = 0;
                    load_page();
                }
                refresh();
                break;
            case GID_T_NEW: design_table(1); break;
            case GID_T_DESIGN: design_table(0); break;
            case GID_T_DELETE: delete_table(); break;
            case GID_R_ADD: edit_row(1); break;
            case GID_R_EDIT: edit_row(0); break;
            case GID_R_DELETE: delete_row(); break;
            }
            break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN && have_cur && mx >= ds_x && mx < ds_x + ds_w && my >= ds_y && my < ds_y + ds_h) {
                int r = (my - ds_y - 2) / (fh + 3) - 1;
                if (my >= ds_y + 2 && r < 0) {
                    set_status("");
                    sort_by_column(mx);
                    refresh();
                } else if (r >= 0 && r < page_n) {
                    int row = (int)top_row + r;
                    int dbl = row == last_row && DoubleClick(last_s, last_m, secs, micros);
                    sel_row = row;
                    last_row = row;
                    last_s = secs;
                    last_m = micros;
                    set_status("");
                    draw_datasheet();
                    if (dbl) { last_row = -1; edit_row(0); }
                }
            }
            break;
        case IDCMP_VANILLAKEY:
            if (code == 13) edit_row(0);
            else if (code == 127) delete_row();
            break;
        case IDCMP_RAWKEY:
            if (code == 0x4c) move_selection(-1);           /* cursor up */
            else if (code == 0x4d) move_selection(1);       /* cursor down */
            else if (code == 0x4f && left_col > 0) { left_col--; draw_datasheet(); }        /* left */
            else if (code == 0x4e && left_col < ds_ncols() - 1) { left_col++; draw_datasheet(); }
            break;
        }
    }
}

/* ---- The window ---- */

/* ---- ARexx: the GFOPENBASE port ---- */

#define RX_OK 0
#define RX_NOW 5
#define RX_ERROR 10
#define RX_UNKNOWN 20
#define REXX_PORT_NAME "GFOPENBASE"
static struct MsgPort *rexx_port;
static char rexx_name[16];

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static const char *skip_space(const char *s)
{
    while (*s && is_space(*s)) s++;
    return s;
}
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
/* s starts with word (any case) and then a space or its end; *rest is what follows */
static int starts_word_ci(const char *s, const char *word, const char **rest)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; ++i) if (!s[i] || lower((unsigned char)s[i]) != lower((unsigned char)word[i])) return 0;
    if (s[n] && !is_space(s[n]) && s[n] != '=') return 0;
    if (rest) *rest = skip_space(s + n + (s[n] == '=' ? 1 : 0));
    return 1;
}
/* the next argument ("in quotes" or one word) into out; where the rest starts */
static const char *rexx_arg(const char *p, char *out, size_t size)
{
    size_t k = 0;
    p = skip_space(p);
    if (*p == '"') {
        for (++p; *p && *p != '"'; ++p) if (k + 1 < size) out[k++] = *p;
        if (*p == '"') ++p;
    } else {
        while (*p && !is_space(*p)) { if (k + 1 < size) out[k++] = *p; ++p; }
    }
    out[k] = 0;
    return skip_space(p);
}

/* the table named (any case): its index, or -1 */
static int table_named(const char *name)
{
    int i;
    for (i = 0; proj && i < table_count; i++) {
        const char *t = ob_table_name(proj, i);
        size_t j;
        for (j = 0; t[j] && name[j] && lower((unsigned char)t[j]) == lower((unsigned char)name[j]); j++) ;
        if (!t[j] && !name[j]) return i;
    }
    return -1;
}

struct key_find { ob_int key, at, seen; };
static int key_cb(void *user, ob_int key, int n, const char *const *values)
{
    struct key_find *f = (struct key_find *)user;
    (void)n; (void)values;
    if (key == f->key) { f->at = f->seen; return 1; }
    f->seen++;
    return 0;
}

/* The row chosen: its key ("118"), or its place from 1 ("#7"), every row in key order shown. 1 when it is there. */
static int choose_row(const char *row)
{
    struct key_find f;
    ob_int first;
    char *end;
    if (!proj || !have_cur || !*row) return 0;
    ob_view_init(&view);
    put_text(view_table, sizeof view_table, cur.name);
    total_rows = ob_view_total(proj, cur.name, &view);
    f.at = -1;
    if (row[0] == '#') {
        long n = strtol(row + 1, &end, 10);
        if (*end || n < 1 || n > total_rows) return 0;
        f.at = n - 1;
    } else {
        f.key = (ob_int)strtol(row, &end, 10);
        if (*end) return 0;
        for (first = 0, f.seen = 0; f.at < 0 && first < total_rows; first += 256) {
            f.seen = first;
            if (ob_view_rows(proj, cur.name, NULL, first, 256, key_cb, &f) < 0) return 0;
        }
        if (f.at < 0) return 0;
    }
    sel_row = (int)f.at;
    top_row = f.at - ds_rows / 2;
    if (top_row < 0) top_row = 0;
    load_page();
    return 1;
}

/* OPEN FILE x [TABLE t] [ROW n] (FILE= may be left out), and the same on the command line */
static int rexx_open_cmd(const char *arg, char *out, size_t size)
{
    char file[512] = "", table[OB_NAME_MAX] = "", row[24] = "";
    const char *rest;
    while (*(arg = skip_space(arg))) {
        if (starts_word_ci(arg, "FILE", &rest)) arg = rexx_arg(rest, file, sizeof file);
        else if (starts_word_ci(arg, "TABLE", &rest)) arg = rexx_arg(rest, table, sizeof table);
        else if (starts_word_ci(arg, "ROW", &rest)) arg = rexx_arg(rest, row, sizeof row);
        else arg = rexx_arg(arg, file, sizeof file);
    }
    if (!file[0] && !proj) { put_text(out, size, "OPEN needs a FILE"); return RX_ERROR; }
    if (file[0] && !open_project(file, 0)) {
        put_text(out, size, rexx_noted ? rexx_noting : "The project could not be opened");
        return RX_ERROR;
    }
    if (table[0]) {
        int t = table_named(table);
        if (t < 0) { snprintf(out, size, "No table %.100s in the project", table); return RX_ERROR; }
        load_tables(ob_table_name(proj, t));
    }
    if (row[0] && !choose_row(row)) { snprintf(out, size, "No row %.20s in the table", row); return RX_NOW; }
    if (win) { WindowToFront(win); ActivateWindow(win); }
    put_text(out, size, "OK");
    return RX_OK;
}

/* FIRSTROW LASTROW NEXTROW PREVROW: the datasheet's row chosen moves, in
 * its sort and filter (a query's rows too); RESULT the row's key. COUNTROWS:
 * the rows it shows. */
static int rexx_walk(const char *command, char *out, size_t size)
{
    ob_int to;
    if (!proj || (!have_cur && !q_on)) { put_text(out, size, "No table is shown: TABLE chooses one"); return RX_NOW; }
    load_page();
    if (starts_word_ci(command, "COUNTROWS", NULL)) { snprintf(out, size, "%lld", (long long)total_rows); return RX_OK; }
    if (total_rows <= 0) { put_text(out, size, "There are no rows"); return RX_NOW; }
    if (starts_word_ci(command, "FIRSTROW", NULL)) to = 0;
    else if (starts_word_ci(command, "LASTROW", NULL)) to = total_rows - 1;
    else if (starts_word_ci(command, "NEXTROW", NULL)) to = sel_row < 0 ? 0 : sel_row + 1;
    else to = sel_row < 0 ? total_rows - 1 : sel_row - 1;
    if (to < 0 || to >= total_rows) { put_text(out, size, "No more rows"); return RX_NOW; }
    sel_row = (int)to;
    if (sel_row < top_row) top_row = sel_row;
    if (sel_row >= top_row + ds_rows) top_row = sel_row - ds_rows + 1;
    load_page();
    if (sel_row - top_row < 0 || sel_row - top_row >= page_n) { put_text(out, size, "The row couldn't be read"); return RX_ERROR; }
    snprintf(out, size, "%lld", (long long)page_keys[sel_row - top_row]);
    return RX_OK;
}

/* ADDROW CHANGEROW DELETEROW GETROW GETFIELDS (ob_rexx_rows) on the table
 * shown, KEY left out being the row chosen; a row added or changed is
 * chosen. -1 when command isn't one of these. */
static int rexx_rows(const char *command, char *out, size_t size)
{
    ob_int current = 0, row = 0;
    int rc;
    if (have_cur && !q_on && sel_row >= 0 && sel_row >= top_row && sel_row - top_row < page_n)
        current = page_keys[sel_row - top_row];
    rc = ob_rexx_rows(proj, have_cur && !q_on ? cur.name : NULL, current, command, out, size, &row);
    if (rc < 0) return -1;
    if (rc == 5 && q_on && !strncmp(out, "No table", 8)) put_text(out, size, "A query's rows are shown: TABLE chooses a table");
    if (rc == 0 && (starts_word_ci(command, "ADDROW", NULL) || starts_word_ci(command, "CHANGEROW", NULL)
                    || starts_word_ci(command, "DELETEROW", NULL))) {
        ob_int at = starts_word_ci(command, "DELETEROW", NULL) ? -1 : ob_view_position(proj, cur.name, &view, row);
        load_page();
        if (at >= 0) {
            sel_row = (int)at;
            if (sel_row < top_row || sel_row >= top_row + ds_rows) top_row = sel_row - ds_rows / 2;
            load_page();
        }
    }
    return rc;
}

static int rexx_do_command(const char *command, char **result_out)
{
    const char *arg = "";
    static char result_text[4096];       /* a GETROW of a wide table */
    int rc = RX_OK, r;
    result_text[0] = 0;
    command = skip_space(command);
    if (starts_word_ci(command, "VERSION", &arg)) put_text(result_text, sizeof result_text, VERSION_TEXT);
    else if (starts_word_ci(command, "LASTERROR", &arg)) put_text(result_text, sizeof result_text, rexx_lasterror);
    else if (starts_word_ci(command, "HELP", &arg))
        put_text(result_text, sizeof result_text, "VERSION HELP ACTIVATE DEACTIVATE FAULT LASTERROR ISDIRTY GETFILE "
                 "OPEN TABLES TABLE FORMS ROW FIRSTROW NEXTROW PREVROW LASTROW COUNTROWS GETFIELDS GETROW ADDROW CHANGEROW DELETEROW QUIT");
    else if (starts_word_ci(command, "DEACTIVATE", &arg)) {
        rc = RX_NOW; put_text(result_text, sizeof result_text, "DEACTIVATE: GFOpenBase has no icon to go to");
    } else if (starts_word_ci(command, "FAULT", &arg)) {
        if (!*arg) { rc = RX_ERROR; put_text(result_text, sizeof result_text, "FAULT needs a code"); }
        else Fault(atol(arg), NULL, (STRPTR)result_text, sizeof result_text);
    } else if (starts_word_ci(command, "ACTIVATE", &arg)) {
        if (win) { WindowToFront(win); ActivateWindow(win); }
    } else if (starts_word_ci(command, "ISDIRTY", &arg)) put_text(result_text, sizeof result_text, "0");   /* rows are written as they are typed */
    else if (starts_word_ci(command, "GETFILE", &arg)) put_text(result_text, sizeof result_text, proj_path);
    else if (starts_word_ci(command, "OPEN", &arg)) rc = rexx_open_cmd(arg, result_text, sizeof result_text);
    else if (starts_word_ci(command, "TABLES", &arg)) {
        int i;
        size_t k = 0;
        for (i = 0; proj && i < table_count; i++) {
            const char *t = ob_table_name(proj, i);
            size_t l = strlen(t);
            if (k + l + 4 >= sizeof result_text) break;
            if (k) result_text[k++] = ' ';
            result_text[k++] = '"'; memcpy(result_text + k, t, l); k += l; result_text[k++] = '"';
        }
        result_text[k] = 0;
    } else if (starts_word_ci(command, "FORMS", &arg)) {      /* the current table's designed forms */
        static char fn[PICK_FORMS][OB_NAME_MAX];
        int i, n = proj && have_cur ? ob_forms_of(proj, cur.name, fn, PICK_FORMS) : 0;
        size_t k = 0;
        if (n > PICK_FORMS) n = PICK_FORMS;
        for (i = 0; i < n; i++) {
            size_t l = strlen(fn[i]);
            if (k + l + 4 >= sizeof result_text) break;
            if (k) result_text[k++] = ' ';
            result_text[k++] = '"'; memcpy(result_text + k, fn[i], l); k += l; result_text[k++] = '"';
        }
        result_text[k] = 0;
        if (n < 0) { rc = RX_ERROR; put_text(result_text, sizeof result_text, ob_error(proj)); }
    } else if (starts_word_ci(command, "TABLE", &arg)) {
        char t[OB_NAME_MAX];
        int i;
        rexx_arg(arg, t, sizeof t);
        if (!t[0]) put_text(result_text, sizeof result_text, have_cur ? cur.name : "");
        else if ((i = table_named(t)) < 0) { rc = RX_ERROR; snprintf(result_text, sizeof result_text, "No table %.100s in the project", t); }
        else { load_tables(ob_table_name(proj, i)); put_text(result_text, sizeof result_text, "OK"); }
    } else if (starts_word_ci(command, "ROW", &arg)) {
        char r[24];
        rexx_arg(arg, r, sizeof r);
        if (!r[0]) {
            if (have_cur && sel_row >= top_row && sel_row - top_row < page_n)
                snprintf(result_text, sizeof result_text, "%ld", (long)page_keys[sel_row - top_row]);
        } else if (!have_cur) { rc = RX_NOW; put_text(result_text, sizeof result_text, "No table is shown"); }
        else if (!choose_row(r)) { rc = RX_NOW; snprintf(result_text, sizeof result_text, "No row %.20s in the table", r); }
        else put_text(result_text, sizeof result_text, "OK");
    } else if (starts_word_ci(command, "QUIT", &arg)) {
        quit_now = 1; put_text(result_text, sizeof result_text, "Quitting");
    } else if (starts_word_ci(command, "FIRSTROW", NULL) || starts_word_ci(command, "LASTROW", NULL)
               || starts_word_ci(command, "NEXTROW", NULL) || starts_word_ci(command, "PREVROW", NULL)
               || starts_word_ci(command, "COUNTROWS", NULL)) {
        rc = rexx_walk(command, result_text, sizeof result_text);
    } else if ((r = rexx_rows(command, result_text, sizeof result_text)) >= 0) {
        rc = r;
    } else { rc = RX_UNKNOWN; put_text(result_text, sizeof result_text, "Unknown GFOpenBase ARexx command; HELP lists them"); }
    if (rc != RX_OK) put_text(rexx_lasterror, sizeof rexx_lasterror, result_text);
    if (result_out) {
        *result_out = (char *)malloc(strlen(result_text) + 1);
        if (*result_out) strcpy(*result_out, result_text);
        else if (rc == RX_OK) rc = RX_UNKNOWN;
    }
    if (win) refresh();
    return rc;
}

static void rexx_messages(void)
{
    struct RexxMsg *rm;
    while (rexx_port && (rm = (struct RexxMsg *)GetMsg(rexx_port)) != NULL) {
        char *res = NULL;
        int rc;
        rexx_busy = 1;
        rexx_noted = 0;
        rc = rexx_do_command((const char *)rm->rm_Args[0], &res);
        rexx_busy = 0;
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
    for (n = 1; n < 10; n++) {                       /* GFOPENBASE, then GFOPENBASE.2 for a second copy */
        if (n == 1) put_text(rexx_name, sizeof rexx_name, REXX_PORT_NAME);
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
    w = scr->Width < 800 ? scr->Width : 800;
    h = scr->Height - scr->BarHeight - 1 < 600 ? scr->Height - scr->BarHeight - 1 : 600;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenBase",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, (scr->Width - w) / 2, WA_Top, scr->BarHeight + 1 + (scr->Height - scr->BarHeight - 1 - h) / 2,
        WA_MinWidth, 400, WA_MinHeight, 240, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
                  LISTVIEWIDCMP | SCROLLERIDCMP,
        TAG_DONE);
    if (!win) return 0;
    if (menu) SetMenuStrip(win, menu);
    SetFont(win->RPort, font);
    if (!make_gadgets()) return 0;
    return 1;
}

static void close_window(void)
{
    if (win) {
        ClearMenuStrip(win);
        free_gadgets();
        CloseWindow(win);
        win = NULL;
    }
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static int window_main(int argc, char **argv)
{
    int rc = 0, create = 0;
    char first[512] = "", table[OB_NAME_MAX] = "", row[24] = "";
    NewList(&table_list);
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38))) {
        if (argc) PutStr((STRPTR)"GFOpenBase needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    if (argc > 1) {
        int i;
        for (i = 1; i < argc; i++) {
            const char *rest;
            if (!strcmp(argv[i], "NEW") || !strcmp(argv[i], "new")) create = 1;
            else if (starts_word_ci(argv[i], "TABLE", &rest) && argv[i][5] == '=') put_text(table, sizeof table, rest);
            else if (starts_word_ci(argv[i], "ROW", &rest) && argv[i][3] == '=') put_text(row, sizeof row, rest);
            else put_text(first, sizeof first, argv[i]);
        }
    } else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    if (!open_window()) {
        if (argc) PutStr((STRPTR)"GFOpenBase: the window could not open\n");
        rc = 20;
        goto out;
    }
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    rexx_open();
    if (first[0] && open_project(first, create)) {
        int t = table[0] ? table_named(table) : -1;
        if (t >= 0) load_tables(ob_table_name(proj, t));
        else if (table[0]) set_status("No such table in the project");
        if (row[0] && !choose_row(row)) set_status("No such row in the table");
    }
    refresh();
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
    close_project();
    close_window();
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}

int main(int argc, char **argv)
{
    /* SQLite's parser and planner recurse: the design asks for at least 64 KB (11.2) */
    return ow_run_with_stack(65536, window_main, argc, argv);
}
