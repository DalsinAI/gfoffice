/*
 * OpenCalc's portable core: the spreadsheet's editing model, as
 * openwrite_core.h is OpenWrite's. It owns a workbook (libowf's owf_book)
 * and its formula engine (owf_calc.h), and holds everything the Amiga
 * window would otherwise have to work out itself:
 *
 *   - the view: which rows and columns show at a zoom, from the sheet's
 *     column widths and row heights (twips), hidden rows and columns,
 *     frozen panes and the scroll position;
 *   - the active cell and the selection: ranges, whole rows and columns,
 *     several ranges at once;
 *   - keyboard navigation as Excel and LibreOffice do it;
 *   - cell editing (a caret in the cell or in the formula bar);
 *   - the commands: fill, insert and delete rows and columns, clear,
 *     cut, copy and paste, styles, column widths and row heights, sheets;
 *   - bounded undo and redo;
 *   - the grid as a list of things to draw (oc_draw_list), in pixels, so
 *     that a window only paints it.
 *
 * Nothing here knows the Amiga: it builds and is tested on the PC
 * (tests/calc_core). Plain C99, no floating point of its own (the engine's
 * doubles aside), memory bounded by the book and OC_UNDO_BUDGET.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OPENCALC_CORE_H
#define OPENCALC_CORE_H

#include <stddef.h>
#include "owf.h"
#include "owf_calc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_CORE_VERSION "0.1.0"

/* The sheet's size, as Excel 2007 and later. */
#define OC_MAX_ROWS 1048576u
#define OC_MAX_COLS 16384u

/* Undo keeps at most OC_UNDO_LIMIT steps (OpenWrite keeps 16) and at most
 * OC_UNDO_BUDGET bytes of saved cells across undo and redo; the oldest
 * steps go first. A change bigger than the budget is made, and clears the
 * undo history (oc_core_undo_lost says so). */
#define OC_UNDO_LIMIT 16
#define OC_UNDO_BUDGET (1024L * 1024L)

/* At most this many ranges in a selection (Ctrl+click adds one). */
#define OC_MAX_RANGES 8

/* Commands that touch every cell of a range refuse ranges bigger than
 * this (a whole row or column counts only as far as the sheet is used). */
#define OC_MAX_RANGE_CELLS 262144L

typedef struct oc_core oc_core;

/* ---- The core and its book ---- */

/* A core for a book, which it then owns (frees). NULL: a new book with one
 * sheet, "Sheet1". NULL when memory runs out (the book is freed). */
oc_core *oc_core_new(owf_book *book);
void oc_core_free(oc_core *core);
owf_book *oc_core_book(const oc_core *core);
owf_calc *oc_core_calc(const oc_core *core);
/* Replaces the book (the old one is freed): the cursor goes to A1, undo
 * is cleared, the book is clean. OWF_OK or OWF_ERR_MEMORY (then nothing
 * changed and the new book is freed). NULL: a new book. */
int oc_core_set_book(oc_core *core, owf_book *book);

/* Opens a workbook file (format NULL: from its contents) in place of the
 * book. OWF_OK or libowf's error; report and used as owf_import_book_file. */
int oc_core_open(oc_core *core, const char *path, const char *format, owf_report *report,
                 const owf_book_format **used);
/* Saves the book (format NULL: by the file name's extension). A format that
 * holds one sheet (CSV) gets the sheet showing. Marks the book saved on
 * OWF_OK. */
int oc_core_save(oc_core *core, const char *path, const char *format, owf_report *report);

/* The same, as a copy (Export): the book stays as unsaved as it was. */
int oc_core_export(oc_core *core, const char *path, const char *format, owf_report *report);

int oc_core_is_dirty(const oc_core *core);
void oc_core_mark_saved(oc_core *core);
/* Marks the book changed (a recovered copy that must not be lost unasked). */
void oc_core_set_dirty(oc_core *core);
/* Counts every change to the book (an edit, undo, redo, a style). */
unsigned long oc_core_changes(const oc_core *core);

/* ---- Sheets ---- */

int oc_core_sheet(const oc_core *core);             /* the sheet showing */
int oc_core_set_sheet(oc_core *core, int sheet);    /* OWF_OK or OWF_ERR_UNSUPPORTED */
/* Adds a sheet after the one showing and shows it; name NULL: "SheetN".
 * The new sheet's index, or -1 when memory runs out. Undoable. */
