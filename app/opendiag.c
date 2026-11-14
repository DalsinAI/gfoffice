/*
 * GFOpenDiag - the diagram editor of the GFOffice Suite (amicradle-teams designs/GFOPENDIAG-20261113.md, step D4).
 *
 * The window is the canvas: the page as GFOpenDiag's core draws it (src/diag/odg_view.c) through GFOpenLayout's
 * drawing calls, edited with the mouse as src/diag/odg_edit.c says (select, lasso, move, resize, draw a shape, join
 * shapes), and the GFOPENDIAG ARexx port. Everything about the diagram is the core's: this file only shows it and
 * hands it what the user does. Files open, save and export through the core's ARexx commands (OPEN, SAVE, SAVEAS,
 * EXPORT), so a script and the menus do the same thing. .drawio is the file; SVG and PNG files with the diagram inside
 * open too, and SVG exports. Plain GadTools menus in the screen's font, so it works on any Workbench screen; 68040 code,
 * integer arithmetic only, no FPU needed.
 *
 *   GFOpenDiag [FILE]        FILE: a diagram (.drawio, or an .svg or .png with one inside)
 *
 * Mouse: the Tool menu picks what a drag does. Select picks a shape or connector (Shift adds), drags it, drags a
 * handle to resize, or drags a box round what to pick; double-click a shape to type its label. Box, Rounded,
 * Ellipse, Rhombus and Text draw a shape (a click gives the usual size). Connector joins the shape pressed on to
 * the one let go on (or leaves the end loose).
 * Keys: Del deletes the selection, the cursor keys move it a pixel (Shift: ten) or, with nothing selected, scroll;
 * + and - zoom, Return edits the selected shape's label, Esc goes back to Select.
 *
 * Cleanroom: written from the design and this repository's own code (app/openproject.c's window, requesters and
 * ARexx port); no other program's code was read for it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <workbench/startup.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <devices/inputevent.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/rexxsyslib.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opendiag_rexx.h"
#include "opendiag_edit.h"
#include "opendiag_style.h"

struct Library *GadToolsBase = NULL, *AslBase = NULL, *DiskfontBase = NULL;   /* labels' sizes: OpenDiskFont */
struct RxsLib *RexxSysBase = NULL;

#define VERSION_TEXT "GFOpenDiag 0.1.0 (14.11.2026)"
static const char version[] __attribute__((used)) =
    "$VER: " VERSION_TEXT " GFOpenDiag, the diagram editor of the GFOffice Suite. MIT, Copyright (c) 2026 AmiCradle Team";

/* ---- Menus ---- */

enum {
    M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXPORT_SVG, M_ABOUT, M_QUIT,
    M_UNDO, M_REDO, M_LABEL, M_STYLE, M_DELETE, M_SELECT_ALL, M_SELECT_NONE,
    M_ZOOM_IN, M_ZOOM_OUT, M_ZOOM_100, M_FIT, M_GRID, M_SNAP, M_PAGE_NEXT, M_PAGE_PREV, M_PAGE_ADD, M_LAYERS,
    M_TOOL = 100                         /* M_TOOL + odg_tool */
};

#define TOOL_ITEM(name, key, t) \
    { NM_ITEM, name, key, CHECKIT | ((t) == 0 ? CHECKED : 0), 0x7F & ~(1L << (t)), (APTR)(M_TOOL + (t)) }

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "New", "N", 0, 0, (APTR)M_NEW },
    { NM_ITEM, "Open...", "O", 0, 0, (APTR)M_OPEN },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Save", "S", 0, 0, (APTR)M_SAVE },
    { NM_ITEM, "Save As...", "A", 0, 0, (APTR)M_SAVEAS },
    { NM_ITEM, "Export SVG...", NULL, 0, 0, (APTR)M_EXPORT_SVG },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "About...", "?", 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, "Edit", NULL, 0, 0, NULL },
    { NM_ITEM, "Undo", "Z", 0, 0, (APTR)M_UNDO },
    { NM_ITEM, "Redo", "Y", 0, 0, (APTR)M_REDO },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Edit Label...", "E", 0, 0, (APTR)M_LABEL },
    { NM_ITEM, "Style...", "K", 0, 0, (APTR)M_STYLE },
    { NM_ITEM, "Delete", NULL, 0, 0, (APTR)M_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Select All", "L", 0, 0, (APTR)M_SELECT_ALL },
    { NM_ITEM, "Select None", NULL, 0, 0, (APTR)M_SELECT_NONE },
    { NM_TITLE, "View", NULL, 0, 0, NULL },
    { NM_ITEM, "Zoom In", "+", 0, 0, (APTR)M_ZOOM_IN },
    { NM_ITEM, "Zoom Out", "-", 0, 0, (APTR)M_ZOOM_OUT },
    { NM_ITEM, "Actual Size", "=", 0, 0, (APTR)M_ZOOM_100 },
    { NM_ITEM, "Fit to Window", "F", 0, 0, (APTR)M_FIT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Show Grid", "G", CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)M_GRID },
    { NM_ITEM, "Snap to Grid", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)M_SNAP },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Next Page", ">", 0, 0, (APTR)M_PAGE_NEXT },
    { NM_ITEM, "Previous Page", "<", 0, 0, (APTR)M_PAGE_PREV },
    { NM_ITEM, "Add Page...", NULL, 0, 0, (APTR)M_PAGE_ADD },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Layers...", "J", 0, 0, (APTR)M_LAYERS },
    { NM_TITLE, "Tool", NULL, 0, 0, NULL },
    TOOL_ITEM("Select", "1", ODG_TOOL_SELECT),
    TOOL_ITEM("Box", "2", ODG_TOOL_BOX),
    TOOL_ITEM("Rounded Box", "3", ODG_TOOL_ROUNDED),
    TOOL_ITEM("Ellipse", "4", ODG_TOOL_ELLIPSE),
    TOOL_ITEM("Rhombus", "5", ODG_TOOL_RHOMBUS),
    TOOL_ITEM("Text", "6", ODG_TOOL_TEXT),
    TOOL_ITEM("Connector", "7", ODG_TOOL_CONNECTOR),
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

static odg_rexx rx;                /* the diagram (rx.d), the page edited (rx.doc) and its file, shared with ARexx */
static int need_fit = 1;           /* NEW or OPEN ran: the view shows the new diagram whole */
static int shown_page = -1;        /* the page the view was fitted to: another page is fitted too */
static odg_view view;
static odg_edit ed;
static oldraw *od;
static owf_dnum snap_step = OWF_DNUM(10);  /* the page's own gridSize when it has one */
static int snapping = 1;
static char status[256];
static char title_text[300];

#define DOC (&rx.doc)

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
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenDiag", (UBYTE *)"%s", (UBYTE *)"OK" };
    if (rexx_busy) return;
    EasyRequest(win, &es, NULL, (ULONG)text);
}

