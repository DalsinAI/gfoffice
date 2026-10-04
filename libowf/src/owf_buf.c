/*
 * libowf: growing buffers, UTF-8 and the character sets.
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void owf_buf_init(owf_buf *buf)
{
    buf->data = NULL;
    buf->length = buf->capacity = 0;
    buf->failed = 0;
}

void owf_buf_free(owf_buf *buf)
{
    free(buf->data);
    owf_buf_init(buf);
}

static int grow(owf_buf *buf, size_t extra)
{
    size_t want;
    unsigned char *data;

    if (buf->failed)
        return 0;
    if (buf->length + extra + 1 <= buf->capacity)
        return 1;
    want = buf->capacity ? buf->capacity : 256;
    while (want < buf->length + extra + 1) {
        if (want > ((size_t)-1) / 2) {
            buf->failed = 1;
            return 0;
        }
        want *= 2;
    }
    data = realloc(buf->data, want);
    if (!data) {
        buf->failed = 1;
        return 0;
    }
    buf->data = data;
    buf->capacity = want;
    return 1;
}

void owf_buf_put(owf_buf *buf, const void *data, size_t length)
{
    if (!grow(buf, length))
        return;
    if (length)
        memcpy(buf->data + buf->length, data, length);
    buf->length += length;
    buf->data[buf->length] = 0;
}

void owf_buf_putc(owf_buf *buf, int c)
{
    unsigned char byte = (unsigned char)c;
    owf_buf_put(buf, &byte, 1);
}

void owf_buf_puts(owf_buf *buf, const char *s)
{
    owf_buf_put(buf, s, strlen(s));
}

void owf_buf_printf(owf_buf *buf, const char *fmt, ...)
{
    char small[256];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(small, sizeof small, fmt, args);
    va_end(args);
    if (n < 0) {
        buf->failed = 1;
        return;
    }
    if ((size_t)n < sizeof small) {
        owf_buf_put(buf, small, (size_t)n);
        return;
    }
    if (!grow(buf, (size_t)n))
        return;
    va_start(args, fmt);
    vsnprintf((char *)buf->data + buf->length, (size_t)n + 1, fmt, args);
    va_end(args);
    buf->length += (size_t)n;
}

void owf_buf_put_xml(owf_buf *buf, const char *utf8, size_t length)
{
    size_t i, start = 0;

    for (i = 0; i < length; i++) {
        const char *with = NULL;
        switch (utf8[i]) {
        case '&': with = "&amp;"; break;
        case '<': with = "&lt;"; break;
        case '>': with = "&gt;"; break;
        case '"': with = "&quot;"; break;
        default: break;
        }
        if (with) {
            owf_buf_put(buf, utf8 + start, i - start);
            owf_buf_puts(buf, with);
            start = i + 1;
        }
    }
    owf_buf_put(buf, utf8 + start, length - start);
}

void owf_buf_puts_xml(owf_buf *buf, const char *utf8)
{
    owf_buf_put_xml(buf, utf8, strlen(utf8));
}

int owf_buf_take(owf_buf *buf, unsigned char **data, size_t *length)
{
    if (buf->failed) {
        owf_buf_free(buf);
        return OWF_ERR_MEMORY;
    }
    if (!buf->data)
        owf_buf_put(buf, "", 0);
    if (buf->failed) {
        owf_buf_free(buf);
        return OWF_ERR_MEMORY;
    }
    *data = buf->data;
    *length = buf->length;
    owf_buf_init(buf);
    return OWF_OK;
}

void owf_buf_put_utf8(owf_buf *buf, unsigned long c)
{
    unsigned char out[4];

    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
        c = 0xFFFD;
    if (c < 0x80) {
        out[0] = (unsigned char)c;
        owf_buf_put(buf, out, 1);
    } else if (c < 0x800) {
        out[0] = (unsigned char)(0xC0 | (c >> 6));
        out[1] = (unsigned char)(0x80 | (c & 0x3F));
        owf_buf_put(buf, out, 2);
    } else if (c < 0x10000) {
        out[0] = (unsigned char)(0xE0 | (c >> 12));
        out[1] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
        out[2] = (unsigned char)(0x80 | (c & 0x3F));
        owf_buf_put(buf, out, 3);
    } else {
        out[0] = (unsigned char)(0xF0 | (c >> 18));
        out[1] = (unsigned char)(0x80 | ((c >> 12) & 0x3F));
        out[2] = (unsigned char)(0x80 | ((c >> 6) & 0x3F));
        out[3] = (unsigned char)(0x80 | (c & 0x3F));
        owf_buf_put(buf, out, 4);
    }
}

void owf_buf_put_latin1(owf_buf *buf, const unsigned char *text, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++)
        owf_buf_put_utf8(buf, text[i]);
}

/* Windows-1252's 0x80 to 0x9F; 0 where it has no character. */
static const unsigned short cp1252_high[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

void owf_buf_put_cp1252(owf_buf *buf, const unsigned char *text, size_t length)
{
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned c = text[i];
        if (c >= 0x80 && c < 0xA0)
            owf_buf_put_utf8(buf, cp1252_high[c - 0x80] ? cp1252_high[c - 0x80] : 0xFFFD);
        else
            owf_buf_put_utf8(buf, c);
    }
}

unsigned long owf_utf8_next(const unsigned char **p, const unsigned char *end)
{
    const unsigned char *s = *p;
    unsigned long c;
    int extra, i;

    if (s >= end)
        return 0;
    c = *s++;
    if (c < 0x80) {
        *p = s;
        return c;
    }
    if (c >= 0xC2 && c <= 0xDF) {
        extra = 1;
        c &= 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
        extra = 2;
        c &= 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        extra = 3;
        c &= 0x07;
    } else {
        *p = s;
        return 0xFFFD;
    }
    for (i = 0; i < extra; i++) {
        if (s >= end || (*s & 0xC0) != 0x80) {
            *p = s;
            return 0xFFFD;
        }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *p = s;
    if ((extra == 2 && c < 0x800) || (extra == 3 && (c < 0x10000 || c > 0x10FFFF)) ||
        (c >= 0xD800 && c <= 0xDFFF))
        return 0xFFFD;
    return c;
}

int owf_utf8_valid(const unsigned char *text, size_t length)
{
    const unsigned char *p = text, *end = text + length;

    while (p < end) {
        const unsigned char *before = p;
        unsigned long c = owf_utf8_next(&p, end);
        /* A real U+FFFD is EF BF BD; anything else that decoded to it was bad. */
        if (c == 0xFFFD && !(p - before == 3 && before[0] == 0xEF && before[1] == 0xBF && before[2] == 0xBD))
            return 0;
    }
    return 1;
}

void owf_buf_trim_from(owf_buf *buf, size_t start)
{
    size_t n = start;
    if (buf->failed || start > buf->length)
        return;
    while (n < buf->length && (buf->data[n] == ' ' || buf->data[n] == '\t'))
        n++;
    memmove(buf->data + start, buf->data + n, buf->length - n);
    buf->length -= n - start;
    buf->data[buf->length] = 0;
}
