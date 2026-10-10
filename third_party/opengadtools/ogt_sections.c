/* ogt_sections: OpenGadTools' inspector sections, GadTools gadgets in a labelled column.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>

#include <string.h>

#include "ogt_sections.h"

#define TITLE_H(lh) ((lh) + 4)
#define SEC_GAP 10

void ogt_sections_init(ogt_sections *s, int x, int y, int w, int label_w, int lh, struct TextAttr *ta, APTR vi)
{
    memset(s, 0, sizeof *s);
    s->x = x; s->y = y; s->w = w;
    s->label_w = label_w > 0 ? label_w : w / 3;
    s->lh = lh;
    s->gap = 4;
    s->ta = ta;
    s->vi = vi;
}

int ogt_sections_add(ogt_sections *s, const char *title, const ogt_row *rows, int n)
{
    ogt_isection *sec;
    if (s->n >= OGT_SEC_MAX) return -1;
    sec = &s->sec[s->n];
    sec->title = title;
    sec->n = n > OGT_SEC_ROWS ? OGT_SEC_ROWS : n;
    memcpy(sec->row, rows, sec->n * sizeof *rows);
    return s->n++;
}

struct Gadget *ogt_sections_gadgets(ogt_sections *s, struct Gadget *g)
{
    struct NewGadget ng;
    int i, k, y = s->y, fx = s->x + s->label_w + 8, fw = s->w - s->label_w - 8;
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = s->ta;
    ng.ng_VisualInfo = s->vi;
    for (i = 0; i < s->n && g; i++) {
        ogt_isection *sec = &s->sec[i];
        sec->y = y;
        y += TITLE_H(s->lh);
        for (k = 0; k < sec->n && g; k++) {
            ogt_row *r = &sec->row[k];
            int w = r->width > 0 ? r->width : fw;
            ng.ng_LeftEdge = r->label ? fx : s->x;
            ng.ng_TopEdge = y;
            ng.ng_Width = r->label ? w : (r->width > 0 ? r->width : s->w);
            ng.ng_Height = s->lh;
            ng.ng_GadgetText = (STRPTR)r->label;
            ng.ng_GadgetID = (UWORD)r->id;
            ng.ng_Flags = r->label ? PLACETEXT_LEFT : 0;
            switch (r->kind) {
            case OGT_ROW_CYCLE:
                g = CreateGadget(CYCLE_KIND, g, &ng, GTCY_Labels, (ULONG)r->labels, GTCY_Active, (ULONG)r->value,
                                 GA_Disabled, r->disabled, TAG_DONE);
                break;
            case OGT_ROW_STRING:
                g = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)(r->text ? r->text : ""), GTST_MaxChars, 120,
                                 GA_Disabled, r->disabled, TAG_DONE);
                break;
            case OGT_ROW_INTEGER:
                g = CreateGadget(INTEGER_KIND, g, &ng, GTIN_Number, (ULONG)r->value, GTIN_MaxChars, 6,
                                 GA_Disabled, r->disabled, TAG_DONE);
                break;
            case OGT_ROW_CHECKBOX:
                ng.ng_Width = 26;
                g = CreateGadget(CHECKBOX_KIND, g, &ng, GTCB_Checked, r->value != 0, GTCB_Scaled, TRUE,
                                 GA_Disabled, r->disabled, TAG_DONE);
                break;
            case OGT_ROW_BUTTON:                     /* its text inside; a label is not drawn (GadTools draws one or the other) */
                ng.ng_GadgetText = (STRPTR)r->text;
                ng.ng_Flags = PLACETEXT_IN;
                g = CreateGadget(BUTTON_KIND, g, &ng, GT_Underscore, '_', GA_Disabled, r->disabled, TAG_DONE);
                break;
            case OGT_ROW_TEXT:
                g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)(r->text ? r->text : ""), GTTX_CopyText, TRUE, TAG_DONE);
                break;
            case OGT_ROW_SLIDER:
                g = CreateGadget(SLIDER_KIND, g, &ng, GTSL_Min, (ULONG)r->min, GTSL_Max, (ULONG)r->max, GTSL_Level, (ULONG)r->value,
                                 GTSL_LevelFormat, (ULONG)"%3ld", GTSL_MaxLevelLen, 4, GTSL_LevelPlace, PLACETEXT_RIGHT,
                                 GA_Disabled, r->disabled, TAG_DONE);
                break;
            default:
                g = NULL;
            }
            s->gad[i][k] = g;
            y += s->lh + s->gap;
        }
        sec->h = y - sec->y;
        y += SEC_GAP;
    }
    s->h = y - s->y;
    return g;
}

struct Gadget *ogt_sections_gadget(ogt_sections *s, int id)
{
    int i, k;
    for (i = 0; i < s->n; i++)
        for (k = 0; k < s->sec[i].n; k++)
            if (s->sec[i].row[k].id == id) return s->gad[i][k];
    return NULL;
}

void ogt_sections_draw(ogt_sections *s, struct Window *win, ogt_ctx *ctx)
{
    struct RastPort *rp = win->RPort;
    int i;
    LONG text = ogt_pen(ctx, "label"), line = ogt_pen(ctx, "muted");
    for (i = 0; i < s->n; i++) {
        ogt_isection *sec = &s->sec[i];
        int ty = sec->y + (TITLE_H(s->lh) - rp->TxHeight) / 2 - 1;
        ogt_bold(rp, 1);
        ogt_text(rp, text, s->x, ty, sec->title, s->w);
        ogt_bold(rp, 0);
        ogt_hline(rp, line, s->x, sec->y + TITLE_H(s->lh) - 3, s->w);
    }
}