/* The button picked, counted from 1; the last button is 0 (Cancel). */
static int ask(const char *text, const char *buttons)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"GFOpenDiag", (UBYTE *)"%s", (UBYTE *)buttons };
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

/* A core command (odg_rexx.c), from the menus or a script; its reason in rx.lasterror when it fails. NEW and
 * OPEN put another diagram in the window, so the view is fitted to it. */
static int core(const char *command, char *result, size_t size)
{
    int rc = odg_rexx_command(&rx, command, result, size);
    if (rc == ODG_RX_OK && (starts_word_ci(command, "NEW", NULL) || starts_word_ci(command, "OPEN", NULL))) need_fit = 1;
    return rc;
}

static int run(const char *command, char *result, size_t size)
{
    char scratch[64];
    if (!result) { result = scratch; size = sizeof scratch; }
    return core(command, result, size);
}

/* A command with a file name: VERB "name" [more]. A name with a quote in it is refused. */
static int run_file(const char *verb, const char *name, const char *more)
{
    char line[600];
    if (strchr(name, '"')) {
        snprintf(rx.lasterror, sizeof rx.lasterror, "A file name with a quote in it can't be used");
        return ODG_RX_ERROR;
    }
    if (strlen(name) > 500) {            /* refused, never saved under a shortened name */
        snprintf(rx.lasterror, sizeof rx.lasterror, "The file name is too long");
        return ODG_RX_ERROR;
    }
    snprintf(line, sizeof line, "%.15s \"%s\"%s%.40s", verb, name, more ? " " : "", more ? more : "");
    return run(line, NULL, 0);
}

static void edit_failed(int rc, const char *what)
{
    char msg[200];
    if (rc == OWF_OK) return;
    if (rc == OWF_ERR_MEMORY) snprintf(msg, sizeof msg, "%.60s: not enough memory", what);
    else snprintf(msg, sizeof msg, "%.60s: not possible here", what);
    tell(msg);
}

/* ---- The view ---- */

static int inner_w(void) { return win ? win->GZZWidth : 0; }
static int inner_h(void) { return win ? win->GZZHeight : 0; }

static void sync_tool_menu(void)
{
    struct MenuItem *item;
    int t;
    if (!win || !menu) return;
    ClearMenuStrip(win);
    for (t = 0; t < ODG_NTOOLS; t++)
        if ((item = ItemAddress(menu, FULLMENUNUM(3, t, NOSUB))) != NULL) {
            if (t == (int)ed.tool) item->Flags |= CHECKED;
            else item->Flags &= ~CHECKED;
        }
    ResetMenuStrip(win, menu);
}

/* After anything that may have replaced the diagram or changed the page: the view, the snap and the selection
 * follow. Another diagram or page is shown whole, nothing selected, snapping to its own grid when it has one. */
static void follow_doc(void)
{
    view.width = inner_w();
    view.height = inner_h();
    if (need_fit || DOC->page != shown_page) {
        odg_edit_clear(&ed);
        if (DOC->grid > 0) snap_step = DOC->grid;
        odg_view_fit(&view, DOC);
        need_fit = 0;
        shown_page = DOC->page;
    }
    DOC->grid = snapping ? snap_step : 0;
    odg_edit_prune(&ed, DOC);
    odg_edit_show(&ed, &view);
}

static void update_title(void)
{
    const char *name = rx.file[0] ? leaf(rx.file) : "Untitled";
    char where[40] = "";
    if (!win) return;
    if (rx.d.npages > 1) snprintf(where, sizeof where, " page %d/%d", DOC->page + 1, rx.d.npages);
    snprintf(title_text, sizeof title_text, "GFOpenDiag - %.80s%s%s - %s %d%%%s%.120s", name, DOC->changed ? " *" : "",
             where, odg_tool_name(ed.tool), view.zoom, status[0] ? " - " : "", status);
    SetWindowTitles(win, (UBYTE *)title_text, (UBYTE *)~0);
}

static void draw_cb(void *user, oldraw *d, const oldraw_rect *area)
{
    (void)user;
    odg_draw(d, &view, DOC, area);
}

/* The drag's outline, drawn complemented by GFOpenLayout (UI-LAYER-RULE: no drawing of our own beside it), so the
 * same call takes it off again. A connector being drawn shows as the box its two ends span. */
static int fb_on;
static oldraw_rect fb;

