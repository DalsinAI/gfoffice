/*
 * libowf: numbers, dates and times as workbook files write them (XLSX,
 * ODS, CSV). Reading a decimal number gives the nearest double, and
 * writing one gives the fewest digits (up to 17) that read back as the same
 * double, so a value survives any number of saves. Neither uses the C
 * library's strtod, printf's %g or <math.h>: owf.library has none of them.
 * The exact work is whole-number arithmetic on 16-bit pieces, as
 * owf_number_text does it; the system's doubles only take values apart and
 * put them together by powers of two, which never round.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <string.h>

/* ---- Big whole numbers ---- */

#define PIECES 100                       /* 1600 bits: 10^360 x 2^130 fits */

typedef struct {
    int n;                               /* pieces in use, lowest first */
    unsigned short d[PIECES];
} big;

static void big_set(big *b, unsigned long long v)
{
    b->n = 0;
    while (v) {
        b->d[b->n++] = (unsigned short)(v & 0xFFFF);
        v >>= 16;
    }
}

static void big_mul(big *b, unsigned long m)  /* m below 65536 */
{
    unsigned long carry = 0;
    int i;
    for (i = 0; i < b->n; i++) {
        carry += b->d[i] * m;
        b->d[i] = (unsigned short)(carry & 0xFFFF);
        carry >>= 16;
    }
    if (carry && b->n < PIECES)
        b->d[b->n++] = (unsigned short)carry;
}

static void big_mul_pow(big *b, unsigned base, int k)  /* b * base^k, base 2 or 10 */
{
    unsigned long step = base == 2 ? 32768 : 10000;
    int per = base == 2 ? 15 : 4;
    for (; k >= per; k -= per)
        big_mul(b, step);
    for (; k > 0; k--)
        big_mul(b, base);
}

static int big_cmp(const big *a, const big *b)
{
    int i;
    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i])
            return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

static void big_sub(big *a, const big *b)    /* a >= b */
{
    long borrow = 0;
    int i;
    for (i = 0; i < a->n; i++) {
        long t = (long)a->d[i] - (i < b->n ? (long)b->d[i] : 0) - borrow;
        borrow = t < 0;
        a->d[i] = (unsigned short)(t + (borrow << 16));
    }
    while (a->n && !a->d[a->n - 1])
        a->n--;
}

static void big_half(big *b)
{
    int i;
    for (i = 0; i < b->n; i++) {
        b->d[i] = (unsigned short)(b->d[i] >> 1);
        if (i + 1 < b->n && (b->d[i + 1] & 1))
            b->d[i] = (unsigned short)(b->d[i] | 0x8000);
    }
    while (b->n && !b->d[b->n - 1])
        b->n--;
}

static int big_bits(const big *b)
{
    int bits;
    unsigned top;
    if (!b->n)
        return 0;
    bits = (b->n - 1) * 16;
    for (top = b->d[b->n - 1]; top; top >>= 1)
        bits++;
    return bits;
}

/* ---- Powers of two, exactly ---- */

static const double two_up[10] = { 2.0, 4.0, 16.0, 256.0, 65536.0, 4294967296.0, 1.8446744073709552e19,
                                   3.4028236692093846e38, 1.157920892373162e77, 1.3407807929942597e154 };

/* v * 2^k for |k| below 2048, by powers of two; the caller makes sure the
 * result is a double (normal or not) that v's digits fit. */
static double scale2(double v, int k)
{
    int i;
    for (i = 9; i >= 0; i--) {
        int step = 1 << i;
        while (k >= step) {
            v *= two_up[i];
            k -= step;
        }
        while (k <= -step) {
            v /= two_up[i];
            k += step;
        }
    }
    return v;
}

/* ---- Reading ---- */

static const double tens[23] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13,
                                 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };

#define KEEP_DIGITS 40

/* Correctly rounded for up to 40 significant digits, which is more than any
 * program writes; longer numbers are read as their first 40 digits and a
 * little more. */
