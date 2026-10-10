/* ogt_ruler: OpenGadTools' horizontal ruler, drawn in the theme.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <intuition/intuition.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>

#include <stdio.h>
#include <string.h>

#include "ogt_ruler.h"

#define MARK 7                      /* a marker's size */

void ogt_ruler_init(ogt_ruler *r, struct Window *win, ogt_ctx *ctx)
{
    memset(r, 0, sizeof *r);
    r->win = win;
    r->ctx = ctx;
    r->num = 1; r->den = 20;        /* 72 dpi: a twip is a twentieth of a pixel */
    r->page_w_twips = 11906;        /* A4 */
    r->ml_twips = r->mr_twips = 1134;
}

void ogt_ruler_layout(ogt_ruler *r, int x, int y, int w, int h, int page_x)
{
    r->x = x; r->y = y; r->w = w; r->h = h; r->page_x = page_x;
}

void ogt_ruler_set_page(ogt_ruler *r, int page_w_twips, int ml_twips, int mr_twips, int num, int den, int unit)
{
    r->page_w_twips = page_w_twips;
    r->ml_twips = ml_twips;
    r->mr_twips = mr_twips;
    r->num = num > 0 ? num : 1;
    r->den = den > 0 ? den : 1;
    r->unit = unit;
}

void ogt_ruler_set_indents(ogt_ruler *r, int left, int first, int right)
{
    r->left_twips = left; r->first_twips = first; r->right_twips = right;
}

static int px(const ogt_ruler *r, int twips) { return r->page_x + (int)((long)twips * r->num / r->den); }
static int twips_of(const ogt_ruler *r, int x) { return (int)((long)(x - r->page_x) * r->den / r->num); }

/* The three markers' x: a triangle each; the left and the first-line share the left indent's column. */
static void marks(const ogt_ruler *r, int *xl, int *xf, int *xr)
{
    *xl = px(r, r->ml_twips + r->left_twips);
    *xf = px(r, r->ml_twips + r->left_twips + r->first_twips);
    *xr = px(r, r->page_w_twips - r->mr_twips - r->right_twips);
}

static void triangle(struct RastPort *rp, LONG pen, int x, int y, int up)
{
    int xy[6] = { x - MARK / 2, up ? y + MARK : y, x + MARK / 2, up ? y + MARK : y, x, up ? y : y + MARK };
    ogt_fill_poly(rp, pen, xy, 3);
}

void ogt_ruler_draw(ogt_ruler *r)
{
    struct RastPort *rp = r->win->RPort;
    struct Region *clip, *old;
    struct Rectangle box;
    int unit_twips = r->unit == OGT_RULER_INCH ? 1440 : r->unit == OGT_RULER_CM ? 567 : 57;    /* inch, cm, mm */
    int label_every = r->unit == OGT_RULER_MM ? 10 : 1;
    int pl = px(r, 0), pr = px(r, r->page_w_twips), ml = px(r, r->ml_twips), mr = px(r, r->page_w_twips - r->mr_twips);
    int i, xl, xf, xr, ty = r->y + r->h - 1;
    LONG line = ogt_pen(r->ctx, "label"), dim = ogt_pen(r->ctx, "muted"), mark = ogt_pen(r->ctx, "accent");
    if (r->w <= 0 || r->h <= 0) return;
    box.MinX = r->x; box.MinY = r->y; box.MaxX = r->x + r->w - 1; box.MaxY = r->y + r->h - 1;
    if (!(clip = NewRegion())) return;
    OrRectRegion(clip, &box);
    old = InstallClipRegion(r->win->WLayer, clip);
    ogt_fill(r->ctx, rp, "panel", r->x, r->y, r->w, r->h);
    /* the page's width, the margins shaded */
    ogt_fill(r->ctx, rp, "string", pl, r->y + 2, pr - pl, r->h - 4);
    ogt_fill(r->ctx, rp, "list.alternate", pl, r->y + 2, ml - pl, r->h - 4);
    ogt_fill(r->ctx, rp, "list.alternate", mr, r->y + 2, pr - mr, r->h - 4);
    ogt_hline(rp, dim, pl, r->y + 1, pr - pl);
    ogt_hline(rp, dim, pl, ty - 1, pr - pl);
    /* the ticks from the left margin, both ways, every unit; a number at the labelled ones */
    SetFont(rp, r->win->WScreen->RastPort.Font);
    for (i = -40; i <= 80; i++) {
        int x = px(r, r->ml_twips + i * unit_twips), big = i % label_every == 0;
        if (x < pl || x > pr) continue;
        ogt_vline(rp, line, x, ty - (big ? 6 : 3), big ? 5 : 2);
        if (big && i) {
            char s[8];
            snprintf(s, sizeof s, "%d", i < 0 ? -i / label_every : i / label_every);
            ogt_text(rp, line, x - TextLength(rp, (STRPTR)s, strlen(s)) / 2, r->y + 3, s, 40);
        }
    }
    marks(r, &xl, &xf, &xr);
    triangle(rp, mark, xf, r->y + 1, 0);                /* the first line: a triangle pointing down from the top */
    triangle(rp, mark, xl, ty - MARK, 1);               /* the left indent: up from the bottom */
    triangle(rp, mark, xr, ty - MARK, 1);               /* the right indent */
    InstallClipRegion(r->win->WLayer, old);
    DisposeRegion(clip);
}

static int marker_at(const ogt_ruler *r, int mx, int my)
{
    int xl, xf, xr;
    if (mx < r->x || mx >= r->x + r->w || my < r->y || my >= r->y + r->h) return OGT_RULER_NONE;
    marks(r, &xl, &xf, &xr);
    if (my < r->y + r->h / 2) return mx >= xf - MARK && mx <= xf + MARK ? OGT_RULER_FIRST : OGT_RULER_NONE;
    if (mx >= xr - MARK && mx <= xr + MARK) return OGT_RULER_RIGHT;
    if (mx >= xl - MARK && mx <= xl + MARK) return OGT_RULER_LEFT;
    return OGT_RULER_NONE;
}

int ogt_ruler_event(ogt_ruler *r, ULONG class, UWORD code, int mx, int my)
{
    if (class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
        r->dragging = marker_at(r, mx, my);
        return OGT_RULER_NONE;
    }
    if (r->dragging && (class == IDCMP_MOUSEMOVE || (class == IDCMP_MOUSEBUTTONS && code == SELECTUP))) {
        int t = twips_of(r, mx), lim = r->page_w_twips - r->mr_twips;
        switch (r->dragging) {
        case OGT_RULER_LEFT:
            r->left_twips = t - r->ml_twips;
            if (r->left_twips < 0) r->left_twips = 0;
            if (r->ml_twips + r->left_twips > lim - 567) r->left_twips = lim - 567 - r->ml_twips;
            break;
        case OGT_RULER_FIRST:
            r->first_twips = t - r->ml_twips - r->left_twips;
            if (r->ml_twips + r->left_twips + r->first_twips < 0) r->first_twips = -r->ml_twips - r->left_twips;
            break;
        case OGT_RULER_RIGHT:
            r->right_twips = lim - t;
            if (r->right_twips < 0) r->right_twips = 0;
            if (lim - r->right_twips < r->ml_twips + r->left_twips + 567) r->right_twips = lim - r->ml_twips - r->left_twips - 567;
            break;
        }
        ogt_ruler_draw(r);
        if (class == IDCMP_MOUSEBUTTONS) { int was = r->dragging; r->dragging = 0; return was; }
    }
    return OGT_RULER_NONE;
}