static void feedback_toggle(void)
{
    if (!od) return;
    oldraw_select_box(od, fb.x, fb.y, fb.x + fb.w, fb.y + fb.h);
    oldraw_flush(od);
}

static void feedback_off(void)
{
    if (fb_on && win) feedback_toggle();
    fb_on = 0;
}

static void feedback_on(void)
{
    feedback_off();
    if (!win || !odg_edit_feedback(&ed, &view, DOC, &fb)) return;
    feedback_toggle();
    fb_on = 1;
}

static void redraw(void)
{
    if (!win) return;
    feedback_off();
    follow_doc();
    if (!od) od = oldraw_new_rastport(win->RPort, scr, inner_w(), inner_h());
    if (!od) return;
    view.font = font_name;
    oldraw_resize(od, inner_w(), inner_h());
    oldraw_damage_all(od);
    oldraw_redraw(od, draw_cb, NULL);
    update_title();
}

static void set_zoom(int zoom)
{
    odg_view_zoom_at(&view, zoom, view.x + view.width / 2, view.y + view.height / 2);
    redraw();
}

static void scroll_by(int dx, int dy)
{
    view.left += (owf_dnum)((long)dx * 10000L / view.zoom);
    view.top += (owf_dnum)((long)dy * 10000L / view.zoom);
    redraw();
}

static void set_tool(odg_tool t)
{
    ed.tool = t;
    sync_tool_menu();
    update_title();
}

/* ---- Edits ---- */

/* text as a label: escaped when the cell's label is HTML */
static int set_label(const char *id, const char *text)
{
    const owf_dcell *c = owf_dpage_find(odg_page(DOC), id);
    char *out;
    size_t n = 0, i, k = 0;
    int rc;
    if (!c) return OWF_ERR_FORMAT;
    if (!c->style || !strstr(c->style, "html=1")) return odg_set_value(DOC, id, text);
    for (i = 0; text[i]; i++) n += text[i] == '&' ? 5 : (text[i] == '<' || text[i] == '>') ? 4 : 1;
    if (!(out = (char *)malloc(n + 1))) return OWF_ERR_MEMORY;
    for (i = 0; text[i]; i++) {
        const char *rep = text[i] == '&' ? "&amp;" : text[i] == '<' ? "&lt;" : text[i] == '>' ? "&gt;" : NULL;
        if (rep) { memcpy(out + k, rep, strlen(rep)); k += strlen(rep); }
        else out[k++] = text[i];
    }
    out[k] = 0;
    rc = odg_set_value(DOC, id, out);
    free(out);
    return rc;
}

static void edit_label(const char *id)
{
    const owf_dcell *c = owf_dpage_find(odg_page(DOC), id);
    char text[400] = "";
    if (!c) return;
    if (c->value) {
        size_t len = 0;
        char *plain = owf_dlabel_text(c->value, c->style && strstr(c->style, "html=1") != NULL, &len);
        if (plain) {
            size_t i;
            put_text(text, sizeof text, plain);
            for (i = 0; text[i]; i++) if (text[i] == '\n') text[i] = ' ';
            owf_free(plain);
        }
    }
    if (!ask_text("Edit Label", "Label", text, sizeof text)) return;
    edit_failed(set_label(id, text), "Label");
    redraw();
}

/* The Style window: the selection's fill, line and text colours, line width, text size, dashed and rounded. A
 * field shows what every selected cell has (empty, or unticked, when they differ); only the fields changed are set,
 * each through odg_style_apply (opendiag_style.h), which checks the text first. */
