/*
 * test_bookfmt: the workbook files' helpers on their own. OpenFormula to
 * Excel's text and back (references, sheets, ranges, function names,
 * separators, arrays, unions and intersections), XLSX's shared formulas
 * and "_xlfn." names, numbers read and written exactly, ISO dates and
 * durations, and what a number format shows.
 *
 *   test_bookfmt            the tests; exit 1 when one fails
 *   test_bookfmt --parse    reads numbers a line at a time and prints each
 *                           as libowf reads it (hex bits) and writes it,
 *                           for test_xml_books.py to hold against Python's
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

static void same(const char *what, const char *in, const char *got, const char *want)
{
    checks++;
    if ((got == NULL) != (want == NULL) || (got && strcmp(got, want))) {
        failures++;
        printf("FAIL %s: %s\n  got  %s\n  want %s\n", what, in, got ? got : "(null)", want ? want : "(null)");
    }
}

/* Excel's text and OpenFormula's, both ways. */
static const char *const both_ways[][2] = {
    { "SUM(A1:A3)", "of:=SUM([.A1:.A3])" },
    { "B2*C2", "of:=[.B2]*[.C2]" },
    { "IF(D2>100,\"big\",\"small\")", "of:=IF([.D2]>100;\"big\";\"small\")" },
    { "IF(A1,\"a,b;c|d\",1)", "of:=IF([.A1];\"a,b;c|d\";1)" },
    { "'Text & more'!A1", "of:=[$'Text & more'.A1]" },
    { "SUM(Numbers!B2:B5)", "of:=SUM([$Numbers.B2:.B5])" },
    { "Numbers!$A$1", "of:=[$Numbers.$A$1]" },
    { "'Zoë''s'!B$2", "of:=[$'Zoë''s'.B$2]" },
    { "'2026'!A1+'A1'!B2", "of:=[$'2026'.A1]+[$A1.B2]" },
    { "SUM('First:Second sheet'!A1:A3)", "of:=SUM([$First.A1:$'Second sheet'.A3])" },
    { "First:Last!C3", "of:=[$First.C3:$Last.C3]" },
    { "SUM((A1,B1))", "of:=SUM(([.A1]~[.B1]))" },
    { "SUM(A1:A3,(A1,B1))", "of:=SUM([.A1:.A3];([.A1]~[.B1]))" },
    { "SUM(A1:B2 B1:C3)", "of:=SUM([.A1:.B2]![.B1:.C3])" },
    { "SUM({1,2;3,4})", "of:=SUM({1;2|3;4})" },
    { "{1,TRUE;#N/A,\"x\"}", "of:={1;TRUE|#N/A;\"x\"}" },
    { "-{1,-2}", "of:=-{1;-2}" },
    { "TRUE", "of:=TRUE()" },
    { "NOT(FALSE)", "of:=NOT(FALSE())" },
    { "A1=TRUE", "of:=[.A1]=TRUE()" },
    { "SUM(B:B)", "of:=SUM([.B:.B])" },
    { "SUM($A:$C)", "of:=SUM([.$A:.$C])" },
    { "SUM(2:3)", "of:=SUM([.2:.3])" },
    { "SUM($1:$1)", "of:=SUM([.$1:.$1])" },
    { "$A$1:B$3", "of:=[.$A$1:.B$3]" },
    { "XFD1048576", "of:=[.XFD1048576]" },
    { "CEILING(A1,2)", "of:=COM.MICROSOFT.CEILING([.A1];2)" },
    { "FLOOR(A1,2)", "of:=COM.MICROSOFT.FLOOR([.A1];2)" },
    { "CONCAT(A1,B1)", "of:=COM.MICROSOFT.CONCAT([.A1];[.B1])" },
    { "IFS(A1>1,1,TRUE,2)", "of:=COM.MICROSOFT.IFS([.A1]>1;1;TRUE();2)" },
    { "STDEV.S(A1:A3)", "of:=COM.MICROSOFT.STDEV.S([.A1:.A3])" },
    { "ISO.CEILING(A1)", "of:=COM.MICROSOFT.ISO.CEILING([.A1])" },
    { "IFNA(A1,0)", "of:=IFNA([.A1];0)" },
    { "DAYS(A1,A2)", "of:=DAYS([.A1];[.A2])" },
    { "CHIDIST(1,2)", "of:=LEGACY.CHIDIST(1;2)" },
    { "FDIST(1,2,3)", "of:=LEGACY.FDIST(1;2;3)" },
    { "F.DIST(1,2,3,TRUE)", "of:=FDIST(1;2;3;TRUE())" },
    { "FORMULATEXT(A1)", "of:=FORMULA([.A1])" },
    { "SKEW.P(A1:A3)", "of:=SKEWP([.A1:.A3])" },
    { "IFERROR(1/0,\"none\")", "of:=IFERROR(1/0;\"none\")" },
    { "VLOOKUP(\"Pear\",A2:D5,4,0)", "of:=VLOOKUP(\"Pear\";[.A2:.D5];4;0)" },
    { "#REF!", "of:=#REF!" },
    { "SUM(#REF!,#DIV/0!)", "of:=SUM(#REF!;#DIV/0!)" },
    { "-A1^2+(B3+B4)*2", "of:=-[.A1]^2+([.B3]+[.B4])*2" },
    { "A1 + B1", "of:=[.A1] + [.B1]" },
    { "SUM( A1 , B1 )", "of:=SUM( [.A1] ; [.B1] )" },
    { "50%", "of:=50%" },
    { "1E+20*0.5", "of:=1E+20*0.5" },
    { "LOG10(100)", "of:=LOG10(100)" },
    { "LOG10", "of:=[.LOG10]" },
    { "SUM(Prices)", "of:=SUM(Prices)" },
    { "B3:INDEX(A1:A3,2)", "of:=[.B3]:INDEX([.A1:.A3];2)" },
    { "IF(A1,,1)", "of:=IF([.A1];;1)" },
    { "A1<>B1", "of:=[.A1]<>[.B1]" },
    { "A1<=B1", "of:=[.A1]<=[.B1]" },
    { "A2&\" & \"&A3", "of:=[.A2]&\" & \"&[.A3]" },
    { "[Other.xls]Rates!$A$1", "of:=['Other.xls'#$Rates.$A$1]" },
    { "'[Other book.xls]Rates'!$A$1:$B$2", "of:=['Other book.xls'#$Rates.$A$1:.$B$2]" },
    { "MAX(Table1[Col])", "of:=MAX(Table1[Col])" },
    { NULL, NULL }
};

