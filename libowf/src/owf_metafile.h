/*
 * libowf: a Windows metafile player (Design-OpenPressmaster.md, 7). There is no metafile datatype for OS 3.2, so a
 * picture in WMF form (Publisher's .pub, PowerPoint's and OpenOffice's slides) is played here: its records become
 * lines, polygons, rectangles, ellipses and text in a box, which the caller draws (the page PDF writer, owf_pdf.c;
 * GFOpenLayout's screen next). Written from the format's published specification ([MS-WMF], Windows Metafile
 * Format, sections 2.1-2.3), not from any program's code. No floating point.
 *
 * Played: the placeable header's bounds, the window's origin and extent, pens (solid, dashed or none), solid and
 * hatched brushes (hatches as their colour), fonts (height, weight, italic, underline, strike-out, face as a
 * kind), text colour and alignment, MoveTo and LineTo, Rectangle, RoundRect, Ellipse, Polygon, Polyline,
 * PolyPolygon, Arc, Chord and Pie, IntersectClipRect, TextOut and ExtTextOut, SaveDC and RestoreDC. Pictures inside
 * (StretchDIB and the like), regions and ExcludeClipRect are left out and counted.
 *
 * An EMF ([MS-EMF] 2.3) plays the same way: its Frame (or Bounds), the mapping modes with the window's and the
 * viewport's origins and extents, pens (CreatePen, ExtCreatePen), brushes, fonts (ExtCreateFontIndirectW) and stock
 * objects, MoveToEx and LineTo, Rectangle, RoundRect, Ellipse, the Poly records in 32 and 16 bits (Polygon,
 * Polyline, PolyBezier and their To forms, PolyPolygon, PolyPolyline; curves as lines), paths (BeginPath to
 * EndPath, then FillPath, StrokePath or StrokeAndFillPath), Arc, Chord, Pie and ArcTo with SetArcDirection,
 * IntersectClipRect and ExtSelectClipRgn's reset, ExtTextOutW and A, SaveDC and RestoreDC. EMF+ comments are passed
 * over for the EMF records beside them; pictures inside, ExcludeClipRect, clip regions and paths, and world
 * transforms are left out and counted.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_METAFILE_H
#define OWF_METAFILE_H

#include "owf.h"

#include <stddef.h>

/* What a metafile is drawn with. Positions are the caller's units (twips for owf_pdf), from the box's top left
 * corner as the caller gave it; colours OWF_SHADE(0xRRGGBB), 0 for none. Each call returns 0, or -1 to stop. */
typedef struct owf_mf_sink {
    /* npoints x, y pairs; closed joins the last to the first (and only a closed one is filled); dash: dashed */
    int (*path)(void *ctx, const int *xy, int npoints, int closed, unsigned long fill, unsigned long line,
                int line_width, int dash);
    int (*ellipse)(void *ctx, int x, int y, int w, int h, unsigned long fill, unsigned long line, int line_width,
                   int dash);
    /* UTF-8 text with its left end at x on its baseline; size the font's height */
    int (*text)(void *ctx, int x, int baseline, const char *utf8, size_t length, owf_font_kind kind, unsigned flags,
                int size, unsigned long colour);
    /* the text's width as text draws it; NULL: half its size a character */
    int (*text_width)(void *ctx, const char *utf8, size_t length, owf_font_kind kind, unsigned flags, int size);
    /* what follows is drawn only inside x, y, w, h (w, h 0: nothing shows); w -1: anywhere again. NULL: the
     * clipping records are counted as left out and everything is drawn */
    int (*clip)(void *ctx, int x, int y, int w, int h);
} owf_mf_sink;

typedef struct owf_mf_result {
    int drawn;               /* shapes and texts drawn */
    int skipped;             /* records played as nothing: pictures inside, regions, clipping the sink can't */
} owf_mf_result;

/* 1: the bytes are a WMF (with or without the placeable header), 0: not */
int owf_metafile_is_wmf(const unsigned char *data, size_t length);

/* 1: the bytes are an EMF (its header record, " EMF"), 0: not */
int owf_metafile_is_emf(const unsigned char *data, size_t length);

/* 1: a WMF or an EMF, which owf_metafile_play and owf_metafile_render draw */
int owf_metafile_can_play(const unsigned char *data, size_t length);

/* Plays a WMF or an EMF into the box x, y, w, h (w, h > 0): the picture's own bounds (a WMF's window, else its
 * placeable header's; an EMF's Frame, else its Bounds) fill the box. OWF_OK; OWF_ERR_FORMAT when it is neither or
 * its first records don't hold together
 * (nothing drawn); a record cut short ends the playing, keeping what was drawn; -1 from the sink stops it
 * (OWF_ERR_MEMORY). result may be NULL. */
int owf_metafile_play(const unsigned char *data, size_t length, int x, int y, int w, int h, const owf_mf_sink *sink,
                      void *ctx, owf_mf_result *result);

/* The WMF or EMF drawn into w x h pixels (A, R, G, B bytes a pixel, rows of w), over what argb holds: the picture's
 * bounds fill it. Shapes and lines only, solid: text and the records the player leaves out are counted as
 * skipped (owf_mfraster.c). OWF_OK, OWF_ERR_FORMAT, or OWF_ERR_MEMORY. */
int owf_metafile_render(const unsigned char *data, size_t length, unsigned char *argb, int w, int h,
                        owf_mf_result *result);

/* A placeable WMF's size from its bounds and units an inch, or an EMF's from its Frame, twips: 1, or 0 when it
 * doesn't say. */
int owf_metafile_size(const unsigned char *data, size_t length, int *w, int *h);

#endif