static void style_window(void)
{
    struct Window *sw;
    struct Gadget *eg = NULL, *g, *field[ODG_NPROPS];
    struct NewGadget ng;
    char was[ODG_NPROPS][24], why[300] = "";
    int same[ODG_NPROPS], ticked[ODG_NPROPS], done = 0, okd = 0, i, lw = 0, w, h, row = fh + 8;
    if (!ed.n || rexx_busy) return;
    for (i = 0; i < ODG_NPROPS; i++) {
        int l = (int)strlen(odg_prop_title((odg_prop)i)) * fw + 12;
        if (l > lw) lw = l;
        same[i] = odg_style_common(DOC, ed.list, ed.n, (odg_prop)i, was[i], sizeof was[i]);
        ticked[i] = same[i] && !strcmp(was[i], "1");
    }
    w = lw + fw * 16 + 24;
    h = row * (ODG_NPROPS + 1) + 16;
    sw = OpenWindowTags(NULL, WA_Title, (ULONG)"Style", WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 40, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP |
                  BUTTONIDCMP | CHECKBOXIDCMP,
        TAG_DONE);
    if (!sw) return;
    if (!(g = CreateContext(&eg))) { CloseWindow(sw); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    for (i = 0; i < ODG_NPROPS; i++) {
        int sw_kind = odg_prop_kind_of((odg_prop)i) == ODG_KIND_SWITCH;
        ng.ng_LeftEdge = sw->BorderLeft + 8 + lw;
        ng.ng_TopEdge = sw->BorderTop + 6 + row * i;
        ng.ng_Width = sw_kind ? 26 : fw * 16;
        ng.ng_Height = fh + 4;
        ng.ng_GadgetText = (UBYTE *)odg_prop_title((odg_prop)i);
        ng.ng_Flags = PLACETEXT_LEFT;
        ng.ng_GadgetID = 10 + i;
        if (sw_kind)
            g = field[i] = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, ticked[i], GTCB_Scaled, TRUE, TAG_DONE);
        else
            g = field[i] = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(same[i] ? was[i] : ""),
                                        GTST_MaxChars, 20, TAG_DONE);
    }
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = sw->BorderTop + 6 + row * ODG_NPROPS + 4;
    ng.ng_LeftEdge = sw->BorderLeft + 8;
    ng.ng_Width = fw * 10 + 16;
    ng.ng_GadgetText = (UBYTE *)"OK";
    ng.ng_GadgetID = 2;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    ng.ng_LeftEdge = sw->Width - sw->BorderRight - 8 - ng.ng_Width;
    ng.ng_GadgetText = (UBYTE *)"Cancel";
    ng.ng_GadgetID = 3;
    g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    if (!g) { FreeGadgets(eg); CloseWindow(sw); return; }
    AddGList(sw, eg, ~0, -1, NULL);
    RefreshGList(eg, sw, NULL, -1);
    GT_RefreshWindow(sw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(sw->UserPort);
        while ((m = GT_GetIMsg(sw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) done = 1;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(sw); GT_EndRefresh(sw, TRUE); }
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 2) done = okd = 1;
            else if (class == IDCMP_GADGETUP && gg->GadgetID == 3) done = 1;
        }
    }
    if (okd) {
        for (i = 0; i < ODG_NPROPS; i++) {
            char now[24];
            int rc;
            if (odg_prop_kind_of((odg_prop)i) == ODG_KIND_SWITCH) {
                LONG on = 0;
                GT_GetGadgetAttrs(field[i], sw, NULL, GTCB_Checked, (ULONG)&on, TAG_DONE);
                if ((on != 0) == ticked[i]) continue;      /* not changed */
                put_text(now, sizeof now, on ? "1" : "0");
            } else {
                STRPTR str = NULL;
                GT_GetGadgetAttrs(field[i], sw, NULL, GTST_String, (ULONG)&str, TAG_DONE);
                put_text(now, sizeof now, str ? (const char *)str : "");
                if (!strcmp(now, same[i] ? was[i] : "")) continue;
            }
            rc = odg_style_apply(DOC, ed.list, ed.n, (odg_prop)i, now);
            if (rc == OWF_ERR_MEMORY) { put_text(why, sizeof why, "Style: not enough memory"); break; }
            if (rc != OWF_OK) {
                size_t at = strlen(why);
                snprintf(why + at, sizeof why - at, "%s%.20s: %.20s isn't %s", at ? "\n" : "", odg_prop_title((odg_prop)i), now,
                         odg_prop_kind_of((odg_prop)i) == ODG_KIND_COLOUR ? "#rgb, #rrggbb or none" :
                         "a number above 0 and below 1000");
            }
        }
    }
    RemoveGList(sw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(sw);
    if (why[0]) tell(why);
    redraw();
}

/* ---- The Layers window: the page's layers, top first, with the current one marked ---- */

static struct List lay_list;
static struct Node lay_nodes[32];
static char lay_text[32][80];

static int lay_count(void)
{
    int n = odg_layer_count(DOC);
    return n > 32 ? 32 : n;
}

/* row r of the list (0 the top) is layer n - 1 - r */
static void lay_build(void)
{
    int n = lay_count(), r;
    NewList(&lay_list);
    for (r = 0; r < n; r++) {
        int i = n - 1 - r;
        const owf_dcell *c = odg_layer_at(DOC, i);
        snprintf(lay_text[r], sizeof lay_text[r], "%s %.50s%s%s", i == DOC->layer ? ">" : " ",
                 c && c->value && c->value[0] ? c->value : (i == 0 ? "Background" : "Layer"),
                 odg_layer_shown(DOC, i) ? "" : " (hidden)", odg_layer_locked(DOC, i) ? " (locked)" : "");
        lay_nodes[r].ln_Name = lay_text[r];
        AddTail(&lay_list, &lay_nodes[r]);
    }
}

