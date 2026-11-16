/*
 * GFOpenBase reports (Design-OpenBase.md section 7), the portable half:
 * a report made from a table or a saved query, its rows grouped by one
 * field, with a total under each column for each group and for the report,
 * built as an owf_doc and saved through libowf (ODT, DOCX, text, PDF...).
 *
 * The document: the report's title as heading 1; for each group a heading 2
 * ("Town: Leeds") and a native table (a bold row of headings, the rows, a
 * bold row of totals); after the groups, "Report totals" and a table of the
 * report's totals. Without a group, one table with the totals as its last
 * row. A page footer: "Page n of m". No rows: the title and "No records.".
 *
 * Text in a project is ISO-8859-1; the document gets it as UTF-8.
 *
 * 0.2.0, report design: up to three group levels (group, then subgroup[0]
 * and subgroup[1]), each a heading one size down, the rows in a table for
 * each group of the last level with its totals, and an outer level's totals
 * after its groups ("Totals for Region: North" and a table). And the bands,
 * each a line of text with [names] in it:
 *   head        the report header, heading 1 ("" [Title])
 *   group_head  each level's group header ("" [Group]: [Value])
 *   group_foot  each level's group footer, after its totals ("" none)
 *   foot        the report footer, after the report's totals ("" none)
 *   page_head   on every page, at the top ("" none)
 *   page_foot   on every page, centred at the bottom ("" Page [Page] of [Pages])
 * The names: [Title] (the title, or the name), [Date] (the day it is
 * printed), [Page] and [Pages] (page bands only), [Group] (the level's
 * field, by its heading) and [Value] (its value; "(none)" when empty) in
 * group bands, [Count] (the rows in the group, or in the report) in footers,
 * and any field (column, for a query) of the source in group bands: the
 * group's first row in its header, its last row in its footer. "[[" is a
 * '['. A field called Title, Date... is shadowed by the name.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENBASE_REPORT_H
#define OPENBASE_REPORT_H

#include "openbase_core.h"
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_REPORT_VERSION "0.2.0"
#define OB_REPORT_COLUMNS 16
#define OB_REPORT_LEVELS 3        /* group levels: group, subgroup[0], subgroup[1] */
#define OB_BAND_MAX 160            /* a band's text, with its 0 */

/* A column: a field of the table (or a column of the query, by its heading)
 * and the total under it. Count counts the values that aren't empty; Sum,
 * Average, Min and Max need an Integer, Number or Currency field (a query's
 * expression columns are taken as numbers). OB_TOTAL_GROUP is not a column's
 * total: the report's group field says that. */
typedef struct ob_rcolumn {
    char field[OB_NAME_MAX];
    char heading[OB_NAME_MAX];      /* "" is the field's label (a query's: its heading) */
    ob_total total;
} ob_rcolumn;

/* About 4.5 KB: a window keeps one static, not on a small Amiga stack. */
typedef struct ob_report {
    char name[OB_NAME_MAX];
    char title[OB_VALUE_MAX];       /* "" is the name */
    char table[OB_NAME_MAX];        /* the source: a table... */
    char query[OB_NAME_MAX];        /* ...or a saved query, not both */
    char sort[OB_NAME_MAX];         /* a table's: the order of the rows ("" key order). Not used with a
                                       group: the groups are in their field's order and the rows in a
                                       group in key order. A query's rows come in its own order. */
    int desc;                       /* sort (or the groups) Z to A */
    char group[OB_NAME_MAX];        /* "" none: a heading and totals for each value of this field. A grid
                                       query must sort by it first (ob_report_check says so); an SQL
                                       query must ORDER BY it first, which isn't checked. */
    int ncolumns;
    ob_rcolumn columns[OB_REPORT_COLUMNS];
    /* 0.2.0 */
    char subgroup[OB_REPORT_LEVELS - 1][OB_NAME_MAX];   /* the second and third levels ("" none); a table's
                                       rows sort by every level (ob_view's then[]), the same way up or
                                       down; a grid query must sort by every level first, in order */
    char head[OB_BAND_MAX], foot[OB_BAND_MAX];                  /* the bands: see the top of this file */
    char page_head[OB_BAND_MAX], page_foot[OB_BAND_MAX];
    char group_head[OB_REPORT_LEVELS][OB_BAND_MAX], group_foot[OB_REPORT_LEVELS][OB_BAND_MAX];
    int no_totals;                  /* bit L: level L's groups have no totals (the report's stay) */
    int new_page;                   /* each first-level group after the first starts a new page */
} ob_report;

void ob_report_init(ob_report *r, const char *name);
/* Adds a column; NULL when there are OB_REPORT_COLUMNS already. */
ob_rcolumn *ob_report_add_column(ob_report *r, const char *field, ob_total total);

/* Whether r can be made in project p: 0, or -1 with the reason in err
 * ("Sales: Sum needs a number: Customer is Text"). */
int ob_report_check(ob_project *p, const ob_report *r, char *err, size_t errsize);

/* Makes the report as a new document (*out, for owf_doc_free); params are the
 * query's parameter values (ob_query_rows'). 0, or -1 with the reason in err
 * and *out NULL. */
int ob_report_build(ob_project *p, const ob_report *r, const char *const *params, owf_doc **out,
                    char *err, size_t errsize);

/* ob_report_build and owf_export_file: format is libowf's name ("odt",
 * "docx", "pdf"...), NULL to go by path's extension; notes (may be NULL)
 * gets what the format could not keep. 0, or -1 with the reason in err. */
int ob_report_save(ob_project *p, const ob_report *r, const char *const *params, const char *path,
                   const char *format, owf_report *notes, char *err, size_t errsize);

/* ---- The text a report is kept as ----
 * "OADBREPORT 1" then a line a setting ("table=Orders", "column=Amount|Total|Sum"), '\n' at the end of
 * each; "OADBREPORT 2" when any 0.2.0 setting is used (subgroup=, head=, foot=, pagehead=, pagefoot=,
 * grouphead=L|text, groupfoot=L|text with L 1 to 3, nototals=, newpage=), so a GFOpenBase that can't
 * make it says so; '\\', '\n' and '|' in a value are written \\, \n and \|. A reader skips keys it doesn't know.
 * The name isn't in the text: it is the key it is kept under. */

/* r as text in out: its length, or -1 when it doesn't fit in size. */
int ob_report_to_text(const ob_report *r, char *out, size_t size);
/* r from text, named name: 0, or -1 with the reason in err ("line 3: ..."). */
int ob_report_from_text(ob_report *r, const char *name, const char *text, char *err, size_t errsize);

/* ---- Reports kept in the project ----
 * The core keeps each report's text in oadb_reports (ob_report_count... in openbase_core.h). */

/* Checks r (ob_report_check) and keeps it under its name, replacing one kept there: 0, or -1 with the
 * reason in err. */
int ob_report_store(ob_project *p, const ob_report *r, char *err, size_t errsize);
/* The report kept as name (any case), named as it was kept: 0, or -1 with the reason in err. */
int ob_report_load(ob_project *p, const char *name, ob_report *r, char *err, size_t errsize);

#ifdef __cplusplus
}
#endif

#endif
