/*
 * GFOpenPresentation's chart editor window (app/opr_chartwin.h; Presentation gap 2).
 *
 * The chart opens on GFOpenCalc's core (opr_chart_edit_open): its data on the sheet "Data", the chart beside
 * it, both drawn as GFOpenCalc draws its grid (oc_view_draw). Click a cell and type to change the data, as in
 * GFOpenCalc: Return and Tab commit and move, Escape cancels, Delete clears, the arrows move. A new column is a
 * new series, a new row a new category. Type, Arrangement and Legend change the chart's look through
 * oc_core_update_chart, Title its title. Undo takes back the last change of either kind. OK gives the chart
 * back (opr_chart_edit_take) for the slide; Cancel and the close gadget drop the changes.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <graphics/regions.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/layers.h>

#include <stdlib.h>
#include <string.h>

#include "opr_chartedit.h"
#include "opr_chartwin.h"

enum { G_KIND = 1, G_GROUPING, G_LEGEND, G_TITLE, G_UNDO, G_OK, G_CANCEL };

static const opr_cwin_env *env;
static struct Window *cw;
static oc_core *core;
static oc_draw_list dl;
static struct Gadget *g_kind, *g_grouping, *g_legend, *g_title;
static int gx, gy, gw, gh;                 /* the grid's box in the window */
static int fh;
static char title_buf[256];

/* UTF-8 to Latin-1 (what the screen's fonts draw), letters outside it as '?'. */
static size_t to_latin1(const char *s, size_t n, char *out, size_t size)
{
    size_t i = 0, o = 0;
    if (!size) return 0;
    while (i < n && o + 1 < size) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out[o++] = (char)c; i++; }
        else if ((c & 0xE0) == 0xC0 && i + 1 < n) {
            unsigned v = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
            out[o++] = v < 256 ? (char)v : '?';
            i += 2;
        } else {
            out[o++] = '?';
            i++;
            while (i < n && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
        }
    }
    out[o] = 0;
    return o;
}

