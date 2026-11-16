/*
 * GFOpenPresentation: a slide's chart edited on GFOpenCalc's chart engine
 * and a data sheet (Presentation gap 2, part 4). The chart's data goes on
 * a one-sheet book, "Data": its categories (or a scatter's x values) down
 * column A from A2, a series a column from B, the series' names in row 1.
 * The chart goes on that sheet with its series pointing at those cells, in
 * an oc_core, so the editor shows and changes it as GFOpenCalc does: its
 * type, titles and legend through oc_core_update_chart, its data through
 * the cells (oc_edit_*), each one undoable there.
 *
 * Taken back, the chart is the slide's again: its look as edited, its
 * series and points read from the sheet as it is now (a column added is a
 * series added, a row added a point), with no cells of its own.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPR_CHARTEDIT_H
#define OPR_CHARTEDIT_H

#include "opencalc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A core holding the chart (not changed) on its data sheet, the chart selected. NULL when memory runs out. */
oc_core *opr_chart_edit_open(const owf_chart *chart);

/* The chart as edited, into out (which holds nothing yet; owf_chart_clear frees it). OWF_OK; OWF_ERR_FORMAT when
 * the sheet holds no chart; OWF_ERR_MEMORY (out then holds nothing). The core is not changed. */
int opr_chart_edit_take(oc_core *core, owf_chart *out);

/* The editor window's choices, as its cycle gadgets list them (NULL-ended). A chart's kind and legend to the
 * choice showing (an OTHER kind shows as Column), and a choice back to a kind or a legend ('r'... or 0). */
#define OPR_CHART_KINDS 6
#define OPR_CHART_GROUPINGS 3
#define OPR_CHART_LEGENDS 5
extern const char *const opr_chart_kind_labels[OPR_CHART_KINDS + 1];
extern const char *const opr_chart_grouping_labels[OPR_CHART_GROUPINGS + 1];
extern const char *const opr_chart_legend_labels[OPR_CHART_LEGENDS + 1];
int opr_chart_kind_choice(int kind);
int opr_chart_choice_kind(int choice);
int opr_chart_legend_choice(int legend);
int opr_chart_choice_legend(int choice);

/* A key typed in the editor's data grid, as the window gets it (Latin-1: 8 Backspace, 9 Tab, 13 Return,
 * 27 Escape, 127 Delete; printable letters typed into the active cell as UTF-8), given to the core as
 * GFOpenCalc's grid takes it. OWF_OK when it did something (redraw), OWF_ERR_UNSUPPORTED when it means
 * nothing, or the error of the edit it committed. */
int opr_chart_edit_char(oc_core *core, unsigned char latin1);

#ifdef __cplusplus
}
#endif

#endif
