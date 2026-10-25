/*
 * libowf: what the formula engine's function files share. owf_calc_fn.c
 * has the first functions, the helpers below and the table of every
 * function (in alphabetical order); the files beside it have the rest, by
 * kind:
 *
 *   owf_calc_fnmath.c   maths and trigonometry, matrices, SUBTOTAL
 *   owf_calc_fnstat.c   statistics and distributions
 *   owf_calc_fntext.c   text, logic and information
 *   owf_calc_fndate.c   dates and times
 *   owf_calc_fnref.c    lookup and reference
 *   owf_calc_fnfin.c    money
 *
 * Each function has Excel's name, argument count and value rules; where
 * Excel's documentation and OpenFormula (ODF 1.2 part 2) differ, the
 * function follows Excel's and its comment says so.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_CALC_FN_H
#define OWF_CALC_FN_H

#include "owf_calc_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NOERR (-1)                       /* OWF_ERROR_NULL is 0 */
#define PI_ 3.14159265358979323846

/* ---- Arguments ---- */

/* Argument i is there and not left out. */
int calc_given(int argc, const calc_value *args, int i);
/* A number argument: absent, the default; left out (F(1,)), 0. 0 or an
 * error. */
int calc_num_arg(calc_ctx *ctx, calc_value *args, int argc, int i, double def, double *out);
/* Text from a buffer into the arena as the result (the buffer is freed). */
void calc_take_text(calc_ctx *ctx, owf_buf *b, calc_value *r);

