#!/usr/bin/env python3
"""ProWrite's reader (libowf/src/in_prowrite.c): what ProWrite 3's own files
hold beyond the 1987 specification.
New Horizons' own Convert Notes (fixtures/convert-notes.pw) reads whole.

    test_prowrite.py path/to/owconvert

- COLR holds ISO 6429 colour numbers, 30-37, as ProWrite writes them;
- PSET holds the page's size in decipoints;
- two page breaks in a row leave an empty page;
- of several headers (left, right, both, none), the right pages' and the
  left pages' are kept (2.0.23), and written to DOCX (even headers) and
  ODT (style:header-left) and read back.
Exits 1 on a failure. Team 5 (W142), 26 October 2026.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

OWCONVERT = os.path.abspath(sys.argv[1])
failures = []


def check(name, ok, detail=""):
    if not ok:
        failures.append(name)
        print("FAIL", name, detail[:600])


def chunk(cid, data):
    out = cid.encode("ascii") + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def form(ftype, chunks):
    body = ftype.encode("ascii") + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def para(font=0x22, style=0, color=0, justify=0):
    return chunk("PARA", struct.pack(">HHHBBBBBBI", 0, 0, 0, 0, justify, font, style, 0, color, 0))


def sample():
    return form("WORD", [
        chunk("FONT", struct.pack(">BxH", 0x22, 12) + b"Times\0"),
        # ProWrite's own map: pens black, white, red, cyan, green, magenta, blue, yellow
        chunk("COLR", bytes([30, 37, 31, 36, 32, 35, 34, 33])),
        # 6120 x 7920 decipoints: US Letter, as in New Horizons' own file
        chunk("PSET", struct.pack(">BBIII", 1, 0, 6120, 7920, 0)),
        chunk("DOC ", struct.pack(">HBxI", 1, 0, 0)),
        para(), chunk("TEXT", b"Black text"),
        para(color=6), chunk("TEXT", b"Blue text"),
        para(color=2), chunk("TEXT", b"Red text"),
        chunk("PAGE", b""), chunk("PAGE", b""),
        para(), chunk("TEXT", b"After two breaks"),
        chunk("HEAD", struct.pack(">BBI", 0, 1, 0)), para(), chunk("TEXT", b""),
        chunk("HEAD", struct.pack(">BBI", 1, 0, 0)), para(), chunk("TEXT", b"Left header"),
        chunk("HEAD", struct.pack(">BBI", 2, 0, 0)), para(), chunk("TEXT", b"Right header"),
        chunk("FOOT", struct.pack(">BBI", 0, 1, 0)), para(), chunk("TEXT", b"Footer on no pages"),
    ])


def furniture(kinds):
    """A small document whose headers and footers are kinds: (chunk id, page type, text)."""
    chunks = [chunk("FONT", struct.pack(">BxH", 0x22, 12) + b"Times\0"),
              chunk("DOC ", struct.pack(">HBxI", 1, 0, 0)),
              para(), chunk("TEXT", b"Body"), chunk("PAGE", b""), para(), chunk("TEXT", b"Page two")]
    for cid, kind, text in kinds:
        chunks += [chunk(cid, struct.pack(">BBI", kind, 1, 0)), para(), chunk("TEXT", text.encode("latin-1"))]
    return form("WORD", chunks)


def div(h, cls):
    """The text of the first <div class="cls"> in h, or None."""
    m = re.search(r'<div class="%s"[^>]*>(.*?)</div>' % re.escape(cls), h, re.S)
    return re.sub(r"<[^>]+>", "", m.group(1)).strip() if m else None


def run(path, to):
    p = subprocess.run([OWCONVERT, "--report", path, to], capture_output=True, timeout=60)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def convert_notes(work):
    """New Horizons' own file (fixtures/PROVENANCE.md): every paragraph, its
    page, and its header and footer for no pages left out."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "convert-notes.pw")
    out = os.path.join(work, "notes.txt")
    rc, report = run(src, out)
    check("Convert Notes converts", rc == 0 and "31 paragraph(s)" in report, report)
    t = open(out, encoding="utf-8").read() if rc == 0 else ""
    check("Convert Notes: its title", t.startswith("ProWrite File Convert\nVersion 2.0\n"), t[:200])
    check("Convert Notes: New Horizons' notice", "Copyright \u00a9 1992, New Horizons Software, Inc." in t, t[:1200])
    check("Convert Notes: its last paragraph", t.rstrip().endswith("only features currently implemented in ProWrite 3.2 are converted."), t[-300:])
    out = os.path.join(work, "notes.html")
    rc, report = run(src, out)
    h = open(out, encoding="utf-8").read() if rc == 0 else ""
    check("Convert Notes: US Letter from PSET", "size: 612pt 792pt" in h, h[:400])
    check("Convert Notes: a page break", h.count('class="ow-page-break"') == 1, h[-800:])
    check("Convert Notes: no header or footer", 'class="ow-header"' not in h and 'class="ow-footer"' not in h, h[-800:])


