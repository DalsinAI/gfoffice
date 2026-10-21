#!/usr/bin/env python3
"""The workbook tests: test_books.py path/to/book_dump [path/to/owconvert]

Reads fixtures/book.xls (saved by LibreOffice from book.fods, see
make_fixtures.py) and a workbook this script builds itself from
[MS-XLS] and [MS-CFB], for the records LibreOffice does not write: frozen
panes, RK and MULRK numbers, rich and split shared strings, add-in and
other-book references, 3-D ranges, deleted sheets, unions, array
constants, a chart sheet. Checks what book_dump prints. Then feeds every
cut-short and damaged copy to book_dump, which must answer with a book or
an error, never a crash (make book-check runs it under AddressSanitizer).
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import random
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.abspath(sys.argv[1])
OWCONVERT = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else None
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name} {detail}")


def dump(path):
    p = subprocess.run([DUMP, path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def parse(text):
    """The dump as {"sheets": [{"name", "line", "cells": {(r, c): line}, ...}], ...}."""
    book = {"notes": [], "names": {}, "styles": {}, "fonts": {}, "numfmts": {}, "sheets": [], "line": ""}
    sheet = None
    for line in text.splitlines():
        word = line.split(" ", 1)[0]
        if word == "note":
            book["notes"].append(line)
        elif word == "book":
            book["line"] = line
        elif word in ("font", "numfmt", "style"):
            book[word + ("s" if word != "font" else "s")][int(line.split()[1])] = line
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
    col = 0
    i = 0
    while ref[i].isalpha():
        col = col * 26 + ord(ref[i].upper()) - 64
        i += 1
    return int(ref[i:]) - 1, col - 1


def cell_is(book, sheet, ref, expect):
    """expect: what follows "style=N " in the dump, e.g. 'number 3' or
    'text "x" formula "A1"'."""
    s = book["sheets"][sheet]
    line = s["cells"].get(a1(ref))
    got = line.split(" ", 3)[3] if line else None
    check(f"{s['name']}!{ref}", got == expect, f"got {got!r}, want {expect!r}")


def style_of(book, sheet, ref):
    line = book["sheets"][sheet]["cells"].get(a1(ref), "")
    n = int(line.split()[2].split("=")[1]) if line else -1
    return book["styles"].get(n, "")


# ---- The LibreOffice-made workbook ----

def test_libreoffice_book():
    rc, out, err = dump(os.path.join(HERE, "fixtures", "book.xls"))
    check("book.xls read", rc == 0, out[-300:] + err[-300:])
    if rc:
        return
    b = parse(out)
    check("book.xls sheets", [s["name"] for s in b["sheets"]] == ["Numbers", "Text & more", "Hidden"], b["line"])
    check("book.xls no notes", not b["notes"], b["notes"])
    check("hidden sheet", "hidden=1" in b["sheets"][2]["line"], b["sheets"][2]["line"])
    check("defined name", b["names"].get("Prices", "").endswith('sheet=-1 hidden=0 "Numbers!$C$2:$C$5"'), b["names"])
    expect = {
        "A1": 'text "Item"', "B2": "number 3", "C2": "number 0.5",
        "D2": 'number 1.5 formula "B2*C2"', "D5": 'number -7E+20 formula "B5*C5"',
        "E2": 'text "small" formula "IF(D2>100,\\"big\\",\\"small\\")"',
        "E4": 'text "big" formula "IF(D4>100,\\"big\\",\\"small\\")"',
        "F2": 'number 0 formula "D2>50"', "G2": 'error #DIV/0! formula "C2/(B2-3)"',
        "G3": 'number 0.178571428571429 formula "C3/(B3-3)"',
        "H2": 'text "Grüße aus Köln" formula "\'Text & more\'!A1"',
        "A5": 'text "Fig, dried"', "C5": "number 1E+20",
        "B6": 'number 126 formula "SUM(B2:B5)"', "E6": 'number 31.5 formula "ROUND(AVERAGE(B2:B5),2)"',
        "F6": 'number 2 formula "COUNTIF(B2:B5,\\">5\\")"', "G6": 'number 1E+20 formula "SUM(Prices)"',
        "H6": 'number -7E+20 formula "SUM(B2:B5*C2:C5)" array',
        "A8": "number 46316", "B8": "number 0.573263888888889", "C8": "number 0.25",
        "G8": 'number 269 formula "-B2^2+(B3+B4)*2"', "H8": 'number -13 formula "-(B2+B3)"',
        "A9": 'text "Apple & Pear" formula "CONCATENATE(A2,\\" & \\",A3)"',
        "B9": 'text "Apple-Pear" formula "A2&\\"-\\"&A3"',
        "E9": 'number 12.5 formula "VLOOKUP(\\"Pear\\",A2:D5,4,0)"',
        "F9": 'number 120 formula "INDEX(B2:B5,MATCH(\\"Plum\\",A2:A5,0))"',
        "G9": 'text "none" formula "IFERROR(1/0,\\"none\\")"',
        "H9": 'number 253.373263888889 formula "SUM(B:B)"',
        "A10": 'number 6 formula "SUM({1,2,3})"', "B10": 'number 0.5 formula "50%"',
        "D10": 'bool 0 formula "NOT(F3)"', "F10": 'number 3.14159265358979 formula "PI()"',
        "G10": 'number 64 formula "2^3^2"',
        "A12": 'text "Merged title"', "B13": 'number 0.3 formula "0.1+0.2"',
        "D13": "number -0.000123", "E13": "number 123456789012345", "I13": "number 42",
    }
    for ref, want in expect.items():
        cell_is(b, 0, ref, want)
    numbers = b["sheets"][0]
    check("merge", "merge 11,0-11,2" in numbers["merges"], numbers["merges"])
    check("hidden column", any(c.startswith("col 8-8 ") and c.endswith("hidden=1") for c in numbers["cols"]), numbers["cols"])
    check("wide column", any(c.startswith("col 0-0 width=2145") for c in numbers["cols"]), numbers["cols"])
    check("tall row", "row 11 height=567 hidden=0" in numbers["rows"], numbers["rows"])
    head = style_of(b, 0, "A1")
    check("header style", "flags=1 colour=FF0000" in head and "fill=FFFF00" in head and head.endswith(",1:000000"), head)
    money = style_of(b, 0, "C2")
    check("money format", money and b["numfmts"].get(int(money.split("numfmt=")[1].split()[0]), "").endswith('"#,##0.00"'), money)
    date = style_of(b, 0, "A8")
    check("date format", date and "yyyy" in b["numfmts"].get(int(date.split("numfmt=")[1].split()[0]), ""), date)
    serif = style_of(b, 0, "A13")
    check("serif font", "size=280 flags=6" in serif and 'font 1 "Times New Roman" kind=1' in b["fonts"].get(1, ""), serif)
    title = style_of(b, 0, "A12")
    check("centred title", "halign=2 valign=1" in title, title)
    boxed = style_of(b, 0, "G13")
    check("boxed", boxed.endswith("border=1:0000FF,1:0000FF,1:0000FF,1:0000FF"), boxed)

    text = b["sheets"][1]
    cell_is(b, 1, "A1", 'text "Grüße aus Köln"')
    cell_is(b, 1, "B1", 'text "日本語のテキスト"')
    cell_is(b, 1, "C1", 'text "Emoji 😀 here"')
    cell_is(b, 1, "D1", 'text "Zoë\'s \\"quote\\""')
    cell_is(b, 1, "E1", 'text "Line one\\nLine two"')
    check("wrap", "wrap=1" in style_of(b, 1, "E1"), style_of(b, 1, "E1"))
    cell_is(b, 1, "C2", 'number -1.4E+21 formula "Numbers!D6*2"')
    cell_is(b, 1, "C3", 'number 126 formula "SUM(Numbers!B2:B5)"')
    cell_is(b, 1, "C4", 'text "Apple from Numbers" formula "Numbers!A2&\\" from Numbers\\""')
    wide = "".join("Euro €%d " % i for i in range(1500))
    narrow = "".join("plain %d " % i for i in range(1500))
    cell_is(b, 1, "B2", 'text "%s"' % wide)
    cell_is(b, 1, "B3", 'text "%s"' % narrow)
    for n in (1, 150, 399):
        cell_is(b, 1, "A%d" % (n + 1), 'text "Row %d: words to fill the shared strings"' % n)
    check("all rows", len([k for k in text["cells"] if k[1] == 0]) == 400, len(text["cells"]))

    if OWCONVERT:
        with tempfile.TemporaryDirectory() as work:
            out_csv = os.path.join(work, "numbers.csv")
            p = subprocess.run([OWCONVERT, os.path.join(HERE, "fixtures", "book.xls"), out_csv],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            check("owconvert xls to csv", p.returncode == 0, p.stdout + p.stderr)
            if p.returncode == 0:
                csv = open(out_csv, "rb").read().decode("utf-8")
                lines = csv.split("\r\n")
                check("csv header", lines[0] == "Item,Qty,Price,Total,Size,Over50,Ratio,Other,", lines[0])
                check("csv quoting", lines[4] == '"Fig, dried",-7,1E+20,-7E+20,small,0,-1E+19,,', lines[4])
                check("csv error and text", lines[1] == "Apple,3,0.5,1.5,small,0,#DIV/0!,Grüße aus Köln,", lines[1])
                check("csv rows", len(lines) == 14 and lines[-1] == "", len(lines))


# ---- A workbook built here, record by record ----

def rec(rtype, data=b""):
    return struct.pack("<HH", rtype, len(data)) + data


def u16str(s, length_bytes=2):
    """An XLUnicodeString: compressed when it can be."""
    try:
        raw, flags = s.encode("latin-1"), 0
    except UnicodeEncodeError:
        raw, flags = s.encode("utf-16-le"), 1
    n = len(s.encode("utf-16-le")) // 2
    return (struct.pack("<H", n) if length_bytes == 2 else bytes([n])) + bytes([flags]) + raw


def short_str(s):
    return u16str(s, 1)


def cellhdr(row, col, xf=15):
    return struct.pack("<HHH", row, col, xf)


def formula(row, col, rgce, value=0.0, xf=15, extra=b"", result=None):
    """result: None for a number, or ("str"|"bool"|"err"|"empty", v)."""
    if result is None:
        val = struct.pack("<d", value)
    else:
        kind, v = result
        val = bytes([{"str": 0, "bool": 1, "err": 2, "empty": 3}[kind], 0, v, 0, 0, 0, 0xFF, 0xFF])
    return rec(0x0006, cellhdr(row, col, xf) + val + struct.pack("<HI", 0, 0) + struct.pack("<H", len(rgce)) + rgce + extra)


REL = 0xC000      # a reference's column field: row and column relative


def ref(row, col, flags=REL):
    return bytes([0x44]) + struct.pack("<HH", row, col | flags)


def area(r0, r1, c0, c1, flags=REL):
    return bytes([0x25]) + struct.pack("<HHHH", r0, r1, c0 | flags, c1 | flags)


def funcvar(argc, tab):
    return bytes([0x42, argc]) + struct.pack("<H", tab)


def pstr(s):
    return bytes([0x17]) + short_str(s)


def pint(n):
    return bytes([0x1E]) + struct.pack("<H", n)


def xf(font=0, fmt=0, style=False, align=0, rot=0, indent=0, borders=0, colours=0, fill=0, used=0):
    t = 0xFFF4 if style else 0x0000
    return rec(0x00E0, struct.pack("<HHHBBBBIIH", font, fmt, t | (0x0001 if not style else 0), align, rot, indent, used, borders, colours, fill))


def font(name, height=200, grbit=0, icv=0x7FFF, weight=400, script=0, underline=0, family=2):
    return rec(0x0031, struct.pack("<HHHHHBBBB", height, grbit, icv, weight, script, underline, family, 0, 0) + short_str(name))


def bof(dt):
    return rec(0x0809, struct.pack("<HHHHII", 0x0600, dt, 0x0DBB, 0x07CC, 0x000100C1, 0x00000206))


def rk_int(n):
    return ((n & 0x3FFFFFFF) << 2) | 2


def built_book(filepass=False, biff5=False):
    """The globals, then the sheets; BOUNDSHEET offsets filled in after."""
    def boundsheet(offset, name, dt=0, hidden=0):
        return rec(0x0085, struct.pack("<IBB", offset, hidden, dt) + short_str(name))

    first = [bof(0x0010),
             rec(0x0055, struct.pack("<H", 10)),                       # DEFCOLWIDTH
             rec(0x0225, struct.pack("<HH", 0, 300)),                  # DEFAULTROWHEIGHT
             rec(0x007D, struct.pack("<HHHHHH", 1, 2, 512, 16, 1, 0)),  # COLINFO B:C hidden
             rec(0x0208, struct.pack("<HHHHHHI", 1, 0, 6, 600, 0, 0, 0x0100 | 0x40)),  # ROW 2 tall
             rec(0x00FD, cellhdr(0, 0) + struct.pack("<I", 0)),
             rec(0x00FD, cellhdr(0, 1) + struct.pack("<I", 1)),
             rec(0x00FD, cellhdr(0, 2) + struct.pack("<I", 2)),
             rec(0x00FD, cellhdr(0, 3) + struct.pack("<I", 3)),
             rec(0x00FD, cellhdr(0, 4) + struct.pack("<I", 4)),
             rec(0x00FD, cellhdr(0, 5) + struct.pack("<I", 99)),        # no such string
             rec(0x027E, cellhdr(1, 0) + struct.pack("<I", rk_int(1234))),
             rec(0x027E, cellhdr(1, 1) + struct.pack("<I", rk_int(12345) | 1)),
             rec(0x027E, cellhdr(1, 2) + struct.pack("<I", 0x40040000)),  # 2.5
             rec(0x027E, cellhdr(1, 3) + struct.pack("<I", rk_int(-5))),
             rec(0x00BD, struct.pack("<HH", 2, 0) + b"".join(struct.pack("<HI", 15, rk_int(n)) for n in (1, 2, 3)) + struct.pack("<H", 2)),
             rec(0x00BE, struct.pack("<HH", 2, 3) + struct.pack("<HHH", 16, 16, 16) + struct.pack("<H", 5)),
             rec(0x0205, cellhdr(3, 0) + bytes([1, 0])),
             rec(0x0205, cellhdr(3, 1) + bytes([0x2A, 1])),
             rec(0x0203, cellhdr(4, 0) + struct.pack("<d", 1e-300)),
             rec(0x0203, cellhdr(4, 1, 17) + struct.pack("<d", 45000.5)),
             rec(0x0204, cellhdr(4, 2) + u16str("Ünïcode label ✓")),
             # SUM(A2:B2) through tAttrSum, with a space
             formula(5, 0, area(1, 1, 0, 1) + bytes([0x19, 0x40, 0x00, 0x01]) + bytes([0x19, 0x10, 0, 0]), 13579),
             # IFERROR(1/0,"x"), an add-in function by name
             formula(5, 1, bytes([0x59]) + struct.pack("<HI", 3, 1) + pint(1) + pint(0) + bytes([0x06]) + pstr("x") + funcvar(3, 255),
                     result=("str", 0)),
             rec(0x0207, u16str("x")),
             # SUM over two sheets
             formula(5, 2, bytes([0x3B]) + struct.pack("<HHHHH", 2, 0, 2, REL, REL) + funcvar(1, 4), 15),
             formula(5, 3, bytes([0x3A]) + struct.pack("<HHH", 5, 0, 0)),                   # a deleted sheet
             formula(5, 4, bytes([0x3A]) + struct.pack("<HHH", 4, 0, 0)),                   # another book
             formula(5, 5, bytes([0x39]) + struct.pack("<HI", 4, 1)),                        # a name there
             # SUM(A1:B2) through PtgMemArea
             formula(6, 0, bytes([0x26]) + struct.pack("<IH", 0, 9) + area(0, 1, 0, 1) + funcvar(1, 4), 0,
                     extra=struct.pack("<H", 1) + struct.pack("<HHHH", 0, 1, 0, 1)),
             formula(6, 1, area(1, 2, 0, 0xFF) + funcvar(1, 4)),                          # whole rows
             formula(6, 2, ref(0, 0) + ref(0, 1) + bytes([0x10, 0x15]) + funcvar(1, 4)),    # SUM((A1,B1))
             formula(6, 3, ref(0, 0) + ref(0, 1) + bytes([0x10]) + funcvar(1, 4)),          # no brackets written
             formula(6, 4, bytes([0x40]) + bytes(7) + funcvar(1, 4), 0,
                     extra=bytes([1]) + struct.pack("<H", 1) + bytes([1]) + struct.pack("<d", 1.5) + bytes([2]) + u16str("a\"b")
                     + bytes([4, 1]) + bytes(7) + bytes([16, 0x07]) + bytes(7)),
             formula(6, 5, ref(1, 0) + ref(1, 1) + bytes([0x03]) + ref(1, 2) + bytes([0x05])
                     + ref(1, 0) + ref(1, 1) + ref(1, 2) + bytes([0x04, 0x04]) + bytes([0x03])),
             formula(7, 0, bytes([0x02]) + struct.pack("<HH", 7, 0), 1),                     # a data table
             formula(7, 1, bytes([0x18, 0x00]), 2),                                           # a token we don't know
             formula(7, 2, bytes([0x1D, 1]), result=("bool", 1)),
             formula(7, 3, bytes([0x1C, 0x0F]), result=("err", 0x0F)),
             formula(7, 4, pstr(""), result=("empty", 0)),
             formula(7, 5, bytes([0x43]) + struct.pack("<I", 1) + funcvar(1, 4), 10),
             rec(0x00E5, struct.pack("<H", 1) + struct.pack("<HHHH", 9, 10, 0, 1)),        # MERGEDCELLS A10:B11
             rec(0x001C, cellhdr(0, 0)[:4] + struct.pack("<HH", 0, 1) + short_str("me")),  # NOTE
             rec(0x005D, struct.pack("<HHHH", 0x15, 0x12, 0x08, 1) + bytes(14)),          # OBJ, a picture
             rec(0x023E, struct.pack("<HHHI", 0x06B6 | 0x08, 0, 0, 64) + bytes(8)),       # WINDOW2, frozen
             rec(0x0041, struct.pack("<HHHHB", 1, 2, 2, 1, 0)),                           # PANE
             rec(0x000A)]
    second = [bof(0x0010), rec(0x0203, cellhdr(0, 0) + struct.pack("<d", 7)),
              rec(0x0203, cellhdr(2, 0) + struct.pack("<d", 8)), rec(0x000A)]
    chart = [bof(0x0020), rec(0x000A)]

    # shared strings: a rich one, one split across a CONTINUE with its
    # characters going from two bytes to one, a surrogate pair
    s_long = "Ω" * 4000 + "abcdefghij" * 50
    sst_strings = [u16str("Hello"),
                   struct.pack("<HB", 4, 0x0C) + struct.pack("<HI", 2, 6) + b"Rich" + bytes(8) + bytes(6),
                   None, u16str("After"), u16str("Face \U0001F600")]
    head = struct.pack("<II", 6, 5) + sst_strings[0] + sst_strings[1]
    long_part1 = struct.pack("<HB", len(s_long), 1) + ("Ω" * 4000).encode("utf-16-le")
    cont = bytes([0]) + ("abcdefghij" * 50).encode("latin-1") + sst_strings[3] + sst_strings[4]
    sst = rec(0x00FC, head + long_part1) + rec(0x003C, cont)

    palette = [0x000000, 0xFFFFFF, 0x123456] + [0x808080] * 53     # index 10 is 0x123456
    xfs = [xf(style=True) for _ in range(15)]
    xfs.append(xf())                                                   # 15: Normal
    xfs.append(xf(font=5, fmt=164, align=0x03 | 0x08, indent=2,
                  borders=0x1 | (6 << 12) | (8 << 16), colours=12 << 7 | (1 << 26), fill=13))   # 16
    xfs.append(xf(font=3, fmt=14, colours=3 << 26, fill=10 | (9 << 7)))  # 17: date, patterned
    globals_ = [bof(0x0005)]
    if filepass:
        globals_.append(rec(0x002F, struct.pack("<H", 0) + bytes(4)))
    globals_ += [rec(0x0022, struct.pack("<H", 1)),                   # DATEMODE 1904
                 rec(0x003D, struct.pack("<hhHHHHHHH", 0, 0, 10000, 8000, 0x38, 1, 0, 1, 600)),
                 font("Arial"), font("Arial", weight=700), font("Arial", grbit=2), font("Courier New", 240, icv=10, family=3),
                 font("Times New Roman", 180, grbit=8, weight=700, script=1, underline=1, family=1),
                 rec(0x041E, struct.pack("<H", 164) + u16str('0.000 "kg"'))] + xfs + [
                 rec(0x0092, struct.pack("<H", 56) + b"".join(struct.pack("BBBB", c >> 16, (c >> 8) & 255, c & 255, 0) for c in palette))]
    bs_at = len(b"".join(globals_))
    globals_ += [boundsheet(0, "First"), boundsheet(0, "Second sheet"), boundsheet(0, "Chart1", dt=2)]
    globals_ += [rec(0x01AE, struct.pack("<HH", 3, 0x0401)), rec(0x01AE, struct.pack("<HH", 1, 0x3A01)),
                 rec(0x0023, struct.pack("<HI", 0, 0) + short_str("_xlfn.IFERROR")),
                 rec(0x01AE, struct.pack("<H", 1) + u16str("\x01\x03data\x03Other.xls") + u16str("Rates")),
                 rec(0x0023, struct.pack("<HI", 0, 0) + short_str("Rate")),
                 rec(0x0017, struct.pack("<H", 6) + b"".join(struct.pack("<HHH", *t) for t in
                     [(0, 0, 0), (0, 1, 1), (0, 0, 1), (1, 0xFFFE, 0xFFFE), (2, 0, 0), (0, 0xFFFF, 0xFFFF)])),
                 rec(0x0018, struct.pack("<HBBHHHBBBB", 0, 0, 5, 11, 0, 0, 0, 0, 0, 0) + b"\x00Total"
                     + bytes([0x3B]) + struct.pack("<HHHHH", 0, 0, 1, 0, 1)),
                 rec(0x0018, struct.pack("<HBBHHHBBBB", 0x20, 0, 1, 11, 0, 1, 0, 0, 0, 0) + b"\x00\x06"
                     + bytes([0x3B]) + struct.pack("<HHHHH", 0, 0, 2, 0, 2)),
                 sst, rec(0x000A)]
    g = b"".join(globals_)
    offsets = [len(g)]
    offsets.append(offsets[0] + len(b"".join(first)))
    offsets.append(offsets[1] + len(b"".join(second)))
    names = [("First", 0), ("Second sheet", 0), ("Chart1", 2)]
    fixed = b"".join(boundsheet(o, n, dt) for o, (n, dt) in zip(offsets, names))
    g = g[:bs_at] + fixed + g[bs_at + len(fixed):]
    stream = g + b"".join(first) + b"".join(second) + b"".join(chart)
    if biff5:
        stream = rec(0x0809, struct.pack("<HHHH", 0x0500, 0x0005, 0, 0)) + rec(0x000A)
    return stream


def cfb(streams):
    """A compound file holding the streams (name, bytes), 512-byte sectors,
    every stream padded to 4096 bytes so that none goes in the mini stream."""
    data = [(n, s + bytes(max(0, 4096 - len(s))), len(s) if len(s) >= 4096 else 4096) for n, s in streams]
    ndir = (len(data) + 1 + 3) // 4
    nstream = sum((len(s) + 511) // 512 for _, s, _ in data)
    nfat = 1
    while nfat * 128 < nfat + ndir + nstream:
        nfat += 1
    fat = [0xFFFFFFFD] * nfat
    first_dir = len(fat)
    fat += list(range(first_dir + 1, first_dir + ndir)) + [0xFFFFFFFE]
    starts = []
    for _, s, _ in data:
        n = (len(s) + 511) // 512
        starts.append(len(fat))
        fat += list(range(len(fat) + 1, len(fat) + n)) + [0xFFFFFFFE]
        fat[-1] = 0xFFFFFFFE
    fat += [0xFFFFFFFF] * (nfat * 128 - len(fat))

    def entry(name, kind, start, size, child=0xFFFFFFFF, right=0xFFFFFFFF):
        n = name.encode("utf-16-le") + b"\0\0"
        return (n + bytes(64 - len(n)) + struct.pack("<HBBIII", len(n), kind, 1, 0xFFFFFFFF, right, child)
                + bytes(16) + struct.pack("<I", 0) + bytes(16) + struct.pack("<III", start, size, 0))

    dirs = [entry("Root Entry", 5, 0xFFFFFFFE, 0, child=1)]
    for i, ((name, _, size), start) in enumerate(zip(data, starts)):
        dirs.append(entry(name, 2, start, size, right=i + 2 if i + 1 < len(data) else 0xFFFFFFFF))
    dirs += [bytes(128)] * (ndir * 4 - len(dirs))
    difat = list(range(nfat)) + [0xFFFFFFFF] * (109 - nfat)
    header = (bytes([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1]) + bytes(16)
              + struct.pack("<HHHHH", 0x3E, 3, 0xFFFE, 9, 6) + bytes(6)
              + struct.pack("<IIIIIIIII", 0, nfat, first_dir, 0, 4096, 0xFFFFFFFE, 0, 0xFFFFFFFE, 0)
              + struct.pack("<109I", *difat))
    out = header + struct.pack("<%dI" % len(fat), *fat) + b"".join(dirs)
    for _, s, _ in data:
        out += s + bytes((-len(s)) % 512)
    return out


def test_built_book(work):
    path = os.path.join(work, "built.xls")
    with open(path, "wb") as f:
        f.write(cfb([("Workbook", built_book())]))
    rc, out, err = dump(path)
    check("built.xls read", rc == 0, out[-400:] + err[-400:])
    if rc:
        return path
    b = parse(out)
    check("built: book", b["line"] == "book format=xls sheets=2 active=1 date1904=1", b["line"])
    notes = "\n".join(b["notes"])
    for want in ("Chart sheets are not read yet", "Comments on cells are not read yet",
                 "Pictures, charts and drawings", "Data tables keep their values",
                 "Some formulas could not be read", "Patterned cell backgrounds"):
        check("built: note " + want, want in notes, notes)
    first = b["sheets"][0]
    check("built: sheet", first["line"] == 'sheet 0 "First" hidden=0 width=1200 height=300 freeze=2,1', first["line"])
    check("built: columns", first["cols"] == ["col 1-2 width=210 style=1 hidden=1"], first["cols"])
    check("built: row", first["rows"] == ["row 1 height=600 hidden=0"], first["rows"])
    check("built: merge", first["merges"] == ["merge 9,0-10,1"], first["merges"])
    expect = {
        "A1": 'text "Hello"', "B1": 'text "Rich"',
        "C1": 'text "%s"' % ("Ω" * 4000 + "abcdefghij" * 50),
        "D1": 'text "After"', "E1": 'text "Face \U0001F600"', "F1": 'text ""',
        "A2": "number 1234", "B2": "number 123.45", "C2": "number 2.5", "D2": "number -5",
        "A3": "number 1", "B3": "number 2", "C3": "number 3", "D3": "empty", "F3": "empty",
        "A4": "bool 1", "B4": "error #N/A", "A5": "number 1E-300", "B5": "number 45000.5",
        "C5": 'text "Ünïcode label ✓"',
        "A6": 'number 13579 formula "SUM(A2:B2)"',
        "B6": 'text "x" formula "IFERROR(1/0,\\"x\\")"',
        "C6": 'number 15 formula "SUM(\'First:Second sheet\'!A1:A3)"',
        "D6": 'number 0 formula "#REF!"',
        "E6": 'number 0 formula "[Other.xls]Rates!$A$1"',
        "F6": 'number 0 formula "Other.xls!Rate"',
        "A7": 'number 0 formula "SUM(A1:B2)"',
        "B7": 'number 0 formula "SUM(2:3)"',
        "C7": 'number 0 formula "SUM((A1,B1))"',
        "D7": 'number 0 formula "SUM((A1,B1))"',
        "E7": 'number 0 formula "SUM({1.5,\\"a\\"\\"b\\";TRUE,#DIV/0!})"',
        "F7": 'number 0 formula "(A2+B2)*C2+(A2-(B2-C2))"',
        "A8": "number 1", "B8": "number 2",
        "C8": 'bool 1 formula "TRUE"', "D8": 'error #VALUE! formula "#VALUE!"',
        "E8": 'text "" formula "\\"\\""', "F8": 'number 10 formula "SUM(Total)"',
    }
    for r, want in expect.items():
        cell_is(b, 0, r, want)
    cell_is(b, 1, "A1", "number 7")
    check("built: second sheet", b["sheets"][1]["line"].startswith('sheet 1 "Second sheet" hidden=0'), b["sheets"][1]["line"])
    check("built: name", b["names"].get("Total", "").endswith('sheet=-1 hidden=0 "First!$A$1:$B$2"'), b["names"])
    check("built: built-in name", b["names"].get("Print_Area", "").endswith('sheet=0 hidden=0 "First!$A$1:$C$3"'), b["names"])
    s16 = style_of(b, 0, "D3")
    check("built: style font", 'font=' in s16 and "size=180 flags=29" in s16, s16)
    check("built: style alignment", "halign=3 valign=0 wrap=1 indent=2" in s16, s16)
    check("built: style fill and borders", "fill=808080" in s16 and "border=1:000000,0:-,0:-,6:808080" in s16, s16)
    check("built: number format", b["numfmts"].get(int(s16.split("numfmt=")[1].split()[0]), "").endswith('"0.000 \\"kg\\""'), s16)
    s17 = style_of(b, 0, "B5")
    check("built: palette colour", "colour=123456" in s17 and "fill=123456" in s17, s17)
    check("built: date format", b["numfmts"].get(int(s17.split("numfmt=")[1].split()[0]), "").endswith('"m/d/yyyy"'), s17)
    return path


def test_refused(work):
    for name, data, want in (
            ("encrypted", cfb([("Workbook", built_book(filepass=True))]), "protected with a password"),
            ("excel5", cfb([("Workbook", built_book(biff5=True))]), "Excel 5 or Excel 95"),
            ("book-stream", cfb([("Book", built_book(biff5=True))]), "Excel 5 or Excel 95"),
            ("excel4", rec(0x0409, struct.pack("<HHH", 0, 0x10, 0)) + rec(0x000A) + bytes(100), "Excel 4 or earlier")):
        path = os.path.join(work, name + ".xls")
        with open(path, "wb") as f:
            f.write(data)
        rc, out, err = dump(path)
        check("refused " + name, rc == 10 and want in out and "error" in out, out + err)


def test_damaged(paths):
    """Every file cut short at many places, and with bytes changed, must
    give a book or an error."""
    rnd = random.Random(1904)
    runs = 0
    with tempfile.TemporaryDirectory() as work:
        bad = os.path.join(work, "bad.xls")
        for path in paths:
            data = open(path, "rb").read()
            cuts = sorted(set(list(range(0, 1024, 37)) + list(range(0, len(data), max(1, len(data) // 97)))))
            versions = [data[:n] for n in cuts]
            for _ in range(60):
                d = bytearray(data)
                for _ in range(rnd.randint(1, 12)):
                    d[rnd.randrange(512, len(d))] = rnd.randrange(256)
                versions.append(bytes(d))
            for v in versions:
                with open(bad, "wb") as f:
                    f.write(v)
                rc, out, err = dump(bad)
                runs += 1
                if rc not in (0, 10):
                    check("damaged %s (%d bytes)" % (os.path.basename(path), len(v)), False, "exit %d %s" % (rc, err[-600:]))
                    return
    print("%d damaged copies read safely" % runs)


def main():
    with tempfile.TemporaryDirectory() as work:
        test_libreoffice_book()
        built = test_built_book(work)
        test_refused(work)
        test_damaged([os.path.join(HERE, "fixtures", "book.xls"), built])
    if failures:
        print("%d workbook check(s) failed" % len(failures))
        sys.exit(1)
    print("workbook tests passed")


if __name__ == "__main__":
    main()
