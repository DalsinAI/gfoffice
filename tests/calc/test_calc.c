/*
 * test_calc [FIXTURES...]: the formula engine's tests (make calc-test,
 * and make calc-check under AddressSanitizer and UBSan).
 *
 * The parser, every function, Excel's value and error rules, circular
 * references, typed input, inserting and deleting rows and columns,
 * moving formulas, number formats; then the oracle: each workbook given
 * (tests/book/fixtures/book.xls, tests/calc/fixtures/calc.xls) is read with
 * the results its writer saved, every formula is worked out again, and
 * the two are compared; a difference not on the known list fails. Last, a
 * speed note: 10,000 formulas worked out.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf.h"
#include "owf_calc.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int checks, failures;

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        checks++;                                                                                            \
        if (!(cond)) {                                                                                       \
            failures++;                                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                      \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
        }                                                                                                    \
    } while (0)

/* A value as the tests write it: 12.5, "text", TRUE, #DIV/0!, or nothing. */
static void show(const owf_cell *c, char *out, size_t size)
{
    char num[40];
    if (!c) {
        snprintf(out, size, "(none)");
        return;
    }
    switch (c->type) {
    case OWF_VALUE_NUMBER:
        owf_number_text(c->number, num);
        snprintf(out, size, "%s", num);
        break;
    case OWF_VALUE_TEXT:
        snprintf(out, size, "\"%s\"", c->text ? c->text : "");
        break;
    case OWF_VALUE_BOOL:
        snprintf(out, size, "%s", c->number ? "TRUE" : "FALSE");
        break;
    case OWF_VALUE_ERROR:
        snprintf(out, size, "%s", owf_error_name(c->error) ? owf_error_name(c->error) : "#?");
        break;
    default:
        snprintf(out, size, "(empty)");
        break;
    }
}

static owf_book *book;
static owf_calc *calc;

static void fresh(void)
{
    if (calc)
        owf_calc_free(calc);
    if (book)
        owf_book_free(book);
    book = owf_book_new();
    owf_book_add_sheet(book, "Sheet1");
    owf_book_add_sheet(book, "Sheet2");
    owf_book_add_sheet(book, "My 'Q' Sheet");
    calc = owf_calc_new(book);
}

static int at(const char *ref, unsigned *row, unsigned *col)
{
    if (!owf_calc_parse_cell(ref, row, col)) {
        printf("bad cell name in a test: %s\n", ref);
        exit(2);
    }
    return 1;
}

static int put_in(int sheet, const char *ref, const char *input)
{
    unsigned row, col;
    at(ref, &row, &col);
    return owf_calc_enter(calc, sheet, row, col, input);
}

static int put(const char *ref, const char *input)
{
    return put_in(0, ref, input);
}

static const char *get_in(int sheet, const char *ref)
{
    static char buf[4][512];
    static int k;
    unsigned row, col;
    char *out = buf[k++ & 3];
    at(ref, &row, &col);
    show(owf_sheet_find(&book->sheets[sheet], row, col), out, 512);
    return out;
}

static void recalc(void)
{
    int r = owf_calc_recalc(calc);
    CHECK(r == OWF_OK, "recalc: %d", r);
}

#define EXPECT_IN(sheet, ref, want)                                                                          \
    do {                                                                                                     \
        const char *got_ = get_in(sheet, ref);                                                               \
        CHECK(!strcmp(got_, want), "%s: got %s, want %s", ref, got_, want);                                  \
    } while (0)
#define EXPECT(ref, want) EXPECT_IN(0, ref, want)

/* A formula worked out in Sheet1!Z100. */
static const char *eval(const char *formula)
{
    static char buf[4][512];
    static int k;
    char *out = buf[k++ & 3];
    owf_cell r;
    int e = owf_calc_evaluate(calc, 0, 99, 25, formula, &r);
    if (e == OWF_ERR_FORMAT) {
        snprintf(out, 512, "(won't parse)");
        return out;
    }
    if (e != OWF_OK) {
        snprintf(out, 512, "(error %d)", e);
        return out;
    }
    show(&r, out, 512);
    owf_free(r.text);
    return out;
}

#define EVAL(formula, want)                                                                                  \
    do {                                                                                                     \
        const char *got_ = eval(formula);                                                                    \
        CHECK(!strcmp(got_, want), "%s: got %s, want %s", formula, got_, want);                              \
    } while (0)

/* ---- The parser ---- */

static void test_parser(void)
{
    static const char *const good[] = {
        "1", "1.5", ".5", "1E+20", "1e-5", "\"text\"", "\"a\"\"b\"", "TRUE", "false", "#N/A", "#DIV/0!",
        "A1", "$A$1", "a1", "A$1", "$A1", "XFD1048576", "A1:B2", "$A$1:$B$2", "A:A", "$A:$C", "1:1", "$2:$5",
        "Sheet2!A1", "Sheet2!A1:B2", "'My ''Q'' Sheet'!A1", "Sheet1:Sheet2!A1", "SUM(Sheet1:Sheet2!A1:B2)",
        "-1", "--1", "+1", "1%", "2^-1", "1+2*3-4/5^6&\"x\"", "1<2", "1<=2", "1<>2", "1>=2", "1>2", "1=2",
        "SUM(A1,)", "IF(A1,,2)", "SUM((A1,B1))", "SUM(A1:C3 B2:D4)", "SUM(A1:INDEX(A1:A5,2))",
        "{1,2;3,4}", "{-1,\"a\";TRUE,#N/A}", "_xlfn.IFERROR(1,2)", "PI()", "  1 +  2  ", "Unknown(1)",
        "SUM(A1:A3)%", "(((1)))", "#REF!", "Sheet2!#REF!", "[Other.xls]Sheet1!A1",
    };
    static const char *const bad[] = {
        "", "1+", "(1", "1)", "SUM(1", "SUM(1))", "\"open", "1 2", "A1 1", "{1,2;3}", "{A1}", "#WHAT",
        "SUM(1,,", ",", "*1", "IF(1)", "ROUND(1)", "ABS(1,2)", "'Sheet1!A1", "1..2", "A1:",
    };
    size_t i;
    char deep[400];
    fresh();
    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        char input[200];
        snprintf(input, sizeof input, "=%s", good[i]);
        CHECK(put("A1", input) == OWF_OK, "should parse: %s", good[i]);
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char input[200];
        snprintf(input, sizeof input, "=%s", bad[i]);
        if (!*bad[i])
            continue;                    /* "=" alone is text */
        CHECK(put("A1", input) == OWF_ERR_FORMAT, "should not parse: %s", bad[i]);
        CHECK(owf_calc_state(calc, 0, 0, 0) & OWF_CALC_BAD, "state BAD: %s", bad[i]);
        EXPECT("A1", "#NAME?");
    }
    /* typed formulas are kept as Excel keeps them */
    put("A2", "=sum(a1:b2)+true+if(1,#n/a)+'My ''Q'' Sheet'!c3+_xlfn.iferror(1,2)+myName+\"text\"+nosuch(1)");
    CHECK(!strcmp(owf_sheet_find(&book->sheets[0], 1, 0)->formula,
                  "SUM(A1:B2)+TRUE+IF(1,#N/A)+'My ''Q'' Sheet'!C3+_xlfn.IFERROR(1,2)+myName+\"text\"+NOSUCH(1)"),
          "normalised: %s", owf_sheet_find(&book->sheets[0], 1, 0)->formula);
    /* a formula that won't parse keeps its text */
    put("A1", "=1+");
    CHECK(!strcmp(owf_sheet_find(&book->sheets[0], 0, 0)->formula, "1+"), "text kept");
    /* nesting: 64 levels are fine, 70 are not */
    strcpy(deep, "");
    for (i = 0; i < 64; i++)
        strcat(deep, "(");
    strcat(deep, "1");
    for (i = 0; i < 64; i++)
        strcat(deep, ")");
    EVAL(deep, "1");
    strcpy(deep, "");
    for (i = 0; i < 70; i++)
        strcat(deep, "ABS(");
    strcat(deep, "1");
    for (i = 0; i < 70; i++)
        strcat(deep, ")");
    EVAL(deep, "(won't parse)");
    /* precedence, as Excel's */
    EVAL("-2^2", "4");
    EVAL("2^3^2", "64");
    EVAL("1+2*3", "7");
    EVAL("(1+2)*3", "9");
    EVAL("2*3%", "0.06");
    EVAL("-50%", "-0.5");
    EVAL("1&2+3", "\"15\"");
    EVAL("1+2=3", "TRUE");
    EVAL("2^-1", "0.5");
    EVAL("10-2-3", "5");
    EVAL("12/2/3", "2");
    EVAL("\"a\"&\"b\"=\"AB\"", "TRUE");
    EVAL("--\"3\"", "3");
    EVAL("+\"x\"", "\"x\"");
    EVAL("\"a\"\"b\"", "\"a\"b\"");
    EVAL("1E+20", "1E+20");
    EVAL("{1,2;3,4}", "1");
    EVAL("SUM({1,2;3,4})", "10");
    EVAL("_xlfn.IFERROR(1/0,7)", "7");
    EVAL("Unknown(1)", "#NAME?");
    EVAL("NoSuchName", "#NAME?");
    EVAL("NoSheet!A1", "#REF!");
    EVAL("[Other.xls]Sheet1!A1", "#REF!");
    EVAL("#REF!", "#REF!");
}

/* ---- References ---- */