int owf_number_parse(const char *s, size_t length, double *value)
{
    const char *p = s, *end = s + length;
    unsigned long long m = 0, q, rem, half, r;
    char keep[KEEP_DIGITS];
    int negative = 0, digits = 0, kept = 0, sticky = 0, dexp = 0, s2, top, e2, prec, drop, i;
    long exponent = 0;
    big num, den, t;
    double v;

    if (p < end && (*p == '-' || *p == '+'))
        negative = *p++ == '-';
    for (; p < end && *p >= '0' && *p <= '9'; p++) {
        digits++;
        if (!kept && *p == '0')
            continue;
        if (kept < KEEP_DIGITS) {
            keep[kept++] = (char)(*p - '0');
        } else {
            dexp++;
            sticky |= *p != '0';
        }
    }
    if (p < end && *p == '.') {
        for (p++; p < end && *p >= '0' && *p <= '9'; p++) {
            digits++;
            if (!kept && *p == '0') {
                dexp--;
                continue;
            }
            if (kept < KEEP_DIGITS) {
                keep[kept++] = (char)(*p - '0');
                dexp--;
            } else {
                sticky |= *p != '0';
            }
        }
    }
    if (!digits)
        return 0;
    if (p < end && (*p == 'e' || *p == 'E')) {
        int eneg = 0, edigits = 0;
        p++;
        if (p < end && (*p == '-' || *p == '+'))
            eneg = *p++ == '-';
        for (; p < end && *p >= '0' && *p <= '9'; p++) {
            edigits++;
            if (exponent < 100000)
                exponent = exponent * 10 + (*p - '0');
        }
        if (!edigits)
            return 0;
        if (eneg)
            exponent = -exponent;
    }
    if (p != end)
        return 0;
    while (kept && !keep[kept - 1]) {   /* trailing zeros */
        kept--;
        dexp++;
    }
    if (!kept) {
        *value = negative ? -0.0 : 0.0;
        return 1;
    }
    for (i = 0; i < kept && i < 19; i++)
        m = m * 10 + (unsigned)keep[i];
    exponent += dexp;
    /* the value is m x 10^exponent, a little more when sticky */
    if (exponent + kept > 310)
        return 0;                        /* beyond a double */
    if (exponent + kept < -330) {
        *value = negative ? -0.0 : 0.0;
        return 1;
    }
    dexp = (int)exponent;
    if (!sticky && kept <= 19 && m <= 9007199254740992ULL && dexp >= -22 && dexp <= 22) {
        v = dexp >= 0 ? (double)m * tens[dexp] : (double)m / tens[-dexp];
        *value = negative ? -v : v;
        return 1;
    }
    /* q = m x 10^dexp x 2^s2 as a whole number of 63 or 64 bits */
    big_set(&num, 0);
    for (i = 0; i < kept; i++) {
        big_mul(&num, 10);
        if (keep[i]) {                   /* add the digit */
            unsigned long carry = keep[i];
            int k;
            for (k = 0; carry && k < num.n; k++) {
                carry += num.d[k];
                num.d[k] = (unsigned short)(carry & 0xFFFF);
                carry >>= 16;
            }
            if (carry && num.n < PIECES)
                num.d[num.n++] = (unsigned short)carry;
        }
    }
    big_set(&den, 1);
    if (dexp > 0)
        big_mul_pow(&num, 10, dexp);
    else
        big_mul_pow(&den, 10, -dexp);
    s2 = 63 - (big_bits(&num) - big_bits(&den));
    if (s2 > 0)
        big_mul_pow(&num, 2, s2);
    else
        big_mul_pow(&den, 2, -s2);
    t = den;
    big_mul_pow(&t, 2, 63);
    q = 0;
    for (top = 63; top >= 0; top--) {
        if (big_cmp(&num, &t) >= 0) {
            big_sub(&num, &t);
            q |= 1ULL << top;
        }
        big_half(&t);
    }
    sticky |= num.n != 0;
    for (top = 63; !(q >> top); top--)
        ;
    e2 = top - s2;                       /* the value is in [2^e2, 2^(e2+1)) */
    if (e2 > 1023)
        return 0;
    prec = e2 >= -1022 ? 53 : 53 - (-1022 - e2);
    drop = top + 1 - prec;
    if (drop > 64) {
        *value = negative ? -0.0 : 0.0;
        return 1;
    }
    if (drop == 64) {
        r = 0;
        rem = q;
        half = 1ULL << 63;
    } else {
        r = q >> drop;
        rem = q & ((1ULL << drop) - 1);
        half = 1ULL << (drop - 1);
    }
    if (rem > half || (rem == half && (sticky || (r & 1))))
        r++;
    if (e2 == 1023 && r >> 53)
        return 0;                        /* rounded up past the largest double */
    v = scale2((double)r, drop - s2);
    *value = negative ? -v : v;
    return 1;
}

