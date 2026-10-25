/*
 * libowf: the formula engine's money functions beyond PMT, PV, FV and NPV
 * (owf_calc_fn.c): IPMT, PPMT, CUMIPMT, CUMPRINC, NPER, RATE, IRR, XIRR,
 * XNPV, MIRR, FVSCHEDULE, EFFECT, NOMINAL, and depreciation (SLN, SYD, DB,
 * DDB). Excel's names, argument counts and errors (owf_calc_fn.h).
 *
 * RATE, IRR and XIRR use Newton's method from the guess, to 1e-10, with
 * Excel's documented limits on tries (20, 20 and 100); past them, #NUM!.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_fn.h"

#define CONVERGED 1e-10

static double trunc_(double x)
{
    return x < 0 ? -floor(-x) : floor(x);
}

/* PMT and FV as owf_calc_fn.c works them out (type 0 or 1). */
static double pmt_(double rate, double n, double pv, double fv, double type)
{
    double g, g1;
    if (rate == 0)
        return -(pv + fv) / n;
    calc_growth(rate, n, &g, &g1);
    return -(rate * (fv + pv * g)) / ((1 + rate * type) * g1);
}

static double fv_(double rate, double n, double pmt, double pv, double type)
{
    double g, g1;
    if (rate == 0)
        return -(pv + pmt * n);
    calc_growth(rate, n, &g, &g1);
    return -(pv * g + pmt * (1 + rate * type) * g1 / rate);
}

/* The interest in payment `per` (1 to n): the balance after per - 1
 * payments times the rate (paid at the start of each period: none in the
 * first, and a period's less). */
static double ipmt_(double rate, double per, double n, double pv, double fv, double type)
{
    double pmt = pmt_(rate, n, pv, fv, type), i;
    if (type && per == 1)
        return 0;
    i = fv_(rate, per - 1, pmt, pv, type) * rate;
    return type ? i / (1 + rate) : i;
}

