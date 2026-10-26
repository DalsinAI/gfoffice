/*
 * OpenCalc's core: what its files share (not for the window).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OC_INTERNAL_H
#define OC_INTERNAL_H

#include "opencalc_core.h"

/* Where a sheet was left: the cursor, the selection, the scroll. */
typedef struct {
    oc_selection sel;
    unsigned top, left;             /* the scrolling part's first row and column */
    long tab_col;                   /* Tab then Return: the column the Tabs began in; -1 */
} oc_sheetstate;

/* Cells saved for undo: those of a range of one sheet (copies). */
typedef struct {
    int sheet;
    owf_range range;
    int ncells;
    owf_cell *cells;
    int ncols;                      /* columns' styles for whole columns: the sheet's column list */
    owf_colinfo *cols;
    int has_cols;                   /* OC_SAVE_COLS, OC_SAVE_ROWS */
    int nrows;                      /* rows' styles for whole rows: the sheet's row list */
    owf_rowinfo *rows;
} oc_cellsave;
enum { OC_SAVE_COLS = 1, OC_SAVE_ROWS = 2 };
#define OC_MAX_STYLED_ROWS 65536U        /* rows formatted whole at once (more: their cells only) */

enum { OC_UNDO_CELLS, OC_UNDO_LAYOUT, OC_UNDO_BOOK, OC_UNDO_SHIFT };

typedef struct oc_undo {
    int kind;
    int sheet;                      /* the sheet showing before */
    oc_sheetstate state;            /* and its cursor */
    int dirty;
    long bytes;
    /* CELLS */
    int nsaves;
    oc_cellsave *saves;
    /* LAYOUT: a sheet's column and row lists and frozen panes */
    int lsheet;
    int ncols, nrows;
    owf_colinfo *cols;
    owf_rowinfo *rows;
    unsigned freeze_rows, freeze_cols;
    /* SHIFT: rows or columns to insert (count > 0) or delete to go back
       (the step of an insert: what went in is empty again when it is undone) */
    int shift_sheet, shift_axis, shift_count;
    unsigned shift_at;
    /* BOOK: all of it, and each sheet's cursor */
    owf_book *book;
    oc_sheetstate *states;
    int nstates;
} oc_undo;

/* The text a copy left, with its cells. */
typedef struct {
    int valid;
    int cut;                        /* a cut: the paste moves the cells (owf_calc_move_range) */
    int cut_sheet;                  /* where from */
    unsigned long cut_changes;      /* the book as it was then (a change since: pasted as a copy) */
    unsigned rows, cols;            /* its size */
    unsigned row0, col0;            /* where it came from */
    int ncells;
    owf_cell *cells;                /* row and col relative to row0, col0 */
    owf_cellstyle *styles;          /* each cell's style, by value (books differ) */
    char *text;                     /* what went to the system clipboard */
    size_t length;
} oc_clip;

/* A visible row or column: which, where (pixels) and how big. */
typedef struct {
    unsigned index;
    int pos, size;
    int pane;                       /* 0 frozen, 1 scrolling */
} oc_line;

/* The look conditional formatting gives a cell showing (oc_condfmt.c): which parts it sets. */
enum { OC_LOOK_FILL = 1, OC_LOOK_COLOUR = 2 };
typedef struct {
    unsigned long fill, colour;     /* 0xRRGGBB, for OC_LOOK_FILL, OC_LOOK_COLOUR */
    unsigned set, flags;            /* OWF_BOLD...: which it sets, and to what */
    unsigned char parts, stop;
} oc_look;

struct oc_core {
    owf_book *book;
    owf_calc *calc;
    int sheet;
    int nstates;
    oc_sheetstate *states;

    /* editing */
    int edit_mode;                  /* OC_EDIT_... */
    char *edit;
    size_t edit_len, edit_cap, caret;
    int point;                      /* typing a reference by the arrows */
    size_t point_start, point_end;
    unsigned point_row, point_col;
    char *input;                    /* the formula bar's text when not editing */

    /* the view */
    int width, height, rowhdr, colhdr, zoom;
    int rn_digit, rn_pad;           /* the row numbers' bar follows them: a digit's width, the space beside them */
    oc_measure_fn measure;
    void *measure_user;
    int dragging;                   /* 0, or OC_HIT_CELL / _ROW_HEADER / _COL_HEADER being dragged */

    /* undo */
    oc_undo *undo[OC_UNDO_LIMIT];
    int nundo;
    oc_undo *redo[OC_UNDO_LIMIT];
    int nredo;
    int undo_lost;

    oc_clip clip;

    int dirty;
    unsigned long changes;

    /* conditional formatting's looks for the cells showing (oc_cf_looks), kept while nothing changes */
    oc_look *cf_looks;
    unsigned long cf_stamp, cf_hash;
    int cf_sheet, cf_nr, cf_nc;

