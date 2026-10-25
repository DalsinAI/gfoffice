/*
 * OWFLibsTest: what each library's file has, included by each after owf.h
 * and owf_open.h, so its calls go to that file's library: the checks of the
 * 17 calls all four libraries share (memory, reports, kinds, zip and
 * inflate, the structures' sizes), reading a file into that library's
 * memory, and the file's owf_libs_unit (OWF_LIBS_UNIT).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#ifndef OWF_LIBS_SHARED_H
#define OWF_LIBS_SHARED_H

#include "owf_libs.h"

#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

/* the condition first: the message may show what it worked out */
#define LCHECK(env, ok, ...) do { \
    char what_[160]; \
    int ok_ = (ok); \
    snprintf(what_, sizeof what_, __VA_ARGS__); \
    (env)->check((env)->ctx, ok_, what_); \
} while (0)

static void unit_path(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    snprintf(out, size, "%s%s%s", dir, n && dir[n - 1] != ':' && dir[n - 1] != '/' ? "/" : "", name);
}

/* a whole file through dos.library into this library's memory (owf_free gives it back): safe from either task, as
 * libnix's stdio is not */
static int unit_read_file(const char *path, unsigned char **data, size_t *length)
{
    BPTR fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG n;
    *data = NULL;
    *length = 0;
    if (!fh)
        return 0;
    Seek(fh, 0, OFFSET_END);
    n = Seek(fh, 0, OFFSET_BEGINNING);
    if (n < 0 || !(*data = (unsigned char *)owf_malloc((size_t)n + 1)) || Read(fh, *data, n) != n) {
        Close(fh);
        owf_free(*data);
        *data = NULL;
        return 0;
    }
    Close(fh);
    (*data)[n] = 0;
    *length = (size_t)n;
    return 1;
}

static void unit_delete_file(const char *path)
{
    DeleteFile((CONST_STRPTR)path);
}

static const char *const kind_names[] = { "unknown", "document", "workbook", "deck", "database", "publication" };

static const char *kind_name(owf_kind kind)
{
    return (unsigned)kind < sizeof kind_names / sizeof kind_names[0] ? kind_names[kind] : "?";
}

/* The shared calls through this file's library. text_kind: what it makes of plain text (only owfwrite.library's
 * readers know it: the others say unknown). */
