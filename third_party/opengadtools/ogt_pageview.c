/* ogt_pageview: OpenGadTools' scrolled view, drawn in the theme.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/utility.h>

#include <stdlib.h>
#include <string.h>

#include "ogt_pageview.h"

#define SCROLLER_W 14
#define WHEEL_UP 0x7a
#define WHEEL_DOWN 0x7b
#ifndef IDCMP_EXTENDEDMOUSE                      /* OS 3.2's wheel messages (intuition/intuition.h V47) */
#define IDCMP_EXTENDEDMOUSE 0x80000000
#define IMSGCODE_INTUIWHEELDATA 0x0001
struct IntuiWheelData { UWORD Version; WORD WheelX; WORD WheelY; };
#endif
#define HSCROLL_GID(v) ((v)->gid + 1000)       /* the horizontal scroller's GA_ID: the view's + 1000 */

ogt_pageview *ogt_pageview_new(struct Window *win, ogt_ctx *ctx, int gid, ogt_pageview_draw_fn draw, void *user, const char *bg_key)
{
    ogt_pageview *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->win = win;
    v->ctx = ctx;
    v->gid = gid;
    v->draw = draw;
    v->user = user;
    v->bg_key = bg_key;
    v->sw = SCROLLER_W;
    v->sh = SCROLLER_W;
    v->step = 48;
    return v;
}

static void remove_prop(ogt_pageview *v)
{
    if (v->prop) {
        RemoveGadget(v->win, v->prop);
        DisposeObject(v->prop);
        v->prop = NULL;
    }
    if (v->hprop) {
        RemoveGadget(v->win, v->hprop);
        DisposeObject(v->hprop);
        v->hprop = NULL;
    }
}

void ogt_pageview_free(ogt_pageview *v)
{
    if (!v) return;
    remove_prop(v);
    free(v);
}

static int visible_h(const ogt_pageview *v) { return v->h - v->sh - 2 > 0 ? v->h - v->sh - 2 : 0; }
static int visible_w(const ogt_pageview *v) { return v->w - v->sw - 2 > 0 ? v->w - v->sw - 2 : 0; }

static void clamp(ogt_pageview *v)
{
    int max = v->total - visible_h(v);
    if (v->top > max) v->top = max;
    if (v->top < 0) v->top = 0;
    max = v->cw - visible_w(v);
    if (v->left > max) v->left = max;
    if (v->left < 0) v->left = 0;
}

static void update_prop(ogt_pageview *v)
{
    if (v->prop)
        SetGadgetAttrs(v->prop, v->win, NULL,
                       PGA_Total, (ULONG)(v->total >> v->scale), PGA_Visible, (ULONG)(visible_h(v) >> v->scale),
                       PGA_Top, (ULONG)(v->top >> v->scale), TAG_DONE);
    if (v->hprop)
        SetGadgetAttrs(v->hprop, v->win, NULL,
                       PGA_Total, (ULONG)v->cw, PGA_Visible, (ULONG)visible_w(v), PGA_Top, (ULONG)v->left, TAG_DONE);
}

void ogt_pageview_layout(ogt_pageview *v, int x, int y, int w, int h)
{
    v->x = x; v->y = y; v->w = w; v->h = h;
    remove_prop(v);
    clamp(v);
    v->prop = (struct Gadget *)NewObject(NULL, (UBYTE *)"propgclass",
                                         GA_Left, x + w - v->sw, GA_Top, y + 1, GA_Width, v->sw - 1, GA_Height, h - 2,
                                         GA_ID, v->gid, GA_RelVerify, TRUE, GA_Immediate, TRUE,
                                         PGA_Freedom, FREEVERT, PGA_NewLook, TRUE, PGA_Borderless, TRUE,
                                         PGA_Total, (ULONG)(v->total >> v->scale), PGA_Visible, (ULONG)(visible_h(v) >> v->scale),
                                         PGA_Top, (ULONG)(v->top >> v->scale),
                                         ICA_TARGET, ICTARGET_IDCMP, TAG_DONE);
    if (v->prop) AddGadget(v->win, v->prop, (UWORD)~0);
    /* the horizontal scroller along the bottom (0.2.1): wide content (a page at 150%) scrolls sideways */
    v->hprop = (struct Gadget *)NewObject(NULL, (UBYTE *)"propgclass",
                                          GA_Left, x + 1, GA_Top, y + h - v->sh, GA_Width, w - v->sw - 2, GA_Height, v->sh - 1,
                                          GA_ID, HSCROLL_GID(v), GA_RelVerify, TRUE, GA_Immediate, TRUE,
                                          PGA_Freedom, FREEHORIZ, PGA_NewLook, TRUE, PGA_Borderless, TRUE,
                                          PGA_Total, (ULONG)v->cw, PGA_Visible, (ULONG)visible_w(v), PGA_Top, (ULONG)v->left,
                                          ICA_TARGET, ICTARGET_IDCMP, TAG_DONE);
    if (v->hprop) AddGadget(v->win, v->hprop, (UWORD)~0);
}

