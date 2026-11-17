/*
 * libowf: the print core, one copy for every GFOffice program (docs/suite/owf-shared-core.md): the room a
 * printer's bleed and crop marks take round a page, the crop marks themselves, a page's background as it
 * runs into the bleed, and the order of a folded booklet's pages. It draws nothing itself: the caller
 * puts the lines and the colour on its page (a PDF's, a screen's or a printer's). GFOpenPressmaster's printing
 * (owfpub.library 3.10 exports it) and GFOpenPresentation's PDF (src/deck/opr_pdf.c, through its old
 * opr_ names) both draw on it. Lengths are twips. Integer only, no Amiga headers, no GFOpenLayout.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_PRINT_H
#define OWF_PRINT_H

#include "owf_deck.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OWF_PRINT_MAX_BLEED 1440   /* an inch */
#define OWF_PRINT_MARK_LENGTH 360  /* a crop mark */
#define OWF_PRINT_MARK_GAP 120     /* the least room between a mark and the trimmed edge */
#define OWF_PRINT_MARK_ROOM 60     /* past a mark's outer end, to the paper's edge */
#define OWF_PRINT_CROP_LINES 8     /* two a corner */

/* The bleed asked for, at most OWF_PRINT_MAX_BLEED; 0 for none (0 or less). */
int owf_print_bleed(int bleed);

/* The gap between the trimmed edge and a crop mark: past the bleed, and at least OWF_PRINT_MARK_GAP. */
int owf_print_mark_gap(int bleed);

/* The room round each side of a page for a bleed and crop marks (0: neither). */
int owf_print_margin(int bleed, int crop_marks);

/* The crop marks of the trimmed page at (m, m), w by h, gap away from its edges: OWF_PRINT_CROP_LINES
 * lines into xy as x1, y1, x2, y2 each (4 * OWF_PRINT_CROP_LINES ints). Each corner gets one line across,
 * level with the top or bottom edge, and one down, in line with the left or right edge, both outside the
 * page. The line count, or -1 when xy is NULL. */
int owf_print_crop_lines(int m, int w, int h, int gap, int *xy);

/* The colour a page's background runs into the bleed with: the page's own, else its master's (as the
 * page is drawn), or 0 for white, none, or no such page; then the bleed is left white. */
unsigned long owf_print_bleed_colour(const owf_deck *deck, int page);

/* A booklet of pages pages, printed two to a side and folded: the sides it takes (each sheet has two,
 * pages padded to a multiple of four with blanks), 0 for none. */
int owf_print_booklet_sides(int pages);

/* Side k (0..sides-1) of that booklet: the pages on its left and right (0-based), -1 for a blank. Even
 * sides are a sheet's front (the last page and the first, then inwards), odd sides its back. 0, or -1
 * when pages or side is out of range. */
int owf_print_booklet_side(int pages, int side, int *left, int *right);

#ifdef __cplusplus
}
#endif

#endif
