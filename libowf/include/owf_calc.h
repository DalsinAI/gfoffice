/*
 * libowf: the formula engine (OpenCalc's, in the GFOffice Suite). It
 * parses the formulas a book's cells hold (Excel's A1 syntax, without the
 * "="), keeps which cells each one reads, and works them out again, in
 * order, when cells change. Each result goes into its cell's value
 * (owf_cell), where the files keep it. docs/CALC_ENGINE.md has the
 * functions and the rules.
 *
 * A book read from a file shows the file's results until something
 * changes: owf_calc_new parses, it does not calculate. A formula the engine
 * cannot work out (a function it does not have, a reference to another
 * book) keeps the value the file gave it, and the cells that read it use
 * that value, until the user types the cell again; typed afresh it gives
 * #NAME? (or #REF! for the other book).
 *
 * Cells in a circular reference (a loop of formulas that read each other,
 * or one that reads itself) get #REF! and the OWF_CALC_CIRCULAR state; the
 * cells that read them are worked out from that #REF!. The engine never
 * loops: nothing in it recurses (but defined names inside names, at most
 * eight deep), and its memory grows with the book, not with how formulas
 * nest; a formula's working values have a limit (1 MB: past it, #NUM!).
 *
 * Plain C99 doubles and <math.h>. The sources are the .c files in
 * libowf/calc: a program that calculates compiles them with libowf's own
 * and links the maths library (-lm on the PC, libnix's soft maths on the
 * Amiga). They are not in owf.library yet (its calls come later).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_CALC_H
#define OWF_CALC_H

#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One book's parsed formulas and what they read. */
typedef struct owf_calc owf_calc;

/* The engine for a book: it parses every formula already in the book and
 * its defined names, and marks nothing dirty, so the cells keep the values
 * the file gave them (owf_calc_recalc_all works everything out). NULL when
 * memory runs out. The book must outlive the engine. Change its cells
 * through the engine (owf_calc_enter, owf_calc_shift); after changing the
 * book any other way (a sheet added, renamed or removed, names defined,
 * cells set with owf_sheet_cell) call owf_calc_rebuild. */
owf_calc *owf_calc_new(owf_book *book);
void owf_calc_free(owf_calc *calc);
/* Parses every formula and name again and marks every formula dirty.
 * OWF_OK or OWF_ERR_MEMORY (then nothing calculates until it succeeds). */
int owf_calc_rebuild(owf_calc *calc);

/* How typed dates are read: "1/2/2026" is 1 February (OWF_CALC_DMY, the
 * default), 2 January (OWF_CALC_MDY). "2026-02-01" is always ISO. */
enum { OWF_CALC_DMY, OWF_CALC_MDY };
void owf_calc_set_date_order(owf_calc *calc, int order);

/* A cell's content as a user types it, as Excel reads typed input:
 *   "=SUM(A1:A3)"   a formula (the cell keeps "SUM(A1:A3)"; typed
 *                   "=sum(a1:a3)", it keeps that too, in capitals);
 *   "12.5", "-3", "1,234.5", "1E3", "(12)", "50%", "£12.50", "$3", "€4"
 *                   a number;
 *   "25/10/2026", "2026-10-25", "25-Oct-2026", "13:45", "1:45 PM",
 *   "25/10/2026 13:45"
 *                   a date or time (a number: days since the epoch);
 *   "TRUE", "false" a boolean;  "#N/A" and the other errors: that error;
 *   "'0123"         text without the apostrophe;  anything else: text;
 *   ""              clears the cell's value and formula (not its style).
 * When the cell's number format is General, a typed percentage, currency,
 * date or time also gives the cell a format to match ("0%", "£#,##0.00",
 * "dd/mm/yyyy", "h:mm"...), as Excel does. The cell is set (created if
 * need be: owf_cell pointers into the sheet go stale) and what reads it is
 * marked dirty; nothing is worked out until owf_calc_recalc.
 * Returns OWF_OK; OWF_ERR_FORMAT for a formula that won't parse (the text
 * is kept as the cell's formula and the cell shows #NAME?, state
 * OWF_CALC_BAD); OWF_ERR_MEMORY (the cell is as it was); OWF_ERR_UNSUPPORTED
 * for a sheet that is not in the book. */
int owf_calc_enter(owf_calc *calc, int sheet, unsigned row, unsigned col, const char *input);

/* Works out every dirty formula, and every volatile one (RAND, NOW,
 * TODAY) with what reads it, in dependency order; loops get #REF!.
 * OWF_OK or OWF_ERR_MEMORY (what was not worked out stays dirty). */
int owf_calc_recalc(owf_calc *calc);
/* Every formula, dirty or not. */
int owf_calc_recalc_all(owf_calc *calc);

/* Called during a recalc for each cell whose value changed, so that a grid
 * redraws only those; NULL for none. */
typedef void (*owf_calc_notify)(void *user, int sheet, unsigned row, unsigned col);
void owf_calc_set_notify(owf_calc *calc, owf_calc_notify notify, void *user);

/* What the engine knows of a cell, for a status line. */
enum {
    OWF_CALC_FORMULA = 1,    /* it has a formula */
    OWF_CALC_DIRTY = 2,      /* to be worked out at the next recalc */
    OWF_CALC_BAD = 4,        /* the formula won't parse: #NAME? */
    OWF_CALC_KEPT = 8,       /* the engine cannot work it out (an unknown function, another
                                book): it keeps the file's value */
    OWF_CALC_CIRCULAR = 16,  /* in a circular reference at the last recalc: #REF! */
    OWF_CALC_VOLATILE = 32   /* RAND, NOW, TODAY: worked out at every recalc */
};
unsigned owf_calc_state(const owf_calc *calc, int sheet, unsigned row, unsigned col);
/* How many cells the last recalc found in circular references. */
int owf_calc_circular_count(const owf_calc *calc);