static void test_references(void)
{
    fresh();
    put("A1", "1");
    put("A2", "2");
    put("A3", "3");
    put("B1", "10");
    put("B2", "20");
    put("B3", "30");
    put("C2", "100");
    put_in(1, "A1", "1000");
    put_in(1, "B2", "2000");
    put_in(2, "A1", "5");
    put_in(0, "D10", "7");
    EVAL("A1+$A$2+A$3", "6");
    EVAL("SUM(A1:B3)", "66");
    EVAL("SUM(B3:A1)", "66");
    EVAL("SUM(A:A)", "6");
    EVAL("SUM(A:B)", "66");
    EVAL("SUM(2:2)", "122");
    EVAL("SUM($1:$3)", "166");
    EVAL("Sheet2!A1", "1000");
    EVAL("SUM(Sheet2!A1:B2)", "3000");
    EVAL("'My ''Q'' Sheet'!A1*2", "10");
    EVAL("SUM(Sheet1:Sheet2!A1)", "1001");
    EVAL("SUM('Sheet1:My ''Q'' Sheet'!A1)", "1006");
    EVAL("SUM((A1,B2,C2))", "121");
    EVAL("SUM(A1:C3 B2:C3)", "150");
    EVAL("A1:C3 C2", "100");
    EVAL("A1:A2 C3:C4", "#NULL!");
    EVAL("SUM(A1:INDEX(B1:B3,2))", "33");
    EVAL("SUM(INDEX(A1:B3,0,2))", "60");
    EVAL("ROWS(A:A)", "1048576");
    EVAL("COLUMNS(1:1)", "16384");
    EVAL("D10", "7");
    EVAL("Z200", "0");
    /* defined names: the book's and a sheet's own */
    owf_book_add_name(book, "Total", -1, "SUM(Sheet1!$A$1:$A$3)", 0);
    owf_book_add_name(book, "Here", 0, "Sheet1!$B$1", 0);
    owf_book_add_name(book, "Here", 1, "Sheet2!$A$1", 0);
    owf_book_add_name(book, "Loop", -1, "Loop+1", 0);
    owf_book_add_name(book, "Broken", -1, "1+", 0);
    owf_calc_rebuild(calc);
    EVAL("Total*2", "12");
    EVAL("Here", "10");
    EVAL("Sheet2!Here", "1000");
    EVAL("Loop", "#NAME?");
    EVAL("Broken", "#NAME?");
    put("E1", "=Total");
    put("A1", "11");
    recalc();
    EXPECT("E1", "16");
    /* implicit intersection: a range where one value goes */
    put("F2", "=B1:B3*2");
    put("F5", "=B1:B3*2");
    put("G1", "=A1:C1");
    recalc();
    EXPECT("F2", "40");
    EXPECT("F5", "#VALUE!");
    EXPECT("G1", "#VALUE!");
}

/* ---- Values, conversions and errors ---- */

static void test_values(void)
{
    fresh();
    put("A1", "'12");
    put("A2", "abc");
    put("A3", "TRUE");
    put("A5", "=1/0");
    put("A6", "''");
    recalc();
    EVAL("A1+1", "13");
    EVAL("A2+1", "#VALUE!");
    EVAL("A3+1", "2");
    EVAL("A4+1", "1");
    EVAL("A4&\"x\"", "\"x\"");
    EVAL("A4=0", "TRUE");
    EVAL("A4=\"\"", "TRUE");
    EVAL("A4=FALSE", "TRUE");
    EVAL("A5+1", "#DIV/0!");
    EVAL("\"\"+1", "#VALUE!");
    EVAL("\"1,000\"*2", "2000");
    EVAL("\"50%\"*2", "1");
    EVAL("\"$5\"+1", "6");
    EVAL("\"(3)\"+0", "-3");
    EVAL("\"1/2/2026\"+0", "46054");
    EVAL("\"13:30\"*24", "13.5");
    EVAL("TRUE+TRUE", "2");
    EVAL("1&TRUE", "\"1TRUE\"");
    EVAL("0.1+0.2", "0.3");
    EVAL("0.1+0.2=0.3", "TRUE");
    EVAL("0.1+0.2-0.3", "0");
    EVAL("1/3", "0.333333333333333");
    EVAL("1E+308*10", "#NUM!");
    EVAL("0^0", "#NUM!");
    EVAL("0^-1", "#DIV/0!");
    EVAL("(-8)^(1/3)", "#NUM!");
    EVAL("(-2)^3", "-8");
    /* comparisons: numbers < text < booleans; text without case */
    EVAL("1<\"a\"", "TRUE");
    EVAL("\"z\"<TRUE", "TRUE");
    EVAL("\"abc\"=\"ABC\"", "TRUE");
    EVAL("\"a\"<\"B\"", "TRUE");
    EVAL("\"1\"=1", "FALSE");
    EVAL("FALSE<TRUE", "TRUE");
    /* errors: the left one first */
    EVAL("(1/0)+NA()", "#DIV/0!");
    EVAL("NA()+(1/0)", "#N/A");
    EVAL("NA()&\"x\"", "#N/A");
    EVAL("NA()=1", "#N/A");
    EVAL("SUM(1,NA())", "#N/A");
    EVAL("A5", "#DIV/0!");
    /* text of numbers */
    EVAL("1E+20&\"\"", "\"1E+20\"");
    EVAL("0.000001&\"\"", "\"0.000001\"");
    EVAL("123456789012345678&\"\"", "\"1.23456789012346E+17\"");
    /* limits: text past 32767 characters, arrays past the working memory */
    EVAL("LEN(REPT(\"a\",32767))", "32767");
    EVAL("REPT(\"a\",32768)", "#VALUE!");
    EVAL("LEN(REPT(\"ab\",16000)&REPT(\"c\",800))", "#VALUE!");
    EVAL("SUMPRODUCT((ROW(1:1000000)>0)*1)", "#NUM!");
}

/* ---- Every function ---- */

typedef struct {
    const char *formula, *want;
} case_t;

