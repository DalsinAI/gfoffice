#!/usr/bin/env python3
"""Makes tests/core/fixtures/cover.docx: make_cover_docx.py [--wpg] [OUT]

A Word cover page as Word writes one, made by hand (no Word document goes
into the repository): a JPEG the size of the page behind the text
(wp:anchor behindDoc, wrapNone, from the page's corner), and four text boxes
in front of it (wps shapes in mc:AlternateContent, with VML fallbacks that
carry the same text): a title from the margin, a subtitle centred, a filled
and outlined box in the page's bottom right corner, and a date from the
paragraph. Then a page break, a heading, a paragraph with a picture in its
line and one placed at the right margin with text wrapped round it
(wrapSquare), and a header with a picture of its own (word/_rels/
header1.xml.rels). test_layout.c (make layout-test) and run_tests.py read
it. Run it again after changing it, and commit the file.

--wpg makes tests/core/fixtures/cover-wpg.docx, the same cover as Word writes
it in a document with a group anywhere (2.0.10, the plan's): every drawing,
the pictures' too, in mc:AlternateContent whose Choice requires wpg, with a
VML fallback; wp, wp14, a, pic and wps declared on the elements that use
them, not on the document; the cover picture aligned to the page's right and
placed from its paragraph, a little above it, a little larger than the page,
with wp14:sizeRelH and sizeRelV after its graphic; and one more shape, a
filled band with no text.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import base64
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ARGS = [a for a in sys.argv[1:] if a != "--wpg"]
WPG = "--wpg" in sys.argv[1:]
OUT = ARGS[0] if ARGS else os.path.join(HERE, "fixtures", "cover-wpg.docx" if WPG else "cover.docx")
URI_WP = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
URI_WP14 = "http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing"
URI_A = "http://schemas.openxmlformats.org/drawingml/2006/main"
URI_PIC = "http://schemas.openxmlformats.org/drawingml/2006/picture"
URI_WPS = "http://schemas.microsoft.com/office/word/2010/wordprocessingShape"

# 20 x 28 pixels, a blue gradient (made with PIL once; kept here so nothing else is needed)
JPEG = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAA0JCgsKCA0LCgsODg0PEyAVExISEyccHhcgLikxMC4pLSwzOko+MzZGNywtQFdBRkxOUlNSMj5aYVpQYEpRUk//2wBDAQ4ODhMREyYVFSZPNS01T09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT0//wAARCAAcABQDASIAAhEBAxEB/8QAGAAAAwEBAAAAAAAAAAAAAAAAAAMEAQb/xAAZEAADAQEBAAAAAAAAAAAAAAAAAgMBIRH/xAAXAQEBAQEAAAAAAAAAAAAAAAADBQQG/8QAFxEBAQEBAAAAAAAAAAAAAAAAAAMBAv/aAAwDAQACEQMRAD8A5JJD0kOSRQkiv0ywqRkuAW5LgBKGVYkihJDEXB6Lg3TnoU0rJcArxc8AJQymv//Z")

NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
      'xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" '
      'xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture" '
      'xmlns:wps="http://schemas.microsoft.com/office/word/2010/wordprocessingShape" '
      'xmlns:wp14="http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing" '
      'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
      'xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office" '
      'mc:Ignorable="wp14"')
if WPG:   # the drawing namespaces on the elements instead; wpg declared, as Word does with a group anywhere
    NS = ('xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" '
          'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
          'xmlns:wp14="%s" xmlns:wpg="http://schemas.microsoft.com/office/word/2010/wordprocessingGroup" '
          'xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" '
          'xmlns:v="urn:schemas-microsoft-com:vml" xmlns:o="urn:schemas-microsoft-com:office:office" '
          'mc:Ignorable="wp14"' % URI_WP14)


def declare(uri, prefix):
    """xmlns:prefix on the element itself (--wpg), else nothing (the document declares it)."""
    return ' xmlns:%s="%s"' % (prefix, uri) if WPG else ""

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Default Extension="jpeg" ContentType="image/jpeg"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/word/header1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"/>
</Types>"""

