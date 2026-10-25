/*
 * OpenPresentation's portable core (src/deck), as openwrite_core.h is
 * OpenWrite's and opencalc_core.h OpenCalc's: what the Amiga program, the
 * show and OWConvert share, built and tested on the PC.
 *
 * Phase P1 (docs/gfoffice/Design-OpenPresentation.md, 12) is here:
 *
 *   - a slide laid out by OpenLayout: one ol_document a slide, one page the
 *     slide's size, a frame (OL_BOX_ABSOLUTE) for the background and for
 *     each object, back to front; a frame's text made by OpenWrite's
 *     paragraph code (ow_layout_add_story, Shared E5), with each run's font,
 *     size and colour from its level when it has none, and its list's
 *     bullet or number in front;
 *   - text anchored to the middle or bottom of its box, and text that
 *     shrinks to fit (up to four times by 90%), by laying out again
 *     (OpenLayout 0.2 has neither: section 5.2 and 5.3);
 *   - the deck as a PDF, one page a slide, drawn from that display list
 *     through the page PDF writer (owf_pdf.h); shapes OpenLayout 0.2
 *     can't draw (ellipses, rounded rectangles, triangles, diamonds,
 *     arrows, lines, polygons) are drawn at their frame's box, and so are
 *     a picture's crop and an object kept but not drawn (as a labelled box).
 *
 * Plain C99, no floating point. MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OPENPRESENTATION_CORE_H
#define OPENPRESENTATION_CORE_H

#include "owf_deck.h"
#include "openlayout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPR_CORE_VERSION "0.1.0"

/* One frame of a laid-out slide: the background (obj NULL), a master's
 * object, or the slide's. */
typedef struct {
    ol_id node;                  /* the frame's block */
    const owf_obj *obj;
    int from_master;
    owf_obj box;                 /* obj with its box resolved (owf_deck_resolve) */
    int scale;                   /* percent its text was laid out at (shrink to fit) */
    ol_id first, end;            /* the nodes made for its content */
} opr_frame;

typedef struct {
    ol_document *doc;            /* OpenLayout units; page 0 is the slide */
    const owf_deck *deck;
    int slide;
    int nframes;
    opr_frame *frames;           /* drawing order */
    int overflow;                /* something reached below the slide's bottom edge */
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

typedef struct {
    int with_hidden;             /* hidden slides too */
    const char *today;           /* the date fields' text; NULL: today's */
} opr_pdf_options;

/* The deck as a PDF, one page a slide at the slide's size. options may be
 * NULL. OWF_OK, or OWF_ERR_MEMORY. */
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
/* The whole text replaced: the first paragraph's format, level and first
 * run's look kept for all of it. */
int opr_text_set(owf_deck *deck, int slide, int obj, const char *utf8);

/* Undo: up to 16 copies of an object's text, taken before each change
 * (opr_undo_push); opr_undo_undo and opr_undo_redo put one back and say which
 * object it was. */
typedef struct opr_undo opr_undo;
opr_undo *opr_undo_new(void);
void opr_undo_free(opr_undo *u);
void opr_undo_clear(opr_undo *u);
int opr_undo_push(opr_undo *u, const owf_deck *deck, int slide, int obj);
int opr_undo_can(const opr_undo *u);
int opr_redo_can(const opr_undo *u);
int opr_undo_undo(opr_undo *u, owf_deck *deck, int *slide, int *obj);
int opr_undo_redo(opr_undo *u, owf_deck *deck, int *slide, int *obj);

/* Through a laid-out slide (opr_slide_layout's, with the same measure;
 * NULL: opr_measure): the frame showing a slide's object (-1: none), and
 * the object a frame shows (-1: the master's, or the background). */
int opr_slide_frame_for(const opr_slide *s, int obj);
int opr_frame_obj(const opr_slide *s, int frame);
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

#ifdef __cplusplus
}
#endif

#endif