/* OpenFormula only one way: what other programs write. */
static const char *const of_in[][2] = {
    { "of:=SUM([.A1]~[.B1];[.C1])", "SUM((A1,B1),C1)" },
    { "of:=SUM([.A1]~[.B1]~[.C1])", "SUM((A1,B1,C1))" },
    { "of:=SUM([$S1.B2:'Q1 sales'.C3])", "SUM('S1:Q1 sales'!B2:C3)" },
    { "of:=SUM([$Sheet1.A1:$Sheet1.B2])", "SUM(Sheet1!A1:B2)" },
    { "of:=[Sheet2.A1]", "Sheet2!A1" },
    { "of:=_xlfn.xlookup(1;[.A1:.A3];[.B1:.B3])", "XLOOKUP(1,A1:A3,B1:B3)" },
    { "of:=_xlfn._xlws.sort([.A1:.A3])", "SORT(A1:A3)" },
    { "of:=COM.MICROSOFT.NETWORKDAYS.INTL([.A1];[.A2])", "NETWORKDAYS.INTL(A1,A2)" },
    { "of:=LEGACY.FINV(1;2;3)", "FINV(1,2,3)" },
    { "of:=FINV(1;2;3)", "F.INV(1,2,3)" },
    { "of:=sum([.a1:.a2])", "SUM(A1:A2)" },
    { "of:=TRUE( )", "TRUE" },
    { "of:=[.#REF!]", "#REF!" },
    { "of:=[$Sheet1.#REF!]+1", "#REF!+1" },
    { "of:=[.A1:.#REF!]", "#REF!" },
    { "of:=1E+020", "1E+020" },
    { "of:=SUM([.A1]![.B1])", "SUM(A1 B1)" },
    { "of:=WEEKNUM([.A1];1)", "WEEKNUM(A1,1)" },
    { "of:=['file:///home/dale/Books/Other.ods'#$Rates.A1]", "[Other.ods]Rates!A1" },
    { "of:=[$'it''s'.A1]", "'it''s'!A1" },
    { "of:=[.A1] [.B1]", "A1B1" },
    { "msoxl:=SUM(A1:A2)", "SUM(A1:A2)" },
    { "oooc:=SUM([.A1:.A2])", "SUM(A1:A2)" },
    { "=SUM([.A1:.A2])", "SUM(A1:A2)" },
    { "of:=SUM([.A1:.A2]", "SUM(A1:A2" },
    { "of:=[.A1", NULL },
    { "of:=[.A1:.B]", NULL },
    { "of:=[.ZZZZ1]", NULL },
    { "of:=[.A0]", NULL },
    { "of:=[.A1048577]", NULL },
    { NULL, NULL }
};

