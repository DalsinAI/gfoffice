/*
 * OWConvert: converts documents with OpenWrite's filters, from a Shell.
 *
 *   OWConvert FROM,TO,FORMAT/K,REPORT/S,FORMATS/S
 *   1> OWConvert Work:Letters/Bank.pw RAM:Bank.odt
 *   1> OWConvert Story.ftxt Story.html REPORT
 *   1> OWConvert FORMATS
 *
 * On other systems: owconvert [--format NAME] [--report] FROM TO, or
 * owconvert --formats.
 *
 * Workbooks too, when TO (or FORMAT) is a workbook format:
 *   1> OWConvert Accounts.xls RAM:Accounts.csv
 * (with libowf linked in; owf.library gets the workbook calls next).
 *
 * Presentations, when FROM is one (ODP, flat ODP): to ODP or flat ODP,
 * and to PDF, a page a slide, laid out by OpenLayout (OpenPresentation's
 * core, src/deck; built in when OWC_DECK_PDF is defined):
 *   1> OWConvert Talk.odp RAM:Talk.pdf
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_open.h"
#ifndef OWF_USE_LIBRARY
#include "owf_deck.h"
#endif
#ifdef OWC_DECK_PDF
#include "openpresentation_core.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __amigaos__
#include <dos/dos.h>
#include <proto/dos.h>
#define RC_OK RETURN_OK
#define RC_ERROR RETURN_ERROR
#define RC_FAIL RETURN_FAIL
static const char version[] __attribute__((used)) = "$VER: OWConvert " OWF_VERSION " (7.10.2026) AmigaChrome Team";
#else
#define RC_OK 0
#define RC_ERROR 10
#define RC_FAIL 20
#endif

static void list_formats(void)
{
    int i;
    printf("%-11s %-5s %-5s %s\n", "Format", "Open", "Save", "What");
    for (i = 0; i < owf_format_count(); i++) {
        const owf_format *f = owf_format_at(i);
        printf("%-11s %-5s %-5s %s (%s)\n", f->name, f->import ? "yes" : "", f->export ? "yes" : "",
               f->description, f->extensions);
    }
#ifndef OWF_USE_LIBRARY
    printf("\nWorkbooks\n");
    for (i = 0; i < owf_book_format_count(); i++) {
        const owf_book_format *f = owf_book_format_at(i);
        printf("%-11s %-5s %-5s %s (%s)\n", f->name, f->import ? "yes" : "", f->export ? "yes" : "",
               f->description, f->extensions);
    }
    printf("\nPresentations\n");
    for (i = 0; i < owf_deck_format_count(); i++) {
        const owf_deck_format *f = owf_deck_format_at(i);
        printf("%-11s %-5s %-5s %s (%s)\n", f->name, f->import ? "yes" : "", f->export ? "yes" : "",
               f->description, f->extensions);
    }
#ifdef OWC_DECK_PDF
    printf("%-11s %-5s %-5s %s (%s)\n", "pdf", "", "yes", "Portable Document Format, a page a slide", "pdf");
#endif
#endif
}

static void print_report(const owf_report *report)
{
    static const char *levels[] = { "Note", "Approximated", "Left out" };
    int i;
    for (i = 0; i < owf_report_count(report); i++) {
        owf_note_level level;
        int times;
        const char *text = owf_report_note(report, i, &level, &times);
        if (times > 1)
            printf("  %s: %s (%d times)\n", levels[level], text, times);
        else
            printf("  %s: %s\n", levels[level], text);
    }
}

#ifndef OWF_USE_LIBRARY
static int convert_book(const char *from, const char *to, const char *format, int show_report)
{
    owf_report *report = owf_report_new();
    owf_book *book = NULL;
    const owf_book_format *used = NULL;
    int result, rc = RC_OK;

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
#endif

#ifndef OWF_USE_LIBRARY
static int ends_with(const char *s, const char *tail)
{
    size_t a = strlen(s), b = strlen(tail), i;
    if (a < b)
        return 0;
    for (i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != tail[i])
            return 0;
    }
    return 1;
}

/* The whole file, from malloc; NULL when it can't be read. */
static unsigned char *read_all(const char *path, size_t *length)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data = NULL;
    size_t cap = 0, n = 0;
    if (!f)
        return NULL;
    for (;;) {
        size_t got;
        if (n == cap) {
            unsigned char *p;
            cap = cap ? cap * 2 : 65536;
            if (cap > 256UL * 1024 * 1024 || !(p = realloc(data, cap))) {
                free(data);
                fclose(f);
                return NULL;
            }
            data = p;
        }
        got = fread(data + n, 1, cap - n, f);
        n += got;
        if (got == 0)
            break;
    }
    fclose(f);
    *length = n;
    return data;
}

