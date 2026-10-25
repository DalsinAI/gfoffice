/*
 * The <math.h> calls and strtod for GFOffice's libraries (owf.library and
 * the four of docs/suite/owf-libraries.md), under their C names: libowf's
 * own (owf_libm.c), over its own double arithmetic (owf_softdouble.c). A
 * file of their own, so a library whose code calls none of them (built
 * from an archive by build-library.sh) links none of the maths either.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_libm.h"

/* Declared here, not through <math.h>, so no C library's header shapes them. */

double fabs(double x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double sqrt(double x);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double strtod(const char *s, char **end);

double fabs(double x) { return owf_rt_fabs(x); }
double floor(double x) { return owf_rt_floor(x); }
double ceil(double x) { return owf_rt_ceil(x); }
double fmod(double x, double y) { return owf_rt_fmod(x, y); }
double sqrt(double x) { return owf_rt_sqrt(x); }
double exp(double x) { return owf_rt_exp(x); }
double log(double x) { return owf_rt_log(x); }
double log10(double x) { return owf_rt_log10(x); }
double pow(double x, double y) { return owf_rt_pow(x, y); }
double sin(double x) { return owf_rt_sin(x); }
double cos(double x) { return owf_rt_cos(x); }
double tan(double x) { return owf_rt_tan(x); }
double asin(double x) { return owf_rt_asin(x); }
double acos(double x) { return owf_rt_acos(x); }
double atan(double x) { return owf_rt_atan(x); }
double atan2(double y, double x) { return owf_rt_atan2(y, x); }
double sinh(double x) { return owf_rt_sinh(x); }
double cosh(double x) { return owf_rt_cosh(x); }
double tanh(double x) { return owf_rt_tanh(x); }
double strtod(const char *s, char **end) { return owf_rt_strtod(s, end); }
