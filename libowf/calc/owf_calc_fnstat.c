/*
 * libowf: the formula engine's statistics beyond the first ones
 * (owf_calc_fn.c): modes, ranks, percentiles and quartiles, the "A"
 * functions, correlation and regression, the normal, binomial, Poisson,
 * exponential and Student's t distributions. Excel's names, argument
 * counts and errors (owf_calc_fn.h).
 *
 * The special functions are our own, from their mathematical definitions,
 * with nothing but exp, log, sqrt, pow and the trigonometric functions of
 * <math.h> (the Amiga's maths library has no erf or lgamma): log gamma by
 * Stirling's series (Bernoulli numbers) after the recurrence; erfc by its
 * series and its continued fraction; the regularised incomplete beta
 * function by its continued fraction (DLMF 8.17.22), worked by Lentz's
 * method; inverses by Halley's or Newton's method inside a bracket.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_fn.h"

#define SQRT2PI 2.50662827463100050242
#define LN_SQRT2PI 0.91893853320467274178
#define TINY 1e-300

/* ---- Special functions ---- */

/* log Gamma(x), x > 0. */
static double lgam(double x)
{
    double p = 1, z, s;
    while (x < 15) {
        p *= x;
        x += 1;
    }
    z = 1 / (x * x);
    s = (1.0 / 12 - z * (1.0 / 360 - z * (1.0 / 1260 - z * (1.0 / 1680 - z * (1.0 / 1188 -
         z * (691.0 / 360360 - z * (1.0 / 156 - z * 3617.0 / 122400))))))) / x;
    return (x - 0.5) * log(x) - x + LN_SQRT2PI + s - log(p);
}

/* erfc(x): for x below 1, 1 - erf(x) from erf's series of positive terms;
 * from 1 on, the continued fraction (it keeps the digits of a small
 * result). */
static double erfc_(double x)
{
    if (x < 0)
        return 2 - erfc_(-x);
    if (x < 1) {
        double term = x, sum = x, x2 = 2 * x * x;
        int n;
        for (n = 0; n < 200 && term > sum * 1e-17; n++) {
            term *= x2 / (2 * n + 3);
            sum += term;
        }
        return 1 - 1.12837916709551257390 * exp(-x * x) * sum;
    } else {
        /* erfc(x) = exp(-x^2) / sqrt(pi) / (x + (1/2)/(x + 1/(x + (3/2)/(x + ...)))) */
        double f = x, c = x, d = 0, delta;
        int n;
        for (n = 1; n < 5000; n++) {
            double a = n * 0.5;
            d = x + a * d;
            if (fabs(d) < TINY)
                d = TINY;
            c = x + a / c;
            if (fabs(c) < TINY)
                c = TINY;
            d = 1 / d;
            delta = c * d;
            f *= delta;
            if (fabs(delta - 1) < 1e-16)
                break;
        }
        return exp(-x * x) / (1.77245385090551602730 * f);
    }
}

double calc_norm_cdf(double z)
{
    return 0.5 * erfc_(-z * 0.70710678118654752440);
}

static double norm_pdf(double z)
{
    return exp(-0.5 * z * z) / SQRT2PI;
}

/* The standard normal's quantile: Abramowitz and Stegun 26.2.23 to start
 * (error under 4.5e-4), then Halley's method on our own cdf. */
double calc_norm_inv(double p)
{
    double q = p < 0.5 ? p : 1 - p, t, z;
    int i;
    if (p == 0.5)
        return 0;
    t = sqrt(-2 * log(q));
    z = -(t - (2.515517 + t * (0.802853 + t * 0.010328)) / (1 + t * (1.432788 + t * (0.189269 + t * 0.001308))));
    /* z is now the lower tail's quantile for q */
    for (i = 0; i < 8; i++) {
        double rr = (calc_norm_cdf(z) - q) / norm_pdf(z), dz = rr / (1 + z * rr / 2);
        z -= dz;
        if (fabs(dz) <= 1e-15 * fabs(z))
            break;
    }
    return p < 0.5 ? z : -z;
}

/* The regularised incomplete beta function I_x(a, b), with y = 1 - x
 * given too so that it keeps its digits. */
