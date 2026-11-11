/*
 * GFOpenPresentation's portable core (src/deck), as openwrite_core.h is
 * GFOpenWrite's and opencalc_core.h GFOpenCalc's: what the Amiga program, the
 * show and OWConvert share, built and tested on the PC.
 *
 * Phase P1 (docs/gfoffice/Design-OpenPresentation.md, 12) is here:
 *
 *   - a slide laid out by GFOpenLayout: one ol_document a slide, one page the
 *     slide's size, a frame (OL_BOX_ABSOLUTE) for the background and for
 *     each object, back to front; a frame's text made by GFOpenWrite's
 *     paragraph code (ow_layout_add_story, Shared E5), with each run's font,
 *     size and colour from its level when it has none, and its list's
 *     bullet or number in front;
 *   - text anchored to the middle or bottom of its box, and text that
 *     shrinks to fit (up to four times by 90%), by laying out again
 *     (GFOpenLayout 0.2 has neither: section 5.2 and 5.3);
 *   - the deck as a PDF, one page a slide, drawn from that display list
 *     through the page PDF writer (owf_pdf.h); OpenLayout 13 carries the
 *     shared shape paths, styles, arrows and picture crops used by both the
 *     screen and PDF renderers.
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENPRESENTATION_CORE_H
#define OPENPRESENTATION_CORE_H

#include "owf_deck.h"
#include "gfopenlayout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPR_CORE_VERSION "0.33.0"

/* One frame of a laid-out slide: the background (obj NULL), a master's
 * object, or the slide's. */
typedef struct {
    ol_id node;                  /* the frame's block */
    const owf_obj *obj;
    int from_master;
    owf_obj box;                 /* obj with its box resolved (owf_deck_resolve) */
    int scale;                   /* percent its text was laid out at (shrink to fit) */
    ol_id first, end;            /* the nodes made for its content */
    int text_overflow;           /* cached: -1 unknown, 0 fits, 1 overset */
} opr_frame;

typedef struct {
    ol_document *doc;            /* GFOpenLayout units; page 0 is the slide */
    const owf_deck *deck;
    int slide;
    int nframes;
    opr_frame *frames;           /* drawing order */
    int overflow;                /* something reached below the slide's bottom edge */
    int notes;                   /* 1: the slide's notes page (opr_notes_layout), page 0 its size */
} opr_slide;

/* The slide laid out. measure NULL: the PDF's standard fonts' widths
 * (opr_measure). today: the text a date field shows (NULL: today's, from
 * the C library's clock). NULL when memory runs out. */
opr_slide *opr_slide_layout(const owf_deck *deck, int slide, ol_measure_text_fn measure, void *userdata,
                            const char *today);
void opr_slide_free(opr_slide *s);

/* The frame a display op belongs to, or -1. */
int opr_slide_frame_of(const opr_slide *s, const ol_display_op *op);

/* Measures text with the PDF's standard fonts (owf_pdf_text_width), for
 * when there is no screen (OWConvert, the tests): userdata is the deck. */
int opr_measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m);
/* What kind of face a deck's font is: its own kind, else by its name. */
owf_font_kind opr_font_kind(const owf_deck *deck, int font);

typedef enum {
    OPR_PDF_SLIDES,              /* a page a slide, at the slide's size */
    OPR_PDF_NOTES,               /* a notes page a slide: the slide above its notes */
    OPR_PDF_HANDOUTS             /* handouts: handout_slides slides to a page */
} opr_pdf_pages;

typedef struct {
    int with_hidden;             /* hidden slides too */
    const char *today;           /* the date fields' text; NULL: today's */
    opr_pdf_pages pages;         /* 0: the slides */
    int handout_slides;          /* OPR_PDF_HANDOUTS: 1, 2, 3, 4, 6 or 9 (others: 6) */
} opr_pdf_options;

/* The deck as a PDF: by options->pages, one page a slide at the slide's
 * size, a notes page a slide, or handouts, both on the deck's notes page
 * (deck->notes_page; A4 when it has no size). options may be NULL.
 * OWF_OK, or OWF_ERR_MEMORY. */
int opr_export_pdf(const owf_deck *deck, const opr_pdf_options *options, unsigned char **data, size_t *length,
                   owf_report *report);

/* ---- Editing a text frame's text (P3's first part, opr_edit.c) ----
 *
 * A position is a paragraph of an object's text and a byte offset into the
 * paragraph's text as opr_para_text gives it: its text runs' UTF-8, a tab
 * as "\t", a line break as "\v"; a field or a picture counts for nothing.
 * Text typed goes into the text run at the position, so it takes that
 * run's look (the run before, at a run's end). An object showing its
 * placeholder's prompt (OWF_OBJ_EMPTY) gets a paragraph of its own at the
 * first change. The calls return 0, or -1 when memory runs out or the
 * object is not there; the position follows the change. */
