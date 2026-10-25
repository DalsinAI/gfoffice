#!/usr/bin/env python3
"""Makes the formula engine's oracle: make_calc_fixtures.py OUTDIR

Writes calc.fods (OpenDocument, flat XML) with a sheet of data and a sheet
of formulas, one for each case the engine should get right, and has
LibreOffice work them out and save the book as Excel 97-2003 (calc.xls).
The engine's tests (tests/calc/test_calc.c) read calc.xls, keep the results
LibreOffice wrote, work every formula out again and compare. LibreOffice
only makes the test file; nothing of it is in libowf.

The formulas below are in Excel's syntax, as the engine reads them; this
script turns them into OpenFormula for the .fods. Column A of the Calc
sheet holds each formula's text, column B the formula.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape, quoteattr

OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "fixtures"))

NS = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
      'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
      'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
      'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
      'xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0" '
      'xmlns:of="urn:oasis:names:tc:opendocument:xmlns:of:1.2"')

STYLES = """
<number:date-style style:name="D1"><number:year number:style="long"/><number:text>-</number:text><number:month number:style="long"/><number:text>-</number:text><number:day number:style="long"/></number:date-style>
<number:time-style style:name="T1"><number:hours number:style="long"/><number:text>:</number:text><number:minutes number:style="long"/><number:text>:</number:text><number:seconds number:style="long"/></number:time-style>
<style:style style:name="date" style:family="table-cell" style:data-style-name="D1"/>
<style:style style:name="time" style:family="table-cell" style:data-style-name="T1"/>
"""

# ---- Excel's syntax to OpenFormula ----

REF = re.compile(r"(?:('(?:[^']|'')+'|[A-Za-z_][A-Za-z0-9_.]*)!)?"
                 r"(\$?[A-Za-z]{1,3}\$?\d+(?::\$?[A-Za-z]{1,3}\$?\d+)?|\$?[A-Za-z]{1,3}:\$?[A-Za-z]{1,3})"
                 r"(?![A-Za-z0-9_.(!])")


def of_ref(m):
    sheet, body = m.group(1), m.group(2)
    parts = body.split(":")
    prefix = "$" + sheet if sheet else ""
    return "[" + prefix + "." + parts[0] + "".join(":." + p for p in parts[1:]) + "]"


def to_of(f):
    out, i, brace = [], 0, False
    while i < len(f):
        c = f[i]
        if c == '"':
            j = i + 1
            while True:
                if f[j] == '"':
                    if j + 1 < len(f) and f[j + 1] == '"':
                        j += 2
                        continue
                    break
                j += 1
            out.append(f[i:j + 1])
            i = j + 1
            continue
        if c == "{":
            brace = True
        elif c == "}":
            brace = False
        if c == ",":
            out.append(";")
            i += 1
            continue
        if c == ";" and brace:
            out.append("|")
            i += 1
            continue
        if (c.isalpha() or c in "$'_") and (i == 0 or not (f[i - 1].isalnum() or f[i - 1] in "_.")):
            m = REF.match(f, i)
            if m:
                out.append(of_ref(m))
                i = m.end()
                continue
            m = re.compile(r"[A-Za-z_][A-Za-z0-9_.]*").match(f, i)
            if m:
                word = m.group(0)
                nxt = f[m.end():m.end() + 1]
                if word.upper() in ("CEILING", "FLOOR") and nxt == "(":
                    word = "COM.MICROSOFT." + word.upper()   # Excel's, not OpenFormula's
                out.append(word + ("()" if word.upper() in ("TRUE", "FALSE") and nxt != "(" else ""))
                i = m.end()
                continue
        out.append(c)
        i += 1
    return "".join(out)


# ---- Cells ----

def cell(value=None, formula=None, style=None, kind=None, matrix=None):
    """A number, a str, True/False, ("date", "2026-10-21"), ("time", "PT13H45M30S");
    formula in Excel's syntax; matrix=(rows, cols) for an array formula."""
    attrs = []
    if style:
        attrs.append('table:style-name="%s"' % style)
    if formula:
        attrs.append("table:formula=%s" % quoteattr("of:=" + to_of(formula)))
    if matrix:
        attrs.append('table:number-matrix-rows-spanned="%d" table:number-matrix-columns-spanned="%d"' % matrix)
    body = ""
    if isinstance(value, bool):
        attrs.append('office:value-type="boolean" office:boolean-value="%s"' % ("true" if value else "false"))
    elif isinstance(value, (int, float)):
        attrs.append('office:value-type="%s" office:value="%r"' % (kind or "float", value))
    elif isinstance(value, tuple) and value[0] == "date":
        attrs.append('office:value-type="date" office:date-value="%s"' % value[1])
    elif isinstance(value, tuple) and value[0] == "time":
        attrs.append('office:value-type="time" office:time-value="%s"' % value[1])
    elif isinstance(value, str):
        attrs.append('office:value-type="string"')
        body = "<text:p>%s</text:p>" % escape(value)
    return "<table:table-cell %s>%s</table:table-cell>" % (" ".join(attrs), body)


