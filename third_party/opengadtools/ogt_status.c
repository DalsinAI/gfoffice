/* ogt_status: OpenGadTools' status bar, drawn in the theme.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <string.h>

#include "ogt_status.h"

#define PAD 10

void ogt_status_init(ogt_status *s, struct Window *win, ogt_ctx *ctx, int n)
{
    memset(s, 0, sizeof *s);
    s->win = win;
    s->ctx = ctx;
    s->n = n > OGT_STATUS_MAX ? OGT_STATUS_MAX : (n > 0 ? n : 1);
}

void ogt_status_layout(ogt_status *s, int x, int y, int w, int h)
{
    s->x = x; s->y = y; s->w = w; s->h = h;
}

void ogt_status_draw(ogt_status *s)
{
    struct RastPort *rp = s->win->RPort;
    int i, x = s->x + PAD, ty = s->y + (s->h - rp->TxHeight) / 2;
    LONG text = ogt_pen(s->ctx, "label"), line = ogt_pen(s->ctx, "muted");
    if (s->w <= 0 || s->h <= 0) return;
    ogt_fill(s->ctx, rp, "panel", s->x, s->y, s->w, s->h);
    ogt_hline(rp, line, s->x, s->y, s->w);
    for (i = 0; i < s->n - 1; i++) {
        if (!s->text[i][0]) continue;
        x += ogt_text(rp, text, x, ty, s->text[i], s->x + s->w - x - PAD) + 2 * PAD;
        ogt_vline(rp, line, x - PAD, s->y + 4, s->h - 8);
    }
    if (s->text[s->n - 1][0]) {                     /* the last at the right */
        int w = ogt_text_width(rp, s->text[s->n - 1]), rx = s->x + s->w - PAD - w;
        if (rx < x) rx = x;
        ogt_text(rp, text, rx, ty, s->text[s->n - 1], s->x + s->w - PAD - rx);
    }
}

void ogt_status_set(ogt_status *s, int field, const char *text)
{
    if (field < 0 || field >= s->n) return;
    if (!strncmp(s->text[field], text ? text : "", OGT_STATUS_LEN - 1)) return;
    strncpy(s->text[field], text ? text : "", OGT_STATUS_LEN - 1);
    s->text[field][OGT_STATUS_LEN - 1] = 0;
    if (s->w > 0) ogt_status_draw(s);
}
