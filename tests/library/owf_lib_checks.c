/*
 * owf.library 2's checks: see owf_lib_checks.h. Only libowf's calls,
 * snprintf and the string calls: safe from two tasks at once on the Amiga,
 * where every call here goes through the library. The engine's answers are
 * compared as the grid shows them (owf_calc_format), so no double is
 * worked on here: on the Amiga that is the library's arithmetic and maths
 * (owf_softdouble.c, owf_libm.c), not this program's.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_calc.h"
#include "owf_pub.h"
#include "owf_zip.h"

#include "owf_lib_checks.h"

#include <stdio.h>
#include <string.h>

/* the condition first: the message may show what it worked out */
#define CHECK(ok, ...) do { \
    char what_[160]; \
    int ok_ = (ok); \
    snprintf(what_, sizeof what_, __VA_ARGS__); \
    c->check(c->ctx, ok_, what_); \
} while (0)

static void path_in(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    snprintf(out, size, "%s%s%s", dir, n && dir[n - 1] != ':' && dir[n - 1] != '/' ? "/" : "", name);
}

/* a cell as the grid shows it; "" when there is none */
static const char *shown(const owf_book *book, int sheet, unsigned row, unsigned col, char *out, size_t size)
{
    const owf_cell *cell = sheet < book->nsheets ? owf_sheet_find(&book->sheets[sheet], row, col) : NULL;
    out[0] = 0;
    if (cell)
        owf_calc_format(book, cell, out, size);
    return out;
}

/* ---- the fixture workbook, in three formats -------------------------------- */

static void fixture_book(const owf_lib_checks *c, const char *file, const char *format)
{
    char path[256], text[64];
    owf_book *book = NULL;
    owf_report *report = owf_report_new();
    const owf_book_format *used = NULL;
    owf_calc *calc;
    const owf_cell *d3;
    int r;

    path_in(path, sizeof path, c->fixtures, file);
    r = owf_import_book_file(path, NULL, &book, report, &used);
    CHECK(r == OWF_OK && book && used && !strcmp(used->name, format), "%s: %s opens as %s", c->tag, file, format);
    owf_report_free(report);
    if (r != OWF_OK || !book)
        return;
    CHECK(book->nsheets == 3 && !strcmp(book->sheets[0].name, "Numbers"), "%s: %s has its 3 sheets", c->tag, file);
    CHECK(!strcmp(shown(book, 0, 1, 2, text, sizeof text), "0.50"), "%s: %s C2 shows 0.50 (#,##0.00): %s", c->tag,
          file, text);
    CHECK(!strcmp(shown(book, 0, 1, 7, text, sizeof text), "Gr\xC3\xBC\xC3\x9F" "e aus K\xC3\xB6ln"),
          "%s: %s H2's text, from another sheet", c->tag, file);
    d3 = owf_sheet_find(&book->sheets[0], 2, 3);
    CHECK(d3 && d3->formula && !strcmp(d3->formula, "B3*C3") && !strcmp(shown(book, 0, 2, 3, text, sizeof text), "12.5"),
          "%s: %s D3 is B3*C3, 12.5", c->tag, file);
    calc = owf_calc_new(book);
    CHECK(calc != NULL, "%s: %s: the engine takes the book", c->tag, file);
    if (calc) {
        r = owf_calc_enter(calc, 0, 2, 1, "20");      /* B3 */
        r = r == OWF_OK ? owf_calc_recalc(calc) : r;
        CHECK(r == OWF_OK && !strcmp(shown(book, 0, 2, 3, text, sizeof text), "25"),
              "%s: %s B3 = 20 makes D3 25: %s", c->tag, file, text);
        owf_calc_free(calc);
    }
    owf_book_free(book);
}

