/*
 * GFOpenCalc's core on owfcalc.library: its own copy of the pivot
 * definition code (owf_pivot.h), which the library holds but does not
 * export. With libowf linked in, owf_book.c's copy serves.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifdef OWF_USE_CALC_LIBRARY
#include <stdlib.h>
#include <string.h>

static int oc_pivot_number(const char *s, size_t n, double *v)
{
    char text[64], *end;
    if (!n || n >= sizeof text)
        return 0;
    memcpy(text, s, n);
    text[n] = 0;
    *v = strtod(text, &end);
    return end != text && !*end;
}

#define OWF_PIVOT_NUMBER(s, n, v) oc_pivot_number((s), (n), (v))
#include "owf_pivotdef.h"
#else
/* ISO C wants a translation unit to declare something. */
typedef int oc_pivotdef_unused;
#endif
