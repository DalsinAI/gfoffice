/*
 * GFOpenBase's sheets in and out (Design-OpenBase.md section 8): a sheet of
 * an OpenDocument (.ods) or Excel (.xlsx) workbook into a table, or into a
 * new table whose fields are guessed from it, and a table (or a view of it)
 * or a query's rows out as a workbook of one sheet. The files are read and
 * written by libowf, as GFOpenCalc reads and writes them; the rows go
 * through the core's own import (ob_grid_import), so a sheet is taken as a
 * CSV file is: by header or field order, all or none (or the good rows and
 * the bad listed), one step of Undo.
 *
 * Reading: each cell becomes the text a field takes. A number shown as a
 * date is 2026-11-16 (and its time, when it has one), as a time 14:30:00;
 * a number shown with a currency sign keeps two decimals (so a new table
 * makes it Currency); other numbers as GFOpenCalc writes them (up to 15
 * digits, "0.1", "12"); TRUE and FALSE are Yes and No; an error is its
 * name (#N/A). Text is made ISO-8859-1, as the Amiga keeps it ('?' for what
 * it lacks). A formula gives the value the file kept for it. The rows start
 * at the sheet's first row that holds anything, from its first column that
 * does; a title above the table is passed over (rows of one cell each, then
 * an empty row, then a row of two cells or more: the rows start there). A
 * row with nothing in it is left out. Messages say "Row 4", the sheet's own row
 * number. A text file is not a workbook here: CSV goes through the core.
 *
 * Writing: a header row of field names (bold, kept in view when the sheet
 * scrolls), then a row for each row: Counter, Integer, Lookup and Number as
 * numbers, Currency as a number shown 1,234.50, Date, Time and Date and
 * time as dates and times the spreadsheet can count with, Yes/No as TRUE
 * and FALSE, the rest as text. Datatype objects are left out, as CSV leaves
 * them. A query's columns that aren't fields are numbers when their values
 * are numbers, else text.
 *
 * In its own drawer (src/base/sheet/), as reports are, so that the core's
 * tests don't need libowf. Built with OWF_USE_CALC_LIBRARY (OWF=library) the
 * workbook calls are owfcalc.library's: the first call that needs it opens
 * it (a call says so when it isn't in LIBS:), and ob_sheet_close closes it.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENBASE_SHEET_H
#define OPENBASE_SHEET_H

#include "openbase_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OB_SHEET_VERSION "0.1.1"
#define OB_SHEET_NAME_MAX 128      /* a sheet's name, with its 0 (ISO-8859-1) */
#define OB_SHEET_MAX_ROWS 1048576L /* the most rows read or written: Excel's */

typedef struct ob_sheet_opts {
    /* reading: which sheet. sheet_name first (any case); "" is by number:
     * sheet from 0, -1 the one the file opens at */
    int sheet;
    char sheet_name[OB_SHEET_NAME_MAX];
    /* writing: the sheet's name ("" the table's or the query's); the format,
     * "ods" or "xlsx" (NULL: by the file's name) */
    const char *format;
    /* reading, as ob_csv_opts's: -1 found, 1 the first row names fields, 0 data */
    int header;
    int skip_bad;
    /* out, reading */
    int bad;
    char report[1024];
    int found_header;
    char found_sheet[OB_SHEET_NAME_MAX];
} ob_sheet_opts;

void ob_sheet_opts_init(ob_sheet_opts *o);

/* The sheets of the workbook at path, in its order, up to max into names
 * (ISO-8859-1); how many there are (which may be more than max), -1 with
 * the reason in ob_error ("Book.ods is not a workbook GFOpenBase reads"). */
int ob_sheet_list(ob_project *p, const char *path, char (*names)[OB_SHEET_NAME_MAX], int max);

/* Adds the sheet's rows to table: the rows added, -1 with the reason
 * ("Row 4: Price must be a number"). o may be NULL (the first sheet the
 * file opens at, header found, all or none). */
ob_int ob_sheet_import(ob_project *p, const char *table, const char *path, ob_sheet_opts *o);

/* A new table's design from the sheet, as ob_csv_guess makes one from a CSV
 * file: name (or the sheet's name), a Counter, a field for each column typed
 * from its first 200 rows. Then ob_table_create and ob_sheet_import. */
int ob_sheet_guess(ob_project *p, const char *path, const char *name, ob_sheet_opts *o, ob_table *out);

/* Writes every row of the view (NULL: key order) of table as a workbook of
 * one sheet: the rows written, -1 with the reason. */
ob_int ob_sheet_export(ob_project *p, const char *table, const ob_view *v, const char *path,
                       const ob_sheet_opts *o);

/* The same for every row a query gives, its headings as the header; the
 * answers (params) are checked before the file is made. */
ob_int ob_query_sheet_export(ob_project *p, const ob_query *q, const char *const *params, const char *path,
                             const ob_sheet_opts *o);

/* Closes owfcalc.library if a call opened it; at the program's end. */
void ob_sheet_close(void);

#ifdef __cplusplus
}
#endif

#endif