/* ---- Writing ---- */

/* v (above 0) as n significant digits, rounded half up from its exact
 * value; the power of ten of the first. */
static int digits_of(double v, int n, char *digits)
{
    big num, den;
    double x = v;
    int bits = 0, k, e, i, d;

    /* v = x * 2^bits, x a whole number from 2^52 to 2^53 */
    while (x >= 9007199254740992.0)
        for (k = 9; k >= 0; k--)
            if (x / two_up[k] >= 4503599627370496.0) {
                x /= two_up[k];
                bits += 1 << k;
            }
    while (x < 4503599627370496.0)
        for (k = 9; k >= 0; k--)
            if (x * two_up[k] < 9007199254740992.0) {
                x *= two_up[k];
                bits -= 1 << k;
            }
    big_set(&num, (unsigned long long)x);
    big_set(&den, 1);
    k = bits + 52;
    e = k >= 0 ? (int)(((long)k * 78913L) >> 18) : -(int)((-(long)k * 78913L + 262143L) >> 18);
    if (bits > 0)
        big_mul_pow(&num, 2, bits);
    else
        big_mul_pow(&den, 2, -bits);
    if (e > 0)
        big_mul_pow(&den, 10, e);
    else
        big_mul_pow(&num, 10, -e);
    while (big_cmp(&num, &den) < 0) {
        big_mul(&num, 10);
        e--;
    }
    d = 0;
    while (big_cmp(&num, &den) >= 0) {
        big_sub(&num, &den);
        if (++d == 10) {
            big_mul(&den, 10);
            e++;
            d = 1;
        }
    }
    digits[0] = (char)('0' + d);
    for (i = 1; i < n; i++) {
        big_mul(&num, 10);
        d = 0;
        while (big_cmp(&num, &den) >= 0) {
            big_sub(&num, &den);
            d++;
        }
        digits[i] = (char)('0' + d);
    }
    big_mul(&num, 2);
    if (big_cmp(&num, &den) >= 0) {
        for (i = n - 1; i >= 0 && digits[i] == '9'; i--)
            digits[i] = '0';
        if (i >= 0) {
            digits[i]++;
        } else {
            digits[0] = '1';
            e++;
        }
    }
    return e;
}

static void put_digits(char *p, const char *digits, int nd, int e)
{
    int i;
    while (nd > 1 && digits[nd - 1] == '0')
        nd--;
    if (e >= 15 || e < -9) {
        *p++ = digits[0];
        if (nd > 1) {
            *p++ = '.';
            for (i = 1; i < nd; i++)
                *p++ = digits[i];
        }
        *p++ = 'E';
        *p++ = e < 0 ? '-' : '+';
        if (e < 0)
            e = -e;
        if (e >= 100)
            *p++ = (char)('0' + e / 100);
        *p++ = (char)('0' + e / 10 % 10);
        *p++ = (char)('0' + e % 10);
    } else if (e >= 0) {
        for (i = 0; i <= e; i++)
            *p++ = i < nd ? digits[i] : '0';
        if (nd > e + 1) {
            *p++ = '.';
            for (; i < nd; i++)
                *p++ = digits[i];
        }
    } else {
        *p++ = '0';
        *p++ = '.';
        for (i = -1; i > e; i--)
            *p++ = '0';
        for (i = 0; i < nd; i++)
            *p++ = digits[i];
    }
    *p = 0;
}

