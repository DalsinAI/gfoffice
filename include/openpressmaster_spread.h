/*
 * GFOpenPressmaster: facing pages, two to a spread (spreads step 2). Page 1
 * is a right page on its own, as a book's first page is; after it the pages
 * pair left and right (2 and 3, 4 and 5); an even count's last page is a left
 * page on its own. The window shows a spread's two pages side by side, their
 * inner edges touching at the spine. Plain C, integers only; the window
 * (app/openpresent.c) draws and clicks by these.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPRESSMASTER_SPREAD_H
#define OPENPRESSMASTER_SPREAD_H

#include "owf_deck.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { OPP_SPREAD_LEFT, OPP_SPREAD_RIGHT };

typedef struct { int x, y, w, h; } opp_spread_box;

/* 1 when the publication's pages face each other: it has a two-page master, which is two masters of one
 * name (its left and its right page, as opp_pub_to_deck makes them from a .pub). 0 for NULL. */
int opp_spread_facing(const owf_deck *deck);
/* Page's side (pages count from 0): OPP_SPREAD_RIGHT for page 0 and every even one, OPP_SPREAD_LEFT for odd. */
int opp_spread_side(int page);
/* The page that shares page's spread, of npages; -1 when it is on its own (page 0, an even count's last
 * page) or out of range. */
int opp_spread_partner(int npages, int page);
/* The spread's first page (its left one, or the page itself when on its own); -1 out of range. Stepping
 * from spread to spread: opp_spread_first(n, page) - 1 is in the spread before, and the page after a
 * spread's last is in the next. */
int opp_spread_first(int npages, int page);
/* Two pages of page_width by page_height (any unit) as large as fit side by side in a view of view_width by
 * view_height pixels, centred, touching at the spine. *left and *right are the two pages' boxes, from the
 * view's top left. The pages' width in pixels; 0 (boxes zeroed) when a page would be under 8 pixels either
 * way or an argument is not positive. */
int opp_spread_place(int view_width, int view_height, int page_width, int page_height, opp_spread_box *left,
                     opp_spread_box *right);
/* Which box (x, y) is in: OPP_SPREAD_LEFT, OPP_SPREAD_RIGHT, or -1 for neither. */
int opp_spread_hit(const opp_spread_box *left, const opp_spread_box *right, int x, int y);

#ifdef __cplusplus
}
#endif
#endif
