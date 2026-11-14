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
 *   GFOpenProject [FILE]        FILE: a plan (.gfproj, MS Project XML, CSV, MPX or GanttProject .gan)
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

#define VERSION_TEXT "GFOpenProject 0.1.0 (14.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " GFOpenProject, the project planner of the GFOffice Suite. MIT, Copyright (c) 2026 AmiCradle Team";

/* ---- Menus ---- */

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXPORT_XML, M_EXPORT_CSV, M_PRINT, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_EDIT, M_INSERT, M_DELETE, M_INDENT, M_OUTDENT, M_LINK, M_UNLINK,
    M_DAYS, M_WEEKS, M_MONTHS, M_QUARTERS, M_SHOW_BASELINE, M_RESOURCES, M_START,
    M_SET_BASELINE, M_CLEAR_BASELINE
};

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", "A", 0, 0, (APTR)M_SAVEAS },
    { NM_ITEM, "Export MS Project XML...", NULL, 0, 0, (APTR)M_EXPORT_XML },
    { NM_ITEM, "Export CSV...", NULL, 0, 0, (APTR)M_EXPORT_CSV },
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
    { NM_ITEM, "Insert Task", "I", 0, 0, (APTR)M_INSERT },
    { NM_ITEM, "Delete Task", NULL, 0, 0, (APTR)M_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Indent", ">", 0, 0, (APTR)M_INDENT },
    { NM_ITEM, "Outdent", "<", 0, 0, (APTR)M_OUTDENT },
    { NM_ITEM, "Link to Next Task", "L", 0, 0, (APTR)M_LINK },
    { NM_ITEM, "Unlink", "U", 0, 0, (APTR)M_UNLINK },
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
    { NM_TITLE, "Tools", NULL, 0, 0, NULL },
    { NM_ITEM, "Set Baseline", NULL, 0, 0, (APTR)M_SET_BASELINE },
    { NM_ITEM, "Clear Baseline", NULL, 0, 0, (APTR)M_CLEAR_BASELINE },
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
static opj_doc *shown;             /* the doc the view was made for: OPEN and NEW replace rx.doc */
static opj_view view;
static oldraw *od;
static int sel_col = OPJ_COL_NAME;
static char status[256];
static char title_text[300];

#define DOC (rx.doc)

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
    snprintf(line, sizeof line, "%s \"%s\"%s%s", verb, name, more ? " " : "", more ? more : "");
    return run(line, NULL, 0);
}

/* ---- The view ---- */

static int inner_w(void) { return win ? win->GZZWidth : 0; }
static int inner_h(void) { return win ? win->GZZHeight : 0; }

/* The view for the doc shown: a new doc gets a new view, the same doc keeps its scroll and choices. */
static void view_for_doc(void)
{
    int zoom = view.zoom, base = view.show_baseline;
    if (DOC != shown) {
        opj_view_init(&view, DOC, inner_w(), inner_h(), fh + 5, shown ? zoom : OPJ_ZOOM_DAYS);
        view.show_baseline = base;
        view.selected = DOC->plan->ntasks ? 0 : -1;
        shown = DOC;
    }
    view.width = inner_w();
    view.height = inner_h();
    if (view.selected >= DOC->plan->ntasks) view.selected = DOC->plan->ntasks - 1;
}

static void update_title(void)
{
    const char *name = rx.file[0] ? leaf(rx.file) : "Untitled";
    if (!win) return;
    if (status[0]) snprintf(title_text, sizeof title_text, "GFOpenProject - %s%s - %s", name, DOC->dirty ? " *" : "", status);
    else snprintf(title_text, sizeof title_text, "GFOpenProject - %s%s", name, DOC->dirty ? " *" : "");
    SetWindowTitles(win, (UBYTE *)title_text, (UBYTE *)~0);
}

/* The selected cell's box, or 0 when it isn't on show. */
static int cell_box(oldraw_rect *r)
{
    int c, x = 0;
    if (view.selected < view.first_row || view.selected >= view.first_row + opj_rows_shown(&view)) return 0;
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
    oldraw_damage_all(od);
    oldraw_redraw(od, draw_cb, NULL);
    update_title();
}