/* In a function: (i, default, variable); on an error the result is it. */
#define NUM(i, def, var)                                                                                     \
    do {                                                                                                     \
        int e_ = calc_num_arg(ctx, args, argc, (i), (def), &(var));                                          \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

#define TEXT(i, p, n)                                                                                        \
    do {                                                                                                     \
        int e_ = 0;                                                                                          \
        if ((i) < argc)                                                                                      \
            e_ = calc_to_text(ctx, &args[i], &(p), &(n));                                                    \
        else {                                                                                               \
            (p) = "";                                                                                        \
            (n) = 0;                                                                                         \
        }                                                                                                    \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

#define BOOL(i, def, var)                                                                                    \
    do {                                                                                                     \
        if ((i) >= argc)                                                                                     \
            (var) = (def);                                                                                   \
        else {                                                                                               \
            int e_ = calc_to_bool(ctx, &args[i], &(var));                                                    \
            if (e_) {                                                                                        \
                cv_err(r, e_);                                                                               \
                return;                                                                                      \
            }                                                                                                \
        }                                                                                                    \
    } while (0)

/* An argument's error, if it is one. */
#define PASS_ERROR(i)                                                                                        \
    do {                                                                                                     \
        if ((i) < argc && args[i].type == CV_ERR) {                                                          \
            *r = args[i];                                                                                    \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

/* ---- Numbers in the arguments ---- */

/* fn gets each number; it returns NOERR to go on or an error to stop. */
typedef int (*calc_num_fn)(void *user, double v);

/* The numbers in args[from...], as SUM takes them: in references and
 * arrays only numbers (text, booleans and empty cells are passed over);
 * given directly, numbers, booleans and text that reads as a number (other
 * text is #VALUE!), and a left-out argument is 0. An error anywhere stops
 * it. NOERR, or the error. */
int calc_each_number(calc_ctx *ctx, calc_value *args, int from, int argc, calc_num_fn fn, void *user);

/* A list of numbers in the arena. */
typedef struct {
    calc_ctx *ctx;
    double *v;
    unsigned long n, cap;
} calc_numlist;

/* The numbers of args[from...argc - 1] (as calc_each_number), in order. */
int calc_collect(calc_ctx *ctx, calc_value *args, int from, int argc, calc_numlist *l);
int calc_numlist_add(void *user, double x);   /* a calc_num_fn */
void calc_sort_numbers(double *a, unsigned long n);   /* upwards; heapsort */

#define COLLECT(l, from, to)                                                                                 \
    do {                                                                                                     \
        int e_ = calc_collect(ctx, args, (from), (to), &(l));                                                \
        if (e_ != NOERR) {                                                                                   \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

/* x to `digits` decimals: mode 0 half away from zero (ROUND), 1 away from
 * zero (ROUNDUP), 2 toward zero (ROUNDDOWN), as Excel's 15 digits see it. */
double calc_round_to(double x, double digits, int mode);

/* log(1 + x) and exp(x) - 1 without losing a small x's digits (only log
 * and exp: the Amiga's maths library has no log1p or expm1). */
double calc_log1p(double x);
double calc_expm1(double x);
/* (1 + rate)^n and (1 + rate)^n - 1. */
void calc_growth(double rate, double n, double *g, double *g1);

/* Argument 0 as a serial date (0 to 31 December 9999), or an error. */
int calc_serial_arg(calc_ctx *ctx, calc_value *args, int argc, double *out);

/* ---- Tables (lookup) ---- */

/* A reference's one area (cut to the cells the book uses), or an array. */
typedef struct {
    calc_ctx *ctx;
    const calc_area *a;
    const calc_array *arr;
    unsigned rows, cols;
} calc_table;

int calc_table_of(calc_ctx *ctx, const calc_value *v, calc_table *t);
void calc_table_get(const calc_table *t, unsigned i, unsigned j, calc_value *out);
/* Text with a wildcard in it (* ? ~). */
int calc_has_wild(const calc_value *v);
/* Where key is along a row (by_row) or column (fixed: which) of a table:
 * mode 0 exact (wildcards in text), 1 the last at or below (sorted up, by
 * halves, as Excel), -1 the last at or above (sorted down). -1 for none. */
long calc_lookup(const calc_table *t, const calc_value *key, int by_row, unsigned fixed, int mode);

/* ---- Distributions (owf_calc_fnstat.c), for the other files ---- */

/* The standard normal's distribution and its inverse (p in (0, 1)). */
double calc_norm_cdf(double z);
double calc_norm_inv(double p);

/* ---- The functions of the files beside owf_calc_fn.c ---- */

#define CF(name) void cf_##name(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)

/* maths */
CF(acosh); CF(arabic); CF(asinh); CF(atanh); CF(base); CF(ceiling_math); CF(ceiling_precise); CF(combin);
CF(combina); CF(decimal); CF(factdouble); CF(floor_math); CF(floor_precise); CF(gcd); CF(lcm); CF(mdeterm);
CF(minverse); CF(mmult); CF(mround); CF(multinomial); CF(permut); CF(permutationa); CF(quotient);
CF(randbetween); CF(roman); CF(seriessum); CF(sqrtpi); CF(subtotal); CF(sumx2my2); CF(sumx2py2); CF(sumxmy2);
/* statistics */
CF(averagea); CF(avedev); CF(binom_dist); CF(correl); CF(covariance_p); CF(covariance_s);
CF(devsq); CF(expon_dist); CF(forecast); CF(frequency); CF(geomean); CF(harmean); CF(intercept); CF(maxa);
CF(mina); CF(mode); CF(mode_mult); CF(norm_dist); CF(norm_inv); CF(norm_s_dist); CF(norm_s_inv); CF(normsdist);
CF(percentile_exc); CF(percentile_inc); CF(percentrank_exc); CF(percentrank_inc); CF(poisson_dist);
CF(quartile_exc); CF(quartile_inc); CF(rank_avg); CF(rank_eq); CF(rsq); CF(slope); CF(standardize); CF(stdeva);
CF(stdevpa); CF(t_dist); CF(t_dist_2t); CF(t_dist_rt); CF(t_inv); CF(t_inv_2t); CF(tdist); CF(trimmean);
CF(vara); CF(varpa);
/* text, logic and information */
CF(cell); CF(concat); CF(dollar); CF(error_type); CF(fixed); CF(ifs); CF(iseven); CF(isformula); CF(isodd);
CF(isref); CF(numbervalue); CF(sheet); CF(sheets); CF(switch); CF(textjoin); CF(type); CF(unichar);
CF(unicode);
/* dates and times */
CF(datedif); CF(datevalue); CF(days); CF(days360); CF(edate); CF(eomonth); CF(isoweeknum); CF(networkdays);
CF(networkdays_intl); CF(timevalue); CF(weeknum); CF(workday); CF(workday_intl); CF(yearfrac);
/* lookup and reference */
CF(address); CF(areas); CF(formulatext); CF(hyperlink); CF(indirect); CF(lookup); CF(offset); CF(transpose);
CF(xlookup); CF(xmatch);
/* money */
CF(cumipmt); CF(cumprinc); CF(db); CF(ddb); CF(effect); CF(fvschedule); CF(ipmt); CF(irr); CF(mirr);
CF(nominal); CF(nper); CF(ppmt); CF(rate); CF(sln); CF(syd); CF(xirr); CF(xnpv);

#endif