void ogt_pageview_set_content(ogt_pageview *v, int cw, int total)
{
    v->cw = cw > 0 ? cw : 0;
    v->total = total > 0 ? total : 0;
    for (v->scale = 0; (v->total >> v->scale) > 32000; v->scale++) ;
    clamp(v);
    update_prop(v);
}

void ogt_pageview_set_pages(ogt_pageview *v, int n, int pw, int ph, int gap, int margin)
{
    v->pages = n > 0 ? n : 0;
    v->ph = ph > 0 ? ph : 0;
    v->gap = gap >= 0 ? gap : 0;
    v->margin = margin >= 0 ? margin : 0;
    ogt_pageview_set_content(v, pw + 2 * margin, n > 0 ? 2 * margin + n * ph + (n - 1) * gap : 0);
}

int ogt_pageview_page_top(const ogt_pageview *v, int page)
{
    if (page < 0) page = 0;
    if (v->pages && page >= v->pages) page = v->pages - 1;
    return v->margin + page * (v->ph + v->gap);
}

int ogt_pageview_page_at(const ogt_pageview *v, int cy)
{
    int page, within;
    if (!v->pages || v->ph + v->gap <= 0) return -1;
    cy -= v->margin;
    if (cy < 0) return -1;
    page = cy / (v->ph + v->gap);
    within = cy - page * (v->ph + v->gap);
    return page < v->pages && within < v->ph ? page : -1;
}

int ogt_pageview_page(const ogt_pageview *v)
{
    int p = v->pages ? (v->top - v->margin + v->ph / 2) / (v->ph + v->gap) : 0;
    if (p < 0) p = 0;
    if (v->pages && p >= v->pages) p = v->pages - 1;
    return p;
}

static void draw_part(ogt_pageview *v, int py, int ph);

void ogt_pageview_scroll_to(ogt_pageview *v, int top)
{
    int was = v->top, bx = v->x + 1, by = v->y + 1, bw = visible_w(v), bh = visible_h(v), dy;
    v->top = top;
    clamp(v);
    if (v->top == was) return;
    update_prop(v);
    dy = v->top - was;
    if (dy > -bh && dy < bh && bw > 0 && bh > 0) {
        /* 0.2.1: the view moves as a whole and only the strip that came into view is drawn: no flicker */
        ScrollWindowRaster(v->win, 0, dy, bx, by, bx + bw - 1, by + bh - 1);
        if (dy > 0) draw_part(v, by + bh - dy, dy);
        else draw_part(v, by, -dy);
    } else ogt_pageview_draw(v);
}

/* Scrolls sideways to content x at the left. */
void ogt_pageview_scroll_left_to(ogt_pageview *v, int left)
{
    int was = v->left;
    v->left = left;
    clamp(v);
    if (v->left != was) { update_prop(v); ogt_pageview_draw(v); }
}

void ogt_pageview_show_page(ogt_pageview *v, int page)
{
    ogt_pageview_scroll_to(v, ogt_pageview_page_top(v, page) - (v->margin < 8 ? v->margin : 8));
}

void ogt_pageview_scroll_by(ogt_pageview *v, int dy)
{
    ogt_pageview_scroll_to(v, v->top + dy);
}

void ogt_pageview_box(const ogt_pageview *v, int *x, int *y, int *w, int *h)
{
    *x = v->x + 1; *y = v->y + 1; *w = visible_w(v); *h = visible_h(v);
}

/* Narrow content is centred in the box: its left in box coordinates. */
static int content_x0(const ogt_pageview *v)
{
    int vw = visible_w(v);
    return v->cw < vw ? (vw - v->cw) / 2 : 0;
}

int ogt_pageview_to_content(const ogt_pageview *v, int mx, int my, int *cx, int *cy)
{
    int bx = v->x + 1, by = v->y + 1, bw = visible_w(v), bh = visible_h(v);
    *cx = mx - bx - content_x0(v) + v->left;
    *cy = my - by + v->top;
    return mx >= bx && mx < bx + bw && my >= by && my < by + bh;
}

