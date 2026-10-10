/* ogt_ruler: OpenGadTools' horizontal ruler (DESIGN.md section 1), drawn in
 * the theme: ticks in the unit chosen along a page's width, the margins
 * shaded, and markers for the left indent, the first-line indent and the
 * right indent that the mouse can drag. The program gives the page's
 * geometry in twips (1/1440 inch) and the scale it is shown at; the ruler
 * does the rest and says when a marker moved.
 *
 * The window needs IDCMP_MOUSEBUTTONS and IDCMP_MOUSEMOVE; pass its
 * messages to ogt_ruler_event().
 *
 * First user: OpenWrite 2 (the user, 10 October 2026).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_RULER_H
#define OGT_RULER_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

enum { OGT_RULER_MM = 0, OGT_RULER_CM = 1, OGT_RULER_INCH = 2 };
enum { OGT_RULER_NONE = 0, OGT_RULER_LEFT = 1, OGT_RULER_FIRST = 2, OGT_RULER_RIGHT = 3 };

typedef struct ogt_ruler {
    struct Window *win;
    ogt_ctx *ctx;
    int x, y, w, h;             /* the strip */
    int page_x;                 /* where the page's left edge is, in window pixels */
    int page_w_twips, ml_twips, mr_twips;      /* the page's width and margins */
    int left_twips, first_twips, right_twips;  /* the paragraph's indents (first is relative to left) */
    int num, den;               /* pixels = twips * num / den */
    int unit;
    int dragging;               /* OGT_RULER_LEFT/FIRST/RIGHT while a marker is held */
} ogt_ruler;

void ogt_ruler_init(ogt_ruler *r, struct Window *win, ogt_ctx *ctx);
/* Places the strip; the page's left edge sits at page_x. */
void ogt_ruler_layout(ogt_ruler *r, int x, int y, int w, int h, int page_x);
/* The page and the scale (num/den pixels a twip), the unit. */
void ogt_ruler_set_page(ogt_ruler *r, int page_w_twips, int ml_twips, int mr_twips, int num, int den, int unit);
/* The paragraph's indents (twips; first relative to left; right from the right margin). */
void ogt_ruler_set_indents(ogt_ruler *r, int left, int first, int right);
void ogt_ruler_draw(ogt_ruler *r);
/* One IntuiMessage's worth of work. Returns the marker that moved (OGT_RULER_*) on release, else OGT_RULER_NONE. */
int ogt_ruler_event(ogt_ruler *r, ULONG class, UWORD code, int mx, int my);

#endif
