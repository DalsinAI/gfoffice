#!/usr/bin/env python3
"""IFF FTXT stress documents: everything the FTXT specification lets a file hold.
Team 5 (W142), 26 October 2026 (Dale: "generate a really complicated pptx ...
Same for Publisher and PageStream, Final Writer etc.").

Written from the specification alone ("FTXT IFF Formatted Text", Steve Shaw
and Jerry Morrison, Electronic Arts, and Bob Burns, Commodore-Amiga, 1985,
updated 1988; EA IFF 85), not from libowf's reader:
  - FORM FTXT holds CHRS (the text) and FONS (fonts 0-9) chunks; other
    chunks and nested FORMs, LISTs and CATs are skipped by readers.
  - CHRS is 8-bit ISO/ANSI: in C0 only LF and ESC count (CR is not a line
    end); in C1 only CSI (9B) is used, ST ends DCS, OSC, PM and APC strings;
    G1 (A0-FF) is Latin-1. ESC-, DCS-type and malformed sequences are
    reserved and skipped; SS2 and SS3 may be skipped or shown as "?".
  - CSI sequences format the text (ISO 6429); SGR 10-19 select fonts 0-9.
    Text starts in font 1.
  - Several CHRS chunks are one stream: formatting carries over, and a
    property chunk between two CHRS applies to the later one.
  - LIST FTXT with PROP FTXT gives FORMs inherited properties.

    python3 make_stress_ftxt.py OUTDIR

writes ftxt-stress.ftxt (one FORM FTXT), ftxt-stress.txt (the text a reader
following the specification shows: each LF a line end, everything the
specification calls a control removed), ftxt-stress-list.iff (a LIST with a
PROP) and ftxt-stress-cat.iff (a CAT, as the clipboard may hold). Nothing
here is from a private document.
"""
import os
import struct
import sys

CSI = b"\x9b"
ESC = b"\x1b"
ST = b"\x9c"


def chunk(cid, data):
    out = cid.encode("ascii") + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def group(kind, ftype, parts):
    body = ftype.encode("ascii") + b"".join(parts)
    return kind.encode("ascii") + struct.pack(">I", len(body)) + body


def fons(fid, proportional, serif, name):
    """FONS: id, pad1, proportional (0 unknown, 1 no, 2 yes), serif (the same), name."""
    return chunk("FONS", bytes([fid, 0, proportional, serif]) + name.encode("latin-1") + b"\0")


def sgr(*params):
    return CSI + ";".join(str(p) for p in params).encode("ascii") + b"m"


