/*
 * owf_file_kind (libowf/src/owf_kind.c) on the repository's samples and
 * fixtures and on files made here (ODP and PPTX packages, flat ODF, an
 * OpenBase header), and owf_struct_size against sizeof. Run from the
 * repository's top: make kind-test.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_internal.h"
#include "owf_pub.h"
#include "owf_zip.h"
#include "owf_deck.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static const char *const names[] = { "unknown", "document", "workbook", "deck", "database", "publication" };

static void expect(const char *what, owf_kind got, owf_kind want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: %s, not %s\n", what, names[got], names[want]);
    }
}

static void file(const char *path, owf_kind want)
{
    unsigned char *data;
    size_t length;
    if (owf_sys_read_file(path, &data, &length) != OWF_OK) {
        checks++;
        failures++;
        printf("FAIL %s: can't read it\n", path);
        return;
    }
    expect(path, owf_file_kind(data, length), want);
    free(data);
}

/* a zip package made here: mimetype (stored) and one more part */
static void package(const char *what, const char *first, const char *first_text, const char *second,
                    owf_kind want)
{
    owf_buf out;
    owf_zip *zip;
    owf_buf_init(&out);
    zip = owf_zip_new(&out);
    if (!zip || owf_zip_add(zip, first, first_text, strlen(first_text), 0) != OWF_OK ||
        owf_zip_add(zip, second, "<x/>", 4, 1) != OWF_OK || owf_zip_finish(zip) != OWF_OK) {
        checks++;
        failures++;
        printf("FAIL %s: not made\n", what);
        owf_buf_free(&out);
        return;
    }
    expect(what, owf_file_kind((const unsigned char *)out.data, out.length), want);
    owf_buf_free(&out);
}

int main(void)
{
    static const struct { const char *path; owf_kind kind; } files[] = {
        { "samples/OpenWrite-Tour.odt", OWF_KIND_DOCUMENT },
        { "samples/OpenWrite-Tour.docx", OWF_KIND_DOCUMENT },
        { "samples/OpenWrite-Tour.txt", OWF_KIND_DOCUMENT },
        { "samples/ANSI-Text.ans", OWF_KIND_DOCUMENT },
        { "tests/book/fixtures/book.xls", OWF_KIND_WORKBOOK },
        { "tests/book/fixtures/book-lo.ods", OWF_KIND_WORKBOOK },
        { "tests/book/fixtures/book-odfpy.ods", OWF_KIND_WORKBOOK },
        { "tests/book/fixtures/book-lo.xlsx", OWF_KIND_WORKBOOK },
        { "tests/book/fixtures/book-openpyxl.xlsx", OWF_KIND_WORKBOOK },
        { "tests/book/fixtures/book.fods", OWF_KIND_WORKBOOK },
        { "tests/calc/fixtures/calc.fods", OWF_KIND_WORKBOOK },
        { "tests/book/fixtures/csv/quoting.csv", OWF_KIND_DOCUMENT },      /* plain text: see owf.h */
        { "tests/pub/samples/Sample.pub", OWF_KIND_PUBLICATION },
        { "tests/pub/samples/Sample98.pub", OWF_KIND_PUBLICATION },
        { "tests/pub/samples/Sample_2010.pub", OWF_KIND_PUBLICATION },
    };
    static const char fodp[] =
        "<?xml version=\"1.0\"?>\n<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "office:mimetype=\"application/vnd.oasis.opendocument.presentation\"><office:body/></office:document>";
    static const char fodt[] =
        "<?xml version=\"1.0\"?>\n<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "office:mimetype=\"application/vnd.oasis.opendocument.text\"><office:body/></office:document>";
    unsigned char sqlite[128];
    unsigned i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++)
        file(files[i].path, files[i].kind);
    package("ODP made here", "mimetype", "application/vnd.oasis.opendocument.presentation", "content.xml",
            OWF_KIND_DECK);
    package("ODS made here", "mimetype", "application/vnd.oasis.opendocument.spreadsheet", "content.xml",
            OWF_KIND_WORKBOOK);
    package("ODG made here", "mimetype", "application/vnd.oasis.opendocument.graphics", "content.xml",
            OWF_KIND_UNKNOWN);
    package("PPTX made here", "[Content_Types].xml",
            "<Types><Override PartName=\"/ppt/presentation.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/></Types>",
            "ppt/presentation.xml", OWF_KIND_DECK);
    package("a zip of something else", "readme.txt", "hello", "other.txt", OWF_KIND_UNKNOWN);
    expect("FODP made here", owf_file_kind((const unsigned char *)fodp, sizeof fodp - 1), OWF_KIND_DECK);
    expect("FODT made here", owf_file_kind((const unsigned char *)fodt, sizeof fodt - 1), OWF_KIND_DOCUMENT);
    memset(sqlite, 0, sizeof sqlite);
    memcpy(sqlite, "SQLite format 3", 16);
    expect("SQLite, not OpenBase's", owf_file_kind(sqlite, sizeof sqlite), OWF_KIND_UNKNOWN);
    memcpy(sqlite + 68, "OADB", 4);
    expect("OpenBase's database", owf_file_kind(sqlite, sizeof sqlite), OWF_KIND_DATABASE);
    expect("nothing", owf_file_kind(sqlite, 0), OWF_KIND_UNKNOWN);
    expect("NULL", owf_file_kind(NULL, 10), OWF_KIND_UNKNOWN);
    expect("binary", owf_file_kind((const unsigned char *)"\x01\x02\x00\x7F", 4), OWF_KIND_UNKNOWN);
    expect("cut-short zip", owf_file_kind((const unsigned char *)"PK\x03\x04", 4), OWF_KIND_UNKNOWN);

    /* the sizes the library reports are the structures' */
    {
        static const size_t want[OWF_STRUCT_COUNT] = {
            sizeof(owf_charfmt), sizeof(owf_run), sizeof(owf_tab), sizeof(owf_parafmt), sizeof(owf_para),
            sizeof(owf_story), sizeof(owf_font), sizeof(owf_page), sizeof(owf_image), sizeof(owf_doc),
            sizeof(owf_format), sizeof(owf_cell), sizeof(owf_cellstyle), sizeof(owf_colinfo), sizeof(owf_rowinfo),
            sizeof(owf_range), sizeof(owf_sheet), sizeof(owf_defname), sizeof(owf_book), sizeof(owf_book_format),
            sizeof(owf_unzip), sizeof(owf_pub), sizeof(owf_pub_page), sizeof(owf_pub_frame), sizeof(owf_condfmt),
            sizeof(owf_comment), sizeof(owf_validation), sizeof(owf_part), sizeof(owf_frame), sizeof(owf_chart_point),
            sizeof(owf_chart_series), sizeof(owf_chart), sizeof(owf_look), sizeof(owf_textlevel), sizeof(owf_obj),
            sizeof(owf_transition), sizeof(owf_slide), sizeof(owf_master), sizeof(owf_slide_layout), sizeof(owf_kept),
            sizeof(owf_deck_part), sizeof(owf_deck), sizeof(owf_deck_format),
        };
        int k;
        for (k = 0; k < OWF_STRUCT_COUNT; k++) {
            checks++;
            if (owf_struct_size(k) != want[k]) {
                failures++;
                printf("FAIL owf_struct_size(%d): %lu, not %lu\n", k, owf_struct_size(k), (unsigned long)want[k]);
            }
        }
        checks++;
        if (owf_struct_size(-1) || owf_struct_size(OWF_STRUCT_COUNT)) {
            failures++;
            printf("FAIL owf_struct_size out of range\n");
        }
    }
    printf("test-owf-kind: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