typedef struct {
    int para;
    size_t offset;
} opr_pos;

/* A text selection keeps its fixed anchor and moving focus in the same
 * editable object. Positions may be in either order. */
typedef struct {
    opr_pos anchor;
    opr_pos focus;
} opr_selection;

typedef enum {
    OPR_MOVE_LEFT, OPR_MOVE_RIGHT,           /* a character, across paragraphs */
    OPR_MOVE_PARA_START, OPR_MOVE_PARA_END,
    OPR_MOVE_TEXT_START, OPR_MOVE_TEXT_END
} opr_move;

/* 1 for an object whose text can be typed in: a text frame, a shape (not a
 * line); not a table, picture, group or hidden object yet. */
int opr_obj_editable(const owf_deck *deck, int slide, int obj);
size_t opr_para_length(const owf_para *p);
/* From owf_malloc; NULL when memory runs out. */
char *opr_para_text(const owf_para *p, size_t *length);
/* The object's text: paragraphs between "\n". From owf_malloc. */
char *opr_text_get(const owf_deck *deck, int slide, int obj);
/* utf8 at *at: "\n" (or "\r") starts a new paragraph, "\t" is a tab, "\v" a
 * line break. */
int opr_text_insert(owf_deck *deck, int slide, int obj, opr_pos *at, const char *utf8, size_t length);
/* The paragraph split at *at; the new one keeps its format and list level. */
int opr_text_newline(owf_deck *deck, int slide, int obj, opr_pos *at);
/* The character before *at; at a paragraph's start, the paragraph joined to
 * the one before. */
int opr_text_backspace(owf_deck *deck, int slide, int obj, opr_pos *at);
/* The character after *at; at a paragraph's end, the next one joined on. */
int opr_text_delete(owf_deck *deck, int slide, int obj, opr_pos *at);
int opr_text_move(const owf_deck *deck, int slide, int obj, opr_pos *at, opr_move how);
int opr_text_selection_empty(const opr_selection *selection);
/* Exact selected plain text, with '\n' between paragraphs. The returned
 * UTF-8 buffer is owf_malloc-owned; an empty selection returns an empty
 * buffer. NULL means invalid input or memory failure. */
char *opr_text_selection_get(const owf_deck *deck, int slide, int obj,
                             const opr_selection *selection, size_t *length);
typedef struct {
    int slide, obj;
    opr_selection selection;
} opr_find_result;
/* Find the first occurrence at or after start, wrapping once through all
 * editable objects and slides. Search does not cross paragraph separators.
 * match_case 0 folds ASCII letters while retaining exact UTF-8 bytes for the
 * rest. Returns 1 found, 0 absent, or -1 for invalid input/memory failure. */
int opr_text_find(const owf_deck *deck, int start_slide, int start_obj,
                  opr_pos start, const char *needle, int match_case,
                  opr_find_result *result);
/* Delete exactly the selected characters and paragraph separators. Both ends
 * collapse to the start. The caller owns the Undo snapshot, as for typing. */
int opr_text_delete_selection(owf_deck *deck, int slide, int obj,
                              opr_selection *selection);
/* The whole text replaced: the first paragraph's format, level and first
 * run's look kept for all of it. */
int opr_text_set(owf_deck *deck, int slide, int obj, const char *utf8);

/* Undo: up to 16 text or slide-structure changes. Text editing takes a copy
 * before each change with opr_undo_push. The slide commands below perform the
 * change and add its undo step themselves. opr_undo_undo and opr_undo_redo put
 * a state back and say which slide to select; obj is -1 after a structural
 * change. */
typedef struct opr_undo opr_undo;
opr_undo *opr_undo_new(void);
void opr_undo_free(opr_undo *u);
void opr_undo_clear(opr_undo *u);
int opr_undo_push(opr_undo *u, const owf_deck *deck, int slide, int obj);
/* Replace every non-overlapping match in one editable text object. The whole
 * operation is one Undo step. match_case 0 folds ASCII letters, as Find does;
 * replacement may be empty. Returns -2 for invalid input, -1 for memory
 * failure, or 0 (including no matches). */
int opr_text_replace_all(opr_undo *u, owf_deck *deck, int slide, int obj,
                         const char *needle, const char *replacement,
                         int match_case, int *replacements, opr_pos *after);
