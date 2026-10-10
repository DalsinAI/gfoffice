/* ogt_tabs: OpenGadTools' tabs, as MUI's Register draws them.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <string.h>

#include "ogt_tabs.h"

void ogt_tabs_set(ogt_tabs *t, const char *const *labels, int n, int open)
{
    int i;
    if (n > OGT_TABS_MAX) n = OGT_TABS_MAX;
    memset(t, 0, sizeof *t);
    t->n = n;
    for (i = 0; i < n; i++) t->label[i] = labels[i];
    t->open = open >= 0 && open < n ? open : 0;
}

void ogt_tabs_layout(ogt_tabs *t, struct RastPort *rp, int x, int y, int w, int lh)
{
    int i, at = x;
    t->x = x; t->y = y; t->w = w; t->h = lh;
    t->font = rp->Font;
    for (i = 0; i < t->n; i++) {
        t->tw[i] = TextLength(rp, (STRPTR)t->label[i], strlen(t->label[i])) + 2 * OGT_TABS_PAD;
        t->tx[i] = at;
        at += t->tw[i] - 1;                      /* neighbours share an edge, as MUI's do */
    }
}

struct Gadget *ogt_tabs_gadgets(ogt_tabs *t, struct Gadget *after, int first_gid)
{
    int i;
    t->first_gid = first_gid;
    for (i = 0; i < t->n; i++) {
        struct Gadget *g = &t->gad[i];
        memset(g, 0, sizeof *g);
        g->LeftEdge = (WORD)t->tx[i];
        g->TopEdge = (WORD)(t->y - OGT_TABS_LIFT);
        g->Width = (WORD)t->tw[i];
        g->Height = (WORD)(t->h + OGT_TABS_LIFT);
        g->Flags = GFLG_GADGHNONE;
        g->Activation = GACT_RELVERIFY;
        g->GadgetType = GTYP_BOOLGADGET;
        g->GadgetID = (UWORD)(first_gid + i);
        if (after) after->NextGadget = g;
        after = g;
    }
    return after;
}

int ogt_tabs_index(const ogt_tabs *t, int gadget_id)
{
    int i = gadget_id - t->first_gid;
    return i >= 0 && i < t->n ? i : -1;
}

void ogt_tabs_draw(ogt_tabs *t, struct Window *win, int page_bottom)
{
    struct RastPort *rp;
    struct DrawInfo *dri;
    struct TextFont *was;
    UWORD shine, shadow, text;
    int i, base, left, right, bottom;
    UBYTE apen, drmd;
    if (!win || !t->n || !(dri = GetScreenDrawInfo(win->WScreen))) return;
    rp = win->RPort;
    shine = dri->dri_Pens[SHINEPEN]; shadow = dri->dri_Pens[SHADOWPEN]; text = dri->dri_Pens[TEXTPEN];
    apen = rp->FgPen; drmd = rp->DrawMode;
    was = rp->Font;
    if (t->font) SetFont(rp, t->font);
    base = t->y + t->h;                          /* the line the tabs stand on */
    left = t->x - 4;                             /* the frame stands clear of the page's gadgets */
    right = t->x + t->w + 3;
    bottom = page_bottom > base + 4 ? page_bottom : win->Height - win->BorderBottom - 4;
    if (right > win->Width - win->BorderRight - 4) right = win->Width - win->BorderRight - 4;
    SetDrMd(rp, JAM1);
    /* the closed tabs first, then the open one over its neighbours' shared edges */
    for (i = 0; i <= t->n; i++) {
        int k = i < t->n ? i : t->open, open, x0, x1, y0, ty;
        const char *name;
        if (i < t->n && i == t->open) continue;
        open = k == t->open;
        name = t->label[k];
        x0 = t->tx[k]; x1 = x0 + t->tw[k] - 1; y0 = open ? t->y - OGT_TABS_LIFT : t->y;
        if (open) EraseRect(rp, x0 + 1, y0 + 1, x1 - 1, base);     /* in the window's own colour: no grey box */
        SetAPen(rp, shine);                                        /* left and top, the corners cut */
        Move(rp, x0, base - (open ? 0 : 1)); Draw(rp, x0, y0 + 2); Draw(rp, x0 + 2, y0); Draw(rp, x1 - 2, y0);
        SetAPen(rp, shadow);                                       /* right */
        Move(rp, x1 - 1, y0 + 1); Draw(rp, x1, y0 + 2); Draw(rp, x1, base - (open ? 0 : 1));
        SetAPen(rp, text);
        ty = y0 + (t->h + (open ? OGT_TABS_LIFT : 0) - rp->TxHeight) / 2 + rp->TxBaseline + (open ? 1 : 0);
        Move(rp, x0 + OGT_TABS_PAD, ty);
        Text(rp, (STRPTR)name, strlen(name));
    }
    /* the line under the tabs, but not under the open one; then the page's frame */
    SetAPen(rp, shine);
    Move(rp, left, base); Draw(rp, t->tx[t->open], base);
    Move(rp, t->tx[t->open] + t->tw[t->open] - 1, base); Draw(rp, right, base);
    Move(rp, left, base); Draw(rp, left, bottom);
    SetAPen(rp, shadow);
    Move(rp, left + 1, bottom); Draw(rp, right, bottom); Draw(rp, right, base + 1);
    SetAPen(rp, apen); SetDrMd(rp, drmd);
    SetFont(rp, was);
    FreeScreenDrawInfo(win->WScreen, dri);
}

void ogt_tabs_open(ogt_tabs *t, struct Window *win, int page_bottom, int open)
{
    if (open < 0 || open >= t->n) return;
    if (win) EraseRect(win->RPort, t->x - 4, t->y - OGT_TABS_LIFT, t->x + t->w + 3, t->y + t->h + 1);
    t->open = open;
    ogt_tabs_draw(t, win, page_bottom);
}
