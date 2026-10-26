#!/usr/bin/env python3
"""Makes tests/core/fixtures/inline-box.docx: make_inline_docx.py [OUT]

Text boxes in the line (wp:inline holding a wps shape), as Word writes them,
made by hand (no Word document goes into the repository); it borrows
make_cover_docx.py's parts. On A4 with inch margins:
- a paragraph that starts with a filled text box 2 inches wide, "First box",
  followed by text;
- a paragraph with text, a text box "Second box" in its line, and more text;
- a picture in the line, which stays a picture in the line.
OpenWrite 2.0.16 and earlier dropped such text boxes. test_layout.c (make
layout-test) and test_core.c read it: the boxes kept, in their lines, and
written back as wp:inline. Run it again after changing it, and commit the
file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "inline-box.docx")
sys.argv = sys.argv[:1]                    # make_cover_docx reads its own arguments on import
sys.path.insert(0, HERE)
import make_cover_docx as cover            # noqa: E402

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Default Extension="jpeg" ContentType="image/jpeg"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>"""

DOCUMENT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
<Relationship Id="rId11" Type="%s" Target="media/picture.jpeg"/>
</Relationships>""" % cover.IMAGE


def inline_box(ident, cx, cy, text, fill):
    """A wps text box in the line, in mc:AlternateContent with a VML fallback, as Word writes one."""
    graphic = ('<a:graphic><a:graphicData uri="http://schemas.microsoft.com/office/word/2010/wordprocessingShape">'
               '<wps:wsp><wps:cNvSpPr txBox="1"/><wps:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="%d" cy="%d"/>'
               '</a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom><a:solidFill><a:srgbClr val="%s"/>'
               '</a:solidFill><a:ln w="6350"><a:solidFill><a:srgbClr val="000000"/></a:solidFill></a:ln></wps:spPr>'
               '<wps:txbx><w:txbxContent>%s</w:txbxContent></wps:txbx><wps:bodyPr rot="0" vert="horz" wrap="square" '
               'lIns="91440" tIns="45720" rIns="91440" bIns="45720" anchor="t" anchorCtr="0"><a:noAutofit/>'
               '</wps:bodyPr></wps:wsp></a:graphicData></a:graphic>'
               % (cover.emu(cx), cover.emu(cy), fill, cover.box_para(text, 22, "000000")))
    drawing = ('<w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx="%d" cy="%d"/>'
               '<wp:effectExtent l="0" t="0" r="0" b="0"/><wp:docPr id="%d" name="Text Box %d"/>'
               '<wp:cNvGraphicFramePr/>%s</wp:inline></w:drawing>'
               % (cover.emu(cx), cover.emu(cy), ident, ident, graphic))
    vml = ('<w:pict><v:shape id="Text Box %d" o:spid="_x0000_s%d" type="#_x0000_t202" style="width:%dpt;height:%dpt" '
           'filled="t"><v:textbox><w:txbxContent>%s</w:txbxContent></v:textbox></v:shape></w:pict>'
           % (ident, 1024 + ident, cx // 20, cy // 20, cover.box_para(text, 22, "000000")))
    return ('<w:r><mc:AlternateContent><mc:Choice Requires="wps">%s</mc:Choice><mc:Fallback>%s</mc:Fallback>'
            '</mc:AlternateContent></w:r>' % (drawing, vml))


def run(text):
    return '<w:r><w:t xml:space="preserve">%s</w:t></w:r>' % text


def document():
    picture = ('<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx="%d" cy="%d"/>'
               '<wp:docPr id="40" name="Inline picture"/>%s</wp:inline></w:drawing></w:r>'
               % (cover.emu(720), cover.emu(720), cover.picture(40, "rId11", 720, 720)))
    body = [
        '<w:p>%s%s</w:p>' % (inline_box(31, 2880, 720, "First box", "FFF2CC"), run(" and the text after it.")),
        '<w:p>%s%s%s</w:p>' % (run("Before the box "), inline_box(32, 2160, 576, "Second box", "DDEBF7"),
                               run(" after the box.")),
        '<w:p>%s%s</w:p>' % (run("A picture in the line: "), picture),
    ]
    sect = ('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" '
            'w:left="1440" w:header="708" w:footer="708" w:gutter="0"/></w:sectPr>')
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:document %s><w:body>%s%s</w:body>'
            '</w:document>' % (cover.NS, "".join(body), sect))


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    parts = [
        ("[Content_Types].xml", CONTENT_TYPES.encode("utf-8")),
        ("_rels/.rels", cover.PACKAGE_RELS.encode("utf-8")),
        ("word/_rels/document.xml.rels", DOCUMENT_RELS.encode("utf-8")),
        ("word/document.xml", document().encode("utf-8")),
        ("word/styles.xml", cover.STYLES.encode("utf-8")),
        ("word/media/picture.jpeg", cover.JPEG),
    ]
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    print(OUT)


if __name__ == "__main__":
    main()