/* Rich-text formatting over an inclusive paragraph range. These commands
 * share the text snapshot Undo used by typing and return -2 for an invalid
 * object/range/value, -1 for memory failure, or 0. Toggle sets the requested
 * flags on the whole range unless every run already has them, then clears
 * them. Size is in twips; alignment is OWF_ALIGN_*. */
int opr_text_toggle_flags(opr_undo *u, owf_deck *deck, int slide, int obj,
                          int first_para, int last_para, unsigned flags);
int opr_text_set_size(opr_undo *u, owf_deck *deck, int slide, int obj,
                      int first_para, int last_para, int twips);
int opr_text_set_colour(opr_undo *u, owf_deck *deck, int slide, int obj,
                        int first_para, int last_para, unsigned long colour);
int opr_text_set_font(opr_undo *u, owf_deck *deck, int slide, int obj,
                      int first_para, int last_para, const char *family);
int opr_text_set_alignment(opr_undo *u, owf_deck *deck, int slide, int obj,
                           int first_para, int last_para, owf_align alignment);
/* Paragraph indents in twips: first is relative to left, as in owf_parafmt.
 * Left and right must be non-negative; a hanging first line may be negative
 * down to -left. */
int opr_text_set_indents(opr_undo *u, owf_deck *deck, int slide, int obj,
                         int first_para, int last_para,
                         int left, int first, int right);
/* Character formatting over an exact, non-empty selection. Paragraph
 * alignment still uses opr_text_set_alignment because it is paragraph state. */
int opr_text_toggle_flags_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                    const opr_selection *selection, unsigned flags);
int opr_text_set_size_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                const opr_selection *selection, int twips);
int opr_text_set_colour_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                  const opr_selection *selection, unsigned long colour);
int opr_text_set_font_selection(opr_undo *u, owf_deck *deck, int slide, int obj,
                                const opr_selection *selection, const char *family);
/* Insert at (outside 0..n: the end), delete index, or move from to to. selected
 * is optional; on entry it is the selected slide and on return it follows the
 * change. A deck may have no slides at core level; the application may keep
 * one. 0 on success, -1 for a bad index or memory failure. */
int opr_slide_insert(opr_undo *u, owf_deck *deck, int at, int layout, int *selected);
int opr_slide_duplicate(opr_undo *u, owf_deck *deck, int index, int at, int *selected);
int opr_slide_delete(opr_undo *u, owf_deck *deck, int index, int *selected);
int opr_slide_move(opr_undo *u, owf_deck *deck, int from, int to, int *selected);
/* Rename one slide. A nonempty name must be unique in the deck. -2 means the
 * name is empty, too long or already used; -1 means bad input or no memory. */
int opr_slide_rename(opr_undo *u, owf_deck *deck, int index, const char *name, int *selected);
/* Hide or show a slide, as one undoable structural edit. */
int opr_slide_set_hidden(opr_undo *u, owf_deck *deck, int index, int hidden, int *selected);
/* Set one slide's transition, or every slide's when apply_all is nonzero, as
 * one undo step. Editable transitions are NONE through DISSOLVE; an imported
 * OTHER may be retained when its kept-attribute index is valid. Duration is
 * 0..600000 ms and advance_after is -1 (click) or 0..86399999 ms. -2 means
 * invalid transition data. */
int opr_slide_set_transition(opr_undo *u, owf_deck *deck, int index, const owf_transition *transition,
                             int apply_all, int *selected);
/* A new shape's complete reusable specification. Coordinates and radius are
 * slide twips; look carries fill, stroke, dash and arrow style. */
typedef struct {
    owf_shape shape;
    int x, y, width, height;
    int radius;
    owf_look look;
} opr_shape_spec;
/* Append a shape to a slide and select it, as one undo step. -2 means an
 * unsupported kind or invalid box; -1 means bad input or no memory. */
int opr_obj_insert_shape(opr_undo *u, owf_deck *deck, int slide,
                         const opr_shape_spec *spec, int *selected);
/* A new editable text frame. Coordinates and insets are slide twips; text is
 * copied as UTF-8 into its first paragraph (NULL: empty). */
typedef struct {
    int x, y, width, height;
    int inset[4];
    unsigned char valign;
    owf_look look;
    const char *text;
} opr_textbox_spec;
/* Append a text box and select it, as one undo step. -2 means invalid box,
 * inset, vertical alignment or look; -1 means bad input or no memory. */
int opr_obj_insert_textbox(opr_undo *u, owf_deck *deck, int slide,
                           const opr_textbox_spec *spec, int *selected);
/* A new native table. Cells are ordinary tagged story paragraphs, matching
 * imported ODP/PPTX tables and the existing OpenLayout grid path. */
