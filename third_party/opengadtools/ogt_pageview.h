/* ogt_pageview: OpenGadTools' scrolled view of a tall thing (DESIGN.md section
 * 1): a document's pages one under another, a long picture, a playlist. The
 * view owns the scroller (propgclass, in the theme), the mouse wheel and the
 * arithmetic; the program draws what is in the box it is given, in content
 * coordinates, and is told where a click landed.
 *
 * Content is `total` pixels tall and `cw` wide (narrower content is centred).
 * Pages: ogt_pageview_set_pages() lays n pages of ph each with a gap between,
 * and the view then answers which page a y is on and scrolls page by page
 * (ogt_pageview_show_page). Nothing stops a program using it without pages.
 *
 * The window needs IDCMP_MOUSEBUTTONS, IDCMP_GADGETUP, IDCMP_GADGETDOWN,
 * IDCMP_MOUSEMOVE, IDCMP_RAWKEY (the wheel) and IDCMP_IDCMPUPDATE; pass its
 * messages to ogt_pageview_event().
 *
 * First user: OpenWrite 2's page canvas (the user, 10 October 2026).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_PAGEVIEW_H
#define OGT_PAGEVIEW_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

/* Draws the content that shows in the box x,y,w,h of the window; cx, cy is the content
 * coordinate of the box's top left. The view has already clipped to the box and filled the
 * background where no content is. */
typedef void (*ogt_pageview_draw_fn)(void *user, struct RastPort *rp, int x, int y, int w, int h, int cx, int cy);

enum { OGT_PV_NONE = 0, OGT_PV_SCROLLED = 1, OGT_PV_PRESS = 2, OGT_PV_DRAG = 3, OGT_PV_RELEASE = 4 };

typedef struct ogt_pageview {
    struct Window *win;
    ogt_ctx *ctx;
    int gid;
    int x, y, w, h;             /* the whole view, scroller included */
    int sw;                     /* the scroller's width */
    int cw, total;              /* the content's width and height */
    int top, left;              /* content pixels scrolled */
    int pages, ph, gap, margin; /* pages laid one under another (0: none) */
    int scale;                  /* the scroller counts total >> scale */
    int step;                   /* a wheel click, in pixels */
    ogt_pageview_draw_fn draw;
    void *user;
    const char *bg_key;         /* the theme key behind the content */
    struct Gadget *prop;
    int dragging;
} ogt_pageview;

ogt_pageview *ogt_pageview_new(struct Window *win, ogt_ctx *ctx, int gid, ogt_pageview_draw_fn draw, void *user, const char *bg_key);
void ogt_pageview_free(ogt_pageview *v);
/* Places the view (and its scroller) in the window. */
void ogt_pageview_layout(ogt_pageview *v, int x, int y, int w, int h);
/* The content's size. */
void ogt_pageview_set_content(ogt_pageview *v, int cw, int total);
/* n pages of pw x ph, `gap` between and `margin` above the first and below the last: sets the content size too. */
void ogt_pageview_set_pages(ogt_pageview *v, int n, int pw, int ph, int gap, int margin);
/* A page's top in content coordinates; which page holds content y (-1 between pages / none). */
int ogt_pageview_page_top(const ogt_pageview *v, int page);
int ogt_pageview_page_at(const ogt_pageview *v, int cy);
/* The page at the top of the view. */
int ogt_pageview_page(const ogt_pageview *v);
/* Scrolls so content y is at the top; so a page's top is at the top. */
void ogt_pageview_scroll_to(ogt_pageview *v, int top);
void ogt_pageview_show_page(ogt_pageview *v, int page);
/* Scrolls by dy pixels (the wheel, the keys). */
void ogt_pageview_scroll_by(ogt_pageview *v, int dy);
/* Draws everything (the content through the callback). */
void ogt_pageview_draw(ogt_pageview *v);
/* The box the content shows in. */
void ogt_pageview_box(const ogt_pageview *v, int *x, int *y, int *w, int *h);
/* Window x,y -> content x,y (1 when inside the content box). */
int ogt_pageview_to_content(const ogt_pageview *v, int mx, int my, int *cx, int *cy);
/* One IntuiMessage's worth of work. Returns OGT_PV_*; a press, drag or release gives *cx, *cy. */
int ogt_pageview_event(ogt_pageview *v, ULONG class, UWORD code, APTR iaddress, int mx, int my, int *cx, int *cy);

#endif