static void layers_window(void)
{
    enum { L_LIST = 1, L_CURRENT, L_SHOW, L_LOCK, L_NEW, L_RENAME };
    static const char *const labels[] = { "Draw On", "Show/Hide", "Lock/Unlock", "New...", "Rename..." };
    struct Window *lw;
    struct Gadget *eg = NULL, *g, *g_list;
    struct NewGadget ng;
    int done = 0, row = 0, w = fw * 40, lines = 8, h, i, bw;
    if (rexx_busy) return;
    lay_build();
    h = (fh + 2) * lines + 4 + (fh + 8) * 2 + 16;
    lw = OpenWindowTags(NULL, WA_Title, (ULONG)"Layers", WA_PubScreen, (ULONG)scr,
        WA_Left, win->LeftEdge + 50, WA_Top, win->TopEdge + 50, WA_InnerWidth, w, WA_InnerHeight, h,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE,
        WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | LISTVIEWIDCMP |
                  BUTTONIDCMP,
        TAG_DONE);
    if (!lw) return;
    if (!(g = CreateContext(&eg))) { CloseWindow(lw); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_LeftEdge = lw->BorderLeft + 8;
    ng.ng_TopEdge = lw->BorderTop + 6;
    ng.ng_Width = w - 16;
    ng.ng_Height = (fh + 2) * lines + 4;
    ng.ng_GadgetID = L_LIST;
    g = g_list = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&lay_list, GTLV_ShowSelected, 0,
                              GTLV_Selected, 0, TAG_DONE);
    bw = (w - 16 - 4 * 4) / 5;
    ng.ng_TopEdge += ng.ng_Height + 6;
    ng.ng_Height = fh + 6;
    ng.ng_Width = bw;
    ng.ng_Flags = PLACETEXT_IN;
    for (i = 0; i < 5; i++) {
        ng.ng_LeftEdge = lw->BorderLeft + 8 + i * (bw + 4);
        ng.ng_GadgetText = (UBYTE *)labels[i];
        ng.ng_GadgetID = L_CURRENT + i;
        g = CreateGadget(BUTTON_KIND, g, &ng, TAG_DONE);
    }
    if (!g) { FreeGadgets(eg); CloseWindow(lw); return; }
    AddGList(lw, eg, ~0, -1, NULL);
    RefreshGList(eg, lw, NULL, -1);
    GT_RefreshWindow(lw, NULL);
    while (!done) {
        struct IntuiMessage *m;
        WaitPort(lw->UserPort);
        while ((m = GT_GetIMsg(lw->UserPort)) != NULL) {
            ULONG class = m->Class;
            UWORD code = m->Code;
            struct Gadget *gg = (struct Gadget *)m->IAddress;
            int n = lay_count(), layer = n - 1 - row, rc = OWF_OK, changed = 1;
            GT_ReplyIMsg(m);
            if (class == IDCMP_CLOSEWINDOW || (class == IDCMP_VANILLAKEY && code == 27)) { done = 1; continue; }
            if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(lw); GT_EndRefresh(lw, TRUE); continue; }
            if (class != IDCMP_GADGETUP || layer < 0) continue;
            switch (gg->GadgetID) {
            case L_LIST: row = code; changed = 0; break;
            case L_CURRENT: DOC->layer = layer; break;
            case L_SHOW: rc = odg_layer_show(DOC, layer, !odg_layer_shown(DOC, layer)); break;
            case L_LOCK: rc = odg_layer_lock(DOC, layer, !odg_layer_locked(DOC, layer)); break;
            case L_NEW: {
                char name[60] = "";
                int at = 0;
                struct Window *keep = win;
                win = lw;
                if (!ask_text("New Layer", "Name", name, sizeof name)) { win = keep; changed = 0; break; }
                win = keep;
                rc = odg_layer_add(DOC, name[0] ? name : NULL, &at);
                if (rc == OWF_OK) row = lay_count() - 1 - at;
                break;
            }
            case L_RENAME: {
                const owf_dcell *c = odg_layer_at(DOC, layer);
                char name[60] = "";
                struct Window *keep = win;
                if (c && c->value) put_text(name, sizeof name, c->value);
                win = lw;
                if (!ask_text("Rename Layer", "Name", name, sizeof name)) { win = keep; changed = 0; break; }
                win = keep;
                rc = odg_layer_rename(DOC, layer, name);
                break;
            }
            default: changed = 0; break;
            }
            if (rc != OWF_OK) edit_failed(rc, "Layer");
            if (changed) {
                GT_SetGadgetAttrs(g_list, lw, NULL, GTLV_Labels, ~0, TAG_DONE);
                lay_build();
                GT_SetGadgetAttrs(g_list, lw, NULL, GTLV_Labels, (ULONG)&lay_list, GTLV_Selected, (ULONG)row, TAG_DONE);
                redraw();
            }
        }
    }
    RemoveGList(lw, eg, -1);
    FreeGadgets(eg);
    CloseWindow(lw);
    redraw();
}

static void undo_redo(int redo)
{
    if (!(redo ? odg_can_redo(DOC) : odg_can_undo(DOC))) set_status(redo ? "Nothing to redo" : "Nothing to undo");
    else if ((redo ? odg_redo(DOC) : odg_undo(DOC)) != OWF_OK) tell("Not enough memory to undo");
    redraw();
}

static void go_page(int by)
{
    char line[40];
    int page = DOC->page + by;
    if (page < 0 || page >= rx.d.npages) { set_status(by > 0 ? "This is the last page" : "This is the first page"); redraw(); return; }
    snprintf(line, sizeof line, "PAGE %d", page + 1);
    if (run(line, NULL, 0) != ODG_RX_OK) tell(rx.lasterror);
    redraw();
}

static void add_page(void)
{
    char name[100] = "", line[160];
    if (!ask_text("Add Page", "Name", name, sizeof name)) return;
    if (strchr(name, '"')) { tell("A page name with a quote in it can't be used"); return; }
    snprintf(line, sizeof line, "ADDPAGE \"%.100s\"", name);
    if (run(line, NULL, 0) != ODG_RX_OK) { tell(rx.lasterror); return; }
    go_page(rx.d.npages - 1 - DOC->page);
}

/* ---- Files ---- */

/* 1 when the diagram may be dropped: saved, or the user said so. */
static int may_drop(void)
{
    int k;
    if (!DOC->changed) return 1;
    k = ask("The diagram has changes that are not saved.", "Save|Don't Save|Cancel");
    if (k == 0) return 0;
    if (k == 2) return 1;
    if (!rx.file[0]) {
        char path[512];
        if (!ask_file("Save the diagram as", TRUE, "#?.drawio", path, sizeof path)) return 0;
        if (run_file("SAVEAS", path, NULL) != ODG_RX_OK) { tell(rx.lasterror); return 0; }
        return 1;
    }
    if (run("SAVE", NULL, 0) != ODG_RX_OK) { tell(rx.lasterror); return 0; }
    return 1;
}

static void open_file(const char *path)
{
    if (run_file("OPEN", path, "FORCE") != ODG_RX_OK) tell(rx.lasterror);
    else set_status("");
    redraw();
}

/* The file named at the start: a file that can't be opened goes on the status line, not in a requester, so the
 * ARexx port (already public) is served from the first command and an unattended script is never held up. */
static void open_first(const char *path)
{
    set_status(run_file("OPEN", path, "FORCE") != ODG_RX_OK ? rx.lasterror : "");
    redraw();
}

