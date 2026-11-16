/*
 * GFOpenPressmaster's Insert > Page Number (src/pub/opp_pagenum.c): a small
 * centred text box near the foot of a page, or of a master in the Master
 * view, holding a page number field (or the number of pages). On a master
 * every page of that master shows its own number. MIT, Copyright (c) 2026
 * AmiCradle Team.
 */
#ifndef OPENPRESSMASTER_PAGENUM_H
#define OPENPRESSMASTER_PAGENUM_H

#include "openpresentation_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A text box holding which (OWF_FIELD_PAGE or OWF_FIELD_PAGES), centred
 * near the foot of slide (a page, or a master in the Master view's deck),
 * as one Undo step; *selected (when not NULL) is the new object. 0 done,
 * -2 the page is too small for it, -1 bad input or no memory. */
int opp_page_number_box(opr_undo *u, owf_deck *deck, int slide, owf_field which, int *selected);

#ifdef __cplusplus
}
#endif

#endif