static double beta_cf(double a, double b, double x)
{
    double c = 1, d = 1 - (a + b) * x / (a + 1), h;
    int m;
    if (fabs(d) < TINY)
        d = TINY;
    d = 1 / d;
    h = d;
    for (m = 1; m <= 20000; m++) {
        double aa = m * (b - m) * x / ((a + 2 * m - 1) * (a + 2 * m)), del;
        d = 1 + aa * d;
        if (fabs(d) < TINY)
            d = TINY;
        c = 1 + aa / c;
        if (fabs(c) < TINY)
            c = TINY;
        d = 1 / d;
        h *= d * c;
        aa = -(a + m) * (a + b + m) * x / ((a + 2 * m) * (a + 2 * m + 1));
        d = 1 + aa * d;
        if (fabs(d) < TINY)
            d = TINY;
        c = 1 + aa / c;
        if (fabs(c) < TINY)
            c = TINY;
        d = 1 / d;
        del = d * c;
        h *= del;
        if (fabs(del - 1) < 1e-16)
            break;
    }
    return h;
}

static double ibeta(double x, double y, double a, double b)
{
    double front;
    if (x <= 0)
        return 0;
    if (y <= 0)
        return 1;
    if (x > (a + 1) / (a + b + 2))
        return 1 - ibeta(y, x, b, a);
    front = exp(a * log(x) + b * log(y) + lgam(a + b) - lgam(a) - lgam(b));
    return front * beta_cf(a, b, x) / a;
}

/* Student's t: P(T > t) for t >= 0, and the density. */
static double t_tail(double t, double df)
{
    double t2 = t * t;
    return 0.5 * ibeta(df / (df + t2), t2 / (df + t2), df / 2, 0.5);
}

static double t_cdf(double t, double df)
{
    return t >= 0 ? 1 - t_tail(t, df) : t_tail(-t, df);
}

static double t_pdf(double t, double df)
{
    return exp(lgam((df + 1) / 2) - lgam(df / 2) - (df + 1) / 2 * calc_log1p(t * t / df)) / sqrt(df * PI_);
}

/* The t > 0 with P(T > t) = q (0 < q < 0.5): Newton's method on log P,
 * inside a bracket that halves when a step would leave it. */
static double t_tail_inv(double q, double df)
{
    double z = -calc_norm_inv(q), t, lo = 0, hi = HUGE_VAL;
    int i;
    if (df == 1)
        return 1 / tan(PI_ * q);
    if (df == 2)
        return (1 - 2 * q) / sqrt(2 * q * (1 - q));
    t = z + (z * z * z + z) / (4 * df);   /* Cornish-Fisher's first term */
    for (i = 0; i < 200; i++) {
        double p = t_tail(t, df), step, next;
        if (p > q)
            lo = t;
        else
            hi = t;
        step = (log(p) - log(q)) * p / t_pdf(t, df);
        next = t + step;
        if (!(next > lo && next < hi))
            next = hi == HUGE_VAL ? 2 * t + 1 : (lo + hi) / 2;
        if (fabs(next - t) <= 1e-15 * next) {
            t = next;
            break;
        }
        t = next;
    }
    return t;
}

/* ---- Helpers ---- */

static void give_array(calc_value *r, calc_array *a)
{
    r->type = CV_ARRAY;
    r->u.arr = a;
}

/* The numbers of one argument (a range, an array or a value), sorted. */
static int sorted_numbers(calc_ctx *ctx, calc_value *args, int i, calc_numlist *l)
{
    int e = calc_collect(ctx, args, i, i + 1, l);
    if (e == NOERR)
        calc_sort_numbers(l->v, l->n);
    return e;
}

