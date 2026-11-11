/*
 * libowf: a pivot table's definition (GFOpenCalc's DataPilot), as the
 * spreadsheet's core, the ODS reader and writer and the XLSX reader and
 * writer share it.
 *
 * The model keeps a pivot table in two hidden names on its output sheet:
 * _GF_PIVOT_SOURCE, the source table (a range with its heading row, so that
 * it follows inserted rows and columns as any name does), and
 * _GF_PIVOT_CONFIG, a text that holds the field layout. ODS files carry it
 * as table:data-pilot-table and XLSX files as a pivot table with its pivot
 * cache; the readers turn those back into the two names. One pivot table
 * per sheet.
 *
 * Fields are counted from the source's first column (0 is its first
 * column). Functions are numbered as GFOpenCalc's subtotals: 1 average,
 * 2 count (numbers), 3 counta, 4 max, 5 min, 6 product, 7 stdev, 8 stdevp,
 * 9 sum, 10 var, 11 varp.
 *
 * The code is in libowf/calc/owf_pivotdef.h: owf_book.c holds it for
 * libowf, and GFOpenCalc's core compiles its own copy when it runs on
 * owfcalc.library (which does not export it).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_PIVOT_H
#define OWF_PIVOT_H

#include <stddef.h>
#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OWF_PIVOT_SOURCE "_GF_PIVOT_SOURCE"
#define OWF_PIVOT_CONFIG "_GF_PIVOT_CONFIG"

#define OWF_PIVOT_FIELDS 8           /* row fields, and column fields */
#define OWF_PIVOT_DATA 8             /* data fields */
#define OWF_PIVOT_PAGES 4            /* page fields (filters) */
#define OWF_PIVOT_ITEM 96            /* a page field's chosen item, bytes */
#define OWF_PIVOT_TEXT_MAX 2048      /* _GF_PIVOT_CONFIG's text, at most */

enum { OWF_PIVOT_FN_FIRST = 1, OWF_PIVOT_FN_COUNT = 2, OWF_PIVOT_FN_COUNTA = 3, OWF_PIVOT_FN_SUM = 9,
       OWF_PIVOT_FN_LAST = 11 };

/* A page field's chosen item: every item, or one value. */
enum { OWF_PIVOT_ALL, OWF_PIVOT_TEXT, OWF_PIVOT_NUMBER, OWF_PIVOT_BOOL, OWF_PIVOT_BLANK };

typedef struct {
    unsigned field;
    int function;
} owf_pivot_data;

typedef struct {
    unsigned field;
    int kind;                        /* OWF_PIVOT_ALL ... */
    double number;                   /* NUMBER, BOOL */
    char text[OWF_PIVOT_ITEM];       /* TEXT: UTF-8 */
} owf_pivot_page;

typedef struct {
    int source_sheet;
    owf_range source;                /* its first row holds the headings */
    unsigned nrow, ncol, ndata, npage;
    unsigned row[OWF_PIVOT_FIELDS];  /* outermost first */
    unsigned col[OWF_PIVOT_FIELDS];
    owf_pivot_data data[OWF_PIVOT_DATA];
    owf_pivot_page page[OWF_PIVOT_PAGES];
    int row_grand;                   /* a Grand Total row under the table */
    int col_grand;                   /* a Grand Total column at its right (with column fields) */
    unsigned at_row, at_col;         /* where the output starts on its sheet */
    unsigned rows, cols;             /* the output's size when last made; 0: not known */
} owf_pivotdef;

/* An empty definition: no fields, both grand totals, output at A1. */
void owf_pivot_init(owf_pivotdef *def);
/* 1 for _GF_PIVOT_SOURCE and _GF_PIVOT_CONFIG. */
int owf_pivot_is_name(const char *name);
/* The pivot table on a sheet, from its two names. 1 when it has one that
 * makes sense for the book as it is (owf_pivot_check), else 0. */
int owf_pivot_get(const owf_book *book, int sheet, owf_pivotdef *def);
/* 1 when the fields are inside the source, none is used twice (a field may
 * be a data field as well as a row, column or page field, as in
 * OpenOffice), there is a data field and the functions are known. */
int owf_pivot_check(const owf_book *book, const owf_pivotdef *def);
/* _GF_PIVOT_SOURCE's formula: 'Sheet 1'!$A$1:$C$9. 0 when it does not fit. */
int owf_pivot_source_text(const owf_book *book, const owf_pivotdef *def, char *out, size_t size);
/* _GF_PIVOT_CONFIG's formula, a quoted text. 0 when it does not fit. */
int owf_pivot_config_text(const owf_pivotdef *def, char *out, size_t size);
/* A sheet and range from "Sheet1!A1:C9", "'My sheet'!$A$1:$C$9" or
 * "Sheet1.A1:Sheet1.C9" (ODF's, the sheet once or twice). 1 when found. */
int owf_pivot_parse_source(const owf_book *book, const char *text, int *sheet, owf_range *range);
/* The layout from _GF_PIVOT_CONFIG's formula; def's source must be set
 * first (the oldest form named absolute columns). 1 when it reads. */
int owf_pivot_parse_config(const char *formula, owf_pivotdef *def);
/* Whether a source cell is a page field's chosen item. */
int owf_pivot_item_matches(const owf_pivot_page *page, const owf_cell *cell);
/* A page field's item from a cell's value, or from a file's text (a
 * number when the whole text is one; "" is the blank item). */
void owf_pivot_item_from_cell(owf_pivot_page *page, const owf_cell *cell);
void owf_pivot_item_from_text(owf_pivot_page *page, const char *text);
/* The item as text: "East", "12.5", "TRUE", "" for blank. */
void owf_pivot_item_text(const owf_pivot_page *page, char *out, size_t size);
/* A function's name in ODF (table:function) or in OOXML (subtotal), and
 * back from either (case does not matter); 0 when it is not one. */
const char *owf_pivot_function_word(int function, int ooxml);
int owf_pivot_function_from_word(const char *word);
/* A field's heading: its heading cell's text or number, or "Column N"
 * when that is blank. */
void owf_pivot_field_heading(const owf_book *book, const owf_pivotdef *def, unsigned field, char *out,
                             size_t size);

#ifdef __cplusplus
}
#endif

#endif
