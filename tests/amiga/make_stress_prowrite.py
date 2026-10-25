#!/usr/bin/env python3
"""A ProWrite (IFF FORM WORD) stress document: everything the format holds.
Team 5 (W142), 26 October 2026 (Dale: "generate a really complicated pptx ...
Same for Publisher and PageStream, Final Writer etc.").

Written from New Horizons Software's published specification ("WORD IFF
Document Storage", James Bayless, March 1987), not from libowf's reader:
  FONT  UBYTE Num, UWORD Size, the name (no ".font"): one per font and size
  COLR  UBYTE ISOColors[8]: the file's colour numbers as ISO 6429 colours
  DOC   UWORD StartPage, UBYTE PageNumStyle (0 1 2 3 / 1 I II / 2 i ii /
        3 A B / 4 a b), pad: starts the document's section
  HEAD, FOOT  UBYTE PageType (0 none, 1 left, 2 right, 3 both), UBYTE
        FirstPage (0: not on the first page), LONG pad: start a header or
        footer section (the format allows several; ProWrite one of each)
  PCTS  UBYTE NPlanes (3 for ProWrite), pad: starts a picture section; its
        PINF (UWORD Width, Height, Page, XPos, YPos; ILBM Masking and
        Compression; UBYTE TransparentColor, pad) is followed by an ILBM BODY
  PARA  UWORD LeftIndent, LeftMargin, RightMargin (decipoints, 1/720 inch);
        UBYTE Spacing (0 single, 0x10 double), Justify (0 left, 1 centre,
        2 right, 3 full), FontNum, Style, MiscStyle (0, 1 super, 2 sub),
        Color; LONG pad
  TABS  UWORD Position, UBYTE Type (0 left, 1 centre, 2 right, 3 decimal),
        pad, per stop (ProWrite has no centre tabs; the format does)
  PAGE  a page break
  TEXT  one paragraph, even an empty one; 0x80 page number, 0x81 date,
        0x82 time (ProWrite: the page number, in headers and footers only)
  FSCC  UWORD Location, UBYTE FontNum, Style, MiscStyle, Color, UWORD pad:
        the changes inside the TEXT before it
Structures are laid out as a 68000 C compiler lays them out (words on even
offsets), as ProWrite's own files show: FONT's Size is at 2 and its name at
4. Style bits are graphics.library's: 1 underlined, 2 bold, 4 italic. A real
ProWrite 3 file (Aminet text/misc/pwconv202, "Convert Notes") was looked at
for the layout only: its COLR holds ISO colour numbers 30-37, and it has
chunks the 1987 specification doesn't name (PSET, PREC, SECT), which are
left out here because nothing published says what they hold.

    python3 make_stress_prowrite.py OUTDIR

writes prowrite-stress.pw and prowrite-stress.txt (the document's text, a
paragraph a line; page breaks as form feeds; headers, footers and pictures
after the body). Nothing here is from a private document.
"""
import os
import struct
import sys

PAGENUM, DATE, TIME = b"\x80", b"\x81", b"\x82"
UNDERLINED, BOLD, ITALIC = 1, 2, 4
SUPER, SUB = 1, 2
LEFT, CENTRE, RIGHT, FULL = 0, 1, 2, 3
INCH = 720


def chunk(cid, data):
    out = cid.encode("ascii") + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def form(ftype, chunks):
    body = ftype.encode("ascii") + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def font(num, size, name):
    return chunk("FONT", struct.pack(">BxH", num, size) + name.encode("latin-1") + b"\0")


def para(left_indent=0, left_margin=0, right_margin=0, spacing=0, justify=LEFT, font=0, style=0, misc=0, color=0):
    return chunk("PARA", struct.pack(">HHHBBBBBBI", left_indent, left_margin, right_margin, spacing, justify,
                                     font, style, misc, color, 0))


def tabs(*stops):
    return chunk("TABS", b"".join(struct.pack(">HBx", pos, kind) for pos, kind in stops))


def fscc(*changes):
    """(location, font, style, misc, color) per change."""
    return chunk("FSCC", b"".join(struct.pack(">HBBBBxx", *c) for c in changes))