/* the book saved as XLSX, ODS and CSV, and the first two read back */
static void save_and_read(const owf_lib_checks *c)
{
    static const char *const formats[] = { "xlsx", "ods", "csv" };
    char path[256], out[128], text[64];
    owf_book *book = NULL, *back;
    owf_report *report = owf_report_new();
    const owf_book_format *used;
    int i, r;

    path_in(path, sizeof path, c->fixtures, "book.xls");
    if (owf_import_book_file(path, "xls", &book, report, NULL) != OWF_OK || !book) {
        CHECK(0, "%s: book.xls for saving", c->tag);
        owf_report_free(report);
        return;
    }
    for (i = 0; i < 3; i++) {
        snprintf(out, sizeof out, "%sowflibtest-%s.%s", c->temp, c->tag, formats[i]);
        r = owf_export_book_file(book, out, NULL, report);
        if (r == OWF_OK && i < 2) {
            back = NULL;
            used = NULL;
            r = owf_import_book_file(out, NULL, &back, report, &used);
            if (r == OWF_OK && back) {
                if (!used || strcmp(used->name, formats[i]) || strcmp(shown(back, 0, 1, 2, text, sizeof text), "0.50") ||
                    strcmp(shown(back, 0, 2, 3, text, sizeof text), "12.5"))
                    r = -1;
                owf_book_free(back);
            }
        }
        CHECK(r == OWF_OK, "%s: book saved as %s%s", c->tag, formats[i], i < 2 ? " and read back" : "");
        c->delete_file(out);
    }
    owf_book_free(book);
    owf_report_free(report);
}

/* ---- the engine on a new book ---------------------------------------------- */