def ilbm(width, height):
    """A small one-plane ILBM (a checker), to nest inside FTXT."""
    row = (width + 15) // 16 * 2
    body = b"".join((b"\xaa\x55" * (row // 2)) if y & 1 else (b"\x55\xaa" * (row // 2)) for y in range(height))
    bmhd = struct.pack(">HHhhBBBBHBBhh", width, height, 0, 0, 1, 0, 0, 0, 0, 10, 11, width, height)
    return group("FORM", "ILBM", [chunk("BMHD", bmhd), chunk("CMAP", b"\0\0\0\xff\xff\xff"), chunk("BODY", body)])


class Stream:
    """Builds CHRS bytes and, beside them, the text a specification reader shows."""

    def __init__(self):
        self.data = bytearray()
        self.text = []

    def t(self, s):
        """Visible text (Latin-1)."""
        self.data += s.encode("latin-1")
        self.text.append(s)
        return self

    def nl(self):
        self.data += b"\n"
        self.text.append("\n")
        return self

    def c(self, raw, shown=""):
        """Controls: in the file, but not shown (or shown as `shown`)."""
        self.data += raw
        self.text.append(shown)
        return self

    def take(self):
        data, self.data = bytes(self.data), bytearray()
        return data


def build():
    s = Stream()
    chunks = []
    # Fonts 0-9 (font 1 is the default), one with flags unknown, and a
    # reserved id the readers must ignore.
    chunks += [
        fons(1, 2, 2, "times"),
        fons(0, 1, 1, "topaz"),
        fons(2, 2, 1, "helvetica"),
        fons(3, 1, 2, "courier"),
        fons(9, 0, 0, "CGTimes"),
        fons(12, 2, 2, "reserved-id-12"),
        chunk("W142", b"A private chunk: readers skip it."),
    ]

    # The stream's first line selects nothing: font 1, plain.
    s.t("This first line selects no font: FTXT's default is font 1, times (proportional, serif).").nl()
    # 1. Title.
    s.c(sgr(12, 1)).t("FTXT stress document").c(sgr(22, 11)).nl()
    s.t("Team 5, W142. Each section says what it tests; the text after each label is what a reader shows.").nl()
    s.nl()

    # 2. Fonts.
    s.c(sgr(1)).t("1. Fonts (FONS and SGR 10-19)").c(sgr(22)).nl()
    s.c(sgr(10)).t("SGR 10: font 0, topaz (fixed, sans).").nl()
    s.c(sgr(11)).t("SGR 11: font 1, times again.").nl()
    s.c(sgr(12)).t("SGR 12: font 2, helvetica (proportional, sans).").nl()
    s.c(sgr(13)).t("SGR 13: font 3, courier (fixed, serif).").nl()
    s.c(sgr(19)).t("SGR 19: font 9, CGTimes (flags unknown).").nl()
    s.c(sgr(15)).t("SGR 15: font 5, which no FONS names.").nl()
    s.c(sgr(11)).t("Fonts change mid-line: ").c(sgr(10)).t("topaz").c(sgr(11)).t(", times, ").c(sgr(12)).t("helvetica").c(sgr(11)).t(".").nl()
    s.nl()

    # 3. Renditions.
    s.c(sgr(1)).t("2. Rendition (SGR, ISO 6429)").c(sgr(22)).nl()
    s.t("Plain, ").c(sgr(1)).t("bold").c(sgr(22)).t(", ").c(sgr(3)).t("italic").c(sgr(23)).t(", ").c(sgr(4)).t("underlined").c(sgr(24)).t(".").nl()
    s.c(sgr(1, 3, 4)).t("Bold, italic and underlined in one SGR (1;3;4)").c(sgr(0)).t(", then reset by SGR 0.").nl()
    s.c(sgr(1)).t("Bold, ").c(sgr(3)).t("and italic, ").c(sgr(4)).t("and underlined; ").c(CSI + b"m").t("reset by CSI m with no parameter.").nl()
    s.c(sgr(1)).t("Bold on, ").c(sgr(23)).t("23 (not italic) leaves it bold, ").c(sgr(22)).t("22 ends it.").nl()
    s.c(sgr(7)).t("Inverse (7)").c(sgr(27)).t(", positive again (27).").nl()
    s.c(sgr(2)).t("Faint (2)").c(sgr(22)).t(", ").c(sgr(9)).t("crossed out (9)").c(sgr(29)).t(", ").c(sgr(21)).t("doubly underlined (21)").c(sgr(24)).t(", ").c(sgr(5)).t("blinking (5)").c(sgr(25)).t(".").nl()
    s.c(sgr(8)).t("Concealed (8): shown here as text by a reader that keeps characters").c(sgr(28)).t(", revealed (28).").nl()
    s.nl()

    # 4. Colours.
    s.c(sgr(1)).t("3. Colours (SGR 30-37, 39, 40-47, 49)").c(sgr(22)).nl()
    for i, name in enumerate(["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"]):
        s.c(sgr(30 + i)).t(name).c(sgr(39)).t(" ")
    s.nl()
    for i, name in enumerate(["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"]):
        s.c(sgr(40 + i)).t("on " + name).c(sgr(49)).t(" ")
    s.nl()
    s.c(sgr(31, 43)).t("Red on yellow in one SGR (31;43)").c(sgr(0)).t(", then plain.").nl()
    s.nl()

    # 5. Lines and paragraphs.
    s.c(sgr(1)).t("4. Lines").c(sgr(22)).nl()
    s.t("CR LF ends this line once: CR is not a line end.").c(b"\r").nl()
    s.t("A lone CR").c(b"\r").t(" inside a line is skipped.").nl()
    s.t("Two blank lines follow.").nl().nl().nl()
    long = ("This paragraph is long, to test wrapping and long runs of text in one line of the stream. " * 30).strip()
    s.t(long).nl()
    s.nl()

    # 6. Controls the specification calls reserved or unused.
    s.c(sgr(1)).t("5. Reserved and unused controls (all skipped)").c(sgr(22)).nl()
    s.t("ESC sequences: [").c(ESC + b"(B").c(ESC + b"c").c(ESC + b" F").t("] are empty.").nl()
    s.t("DCS, OSC, PM and APC strings: [").c(b"\x90dcs payload\x9c").c(b"\x9d0;window title\x9c").c(b"\x9eprivacy message\x9c").c(b"\x9fapplication command\x9c").t("] are empty.").nl()
    s.t("SS2 and SS3 then a letter: [").c(b"\x8e").t("A").c(b"\x8f").t("B").t("] (each letter may also show as ?).").nl()
    s.t("Unused C0: [").c(b"\x00\x07\x08\x0b\x0c\x0e\x1f").t("]; unused C1: [").c(b"\x80\x84\x85\x88\x9a").t("]; DEL: [").c(b"\x7f").t("].").nl()
    s.t("A tab is an unused C0 control too: [").c(b"\t").t("].").nl()
    s.t("CSI with a private parameter: [").c(CSI + b"?7h").t("]; with an intermediate: [").c(CSI + b"2 H").t("].").nl()
    s.t("A malformed CSI cut by a G1 letter: [").c(CSI + b"1").t("\xe9]; a sequence never finished (CSI then a line end):").c(CSI + b"4").nl()
    s.t("The line after it is whole.").nl()
    s.nl()

    # 7. ISO 6429 formatting the specification defers to (decipoints, PUM 2).
    s.c(sgr(1)).t("6. ISO 6429 formatting (CSI ... SP final)").c(sgr(22)).nl()
    s.c(CSI + b"240 C").t("GSS 240: 24-point text (240 decipoints).").c(CSI + b"120 C").nl()
    s.c(CSI + b"2 H").t("QUAD 2: this line is centred.").nl()
    s.c(CSI + b"4 H").t("QUAD 4: this line is flush right.").nl()
    s.c(CSI + b"0 H").t("QUAD 0: flush left again.").nl()
    s.nl()

    # 8. Latin-1 (G1).
    s.c(sgr(1)).t("7. ISO 8859-1 (G1, A0-FF)").c(sgr(22)).nl()
    s.t("Caf\xe9, na\xefve, \xc6r\xf8sk\xf8bing, \xbfQu\xe9?, \xabguillemets\xbb, 1\xbd \xd7 2\xbc, 90\xb0, \xa9 \xae \xa7 \xb6 \xb5 \xdf.").nl()
    for row in range(0xA0, 0x100, 16):
        s.t("%02X: " % row + "".join(chr(c) for c in range(row, row + 16))).nl()
    # This paragraph does not end in this CHRS: it goes on in the next one.
    s.t("8. This paragraph starts in one CHRS chunk ")
    chunks.append(chunk("CHRS", s.take()))

    # A property chunk between two CHRS applies to the later one.
    chunks.append(fons(2, 2, 2, "garnet"))
    s.t("and ends in the next; formatting carries over: ").c(sgr(1)).t("bold set in this chunk").nl()
    s.t("is still bold on the next line, because a CHRS does not reset it.").c(sgr(22)).nl()
    s.c(sgr(12)).t("SGR 12 now: font 2 is garnet, renamed by a FONS between the chunks.").c(sgr(11)).nl()
    chunks.append(chunk("CHRS", s.take()))

    # A nested FORM: readers skip it (a stripper ignores it).
    chunks.append(ilbm(16, 8))

    # An odd-length CHRS (padded) and an empty one.
    line = "9. After a nested FORM ILBM, an odd-length chunk."
    if len(line) % 2 == 1:          # with its LF, the chunk's length is odd
        line += "!"
    s.t(line).nl()
    data = s.take()
    assert len(data) % 2 == 1
    chunks.append(chunk("CHRS", data))
    chunks.append(chunk("CHRS", b""))

    # The last line has no LF: the stream simply ends.
    s.t("10. The end, with no line feed after it.")
    chunks.append(chunk("CHRS", s.take()))
    return group("FORM", "FTXT", chunks), "".join(s.text)


def build_list():
    """LIST FTXT: a PROP gives every FORM font 1; one FORM overrides it."""
    a = Stream().t("A LIST's first FORM: its font 1 is helvetica, from the LIST's PROP FTXT.").nl()
    b = Stream().t("The second FORM names its own font 1 (courier); ").c(sgr(1)).t("bold").c(sgr(22)).t(" set here").nl()
    c = Stream().t("The third FORM starts plain: formatting does not carry from one FORM to the next.").nl()
    data = group("LIST", "FTXT", [
        group("PROP", "FTXT", [fons(1, 2, 1, "helvetica")]),
        group("FORM", "FTXT", [chunk("CHRS", a.take())]),
        group("FORM", "FTXT", [fons(1, 1, 2, "courier"), chunk("CHRS", b.take())]),
        group("FORM", "FTXT", [chunk("CHRS", c.take())]),
    ])
    return data, "".join(a.text + b.text + c.text)


def build_cat():
    """CAT FTXT: two FORM FTXT and a picture, as a clipboard may hold them."""
    a = Stream().t("A CAT's first FORM FTXT.").nl()
    b = Stream().t("Its second FORM FTXT, after a FORM ILBM the text readers skip.").nl()
    data = group("CAT ", "FTXT", [
        group("FORM", "FTXT", [fons(1, 2, 2, "times"), chunk("CHRS", a.take())]),
        ilbm(32, 16),
        group("FORM", "FTXT", [chunk("CHRS", b.take())]),
    ])
    return data, "".join(a.text + b.text)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_stress_ftxt.py OUTDIR")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    data, text = build()
    with open(os.path.join(out, "ftxt-stress.ftxt"), "wb") as f:
        f.write(data)
    with open(os.path.join(out, "ftxt-stress.txt"), "wb") as f:
        f.write(text.encode("utf-8"))
    for name, (d, t) in (("ftxt-stress-list", build_list()), ("ftxt-stress-cat", build_cat())):
        with open(os.path.join(out, name + ".iff"), "wb") as f:
            f.write(d)
        with open(os.path.join(out, name + ".txt"), "wb") as f:
            f.write(t.encode("utf-8"))


if __name__ == "__main__":
    main()
