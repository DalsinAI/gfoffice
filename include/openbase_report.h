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
 * The report window, and keeping reports in the project (oadb_reports),
 * come later.
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

#define OB_REPORT_VERSION "0.1.0"
#define OB_REPORT_COLUMNS 16

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

/* About 3 KB. */
typedef struct ob_report {
    char name[OB_NAME_MAX];
    char title[OB_VALUE_MAX];       /* "" is the name */
    char table[OB_NAME_MAX];        /* the source: a table... */
    char query[OB_NAME_MAX];        /* ...or a saved query, not both */
    char sort[OB_NAME_MAX];         /* a table's: the order of the rows ("" key order; a group sorts by its field) */
    int desc;                       /* sort (or the groups) Z to A */
    char group[OB_NAME_MAX];        /* "" none: a heading and totals for each value of this field */
    int ncolumns;
    ob_rcolumn columns[OB_REPORT_COLUMNS];
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

#ifdef __cplusplus
}
#endif

#endif