    /* the charts of the sheet showing (oc_core_charts), read once from its drawing, by the drawing's name */
    owf_chart *charts;
    int ncharts;
    char *charts_of;

    /* the used part of the sheet showing, for the scrollers: worked out again when changes moves */
    unsigned long used_stamp;
    int used_sheet, used_valid;
    unsigned used_rows, used_cols;
};

/* oc_core.c */
owf_sheet *oc_sheet(const oc_core *core);
oc_sheetstate *oc_state(oc_core *core);
const oc_sheetstate *oc_state_const(const oc_core *core);
int oc_cell_empty(const owf_cell *cell);
const owf_cell *oc_find(const oc_core *core, int sheet, unsigned row, unsigned col);
void oc_used_range(const owf_sheet *sheet, unsigned *rows, unsigned *cols);
int oc_range_clip(const oc_core *core, const owf_range *in, owf_range *out);
int oc_put_cell(oc_core *core, int sheet, unsigned row, unsigned col, const owf_cell *src, const char *formula,
                const owf_cellstyle *style, int *rebuild);
int oc_recalc(oc_core *core, int rebuild);
void oc_changed(oc_core *core);
void oc_collapse_to(oc_core *core, unsigned row, unsigned col);
char *oc_strdup(const char *s);
int oc_nav_key(oc_core *core, int key, unsigned mods);
char *oc_cell_input(const oc_core *core, int sheet, unsigned row, unsigned col);
/* A cell's style: its own, else (no cell) its row's, else its column's. */
const owf_cellstyle *oc_cell_style(const oc_core *core, const owf_cell *cell, unsigned row, unsigned col);
int oc_col_style(const owf_sheet *sheet, unsigned col);
int oc_row_style(const owf_sheet *sheet, unsigned row);
int oc_empty_style(const owf_sheet *sheet, unsigned row, unsigned col);
/* Text entered into a cell through the engine; a cell it makes takes its row's or column's style, as a cell
 * typed into in Excel does. */
int oc_enter(oc_core *core, int sheet, unsigned row, unsigned col, const char *text);

/* oc_undo.c */
oc_undo *oc_undo_new(oc_core *core, int kind);
/* with: OC_SAVE_COLS, OC_SAVE_ROWS (the column and row lists too) */
int oc_undo_save_range(oc_core *core, oc_undo *u, int sheet, const owf_range *range, int with);
int oc_undo_save_layout(oc_core *core, oc_undo *u, int sheet);
int oc_undo_save_book(oc_core *core, oc_undo *u);
void oc_undo_push(oc_core *core, oc_undo *u);
void oc_undo_free(oc_undo *u);
void oc_undo_clear(oc_core *core);
owf_book *oc_book_copy(const owf_book *book);
int oc_copy_cell(owf_cell *dst, const owf_cell *src);
void oc_free_cell(owf_cell *cell);
int oc_remove_cells(owf_sheet *sheet, const owf_range *range);

/* oc_view.c */
int oc_px(const oc_core *core, int twips);
int oc_col_twips(const owf_sheet *sheet, unsigned col);
int oc_row_twips(const owf_sheet *sheet, unsigned row);
int oc_rows_showing(const oc_core *core, int whole);
int oc_cols_showing(const oc_core *core, int whole);
void oc_view_follow(oc_core *core);
int oc_measure(const oc_core *core, const char *s, size_t n, const owf_cellstyle *style);
const owf_range *oc_merge_at(const owf_sheet *sheet, unsigned row, unsigned col);
oc_draw_item *oc_draw_add(oc_draw_list *list, int kind, int role);
int oc_draw_add_text(oc_draw_list *list, oc_draw_item *it, const char *s, size_t n);
int oc_draw_add_points(oc_draw_list *list, oc_draw_item *it, const int *xy, int n);

/* oc_chart.c: the sheet showing's charts (read once; NULL, 0 when none), forgotten when the book goes; a
 * chart's items in its box x,y,w,h, cut to the clip box (grid pixels). 0 when memory ran out. */
const owf_chart *oc_core_charts(oc_core *core, int *count);
void oc_charts_forget(oc_core *core);
int oc_chart_draw(oc_core *core, oc_draw_list *list, const owf_chart *chart, int x, int y, int w, int h, int cx, int cy,
                  int cw, int ch);

/* oc_condfmt.c: the looks of the cells showing (rows rl, columns cl), nr x nc, the core's own (good until
 * the next call); NULL when the sheet has no conditional formats (or memory ran out). */
const oc_look *oc_cf_looks(oc_core *core, const oc_line *rl, int nr, const oc_line *cl, int nc);
void oc_cf_forget(oc_core *core);

/* oc_clip.c */
void oc_clip_free(oc_clip *clip);

/* oc_edit.c */
void oc_edit_free(oc_core *core);

#endif
