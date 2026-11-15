/*
 * GFOpenBase's Report window (Design-OpenBase.md section 7): make a report from a table or a saved
 * query, keep it in the project, and save it as a document through libowf (ODT, DOCX, PDF, text).
 * The report code is include/openbase_report.h; this is the window over it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_REPORTWIN_H
#define OB_REPORTWIN_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/text.h>
#include "openbase_core.h"

/* What the window needs from GFOpenBase's main window. */
typedef struct ob_rwin_env {
    struct Screen *scr;
    APTR vi;                    /* GadTools' VisualInfo for scr */
    struct TextFont *font;
    struct TextAttr *ta;        /* font's, for the gadgets */
    int fw, fh;                 /* an average character's width; the font's height */
    struct Window *parent;      /* opened over it, activated again on close */
    ob_project *proj;
    const char *proj_path;      /* the project's file: the document requester starts in its drawer */
    const char *table;          /* the table shown ("" none): a new report's source */
    const char *query;          /* or the query shown ("" none) */
} ob_rwin_env;

/* Opens the window and returns when it is closed. */
void ob_report_window(const ob_rwin_env *env);

#endif
