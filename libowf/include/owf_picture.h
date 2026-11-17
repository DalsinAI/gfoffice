/*
 * libowf: the picture writers, one copy for every GFOffice program (docs/suite/owf-shared-core.md): PNG and
 * IFF ILBM files from 24-bit RGB rows, and PNG's chunk CRC. GFOpenPresentation's Export as Pictures, GFOpenDiag's
 * picture EXPORT and its diagram-in-a-PNG (owf_dpng.c) use them; GFOpenPressmaster and GFOpenDraw are to follow.
 * Plain C99, no Amiga headers, no floating point.
 *
 * rgb: height rows of width pixels, 3 bytes each (R, G, B), stride bytes apart. Each writer returns the file
 * (from malloc: free it) and its length, or NULL (memory, or width/height outside 1..16384, or stride too
 * short). PNG is deflated with zlib when built with OWF_HAVE_ZLIB, else written with stored blocks; ILBM is 24
 * planes packed with ByteRun1.
 * Written from published descriptions: W3C's PNG specification (second edition), RFC 1950, and EA IFF 85
 * with its ILBM form.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_PICTURE_H
#define OWF_PICTURE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OWF_PICTURE_MAX_SIDE 16384

unsigned char *owf_picture_png(const unsigned char *rgb, int width, int height, size_t stride, size_t *length);
unsigned char *owf_picture_ilbm(const unsigned char *rgb, int width, int height, size_t stride, size_t *length);

/* PNG's CRC-32 (ISO 3309, as PNG's specification gives it) of n bytes, carried on from crc: 0 to start, so
 * a chunk's CRC is owf_png_crc(owf_png_crc(0, type, 4), data, length). */
unsigned long owf_png_crc(unsigned long crc, const unsigned char *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif
