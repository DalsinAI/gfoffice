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
 *   GFOpenBase [FILE] [NEW]     FILE: a project (.oadb); NEW makes it if there is none
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
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openbase_core.h"
#include "ow_stack.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL;

#define VERSION_TEXT "GFOpenBase 0.2.0 (11.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " GFOpenBase, the database of the GFOffice Suite. MIT, Copyright (c) 2026 AmiCradle Team";

/* ---- Menus and gadgets ---- */

enum {
    M_NEW = 1, M_OPEN, M_CLOSE, M_COPY, M_COMPACT, M_CHECK, M_ABOUT, M_QUIT,
    M_T_NEW, M_T_DESIGN, M_T_DELETE, M_T_RELATIONS, M_R_ADD, M_R_EDIT, M_R_DELETE, M_R_FIRST, M_R_LAST
};

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
    { NM_TITLE, "Table", NULL, 0, 0, NULL },
    { NM_ITEM, "New Table...", "T", 0, 0, (APTR)M_T_NEW },
    { NM_ITEM, "Design...", "D", 0, 0, (APTR)M_T_DESIGN },
    { NM_ITEM, "Delete Table...", NULL, 0, 0, (APTR)M_T_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Relationships...", "R", 0, 0, (APTR)M_T_RELATIONS },
    { NM_TITLE, "Row", NULL, 0, 0, NULL },
    { NM_ITEM, "Add Row...", "A", 0, 0, (APTR)M_R_ADD },
    { NM_ITEM, "Edit Row...", "E", 0, 0, (APTR)M_R_EDIT },
    { NM_ITEM, "Delete Row...", NULL, 0, 0, (APTR)M_R_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "First Row", NULL, 0, 0, (APTR)M_R_FIRST },
    { NM_ITEM, "Last Row", NULL, 0, 0, (APTR)M_R_LAST },
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
static ob_int total_rows;
static ob_int top_row;             /* the first row shown, by position */
static int sel_row = -1;           /* by position; -1 none */
static int left_col;               /* the first field shown */
#define PAGE_MAX 128
static int page_n;
static ob_int page_keys[PAGE_MAX];
static char *page_cells[PAGE_MAX][OB_MAX_FIELDS];

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

static void tell(const char *text)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)"OK" };
    EasyRequest(win, &es, NULL, (ULONG)text);
}