/* Excel's text one way: sheet-level names lose their sheet. */
static const char *const xl_in[][2] = {
    { "Sheet1!MyName*2", "of:=MyName*2" },
    { "Other.xls!Rate", "of:=Rate" },
    { "_xlfn.CONCAT(A1)", "of:=COM.MICROSOFT.CONCAT([.A1])" },
    { "SUM(A1:A3", "of:=SUM([.A1:.A3]" },
    { "\"unterminated", "of:=\"unterminated" },
    { NULL, NULL }
};

static void test_openformula(void)
{
    int i;
    char deep[400], *got;
    for (i = 0; both_ways[i][0]; i++) {
        got = owf_excel_to_of(both_ways[i][0]);
        same("excel to of", both_ways[i][0], got, both_ways[i][1]);
        free(got);
        got = owf_of_to_excel(both_ways[i][1]);
        same("of to excel", both_ways[i][1], got, both_ways[i][0]);
        free(got);
    }
    for (i = 0; of_in[i][0]; i++) {
        got = owf_of_to_excel(of_in[i][0]);
        same("of to excel", of_in[i][0], got, of_in[i][1]);
        free(got);
    }
    for (i = 0; xl_in[i][0]; i++) {
        got = owf_excel_to_of(xl_in[i][0]);
        same("excel to of", xl_in[i][0], got, xl_in[i][1]);
        free(got);
    }
    /* nesting too deep is refused, not overrun */
    memset(deep, '(', 100);
    strcpy(deep + 100, "1");
    memset(deep + 101, ')', 100);
    deep[201] = 0;
    got = owf_excel_to_of(deep);
    same("too deep", "100 brackets", got, NULL);
    free(got);
    got = owf_of_to_excel(deep);
    same("too deep", "100 brackets", got, NULL);
    free(got);
}

static void test_move(void)
{
    static const struct {
        const char *in;
        long drow, dcol;
        const char *want;
    } moves[] = {
        { "B2*C2", 1, 0, "B3*C3" },
        { "$B$2*C2", 2, 1, "$B$2*D4" },
        { "B$2+$B2", 3, 3, "E$2+$B5" },
        { "SUM(A1:B2)", 1, 1, "SUM(B2:C3)" },
        { "SUM(A:A)", 5, 1, "SUM(B:B)" },
        { "SUM(1:1)", 2, 3, "SUM(3:3)" },
        { "A2-A1", -1, 0, "A1-#REF!" },
        { "Sheet2!A1+\"A1\"", 1, 1, "Sheet2!B2+\"A1\"" },
        { "'My Sheet'!$A1", 3, 3, "'My Sheet'!$A4" },
        { "LOG10(A1)+LOG10", 1, 0, "LOG10(A2)+LOG11" },
        { "Prices*TRUE", 4, 4, "Prices*TRUE" },
        { "XFD1+A1", 0, 1, "#REF!+B1" },
        { NULL, 0, 0, NULL }
    };
    int i;
    for (i = 0; moves[i].in; i++) {
        char *got = owf_xl_formula_move(moves[i].in, moves[i].drow, moves[i].dcol);
        same("move", moves[i].in, got, moves[i].want);
        free(got);
    }
}

