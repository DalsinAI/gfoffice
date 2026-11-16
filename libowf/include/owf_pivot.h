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
#define OWF_PIVOT_SORTS 16           /* sorted fields (rows and columns) */
#define OWF_PIVOT_HIDES 32           /* hidden items, all fields together */
#define OWF_PIVOT_GROUPS 8           /* grouped row and column fields */
#define OWF_PIVOT_SUBTOTALS 16       /* row and column fields with a subtotal per item */
#define OWF_PIVOT_ITEM 96            /* a page field's chosen item, bytes */
#define OWF_PIVOT_NAMES 8            /* groups made by hand, all fields together */
#define OWF_PIVOT_NAME 48            /* such a group's name, bytes */
#define OWF_PIVOT_MEMBERS 24         /* items put in them, all fields together */
#define OWF_PIVOT_TEXT_MAX 2048      /* _GF_PIVOT_CONFIG's text, at most */

enum { OWF_PIVOT_FN_FIRST = 1, OWF_PIVOT_FN_COUNT = 2, OWF_PIVOT_FN_COUNTA = 3, OWF_PIVOT_FN_SUM = 9,
       OWF_PIVOT_FN_LAST = 11 };

/* The order of a row or column field's items: as they first come in the
 * source (the default), or sorted by their values. Ascending puts numbers
 * and booleans first, then text (case aside), then errors; descending is
 * that order turned round, so errors come first. Blanks come last in
 * either direction, as in GFOpenCalc's Data > Sort. */
enum { OWF_PIVOT_SORT_SOURCE, OWF_PIVOT_SORT_ASCENDING, OWF_PIVOT_SORT_DESCENDING };

/* A row or column field's numbers grouped into items: ranges of step from
 * start ("0-9", "10-19", with "<0" and ">99" for the numbers outside start
 * and end), or dates (serial numbers) by year, quarter ("Qtr1") or month
 * ("Jan"). Cells that are not numbers stay items of their own. */
enum { OWF_PIVOT_GROUP_NONE, OWF_PIVOT_GROUP_RANGE, OWF_PIVOT_GROUP_YEARS, OWF_PIVOT_GROUP_QUARTERS,
       OWF_PIVOT_GROUP_MONTHS, OWF_PIVOT_GROUP_LAST = OWF_PIVOT_GROUP_MONTHS };

typedef struct {
    unsigned field;
    int by;                          /* OWF_PIVOT_GROUP_... */
    double start, end, step;         /* RANGE only: step > 0, end >= start */
} owf_pivot_group;

/* A group made by hand: a row or column field's items put together under a
 * name ("North" for "East" and "West"), as Data Pilot's Group and Excel's
 * Group Selection make. Items in none stay items of their own. A field has
 * groups by hand or an owf_pivot_group, not both. */
typedef struct {
    unsigned field;
    char name[OWF_PIVOT_NAME];       /* UTF-8, not empty */
} owf_pivot_named;

/* A page field's chosen item: every item, or one value. */
enum { OWF_PIVOT_ALL, OWF_PIVOT_TEXT, OWF_PIVOT_NUMBER, OWF_PIVOT_BOOL, OWF_PIVOT_BLANK };

/* How a data field's values are shown: as they are (the default), as a
 * share of their row's total, of their column's total or of the grand
 * total, or as an index: value x grand total / (row total x column total).
 * Each total is made with the field's own function. A total of 0 shows
 * #DIV/0!. */
enum { OWF_PIVOT_SHOW_NORMAL, OWF_PIVOT_SHOW_ROW, OWF_PIVOT_SHOW_COLUMN, OWF_PIVOT_SHOW_TOTAL,
       OWF_PIVOT_SHOW_INDEX, OWF_PIVOT_SHOW_LAST = OWF_PIVOT_SHOW_INDEX };