static void engine(const owf_lib_checks *c)
{
    /* formulas, and the grid's text for each: they go through the library's
     * soft double arithmetic and maths on the Amiga */
    static const char *const sums[][2] = {
        { "=SUM(A1:A3)", "6.5" },
        { "=SQRT(2)*SQRT(2)", "2" },
        { "=PI()", "3.14159265358979" },
        { "=SIN(PI()/6)", "0.5" },
        { "=COS(PI())", "-1" },
        { "=TAN(PI()/4)", "1" },
        { "=10^15", "1E+15" },
        { "=2^0.5", "1.4142135623731" },
        { "=LN(EXP(1))", "1" },
        { "=EXP(1)", "2.71828182845905" },
        { "=LOG10(1000)", "3" },
        { "=LOG(8,2)", "3" },
        { "=ATAN2(1,1)*4", "3.14159265358979" },
        { "=ASIN(1)*2", "3.14159265358979" },
        { "=ACOS(-1)", "3.14159265358979" },
        { "=SINH(1)", "1.1752011936438" },
        { "=COSH(1)", "1.54308063481524" },
        { "=TANH(0.5)", "0.46211715726001" },
        { "=MOD(-7,3)", "2" },
        { "=INT(-2.5)", "-3" },
        { "=ROUND(2.675,2)", "2.68" },
        { "=1/3", "0.333333333333333" },
        { "=0.1+0.2", "0.3" },
        { "=1E308*10", "#NUM!" },
        { "=SIN(1E22)", "-0.852200849767189" },
        { "=VALUE(\"1234.5e-1\")", "123.45" },
        { "=FV(0.05/12,360,-100)", "83225.8635361472" },
    };
    char text[64], *moved;
    owf_book *book = owf_book_new();
    owf_calc *calc;
    owf_cell *cell, result;
    owf_cellstyle style;
    unsigned row, col;
    unsigned long colour = 0;
    int i, r, bad = 0;

    if (!book || !owf_book_add_sheet(book, "Sheet1") || !(calc = owf_calc_new(book))) {
        CHECK(0, "%s: a new book and engine", c->tag);
        if (book)
            owf_book_free(book);
        return;
    }
    r = owf_calc_enter(calc, 0, 0, 0, "1");
    r |= owf_calc_enter(calc, 0, 1, 0, "2");
    r |= owf_calc_enter(calc, 0, 2, 0, "3.5");
    for (i = 0; i < (int)(sizeof sums / sizeof sums[0]); i++)
        r |= owf_calc_enter(calc, 0, (unsigned)i, 1, sums[i][0]);
    r |= owf_calc_recalc(calc);
    CHECK(r == OWF_OK, "%s: values and formulas entered and worked out", c->tag);
    for (i = 0; i < (int)(sizeof sums / sizeof sums[0]); i++) {
        shown(book, 0, (unsigned)i, 1, text, sizeof text);
        if (strcmp(text, sums[i][1])) {
            CHECK(0, "%s: %s shows %s, not %s", c->tag, sums[i][0], text, sums[i][1]);
            bad++;
        }
    }
    CHECK(!bad, "%s: %d formulas through the engine's maths", c->tag, (int)(sizeof sums / sizeof sums[0]));

    /* a number format, through a style */
    owf_cellstyle_init(&style);
    style.number_format = owf_book_numfmt(book, "#,##0.00");
    r = owf_calc_enter(calc, 0, 4, 0, "1234.5");
    cell = owf_sheet_cell(&book->sheets[0], 4, 0, 1);
    if (cell)
        cell->style = owf_book_style(book, &style);
    CHECK(r == OWF_OK && cell && !strcmp(shown(book, 0, 4, 0, text, sizeof text), "1,234.50"),
          "%s: 1234.5 as #,##0.00: %s", c->tag, text);
    /* a double through two registers each way */
    owf_number_text(0.1, text);
    CHECK(!strcmp(text, "0.1"), "%s: owf_number_text(0.1): %s", c->tag, text);
    cell = owf_sheet_cell(&book->sheets[0], 6, 0, 1);
    if (cell)
        owf_cell_set_number(cell, 2.5e-7);
    CHECK(cell && !strcmp(shown(book, 0, 6, 0, text, sizeof text), "0.00000025"), "%s: owf_cell_set_number(2.5e-7): %s",
          c->tag, text);
    if (cell) {
        /* the ranges are named, not compound literals: through owf.library's macros (inline/owf.h) the commas
         * inside { } would split the macro's arguments */
        static const owf_range a7 = { 6, 6, 0, 0 };
        owf_calc_touch(calc, 0, &a7);
    }

    /* rows in, a formula moved, a sheet renamed, a range moved */
    r = owf_calc_shift(calc, 0, 0, 0, 1);
    r |= owf_calc_recalc(calc);
    cell = owf_sheet_cell(&book->sheets[0], 1, 1, 0);
    CHECK(r == OWF_OK && cell && cell->formula && !strcmp(cell->formula, "SUM(A2:A4)") &&
          !strcmp(shown(book, 0, 1, 1, text, sizeof text), "6.5"), "%s: a row in above: SUM(A2:A4), 6.5", c->tag);
    moved = owf_calc_move_formula("=SUM(A1:A3)+$C$1", 1, 1);
    CHECK(moved && !strcmp(moved, "=SUM(B2:B4)+$C$1"), "%s: SUM(A1:A3)+$C$1 copied down and right: %s", c->tag,
          moved ? moved : "NULL");
    owf_free(moved);
    CHECK(owf_calc_rename_sheet(calc, 0, "Data") == OWF_OK && !strcmp(book->sheets[0].name, "Data"),
          "%s: the sheet renamed", c->tag);
    {
        static const owf_range b2 = { 1, 1, 1, 1 };
        r = owf_calc_move_range(calc, 0, &b2, 0, 0, 2);   /* B2 to D2 */
    }
    r |= owf_calc_recalc(calc);
    CHECK(r == OWF_OK && !owf_sheet_find(&book->sheets[0], 1, 1) && !strcmp(shown(book, 0, 1, 3, text, sizeof text), "6.5"),
          "%s: B2 moved to D2", c->tag);
    CHECK((owf_calc_state(calc, 0, 1, 3) & OWF_CALC_FORMULA) != 0, "%s: D2's state: a formula", c->tag);

    /* working out without changing the book */
    memset(&result, 0, sizeof result);
    r = owf_calc_evaluate(calc, 0, 0, 0, "SUM(A2:A4)*2", &result);
    CHECK(r == OWF_OK && !strcmp((owf_calc_format(book, &result, text, sizeof text), text), "13"),
          "%s: owf_calc_evaluate: %s", c->tag, text);
    owf_free(result.text);

    /* a loop */
    r = owf_calc_enter(calc, 0, 10, 4, "=E12");
    r |= owf_calc_enter(calc, 0, 11, 4, "=E11");
    r |= owf_calc_recalc(calc);
    CHECK(r == OWF_OK && owf_calc_circular_count(calc) == 2 && !strcmp(shown(book, 0, 10, 4, text, sizeof text), "#REF!"),
          "%s: a circular reference: #REF!, 2 cells", c->tag);

    /* names of cells, and a value through a format's text */
    owf_calc_cell_name(11, 1, text);
    CHECK(!strcmp(text, "B12"), "%s: owf_calc_cell_name(11, 1): %s", c->tag, text);
    CHECK(owf_calc_parse_cell("$B$12", &row, &col) && row == 11 && col == 1, "%s: owf_calc_parse_cell($B$12)", c->tag);
    memset(&result, 0, sizeof result);
    result.type = OWF_VALUE_NUMBER;
    owf_cell_set_number(&result, -0.4567);
    owf_calc_format_value("0.0%;[Red]-0.0%", &result, 0, 0, text, sizeof text, &colour);
    CHECK(!strcmp(text, "-45.7%") && colour == 0xFF0000UL, "%s: -0.4567 as 0.0%%;[Red]-0.0%%: %s", c->tag, text);
    owf_calc_recalc_all(calc);
    CHECK(!strcmp(shown(book, 0, 1, 3, text, sizeof text), "6.5"), "%s: everything worked out again", c->tag);
    owf_calc_free(calc);
    owf_book_free(book);
}

