/*
 * libowf: numbers as people read and write them. Dates as serial numbers
 * (Excel's 1900 calendar, with its 29 February 1900, or the 1904 one);
 * text as a number, the way typed input and VALUE read it; and a value
 * through a number format: General, 0.00, #,##0, percentages, scientific,
 * fractions, dates and times, "@", up to four sections with colours and
 * conditions. Digits come from the number's 15 significant digits, as
 * Excel shows them, so 2.675 with two decimals is 2.68.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- Dates ---- */

/* Days from 1 January 1970 (proleptic Gregorian). */
static long days_from_civil(long y, long m, long d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *year, int *month, int *day)
{
    long era, doe, yoe, y, doy, mp, d, m;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    *year = (int)(y + (m <= 2));
    *month = (int)m;
    *day = (int)d;
}

#define LAST_SERIAL_1900 2958465L        /* 31 December 9999 */

int calc_date_serial(long year, long month, long day, int date1904, double *serial)
{
    long s;
    if (year < 0 || year > 9999)
        return 0;
    if (year < 1900)
        year += 1900;
    year += month >= 1 ? (month - 1) / 12 : -((12 - month) / 12);
    month = ((month - 1) % 12 + 12) % 12 + 1;
    if (year < 0 || year > 9999)
        return 0;
    if (date1904) {
        s = days_from_civil(year, month, 1) - days_from_civil(1904, 1, 1) + day - 1;
    } else {
        s = days_from_civil(year, month, 1) - days_from_civil(1899, 12, 31) + day - 1;
        if (year > 1900 || (year == 1900 && month >= 3))
            s++;                         /* Excel's 29 February 1900 */
    }
    if (s < 0 || s > LAST_SERIAL_1900 - (date1904 ? 1462 : 0))
        return 0;
    *serial = (double)s;
    return 1;
}

void calc_serial_date(long serial, int date1904, int *year, int *month, int *day)
{
    if (date1904) {
        civil_from_days(serial + days_from_civil(1904, 1, 1), year, month, day);
    } else if (serial == 0) {
        *year = 1900;
        *month = 1;
        *day = 0;
    } else if (serial == 60) {
        *year = 1900;
        *month = 2;
        *day = 29;
    } else {
        civil_from_days(serial + days_from_civil(1899, 12, serial < 60 ? 31 : 30), year, month, day);
    }
}

static int days_in_month(int year, int month)
{
    static const unsigned char days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && (year == 1900 || (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))))
        return 29;
    return days[month - 1];
}

static const char *const month_names[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
    "November", "December"
};
static const char *const day_names[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                                          "Saturday" };

void calc_number_text(double n, char *out)
{
    owf_number_text(n, out);
}

/* ---- Text as a number ---- */

typedef struct {
    const char *p, *e;
} cur;