/* Rows (axis 0) or columns (axis 1) inserted (count > 0) or deleted
 * (count < 0) at `at` (from 0) on one sheet. The sheet's cells, row
 * heights, column widths and merged ranges move; every formula in the book
 * and every defined name that refers to the sheet is adjusted as Excel
 * adjusts them: a range grows when rows go in inside it and shrinks when
 * some of its rows go, and a reference to a deleted cell (or a range all
 * deleted) becomes #REF!. Every formula is dirty after. OWF_OK,
 * OWF_ERR_MEMORY (the book may be half moved: owf_calc_rebuild) or
 * OWF_ERR_UNSUPPORTED for a sheet that is not there. */
int owf_calc_shift(owf_calc *calc, int sheet, int axis, unsigned at, int count);

/* The program changed cells in a range itself (an undo put back values
 * and formulas with owf_sheet_cell and owf_cell_set_...): the engine takes
 * the range's formulas again and marks dirty what is there and what reads
 * it, without parsing the rest of the book. A formula there that the
 * engine cannot work out keeps the value the program gave it. OWF_OK,
 * OWF_ERR_MEMORY (the next recalc parses everything again) or
 * OWF_ERR_UNSUPPORTED for a sheet that is not there. */
int owf_calc_touch(owf_calc *calc, int sheet, const owf_range *range);

/* Renames a sheet, and every reference to it in the book's formulas and
 * defined names ("Old!A1" becomes "'New name'!A1"; quotes as the name
 * needs them). OWF_OK; OWF_ERR_FORMAT for a name Excel would not take
 * (empty, over 31 characters, one of : \ / ? * [ ], a ' first or last, or
 * another sheet's name in any case): nothing changes; OWF_ERR_MEMORY
 * (nothing changes); OWF_ERR_UNSUPPORTED for a sheet that is not there. */
int owf_calc_rename_sheet(owf_calc *calc, int sheet, const char *name);

/* Cut and paste: the cells of range on sheet move by (drow, dcol) to
 * dsheet, as Excel moves them. The cells there before are replaced; the
 * range's cells left behind are empty. Formulas that move keep pointing
 * where they pointed (relative references do not move; a reference left
 * on the old sheet gets its sheet's name). Every reference in the book to
 * cells all inside the range follows them; a reference to a replaced cell
 * (not in the range) becomes #REF!. What reads either place is marked
 * dirty. OWF_OK, OWF_ERR_UNSUPPORTED (a sheet that is not there, or a
 * place off the sheet), OWF_ERR_MEMORY (the book may be half moved: the
 * next recalc parses everything again). */
int owf_calc_move_range(owf_calc *calc, int sheet, const owf_range *range, int dsheet, int drow, int dcol);

/* A formula's text (with or without its "=") as copied from (row, col) to
 * (row + drow, col + dcol), for copy and fill: relative references move,
 * $ parts stay; a reference moved off the sheet becomes #REF!. From
 * owf_malloc (owf_free it); NULL only when memory runs out. Text that
 * won't parse comes back as it was. */
char *owf_calc_move_formula(const char *text, int drow, int dcol);

/* Works out a formula (without its "=") as if it were in (sheet, row,
 * col), without changing the book: for a status line's sum of the
 * selection. result's text, if any, is from owf_malloc (owf_free it).
 * OWF_OK (the result may be an error value), OWF_ERR_FORMAT for a formula
 * that won't parse, OWF_ERR_MEMORY, OWF_ERR_UNSUPPORTED for a sheet that
 * is not there. */
int owf_calc_evaluate(owf_calc *calc, int sheet, unsigned row, unsigned col, const char *formula,
                      owf_cell *result);

/* "B12" for row 11, column 1; out holds at least 16 bytes. */
void owf_calc_cell_name(unsigned row, unsigned col, char *out);
/* "B12" or "$B$12" as row and column; 0 when the text is not a cell. */
int owf_calc_parse_cell(const char *text, unsigned *row, unsigned *col);

/* A cell's value as the grid shows it, through its number format:
 * "1,234.50", "25/10/2026", "45%", "#DIV/0!", "TRUE". out gets at most
 * size - 1 bytes and a NUL; returns the length. General numbers get up to
 * 15 significant digits here; owf_calc_format_value fits them to a width. */
int owf_calc_format(const owf_book *book, const owf_cell *cell, char *out, size_t size);
/* The same for a value (a cell's type, number, text and error) and a
 * format's text ("#,##0.00", "dd/mm/yyyy", "0.0%;[Red]-0.0%", "@").
 * width (0: no limit) is how many characters a General number may take,
 * as a column fits them: fewer decimals, then scientific, then "#"s (an
 * Excel column of the default width fits 11). colour, if not NULL, gets
 * the section's [Red], [Blue]... as 0xRRGGBB, else OWF_COLOUR_AUTO. */
int owf_calc_format_value(const char *format, const owf_cell *value, int date1904, int width,
                          char *out, size_t size, unsigned long *colour);

#ifdef __cplusplus
}
#endif

#endif