typedef struct {
    int x, y, width, height;
    int rows, columns;
    int header_row;
} opr_table_spec;
/* Append a 1..32 by 1..32 table as one undoable object insertion. The first
 * row receives a neutral header shade when header_row is nonzero. */
int opr_obj_insert_table(opr_undo *u, owf_deck *deck, int slide,
                         const opr_table_spec *spec, int *selected);
/* Insert an empty row before row (0..current row count), or delete an existing
 * row. Cell coordinates and any following rows are repaired atomically; a
 * table always keeps at least one row. Each operation is one Undo step. */
int opr_table_insert_row(opr_undo *u, owf_deck *deck, int slide, int obj,
                         int row);
int opr_table_delete_row(opr_undo *u, owf_deck *deck, int slide, int obj,
                         int row);
/* Insert an empty column before column (0..current column count), or delete an
 * existing column. Width is redistributed without changing the table box. */
int opr_table_insert_column(opr_undo *u, owf_deck *deck, int slide, int obj,
                            int column);
int opr_table_delete_column(opr_undo *u, owf_deck *deck, int slide, int obj,
                            int column);
typedef enum {
    OPR_TABLE_CELL,
    OPR_TABLE_ROW,
    OPR_TABLE_ALL
} opr_table_scope;
/* Apply a fill and four-edge paragraph border to one cell, row or whole
 * table. Zero shading clears fill; zero borders clears the cell grid. */
int opr_table_apply_style(opr_undo *u, owf_deck *deck, int slide, int obj,
                          int row, int column, opr_table_scope scope,
                          unsigned long shading, int borders,
                          unsigned long border_colour);
/* A new embedded picture. The resource bytes and metadata are copied into
 * deck->res; coordinates are slide twips and crop is thousandths cut from
 * left, top, right and bottom. */
typedef struct {
    int x, y, width, height;
    int crop[4];
    const char *name, *mime, *alt;
    const void *data;
    size_t length;
    int natural_width, natural_height; /* picture's uncropped size, twips */
} opr_picture_spec;
/* Append a picture and select it, as one undo step. The operation is
 * transactional: invalid input or an allocation failure leaves neither an
 * object nor an embedded resource behind. -2 means invalid geometry, crop or
 * resource metadata; -1 means bad input or no memory. */
int opr_obj_insert_picture(opr_undo *u, owf_deck *deck, int slide,
                           const opr_picture_spec *spec, int *selected);
/* Deep-copy one ungrouped object, offset its resolved box by the requested
 * twips (clamped to the slide), append and select it as one undo step. Owned
 * text, points, lists, table widths and metadata are independent. */
int opr_obj_duplicate(opr_undo *u, owf_deck *deck, int slide, int obj,
                      int offset_x, int offset_y, int *selected);
/* Paste one top-level object from another deck, remapping its font and image
 * resources into the destination. The source remains untouched. Imported
 * opaque objects, charts, groups and inline frames are refused until their
 * dependent package parts have a portable copier. */
int opr_obj_paste(opr_undo *u, owf_deck *deck, int slide,
                  const owf_deck *source_deck, int source_slide, int source_obj,
                  int offset_x, int offset_y, int *selected);
typedef enum {
    OPR_ARRANGE_FRONT, OPR_ARRANGE_FORWARD,
    OPR_ARRANGE_BACKWARD, OPR_ARRANGE_BACK
} opr_arrange;
/* Change one ungrouped top-level object's back-to-front drawing order as one
 * undo step. Group blocks crossed by the move remain intact. */
int opr_obj_arrange(opr_undo *u, owf_deck *deck, int slide, int obj,
                    opr_arrange how, int *selected);
typedef enum {
    OPR_ALIGN_LEFT, OPR_ALIGN_CENTRE, OPR_ALIGN_RIGHT,
    OPR_ALIGN_TOP, OPR_ALIGN_MIDDLE, OPR_ALIGN_BOTTOM
} opr_align;
/* Align one ungrouped top-level object's resolved box to the slide/page as one
 * undo step. An inherited placeholder becomes an explicit independent box. */
int opr_obj_align(opr_undo *u, owf_deck *deck, int slide, int obj,
                  opr_align how);
/* Replace one top-level text/shape object's complete fill and stroke look as
 * one undo step. -2 means an unsupported object or invalid look. */
int opr_obj_set_look(opr_undo *u, owf_deck *deck, int slide, int obj,
                     const owf_look *look);
/* Set equal columns inside one top-level text frame or text-bearing shape.
 * columns is 1..16 and gap is twips. One column stores the canonical zero
 * values. The change is one undo step. */