#define SORTED(i, l)                                                                                         \
    do {                                                                                                     \
        int e_ = sorted_numbers(ctx, args, (i), &(l));                                                       \
        if (e_ != NOERR) {                                                                                   \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

static double mean_of(const calc_numlist *l)
{
    double s = 0;
    unsigned long i;
    for (i = 0; i < l->n; i++)
        s += l->v[i];
    return s / (double)l->n;
}

/* ---- Modes, ranks, percentiles ---- */

/* MODE, MODE.SNGL: the number that comes most often (of those that come
 * as often, the first); none twice is #N/A. MODE.MULT: all of them, in the
 * order they first come, as a column. */
static void modes(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int all)
{
    calc_numlist l;
    double *s;
    unsigned char *done;
    unsigned long i, best = 1, nbest = 0, k;
    calc_array *out;
    COLLECT(l, 0, argc);
    if (!l.n) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    s = calc_arena_alloc(ctx->arena, l.n * sizeof *s);
    done = calc_arena_alloc(ctx->arena, l.n);
    if (!s || !done) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    memcpy(s, l.v, l.n * sizeof *s);
    calc_sort_numbers(s, l.n);
    for (i = 0; i < l.n;) {              /* the most times any number comes */
        unsigned long j = i;
        while (j < l.n && s[j] == s[i])
            j++;
        if (j - i > best) {
            best = j - i;
            nbest = 0;
        }
        if (j - i == best)
            nbest++;
        i = j;
    }
    if (best < 2) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    if (!all)
        nbest = 1;
    out = calc_array_new(ctx, (unsigned)nbest, 1);
    if (!out) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    memset(done, 0, l.n);
    for (i = 0, k = 0; i < l.n && k < nbest; i++) {
        double x = l.v[i];
        unsigned long lo = 0, hi = l.n, first, end;
        /* where x's run starts and ends in the sorted copy, by halves */
        while (lo < hi) {
            unsigned long mid = lo + (hi - lo) / 2;
            if (s[mid] < x)
                lo = mid + 1;
            else
                hi = mid;
        }
        first = lo;
        hi = l.n;
        while (lo < hi) {
            unsigned long mid = lo + (hi - lo) / 2;
            if (s[mid] <= x)
                lo = mid + 1;
            else
                hi = mid;
        }
        end = lo;
        if (end - first != best || done[first])
            continue;
        done[first] = 1;                 /* each mode once, where it first comes */
        cv_num(&out->v[k++], x);
    }
    if (all)
        give_array(r, out);
    else
        *r = out->v[0];
}

CF(mode)
{
    modes(ctx, args, argc, r, 0);
}

CF(mode_mult)
{
    modes(ctx, args, argc, r, 1);
}

/* RANK.EQ, RANK.AVG(number, ref, [order]): the number's place among the
 * numbers in ref, largest first (order 0) or smallest; ties share the
 * best place (EQ) or the average of theirs (AVG). Not there: #N/A. */
static void rank(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int avg)
{
    calc_numlist l;
    double x, order, better = 0, same = 0;
    unsigned long i;
    int e;
    NUM(0, 0, x);
    PASS_ERROR(1);
    if (args[1].type != CV_REF && args[1].type != CV_ARRAY) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if ((e = calc_collect(ctx, args, 1, 2, &l)) != NOERR) {
        cv_err(r, e);
        return;
    }
    NUM(2, 0, order);
    for (i = 0; i < l.n; i++) {
        if (calc_approx_equal(l.v[i], x))
            same++;
        else if (order == 0 ? l.v[i] > x : l.v[i] < x)
            better++;
    }
    if (!same)
        cv_err(r, OWF_ERROR_NA);
    else
        cv_num(r, avg ? better + (same + 1) / 2 : better + 1);
}

CF(rank_eq)
{
    rank(ctx, args, argc, r, 0);
}

CF(rank_avg)
{
    rank(ctx, args, argc, r, 1);
}

/* The k-th percentile of sorted numbers: inclusive ((n - 1) k) or
 * exclusive ((n + 1) k - 1), between the two either side. */
static void percentile(calc_ctx *ctx, calc_value *args, calc_value *r, double k, int exc)
{
    calc_numlist l;
    double h, i, f;
    int argc = 1;
    SORTED(0, l);
    if (!l.n || (exc ? k <= 0 || k >= 1 : k < 0 || k > 1)) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    h = exc ? (double)(l.n + 1) * k - 1 : (double)(l.n - 1) * k;
    if (calc_approx_equal(h, floor(h + 0.5)))
        h = floor(h + 0.5);
    if (h < 0 || h > (double)(l.n - 1)) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    i = floor(h);
    f = h - i;
    cv_num(r, f == 0 ? l.v[(unsigned long)i] : l.v[(unsigned long)i] + f * (l.v[(unsigned long)i + 1] - l.v[(unsigned long)i]));
    (void)argc;
}

CF(percentile_inc)
{
    double k;
    PASS_ERROR(0);
    NUM(1, 0, k);
    percentile(ctx, args, r, k, 0);
}

CF(percentile_exc)
{
    double k;
    PASS_ERROR(0);
    NUM(1, 0, k);
    percentile(ctx, args, r, k, 1);
}

CF(quartile_inc)
{
    double q;
    PASS_ERROR(0);
    NUM(1, 0, q);
    q = q < 0 ? -floor(-q) : floor(q);
    if (q < 0 || q > 4) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    percentile(ctx, args, r, q / 4, 0);
}

CF(quartile_exc)
{
    double q;
    PASS_ERROR(0);
    NUM(1, 0, q);
    q = q < 0 ? -floor(-q) : floor(q);
    if (q <= 0 || q >= 4) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    percentile(ctx, args, r, q / 4, 1);
}

/* PERCENTRANK.INC, .EXC(array, x, [significance = 3]): x's rank as a
 * fraction, between neighbours when x is not in the array, cut (not
 * rounded) to the significance's digits. Outside the numbers: #N/A. */
static void percentrank(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int exc)
{
    calc_numlist l;
    double x, sig, below = 0, rank, p;
    unsigned long i, n;
    PASS_ERROR(0);
    NUM(1, 0, x);
    NUM(2, 3, sig);
    sig = floor(sig);
    SORTED(0, l);
    n = l.n;
    if (sig < 1 || !n) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (x < l.v[0] || x > l.v[n - 1]) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    for (i = 0; i < n && l.v[i] < x; i++)
        below++;
    if (i < n && l.v[i] == x) {          /* x is there */
        if (n == 1)
            rank = 1;
        else
            rank = exc ? (below + 1) / (double)(n + 1) : below / (double)(n - 1);
    } else {                             /* between l.v[i - 1] and l.v[i] */
        double a = l.v[i - 1], b = l.v[i], ra, rb, below_a = 0;
        unsigned long k;
        for (k = 0; k < n && l.v[k] < a; k++)
            below_a++;
        ra = exc ? (below_a + 1) / (double)(n + 1) : below_a / (double)(n - 1);
        rb = exc ? (below + 1) / (double)(n + 1) : below / (double)(n - 1);
        rank = ra + (x - a) / (b - a) * (rb - ra);
    }
    p = pow(10, sig);
    cv_num(r, calc_approx_floor(rank * p) / p);
}

CF(percentrank_inc)
{
    percentrank(ctx, args, argc, r, 0);
}

CF(percentrank_exc)
{
    percentrank(ctx, args, argc, r, 1);
}

/* TRIMMEAN(array, percent): the mean without the largest and smallest,
 * n * percent of them in all, rounded down to an even count. */
CF(trimmean)
{
    calc_numlist l;
    double pc, s = 0;
    unsigned long cut, i;
    PASS_ERROR(0);
    NUM(1, 0, pc);
    SORTED(0, l);
    if (pc < 0 || pc >= 1 || !l.n) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cut = (unsigned long)floor((double)l.n * pc / 2);
    for (i = cut; i < l.n - cut; i++)
        s += l.v[i];
    cv_num(r, s / (double)(l.n - 2 * cut));
}

/* place[] gets 0..n-1 in the order of v's values (equal ones in their
 * own order): heapsort, no recursion. */
static int place_before(const double *v, unsigned long a, unsigned long b)
{
    return v[a] < v[b] || (v[a] == v[b] && a < b);
}

static void sift(const double *v, unsigned long *p, unsigned long root, unsigned long end)
{
    unsigned long child, t;
    for (; (child = 2 * root + 1) < end; root = child) {
        if (child + 1 < end && place_before(v, p[child], p[child + 1]))
            child++;
        if (!place_before(v, p[root], p[child]))
            break;
        t = p[root];
        p[root] = p[child];
        p[child] = t;
    }
}

static void sort_places(const double *v, unsigned long n, unsigned long *p)
{
    unsigned long i, t;
    for (i = 0; i < n; i++)
        p[i] = i;
    for (i = n / 2; i-- > 0;)
        sift(v, p, i, n);
    for (i = n; i-- > 1;) {
        t = p[0];
        p[0] = p[i];
        p[i] = t;
        sift(v, p, 0, i);
    }
}

/* FREQUENCY(data, bins): how many numbers fall at or below each bin and
 * above the bin below it (bins taken in order of size, the counts in the
 * bins' own order), and one more count for those above every bin; a
 * column. */
CF(frequency)
{
    calc_numlist data, bins;
    double *sorted;
    unsigned long *place;
    calc_array *out;
    unsigned long i;
    int e;
    (void)argc;
    PASS_ERROR(0);
    PASS_ERROR(1);
    if ((e = calc_collect(ctx, args, 0, 1, &data)) != NOERR || (e = calc_collect(ctx, args, 1, 2, &bins)) != NOERR) {
        cv_err(r, e);
        return;
    }
    out = calc_array_new(ctx, (unsigned)bins.n + 1, 1);
    sorted = calc_arena_alloc(ctx->arena, (bins.n + 1) * sizeof *sorted);
    place = calc_arena_alloc(ctx->arena, (bins.n + 1) * sizeof *place);
    if (!out || !sorted || !place) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i <= bins.n; i++)
        cv_num(&out->v[i], 0);
    sort_places(bins.v, bins.n, place);
    for (i = 0; i < bins.n; i++)
        sorted[i] = bins.v[place[i]];
    for (i = 0; i < data.n; i++) {
        unsigned long lo = 0, hi = bins.n;
        while (lo < hi) {                /* the first bin at or above the number */
            unsigned long mid = lo + (hi - lo) / 2;
            if (sorted[mid] < data.v[i])
                lo = mid + 1;
            else
                hi = mid;
        }
        out->v[lo == bins.n ? bins.n : place[lo]].u.num++;   /* of equal bins, the first */
    }
    give_array(r, out);
}

/* ---- Means and spread ---- */

CF(geomean)
{
    calc_numlist l;
    double s = 0;
    unsigned long i;
    COLLECT(l, 0, argc);
    for (i = 0; i < l.n; i++) {
        if (l.v[i] <= 0) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        s += log(l.v[i]);
    }
    if (!l.n)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, exp(s / (double)l.n));
}