def row(cells):
    return "<table:table-row>%s</table:table-row>" % "".join(cells)


# ---- The data ----

A = [1, 2.5, -3, 0, 10, 100, 0.1, 7, 1e10, 42]
B = ["apple", "Banana", "cherry", "date", "Elder berry", "fig", "grape", "  spaced   out  ", "123", None]
C = [True, False, 5, "x", None, 3, True, "7", 2, 1]
D = [("date", "2026-10-25"), ("date", "2000-02-29"), ("date", "1900-03-01"), ("date", "2024-12-31"),
     ("time", "PT13H45M30S"), ("date", "2026-01-01T06:30:00"), ("date", "1999-12-31"), None, None, None]
E = [10, 20, 30, 40, 50, None, None, None, None, None]
F = ["ten", "twenty", "thirty", "forty", "fifty", None, None, None, None, None]
G = [1.5, 2.5, 3.5, 4.5, 5.5, None, None, None, None, None]
H = ["a", "b", "a", "c", "a", "b", None, None, None, None]
I = [10, 20, 30, 40, 50, 60, None, None, None, None]


def data_sheet():
    rows = []
    for k in range(10):
        cells = []
        for col in (A, B, C, D, E, F, G, H, I):
            v = col[k]
            style = None
            if isinstance(v, tuple):
                style = "time" if v[0] == "time" else "date"
            cells.append(cell(v, style=style) if v is not None else cell())
        rows.append(row(cells))
    rows.append(row([cell()]))
    # row 12 and 13: a table across (HLOOKUP)
    rows.append(row([cell()] * 4 + [cell(v) for v in (1, 2, 3, 4, 5)]))
    rows.append(row([cell()] * 4 + [cell(v) for v in ("one", "two", "three", "four", "five")]))
    return '<table:table table:name="Data">%s</table:table>' % "".join(rows)


# ---- The formulas ----