/* the model's text, the file's, and (when not the same) the model's again */
static const char *const books_both[][3] = {
    { "[Other.xls]Rates!$A$1*2", "[1]Rates!$A$1*2", NULL },
    { "SUM('[Other.xls]Q1 sales'!A1:B2)", "SUM('[1]Q1 sales'!A1:B2)", NULL },
    { "Other.xls!Rate+Rates!Local", "[1]!Rate+Rates!Local", NULL },
    { "[Second.xlsx]Rates!A1", "[2]Rates!A1", "[2]Rates!A1" },
    { "'[My Book.xlsx]Sheet 1'!B2", "'[3]Sheet 1'!B2", NULL },
    { NULL, NULL, NULL }
};

static void test_xlfn(void)
{
    static const char *const from[][2] = {
        { "_xlfn.CONCAT(A1,_xlfn.IFS(TRUE,1))", "CONCAT(A1,IFS(TRUE,1))" },
        { "_xlfn._xlws.FILTER(A1:A3,A1:A3>1)", "FILTER(A1:A3,A1:A3>1)" },
        { "_xlfn.LET(_xlpm.x,1,_xlpm.x+1)", "LET(x,1,x+1)" },
        { "\"_xlfn.CONCAT(\"&A1", "\"_xlfn.CONCAT(\"&A1" },
        { NULL, NULL }
    };
    static const char *const to[][2] = {
        { "CONCAT(A1,IFS(TRUE,1))", "_xlfn.CONCAT(A1,_xlfn.IFS(TRUE,1))" },
        { "FILTER(A1:A3,TRUE)+SORT(B1:B2)", "_xlfn._xlws.FILTER(A1:A3,TRUE)+_xlfn._xlws.SORT(B1:B2)" },
        { "SUM(A1)+IFERROR(1,2)+concat(1)", "SUM(A1)+IFERROR(1,2)+_xlfn.concat(1)" },
        { "\"CONCAT(\"&STDEV.S(A1:A2)", "\"CONCAT(\"&_xlfn.STDEV.S(A1:A2)" },
        { NULL, NULL }
    };
    static char other[] = "Other.xls", spaced[] = "My Book.xlsx";
    char *books[3];
    owf_book *book = owf_book_new();
    owf_xl_links links;
    int i;
    books[0] = other;
    books[1] = NULL;
    books[2] = spaced;
    for (i = 0; from[i][0]; i++) {
        char *got = owf_xl_formula_from_file(from[i][0], books, 3);
        same("from xlsx", from[i][0], got, from[i][1]);
        free(got);
    }
    for (i = 0; to[i][0]; i++) {
        char *got = owf_xl_formula_for_file(to[i][0], NULL, NULL);
        same("for xlsx", to[i][0], got, to[i][1]);
        free(got);
    }
    /* other books: to their numbers and back */
    memset(&links, 0, sizeof links);
    owf_book_add_sheet(book, "Rates");
    for (i = 0; books_both[i][0]; i++) {
        char *got = owf_xl_formula_for_file(books_both[i][0], book, &links), *back;
        same("books for xlsx", books_both[i][0], got, books_both[i][1]);
        back = owf_xl_formula_from_file(got ? got : "", books, 3);
        same("books from xlsx", books_both[i][1], back, books_both[i][2] ? books_both[i][2] : books_both[i][0]);
        free(got);
        free(back);
    }
    checks++;
    if (links.failed || links.count != 3 || strcmp(links.items[0].file, "Other.xls") || links.items[0].nsheets != 2 ||
        strcmp(links.items[0].sheets[1], "Q1 sales") || links.items[0].nnames != 1 ||
        strcmp(links.items[0].names[0], "Rate") || strcmp(links.items[2].file, "My Book.xlsx")) {
        failures++;
        printf("FAIL links: %d books\n", links.count);
    }
    owf_xl_links_free(&links);
    owf_book_free(book);
}

