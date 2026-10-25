#!/usr/bin/env python3
"""Makes tests/core/fixtures/wrap.docx: make_wrap_docx.py [OUT]

Text wrapped round frames, as Word writes them, made by hand (no Word
document goes into the repository); it borrows make_cover_docx.py's parts.
On A4 with inch margins, in Trebuchet MS 11 point:
- a picture at the left margin, from its paragraph, wrapSquare: the
  paragraph's first lines start to its right;
- a picture centred between the margins, wrapSquare bothSides, in a
  justified paragraph: its lines on both sides of it;
- a filled text box from its paragraph, wrapTopAndBottom: no text beside it;
- a picture at the right margin, wrapTight: lines end before it;
- filler paragraphs, and on a later page a picture placed from the page,
  wrapSquare, that the lines of whatever paragraph reaches it go round.
test_layout.c (make layout-test) reads it: no body text under a frame.
Run it again after changing it, and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "wrap.docx")
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

WORDS = ("Words wrapped round a frame keep their distance from it on the side that has room, and the lines "
         "under it go back to the margins. ")


def drawing(ident, horizontal, vertical, cx, cy, wrap):
    return '<w:r><w:drawing>%s</w:drawing></w:r>' % cover.anchor(
        ident, "Picture %d" % ident, horizontal, vertical, cx, cy, cover.picture(ident, "rId11", cx, cy), wrap=wrap)


def para(text, runs="", jc=None, style=None):
    ppr = ""
    if jc or style:
        ppr = "<w:pPr>%s%s</w:pPr>" % ('<w:pStyle w:val="%s"/>' % style if style else "",
                                       '<w:jc w:val="%s"/>' % jc if jc else "")
    return '<w:p>%s%s<w:r><w:t xml:space="preserve">%s</w:t></w:r></w:p>' % (ppr, runs, text)


def document():
    square = '<wp:wrapSquare wrapText="bothSides"/>'
    tight = ('<wp:wrapTight wrapText="bothSides"><wp:wrapPolygon edited="0"><wp:start x="0" y="0"/>'
             '<wp:lineTo x="0" y="21600"/><wp:lineTo x="21600" y="21600"/><wp:lineTo x="21600" y="0"/>'
             '<wp:lineTo x="0" y="0"/></wp:wrapPolygon></wp:wrapTight>')
    left = drawing(11, cover.position("H", "margin", 0), cover.position("V", "paragraph", 0), 2160, 2160, square)
    middle = drawing(12, cover.position("H", "margin", align="center"), cover.position("V", "paragraph", 720),
                     1440, 1440, square)
    box = cover.text_box(13, cover.position("H", "margin", 0), cover.position("V", "paragraph", 600), 5760, 1080,
                         cover.box_para("A box the text goes above and below", 24, "FFFFFF"), fill="1F3864")
    box = box.replace("<wp:wrapNone/>", "<wp:wrapTopAndBottom/>")
    right = drawing(14, cover.position("H", "margin", align="right"), cover.position("V", "paragraph", 0),
                    1800, 1440, tight)
    later = drawing(15, cover.position("H", "margin", 2880), cover.position("V", "page", 2880), 2880, 2880, square)
    body = [
        para("Wrapping", style="Heading1"),
        para(WORDS * 5, left),
        para(WORDS * 6, middle, jc="both"),
        para(WORDS * 4, box),
        para(WORDS * 4, right),
    ]
    body += [para("Filler %d. " % i + WORDS * 2) for i in range(14)]
    body.append(para("The page's own picture. " + WORDS * 2, later))
    body += [para("More filler %d. " % i + WORDS * 2) for i in range(12)]
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
