/*
 * libowf: what a file is (owf_file_kind), so that each of the suite's
 * programs can hand a file to the one it belongs to
 * (docs/gfoffice/Design-Suite-Shared.md, 2.1), and the sizes of the
 * structures programs share with owf.library (owf_struct_size).
 *
 * It reads the workbook and Publisher code's detectors, so a program that
 * links libowf in without them (OpenWrite's build-os3.sh list) leaves this
 * file out too.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"
#include "owf_pub.h"
#include "owf_zip.h"

#include <string.h>

static owf_kind package_kind(int kind)
{
    switch (kind) {
    case OWF_PACKAGE_TEXT:
        return OWF_KIND_DOCUMENT;
    case OWF_PACKAGE_SHEET:
    case OWF_PACKAGE_SHEET_MACROS:
    case OWF_PACKAGE_SHEET_BINARY:
        return OWF_KIND_WORKBOOK;
    case OWF_PACKAGE_SLIDES:
        return OWF_KIND_DECK;
    default:
        return OWF_KIND_UNKNOWN;
    }
}

/* a flat ODF file (FODT, FODS, FODP) says what it is near its start */
static owf_kind flat_odf_kind(const unsigned char *data, size_t length)
{
    static const char key[] = "office:mimetype=\"application/vnd.oasis.opendocument.";
    size_t n = length < 4096 ? length : 4096, k = sizeof key - 1, i;
    for (i = 0; i + k <= n; i++) {
        if (data[i] == 'o' && !memcmp(data + i, key, k)) {
            const char *t = (const char *)data + i + k;
            size_t left = n - i - k;
            if (left >= 4 && !memcmp(t, "text", 4))
                return OWF_KIND_DOCUMENT;
            if (left >= 11 && !memcmp(t, "spreadsheet", 11))
                return OWF_KIND_WORKBOOK;
            if (left >= 12 && !memcmp(t, "presentation", 12))
                return OWF_KIND_DECK;
            return OWF_KIND_UNKNOWN;
        }
    }
    return OWF_KIND_UNKNOWN;
}

owf_kind owf_file_kind(const unsigned char *data, size_t length)
{
    int i, best_doc = 0, best_book = 0;
    owf_kind kind;

    if (!data || !length)
        return OWF_KIND_UNKNOWN;
    /* OpenBase: SQLite's header, and PRAGMA application_id 0x4F414442 at 68 */
    if (length >= 100 && !memcmp(data, "SQLite format 3", 16))
        return OWF_BE32(data + 68) == 0x4F414442UL ? OWF_KIND_DATABASE : OWF_KIND_UNKNOWN;
    if (length >= 4 && data[0] == 'P' && data[1] == 'K') {
        kind = package_kind(owf_odf_kind(data, length));
        if (kind == OWF_KIND_UNKNOWN)
            kind = package_kind(owf_ooxml_kind(data, length));
        return kind;
    }
    if (owf_cfb_detect(data, length)) {
        owf_cfb cfb;
        if (owf_pub_detect(data, length) > 0)
            return OWF_KIND_PUBLICATION;
        if (owf_cfb_open(&cfb, data, length) != OWF_OK)
            return OWF_KIND_UNKNOWN;
        kind = OWF_KIND_UNKNOWN;
        if (owf_cfb_has(&cfb, "Workbook") || owf_cfb_has(&cfb, "Book"))
            kind = OWF_KIND_WORKBOOK;
        else if (owf_cfb_has(&cfb, "WordDocument"))
            kind = OWF_KIND_DOCUMENT;
        else if (owf_cfb_has(&cfb, "PowerPoint Document"))
            kind = OWF_KIND_DECK;
        owf_cfb_close(&cfb);
        return kind;
    }
    kind = flat_odf_kind(data, length);
    if (kind != OWF_KIND_UNKNOWN)
        return kind;
    /* the rest by the readers' own scores; CSV is left to the text reader */
    for (i = 0; i < owf_format_count(); i++) {
        const owf_format *f = owf_format_at(i);
        int score;
        if (f->detect && f->import && (score = f->detect(data, length)) > best_doc)
            best_doc = score;
    }
    for (i = 0; i < owf_book_format_count(); i++) {
        const owf_book_format *f = owf_book_format_at(i);
        int score;
        if (f->detect && f->import && strcmp(f->name, "csv") && (score = f->detect(data, length)) > best_book)
            best_book = score;
    }
    if (!best_doc && !best_book)
        return OWF_KIND_UNKNOWN;
    return best_book > best_doc ? OWF_KIND_WORKBOOK : OWF_KIND_DOCUMENT;
}

unsigned long owf_struct_size(int which)
{
    static const unsigned short sizes[OWF_STRUCT_COUNT] = { OWF_STRUCT_SIZES };
    return which >= 0 && which < OWF_STRUCT_COUNT ? sizes[which] : 0;
}