static void save(int as)
{
    char path[512];
    int rc;
    if (as || !rx.file[0]) {
        if (!ask_file("Save the diagram as", TRUE, "#?.(drawio|svg)", path, sizeof path)) return;
        rc = run_file("SAVEAS", path, NULL);
    } else {
        rc = run("SAVE", NULL, 0);
    }
    if (rc != ODG_RX_OK) tell(rx.lasterror);
    else set_status("Saved");
    redraw();
}

static void export_as(const char *format, const char *pattern)
{
    char path[512], more[24];
    if (!ask_file("Export the diagram as", TRUE, pattern, path, sizeof path)) return;
    snprintf(more, sizeof more, "FORMAT %.10s", format);
    if (run_file("EXPORT", path, more) != ODG_RX_OK) tell(rx.lasterror);
    else set_status("Exported");
    redraw();
}

/* ---- Menus, mouse and keys ---- */

static void menu_action(ULONG id, struct MenuItem *item)
{
    char path[512];
    set_status("");
    if (id >= M_TOOL && id < M_TOOL + ODG_NTOOLS) { set_tool((odg_tool)(id - M_TOOL)); return; }
    switch (id) {
    case M_NEW:
        if (may_drop() && run("NEW FORCE", NULL, 0) != ODG_RX_OK) tell(rx.lasterror);
        break;
    case M_OPEN:
        if (may_drop() && ask_file("Open a diagram", FALSE, "#?.(drawio|xml|svg|png)", path, sizeof path)) {
            open_file(path);
            return;
        }
        break;
    case M_SAVE: save(0); return;
    case M_SAVEAS: save(1); return;
    case M_EXPORT_SVG: export_as("SVG", "#?.svg"); return;
    case M_ABOUT:
        tell(VERSION_TEXT "\nThe diagram editor of the GFOffice Suite.\n\n.drawio diagrams, as diagrams.net reads them;\n"
             "SVG and PNG with the diagram inside open;\nSVG exports.\nMIT, Copyright (c) 2026 AmiCradle Team");
        break;
    case M_QUIT: if (may_drop()) quit_now = 1; break;
    case M_UNDO: undo_redo(0); return;
    case M_REDO: undo_redo(1); return;
    case M_LABEL: if (ed.n) { edit_label(ed.ids[0]); return; } break;
    case M_STYLE: if (ed.n) { style_window(); return; } set_status("Select something to style first"); break;
    case M_DELETE: if (ed.n) edit_failed(odg_edit_delete(&ed, DOC), "Delete"); break;
    case M_SELECT_ALL: odg_edit_select_all(&ed, DOC); break;
    case M_SELECT_NONE: odg_edit_clear(&ed); break;
    case M_ZOOM_IN: set_zoom(view.zoom < 100 ? view.zoom + 10 : view.zoom * 5 / 4); return;
    case M_ZOOM_OUT: set_zoom(view.zoom <= 100 ? view.zoom - 10 : view.zoom * 4 / 5); return;
    case M_ZOOM_100: set_zoom(100); return;
    case M_FIT: odg_view_fit(&view, DOC); break;
    case M_GRID: view.grid = item && (item->Flags & CHECKED) ? 1 : 0; break;
    case M_SNAP: snapping = item && (item->Flags & CHECKED) ? 1 : 0; break;
    case M_PAGE_NEXT: go_page(1); return;
    case M_PAGE_PREV: go_page(-1); return;
    case M_PAGE_ADD: add_page(); return;
    case M_LAYERS: layers_window(); return;
    }
    redraw();
}

static void press(int x, int y, int shift, ULONG secs, ULONG micros)
{
    static ULONG last_s, last_m;
    static char last_id[ODG_EDIT_ID];
    char id[ODG_EDIT_ID];
    odg_drag drag;
    if (ed.tool == ODG_TOOL_SELECT && odg_edit_hit(&view, DOC, x, y, 1, id)) {
        if (!strcmp(id, last_id) && DoubleClick(last_s, last_m, secs, micros)) {
            last_id[0] = 0;
            odg_edit_select(&ed, id, 0);
            redraw();
            edit_label(id);
            return;
        }
        put_text(last_id, sizeof last_id, id);
    } else {
        last_id[0] = 0;
    }
    last_s = secs;
    last_m = micros;
    feedback_off();
    drag = odg_edit_press(&ed, &view, DOC, x, y, shift);
    redraw();
    if (drag != ODG_DRAG_NONE) ReportMouse(TRUE, win);
}

static void motion(int x, int y)
{
    if (ed.drag == ODG_DRAG_NONE) return;
    feedback_off();
    odg_edit_motion(&ed, x, y);
    feedback_on();
}

static void release(int x, int y)
{
    odg_tool was = ed.tool;
    int rc;
    if (ed.drag == ODG_DRAG_NONE) return;
    feedback_off();
    ReportMouse(FALSE, win);
    rc = odg_edit_release(&ed, &view, DOC, x, y);
    edit_failed(rc, "That");
    if (ed.tool != was) sync_tool_menu();
    if (rc == OWF_OK && was == ODG_TOOL_TEXT && ed.made[0]) {
        char made[ODG_EDIT_ID];
        put_text(made, sizeof made, ed.made);
        redraw();
        edit_label(made);
        return;
    }
    redraw();
}

static void nudge_or_scroll(int dx, int dy, int shift)
{
    int step = shift ? 10 : 1;
    if (ed.n) {
        edit_failed(odg_edit_nudge(&ed, DOC, OWF_DNUM(dx * step), OWF_DNUM(dy * step)), "Move");
        redraw();
    } else {
        scroll_by(dx * (shift ? view.width / 2 : view.width / 8 + 1), dy * (shift ? view.height / 2 : view.height / 8 + 1));
    }
}

