#!/usr/bin/env python3
"""Final Writer's reader (libowf/src/in_finalwriter.c): a document of several
sections, its paragraphs' indents, alignment and tab stops, and its page.

    test_finalwriter.py path/to/owconvert

Builds the document from docs/formats/finalwriter.md, as Final Writer lays
its sections out (SHD1, TBDY, the body, then RMST and LMST, for each
section), converts it to text and HTML and checks them. Exits 1 on a failure.
Team 5 (W142), 26 October 2026. MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import struct
import subprocess
import sys
import tempfile

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


def attr(length, font=0, size=12):
    b = bytearray(22)
    b[0:4] = struct.pack(">I", length)
    b[4:6] = struct.pack(">H", font)
    b[7] = size
    return bytes(b)


def rule(text_len, left=0, first=0, align=1):
    b = bytearray(24)
    b[0:2] = struct.pack(">H", left)
    b[4:6] = struct.pack(">H", first)
    b[6:8] = struct.pack(">H", 180)
    b[8], b[9] = 0x82, align
    b[12:16] = struct.pack(">I", text_len)
    b[19] = 0x16
    return bytes(b)


def para(text, **kw):
    t = text.encode("latin-1")
    return [chunk("RULE", rule(len(t), **kw)), chunk("ATTR", attr(len(t))), chunk("CHRS", t)]


def tab(kind, pos):
    return chunk("TABS", struct.pack(">HHI", kind, 1, pos))


def sample():
    pag3 = bytearray(112)
    pag3[2:6] = struct.pack(">HH", 5953, 8419)      # A4 in decipoints
    out = [chunk("FDTA", b"SoftSans\0"), chunk("PAG3", bytes(pag3))]
    for n in (1, 2, 3):
        out.append(chunk("SHD1", bytes(80)))
        out.append(chunk("TBDY", b"\0\0"))
        out += para("Section %d body." % n)
        if n == 1:
            out += para("Right aligned.", align=2)
            out += para("Centred.", align=4)
            out += para("Justified.", align=8)
            out += para("Heading flag, left.", align=0x21)
            out += [chunk("RULE", rule(9, left=900, first=180)), tab(1, 900),
                    chunk("ATTR", attr(9)), chunk("CHRS", b"\xb7\tA list")]
            out += [chunk("RULE", rule(5)), tab(2, 4320), tab(4, 2160),
                    chunk("ATTR", attr(5)), chunk("CHRS", b"a\tb\tc")]
        out.append(chunk("RMST", b"\0\0"))
        out += para("Master %d" % n)
        out.append(chunk("LMST", b"\0\0"))
        out += para("Left master %d" % n)
    return form("SWRT", out)


def run(path, to):
    p = subprocess.run([OWCONVERT, "--report", path, to], capture_output=True, timeout=60)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def main():
    with tempfile.TemporaryDirectory() as work:
        src = os.path.join(work, "sections.fw")
        with open(src, "wb") as f:
            f.write(sample())
        rc, report = run(src, os.path.join(work, "out.txt"))
        check("converts", rc == 0, report)
        text = open(os.path.join(work, "out.txt"), encoding="utf-8").read() if rc == 0 else ""
        for n in (1, 2, 3):
            check("section %d's body read" % n, "Section %d body." % n in text, text)
        check("no master text in the body", "Master" not in text, text)
        check("report: sections", "3 sections were read" in report, report)
        check("report: later masters", "sections after the first were left out" in report, report)
        rc, _ = run(src, os.path.join(work, "out.html"))
        h = open(os.path.join(work, "out.html"), encoding="utf-8").read() if rc == 0 else ""
        header = h[h.find('class="ow-header"'):]
        header = header[:header.find("</div>")]
        check("first master is the header", "Master 1" in header and "Master 2" not in header, header)
        for want in ('text-align: right">Right aligned.', 'text-align: center">Centred.',
                     'text-align: justify">Justified.', "<p>Heading flag, left.",
                     "margin-left: 90pt; text-indent: -72pt", 'data-ow-tabs="0:left"',
                     'data-ow-tabs="8640:right 4320:centre"', "size: 595.3pt 841.9pt"):
            check("html has " + want, want in h, h[:300])
    if failures:
        print("%d failure(s)" % len(failures))
        sys.exit(1)
    print("test_finalwriter: passed")


if __name__ == "__main__":
    main()
