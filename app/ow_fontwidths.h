/*
 * OpenWrite's text widths for the layout (2.0.3), apart from graphics.library
 * so that the host tests run them (tests/core/test_fontwidths.c).
 *
 * The rule: the widths a word is laid out with are the widths of the font it
 * is drawn with. The layout measures each style with the font that draws it
 * at 100% (its size there: ow_fw_pixels), so at 100% a word is drawn exactly
 * as wide as the layout made it. At another zoom the word is drawn in the
 * same face at the size the zoom asks for, or, when that runs wider than the
 * layout scaled, the largest smaller size that doesn't (ow_fw_fit).
 *
 * The face is the document's font when it opens; else the first of its
 * kind's that does (ow_fw_face), then the window's theme face, then the
 * window's own font (on an Amiga whose only fonts are bitmaps, the screen's
 * font, which diskfont scales). OS 3.2's Intellifont outlines
 * (CGTriumvirate, CGTimes) don't open through openfont.library, and have
 * no bitmap sizes. Widths a font gives at another size than asked (a bitmap
 * font's nearest, when it can't be scaled) are scaled to the size asked,
 * never taken as they are.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OW_FONTWIDTHS_H
#define OW_FONTWIDTHS_H

#ifdef __cplusplus
extern "C" {
#endif

#define OW_FW_NAME 72
#define OW_FW_FACES 12
#define OW_FW_TABLES 16
#define OW_FW_FITS 16
#define OW_FW_PROBE 15               /* the size a face is tried at: 11 points at 100% */

/* What the program gives: its fonts, opened and kept by it. */
typedef struct {
    void *user;
    /* file's font at pixels: NULL when there is none; *ysize its height and
     * *ascent its baseline plus one, in pixels (a font may come back at
     * another size than asked) */
    void *(*open)(void *user, const char *file, int pixels, int *ysize, int *ascent);
    /* the advance of Latin-1 character c in it, bold with the bold smear */
    int (*advance)(void *user, void *font, unsigned char c, int bold);
    /* the window's font, for when no face opens at all; NULL: none */
    void *fallback;
    int fallback_ysize, fallback_ascent;
} ow_fw_backend;

typedef struct {
    char face[OW_FW_NAME];           /* the face the widths are from; "": the window's font */
    int bold, px;                    /* bold or not, at px pixels */
    int ascent, descent;             /* pixels at px */
    int from;                        /* OW_FW_FROM_...: how they were had */
    unsigned short w[256];           /* advances in pixels at px */
} ow_fw_table;

enum {
    OW_FW_FROM_FACE,                 /* the face at px */
    OW_FW_FROM_SCALED,               /* the face at another size, scaled to px */
    OW_FW_FROM_SCREEN                /* the window's font, scaled to px: no face opened */
};

typedef struct {
    ow_fw_backend be;
    char theme_face[OW_FW_NAME];     /* the theme's font file */
    char window_face[OW_FW_NAME];    /* the window's own font file (the screen's, maybe) */
    struct { char file[OW_FW_NAME], face[OW_FW_NAME]; } faces[OW_FW_FACES];
    int nfaces, next_face;
    ow_fw_table tables[OW_FW_TABLES];
    int ntables, next_table;
    struct { char face[OW_FW_NAME]; int bold, pixels, fitted; long laid; } fits[OW_FW_FITS];
    int nfits, next_fit;
} ow_fontwidths;

/* The sample a style's widths are compared by when it is drawn at a zoom. */
extern const char ow_fw_sample_text[];

void ow_fw_init(ow_fontwidths *fw, const ow_fw_backend *be, const char *theme_face, const char *window_face);
/* Forgets the faces, widths and fits found (a new theme, new fonts). */
void ow_fw_reset(ow_fontwidths *fw);

/* The pixel size text of size units (OpenLayout's, 60 a pixel) is drawn
 * at at 100%: 4 to 128. */
int ow_fw_pixels(long size_units);
/* ... and at zoom percent (at least 1). */
int ow_fw_pixels_at(long size_units, int zoom);

/* The face file's text is measured and drawn in (see above); "" when none
 * opens (the window's font). */
const char *ow_fw_face(ow_fontwidths *fw, const char *file);

/* The widths of file's text at px pixels, bold or not. Never NULL. */
const ow_fw_table *ow_fw_widths(ow_fontwidths *fw, const char *file, int bold, int px);

/* The sample's width in t's pixels. */
unsigned long ow_fw_sample_sum(const ow_fw_table *t);

/* The width of n Latin-1 bytes in a font the backend opened. */
int ow_fw_text(ow_fontwidths *fw, void *font, const char *s, int n, int bold);

/* The pixel size text laid out with t is drawn at where the zoom asks for
 * pixels and lays the sample out laid pixels wide: pixels when t's face
 * draws the sample no wider there, else the largest smaller size that does,
 * down to three quarters. The answer is kept. */
int ow_fw_fit(ow_fontwidths *fw, const ow_fw_table *t, int pixels, long laid);

/* The sample laid out and drawn at a zoom, as OpenWrite does it (FONTCHECK
 * prints it; the tests check drawn against laid). */
typedef struct {
    char face[OW_FW_NAME];
    int from;                        /* the widths': OW_FW_FROM_... */
    int px;                          /* measured at */
    int zoom_px, draw_px;            /* the zoom's size, and the size drawn at */
    int draw_ysize;                  /* the drawing font's height (0: none opened) */
    long laid, drawn;                /* the sample's width, pixels at the zoom */
} ow_fw_check;

void ow_fw_check_sample(ow_fontwidths *fw, const char *file, int bold, long size_units, int zoom,
                        ow_fw_check *out);

#ifdef __cplusplus
}
#endif

#endif
