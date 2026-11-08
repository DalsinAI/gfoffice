/*
 * One implementation of a document page label, shared by libowf's exported
 * call and GFOpenLayout's compatibility wrapper. Header-local so a consumer
 * of legacy owf.library 2 does not acquire a new unresolved library symbol.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_PAGENUM_IMPL_H
#define OWF_PAGENUM_IMPL_H

#include <stdio.h>
#include "owf.h"

static void owf_page_roman_impl(int n, int lower, char *out, size_t size)
{
    static const int value[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const upper[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    size_t used = 0;
    int i;
    if (!size) return;
    out[0] = 0;
    if (n <= 0 || n >= 4000) {
        snprintf(out, size, "%d", n);
        return;
    }
    for (i = 0; i < 13; ++i)
        while (n >= value[i]) {
            const char *s = upper[i];
            for (; *s && used + 1 < size; ++s)
                out[used++] = lower ? (char)(*s - 'A' + 'a') : *s;
            n -= value[i];
        }
    out[used] = 0;
}

static void owf_page_number_impl(const owf_page *page, int number, char *out, size_t size)
{
    owf_pagenum_style style = page ? page->pagenum_style : OWF_PAGENUM_ARABIC;
    if (!out || !size) return;
    switch (style) {
    case OWF_PAGENUM_ROMAN_UPPER: owf_page_roman_impl(number, 0, out, size); break;
    case OWF_PAGENUM_ROMAN_LOWER: owf_page_roman_impl(number, 1, out, size); break;
    case OWF_PAGENUM_LETTER_UPPER:
    case OWF_PAGENUM_LETTER_LOWER:
        if (number > 0) {
            char c = (char)((style == OWF_PAGENUM_LETTER_UPPER ? 'A' : 'a') + (number - 1) % 26);
            size_t times = (size_t)(number - 1) / 26 + 1, i;
            if (times >= size) times = size - 1;
            for (i = 0; i < times; ++i) out[i] = c;
            out[times] = 0;
            break;
        }
        /* fall through */
    default:
        snprintf(out, size, "%d", number);
        break;
    }
}

#endif
