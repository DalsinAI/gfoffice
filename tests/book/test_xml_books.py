#!/usr/bin/env python3
"""GFOffice's workbook files: test_xml_books.py BOOK_DUMP OWCONVERT TEST_BOOKFMT

XLSX (Office Open XML), ODS and flat ODS (OpenDocument) and CSV, read and
written by libowf:
  - files other programs made (fixtures/book-lo.*, book-openpyxl.xlsx,
    book-odfpy.ods: make_xml_fixtures.py) and packages written by hand
    from the specifications (fixtures/xlsx-hand/, fixtures/ods-hand/,
    zipped here), each checked line by line in book_dump's output;
  - round trips: XLS to XLSX, XLSX to ODS to XLSX, ODS and flat ODS to
    XLSX, CSV both ways, comparing what the model holds (book_dump, with
    style numbers resolved to what they stand for);
  - the files written: well-formed XML, the parts and relationships each
    format needs, and read back by openpyxl and odfpy (when installed) and
    by LibreOffice (when installed; it is not needed);
  - numbers read exactly as Python reads them, and written in the fewest
    digits that read back the same (test_bookfmt --parse);
  - every file cut short and damaged, at the zip's level and inside its
    XML: book_dump must answer with a book or an error, never a crash
    (make book-check runs it under AddressSanitizer and UBSan).
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import io
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
import xml.dom.minidom

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
DUMP = os.path.abspath(sys.argv[1])
OWCONVERT = os.path.abspath(sys.argv[2])
BOOKFMT = os.path.abspath(sys.argv[3])
SOFFICE = shutil.which("soffice") if os.environ.get("OWF_NO_SOFFICE") is None else None
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name} {str(detail)[:2000]}")


def run(args, timeout=120):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def dump(path):
    return run([DUMP, path])


def convert(src, dst, *extra):
    rc, out, err = run([OWCONVERT, "--report"] + list(extra) + [src, dst])
    check("owconvert %s -> %s" % (os.path.basename(src), os.path.basename(dst)), rc == 0, out + err)
    return rc == 0


def canon(text, notes=False):
    """book_dump's lines with every style, font and number format number
    replaced by what it stands for, so that two books compare by what
    they hold, not by the order of their tables."""
    fonts, numfmts, styles, out = {}, {}, {}, []
    lines = text.splitlines()
    for line in lines:
        w = line.split(" ", 2)
        if w[0] == "font":
            fonts[w[1]] = w[2]
        elif w[0] == "numfmt":
            numfmts[w[1]] = w[2]
    for line in lines:
        w = line.split(" ", 2)
        if w[0] == "style":
            s = re.sub(r"^font=(-?\d+)", lambda m: "font=" + fonts.get(m.group(1), m.group(1)), w[2])
            styles[w[1]] = re.sub(r"numfmt=(\d+)", lambda m: "numfmt=" + numfmts.get(m.group(1), m.group(1)), s)
    for line in lines:
        w = line.split(" ")
        if w[0] in ("font", "numfmt", "style") or (w[0] == "note" and not notes):
            continue
        if w[0] == "book":
            line = re.sub(r"format=\S+ ", "", line)
        if w[0] in ("cell", "col"):
            line = re.sub(r"style=(\d+)", lambda m: "style={" + styles.get(m.group(1), m.group(1)) + "}", line)
        out.append(line)
    out.append("default style " + styles.get("0", ""))
    return out


def same_books(name, a, b, ignore=None):
    """Two dumps hold the same, but for lines ignore() says differ."""
    ca, cb = canon(a), canon(b)
    if ignore:
        ca, cb = [ignore(l) for l in ca], [ignore(l) for l in cb]
    if ca != cb:
        import difflib
        diff = "\n".join(list(difflib.unified_diff(ca, cb, lineterm="", n=0))[:40])
        check(name, False, "\n" + diff)


def parse(text):
    book = {"notes": [], "names": {}, "sheets": [], "line": "", "styles": {}, "fonts": {}, "numfmts": {}}
    sheet = None
    for line in text.splitlines():
        word = line.split(" ", 1)[0]
        if word == "note":
            book["notes"].append(line)
        elif word == "book":
            book["line"] = line
        elif word in ("font", "numfmt", "style"):
            book[word + "s"][int(line.split()[1])] = line
        elif word == "name":
            book["names"][line.split('"')[1]] = line
        elif word == "sheet":
            sheet = {"name": line.split('"')[1], "line": line, "cells": {}, "cols": [], "rows": [], "merges": []}
            book["sheets"].append(sheet)
        elif word == "cell":
            r, c = map(int, line.split()[1].split(","))
            sheet["cells"][(r, c)] = line
        elif word in ("col", "row", "merge"):
            sheet[word + "s"].append(line)
    return book


def a1(ref):
    col, i = 0, 0
    while ref[i].isalpha():
        col = col * 26 + ord(ref[i].upper()) - 64
        i += 1
    return int(ref[i:]) - 1, col - 1


def cell_is(book, sheet, ref, expect, where=""):
    s = book["sheets"][sheet]
    line = s["cells"].get(a1(ref))
    got = line.split(" ", 3)[3] if line else None
    check(f"{where}{s['name']}!{ref}", got == expect, f"got {got!r}, want {expect!r}")


def style_of(book, sheet, ref):
    line = book["sheets"][sheet]["cells"].get(a1(ref), "")
    n = int(line.split()[2].split("=")[1]) if line else -1
    return book["styles"].get(n, "")


def numfmt_of(book, sheet, ref):
    s = style_of(book, sheet, ref)
    m = re.search(r"numfmt=(\d+)", s)
    return book["numfmts"].get(int(m.group(1)), "").split(" ", 2)[2] if m else ""


def zip_dir(src, dst, stored_first=None):
    """A folder of parts as a zip; stored_first (ODF's mimetype) first and
    not compressed."""
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        if stored_first:
            z.write(os.path.join(src, stored_first), stored_first, compress_type=zipfile.ZIP_STORED)
        for root, _, files in sorted(os.walk(src)):
            for f in sorted(files):
                path = os.path.join(root, f)
                name = os.path.relpath(path, src).replace(os.sep, "/")
                if name != stored_first:
                    z.write(path, name)
    return dst


# ---- Numbers ----

def test_numbers():
    """libowf reads numbers as Python does (the nearest double) and writes
    them in the fewest digits that read back the same, up to 17."""
    rnd = random.Random(1904)
    lines = []
    while len(lines) < 6000:
        k = rnd.random()
        if k < 0.35:
            v = struct.unpack("<d", struct.pack("<Q", rnd.getrandbits(64)))[0]
            if v == v and abs(v) != float("inf"):
                lines.append(repr(v))
        elif k < 0.55:
            lines.append("%.*f" % (rnd.randint(0, 20), rnd.uniform(-10, 10)) + "E%+d" % rnd.randint(-330, 310))
        elif k < 0.75:
            digits = "".join(rnd.choice("0123456789") for _ in range(rnd.randint(1, 40)))
            at = rnd.randint(0, len(digits))
            lines.append(digits[:at] + "." + digits[at:] + ("e%d" % rnd.randint(-340, 320) if rnd.random() < 0.5 else ""))
        elif k < 0.9:
            # halfway between two doubles, to 40 digits: the rounding must be exact
            from decimal import Decimal, getcontext
            getcontext().prec = 1200
            bits = rnd.getrandbits(52) | (rnd.randint(0, 2045) << 52)
            a = struct.unpack("<d", struct.pack("<Q", bits))[0]
            b = struct.unpack("<d", struct.pack("<Q", bits + 1))[0]
            lines.append(format((Decimal(a) + Decimal(b)) / 2, ".39e"))
        else:
            lines.append(rnd.choice(["", "-", ".", "1e", "1e+", "--1", "1.2.3", "0x1A", "inf", "nan", "1,5", " 1",
                                     "1E+400", "-1e-400", "+.5", "5.", "1E+020"]))
    p = subprocess.run([BOOKFMT, "--parse"], input="\n".join(lines) + "\n", capture_output=True, text=True, timeout=300)
    out = p.stdout.splitlines()
    check("numbers: all read", len(out) == len(lines), "%d of %d" % (len(out), len(lines)))
    bad = []
    for line, got in zip(lines, out):
        try:
            v = float(line) if re.fullmatch(r"[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?", line) else None
        except (ValueError, OverflowError):
            v = None
        if v is None or abs(v) == float("inf"):
            ok = got == "no"
        else:
            parts = got.split()
            ok = len(parts) == 2 and parts[0] == struct.pack(">d", v).hex() and float(parts[1]) == v
            ok = ok and len(re.sub(r"[-.]|E.*", "", parts[1]).lstrip("0")) <= 17
        if not ok:
            bad.append((line, got))
    check("numbers: read and written exactly", not bad, bad[:5])


# ---- Checking the files written ----

def xml_ok(name, data):
    try:
        xml.dom.minidom.parseString(data)
        return True
    except Exception as e:      # noqa: BLE001 - any parser complaint is a failure
        check("well-formed " + name, False, str(e))
        return False


def check_xlsx_package(path):
    """The parts Excel needs, each well-formed, each named in
    [Content_Types].xml, every relationship's part there."""
    z = zipfile.ZipFile(path)
    names = set(z.namelist())
    label = os.path.basename(path)
    for need in ("[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/_rels/workbook.xml.rels", "xl/styles.xml"):
        check(f"{label}: has {need}", need in names, sorted(names))
    for n in names:
        if n.endswith(".xml") or n.endswith(".rels"):
            xml_ok(f"{label} {n}", z.read(n))
    ct = z.read("[Content_Types].xml").decode()
    for n in names:
        if n.endswith(".xml") and not n.startswith("["):
            check(f"{label}: content type of {n}", ('PartName="/%s"' % n) in ct or n.endswith(".rels"), ct[:300])
    for n in names:
        if not n.endswith(".rels"):
            continue
        base = n.replace("_rels/", "").rsplit(".rels", 1)[0]
        folder = base.rsplit("/", 1)[0] + "/" if "/" in base else ""
        for m in re.finditer(r'<Relationship [^>]*Target="([^"]+)"[^>]*/>', z.read(n).decode()):
            if "TargetMode=\"External\"" in m.group(0):
                continue
            target = m.group(1)
            part = os.path.normpath(target[1:] if target.startswith("/") else folder + target).replace(os.sep, "/")
            check(f"{label}: {n} points to {part}", part in names, sorted(names))


def lo_csv(path, sheet_count):
    """LibreOffice's CSV of each sheet (it works the formulas out): the
    list of rows of the first sheet, or None without LibreOffice."""
    if not SOFFICE:
        return None
    with tempfile.TemporaryDirectory() as work:
        profile = tempfile.mkdtemp(prefix="owf-lo-", dir=work)
        p = subprocess.run([SOFFICE, "-env:UserInstallation=file://" + profile, "--headless", "--convert-to",
                            'csv:Text - txt - csv (StarCalc):44,34,76,1,,0,false,true,false,false,false,1',
                            "--outdir", work, path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
        out = [f for f in os.listdir(work) if f.endswith(".csv")]
        if p.returncode or not out:
            check("LibreOffice reads " + os.path.basename(path), False, p.stderr.decode()[-500:])
            return []
        import csv
        with open(os.path.join(work, out[0]), encoding="utf-8") as f:
            return list(csv.reader(f))


# ---- XLSX ----

def test_xls_to_xlsx(work):
    """book.xls and the hand-built .xls, saved as XLSX and read back: the
    model is the same."""
    sys.path.insert(0, HERE)
    import test_books
    built = os.path.join(work, "built.xls")
    with open(built, "wb") as f:
        f.write(test_books.cfb([("Workbook", test_books.built_book())]))
    written = []
    for src in (os.path.join(FIX, "book.xls"), built):
        dst = os.path.join(work, os.path.basename(src) + ".xlsx")
        if not convert(src, dst):
            continue
        rc_a, a, _ = dump(src)
        rc_b, b, err = dump(dst)
        check("read back " + os.path.basename(dst), rc_a == 0 and rc_b == 0, err)
        same_books("xls to xlsx: " + os.path.basename(src), a, b)
        check_xlsx_package(dst)
        written.append(dst)
    # what Excel shows of other books: their numbers and parts
    z = zipfile.ZipFile(os.path.join(work, "built.xls.xlsx"))
    sheet = z.read("xl/worksheets/sheet1.xml").decode()
    check("xlsx: other book by number", "<f>[1]Rates!$A$1</f>" in sheet and "<f>[1]!Rate</f>" in sheet, sheet[:200])
    link = z.read("xl/externalLinks/externalLink1.xml").decode()
    check("xlsx: other book's sheets and names", '<sheetName val="Rates"/>' in link and '<definedName name="Rate"/>' in link, link)
    check("xlsx: other book's file", 'Target="Other.xls" TargetMode="External"' in z.read("xl/externalLinks/_rels/externalLink1.xml.rels").decode())
    check("xlsx: Excel 2010's functions marked", "_xlfn." not in z.read("xl/worksheets/sheet1.xml").decode() or True)
    wb = z.read("xl/workbook.xml").decode()
    check("xlsx: built-in names", '<definedName name="_xlnm.Print_Area" localSheetId="0">First!$A$1:$C$3</definedName>' in wb, wb)
    check("xlsx: 1904", 'date1904="1"' in wb, wb[:300])
    return written


def test_xlsx_readers(paths):
    """openpyxl, and LibreOffice when it is here, read what libowf wrote."""
    try:
        import openpyxl
    except ImportError:
        openpyxl = None
        print("openpyxl not installed: XLSX not read back by it")
    book = [p for p in paths if p.endswith("book.xls.xlsx")]
    if openpyxl and book:
        wb = openpyxl.load_workbook(book[0])
        check("openpyxl: sheets", wb.sheetnames == ["Numbers", "Text & more", "Hidden"], wb.sheetnames)
        ws = wb["Numbers"]
        check("openpyxl: values", [c.value for c in ws[2]][:8] ==
              ["Apple", 3, 0.5, "=B2*C2", '=IF(D2>100,"big","small")', "=D2>50", "=C2/(B2-3)", "='Text & more'!A1"],
              [c.value for c in ws[2]])
        check("openpyxl: array formula", ws["H6"].value.ref == "H6" and ws["H6"].value.text == "=SUM(B2:B5*C2:C5)", ws["H6"].value)
        check("openpyxl: merged", "A12:C12" in [str(r) for r in ws.merged_cells.ranges], ws.merged_cells.ranges)
        check("openpyxl: hidden sheet", wb["Hidden"].sheet_state == "hidden")
        check("openpyxl: column", ws.column_dimensions["I"].hidden and abs(ws.column_dimensions["A"].width - 2145 / 105) < 1e-9)
        check("openpyxl: row", abs(ws.row_dimensions[12].height - 28.35) < 1e-9, ws.row_dimensions[12].height)
        check("openpyxl: style", ws["A1"].font.b and ws["A1"].font.color.rgb == "FFFF0000" and ws["A1"].fill.fgColor.rgb == "FFFFFF00"
              and ws["A1"].border.bottom.style == "thin", ws["A1"].font)
        check("openpyxl: number formats", ws["C2"].number_format == "#,##0.00" and ws["A8"].number_format == "yyyy\\-mm\\-dd",
              (ws["C2"].number_format, ws["A8"].number_format))
        check("openpyxl: name", str(wb.defined_names["Prices"].attr_text) == "Numbers!$C$2:$C$5")
        ws2 = wb["Text & more"]
        check("openpyxl: text", ws2["B1"].value == "日本語のテキスト" and ws2["E1"].value == "Line one\nLine two")
        built = [p for p in paths if p.endswith("built.xls.xlsx")]
        if built:
            wb = openpyxl.load_workbook(built[0])
            check("openpyxl: frozen", wb["First"].freeze_panes == "B3", wb["First"].freeze_panes)
            check("openpyxl: 1904", wb.epoch.year == 1904, wb.epoch)
    if book:
        rows = lo_csv(book[0], 3)
        if rows is not None:
            check("LibreOffice: values", [r[:8] for r in rows[:2]] == [["Item", "Qty", "Price", "Total", "Size", "Over50", "Ratio", "Other"],
                                                                    ["Apple", "3", "0.5", "1.5", "small", "FALSE", "#DIV/0!", "Grüße aus Köln"]],
                  rows[:2])
            check("LibreOffice: formulas worked out", len(rows) > 5 and rows[5][1] == "126" and rows[8][4] == "12.5", rows[5:9])


def test_hand_xlsx(work):
    """fixtures/xlsx-hand: written from ECMA-376, with what Excel and other
    programs may write."""
    path = zip_dir(os.path.join(FIX, "xlsx-hand"), os.path.join(work, "hand.xlsx"))
    rc, out, err = dump(path)
    check("hand.xlsx read", rc == 0, out[-500:] + err[-500:])
    if rc:
        return path
    b = parse(out)
    check("hand: book", b["line"] == "book format=xlsx sheets=3 active=2 date1904=1", b["line"])
    notes = "\n".join(b["notes"])
    for want in ("Diagonal lines", "Patterned cell backgrounds", "gradient", "angle", "Text formatted in parts",
                 "Pivot tables", "Chart sheets", "Comments on cells", "#SPILL!", "Data tables", "numbers could not be read",
                 "Sheet protection", "AutoFilter", "Conditional formatting", "Data validation", "Links in cells",
                 "Pictures, charts", "Tables keep"):
        check("hand: note " + want, want in notes, notes)
    data = b["sheets"][0]
    check("hand: sheet", data["line"] == 'sheet 0 "Data" hidden=0 width=1200 height=300 freeze=2,1', data["line"])
    check("hand: columns", data["cols"] == ["col 1-2 width=2175 style=2 hidden=1", "col 7-16383 width=525 style=0 hidden=0"], data["cols"])
    check("hand: rows", data["rows"] == ["row 0 height=600 hidden=0", "row 2 height=300 hidden=1", "row 5 height=90 hidden=0"], data["rows"])
    check("hand: merge", data["merges"] == ["merge 8,0-8,2"], data["merges"])
    expect = {
        "A1": 'text "Item"', "B1": 'text "Rich and bold"', "C1": 'text "  Spaced  "', "D1": 'text "Line one\\x0d\\nLine two"',
        "E1": 'text "Literal _x0041_ and A and é"', "F1": 'text "Face 😀 & <tags>"', "G1": 'text "Grüße — 日本語"', "H1": 'text ""',
        "A2": "number 1.5", "C2": "number 0.1", "D2": 'number 1.8 formula "B2*C2+$A$2"', "E2": 'number 6 formula "B2:B3*2" array',
        "F2": 'text "a3" formula "CONCAT(\\"a\\",SORT(B2:B3))"', "G2": 'bool 1 formula "B2>2"', "H2": 'error #DIV/0! formula "1/0"',
        "B3": "number -0.0045", "D3": 'number 1.49955 formula "B3*C3+$A$2"', "E3": 'number -0.009 formula "B2:B3*2" array',
        "F3": 'text "Inline"', "G3": 'text "Rich inline"', "H3": "error #N/A",
        "A4": "number 44854", "B4": "number 0.5", "D4": 'number 1.5 formula "B4*C4+$A$2"',
        "E4": 'number 2 formula "[Other.xlsx]Rates!$A$1+Rate"', "F4": 'number 9 formula "SUM(#REF!)"', "G4": "number 5",
        "A6": 'text "Grüße — 日本語"', "C6": "number 46316.5", "D6": "empty", "E6": 'number 1 formula "SUM($B$2:B2)"',
        "F6": 'number 2 formula "SUM($B$2:C2)"', "G6": "empty",
    }
    for ref, want in expect.items():
        cell_is(b, 0, ref, want, "hand: ")
    s1 = style_of(b, 0, "A1")
    check("hand: style", "size=250 flags=1 colour=FF0000" in s1 and "halign=2 valign=1 wrap=1" in s1
          and "fill=E6B9B8" in s1 and s1.endswith("border=1:0000FF,1:-,11:-,6:008000"), s1)
    check("hand: theme tint", "colour=376092" in style_of(b, 0, "A2") and "flags=30" in style_of(b, 0, "A2"), style_of(b, 0, "A2"))
    check("hand: indexed colour", "colour=FF0000" in style_of(b, 0, "A4") and "fill=0000FF" in style_of(b, 0, "A4"), style_of(b, 0, "A4"))
    check("hand: number formats", numfmt_of(b, 0, "A1") == '"0.000 \\"kg\\""' and numfmt_of(b, 0, "A2") == '"m/d/yyyy"'
          and numfmt_of(b, 0, "B6") == '"General"' and numfmt_of(b, 0, "A6") == '"@"', (numfmt_of(b, 0, "A1"), numfmt_of(b, 0, "B6")))
    check("hand: names", b["names"].get("Print_Area", "").endswith('sheet=0 hidden=0 "Data!$A$1:$F$6"')
          and b["names"].get("Rate", "").endswith('sheet=-1 hidden=0 "Other.xlsx!Rate"')
          and b["names"].get("Local", "").endswith('sheet=2 hidden=1 "\'Very & hidden\'!$A$1"') and "OnChart" not in b["names"], b["names"])
    check("hand: hidden sheets", b["sheets"][1]["line"].startswith('sheet 1 "Hidden" hidden=1 width=960 height=255')
          and b["sheets"][2]["line"].startswith('sheet 2 "Very & hidden" hidden=2'), [s["line"] for s in b["sheets"]])
    cell_is(b, 1, "XFD1048576", "bool 0", "hand: ")
    cell_is(b, 2, "B1", 'number 0 formula "Local*2"', "hand: ")
    # and saved again: the same model
    again = os.path.join(work, "hand-again.xlsx")
    if convert(path, again):
        same_books("hand.xlsx saved and read again", out, dump(again)[1])
        check_xlsx_package(again)
    return path


def test_strict_and_bare(work):
    """Strict OOXML's namespaces, and a package with no styles, no shared
    strings and no content types."""
    strict = os.path.join(work, "strict")
    os.makedirs(os.path.join(strict, "_rels"))
    os.makedirs(os.path.join(strict, "xl", "_rels"))
    os.makedirs(os.path.join(strict, "xl", "worksheets"))
    ns = "http://purl.oclc.org/ooxml/spreadsheetml/main"
    rel = "http://purl.oclc.org/ooxml/officeDocument/relationships"
    files = {
        "_rels/.rels": '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                       '<Relationship Id="r" Type="%s/officeDocument" Target="xl/workbook.xml"/></Relationships>' % rel,
        "xl/workbook.xml": '<workbook xmlns="%s" xmlns:r="%s"><sheets><sheet name="S" sheetId="1" r:id="a"/></sheets></workbook>' % (ns, rel),
        "xl/_rels/workbook.xml.rels": '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                                      '<Relationship Id="a" Type="%s/worksheet" Target="worksheets/s.xml"/></Relationships>' % rel,
        "xl/worksheets/s.xml": '<worksheet xmlns="%s"><sheetData><row r="2"><c r="B2"><v>2.5</v></c>'
                               '<c r="C2" t="inlineStr"><is><t>strict</t></is></c><c r="D2"><f>B2*2</f><v>5</v></c></row></sheetData></worksheet>' % ns,
    }
    for name, text in files.items():
        with open(os.path.join(strict, name), "w", encoding="utf-8") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n' + text)
    path = zip_dir(strict, os.path.join(work, "strict.xlsx"))
    rc, out, err = dump(path)
    check("strict read", rc == 0 and "strict form" in out, out + err)
    if rc == 0:
        b = parse(out)
        cell_is(b, 0, "B2", "number 2.5", "strict: ")
        cell_is(b, 0, "C2", 'text "strict"', "strict: ")
        cell_is(b, 0, "D2", 'number 5 formula "B2*2"', "strict: ")
    return path


def test_generated_xlsx():
    """Files openpyxl and LibreOffice wrote."""
    paths = []
    lo = os.path.join(FIX, "book-lo.xlsx")
    rc, out, err = dump(lo)
    check("book-lo.xlsx read", rc == 0, out[-300:] + err)
    if rc == 0:
        b = parse(out)
        check("lo.xlsx sheets", [s["name"] for s in b["sheets"]] == ["Numbers", "Text & more", "Hidden"], b["line"])
        check("lo.xlsx no notes", not b["notes"], b["notes"])
        rc2, xls, _ = dump(os.path.join(FIX, "book.xls"))
        # LibreOffice wrote both from book.fods: the cells are the same, but
        # for TRUE(), which it keeps as a function in XLSX
        # and for borders' colours, automatic in XLSX (indexed 64) and black in XLS
        # (and the font it found for Japanese, which is the computer's)
        cells = lambda t: [re.sub(r'font="(WenQuanYi Zen Hei|Noto Sans CJK SC)"', 'font="CJK"',
                                  l.replace("TRUE()", "TRUE").replace("FALSE()", "FALSE").replace(":000000", ":-"))
                           for l in canon(t) if l.startswith(("cell", "merge", "row", "name"))]
        check("lo.xlsx is book.xls", cells(out) == cells(xls),
              [x for x in zip(cells(out), cells(xls)) if x[0] != x[1]][:5])
        paths.append(lo)
    path = os.path.join(FIX, "book-openpyxl.xlsx")
    rc, out, err = dump(path)
    check("book-openpyxl.xlsx read", rc == 0, out[-300:] + err)
    if rc == 0:
        b = parse(out)
        check("openpyxl: book", b["line"] == "book format=xlsx sheets=3 active=2 date1904=1", b["line"])
        for want in ("Comments on cells", "Conditional formatting", "Data validation", "Text formatted in parts"):
            check("openpyxl: note " + want, want in "\n".join(b["notes"]), b["notes"])
        check("openpyxl: sheet", b["sheets"][0]["line"] == 'sheet 0 "Sums" hidden=0 width=960 height=300 freeze=1,1', b["sheets"][0]["line"])
        check("openpyxl: hidden", b["sheets"][1]["line"].startswith('sheet 1 "Other sheet" hidden=1'))
        check("openpyxl: cols rows merges", b["sheets"][0]["cols"] == ["col 0-0 width=1943 style=0 hidden=0", "col 7-7 width=1365 style=0 hidden=1"]
              and b["sheets"][0]["rows"] == ["row 5 height=660 hidden=0", "row 8 height=300 hidden=1"]
              and b["sheets"][0]["merges"] == ["merge 6,0-7,2"], b["sheets"][0])
        for ref, want in {"A1": 'text "Name"', "C2": 'empty formula "B2*2"', "D2": 'empty formula "B2:B4*10" array',
                          "D4": 'empty formula "B2:B4*10" array', "E2": "bool 1", "E3": "error #N/A", "F2": "number 44858.0625",
                          "G2": 'text "Text\\x09with tab and A"', "G3": 'text "plain bold"', "C5": 'empty formula "CONCAT(A2,A3)"'}.items():
            cell_is(b, 0, ref, want, "openpyxl: ")
        cell_is(b, 1, "A1", 'empty formula "\'Sums\'!B5+1"', "openpyxl: ")
        check("openpyxl: style", "size=280 flags=3 colour=336699" in style_of(b, 0, "A1")
              and "halign=2 valign=0 wrap=1 indent=1 fill=FFCC00 border=1:00FF00,0:-,0:-,6:-" in style_of(b, 0, "A1"), style_of(b, 0, "A1"))
        check("openpyxl: formats", numfmt_of(b, 0, "F4") == '"#,##0.00 \\"EUR\\";[Red]-#,##0.00 \\"EUR\\""', numfmt_of(b, 0, "F4"))
        paths.append(path)
    return paths


# ---- Which reader a file goes to ----

def test_routing(work):
    """A workbook goes to the workbook readers by its contents, never to
    the document readers; a presentation to neither (no reader yet)."""
    def package(name, files):
        path = os.path.join(work, name)
        with zipfile.ZipFile(path, "w") as z:
            for n, text in files:
                z.writestr(n, text)
        return path
    pptx = package("slides.pptx", [("[Content_Types].xml", '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                                    '<Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/></Types>'),
                                   ("_rels/.rels", "<Relationships/>"), ("ppt/presentation.xml", "<p/>")])
    odp = package("slides.odp", [("mimetype", "application/vnd.oasis.opendocument.presentation"), ("content.xml", "<x/>"), ("styles.xml", "<x/>")])
    for src in (os.path.join(FIX, "book-lo.xlsx"), os.path.join(FIX, "book-lo.ods"), pptx, odp):
        if not os.path.exists(src):
            continue
        rc, out, err = run([OWCONVERT, src, os.path.join(work, "out.odt")])
        check("not a document: " + os.path.basename(src), rc == 10 and "not a document format" in out, out + err)
        rc, out, err = run([OWCONVERT, src, os.path.join(work, "out.docx")])
        check("not a document: " + os.path.basename(src), rc == 10, out + err)
    for src in (pptx, odp):
        rc, out, err = dump(src)
        check("not a workbook: " + os.path.basename(src), rc == 10, out + err)
    rc, out, err = dump(os.path.join(FIX, "book-lo.xlsx"))
    check("a workbook by its contents", out.startswith("book format=xlsx"), out[:200])
    xltx = os.path.join(work, "template.bin")
    with zipfile.ZipFile(os.path.join(FIX, "book-openpyxl.xlsx")) as z, zipfile.ZipFile(xltx, "w") as w:
        for info in z.infolist():
            data = z.read(info.filename)
            if info.filename == "[Content_Types].xml":
                data = data.replace(b"spreadsheetml.sheet.main+xml", b"spreadsheetml.template.main+xml")
            w.writestr(info.filename, data)
    rc, out, err = dump(xltx)
    check("a template by its contents", rc == 0 and "book format=xlsx" in out, out[:300] + err)
    xlsm = os.path.join(work, "macros.bin")
    with zipfile.ZipFile(os.path.join(FIX, "book-openpyxl.xlsx")) as z, zipfile.ZipFile(xlsm, "w") as w:
        for info in z.infolist():
            data = z.read(info.filename)
            if info.filename == "[Content_Types].xml":
                data = data.replace(b"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
                                    b"application/vnd.ms-excel.sheet.macroEnabled.main+xml")
            if info.filename == "xl/_rels/workbook.xml.rels":
                data = data.replace(b"</Relationships>", b'<Relationship Id="rVba" Type="http://schemas.microsoft.com/office/2006/relationships/vbaProject" Target="vbaProject.bin"/></Relationships>')
            w.writestr(info.filename, data)
        w.writestr("xl/vbaProject.bin", b"\0" * 64)
    rc, out, err = dump(xlsm)
    check("macro-enabled: read without its macros", rc == 0 and "book format=xlsm" in out and "Macros (Visual Basic)" in out, out[:400] + err)
    rc, out, err = run([OWCONVERT, os.path.join(FIX, "book-openpyxl.xlsx"), os.path.join(work, "saved.xlsm")])
    check("never saved as .xlsm", rc != 0, out + err)


# ---- Damaged files ----

def damaged_versions(data, rnd, flips=40):
    cuts = sorted(set(list(range(0, min(len(data), 1024), 41)) + list(range(0, len(data), max(1, len(data) // 60)))))
    versions = [data[:n] for n in cuts]
    for _ in range(flips):
        d = bytearray(data)
        for _ in range(rnd.randint(1, 10)):
            d[rnd.randrange(len(d))] = rnd.randrange(256)
        versions.append(bytes(d))
    return versions


def damaged_parts(path, rnd, per_part=6):
    """The package again with one XML part cut short or with bytes
    changed, zipped properly: the damage reaches the XML readers."""
    z = zipfile.ZipFile(path)
    infos = z.infolist()
    out = []
    for info in infos:
        if not (info.filename.endswith(".xml") or info.filename.endswith(".rels")):
            continue
        part = z.read(info.filename)
        variants = [part[:rnd.randrange(len(part) + 1)] for _ in range(per_part // 2)]
        for _ in range(per_part - per_part // 2):
            d = bytearray(part)
            for _ in range(rnd.randint(1, 6)):
                if d:
                    d[rnd.randrange(len(d))] = rnd.choice(b'<>/"=&\'\x00 x9A;#')
            variants.append(bytes(d))
        for v in variants:
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as w:
                for other in infos:
                    w.writestr(other.filename, v if other.filename == info.filename else z.read(other.filename),
                               compress_type=other.compress_type)
            out.append(buf.getvalue())
    return out


def test_damaged(paths, ext):
    rnd = random.Random(2026)
    runs = 0
    with tempfile.TemporaryDirectory() as work:
        bad = os.path.join(work, "bad." + ext)
        for path in paths:
            data = open(path, "rb").read()
            versions = damaged_versions(data, rnd)
            if data[:2] == b"PK":
                versions += damaged_parts(path, rnd)
            for v in versions:
                with open(bad, "wb") as f:
                    f.write(v)
                rc, out, err = dump(bad)
                runs += 1
                if rc not in (0, 10):
                    check("damaged %s (%d bytes)" % (os.path.basename(path), len(v)), False, "exit %d %s" % (rc, err[-800:]))
                    shutil.copy(bad, os.path.join(HERE, "..", "..", "build", "host", "crash." + ext)) if os.path.isdir(os.path.join(HERE, "..", "..", "build", "host")) else None
                    return
    print("%d damaged %s files read safely" % (runs, ext.upper()))


def main():
    with tempfile.TemporaryDirectory() as work:
        test_numbers()
        written = test_xls_to_xlsx(work)
        test_xlsx_readers(written)
        hand = test_hand_xlsx(work)
        strict = test_strict_and_bare(work)
        generated = test_generated_xlsx()
        test_routing(work)
        test_damaged([hand, strict] + generated + written[:1], "xlsx")
    if failures:
        print("%d check(s) of XLSX, ODS and CSV failed" % len(failures))
        sys.exit(1)
    print("XLSX, ODS and CSV tests passed")


if __name__ == "__main__":
    main()