int opr_obj_set_text_columns(opr_undo *u, owf_deck *deck, int slide, int obj,
                             int columns, int gap);
/* Set columns, their gap and left/top/right/bottom text-frame insets together.
 * Values are twips; columns is 1..16. Geometry must leave a positive text
 * area. The complete change is one undo step. */
int opr_obj_set_text_frame(opr_undo *u, owf_deck *deck, int slide, int obj,
                           int columns, int gap, const int inset[4]);
/* Make one top-level object an exclusion for overlapping text frames. wrap
 * is OWF_WRAP_NONE, SQUARE/TIGHT or TOP_BOTTOM; gaps are top, right,
 * bottom and left twips. The operation is one undo step. */
int opr_obj_set_wrap(opr_undo *u, owf_deck *deck, int slide, int obj,
                     owf_frame_wrap wrap, const int gap[4]);
/* Crop one top-level picture by thousandths from left, top, right and bottom.
 * Opposite sides must leave a positive source area. The four values change as
 * one undo step; existing OpenLayout crop rendering and ODP interchange
 * consume them. */
int opr_obj_set_picture_crop(opr_undo *u, owf_deck *deck, int slide, int obj,
                             const int crop[4]);
/* Link a top-level text frame to a later empty top-level text frame, or remove
 * any non-first frame from a flow. A target must not already belong to another
 * flow; an insertion before a source's existing successor closes into the
 * ordered chain. Unlinking closes the remaining chain around the removed box;
 * no story bytes are discarded. Both are one undo step. -2 means the requested
 * relationship is not valid. */
int opr_obj_link_text_frame(opr_undo *u, owf_deck *deck, int slide, int obj,
                            int target_slide, int target_obj);
int opr_obj_unlink_text_frame(opr_undo *u, owf_deck *deck, int slide, int obj);
/* Delete one ungrouped object, as one undo step. Undo restores all data owned
 * by the object. A group or an object with children returns -2. */
int opr_obj_delete(opr_undo *u, owf_deck *deck, int slide, int obj);
/* Give one slide object an explicit box in slide twips, as one undo step.
 * Moving an inherited placeholder clears OWF_OBJ_INHERIT_BOX; undo restores
 * both its former coordinates and inheritance. -2 means an empty or out-of-
 * slide box, -1 bad input. */
int opr_obj_set_box(opr_undo *u, owf_deck *deck, int slide, int obj,
                    int x, int y, int width, int height);
/* Snap one slide/page coordinate to the nearest positive grid multiple when
 * it lies within threshold twips. Invalid step/threshold values leave it
 * unchanged. Pure editor geometry: OpenLayout is not involved. */
int opr_grid_snap(int value, int step, int threshold);
/* Move an axis-aligned box start so its start, centre or end meets the nearest
 * guide within threshold. length 0 snaps a single resize edge. *matched is
 * the guide coordinate, or -1. Invalid input leaves start unchanged. */
int opr_guide_snap_axis(int start, int length, const int *guides, int count,
                        int threshold, int *matched);
/* Set a shape object's anticlockwise rotation in tenths of a degree, as one
 * undo step. The value is normalized to 0..3599. -2 means the object is not
 * a shape; -1 means bad input. */
int opr_obj_set_rotation(opr_undo *u, owf_deck *deck, int slide, int obj,
                         int rotation_tenths);
/* Several changes as one Undo step: begin with room for most steps, make
 * the changes with the calls above, then end. Undo puts them all back, last
 * first; Redo makes them again. Undo and Redo are refused while a group is
 * open. A group of one step is that step; an empty group adds nothing. -1
 * from begin: bad input, a group already open, or no memory. -1 from end:
 * no group open, or more steps than room, when the whole history is
 * cleared, since the change made without a step can no longer be undone. */
int opr_undo_group_begin(opr_undo *u, int most);
int opr_undo_group_end(opr_undo *u);
/* The open group's changes put back, last first, and the group dropped, for
 * a command that fails part way. 0; -1 with no group open, or when a change
 * could not be put back or had no step (the history is then cleared). */
int opr_undo_group_cancel(opr_undo *u, owf_deck *deck);
int opr_undo_can(const opr_undo *u);
int opr_redo_can(const opr_undo *u);
int opr_undo_undo(opr_undo *u, owf_deck *deck, int *slide, int *obj);
int opr_undo_redo(opr_undo *u, owf_deck *deck, int *slide, int *obj);

/* Through a laid-out slide (opr_slide_layout's, with the same measure;
 * NULL: opr_measure): the frame showing a slide's object (-1: none), and
 * the object a frame shows (-1: the master's, or the background). */
