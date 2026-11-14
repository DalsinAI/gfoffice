/*
 * opendiag_view.h: GFOpenDiag's canvas (src/diag/odg_view.c): the page being edited drawn at a zoom
 * and scroll with GFOpenLayout's drawing calls (gfopenlayout_draw.h), the grid, the selection and its
 * handles, and from the view's pixels back to the page. No graphics.library: what GFOpenLayout draws is
 * what the window shows, so the host tests see every pixel the Amiga does.
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D4.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_VIEW_H
#define OPENDIAG_VIEW_H

#include "opendiag_core.h"
#include "gfopenlayout_draw.h"

#define ODG_ZOOM_MIN 10                  /* percent */
#define ODG_ZOOM_MAX 800
#define ODG_HANDLE 7                     /* a handle's side in pixels */

typedef struct {
    int x, y;                            /* the view's top left corner on the target (a window's canvas) */
    int width, height;                   /* the view's size in pixels */
    int zoom;                            /* percent: 100 draws a page pixel as one pixel */
    owf_dnum left, top;                  /* the page point at the view's top left corner */
    int grid;                            /* draw the grid's dots (when they are 6 pixels apart or more) */
    const char *const *selected;         /* the selection's ids (the program's), drawn with handles */
    int nselected;
    oldraw_colour paper, grid_colour, select_colour;
    const char *font;                    /* the labels' font family; NULL: each label's own fontFamily */
} odg_view;

/* A view width by height at 100% with the page's top left corner at its top left. */
void odg_view_init(odg_view *v, int width, int height);
/* A page place in the target's pixels (the view's x, y added), and back (rounded to the nearest hundredth). */
long odg_view_x(const odg_view *v, owf_dnum x);
long odg_view_y(const odg_view *v, owf_dnum y);
owf_dnum odg_page_x(const odg_view *v, long px);
owf_dnum odg_page_y(const odg_view *v, long py);
/* A page length (hundredths) in pixels at the view's zoom. */
long odg_view_len(const odg_view *v, owf_dnum n);
/* A box on the page (x, y, w, h hundredths) in the view's pixels. */
void odg_view_rect(const odg_view *v, const owf_dnum box[4], oldraw_rect *r);
/* A new zoom (kept within ODG_ZOOM_MIN..MAX) with the page point under px, py staying there. */
void odg_view_zoom_at(odg_view *v, int zoom, long px, long py);
/* The zoom and scroll that show the whole drawing with a margin (100% at most); nothing drawn: the top
 * left corner at 100%. */
void odg_view_fit(odg_view *v, const odg_doc *doc);

/* A colour from a style key: "#rrggbb" or "#rgb" as it is, "none" as OLDRAW_NONE, missing or
 * "default" as def. */
oldraw_colour odg_style_colour(const char *style, const char *key, oldraw_colour def);

/* Draws what of the view crosses area (the view's own pixels) onto d: the paper, the grid, the cells in
 * page order (edges with their arrows, shapes, labels), and the selection with its handles on top. */
void odg_draw(oldraw *d, const odg_view *v, const odg_doc *doc, const oldraw_rect *area);

/* A selected shape's resize handles: corners and sides, clockwise from the top left. */
typedef enum {
    ODG_HANDLE_NONE = -1, ODG_HANDLE_NW, ODG_HANDLE_N, ODG_HANDLE_NE, ODG_HANDLE_E, ODG_HANDLE_SE,
    ODG_HANDLE_S, ODG_HANDLE_SW, ODG_HANDLE_W
} odg_handle;
/* Handle h of a box drawn at r: its square in the view. */
void odg_handle_rect(const oldraw_rect *r, odg_handle h, oldraw_rect *out);
/* The handle of a selected shape under px, py, its id into id (size bytes); ODG_HANDLE_NONE when none. */
odg_handle odg_view_handle(const odg_view *v, const odg_doc *doc, long px, long py, char *id, size_t size);
/* The box a handle dragged by dx, dy pixels gives a shape whose box was box (hundredths, snapped to the
 * doc's grid, at least one grid step or a pixel each way). For odg_resize. */
void odg_handle_drag(const odg_view *v, const odg_doc *doc, const owf_dnum box[4], odg_handle h, long dx, long dy,
                     owf_dnum out[4]);

#endif