/* The selection on show: rows scrolled to it. */
static void keep_selection_shown(void)
{
    int shown_rows = opj_rows_shown(&view);
    if (view.selected < 0) return;
    if (view.selected < view.first_row) view.first_row = view.selected;
    if (shown_rows > 1 && view.selected >= view.first_row + shown_rows - 1) view.first_row = view.selected - shown_rows + 2;
    if (view.first_row < 0) view.first_row = 0;
}

static void move_selection(int rows, int cols)
{
    int n = DOC->plan->ntasks;
    if (rows && n) {
        int s = view.selected < 0 ? 0 : view.selected + rows;
        view.selected = s < 0 ? 0 : s >= n ? n - 1 : s;
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
    if (rc == OWF_ERR_MEMORY) snprintf(msg, sizeof msg, "%s: not enough memory", what);
    else snprintf(msg, sizeof msg, "%s: not possible (out of range, text it doesn't take, or a loop of links)", what);
    tell(msg);
}

static void edit_cell(void)
{
    char text[400], prompt[64];
    int rc;
    if (view.selected < 0) {
        if ((rc = opj_insert_task(DOC, DOC->plan->ntasks, "")) != OWF_OK) { edit_failed(rc, "New task"); return; }
        view.selected = DOC->plan->ntasks - 1;
    }
    if (sel_col == OPJ_COL_ID || sel_col == OPJ_COL_WBS || sel_col >= OPJ_COL_BASELINE_START) {
        set_status("That column is worked out, not typed");
        redraw();
        return;
    }
    opj_cell_text(DOC, view.selected, sel_col, text, sizeof text);
    snprintf(prompt, sizeof prompt, "%s", opj_column_title(sel_col));
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

static void open_file(const char *path)
{
    if (run_file("OPEN", path, "FORCE") != OPJ_RX_OK) tell(rx.lasterror);
    else set_status("");
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
    snprintf(more, sizeof more, "FORMAT %s", format);
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
            static const int width[OPJ_NRCOLS] = { 4, 16, 9, 5, 10, 6, 10, 8, 10 };
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
    int done = 0, sel = -1, col = OPJ_RCOL_NAME, w = fw * 86, lines = 12, h, i, by;
    char head[200];
    size_t k = 0;
    static const int width[OPJ_NRCOLS] = { 4, 16, 9, 5, 10, 6, 10, 8, 10 };
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
    SetAPen(rw->RPort, 1);
    Move(rw->RPort, rw->BorderLeft + 12, rw->BorderTop + 4 + font->tf_Baseline);
    Text(rw->RPort, (STRPTR)head, (WORD)strlen(head));
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

/* ---- Menus, mouse and keys ---- */

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
    case M_PRINT:
        if (!print_plan(why, sizeof why)) tell(why);
        else set_status(why);
        break;
    case M_ABOUT:
        tell(VERSION_TEXT "\nThe project planner of the GFOffice Suite.\n\nPlans in .gfproj, MS Project XML and CSV;\n"
             "MPX and GanttProject .gan read.\nMIT, Copyright (c) 2026 AmiCradle Team");
        break;
    case M_QUIT: if (may_drop()) quit_now = 1; break;
    case M_UNDO: undo_redo(0); return;
    case M_REDO: undo_redo(1); return;
    case M_EDIT: edit_cell(); return;
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
    case M_SET_BASELINE: edit_failed(opj_set_baseline(DOC, 0, -1), "Set Baseline"); view.show_baseline = 1; break;
    case M_CLEAR_BASELINE: edit_failed(opj_clear_baseline(DOC, 0, -1), "Clear Baseline"); break;
    }
    redraw();
}

static void click(int x, int y, ULONG secs, ULONG micros)
{
    static ULONG last_s, last_m;
    static int last_row = -1, last_col = -1;
    opj_hit hit;
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
        view.selected = hit.row;
        redraw();
    } else if (hit.what == OPJ_HIT_COLUMN_TITLE) {
        if (hit.column >= 0 && hit.column < OPJ_NCOLS) { sel_col = hit.column; redraw(); }
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
            break;
        case IDCMP_VANILLAKEY:
            if (code == 13) edit_cell();
            else if (code == 127) delete_task();
            else if (code == 9 && view.selected >= 0) {
                edit_failed(opj_indent(DOC, view.selected, 1, (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? -1 : 1),
                            "Indent");
                redraw();
            }
            break;
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
                  IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS,
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
    if (rx.doc) opj_doc_free(rx.doc);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}