int oc_core_add_sheet(oc_core *core, const char *name);
/* Renames a sheet; formulas and names that refer to it follow. OWF_OK,
 * OWF_ERR_FORMAT (an empty name, one with []:*?/\ or one another sheet
 * has) or OWF_ERR_MEMORY. Undoable. */
int oc_core_rename_sheet(oc_core *core, int sheet, const char *name);
/* Deletes a sheet (never the last one). Undoable. */
int oc_core_delete_sheet(oc_core *core, int sheet);

/* ---- Names of cells ---- */

/* "A", "Z", "AA", "XFD"; out holds at least 4 bytes. */
void oc_col_name(unsigned col, char *out);
/* "B12"; out holds at least 16 bytes. */
void oc_cell_name(unsigned row, unsigned col, char *out);
/* "B12", "$B$12", "B2:D8", "B:D" (columns), "3:5" (rows): 1 and the range,
 * or 0. */
int oc_parse_range(const char *text, owf_range *range);

/* ---- The active cell and the selection ---- */

typedef struct {
    unsigned row, col;                  /* the active cell */
    int nranges;                        /* 1 or more */
    owf_range ranges[OC_MAX_RANGES];    /* the last is the one the keys extend */
    unsigned anchor_row, anchor_col;    /* the last range's fixed corner */
    unsigned focus_row, focus_col;      /* and the corner that moves */
} oc_selection;

void oc_core_selection(const oc_core *core, oc_selection *out);
void oc_core_active(const oc_core *core, unsigned *row, unsigned *col);
/* Makes (row, col) the active cell and the selection that one cell
 * (extend: from the anchor to it; add: a new range beside the others). */
void oc_core_select_cell(oc_core *core, unsigned row, unsigned col, int extend, int add);
void oc_core_select_range(oc_core *core, const owf_range *range);
/* Whole rows or columns: those the selection crosses. */
void oc_core_select_rows(oc_core *core);
void oc_core_select_cols(oc_core *core);
void oc_core_select_all(oc_core *core);
/* The name box: "B12", "A1:C5", "C:C"... OWF_OK or OWF_ERR_FORMAT. */
int oc_core_goto(oc_core *core, const char *name);
/* The name box's text: "B12", or "B2:D8" while a range is selected. */
void oc_core_name_box(const oc_core *core, char *out, size_t size);

/* ---- Keys ----
 * Keys as the window gets them, mapped to these. Not editing: the arrows,
 * Tab, Return, Home, End, PageUp and PageDown move as Excel's do (Shift
 * extends the selection; Ctrl+arrow goes to the edge of the data; Tab and
 * Return move inside a selection of more than one cell; Return after Tabs
 * goes back to the column the Tabs began in); Delete clears the selection;
 * Backspace clears the active cell and starts editing it; F2 edits the
 * active cell; Ctrl+PageUp/PageDown change sheet; Alt+PageUp/PageDown
 * scroll a screen sideways. Editing: as oc_edit_key. */
enum {
    OC_KEY_LEFT = 1, OC_KEY_RIGHT, OC_KEY_UP, OC_KEY_DOWN,
    OC_KEY_HOME, OC_KEY_END, OC_KEY_PAGEUP, OC_KEY_PAGEDOWN,
    OC_KEY_TAB, OC_KEY_RETURN, OC_KEY_ESCAPE, OC_KEY_BACKSPACE, OC_KEY_DELETE,
    OC_KEY_F2
};
enum { OC_MOD_SHIFT = 1, OC_MOD_CTRL = 2, OC_MOD_ALT = 4 };
/* OWF_OK when the key did something (redraw), OWF_ERR_UNSUPPORTED when it
 * means nothing here, or the error of the edit it committed. */
int oc_core_key(oc_core *core, int key, unsigned mods);

/* ---- Editing a cell ----
 * Typing starts editing the active cell, replacing what it holds ("enter"
 * mode: the arrows commit and move, as Excel's do; in a formula after an
 * operator they point at a cell and type its name). F2 or a double click
 * edits what the cell holds ("edit" mode: the arrows move the caret); F2
 * again changes mode. Return commits (and moves down), Tab commits (and
 * moves right), Escape cancels. What is committed goes through
 * owf_calc_enter, then owf_calc_recalc. */
enum { OC_EDIT_NONE, OC_EDIT_ENTER, OC_EDIT_EDIT };
int oc_core_editing(const oc_core *core);
/* Starts editing the active cell: keep its content, or start empty. */
int oc_edit_begin(oc_core *core, int keep);
/* Types text (UTF-8) at the caret, starting to edit (empty) first if need
 * be. */
