/*
 * opendiag_core.h: GFOpenDiag's editing core (src/diag): one page of an owf_diagram edited by operations,
 * each one Undo step; ports and glue; orthogonal routing; snapping to the grid; hit-testing.
 * Plain C99 with integer arithmetic only, no Amiga headers: built and tested on the host.
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D3.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 *
 * Places and sizes are owf_dnum (hundredths of a pixel) on the page. An operation that fails changes
 * nothing and leaves no Undo step.
 */
#ifndef OPENDIAG_CORE_H
#define OPENDIAG_CORE_H

#include "owf_diagram.h"

#define ODG_UNDO_MAX 100                 /* Undo steps kept; the oldest go first */

typedef struct {
    owf_diagram *d;                      /* the program's; the core edits d->pages[page] */
    int page;
    owf_dnum grid;                       /* snap step; 0: no snapping */
    owf_dpage *undo, *redo;              /* the page as it was before each step, oldest first */
    int nundo, nredo;
    int changed;                         /* edited since odg_saved */
    int layer;                           /* the layer new shapes and connectors go on (odg_layer_at); 0 the first */
} odg_doc;

/* Sets up editing page of d (which must exist), with the page's own gridSize when it has grid="1". */
void odg_init(odg_doc *doc, owf_diagram *d, int page);
void odg_free(odg_doc *doc);
/* The page being edited. */
owf_dpage *odg_page(const odg_doc *doc);
/* The file was saved: changed goes back to 0 (Undo steps stay). */
void odg_saved(odg_doc *doc);

/* ---- Undo ---- */
int odg_can_undo(const odg_doc *doc);
int odg_can_redo(const odg_doc *doc);
/* OWF_OK; OWF_ERR_FORMAT when there is nothing to undo (or redo); OWF_ERR_MEMORY. */
int odg_undo(odg_doc *doc);
int odg_redo(odg_doc *doc);

/* ---- Snapping ---- */
/* v to the nearest multiple of the grid (as it is when the grid is 0). */
owf_dnum odg_snap(const odg_doc *doc, owf_dnum v);

/* ---- Operations (each one Undo step) ----
 * Ids come back in id (at least 24 bytes) when it is not NULL. */

/* A shape on the default layer at x, y (snapped), w by h. OWF_OK or OWF_ERR_MEMORY. */
int odg_add_shape(odg_doc *doc, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h, const char *style,
                  const char *value, char *id);
/* A connector from source to target (shapes' ids; NULL leaves an end loose at the point given). An
 * orthogonal style ("edgeStyle=orthogonalEdgeStyle") is routed at once. OWF_ERR_FORMAT when an id is
 * not a shape on the page. */
int odg_add_edge(odg_doc *doc, const char *source, const char *target, const owf_dpoint *loose_source,
                 const owf_dpoint *loose_target, const char *style, char *id);
/* Moves the shapes (and loose connectors) by dx, dy, the first one's corner snapped. A connector with
 * both ends moved takes its bends along; orthogonal connectors joined to a moved shape are routed
 * again. OWF_ERR_FORMAT when no id is a shape or connector on the page. */
int odg_move(odg_doc *doc, const char *const *ids, int n, owf_dnum dx, owf_dnum dy);
/* A shape's new box (snapped); its glued connectors follow. OWF_ERR_FORMAT when it is not a shape, or
 * the size is not above 0. */
int odg_resize(odg_doc *doc, const char *id, owf_dnum x, owf_dnum y, owf_dnum w, owf_dnum h);
/* Deletes the cells with what is inside them and the connectors joined to them. OWF_ERR_FORMAT when
 * none is on the page (or one is the root or a layer). */
int odg_delete(odg_doc *doc, const char *const *ids, int n);
/* A cell's label (NULL clears it). OWF_ERR_FORMAT when there is no such cell. */
int odg_set_value(odg_doc *doc, const char *id, const char *value);
/* One style key of each cell (value NULL removes it). */
int odg_set_style(odg_doc *doc, const char *const *ids, int n, const char *key, const char *value);
/* nkeys style keys of each cell (a NULL value removes its key), all as one Undo step. */
int odg_set_styles(odg_doc *doc, const char *const *ids, int n, const char *const *keys, const char *const *values,
                   int nkeys);

/* ---- Ports and glue ---- */
typedef enum { ODG_PORT_NONE = -1, ODG_PORT_N, ODG_PORT_E, ODG_PORT_S, ODG_PORT_W } odg_port;
/* The middle of a box's side. */
owf_dpoint odg_port_point(const owf_dnum box[4], odg_port port);
/* Joins a connector's end (0 its source, 1 its target) to a shape at a port (ODG_PORT_NONE: wherever
 * the line meets it), or (shape NULL) leaves it loose at the point given. One Undo step. */
int odg_connect(odg_doc *doc, const char *edge, int end, const char *shape, odg_port port,
                const owf_dpoint *loose);

/* ---- Routing ---- */
/* Orthogonal bends for a connector from its ends, as the ends' sides face each other: one turn when the
 * ends leave by sides at right angles, else two at the middle. Not an Undo step of its own (the
 * operations call it); OWF_OK, OWF_ERR_FORMAT when it has no two ends, or OWF_ERR_MEMORY. */
int odg_route(odg_doc *doc, const char *edge);

/* ---- Layers ---- */
/* The page's layers (the cells whose parent is the root), bottom first. A layer's name is its value; it is hidden
 * with visible="0" and locked with style locked=1, as draw.io writes them. New shapes and connectors go on layer
 * doc->layer (the first when it is out of range). */
int odg_layer_count(const odg_doc *doc);
const owf_dcell *odg_layer_at(const odg_doc *doc, int i);
int odg_layer_shown(const odg_doc *doc, int i);
int odg_layer_locked(const odg_doc *doc, int i);
/* A new layer on top named name (NULL: "Layer n"), made the current one; its number into *at. One Undo step. */
int odg_layer_add(odg_doc *doc, const char *name, int *at);
/* Layer i shown or hidden, locked or not, renamed: one Undo step each. OWF_ERR_FORMAT when there is no layer i. */
int odg_layer_show(odg_doc *doc, int i, int shown);
int odg_layer_lock(odg_doc *doc, int i, int locked);
int odg_layer_rename(odg_doc *doc, int i, const char *name);
/* 1 when the cell is on a locked layer or locked itself: the mouse leaves it alone. */
int odg_cell_locked(const odg_doc *doc, const owf_dcell *c);

/* ---- Hit-testing ---- */
/* The topmost shape or connector at x, y (a connector within tol of its line), into id; 0 when none. */
int odg_hit(const odg_doc *doc, owf_dnum x, owf_dnum y, owf_dnum tol, char *id, size_t size);

#endif