CF(harmean)
{
    calc_numlist l;
    double s = 0;
    unsigned long i;
    COLLECT(l, 0, argc);
    for (i = 0; i < l.n; i++) {
        if (l.v[i] <= 0) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        s += 1 / l.v[i];
    }
    if (!l.n)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, (double)l.n / s);
}

/* AVEDEV (mean absolute deviation) and DEVSQ (sum of squared deviations). */
static void deviation(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int squares)
{
    calc_numlist l;
    double m, s = 0;
    unsigned long i;
    COLLECT(l, 0, argc);
    if (!l.n) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    m = mean_of(&l);
    for (i = 0; i < l.n; i++)
        s += squares ? (l.v[i] - m) * (l.v[i] - m) : fabs(l.v[i] - m);
    cv_num(r, squares ? s : s / (double)l.n);
}

CF(avedev)
{
    deviation(ctx, args, argc, r, 0);
}

CF(devsq)
{
    deviation(ctx, args, argc, r, 1);
}

/* ---- The "A" functions: text and booleans in ranges count ---- */

static int a_cell(void *user, const owf_cell *c, int sheet)
{
    (void)sheet;
    switch (c->type) {
    case OWF_VALUE_NUMBER:
    case OWF_VALUE_BOOL:
        return calc_numlist_add(user, c->number) != NOERR ? OWF_ERROR_NUM + 1 : 0;
    case OWF_VALUE_TEXT:
        return calc_numlist_add(user, 0) != NOERR ? OWF_ERROR_NUM + 1 : 0;
    case OWF_VALUE_ERROR:
        return c->error + 1;
    default:
        return 0;
    }
}