static const case_t function_cases[] = {
    /* maths */
    { "SUM(A1:A5)", "15" }, { "SUM(A1:A5,10,TRUE)", "26" }, { "SUM(B1:B5)", "0" }, { "SUM(D1:D3)", "0" },
    { "SUM(\"2\",3)", "5" }, { "SUM(\"x\")", "#VALUE!" }, { "SUM(D1:D5)", "#N/A" }, { "SUM()", "(won't parse)" },
    { "SUMIF(B1:B5,\"a\",C1:C5)", "50" }, { "SUMIF(A1:A5,\">2\")", "12" }, { "SUMIF(B1:B5,\"<>a\",C1:C5)", "100" },
    { "SUMIF(B1:B5,\"?\",C1:C5)", "150" }, { "SUMIF(A1:A5,3,C1:C5)", "30" }, { "SUMIF(B1:B5,\"A\",C1)", "50" },
    { "SUMIF(A1:A5,\">=\"&A4)", "9" }, { "SUM(SUMIF(B1:B5,{\"a\",\"b\"},C1:C5))", "120" },
    { "SUMIFS(C1:C5,B1:B5,\"a\",A1:A5,\">1\")", "40" }, { "SUMIFS(C1:C5,B1:B5,\"a\",A1:A4)", "#VALUE!" },
    { "SUMPRODUCT(A1:A5,C1:C5)", "550" }, { "SUMPRODUCT((B1:B5=\"a\")*C1:C5)", "50" },
    { "SUMPRODUCT(A1:A5,C1:C4)", "#VALUE!" }, { "SUMPRODUCT({1,2},{3,4})", "11" }, { "SUMPRODUCT(B1:B5)", "0" },
    { "AVERAGE(A1:A5)", "3" }, { "AVERAGE(B1:B5)", "#DIV/0!" }, { "AVERAGE(1,\"3\")", "2" },
    { "AVERAGEIF(B1:B5,\"b\",C1:C5)", "35" }, { "AVERAGEIF(A1:A5,\"<3\")", "1.5" },
    { "AVERAGEIF(B1:B5,\"z\",C1:C5)", "#DIV/0!" }, { "AVERAGEIFS(C1:C5,A1:A5,\">=2\",A1:A5,\"<=4\")", "30" },
    { "MIN(A1:A5)", "1" }, { "MIN(B1:B5)", "0" }, { "MIN(A1:A5,-1)", "-1" }, { "MAX(A1:A5)", "5" },
    { "MAX(C1:C5,\"70\")", "70" }, { "COUNT(A1:D5)", "10" }, { "COUNT(1,\"2\",\"x\",TRUE,NA())", "3" },
    { "COUNTA(A1:D5)", "19" }, { "COUNTA(1,\"\",NA())", "3" }, { "COUNTBLANK(A1:D6)", "5" },
    { "COUNTBLANK(G1:G3)", "2" }, { "COUNTIF(B1:B5,\"a\")", "2" }, { "COUNTIF(A1:A5,\">=3\")", "3" },
    { "COUNTIF(B1:B5,\"<>a\")", "3" }, { "COUNTIF(D1:D5,TRUE)", "2" }, { "COUNTIF(D1:D5,\"#N/A\")", "1" },
    { "COUNTIF(D1:D6,\"\")", "2" }, { "COUNTIF(A1:A5,\"3\")", "1" }, { "COUNTIF(D5,5)", "1" },
    { "COUNTIF(A:A,\"<>\")", "5" }, { "COUNTIF(H1:H10,\"\")", "9" }, { "COUNTIF(B1:B5,\"*\")", "5" },
    { "COUNTIF(B1:B5,\"~*\")", "0" }, { "COUNTIF({1,2,3},\">1\")", "2" },
    { "COUNTIFS(B1:B5,\"a\",C1:C5,\">10\")", "1" }, { "COUNTIFS(G1:G4,\"\",H1:H4,\"\")", "3" },
    { "ROUND(2.675,2)", "2.68" }, { "ROUND(-2.5,0)", "-3" }, { "ROUND(1234.5678,-2)", "1200" },
    { "ROUND(1.005,2)", "1.01" }, { "ROUND(0.285,2)", "0.29" }, { "ROUND(5,)", "5" }, { "ROUND(2.5,0.9)", "3" },
    { "ROUNDUP(3.2,0)", "4" }, { "ROUNDUP(-3.2,0)", "-4" }, { "ROUNDUP(0.1+0.2,1)", "0.3" },
    { "ROUNDUP(1234,-2)", "1300" }, { "ROUNDDOWN(3.7,0)", "3" }, { "ROUNDDOWN(-3.7,0)", "-3" },
    { "ROUNDDOWN(1234.5678,-2)", "1200" }, { "INT(-3.5)", "-4" }, { "INT(3.99)", "3" },
    { "INT(0.1+0.7+0.2)", "1" }, { "TRUNC(-3.99)", "-3" }, { "TRUNC(3.14159,3)", "3.141" },
    { "MOD(10,3)", "1" }, { "MOD(-10,3)", "2" }, { "MOD(10,-3)", "-2" }, { "MOD(5.5,2)", "1.5" },
    { "MOD(1,0)", "#DIV/0!" }, { "MOD(3.3,1.1)", "0" }, { "ABS(-4)", "4" }, { "ABS(\"x\")", "#VALUE!" },
    { "SIGN(-0.5)", "-1" }, { "SIGN(0)", "0" }, { "SIGN(3)", "1" }, { "SQRT(16)", "4" }, { "SQRT(-1)", "#NUM!" },
    { "POWER(2,10)", "1024" }, { "POWER(0,0)", "#NUM!" }, { "POWER(-8,1/3)", "#NUM!" }, { "EXP(1)", "2.71828182845905" },
    { "LN(10)", "2.30258509299405" }, { "LN(0)", "#NUM!" }, { "LOG(100)", "2" }, { "LOG(8,2)", "3" },
    { "LOG(10,1)", "#DIV/0!" }, { "LOG(-1)", "#NUM!" }, { "LOG10(1000)", "3" }, { "LOG10(0)", "#NUM!" },
    { "PI()", "3.14159265358979" }, { "SIN(PI()/2)", "1" }, { "COS(0)", "1" }, { "TAN(PI()/4)", "1" },
    { "ASIN(1)", "1.5707963267949" }, { "ASIN(2)", "#NUM!" }, { "ACOS(-1)", "3.14159265358979" },
    { "ACOS(1.5)", "#NUM!" }, { "ATAN(1)", "0.785398163397448" }, { "ATAN2(1,1)", "0.785398163397448" },
    { "ATAN2(-1,0)", "3.14159265358979" }, { "ATAN2(0,0)", "#DIV/0!" }, { "SINH(1)", "1.1752011936438" },
    { "COSH(1)", "1.54308063481524" }, { "TANH(0.5)", "0.46211715726001" }, { "DEGREES(PI())", "180" },
    { "RADIANS(180)", "3.14159265358979" }, { "AND(RAND()>=0,RAND()<1)", "TRUE" }, { "PRODUCT(A1:A5)", "120" },
    { "PRODUCT(2,\"3\")", "6" }, { "PRODUCT(B1:B5)", "0" }, { "MEDIAN(A1:A5)", "3" }, { "MEDIAN(1,2,3,4)", "2.5" },
    { "MEDIAN(B1:B5)", "#NUM!" }, { "STDEV(A1:A5)", "1.58113883008419" }, { "STDEV(1)", "#DIV/0!" },
    { "STDEVP(A1:A5)", "1.4142135623731" }, { "VAR(A1:A5)", "2.5" }, { "VARP(A1:A5)", "2" },
    { "VAR(C1:C5)", "250" }, { "SUMSQ(1,2,3)", "14" }, { "LARGE(C1:C5,2)", "40" }, { "LARGE(C1:C5,6)", "#NUM!" },
    { "SMALL(C1:C5,1)", "10" }, { "SMALL({3,1,2},2)", "2" }, { "CEILING(4.3,2)", "6" },
    { "CEILING(-4.3,-2)", "-6" }, { "CEILING(-4.3,2)", "-4" }, { "CEILING(4.3,-2)", "#NUM!" },
    { "FLOOR(4.3,2)", "4" }, { "FLOOR(-4.3,-2)", "-4" }, { "FLOOR(5,0)", "#DIV/0!" }, { "EVEN(3)", "4" },
    { "EVEN(-1.5)", "-2" }, { "EVEN(2)", "2" }, { "ODD(2)", "3" }, { "ODD(-2.1)", "-3" }, { "ODD(0)", "1" },
    { "FACT(5)", "120" }, { "FACT(0)", "1" }, { "FACT(-1)", "#NUM!" }, { "FACT(171)", "#NUM!" },
    /* logic */
    { "IF(A1>0,\"pos\",\"neg\")", "\"pos\"" }, { "IF(FALSE,1)", "FALSE" }, { "IF(TRUE,,1)", "0" },
    { "IF(\"TRUE\",1,2)", "1" }, { "IF(\"x\",1,2)", "#VALUE!" }, { "IF(D4,1,2)", "#N/A" },
    { "IF(D3,1,2)", "2" }, { "SUM(IF(TRUE,A1:A3))", "6" }, { "IFERROR(1/0,\"div\")", "\"div\"" },
    { "IFERROR(A1,\"x\")", "1" }, { "IFERROR(NA(),)", "0" }, { "IFNA(NA(),1)", "1" }, { "IFNA(1/0,1)", "#DIV/0!" },
    { "AND(TRUE,1)", "TRUE" }, { "AND(TRUE,0)", "FALSE" }, { "AND(B1:B5)", "#VALUE!" }, { "AND(D1:D2)", "TRUE" },
    { "AND(\"x\")", "#VALUE!" }, { "AND(D4)", "#N/A" }, { "OR(FALSE,0)", "FALSE" }, { "OR(A1:A2)", "TRUE" },
    { "OR({0,0,1})", "TRUE" }, { "NOT(0)", "TRUE" }, { "NOT(1)", "FALSE" }, { "NOT(\"x\")", "#VALUE!" },
    { "TRUE()", "TRUE" }, { "FALSE()", "FALSE" }, { "ISBLANK(D3)", "TRUE" }, { "ISBLANK(D6)", "FALSE" },
    { "ISBLANK(A1)", "FALSE" }, { "ISERROR(1/0)", "TRUE" }, { "ISERROR(A1)", "FALSE" }, { "ISERR(NA())", "FALSE" },
    { "ISERR(1/0)", "TRUE" }, { "ISNA(NA())", "TRUE" }, { "ISNA(1/0)", "FALSE" }, { "ISNUMBER(A1)", "TRUE" },
    { "ISNUMBER(D5)", "FALSE" }, { "ISTEXT(B1)", "TRUE" }, { "ISTEXT(A1)", "FALSE" }, { "ISLOGICAL(D1)", "TRUE" },
    { "ISNONTEXT(D3)", "TRUE" }, { "NA()", "#N/A" }, { "N(D1)", "1" }, { "N(B1)", "0" }, { "N(D4)", "#N/A" },
    { "T(B1)", "\"a\"" }, { "T(A1)", "\"\"" },
    /* text */
    { "LEN(\"Elder berry\")", "11" }, { "LEN(12345)", "5" }, { "LEN(\"Grüße\")", "5" }, { "LEN(D3)", "0" },
    { "LEFT(\"apple\",3)", "\"app\"" }, { "LEFT(\"apple\")", "\"a\"" }, { "LEFT(\"apple\",10)", "\"apple\"" },
    { "LEFT(\"apple\",-1)", "#VALUE!" }, { "LEFT(\"Grüße\",3)", "\"Grü\"" }, { "RIGHT(\"Banana\",2)", "\"na\"" },
    { "RIGHT(\"Grüße\",2)", "\"ße\"" }, { "RIGHT(\"ab\",5)", "\"ab\"" }, { "MID(\"Elder berry\",3,4)", "\"der \"" },
    { "MID(\"abc\",5,2)", "\"\"" }, { "MID(\"abc\",0,2)", "#VALUE!" }, { "MID(\"abc\",2,-1)", "#VALUE!" },
    { "UPPER(\"apple ß é\")", "\"APPLE ß É\"" }, { "LOWER(\"BaNaNa ÀÉ\")", "\"banana àé\"" },
    { "PROPER(\"hello wORLD 2nd o'neil\")", "\"Hello World 2Nd O'Neil\"" }, { "TRIM(\"  spaced   out  \")", "\"spaced out\"" },
    { "CONCATENATE(\"a\",1,TRUE)", "\"a1TRUE\"" }, { "CONCATENATE(B1,\" & \",B2)", "\"a & b\"" },
    { "CONCATENATE(NA())", "#N/A" }, { "TEXT(1234.5,\"#,##0.00\")", "\"1,234.50\"" }, { "TEXT(0.256,\"0.0%\")", "\"25.6%\"" },
    { "TEXT(46320,\"dd/mm/yyyy\")", "\"25/10/2026\"" }, { "TEXT(46320,\"dddd d mmmm yyyy\")", "\"Sunday 25 October 2026\"" },
    { "TEXT(0.573263888888889,\"h:mm:ss\")", "\"13:45:30\"" }, { "TEXT(-5,\"0.00;(0.00)\")", "\"(5.00)\"" },
    { "TEXT(\"12\",\"0.0\")", "\"12.0\"" }, { "TEXT(\"abc\",\"0.0\")", "\"abc\"" }, { "TEXT(42,\"000000\")", "\"000042\"" },
    { "VALUE(\"1,234.5\")", "1234.5" }, { "VALUE(\"50%\")", "0.5" }, { "VALUE(\"abc\")", "#VALUE!" },
    { "VALUE(\"2026-10-25\")", "46320" }, { "VALUE(TRUE)", "#VALUE!" }, { "VALUE(12)", "12" },
    { "FIND(\"a\",\"Banana\")", "2" }, { "FIND(\"A\",\"Banana\")", "#VALUE!" }, { "FIND(\"a\",\"Banana\",3)", "4" },
    { "FIND(\"\",\"abc\")", "1" }, { "FIND(\"a\",\"abc\",5)", "#VALUE!" }, { "FIND(\"ß\",\"Grüße\")", "4" },
    { "SEARCH(\"N\",\"Banana\")", "3" }, { "SEARCH(\"e*y\",\"Elder berry\")", "1" }, { "SEARCH(\"b?r\",\"Elder berry\")", "7" },
    { "SEARCH(\"~*\",\"a*b\")", "2" }, { "SEARCH(\"z\",\"abc\")", "#VALUE!" }, { "SEARCH(\"B\",\"abcb\",3)", "4" },
    { "SUBSTITUTE(\"a-b-c\",\"-\",\"+\")", "\"a+b+c\"" }, { "SUBSTITUTE(\"a-b-c\",\"-\",\"+\",2)", "\"a-b+c\"" },
    { "SUBSTITUTE(\"aaa\",\"a\",\"bb\")", "\"bbbbbb\"" }, { "SUBSTITUTE(\"abc\",\"\",\"x\")", "\"abc\"" },
    { "SUBSTITUTE(\"abc\",\"b\",\"x\",0)", "#VALUE!" }, { "REPT(\"ab\",3)", "\"ababab\"" }, { "REPT(\"ab\",0)", "\"\"" },
    { "REPT(\"ab\",-1)", "#VALUE!" }, { "EXACT(\"abc\",\"ABC\")", "FALSE" }, { "EXACT(\"abc\",\"abc\")", "TRUE" },
    { "REPLACE(\"abcdef\",2,3,\"X\")", "\"aXef\"" }, { "REPLACE(\"abc\",5,1,\"X\")", "\"abcX\"" }, { "CHAR(65)", "\"A\"" },
    { "CHAR(128)", "\"€\"" }, { "CHAR(0)", "#VALUE!" }, { "CODE(\"A\")", "65" }, { "CODE(\"€\")", "128" },
    { "CODE(\"\")", "#VALUE!" }, { "CLEAN(\"a\"&CHAR(9)&\"b\")", "\"ab\"" },
    /* lookup */
    { "VLOOKUP(2,E1:F3,2,FALSE)", "\"two\"" }, { "VLOOKUP(2.5,E1:F3,2)", "\"two\"" }, { "VLOOKUP(0,E1:F3,2)", "#N/A" },
    { "VLOOKUP(9,E1:F3,2)", "\"three\"" }, { "VLOOKUP(\"TWO\",F1:F3,1,FALSE)", "\"two\"" }, { "VLOOKUP(\"t*\",F1:F3,1,FALSE)", "\"two\"" },
    { "VLOOKUP(2,E1:F3,3,FALSE)", "#REF!" }, { "VLOOKUP(2,E1:F3,0,FALSE)", "#VALUE!" }, { "VLOOKUP(NA(),E1:F3,2)", "#N/A" },
    { "VLOOKUP(\"2\",E1:F3,2,FALSE)", "#N/A" }, { "VLOOKUP(2,{1,\"x\";2,\"y\"},2,FALSE)", "\"y\"" },
    { "HLOOKUP(2,{1,2,3;\"a\",\"b\",\"c\"},2,FALSE)", "\"b\"" }, { "HLOOKUP(2.5,{1,2,3;\"a\",\"b\",\"c\"},2)", "\"b\"" },
    { "HLOOKUP(5,{1,2,3;\"a\",\"b\",\"c\"},2,FALSE)", "#N/A" }, { "INDEX(E1:F3,2,2)", "\"two\"" },
    { "INDEX(F1:F3,3)", "\"three\"" }, { "INDEX(E1:F1,2)", "\"one\"" }, { "INDEX(E1:F3,4,1)", "#REF!" },
    { "SUM(INDEX(A1:C5,0,3))", "150" }, { "SUM(INDEX(A1:C5,2,0))", "22" }, { "INDEX({1,2;3,4},2,1)", "3" },
    { "SUM(INDEX({1,2;3,4},0,2))", "6" }, { "INDEX((A1:A2,C1:C2),1,1,2)", "10" }, { "INDEX(A1:A5,-1)", "#VALUE!" },
    { "MATCH(30,C1:C5,0)", "3" }, { "MATCH(35,C1:C5)", "3" }, { "MATCH(5,C1:C5)", "#N/A" }, { "MATCH(\"B\",B1:B5,0)", "2" },
    { "MATCH(\"t*\",F1:F3,0)", "2" }, { "MATCH(25,{50,40,30,20,10},-1)", "3" }, { "MATCH(1,A1:C5,0)", "#N/A" },
    { "MATCH(3,{1,2,3},0)", "3" }, { "CHOOSE(2,\"a\",\"b\",\"c\")", "\"b\"" }, { "CHOOSE(4,\"a\",\"b\")", "#VALUE!" },
    { "SUM(CHOOSE(2,A1:A5,C1:C5))", "150" }, { "CHOOSE(1.9,\"a\",\"b\")", "\"a\"" }, { "ROW()", "100" },
    { "ROW(C7)", "7" }, { "ROW(C7:D9)", "7" }, { "COLUMN()", "26" }, { "COLUMN(C7)", "3" }, { "ROWS(A1:C10)", "10" },
    { "COLUMNS(A1:C10)", "3" }, { "ROWS({1,2;3,4})", "2" }, { "COLUMNS(5)", "1" }, { "ROWS((A1,B2))", "#REF!" },
    /* dates */
    { "DATE(2026,10,25)", "46320" }, { "DATE(2026,14,1)", "46419" }, { "DATE(2026,1,0)", "46022" },
    { "DATE(99,1,1)", "36161" }, { "DATE(1900,2,29)", "60" }, { "DATE(1900,3,1)", "61" }, { "DATE(1900,1,1)", "1" },
    { "DATE(10000,1,1)", "#NUM!" }, { "DATE(2026,-1,1)", "45962" }, { "YEAR(46320)", "2026" }, { "MONTH(46320)", "10" },
    { "DAY(46320)", "25" }, { "DAY(60)", "29" }, { "MONTH(60)", "2" }, { "DAY(0)", "0" }, { "YEAR(-1)", "#NUM!" },
    { "YEAR(\"2026-10-25\")", "2026" }, { "WEEKDAY(46320)", "1" }, { "WEEKDAY(46320,2)", "7" }, { "WEEKDAY(46320,3)", "6" },
    { "WEEKDAY(46320,11)", "7" }, { "WEEKDAY(46320,16)", "2" }, { "WEEKDAY(46320,4)", "#NUM!" }, { "WEEKDAY(1)", "1" },
    { "TIME(13,45,30)", "0.573263888888889" }, { "TIME(25,0,0)", "0.0416666666666667" }, { "TIME(-1,0,0)", "#NUM!" },
    { "HOUR(0.573263888888889)", "13" }, { "MINUTE(0.573263888888889)", "45" }, { "SECOND(0.573263888888889)", "30" },
    { "HOUR(46023.2708333333)", "6" }, { "HOUR(\"13:45\")", "13" }, { "SECOND(0.99999999)", "0" },
    { "TODAY()=INT(NOW())", "TRUE" }, { "TODAY()>46000", "TRUE" },
    /* money */
    { "PMT(0.05/12,360,200000)", "-1073.64324602428" }, { "PMT(0,12,1200)", "-100" },
    { "PMT(0.06/12,60,-10000,0,1)", "192.366184372417" }, { "PMT(0.05,0,100)", "#NUM!" },
    /* to the last digit the exact values (LibreOffice's PV and FV differ from them in the 14th) */
    { "PV(0.08/12,240,-500)", "59777.145851188" }, { "PV(0,10,-100)", "1000" },
    { "FV(0.06/12,120,-200,-500,1)", "33849.4470751152" }, { "FV(0,10,-100)", "1000" },
    { "NPV(0.1,-10000,3000,4200,6800)", "1188.44341233522" }, { "NPV(0.08,A1:A3)", "5.02210028959" },
    { "NPV(-1,1)", "#DIV/0!" },
};