static int is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int is_alpha(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static void spaces(cur *c)
{
    while (c->p < c->e && *c->p == ' ')
        c->p++;
}

static int take(cur *c, char ch)
{
    if (c->p < c->e && *c->p == ch) {
        c->p++;
        return 1;
    }
    return 0;
}

/* Digits as a number: how many there were (0: none); at most 9. */
static int digits(cur *c, long *v)
{
    int n = 0;
    *v = 0;
    while (c->p < c->e && is_digit(*c->p)) {
        if (++n > 9)
            return 0;
        *v = *v * 10 + (*c->p++ - '0');
    }
    return n;
}

static const char *const currencies[] = { "$", "\xC2\xA3", "\xE2\x82\xAC", "\xC2\xA5" };

static int take_currency(cur *c, char *out, size_t size)
{
    size_t i;
    for (i = 0; i < sizeof currencies / sizeof currencies[0]; i++) {
        size_t n = strlen(currencies[i]);
        if ((size_t)(c->e - c->p) >= n && !memcmp(c->p, currencies[i], n)) {
            if (out && size > n)
                memcpy(out, currencies[i], n + 1);
            c->p += n;
            return 1;
        }
    }
    return 0;
}

/* "1,234.5", "-3", "(12)", "1E3", "50%", "£3.50", "12 €" */
static int plain_number(cur c, double *out, int *kind, int *decimals, char *currency, size_t csize)
{
    char buf[80];
    size_t n = 0;
    int negative = 0, paren = 0, money = 0, pct = 0, groups = 0, ndig = 0;
    *decimals = 0;
    if (take(&c, '(')) {
        paren = 1;
        spaces(&c);
    }
    if (take(&c, '-'))
        negative = 1;
    else
        take(&c, '+');
    if (take_currency(&c, currency, csize)) {
        money = 1;
        if (!negative && take(&c, '-'))
            negative = 1;
    }
    /* the whole part: commas only between groups of three */
    while (c.p < c.e && is_digit(*c.p)) {
        int k = 0;
        while (c.p < c.e && is_digit(*c.p)) {
            if (n + 2 >= sizeof buf)
                return 0;
            buf[n++] = *c.p++;
            k++;
            ndig++;
        }
        if (groups && k != 3)
            return 0;
        if (c.p + 1 < c.e && *c.p == ',' && is_digit(c.p[1])) {
            if (groups == 0 && k > 3)
                return 0;
            groups++;
            c.p++;
            continue;
        }
        break;
    }
    if (take(&c, '.')) {
        buf[n++] = '.';
        while (c.p < c.e && is_digit(*c.p)) {
            if (n + 2 >= sizeof buf)
                return 0;
            buf[n++] = *c.p++;
            (*decimals)++;
            ndig++;
        }
    }
    if (!ndig)
        return 0;
    if (c.p < c.e && (*c.p == 'e' || *c.p == 'E')) {
        cur save = c;
        c.p++;
        if (n + 2 >= sizeof buf)
            return 0;
        buf[n++] = 'e';
        if (c.p < c.e && (*c.p == '+' || *c.p == '-'))
            buf[n++] = *c.p++;
        if (c.p >= c.e || !is_digit(*c.p)) {
            c = save;
            return 0;
        }
        while (c.p < c.e && is_digit(*c.p)) {
            if (n + 2 >= sizeof buf)
                return 0;
            buf[n++] = *c.p++;
        }
    }
    buf[n] = 0;
    spaces(&c);
    if (take(&c, '%'))
        pct = 1;
    spaces(&c);
    if (!money && take_currency(&c, currency, csize))
        money = 1;
    spaces(&c);
    if (paren) {
        if (negative || !take(&c, ')'))
            return 0;
        negative = 1;
    }
    spaces(&c);
    if (c.p != c.e)
        return 0;
    *out = strtod(buf, NULL);
    if (pct)
        *out /= 100;
    if (negative)
        *out = -*out;
    *kind = pct ? NK_PERCENT : money ? NK_CURRENCY : NK_NUMBER;
    return 1;
}

/* A month's name or its first three letters (any case): 1 to 12, or 0. */
static int month_word(cur *c)
{
    const char *s = c->p;
    size_t n = 0;
    int i;
    while (s + n < c->e && is_alpha(s[n]))
        n++;
    if (n < 3)
        return 0;
    for (i = 0; i < 12; i++) {
        size_t k;
        const char *name = month_names[i];
        if (n > strlen(name) && !(i == 8 && n == 4))
            continue;
        for (k = 0; k < n; k++)
            if (owf_lower((unsigned char)s[k]) != owf_lower((unsigned char)(i == 8 && n == 4 && k == 3 ? 't' : name[k])))
                break;
        if (k == n) {
            c->p += n;
            return i + 1;
        }
    }
    return 0;
}

static long two_digit_year(long y, int n)
{
    if (n <= 2)
        return y < 30 ? 2000 + y : 1900 + y;
    return n == 4 ? y : -1;
}

static int this_year(void)
{
    owf_time t;
    return owf_sys_now(&t) ? t.year : 2026;
}

/* A date: 25/10/2026, 10/25/2026 (by the order), 2026-10-25, 25-Oct-2026,
 * 25 October 2026, Oct 25, 2026, 25/10, Oct-26. style: 0 numbers, 1 with
 * the month's name, 2 day and month's name, 3 month's name and year, 4 ISO. */
static int date_part(cur *c, int dmy, long *year, long *month, long *day, int *style)
{
    cur s = *c;
    long a, b, y;
    int na, nb, ny, m;
    char sep;

    if ((m = month_word(&s)) != 0) {     /* Oct 25, 2026; October 2026; Oct-26 */
        int dash = 0;
        if (take(&s, '-') || take(&s, '/'))
            dash = 1;
        else
            spaces(&s);
        if (!(na = digits(&s, &a)))
            return 0;
        if (!dash && na <= 2 && a >= 1) {
            cur t = s;
            take(&t, ',');
            spaces(&t);
            if ((ny = digits(&t, &y)) != 0 && (y = two_digit_year(y, ny)) >= 0) {
                *year = y;
                *month = m;
                *day = a;
                *style = 1;
                *c = t;
                return a <= days_in_month((int)y, m);
            }
            if (s.p == s.e || *s.p == ' ') {
                *year = this_year();
                *month = m;
                *day = a;
                *style = 2;
                *c = s;
                return a <= days_in_month((int)*year, m);
            }
        }
        if ((y = two_digit_year(a, na)) < 0)
            return 0;
        *year = y;
        *month = m;
        *day = 1;
        *style = 3;
        *c = s;
        return 1;
    }
    if (!(na = digits(&s, &a)))
        return 0;
    if (s.p >= s.e)
        return 0;
    sep = *s.p;
    if (sep != '/' && sep != '-' && sep != ' ')
        return 0;
    s.p++;
    if (sep == ' ')
        spaces(&s);
    if ((m = month_word(&s)) != 0) {     /* 25-Oct-2026, 25 Oct, 25-Oct */
        cur t = s;
        if (a < 1 || na > 2)
            return 0;
        if ((sep == ' ' ? (spaces(&t), 1) : take(&t, sep)) && (ny = digits(&t, &y)) != 0 &&
            (y = two_digit_year(y, ny)) >= 0) {
            *year = y;
            *style = 1;
            s = t;
        } else {
            *year = this_year();
            *style = 2;
        }
        *month = m;
        *day = a;
        *c = s;
        return a <= days_in_month((int)*year, m);
    }
    if (sep == ' ' || !(nb = digits(&s, &b)))
        return 0;
    if (take(&s, sep)) {                 /* three numbers */
        if (!(ny = digits(&s, &y)))
            return 0;
        if (na == 4) {                   /* ISO: year first */
            *year = a;
            *month = b;
            *day = y;
            *style = 4;
        } else {
            if ((y = two_digit_year(y, ny)) < 0 || na > 2 || nb > 2)
                return 0;
            *year = y;
            *month = dmy == OWF_CALC_MDY ? a : b;
            *day = dmy == OWF_CALC_MDY ? b : a;
            *style = 0;
        }
    } else {                             /* two: day and month this year, or month and year */
        long d = dmy == OWF_CALC_MDY ? b : a, mo = dmy == OWF_CALC_MDY ? a : b;
        if (na <= 2 && nb <= 2 && mo >= 1 && mo <= 12 && d >= 1 && d <= days_in_month(this_year(), (int)mo)) {
            *year = this_year();
            *month = mo;
            *day = d;
            *style = 2;
        } else if (na <= 2 && a >= 1 && a <= 12 && (y = two_digit_year(b, nb)) >= 0) {
            *year = y;
            *month = a;
            *day = 1;
            *style = 3;
        } else {
            return 0;
        }
    }
    if (*month < 1 || *month > 12 || *day < 1 || *year < 1900 || *year > 9999 ||
        *day > days_in_month((int)*year, (int)*month))
        return 0;
    *c = s;
    return 1;
}

/* A time: 13:45, 13:45:30, 13:45:30.5, 1:45 PM, 1 PM. *fraction of a day. */
static int time_part(cur *c, double *fraction, int *seconds, int *ampm)
{
    cur s = *c;
    long h, m = 0, sec = 0;
    double frac = 0;
    int nh, colon = 0;
    *seconds = 0;
    *ampm = 0;
    if (!(nh = digits(&s, &h)) || nh > 4)
        return 0;
    if (take(&s, ':')) {
        colon = 1;
        if (!digits(&s, &m) || m > 59)
            return 0;
        if (take(&s, ':')) {
            *seconds = 1;
            if (!digits(&s, &sec) || sec > 59)
                return 0;
            if (take(&s, '.')) {
                double scale = 0.1;
                while (s.p < s.e && is_digit(*s.p)) {
                    frac += (*s.p++ - '0') * scale;
                    scale /= 10;
                }
            }
        }
    }
    spaces(&s);
    if (s.p < s.e && (*s.p == 'a' || *s.p == 'A' || *s.p == 'p' || *s.p == 'P')) {
        int pm = *s.p == 'p' || *s.p == 'P';
        s.p++;
        if (s.p < s.e && (*s.p == 'm' || *s.p == 'M'))
            s.p++;
        if (h < 1 || h > 12)
            return 0;
        h = h % 12 + (pm ? 12 : 0);
        *ampm = 1;
    } else if (!colon) {
        return 0;
    }
    if (*ampm && s.p < s.e && is_alpha(*s.p))
        return 0;
    *fraction = ((double)h * 3600 + (double)m * 60 + (double)sec + frac) / 86400;
    *c = s;
    return 1;
}

int calc_parse_number(const char *p, size_t len, int dmy, int date1904, double *out, int *kind, int *decimals,
                      char *currency, size_t currency_size)
{
    cur c;
    long y, m, d;
    double serial, frac;
    int style, secs, ampm;

    c.p = p;
    c.e = p + len;
    spaces(&c);
    while (c.e > c.p && c.e[-1] == ' ')
        c.e--;
    if (c.p == c.e)
        return 0;
    if (currency && currency_size)
        currency[0] = 0;
    if (plain_number(c, out, kind, decimals, currency, currency_size))
        return 1;
    {
        cur t = c;
        if (time_part(&t, &frac, &secs, &ampm) && t.p == t.e) {
            *out = frac;
            *kind = NK_TIME;
            *decimals = secs | (ampm << 1);
            return 1;
        }
    }
    if (!date_part(&c, dmy, &y, &m, &d, &style) || !calc_date_serial(y, m, d, date1904, &serial))
        return 0;
    if (c.p == c.e) {
        *out = serial;
        *kind = NK_DATE;
        *decimals = style;
        return 1;
    }
    spaces(&c);
    if (!time_part(&c, &frac, &secs, &ampm) || c.p != c.e)
        return 0;
    *out = serial + frac;
    *kind = NK_DATETIME;
    *decimals = style;
    return 1;
}

/* ---- Decimal digits ---- */

/* v >= 0 as up to 15 significant digits: v = 0.DIGITS x 10^point.
 * Returns how many digits (0 for zero). */
static int decimal(double v, char *dig, int *point)
{
    char text[40];
    const char *s;
    int n = 0, before = 0, seen_point = 0, exp = 0, lead = 1;
    if (v == 0) {
        *point = 0;
        return 0;
    }
    if (v < 1e15 && v == floor(v)) {     /* whole numbers without the long way */
        char tmp[20];
        int k = 0;
        double x = v;
        while (x >= 1) {
            double q = floor(x / 10);
            tmp[k++] = (char)('0' + (int)(x - q * 10));
            x = q;
        }
        *point = k;
        while (k)
            dig[n++] = tmp[--k];
        while (n > 1 && dig[n - 1] == '0')
            n--;
        return n;
    }
    owf_number_text(v, text);
    for (s = text; *s && *s != 'E'; s++) {
        if (*s == '.') {
            seen_point = 1;
            continue;
        }
        if (*s == '0' && lead) {
            if (seen_point)
                before--;
            continue;
        }
        lead = 0;
        dig[n++] = *s;
        if (!seen_point)
            before++;
    }
    if (*s == 'E')
        exp = atoi(s + 1);
    while (n > 1 && dig[n - 1] == '0')
        n--;
    *point = before + exp;
    return n;
}

/* Rounds the digits to keep of them (half up): returns the new count, and
 * *point may grow by one. */
static int round_digits(char *dig, int n, int keep, int *point)
{
    int i;
    if (keep >= n)
        return n;
    if (keep < 0)
        return 0;
    if (dig[keep] >= '5') {
        for (i = keep - 1; i >= 0 && dig[i] == '9'; i--)
            dig[i] = '0';
        if (i >= 0) {
            dig[i]++;
        } else {
            memmove(dig + 1, dig, (size_t)keep);
            dig[0] = '1';
            (*point)++;
            keep++;
        }
    }
    n = keep;
    while (n > 0 && dig[n - 1] == '0')
        n--;
    return n;
}

/* v >= 0 with `places` decimals: its whole part's digits (none for 0) and
 * exactly `places` fraction digits. */
static void fixed(double v, int places, char *whole, size_t wsize, char *frac)
{
    char dig[40];
    int point, n = decimal(v, dig, &point), i, w = 0;
    n = round_digits(dig, n, point + places, &point);
    if (n == 0)
        point = 0;
    for (i = 0; i < point && (size_t)w + 1 < wsize; i++)
        whole[w++] = i < n ? dig[i] : '0';
    whole[w] = 0;
    for (i = 0; i < places; i++) {
        int k = point + i;
        frac[i] = k >= 0 && k < n ? dig[k] : '0';
    }
    frac[places] = 0;
}

/* ---- Number formats ---- */

typedef struct {
    const char *s, *e;
    int has_cond;
    int cond_op;                 /* OP_EQ ... OP_GE */
    double cond;
    unsigned long colour;
} section;

static const struct {
    const char *name;
    unsigned long rgb;
} colours[] = {
    { "black", 0x000000 }, { "blue", 0x0000FF }, { "cyan", 0x00FFFF }, { "green", 0x00FF00 },
    { "magenta", 0xFF00FF }, { "red", 0xFF0000 }, { "white", 0xFFFFFF }, { "yellow", 0xFFFF00 },
};
static const unsigned long palette8[8] = { 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00,
                                           0xFF00FF, 0x00FFFF };

/* The text from s to e is word, in any case. */
static int word_is(const char *s, const char *e, const char *word)
{
    size_t n = strlen(word), i;
    if ((size_t)(e - s) != n)
        return 0;
    for (i = 0; i < n; i++)
        if (owf_lower((unsigned char)s[i]) != owf_lower((unsigned char)word[i]))
            return 0;
    return 1;
}

/* A whole number (>= 0, held in a double) in at least width digits. No
 * floating point in printf: owf.library's own printf has none. */
static void put_whole(owf_buf *out, double v, int width)
{
    char tmp[24];
    int n = 0;
    v = floor(v);
    do {
        double q = floor(v / 10);
        tmp[n++] = (char)('0' + (int)(v - q * 10));
        v = q;
    } while (v >= 1 && n < (int)sizeof tmp);
    while (width-- > n)
        owf_buf_putc(out, '0');
    while (n)
        owf_buf_putc(out, tmp[--n]);
}

/* What is inside [...]: a colour or a condition goes into the section. */
static void bracket(section *sec, const char *s, const char *e)
{
    size_t i;
    if (s < e && (*s == '<' || *s == '>' || *s == '=')) {
        int op;
        if (s + 1 < e && s[0] == '<' && s[1] == '=') {
            op = OP_LE;
            s += 2;
        } else if (s + 1 < e && s[0] == '>' && s[1] == '=') {
            op = OP_GE;
            s += 2;
        } else if (s + 1 < e && s[0] == '<' && s[1] == '>') {
            op = OP_NE;
            s += 2;
        } else {
            op = *s == '<' ? OP_LT : *s == '>' ? OP_GT : OP_EQ;
            s++;
        }
        {
            char num[40];
            size_t n = (size_t)(e - s);
            if (n >= sizeof num)
                return;
            memcpy(num, s, n);
            num[n] = 0;
            sec->cond = strtod(num, NULL);
            sec->cond_op = op;
            sec->has_cond = 1;
        }
        return;
    }
    for (i = 0; i < sizeof colours / sizeof colours[0]; i++)
        if (word_is(s, e, colours[i].name)) {
            sec->colour = colours[i].rgb;
            return;
        }
    if (e - s > 5 && (!strncmp(s, "Color", 5) || !strncmp(s, "color", 5) || !strncmp(s, "COLOR", 5))) {
        int k = atoi(s + 5);
        if (k >= 1 && k <= 8)
            sec->colour = palette8[k - 1];
    }
}

/* Splits a format into its sections (at most four). */
static int sections(const char *fmt, section *sec)
{
    const char *p = fmt;
    int n = 0;
    sec[0].s = p;
    sec[0].has_cond = 0;
    sec[0].colour = OWF_COLOUR_AUTO;
    for (; *p; p++) {
        if (*p == '"') {
            for (p++; *p && *p != '"'; p++)
                ;
            if (!*p)
                break;
        } else if (*p == '\\' || *p == '_' || *p == '*') {
            if (!p[1])
                break;
            p++;
        } else if (*p == '[') {
            const char *close = strchr(p, ']');
            if (!close)
                break;
            bracket(&sec[n], p + 1, close);
            p = close;
        } else if (*p == ';') {
            sec[n].e = p;
            if (++n == 4)
                return n;
            sec[n].s = p + 1;
            sec[n].has_cond = 0;
            sec[n].colour = OWF_COLOUR_AUTO;
        }
    }
    sec[n].e = p;
    return n + 1;
}

static int cond_true(const section *s, double v)
{
    switch (s->cond_op) {
    case OP_EQ: return v == s->cond;
    case OP_NE: return v != s->cond;
    case OP_LT: return v < s->cond;
    case OP_LE: return v <= s->cond;
    case OP_GT: return v > s->cond;
    }
    return v >= s->cond;
}

/* What a section holds. */
typedef struct {
    int date, general, text_at, ampm;
    int int_ph, frac_ph, point, group, scale, percent;
    int exp, exp_ph;
    int slash;                   /* a fraction */
} info;

static int is_ph(int c)
{
    return c == '0' || c == '#' || c == '?';
}

static void analyse(const char *s, const char *e, info *in)
{
    const char *p;
    int last_ph = 0, after_slash = 0;
    memset(in, 0, sizeof *in);
    for (p = s; p < e; p++) {
        int c = (unsigned char)*p, lc = owf_lower(c);
        if (c == '"') {
            for (p++; p < e && *p != '"'; p++)
                ;
            last_ph = 0;
            continue;
        }
        if (c == '\\' || c == '_' || c == '*') {
            p++;
            last_ph = 0;
            continue;
        }
        if (c == '[') {
            const char *close = p;
            while (close < e && *close != ']')
                close++;
            if (close - p >= 2 && (owf_lower((unsigned char)p[1]) == 'h' || owf_lower((unsigned char)p[1]) == 'm' ||
                                   owf_lower((unsigned char)p[1]) == 's'))
                in->date = 1;
            p = close;
            continue;
        }
        if (e - p >= 7 && word_is(p, p + 7, "general")) {
            in->general = 1;
            p += 6;
            continue;
        }
        if (e - p >= 5 && word_is(p, p + 5, "am/pm")) {
            in->ampm = in->date = 1;
            p += 4;
            continue;
        }
        if (e - p >= 3 && word_is(p, p + 3, "a/p")) {
            in->ampm = in->date = 1;
            p += 2;
            continue;
        }
        if (is_ph(c)) {
            if (in->exp)
                in->exp_ph++;
            else if (after_slash)
                ;
            else if (in->point)
                in->frac_ph++;
            else
                in->int_ph++;
            last_ph = 1;
            continue;
        }
        if (c == '.') {
            if (!in->point && !in->exp)
                in->point = 1;
            last_ph = 0;
            continue;
        }
        if (c == ',') {
            if (last_ph && p + 1 < e && is_ph((unsigned char)p[1]))
                in->group = 1;
            else if (last_ph || (p > s && p[-1] == ','))
                in->scale++;
            continue;
        }
        if (c == '%') {
            in->percent++;
            last_ph = 0;
            continue;
        }
        if (lc == 'e' && p + 1 < e && (p[1] == '+' || p[1] == '-') && !in->date) {
            in->exp = 1;
            p++;
            last_ph = 0;
            continue;
        }
        if (c == '/' && (in->int_ph || last_ph)) {
            in->slash = 1;
            after_slash = 1;
            continue;
        }
        if (c == '@') {
            in->text_at = 1;
            continue;
        }
        if (lc == 'y' || lc == 'd' || lc == 'h' || lc == 's' || lc == 'm')
            in->date = 1;
        last_ph = 0;
    }
}

/* A literal character at p (UTF-8 aware); returns where the next one starts. */
static const char *put_char(owf_buf *out, const char *p, const char *e)
{
    const char *q = p + 1;
    while (q < e && ((unsigned char)*q & 0xC0) == 0x80)
        q++;
    owf_buf_put(out, p, (size_t)(q - p));
    return q;
}

/* Text that is no format of its own: quoted, escaped, _x, *x, [$€-407];
 * returns where it ended, or NULL when p is something else. */
static const char *literal(owf_buf *out, const char *p, const char *e)
{
    if (*p == '"') {
        const char *q = p + 1;
        while (q < e && *q != '"')
            q++;
        owf_buf_put(out, p + 1, (size_t)(q - p - 1));
        return q < e ? q + 1 : q;
    }
    if (*p == '\\')
        return p + 1 < e ? put_char(out, p + 1, e) : p + 1;
    if (*p == '_') {
        owf_buf_putc(out, ' ');
        return p + 1 < e ? p + 2 : p + 1;
    }
    if (*p == '*')
        return p + 1 < e ? p + 2 : p + 1;
    if (*p == '[') {
        const char *close = p;
        while (close < e && *close != ']')
            close++;
        if (p + 1 < e && p[1] == '$') {  /* [$€-407]: the symbol */
            const char *s = p + 2, *dash = s;
            while (dash < close && *dash != '-')
                dash++;
            owf_buf_put(out, s, (size_t)(dash - s));
        }
        return close < e ? close + 1 : close;
    }
    return NULL;
}

/* Digits for a formatted number: one set, passed down, so that the
 * formatter's stack stays small (an Amiga task's is). */
typedef struct {
    char whole[320];             /* a double has at most 309 whole digits */
    char frac[32];
    char dig[32];
    char tmp[32];
} digits_t;

/* A General number in at most width characters (0: any). */
static void general(owf_buf *out, double v, int width, digits_t *d)
{
    char text[40], *whole = d->whole, *frac = d->frac;
    int len, neg = v < 0, places, digits_before;
    calc_number_text(v, text);
    len = (int)strlen(text);
    if (!width || len <= width) {
        owf_buf_puts(out, text);
        return;
    }
    if (neg)
        v = -v;
    if (!strchr(text, 'E') && v >= 1) {
        fixed(v, 0, whole, sizeof d->whole, frac);
        digits_before = (int)strlen(whole) + neg;
        if (digits_before <= width) {
            places = width - digits_before - 1;
            if (places > 30)
                places = 30;
            if (places > 0) {
                fixed(v, places, whole, sizeof d->whole, frac);
                while (places > 0 && frac[places - 1] == '0')
                    frac[--places] = 0;
            }
            if (neg)
                owf_buf_putc(out, '-');
            owf_buf_puts(out, whole[0] ? whole : "0");
            if (places > 0) {
                owf_buf_putc(out, '.');
                owf_buf_puts(out, frac);
            }
            return;
        }
    } else if (!strchr(text, 'E') && v >= 0.0001) {
        places = width - 2 - neg;
        if (places > 30)
            places = 30;
        if (places > 0) {
            fixed(v, places, whole, sizeof d->whole, frac);
            while (places > 0 && frac[places - 1] == '0')
                frac[--places] = 0;
            if (places > 0) {
                if (neg)
                    owf_buf_putc(out, '-');
                owf_buf_puts(out, whole[0] ? whole : "0");
                owf_buf_putc(out, '.');
                owf_buf_puts(out, frac);
                return;
            }
        }
    }
    {   /* scientific: d.dddE+XX */
        char *dig = d->dig;
        int point, n = decimal(v, dig, &point), e, keep, i;
        e = point - 1;
        keep = width - neg - (e >= 100 || e <= -100 ? 6 : 5);   /* d, E, sign, two or three digits */
        if (keep < 1) {
            for (i = 0; i < width; i++)
                owf_buf_putc(out, '#');
            return;
        }
        n = round_digits(dig, n, keep, &point);
        e = point - 1;
        if (n == 0) {
            dig[0] = '0';
            n = 1;
        }
        if (neg)
            owf_buf_putc(out, '-');
        owf_buf_putc(out, dig[0]);
        if (n > 1) {
            owf_buf_putc(out, '.');
            owf_buf_put(out, dig + 1, (size_t)(n - 1));
        }
        owf_buf_putc(out, 'E');
        owf_buf_putc(out, e < 0 ? '-' : '+');
        put_whole(out, e < 0 ? -e : e, 2);
    }
}

/* Digits into the placeholders of a number's whole part: the first one
 * takes any digits there are more of; thousands separators every three. */
typedef struct {
    const char *whole;           /* digits, "" for none */
    int len, ni, k, group;
} whole_out;

static void put_digit(owf_buf *out, whole_out *w, char d, int pos)
{
    owf_buf_putc(out, d);
    if (w->group && pos > 0 && pos % 3 == 0)
        owf_buf_putc(out, ',');
}

static void whole_ph(owf_buf *out, whole_out *w, int ph)
{
    int pos = w->ni - 1 - w->k, j;
    if (w->k == 0)
        for (j = 0; j < w->len - w->ni; j++)
            put_digit(out, w, w->whole[j], w->len - 1 - j);
    if (pos < w->len)
        put_digit(out, w, w->whole[w->len - 1 - pos], pos);
    else if (ph == '0')
        put_digit(out, w, '0', pos);
    else if (ph == '?')
        owf_buf_putc(out, ' ');
    w->k++;
}

/* A number through a section; neg: put a minus first, when what shows is
 * not all zeros. */
static void number_section(owf_buf *out, const char *s, const char *e, const info *in, double v, int neg,
                           digits_t *d)
{
    char *whole = d->whole, *frac = d->frac, *dig = d->dig;
    whole_out w;
    int fk = 0, places = in->frac_ph > 30 ? 30 : in->frac_ph, exponent = 0, i, point = 0, n;
    const char *p;

    for (i = 0; i < in->percent; i++)
        v *= 100;
    for (i = 0; i < in->scale; i++)
        v /= 1000;
    if (in->exp) {
        /* mantissa with int_ph whole digits ("##0.0E+0": exponents in threes) */
        int ni = in->int_ph ? (in->int_ph > 300 ? 300 : in->int_ph) : 1, keep;
        int eng = in->int_ph > 1 && *s != '0';
        n = decimal(v, dig, &point);
        if (n == 0) {
            exponent = 0;
            strcpy(whole, "0");
            for (i = 0; i < places; i++)
                frac[i] = '0';
            frac[places] = 0;
        } else {
            for (i = 0; i < 2; i++) {
                if (eng)
                    exponent = (int)floor((point - 1) / (double)ni) * ni;
                else
                    exponent = point - ni;
                keep = point - exponent + places;
                {
                    char *tmp = d->tmp;
                    int pt = point, k;
                    memcpy(tmp, dig, (size_t)n);
                    k = round_digits(tmp, n, keep, &pt);
                    if (pt != point && i == 0) {
                        point = pt;      /* 9.99 became 10.0: once more with the new length */
                        memcpy(dig, tmp, (size_t)k);
                        n = k;
                        continue;
                    }
                    {
                        int wl = point - exponent, j;
                        for (j = 0; j < wl; j++)
                            whole[j] = j < k ? tmp[j] : '0';
                        whole[wl > 0 ? wl : 0] = 0;
                        for (j = 0; j < places; j++)
                            frac[j] = wl + j < k && wl + j >= 0 ? tmp[wl + j] : '0';
                        frac[places] = 0;
                    }
                }
                break;
            }
        }
    } else {
        fixed(v, places, whole, sizeof d->whole, frac);
    }
    if (neg && (in->exp ? v != 0 : (whole[0] || strspn(frac, "0") != strlen(frac))))
        owf_buf_putc(out, '-');
    w.whole = whole;
    w.len = (int)strlen(whole);
    w.ni = in->int_ph;
    w.k = 0;
    w.group = in->group;
    for (p = s; p < e;) {
        const char *q = literal(out, p, e);
        int c = (unsigned char)*p;
        if (q) {
            p = q;
            continue;
        }
        if (is_ph(c)) {
            if (fk < 0) {
                /* exponent digits, handled at the E */
            } else if (w.k < w.ni && !(in->point && fk > 0)) {
                whole_ph(out, &w, c);
            } else if (fk < places) {
                int rest = 1, j;
                for (j = fk; j < places; j++)
                    if (frac[j] != '0')
                        rest = 0;
                if (c == '0' || !rest)
                    owf_buf_putc(out, frac[fk]);
                else if (c == '?')
                    owf_buf_putc(out, ' ');
                fk++;
            }
            p++;
            continue;
        }
        if (c == '.') {
            if (w.ni == 0 && w.len)
                owf_buf_puts(out, whole);
            owf_buf_putc(out, '.');
            fk = 0;
            w.k = w.ni;                  /* the whole part is done */
            p++;
            continue;
        }
        if (c == ',') {
            p++;
            continue;
        }
        if ((c == 'E' || c == 'e') && in->exp && p + 1 < e && (p[1] == '+' || p[1] == '-')) {
            char num[16];
            int nd = 0, ae = exponent < 0 ? -exponent : exponent, len;
            owf_buf_putc(out, c);
            if (exponent < 0)
                owf_buf_putc(out, '-');
            else if (p[1] == '+')
                owf_buf_putc(out, '+');
            p += 2;
            while (p < e && is_ph((unsigned char)*p)) {
                nd++;
                p++;
            }
            len = 0;
            do {
                num[len++] = (char)('0' + ae % 10);
                ae /= 10;
            } while (ae);
            for (i = len; i < nd; i++)
                owf_buf_putc(out, '0');
            while (len)
                owf_buf_putc(out, num[--len]);
            continue;
        }
        if (c == '%') {
            owf_buf_putc(out, '%');
            p++;
            continue;
        }
        if (e - p >= 7 && word_is(p, p + 7, "general")) {
            char text[40];
            calc_number_text(v, text);
            owf_buf_puts(out, text);
            p += 7;
            continue;
        }
        if (c == '@') {
            p++;
            continue;
        }
        p = put_char(out, p, e);
    }
}

/* "# ?/?", "# ??/??", "?/8": a whole part (when the format has one) and a fraction. */
static void fraction_section(owf_buf *out, const char *s, const char *e, double v)
{
    const char *slash = s, *p;
    long den_max = 0, fixed_den = 0, best_d = 1, d;
    int groups = 0, in_group = 0, den_ph = 0, fixed_digits = 0;
    double best = 2, x, whole = 0, best_n = 0;

    while (slash < e && *slash != '/')
        slash++;
    for (p = s; p < slash; p++) {        /* placeholder groups before the slash */
        if (*p == '"') {
            for (p++; p < slash && *p != '"'; p++)
                ;
            in_group = 0;
            continue;
        }
        if (is_ph((unsigned char)*p)) {
            if (!in_group)
                groups++;
            in_group = 1;
        } else {
            in_group = 0;
        }
    }
    for (p = slash + 1; p < e; p++) {
        if (is_ph((unsigned char)*p))
            den_ph++;
        else if (*p >= '0' && *p <= '9' && !den_ph && (fixed_den || *p != '0') && ++fixed_digits <= 6)
            fixed_den = fixed_den * 10 + (*p - '0');
        else
            break;
    }
    if (den_ph > 4)
        den_ph = 4;                      /* at most 9999: the search below is that long */
    if (groups >= 2 || v >= 1e15) {
        whole = floor(v);
        v -= whole;
    }
    if (fixed_den) {
        best_d = fixed_den;
        best_n = floor(v * (double)fixed_den + 0.5);
    } else {
        for (den_max = 1; den_ph-- > 0;)
            den_max *= 10;
        den_max--;
        if (den_max < 1)
            den_max = 9;
        for (d = 1; d <= den_max; d++) {
            double n = floor(v * (double)d + 0.5);
            x = fabs(v - n / (double)d);
            if (x < best - 1e-12) {
                best = x;
                best_n = n;
                best_d = d;
            }
        }
    }
    if (groups >= 2 && best_n == (double)best_d) {
        whole++;
        best_n = 0;
    }
    if ((groups >= 2 || whole) && (whole || !best_n))
        put_whole(out, whole, 1);
    if (best_n) {
        if ((groups >= 2 || whole) && whole)
            owf_buf_putc(out, ' ');
        put_whole(out, best_n, 1);
        owf_buf_putc(out, '/');
        put_whole(out, (double)best_d, 1);
    } else if (groups < 2 && !whole) {
        owf_buf_puts(out, "0");
    }
}

/* ---- Dates and times through a format ---- */

/* A piece of a date format; small, as there may be 64 of them on the stack. */
typedef struct {
    unsigned char kind;          /* 'y' 'm' 'd' 'h' 'M' (minutes) 's' 'a' (AM/PM) 'H' 'N' 'S' (elapsed) '.' 'l' (literal) */
    unsigned char len;
    unsigned short s, e;         /* from the section's start */
} dtok;

static void date_section(owf_buf *out, const char *s, const char *e, const info *in, double v, int date1904)
{
    dtok t[64];
    int nt = 0, i, fd = 0, year, month, day;
    double units, total, days, rest;
    long secs_of_day, h, mi, sec;
    const char *p = s;

    if (v < 0 || v >= (double)(LAST_SERIAL_1900 + 1 - (date1904 ? 1462 : 0)) || e - s > 4096) {
        owf_buf_puts(out, "########");
        return;
    }
    /* the tokens */
    while (p < e && nt < 64) {
        int c = (unsigned char)*p, lc = owf_lower(c);
        const char *q;
        dtok *k = &t[nt];
        k->s = (unsigned short)(p - s);
        if (c == '"' || c == '\\' || c == '_' || c == '*' || (c == '[' && p + 1 < e && p[1] == '$')) {
            q = p;
            if (c == '"') {
                for (q++; q < e && *q != '"'; q++)
                    ;
                q = q < e ? q + 1 : q;
            } else if (c == '[') {
                while (q < e && *q != ']')
                    q++;
                q = q < e ? q + 1 : q;
            } else {
                q = p + 2 <= e ? p + 2 : e;
            }
            k->kind = 'l';
            k->e = (unsigned short)((q) - s);
            nt++;
            p = q;
            continue;
        }
        if (c == '[') {
            const char *close = p;
            int l;
            while (close < e && *close != ']')
                close++;
            l = owf_lower((unsigned char)(p + 1 < e ? p[1] : 0));
            if (l == 'h' || l == 'm' || l == 's') {
                k->kind = (unsigned char)(l == 'h' ? 'H' : l == 'm' ? 'N' : 'S');
                k->len = (unsigned char)(close - p - 1 > 255 ? 255 : close - p - 1);
                k->e = (unsigned short)((close) - s);
                nt++;
            }
            p = close < e ? close + 1 : close;
            continue;
        }
        if (e - p >= 5 && word_is(p, p + 5, "am/pm")) {
            k->kind = 'a';
            k->len = 5;
            k->e = (unsigned short)((p + 5) - s);
            nt++;
            p += 5;
            continue;
        }
        if (e - p >= 3 && word_is(p, p + 3, "a/p")) {
            k->kind = 'a';
            k->len = 3;
            k->e = (unsigned short)((p + 3) - s);
            nt++;
            p += 3;
            continue;
        }
        if (lc == 'y' || lc == 'm' || lc == 'd' || lc == 'h' || lc == 's') {
            q = p;
            while (q < e && owf_lower((unsigned char)*q) == lc)
                q++;
            k->kind = (unsigned char)lc;
            k->len = (unsigned char)(q - p > 255 ? 255 : q - p);
            k->e = (unsigned short)((q) - s);
            nt++;
            p = q;
            continue;
        }
        if (c == '.' && p + 1 < e && p[1] == '0' && nt && (t[nt - 1].kind == 's' || t[nt - 1].kind == 'S')) {
            q = p + 1;
            while (q < e && *q == '0')
                q++;
            k->kind = '.';
            k->len = (unsigned char)(q - p - 1 > 3 ? 3 : q - p - 1);
            fd = k->len;
            k->e = (unsigned short)((q) - s);
            nt++;
            p = q;
            continue;
        }
        if (e - p >= 7 && word_is(p, p + 7, "general")) {
            p += 7;
            continue;
        }
        q = p + 1;
        while (q < e && ((unsigned char)*q & 0xC0) == 0x80)
            q++;
        k->kind = 'l';
        k->e = (unsigned short)((q) - s);
        nt++;
        p = q;
    }
    /* m after h or before s is minutes */
    for (i = 0; i < nt; i++) {
        int j;
        if (t[i].kind != 'm' || t[i].len > 2)
            continue;
        for (j = i - 1; j >= 0 && (t[j].kind == 'l' || t[j].kind == '.'); j--)
            ;
        if (j >= 0 && (t[j].kind == 'h' || t[j].kind == 'H')) {
            t[i].kind = 'M';
            continue;
        }
        for (j = i + 1; j < nt && (t[j].kind == 'l' || t[j].kind == '.'); j++)
            ;
        if (j < nt && (t[j].kind == 's' || t[j].kind == 'S'))
            t[i].kind = 'M';
    }
    /* rounded to what is shown of the seconds */
    units = 86400;
    for (i = 0; i < fd; i++)
        units *= 10;
    total = floor(v * units + 0.5);
    days = floor(total / units);
    rest = total - days * units;
    secs_of_day = (long)floor(rest / (units / 86400));
    h = secs_of_day / 3600;
    mi = secs_of_day / 60 % 60;
    sec = secs_of_day % 60;
    calc_serial_date((long)days, date1904, &year, &month, &day);
    for (i = 0; i < nt; i++) {
        dtok *k = &t[i];
        switch (k->kind) {
        case 'l':
            if (!literal(out, s + k->s, s + k->e))
                owf_buf_put(out, s + k->s, (size_t)(k->e - k->s));
            break;
        case 'y':
            if (k->len <= 2)
                put_whole(out, year % 100, 2);
            else
                put_whole(out, year, 4);
            break;
        case 'm':
            if (k->len <= 2)
                put_whole(out, month, k->len);
            else if (k->len == 3)
                owf_buf_put(out, month_names[month - 1], 3);
            else if (k->len == 5)
                owf_buf_put(out, month_names[month - 1], 1);
            else
                owf_buf_puts(out, month_names[month - 1]);
            break;
        case 'd':
            if (k->len <= 2) {
                put_whole(out, day, k->len);
            } else {
                long serial = (long)days + (date1904 ? 1462 : 0);
                const char *name = day_names[(serial + 6) % 7];
                if (k->len == 3)
                    owf_buf_put(out, name, 3);
                else
                    owf_buf_puts(out, name);
            }
            break;
        case 'h': {
            long hh = h;
            if (in->ampm) {
                hh = h % 12;
                if (!hh)
                    hh = 12;
            }
            put_whole(out, (double)hh, k->len > 1 ? 2 : 1);
            break;
        }
        case 'M':
            put_whole(out, (double)mi, k->len > 1 ? 2 : 1);
            break;
        case 's':
            put_whole(out, (double)sec, k->len > 1 ? 2 : 1);
            break;
        case 'H':
            put_whole(out, days * 24 + (double)h, k->len > 1 ? 2 : 1);
            break;
        case 'N':
            put_whole(out, (days * 24 + (double)h) * 60 + (double)mi, k->len > 1 ? 2 : 1);
            break;
        case 'S':
            put_whole(out, ((days * 24 + (double)h) * 60 + (double)mi) * 60 + (double)sec, k->len > 1 ? 2 : 1);
            break;
        case '.':
            owf_buf_putc(out, '.');
            put_whole(out, rest - (double)secs_of_day * (units / 86400), k->len);
            break;
        case 'a': {
            int pm = h >= 12;
            if (k->len == 5)
                owf_buf_puts(out, s[k->s] == 'a' || s[k->s] == 'p' ? (pm ? "pm" : "am") : (pm ? "PM" : "AM"));
            else
                owf_buf_putc(out, s[k->s] == 'a' || s[k->s] == 'p' ? (pm ? 'p' : 'a') : (pm ? 'P' : 'A'));
            break;
        }
        }
    }
}

static void text_section(owf_buf *out, const char *s, const char *e, const char *text, size_t len)
{
    const char *p = s;
    while (p < e) {
        const char *q = literal(out, p, e);
        if (q) {
            p = q;
            continue;
        }
        if (*p == '@') {
            owf_buf_put(out, text, len);
            p++;
            continue;
        }
        p = put_char(out, p, e);
    }
}

int calc_format(const char *format, const calc_value *v, int date1904, int width, owf_buf *out,
                unsigned long *colour)
{
    section sec[4];
    const section *use;
    info in;
    digits_t d;
    int n, neg = 0;
    double x;

    if (colour)
        *colour = OWF_COLOUR_AUTO;
    if (!format || !*format)
        format = "General";
    switch (v->type) {
    case CV_EMPTY:
    case CV_MISSING:
        return 0;
    case CV_BOOL:
        owf_buf_puts(out, v->u.num ? "TRUE" : "FALSE");
        return 0;
    case CV_ERR: {
        const char *name = owf_error_name(v->u.err);
        owf_buf_puts(out, name ? name : "#N/A");
        return 0;
    }
    case CV_STR:
        n = sections(format, sec);
        if (n == 4)
            use = &sec[3];
        else {
            analyse(sec[0].s, sec[0].e, &in);
            use = n == 1 && in.text_at ? &sec[0] : NULL;
            if (!use && n > 1) {
                analyse(sec[n - 1].s, sec[n - 1].e, &in);
                if (in.text_at)
                    use = &sec[n - 1];
            }
        }
        if (!use) {
            owf_buf_put(out, v->u.str.p, v->u.str.len);
            return 0;
        }
        if (colour)
            *colour = use->colour;
        text_section(out, use->s, use->e, v->u.str.p, v->u.str.len);
        return 0;
    default:
        break;
    }
    if (v->type != CV_NUM)
        return 0;
    x = v->u.num;
    n = sections(format, sec);
    if (sec[0].has_cond || (n > 1 && sec[1].has_cond)) {
        if (sec[0].has_cond && cond_true(&sec[0], x))
            use = &sec[0];
        else if (n > 1 && sec[1].has_cond && cond_true(&sec[1], x))
            use = &sec[1];
        else if (n > 2)
            use = &sec[2];
        else if (n > 1 && !sec[1].has_cond)
            use = &sec[1];
        else
            use = &sec[0];
        neg = x < 0 && use == &sec[0];
        if (x < 0 && use != &sec[0])
            neg = !(n > 1 && use == &sec[1] && !sec[1].has_cond);
        x = fabs(x);
    } else if (x < 0 && n > 1) {
        use = &sec[1];
        x = -x;
    } else if (x == 0 && n > 2) {
        use = &sec[2];
    } else {
        use = &sec[0];
        if (x < 0) {
            neg = 1;
            x = -x;
        }
    }
    if (colour)
        *colour = use->colour;
    analyse(use->s, use->e, &in);
    if (in.date) {
        if (neg) {
            owf_buf_puts(out, "########");
            return 0;
        }
        date_section(out, use->s, use->e, &in, x, date1904);
        return 0;
    }
    if (in.text_at && !in.int_ph && !in.frac_ph && !in.general) {
        general(out, neg ? -x : x, width, &d);   /* a number in a text format: as General */
        return 0;
    }
    if (in.general && !in.int_ph && !in.frac_ph) {
        /* General, with whatever text is around it */
        const char *p = use->s;
        while (p < use->e) {
            const char *q = literal(out, p, use->e);
            if (q) {
                p = q;
                continue;
            }
            if (use->e - p >= 7 && word_is(p, p + 7, "general")) {
                general(out, neg ? -x : x, width, &d);
                p += 7;
                continue;
            }
            p = put_char(out, p, use->e);
        }
        return 0;
    }
    if (in.slash) {
        if (neg && x != 0)
            owf_buf_putc(out, '-');
        fraction_section(out, use->s, use->e, x);
    } else {
        number_section(out, use->s, use->e, &in, x, neg, &d);
    }
    return 0;
}

/* ---- The public calls ---- */

static int finish(owf_buf *b, char *out, size_t size)
{
    size_t n = b->failed ? 0 : b->length;
    if (!size) {
        owf_buf_free(b);
        return 0;
    }
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)b->data[n] & 0xC0) == 0x80)
            n--;                         /* not in the middle of a character */
    }
    if (n)
        memcpy(out, b->data, n);
    out[n] = 0;
    owf_buf_free(b);
    return (int)n;
}

int owf_calc_format_value(const char *format, const owf_cell *value, int date1904, int width, char *out,
                          size_t size, unsigned long *colour)
{
    owf_buf b;
    calc_value v;
    owf_buf_init(&b);
    calc_value_of_cell(value, &v);
    calc_format(format, &v, date1904, width, &b, colour);
    return finish(&b, out, size);
}

int owf_calc_format(const owf_book *book, const owf_cell *cell, char *out, size_t size)
{
    const char *format = "General";
    if (cell && cell->style >= 0 && cell->style < book->nstyles) {
        int k = book->styles[cell->style].number_format;
        if (k >= 0 && k < book->nnumfmts)
            format = book->numfmts[k];
    }
    return owf_calc_format_value(format, cell, book->date1904, 0, out, size, NULL);
}