/* A presentation: 1 when FROM is one (and it was converted, or failed). */
static int convert_deck(const char *from, const char *to, const char *format, int show_report, int *rc)
{
    owf_report *report;
    owf_deck *deck = NULL;
    const owf_deck_format *used = NULL;
    unsigned char *data;
    size_t n = 0;
    int result, to_pdf = format ? !strcmp(format, "pdf") : ends_with(to, ".pdf");

    data = read_all(from, &n);
    if (!data)
        return 0;
    if (!(format && owf_deck_format_named(format)) && !owf_deck_format_detect(data, n)) {
        free(data);
        return 0;
    }
    *rc = RC_OK;
    report = owf_report_new();
    if (!report) {
        printf("OWConvert: %s\n", owf_error_text(OWF_ERR_MEMORY));
        free(data);
        *rc = RC_FAIL;
        return 1;
    }
    result = owf_import_deck_memory(data, n, NULL, &deck, report, &used);
    free(data);
    if (result != OWF_OK) {
        printf("OWConvert: %s: %s\n", from, owf_error_text(result));
        if (owf_report_count(report))
            print_report(report);
        owf_report_free(report);
        *rc = result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
        return 1;
    }
    if (to_pdf) {
#ifdef OWC_DECK_PDF
        unsigned char *data;
        size_t length;
        result = opr_export_pdf(deck, NULL, &data, &length, report);
        if (result == OWF_OK) {
            FILE *out = fopen(to, "wb");
            if (!out || fwrite(data, 1, length, out) != length)
                result = OWF_ERR_IO;
            if (out && fclose(out) != 0)
                result = OWF_ERR_IO;
            free(data);
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
        *rc = result == OWF_ERR_MEMORY ? RC_FAIL : RC_ERROR;
    } else {
        printf("%s (%s) -> %s, %d slide(s)\n", from, used->description, to, deck->nslides);
    }
    if (show_report && owf_report_count(report))
        print_report(report);
    owf_deck_free(deck);
    owf_report_free(report);
    return 1;
}
#endif

static int convert(const char *from, const char *to, const char *format, int show_report)
{
    owf_report *report = owf_report_new();
    owf_doc *doc = NULL;
    const owf_format *used = NULL;
    int result, rc = RC_OK;

#ifndef OWF_USE_LIBRARY
    {
        int deck_rc;
        if (convert_deck(from, to, format, show_report, &deck_rc)) {
            owf_report_free(report);
            return deck_rc;
        }
    }
    if (format ? owf_book_format_named(format) != NULL : owf_book_format_for_filename(to) != NULL) {
        owf_report_free(report);
        return convert_book(from, to, format, show_report);
    }
#endif
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

#ifdef __amigaos__

int main(void)
{
    static const char template[] = "FROM,TO,FORMAT/K,REPORT/S,FORMATS/S";
    LONG args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)template, args, NULL);
    int rc;

    if (!rda) {
        PrintFault(IoErr(), (CONST_STRPTR)"OWConvert");
        return RC_ERROR;
    }
    if (!owf_open()) {
        printf("OWConvert: needs owf.library %d in LIBS:\n", OWF_LIBRARY_VERSION);
        FreeArgs(rda);
        return RC_FAIL;
    }
    if (args[4]) {
        list_formats();
        rc = RC_OK;
    } else if (!args[0] || !args[1]) {
        printf("OWConvert: give FROM and TO (template %s)\n", template);
        rc = RC_ERROR;
    } else {
        rc = convert((const char *)args[0], (const char *)args[1], (const char *)args[2], args[3] != 0);
    }
    owf_close();
    FreeArgs(rda);
    return rc;
}

#else

int main(int argc, char **argv)
{
    const char *from = NULL, *to = NULL, *format = NULL;
    int i, show_report = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--formats") == 0) {
            list_formats();
            return RC_OK;
        } else if (strcmp(argv[i], "--report") == 0) {
            show_report = 1;
        } else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            format = argv[++i];
        } else if (!from) {
            from = argv[i];
        } else if (!to) {
            to = argv[i];
        } else {
            from = NULL;
            break;
        }
    }
    if (!from || !to) {
        fprintf(stderr, "usage: owconvert [--format NAME] [--report] FROM TO\n       owconvert --formats\n");
        return RC_ERROR;
    }
    return convert(from, to, format, show_report);
}

#endif
