/*
 * libowf: fills, one copy for every GFOffice program (docs/suite/owf-shared-core.md): the integer sine the
 * gradients turn with, a colour part of the way from one to another, and a gradient fill as plain polygons
 * for a screen that fills polygons but cannot clip (GFOpenPresentation's and GFOpenPressmaster's screens;
 * the PDF writer, owf_pdf_gradient, clips instead and uses the same sine and colours). GFOpenDiag and
 * GFOpenDraw are to follow. Integer only, no Amiga headers.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_FILL_H
#define OWF_FILL_H

#ifdef __cplusplus
extern "C" {
#endif

/* sin of tenths of a degree (to the nearest whole degree), * 16384; cos is owf_fill_sin16k(tenths + 900) */
long owf_fill_sin16k(int tenths);

/* The colour k of n of the way from from to to (each 0xRRGGBB, the top byte ignored). */
unsigned long owf_fill_blend(unsigned long from, unsigned long to, long k, long n);

/* A gradient fill as plain polygons: band (0..bands-1) of the gradient inside the closed outline xy (n
 * points, at most OWF_FILL_MAX_OUTLINE) across the box x, y, w, h, into out (at most cap points) with its
 * colour (0xRRGGBB). LINEAR runs from one colour to the other along angle (tenths anticlockwise, 0 left to
 * right; each band overlaps the next by a unit); RADIAL's band 0 is the outermost ring, from at the centre,
 * each band drawn over the one before. The point count (0: nothing to draw), or -1 when out is too small.
 * The kinds are OWF_PDF_GRADIENT_LINEAR's and _RADIAL's numbers. */
#define OWF_FILL_LINEAR 1
#define OWF_FILL_RADIAL 2
#define OWF_FILL_MAX_OUTLINE 128          /* GFOpenLayout's OL_MAX_SHAPE_POINTS */
/* enough points for any band of an OWF_FILL_MAX_OUTLINE outline: each cut keeps the outline's points and
 * adds at most two per edge, and a ring adds its sides */
#define OWF_FILL_MAX_POINTS (3 * OWF_FILL_MAX_OUTLINE + 32 + 8)
int owf_fill_gradient_band(const int *xy, int n, int x, int y, int w, int h, int kind, int angle,
                           unsigned long from, unsigned long to, int band, int bands, int *out, int cap,
                           unsigned long *colour);

#ifdef __cplusplus
}
#endif

#endif
