/*
 * opendiag_edit.h: GFOpenDiag's mouse (src/diag/odg_edit.c): the tool picked, the selection, and what a press, a
 * drag and a release on the canvas do to the page (select, lasso, move, resize by a handle, draw a shape, join two
 * shapes with a connector), through the editing core (opendiag_core.h) so each is one Undo step. The app gives it
 * the view's pixels and draws the box it gives back while the button is down. Plain C99, no Amiga headers: tested
 * on the host (tests/diag/test_odg_edit.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D4.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_EDIT_H
#define OPENDIAG_EDIT_H

#include "opendiag_view.h"

#define ODG_EDIT_MAX 64                  /* cells selected at most */
#define ODG_EDIT_ID 64                   /* an id's bytes; a cell with a longer id can't be picked with the mouse */
#define ODG_EDIT_SLOP 3                  /* pixels the mouse moves before a press is a drag */
#define ODG_EDIT_TOLERANCE 4             /* pixels from a connector's line that still hit it */

typedef enum {
    ODG_TOOL_SELECT, ODG_TOOL_BOX, ODG_TOOL_ROUNDED, ODG_TOOL_ELLIPSE, ODG_TOOL_RHOMBUS, ODG_TOOL_TEXT,
    ODG_TOOL_CONNECTOR, ODG_NTOOLS
} odg_tool;

typedef enum {
    ODG_DRAG_NONE, ODG_DRAG_MOVE, ODG_DRAG_RESIZE, ODG_DRAG_CREATE, ODG_DRAG_CONNECT, ODG_DRAG_LASSO
} odg_drag;

typedef struct {
    odg_tool tool;
    char ids[ODG_EDIT_MAX][ODG_EDIT_ID]; /* the selection, in the order picked */
    const char *list[ODG_EDIT_MAX];      /* ids[0..n-1], for odg_view.selected and the core's calls */
    int n;
    odg_drag drag;                       /* what the button held down is doing */
    odg_handle handle;                   /* ODG_DRAG_RESIZE: the handle, of the shape in from */
    char from[ODG_EDIT_ID];              /* the shape pressed on (ODG_DRAG_CONNECT: "" for a loose start) */
    long x0, y0, x1, y1;                 /* where the button went down and where the mouse is (view pixels) */
    int moved;                           /* the mouse went ODG_EDIT_SLOP pixels or more from x0, y0 */
    int add;                             /* Shift was held at the press: the selection grows */
    char made[ODG_EDIT_ID];              /* the shape or connector the last release made, "" when none */
} odg_edit;

/* The select tool and nothing selected. */
void odg_edit_init(odg_edit *e);
/* A tool's name ("Box") and the style its shapes get ("rounded=0;whiteSpace=wrap;html=1;"). */
const char *odg_tool_name(odg_tool t);
const char *odg_tool_style(odg_tool t);

/* The selection given to the view to draw. */
void odg_edit_show(odg_edit *e, odg_view *v);
/* Selection: is id in it; clear it; add id (add 1) or make it the only one; drop id; drop what isn't on the page
 * any more (after Undo, Delete, NEW or OPEN) or is hidden or locked now. odg_edit_select returns 0 when the id is too long or ODG_EDIT_MAX are
 * selected. */
int odg_edit_has(const odg_edit *e, const char *id);
void odg_edit_clear(odg_edit *e);
int odg_edit_select(odg_edit *e, const char *id, int add);
void odg_edit_drop(odg_edit *e, const char *id);
void odg_edit_prune(odg_edit *e, const odg_doc *doc);
/* Every shape and connector on the page selected; how many. */
int odg_edit_select_all(odg_edit *e, const odg_doc *doc);
/* 1 when id is a shape or connector the mouse may pick: on the page, shown, not locked (nor on a locked layer). */
int odg_edit_pickable(const odg_doc *doc, const char *id);

/* The topmost shape or connector at px, py into id (ODG_EDIT_ID bytes); 0 when none (or its id is too long).
 * shapes 1: shapes only. */
int odg_edit_hit(const odg_view *v, const odg_doc *doc, long px, long py, int shapes, char *id);

/* The button went down at px, py (shift: Shift held). The select tool picks there at once (a handle of a selected
 * shape, else the cell under it, else nothing for a lasso). Returns the drag started. */
odg_drag odg_edit_press(odg_edit *e, const odg_view *v, const odg_doc *doc, long px, long py, int shift);
/* The mouse is at px, py with the button down. */
void odg_edit_motion(odg_edit *e, long px, long py);
/* While the button is down: the box to draw as feedback (view pixels, corners in r->x, r->y and r->x + r->w,
 * r->y + r->h, so w and h may be below 0); 0 when nothing is to be drawn. For a connector it is the line's
 * ends. */
int odg_edit_feedback(const odg_edit *e, const odg_view *v, const odg_doc *doc, oldraw_rect *r);
/* The button went up at px, py: the drag done to the page. OWF_OK (nothing changed when it was only a click),
 * or the core's error; made holds what a shape or connector tool made, and it is then the selection. A shape
 * tool goes back to the select tool. */
int odg_edit_release(odg_edit *e, const odg_view *v, odg_doc *doc, long px, long py);
/* The button held down was let go of elsewhere (the window went inactive): nothing done. */
void odg_edit_cancel(odg_edit *e);

/* The selection deleted (OWF_ERR_FORMAT when nothing is selected), and moved by dx, dy page pixels (hundredths). */
int odg_edit_delete(odg_edit *e, odg_doc *doc);
int odg_edit_nudge(odg_edit *e, odg_doc *doc, owf_dnum dx, owf_dnum dy);

#endif