/* IPMT, PPMT(rate, per, nper, pv, [fv], [type]) */
static void part_of_payment(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int principal)
{
    double rate, per, n, pv, fv, type, i;
    NUM(0, 0, rate);
    NUM(1, 0, per);
    NUM(2, 0, n);
    NUM(3, 0, pv);
    NUM(4, 0, fv);
    NUM(5, 0, type);
    type = type != 0;
    if (per < 1 || per > n || n == 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    i = ipmt_(rate, per, n, pv, fv, type);
    cv_num(r, principal ? pmt_(rate, n, pv, fv, type) - i : i);
}

CF(ipmt)
{
    part_of_payment(ctx, args, argc, r, 0);
}

CF(ppmt)
{
    part_of_payment(ctx, args, argc, r, 1);
}

/* CUMIPMT, CUMPRINC(rate, nper, pv, start, end, type): the interest or
 * principal paid from period start to end, both included. */
static void cumulative(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int principal)
{
    double rate, n, pv, start, end, type, s = 0, per;
    NUM(0, 0, rate);
    NUM(1, 0, n);
    NUM(2, 0, pv);
    NUM(3, 0, start);
    NUM(4, 0, end);
    NUM(5, 0, type);
    n = trunc_(n);
    start = trunc_(start);
    end = trunc_(end);
    type = trunc_(type);
    if (rate <= 0 || n <= 0 || pv <= 0 || start < 1 || end < 1 || start > end || end > n ||
        (type != 0 && type != 1)) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (per = start; per <= end; per++) {
        double i = ipmt_(rate, per, n, pv, 0, type);
        s += principal ? pmt_(rate, n, pv, 0, type) - i : i;
    }
    cv_num(r, s);
}

CF(cumipmt)
{
    cumulative(ctx, args, argc, r, 0);
}

CF(cumprinc)
{
    cumulative(ctx, args, argc, r, 1);
}

/* NPER(rate, pmt, pv, [fv], [type]) */
CF(nper)
{
    double rate, pmt, pv, fv, type, a, b;
    NUM(0, 0, rate);
    NUM(1, 0, pmt);
    NUM(2, 0, pv);
    NUM(3, 0, fv);
    NUM(4, 0, type);
    type = type != 0;
    if (rate == 0) {
        if (pmt == 0)
            cv_err(r, OWF_ERROR_NUM);
        else
            cv_num(r, -(pv + fv) / pmt);
        return;
    }
    a = pmt * (1 + rate * type) - fv * rate;
    b = pmt * (1 + rate * type) + pv * rate;
    if (rate <= -1 || b == 0 || a / b <= 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, log(a / b) / calc_log1p(rate));
}

/* RATE's equation, pv (1+r)^n + pmt (1 + r type) ((1+r)^n - 1) / r + fv
 * = 0, divided by ((1+r)^n - 1) / r: as a payment, it is nearly straight
 * for long loans, where the first form is all (1+r)^n and Newton's method
 * creeps (a 30-year loan from 10% needs far more than 20 steps). */
static double rate_fn(double r, double n, double pmt, double pv, double fv, double type)
{
    double g, g1;
    if (fabs(r) < 1e-12)
        return (pv + fv) / n + pmt;
    calc_growth(r, n, &g, &g1);
    return (pv * g + fv) * r / g1 + pmt * (1 + r * type);
}

/* RATE(nper, pmt, pv, [fv], [type], [guess = 0.1]): Newton's method on
 * rate_fn, the slope by central differences. */
CF(rate)
{
    double n, pmt, pv, fv, type, x;
    int i;
    NUM(0, 0, n);
    NUM(1, 0, pmt);
    NUM(2, 0, pv);
    NUM(3, 0, fv);
    NUM(4, 0, type);
    NUM(5, 0.1, x);
    if (argc > 5 && args[5].type == CV_MISSING)
        x = 0.1;
    type = type != 0;
    if (n <= 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < 20; i++) {
        double f, d, step, h = 1e-6 * (fabs(x) > 1e-4 ? fabs(x) : 1e-4);
        if (x <= -1 || x - h <= -1)
            break;
        f = rate_fn(x, n, pmt, pv, fv, type);
        d = (rate_fn(x + h, n, pmt, pv, fv, type) - rate_fn(x - h, n, pmt, pv, fv, type)) / (2 * h);
        if (d == 0 || d != d)
            break;
        step = f / d;
        x -= step;
        if (fabs(step) < CONVERGED) {
            cv_num(r, x);
            return;
        }
    }
    cv_err(r, OWF_ERROR_NUM);
}

/* The numbers of an array argument; with need_both, at least one above
 * and one below 0 (else #NUM!). */
static int cash_flows(calc_ctx *ctx, calc_value *args, int i, calc_numlist *l, int need_both)
{
    unsigned long k;
    int pos = 0, neg = 0, e;
    if (args[i].type == CV_ERR)
        return args[i].u.err;
    if ((e = calc_collect(ctx, args, i, i + 1, l)) != NOERR)
        return e;
    for (k = 0; k < l->n; k++) {
        pos |= l->v[k] > 0;
        neg |= l->v[k] < 0;
    }
    return need_both && !(pos && neg) ? OWF_ERROR_NUM : NOERR;
}

/* IRR(values, [guess = 0.1]) */
CF(irr)
{
    calc_numlist l;
    double x;
    unsigned long k;
    int i, e;
    if ((e = cash_flows(ctx, args, 0, &l, 1)) != NOERR) {
        cv_err(r, e);
        return;
    }
    NUM(1, 0.1, x);
    if (argc > 1 && args[1].type == CV_MISSING)
        x = 0.1;
    for (i = 0; i < 20; i++) {
        double f = 0, d = 0, v = 1, step;
        if (x <= -1)
            break;
        for (k = 0; k < l.n; k++) {      /* sum of c / (1+x)^k and its derivative */
            f += l.v[k] * v;
            d -= (double)k * l.v[k] * v / (1 + x);
            v /= 1 + x;
        }
        if (d == 0)
            break;
        step = f / d;
        x -= step;
        if (fabs(step) < CONVERGED) {
            cv_num(r, x);
            return;
        }
    }
    cv_err(r, OWF_ERROR_NUM);
}

/* XNPV's and XIRR's values and dates: as many of each, all numbers (else
 * #VALUE!), no date before the first (else #NUM!); dates truncated. */
static int dated_flows(calc_ctx *ctx, calc_value *args, double **v, double **t, unsigned long *n)
{
    calc_array *a, *b;
    unsigned long k;
    int e;
    if (args[0].type == CV_ERR)
        return args[0].u.err;
    if (args[1].type == CV_ERR)
        return args[1].u.err;
    if ((e = calc_to_array(ctx, &args[0], &a)) != 0 || (e = calc_to_array(ctx, &args[1], &b)) != 0)
        return e;
    *n = (unsigned long)a->rows * a->cols;
    if (*n != (unsigned long)b->rows * b->cols)
        return OWF_ERROR_NUM;
    *v = calc_arena_alloc(ctx->arena, (*n ? *n : 1) * sizeof(double));
    *t = calc_arena_alloc(ctx->arena, (*n ? *n : 1) * sizeof(double));
    if (!*v || !*t)
        return OWF_ERROR_NUM;
    for (k = 0; k < *n; k++) {
        const calc_value *x = &a->v[k], *d = &b->v[k];
        if (x->type == CV_ERR)
            return x->u.err;
        if (d->type == CV_ERR)
            return d->u.err;
        if (x->type != CV_NUM || d->type != CV_NUM)
            return OWF_ERROR_VALUE;
        (*v)[k] = x->u.num;
        (*t)[k] = floor(d->u.num);
    }
    for (k = 0; k < *n; k++) {
        double t0 = (*t)[0];
        if ((*t)[k] < t0 || (*t)[k] < 0)
            return OWF_ERROR_NUM;
    }
    for (k = *n; k-- > 0;)                /* years from the first (the first last) */
        (*t)[k] = ((*t)[k] - (*t)[0]) / 365;
    return *n ? NOERR : OWF_ERROR_NUM;
}

/* sum of v / (1 + rate)^t, and its derivative */
static double xnpv_(const double *v, const double *t, unsigned long n, double rate, double *deriv)
{
    double f = 0, d = 0, l = calc_log1p(rate);
    unsigned long k;
    for (k = 0; k < n; k++) {
        double p = exp(-t[k] * l);
        f += v[k] * p;
        d -= t[k] * v[k] * p / (1 + rate);
    }
    if (deriv)
        *deriv = d;
    return f;
}

/* XNPV(rate, values, dates) */
CF(xnpv)
{
    double rate, *v, *t;
    unsigned long n;
    int e;
    NUM(0, 0, rate);
    if (rate <= -1) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if ((e = dated_flows(ctx, args + 1, &v, &t, &n)) != NOERR) {
        cv_err(r, e);
        return;
    }
    cv_num(r, xnpv_(v, t, n, rate, NULL));
}

/* XIRR(values, dates, [guess = 0.1]) */
CF(xirr)
{
    double x, *v, *t;
    unsigned long n, k;
    int e, i, pos = 0, neg = 0;
    if ((e = dated_flows(ctx, args, &v, &t, &n)) != NOERR) {
        cv_err(r, e);
        return;
    }
    for (k = 0; k < n; k++) {
        pos |= v[k] > 0;
        neg |= v[k] < 0;
    }
    if (!pos || !neg) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    NUM(2, 0.1, x);
    if (argc > 2 && args[2].type == CV_MISSING)
        x = 0.1;
    for (i = 0; i < 100; i++) {
        double d, f, step;
        if (x <= -1)
            break;
        f = xnpv_(v, t, n, x, &d);
        if (d == 0)
            break;
        step = f / d;
        x -= step;
        if (fabs(step) < CONVERGED) {
            cv_num(r, x);
            return;
        }
    }
    cv_err(r, OWF_ERROR_NUM);
}

/* MIRR(values, finance_rate, reinvest_rate): the positive flows grown to
 * the end at the reinvestment rate, the negative ones brought to the start
 * at the finance rate; without both kinds, #DIV/0!. */
CF(mirr)
{
    calc_numlist l;
    double fr, rr, pos = 0, neg = 0;
    unsigned long k;
    int e;
    NUM(1, 0, fr);
    NUM(2, 0, rr);
    if ((e = cash_flows(ctx, args, 0, &l, 0)) != NOERR) {
        cv_err(r, e);
        return;
    }
    for (k = 0; k < l.n; k++) {
        if (l.v[k] > 0)
            pos += l.v[k] * pow(1 + rr, (double)(l.n - 1 - k));
        else
            neg += l.v[k] / pow(1 + fr, (double)k);
    }
    if (pos == 0 || neg == 0 || l.n < 2) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    cv_num(r, pow(pos / -neg, 1.0 / (double)(l.n - 1)) - 1);
}

/* FVSCHEDULE(principal, schedule): grown by each rate in turn; an empty
 * cell is 0, text is #VALUE!. */
CF(fvschedule)
{
    double p;
    calc_array *a;
    unsigned long k, n;
    int e;
    (void)argc;
    NUM(0, 0, p);
    PASS_ERROR(1);
    if ((e = calc_to_array(ctx, &args[1], &a)) != 0) {
        cv_err(r, e);
        return;
    }
    n = (unsigned long)a->rows * a->cols;
    for (k = 0; k < n; k++) {
        const calc_value *x = &a->v[k];
        if (x->type == CV_ERR) {
            cv_err(r, x->u.err);
            return;
        }
        if (x->type == CV_NUM)
            p *= 1 + x->u.num;
        else if (x->type != CV_EMPTY) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
    }
    cv_num(r, p);
}

/* EFFECT(nominal, npery), NOMINAL(effect, npery): npery truncated. */
CF(effect)
{
    double rate, n, g, g1;
    NUM(0, 0, rate);
    NUM(1, 0, n);
    n = trunc_(n);
    if (rate <= 0 || n < 1) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    calc_growth(rate / n, n, &g, &g1);
    cv_num(r, g1);
}

CF(nominal)
{
    double rate, n;
    NUM(0, 0, rate);
    NUM(1, 0, n);
    n = trunc_(n);
    if (rate <= 0 || n < 1) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, n * calc_expm1(calc_log1p(rate) / n));
}