def main():
    with tempfile.TemporaryDirectory() as work:
        convert_notes(work)
        src = os.path.join(work, "pw.pw")
        with open(src, "wb") as f:
            f.write(sample())
        out = os.path.join(work, "out.html")
        rc, report = run(src, out)
        check("converts", rc == 0, report)
        h = open(out, encoding="utf-8").read() if rc == 0 else ""
        check("black is the default colour", "<p>Black text</p>" in h, h[-1500:])
        check("pen 6 is blue", 'color: #0000cc">Blue text' in h, h[-1500:])
        check("pen 2 is red", 'color: #cc0000">Red text' in h, h[-1500:])
        check("PSET's page", "size: 612pt 792pt" in h, h[:400])
        check("no paper-size note", "does not store the paper size" not in report, report)
        check("two page breaks, an empty page", h.count('class="ow-page-break"') == 2, h[-1500:])
        check("the right pages' header", div(h, "ow-header") == "Right header", h[-1500:])
        check("the left pages' header", div(h, "ow-header-left") == "Left header", h[-1500:])
        check("report: no header left out", "header(s)" not in report and "every page" not in report, report)
        check("a footer for no pages is none", "<div class=\"ow-footer" not in h and "Footer on no pages" not in h, h[-800:])
        # DOCX: Word's even header, with the setting that turns them on; and read back
        docx = os.path.join(work, "out.docx")
        rc, report = run(src, docx)
        check("DOCX converts", rc == 0, report)
        z = zipfile.ZipFile(docx) if rc == 0 else None
        names = z.namelist() if z else []
        doc_xml = z.read("word/document.xml").decode("utf-8") if z else ""
        check("DOCX: an even header", 'w:headerReference w:type="even" r:id="rId4"' in doc_xml and
              'w:headerReference w:type="default" r:id="rId2"' in doc_xml, doc_xml[-600:])
        check("DOCX: header2.xml has the left one", "word/header2.xml" in names and
              "Left header" in z.read("word/header2.xml").decode("utf-8"), str(names))
        check("DOCX: evenAndOddHeaders", "word/settings.xml" in names and
              "evenAndOddHeaders" in z.read("word/settings.xml").decode("utf-8"), str(names))
        back = os.path.join(work, "back.html")
        rc, report = run(docx, back)
        hb = open(back, encoding="utf-8").read() if rc == 0 else ""
        check("DOCX read back: both headers", div(hb, "ow-header") == "Right header" and
              div(hb, "ow-header-left") == "Left header", hb[-1500:])
        # ODT: style:header-left after style:header; and read back
        odt = os.path.join(work, "out.odt")
        rc, report = run(src, odt)
        styles = zipfile.ZipFile(odt).read("styles.xml").decode("utf-8") if rc == 0 else ""
        check("ODT: header-left", re.search(r"<style:header>.*Right header.*</style:header>"
                                            r"<style:header-left>.*Left header.*</style:header-left>", styles) is not None,
              styles[-900:])
        rc, report = run(odt, back)
        hb = open(back, encoding="utf-8").read() if rc == 0 else ""
        check("ODT read back: both headers", div(hb, "ow-header") == "Right header" and
              div(hb, "ow-header-left") == "Left header", hb[-1500:])
        # one side only: the other side's is an empty paragraph (nothing there)
        for name, kinds, right, left in (
                ("right pages only", [("HEAD", 2, "R")], "R", ""),
                ("left pages only", [("HEAD", 1, "L")], "", "L"),
                ("both pages", [("HEAD", 3, "B")], "B", None),
                ("footers, left and both", [("FOOT", 1, "FL"), ("FOOT", 3, "FB")], "FB", "FL")):
            with open(src, "wb") as f:
                f.write(furniture(kinds))
            rc, report = run(src, out)
            h = open(out, encoding="utf-8").read() if rc == 0 else ""
            what = "ow-footer" if kinds[0][0] == "FOOT" else "ow-header"
            check("%s: right pages" % name, div(h, what) == right, h[-900:])
            check("%s: left pages" % name, div(h, what + "-left") == left, h[-900:])
        with open(src, "wb") as f:
            f.write(furniture([("HEAD", 3, "B"), ("HEAD", 1, "L"), ("HEAD", 2, "R"), ("HEAD", 2, "R2")]))
        rc, report = run(src, out)
        h = open(out, encoding="utf-8").read() if rc == 0 else ""
        check("left, right and both: left and right kept", div(h, "ow-header") == "R" and div(h, "ow-header-left") == "L", h[-900:])
        check("left, right and both: two left out", "2 more header(s) for the same pages" in report, report)
    if failures:
        print("%d failure(s)" % len(failures))
        sys.exit(1)
    print("test_prowrite: passed")


if __name__ == "__main__":
    main()