static void shared_checks(const owf_libs_env *e, owf_kind text_kind)
{
    /* owf_inflate on a raw DEFLATE stream made with zlib (level 9): 511 bytes, as owf_lib_checks.c's */
    static const unsigned char packed[] = {
        0xCB, 0x2F, 0x4F, 0xD3, 0xCB, 0xC9, 0x4C, 0x2A, 0x4A, 0x2C, 0xAA, 0x54, 0x30, 0x52, 0xC8, 0xCC,
        0x4B, 0xCB, 0x49, 0x2C, 0x49, 0x2D, 0x56, 0x28, 0xC9, 0xC8, 0x2C, 0x56, 0x28, 0xCF, 0x2C, 0xC9,
        0x50, 0x00, 0x4A, 0xE6, 0x97, 0xA7, 0xA9, 0x17, 0x2B, 0xE4, 0x97, 0xE7, 0xC1, 0xE4, 0x75, 0x20,
        0x52, 0xF9, 0x45, 0x60, 0x3A, 0xBF, 0xB4, 0x44, 0xA1, 0x0A, 0x66, 0x88, 0x1E, 0x50, 0xDD, 0xA8,
        0x89, 0x83, 0xD3, 0x44, 0xF7, 0xA2, 0xC3, 0x7B, 0x0E, 0xCF, 0x4F, 0x55, 0x48, 0x2C, 0x2D, 0x56,
        0xF0, 0x3E, 0xBC, 0x2D, 0x27, 0x4F, 0x8F, 0x0B, 0x00,
    };
    static const char start[] = "owf.library 2 inflates this with libowf's own inflate";
    static const struct { const char *dir, *file; owf_kind kind; } kinds[] = {
        { NULL, "book.xls", OWF_KIND_WORKBOOK }, { NULL, "book-lo.xlsx", OWF_KIND_WORKBOOK },
        { NULL, "book-lo.ods", OWF_KIND_WORKBOOK }, { NULL, "Sample.pub", OWF_KIND_PUBLICATION },
        { NULL, "libreoffice.odp", OWF_KIND_DECK }, { "s", "OpenWrite-Tour.docx", OWF_KIND_DOCUMENT },
        { "s", "OpenWrite-Tour.odt", OWF_KIND_DOCUMENT },
    };
    static const char plain[] = "A letter.\nPlain text, two lines of it.\n";
    char path[256], *p, text[64];
    unsigned char out[511], *data, *part;
    size_t length, n;
    owf_unzip zip;
    owf_report *report;
    owf_note_level level = OWF_NOTE_INFO;
    const char *note;
    int i, r, times = 0;

    LCHECK(e, !strcmp(owf_error_text(OWF_ERR_MEMORY), "not enough memory"), "%s: owf_error_text", e->tag);
    LCHECK(e, owf_error_name(OWF_ERROR_DIV0) && !strcmp(owf_error_name(OWF_ERROR_DIV0), "#DIV/0!"), "%s: owf_error_name",
           e->tag);
    LCHECK(e, owf_struct_size(OWF_STRUCT_DOC) == sizeof(owf_doc) && owf_struct_size(OWF_STRUCT_CELL) == sizeof(owf_cell) &&
           owf_struct_size(OWF_STRUCT_DECK) == sizeof(owf_deck) && owf_struct_size(OWF_STRUCT_COUNT) == 0 &&
           owf_struct_size(-1) == 0, "%s: owf_struct_size (owf_open checked this library's)", e->tag);

    /* memory: the library's pool, given back to it */
    p = (char *)owf_malloc(10);
    if (p) {
        strcpy(p, "abc");
        p = (char *)owf_realloc(p, 5000);
    }
    LCHECK(e, p && !strcmp(p, "abc"), "%s: owf_malloc and owf_realloc", e->tag);
    owf_free(p);
    p = (char *)owf_calloc(100, 3);
    for (i = 0; p && i < 300 && !p[i]; i++)
        ;
    LCHECK(e, p && i == 300, "%s: owf_calloc", e->tag);
    owf_free(p);

    /* a report: owf_report_add is formatted here and handed over as text (owf_proto.h) */
    report = owf_report_new();
    owf_report_add(report, OWF_NOTE_APPROX, "%d of %s", 3, "these");
    owf_report_add(report, OWF_NOTE_APPROX, "%d of %s", 3, "these");
    owf_report_text(report, OWF_NOTE_LOST, "left out");
    note = owf_report_note(report, 0, &level, &times);
    LCHECK(e, report && owf_report_count(report) == 2 && note && !strcmp(note, "3 of these") &&
           level == OWF_NOTE_APPROX && times == 2, "%s: a report's notes, counted", e->tag);
    owf_report_free(report);

    memset(out, 0, sizeof out);
    r = owf_inflate(out, sizeof out, packed, sizeof packed);
    LCHECK(e, r == OWF_OK && !memcmp(out, start, sizeof start - 1) && out[sizeof out - 1] == '\n',
           "%s: owf_inflate, 89 bytes to 511 (result %d)", e->tag, r);
    LCHECK(e, owf_inflate(out, sizeof out - 1, packed, sizeof packed) == OWF_ERR_CORRUPT,
           "%s: owf_inflate refuses a wrong size", e->tag);

    for (i = 0; i < (int)(sizeof kinds / sizeof kinds[0]); i++) {
        owf_kind k;
        unit_path(path, sizeof path, kinds[i].dir ? e->samples : e->fixtures, kinds[i].file);
        if (!unit_read_file(path, &data, &length)) {
            LCHECK(e, 0, "%s: %s read", e->tag, kinds[i].file);
            continue;
        }
        k = owf_file_kind(data, length);
        LCHECK(e, k == kinds[i].kind, "%s: %s is a %s (%s)", e->tag, kinds[i].file, kind_name(kinds[i].kind),
               kind_name(k));
        if (kinds[i].kind == OWF_KIND_WORKBOOK && strstr(kinds[i].file, ".xlsx")) {
            r = owf_unzip_open(&zip, data, length);
            LCHECK(e, r && owf_unzip_has(&zip, "xl/workbook.xml") && !owf_unzip_has(&zip, "word/document.xml"),
                   "%s: %s opened as a zip", e->tag, kinds[i].file);
            r = r ? owf_unzip_read(&zip, "xl/workbook.xml", &part, &n) : -1;
            LCHECK(e, r == OWF_OK && n > 100 && strstr((char *)part, "<workbook"),
                   "%s: its xl/workbook.xml inflated, %lu bytes", e->tag, (unsigned long)(r == OWF_OK ? n : 0));
            if (r == OWF_OK)
                owf_free(part);
        }
        owf_free(data);
    }
    LCHECK(e, owf_file_kind((const unsigned char *)plain, sizeof plain - 1) == text_kind,
           "%s: plain text is %s here", e->tag, kind_name(text_kind));
    snprintf(text, sizeof text, "%s", kind_name(owf_file_kind((const unsigned char *)"\x01\x02\x00\x7F", 4)));
    LCHECK(e, !strcmp(text, "unknown"), "%s: four bytes of nothing: %s", e->tag, text);
}

static int unit_open(void)
{
    return owf_open();
}

static void unit_close(void)
{
    owf_close();
}

static const char *unit_why(void)
{
    return owf_open_sizes_differ ? "its structures are not the sizes this test was built with" : "it is not in LIBS:";
}

static void unit_opened(char *out, size_t size)
{
    const char *id = OWF_LIBRARY_BASE ? (const char *)OWF_LIBRARY_BASE->lib_IdString : NULL;
    size_t n;
    snprintf(out, size, "%s", id ? id : "?");
    n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r'))
        out[--n] = 0;
}

#define OWF_LIBS_UNIT(var, run) \
    const owf_libs_unit var = { OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION, unit_open, unit_close, unit_why, unit_opened, run }

#endif
