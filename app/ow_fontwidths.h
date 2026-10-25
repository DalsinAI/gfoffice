/*
 * OpenWrite's text widths for the layout (2.0.3), apart from graphics.library
 * so that the host tests run them (tests/core/test_fontwidths.c).
 *
 * The layout measures each style with the font that draws it at 100%: its
 * advance widths at the size text of that size is drawn there
 * (ow_fw_pixels), so at 100% a word is drawn exactly as wide as the layout
 * made it. The font is the document's face, or, when that one does not
 * open (openfont.library can't read OS 3.2's Intellifont outlines, and
 * CGTriumvirate and CGTimes have no bitmap sizes to fall back to), the
 * first of its kind's that does (ow_fw_face): measuring and drawing both
 * use that face, so they agree. Widths a font gives at another size than
 * asked (a bitmap font's nearest, or the screen's font when no face opens
 * at all) are scaled to the size asked, never taken as they are.
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
    /* the screen's font, for when no face opens; NULL: none */
    void *fallback;
    int fallback_ysize, fallback_ascent;
} ow_fw_backend;

typedef struct {
    char face[OW_FW_NAME];           /* the face the widths are from */
    int bold, px;                    /* bold or not, at px pixels */
    int ascent, descent;             /* pixels at px */
    int from;                        /* OW_FW_FROM_...: how they were had */
    unsigned short w[256];           /* advances in pixels at px */
} ow_fw_table;

enum {
    OW_FW_FROM_FACE,                 /* the face at px */
    OW_FW_FROM_SCALED,               /* the face at another size, scaled to px */
    OW_FW_FROM_SCREEN                /* the screen's font, scaled to px: no face opened */
};

typedef struct {
    ow_fw_backend be;
    char theme_face[OW_FW_NAME];     /* the window's font file: a face that opens */
    struct { char file[OW_FW_NAME], face[OW_FW_NAME]; } faces[OW_FW_FACES];
    int nfaces, next_face;
    ow_fw_table tables[OW_FW_TABLES];
    int ntables, next_table;
} ow_fontwidths;

void ow_fw_init(ow_fontwidths *fw, const ow_fw_backend *be, const char *theme_face);
/* Forgets the faces and widths found (a new document, a new theme). */
void ow_fw_reset(ow_fontwidths *fw);

/* The pixel size text of size units (OpenLayout's, 60 a pixel) is drawn
 * at at 100%: 4 to 128. */
int ow_fw_pixels(long size_units);

/* The face file's text is measured and drawn in: file when it opens, else
 * the first of its kind's (serif, monospaced or sans, by its name) that
 * does, else the window's font; "" when none opens (the screen's font). */
const char *ow_fw_face(ow_fontwidths *fw, const char *file);

/* The widths of file's text at px pixels, bold or not. Never NULL. */
const ow_fw_table *ow_fw_widths(ow_fontwidths *fw, const char *file, int bold, int px);

#ifdef __cplusplus
}
#endif

#endif