static void events(void)
{
    struct IntuiMessage *m;
    int moved = 0, mx_last = 0, my_last = 0;
    while (win && (m = GT_GetIMsg(win->UserPort)) != NULL) {
        ULONG class = m->Class, secs = m->Seconds, micros = m->Micros;
        UWORD code = m->Code, qual = m->Qualifier;
        WORD mx = m->MouseX - win->BorderLeft, my = m->MouseY - win->BorderTop;   /* in the inner (GZZ) area */
        int shift = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0;
        GT_ReplyIMsg(m);
        if (class == IDCMP_MOUSEMOVE) {      /* only the last place of a run of moves is drawn */
            moved = 1;
            mx_last = mx;
            my_last = my;
            continue;
        }
        if (moved) { motion(mx_last, my_last); moved = 0; }
        switch (class) {
        case IDCMP_CLOSEWINDOW: if (may_drop()) quit_now = 1; break;
        case IDCMP_NEWSIZE: redraw(); break;
        case IDCMP_REFRESHWINDOW:
            GT_BeginRefresh(win);
            GT_EndRefresh(win, TRUE);
            redraw();
            break;
        case IDCMP_INACTIVEWINDOW:
            if (ed.drag != ODG_DRAG_NONE) { feedback_off(); ReportMouse(FALSE, win); odg_edit_cancel(&ed); redraw(); }
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
            if (code == SELECTDOWN) press(mx, my, shift, secs, micros);
            else if (code == SELECTUP) release(mx, my);
            break;
        case IDCMP_VANILLAKEY:
            if (code == 127 || code == 8) { if (ed.n) { edit_failed(odg_edit_delete(&ed, DOC), "Delete"); redraw(); } }
            else if (code == 13) { if (ed.n) edit_label(ed.ids[0]); }
            else if (code == 27) { odg_edit_clear(&ed); set_tool(ODG_TOOL_SELECT); redraw(); }
            else if (code == '+') menu_action(M_ZOOM_IN, NULL);
            else if (code == '-') menu_action(M_ZOOM_OUT, NULL);
            break;
        case IDCMP_RAWKEY:
            if (code == 0x4c) nudge_or_scroll(0, -1, shift);
            else if (code == 0x4d) nudge_or_scroll(0, 1, shift);
            else if (code == 0x4f) nudge_or_scroll(-1, 0, shift);
            else if (code == 0x4e) nudge_or_scroll(1, 0, shift);
            break;
        }
    }
    if (moved) motion(mx_last, my_last);
}

/* ---- ARexx: the GFOPENDIAG port ---- */

#define REXX_PORT_NAME "GFOPENDIAG"
static struct MsgPort *rexx_port;
static char rexx_name[20];
static char app_lasterror[256];

static int app_error(int rc, const char *why)
{
    put_text(app_lasterror, sizeof app_lasterror, why);
    return rc;
}

/* SELECT id,id,... (none: nothing), comma or space between ids */
static int rexx_select(const char *arg)
{
    char id[ODG_EDIT_ID];
    odg_edit_clear(&ed);
    while (*arg) {
        size_t n = 0;
        while (*arg == ',' || is_space(*arg)) arg++;
        if (!*arg) break;
        while (arg[n] && arg[n] != ',' && !is_space(arg[n])) n++;
        if (n >= sizeof id) return app_error(ODG_RX_ERROR, "SELECT: an id is too long");
        memcpy(id, arg, n);
        id[n] = 0;
        arg += n;
        {
            const owf_dcell *c = owf_dpage_find(odg_page(DOC), id);
            char why[160];
            if (!c || (c->kind != OWF_DCELL_VERTEX && c->kind != OWF_DCELL_EDGE)) {
                snprintf(why, sizeof why, "SELECT: no shape or connector %.100s on the page", id);
                odg_edit_clear(&ed);
                return app_error(ODG_RX_ERROR, why);
            }
        }
        if (!odg_edit_select(&ed, id, 1)) return app_error(ODG_RX_ERROR, "SELECT: too many ids");
    }
    return ODG_RX_OK;
}

/* VERSION, ACTIVATE, DEACTIVATE, FAULT, PRINT, QUIT and the canvas's ZOOM, FIT, TOOL, SELECT and GETSELECTION are
 * the app's; the rest the core's (odg_rexx.c). */
