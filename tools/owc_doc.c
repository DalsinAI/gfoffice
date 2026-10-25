/*
 * OWConvert's documents, on owfwrite.library (OWF_USE_WRITE_LIBRARY) or
 * with libowf linked in: the document formats, converting between them,
 * and what a file is (owf_file_kind), which OWConvert asks first.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#if defined(OWF_USE_LIBRARY) && !defined(OWF_USE_WRITE_LIBRARY)
#error "owc_doc.c is built for owfwrite.library (OWF_USE_WRITE_LIBRARY)"
#endif
#include "owf.h"
#include "owf_open.h"
#include "owc.h"

#include <stdio.h>
#include <string.h>

static int opened, missing;

int owc_doc_open(void)
{
    if (!opened && !missing) {
        if (owf_open())
            opened = 1;
        else
            missing = owc_missing(OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION, "documents");
    }
    return opened;
}

void owc_doc_close(void)
{
    if (opened)
        owf_close();
    opened = 0;
}

int owc_doc_kind(const unsigned char *data, size_t length)
{
    return owc_doc_open() ? (int)owf_file_kind(data, length) : OWF_KIND_UNKNOWN;
}

int owc_doc_writes(const char *to, const char *format)
{
    const owf_format *f;
    if (!owc_doc_open())
        return 0;
    f = format ? owf_format_named(format) : owf_format_for_filename(to);
    return f && f->export;
}

static void print_report(const owf_report *report)
{
    int i;
    for (i = 0; i < owf_report_count(report); i++) {
        owf_note_level level;
        int times;
        const char *text = owf_report_note(report, i, &level, &times);
        if (times > 1)
            printf("  %s: %s (%d times)\n", owc_level_name(level), text, times);
        else
            printf("  %s: %s\n", owc_level_name(level), text);
    }
}

int owc_doc_convert(const char *from, const char *to, const char *format, int show_report)
{
    owf_report *report;
    owf_doc *doc = NULL;
    const owf_format *used = NULL;
    int result, rc = RC_OK;

    if (!owc_doc_open())
        return RC_FAIL;
    report = owf_report_new();
    if (!report) {
        printf("OWConvert: %s\n", owf_error_text(OWF_ERR_MEMORY));
        return RC_FAIL;
    }
    result = owf_import_file(from, NULL, &doc, report, &used);
    if (result != OWF_OK) {
        printf("OWConvert: %s: %s\n", from, owf_error_text(result));
        if (owf_report_count(report))
            print_report(report);
        owf_report_free(report);
        return result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    }
    result = owf_export_file(doc, to, format, report);
    if (result != OWF_OK) {
        if (result == OWF_ERR_FORMAT)
            printf("OWConvert: %s: give FORMAT, or a name ending in a format's extension (see FORMATS)\n", to);
        else
            printf("OWConvert: %s: %s\n", to, owf_error_text(result));
        rc = result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    } else {
        printf("%s (%s) -> %s, %d paragraph(s)\n", from, used->description, to, doc->body.nparas);
    }
    if (show_report && owf_report_count(report))
        print_report(report);
    owf_doc_free(doc);
    owf_report_free(report);
    return rc;
}

void owc_doc_formats(void)
{
    int i;
    printf("%-11s %-5s %-5s %s\n", "Format", "Open", "Save", "What");
    if (!owc_doc_open())
        return;
    for (i = 0; i < owf_format_count(); i++) {
        const owf_format *f = owf_format_at(i);
        printf("%-11s %-5s %-5s %s (%s)\n", f->name, f->import ? "yes" : "", f->export ? "yes" : "",
               f->description, f->extensions);
    }
}