PACKAGE_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

IMAGE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"
DOCUMENT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/header" Target="header1.xml"/>
<Relationship Id="rId10" Type="%s" Target="media/cover.jpeg"/>
<Relationship Id="rId11" Type="%s" Target="media/side.jpeg"/>
</Relationships>""" % (IMAGE, IMAGE)

# the header's own relationships: its rId1 is a picture, the document's rId1 the styles
HEADER_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="%s" Target="media/logo.jpeg"/>
</Relationships>""" % IMAGE

STYLES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Trebuchet MS" w:hAnsi="Trebuchet MS"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>
<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/>
<w:pPr><w:keepNext/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/></w:rPr></w:style>
</w:styles>"""

EMU = 635                                  # a twip


def emu(twips):
    return twips * EMU


def position(axis, rel, offset=None, align=None):
    inner = "<wp:posOffset>%d</wp:posOffset>" % emu(offset) if align is None else "<wp:align>%s</wp:align>" % align
    return '<wp:position%s relativeFrom="%s">%s</wp:position%s>' % (axis, rel, inner, axis)


def anchor(ident, name, horizontal, vertical, cx, cy, graphic, behind=False, wrap="<wp:wrapNone/>", size_emu=None):
    """wp:anchor as Word writes it; cx, cy in twips (size_emu: the extent in EMU instead)."""
    ecx, ecy = size_emu if size_emu else (emu(cx), emu(cy))
    rel = ('<wp14:sizeRelH relativeFrom="page"><wp14:pctWidth>0</wp14:pctWidth></wp14:sizeRelH>'
           '<wp14:sizeRelV relativeFrom="page"><wp14:pctHeight>0</wp14:pctHeight></wp14:sizeRelV>') if WPG else ""
    return ('<wp:anchor%s%s distT="0" distB="0" distL="114300" distR="114300" simplePos="0" relativeHeight="%d" '
            'behindDoc="%d" locked="0" layoutInCell="1" allowOverlap="1"><wp:simplePos x="0" y="0"/>%s%s'
            '<wp:extent cx="%d" cy="%d"/><wp:effectExtent l="0" t="0" r="0" b="0"/>%s'
            '<wp:docPr id="%d" name="%s"/><wp:cNvGraphicFramePr/>%s%s</wp:anchor>'
            % (declare(URI_WP, "wp"), declare(URI_WP14, "wp14"), 251658240 + ident, 1 if behind else 0, horizontal,
               vertical, ecx, ecy, wrap, ident, name, graphic, rel))


def alternate(drawing, fallback, requires="wps"):
    """A drawing in mc:AlternateContent with its VML fallback (--wpg: the Choice requires wpg)."""
    return ('<mc:AlternateContent><mc:Choice Requires="%s"><w:drawing>%s</w:drawing></mc:Choice>'
            '<mc:Fallback>%s</mc:Fallback></mc:AlternateContent>' % ("wpg" if WPG else requires, drawing, fallback))


def vml_picture(ident, rid, cx, cy):
    return ('<w:pict><v:shape id="Picture %d" o:spid="_x0000_s%d" type="#_x0000_t75" '
            'style="position:absolute;width:%dpt;height:%dpt;z-index:%d"><v:imagedata r:id="%s" o:title=""/>'
            '</v:shape></w:pict>' % (ident, 1024 + ident, cx // 20, cy // 20, ident, rid))


def picture(ident, rid, cx, cy):
    return ('<a:graphic%s><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:pic%s>'
            '<pic:nvPicPr><pic:cNvPr id="%d" name="Picture %d"/><pic:cNvPicPr/></pic:nvPicPr>'
            '<pic:blipFill><a:blip r:embed="%s"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
            '<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="%d" cy="%d"/></a:xfrm>'
            '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic>'
            % (declare(URI_A, "a"), declare(URI_PIC, "pic"), ident, ident, rid, emu(cx), emu(cy)))


def box_para(text, size, colour, bold=False, jc=None):
    ppr = '<w:pPr><w:spacing w:after="0"/>%s</w:pPr>' % ('<w:jc w:val="%s"/>' % jc if jc else "")
    rpr = '<w:rPr>%s<w:color w:val="%s"/><w:sz w:val="%d"/></w:rPr>' % ("<w:b/>" if bold else "", colour, size)
    return '<w:p>%s<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r></w:p>' % (ppr, rpr, text)


def text_box(ident, horizontal, vertical, cx, cy, paras, fill=None, line=None):
    """A wps text box in mc:AlternateContent, its VML fallback with the same text, as Word writes them; with no
    paragraphs, a shape with no text."""
    sp_fill = '<a:solidFill><a:srgbClr val="%s"/></a:solidFill>' % fill if fill else "<a:noFill/>"
    sp_line = '<a:ln w="12700"><a:solidFill><a:srgbClr val="%s"/></a:solidFill></a:ln>' % line if line \
        else "<a:ln><a:noFill/></a:ln>"
    txbx = '<wps:txbx><w:txbxContent>%s</w:txbxContent></wps:txbx>' % paras if paras else ""
    wsp = ('<a:graphic%s><a:graphicData uri="http://schemas.microsoft.com/office/word/2010/wordprocessingShape">'
           '<wps:wsp%s><wps:cNvSpPr%s/><wps:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="%d" cy="%d"/></a:xfrm>'
           '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom>%s%s</wps:spPr>%s'
           '<wps:bodyPr rot="0" vert="horz" wrap="square" lIns="91440" tIns="45720" rIns="91440" bIns="45720" '
           'anchor="t" anchorCtr="0"><a:noAutofit/></wps:bodyPr></wps:wsp></a:graphicData></a:graphic>'
           % (declare(URI_A, "a"), declare(URI_WPS, "wps"), ' txBox="1"' if paras else "", emu(cx), emu(cy), sp_fill,
              sp_line, txbx))
    inner = '<v:textbox><w:txbxContent>%s</w:txbxContent></v:textbox>' % paras if paras else ""
    vml = ('<w:pict><v:shape id="Text Box %d" o:spid="_x0000_s%d" type="#_x0000_t202" '
           'style="position:absolute;margin-left:0;margin-top:0;width:%dpt;height:%dpt;z-index:%d" filled="%s" '
           'stroked="%s">%s</v:shape></w:pict>'
           % (ident, 1024 + ident, cx // 20, cy // 20, ident, "t" if fill else "f", "t" if line else "f", inner))
    return '<w:r>%s</w:r>' % alternate(anchor(ident, "Text Box %d" % ident, horizontal, vertical, cx, cy, wsp), vml)


def document():
    page_w, page_h = 11906, 16838
    if WPG:   # as the plan's: from the page's right and its paragraph, above it, a little larger than the page
        cover = '<w:r><w:rPr><w:noProof/></w:rPr>%s</w:r>' % alternate(anchor(
            1, "Cover picture", position("H", "page", align="right"), position("V", "paragraph", -1536), 0, 0,
            picture(1, "rId10", 11931, 16968), behind=True, size_emu=(7576336, 10774680)),
            vml_picture(1, "rId10", 11931, 16968))
    else:
        cover = '<w:r><w:rPr><w:noProof/></w:rPr><w:drawing>%s</w:drawing></w:r>' % anchor(
            1, "Cover picture", position("H", "page", 0), position("V", "page", 0), page_w, page_h,
            picture(1, "rId10", page_w, page_h), behind=True)
    title = text_box(2, position("H", "margin", 0), position("V", "page", 3600), 9026, 2160,
                     box_para("Cover Title", 72, "FFFFFF", bold=True))
    subtitle = text_box(3, position("H", "margin", align="center"), position("V", "page", 6240), 7200, 1080,
                        box_para("A plan made up for the tests", 32, "FFFFFF", jc="center"))
    version = text_box(4, position("H", "page", align="right"), position("V", "page", align="bottom"), 4320, 1440,
                       box_para("Version 1.0", 28, "FFFFFF"), fill="1F3864", line="FFFFFF")
    date = text_box(5, position("H", "margin", 0), position("V", "paragraph", 12960), 3600, 720,
                    box_para("25 October 2026", 24, "FFFFFF"))
    side = anchor(6, "Side picture", position("H", "margin", align="right"), position("V", "paragraph", 0), 1440,
                  2016, picture(6, "rId11", 1440, 2016), wrap='<wp:wrapSquare wrapText="bothSides"/>')
    side = ('<w:r>%s</w:r>' % alternate(side, vml_picture(6, "rId11", 1440, 2016)) if WPG
            else '<w:r><w:drawing>%s</w:drawing></w:r>' % side)
    band = text_box(8, position("H", "page", 0), position("V", "page", 0), page_w, 360, "", fill="C00000") \
        if WPG else ""
    inline = ('<w:r><w:drawing><wp:inline%s distT="0" distB="0" distL="0" distR="0"><wp:extent cx="%d" cy="%d"/>'
              '<wp:docPr id="7" name="Inline picture"/>%s</wp:inline></w:drawing></w:r>'
              % (declare(URI_WP, "wp"), emu(720), emu(1008), picture(7, "rId11", 720, 1008)))
    words = ("The body starts on the second page, where it would start without the cover's picture and text "
             "boxes, which move no line. ")
    body = [
        '<w:p>%s%s%s%s%s%s</w:p>' % (cover, title, subtitle, version, date, band),
        '<w:p><w:r><w:br w:type="page"/></w:r></w:p>',
        '<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr><w:r><w:t>Introduction</w:t></w:r></w:p>',
        '<w:p>%s<w:r><w:t xml:space="preserve">%s</w:t></w:r></w:p>' % (side, words * 3),
        '<w:p><w:r><w:t xml:space="preserve">A picture in the line: </w:t></w:r>%s</w:p>' % inline,
    ]
    sect = ('<w:sectPr><w:headerReference w:type="default" r:id="rId2"/><w:pgSz w:w="%d" w:h="%d"/>'
            '<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="708" w:footer="708" '
            'w:gutter="0"/><w:titlePg/></w:sectPr>' % (page_w, page_h))
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:document %s><w:body>%s%s</w:body>'
            '</w:document>' % (NS, "".join(body), sect))


def header():
    logo = ('<w:r><w:drawing><wp:inline%s distT="0" distB="0" distL="0" distR="0"><wp:extent cx="%d" cy="%d"/>'
            '<wp:docPr id="20" name="Logo"/>%s</wp:inline></w:drawing></w:r>'
            % (declare(URI_WP, "wp"), emu(360), emu(504), picture(20, "rId1", 360, 504)))
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:hdr %s><w:p>%s<w:r><w:t xml:space='
            '"preserve"> The plan</w:t></w:r></w:p></w:hdr>' % (NS, logo))


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    parts = [
        ("[Content_Types].xml", CONTENT_TYPES.encode("utf-8")),
        ("_rels/.rels", PACKAGE_RELS.encode("utf-8")),
        ("word/_rels/document.xml.rels", DOCUMENT_RELS.encode("utf-8")),
        ("word/_rels/header1.xml.rels", HEADER_RELS.encode("utf-8")),
        ("word/document.xml", document().encode("utf-8")),
        ("word/styles.xml", STYLES.encode("utf-8")),
        ("word/header1.xml", header().encode("utf-8")),
        ("word/media/cover.jpeg", JPEG),
        ("word/media/side.jpeg", JPEG),
        ("word/media/logo.jpeg", JPEG),
    ]
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    print(OUT)


if __name__ == "__main__":
    main()
