/*
 * libowf: the formula engine's date and time functions beyond the first
 * ones (owf_calc_fn.c): EDATE, EOMONTH, NETWORKDAYS(.INTL),
 * WORKDAY(.INTL), WEEKNUM, ISOWEEKNUM, YEARFRAC, DATEDIF, DAYS, DAYS360,
 * TIMEVALUE, DATEVALUE. Serial dates as the rest of the engine has them
 * (Excel's 29 February 1900 included). Excel's names, argument counts and
 * errors (owf_calc_fn.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_fn.h"

#define LAST_SERIAL 2958465.0            /* 31 December 9999 */

typedef struct {
    int y, m, d;
} ymd;

/* Argument i as a whole serial date (truncated), 0 to 31 December 9999. */
static int date_at(calc_ctx *ctx, calc_value *args, int argc, int i, double *out)
{
    double x;
    int e = calc_num_arg(ctx, args, argc, i, 0, &x);
    if (e)
        return e;
    if (x < 0 || x >= LAST_SERIAL + 1)
        return OWF_ERROR_NUM;
    *out = floor(x);
    return 0;
}

#define DATE_AT(i, var)                                                                                      \
    do {                                                                                                     \
        int e_ = date_at(ctx, args, argc, (i), &(var));                                                      \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

static ymd parts(calc_ctx *ctx, double serial)
{
    ymd t;
    calc_serial_date((long)serial, ctx->book->date1904, &t.y, &t.m, &t.d);
    return t;
}

/* The serial of a date (month and day may run over, as DATE's); -1 when
 * it is outside Excel's dates. */
static double serial_of(calc_ctx *ctx, long y, long m, long d)
{
    double s;
    if (!calc_date_serial(y, m, d, ctx->book->date1904, &s) || s < 0 || s > LAST_SERIAL)
        return -1;
    return s;
}

/* 0 Sunday ... 6 Saturday, as WEEKDAY counts. */
static int weekday0(calc_ctx *ctx, double serial)
{
    return (int)fmod(serial + (ctx->book->date1904 ? 1462 : 0) + 6, 7);
}

static int days_in_month(calc_ctx *ctx, int y, int m)
{
    double a, b;
    if (!calc_date_serial(y, m, 1, ctx->book->date1904, &a) || !calc_date_serial(y, m + 1, 1, ctx->book->date1904, &b))
        return 31;
    return (int)(b - a);
}

static double trunc_(double x)
{
    return x < 0 ? -floor(-x) : floor(x);
}

/* ---- Months ---- */

/* EDATE(start, months): the same day so many months on (the month's last
 * day when it is shorter); EOMONTH: the last day of that month. */
static void months_on(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int end)
{
    double start, months, s, total, y;
    ymd t;
    int m, dim;
    DATE_AT(0, start);
    NUM(1, 0, months);
    months = trunc_(months);
    if (fabs(months) > 120000) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    t = parts(ctx, start);
    total = t.y * 12.0 + (t.m - 1) + months;   /* months since year 0 */
    y = floor(total / 12);
    m = (int)(total - y * 12) + 1;
    dim = days_in_month(ctx, (int)y, m);
    s = serial_of(ctx, (long)y, m, end ? dim : (t.d < dim ? t.d : dim));
    if (s < 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, s);
}

CF(edate)
{
    months_on(ctx, args, argc, r, 0);
}

CF(eomonth)
{
    months_on(ctx, args, argc, r, 1);
}

/* ---- Working days ---- */

/* The weekend as bits by WEEKDAY's 0 (Sunday) to 6, from a number (1 to
 * 7, 11 to 17) or a string of seven 0s and 1s from Monday. Left out: 1
 * (Saturday and Sunday). */
static int weekend_of(calc_ctx *ctx, calc_value *args, int argc, int i, int *mask)
{
    calc_value v;
    double x;
    int e;
    if (!calc_given(argc, args, i)) {
        *mask = 1 << 0 | 1 << 6;
        return 0;
    }
    v = args[i];
    if (v.type == CV_ERR)
        return v.u.err;
    if (v.type == CV_STR) {
        unsigned k;
        if (v.u.str.len != 7)
            return OWF_ERROR_VALUE;
        *mask = 0;
        for (k = 0; k < 7; k++) {
            if (v.u.str.p[k] != '0' && v.u.str.p[k] != '1')
                return OWF_ERROR_VALUE;
            if (v.u.str.p[k] == '1')
                *mask |= 1 << ((k + 1) % 7);   /* Monday is 1 */
        }
        return 0;
    }
    if ((e = calc_to_number(ctx, &v, &x)) != 0)
        return e;
    x = trunc_(x);
    if (x >= 1 && x <= 7)                /* two days: Saturday and Sunday, Sunday and Monday... */
        *mask = 1 << ((int)x + 5) % 7 | 1 << ((int)x + 6) % 7;
    else if (x >= 11 && x <= 17)         /* one day: Sunday, Monday... */
        *mask = 1 << ((int)x - 11);
    else
        return OWF_ERROR_NUM;
    return 0;
}

/* The holidays: whole serial dates, sorted, without repeats. */
typedef struct {
    double *d;
    unsigned long n;
} holidays;

static int holidays_of(calc_ctx *ctx, calc_value *args, int argc, int i, holidays *h)
{
    calc_numlist l;
    unsigned long k, n = 0;
    int e;
    h->n = 0;
    h->d = NULL;
    if (!calc_given(argc, args, i))
        return 0;
    if ((e = calc_collect(ctx, args, i, i + 1, &l)) != NOERR)
        return e;
    for (k = 0; k < l.n; k++) {
        if (l.v[k] < 0 || l.v[k] >= LAST_SERIAL + 1)
            return OWF_ERROR_NUM;
        l.v[k] = floor(l.v[k]);
    }
    calc_sort_numbers(l.v, l.n);
    for (k = 0; k < l.n; k++)
        if (!n || l.v[k] != l.v[n - 1])
            l.v[n++] = l.v[k];
    h->d = l.v;
    h->n = n;
    return 0;
}

/* How many holidays in [a, b] fall on working days. */
static double holidays_in(calc_ctx *ctx, const holidays *h, double a, double b, int mask)
{
    unsigned long lo = 0, hi = h->n;
    double n = 0;
    while (lo < hi) {
        unsigned long mid = lo + (hi - lo) / 2;
        if (h->d[mid] < a)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < h->n && h->d[lo] <= b; lo++)
        n += !(mask >> weekday0(ctx, h->d[lo]) & 1);
    return n;
}

static int workdays_a_week(int mask)
{
    int k, n = 7;
    for (k = 0; k < 7; k++)
        n -= mask >> k & 1;
    return n;
}

/* Working days from a to b, both counted (a <= b). */
static double count_workdays(calc_ctx *ctx, double a, double b, int mask, const holidays *h)
{
    double days = b - a + 1, weeks = floor(days / 7), n = weeks * workdays_a_week(mask), d;
    for (d = a + weeks * 7; d <= b; d++)
        n += !(mask >> weekday0(ctx, d) & 1);
    return n - holidays_in(ctx, h, a, b, mask);
}

static void networkdays(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int intl)
{
    double a, b;
    int mask, e;
    holidays h;
    DATE_AT(0, a);
    DATE_AT(1, b);
    if ((e = weekend_of(ctx, args, intl ? argc : 0, 2, &mask)) != 0 ||
        (e = holidays_of(ctx, args, argc, intl ? 3 : 2, &h)) != 0) {
        cv_err(r, e);
        return;
    }
    if (a <= b)
        cv_num(r, count_workdays(ctx, a, b, mask, &h));
    else
        cv_num(r, -count_workdays(ctx, b, a, mask, &h));
}

/* NETWORKDAYS(start, end, [holidays]); NETWORKDAYS.INTL(start, end,
 * [weekend], [holidays]): working days from start to end, both counted
 * (negative when end is first). */
CF(networkdays)
{
    networkdays(ctx, args, argc, r, 0);
}

CF(networkdays_intl)
{
    networkdays(ctx, args, argc, r, 1);
}

static int is_holiday(const holidays *h, double d)
{
    unsigned long lo = 0, hi = h->n;
    while (lo < hi) {
        unsigned long mid = lo + (hi - lo) / 2;
        if (h->d[mid] < d)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < h->n && h->d[lo] == d;
}

/* WORKDAY(start, days, [holidays]); WORKDAY.INTL(start, days, [weekend],
 * [holidays]): the working day so many working days on (or back). Whole
 * weeks are jumped at once, the holidays in them counted back in. */
static void workday(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int intl)
{
    double d, days, left, step;
    int mask, e, per_week;
    holidays h;
    DATE_AT(0, d);
    NUM(1, 0, days);
    days = trunc_(days);
    if ((e = weekend_of(ctx, args, intl ? argc : 0, 2, &mask)) != 0 ||
        (e = holidays_of(ctx, args, argc, intl ? 3 : 2, &h)) != 0) {
        cv_err(r, e);
        return;
    }
    if ((per_week = workdays_a_week(mask)) == 0) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (fabs(days) > LAST_SERIAL) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    step = days < 0 ? -1 : 1;
    left = fabs(days);
    while (left > 0) {
        if (left > 2.0 * per_week) {
            double weeks = floor(left / per_week) - 1, to = d + step * 7 * weeks;
            if (to < 0 || to > LAST_SERIAL) {
                cv_err(r, OWF_ERROR_NUM);
                return;
            }
            left -= weeks * per_week;
            left += step > 0 ? holidays_in(ctx, &h, d + 1, to, mask) : holidays_in(ctx, &h, to, d - 1, mask);
            d = to;
            continue;
        }
        d += step;
        if (d < 0 || d > LAST_SERIAL) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        if (!(mask >> weekday0(ctx, d) & 1) && !is_holiday(&h, d))
            left--;
    }
    cv_num(r, d);
}

CF(workday)
{
    workday(ctx, args, argc, r, 0);
}

CF(workday_intl)
{
    workday(ctx, args, argc, r, 1);
}

/* ---- Weeks ---- */

/* WEEKNUM(date, [type = 1]): the week of the year, week 1 holding 1
 * January, weeks starting on Sunday (1, 17), Monday (2, 11), Tuesday
 * (12) ... Saturday (16); 21 is ISO 8601's week. */
static double iso_week(calc_ctx *ctx, double serial)
{
    int monday0 = (weekday0(ctx, serial) + 6) % 7;
    double thursday = serial - monday0 + 3, jan1;
    ymd t = parts(ctx, thursday < 0 ? 0 : thursday);
    jan1 = serial_of(ctx, t.y, 1, 1);
    return floor((thursday - jan1) / 7) + 1;
}

CF(weeknum)
{
    double d, type, jan1;
    int first, off;
    DATE_AT(0, d);
    NUM(1, 1, type);
    type = trunc_(type);
    switch (fabs(type) < 100 ? (int)type : 0) {
    case 1: case 17: first = 0; break;
    case 2: case 11: first = 1; break;
    case 12: first = 2; break;
    case 13: first = 3; break;
    case 14: first = 4; break;
    case 15: first = 5; break;
    case 16: first = 6; break;
    case 21:
        cv_num(r, iso_week(ctx, d));
        return;
    default:
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    jan1 = serial_of(ctx, parts(ctx, d).y, 1, 1);
    if (jan1 < 0)
        jan1 = 0;
    off = (weekday0(ctx, jan1) - first + 7) % 7;
    cv_num(r, floor((d - jan1 + off) / 7) + 1);
}

CF(isoweeknum)
{
    double d;
    DATE_AT(0, d);
    cv_num(r, iso_week(ctx, d));
}

/* ---- Differences ---- */

CF(days)
{
    double a, b;
    DATE_AT(0, a);
    DATE_AT(1, b);
    cv_num(r, a - b);
}

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int last_of_february(calc_ctx *ctx, ymd t)
{
    return t.m == 2 && t.d == days_in_month(ctx, t.y, 2);
}

/* DAYS360(start, end, [european]): the days between two dates in a year
 * of twelve 30-day months. The US (NASD) method, as Excel's documentation
 * gives it: a start on its month's last day is the 30th; an end on the
 * 31st is the 30th when the start is the 30th or 31st (else the 1st of
 * the next month, which the arithmetic gives as it is). European: both
 * 31sts are 30ths. */
CF(days360)
{
    double a, b;
    int eu;
    ymd s, t;
    DATE_AT(0, a);
    DATE_AT(1, b);
    BOOL(2, 0, eu);
    s = parts(ctx, a);
    t = parts(ctx, b);
    if (eu) {
        if (s.d == 31)
            s.d = 30;
        if (t.d == 31)
            t.d = 30;
    } else {
        if (s.d == days_in_month(ctx, s.y, s.m))
            s.d = 30;
        if (t.d == 31 && s.d >= 30)
            t.d = 30;
    }
    cv_num(r, (t.y - s.y) * 360.0 + (t.m - s.m) * 30.0 + (t.d - s.d));
}

/* YEARFRAC(start, end, [basis = 0]): the fraction of a year between two
 * dates. 0 US 30/360, 1 actual/actual, 2 actual/360, 3 actual/365, 4
 * European 30/360. Basis 0 and 1 follow Excel's behaviour as OpenFormula
 * describes it (4.11.7.7): for 1, the year's length is 365 or 366 for
 * spans of up to a year (366 when a 29 February is in it), else the
 * average of the years' lengths. */
CF(yearfrac)
{
    double a, b, basis, days;
    ymd s, t;
    DATE_AT(0, a);
    DATE_AT(1, b);
    NUM(2, 0, basis);
    basis = trunc_(basis);
    if (basis < 0 || basis > 4) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (a > b) {
        double k = a;
        a = b;
        b = k;
    }
    s = parts(ctx, a);
    t = parts(ctx, b);
    days = b - a;
    switch ((int)basis) {
    case 0: {
        int d1 = s.d, d2 = t.d;
        if (d1 == 31 && d2 == 31) {
            d1 = 30;
            d2 = 30;
        } else if (d1 == 31) {
            d1 = 30;
        } else if (d1 == 30 && d2 == 31) {
            d2 = 30;
        } else if (s.m == 2 && t.m == 2 && last_of_february(ctx, s) && last_of_february(ctx, t)) {
            d1 = 30;
            d2 = 30;
        } else if (last_of_february(ctx, s)) {
            d1 = 30;
        }
        cv_num(r, ((t.y - s.y) * 360.0 + (t.m - s.m) * 30.0 + (d2 - d1)) / 360);
        return;
    }
    case 1: {
        double len;
        if (s.y == t.y || (t.y == s.y + 1 && (s.m > t.m || (s.m == t.m && s.d >= t.d)))) {
            /* up to a year: 366 if a 29 February is in it */
            if (s.y == t.y)
                len = is_leap(s.y) ? 366 : 365;
            else if ((is_leap(s.y) && s.m <= 2) || (is_leap(t.y) && (t.m > 2 || (t.m == 2 && t.d == 29))))
                len = 366;
            else
                len = 365;
        } else {
            double y0 = serial_of(ctx, s.y, 1, 1), y1 = serial_of(ctx, t.y + 1, 1, 1);
            if (y1 < 0)
                y1 = LAST_SERIAL + 1;
            len = (y1 - y0) / (t.y - s.y + 1);
        }
        cv_num(r, days / len);
        return;
    }
    case 2: cv_num(r, days / 360); return;
    case 3: cv_num(r, days / 365); return;
    default: {
        int d1 = s.d == 31 ? 30 : s.d, d2 = t.d == 31 ? 30 : t.d;
        cv_num(r, ((t.y - s.y) * 360.0 + (t.m - s.m) * 30.0 + (d2 - d1)) / 360);
        return;
    }
    }
}

/* DATEDIF(start, end, unit): whole years "Y", months "M" or days "D"
 * between two dates; "MD" the days without the months and years, "YM"
 * the months without the years, "YD" the days without the years. As
 * Excel's documentation warns, "MD" can be negative when the start's day
 * is past the end of the month before the end's month. */
CF(datedif)
{
    double a, b, months;
    const char *u;
    unsigned n;
    ymd s, t;
    char unit[3];
    DATE_AT(0, a);
    DATE_AT(1, b);
    TEXT(2, u, n);
    if (a > b || n < 1 || n > 2) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    unit[0] = (char)owf_upper((unsigned char)u[0]);
    unit[1] = n > 1 ? (char)owf_upper((unsigned char)u[1]) : 0;
    unit[2] = 0;
    s = parts(ctx, a);
    t = parts(ctx, b);
    months = (t.y - s.y) * 12.0 + (t.m - s.m) - (t.d < s.d);
    if (!strcmp(unit, "Y"))
        cv_num(r, floor(months / 12));
    else if (!strcmp(unit, "M"))
        cv_num(r, months);
    else if (!strcmp(unit, "D"))
        cv_num(r, b - a);
    else if (!strcmp(unit, "YM"))
        cv_num(r, fmod(months, 12));
    else if (!strcmp(unit, "MD")) {
        double from;
        if (t.d >= s.d) {
            cv_num(r, t.d - s.d);
            return;
        }
        calc_date_serial(t.y, t.m - 1, s.d, ctx->book->date1904, &from);
        cv_num(r, b - from);
    } else if (!strcmp(unit, "YD")) {
        double from = serial_of(ctx, (t.m > s.m || (t.m == s.m && t.d >= s.d)) ? t.y : t.y - 1, s.m, s.d);
        cv_num(r, from < 0 ? b - a : b - from);
    } else {
        cv_err(r, OWF_ERROR_NUM);
    }
}

/* ---- Text to dates and times ---- */

/* TIMEVALUE(text), DATEVALUE(text): the time (a fraction of a day) or the
 * date (a whole serial) that text holds; the other part is dropped. Text
 * that is not a date or time, and a value that is not text, are #VALUE!. */
static void text_value(calc_ctx *ctx, calc_value *args, calc_value *r, int time)
{
    double x;
    int kind, dec;
    if (args[0].type == CV_ERR) {
        *r = args[0];
        return;
    }
    if (args[0].type != CV_STR ||
        !calc_parse_number(args[0].u.str.p, args[0].u.str.len, ctx->dmy, ctx->book->date1904, &x, &kind, &dec, NULL, 0) ||
        (kind != NK_DATE && kind != NK_TIME && kind != NK_DATETIME) || (!time && kind == NK_TIME)) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (time)                            /* to the millisecond: a date's digits leave the time a few short */
        cv_num(r, floor((x - floor(x)) * 86400000.0 + 0.5) / 86400000.0);
    else if (x < 0 || x >= LAST_SERIAL + 1)
        cv_err(r, OWF_ERROR_VALUE);
    else
        cv_num(r, floor(x));
}

CF(timevalue)
{
    (void)argc;
    text_value(ctx, args, r, 1);
}

CF(datevalue)
{
    (void)argc;
    text_value(ctx, args, r, 0);
}