/* AVERAGEA, MAXA, MINA, STDEVA...: in references and arrays, numbers,
 * booleans (1 and 0) and text (0) count and empty cells do not; given
 * directly, as SUM takes them. */
static int collect_a(calc_ctx *ctx, calc_value *args, int argc, calc_numlist *l)
{
    int i, k, e;
    l->ctx = ctx;
    l->v = NULL;
    l->n = l->cap = 0;
    for (i = 0; i < argc; i++) {
        calc_value *v = &args[i];
        if (v->type == CV_REF) {
            for (k = 0; k < v->u.ref.n; k++)
                if ((e = calc_area_each(ctx, &v->u.ref.a[k], a_cell, l)) != 0)
                    return e - 1;
        } else if (v->type == CV_ARRAY) {
            unsigned long n = (unsigned long)v->u.arr->rows * v->u.arr->cols, j;
            for (j = 0; j < n; j++) {
                const calc_value *el = &v->u.arr->v[j];
                if (el->type == CV_ERR)
                    return el->u.err;
                if (el->type != CV_EMPTY && calc_numlist_add(l, el->type == CV_STR ? 0 : el->u.num) != NOERR)
                    return OWF_ERROR_NUM;
            }
        } else if ((e = calc_each_number(ctx, args, i, i + 1, calc_numlist_add, l)) != NOERR) {
            return e;
        }
    }
    return NOERR;
}

