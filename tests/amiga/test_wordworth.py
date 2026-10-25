#!/usr/bin/env python3
"""Wordworth's reader (libowf/src/in_wordworth.c): the drawing layer's text.

    test_wordworth.py path/to/owconvert

A document whose FORM GTID holds two text boxes (a WOWO chunk with the
box's place, then its FORM WOWO), a PTXT text object and two shapes, laid
out as Wordworth 6 and 7 write them (docs/formats/wordworth.md). Their text
must come out as frames, in reading order (top to bottom, then left to
right), and the shapes be reported. Exits 1 on a failure.
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
        print("FAIL", name, repr(detail)[:600])


def chunk(cid, data):
    out = cid.encode("ascii") + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def form(ftype, chunks):
    body = ftype.encode("ascii") + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def place(x, y, w, h, size=48):
    """An object's place: millipoints, longs at 2, 7, 12 and 17."""
    b = bytearray(size)
    b[0:2] = b"\x00\x01"
    for i, v in enumerate((x, y, w, h)):
        b[2 + 5 * i:6 + 5 * i] = struct.pack(">I", v)
        b[6 + 5 * i] = 1
    return b


def wpar():
    b = bytearray(36)
    b[21] = 1
    return bytes(b)


def box(x, y, text):
    return [chunk("WOWO", bytes(place(x, y, 100000, 20000, 81))),
            form("WOWO", [chunk("WPAR", wpar()), chunk("WTAB", b""), chunk("WTXT", text)])]


def ptxt(x, y, text):
    b = place(x, y, 24000, 34000, 131)
    b[38:40] = struct.pack(">H", len(text))
    b[40:40 + len(text)] = text
    return chunk("PTXT", bytes(b))


def sample():
    wdoc = bytearray(54)
    wdoc[4:12] = struct.pack(">II", 595440, 843336)
    drawing = form("GTID", [form("GTMP", []), form("GTIP", [
        chunk("RECT", bytes(place(50000, 30000, 100000, 80000))),
        *box(300000, 120000, b"Lower box\x0f"),
        *box(50000, 120000, b"Left box, first line\x0fsecond line\x0f"),
        ptxt(50000, 40000, b"Top"),
        chunk("ELLP", bytes(place(50000, 300000, 50000, 50000))),
        *box(50000, 400000, b"\x0f"),
    ])])
    return form("WOWO", [
        chunk("WVRN", struct.pack(">II", 5, 0)),
        chunk("WFNT", bytes([1, 0xFA]) + struct.pack(">H", 12) + b"IF_CG Times\0"),
        chunk("WDOC", bytes(wdoc)),
        chunk("WPAR", wpar()), chunk("WTAB", b""), chunk("WTXT", b"Body\x0f"),
        drawing,
    ])


def main():
    with tempfile.TemporaryDirectory() as work:
        src = os.path.join(work, "boxes.ww")
        with open(src, "wb") as f:
            f.write(sample())
        out = os.path.join(work, "out.txt")
        p = subprocess.run([OWCONVERT, "--report", src, out], capture_output=True, timeout=60)
        report = p.stdout.decode("utf-8", "replace")
        check("converts", p.returncode == 0, report)
        text = open(out, encoding="utf-8").read() if p.returncode == 0 else ""
        want = ["Body", "Top", "Left box, first line", "second line", "Lower box"]
        pos = [text.find(w) for w in want]
        check("all the text", all(i >= 0 for i in pos), text)
        check("in reading order", pos == sorted(pos), text)
        check("report: frames", "3 text box(es)" in report, report)
        check("report: shapes", "2 drawn shape(s)" in report, report)
        check("no picture count for the drawing", "picture(s) or drawing(s)" not in report, report)
        out = os.path.join(work, "out.odt")
        p = subprocess.run([OWCONVERT, src, out], capture_output=True, timeout=60)
        check("to ODT", p.returncode == 0, p.stdout)
    if failures:
        print("%d failure(s)" % len(failures))
        sys.exit(1)
    print("test_wordworth: passed")


if __name__ == "__main__":
    main()