FORMULAS = [
    # maths
    "SUM(Data!A1:A10)", "SUM(Data!A1:A3,10,TRUE)", "SUM(Data!B1:B10)", "SUM(Data!C1:C10)", "SUM(Data!A:A)",
    'SUM(1,"2")', 'SUMIF(Data!H1:H6,"a",Data!I1:I6)', 'SUMIF(Data!A1:A10,">5")',
    'SUMIF(Data!H1:H6,"<>a",Data!I1:I6)', 'SUMIF(Data!B1:B10,"*e*",Data!A1:A10)', 'SUMIF(Data!H1:H6,"a",Data!I3)',
    "SUMPRODUCT(Data!A1:A3,Data!I1:I3)", 'SUMPRODUCT((Data!H1:H6="a")*Data!I1:I6)', "SUMPRODUCT(Data!A1:A3,Data!I1:I4)",
    "AVERAGE(Data!A1:A10)", "AVERAGE(Data!C1:C10)", "AVERAGE(Data!B1:B3)", 'AVERAGEIF(Data!H1:H6,"b",Data!I1:I6)',
    'AVERAGEIF(Data!A1:A10,"<0")', 'AVERAGEIF(Data!H1:H6,"z",Data!I1:I6)',
    "MIN(Data!A1:A10)", "MAX(Data!A1:A10)", "MIN(Data!B1:B3)", "MAX(Data!C1:C10)",
    "COUNT(Data!A1:C10)", "COUNT(Data!C1:C10)", "COUNTA(Data!A1:C10)", "COUNTA(Data!C1:C10)", 'COUNT(1,"2","x",TRUE)',
    "COUNTBLANK(Data!A1:C10)", 'COUNTIF(Data!H1:H6,"a")', 'COUNTIF(Data!A1:A10,">=10")', 'COUNTIF(Data!B1:B10,"?a*")',
    "COUNTIF(Data!C1:C10,TRUE)", 'COUNTIF(Data!A1:A10,"<>0")', 'COUNTIF(Data!H1:H6,"<b")', 'COUNTIF(Data!B1:B10,"")',
    'COUNTIFS(Data!H1:H6,"a",Data!I1:I6,">10")', 'SUMIFS(Data!I1:I6,Data!H1:H6,"a",Data!I1:I6,"<50")',
    'AVERAGEIFS(Data!I1:I6,Data!H1:H6,"<>c")',
    "ROUND(2.675,2)", "ROUND(-2.5,0)", "ROUND(1234.5678,-2)", "ROUND(0.285,2)", "ROUND(1.005,2)",
    "ROUNDUP(3.2,0)", "ROUNDUP(-3.2,0)", "ROUNDUP(0.1+0.2,1)", "ROUNDDOWN(3.7,0)", "ROUNDDOWN(-3.7,0)",
    "ROUNDDOWN(1234.5678,-2)", "INT(-3.5)", "INT(3.99)", "TRUNC(-3.99)", "TRUNC(3.14159,3)",
    "MOD(10,3)", "MOD(-10,3)", "MOD(10,-3)", "MOD(5.5,2)", "MOD(1,0)", "ABS(-4)", "SIGN(-0.5)", "SIGN(0)",
    "SQRT(16)", "SQRT(-1)", "POWER(2,10)", "POWER(2,0.5)", "POWER(0,-1)", "EXP(1)", "LN(10)", "LN(0)", "LOG(100)",
    "LOG(8,2)", "LOG10(1000)", "PI()", "SIN(PI()/2)", "COS(0)", "TAN(PI()/4)", "ASIN(1)", "ACOS(-1)", "ATAN(1)",
    "ATAN2(1,1)", "ASIN(2)", "SINH(1)", "COSH(1)", "TANH(0.5)", "DEGREES(PI())", "RADIANS(180)",
    "PRODUCT(Data!A1:A3)", 'PRODUCT(2,3,"4")', "MEDIAN(Data!A1:A10)", "MEDIAN(1,2,3,4)", "STDEV(Data!A1:A6)",
    "VAR(Data!A1:A6)", "STDEVP(Data!A1:A6)", "VARP(Data!A1:A6)", "STDEV(1)", "SUMSQ(1,2,3)",
    "LARGE(Data!A1:A10,2)", "SMALL(Data!A1:A10,3)", "LARGE(Data!A1:A10,11)", "CEILING(4.3,2)", "CEILING(-4.3,-2)",
    "FLOOR(4.3,2)", "FLOOR(-4.3,-2)", "EVEN(3)", "ODD(2)", "EVEN(-1.5)", "FACT(5)", "RAND()<1",
    # logic
    'IF(Data!A1>0,"pos","neg")', "IF(FALSE,1)", 'IF(Data!A4,"yes","no")', 'IFERROR(1/0,"div")',
    'IFERROR(Data!A1,"x")', "AND(TRUE,1)", "AND(Data!C1:C2)", "OR(FALSE,0)", "OR(Data!C1:C2)", "NOT(Data!A4)",
    "NOT(1)", "TRUE()", "FALSE()", "ISBLANK(Data!B10)", "ISBLANK(Data!A1)", "ISERROR(1/0)", "ISERROR(Data!A1)",
    "ISNUMBER(Data!A1)", "ISNUMBER(Data!B9)", "ISTEXT(Data!B1)", "NA()", "ISNA(NA())", "ISERR(NA())",
    "ISLOGICAL(TRUE)", "ISNONTEXT(Data!A1)", "N(Data!C1)", "T(Data!B1)", "IF(Data!A1>0,Data!B1,Data!B2)",
    # text
    "LEN(Data!B5)", "LEN(12345)", "LEFT(Data!B1,3)", "LEFT(Data!B1)", "RIGHT(Data!B2,2)", "MID(Data!B5,3,4)",
    'MID("abc",5,2)', "UPPER(Data!B1)", "LOWER(Data!B2)", 'PROPER("hello wORLD 2nd")', "TRIM(Data!B8)",
    'CONCATENATE(Data!B1," & ",Data!B2)', 'CONCATENATE("a",1,TRUE)', 'TEXT(1234.5,"#,##0.00")',
    'TEXT(0.256,"0.0%")', 'TEXT(Data!D1,"dd/mm/yyyy")', 'TEXT(Data!D1,"dddd d mmmm yyyy")',
    'TEXT(Data!D5,"h:mm:ss")', 'TEXT(-5,"0.00;(0.00)")', 'TEXT(1234567,"0.00E+00")', 'TEXT(42,"000000")',
    'TEXT(Data!D6,"yyyy-mm-dd hh:mm")', 'TEXT(0.5,"h:mm AM/PM")', 'VALUE("1,234.5")', 'VALUE("50%")', 'VALUE("abc")',
    'FIND("a",Data!B2)', 'FIND("A",Data!B2)', 'FIND("z","abc")', 'SEARCH("N",Data!B2)', 'SEARCH("e*y",Data!B5)',
    'SEARCH("b?r",Data!B5)', 'SUBSTITUTE("a-b-c","-","+")', 'SUBSTITUTE("a-b-c","-","+",2)', 'REPT("ab",3)',
    'EXACT("abc","ABC")', 'EXACT("abc","abc")', 'REPLACE("abcdef",2,3,"X")', "CHAR(65)", 'CODE("A")',
    'CLEAN("a"&CHAR(9)&"b")',
    # lookup
    "VLOOKUP(30,Data!E1:G5,2,FALSE)", "VLOOKUP(35,Data!E1:G5,3)", "VLOOKUP(5,Data!E1:G5,2)",
    'VLOOKUP("forty",Data!F1:G5,2,FALSE)', "VLOOKUP(30,Data!E1:G5,4,FALSE)", 'VLOOKUP("T*",Data!F1:G5,2,FALSE)',
    "HLOOKUP(3,Data!E12:I13,2,FALSE)", "HLOOKUP(3.5,Data!E12:I13,2)", "INDEX(Data!E1:G5,2,3)",
    "INDEX(Data!F1:F5,4)", "SUM(INDEX(Data!E1:G5,0,1))", "INDEX(Data!E1:G5,6,1)", "MATCH(40,Data!E1:E5,0)",
    "MATCH(45,Data!E1:E5)", 'MATCH("t*",Data!F1:F5,0)', "MATCH(60,Data!E1:E5,0)", 'CHOOSE(2,"a","b","c")',
    'CHOOSE(4,"a","b")', "ROW()", "ROW(Data!C7)", "COLUMN()", "COLUMN(Data!C7)", "ROWS(Data!A1:C10)",
    "COLUMNS(Data!A1:C10)", "SUM(Data!E1:INDEX(Data!E1:E5,3))",
    # dates
    "DATE(2026,10,25)", "DATE(2026,14,1)", "DATE(2026,1,0)", "DATE(99,1,1)", "YEAR(Data!D1)", "MONTH(Data!D1)",
    "DAY(Data!D1)", "YEAR(Data!D2)", "DAY(Data!D2)", "WEEKDAY(Data!D1)", "WEEKDAY(Data!D1,2)",
    "WEEKDAY(Data!D1,3)", "TIME(13,45,30)", "TIME(25,0,0)", "HOUR(Data!D5)", "MINUTE(Data!D5)",
    "SECOND(Data!D5)", "HOUR(Data!D6)", "TODAY()>45000", "NOW()>=TODAY()", "Data!D1+30", "Data!D1-Data!D2",
    'DATE(2024,2,29)-DATE(2023,2,28)', 'YEAR("2026-10-25")',
    # money
    "PMT(0.05/12,360,200000)", "PMT(0,12,1200)", "PMT(0.06/12,60,-10000,0,1)", "PV(0.08/12,240,-500)",
    "PV(0,10,-100)", "FV(0.06/12,120,-200,-500,1)", "FV(0,10,-100)", "NPV(0.1,-10000,3000,4200,6800)",
    "NPV(0.08,Data!A1:A3)",
    # operators and values
    '1+"2"', '"a"&1.5', "2^3^2", "-2^2", "10%", "5%*Data!I2", '"abc"<"abd"', '"B">"a"', '1<"a"', 'TRUE>"z"',
    "0.1+0.2=0.3", "1/0", "Data!B1*2", "Data!B10+1", 'Data!B10&"x"', '3&""', "(1/0)+NA()", "NA()+(1/0)",
    '"1"=1', "0.1+0.2-0.3", "1E+300*1E+300", '"Apple"=Data!B1', "SUM(Amounts)", "Rate*100",
    "Data!A2:A4*2", "-(-(3))", "+Data!B1",
]

