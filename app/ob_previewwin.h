/*
 * GFOpenBase's print preview window (Design-OpenBase.md section 7): a report's pages as the PDF and the
 * printer get them, a whole page at a time, with Previous, Next and Close. The pages come from
 * include/openbase_preview.h; this is the window over them.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OB_PREVIEWWIN_H
#define OB_PREVIEWWIN_H

#include "ob_reportwin.h"
#include "openbase_report.h"

/* Builds r (its parameters' values params, NULL none) and shows its pages over env's window; returns when
 * the preview is closed. err gets why when the report can't be shown, and 0 comes back; 1 otherwise. */
int ob_preview_window(const ob_rwin_env *env, struct Window *over, const ob_report *r,
                      const char *const *params, char *err, size_t errsize);

#endif
