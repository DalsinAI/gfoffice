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
 * Workbooks too, when FROM is one, or TO (or FORMAT) is a workbook format:
 *   1> OWConvert Accounts.xls RAM:Accounts.csv
 *
 * Presentations, when FROM is one (ODP, flat ODP): to ODP or flat ODP,
 * and to PDF, a page a slide, laid out by OpenLayout (OpenPresentation's
 * core, src/deck; built in when OWC_DECK_PDF is defined):
 *   1> OWConvert Talk.odp RAM:Talk.pdf
 *
 * A Publisher file it reads, says what it is, and can't convert yet.
 *
 * On the Amiga it is built on GFOffice's libraries (docs/suite/owf-libraries.md,
 * section 5), one file each (owc.h): owfwrite.library says what FROM is and
 * converts documents; owfcalc.library, owfpresent.library and owfpub.library
 * are opened only when a conversion needs them. With libowf linked in the
 * same files are built together.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __amigaos__
#include <proto/dos.h>
static const char version[] __attribute__((used)) = "$VER: OWConvert " OWF_VERSION " (25.10.2026) AmigaChrome Team";
#endif

int owc_ends_with(const char *s, const char *tail)
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

const char *owc_level_name(int level)
{
    static const char *levels[] = { "Note", "Approximated", "Left out" };
    return level >= 0 && level < 3 ? levels[level] : "Note";
}

int owc_missing(const char *library, int version, const char *what)
{
    printf("OWConvert: %s needs %s %d in LIBS:\n", what, library, version);
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

static void list_formats(void)
{
    owc_doc_formats();
    owc_book_formats();
    owc_deck_formats();
}

/* Which part converts, as OWConvert always chose: a presentation when FROM is one (by what it is, then its
 * reader) or FORMAT names a presentation format; a workbook when FORMAT or TO is a workbook format; else a
 * document. What FROM is comes from owfwrite.library's owf_file_kind; each other part's library is opened only
 * when it is asked: owfcalc.library when FROM is a workbook or TO isn't a document, owfpresent.library for a
 * presentation, owfpub.library for a Publisher file. */
static int convert(const char *from, const char *to, const char *format, int show_report)
{
    size_t n = 0;
    unsigned char *data = read_all(from, &n);
    int kind = data ? owc_doc_kind(data, n) : OWF_KIND_UNKNOWN, rc;

    if (data && ((kind == OWF_KIND_DECK && owc_deck_reads(data, n)) ||
                 (format && !owc_doc_writes(to, format) && owc_deck_names(format)))) {
        rc = owc_deck_convert(data, n, from, to, format, show_report);
        free(data);
        return rc;
    }
    free(data);
    /* a workbook's library only when FROM is a workbook or TO isn't a document */
    if ((kind == OWF_KIND_WORKBOOK || !owc_doc_writes(to, format)) && owc_book_writes(to, format))
        return owc_book_convert(from, to, format, show_report);
    if (kind == OWF_KIND_PUBLICATION)
        return owc_pub_convert(from, show_report);
    return owc_doc_convert(from, to, format, show_report);
}

static void close_all(void)
{
    owc_doc_close();
    owc_book_close();
    owc_deck_close();
    owc_pub_close();
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
    close_all();
    FreeArgs(rda);
    return rc;
}

#else

int main(int argc, char **argv)
{
    const char *from = NULL, *to = NULL, *format = NULL;
    int i, rc, show_report = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--formats") == 0) {
            list_formats();
            close_all();
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
    rc = convert(from, to, format, show_report);
    close_all();
    return rc;
}

#endif
