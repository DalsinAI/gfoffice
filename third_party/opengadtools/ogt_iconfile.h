/* ogt_iconfile: reads an icon strip, the ILBM file an icon set is kept in
 * (docs/style/README.md, section 3.7; tools/iconset/ilbm.py
 * writes them). Every icon of the set at one size stands side by side in
 * one row; colour 0 is see-through. Two private chunks carry the icons'
 * names (OGTN) and the colours of the disabled picture (OGTD).
 *
 * Plain C with no Amiga calls, so the same code is tested on the host
 * (tests/host/test_iconfile.c) and runs on the Amiga.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OGT_ICONFILE_H
#define OGT_ICONFILE_H

#include <stddef.h>
#include "ogt_theme.h"

#define OGT_ICONFILE_MAX 128            /* icons in one strip */

typedef struct ogt_iconfile {
    int size;                           /* each icon is size x size */
    int count;
    int colours;                        /* 8 or 32 (colour 0 see-through) */
    ogt_rgb normal[32];                 /* CMAP */
    ogt_rgb disabled[32];               /* OGTD, or made from CMAP when missing */
    const char *name[OGT_ICONFILE_MAX];
    char *names;                        /* the names' text */
    unsigned char *pix;                 /* count * size wide, size high: one colour number a pixel */
    int stride;                         /* count * size */
} ogt_iconfile;

/* Reads a strip from memory. 1, or 0 with a reason in err. */
int ogt_iconfile_parse(ogt_iconfile *f, const unsigned char *data, size_t len, char *err, size_t errlen);
void ogt_iconfile_free(ogt_iconfile *f);
/* The place of an icon by its name, or -1. */
int ogt_iconfile_find(const ogt_iconfile *f, const char *name);
/* The first pixel of an icon (rows are stride apart). */
const unsigned char *ogt_iconfile_pixels(const ogt_iconfile *f, int index);

#endif