void owf_number_exact(double v, char *out)
{
    char digits[17], *p = out;
    int n, e;
    double back;

    if (v != v || v > 1.7976931348623157e308 || v < -1.7976931348623157e308) {
        strcpy(out, "0");                /* a file cannot hold it; the callers keep errors apart */
        return;
    }
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    if (v == 0) {
        strcpy(out, "0");
        return;
    }
    for (n = 15; n <= 17; n++) {
        e = digits_of(v, n, digits);
        put_digits(p, digits, n, e);
        if (n == 17 || (owf_number_parse(p, strlen(p), &back) && back == v))
            return;
    }
}

/* ---- Dates and times: days since 30 December 1899 ----
 * The proleptic Gregorian calendar, by whole-number arithmetic. */

long owf_days_from_date(long y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L + 25569L;    /* from 1 January 1970, then from 30 December 1899 */
}

void owf_date_from_days(long days, long *y, int *m, int *d)
{
    long z = days - 25569L + 719468L, era, doe, yoe, doy, mp;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yoe + era * 400 + (*m <= 2);
}

/* Whole digits at *p, at most max of them; -1 when there are none. */
static long whole(const char **p, const char *end, int max)
{
    long v = 0;
    int n = 0;
    while (*p < end && **p >= '0' && **p <= '9' && n < max) {
        v = v * 10 + (**p - '0');
        (*p)++;
        n++;
    }
    return n ? v : -1;
}

/* Seconds with a fraction ("30", "30.25") as a double; -1 when not one. */
static double seconds(const char **p, const char *end)
{
    const char *start = *p;
    double v;
    while (*p < end && ((**p >= '0' && **p <= '9') || **p == '.'))
        (*p)++;
    if (*p == start || *p - start > 30 || !owf_number_parse(start, (size_t)(*p - start), &v))
        return -1;
    return v;
}

int owf_parse_iso_date(const char *s, double *serial)
{
    const char *p = s, *end = s + strlen(s);
    long y, mo, d, h = 0, mi = 0;
    double sec = 0;
    int negative = 0;
    if (*p == '-') {
        negative = 1;
        p++;
    }
    y = whole(&p, end, 6);
    if (y < 0 || p >= end || *p++ != '-')
        return 0;
    mo = whole(&p, end, 2);
    if (mo < 1 || mo > 12 || p >= end || *p++ != '-')
        return 0;
    d = whole(&p, end, 2);
    if (d < 1 || d > 31)
        return 0;
    if (p < end && *p == 'T') {
        p++;
        h = whole(&p, end, 2);
        if (h < 0 || h > 24 || p >= end || *p++ != ':')
            return 0;
        mi = whole(&p, end, 2);
        if (mi < 0 || mi > 59)
            return 0;
        if (p < end && *p == ':') {
            p++;
            sec = seconds(&p, end);
            if (sec < 0 || sec >= 61)
                return 0;
        }
    }
    /* a time zone is left as it is: the cell shows the time written */
    if (p < end && *p != 'Z' && *p != '+' && *p != '-')
        return 0;
    *serial = (double)owf_days_from_date(negative ? -y : y, (int)mo, (int)d) +
              ((double)(h * 3600 + mi * 60) + sec) / 86400.0;
    return 1;
}

