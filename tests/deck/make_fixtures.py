#!/usr/bin/env python3
"""Makes the presentation test files: make_fixtures.py [OUTDIR]

- fixtures/odp-hand/ is a package written by hand from OASIS ODF 1.2; its
  pictures are made here with PIL when missing. test_decks.py zips it.
- odfpy.odp is made here with odfpy (another program's ODP writer).
- libreoffice.odp and libreoffice.fodp are odp-hand saved by LibreOffice
  (soffice --convert-to), so that the reader meets the parts and styles
  LibreOffice writes.

Run it again after changing odp-hand or the slides below, and change
test_decks.py to match. odfpy, PIL and LibreOffice only make the test
files; nothing of them is in OpenPresentation.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else FIX


def pack(src, dst):
    """A folder of parts as an ODF package: mimetype first and stored."""
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(src, "mimetype"), "mimetype", compress_type=zipfile.ZIP_STORED)
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for f in sorted(files):
                path = os.path.join(root, f)
                name = os.path.relpath(path, src).replace(os.sep, "/")
                if name != "mimetype":
                    z.write(path, name)
    return dst


def pictures():
    from PIL import Image, ImageDraw
    hand = os.path.join(FIX, "odp-hand")
    red = os.path.join(hand, "Pictures", "red.png")
    if not os.path.exists(red):
        im = Image.new("RGB", (40, 30), (200, 30, 30))
        ImageDraw.Draw(im).rectangle([10, 8, 29, 21], fill=(250, 220, 40))
        im.save(red, dpi=(96, 96))
        im = Image.new("RGB", (32, 24), (40, 90, 200))
        ImageDraw.Draw(im).ellipse([6, 4, 25, 19], fill=(255, 255, 255))
        im.save(os.path.join(hand, "Pictures", "photo.jpg"), quality=80, dpi=(96, 96))
        Image.new("RGB", (16, 9), (230, 240, 255)).save(os.path.join(hand, "Pictures", "background.png"))
        im = Image.new("RGB", (20, 15), (240, 240, 240))
        d = ImageDraw.Draw(im)
        for box in ([3, 6, 6, 13], [9, 3, 12, 13], [15, 9, 18, 13]):
            d.rectangle(box, fill=(30, 100, 200))
        im.save(os.path.join(hand, "ObjectReplacements", "Object 1"), format="PNG")


def odfpy_deck(path):
    from odf.draw import Ellipse, Frame, Image as DrawImage, Line, Page, Rect, TextBox
    from odf.namespaces import PRESENTATIONNS
    from odf.opendocument import OpenDocumentPresentation
    from odf.presentation import Notes
    from odf.style import (DrawingPageProperties, GraphicProperties, MasterPage, PageLayout, PageLayoutProperties,
                           ParagraphProperties, Style, TableColumnProperties, TextProperties)
    from odf.table import Table, TableCell, TableColumn, TableRow
    from odf.text import A, List, ListItem, P, Span

    def cls(e, name, style=None):
        e.setAttrNS(PRESENTATIONNS, "class", name)
        if style:
            e.setAttrNS(PRESENTATIONNS, "style-name", style)
        return e

    doc = OpenDocumentPresentation()
    pl = PageLayout(name="PM1")
    pl.addElement(PageLayoutProperties(margin="0cm", pagewidth="25.4cm", pageheight="19.05cm", printorientation="landscape"))
    doc.automaticstyles.addElement(pl)
    mp = MasterPage(name="Plain", pagelayoutname=pl)
    doc.masterstyles.addElement(mp)
    title = Style(name="Plain-title", family="presentation")
    title.addElement(ParagraphProperties(textalign="center"))
    title.addElement(TextProperties(fontsize="36pt", fontweight="bold", color="#203040"))
    doc.styles.addElement(title)
    outline = Style(name="Plain-outline1", family="presentation")
    outline.addElement(TextProperties(fontsize="26pt"))
    doc.styles.addElement(outline)
    dp = Style(name="dp1", family="drawing-page")
    dp.addElement(DrawingPageProperties(fill="solid", fillcolor="#eef4ff"))
    doc.automaticstyles.addElement(dp)
    gr = Style(name="gr1", family="graphic")
    gr.addElement(GraphicProperties(fill="solid", fillcolor="#cc3333", stroke="solid", strokecolor="#000000",
                                    strokewidth="0.05cm"))
    doc.automaticstyles.addElement(gr)
    gr2 = Style(name="gr2", family="graphic")
    gr2.addElement(GraphicProperties(fill="none", stroke="none"))
    doc.automaticstyles.addElement(gr2)
    co = Style(name="co1", family="table-column")
    co.addElement(TableColumnProperties(columnwidth="4cm"))
    doc.automaticstyles.addElement(co)
    bold = Style(name="T1", family="text")
    bold.addElement(TextProperties(fontweight="bold", color="#008000"))
    doc.automaticstyles.addElement(bold)

    page = Page(name="One", masterpagename=mp, stylename=dp)
    doc.presentation.addElement(page)
    f = cls(Frame(width="22cm", height="3cm", x="1.7cm", y="1cm"), "title", "Plain-title")
    tb = TextBox()
    f.addElement(tb)
    tb.addElement(P(text="Made with odfpy"))
    page.addElement(f)
    f = cls(Frame(width="22cm", height="12cm", x="1.7cm", y="5cm"), "outline", "Plain-outline1")
    tb = TextBox()
    f.addElement(tb)
    lst = List()
    tb.addElement(lst)
    item = ListItem()
    p = P(text="Plain and ")
    p.addElement(Span(stylename=bold, text="bold green"))
    item.addElement(p)
    lst.addElement(item)
    item = ListItem()
    inner = List()
    sub = ListItem()
    sub.addElement(P(text="One level in"))
    inner.addElement(sub)
    item.addElement(inner)
    lst.addElement(item)
    item = ListItem()
    p = P(text="A link: ")
    p.addElement(A(href="https://example.org/", text="example.org"))
    item.addElement(p)
    lst.addElement(item)
    page.addElement(f)
    notes = Notes()
    nf = cls(Frame(width="16cm", height="10cm", x="2cm", y="14cm"), "notes")
    nt = TextBox()
    nf.addElement(nt)
    nt.addElement(P(text="odfpy's notes"))
    notes.addElement(nf)
    page.addElement(notes)

    page = Page(name="Two", masterpagename=mp)
    doc.presentation.addElement(page)
    f = cls(Frame(width="22cm", height="3cm", x="1.7cm", y="1cm"), "title", "Plain-title")
    tb = TextBox()
    f.addElement(tb)
    tb.addElement(P(text="Shapes, a picture, a table"))
    page.addElement(f)
    r = Rect(stylename=gr, width="5cm", height="3cm", x="1.7cm", y="5cm")
    r.addElement(P(text="Box"))
    page.addElement(r)
    page.addElement(Ellipse(stylename=gr, width="4cm", height="3cm", x="8cm", y="5cm"))
    page.addElement(Line(stylename=gr, x1="14cm", y1="5cm", x2="22cm", y2="8cm"))
    pic = doc.addPicture(os.path.join(FIX, "odp-hand", "Pictures", "red.png"))
    f = Frame(stylename=gr2, width="4cm", height="3cm", x="1.7cm", y="10cm")
    f.addElement(DrawImage(href=pic))
    page.addElement(f)
    f = Frame(stylename=gr2, width="12cm", height="3cm", x="8cm", y="10cm")
    t = Table()
    t.addElement(TableColumn(stylename=co, numbercolumnsrepeated=3))
    for row in (("A", "B", "C"), ("1", "2", "3")):
        tr = TableRow()
        for v in row:
            tc = TableCell()
            tc.addElement(P(text=v))
            tr.addElement(tc)
        t.addElement(tr)
    f.addElement(t)
    page.addElement(f)
    doc.save(path)


def libreoffice(src):
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    if not soffice:
        print("make_fixtures: no LibreOffice (soffice): libreoffice.odp and .fodp not made")
        return
    work = tempfile.mkdtemp(prefix="deck-fix-")
    for fmt in ("odp", "fodp"):
        subprocess.run([soffice, "--headless", "-env:UserInstallation=file://" + os.path.join(work, "profile"),
                        "--convert-to", fmt, "--outdir", work, src], check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=180)
        shutil.copy(os.path.join(work, "hand." + fmt), os.path.join(OUT, "libreoffice." + fmt))
    shutil.rmtree(work, ignore_errors=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    pictures()
    work = tempfile.mkdtemp(prefix="deck-hand-")
    hand = pack(os.path.join(FIX, "odp-hand"), os.path.join(work, "hand.odp"))
    odfpy_deck(os.path.join(OUT, "odfpy.odp"))
    libreoffice(hand)
    shutil.rmtree(work, ignore_errors=True)
    print("made", ", ".join(sorted(os.listdir(OUT))))


main()
