/* ogt_tabs: OpenGadTools' tabs (DESIGN.md section 1, "Tabs and pages"), drawn
 * as MUI's Register class draws them: each tab a raised shape on a line, as
 * wide as its name; the open page's tab stands taller with no line under it,
 * joined to a frame round the page, so the tab and its page read as one.
 * Each tab is a plain Intuition boolean gadget that draws nothing itself.
 *
 * Grown out of OpenPrefs' Common/looktabs.h (the user, 10 October 2026: "the
 * tab option should be closer to MUI"), so the prefs editors, OpenSocketControl
 * and OpenWrite's inspector share one implementation.
 *
 * The window needs IDCMP_GADGETUP; a click comes with the gadget's id
 * (first_gid + tab). Call ogt_tabs_draw() after GT_RefreshWindow() and
 * between GT_BeginRefresh() and GT_EndRefresh().
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_TABS_H
#define OGT_TABS_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

#define OGT_TABS_MAX 12
#define OGT_TABS_PAD 12                 /* each side of a tab's name */
#define OGT_TABS_LIFT 2                 /* how much taller the open tab stands */

typedef struct ogt_tabs {
    int n, open;
    const char *label[OGT_TABS_MAX];
    int tx[OGT_TABS_MAX], tw[OGT_TABS_MAX];
    struct Gadget gad[OGT_TABS_MAX];
    int x, y, w, h;                     /* the row: x, y its top left, w the frame's width, h a tab's height */
    int first_gid;
    struct TextFont *font;              /* the font the names were measured in, drawn in too: keep it open */
} ogt_tabs;

/* Sets the names (not copied: keep them). */
void ogt_tabs_set(ogt_tabs *t, const char *const *labels, int n, int open);
/* Places the tabs along y from x, the page frame w wide; lh a tab's height (the font's plus 6 is right). */
void ogt_tabs_layout(ogt_tabs *t, struct RastPort *rp, int x, int y, int w, int lh);
/* The tabs' gadgets, linked after `after` (NULL: the first); returns the last. Add them with the window's. */
struct Gadget *ogt_tabs_gadgets(ogt_tabs *t, struct Gadget *after, int first_gid);
/* Which tab a gadget id is, or -1. */
int ogt_tabs_index(const ogt_tabs *t, int gadget_id);
/* Draws the tabs and the page's frame down to page_bottom (the window's inner bottom less 4 when -1). */
void ogt_tabs_draw(ogt_tabs *t, struct Window *win, int page_bottom);
/* Opens a tab (draws). */
void ogt_tabs_open(ogt_tabs *t, struct Window *win, int page_bottom, int open);

#endif