int opr_slide_frame_for(const opr_slide *s, int obj);
int opr_frame_obj(const opr_slide *s, int frame);
/* Whether prepared text remains beyond a bounded frame. The result is cached
 * in the frame, so the extra narrow-galley fit runs only for a frame whose
 * overflow indicator is actually requested. -1 means an invalid frame or an
 * allocation/layout failure. */
int opr_frame_text_overflow(opr_slide *s, int frame,
                            ol_measure_text_fn measure, void *userdata);
/* The caret for a position: a box in the slide's units, page 0. 0 when
 * the frame shows no such paragraph. */
int opr_caret_box(const opr_slide *s, int frame, opr_pos at, ol_measure_text_fn measure, void *userdata,
                  ol_rect *box);
/* The position nearest a point (slide units) in one frame's text. */
int opr_hit_frame(const opr_slide *s, int frame, ol_unit x, ol_unit y, ol_measure_text_fn measure, void *userdata,
                  opr_pos *at);
/* The frame of a slide's own object under a point (the frontmost), and the
 * position in its text (at may be NULL); -1 when there is none. */
int opr_hit(const opr_slide *s, ol_unit x, ol_unit y, ol_measure_text_fn measure, void *userdata, opr_pos *at);

/* ---- Notes pages and handouts (opr_notes.c) ----
 *
 * A slide's speaker notes are its notes object (owf_slide.notes), typed in
 * through the text calls above with obj OPR_NOTES: opr_text_insert(deck,
 * slide, OPR_NOTES, ...), opr_text_set, Undo and the rest work on it as on
 * any text frame. The notes page is deck->notes_page's size (A4 portrait
 * when it has none): the slide's picture at the top, the notes below it,
 * as ODP's notes pages place them. */
/* Above any slide's object count, so "no object" stays below 0. */
#define OPR_NOTES 0x7FFF

/* The notes page's size and its two boxes, in twips: x, y, width, height of
 * the slide's picture and of the notes' text. */
void opr_notes_page(const owf_deck *deck, int *width, int *height);
void opr_notes_boxes(const owf_deck *deck, int slide_box[4], int text_box[4]);
/* A slide's notes page laid out for the screen and the PDF: frame 0 the
 * page's white, frame 1 the notes text in its box (opr_frame_obj gives
 * OPR_NOTES for it). opr_caret_box, opr_hit_frame and opr_hit work on it.
 * The slide's picture is not in it: draw opr_slide_layout's into
 * opr_notes_boxes' slide box. NULL when memory runs out. */
opr_slide *opr_notes_layout(const owf_deck *deck, int slide, ol_measure_text_fn measure, void *userdata);
/* A handout page with per_page slides on it (1, 2, 3, 4, 6 or 9; others:
 * 6): the box of the k'th slide on it (0 first), twips; the slide's shape
 * kept, centred in its cell. -1 for k outside 0..per_page-1. */
int opr_handout_box(const owf_deck *deck, int per_page, int k, int box[4]);
/* The text of a slide's notes, paragraphs between "\n" (owf_malloc), and
 * setting it as one Undo step (u may be NULL: no step). */
char *opr_notes_get(const owf_deck *deck, int slide);
int opr_notes_set(opr_undo *u, owf_deck *deck, int slide, const char *utf8);

/* ---- The outline (opr_outline.c) ----
 *
 * The deck as one list of rows, as the Outline view shows it: each slide's
 * title (level 0), then the paragraphs of its outline box (the first
 * body, object or text placeholder) at levels 1 to 9. A box still showing
 * its prompt has no rows. Rows are rebuilt after each change. */
typedef struct {
    int slide;
    int obj;                     /* the title box (-1: the slide has none) or the outline box */
    int para;                    /* the paragraph in obj */
    int level;                   /* 0: the title; 1..9: the outline's levels */
} opr_outline_row;

typedef struct {
    int count, cap;
    opr_outline_row *rows;
} opr_outline;

