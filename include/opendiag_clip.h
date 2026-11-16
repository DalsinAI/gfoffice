/*
 * opendiag_clip.h: GFOpenDiag's clipboard (src/diag/odg_clip.c): Copy, Cut, Paste and Duplicate of shapes and
 * connectors on the editing core (opendiag_core.h). What is copied keeps its connectors: a connector between two
 * copied shapes comes along, and a copied connector whose end is joined to a shape left behind keeps that end
 * loose where it was. Cut, Paste and Duplicate are one Undo step each; Copy changes nothing.
 * Plain C99, no Amiga headers: tested on the host (tests/diag/test_odg_clip.c).
 * Design: amicradle-teams designs/GFOPENDIAG-20261113.md; docs/parity/diag.md, "Arrange and clipboard".
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENDIAG_CLIP_H
#define OPENDIAG_CLIP_H

#include "opendiag_core.h"

#define ODG_CLIP_ID 24                   /* bytes of an id Paste gives back */
#define ODG_CLIP_STEP OWF_DNUM(10)       /* how far each Paste of the same cells lands from the one before */

typedef struct {
    owf_dpage cells;                     /* the copied cells in drawing order; the top ones have no parent, and
                                          * their places are on the page they came from */
    int next;                            /* steps of ODG_CLIP_STEP the next odg_paste_next moves them: 1 after a
                                          * Copy, 0 after a Cut, one more after each */
} odg_clip;

/* An empty clipboard; and what it holds freed. */
void odg_clip_init(odg_clip *clip);
void odg_clip_free(odg_clip *clip);
/* 1 when there is nothing to paste. */
int odg_clip_empty(const odg_clip *clip);

/* The shapes and connectors named (with every cell inside them, and the connectors between them) copied onto the
 * clipboard in place of what it held. A root, a layer, an unknown id or a label on a connector not copied is
 * passed over. OWF_OK; OWF_ERR_FORMAT when nothing was left to copy (the clipboard is then as it was);
 * OWF_ERR_MEMORY. Not an Undo step: the page doesn't change. */
int odg_copy(const odg_doc *doc, const char *const *ids, int n, odg_clip *clip);
/* odg_copy, then the cells deleted as odg_delete does (connectors joined to them go too). One Undo step. */
int odg_cut(odg_doc *doc, const char *const *ids, int n, odg_clip *clip);
/* The clipboard's cells put on the current layer with new ids, moved by dx, dy from where they were copied. The
 * ids of the top cells (in drawing order) go into made[0..max-1] when made is not NULL, and how many there are
 * into *nmade: the app selects them. One Undo step. OWF_ERR_FORMAT when the clipboard is empty or the current
 * layer is hidden or locked; OWF_ERR_MEMORY. */
int odg_paste(odg_doc *doc, odg_clip *clip, owf_dnum dx, owf_dnum dy, char (*made)[ODG_CLIP_ID], int max,
              int *nmade);
/* Paste as the Edit menu does it: each Paste of the same copy lands ODG_CLIP_STEP further down and right than the
 * one before; the first is ODG_CLIP_STEP from the copied cells, or where they were after a Cut. */
int odg_paste_next(odg_doc *doc, odg_clip *clip, char (*made)[ODG_CLIP_ID], int max, int *nmade);
/* Copy and Paste at dx, dy as one Undo step, leaving the clipboard alone. */
int odg_duplicate(odg_doc *doc, const char *const *ids, int n, owf_dnum dx, owf_dnum dy, char (*made)[ODG_CLIP_ID],
                  int max, int *nmade);

#endif
