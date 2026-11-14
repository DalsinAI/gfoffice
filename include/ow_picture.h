/*
 * ow_picture.h: the PNG and IFF ILBM writers for 24-bit RGB rows (src/deck/opr_picture.c, GFOpenPresentation P9),
 * shared by GFOpenPresentation's Export as Pictures and GFOpenDiag's picture EXPORT. Plain C99, no Amiga headers.
 * rgb: height rows of width pixels, 3 bytes each (R, G, B), stride bytes apart. Each returns the file in memory
 * (malloc'd: free it) and its length, or NULL (memory, or width/height outside 1..16384, or stride too short).
 * PNG is deflated with zlib when built with OWF_HAVE_ZLIB, else written with stored blocks; ILBM is 24 planes
 * packed with ByteRun1.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OW_PICTURE_H
#define OW_PICTURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

unsigned char *opr_png_encode(const unsigned char *rgb, int width, int height, size_t stride, size_t *length);
unsigned char *opr_ilbm_encode(const unsigned char *rgb, int width, int height, size_t stride, size_t *length);

#ifdef __cplusplus
}
#endif

#endif
