/*
 * libowf 1.12.1: how a kind of note is numbered (owf_page.note_format, note_start), shared by libowf and
 * GFOpenWrite's layout. Header-local, as owf_pagenum_impl.h is, so a program on owfwrite.library gets no new
 * library symbol.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OWF_NOTENUM_IMPL_H
#define OWF_NOTENUM_IMPL_H

#include <stdio.h>
#include <string.h>
#include "owf.h"

/* lower or upper case roman, 1 to 3999; others in digits */
static void owf_note_roman_impl(int n, int lower, char *out, size_t size)
{
    static const int value[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
    static const char *const upper[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
    size_t used = 0;
    int i;
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

static int owf_note_kind_format_impl(const owf_doc *doc, int endnote)
{
    int f = doc ? doc->page.note_format[endnote ? 1 : 0] : 0;
    if (f <= OWF_NOTE_FORMAT_DEFAULT || f >= OWF_NOTE_FORMAT_COUNT)
        f = endnote ? OWF_NOTE_FORMAT_LOWER_ROMAN : OWF_NOTE_FORMAT_ARABIC;
    return f;
}

static int owf_note_kind_start_impl(const owf_doc *doc, int endnote)
{
    int s = doc ? doc->page.note_start[endnote ? 1 : 0] : 0;
    return s >= 1 ? s : 1;
}

/* 1.12.3: where a kind's numbers start again (owf_note_restart); a page only for footnotes */
static int owf_note_kind_restart_impl(const owf_doc *doc, int endnote)
{
    int r = doc ? doc->page.note_restart[endnote ? 1 : 0] : 0;
    if (r == OWF_NOTE_RESTART_PAGE && endnote)
        r = OWF_NOTE_RESTART_CONTINUOUS;
    return r > OWF_NOTE_RESTART_CONTINUOUS && r < OWF_NOTE_RESTART_COUNT ? r : OWF_NOTE_RESTART_CONTINUOUS;
}

/* 1.12.3: at a body paragraph, the kinds' counts that start again there (a section's start, para > 0) */
static void owf_note_section_restart_impl(const owf_doc *doc, const owf_story *story, int para, int count[2])
{
    int k;
    if (!doc || story != &doc->body || para <= 0 || para >= story->nparas || !story->paras[para].section_columns)
        return;
    for (k = 0; k < 2; ++k)
        if (owf_note_kind_restart_impl(doc, k) == OWF_NOTE_RESTART_SECTION)
            count[k] = 0;
}

static void owf_note_format_impl(int format, int endnote, int number, char *out, size_t size)
{
    static const char *const symbols[] = { "*", "\xe2\x80\xa0", "\xe2\x80\xa1", "\xc2\xa7" };
    size_t at = 0;
    int times;
    if (!out || !size)
        return;
    out[0] = 0;
    if (format <= OWF_NOTE_FORMAT_DEFAULT || format >= OWF_NOTE_FORMAT_COUNT)
        format = endnote ? OWF_NOTE_FORMAT_LOWER_ROMAN : OWF_NOTE_FORMAT_ARABIC;
    if (number < 1 && format != OWF_NOTE_FORMAT_ARABIC)
        format = OWF_NOTE_FORMAT_ARABIC;
    switch (format) {
    case OWF_NOTE_FORMAT_LOWER_ROMAN:
    case OWF_NOTE_FORMAT_UPPER_ROMAN:
        owf_note_roman_impl(number, format == OWF_NOTE_FORMAT_LOWER_ROMAN, out, size);
        return;
    case OWF_NOTE_FORMAT_LOWER_LETTER:
    case OWF_NOTE_FORMAT_UPPER_LETTER:
        /* a to z, then aa to zz, and so on: the letter (number - 1) % 26, (number - 1) / 26 + 1 times */
        times = (number - 1) / 26 + 1;
        while (times-- > 0 && at + 1 < size)
            out[at++] = (char)((format == OWF_NOTE_FORMAT_LOWER_LETTER ? 'a' : 'A') + (number - 1) % 26);
        out[at] = 0;
        return;
    case OWF_NOTE_FORMAT_SYMBOL:
        times = (number - 1) / 4 + 1;
        while (times-- > 0) {
            const char *sym = symbols[(number - 1) % 4];
            size_t len = strlen(sym);
            if (at + len >= size) break;
            memcpy(out + at, sym, len + 1);
            at += len;
        }
        return;
    default:
        snprintf(out, size, "%d", number);
        return;
    }
}

#endif