int opr_outline_title_obj(const owf_deck *deck, int slide);
int opr_outline_body_obj(const owf_deck *deck, int slide);
/* The rows, into out (zeroed the first time; opr_outline_free after). 0, or -1 when memory runs out. */
int opr_outline_build(const owf_deck *deck, opr_outline *out);
void opr_outline_free(opr_outline *out);
/* The row of a slide's title (obj -1) or of a paragraph; -1 when there is none. */
int opr_outline_find(const opr_outline *out, int slide, int obj, int para);
/* A row's text (owf_malloc): a title's paragraphs joined by spaces. */
char *opr_outline_text(const owf_deck *deck, const opr_outline_row *r);
/* The changes, each one Undo step (u may be NULL), and the rows stale after
 * them. 0, -1 for memory or a bad row, -2 when the row cannot change so:
 *   set_text: a row's text replaced (a title's whole box); -2 no title box.
 *   insert_after: Return at a row's end: a new slide after a title (its
 *     layout), a new paragraph after body text; where the new row is.
 *   demote: a paragraph a level lower; a title: its slide joins the one
 *     before, the title a top-level paragraph there and its paragraphs a
 *     level lower under it (-2 for the first slide, or one before with no
 *     outline box).
 *   promote: a paragraph a level higher; a top-level one starts a slide of
 *     its own after this one, its title, the paragraphs after it going with
 *     it a level higher (-2 for a title).
 *   delete: a paragraph taken out; a title row, its whole slide (-2 the last). */
int opr_outline_set_text(opr_undo *u, owf_deck *deck, const opr_outline_row *r, const char *utf8);
int opr_outline_insert_after(opr_undo *u, owf_deck *deck, const opr_outline_row *r, int *new_slide, int *new_para);
int opr_outline_demote(opr_undo *u, owf_deck *deck, const opr_outline_row *r);
int opr_outline_promote(opr_undo *u, owf_deck *deck, const opr_outline_row *r, int *new_slide);
int opr_outline_delete(opr_undo *u, owf_deck *deck, const opr_outline_row *r);

/* ---- The slide sorter (opr_sorter.c) ----
 *
 * Every slide as a thumbnail in a grid, row by row, for seeing the whole
 * deck and arranging it: several slides picked at once and moved, copied,
 * deleted, hidden or shown together, each as one Undo step.
 *
 * A pick is which slides are picked: on[i] for slide i, the anchor a Shift
 * range starts from, and the focus (the slide the keys move from; -1 for
 * none). Its count follows the deck's (opr_pick_resize). */
typedef struct {
    unsigned char *on;
    int count;
    int anchor, focus;
} opr_pick;

typedef enum {
    OPR_PICK_ONE,                /* a plain click: only this slide */
    OPR_PICK_RANGE,              /* Shift: from the anchor to this slide */
    OPR_PICK_TOGGLE,             /* Amiga/Ctrl: this slide in or out */
    OPR_PICK_ADD_RANGE           /* Shift with Ctrl: the range added */
} opr_pick_how;

/* 0, or -1 when memory runs out (the pick is then empty, count 0). */
int opr_pick_resize(opr_pick *p, int count);
void opr_pick_free(opr_pick *p);
void opr_pick_clear(opr_pick *p);
/* Only index picked, as the anchor and focus. -1 for an index outside. */
int opr_pick_only(opr_pick *p, int index);
int opr_pick_click(opr_pick *p, int index, opr_pick_how how);
void opr_pick_all(opr_pick *p);
int opr_pick_has(const opr_pick *p, int index);
int opr_pick_number(const opr_pick *p);
/* The picked slides in order, up to max of them; returns how many there are. */
int opr_pick_list(const opr_pick *p, int *out, int max);

/* The grid in pixels, inside a view w by h. columns 0 picks as many as fit
 * thumbnails about 160 pixels wide; otherwise 1..OPR_SORTER_MAX_COLUMNS,
 * fewer when the thumbnails would be under 32 pixels. label_h is the line
 * under each thumbnail for its number. */
#define OPR_SORTER_MAX_COLUMNS 12
typedef struct {
    int count, columns, rows;
    int view_w, view_h;
    int margin, gap, label_h;
    int thumb_w, thumb_h;        /* the slide's own shape */
    int cell_w, cell_h;          /* thumbnail and label */
    int content_h;               /* all of it, for the scroller */
} opr_sorter_grid;

int opr_sorter_layout(opr_sorter_grid *g, int view_w, int view_h, int slide_w, int slide_h,
                      int count, int columns, int label_h);
/* The scroll (pixels from the top) kept between 0 and the most there is. */
int opr_sorter_clamp_scroll(const opr_sorter_grid *g, int scroll);
/* Slide index's thumbnail's top left within the view, scrolled. -1 when
 * there is no such slide. */
int opr_sorter_cell(const opr_sorter_grid *g, int index, int scroll, int *x, int *y);
/* The slide whose thumbnail or label is under x, y (view pixels), or -1. */
int opr_sorter_hit(const opr_sorter_grid *g, int x, int y, int scroll);
/* Where slides dropped at x, y go: the gap before slide 0..count. */
int opr_sorter_gap(const opr_sorter_grid *g, int x, int y, int scroll);
/* The insertion mark for a gap: a vertical bar at x from y, h high. */
int opr_sorter_gap_mark(const opr_sorter_grid *g, int gap, int scroll, int *x, int *y, int *h);
/* The scroll that shows slide index's whole row, changing scroll as little
 * as it can. */
