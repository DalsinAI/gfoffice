/*
 * owf_diag_int.h: what libowf/diagram's files share and nothing else uses.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_DIAG_INT_H
#define OWF_DIAG_INT_H

#include "owf_diagram.h"
#include "owf_internal.h"

/* A page with no cells, not even the root (the readers'). */
owf_dpage *owf_diagram_bare_page(owf_diagram *d);
/* decodeURIComponent in place; the new length. */
size_t owf_diag_uri_decode(char *s, size_t n);
/* encodeURIComponent. */
void owf_diag_uri_encode(owf_buf *b, const unsigned char *s, size_t n);
/* Raw DEFLATE of unknown size into owf_malloc'd bytes with a NUL after them. */
int owf_diag_inflate_any(const unsigned char *in, size_t n, unsigned char **out, size_t *out_n);
/* Text as an XML attribute value or element text: &, <, >, " escaped, line breaks and tabs as references. */
void owf_diag_put_value(owf_buf *b, const char *s);

/* Integer square root (owf_dsvg.c). */
long long owf_diag_isqrt(long long v);
/* The point at frac (hundredths: -100 the start, 0 the middle, 100 the end) of the way along a path of n points. */
owf_dpoint owf_diag_along(const owf_dpoint *pts, int n, long frac);

#endif
