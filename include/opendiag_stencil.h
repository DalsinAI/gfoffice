/*
 * opendiag_stencil.h: GFOpenDiag's shapes palette (src/diag/odg_stencil.c): the flowchart shapes the Shapes menu
 * draws, each a draw.io style and the size a click gives, so a file made here opens in diagrams.net as the same
 * shapes. Plain C99: tested on the host (tests/diag/test_odg_edit.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md, step D4 (the stencil panel; version 1 is plain labelled
 * shapes, the Amiga machine drawings come later).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_STENCIL_H
#define OPENDIAG_STENCIL_H

typedef struct {
    const char *name;                    /* "Decision": the menu's word, and ARexx's SHAPE name (any case) */
    const char *style;                   /* the cell's style */
    int width, height;                   /* page pixels, for a click */
} odg_stencil;

int odg_stencil_count(void);
/* Shape i, NULL when there is none; the shape named (any case, spaces as they are), -1 when none is. */
const odg_stencil *odg_stencil_at(int i);
int odg_stencil_find(const char *name);

#endif
