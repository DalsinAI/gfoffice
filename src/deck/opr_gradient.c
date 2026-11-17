/*
 * GFOpenPresentation's and GFOpenPressmaster's name for libowf's gradient fill as plain polygons
 * (openpresentation_core.h, opr_gradient_band): it is owf_fill_gradient_band (libowf/src/owf_fill.c,
 * owf_fill.h), the one copy every GFOffice program uses, kept so its callers need no change.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openpresentation_core.h"
#include "owf_fill.h"

/* the same kinds, and the same largest outline as GFOpenLayout's, or the build stops here */
typedef char opr_gradient_kinds_match[OPR_GRADIENT_LINEAR == OWF_FILL_LINEAR && OPR_GRADIENT_RADIAL == OWF_FILL_RADIAL
                                      ? 1 : -1];
typedef char opr_gradient_outline_fits[OL_MAX_SHAPE_POINTS == OWF_FILL_MAX_OUTLINE ? 1 : -1];

int opr_gradient_band(const int *xy, int n, int x, int y, int w, int h, int kind, int angle, unsigned long from,
                      unsigned long to, int band, int bands, int *out, int cap, unsigned long *colour)
{
    return owf_fill_gradient_band(xy, n, x, y, w, h, kind, angle, from, to, band, bands, out, cap, colour);
}