int opr_sorter_scroll_to(const opr_sorter_grid *g, int index, int scroll);

typedef enum {
    OPR_SORTER_LEFT, OPR_SORTER_RIGHT, OPR_SORTER_UP, OPR_SORTER_DOWN,
    OPR_SORTER_PAGE_UP, OPR_SORTER_PAGE_DOWN, OPR_SORTER_HOME, OPR_SORTER_END
} opr_sorter_key;
/* The slide a key goes to from index (kept inside the deck). */
int opr_sorter_step(const opr_sorter_grid *g, int index, opr_sorter_key key);

/* The picked slides, as one Undo step each command. The pick follows: the
 * slides moved, the copies made, the slides hidden or shown stay picked;
 * after a delete, the slide after the last deleted one is. *selected (may
 * be NULL) is the slide to show. 0, -2 when nothing is picked (or a delete
 * would leave no slide), -1 for bad input or no memory (the deck is then as
 * it was, and the Undo history too). */
int opr_sorter_move(opr_undo *u, owf_deck *deck, opr_pick *pick, int gap, int *selected);
int opr_sorter_duplicate(opr_undo *u, owf_deck *deck, opr_pick *pick, int *selected);
int opr_sorter_delete(opr_undo *u, owf_deck *deck, opr_pick *pick, int *selected);
int opr_sorter_set_hidden(opr_undo *u, owf_deck *deck, opr_pick *pick, int hidden, int *selected);

/* ---- Portable slide-show state (opr_show.c) ----
 *
 * The Amiga player, ARexx and host tests use the same navigation and timing
 * rules. Normal movement skips hidden slides; an explicit numbered jump may
 * show one, and opr_show_next_hidden implements the show command for doing so.
 * Navigation resets elapsed time. Calls return 1 when the slide changed, 0
 * when it stayed put, and -1 for bad input or when forward movement reaches
 * the end of a non-looping show. */
typedef struct {
    const owf_deck *deck;
    int slide;
    int loop;
    unsigned long elapsed_ms;
} opr_show;

int opr_show_begin(opr_show *show, const owf_deck *deck, int from, int loop);
int opr_show_next(opr_show *show);
int opr_show_previous(opr_show *show);
int opr_show_first(opr_show *show);
int opr_show_last(opr_show *show);
int opr_show_goto(opr_show *show, int slide);
int opr_show_next_hidden(opr_show *show);
/* Add elapsed milliseconds and advance once when this slide's automatic
 * timing expires. 0 means it is still showing; 1 means it advanced; -1 means
 * the show ended or the input/state was invalid. */
int opr_show_tick(opr_show *show, unsigned long milliseconds);

/* Backend-independent transition geometry. A frame describes which rectangle
 * of the old and new full-screen bitmap to copy and where. Wipe has only a
 * new_part (the old screen remains underneath); push has both. */
typedef struct {
    int source_x, source_y, dest_x, dest_y, width, height;
} opr_blit;

typedef struct {
    int have_old, have_new;
    opr_blit old_part, new_part;
} opr_transition_frame;

int opr_transition_frame_at(owf_transition_kind kind, int direction, int width, int height,
                            int step, int steps, opr_transition_frame *frame);

/* A complete frame plan for every geometric transition. base_new selects the
 * bitmap copied before its parts (old otherwise); push parts cover the whole
 * frame, while cover, uncover, split and blinds use that base. */
#define OPR_TRANSITION_MAX_PARTS 8
typedef struct {
    int base_new, old_count, new_count;
    opr_blit old_parts[OPR_TRANSITION_MAX_PARTS];
    opr_blit new_parts[OPR_TRANSITION_MAX_PARTS];
} opr_transition_plan;

int opr_transition_plan_at(owf_transition_kind kind, int direction, int width, int height,
                           int step, int steps, opr_transition_plan *plan);

/* A four-row, 4 x 4 ordered-dither mask repeated across a bitmap. Bits grow
 * monotonically from the first step to a completely set last step, so a
 * backend can mask-blit a crossfade or fade through black efficiently. */
int opr_transition_pattern_at(int step, int steps, unsigned short pattern[4]);

/* Dissolve asks for its 16 x 16 tiles in deterministic shuffled order. */
int opr_transition_tile_count(int width, int height);
int opr_transition_tile_at(int width, int height, int index, opr_blit *tile);

#ifdef __cplusplus
}
#endif

#endif
