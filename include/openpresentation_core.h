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

#ifdef __cplusplus
}
#endif

#endif
