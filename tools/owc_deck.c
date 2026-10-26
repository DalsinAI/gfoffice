/*
 * OWConvert's presentations, on owfpresent.library (OWF_USE_PRESENT_LIBRARY)
 * or with libowf linked in: to ODP or flat ODP, and to PDF, a page a slide,
 * laid out by OpenLayout (OpenPresentation's core, src/deck, built in when
 * OWC_DECK_PDF is defined, for the same library as this file).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#if defined(OWF_USE_LIBRARY) && !defined(OWF_USE_PRESENT_LIBRARY)
#error "owc_deck.c is built for owfpresent.library (OWF_USE_PRESENT_LIBRARY)"
#endif
#include "owf.h"
#include "owf_open.h"
#include "owf_deck.h"
#include "owc.h"
#ifdef OWC_DECK_PDF
#include "openpresentation_core.h"
#endif

#include <stdio.h>
#include <string.h>

static int opened, missing;

int owc_deck_open(void)
{
    if (!opened && !missing) {
        if (owf_open())
            opened = 1;
        else
            missing = owc_missing(OWF_LIBRARY_NAME, OWF_LIBRARY_VERSION, "presentations");
    }
    return opened;
}

void owc_deck_close(void)
{
    if (opened)
        owf_close();
    opened = 0;
}

int owc_deck_names(const char *format)
{
    return owc_deck_open() && owf_deck_format_named(format) != NULL;
}

int owc_deck_reads(const unsigned char *data, size_t length)
{
    return owc_deck_open() && owf_deck_format_detect(data, length) != NULL;
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

int owc_deck_convert(const unsigned char *data, size_t n, const char *from, const char *to, const char *format,
                     int show_report)
{
    owf_report *report;
    owf_deck *deck = NULL;
    const owf_deck_format *used = NULL;
    int result, rc = RC_OK, to_pdf = format ? !strcmp(format, "pdf") : owc_ends_with(to, ".pdf");

    if (!owc_deck_open())
        return RC_FAIL;
    report = owf_report_new();
    if (!report) {
        printf("OWConvert: %s\n", owf_error_text(OWF_ERR_MEMORY));
        return RC_FAIL;
    }
    result = owf_import_deck_memory(data, n, NULL, &deck, report, &used);
    if (result != OWF_OK) {
        printf("OWConvert: %s: %s\n", from, owf_error_text(result));
        if (owf_report_count(report))
            print_report(report);
        owf_report_free(report);
        return result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    }
    if (to_pdf) {
#ifdef OWC_DECK_PDF
        unsigned char *pdf;
        size_t length;
        result = opr_export_pdf(deck, NULL, &pdf, &length, report);
        if (result == OWF_OK) {
            FILE *out = fopen(to, "wb");
            if (!out || fwrite(pdf, 1, length, out) != length)
                result = OWF_ERR_IO;
            if (out && fclose(out) != 0)
                result = OWF_ERR_IO;
            owf_free(pdf);              /* the page PDF writer's, from the library's memory */
        }
#else
        printf("OWConvert: this OWConvert was built without OpenLayout, which presentations' PDFs need\n");
        result = OWF_ERR_UNSUPPORTED;
#endif
    } else {
        result = owf_export_deck_file(deck, to, format, report);
    }
    if (result != OWF_OK) {
        if (result == OWF_ERR_FORMAT)
            printf("OWConvert: %s: a presentation converts to odp, otp, fodp or pdf\n", to);
        else
            printf("OWConvert: %s: %s\n", to, owf_error_text(result));
        rc = result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    } else {
        printf("%s (%s) -> %s, %d slide(s)\n", from, used->description, to, deck->nslides);
    }
    if (show_report && owf_report_count(report))
        print_report(report);
    owf_deck_free(deck);
    owf_report_free(report);
    return rc;
}

void owc_deck_formats(void)
{
    int i;
    printf("\nPresentations\n");
    if (!owc_deck_open())
        return;
    for (i = 0; i < owf_deck_format_count(); i++) {
        const owf_deck_format *f = owf_deck_format_at(i);
        printf("%-11s %-5s %-5s %s (%s)\n", f->name, f->import ? "yes" : "", f->export ? "yes" : "",
               f->description, f->extensions);
    }
#ifdef OWC_DECK_PDF
    printf("%-11s %-5s %-5s %s (%s)\n", "pdf", "", "yes", "Portable Document Format, a page a slide", "pdf");
#endif
}