static void test_functions(void)
{
    size_t i;
    fresh();
    put("A1", "1");
    put("A2", "2");
    put("A3", "3");
    put("A4", "4");
    put("A5", "5");
    put("B1", "a");
    put("B2", "b");
    put("B3", "c");
    put("B4", "a");
    put("B5", "b");
    put("C1", "10");
    put("C2", "20");
    put("C3", "30");
    put("C4", "40");
    put("C5", "50");
    put("D1", "TRUE");
    put("D2", "TRUE");
    put("D4", "#N/A");
    put("D5", "'5");
    put("D6", "=\"\"");
    put("E1", "1");
    put("E2", "2");
    put("E3", "3");
    put("F1", "one");
    put("F2", "two");
    put("F3", "three");
    put("G2", "x");
    put("H2", "y");
    recalc();
    for (i = 0; i < sizeof function_cases / sizeof function_cases[0]; i++)
        EVAL(function_cases[i].formula, function_cases[i].want);
}

/* Every function in the engine's table has a case above. */
static void test_function_coverage(void)
{
    static const char *const names[] = {
        "ABS", "ACOS", "AND", "ASIN", "ATAN", "ATAN2", "AVERAGE", "AVERAGEIF", "AVERAGEIFS", "CEILING", "CHAR",
        "CHOOSE", "CLEAN", "CODE", "COLUMN", "COLUMNS", "CONCATENATE", "COS", "COSH", "COUNT", "COUNTA",
        "COUNTBLANK", "COUNTIF", "COUNTIFS", "DATE", "DAY", "DEGREES", "EVEN", "EXACT", "EXP", "FACT", "FALSE",
        "FIND", "FLOOR", "FV", "HLOOKUP", "HOUR", "IF", "IFERROR", "IFNA", "INDEX", "INT", "ISBLANK", "ISERR",
        "ISERROR", "ISLOGICAL", "ISNA", "ISNONTEXT", "ISNUMBER", "ISTEXT", "LARGE", "LEFT", "LEN", "LN", "LOG",
        "LOG10", "LOWER", "MATCH", "MAX", "MEDIAN", "MID", "MIN", "MINUTE", "MOD", "MONTH", "N", "NA", "NOT",
        "NOW", "NPV", "ODD", "OR", "PI", "PMT", "POWER", "PRODUCT", "PROPER", "PV", "RADIANS", "RAND", "REPLACE",
        "REPT", "RIGHT", "ROUND", "ROUNDDOWN", "ROUNDUP", "ROW", "ROWS", "SEARCH", "SECOND", "SIGN", "SIN",
        "SINH", "SMALL", "SQRT", "STDEV", "STDEVP", "SUBSTITUTE", "SUM", "SUMIF", "SUMIFS", "SUMPRODUCT", "SUMSQ",
        "T", "TAN", "TANH", "TEXT", "TIME", "TODAY", "TRIM", "TRUE", "TRUNC", "UPPER", "VALUE", "VAR", "VARP",
        "VLOOKUP", "WEEKDAY", "YEAR",
    };
    size_t i, k;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t n = strlen(names[i]);
        int found = 0;
        for (k = 0; k < sizeof function_cases / sizeof function_cases[0] && !found; k++) {
            const char *p = function_cases[k].formula;
            while ((p = strstr(p, names[i])) != NULL) {
                if ((p == function_cases[k].formula || !((p[-1] >= 'A' && p[-1] <= 'Z') || p[-1] == '.')) && p[n] == '(') {
                    found = 1;
                    break;
                }
                p++;
            }
        }
        CHECK(found, "no test case for %s", names[i]);
        /* and the engine has it: an unknown name would be #NAME? */
    }
    fresh();
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        char f[64];
        snprintf(f, sizeof f, "ISERROR(%s())", names[i]);
        /* a function the engine has either takes no arguments or refuses
           too few; only an unknown one compiles to #NAME? with none */
        if (strcmp(eval(f), "(won't parse)") && strcmp(names[i], "PI") && strcmp(names[i], "TRUE") &&
            strcmp(names[i], "FALSE") && strcmp(names[i], "NA") && strcmp(names[i], "NOW") &&
            strcmp(names[i], "TODAY") && strcmp(names[i], "RAND") && strcmp(names[i], "ROW") &&
            strcmp(names[i], "COLUMN"))
            CHECK(0, "%s() with no arguments should not parse", names[i]);
    }
}

/* ---- Array formulas ---- */