#define COLLECT_A(l)                                                                                         \
    do {                                                                                                     \
        int e_ = collect_a(ctx, args, argc, &(l));                                                           \
        if (e_ != NOERR) {                                                                                   \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

CF(averagea)
{
    calc_numlist l;
    COLLECT_A(l);
    if (!l.n)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, mean_of(&l));
}

static void max_min_a(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int max)
{
    calc_numlist l;
    double m = 0;
    unsigned long i;
    COLLECT_A(l);
    for (i = 0; i < l.n; i++)
        if (!i || (max ? l.v[i] > m : l.v[i] < m))
            m = l.v[i];
    cv_num(r, m);
}

CF(maxa)
{
    max_min_a(ctx, args, argc, r, 1);
}

CF(mina)
{
    max_min_a(ctx, args, argc, r, 0);
}

static void variance_a(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int sample, int root)
{
    calc_numlist l;
    double m, ss = 0;
    unsigned long i;
    COLLECT_A(l);
    if (l.n < (sample ? 2u : 1u)) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    m = mean_of(&l);
    for (i = 0; i < l.n; i++)
        ss += (l.v[i] - m) * (l.v[i] - m);
    ss /= (double)(sample ? l.n - 1 : l.n);
    cv_num(r, root ? sqrt(ss) : ss);
}

CF(stdeva)
{
    variance_a(ctx, args, argc, r, 1, 1);
}

CF(stdevpa)
{
    variance_a(ctx, args, argc, r, 0, 1);
}

CF(vara)
{
    variance_a(ctx, args, argc, r, 1, 0);
}

CF(varpa)
{
    variance_a(ctx, args, argc, r, 0, 0);
}

/* ---- Two arrays: correlation and regression ---- */

typedef struct {
    double *x, *y;
    unsigned long n;
    double mx, my, sxx, syy, sxy;   /* means, and sums of products of deviations */
} pairs;

/* Arguments i (y) and j (x): their elements in pairs where both are
 * numbers. A different count of elements is #N/A; an error in either
 * stops it. */
static int get_pairs(calc_ctx *ctx, calc_value *args, int iy, int ix, pairs *p)
{
    calc_array *a, *b;
    unsigned long n, k;
    int e;
    if (args[iy].type == CV_ERR)
        return args[iy].u.err;
    if (args[ix].type == CV_ERR)
        return args[ix].u.err;
    if ((e = calc_to_array(ctx, &args[iy], &a)) != 0 || (e = calc_to_array(ctx, &args[ix], &b)) != 0)
        return e;
    n = (unsigned long)a->rows * a->cols;
    if (n != (unsigned long)b->rows * b->cols)
        return OWF_ERROR_NA;
    p->x = calc_arena_alloc(ctx->arena, (n ? n : 1) * sizeof(double));
    p->y = calc_arena_alloc(ctx->arena, (n ? n : 1) * sizeof(double));
    if (!p->x || !p->y)
        return OWF_ERROR_NUM;
    p->n = 0;
    p->mx = p->my = p->sxx = p->syy = p->sxy = 0;
    for (k = 0; k < n; k++) {
        const calc_value *y = &a->v[k], *x = &b->v[k];
        if (y->type == CV_ERR)
            return y->u.err;
        if (x->type == CV_ERR)
            return x->u.err;
        if (x->type == CV_NUM && y->type == CV_NUM) {
            p->x[p->n] = x->u.num;
            p->y[p->n++] = y->u.num;
            p->mx += x->u.num;
            p->my += y->u.num;
        }
    }
    if (p->n) {
        p->mx /= (double)p->n;
        p->my /= (double)p->n;
    }
    for (k = 0; k < p->n; k++) {
        double dx = p->x[k] - p->mx, dy = p->y[k] - p->my;
        p->sxx += dx * dx;
        p->syy += dy * dy;
        p->sxy += dx * dy;
    }
    return NOERR;
}

#define PAIRS(iy, ix, p)                                                                                     \
    do {                                                                                                     \
        int e_ = get_pairs(ctx, args, (iy), (ix), &(p));                                                     \
        if (e_ != NOERR) {                                                                                   \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

/* CORREL, PEARSON(array1, array2) */
CF(correl)
{
    pairs p;
    (void)argc;
    PAIRS(0, 1, p);
    if (!p.n || p.sxx == 0 || p.syy == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.sxy / sqrt(p.sxx * p.syy));
}

CF(rsq)
{
    pairs p;
    (void)argc;
    PAIRS(0, 1, p);
    if (!p.n || p.sxx == 0 || p.syy == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.sxy * p.sxy / (p.sxx * p.syy));
}

/* COVAR, COVARIANCE.P and COVARIANCE.S */
CF(covariance_p)
{
    pairs p;
    (void)argc;
    PAIRS(0, 1, p);
    if (!p.n)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.sxy / (double)p.n);
}

CF(covariance_s)
{
    pairs p;
    (void)argc;
    PAIRS(0, 1, p);
    if (p.n < 2)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.sxy / (double)(p.n - 1));
}

/* SLOPE, INTERCEPT(known_y, known_x): the least-squares line. */
CF(slope)
{
    pairs p;
    (void)argc;
    PAIRS(0, 1, p);
    if (!p.n || p.sxx == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.sxy / p.sxx);
}

CF(intercept)
{
    pairs p;
    (void)argc;
    PAIRS(0, 1, p);
    if (!p.n || p.sxx == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.my - p.sxy / p.sxx * p.mx);
}

/* FORECAST, FORECAST.LINEAR(x, known_y, known_x) */
CF(forecast)
{
    pairs p;
    double x;
    NUM(0, 0, x);
    PAIRS(1, 2, p);
    if (!p.n || p.sxx == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, p.my + p.sxy / p.sxx * (x - p.mx));
}

/* ---- Distributions ---- */

CF(standardize)
{
    double x, m, sd;
    NUM(0, 0, x);
    NUM(1, 0, m);
    NUM(2, 0, sd);
    if (sd <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, (x - m) / sd);
}

/* NORM.DIST, NORMDIST(x, mean, sd, cumulative) */
CF(norm_dist)
{
    double x, m, sd;
    int cum;
    NUM(0, 0, x);
    NUM(1, 0, m);
    NUM(2, 0, sd);
    BOOL(3, 0, cum);
    if (sd <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, cum ? calc_norm_cdf((x - m) / sd) : norm_pdf((x - m) / sd) / sd);
}

/* NORM.INV, NORMINV(p, mean, sd) */
CF(norm_inv)
{
    double p, m, sd;
    NUM(0, 0, p);
    NUM(1, 0, m);
    NUM(2, 0, sd);
    if (p <= 0 || p >= 1 || sd <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, m + sd * calc_norm_inv(p));
}

/* NORM.S.DIST(z, cumulative); NORMSDIST(z) is the cumulative one. */
CF(norm_s_dist)
{
    double z;
    int cum;
    NUM(0, 0, z);
    BOOL(1, 0, cum);
    cv_num(r, cum ? calc_norm_cdf(z) : norm_pdf(z));
}

CF(normsdist)
{
    double z;
    NUM(0, 0, z);
    cv_num(r, calc_norm_cdf(z));
}

/* NORM.S.INV, NORMSINV(p) */
CF(norm_s_inv)
{
    double p;
    NUM(0, 0, p);
    if (p <= 0 || p >= 1)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, calc_norm_inv(p));
}

