/* GFOpenPresentation's and GFOpenDiag's names for libowf's picture writers (ow_picture.h): opr_png_encode and
 * opr_ilbm_encode are owf_picture_png and owf_picture_ilbm (libowf/src/owf_picture.c, owf_picture.h), the one
 * copy every GFOffice program uses. Kept so the callers (opr_picture.c, src/diag/odg_rexx.c) need no change.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "ow_picture.h"
#include "owf_picture.h"

unsigned char *opr_png_encode(const unsigned char *rgb, int width, int height, size_t stride, size_t *length)
{
    return owf_picture_png(rgb, width, height, stride, length);
}

unsigned char *opr_ilbm_encode(const unsigned char *rgb, int width, int height, size_t stride, size_t *length)
{
    return owf_picture_ilbm(rgb, width, height, stride, length);
}
