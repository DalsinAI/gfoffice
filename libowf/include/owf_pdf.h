/*
 * libowf: the page-drawing PDF writer (Design-Suite-Shared.md, 9.2, E4),
 * its first part: pages of a given size, and text, rectangles, rounded
 * rectangles, ellipses, lines, polygons, pictures and links placed where
 * a layout put them. OpenPresentation draws its slides into it, from
 * OpenLayout's display list; the other GFOffice programs are to follow.
 *
 * Positions and sizes are twips from the page's top left corner; colours
 * are OWF_SHADE(0xRRGGBB), 0 for none. Text is in PDF's standard fonts
 * (Helvetica, Times, Courier and their bold and italic faces) in
 * WinAnsiEncoding, so it stays searchable and the PDF small; a character
 * outside it is drawn as "?". Text is fitted to the width the layout
 * measured (PDF's horizontal scaling), so the PDF breaks lines where the
 * layout did, whatever fonts the layout measured with. Pictures: JPEG as
 * it is, PNG decoded (as out_pdf.c does). No floating point.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_PDF_H
#define OWF_PDF_H

#include "owf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct owf_pdf owf_pdf;

owf_pdf *owf_pdf_new(void);
void owf_pdf_free(owf_pdf *pdf);
/* A new page, drawn on until the next. 0, or -1 when memory runs out. */
int owf_pdf_page(owf_pdf *pdf, int width, int height);
/* Text with its left end at x on its baseline, fitted to width (0: its
 * own width); flags OWF_BOLD, _ITALIC, _UNDERLINE, _STRIKE; size twips;
 * colour 0xRRGGBB. */
int owf_pdf_text(owf_pdf *pdf, int x, int baseline, int width, const char *utf8, size_t length,
                 owf_font_kind kind, unsigned flags, int size, unsigned long colour);
/* line_width twips (0: the thinnest line); dash: a dashed line. */
int owf_pdf_rect(owf_pdf *pdf, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width);
int owf_pdf_round_rect(owf_pdf *pdf, int x, int y, int w, int h, int radius, unsigned long fill,
                       unsigned long line, int line_width);
int owf_pdf_ellipse(owf_pdf *pdf, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width);
/* npoints x, y pairs; closed joins the last to the first. */
int owf_pdf_path(owf_pdf *pdf, const int *xy, int npoints, int closed, unsigned long fill,
                 unsigned long line, int line_width);
/* Lines are dashed from now on (dash 1) or solid again (0). */
void owf_pdf_dash(owf_pdf *pdf, int dash);
/* A picture in the box x, y, w, h; with clip, only what is inside it shows
 * (a cropped picture). 0, or -1 when memory runs out; a picture the PDF
 * can't hold (only JPEG and PNG) is left out and reported at the end. */
int owf_pdf_image(owf_pdf *pdf, const owf_image *image, int x, int y, int w, int h, const int *clip);
/* A link to href over the box. */
int owf_pdf_link(owf_pdf *pdf, int x, int y, int w, int h, const char *href);
/* The PDF, from malloc. The writer may be freed after. */
int owf_pdf_finish(owf_pdf *pdf, const char *title, unsigned char **data, size_t *length, owf_report *report);
/* The pages drawn so far. */
int owf_pdf_page_count(const owf_pdf *pdf);

/* Text's width in a standard font, twips, as owf_pdf_text draws it. */
int owf_pdf_text_width(const char *utf8, size_t length, owf_font_kind kind, unsigned flags, int size);
/* A character's byte in WinAnsiEncoding, or 0 when it has none. */
int owf_pdf_winansi(unsigned long codepoint);

#ifdef __cplusplus
}
#endif

/* the library's calls, once every header with calls is complete (owf.h) */
#define OWF_PDF_H_DONE
#if defined(OWF_USE_LIBRARY) && !defined(OWF_LIBRARY_BUILD)
#include "owf_proto.h"
#endif

#endif