static int ask(const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenBase", (UBYTE *)"%s", (UBYTE *)buttons };
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
static int ask_file(const char *title, int save, char *out, size_t size)
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
        ASLFR_DoSaveMode, save, ASLFR_DoPatterns, TRUE, ASLFR_InitialPattern, (ULONG)"#?.oadb",
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
    if (!proj || !have_cur) return;
    total_rows = ob_row_total(proj, cur.name);
    if (total_rows < 0) total_rows = 0;
    if (top_row > total_rows - ds_rows) top_row = total_rows - ds_rows;
    if (top_row < 0) top_row = 0;
    if (sel_row >= total_rows) sel_row = (int)total_rows - 1;
    ob_rows_at(proj, cur.name, top_row, ds_rows < PAGE_MAX ? ds_rows : PAGE_MAX, page_cb, NULL);
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
    if (!have_cur) {
        SetAPen(rp, text_pen);
        text_fit(rp, ds_x + 8, ds_y + 8, ds_w - 16, table_count ? "Choose a table" : "No tables yet: Table > New Table...");
        goto status_line;
    }
    /* the heading */
    x = ds_x + 4;
    y = ds_y + 2;
    SetAPen(rp, fill_pen);
    RectFill(rp, ds_x + 1, ds_y + 1, ds_x + ds_w - 2, ds_y + row_h);
    for (j = left_col; j < cur.nfields && x < ds_x + ds_w - 4; j++) {
        const ob_field *f = &cur.fields[j];
        int w = col_width(f);
        SetAPen(rp, filltext_pen);
        text_fit(rp, x, y, (x + w - 6 < ds_x + ds_w - 4 ? w - 6 : ds_x + ds_w - 4 - x), f->label[0] ? f->label : f->name);
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
        for (j = left_col; j < cur.nfields && x < ds_x + ds_w - 4; j++) {
            int w = col_width(&cur.fields[j]);
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
        text_fit(rp, ds_x + 8, ds_y + 2 + row_h + 4, ds_w - 16, "No rows yet: Row > Add Row...");
    }
status_line:
    /* the status line, under the datasheet */
    SetAPen(rp, back_pen);
    RectFill(rp, ds_x, ds_y + ds_h + 2, ds_x + ds_w - 1, ds_y + ds_h + 3 + fh);
    SetAPen(rp, text_pen);
    if (status[0]) {
        text_fit(rp, ds_x + 2, ds_y + ds_h + 3, ds_w - 4, status);
    } else if (have_cur) {
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

static void edit_row(int adding)
{
    struct Window *ew;
    struct Gadget *eg = NULL, *g, *fields_g[ED_MAX] = { 0 };
    struct NewGadget ng;
    char *values[OB_MAX_FIELDS], *before[OB_MAX_FIELDS];
    ob_int key = 0;
    int i, page = 0, pages, per, done = 0, lw = 0, vw, h, row_h = fh + 8, nshow, first;
    if (!proj || !have_cur) return;
    if (!adding && (sel_row < 0 || sel_row < top_row || sel_row >= top_row + page_n)) {
        tell("Choose a row first.");
        return;
    }
    memset(values, 0, sizeof values);
    memset(before, 0, sizeof before);
    for (i = 0; i < cur.nfields; i++) {
        const char *v = "";
        int n = (int)strlen(cur.fields[i].label[0] ? cur.fields[i].label : cur.fields[i].name);
        if (n > lw) lw = n;
        if (!adding) {
            int k = sel_row - (int)top_row;
            key = page_keys[k];
            v = page_cells[k][i] ? page_cells[k][i] : "";
        } else if (cur.fields[i].has_default) {
            v = cur.fields[i].deflt;
        }
        values[i] = malloc(1024);
        before[i] = malloc(1024);
        if (!values[i] || !before[i]) goto out;
        put_text(values[i], 1024, v);
        put_text(before[i], 1024, adding ? "" : v);
    }
    /* the datasheet keeps values cut short: read the row whole */
    if (!adding) {
        ob_rows(proj, cur.name, key - 1, 1, whole_cb, values);
        for (i = 0; i < cur.nfields; i++) put_text(before[i], 1024, values[i]);
    }
    per = (scr->Height - 120) / row_h;
    if (per > ED_MAX) per = ED_MAX;
    if (per < 4) per = 4;
    pages = (cur.nfields + per - 1) / per;
    lw = (lw > 24 ? 24 : lw) * fw + 16;
    vw = fw * 40;
    if (lw + vw + 40 > scr->Width) vw = scr->Width - lw - 40;
    nshow = cur.nfields < per ? cur.nfields : per;
    h = nshow * row_h + fh * 2 + 40;
    ew = OpenWindowTags(NULL,
        WA_Title, (ULONG)(adding ? "Add Row" : "Edit Row"),
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
        nshow = cur.nfields - first < per ? cur.nfields - first : per;
        if (!(g = CreateContext(&eg))) break;
        memset(&ng, 0, sizeof ng);
        ng.ng_VisualInfo = vi;
        ng.ng_TextAttr = &font_attr;
        for (i = 0; i < nshow; i++) {
            const ob_field *f = &cur.fields[first + i];
            ng.ng_LeftEdge = ew->BorderLeft + 10 + lw;
            ng.ng_TopEdge = ew->BorderTop + 8 + i * row_h;
            ng.ng_Width = vw;
            ng.ng_Height = fh + 6;
            ng.ng_GadgetText = (UBYTE *)(f->label[0] ? f->label : f->name);
            ng.ng_Flags = PLACETEXT_LEFT;
            ng.ng_GadgetID = 100 + i;
            if (f->type == OB_COUNTER || f->type == OB_OBJECT)
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
            if (fields_g[i] && cur.fields[first + i].type != OB_COUNTER && cur.fields[first + i].type != OB_OBJECT) {
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
                const ob_field *f = &cur.fields[first + i];
                if (f->type != OB_COUNTER && f->type != OB_OBJECT) {
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
                for (i = 0; i < cur.nfields; i++) {
                    const ob_field *f = &cur.fields[i];
                    if (f->type == OB_COUNTER || f->type == OB_OBJECT) continue;
                    if (adding ? values[i][0] == 0 && !f->has_default : !strcmp(values[i], before[i])) continue;
                    names[n] = f->name;
                    vals[n] = values[i];
                    n++;
                }
                if (adding) good = ob_row_add(proj, cur.name, n, names, vals, &key);
                else good = ob_row_change(proj, cur.name, key, n, names, vals);
                if (good) {
                    done = 1;
                    set_status(adding ? "Row added" : "Row saved");
                    if (adding) {
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
out:
    if (eg) FreeGadgets(eg);
    for (i = 0; i < OB_MAX_FIELDS; i++) { free(values[i]); free(before[i]); }
    load_page();
    refresh();
}

static void delete_row(void)
{
    char q[200];
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
 * relationship a line from the Lookup field to the table it looks up. The
 * relationships are made and changed in the table designer (Looks up). */

#define RV_FIELDS 10        /* fields shown in a box */

static ob_table rv_t;       /* one table's design at a time (too big for the stack) */

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
};

static void show_relationships(void)
{
    struct Window *rw;
    struct RastPort *rp;
    struct rv_box *box = NULL;
    ob_relation *rel = NULL;
    int nt, nr, i, j, done = 0, row_h = fh + 2, bw = fw * 18, gap, cols, x, y, rowmax, w, h, hidden = 0;
    char line[160];
    if (!proj) { tell("Open or make a project first."); return; }
    nt = ob_table_count(proj);
    nr = ob_relations(proj, NULL, 0);
    if (nr < 0) { tell(ob_error(proj)); return; }
    box = calloc((size_t)(nt ? nt : 1), sizeof *box);
    rel = calloc((size_t)(nr ? nr : 1), sizeof *rel);
    if (!box || !rel) { tell("Not enough memory."); goto out; }
    ob_relations(proj, rel, nr);
    w = scr->Width * 4 / 5;
    h = scr->Height * 3 / 4;
    rw = OpenWindowTags(NULL,
        WA_Title, (ULONG)"Relationships",
        WA_PubScreen, (ULONG)scr,
        WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2,
        WA_Width, w, WA_Height, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
        WA_SmartRefresh, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_DONE);
    if (!rw) { tell("The window could not be opened."); goto out; }
    rp = rw->RPort;
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    SetAPen(rp, pen(BACKGROUNDPEN));
    RectFill(rp, rw->BorderLeft, rw->BorderTop, rw->Width - rw->BorderRight - 1, rw->Height - rw->BorderBottom - 1);
    /* lay the boxes out in rows, leaving room between columns for the lines */
    gap = fw * 6;
    cols = (rw->Width - rw->BorderLeft - rw->BorderRight - gap) / (bw + gap);
    if (cols < 1) cols = 1;
    x = rw->BorderLeft + gap;
    y = rw->BorderTop + 10;
    rowmax = 0;
    for (i = 0; i < nt; i++) {
        struct rv_box *b = &box[i];
        put_text(b->name, sizeof b->name, ob_table_name(proj, i));
        if (ob_table_get(proj, b->name, &rv_t)) {
            for (j = 0; j < rv_t.nfields && b->nfields < RV_FIELDS; j++)
                put_text(b->fields[b->nfields++], OB_NAME_MAX, rv_t.fields[j].name);
            if (rv_t.nfields > RV_FIELDS) put_text(b->fields[RV_FIELDS - 1], OB_NAME_MAX, "...");
        }
        b->w = bw;
        b->h = row_h * (b->nfields + 1) + 6;
        if (i && i % cols == 0) { x = rw->BorderLeft + gap; y += rowmax + row_h * 2; rowmax = 0; }
        b->x = x;
        b->y = y;
        b->shown = y + b->h < rw->Height - rw->BorderBottom - row_h - 8;
        if (!b->shown) hidden++;
        if (b->h > rowmax) rowmax = b->h;
        x += bw + gap;
    }
    /* the lines first, so the boxes are drawn over their ends */
    for (i = 0; i < nr; i++) {
        struct rv_box *from = NULL, *to = NULL;
        int fy, tx, fx, ty, f = 0;
        for (j = 0; j < nt; j++) {
            if (same_name(box[j].name, rel[i].table)) from = &box[j];
            if (same_name(box[j].name, rel[i].lookup)) to = &box[j];
        }
        if (!from || !to || !from->shown || !to->shown) continue;
        for (j = 0; j < from->nfields; j++) if (same_name(from->fields[j], rel[i].field)) f = j + 1;
        fy = f ? from->y + 3 + row_h * f + row_h / 2 : from->y + from->h / 2;
        ty = to->y + row_h / 2 + 1;
        if (from == to) {
            /* a table that looks itself up: a loop on its right */
            fx = from->x + from->w;
            SetAPen(rp, pen(FILLPEN));
            Move(rp, fx, fy); Draw(rp, fx + gap / 2, fy); Draw(rp, fx + gap / 2, ty); Draw(rp, fx, ty);
            continue;
        }
        if (to->x >= from->x + from->w) { fx = from->x + from->w; tx = to->x; }
        else if (to->x + to->w <= from->x) { fx = from->x; tx = to->x + to->w; }
        else { fx = from->x + from->w; tx = to->x + to->w; }
        SetAPen(rp, pen(FILLPEN));
        Move(rp, fx, fy);
        if (fx == tx || (fx == from->x + from->w && tx == to->x + to->w)) {
            /* one above the other: out to the side and back */
            int side = (fx > tx ? fx : tx) + gap / 2;
            Draw(rp, side, fy); Draw(rp, side, ty); Draw(rp, tx, ty);
        } else {
            int mid = (fx + tx) / 2;
            Draw(rp, mid, fy); Draw(rp, mid, ty); Draw(rp, tx, ty);
        }
        /* the rule, by the line's start */
        SetAPen(rp, pen(TEXTPEN));
        {
            const char *rule = rel[i].on_delete == OB_DELETE_CASCADE ? "cascade"
                             : rel[i].on_delete == OB_DELETE_CLEAR ? "clear" : "refuse";
            int tw = TextLength(rp, (STRPTR)rule, strlen(rule));
            int rx = fx == from->x ? fx - tw - 3 : fx + 3;
            Move(rp, rx, fy - 2);
            Text(rp, (STRPTR)rule, strlen(rule));
        }
    }
    for (i = 0; i < nt; i++) {
        struct rv_box *b = &box[i];
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
        Text(rp, (STRPTR)b->name, (ULONG)TextFit(rp, (STRPTR)b->name, strlen(b->name), NULL, NULL, 1,
                                                 b->w - 8, row_h + 2));
        SetSoftStyle(rp, 0, FSF_BOLD);
        SetAPen(rp, pen(TEXTPEN));
        ob_table_get(proj, b->name, &rv_t);
        for (j = 0; j < b->nfields; j++) {
            int k = ob_table_find_field(&rv_t, b->fields[j]);
            int keyed = k >= 0 && (rv_t.fields[k].type == OB_COUNTER || rv_t.fields[k].key);
            if (keyed) SetSoftStyle(rp, FSF_BOLD, FSF_BOLD);
            Move(rp, b->x + 6, b->y + 3 + row_h * (j + 1) + rp->TxBaseline);
            Text(rp, (STRPTR)b->fields[j], (ULONG)TextFit(rp, (STRPTR)b->fields[j], strlen(b->fields[j]), NULL, NULL, 1,
                                                          b->w - 10, row_h));
            if (keyed) SetSoftStyle(rp, 0, FSF_BOLD);
        }
    }
    if (!nt) snprintf(line, sizeof line, "No tables yet: Table > New Table...");
    else if (!nr) snprintf(line, sizeof line, "No relationships yet: give a field the type Lookup in the table designer");
    else snprintf(line, sizeof line, "%d table%s, %d relationship%s. Key fields are in bold. Made in the table designer (Looks up).",
                  nt, nt == 1 ? "" : "s", nr, nr == 1 ? "" : "s");
    if (hidden) {
        size_t n = strlen(line);
        snprintf(line + n, sizeof line - n, " %d don't fit.", hidden);
    }
    SetAPen(rp, pen(TEXTPEN));
    Move(rp, rw->BorderLeft + 8, rw->Height - rw->BorderBottom - 6 - fh + rp->TxBaseline);
    Text(rp, (STRPTR)line, (ULONG)TextFit(rp, (STRPTR)line, strlen(line), NULL, NULL, 1,
                                          rw->Width - rw->BorderLeft - rw->BorderRight - 16, fh + 2));
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(rw->UserPort);
        while ((m = GT_GetIMsg(rw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && (code == 27 || code == 13))) done = 1;
            if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(rw); GT_EndRefresh(rw, TRUE); }
        }
    }
    CloseWindow(rw);
out:
    free(box);
    free(rel);
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

static void menu_action(ULONG id)
{
    char path[512];
    set_status("");
    switch (id) {
    case M_NEW:
        if (ask_file("New project", TRUE, path, sizeof path)) open_project(path, 1);
        break;
    case M_OPEN:
        if (ask_file("Open a project", FALSE, path, sizeof path)) open_project(path, 0);
        break;
    case M_CLOSE:
        close_project();
        update_title();
        break;
    case M_COPY:
        if (proj && ask_file("Save a copy as", TRUE, path, sizeof path)) {
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
    case M_R_ADD: edit_row(1); break;
    case M_R_EDIT: edit_row(0); break;
    case M_R_DELETE: delete_row(); break;
    case M_R_FIRST: top_row = 0; sel_row = total_rows ? 0 : -1; load_page(); break;
    case M_R_LAST: top_row = total_rows - ds_rows; sel_row = (int)total_rows - 1; load_page(); break;
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
                if (r >= 0 && r < page_n) {
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
            else if (code == 0x4e && have_cur && left_col < cur.nfields - 1) { left_col++; draw_datasheet(); }
            break;
        }
    }
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
    char first[512] = "";
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
            if (!strcmp(argv[i], "NEW") || !strcmp(argv[i], "new")) create = 1;
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
    if (first[0]) open_project(first, create);
    refresh();
    while (!quit_now) {
        ULONG sig = 1UL << win->UserPort->mp_SigBit;
        ULONG got = Wait(sig | SIGBREAKF_CTRL_C);
        if (got & SIGBREAKF_CTRL_C) quit_now = 1;
        if (got & sig) events();
    }
out:
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
