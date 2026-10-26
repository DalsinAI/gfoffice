/*
 * owf.library's maths (owf_libm.c): the <math.h> calls the formula engine
 * makes, and strtod, named owf_rt_ so the PC's tests can hold them beside
 * the C library's; owf_runtime_math.c gives them their C names inside the
 * library. Plain C on doubles, with no FPU, no long double and no other
 * library; no state, so any number of tasks can call them at once.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_LIBM_H
#define OWF_LIBM_H

/* exact */
double owf_rt_fabs(double x);
double owf_rt_floor(double x);
double owf_rt_ceil(double x);
double owf_rt_fmod(double x, double y);
double owf_rt_sqrt(double x);              /* correctly rounded */

/* within about one unit in the last place (tests/library/test_owf_libm.c
 * measures each against the PC's) */
double owf_rt_exp(double x);
double owf_rt_log(double x);
double owf_rt_log10(double x);
double owf_rt_pow(double x, double y);
double owf_rt_sin(double x);
double owf_rt_cos(double x);
double owf_rt_tan(double x);
double owf_rt_asin(double x);
double owf_rt_acos(double x);
double owf_rt_atan(double x);
double owf_rt_atan2(double y, double x);
double owf_rt_sinh(double x);
double owf_rt_cosh(double x);
double owf_rt_tanh(double x);

/* strtod: decimal numbers, correctly rounded (libowf's owf_number_parse),
 * after any white space; "inf", "infinity" and "nan" in any case. No hex
 * numbers. Out of range gives an infinity or zero with its sign. */
double owf_rt_strtod(const char *s, char **end);

#endif