/* ---- what files are, zips and inflate, .pub --------------------------------- */

static void files(const owf_lib_checks *c)
{
    /* owf_inflate on a raw DEFLATE stream made with zlib (level 9): 511 bytes */
    static const unsigned char packed[] = {
        0xCB, 0x2F, 0x4F, 0xD3, 0xCB, 0xC9, 0x4C, 0x2A, 0x4A, 0x2C, 0xAA, 0x54, 0x30, 0x52, 0xC8, 0xCC,
        0x4B, 0xCB, 0x49, 0x2C, 0x49, 0x2D, 0x56, 0x28, 0xC9, 0xC8, 0x2C, 0x56, 0x28, 0xCF, 0x2C, 0xC9,
        0x50, 0x00, 0x4A, 0xE6, 0x97, 0xA7, 0xA9, 0x17, 0x2B, 0xE4, 0x97, 0xE7, 0xC1, 0xE4, 0x75, 0x20,
        0x52, 0xF9, 0x45, 0x60, 0x3A, 0xBF, 0xB4, 0x44, 0xA1, 0x0A, 0x66, 0x88, 0x1E, 0x50, 0xDD, 0xA8,
        0x89, 0x83, 0xD3, 0x44, 0xF7, 0xA2, 0xC3, 0x7B, 0x0E, 0xCF, 0x4F, 0x55, 0x48, 0x2C, 0x2D, 0x56,
        0xF0, 0x3E, 0xBC, 0x2D, 0x27, 0x4F, 0x8F, 0x0B, 0x00,
    };
    static const char phrase[] = "owf.library 2 inflates this with libowf's own inflate, with or without z.library. ";
    static const struct { const char *file; owf_kind kind; } kinds[] = {
        { "book.xls", OWF_KIND_WORKBOOK }, { "book-lo.xlsx", OWF_KIND_WORKBOOK },
        { "book-lo.ods", OWF_KIND_WORKBOOK }, { "Sample.pub", OWF_KIND_PUBLICATION },
    };
    char path[256], want[600];
    unsigned char out[511], *data, *part;
    size_t length, n;
    owf_unzip zip;
    owf_pub *pub = NULL;
    owf_report *report;
    int i, r;

    want[0] = 0;
    for (i = 0; i < 6; i++)
        strcat(want, phrase);
    strcat(want, "Gr\xC3\xBC\xC3\x9F" "e aus K\xC3\xB6ln.\n");
    memset(out, 0, sizeof out);
    r = owf_inflate(out, sizeof out, packed, sizeof packed);
    for (n = 0; n < sizeof out && out[n] == (unsigned char)want[n]; n++)
        ;
    CHECK(r == OWF_OK && strlen(want) == sizeof out && n == sizeof out,
          "%s: owf_inflate, 89 bytes to 511 (result %d, the first %u bytes right%s%.24s)", c->tag, r, (unsigned)n,
          n < sizeof out ? ", then: " : "", n < sizeof out ? (const char *)out + n : "");
    CHECK(owf_inflate(out, sizeof out - 1, packed, sizeof packed) == OWF_ERR_CORRUPT &&
          owf_inflate(out, sizeof out, packed, sizeof packed - 9) == OWF_ERR_CORRUPT,
          "%s: owf_inflate refuses a wrong size and a cut-short stream", c->tag);

    for (i = 0; i < 4; i++) {
        path_in(path, sizeof path, c->fixtures, kinds[i].file);
        if (!c->read_file(path, &data, &length)) {
            CHECK(0, "%s: %s read", c->tag, kinds[i].file);
            continue;
        }
        CHECK(owf_file_kind(data, length) == kinds[i].kind, "%s: %s is a %s", c->tag, kinds[i].file,
              kinds[i].kind == OWF_KIND_WORKBOOK ? "workbook" : "publication");
        if (i == 1) {                            /* the XLSX: a zip of deflated parts */
            r = owf_unzip_open(&zip, data, length);
            CHECK(r && owf_unzip_has(&zip, "xl/workbook.xml") && !owf_unzip_has(&zip, "word/document.xml"),
                  "%s: book-lo.xlsx opened as a zip", c->tag);
            r = r ? owf_unzip_read(&zip, "xl/workbook.xml", &part, &n) : -1;
            CHECK(r == OWF_OK && n > 100 && strstr((char *)part, "<workbook") && strstr((char *)part, "Numbers"),
                  "%s: its xl/workbook.xml inflated, %lu bytes", c->tag, (unsigned long)(r == OWF_OK ? n : 0));
            if (r == OWF_OK)
                owf_free(part);
            CHECK(r == OWF_OK ? owf_unzip_read(&zip, "no/such.xml", &part, &n) == OWF_ERR_FORMAT : 0,
                  "%s: a part it hasn't: OWF_ERR_FORMAT", c->tag);
        }
        if (i == 3)
            CHECK(owf_pub_detect(data, length) > 0, "%s: Sample.pub detected", c->tag);
        owf_free(data);
    }

    path_in(path, sizeof path, c->fixtures, "Sample.pub");
    report = owf_report_new();
    r = owf_pub_read_file(path, &pub, report);
    CHECK(r == OWF_OK && pub && pub->npages >= 1 && pub->width > 0 && owf_pub_version_name(pub->version),
          "%s: Sample.pub read: %s, %d page(s)", c->tag, pub ? owf_pub_version_name(pub->version) : "-",
          pub ? pub->npages : 0);
    if (pub)
        owf_pub_free(pub);
    owf_report_free(report);
}

void owf_lib_run_checks(const owf_lib_checks *c)
{
    char text[32];
    CHECK(owf_error_name(OWF_ERROR_DIV0) && !strcmp(owf_error_name(OWF_ERROR_DIV0), "#DIV/0!"), "%s: owf_error_name",
          c->tag);
    CHECK(owf_book_format_count() >= 5 && owf_book_format_named("xlsx") &&
          owf_book_format_for_filename("Prices.ODS") == owf_book_format_named("ods") && owf_book_format_at(0),
          "%s: workbook formats listed, by name and by file name", c->tag);
    owf_calc_cell_name(0, 27, text);
    CHECK(!strcmp(text, "AB1"), "%s: column 27 is AB", c->tag);
    fixture_book(c, "book.xls", "xls");
    fixture_book(c, "book-lo.xlsx", "xlsx");
    fixture_book(c, "book-lo.ods", "ods");
    save_and_read(c);
    engine(c);
    files(c);
}