typedef struct {
    unsigned field;
    int function;
    int show;                        /* OWF_PIVOT_SHOW_... */
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
    unsigned nsort;                  /* fields whose items are sorted, kept by field, so that */
    unsigned sort_field[OWF_PIVOT_SORTS];   /* moving a field in the layout keeps its order */
    unsigned char sort_order[OWF_PIVOT_SORTS];   /* OWF_PIVOT_SORT_ASCENDING or _DESCENDING */
    unsigned nhide;                  /* row and column items left out: each a field and a value */
    owf_pivot_page hide[OWF_PIVOT_HIDES];   /* (kind is never OWF_PIVOT_ALL) */
    unsigned ngroup;                 /* grouped fields, kept by field as sorts are */
    owf_pivot_group group[OWF_PIVOT_GROUPS];
    unsigned nsubtotal;              /* fields with a subtotal per item, kept by field as sorts are */
    unsigned subtotal_field[OWF_PIVOT_SUBTOTALS];
    unsigned nnamed;                 /* groups made by hand */
    owf_pivot_named named[OWF_PIVOT_NAMES];
    unsigned nmember;                /* their items: each a field's value (kind never ALL) */
    owf_pivot_page member[OWF_PIVOT_MEMBERS];
    unsigned char member_of[OWF_PIVOT_MEMBERS];   /* which of named[] it is in */
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
/* A data field's way of showing its values, by name in ODF
 * (table:data-pilot-field-reference's table:type) or OOXML (showDataAs),
 * and back from either; NULL and -1 when it is not one. */
const char *owf_pivot_show_word(int show, int ooxml);
int owf_pivot_show_from_word(const char *word);
/* A row or column field's item order (OWF_PIVOT_SORT_SOURCE when it has
 * none), and setting it: SOURCE removes the field's entry. 0 when there is
 * no room for another sorted field or the order is not one. */
int owf_pivot_sort_of(const owf_pivotdef *def, unsigned field);
int owf_pivot_set_sort(owf_pivotdef *def, unsigned field, int order);
/* Hidden items: a row or column field's item that is left out of the
 * table, its totals with it, as a page field leaves out what it doesn't
 * choose. owf_pivot_is_hidden: 1 when a source cell of that field is one.
 * owf_pivot_set_hidden adds (hidden 1) or removes (0) item's value for
 * item->field; 0 when there is no room or item is OWF_PIVOT_ALL. */
int owf_pivot_is_hidden(const owf_pivotdef *def, unsigned field, const owf_cell *cell);
/* A field's grouping (NULL when it has none), and setting it: by NONE
 * removes it. 0 when there is no room or the grouping is not one. */
const owf_pivot_group *owf_pivot_group_of(const owf_pivotdef *def, unsigned field);
int owf_pivot_set_group(owf_pivotdef *def, const owf_pivot_group *group);
/* The item a number falls in: the start of its range (-1E300 below start,
 * 1E300 above end), the year, the quarter (1-4) or the month
 * (1-12). date1904 as the book has it. And that item's name. */
double owf_pivot_group_key(const owf_pivot_group *group, double number, int date1904);
void owf_pivot_group_label(const owf_pivot_group *group, double key, char *out, size_t size);
int owf_pivot_set_hidden(owf_pivotdef *def, const owf_pivot_page *item, int hidden);
/* Whether a row field's items each get a subtotal row under them (an
 * "East Total" row after East's rows), made with each data field's own
 * function; and setting it (on 1) or taking it away (0). The innermost row
 * field has none (its rows are its items). A column field keeps the setting
 * for the files that carry it (XLSX, ODS); its subtotal columns are not
 * laid out yet. 0 when there is no room for another field. */
int owf_pivot_subtotal_of(const owf_pivotdef *def, unsigned field);
int owf_pivot_set_subtotal(owf_pivotdef *def, unsigned field, int on);
/* The name of the group made by hand that a cell of field is in, or NULL.
 * owf_pivot_set_named puts item (its field and value) in the group of that
 * name on its field, making the group when it is new and taking the item
 * out of any other; a NULL or empty name takes it out of its group. A group
 * left with no items goes. 0 when there is no room, the name is too long,
 * item is OWF_PIVOT_ALL or the field is grouped by an owf_pivot_group. */
const char *owf_pivot_named_of(const owf_pivotdef *def, unsigned field, const owf_cell *cell);
int owf_pivot_set_named(owf_pivotdef *def, const owf_pivot_page *item, const char *name);

/* A field's heading: its heading cell's text or number, or "Column N"
 * when that is blank. */
void owf_pivot_field_heading(const owf_book *book, const owf_pivotdef *def, unsigned field, char *out,
                             size_t size);

#ifdef __cplusplus
}
#endif

#endif