ARRAYS = [  # array formulas, one cell each
    "SUM(Data!A1:A3*Data!I1:I3)", 'SUM(IF(Data!H1:H6="a",Data!I1:I6))', "SUM(LEN(Data!B1:B7))",
    'MAX(IF(Data!H1:H6="b",Data!I1:I6))', "SUM((Data!A1:A10>5)*1)",
]


def calc_sheet():
    rows = []
    for f in FORMULAS:
        rows.append(row([cell(f), cell(0, f)]))
    for f in ARRAYS:
        rows.append(row([cell(f), cell(0, f, matrix=(1, 1))]))
    # an array formula over three cells, and two that read each other
    f = "ROW(Data!A1:A3)*2"
    rows.append(row([cell(f), cell(0, f, matrix=(3, 1))]))
    rows.append(row([cell(f), cell(0)]))
    rows.append(row([cell(f), cell(0)]))
    return '<table:table table:name="Calc">%s</table:table>' % "".join(rows)


def fods():
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<office:document %s office:version="1.2" '
            'office:mimetype="application/vnd.oasis.opendocument.spreadsheet">'
            '<office:automatic-styles>%s</office:automatic-styles><office:body><office:spreadsheet>'
            # as Excel works: text compared without case, wildcards (not regular expressions)
            '<table:calculation-settings table:case-sensitive="false" table:use-regular-expressions="false" '
            'table:use-wildcards="true" table:search-criteria-must-apply-to-whole-cell="true"/>'
            '<table:named-expressions>'
            '<table:named-range table:name="Amounts" table:base-cell-address="$Data.$A$1" '
            'table:cell-range-address="$Data.$I$1:.$I$6"/>'
            '<table:named-expression table:name="Rate" table:base-cell-address="$Data.$A$1" table:expression="of:=0.05"/>'
            '</table:named-expressions>%s%s</office:spreadsheet></office:body></office:document>\n'
            % (NS, STYLES, data_sheet(), calc_sheet()))


# LibreOffice keeps a formula's last result from the file unless told to
# work them out; a profile of its own tells it to.
RECALC = """<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<item oor:path="/org.openoffice.Office.Calc/Formula/Load"><prop oor:name="ODFRecalcMode" oor:op="fuse"><value>0</value></prop></item>
</oor:items>
"""


def main():
    os.makedirs(OUT, exist_ok=True)
    source = os.path.join(OUT, "calc.fods")
    with open(source, "w", encoding="utf-8") as f:
        f.write(fods())
    profile = tempfile.mkdtemp(prefix="owf-lo-")
    try:
        os.makedirs(os.path.join(profile, "user"))
        with open(os.path.join(profile, "user", "registrymodifications.xcu"), "w") as f:
            f.write(RECALC)
        subprocess.run(["soffice", "-env:UserInstallation=file://" + profile, "--headless",
                        "--convert-to", "xls:MS Excel 97", "--outdir", OUT, source],
                       check=True, stdout=subprocess.DEVNULL, timeout=300)
    finally:
        shutil.rmtree(profile, ignore_errors=True)
    print(os.path.join(OUT, "calc.xls"))


if __name__ == "__main__":
    main()