static void test_numbers(void)
{
    static const struct {
        const char *text;
        double value;
        int ok;
    } reads[] = {
        { "0.1", 0.1, 1 }, { "12", 12, 1 }, { "-12.5", -12.5, 1 }, { "1E+20", 1e20, 1 }, { "1E+020", 1e20, 1 },
        { ".5", 0.5, 1 }, { "5.", 5, 1 }, { "+3", 3, 1 }, { "0.30000000000000004", 0.30000000000000004, 1 },
        { "4.9406564584124654E-324", 4.9406564584124654e-324, 1 }, { "2.2250738585072014E-308", 2.2250738585072014e-308, 1 },
        { "1.7976931348623157E+308", 1.7976931348623157e308, 1 }, { "1.7976931348623159E+308", 0, 0 },
        { "1e400", 0, 0 }, { "1e-400", 0, 1 }, { "9007199254740993", 9007199254740992.0, 1 },
        { "123456789012345678901234567890", 1.2345678901234568e29, 1 }, { "0.000123", 0.000123, 1 },
        { "", 0, 0 }, { "-", 0, 0 }, { "1e", 0, 0 }, { "abc", 0, 0 }, { "1.5.2", 0, 0 }, { "1 ", 0, 0 },
        { "0x10", 0, 0 }, { "inf", 0, 0 }, { NULL, 0, 0 }
    };
    static const struct {
        double value;
        const char *want;
    } writes[] = {
        { 0.1, "0.1" }, { 0.30000000000000004, "0.30000000000000004" }, { 1e20, "1E+20" }, { -2.5, "-2.5" },
        { 1.0 / 3, "0.3333333333333333" }, { 5e-324, "4.94065645841247E-324" }, { 123.45, "123.45" },
        { 46316, "46316" }, { 0.000123, "0.000123" }, { 1e-10, "1E-10" }, { 0, "0" },
        { 123456789012345678.0, "1.2345678901234568E+17" }, { 2.5e-7, "0.00000025" }, { 0, NULL }
    };
    int i;
    char text[64], want[64];
    for (i = 0; reads[i].text; i++) {
        double v = -1;
        int ok = owf_number_parse(reads[i].text, strlen(reads[i].text), &v);
        checks++;
        if (ok != reads[i].ok || (ok && v != reads[i].value)) {
            failures++;
            printf("FAIL parse %s: %d %.17g, want %d %.17g\n", reads[i].text, ok, v, reads[i].ok, reads[i].value);
        }
    }
    for (i = 0; writes[i].want; i++) {
        owf_number_exact(writes[i].value, text);
        snprintf(want, sizeof want, "%.17g", writes[i].value);
        same("exact", want, text, writes[i].want);
    }
}

static void test_dates(void)
{
    owf_buf b;
    double v;
    long y;
    int m, d;
    checks += 4;
    if (owf_days_from_date(1899, 12, 30) != 0 || owf_days_from_date(1900, 1, 1) != 2 ||
        owf_days_from_date(2026, 10, 21) != 46316 || owf_days_from_date(1904, 1, 1) != 1462) {
        failures++;
        printf("FAIL days_from_date\n");
    }
    owf_date_from_days(46316, &y, &m, &d);
    if (y != 2026 || m != 10 || d != 21) {
        failures++;
        printf("FAIL date_from_days %ld-%d-%d\n", y, m, d);
    }
    owf_date_from_days(-693593, &y, &m, &d);
    if (y != 1 || m != 1 || d != 1) {
        failures++;
        printf("FAIL date_from_days year 1: %ld-%d-%d\n", y, m, d);
    }
    if (!owf_parse_iso_date("2026-10-21T13:45:30", &v) || v != 46316 + 49530.0 / 86400.0) {
        failures++;
        printf("FAIL iso date %.17g\n", v);
    }
    checks += 6;
    if (!owf_parse_iso_date("1904-01-01", &v) || v != 1462 || owf_parse_iso_date("2026-13-01", &v) ||
        owf_parse_iso_date("2026-10-21junk", &v) || !owf_parse_iso_date("2026-10-21T00:00:00Z", &v)) {
        failures++;
        printf("FAIL iso dates\n");
    }
    if (!owf_parse_iso_duration("PT13H45M30S", &v) || v != 49530.0 / 86400.0) {
        failures++;
        printf("FAIL duration %.17g\n", v);
    }
    if (!owf_parse_iso_duration("PT36H00M00S", &v) || v != 1.5 || !owf_parse_iso_duration("-PT1H", &v) ||
        v != -1.0 / 24 || !owf_parse_iso_duration("P1DT2H", &v) || v != 1 + 2.0 / 24) {
        failures++;
        printf("FAIL durations\n");
    }
    if (owf_parse_iso_duration("P", &v) || owf_parse_iso_duration("PT1X", &v) || owf_parse_iso_duration("T1H", &v) ||
        owf_parse_iso_duration("P1H", &v)) {
        failures++;
        printf("FAIL bad durations\n");
    }
    owf_buf_init(&b);
    owf_put_iso_date(&b, 46316.5, 0);
    owf_buf_putc(&b, ' ');
    owf_put_iso_date(&b, 46316, 0);
    owf_buf_putc(&b, ' ');
    owf_put_iso_date(&b, 46316, 1);
    owf_buf_putc(&b, ' ');
    owf_put_iso_duration(&b, 1.5);
    owf_buf_putc(&b, ' ');
    owf_put_iso_duration(&b, -0.5 / 86400);
    owf_buf_putc(&b, ' ');
    owf_put_iso_date(&b, 46316 + 49530.25 / 86400.0, 0);
    same("put iso", "", (const char *)b.data,
         "2026-10-21T12:00:00 2026-10-21 2026-10-21T00:00:00 PT36H00M00S -PT00H00M00.500S 2026-10-21T13:45:30.250");
    owf_buf_free(&b);
}

