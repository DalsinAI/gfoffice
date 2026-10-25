#!/usr/bin/env python3
"""Makes the PPTX fixtures for OpenPresentation's PPTX reader:

    make_pptx_fixtures.py [OUT_DIR]     (default: fixtures/ beside this file)

Needs python-pptx (pip install python-pptx; it brings Pillow). The decks are
made here from nothing, with python-pptx's own template (its masters and
layouts are PowerPoint's 4:3 ones); no one's documents are used.

- pptx-talk.pptx: 16:9, five slides: a title slide; a title and a bullet
  list on three levels, with speaker notes; text boxes and shapes (a bold
  red run, a filled rectangle with text, an ellipse, a rounded rectangle, a
  triangle, a diamond, a right arrow, a line with an arrowhead); a picture
  (made here, a 64 x 48 PNG) and a 3 x 3 table with a filled header row; a
  hidden slide with a group of two shapes. Slide 2 has a push transition
  and slide 3 a fade (written into the slide's XML: python-pptx has no call
  for them).
- pptx-plain.pptx: python-pptx's defaults as they are (4:3), two slides.

The fixtures are checked in; run this again only to change them, and
update test_pptx.py's expectations with them.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import io
import os
import sys

from lxml import etree
from PIL import Image
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import PP_ALIGN
from pptx.util import Emu, Inches, Pt

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures")
P = "http://schemas.openxmlformats.org/presentationml/2006/main"


def picture_png():
    """A 64 x 48 picture: four coloured quarters."""
    im = Image.new("RGB", (64, 48), (255, 255, 255))
    px = im.load()
    for y in range(48):
        for x in range(64):
            px[x, y] = ((200, 40, 40) if x < 32 else (40, 120, 200)) if y < 24 else \
                       ((60, 160, 60) if x < 32 else (240, 200, 40))
    b = io.BytesIO()
    im.save(b, "PNG")
    return b.getvalue()


def transition(slide, xml):
    """A p:transition after the slide's p:clrMapOvr (or p:cSld), where
    PresentationML puts it."""
    el = etree.fromstring(f'<p:transition xmlns:p="{P}" {xml}')
    sld = slide._element
    after = sld.find(f"{{{P}}}clrMapOvr")
    if after is None:
        after = sld.find(f"{{{P}}}cSld")
    after.addnext(el)


def talk(path):
    prs = Presentation()
    prs.slide_width = Emu(12192000)          # 13.333 x 7.5 inches: PowerPoint's 16:9
    prs.slide_height = Emu(6858000)
    prs.core_properties.title = "OpenPresentation PPTX fixture"
    layouts = prs.slide_layouts
    # the template's master and layouts are 4:3: their boxes widened to 16:9, as PowerPoint does
    for part in [prs.slide_master] + list(layouts):
        for shape in part.shapes:
            off = shape._element.find(".//{http://schemas.openxmlformats.org/drawingml/2006/main}off")
            ext = shape._element.find(".//{http://schemas.openxmlformats.org/drawingml/2006/main}ext")
            if off is not None and ext is not None:     # its own box, not its master's
                off.set("x", str(int(off.get("x")) * 4 // 3))
                ext.set("cx", str(int(ext.get("cx")) * 4 // 3))

    # 1: a title slide
    s = prs.slides.add_slide(layouts[0])
    s.shapes.title.text = "GFOffice on the Amiga"
    s.placeholders[1].text = "OpenPresentation reads PowerPoint"

    # 2: a title and a bullet list, with notes
    s = prs.slides.add_slide(layouts[1])
    s.shapes.title.text = "What it reads"
    body = s.placeholders[1].text_frame
    body.text = "Titles and bullet lists"
    for text, level in (("A second level", 1), ("A third level", 2), ("Text boxes and shapes", 0),
                        ("Pictures and tables", 0)):
        p = body.add_paragraph()
        p.text = text
        p.level = level
    s.notes_slide.notes_text_frame.text = "Speaker notes for slide two."
    transition(s, 'spd="fast"><p:push dir="u"/></p:transition>')

    # 3: text boxes and shapes
    s = prs.slides.add_slide(layouts[5])     # Title Only
    s.shapes.title.text = "Shapes"
    tb = s.shapes.add_textbox(Inches(0.5), Inches(1.5), Inches(4), Inches(0.8))
    tb.name = "Note"
    p = tb.text_frame.paragraphs[0]
    r = p.add_run()
    r.text = "A text box with "
    r = p.add_run()
    r.text = "bold red"
    r.font.bold = True
    r.font.color.rgb = RGBColor(0xC0, 0x00, 0x00)
    r = p.add_run()
    r.text = " words"
    p2 = tb.text_frame.add_paragraph()
    p2.text = "Second line, centred"
    p2.alignment = PP_ALIGN.CENTER
    p2.runs[0].font.size = Pt(24)
    p2.runs[0].font.italic = True

    box = s.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.5), Inches(3), Inches(2.5), Inches(1.5))
    box.name = "Box"
    box.fill.solid()
    box.fill.fore_color.rgb = RGBColor(0x1F, 0x38, 0x64)
    box.line.color.rgb = RGBColor(0x00, 0x00, 0x00)
    box.line.width = Pt(2)
    box.text_frame.text = "Rectangle"
    ell = s.shapes.add_shape(MSO_SHAPE.OVAL, Inches(3.5), Inches(3), Inches(2), Inches(1.5))
    ell.name = "Ellipse"
    ell.text_frame.text = "Ellipse"
    rr = s.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(6), Inches(3), Inches(2.5), Inches(1.5))
    rr.name = "Rounded"
    rr.fill.solid()
    rr.fill.fore_color.rgb = RGBColor(0xFF, 0xC0, 0x00)
    rr.text_frame.text = "Rounded"
    tri = s.shapes.add_shape(MSO_SHAPE.ISOSCELES_TRIANGLE, Inches(9), Inches(3), Inches(1.5), Inches(1.5))
    tri.name = "Triangle"
    dia = s.shapes.add_shape(MSO_SHAPE.DIAMOND, Inches(11), Inches(3), Inches(1.5), Inches(1.5))
    dia.name = "Diamond"
    arrow = s.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, Inches(0.5), Inches(5.2), Inches(3), Inches(1))
    arrow.name = "Next"
    arrow.text_frame.text = "Next"
    line = s.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(4), Inches(5.7), Inches(8), Inches(6.5))
    line.name = "Line"
    line.line.color.rgb = RGBColor(0xC0, 0x00, 0x00)
    line.line.width = Pt(3)
    ln = line.line._get_or_add_ln()
    ln.append(etree.fromstring('<a:tailEnd xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" type="triangle"/>'))
    transition(s, 'spd="med" advTm="5000"><p:fade/></p:transition>')

    # 4: a picture and a table
    s = prs.slides.add_slide(layouts[5])
    s.shapes.title.text = "A picture and a table"
    pic = s.shapes.add_picture(io.BytesIO(picture_png()), Inches(0.5), Inches(1.6), Inches(4), Inches(3))
    pic.name = "Quarters"
    pic._element.nvPicPr.cNvPr.set("descr", "Four coloured quarters")
    rows, cols = 3, 3
    table = s.shapes.add_table(rows, cols, Inches(5), Inches(1.6), Inches(7.5), Inches(2)).table
    table.columns[0].width = Inches(3.5)
    table.columns[1].width = Inches(2)
    table.columns[2].width = Inches(2)
    for c, text in enumerate(("Program", "Reads", "Saves")):
        table.cell(0, c).text = text
    for r, row in enumerate((("OpenWrite", "DOCX ODT", "DOCX ODT"), ("OpenPresentation", "PPTX ODP", "ODP")), 1):
        for c, text in enumerate(row):
            table.cell(r, c).text = text
    table.cell(2, 2).fill.solid()
    table.cell(2, 2).fill.fore_color.rgb = RGBColor(0xE2, 0xEF, 0xDA)

    # 5: hidden, a group
    s = prs.slides.add_slide(layouts[6])     # Blank
    s._element.set("show", "0")
    grp = s.shapes.add_group_shape()
    grp.name = "Pair"
    a = grp.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(1), Inches(1), Inches(2), Inches(1))
    a.name = "Left"
    b = grp.shapes.add_shape(MSO_SHAPE.OVAL, Inches(4), Inches(1), Inches(2), Inches(1))
    b.name = "Right"
    t = s.shapes.add_textbox(Inches(1), Inches(3), Inches(6), Inches(1))
    t.text_frame.text = "Backup (hidden)"

    prs.save(path)


def plain(path):
    prs = Presentation()
    s = prs.slides.add_slide(prs.slide_layouts[0])
    s.shapes.title.text = "Made with python-pptx"
    s.placeholders[1].text = "Its own template, 4:3"
    s = prs.slides.add_slide(prs.slide_layouts[1])
    s.shapes.title.text = "Two points"
    tf = s.placeholders[1].text_frame
    tf.text = "One"
    tf.add_paragraph().text = "Two"
    prs.save(path)


def main():
    os.makedirs(OUT, exist_ok=True)
    talk(os.path.join(OUT, "pptx-talk.pptx"))
    plain(os.path.join(OUT, "pptx-plain.pptx"))
    print("wrote", os.path.join(OUT, "pptx-talk.pptx"), "and", os.path.join(OUT, "pptx-plain.pptx"))


if __name__ == "__main__":
    main()