def byterun1(row):
    """ILBM's ByteRun1 packing of one row."""
    out, i = bytearray(), 0
    while i < len(row):
        run = 1
        while i + run < len(row) and run < 128 and row[i + run] == row[i]:
            run += 1
        if run >= 3:
            out += bytes([257 - run, row[i]])
            i += run
            continue
        lit = i
        while i < len(row) and i - lit < 128:
            if i + 2 < len(row) and row[i] == row[i + 1] == row[i + 2]:
                break
            i += 1
        out += bytes([i - lit - 1]) + row[lit:i]
    return bytes(out)


def picture_body(width, height, planes, mask, packed):
    """An ILBM BODY: colour bars, one per pen, with a mask plane if asked."""
    rowbytes = (width + 15) // 16 * 2
    out = bytearray()
    for y in range(height):
        for p in range(planes):
            row = bytearray(rowbytes)
            for x in range(width):
                pen = (x * 8 // width) if y < height - 4 else 0
                if pen >> p & 1:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += byterun1(bytes(row)) if packed else row
        if mask:
            row = bytearray(rowbytes)
            for x in range(width):
                if (x // 4 + y // 4) % 2 == 0:
                    row[x >> 3] |= 0x80 >> (x & 7)
            out += byterun1(bytes(row)) if packed else row
    return bytes(out)


class Doc:
    """Chunks, and beside them the text a reader shows."""

    def __init__(self):
        self.chunks = []
        self.text = []

    def add(self, *chunks):
        self.chunks += chunks

    def p(self, raw, shown=None, changes=None):
        """A paragraph: TEXT, then its FSCC if it has changes."""
        self.chunks.append(chunk("TEXT", raw))
        if changes:
            self.chunks.append(fscc(*changes))
        self.text.append(raw.decode("latin-1") if shown is None else shown)


# Font numbers are any byte (ProWrite's own files use 0x06, 0x22, 0x6b...).
TIMES12, HELV24, COURIER10, TIMES18, TOPAZ8, DIAMOND12, BIG = 0x22, 0x6B, 0x10, 0x23, 0x5D, 0xFF, 0x01


def build():
    d = Doc()
    d.add(font(TIMES12, 12, "Times"), font(HELV24, 24, "Helvetica"), font(COURIER10, 10, "Courier"),
          font(TIMES18, 18, "Times"), font(TOPAZ8, 8, "topaz"), font(DIAMOND12, 12, "diamond"),
          font(BIG, 72, "CGTimes"))
    # ProWrite's own map (as its files have it): pen 0 black, 1 white, 2 red,
    # 3 cyan, 4 green, 5 magenta, 6 blue, 7 yellow.
    d.add(chunk("COLR", bytes([30, 37, 31, 36, 32, 35, 34, 33])))
    d.add(chunk("DOC ", struct.pack(">HBxI", 7, 1, 0)))   # pages start at VII

    # Title: centred, Helvetica 24, bold, blue.
    d.add(para(justify=CENTRE, font=HELV24, style=BOLD, color=6))
    d.p(b"ProWrite stress document")
    d.add(para(justify=CENTRE, font=TIMES12, style=ITALIC))
    d.p(b"Team 5, W142: every chunk the 1987 specification names")
    d.add(para(font=TIMES12))
    d.p(b"")

    # 1. Character formatting through FSCC.
    d.add(para(font=TIMES12, style=BOLD))
    d.p(b"1. Fonts, sizes, styles and colours")
    text = (b"Plain, bold, italic, underlined, all three, Courier 10, Times 18, topaz 8, diamond, "
            b"CGTimes 72, H2O and E=mc2, red, green, blue, yellow, magenta, cyan, white, back to plain.")

    def at(word):
        return text.index(word)

    changes = [
        (at(b"bold"), TIMES12, BOLD, 0, 0), (at(b", italic"), TIMES12, 0, 0, 0),
        (at(b"italic"), TIMES12, ITALIC, 0, 0), (at(b", underlined"), TIMES12, 0, 0, 0),
        (at(b"underlined"), TIMES12, UNDERLINED, 0, 0), (at(b", all three"), TIMES12, 0, 0, 0),
        (at(b"all three"), TIMES12, BOLD | ITALIC | UNDERLINED, 0, 0), (at(b", Courier"), TIMES12, 0, 0, 0),
        (at(b"Courier 10"), COURIER10, 0, 0, 0), (at(b", Times 18"), TIMES12, 0, 0, 0),
        (at(b"Times 18"), TIMES18, 0, 0, 0), (at(b", topaz"), TIMES12, 0, 0, 0),
        (at(b"topaz 8"), TOPAZ8, 0, 0, 0), (at(b", diamond"), TIMES12, 0, 0, 0),
        (at(b"diamond"), DIAMOND12, 0, 0, 0), (at(b", CGTimes"), TIMES12, 0, 0, 0),
        (at(b"CGTimes 72"), BIG, 0, 0, 0), (at(b", H2O"), TIMES12, 0, 0, 0),
        (at(b"2O"), TIMES12, 0, SUB, 0), (at(b"O and"), TIMES12, 0, 0, 0),
        (at(b"2, red"), TIMES12, 0, SUPER, 0), (at(b", red"), TIMES12, 0, 0, 0),
        (at(b"red"), TIMES12, 0, 0, 2), (at(b", green"), TIMES12, 0, 0, 0),
        (at(b"green"), TIMES12, 0, 0, 4), (at(b", blue"), TIMES12, 0, 0, 0),
        (at(b"blue"), TIMES12, 0, 0, 6), (at(b", yellow"), TIMES12, 0, 0, 0),
        (at(b"yellow"), TIMES12, 0, 0, 7), (at(b", magenta"), TIMES12, 0, 0, 0),
        (at(b"magenta"), TIMES12, 0, 0, 5), (at(b", cyan"), TIMES12, 0, 0, 0),
        (at(b"cyan"), TIMES12, 0, 0, 3), (at(b", white"), TIMES12, 0, 0, 0),
        (at(b"white"), TIMES12, BOLD, 0, 1), (at(b", back"), TIMES12, 0, 0, 0),
    ]
    d.add(para(font=TIMES12))
    d.p(text, changes=sorted(changes))
    # A paragraph that starts formatted (PARA's own font and style) and ends plain.
    d.add(para(font=COURIER10, style=BOLD | UNDERLINED, color=2))
    t = b"This paragraph starts in red bold underlined Courier from its PARA, and an FSCC makes the rest plain Times."
    d.p(t, changes=[(t.index(b", and"), TIMES12, 0, 0, 0)])
    # Superscript and subscript for a whole paragraph (PARA's MiscStyle).
    d.add(para(font=TIMES12, misc=SUPER))
    d.p(b"A whole paragraph raised (MiscStyle 1).")
    d.add(para(font=TIMES12, misc=SUB))
    d.p(b"A whole paragraph lowered (MiscStyle 2).")
    d.add(para(font=TIMES12))
    d.p(b"")

    # 2. Paragraph formatting.
    d.add(para(font=TIMES12, style=BOLD))
    d.p(b"2. Alignment, indents, spacing")
    long = b"The quick brown fox jumps over the lazy dog, and the dog does not mind at all. " * 5
    for justify, name in ((LEFT, b"Left"), (CENTRE, b"Centred"), (RIGHT, b"Right"), (FULL, b"Fully justified")):
        d.add(para(justify=justify, font=TIMES12))
        d.p(name + b": " + long.strip())
    d.add(para(left_indent=INCH, left_margin=INCH, right_margin=6 * INCH, font=TIMES12))
    d.p(b"LeftIndent and LeftMargin 720 (an inch), RightMargin 4320 (six inches; what it is measured from is not said): " + long.strip())
    d.add(para(left_indent=0, left_margin=INCH // 2, font=TIMES12))
    d.p(b"\xb7\tA hanging indent: the first line starts at the margin, the rest at half an inch. " + long.strip())
    d.add(para(left_indent=INCH, left_margin=0, font=TIMES12))
    d.p(b"A first-line indent of an inch, the rest at the margin. " + long.strip())
    d.add(para(spacing=0x10, font=TIMES12))
    d.p(b"Double-spaced (Spacing 0x10): " + long.strip())
    d.add(para(font=TIMES12))
    d.p(b"")

    # 3. Tab stops of every kind.
    d.add(para(font=TIMES12, style=BOLD))
    d.p(b"3. Tab stops: left 1 in, centre 2.5 in, right 4 in, decimal 5.5 in")
    d.add(para(font=TIMES12), tabs((INCH, 0), (INCH * 5 // 2, 1), (INCH * 4, 2), (INCH * 11 // 2, 3)))
    d.p(b"Start\tLeft\tCentre\tRight\t3.14159")
    d.p(b"\tl\tcentred text\tright text\t1234.5")
    d.p(b"\t\t\t\t-0.001")
    d.add(tabs())
    d.p(b"No tab stops now: a tab here\tgoes to the default stop.")
    d.p(b"")

    # 4. Special characters and Latin-1.
    d.add(para(font=TIMES12, style=BOLD))
    d.p(b"4. Special characters")
    d.add(para(font=TIMES12))
    d.p(b"In the body: page " + PAGENUM + b", date " + DATE + b", time " + TIME + b" (ProWrite allows them in headers and footers only).",
        shown="In the body: page #, date @date, time @time (ProWrite allows them in headers and footers only).")
    d.p("Latin-1: Caf\xe9, na\xefve, \xc6r\xf8sk\xf8bing, \xbfQu\xe9?, \xabguillemets\xbb, 1\xbd \xd7 2\xbc, 90\xb0, \xa9 \xae \xa7 \xb6 \xb5 \xdf.".encode("latin-1"))
    d.p(b"")
    para_text = b" ".join(b"Run %d" % i for i in range(1, 121)) + b"."
    runs = [(para_text.index(b"Run %d " % i if i < 120 else b"Run 120"), TIMES12, (i % 8) & 7, 0, i % 8) for i in range(1, 121)]
    d.add(para(font=TIMES12))
    d.p(para_text, changes=runs)

    # 5. Pages.
    d.add(chunk("PAGE", b""))
    d.add(para(font=HELV24, style=BOLD, justify=CENTRE))
    d.p(b"5. The second page (VIII)", shown="\f5. The second page (VIII)")
    d.add(para(font=TIMES12))
    d.p(b"A page break (PAGE) came before this page's first paragraph.")
    d.add(chunk("PAGE", b""))
    d.add(chunk("PAGE", b""))
    d.add(para(font=TIMES12))
    d.p(b"Two page breaks in a row: page IX is empty, and this is page X.", shown="\f\fTwo page breaks in a row: page IX is empty, and this is page X.")
    d.p(b"", shown="")

    body = d.text
    d.text = []
    # 6. Headers and footers: left, right, both; first page or not.
    d.add(chunk("HEAD", struct.pack(">BBI", 1, 0, 0)))
    d.add(para(justify=LEFT, font=TOPAZ8, style=ITALIC))
    d.p(b"Left-page header, page " + PAGENUM, shown="Left-page header, page #")
    d.add(chunk("HEAD", struct.pack(">BBI", 2, 0, 0)))
    d.add(para(justify=RIGHT, font=TOPAZ8, style=ITALIC), tabs((INCH * 3, 1)))
    d.p(b"Right-page header\t" + DATE + b" " + TIME + b"\tpage " + PAGENUM, shown="Right-page header\t@date @time\tpage #")
    d.add(chunk("FOOT", struct.pack(">BBI", 3, 1, 0)))
    d.add(para(justify=CENTRE, font=TIMES12))
    d.p(b"- " + PAGENUM + b" -", shown="- # -")
    d.p(b"A second footer paragraph, on the first page too.")
    furniture = d.text

    # 7. Pictures: a packed picture with a transparent colour on page VII and
    # an unpacked one with a mask on page VIII.
    d.add(chunk("PCTS", bytes([3, 0])))
    d.add(chunk("PINF", struct.pack(">HHHHHBBBB", 96, 40, 0, INCH * 5, INCH, 2, 1, 0, 0)))
    d.add(chunk("BODY", picture_body(96, 40, 3, False, True)))
    d.add(chunk("PCTS", bytes([3, 0])))
    d.add(chunk("PINF", struct.pack(">HHHHHBBBB", 48, 48, 1, INCH, INCH * 3, 1, 0, 0, 0)))
    d.add(chunk("BODY", picture_body(48, 48, 3, True, False)))
    return form("WORD", d.chunks), body, furniture


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_stress_prowrite.py OUTDIR")
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    data, body, furniture = build()
    with open(os.path.join(out, "prowrite-stress.pw"), "wb") as f:
        f.write(data)
    text = "\n".join(body) + "\n--- headers and footers ---\n" + "\n".join(furniture) + "\n--- 2 pictures ---\n"
    with open(os.path.join(out, "prowrite-stress.txt"), "wb") as f:
        f.write(text.encode("utf-8"))


if __name__ == "__main__":
    main()