int owf_parse_iso_duration(const char *s, double *days)
{
    const char *p = s, *end = s + strlen(s);
    double total = 0, v;
    int negative = 0, in_time = 0, any = 0;
    if (*p == '-') {
        negative = 1;
        p++;
    }
    if (p >= end || *p++ != 'P')
        return 0;
    while (p < end) {
        if (*p == 'T') {
            in_time = 1;
            p++;
            continue;
        }
        v = seconds(&p, end);
        if (v < 0 || v > 1e9 || p >= end)
            return 0;
        switch (*p++) {
        case 'D': if (in_time) return 0; total += v * 86400.0; break;
        case 'H': if (!in_time) return 0; total += v * 3600.0; break;
        case 'M': if (!in_time) return 0; total += v * 60.0; break;
        case 'S': if (!in_time) return 0; total += v; break;
        default: return 0;
        }
        any = 1;
    }
    if (!any)
        return 0;
    *days = (negative ? -total : total) / 86400.0;
    return 1;
}

/* Milliseconds of a day's fraction, rounded; carries into the day. */
static void split_serial(double serial, long *day, long *ms)
{
    double f;
    *day = (long)serial;
    if ((double)*day > serial)
        (*day)--;
    f = (serial - (double)*day) * 86400000.0 + 0.5;
    *ms = (long)f;
    if (*ms >= 86400000L) {
        *ms -= 86400000L;
        (*day)++;
    }
}

static void put_time_of_day(owf_buf *out, long ms)
{
    owf_buf_printf(out, "%02ld:%02ld:%02ld", ms / 3600000L, ms / 60000L % 60, ms / 1000 % 60);
    if (ms % 1000)
        owf_buf_printf(out, ".%03ld", ms % 1000);
}

int owf_put_iso_date(owf_buf *out, double serial, int with_time)
{
    long day, ms, y;
    int m, d;
    if (serial != serial || serial > 2958465.0 || serial < -693593.0)
        return 0;                        /* beyond 9999 or before year 1 */
    split_serial(serial, &day, &ms);
    owf_date_from_days(day, &y, &m, &d);
    owf_buf_printf(out, "%04ld-%02d-%02d", y, m, d);
    if (with_time || ms) {
        owf_buf_putc(out, 'T');
        put_time_of_day(out, ms);
    }
    return 1;
}

int owf_put_iso_duration(owf_buf *out, double days)
{
    long day, ms;
    if (days != days || days > 1e7 || days < -1e7)
        return 0;
    if (days < 0) {
        owf_buf_putc(out, '-');
        days = -days;
    }
    split_serial(days, &day, &ms);
    owf_buf_printf(out, "PT%02ldH%02ldM%02ldS", day * 24 + ms / 3600000L, ms / 60000L % 60, ms / 1000 % 60);
    if (ms % 1000) {
        out->length--;                   /* the S goes after the fraction */
        owf_buf_printf(out, ".%03ldS", ms % 1000);
    }
    return 1;
}

/* ---- What a number format shows ---- */

int owf_numfmt_kind(const char *format)
{
    const char *p;
    int date = 0, time = 0, percent = 0, text = 0, digits = 0;
    if (!format || owf_same_word(format, "General"))
        return OWF_NUMFMT_NUMBER;
    for (p = format; *p; p++) {
        int c = owf_lower((unsigned char)*p);
        if (*p == '"') {                 /* quoted text */
            for (p++; *p && *p != '"'; p++)
                ;
            if (!*p)
                break;
        } else if (*p == '\\' || *p == '_' || *p == '*') {
            if (!*++p)
                break;
        } else if (*p == '[') {          /* [Red], [$-409], [h] */
            const char *close = strchr(p, ']');
            int lc = owf_lower((unsigned char)p[1]);
            if (!close)
                break;
            if (lc == 'h' || lc == 'm' || lc == 's')
                time = 1;
            p = close;
        } else if (c == 'y' || c == 'd' || c == 'e' || c == 'g' || c == 'b') {
            if (c != 'e' || !digits)     /* "0.00E+00" is not a year */
                date = 1;
        } else if (c == 'h' || c == 's') {
            time = 1;
        } else if (c == 'm') {
            time |= 2;                   /* minutes or months: the neighbours say */
        } else if (*p == '%') {
            percent = 1;
        } else if (*p == '@') {
            text = 1;
        } else if (*p == '0' || *p == '#' || *p == '?') {
            digits = 1;
        } else if (*p == ';') {
            break;                       /* the first section says */
        }
    }
    if (date)
        return OWF_NUMFMT_DATE;
    if (time & 1)
        return OWF_NUMFMT_TIME;
    if (time & 2)
        return OWF_NUMFMT_DATE;          /* "mmm" alone is a month */
    if (percent)
        return OWF_NUMFMT_PERCENT;
    if (text && !digits)
        return OWF_NUMFMT_TEXT;
    return OWF_NUMFMT_NUMBER;
}

