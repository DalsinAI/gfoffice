/*
 * owf.library's own C library calls (owf_libc.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_libc.h"

#include <limits.h>

static int is_space(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

int owf_rt_strcasecmp(const char *a, const char *b)
{
    int ca, cb;
    do {
        ca = lower((unsigned char)*a++);
        cb = lower((unsigned char)*b++);
    } while (ca == cb && ca);
    return ca - cb;
}

int owf_rt_strncasecmp(const char *a, const char *b, size_t n)
{
    int ca = 0, cb = 0;
    while (n--) {
        ca = lower((unsigned char)*a++);
        cb = lower((unsigned char)*b++);
        if (ca != cb || !ca) break;
    }
    return ca - cb;
}

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return 99;
}

/* the digits after the sign; *neg, *over and *any say what was found */
static unsigned long parse(const char *s, char **end, int base, unsigned long limit_pos,
                           unsigned long limit_neg, int *neg, int *over)
{
    const char *p = s;
    unsigned long v = 0, limit;
    int any = 0;
    *neg = 0;
    *over = 0;
    if (end) *end = (char *)s;
    if (base != 0 && (base < 2 || base > 36)) return 0;
    while (is_space((unsigned char)*p)) p++;
    if (*p == '+' || *p == '-') *neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        digit_value((unsigned char)p[2]) < 16) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    limit = *neg ? limit_neg : limit_pos;
    for (;; p++) {
        int d = digit_value((unsigned char)*p);
        if (d >= base) break;
        any = 1;
        if (*over) continue;
        if (v > (limit - (unsigned long)d) / (unsigned long)base) { *over = 1; v = limit; continue; }
        v = v * (unsigned long)base + (unsigned long)d;
    }
    if (!any) return 0;
    if (end) *end = (char *)p;
    return v;
}

long owf_rt_strtol(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long v = parse(s, end, base, (unsigned long)LONG_MAX, (unsigned long)LONG_MAX + 1UL, &neg, &over);
    if (neg) return v == (unsigned long)LONG_MAX + 1UL ? LONG_MIN : -(long)v;
    return (long)v;
}

unsigned long owf_rt_strtoul(const char *s, char **end, int base)
{
    int neg, over;
    unsigned long v = parse(s, end, base, ULONG_MAX, ULONG_MAX, &neg, &over);
    if (over) return ULONG_MAX;
    return neg ? 0UL - v : v;
}

/* ---- snprintf ------------------------------------------------------------ */

struct out {
    char *p;
    size_t size;
    size_t n;                       /* the whole length, even past size */
};

static void put(struct out *o, char c)
{
    if (o->n + 1 < o->size) o->p[o->n] = c;
    o->n++;
}

static void put_padded(struct out *o, const char *s, size_t len, int width, int left, char pad)
{
    size_t i;
    if (!left) for (; width > 0 && (size_t)width > len; width--) put(o, pad);
    for (i = 0; i < len; i++) put(o, s[i]);
    if (left) for (; width > 0 && (size_t)width > len; width--) put(o, ' ');
}

int owf_rt_vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    struct out o;
    o.p = out;
    o.size = size;
    o.n = 0;
    while (*fmt) {
        int left = 0, width = 0, prec = -1, lng = 0;
        char pad = ' ', conv;
        if (*fmt != '%') { put(&o, *fmt++); continue; }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') pad = '0';
            else break;
        }
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; if (width < 0) { left = 1; width = -width; } }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }
        if (*fmt == 'l') { lng = 1; fmt++; }
        conv = *fmt ? *fmt++ : 0;
        if (left) pad = ' ';
        switch (conv) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t len = 0;
            if (!s) s = "(null)";
            while ((prec < 0 || len < (size_t)prec) && s[len]) len++;
            put_padded(&o, s, len, width, left, ' ');
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            put_padded(&o, &c, 1, width, left, ' ');
            break;
        }
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            char buf[24];
            const char *hex = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            unsigned long v;
            unsigned base = (conv == 'x' || conv == 'X') ? 16 : conv == 'o' ? 8 : 10;
            int neg = 0, i = (int)sizeof buf;
            if (conv == 'd' || conv == 'i') {
                long s = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
                neg = s < 0;
                v = neg ? 0UL - (unsigned long)s : (unsigned long)s;
            } else {
                v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
            }
            do { buf[--i] = hex[v % base]; v /= base; } while (v);
            if (neg) {
                if (pad == '0') { put(&o, '-'); if (width) width--; }
                else buf[--i] = '-';
            }
            put_padded(&o, buf + i, sizeof buf - (size_t)i, width, left, pad);
            break;
        }
        case '%':
            put(&o, '%');
            break;
        default:                     /* not one of ours: written as it is */
            put(&o, '%');
            if (conv) put(&o, conv);
            break;
        }
    }
    if (size) out[o.n < size ? o.n : size - 1] = 0;
    return o.n > (size_t)INT_MAX ? INT_MAX : (int)o.n;
}

/* ---- sscanf -------------------------------------------------------------- */

int owf_rt_vsscanf(const char *s, const char *fmt, va_list ap)
{
    int count = 0;
    while (*fmt) {
        char *end;
        int is_long = 0, base;
        unsigned char conv;
        if (is_space((unsigned char)*fmt)) {
            while (is_space((unsigned char)*fmt)) fmt++;
            while (is_space((unsigned char)*s)) s++;
            continue;
        }
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%') fmt++;
            if (*s != *fmt) return count ? count : (*s ? 0 : -1);
            s++;
            fmt++;
            continue;
        }
        fmt++;
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }
        conv = (unsigned char)*fmt++;
        base = conv == 'd' || conv == 'u' ? 10 : conv == 'x' || conv == 'X' ? 16 : conv == 'i' ? 0 : -1;
        if (base < 0) return count;    /* a conversion this one doesn't do */
        while (is_space((unsigned char)*s)) s++;
        if (!*s) return count ? count : -1;
        if (conv == 'd' || conv == 'i') {
            long v = owf_rt_strtol(s, &end, base);
            if (end == s) return count;
            if (is_long) *va_arg(ap, long *) = v;
            else *va_arg(ap, int *) = (int)v;
        } else {
            unsigned long v = owf_rt_strtoul(s, &end, base);
            if (end == s) return count;
            if (is_long) *va_arg(ap, unsigned long *) = v;
            else *va_arg(ap, unsigned *) = (unsigned)v;
        }
        s = end;
        count++;
    }
    return count;
}
