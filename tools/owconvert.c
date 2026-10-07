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
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include "owf.h"

#include <stdio.h>
#include <string.h>

#ifdef __amigaos__
#include <dos/dos.h>
#include <proto/dos.h>
#define RC_OK RETURN_OK
#define RC_ERROR RETURN_ERROR
#define RC_FAIL RETURN_FAIL
static const char version[] __attribute__((used)) = "$VER: OWConvert " OWF_VERSION " (7.10.2026) Dalsin Limited";
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

static int convert(const char *from, const char *to, const char *format, int show_report)
{
    owf_report *report = owf_report_new();
    owf_doc *doc = NULL;
    const owf_format *used = NULL;
    int result, rc = RC_OK;

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
    if (args[4]) {
        list_formats();
        rc = RC_OK;
    } else if (!args[0] || !args[1]) {
        printf("OWConvert: give FROM and TO (template %s)\n", template);
        rc = RC_ERROR;
    } else {
        rc = convert((const char *)args[0], (const char *)args[1], (const char *)args[2], args[3] != 0);
    }
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