static void test_arrays(void)
{
    owf_cell *c;
    unsigned r;
    fresh();
    put("A1", "1");
    put("A2", "2");
    put("A3", "3");
    put("B1", "10");
    put("B2", "20");
    put("B3", "30");
    put("C1", "x");
    put("C2", "y");
    put("C3", "x");
    /* an array formula's cells are made by the importers; set them here as one does */
    for (r = 0; r < 3; r++) {
        c = owf_sheet_cell(&book->sheets[0], r, 4, 1);
        owf_cell_set_formula(c, "A1:A3*B1:B3");
        c->flags |= OWF_CELL_ARRAY;
    }
    c = owf_sheet_cell(&book->sheets[0], 0, 5, 1);
    owf_cell_set_formula(c, "SUM(IF(C1:C3=\"x\",B1:B3))");
    c->flags |= OWF_CELL_ARRAY;
    c = owf_sheet_cell(&book->sheets[0], 1, 5, 1);
    owf_cell_set_formula(c, "SUM(LEN(C1:C3)*A1:A3)");
    c->flags |= OWF_CELL_ARRAY;
    c = owf_sheet_cell(&book->sheets[0], 2, 5, 1);
    owf_cell_set_formula(c, "MAX(ABS(A1:A3-3))");
    c->flags |= OWF_CELL_ARRAY;
    c = owf_sheet_cell(&book->sheets[0], 3, 5, 1);
    owf_cell_set_formula(c, "{1,2,3}*2");
    c->flags |= OWF_CELL_ARRAY;
    for (r = 4; r < 8; r++) {
        c = owf_sheet_cell(&book->sheets[0], r, 5, 1);
        owf_cell_set_formula(c, "ROW(A1:A3)");
        c->flags |= OWF_CELL_ARRAY;
    }
    owf_calc_rebuild(calc);
    recalc();
    EXPECT("E1", "10");
    EXPECT("E2", "40");
    EXPECT("E3", "90");
    EXPECT("F1", "40");
    EXPECT("F2", "6");
    EXPECT("F3", "2");
    EXPECT("F4", "2");
    EXPECT("F5", "1");
    EXPECT("F6", "2");
    EXPECT("F7", "3");
    EXPECT("F8", "#N/A");
    put("B2", "5");
    recalc();
    EXPECT("E2", "10");
    EXPECT("F1", "40");
    /* array constants work element by element anywhere */
    EVAL("SUM({1,2,3}*{4,5,6})", "32");
    EVAL("SUM({1;2}*{1,2,3})", "18");
    EVAL("SUM({1,2}+{1,2,3})", "#N/A");
    EVAL("SUMPRODUCT(LEN(C1:C3))", "3");
    EVAL("SUMPRODUCT(ABS(A1:A3-3))", "3");
    EVAL("SUMPRODUCT((C1:C3=\"x\")*(A1:A3>1)*B1:B3)", "30");
}

/* ---- Recalculation ---- */

static int changes;

static void count_change(void *user, int sheet, unsigned row, unsigned col)
{
    (void)user;
    (void)sheet;
    (void)row;
    (void)col;
    changes++;
}

static void test_recalc(void)
{
    int i;
    char ref[16], f[64];
    fresh();
    owf_calc_set_notify(calc, count_change, NULL);
    put("A1", "1");
    put("B1", "=A1*2");
    put("C1", "=B1+A1");
    put("D1", "=SUM(A1:C1)");
    put("E1", "=Sheet2!A1+D1");
    put_in(1, "A1", "=Sheet1!C1*10");
    CHECK(owf_calc_state(calc, 0, 0, 1) & OWF_CALC_DIRTY, "B1 dirty after typing");
    changes = 0;
    recalc();
    EXPECT("B1", "2");
    EXPECT("C1", "3");
    EXPECT("D1", "6");
    EXPECT_IN(1, "A1", "30");
    EXPECT("E1", "36");
    CHECK(changes == 5, "5 changed, got %d", changes);
    CHECK(!(owf_calc_state(calc, 0, 0, 1) & OWF_CALC_DIRTY), "B1 clean after recalc");
    /* only what reads a changed cell is worked out again */
    put("Z1", "5");
    put("Y1", "=Z1+1");
    recalc();
    changes = 0;
    put("A1", "2");
    CHECK(!(owf_calc_state(calc, 0, 0, 24) & OWF_CALC_DIRTY), "Y1 not dirty");
    recalc();
    CHECK(changes == 5, "5 changed again, got %d", changes);
    EXPECT("E1", "72");
    /* a value that comes out the same is no change */
    changes = 0;
    put("Z1", "5");
    recalc();
    CHECK(changes == 0, "no change, got %d", changes);
    /* clearing a cell */
    put("A1", "");
    recalc();
    EXPECT("A1", "(empty)");
    EXPECT("D1", "0");
    /* a formula replaced by a value, and a value by a formula */
    put("B1", "100");
    recalc();
    EXPECT("C1", "100");
    put("B1", "=7");
    recalc();
    EXPECT("C1", "7");
    /* volatile: worked out at every recalc */
    put("F1", "=RAND()");
    put("G1", "=F1*0+1");
    recalc();
    CHECK(owf_calc_state(calc, 0, 0, 5) & OWF_CALC_VOLATILE, "RAND volatile");
    {
        double first = owf_sheet_find(&book->sheets[0], 0, 5)->number;
        recalc();
        CHECK(owf_sheet_find(&book->sheets[0], 0, 5)->number != first, "RAND again");
    }
    /* a long chain: no recursion, no trouble */
    fresh();
    put("A1", "1");
    for (i = 2; i <= 20000; i++) {
        snprintf(ref, sizeof ref, "A%d", i);
        snprintf(f, sizeof f, "=A%d+1", i - 1);
        put(ref, f);
    }
    recalc();
    EXPECT("A20000", "20000");
    put("A1", "0");
    recalc();
    EXPECT("A20000", "19999");
    /* a chain typed from the bottom up: the order is the dependencies', not the cells' */
    fresh();
    for (i = 1000; i >= 2; i--) {
        snprintf(ref, sizeof ref, "B%d", i);
        snprintf(f, sizeof f, "=B%d*1+1", i - 1);
        put(ref, f);
    }
    put("B1", "1");
    recalc();
    EXPECT("B1000", "1000");
    /* recalc_all works out everything, dirty or not */
    fresh();
    put("A1", "2");
    put("B1", "=A1*3");
    recalc();
    owf_sheet_cell(&book->sheets[0], 0, 0, 0)->number = 5;    /* behind the engine's back */
    recalc();
    EXPECT("B1", "6");
    CHECK(owf_calc_recalc_all(calc) == OWF_OK, "recalc_all");
    EXPECT("B1", "15");
}

/* ---- Circular references ---- */

static void test_circular(void)
{
    int i;
    char ref[16], f[64];
    fresh();
    put("A1", "=B1+1");
    put("B1", "=A1+1");
    put("C1", "=A1*2");
    put("D1", "=ISERROR(A1)");
    put("E1", "5");
    put("F1", "=E1*2");
    put("G1", "=G1+1");
    put("H1", "=SUM(H2:H5)");
    put("H3", "=H1");
    recalc();
    EXPECT("A1", "#REF!");
    EXPECT("B1", "#REF!");
    EXPECT("C1", "#REF!");
    EXPECT("D1", "TRUE");
    EXPECT("F1", "10");
    EXPECT("G1", "#REF!");
    EXPECT("H1", "#REF!");
    EXPECT("H3", "#REF!");
    CHECK(owf_calc_state(calc, 0, 0, 0) & OWF_CALC_CIRCULAR, "A1 circular");
    CHECK(!(owf_calc_state(calc, 0, 0, 2) & OWF_CALC_CIRCULAR), "C1 reads the loop, not in it");
    CHECK(owf_calc_circular_count(calc) == 5, "5 in loops, got %d", owf_calc_circular_count(calc));
    /* breaking the loop */
    put("B1", "1");
    recalc();
    EXPECT("A1", "2");
    EXPECT("C1", "4");
    EXPECT("D1", "FALSE");
    CHECK(!(owf_calc_state(calc, 0, 0, 0) & OWF_CALC_CIRCULAR), "A1 no longer circular");
    /* ROW and COLUMN of the formula's own cell are no loop */
    put("J1", "=ROW(J1)+COLUMNS(J1:K1)");
    recalc();
    EXPECT("J1", "3");
    /* a big loop, 5000 long, and a recalc_all over it */
    fresh();
    for (i = 1; i <= 5000; i++) {
        snprintf(ref, sizeof ref, "A%d", i);
        snprintf(f, sizeof f, "=A%d+1", i == 5000 ? 1 : i + 1);
        put(ref, f);
    }
    put("B1", "=A2500");
    recalc();
    EXPECT("A1", "#REF!");
    EXPECT("A5000", "#REF!");
    EXPECT("B1", "#REF!");
    CHECK(owf_calc_circular_count(calc) == 5000, "5000 in the loop, got %d", owf_calc_circular_count(calc));
    CHECK(owf_calc_recalc_all(calc) == OWF_OK, "recalc_all with a loop");
}

/* ---- Typed input ---- */

static const char *format_of(const char *ref)
{
    unsigned row, col;
    const owf_cell *c;
    at(ref, &row, &col);
    c = owf_sheet_find(&book->sheets[0], row, col);
    if (!c)
        return "(none)";
    return book->numfmts[book->styles[c->style].number_format];
}

