#!/usr/bin/env python3
"""Makes the workbook test files: make_fixtures.py OUTDIR

Writes book.fods (OpenDocument, flat XML) by hand, then has LibreOffice
save it as Excel 97-2003 (book.xls), so that the .xls reader is tested on a
file another program wrote. LibreOffice only makes the test file; nothing of
it is in OpenWrite. Run it again after changing the sheets below, and change
test_books.py to match. (Frozen panes do not survive a conversion without a
window; test_books.py builds its own workbook for those and the records
LibreOffice does not write.)
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import shutil
import subprocess
import sys
import tempfile
from xml.sax.saxutils import escape, quoteattr

OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(__file__))

NS = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
      'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
      'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
      'xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" '
      'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
      'xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" '
      'xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0" '
      'xmlns:of="urn:oasis:names:tc:opendocument:xmlns:of:1.2" '
      'xmlns:config="urn:oasis:names:tc:opendocument:xmlns:config:1.0" '
      'xmlns:ooo="http://openoffice.org/2004/office"')

STYLES = """
<number:number-style style:name="N2"><number:grouping/><number:number number:decimal-places="2" number:min-integer-digits="1" number:grouping="true"/></number:number-style>
<number:date-style style:name="D1"><number:year number:style="long"/><number:text>-</number:text><number:month number:style="long"/><number:text>-</number:text><number:day number:style="long"/></number:date-style>
<number:time-style style:name="T1"><number:hours number:style="long"/><number:text>:</number:text><number:minutes number:style="long"/><number:text>:</number:text><number:seconds number:style="long"/></number:time-style>
<number:percentage-style style:name="P0"><number:number number:decimal-places="0" number:min-integer-digits="1"/><number:text>%</number:text></number:percentage-style>
<style:style style:name="coA" style:family="table-column"><style:table-column-properties style:column-width="4cm"/></style:style>
<style:style style:name="coN" style:family="table-column"><style:table-column-properties style:column-width="2.258cm"/></style:style>
<style:style style:name="coH" style:family="table-column"><style:table-column-properties style:column-width="2.258cm"/></style:style>
<style:style style:name="roN" style:family="table-row"><style:table-row-properties style:row-height="0.452cm" style:use-optimal-row-height="true"/></style:style>
<style:style style:name="roTall" style:family="table-row"><style:table-row-properties style:row-height="1cm" style:use-optimal-row-height="false"/></style:style>
<style:style style:name="taShown" style:family="table"><style:table-properties table:display="true"/></style:style>
<style:style style:name="taHidden" style:family="table"><style:table-properties table:display="false"/></style:style>
<style:style style:name="head" style:family="table-cell"><style:table-cell-properties fo:background-color="#ffff00" fo:border-bottom="0.74pt solid #000000"/><style:text-properties fo:font-weight="bold" fo:color="#ff0000"/></style:style>
<style:style style:name="money" style:family="table-cell" style:data-style-name="N2"/>
<style:style style:name="date" style:family="table-cell" style:data-style-name="D1"/>
<style:style style:name="time" style:family="table-cell" style:data-style-name="T1"/>
<style:style style:name="pct" style:family="table-cell" style:data-style-name="P0"/>
<style:style style:name="title" style:family="table-cell"><style:table-cell-properties style:vertical-align="middle"/><style:paragraph-properties fo:text-align="center"/><style:text-properties fo:font-size="14pt"/></style:style>
<style:style style:name="serif" style:family="table-cell"><style:text-properties style:font-name="Liberation Serif" fo:font-size="14pt" fo:font-style="italic" style:text-underline-style="solid" style:text-underline-width="auto" style:text-underline-color="font-color"/></style:style>
<style:style style:name="wrap" style:family="table-cell"><style:table-cell-properties fo:wrap-option="wrap"/></style:style>
<style:style style:name="boxed" style:family="table-cell"><style:table-cell-properties fo:border="0.74pt solid #0000ff"/></style:style>
"""


def cell(value=None, formula=None, style=None, kind=None, span=None, matrix=False):
    """One cell: a number, a str, True/False, ("date", "2026-10-21"),
    ("time", "PT13H45M30S"), with a formula (OpenFormula, without of:=)."""
    attrs = []
    if style:
        attrs.append('table:style-name="%s"' % style)
    if formula:
        attrs.append('table:formula=%s' % quoteattr("of:=" + formula))
    if matrix:
        attrs.append('table:number-matrix-columns-spanned="1" table:number-matrix-rows-spanned="1"')
    if span:
        attrs.append('table:number-columns-spanned="%d" table:number-rows-spanned="1"' % span)
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
        body = "".join("<text:p>%s</text:p>" % escape(line) for line in value.split("\n"))
    out = "<table:table-cell %s>%s</table:table-cell>" % (" ".join(attrs), body)
    if span:
        out += "<table:covered-table-cell/>" * (span - 1)
    return out


def row(cells, style="roN"):
    return '<table:table-row table:style-name="%s">%s</table:table-row>' % (style, "".join(c if c.startswith("<") else cell(c) for c in cells))


def empty():
    return cell()


def numbers_sheet():
    rows = [row([cell(t, style="head") for t in ("Item", "Qty", "Price", "Total", "Size", "Over50", "Ratio", "Other")])]
    items = [("Apple", 3, 0.5), ("Pear", 10, 1.25), ("Plum", 120, 2), ("Fig, dried", -7, 1e20)]
    for n, (name, qty, price) in enumerate(items, start=2):
        rows.append(row([
            cell(name), cell(qty), cell(price, style="money"),
            cell(0, "[.B%d]*[.C%d]" % (n, n)),
            cell("", 'IF([.D%d]>100;"big";"small")' % n),
            cell(False, "[.D%d]>50" % n),
            cell(0, "[.C%d]/([.B%d]-3)" % (n, n)),
            cell("", "[$'Text & more'.A1]") if n == 2 else empty(),
        ]))
    rows.append(row([
        cell("Total", style="head"), cell(0, "SUM([.B2:.B5])"), cell(0, "MAX([.C2:.C5])"),
        cell(0, "SUM([.D2:.D5])"), cell(0, "ROUND(AVERAGE([.B2:.B5]);2)"), cell(0, 'COUNTIF([.B2:.B5];">5")'),
        cell(0, "SUM(Prices)"), cell(0, "SUM([.B2:.B5]*[.C2:.C5])", matrix=True),
    ]))
    rows.append(row([empty()]))
    rows.append(row([
        cell(("date", "2026-10-21"), style="date"), cell(("time", "PT13H45M30S"), style="time"),
        cell(0.25, style="pct", kind="percentage"), cell(1234.5, style="money"),
        cell(0, "[.A8]+30", style="date"), cell(0, "YEAR([.A8])"), cell(0, "-[.B2]^2+([.B3]+[.B4])*2"),
        cell(0, "-([.B2]+[.B3])"),
    ]))
    rows.append(row([
        cell("", 'CONCATENATE([.A2];" & ";[.A3])'), cell("", '[.A2]&"-"&[.A3]'), cell(0, "LEN([.A5])"),
        cell("", "UPPER([.A2])"), cell(0, 'VLOOKUP("Pear";[.A2:.D5];4;0)'),
        cell(0, 'INDEX([.B2:.B5];MATCH("Plum";[.A2:.A5];0))'), cell("", 'IFERROR(1/0;"none")'),
        cell(0, "SUM([.B:.B])"),
    ]))
    rows.append(row([
        cell(0, "SUM({1;2;3})"), cell(0, "50%"), cell(True, "TRUE()"), cell(False, "NOT([.F3])"),
        cell(0, "1E-05*3"), cell(0, "PI()"), cell(0, "2^3^2"), cell(0, "[.B2]=[.B3]=FALSE()"),
    ]))
    rows.append(row([empty()]))
    rows.append(row([cell("Merged title", style="title", span=3), empty(), cell(True)], style="roTall"))
    rows.append(row([
        cell("Serif", style="serif"), cell(0, "0.1+0.2"), cell(1e20), cell(-0.000123), cell(123456789012345),
        cell(3.14159265358979), cell("boxed", style="boxed"), empty(), cell(42),
    ]))
    columns = ('<table:table-column table:style-name="coA"/>'
               '<table:table-column table:style-name="coN" table:number-columns-repeated="7"/>'
               '<table:table-column table:style-name="coH" table:visibility="collapse"/>')
    return '<table:table table:name="Numbers" table:style-name="taShown">%s%s</table:table>' % (columns, "".join(rows))


def text_sheet():
    long_wide = "".join("Euro €%d " % i for i in range(1500))     # past 8224 bytes, two to a character
    long_narrow = "".join("plain %d " % i for i in range(1500))          # and one to a character
    rows = [row([
        cell("Grüße aus Köln"), cell("日本語のテキスト"), cell("Emoji \U0001F600 here"),
        cell('Zoë\'s "quote"'), cell("Line one\nLine two", style="wrap"),
    ])]
    rows.append(row([cell("Row 1: words to fill the shared strings"), cell(long_wide), cell(0, "[$Numbers.D6]*2")]))
    rows.append(row([cell("Row 2: words to fill the shared strings"), cell(long_narrow), cell(0, "SUM([$Numbers.B2:.B5])")]))
    rows.append(row([cell("Row 3: words to fill the shared strings"), empty(), cell("", '[$Numbers.A2]&" from Numbers"')]))
    for n in range(4, 400):
        rows.append(row([cell("Row %d: words to fill the shared strings" % n)]))
    return '<table:table table:name="Text &amp; more" table:style-name="taShown"><table:table-column table:number-columns-repeated="5"/>%s</table:table>' % "".join(rows)


def hidden_sheet():
    return ('<table:table table:name="Hidden" table:style-name="taHidden"><table:table-column/>%s%s</table:table>'
            % (row([cell(42)]), row([cell("secret")])))


def fods():
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<office:document %s office:version="1.2" '
            'office:mimetype="application/vnd.oasis.opendocument.spreadsheet">'
            '<office:font-face-decls><style:font-face style:name="Liberation Serif" svg:font-family="\'Liberation Serif\'" style:font-family-generic="roman"/></office:font-face-decls>'
            '<office:automatic-styles>%s</office:automatic-styles><office:body><office:spreadsheet>'
            '<table:named-expressions><table:named-range table:name="Prices" table:base-cell-address="$Numbers.$A$1" '
            'table:cell-range-address="$Numbers.$C$2:.$C$5"/></table:named-expressions>'
            '%s%s%s</office:spreadsheet></office:body></office:document>\n'
            % (NS, STYLES, numbers_sheet(), text_sheet(), hidden_sheet()))


# LibreOffice keeps a formula's last result from the file unless told to
# work them out; a profile of its own tells it to.
RECALC = """<?xml version="1.0" encoding="UTF-8"?>
<oor:items xmlns:oor="http://openoffice.org/2001/registry" xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<item oor:path="/org.openoffice.Office.Calc/Formula/Load"><prop oor:name="ODFRecalcMode" oor:op="fuse"><value>0</value></prop></item>
</oor:items>
"""


def main():
    os.makedirs(OUT, exist_ok=True)
    source = os.path.join(OUT, "book.fods")
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
    print(os.path.join(OUT, "book.xls"))


if __name__ == "__main__":
    main()