static size_t to_utf8(const char *s, char *out, size_t size)
{
    size_t o = 0;
    for (; *s && o + 2 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x80) out[o++] = (char)c;
        else { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    if (size) out[o] = 0;
    return o;
}

static int measure(void *user, const char *utf8, size_t length, unsigned flags, int font, int size)
{
    char buf[256];
    size_t n = to_latin1(utf8, length, buf, sizeof buf);
    (void)user; (void)flags; (void)font; (void)size;
    return cw ? (int)TextLength(cw->RPort, (STRPTR)buf, (ULONG)n) : (int)n * 8;
}

static LONG role_pen(int role, unsigned long rgb)
{
    switch (role) {
    case OC_ROLE_CELL_FILL:
    case OC_ROLE_CHART: return env->pen(rgb);
    case OC_ROLE_SELECTION: return env->pen(0xD6E4F5UL);
    case OC_ROLE_GRID: return env->pen(0xD4D4D4UL);
    case OC_ROLE_BORDER:
    case OC_ROLE_TEXT: return env->pen(rgb == OWF_COLOUR_AUTO ? 0 : rgb);
    case OC_ROLE_HEADER: return env->pen(0xEBEBEBUL);
    case OC_ROLE_HEADER_SELECTED: return env->pen(0xD2D2D2UL);
    case OC_ROLE_HEADER_LINE: return env->pen(0xC0C0C0UL);
    case OC_ROLE_CURSOR: return env->pen(0x2F6FC9UL);
    }
    return env->pen(0xFFFFFFUL);
}

static void box(LONG pen, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(cw->RPort, (ULONG)pen);
    RectFill(cw->RPort, gx + x, gy + y, gx + x + w - 1, gy + y + h - 1);
}

/* Text from x in the window's font, cut to cw pixels from cx. */
static void text_at(LONG pen, const char *utf8, int x, int y, int cx, int clipw)
{
    char buf[256];
    struct TextExtent te;
    size_t n = to_latin1(utf8, strlen(utf8), buf, sizeof buf);
    LONG fit;
    if (!n || x < cx || clipw <= 0) return;
    fit = TextFit(cw->RPort, (STRPTR)buf, (ULONG)n, &te, NULL, 1, (ULONG)(cx + clipw - x), (ULONG)(fh + 4));
    if (fit <= 0) return;
    SetAPen(cw->RPort, (ULONG)pen);
    SetDrMd(cw->RPort, JAM1);
    Move(cw->RPort, gx + x, gy + y + cw->RPort->TxBaseline);
    Text(cw->RPort, (STRPTR)buf, (ULONG)fit);
}

static void paint_item(const oc_draw_item *it)
{
    switch (it->kind) {
    case OC_DRAW_FILL:
        box(role_pen(it->role, it->rgb), it->x, it->y, it->w, it->h);
        break;
    case OC_DRAW_LINE:
        box(role_pen(it->role, it->rgb), it->x, it->y, it->w ? it->w : 1, it->w ? 1 : it->h);
        break;
    case OC_DRAW_TEXT:
        if (it->text)
            text_at(role_pen(OC_ROLE_TEXT, it->rgb), it->text, it->text_x, it->y + (it->h - fh) / 2, it->clip_x,
                    it->clip_w);
        break;
    case OC_DRAW_HEADER:
        box(role_pen(it->role, 0), it->x, it->y, it->w, it->h);
        if (it->text) {
            int tw = measure(NULL, it->text, strlen(it->text), 0, -1, 0);
            text_at(env->pen(0x333333UL), it->text, it->align == OC_ALIGN_LEFT ? it->x + it->text_x : it->x + (it->w - tw) / 2,
                    it->y + (it->h - fh) / 2, it->x, it->w - 1);
        }
        break;
    case OC_DRAW_FRAME: {
        LONG pen = role_pen(it->role, 0);
        int t = it->line > 0 ? it->line : 1;
        box(pen, it->x, it->y, it->w, t);
        box(pen, it->x, it->y + it->h - t, it->w, t);
        box(pen, it->x, it->y, t, it->h);
        box(pen, it->x + it->w - t, it->y, t, it->h);
        break;
    }
    case OC_DRAW_CARET:
        box(env->pen(0), it->x, it->y, 1, it->h);
        break;
    case OC_DRAW_SEGMENT:
        SetAPen(cw->RPort, (ULONG)role_pen(it->role, it->rgb));
        Move(cw->RPort, gx + it->x, gy + it->y);
        Draw(cw->RPort, gx + it->x + it->w, gy + it->y + it->h);
        break;
    case OC_DRAW_POLY: {
        int k, n = it->npoints;
        if (!it->points || n < 3) break;
        SetAPen(cw->RPort, (ULONG)role_pen(it->role, it->rgb));
        /* an outline, filled by lines across from its first point: no TmpRas to borrow here */
        for (k = 1; k + 1 < n; k++) {
            int ax = it->points[0], ay = it->points[1];
            int bx = it->points[2 * k], by = it->points[2 * k + 1], cx = it->points[2 * k + 2], cy = it->points[2 * k + 3];
            int steps = abs(cx - bx) > abs(cy - by) ? abs(cx - bx) : abs(cy - by), s;
            if (steps < 1) steps = 1;
            for (s = 0; s <= steps; s++) {
                Move(cw->RPort, gx + ax, gy + ay);
                Draw(cw->RPort, gx + bx + (cx - bx) * s / steps, gy + by + (cy - by) * s / steps);
            }
        }
        break;
    }
    }
}

static void paint_grid(void)
{
    struct Region *region = NULL, *old = NULL;
    struct Rectangle r;
    int i;
    if (gw <= 0 || gh <= 0) return;
    oc_view_set_size(core, gw, gh);
    oc_view_draw(core, &dl);
    r.MinX = (WORD)gx; r.MinY = (WORD)gy; r.MaxX = (WORD)(gx + gw - 1); r.MaxY = (WORD)(gy + gh - 1);
    if (LayersBase && (region = NewRegion()) != NULL) {
        if (OrRectRegion(region, &r)) old = InstallClipRegion(cw->WLayer, region);
        else { DisposeRegion(region); region = NULL; }
    }
    if (env->font) SetFont(cw->RPort, env->font);
    box(env->pen(0xFFFFFFUL), 0, 0, gw, gh);
    for (i = 0; i < dl.n; i++)
        paint_item(&dl.items[i]);
    if (region) {
        InstallClipRegion(cw->WLayer, old);
        DisposeRegion(region);
    }
}

/* The gadgets as the chart now is. */
static void show_chart(void)
{
    owf_chart c;
    memset(&c, 0, sizeof c);
    if (opr_chart_edit_take(core, &c) != OWF_OK) return;
    GT_SetGadgetAttrs(g_kind, cw, NULL, GTCY_Active, (ULONG)opr_chart_kind_choice(c.kind), TAG_DONE);
    GT_SetGadgetAttrs(g_grouping, cw, NULL, GTCY_Active,
                      (ULONG)(c.grouping >= 0 && c.grouping < OPR_CHART_GROUPINGS ? c.grouping : 0), TAG_DONE);
    GT_SetGadgetAttrs(g_legend, cw, NULL, GTCY_Active, (ULONG)opr_chart_legend_choice(c.legend), TAG_DONE);
    to_latin1(c.title ? c.title : "", c.title ? strlen(c.title) : 0, title_buf, sizeof title_buf);
    GT_SetGadgetAttrs(g_title, cw, NULL, GTST_String, (ULONG)title_buf, TAG_DONE);
    owf_chart_clear(&c);
}

static void update(unsigned set, int value, const char *title)
{
    oc_chart_edit_spec e;
    char utf8[512];
    memset(&e, 0, sizeof e);
    e.set = set;
    if (set == OC_CHART_SET_KIND) e.kind = opr_chart_choice_kind(value);
    else if (set == OC_CHART_SET_GROUPING) e.grouping = value;
    else if (set == OC_CHART_SET_LEGEND) e.legend = opr_chart_choice_legend(value);
    else if (set == OC_CHART_SET_TITLE) {
        to_utf8(title, utf8, sizeof utf8);
        e.title = utf8;
    }
    if (oc_core_editing(core)) oc_edit_commit(core, OC_MOVE_NONE);
    if (oc_core_update_chart(core, 0, &e) != OWF_OK) DisplayBeep(env->scr);
    paint_grid();
}

static int rawkey(UWORD code, UWORD qual)
{
    unsigned mods = (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? OC_MOD_SHIFT : 0;
    if (qual & IEQUALIFIER_CONTROL) mods |= OC_MOD_CTRL;
    switch (code) {
    case 0x4C: return oc_core_key(core, OC_KEY_UP, mods);
    case 0x4D: return oc_core_key(core, OC_KEY_DOWN, mods);
    case 0x4E: return oc_core_key(core, OC_KEY_RIGHT, mods);
    case 0x4F: return oc_core_key(core, OC_KEY_LEFT, mods);
    case 0x51: return oc_core_key(core, OC_KEY_F2, mods);
    }
    return OWF_ERR_UNSUPPORTED;
}

int opr_chart_window(const opr_cwin_env *e, const owf_chart *chart, owf_chart *out)
{
    struct Gadget *list = NULL, *last;
    struct NewGadget ng;
    struct TextFont *tf;
    int fw, bl, bt, ww, wh, m = 6, row, lw, ok = 0, done = 0, pressing = 0;
    STRPTR kind_labels[OPR_CHART_KINDS + 1], grouping_labels[OPR_CHART_GROUPINGS + 1], legend_labels[OPR_CHART_LEGENDS + 1];
    int i;
    if (!e || !e->scr || !e->vi || !e->pen || !chart || !out) return 0;
    env = e;
    memset(out, 0, sizeof *out);
    for (i = 0; i <= OPR_CHART_KINDS; i++) kind_labels[i] = (STRPTR)opr_chart_kind_labels[i];
    for (i = 0; i <= OPR_CHART_GROUPINGS; i++) grouping_labels[i] = (STRPTR)opr_chart_grouping_labels[i];
    for (i = 0; i <= OPR_CHART_LEGENDS; i++) legend_labels[i] = (STRPTR)opr_chart_legend_labels[i];
    core = opr_chart_edit_open(chart);
    if (!core) return 0;
    oc_draw_list_init(&dl);
    tf = e->font;
    fh = tf ? tf->tf_YSize : 8;
    fw = tf ? (tf->tf_XSize > 0 ? tf->tf_XSize : 8) : 8;
    bl = e->scr->WBorLeft;
    bt = e->scr->WBorTop + e->scr->Font->ta_YSize + 1;
    ww = 78 * fw;
    if (ww > e->scr->Width - 24) ww = e->scr->Width - 24;
    wh = 32 * (fh + 2);
    if (wh > e->scr->Height - bt - 24) wh = e->scr->Height - bt - 24;
    row = fh + 6;

    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = e->ta;
    ng.ng_VisualInfo = e->vi;
    ng.ng_Height = row;
    last = CreateContext(&list);
    lw = 6 * fw;
    ng.ng_Flags = PLACETEXT_LEFT;
    ng.ng_LeftEdge = bl + m + lw; ng.ng_TopEdge = bt + m; ng.ng_Width = 14 * fw;
    ng.ng_GadgetText = (STRPTR)"Type"; ng.ng_GadgetID = G_KIND;
    last = g_kind = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)kind_labels,
                                 GTCY_Active, (ULONG)opr_chart_kind_choice(chart->kind), TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 13 * fw; ng.ng_Width = 16 * fw;
    ng.ng_GadgetText = (STRPTR)"Arrangement"; ng.ng_GadgetID = G_GROUPING;
    last = g_grouping = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)grouping_labels,
                                     GTCY_Active, (ULONG)(chart->grouping >= 0 && chart->grouping < OPR_CHART_GROUPINGS ?
                                                          chart->grouping : 0), TAG_DONE);
    ng.ng_LeftEdge += ng.ng_Width + 8 * fw; ng.ng_Width = 13 * fw;
    ng.ng_GadgetText = (STRPTR)"Legend"; ng.ng_GadgetID = G_LEGEND;
    last = g_legend = CreateGadget(CYCLE_KIND, last, &ng, GTCY_Labels, (ULONG)legend_labels,
                                   GTCY_Active, (ULONG)opr_chart_legend_choice(chart->legend), TAG_DONE);
    to_latin1(chart->title ? chart->title : "", chart->title ? strlen(chart->title) : 0, title_buf, sizeof title_buf);
    ng.ng_LeftEdge = bl + m + lw; ng.ng_TopEdge = bt + m + row + m; ng.ng_Width = ww - 2 * m - lw;
    ng.ng_GadgetText = (STRPTR)"Title"; ng.ng_GadgetID = G_TITLE;
    last = g_title = CreateGadget(STRING_KIND, last, &ng, GTST_String, (ULONG)title_buf, GTST_MaxChars, 200, TAG_DONE);
    ng.ng_Flags = PLACETEXT_IN;
    ng.ng_TopEdge = bt + wh - m - row; ng.ng_Width = 10 * fw;
    ng.ng_LeftEdge = bl + m; ng.ng_GadgetText = (STRPTR)"Undo"; ng.ng_GadgetID = G_UNDO;
    last = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = bl + ww - m - 2 * ng.ng_Width - m; ng.ng_GadgetText = (STRPTR)"OK"; ng.ng_GadgetID = G_OK;
    last = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    ng.ng_LeftEdge = bl + ww - m - ng.ng_Width; ng.ng_GadgetText = (STRPTR)"Cancel"; ng.ng_GadgetID = G_CANCEL;
    last = CreateGadget(BUTTON_KIND, last, &ng, TAG_DONE);
    if (!last || !g_kind || !g_grouping || !g_legend || !g_title) goto out;

    gx = bl + m;
    gy = bt + m + 2 * (row + m);
    gw = ww - 2 * m;
    gh = wh - (gy - bt) - row - 2 * m;
    cw = OpenWindowTags(NULL, WA_Title, (ULONG)"Chart Data", WA_PubScreen, (ULONG)e->scr,
        WA_InnerWidth, ww, WA_InnerHeight, wh, WA_Left, (e->scr->Width - ww - bl * 2) / 2,
        WA_Top, (e->scr->Height - wh - bt) / 2, WA_Gadgets, (ULONG)list, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE, WA_RMBTrap, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_VANILLAKEY |
                  IDCMP_RAWKEY | BUTTONIDCMP | CYCLEIDCMP | STRINGIDCMP,
        TAG_DONE);
    if (!cw) goto out;
    if (tf) SetFont(cw->RPort, tf);
    oc_view_set_measure(core, measure, NULL);
    oc_view_set_headers(core, 5 * fw, fh + 4);
    oc_view_set_row_numbers(core, fw, fw);
    GT_RefreshWindow(cw, NULL);
    paint_grid();
    while (!done) {
        struct IntuiMessage *msg;
        Wait(1UL << cw->UserPort->mp_SigBit);
        while (!done && (msg = GT_GetIMsg(cw->UserPort)) != NULL) {
            ULONG cls = msg->Class;
            UWORD code = msg->Code, qual = msg->Qualifier;
            int mx = msg->MouseX - gx, my = msg->MouseY - gy, redraw = 0;
            struct Gadget *gad = (cls == IDCMP_GADGETUP) ? (struct Gadget *)msg->IAddress : NULL;
            GT_ReplyIMsg(msg);
            switch (cls) {
            case IDCMP_CLOSEWINDOW: done = 1; break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(cw);
                GT_EndRefresh(cw, TRUE);
                paint_grid();
                break;
            case IDCMP_GADGETUP:
                switch (gad ? gad->GadgetID : 0) {
                case G_KIND: update(OC_CHART_SET_KIND, code, NULL); break;
                case G_GROUPING: update(OC_CHART_SET_GROUPING, code, NULL); break;
                case G_LEGEND: update(OC_CHART_SET_LEGEND, code, NULL); break;
                case G_TITLE: {
                    STRPTR s = NULL;
                    GT_GetGadgetAttrs(g_title, cw, NULL, GTST_String, (ULONG)&s, TAG_DONE);
                    update(OC_CHART_SET_TITLE, 0, s ? (const char *)s : "");
                    break;
                }
                case G_UNDO:
                    if (oc_core_editing(core)) oc_edit_cancel(core);
                    else if (oc_core_undo(core) != OWF_OK) DisplayBeep(e->scr);
                    show_chart();
                    redraw = 1;
                    break;
                case G_OK:
                    if (oc_core_editing(core) && oc_edit_commit(core, OC_MOVE_NONE) != OWF_OK) { DisplayBeep(e->scr); break; }
                    if (opr_chart_edit_take(core, out) == OWF_OK) ok = 1;
                    else DisplayBeep(e->scr);
                    done = 1;
                    break;
                case G_CANCEL: done = 1; break;
                }
                break;
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTDOWN && mx >= 0 && my >= 0 && mx < gw && my < gh) {
                    oc_hit hit;
                    if (oc_view_hit(core, mx, my, &hit) && hit.kind != OC_HIT_NONE) {
                        oc_core_press(core, &hit, (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) != 0, 0);
                        pressing = 1;
                        redraw = 1;
                    }
                } else if (code == SELECTUP) pressing = 0;
                break;
            case IDCMP_MOUSEMOVE:
                if (pressing) {
                    oc_hit hit;
                    if (oc_view_hit(core, mx, my, &hit) && hit.kind == OC_HIT_CELL) {
                        oc_core_drag(core, &hit);
                        redraw = 1;
                    }
                }
                break;
            case IDCMP_VANILLAKEY:
                if (opr_chart_edit_char(core, (unsigned char)code) == OWF_OK) redraw = 1;
                break;
            case IDCMP_RAWKEY:
                if (!(code & 0x80) && rawkey(code, qual) == OWF_OK) redraw = 1;
                break;
            }
            if (redraw && !done) paint_grid();
        }
    }
out:
    if (cw) { CloseWindow(cw); cw = NULL; }
    if (list) FreeGadgets(list);
    oc_draw_list_free(&dl);
    oc_core_free(core);
    core = NULL;
    if (e->parent) ActivateWindow(e->parent);
    return ok;
}
