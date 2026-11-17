/*
 * opendiag_align.h: GFOpenDiag's Arrange menu, Align and Distribute (src/diag/odg_align.c), on the editing core
 * (opendiag_core.h). Each is one Undo step; orthogonal connectors joined to a shape that moves are routed again.
 * Plain C99, no Amiga headers: tested on the host (tests/diag/test_odg_align.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md; docs/parity/diag.md, "Arrange and clipboard".
 * MIT, Copyright (c) 2026 AmiCradle Team.
 *
 * Only shapes move (connectors follow them); a shape inside another shape named moves with that one. Places are
 * taken on the page, so shapes inside different groups line up where they are seen.
 */
#ifndef OPENDIAG_ALIGN_H
#define OPENDIAG_ALIGN_H

#include "opendiag_core.h"

typedef enum {
    ODG_ALIGN_LEFT,                      /* left sides on the leftmost one's */
    ODG_ALIGN_CENTRE,                    /* middles on the middle, across, of the box round them all */
    ODG_ALIGN_RIGHT,                     /* right sides on the rightmost one's */
    ODG_ALIGN_TOP,                       /* tops on the topmost one's */
    ODG_ALIGN_MIDDLE,                    /* middles on the middle, down, of the box round them all */
    ODG_ALIGN_BOTTOM                     /* bottoms on the lowest one's */
} odg_align_how;

/* The shapes named lined up. OWF_OK; OWF_ERR_FORMAT when fewer than two of them are shapes on the page, or none
 * would move (no Undo step is left); OWF_ERR_MEMORY. */
int odg_align(odg_doc *doc, const char *const *ids, int n, odg_align_how how);

/* The shapes named spread out evenly, across (across 1) or down: the two at the ends stay, and the ones between
 * have their middles at even steps between those two's middles, in the order their middles were. OWF_ERR_FORMAT
 * when fewer than three are shapes on the page, or none would move. */
int odg_distribute(odg_doc *doc, const char *const *ids, int n, int across);

/* On owfdiag.library (OWF_USE_DIAG_LIBRARY): its calls, once every header that declares them is complete. */
#if defined(OWF_USE_DIAG_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
#include "opendiag_proto.h"
#endif

#endif
