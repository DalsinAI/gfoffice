/*
 * GFOpenPresentation's chart editor window (Presentation gap 2): a slide's chart on GFOpenCalc's core, its data
 * in a grid beside the chart as GFOpenCalc draws it, its type, arrangement, title and legend in gadgets above.
 * The core is src/deckchart/opr_chartedit.c (include/opr_chartedit.h); this is the window over it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPR_CHARTWIN_H
#define OPR_CHARTWIN_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include "owf.h"

/* What the window needs from GFOpenPresentation's main window. */
typedef struct opr_cwin_env {
    struct Screen *scr;
    APTR vi;                    /* GadTools' VisualInfo for scr */
    struct TextFont *font;
    struct TextAttr *ta;        /* font's, for the gadgets */
    struct Window *parent;      /* opened over it, activated again on close */
    LONG (*pen)(unsigned long rgb);   /* a pen for 0xRRGGBB, the window's theme's (kept by it) */
} opr_cwin_env;

/* Opens the window on chart (not changed) and returns when it is closed: 1 with out (which holds nothing yet;
 * owf_chart_clear frees it) the chart as edited when OK was pressed, 0 when it was cancelled or could not open. */
int opr_chart_window(const opr_cwin_env *env, const owf_chart *chart, owf_chart *out);

#endif