static void test_enter(void)
{
    fresh();
    put("A1", "12.5");
    put("A2", "-3");
    put("A3", "1,234.5");
    put("A4", "1E3");
    put("A5", "(12)");
    put("A6", "50%");
    put("A7", "\xC2\xA3" "12.50");
    put("A8", "25/10/2026");
    put("A9", "2026-10-25");
    put("A10", "25-Oct-2026");
    put("A11", "13:45");
    put("A12", "1:45 PM");
    put("A13", "25/10/2026 13:45");
    put("A14", "true");
    put("A15", "#N/A");
    put("A16", "'0123");
    put("A17", "hello");
    put("A18", "12,34");
    put("A19", "31/02/2026");
    put("A20", "=");
    put("A21", "$3");
    put("A22", "12.5%");
    EXPECT("A1", "12.5");
    EXPECT("A2", "-3");
    EXPECT("A3", "1234.5");
    EXPECT("A4", "1000");
    EXPECT("A5", "-12");
    EXPECT("A6", "0.5");
    EXPECT("A7", "12.5");
    EXPECT("A8", "46320");
    EXPECT("A9", "46320");
    EXPECT("A10", "46320");
    EXPECT("A11", "0.572916666666667");
    EXPECT("A12", "0.572916666666667");
    EXPECT("A13", "46320.5729166667");
    EXPECT("A14", "TRUE");
    EXPECT("A15", "#N/A");
    EXPECT("A16", "\"0123\"");
    EXPECT("A17", "\"hello\"");
    EXPECT("A18", "\"12,34\"");
    EXPECT("A19", "\"31/02/2026\"");
    EXPECT("A20", "\"=\"");
    EXPECT("A21", "3");
    EXPECT("A22", "0.125");
    CHECK(!strcmp(format_of("A1"), "General"), "A1 General: %s", format_of("A1"));
    CHECK(!strcmp(format_of("A6"), "0%"), "A6: %s", format_of("A6"));
    CHECK(!strcmp(format_of("A22"), "0.00%"), "A22: %s", format_of("A22"));
    CHECK(!strcmp(format_of("A7"), "\xC2\xA3#,##0.00"), "A7: %s", format_of("A7"));
    CHECK(!strcmp(format_of("A21"), "$#,##0"), "A21: %s", format_of("A21"));
    CHECK(!strcmp(format_of("A8"), "dd/mm/yyyy"), "A8: %s", format_of("A8"));
    CHECK(!strcmp(format_of("A9"), "yyyy-mm-dd"), "A9: %s", format_of("A9"));
    CHECK(!strcmp(format_of("A10"), "dd-mmm-yy"), "A10: %s", format_of("A10"));
    CHECK(!strcmp(format_of("A11"), "h:mm"), "A11: %s", format_of("A11"));
    CHECK(!strcmp(format_of("A12"), "h:mm AM/PM"), "A12: %s", format_of("A12"));
    CHECK(!strcmp(format_of("A13"), "dd/mm/yyyy h:mm"), "A13: %s", format_of("A13"));
    /* month first */
    owf_calc_set_date_order(calc, OWF_CALC_MDY);
    put("B1", "10/25/2026");
    put("B2", "25/10/2026");
    EXPECT("B1", "46320");
    EXPECT("B2", "\"25/10/2026\"");
    CHECK(!strcmp(format_of("B1"), "mm/dd/yyyy"), "B1: %s", format_of("B1"));
    owf_calc_set_date_order(calc, OWF_CALC_DMY);
    /* a format already set stays */
    put("C1", "50%");
    put("C1", "25/10/2026");
    CHECK(!strcmp(format_of("C1"), "0%"), "C1 keeps 0%%: %s", format_of("C1"));
    /* errors in the call */
    CHECK(owf_calc_enter(calc, 7, 0, 0, "1") == OWF_ERR_UNSUPPORTED, "no sheet 7");
    CHECK(owf_calc_enter(calc, 0, 2000000, 0, "1") == OWF_ERR_UNSUPPORTED, "no row 2000001");
    CHECK(owf_calc_enter(calc, 0, 500, 500, "") == OWF_OK, "clearing nothing");
    CHECK(!owf_sheet_find(&book->sheets[0], 500, 500), "and no cell made");
}

/* ---- Moving formulas ---- */

static void test_move(void)
{
    static const struct {
        const char *text;
        int drow, dcol;
        const char *want;
    } cases[] = {
        { "A1+B2", 1, 1, "B2+C3" },
        { "$A$1+A$1+$A1", 2, 3, "$A$1+D$1+$A3" },
        { "SUM(A1:B2)", 3, 0, "SUM(A4:B5)" },
        { "SUM($A1:B$2)", 1, 1, "SUM($A2:C$2)" },
        { "SUM(A:B)", 5, 2, "SUM(C:D)" },
        { "SUM($A:B)", 5, 2, "SUM($A:D)" },
        { "SUM(1:2)", 3, 9, "SUM(4:5)" },
        { "A1", -1, 0, "#REF!" },
        { "Sheet2!A1*2", 0, 1, "Sheet2!B1*2" },
        { "'My Sheet'!A1:A2", 1, 0, "'My Sheet'!A2:A3" },
        { "Sheet2!A1", -1, 0, "Sheet2!#REF!" },
        { "\"A1\"&A1", 1, 0, "\"A1\"&A2" },
        { "SUM(a1)", 1, 0, "SUM(A2)" },
        { "LOG10(A1)", 1, 0, "LOG10(A2)" },
        { "Total+A1", 0, 1, "Total+B1" },
        { "=A1", 1, 0, "=A2" },
        { "XFD1", 0, 1, "#REF!" },
        { "A1+", 1, 0, "A1+" }, { "SUM(A1", 1, 0, "SUM(A1" },
        { "A1+B1", 0, 0, "A1+B1" },
    };
    size_t i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char *got = owf_calc_move_formula(cases[i].text, cases[i].drow, cases[i].dcol);
        CHECK(got && !strcmp(got, cases[i].want), "move %s by %d,%d: got %s, want %s", cases[i].text, cases[i].drow,
              cases[i].dcol, got ? got : "(null)", cases[i].want);
        owf_free(got);
    }
}

/* ---- Inserting and deleting rows and columns ---- */

static const char *formula_at(int sheet, const char *ref)
{
    unsigned row, col;
    const owf_cell *c;
    at(ref, &row, &col);
    c = owf_sheet_find(&book->sheets[sheet], row, col);
    return c && c->formula ? c->formula : "(none)";
}

#define FORMULA(sheet, ref, want)                                                                           \
    CHECK(!strcmp(formula_at(sheet, ref), want), "%s: formula %s, want %s", ref, formula_at(sheet, ref), want)

static void test_shift(void)
{
    owf_range m;
    fresh();
    put("A1", "1");
    put("A2", "2");
    put("A3", "3");
    put("A4", "4");
    put("B1", "=SUM(A1:A4)");
    put("B2", "=A3*10");
    put("B3", "=$A$4");
    put("B4", "=A2");
    put("C1", "=SUM(A:A)");
    put("D1", "=SUM(2:3)");
    put_in(1, "A1", "=Sheet1!A3+Sheet1!A1");
    owf_book_add_name(book, "Part", -1, "Sheet1!$A$2:$A$3", 0);
    owf_sheet_set_row(&book->sheets[0], 2, 400, 0);
    m.first_row = 1;
    m.last_row = 2;
    m.first_col = 4;
    m.last_col = 5;
    owf_sheet_merge(&book->sheets[0], &m);
    owf_calc_rebuild(calc);
    put("E5", "=SUM(Part)");
    recalc();
    EXPECT("B1", "10");
    EXPECT("E5", "5");
    /* two rows in at row 2 (index 1) */
    CHECK(owf_calc_shift(calc, 0, 0, 1, 2) == OWF_OK, "insert rows");
    FORMULA(0, "B1", "SUM(A1:A6)");
    FORMULA(0, "B4", "A5*10");
    FORMULA(0, "B5", "$A$6");
    FORMULA(0, "B6", "A4");
    FORMULA(0, "C1", "SUM(A:A)");
    FORMULA(0, "D1", "SUM(4:5)");
    FORMULA(1, "A1", "Sheet1!A5+Sheet1!A1");
    FORMULA(0, "E7", "SUM(Part)");
    CHECK(!strcmp(book->names[0].formula, "Sheet1!$A$4:$A$5"), "name: %s", book->names[0].formula);
    CHECK(book->sheets[0].rows[0].row == 4, "row height moved to %u", book->sheets[0].rows[0].row);
    CHECK(book->sheets[0].merges[0].first_row == 3 && book->sheets[0].merges[0].last_row == 4, "merge moved");
    recalc();
    EXPECT("A4", "2");
    EXPECT("B1", "10");
    EXPECT("B4", "30");
    EXPECT_IN(1, "A1", "4");
    EXPECT("E7", "5");
    /* delete rows 4 and 5 (index 3, two): the name's range goes, references into it break */
    CHECK(owf_calc_shift(calc, 0, 0, 3, -2) == OWF_OK, "delete rows");
    FORMULA(0, "B1", "SUM(A1:A4)");
    FORMULA(0, "B4", "#REF!");
    FORMULA(0, "B3", "(none)");
    FORMULA(1, "A1", "Sheet1!#REF!+Sheet1!A1");
    FORMULA(0, "D1", "SUM(#REF!)");
    CHECK(!strcmp(book->names[0].formula, "Sheet1!#REF!"), "name: %s", book->names[0].formula);
    recalc();
    EXPECT("B1", "5");
    EXPECT_IN(1, "A1", "#REF!");
    EXPECT("E5", "#REF!");
    /* a range losing its first row shrinks */
    fresh();
    put("A1", "1");
    put("A2", "2");
    put("A3", "3");
    put("B1", "=SUM(A2:A3)");
    put("B5", "=SUM(A1:A3)");
    CHECK(owf_calc_shift(calc, 0, 0, 1, -1) == OWF_OK, "delete row 2");
    FORMULA(0, "B1", "SUM(A2:A2)");
    FORMULA(0, "B4", "SUM(A1:A2)");
    recalc();
    EXPECT("B1", "3");
    EXPECT("B4", "4");
    /* columns */
    fresh();
    put("A1", "1");
    put("B1", "2");
    put("C1", "3");
    put("D1", "=SUM(A1:C1)");
    put("E1", "=C1*2+SUM(B:B)");
    put("A2", "=B1");
    CHECK(owf_calc_shift(calc, 0, 1, 1, 1) == OWF_OK, "insert column");
    FORMULA(0, "E1", "SUM(A1:D1)");
    FORMULA(0, "F1", "D1*2+SUM(C:C)");
    FORMULA(0, "A2", "C1");
    recalc();
    EXPECT("E1", "6");
    EXPECT("F1", "8");
    CHECK(owf_calc_shift(calc, 0, 1, 2, -1) == OWF_OK, "delete column C");
    FORMULA(0, "D1", "SUM(A1:C1)");
    FORMULA(0, "E1", "C1*2+SUM(#REF!)");
    FORMULA(0, "A2", "#REF!");
    recalc();
    EXPECT("D1", "4");
    EXPECT("A2", "#REF!");
    /* nothing pushed off the sheet */
    put("A1048576", "1");
    CHECK(owf_calc_shift(calc, 0, 0, 0, 1) == OWF_ERR_UNSUPPORTED, "no room");
    CHECK(owf_calc_shift(calc, 9, 0, 0, 1) == OWF_ERR_UNSUPPORTED, "no sheet");
}

/* ---- Unknown functions and kept values ---- */