/* ---- Depreciation ---- */

CF(sln)
{
    double cost, salvage, life;
    NUM(0, 0, cost);
    NUM(1, 0, salvage);
    NUM(2, 0, life);
    if (life == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, (cost - salvage) / life);
}

CF(syd)
{
    double cost, salvage, life, per;
    NUM(0, 0, cost);
    NUM(1, 0, salvage);
    NUM(2, 0, life);
    NUM(3, 0, per);
    if (life <= 0 || per <= 0 || per > life)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, (cost - salvage) * (life - per + 1) * 2 / (life * (life + 1)));
}

/* DB(cost, salvage, life, period, [month = 12]): fixed-declining balance,
 * the rate 1 - (salvage / cost)^(1 / life) rounded to three decimals, as
 * Excel's documentation gives it; the first year has `month` months, and a
 * year past life the months left. */
CF(db)
{
    double cost, salvage, life, period, month, rate, total = 0, dep = 0, p;
    NUM(0, 0, cost);
    NUM(1, 0, salvage);
    NUM(2, 0, life);
    NUM(3, 0, period);
    NUM(4, 12, month);
    period = trunc_(period);
    month = trunc_(month);
    if (cost <= 0 || salvage < 0 || life <= 0 || period < 1 || month < 1 || month > 12 ||
        period > (month < 12 ? life + 1 : life)) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    rate = calc_round_to(1 - pow(salvage / cost, 1 / life), 3, 0);
    for (p = 1; p <= period; p++) {
        if (p == 1)
            dep = cost * rate * month / 12;
        else if (p > life)
            dep = (cost - total) * rate * (12 - month) / 12;
        else
            dep = (cost - total) * rate;
        total += dep;
    }
    cv_num(r, dep);
}

/* DDB(cost, salvage, life, period, [factor = 2]): declining balance, each
 * period's depreciation the book value times factor / life, but never
 * below salvage. */
CF(ddb)
{
    double cost, salvage, life, period, factor, rate, book, dep = 0, p;
    NUM(0, 0, cost);
    NUM(1, 0, salvage);
    NUM(2, 0, life);
    NUM(3, 0, period);
    NUM(4, 2, factor);
    if (cost < 0 || salvage < 0 || life <= 0 || period <= 0 || factor <= 0 || period > life) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    rate = factor / life;
    if (rate > 1)
        rate = 1;
    if (period != floor(period)) {       /* a fraction of a period: the balance as a power */
        book = cost * pow(1 - rate, period - 1);
        dep = book * rate;
        if (dep > book - salvage)
            dep = book - salvage;
        cv_num(r, dep < 0 ? 0 : dep);
        return;
    }
    book = cost;
    for (p = 1; p <= period; p++) {
        dep = book * rate;
        if (dep > book - salvage)
            dep = book - salvage;
        if (dep < 0)
            dep = 0;
        book -= dep;
    }
    cv_num(r, dep);
}
