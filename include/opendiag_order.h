/*
 * opendiag_order.h: GFOpenDiag's Arrange menu, first part (src/diag/odg_order.c): the drawing order (Bring to
 * Front, Send to Back, Bring Forward, Send Backward) and Group and Ungroup, on the editing core
 * (opendiag_core.h). Each is one Undo step.
 * Plain C99, no Amiga headers: tested on the host (tests/diag/test_odg_order.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md; docs/parity/diag.md, "Arrange and clipboard".
 * MIT, Copyright (c) 2026 AmiCradle Team.
 *
 * The page's cells are in drawing order, and a cell is drawn over the cells before it that share its parent. A
 * cell moves in that order with every cell inside it. A group is a shape with style "group" (as draw.io writes
 * it) whose box is round the cells inside it; their places are measured from its corner.
 */
#ifndef OPENDIAG_ORDER_H
#define OPENDIAG_ORDER_H

#include "opendiag_core.h"

#define ODG_ORDER_ID 24                  /* bytes of an id Group and Ungroup give back */
typedef char odg_order_id[ODG_ORDER_ID];   /* one of them */

typedef enum {
    ODG_ORDER_FRONT,                     /* over every other cell with its parent */
    ODG_ORDER_BACK,                      /* under every one */
    ODG_ORDER_FORWARD,                   /* over the next one up */
    ODG_ORDER_BACKWARD                   /* under the next one down */
} odg_order;

/* The shapes and connectors named moved in the drawing order; those named keep their order among themselves. A
 * cell inside another named cell moves with that one. OWF_OK; OWF_ERR_FORMAT when none of them is a shape or
 * connector on the page, or none would move (already at the front, say: no Undo step is left); OWF_ERR_MEMORY. */
int odg_reorder(odg_doc *doc, const char *const *ids, int n, odg_order how);

/* The shapes and connectors named that share the first one's parent (others are passed over) put inside a new
 * group, which takes the place in the drawing order of the lowest of them. Its id into group (ODG_ORDER_ID bytes)
 * when that is not NULL. OWF_ERR_FORMAT when none can be grouped. */
int odg_group(odg_doc *doc, const char *const *ids, int n, char *group);
/* Each group named taken apart: what was inside it goes to the group's parent, where the group was in the drawing
 * order, and the group goes (a shape that holds others but isn't a group, a container, stays, emptied). A
 * connector joined to a group that goes keeps that end loose where it was. The ids
 * of the cells that came out go into made[0..max-1] when made is not NULL, how many there are into *nmade.
 * OWF_ERR_FORMAT when none of them holds anything. */
int odg_ungroup(odg_doc *doc, const char *const *ids, int n, odg_order_id *made, int max, int *nmade);
/* 1 when the cell is a group (style "group"). */
int odg_is_group(const owf_dcell *c);

/* On owfdiag.library (OWF_USE_DIAG_LIBRARY): its calls, once every header that declares them is complete. */
#define OPENDIAG_ORDER_H_DONE
#if defined(OWF_USE_DIAG_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
#include "opendiag_proto.h"
#endif

#endif