int oc_edit_type(oc_core *core, const char *utf8, size_t length);
/* The whole text and caret, as a formula bar's gadget has them; starts
 * editing (edit mode) if need be. */
int oc_edit_set(oc_core *core, const char *utf8, size_t caret);
/* The text being edited and the caret (a byte offset); NULL when not
 * editing. */
const char *oc_edit_text(const oc_core *core, size_t *caret);
int oc_edit_key(oc_core *core, int key, unsigned mods);
/* Ends editing: puts the text in the cell, then moves the active cell
 * (OC_MOVE_...). OC_MOVE_FILL puts it in every cell of the selection
 * (Ctrl+Return), a formula moved for each. OWF_OK, OWF_ERR_FORMAT (the
 * formula won't parse: it is kept and shows #NAME?) or OWF_ERR_MEMORY. */
enum { OC_MOVE_NONE, OC_MOVE_DOWN, OC_MOVE_UP, OC_MOVE_RIGHT, OC_MOVE_LEFT, OC_MOVE_FILL };
int oc_edit_commit(oc_core *core, int move);
void oc_edit_cancel(oc_core *core);
/* While editing a formula: 1 when a click on a cell should type its name
 * (the caret follows an operator, "(" or ","), else the click commits. */
int oc_edit_wants_ref(const oc_core *core);
/* Types a reference to the cell (or the range, when it is more than one). */
int oc_edit_insert_ref(oc_core *core, const owf_range *range);

/* The formula bar's text: what is being typed, or what the active cell
 * holds as one would type it ("=SUM(A1:A3)", "12.5", "'0123"). The
 * pointer is good until the next call into the core. */
const char *oc_core_input_text(oc_core *core);
/* What a cell holds as one would type it (owf_malloc'd; NULL: memory). */
char *oc_core_cell_input(const oc_core *core, int sheet, unsigned row, unsigned col);
/* A cell's value as the grid shows it, through its number format. */
int oc_core_cell_text(const oc_core *core, int sheet, unsigned row, unsigned col, char *out, size_t size);
/* Puts typed input in a cell, as editing would (undoable): for ARexx. */
int oc_core_set_input(oc_core *core, int sheet, unsigned row, unsigned col, const char *input);

/* ---- Commands on the selection (each one undo step) ---- */

/* Fill down (the top row of each range into the rest; one cell: from the
 * cell above) or right (the left column; one cell: from the left). */
enum { OC_FILL_DOWN, OC_FILL_RIGHT };
int oc_core_fill(oc_core *core, int direction);
/* Inserts as many rows (axis 0) or columns (axis 1) as the selection
 * crosses, before it. OWF_ERR_FORMAT when cells would fall off the sheet. */
int oc_core_insert(oc_core *core, int axis);
/* Deletes the rows or columns the selection crosses. */
int oc_core_delete(oc_core *core, int axis);
/* Clears the selection's values and formulas, its styles, or both. */
enum { OC_CLEAR_CONTENTS = 1, OC_CLEAR_FORMATS = 2, OC_CLEAR_ALL = 3 };
int oc_core_clear(oc_core *core, unsigned what);

/* ---- Clipboard ----
 * Copy keeps the selection (its first range) inside the core, with
 * formulas and styles, and gives the text for the system clipboard: the
 * cells as the grid shows them, a tab between them and a newline after
 * each row (a value with a tab, newline or quote in it quoted as Excel
 * quotes it). Paste is given the system clipboard's text: when it is still
 * the text of the core's last copy, the cells are pasted with their
 * formulas (moved) and styles; otherwise the text is read as tab separated
 * values, each one as typed input. A paste into a selection that is a
 * whole multiple of what was copied fills it. Cut is a copy that clears
 * the cells (one undo step); its paste keeps formulas as they were. */
char *oc_core_copy(oc_core *core, size_t *length);   /* owf_malloc'd text, NULL: nothing / memory */
char *oc_core_cut(oc_core *core, size_t *length);
int oc_core_paste(oc_core *core, const char *text, size_t length);
/* The selection's cells as tab separated text, without copying. */
char *oc_core_selection_text(const oc_core *core, size_t *length);

/* The Amiga clipboard's FTXT holds ISO-8859-1 text (CHRS), the core UTF-8:
 * UTF-8 to Latin-1 ('?' for a character Latin-1 hasn't, or a broken
 * sequence) and back. owf_malloc'd, NUL-ended; NULL when memory runs out. */
