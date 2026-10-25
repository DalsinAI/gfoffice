#!/usr/bin/env python3
"""make_pdf_widths.py [FONTDIR]: writes libowf/src/owf_pdffonts.c, the
widths the page PDF writer (owf_pdf.c) measures text with.

PDF's standard fonts (Helvetica, Times, Courier) need no embedding, so the
PDF's text stays small and searchable, but a writer must know their widths
to place it. These are the advance widths of the Liberation fonts (Liberation
Sans for Helvetica, Liberation Serif for Times; SIL Open Font Licence 1.1),
which are made to the same metrics, in thousandths of an em, for each byte of
WinAnsiEncoding from 32 to 255. Courier's are all 600.

FONTDIR is where LiberationSans-Regular.ttf and the others are
(default /usr/share/fonts/truetype/liberation). Needs fontTools (tests and
tools only; pip install fonttools).
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys

from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTDIR = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/liberation"
FACES = [
    ("Helvetica", "LiberationSans-Regular.ttf"), ("Helvetica-Bold", "LiberationSans-Bold.ttf"),
    ("Helvetica-Oblique", "LiberationSans-Italic.ttf"), ("Helvetica-BoldOblique", "LiberationSans-BoldItalic.ttf"),
    ("Times-Roman", "LiberationSerif-Regular.ttf"), ("Times-Bold", "LiberationSerif-Bold.ttf"),
    ("Times-Italic", "LiberationSerif-Italic.ttf"), ("Times-BoldItalic", "LiberationSerif-BoldItalic.ttf"),
]


def widths(path):
    font = TTFont(path)
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    upem = font["head"].unitsPerEm
    space = hmtx[cmap[32]][0]
    out = []
    for b in range(32, 256):
        try:
            ch = bytes([b]).decode("cp1252")
        except UnicodeDecodeError:
            ch = None
        glyph = cmap.get(ord(ch)) if ch else None
        adv = hmtx[glyph][0] if glyph else space
        out.append((adv * 1000 + upem // 2) // upem)
    return out


def main():
    lines = [
        "/*",
        " * libowf: the widths of PDF's standard fonts, for the page PDF writer",
        " * (owf_pdf.c), in thousandths of an em, for WinAnsiEncoding's bytes 32 to",
        " * 255. Made by tools/make_pdf_widths.py from the Liberation fonts' advance",
        " * widths (SIL Open Font Licence 1.1), which match Helvetica's and Times'.",
        " * Courier's are all 600. Do not edit: run the tool again.",
        " * MIT, Copyright (c) 2026 AmigaChrome Team.",
        " */",
        '#include "owf_internal.h"',
        "",
        "const unsigned short owf_pdf_widths[8][224] = {",
    ]
    for name, file in FACES:
        w = widths(os.path.join(FONTDIR, file))
        lines.append(f"    /* {name} */")
        lines.append("    {")
        for i in range(0, 224, 16):
            lines.append("        " + ", ".join(str(v) for v in w[i:i + 16]) + ",")
        lines.append("    },")
    lines.append("};")
    with open(os.path.join(HERE, "..", "libowf", "src", "owf_pdffonts.c"), "w") as f:
        f.write("\n".join(lines) + "\n")


main()