static void test_kept(void)
{
    owf_cell *c;
    fresh();
    put("A1", "4");
    c = owf_sheet_cell(&book->sheets[0], 0, 1, 1);
    owf_cell_set_formula(c, "GAMMALN(A1)");
    owf_cell_set_number(c, 1.79175946922805);
    c = owf_sheet_cell(&book->sheets[0], 0, 2, 1);
    owf_cell_set_formula(c, "[Other.xls]Sheet1!A1");
    owf_cell_set_number(c, 99);
    c = owf_sheet_cell(&book->sheets[0], 0, 3, 1);
    owf_cell_set_formula(c, "B1*2");
    owf_cell_set_number(c, 0);
    owf_calc_rebuild(calc);
    CHECK(owf_calc_recalc_all(calc) == OWF_OK, "recalc_all");
    EXPECT("B1", "1.79175946922805");
    EXPECT("C1", "99");
    EXPECT("D1", "3.5835189384561");
    CHECK(owf_calc_state(calc, 0, 0, 1) & OWF_CALC_KEPT, "B1 kept");
    CHECK(owf_calc_state(calc, 0, 0, 2) & OWF_CALC_KEPT, "C1 kept");
    /* typed again, the cell is the user's: #NAME? */
    put("B1", "=GAMMALN(A1)");
    recalc();
    EXPECT("B1", "#NAME?");
    EXPECT("D1", "#NAME?");
    CHECK(!(owf_calc_state(calc, 0, 0, 1) & OWF_CALC_KEPT), "B1 no longer kept");
}

/* ---- Number formats ---- */

static void test_formats(void)
{
    static const struct {
        const char *format;
        double value;
        const char *want;
    } cases[] = {
        { "General", 1234.5, "1234.5" }, { "General", 0.1 + 0.2, "0.3" }, { "General", 1e20, "1E+20" },
        { "General", -0.5, "-0.5" }, { "0", 2.5, "3" }, { "0", -2.5, "-3" }, { "0.00", 2.675, "2.68" },
        { "0.00", 1e15, "1000000000000000.00" }, { "#,##0", 1234567.89, "1,234,568" }, { "#,##0.00", 1234.5, "1,234.50" },
        { "#,##0.00", -1234.5, "-1,234.50" }, { "#,##0", 0, "0" }, { "#,###", 0, "" }, { "0%", 0.256, "26%" },
        { "0.00%", 0.256, "25.60%" }, { "0.00E+00", 1234567, "1.23E+06" }, { "0.00E+00", 0.000123, "1.23E-04" },
        { "##0.0E+0", 12345, "12.3E+3" }, { "0.0", -0.04, "0.0" }, { "0.0", -0.06, "-0.1" }, { "#.##", 3, "3." },
        { "0.0#", 1.5, "1.5" }, { "0.0#", 1.567, "1.57" }, { "00000", 42, "00042" }, { "#,##0,", 1234567, "1,235" },
        { "0.0,,", 1234567, "1.2" }, { "\"$\"#,##0.00", 5, "$5.00" }, { "$#,##0.00", -5, "-$5.00" },
        { "#,##0.00;(#,##0.00)", -5, "(5.00)" }, { "#,##0.00;[Red](#,##0.00)", -5, "(5.00)" },
        { "0.00;-0.00;\"zero\"", 0, "zero" }, { "[>100]\"big\";[<=100]\"small\"", 150, "big" },
        { "[>100]\"big\";[<=100]\"small\"", 50, "small" }, { "0.00_);(0.00)", 5, "5.00 " }, { "@", 5, "5" },
        { "# ?/?", 1.5, "1 1/2" }, { "# ?\?/?\?", 0.333, "1/3" }, { "?/4", 0.75, "3/4" },
        { "dd/mm/yyyy", 46320, "25/10/2026" }, { "m/d/yyyy", 46320, "10/25/2026" }, { "d-mmm-yy", 46320, "25-Oct-26" },
        { "d-mmm", 46320, "25-Oct" }, { "mmm-yy", 46320, "Oct-26" }, { "dddd, mmmm d, yyyy", 46320, "Sunday, October 25, 2026" },
        { "ddd d mmmmm", 46320, "Sun 25 O" }, { "yyyy-mm-dd", 60, "1900-02-29" }, { "yyyy-mm-dd", 0, "1900-01-00" },
        { "h:mm", 0.573263888888889, "13:45" }, { "h:mm:ss", 0.573263888888889, "13:45:30" },
        { "hh:mm AM/PM", 0.573263888888889, "01:45 PM" }, { "h:mm a/p", 0.25, "6:00 a" },
        { "[h]:mm", 1.5, "36:00" }, { "[mm]:ss", 0.04166666666, "60:00" }, { "mm:ss.0", 0.000011574, "00:01.0" },
        { "h:mm:ss", 0.99998, "23:59:58" }, { "h:mm:ss", 0.999999, "0:00:00" }, { "h:mm", 0.9999999, "0:00" }, { "dd/mm/yyyy h:mm", 46320.5, "25/10/2026 12:00" },
        { "yyyy", -1, "########" }, { "[$\xE2\x82\xAC-407] #,##0.00", 3.5, "\xE2\x82\xAC 3.50" },
        { "0.00 \"kg\"", 2, "2.00 kg" }, { "\\a0", 7, "a7" },
    };
    size_t i;
    char out[128];
    owf_cell v;
    unsigned long colour;
    memset(&v, 0, sizeof v);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        v.type = OWF_VALUE_NUMBER;
        v.number = cases[i].value;
        owf_calc_format_value(cases[i].format, &v, 0, 0, out, sizeof out, NULL);
        CHECK(!strcmp(out, cases[i].want), "format %s of %.15g: got \"%s\", want \"%s\"", cases[i].format,
              cases[i].value, out, cases[i].want);
    }
    /* colours */
    v.type = OWF_VALUE_NUMBER;
    v.number = -5;
    owf_calc_format_value("#,##0;[Red]-#,##0", &v, 0, 0, out, sizeof out, &colour);
    CHECK(!strcmp(out, "-5") && colour == 0xFF0000, "red: %s %06lX", out, colour);
    v.number = 5;
    owf_calc_format_value("#,##0;[Red]-#,##0", &v, 0, 0, out, sizeof out, &colour);
    CHECK(colour == OWF_COLOUR_AUTO, "auto colour");
    owf_calc_format_value("[Blue]0", &v, 0, 0, out, sizeof out, &colour);
    CHECK(colour == 0x0000FF && !strcmp(out, "5"), "blue");
    /* text, booleans, errors, nothing */
    v.type = OWF_VALUE_TEXT;
    v.text = (char *)"abc";
    owf_calc_format_value("0.00", &v, 0, 0, out, sizeof out, NULL);
    CHECK(!strcmp(out, "abc"), "text through a number format: %s", out);
    owf_calc_format_value("\"<\"@\">\"", &v, 0, 0, out, sizeof out, NULL);
    CHECK(!strcmp(out, "<abc>"), "@: %s", out);
    owf_calc_format_value("0;-0;0;\"T:\"@", &v, 0, 0, out, sizeof out, NULL);
    CHECK(!strcmp(out, "T:abc"), "fourth section: %s", out);
    v.text = NULL;
    v.type = OWF_VALUE_BOOL;
    v.number = 1;
    owf_calc_format_value("0.00", &v, 0, 0, out, sizeof out, NULL);
    CHECK(!strcmp(out, "TRUE"), "bool: %s", out);
    v.type = OWF_VALUE_ERROR;
    v.error = OWF_ERROR_DIV0;
    owf_calc_format_value("0.00", &v, 0, 0, out, sizeof out, NULL);
    CHECK(!strcmp(out, "#DIV/0!"), "error: %s", out);
    v.type = OWF_VALUE_EMPTY;
    CHECK(owf_calc_format_value("0.00", &v, 0, 0, out, sizeof out, NULL) == 0 && !*out, "empty");
    /* the 1904 calendar */
    v.type = OWF_VALUE_NUMBER;
    v.number = 0;
    owf_calc_format_value("yyyy-mm-dd", &v, 1, 0, out, sizeof out, NULL);
    CHECK(!strcmp(out, "1904-01-01"), "1904: %s", out);
    /* General in a width */
    v.number = 3.14159265358979;
    owf_calc_format_value("General", &v, 0, 11, out, sizeof out, NULL);
    CHECK(!strcmp(out, "3.141592654"), "width 11: %s", out);
    v.number = 123456789012345.0;
    owf_calc_format_value("General", &v, 0, 11, out, sizeof out, NULL);
    CHECK(!strcmp(out, "1.23457E+14"), "width 11, big: %s", out);
    v.number = 0.000012345678901;
    owf_calc_format_value("General", &v, 0, 11, out, sizeof out, NULL);
    CHECK(!strcmp(out, "1.23457E-05"), "width 11, small: %s", out);
    v.number = 1e20;
    owf_calc_format_value("General", &v, 0, 3, out, sizeof out, NULL);
    CHECK(!strcmp(out, "###"), "width 3: %s", out);
    /* cut short to the buffer, not in the middle of a character */
    v.type = OWF_VALUE_TEXT;
    v.text = (char *)"Gr\xC3\xBC\xC3\x9F" "e";
    CHECK(owf_calc_format_value("@", &v, 0, 0, out, 4, NULL) == 2 && !strcmp(out, "Gr"), "cut: %s", out);
    /* through a book: the cell's style */
    fresh();
    put("A1", "25/10/2026");
    put("A2", "0.5");
    put("A3", "=1/0");
    recalc();
    owf_calc_format(book, owf_sheet_find(&book->sheets[0], 0, 0), out, sizeof out);
    CHECK(!strcmp(out, "25/10/2026"), "cell format: %s", out);
    owf_calc_format(book, owf_sheet_find(&book->sheets[0], 1, 0), out, sizeof out);
    CHECK(!strcmp(out, "0.5"), "General cell: %s", out);
    owf_calc_format(book, owf_sheet_find(&book->sheets[0], 2, 0), out, sizeof out);
    CHECK(!strcmp(out, "#DIV/0!"), "error cell: %s", out);
}

static void test_names_of_cells(void)
{
    char out[16];
    unsigned r, c;
    owf_calc_cell_name(11, 1, out);
    CHECK(!strcmp(out, "B12"), "B12: %s", out);
    owf_calc_cell_name(1048575, 16383, out);
    CHECK(!strcmp(out, "XFD1048576"), "XFD1048576: %s", out);
    CHECK(owf_calc_parse_cell("$B$12", &r, &c) && r == 11 && c == 1, "$B$12");
    CHECK(owf_calc_parse_cell(" aa3 ", &r, &c) && r == 2 && c == 26, "aa3");
    CHECK(!owf_calc_parse_cell("B", &r, &c), "B");
    CHECK(!owf_calc_parse_cell("XFE1", &r, &c), "XFE1");
    CHECK(!owf_calc_parse_cell("A0", &r, &c), "A0");
    CHECK(!owf_calc_parse_cell("A1:B2", &r, &c), "A1:B2");
}

