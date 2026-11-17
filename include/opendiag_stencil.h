/*
 * opendiag_stencil.h: GFOpenDiag's shapes palette (src/diag/odg_stencil.c): the Flowchart and Basic shapes the
 * Shapes menu draws, each a draw.io style and the size a click gives, so a file made here opens in diagrams.net as
 * the same shapes (libowf/diagram/owf_dshape.c draws their outlines in our own style).
 * Plain C99: tested on the host (tests/diag/test_odg_stencil.c, tests/diag/test_odg_edit.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D4 (the stencil panel; version 1 is plain labelled
 * shapes, the Amiga machine drawings come later).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_STENCIL_H
#define OPENDIAG_STENCIL_H

typedef enum { ODG_SET_FLOWCHART, ODG_SET_BASIC, ODG_NSETS } odg_stencil_set;

typedef struct {
    const char *name;                    /* "Decision": the menu's word, and ARexx's SHAPE name (any case) */
    const char *style;                   /* the cell's style */
    int width, height;                   /* page pixels, for a click */
    odg_stencil_set set;                 /* the set the palette shows it in */
} odg_stencil;

int odg_stencil_count(void);
/* Shape i, NULL when there is none; the shape named (any case, spaces as they are), -1 when none is. */
const odg_stencil *odg_stencil_at(int i);
int odg_stencil_find(const char *name);
/* A set's name ("Basic"), NULL when there is none; the number of the nth shape of a set (from 0), -1 past its last. */
const char *odg_stencil_set_name(odg_stencil_set set);
int odg_stencil_in_set(odg_stencil_set set, int nth);

/* On owfdiag.library (OWF_USE_DIAG_LIBRARY): its calls, once every header that declares them is complete. */
#if defined(OWF_USE_DIAG_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
#include "opendiag_proto.h"
#endif

#endif
