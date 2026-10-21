/*
 * owf.library's own C library calls, named owf_rt_ so the PC tests can hold
 * them beside the real ones; owf_runtime.c gives them their C names inside
 * the library. Taken from weblayout's (DalsinAI/openamigabrowser
 * src/weblayout/wl_libc.c), with %o and strtoul added for libowf.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_LIBC_H
#define OWF_LIBC_H

#include <stdarg.h>
#include <stddef.h>

int owf_rt_strcasecmp(const char *a, const char *b);
int owf_rt_strncasecmp(const char *a, const char *b, size_t n);
/* strtol and strtoul: base 0 (decimal, 0 octal, 0x hex) or 2 to 36; clamp
 * at the type's limits as the C library does. */
long owf_rt_strtol(const char *s, char **end, int base);
unsigned long owf_rt_strtoul(const char *s, char **end, int base);
/* snprintf for what libowf writes: %s, %.Ns, %.*s, %c, %d, %i, %u, %o,
 * %x, %X, with l, a width, and the '0' and '-' flags; %%. Always
 * terminated when size > 0; returns the length the whole would have. */
int owf_rt_vsnprintf(char *out, size_t size, const char *fmt, va_list ap);

#endif
