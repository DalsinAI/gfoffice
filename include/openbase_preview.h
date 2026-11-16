/*
 * GFOpenBase's print preview of a report (Design-OpenBase.md section 7): the report built as its document
 * (ob_report_build) and laid out into pages by GFOpenWrite's layout (ow_layout, through GFOpenLayout), the
 * pages the PDF and the printer get. The Report window's Preview... shows them a page at a time
 * (app/ob_previewwin.c); this part has no Intuition in it, so the host tests run it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENBASE_PREVIEW_H
#define OPENBASE_PREVIEW_H

#include "openbase_report.h"
#include "ow_layout.h"

#define OB_PREVIEW_VERSION "0.1.0"

typedef struct ob_preview ob_preview;

/* The report r of p (its parameters' values params, as ob_report_build takes them) built and laid out,
 * its text measured by measure. NULL with err said when the report can't be built or memory runs out. */
ob_preview *ob_preview_new(ob_project *p, const ob_report *r, const char *const *params,
                           ol_measure_text_fn measure, void *measure_userdata, char *err, size_t errsize);
void ob_preview_free(ob_preview *v);

int ob_preview_pages(const ob_preview *v);                 /* at least 1 */
const owf_doc *ob_preview_doc(const ob_preview *v);
const ow_layout *ob_preview_layout(const ob_preview *v);

/* page (from 0) moved by by pages, kept inside the report's pages. */
int ob_preview_turn(const ob_preview *v, int page, int by);
/* "Page 2 of 3" for page 1 (from 0), the window's title line. */
void ob_preview_label(const ob_preview *v, int page, char *out, size_t size);

/* The page's header or footer text (which: OW_STORY_HEADER or OW_STORY_FOOTER), its fields filled in for
 * that page and date the date; "" when the page has none. For the tests and a one-line status. */
void ob_preview_band_text(const ob_preview *v, ow_story_kind which, int page, const char *date, char *out,
                          size_t size);

#endif