/* C(n, k) p^k (1 - p)^(n - k): exactly as far as the binomial fits a
 * double, through log gamma past it. */
static double binom_pmf(double k, double n, double p)
{
    double c = 1, m = k < n - k ? k : n - k, i;
    if (p == 0)
        return k == 0;
    if (p == 1)
        return k == n;
    if (n <= 1000) {
        for (i = 1; i <= m; i++) {
            c = c * (n - m + i) / i;
            if (c < 4503599627370496.0)
                c = floor(c + 0.5);
        }
        return c * pow(p, k) * pow(1 - p, n - k);
    }
    return exp(lgam(n + 1) - lgam(k + 1) - lgam(n - k + 1) + k * log(p) + (n - k) * calc_log1p(-p));
}

/* BINOM.DIST, BINOMDIST(k, n, p, cumulative): k and n truncated. */
CF(binom_dist)
{
    double k, n, p, s = 0, j;
    int cum;
    NUM(0, 0, k);
    NUM(1, 0, n);
    NUM(2, 0, p);
    BOOL(3, 0, cum);
    k = k < 0 ? -floor(-k) : floor(k);
    n = n < 0 ? -floor(-n) : floor(n);
    if (k < 0 || n < 0 || k > n || p < 0 || p > 1) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (!cum) {
        cv_num(r, binom_pmf(k, n, p));
        return;
    }
    if (k == n) {
        cv_num(r, 1);
        return;
    }
    if (n > 1000) {                      /* P(X <= k) = I_(1-p)(n - k, k + 1) */
        cv_num(r, ibeta(1 - p, p, n - k, k + 1));
        return;
    }
    for (j = 0; j <= k; j++)
        s += binom_pmf(j, n, p);
    cv_num(r, s > 1 ? 1 : s);
}

/* The Poisson term P(X = k): a running product while exp(-mean) and the
 * product hold, through log gamma past them. */