/* ---- The oracle: the workbooks' saved results ---- */

/* Differences explained: the writer's rule is not Excel's, or a formula's
 * result depends on when it was worked out. */
typedef struct {
    const char *file, *sheet, *cell, *why;
} known_t;

static const known_t known[] = {
    { "calc.xls", "Calc", "B6", "SUM(1,\"2\"): Excel takes text given directly as a number (3); LibreOffice wrote #VALUE!" },
    { "calc.xls", "Calc", "B65", "SQRT(-1): Excel's #NUM!; LibreOffice wrote #VALUE!" },
    { "calc.xls", "Calc", "B68", "POWER(0,-1): Excel's #DIV/0!; LibreOffice wrote #NUM!" },
    { "calc.xls", "Calc", "B71", "LN(0): Excel's #NUM!; LibreOffice wrote #VALUE!" },
    { "calc.xls", "Calc", "B90", "PRODUCT(2,3,\"4\"): Excel takes the text 4 (24); LibreOffice wrote #VALUE!" },
    { "calc.xls", "Calc", "B101", "LARGE(range,11) of ten: Excel's #NUM!; LibreOffice wrote #VALUE!" },
    { "calc.xls", "Calc", "B151", "CONCATENATE(\"a\",1,TRUE): Excel's \"a1TRUE\"; LibreOffice's TRUE is the number 1" },
    { "calc.xls", "Calc", "B184", "VLOOKUP(...,4,...) past a 3-column table: Excel's #REF!; LibreOffice wrote #VALUE!" },
    { "calc.xls", "Calc", "B34", "COUNTIF(range,TRUE): LibreOffice saved the booleans in Data!C as numbers 1 and 0, and counts the 1s; Excel's TRUE matches only booleans" },
    { "calc.xls", "Calc", "B247", "TRUE>\"z\": in Excel booleans sort after text (TRUE); LibreOffice's TRUE is the number 1, before text" },
    { "book.xls", "Numbers", "F2", "LibreOffice did not work this boolean cell out: the file has book.fods' placeholder (false, saved as 0)" },
    { "book.xls", "Numbers", "F3", "the same placeholder; the formula's value is FALSE" },
    { "book.xls", "Numbers", "F4", "the same placeholder; D4 is 240, so D4>50 is TRUE" },
    { "book.xls", "Numbers", "F5", "the same placeholder; the formula's value is FALSE" },
    { "book.xls", "Numbers", "D10", "the same: book.fods' placeholder false; F3 is FALSE, so NOT(F3) is TRUE" },
    { "book.xls", "Numbers", "E8", "the same: book.fods' placeholder 0 (a date-formatted cell); A8 is 21 October 2026, +30 is 46346" },
};

static const char *known_reason(const char *file, const char *sheet, const char *cell)
{
    size_t i;
    for (i = 0; i < sizeof known / sizeof known[0]; i++)
        if (strstr(file, known[i].file) && !strcmp(sheet, known[i].sheet) && !strcmp(cell, known[i].cell))
            return known[i].why;
    return NULL;
}

/* 1 the same (numbers to 15 significant digits), 2 numbers the same to
 * 13 (rounding in the last digits), 0 different. */
static int same_result(const owf_cell *a, const owf_cell *b)
{
    char x[40], y[40];
    if (a->type != b->type)
        return 0;
    switch (a->type) {
    case OWF_VALUE_NUMBER:
        if (a->number == b->number)
            return 1;
        owf_number_text(a->number, x);
        owf_number_text(b->number, y);
        if (!strcmp(x, y))
            return 1;
        return fabs(a->number - b->number) <= fabs(a->number) * 1e-13 ? 2 : 0;
    case OWF_VALUE_BOOL:
        return (a->number != 0) == (b->number != 0);
    case OWF_VALUE_ERROR:
        return a->error == b->error;
    case OWF_VALUE_TEXT:
        return !strcmp(a->text ? a->text : "", b->text ? b->text : "");
    default:
        return 1;
    }
}

static int oracle_total, oracle_match, oracle_close, oracle_kept, oracle_known;

static void oracle(const char *path)
{
    owf_book *b = NULL;
    owf_calc *k;
    owf_cell *saved;
    int s, i, n = 0, total = 0, match = 0, close = 0, kept = 0, knowns = 0, same;
    const char *file = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;

    if (owf_import_book_file(path, NULL, &b, NULL, NULL) != OWF_OK) {
        CHECK(0, "could not read %s", path);
        return;
    }
    for (s = 0; s < b->nsheets; s++)
        for (i = 0; i < b->sheets[s].ncells; i++)
            n += b->sheets[s].cells[i].formula != NULL;
    saved = calloc((size_t)n + 1, sizeof *saved);
    n = 0;
    for (s = 0; s < b->nsheets; s++)
        for (i = 0; i < b->sheets[s].ncells; i++) {
            const owf_cell *c = &b->sheets[s].cells[i];
            if (!c->formula)
                continue;
            saved[n] = *c;
            saved[n].text = NULL;
            if (c->text && (saved[n].text = malloc(strlen(c->text) + 1)) != NULL)
                strcpy(saved[n].text, c->text);
            saved[n].formula = NULL;
            n++;
        }
    k = owf_calc_new(b);
    CHECK(k != NULL, "engine for %s", path);
    if (!k) {
        owf_book_free(b);
        return;
    }
    CHECK(owf_calc_recalc_all(k) == OWF_OK, "recalc %s", path);
    n = 0;
    for (s = 0; s < b->nsheets; s++)
        for (i = 0; i < b->sheets[s].ncells; i++) {
            const owf_cell *c = &b->sheets[s].cells[i];
            char name[16], want[600], got[600];
            const char *why;
            if (!c->formula)
                continue;
            total++;
            owf_calc_cell_name(c->row, c->col, name);
            if (owf_calc_state(k, s, c->row, c->col) & OWF_CALC_KEPT) {
                kept++;
                printf("  kept  %s %s!%s =%s (a function or reference the engine does not have)\n", file,
                       b->sheets[s].name, name, c->formula);
            } else if ((same = same_result(c, &saved[n])) != 0) {
                match++;
                if (same == 2) {
                    close++;
                    show(&saved[n], want, sizeof want);
                    show(c, got, sizeof got);
                    printf("  close %s %s!%s =%s: file %s, engine %s (the same to 13 digits)\n", file,
                           b->sheets[s].name, name, c->formula, want, got);
                }
            } else {
                show(&saved[n], want, sizeof want);
                show(c, got, sizeof got);
                why = known_reason(file, b->sheets[s].name, name);
                if (why) {
                    knowns++;
                    printf("  known %s %s!%s =%s: file %s, engine %s: %s\n", file, b->sheets[s].name, name,
                           c->formula, want, got, why);
                } else {
                    CHECK(0, "oracle %s %s!%s =%s: file %s, engine %s", file, b->sheets[s].name, name, c->formula,
                          want, got);
                }
            }
            free(saved[n].text);
            n++;
        }
    printf("oracle %s: %d formulas, %d match the saved results (%d of them to 13 digits, not 15), %d known "
           "differences, %d kept (not worked out)\n", file, total, match, close, knowns, kept);
    oracle_total += total;
    oracle_match += match;
    oracle_close += close;
    oracle_kept += kept;
    oracle_known += knowns;
    free(saved);
    owf_calc_free(k);
    owf_book_free(b);
}

/* ---- Speed ---- */

static double seconds(clock_t a, clock_t b)
{
    return (double)(b - a) / CLOCKS_PER_SEC;
}

static void test_speed(void)
{
    char ref[16], f[128];
    int i, r;
    clock_t t0, t1, t2, t3;
    fresh();
    for (i = 1; i <= 1000; i++) {
        snprintf(ref, sizeof ref, "A%d", i);
        snprintf(f, sizeof f, "%d", (i * 37) % 1000);
        put(ref, f);
    }
    /* ten formulas a row, 1000 rows: 10,000 */
    for (i = 1; i <= 1000; i++) {
        static const char *const cols[10] = {
            "=A%d*2", "=B%d+A%d", "=SUM($A$1:A%d)", "=IF(A%d>500,\"big\",\"small\")", "=ROUND(C%d/3,2)",
            "=LEN(E%d)&\"x\"", "=VLOOKUP(A%d,$A$1:$B$1000,2,FALSE)", "=AVERAGE(A%d:C%d)", "=MAX(B%d,C%d)*MIN(A%d,1)",
            "=COUNTIF($E$1:$E$1000,E%d)",
        };
        int k;
        for (k = 0; k < 10; k++) {
            snprintf(ref, sizeof ref, "%c%d", 'B' + k, i);
            snprintf(f, sizeof f, cols[k], i, i, i, i);
            put(ref, f);
        }
    }
    t0 = clock();
    r = owf_calc_recalc_all(calc);
    t1 = clock();
    CHECK(r == OWF_OK, "recalc 10,000");
    EXPECT("B1", "74");
    EXPECT("C1000", "0");
    t2 = clock();
    owf_calc_free(calc);
    calc = owf_calc_new(book);
    t3 = clock();
    CHECK(calc != NULL, "parse 10,000");
    printf("speed: 10,000 formulas worked out in %.3f s (host, %s), parsed again in %.3f s\n", seconds(t0, t1),
#ifdef __OPTIMIZE__
           "optimised",
#else
           "not optimised",
#endif
           seconds(t2, t3));
    /* and a recalc after one change: only what reads it */
    t0 = clock();
    put("A500", "999");
    owf_calc_recalc(calc);
    t1 = clock();
    printf("speed: one cell changed, its readers worked out in %.3f s\n", seconds(t0, t1));
}

int main(int argc, char **argv)
{
    int i;
    test_parser();
    test_references();
    test_values();
    test_functions();
    test_function_coverage();
    test_arrays();
    test_recalc();
    test_circular();
    test_enter();
    test_move();
    test_shift();
    test_kept();
    test_formats();
    test_names_of_cells();
    for (i = 1; i < argc; i++)
        oracle(argv[i]);
    if (argc > 1)
        printf("oracle: %d formulas, %d match (%d to 13 digits), %d known differences, %d kept\n", oracle_total,
               oracle_match, oracle_close, oracle_known, oracle_kept);
    test_speed();
    owf_calc_free(calc);
    owf_book_free(book);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
