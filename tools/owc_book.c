/*
 * OWConvert's workbooks, on owfcalc.library (OWF_USE_CALC_LIBRARY) or with
 * libowf linked in: the workbook formats and converting between them.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#if defined(OWF_USE_LIBRARY) && !defined(OWF_USE_CALC_LIBRARY)
#error "owc_book.c is built for owfcalc.library (OWF_USE_CALC_LIBRARY)"
#endif
#include "owf.h"
#include "owf_open.h"
#include "owc.h"

#include <stdio.h>
#include <string.h>

static int opened, missing;

int owc_book_open(void)
{
    if (!opened && !missing) {
        if (owf_open())
            opened = 1;
        else
            missing = owc_missing(OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION, "workbooks");
    }
    return opened;
}

void owc_book_close(void)
{
    if (opened)
        owf_close();
    opened = 0;
}

int owc_book_writes(const char *to, const char *format)
{
    if (!owc_book_open())
        return 0;
    return format ? owf_book_format_named(format) != NULL : owf_book_format_for_filename(to) != NULL;
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

int owc_book_convert(const char *from, const char *to, const char *format, int show_report)
{
    owf_report *report;
    owf_book *book = NULL;
    const owf_book_format *used = NULL;
    int result, rc = RC_OK;

    if (!owc_book_open())
        return RC_FAIL;
    report = owf_report_new();
    if (!report) {
        printf("OWConvert: %s\n", owf_error_text(OWF_ERR_MEMORY));
        return RC_FAIL;
    }
    result = owf_import_book_file(from, NULL, &book, report, &used);
    if (result != OWF_OK) {
        printf("OWConvert: %s: %s\n", from, owf_error_text(result));
        if (owf_report_count(report))
            print_report(report);
        owf_report_free(report);
        return result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    }
    result = owf_export_book_file(book, to, format, report);
    if (result != OWF_OK) {
        printf("OWConvert: %s: %s\n", to, owf_error_text(result));
        rc = result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    } else {
        printf("%s (%s) -> %s, %d sheet(s)\n", from, used->description, to, book->nsheets);
    }
    if (show_report && owf_report_count(report))
        print_report(report);
    owf_book_free(book);
    owf_report_free(report);
    return rc;
}

void owc_book_formats(void)
{
    int i;
    printf("\nWorkbooks\n");
    if (!owc_book_open())
        return;
    for (i = 0; i < owf_book_format_count(); i++) {
        const owf_book_format *f = owf_book_format_at(i);
        printf("%-11s %-5s %-5s %s (%s)\n", f->name, f->import ? "yes" : "", f->export ? "yes" : "",
               f->description, f->extensions);
    }
}