static double poisson_pmf(double k, double mean)
{
    double t, j;
    if (mean < 700 && k < 1000) {
        t = exp(-mean);
        for (j = 1; j <= k; j++)
            t *= mean / j;
        return t;
    }
    return exp(k * log(mean) - mean - lgam(k + 1));
}

/* POISSON.DIST, POISSON(x, mean, cumulative): x truncated. The sum for
 * the cumulative one starts at its largest term and goes out until the
 * terms no longer count; past a mean of 10^8 (thousands of terms count) it
 * is the normal approximation. */
CF(poisson_dist)
{
    double x, mean, m, t, s, j;
    int cum;
    NUM(0, 0, x);
    NUM(1, 0, mean);
    BOOL(2, 0, cum);
    x = x < 0 ? -floor(-x) : floor(x);
    if (x < 0 || mean < 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (mean == 0) {
        cv_num(r, cum || x == 0 ? 1 : 0);
        return;
    }
    if (!cum) {
        cv_num(r, poisson_pmf(x, mean));
        return;
    }
    if (mean > 1e8) {                    /* the terms that count number some sqrt(mean): too many to add */
        cv_num(r, calc_norm_cdf((x + 0.5 - mean) / sqrt(mean)));
        return;
    }
    m = floor(mean) < x ? floor(mean) : x;
    t = poisson_pmf(m, mean);
    s = t;
    for (j = m; j > 0; j--) {            /* down from m */
        t *= j / mean;
        s += t;
        if (t < s * 1e-17)
            break;
    }
    t = poisson_pmf(m, mean);
    for (j = m + 1; j <= x; j++) {       /* up to x */
        t *= mean / j;
        s += t;
        if (t < s * 1e-17)
            break;
    }
    cv_num(r, s > 1 ? 1 : s);
}

/* EXPON.DIST, EXPONDIST(x, lambda, cumulative) */
CF(expon_dist)
{
    double x, l;
    int cum;
    NUM(0, 0, x);
    NUM(1, 0, l);
    BOOL(2, 0, cum);
    if (x < 0 || l <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, cum ? -calc_expm1(-l * x) : l * exp(-l * x));
}

/* Student's t: the degrees of freedom are truncated, as Excel's; below 1
 * is #NUM!. */
static int t_df(calc_ctx *ctx, calc_value *args, int argc, int i, double *df)
{
    int e = calc_num_arg(ctx, args, argc, i, 0, df);
    if (e)
        return e;
    *df = floor(*df);
    return *df < 1 || *df >= 1e10 ? OWF_ERROR_NUM : 0;
}

#define DF(i, var)                                                                                           \
    do {                                                                                                     \
        int e_ = t_df(ctx, args, argc, (i), &(var));                                                         \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

/* T.DIST(x, df, cumulative): the left tail, or the density. */
CF(t_dist)
{
    double x, df;
    int cum;
    NUM(0, 0, x);
    DF(1, df);
    BOOL(2, 0, cum);
    cv_num(r, cum ? t_cdf(x, df) : t_pdf(x, df));
}

/* T.DIST.2T(x, df): both tails; x below 0 is #NUM!. */
CF(t_dist_2t)
{
    double x, df;
    NUM(0, 0, x);
    DF(1, df);
    if (x < 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, 2 * t_tail(x, df));
}

/* T.DIST.RT(x, df): the right tail. */
CF(t_dist_rt)
{
    double x, df;
    NUM(0, 0, x);
    DF(1, df);
    cv_num(r, x >= 0 ? t_tail(x, df) : 1 - t_tail(-x, df));
}

/* TDIST(x, df, tails): one or both right tails; tails is truncated. */
CF(tdist)
{
    double x, df, tails;
    NUM(0, 0, x);
    DF(1, df);
    NUM(2, 0, tails);
    tails = floor(tails);
    if (x < 0 || (tails != 1 && tails != 2))
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, tails * t_tail(x, df));
}

/* T.INV(p, df): the left tail's inverse. */
CF(t_inv)
{
    double p, df, q;
    NUM(0, 0, p);
    DF(1, df);
    if (p <= 0 || p >= 1) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (p == 0.5) {
        cv_num(r, 0);
        return;
    }
    q = p < 0.5 ? p : 1 - p;
    q = t_tail_inv(q, df);
    cv_num(r, p < 0.5 ? -q : q);
}

/* T.INV.2T, TINV(p, df): the t >= 0 whose two tails make p. */
CF(t_inv_2t)
{
    double p, df;
    NUM(0, 0, p);
    DF(1, df);
    if (p <= 0 || p > 1)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, p == 1 ? 0 : t_tail_inv(p / 2, df));
}