/* Draws the rows py .. py+ph-1 (window coordinates) of the view: the whole of it, or the strip a scroll uncovered. */
static void draw_part(ogt_pageview *v, int py, int ph)
{
    struct RastPort *rp = v->win->RPort;
    struct Region *clip, *old;
    struct Rectangle r;
    int bx = v->x + 1, by = v->y + 1, bw = visible_w(v), bh = visible_h(v), x0 = content_x0(v);
    if (bw <= 0 || bh <= 0 || ph <= 0) return;
    r.MinX = bx; r.MinY = py > by ? py : by; r.MaxX = bx + bw - 1; r.MaxY = (py + ph - 1 < by + bh - 1) ? py + ph - 1 : by + bh - 1;
    if (r.MinY > r.MaxY) return;
    if (!(clip = NewRegion())) return;
    OrRectRegion(clip, &r);
    old = InstallClipRegion(v->win->WLayer, clip);
    /* the background where the content doesn't reach: beside narrow content, below short content */
    if (x0 > 0) {
        ogt_fill(v->ctx, rp, v->bg_key, bx, by, x0, bh);
        if (bx + x0 + v->cw < bx + bw) ogt_fill(v->ctx, rp, v->bg_key, bx + x0 + v->cw, by, bw - x0 - v->cw, bh);
    }
    if (v->total - v->top < bh) {
        int from = v->total - v->top > 0 ? v->total - v->top : 0;
        ogt_fill(v->ctx, rp, v->bg_key, bx, by + from, bw, bh - from);
    }
    if (v->draw && v->total > v->top) {
        int dw = v->cw < bw ? v->cw : bw, dh = v->total - v->top < bh ? v->total - v->top : bh;
        v->draw(v->user, rp, bx + x0, by, dw, dh, v->left, v->top);
    }
    InstallClipRegion(v->win->WLayer, old);
    DisposeRegion(clip);
}

void ogt_pageview_draw(ogt_pageview *v)
{
    int bh = visible_h(v);
    if (bh <= 0) return;
    draw_part(v, v->y + 1, bh);
    /* the frame, as the list's: a sunken bevel round the content */
    ogt_bevel(v->win->RPort, ogt_pen(v->ctx, "string.shadow"), ogt_pen(v->ctx, "string.shine"), v->x, v->y, v->w - v->sw, v->h - v->sh);
}

static int read_prop(ogt_pageview *v)
{
    ULONG top = 0;
    if (!v->prop) return 0;
    GetAttr(PGA_Top, v->prop, &top);
    if ((int)(top << v->scale) != v->top) {
        int was = v->top;
        ogt_pageview_scroll_to(v, (int)(top << v->scale));   /* draws: a raster scroll where it can */
        return v->top != was;
    }
    return 0;
}

static int read_hprop(ogt_pageview *v)
{
    ULONG left = 0;
    if (!v->hprop) return 0;
    GetAttr(PGA_Top, v->hprop, &left);
    if ((int)left != v->left) { ogt_pageview_scroll_left_to(v, (int)left); return 1; }
    return 0;
}

int ogt_pageview_event(ogt_pageview *v, ULONG class, UWORD code, APTR iaddress, int mx, int my, int *cx, int *cy)
{
    if (class == IDCMP_IDCMPUPDATE) {
        struct TagItem *tags = (struct TagItem *)iaddress;
        if (tags && GetTagData(GA_ID, 0, tags) == (ULONG)v->gid) return read_prop(v) ? OGT_PV_SCROLLED : OGT_PV_NONE;
        if (tags && GetTagData(GA_ID, 0, tags) == (ULONG)HSCROLL_GID(v)) return read_hprop(v) ? OGT_PV_SCROLLED : OGT_PV_NONE;
        return OGT_PV_NONE;
    }
    if ((class == IDCMP_GADGETUP || class == IDCMP_GADGETDOWN) && iaddress == (APTR)v->prop)
        return read_prop(v) ? OGT_PV_SCROLLED : OGT_PV_NONE;
    if ((class == IDCMP_GADGETUP || class == IDCMP_GADGETDOWN) && v->hprop && iaddress == (APTR)v->hprop)
        return read_hprop(v) ? OGT_PV_SCROLLED : OGT_PV_NONE;
    if (class == IDCMP_RAWKEY && (code == WHEEL_UP || code == WHEEL_DOWN)) {
        int was = v->top;
        ogt_pageview_scroll_by(v, code == WHEEL_UP ? -v->step : v->step);
        return v->top != was ? OGT_PV_SCROLLED : OGT_PV_NONE;
    }
    if (class == IDCMP_EXTENDEDMOUSE && code == IMSGCODE_INTUIWHEELDATA && iaddress) {
        /* 0.2.1: OS 3.2 reports the wheel this way to a window that asks for IDCMP_EXTENDEDMOUSE */
        const struct IntuiWheelData *wd = (const struct IntuiWheelData *)iaddress;
        int was = v->top, wasl = v->left;
        if (wd->WheelY) ogt_pageview_scroll_by(v, wd->WheelY * v->step);
        if (wd->WheelX) ogt_pageview_scroll_left_to(v, v->left + wd->WheelX * v->step);
        return v->top != was || v->left != wasl ? OGT_PV_SCROLLED : OGT_PV_NONE;
    }
    if (class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN && ogt_pageview_to_content(v, mx, my, cx, cy)) {
        v->dragging = 1;
        return OGT_PV_PRESS;
    }
    if (class == IDCMP_MOUSEMOVE && v->dragging) {
        ogt_pageview_to_content(v, mx, my, cx, cy);
        return OGT_PV_DRAG;
    }
    if (class == IDCMP_MOUSEBUTTONS && code == SELECTUP && v->dragging) {
        v->dragging = 0;
        ogt_pageview_to_content(v, mx, my, cx, cy);
        return OGT_PV_RELEASE;
    }
    return OGT_PV_NONE;
}