char *oc_text_to_latin1(const char *utf8, size_t length, size_t *out_length);
char *oc_text_from_latin1(const char *latin1, size_t length, size_t *out_length);

/* ---- Styles of the selection (each one undo step) ---- */

/* The active cell's style. */
void oc_core_current_style(const oc_core *core, owf_cellstyle *out);
/* Turns OWF_BOLD, OWF_ITALIC or OWF_UNDERLINE on for the selection, or
 * off when the active cell already has it. */
int oc_core_toggle_flags(oc_core *core, unsigned flags);
int oc_core_set_halign(oc_core *core, int halign);   /* OWF_HALIGN_... */
int oc_core_set_valign(oc_core *core, int valign);
/* A number format's text ("0.00", "#,##0", "0%", "dd/mm/yyyy"); "General". */
int oc_core_set_number_format(oc_core *core, const char *format);
/* OWF_SHADE(0xRRGGBB) behind the cells, or 0 for none. */
int oc_core_set_fill(oc_core *core, unsigned long fill);
int oc_core_set_font_colour(oc_core *core, unsigned long rgb);   /* or OWF_COLOUR_AUTO */
/* Borders: line OWF_LINE_... (NONE takes them away) round each cell
 * (OC_BORDERS_ALL), round the range (OC_BORDERS_OUTLINE) or below it. */
enum { OC_BORDERS_ALL, OC_BORDERS_OUTLINE, OC_BORDERS_BOTTOM, OC_BORDERS_NONE };
int oc_core_set_borders(oc_core *core, int where, int line, unsigned long colour);
/* The formats a Format menu offers (NULL-terminated), and their names. */
extern const char *const oc_number_formats[];
extern const char *const oc_number_format_names[];

/* ---- Column widths and row heights (undoable) ---- */

/* In twips; 0: the sheet's default. Hidden ones are 0. */
int oc_core_col_width(const oc_core *core, unsigned col);
int oc_core_row_height(const oc_core *core, unsigned row);
/* For the columns (rows) the selection crosses; twips 0: the default. */
int oc_core_set_col_width(oc_core *core, int twips);
int oc_core_set_row_height(oc_core *core, int twips);
/* For given columns (a header's edge dragged). */
int oc_core_set_cols_width(oc_core *core, unsigned first, unsigned last, int twips);
int oc_core_set_rows_height(oc_core *core, unsigned first, unsigned last, int twips);
/* Hides (1) or shows (0) the columns or rows the selection crosses. */
int oc_core_hide(oc_core *core, int axis, int hide);
/* Freezes the rows above and the columns left of the active cell (A1:
 * unfreezes). */
int oc_core_freeze(oc_core *core);

/* ---- Undo ---- */

int oc_core_can_undo(const oc_core *core);
int oc_core_can_redo(const oc_core *core);
int oc_core_undo(oc_core *core);    /* OWF_OK, or OWF_ERR_UNSUPPORTED when there is nothing */
int oc_core_redo(oc_core *core);
/* 1 when the last change could not be kept for undo (too big). */
int oc_core_undo_lost(const oc_core *core);

/* ---- The view ----
 * Pixels: the grid area's top left (the corner above the row numbers) is
 * 0,0. At 100% a pixel is 15 twips (96 dpi), as OpenWrite's pages. */

/* Measures text in pixels at the view's zoom, in a cell's font (flags
 * OWF_BOLD..., font the book's font index or -1, size in twips, 0: the
 * default 10 points). Without one, a character is 7 pixels at 100%. */
typedef int (*oc_measure_fn)(void *user, const char *utf8, size_t length, unsigned flags, int font, int size);
void oc_view_set_measure(oc_core *core, oc_measure_fn measure, void *user);
/* A cell font's size in pixels at the view's zoom (twips 0: the default,
 * 10 points): its em, which a window opens the font at, to measure and
 * draw a cell's text. At 100% 11 points is 15 pixels (96 dpi). Lines of
 * wrapped text are oc_line_px apart: 1.15 em, as Arial's and Calibri's
 * lines (11 points: 17 pixels). */