/* ---- Excel's own tables, as XLSX uses them too ---- */

/* The number formats Excel has without saying ([MS-XLS] 2.4.126, ECMA-376
 * 18.8.30); NULL for one that depends on the country (currency, East Asian
 * dates). */
const char *owf_xl_builtin_format(unsigned id)
{
    static const char *const formats[50] = {
        "General", "0", "0.00", "#,##0", "#,##0.00", NULL, NULL, NULL, NULL, "0%",
        "0.00%", "0.00E+00", "# ?/?", "# ?\?/?\?", "m/d/yyyy", "d-mmm-yy", "d-mmm", "mmm-yy",
        "h:mm AM/PM", "h:mm:ss AM/PM", "h:mm", "h:mm:ss", "m/d/yyyy h:mm", NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "#,##0 ;(#,##0)",
        "#,##0 ;[Red](#,##0)", "#,##0.00;(#,##0.00)", "#,##0.00;[Red](#,##0.00)", NULL, NULL, NULL,
        NULL, "mm:ss", "[h]:mm:ss", "mm:ss.0", "##0.0E+0", "@"
    };
    return id < 50 ? formats[id] : NULL;
}

/* Excel's colours by number: eight fixed, then the 56 of the default
 * palette (indexes 8 to 63). */
const unsigned long owf_xl_palette[64] = {
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
    0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080,
    0x9999FF, 0x993366, 0xFFFFCC, 0xCCFFFF, 0x660066, 0xFF8080, 0x0066CC, 0xCCCCFF,
    0x000080, 0xFF00FF, 0xFFFF00, 0x00FFFF, 0x800080, 0x800000, 0x008080, 0x0000FF,
    0x00CCFF, 0xCCFFFF, 0xCCFFCC, 0xFFFF99, 0x99CCFF, 0xFF99CC, 0xCC99FF, 0xFFCC99,
    0x3366FF, 0x33CCCC, 0x99CC00, 0xFFCC00, 0xFF9900, 0xFF6600, 0x666699, 0x969696,
    0x003366, 0x339966, 0x003300, 0x333300, 0x993300, 0x993366, 0x333399, 0x333333
};

/* Text for a file, as UTF-8 that is valid: bad bytes become U+FFFD. */
void owf_buf_put_utf8_clean(owf_buf *out, const char *s, size_t length)
{
    const unsigned char *p = (const unsigned char *)s, *end = p + length;
    if (owf_utf8_valid(p, length)) {
        owf_buf_put(out, s, length);
        return;
    }
    while (p < end) {
        if (*p < 0x80)
            owf_buf_putc(out, *p++);
        else
            owf_buf_put_utf8(out, owf_utf8_next(&p, end));
    }
}

const char *owf_xl_error_text(int error)
{
    const char *e = owf_error_name(error);
    return e ? e : "#N/A";
}

int owf_xl_error_code(const char *s, size_t n)
{
    static const int codes[] = { OWF_ERROR_NULL, OWF_ERROR_DIV0, OWF_ERROR_VALUE, OWF_ERROR_REF,
                                 OWF_ERROR_NAME, OWF_ERROR_NUM, OWF_ERROR_NA };
    unsigned i;
    for (i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        const char *e = owf_error_name(codes[i]);
        if (strlen(e) == n && !memcmp(e, s, n))
            return codes[i];
    }
    return -1;
}