static int rexx_do_command(const char *command, char *result, size_t size)
{
    const char *arg = "";
    int rc = ODG_RX_OK, own = 1;
    result[0] = 0;
    if (starts_word_ci(command, "VERSION", &arg)) put_text(result, size, VERSION_TEXT);
    else if (starts_word_ci(command, "ACTIVATE", &arg)) { if (win) { WindowToFront(win); ActivateWindow(win); } }
    else if (starts_word_ci(command, "DEACTIVATE", &arg))
        rc = app_error(ODG_RX_NOW, "DEACTIVATE: GFOpenDiag has no icon to go to");
    else if (starts_word_ci(command, "FAULT", &arg)) {
        if (!*arg) rc = app_error(ODG_RX_ERROR, "FAULT needs a code");
        else Fault(atol(arg), NULL, (STRPTR)result, (LONG)size);
    } else if (starts_word_ci(command, "PRINT", &arg))
        rc = app_error(ODG_RX_NOW, "PRINT: GFOpenDiag doesn't print yet; EXPORT to SVG and print that");
    else if (starts_word_ci(command, "QUIT", &arg)) {
        if (DOC->changed && !starts_word_ci(arg, "FORCE", NULL))
            rc = app_error(ODG_RX_NOW, "QUIT: the diagram has unsaved changes (FORCE drops them)");
        else quit_now = 1;
    } else if (starts_word_ci(command, "ZOOM", &arg)) {
        long z = atol(arg);
        if (!*arg) snprintf(result, size, "%d", view.zoom);
        else if (z < ODG_ZOOM_MIN || z > ODG_ZOOM_MAX) rc = app_error(ODG_RX_ERROR, "ZOOM: a percent from 10 to 800");
        else odg_view_zoom_at(&view, (int)z, view.x + view.width / 2, view.y + view.height / 2);
    } else if (starts_word_ci(command, "FIT", &arg)) {
        odg_view_fit(&view, DOC);
    } else if (starts_word_ci(command, "TOOL", &arg)) {
        int t;
        if (!*arg) put_text(result, size, odg_tool_name(ed.tool));
        else {
            for (t = 0; t < ODG_NTOOLS; t++) if (starts_word_ci(arg, odg_tool_name((odg_tool)t), NULL)) break;
            if (t == ODG_NTOOLS) rc = app_error(ODG_RX_ERROR, "TOOL: Select, Box, Rounded, Ellipse, Rhombus, Text or Connector");
            else set_tool((odg_tool)t);
        }
    } else if (starts_word_ci(command, "SELECT", &arg)) {
        follow_doc();
        rc = rexx_select(arg);
    } else if (starts_word_ci(command, "GETSELECTION", &arg)) {
        size_t at = 0;
        int i;
        follow_doc();
        for (i = 0; i < ed.n; i++) {
            size_t n = strlen(ed.ids[i]);
            if (at + n + 2 > size) break;
            if (at) result[at++] = ',';
            memcpy(result + at, ed.ids[i], n);
            at += n;
        }
        result[at] = 0;
    } else if (starts_word_ci(command, "LASTERROR", &arg) && app_lasterror[0]) {
        put_text(result, size, app_lasterror);
    } else {
        own = 0;
        app_lasterror[0] = 0;
        rc = core(command, result, size);
    }
    if (own && rc == ODG_RX_OK && !starts_word_ci(command, "LASTERROR", NULL)) app_lasterror[0] = 0;
    return rc;
}

static void rexx_messages(void)
{
    struct RexxMsg *rm;
    while (rexx_port && (rm = (struct RexxMsg *)GetMsg(rexx_port)) != NULL) {
        static char res[4096];
        int rc;
        struct Process *me = (struct Process *)FindTask(NULL);
        APTR was = me->pr_WindowPtr;
        if (!RexxSysBase || !IsRexxMsg(rm)) {     /* not an ARexx message: sent back untouched */
            ReplyMsg((struct Message *)rm);
            continue;
        }
        /* ARexx-RULE: no requester holds a script up, DOS's own ("Please insert volume") included, so an
         * OPEN, SAVEAS or EXPORT naming a volume that isn't there fails with RC 10 instead */
        me->pr_WindowPtr = (APTR)-1;
        rexx_busy = 1;
        rc = rexx_do_command((const char *)rm->rm_Args[0], res, sizeof res);
        rexx_busy = 0;
        me->pr_WindowPtr = was;
        rm->rm_Result1 = rc;
        rm->rm_Result2 = 0;
        if (rc == ODG_RX_OK && (rm->rm_Action & RXFF_RESULT) && RexxSysBase)
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
    for (n = 1; n < 10; n++) {                       /* GFOPENDIAG, then GFOPENDIAG.2 for a second copy */
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
    w = scr->Width < 900 ? scr->Width : 900;
    h = scr->Height - scr->BarHeight - 1 < 600 ? scr->Height - scr->BarHeight - 1 : 600;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenDiag",
        WA_ScreenTitle, (ULONG)VERSION_TEXT,
        WA_PubScreen, (ULONG)scr,
        WA_Width, w, WA_Height, h,
        WA_Left, (scr->Width - w) / 2, WA_Top, scr->BarHeight + 1 + (scr->Height - scr->BarHeight - 1 - h) / 2,
        WA_MinWidth, 240, WA_MinHeight, 120, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE,
        WA_GimmeZeroZero, TRUE,
        WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE, WA_AutoAdjust, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY |
                  IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_INACTIVEWINDOW,
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

int main(int argc, char **argv)
{
    int rc = 0, have_rx = 0;
    char first[512] = "";
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39)) ||
        !(AslBase = OpenLibrary((STRPTR)"asl.library", 38)) ||
        !(DiskfontBase = OpenLibrary((STRPTR)"diskfont.library", 36))) {
        if (argc) PutStr((STRPTR)"GFOpenDiag needs AmigaOS 3.0 or later\n");
        rc = 20;
        goto out;
    }
    if (argc > 1) put_text(first, sizeof first, argv[1]);
    else if (argc == 0) {
        struct WBStartup *w = (struct WBStartup *)argv;
        if (w->sm_NumArgs > 1 && NameFromLock(w->sm_ArgList[1].wa_Lock, (STRPTR)first, sizeof first))
            AddPart((STRPTR)first, w->sm_ArgList[1].wa_Name, sizeof first);
    }
    if (odg_rexx_init(&rx) != OWF_OK) {
        if (argc) PutStr((STRPTR)"GFOpenDiag: not enough memory\n");
        rc = 20;
        goto out;
    }
    have_rx = 1;
    odg_view_init(&view, 100, 100);
    view.grid = 1;
    odg_edit_init(&ed);
    if (!open_window()) {
        if (argc) PutStr((STRPTR)"GFOpenDiag: the window could not open\n");
        rc = 20;
        goto out;
    }
    RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36);
    rexx_open();
    if (first[0]) open_first(first);
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
    if (have_rx) odg_rexx_free(&rx);
    if (DiskfontBase) CloseLibrary(DiskfontBase);
    if (AslBase) CloseLibrary(AslBase);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    return rc;
}