#define OC_DEFAULT_FONT_TWIPS 200
int oc_font_px(const oc_core *core, int twips);
int oc_line_px(const oc_core *core, int twips);
/* The size to make a face at for a cell font of em pixels, so that its
 * capitals are 0.716 em high (Arial's: 11 at 15 pixels; Amiga fonts are
 * made at a size of their own reckoning): cap_at(user, size) is the
 * capitals' height the face has made at a size (0: it can't be made).
 * Tried at pixels, then in proportion, then a size at a time toward the
 * height from the nearest yet, up to OC_CAP_TRIES sizes after the first,
 * none twice; the nearest is chosen (the first of equals). *cap: its
 * capitals' height. */
#define OC_CAP_TRIES 4
int oc_font_size_for_caps(int pixels, int (*cap_at)(void *user, int size), void *user, int *cap);
/* The grid area's size, and the row numbers' width and the column
 * letters' height (a window sets these from its font). */
void oc_view_set_size(oc_core *core, int width, int height);
void oc_view_set_headers(oc_core *core, int row_header_width, int col_header_height);
/* The row numbers' bar following the rows showing (Dale, 25 October 2026): as wide as the largest row number
 * showing (at least two digits) at digit_width pixels a digit, plus pad pixels, the numbers left-aligned
 * pad / 2 in. It widens as the view reaches rows of more digits and narrows again, so it changes only where
 * the digits do; the columns, the frozen panes' split and what is under the pointer follow it. digit_width 0:
 * the width oc_view_set_headers gave, the numbers centred. */
void oc_view_set_row_numbers(oc_core *core, int digit_width, int pad);
/* The row numbers' bar's width now. */
int oc_view_row_header(const oc_core *core);
void oc_view_set_zoom(oc_core *core, int percent);   /* 25 to 400 */
int oc_view_zoom(const oc_core *core);
/* The first row and column of the scrolling part (after frozen ones). */
void oc_view_scroll_pos(const oc_core *core, unsigned *top, unsigned *left);
void oc_view_scroll_to(oc_core *core, unsigned top, unsigned left);
/* For a scroller: the first row (axis 0) or column showing, how many
 * show, and a total that grows past the used part of the sheet. */
void oc_view_scroller(oc_core *core, int axis, unsigned *first, unsigned *visible, unsigned *total);
/* Scrolls so the cell shows. */
void oc_view_show_cell(oc_core *core, unsigned row, unsigned col);

/* What is at a point. */
enum {
    OC_HIT_NONE, OC_HIT_CELL, OC_HIT_COL_HEADER, OC_HIT_ROW_HEADER, OC_HIT_CORNER,
    OC_HIT_COL_EDGE,        /* a column letter's right edge: drag to size it */
    OC_HIT_ROW_EDGE
};
typedef struct {
    int kind;
    unsigned row, col;      /* for an edge, the column (row) whose edge it is */
    int x, y, w, h;         /* the cell's (header's) box */
} oc_hit;
int oc_view_hit(const oc_core *core, int x, int y, oc_hit *hit);
/* A click: press (extend: Shift; add: Ctrl), then drags as the pointer moves.
 * Editing, a click commits first, or types the cell's name in a formula
 * (oc_edit_wants_ref). Clicks on headers select rows and columns. */
int oc_core_press(oc_core *core, const oc_hit *hit, int extend, int add);
int oc_core_drag(oc_core *core, const oc_hit *hit);
/* Pixels to twips at the view's zoom (a dragged edge's width). */
int oc_view_px_to_twips(const oc_core *core, int px);

/* ---- The grid as things to draw ----
 * In the order they are painted, clipped to the grid area. Colours are
 * either the book's (rgb, 0xRRGGBB) or a role the window gives its
 * theme's colour. */