static void test_numfmt_kind(void)
{
    static const struct {
        const char *format;
        int kind;
    } kinds[] = {
        { "General", OWF_NUMFMT_NUMBER }, { "0.00", OWF_NUMFMT_NUMBER }, { "#,##0.00 \"kg\"", OWF_NUMFMT_NUMBER },
        { "0%", OWF_NUMFMT_PERCENT }, { "0.00%", OWF_NUMFMT_PERCENT }, { "m/d/yyyy", OWF_NUMFMT_DATE },
        { "yyyy\\-mm\\-dd", OWF_NUMFMT_DATE }, { "d-mmm-yy", OWF_NUMFMT_DATE }, { "mmm", OWF_NUMFMT_DATE },
        { "h:mm", OWF_NUMFMT_TIME }, { "[h]:mm:ss", OWF_NUMFMT_TIME }, { "mm:ss", OWF_NUMFMT_TIME },
        { "[$-409]h:mm AM/PM", OWF_NUMFMT_TIME }, { "m/d/yyyy h:mm", OWF_NUMFMT_DATE },
        { "0.00E+00", OWF_NUMFMT_NUMBER }, { "@", OWF_NUMFMT_TEXT }, { "\"TRUE\";\"TRUE\";\"FALSE\"", OWF_NUMFMT_NUMBER },
        { "[Red]0.00;[Blue]-0.00", OWF_NUMFMT_NUMBER }, { "\"day\" 0", OWF_NUMFMT_NUMBER }, { NULL, 0 }
    };
    int i;
    for (i = 0; kinds[i].format; i++) {
        checks++;
        if (owf_numfmt_kind(kinds[i].format) != kinds[i].kind) {
            failures++;
            printf("FAIL numfmt kind %s: %d, want %d\n", kinds[i].format, owf_numfmt_kind(kinds[i].format), kinds[i].kind);
        }
    }
}

/* --parse: a line in, "bits text" out (or "no"), for Python to check */
static int parse_lines(void)
{
    static char line[8192];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strcspn(line, "\r\n");
        double v;
        line[n] = 0;
        if (owf_number_parse(line, n, &v)) {
            union {
                double d;
                unsigned char b[8];
            } u;
            char text[40];
            int i, little;
            u.d = 1.0;
            little = u.b[7] == 0x3F;
            u.d = v;
            for (i = 0; i < 8; i++)
                printf("%02x", u.b[little ? 7 - i : i]);
            owf_number_exact(v, text);
            printf(" %s\n", text);
        } else {
            printf("no\n");
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--parse"))
        return parse_lines();
    test_openformula();
    test_move();
    test_xlfn();
    test_numbers();
    test_dates();
    test_numfmt_kind();
    printf("%d of %d formula and number checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
