#!/usr/bin/env python3
"""IFF FTXT's reader (libowf/src/in_ftxt.c) against the FTXT specification.

    test_ftxt.py path/to/owconvert

1. Small FTXT files, one rule each: ESC sequences and DCS, OSC, PM and APC
   strings skipped whole; a CSI cut short drops nothing after it; a CSI
   with a private parameter does nothing; one paragraph over two CHRS;
   text in font 1; a LIST with a PROP; a CAT.
2. With tests/amiga/fixtures/ftxt-stress*.iff present (make_stress_ftxt.py),
   each must read as its .txt says, which a stripper written from the
   specification's section 8 (below) also gets. Tab, form feed and vertical
   tab, which libowf keeps (as a tab, a page break and a line break) though
   the specification leaves them unused, are taken out of the file first.
Exits 1 on a failure. Team 5 (W142), 26 October 2026.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import struct
import subprocess
import sys
import tempfile

OWCONVERT = os.path.abspath(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
failures = []


def check(name, ok, detail=""):
    if not ok:
        failures.append(name)
        print("FAIL", name, repr(detail)[:600])


def chunk(cid, data):
    out = cid.encode("ascii") + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def group(kind, ftype, parts):
    body = ftype.encode("ascii") + b"".join(parts)
    return kind.encode("ascii") + struct.pack(">I", len(body)) + body


def fons(fid, prop, serif, name):
    return chunk("FONS", bytes([fid, 0, prop, serif]) + name + b"\0")


def strip(data):
    """The specification's stripper (section 8): CHRS of FORM FTXT, nested in
    LIST and CAT too, without any control or sequence; LF ends lines."""
    out = []

    def chrs(b):
        i = 0
        while i < len(b):
            c = b[i]
            if c == 0x0A:
                out.append("\n")
                i += 1
            elif c == 0x1B:
                i += 1
                while i < len(b) and 0x20 <= b[i] <= 0x2F:
                    i += 1
                if i < len(b) and 0x30 <= b[i] <= 0x7E:
                    i += 1
            elif c == 0x9B:
                i += 1
                while i < len(b) and 0x20 <= b[i] <= 0x3F:
                    i += 1
                if i < len(b) and 0x40 <= b[i] <= 0x7E:
                    i += 1
            elif c in (0x90, 0x9D, 0x9E, 0x9F):
                i += 1
                while i < len(b) and b[i] != 0x9C:
                    i += 1
                i += 1
            elif c < 0x20 or 0x7F <= c <= 0x9F:
                i += 1
            else:
                out.append(chr(c))
                i += 1

    def walk(o, e, in_ftxt):
        while o + 8 <= e:
            cid, n = data[o:o + 4], struct.unpack_from(">I", data, o + 4)[0]
            if cid in (b"FORM", b"LIST", b"CAT "):
                if cid != b"FORM" or data[o + 8:o + 12] == b"FTXT":
                    walk(o + 12, o + 8 + n, cid == b"FORM")
            elif cid == b"CHRS" and in_ftxt:
                chrs(data[o + 8:o + 8 + n])
            o += 8 + n + (n & 1)

    walk(0, len(data), False)
    return "".join(out)


def without_kept_controls(data):
    """The same file with tab, vertical tab and form feed taken out of its CHRS."""
    def rebuild(o, e):
        parts = []
        while o + 8 <= e:
            cid, n = data[o:o + 4], struct.unpack_from(">I", data, o + 4)[0]
            if cid in (b"FORM", b"LIST", b"CAT ", b"PROP"):
                inner = rebuild(o + 12, o + 8 + n)
                body = data[o + 8:o + 12] + inner
                parts.append(cid + struct.pack(">I", len(body)) + body)
            else:
                body = data[o + 8:o + 8 + n]
                if cid == b"CHRS":
                    body = bytes(c for c in body if c not in (0x09, 0x0B, 0x0C))
                parts.append(chunk(cid.decode("latin-1"), body))
            o += 8 + n + (n & 1)
        return b"".join(parts)
    return rebuild(0, len(data))


def convert(work, name, data, to="txt"):
    src = os.path.join(work, name)
    with open(src, "wb") as f:
        f.write(data)
    out = os.path.join(work, name + "." + to)
    p = subprocess.run([OWCONVERT, "--report", src, out], capture_output=True, timeout=60)
    if p.returncode:
        return None, p.stdout.decode("utf-8", "replace")
    return open(out, encoding="utf-8").read(), p.stdout.decode("utf-8", "replace")


def small_cases(work):
    form = lambda *parts: group("FORM", "FTXT", list(parts))
    cases = [
        ("esc", form(chunk("CHRS", b"a\x1b(Bb\x1b cc\x1b]0;title\x07d\x1bPdcs\x1b\\e\n")), "abcde\n"),
        ("strings", form(chunk("CHRS", b"a\x90dcs\x9cb\x9dosc\x9cc\x9epm\x9cd\x9fapc\x9ce\n")), "abcde\n"),
        ("private", form(chunk("CHRS", b"a\x9b?7hb\x9b>1;2cc\n")), "abc\n"),
        ("cut by a letter", form(chunk("CHRS", b"a\x9b1\xe9b\n")), "aéb\n"),
        ("cut by a line end", form(chunk("CHRS", b"one\x9b4\ntwo\n")), "one\ntwo\n"),
        ("one stream", form(chunk("CHRS", b"one "), fons(2, 2, 2, b"garnet"), chunk("CHRS", b"paragraph\n")),
         "one paragraph\n"),
        ("list", group("LIST", "FTXT", [group("PROP", "FTXT", [fons(1, 2, 1, b"helvetica")]),
                                        form(chunk("CHRS", b"first\n")), form(chunk("CHRS", b"second"))]),
         "first\nsecond\n"),
        ("cat", group("CAT ", "FTXT", [form(chunk("CHRS", b"one\n")),
                                       group("FORM", "ILBM", [chunk("BMHD", bytes(20))]),
                                       form(chunk("CHRS", b"two\n"))]), "one\ntwo\n"),
    ]
    for name, data, want in cases:
        got, report = convert(work, name.replace(" ", "-") + ".iff", data)
        check(name, got == want, (got, report))
    # Text starts in font 1, and a LIST's PROP gives a FORM its fonts.
    html, _ = convert(work, "font1.ftxt", form(fons(1, 2, 2, b"times"), chunk("CHRS", b"plain\n")), "html")
    check("font 1 at the start", html and "'times'" in html[html.find("<body>"):], html)
    html, _ = convert(work, "prop.iff", group("LIST", "FTXT", [group("PROP", "FTXT", [fons(1, 2, 1, b"helvetica")]),
                                                               form(chunk("CHRS", b"x\n"))]), "html")
    check("PROP's font 1", html and "'helvetica'" in html[html.find("<body>"):], html)


def fixtures(work):
    d = os.path.join(HERE, "fixtures")
    for name in ("ftxt-stress.ftxt", "ftxt-stress-list.iff", "ftxt-stress-cat.iff"):
        path = os.path.join(d, name)
        if not os.path.exists(path):
            print("(no %s: make_stress_ftxt.py makes it)" % name)
            continue
        data = open(path, "rb").read()
        want = open(os.path.splitext(path)[0] + ".txt", encoding="utf-8").read()
        check(name + ": the stripper", strip(data) == want)
        got, report = convert(work, name, without_kept_controls(data))
        got = got.rstrip("\n") if got else got
        check(name + ": libowf", got == want.rstrip("\n"), (got, report))


def main():
    with tempfile.TemporaryDirectory() as work:
        small_cases(work)
        fixtures(work)
    if failures:
        print("%d failure(s)" % len(failures))
        sys.exit(1)
    print("test_ftxt: passed")


if __name__ == "__main__":
    main()