enum {
    OC_DRAW_FILL,       /* a box: role, rgb for OC_ROLE_CELL_FILL */
    OC_DRAW_LINE,       /* from x,y, w or h long (the other is 0): role, line, rgb for OC_ROLE_BORDER */
    OC_DRAW_TEXT,       /* text in its clip box, from text_x; valign in the box y,h; flags, font, size, rgb */
    OC_DRAW_HEADER,     /* a row number or column letter in its box, centred (align OC_ALIGN_LEFT: from text_x); selected or not */
    OC_DRAW_FRAME,      /* a box's outline, `line` pixels thick: the active cell, the selection */
    OC_DRAW_CARET,      /* the caret: a line from x,y, h long */
    OC_DRAW_MARK        /* a comment's mark: a triangle filling the top right half of the box x,y,w,h */
};
enum {
    OC_ROLE_PAPER,          /* the empty sheet (the window fills the area with it first) */
    OC_ROLE_CELL_FILL,      /* a cell's own fill: rgb */
    OC_ROLE_SELECTION,      /* selected cells */
    OC_ROLE_GRID,           /* the grid lines */
    OC_ROLE_BORDER,         /* a cell's border: rgb (OWF_COLOUR_AUTO: black), line OWF_LINE_... */
    OC_ROLE_TEXT,           /* a cell's text: rgb, or OWF_COLOUR_AUTO */
    OC_ROLE_HEADER,         /* the row numbers' and column letters' boxes */
    OC_ROLE_HEADER_SELECTED,/* those the selection crosses */
    OC_ROLE_HEADER_LINE,
    OC_ROLE_CURSOR,         /* the active cell's frame, the caret */
    OC_ROLE_FREEZE,         /* the line under frozen rows, beside frozen columns */
    OC_ROLE_EDIT,           /* behind the text being typed */
    OC_ROLE_COMMENT,        /* a comment's mark (red) */
    OC_ROLE_NOTE,           /* behind the active cell's comment (pale yellow) */
    OC_ROLE_NOTE_LINE       /* its frame */
};
enum { OC_ALIGN_LEFT, OC_ALIGN_CENTRE, OC_ALIGN_RIGHT };

typedef struct {
    int kind, role;
    int x, y, w, h;
    int clip_x, clip_y, clip_w, clip_h;     /* TEXT */
    int text_x;                             /* TEXT: where it starts */
    int align, valign;                      /* TEXT: OC_ALIGN_..., OWF_VALIGN_... */
    unsigned flags;                         /* TEXT: OWF_BOLD...; HEADER: 1 selected */
    int font, size;                         /* TEXT: the book's font (-1 default), twips (0 default) */
    int line;                               /* LINE: OWF_LINE_...; FRAME: thickness */
    unsigned long rgb;
    unsigned row, col;                      /* the cell (or header) it belongs to */
    const char *text;                       /* TEXT, HEADER */
    size_t text_off;
} oc_draw_item;

typedef struct {
    int n, cap;
    oc_draw_item *items;
    char *pool;
    size_t pool_used, pool_cap;
} oc_draw_list;

void oc_draw_list_init(oc_draw_list *list);
void oc_draw_list_free(oc_draw_list *list);
/* The grid as it should show now (the list is emptied first). OWF_OK or
 * OWF_ERR_MEMORY. */
int oc_view_draw(oc_core *core, oc_draw_list *list);

/* Cell borders (OC_ROLE_BORDER lines): the edge two cells share is one
 * item, in the heavier of their two lines, and none is drawn inside a
 * merged range; a line along a row or down a column is one item. How to
 * draw a line OWF_LINE_...: its weight in pixels at any zoom (hair, thin,
 * dashed and dotted 1; the medium ones 2; thick 3; double 3, two 1-pixel
 * lines with one between) and its dashes, a 16-pixel pattern read from the
 * top bit (0xFFFF solid), as SetDrPt takes it. */
int oc_line_weight(int line);
unsigned oc_line_pattern(int line);

/* ---- Comments ----
 * A commented cell has a mark in its top right corner (OC_DRAW_MARK), and
 * the active cell's comment shows beside it, over the grid, as Excel shows
 * one under the pointer (not while typing); a cell without one shows its
 * data validation's input message there, when the file says to show it. */
const owf_comment *oc_core_comment(const oc_core *core, int sheet, unsigned row, unsigned col);

/* ---- The status line ---- */

/* "Sum=12 Average=4 Count=3" for a selection of more than one cell, a
 * circular reference's or a bad formula's warning; "" when nothing. */
void oc_core_status(oc_core *core, char *out, size_t size);

/* ---- The sheet tabs ----
 * Which sheets' tabs a strip shows. width[s] is sheet s's tab in pixels,
 * with the gap after it (0: no tab, a hidden sheet), avail the strip's width
 * for tabs, arrows the width of the two scroll arrows a strip shows when
 * not every tab fits. *first (in and out) is the first sheet shown: it moves
 * so that the current sheet's tab shows, and back when there is room, so
 * that the strip stays full. *scroll is 1 when the arrows show. The sheet
 * after the last tab shown (n when the last shows). At least one tab shows,
 * cut short when it is wider than the strip. */
int oc_tab_strip(const int *width, int n, int avail, int arrows, int current, int *first, int *scroll);

#ifdef __cplusplus
}
#endif

#endif
