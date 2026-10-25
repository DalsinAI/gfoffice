/*
 * OpenCalc's core: what a chart is drawn with. Lines, filled polygons and
 * one line of text at a time, in the grid's pixels, and how wide a text
 * is: nothing else, so that another drawer (Cairo's, say) can stand in
 * for the core's own without the charts changing. The core's own
 * (oc_gfx.c) makes the draw list's items, cut to a pane, for the window
 * to paint.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OC_GFX_H
#define OC_GFX_H

#include "oc_internal.h"

typedef struct oc_gfx oc_gfx;
struct oc_gfx {
    /* a line from x0,y0 to x1,y1, width pixels wide (at least 1) */
    void (*line)(oc_gfx *g, int x0, int y0, int x1, int y1, int width, unsigned long rgb);
    /* a polygon filled (even-odd), n points as x, y pairs; its right and bottom edges are outside it, as a
     * box's are: the box x,y,w,h is x,y x+w,y x+w,y+h x,y+h */
    void (*polygon)(oc_gfx *g, const int *xy, int n, unsigned long rgb);
    /* one line of text in the box x,y,w,h, aligned in it (OC_ALIGN_...) and centred up and down; twips its
     * size, flags OWF_BOLD... */
    void (*text)(oc_gfx *g, const char *s, int x, int y, int w, int h, int align, int twips, unsigned flags,
                 unsigned long rgb);
    int (*text_width)(oc_gfx *g, const char *s, int twips, unsigned flags);
    int failed;                  /* memory ran out: what followed was not drawn */
};

/* The core's: draw items (OC_ROLE_CHART fills, segments and polygons, OC_ROLE_TEXT text) added to list,
 * cut to the clip box cx,cy,cw,ch. Text shows only when its line is wholly in the clip box. */
typedef struct {
    oc_gfx g;
    oc_core *core;
    oc_draw_list *list;
    int cx, cy, cw, ch;
    owf_cellstyle style;         /* for measuring: the default font at a size */
} oc_gfx_items;
void oc_gfx_items_init(oc_gfx_items *gi, oc_core *core, oc_draw_list *list, int cx, int cy, int cw, int ch);

/* A box (x,y,w,h) as a polygon. */
void oc_gfx_box(oc_gfx *g, int x, int y, int w, int h, unsigned long rgb);

/* A chart drawn with g in the box x,y,w,h (oc_chart.c; oc_chart_draw with the core's drawer). 0 when memory
 * ran out. */
int oc_chart_render(oc_core *core, oc_gfx *g, const owf_chart *chart, int x, int y, int w, int h);

#endif
