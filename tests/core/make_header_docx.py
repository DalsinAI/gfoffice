#!/usr/bin/env python3
"""Makes tests/core/fixtures/header-frames.docx: make_header_docx.py [OUT]

Frames in a header and a footer, as Word writes them, made by hand (no Word
document goes into the repository); it borrows make_cover_docx.py's parts.
On A4 with inch margins:
- the header: a line of text, a picture 0.5 inch square placed 0.25 inch
  from the page's top and right edges (wp:anchor from the page, in front),
  and a filled text box "DRAFT" from the margin, 0.5 inch down from the
  page's top;
- the footer: a line of text and a text box at the right margin, from its
  paragraph, holding "Footer box";
- three pages of body text.
test_layout.c (make layout-test) reads it: every page's header and footer
have their frames where Word puts them.
Run it again after changing it, and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "header-frames.docx")
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
<Override PartName="/word/header1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"/>
<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>
</Types>"""

DOCUMENT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/header" Target="header1.xml"/>
<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/>
</Relationships>"""

# the header's own relationships: its rId1 is the picture
HEADER_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="%s" Target="media/logo.jpeg"/>
</Relationships>""" % cover.IMAGE

WORDS = "The body's text, three pages of it, under a header and over a footer that both hold frames. "


def header():
    logo = '<w:r><w:drawing>%s</w:drawing></w:r>' % cover.anchor(
        21, "Logo", cover.position("H", "page", 11906 - 360 - 720), cover.position("V", "page", 360), 720, 720,
        cover.picture(21, "rId1", 720, 720))
    draft = cover.text_box(22, cover.position("H", "margin", 0), cover.position("V", "page", 720), 2880, 432,
                           cover.box_para("DRAFT", 20, "FFFFFF", bold=True), fill="C00000")
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:hdr %s><w:p>%s%s<w:r><w:t xml:space='
            '"preserve">The header</w:t></w:r></w:p></w:hdr>' % (cover.NS, logo, draft))


def footer():
    box = cover.text_box(23, cover.position("H", "margin", align="right"), cover.position("V", "paragraph", 0),
                         2160, 360, cover.box_para("Footer box", 18, "000000"), fill="DDDDDD", line="000000")
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:ftr %s><w:p>%s<w:r><w:t xml:space='
            '"preserve">The footer</w:t></w:r></w:p></w:ftr>' % (cover.NS, box))


def document():
    body = ['<w:p><w:r><w:t xml:space="preserve">Paragraph %d. %s</w:t></w:r></w:p>' % (i, WORDS * 4)
            for i in range(30)]
    sect = ('<w:sectPr><w:headerReference w:type="default" r:id="rId2"/><w:footerReference w:type="default" '
            'r:id="rId3"/><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" '
            'w:left="1440" w:header="708" w:footer="708" w:gutter="0"/></w:sectPr>')
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<w:document %s><w:body>%s%s</w:body>'
            '</w:document>' % (cover.NS, "".join(body), sect))


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    parts = [
        ("[Content_Types].xml", CONTENT_TYPES.encode("utf-8")),
        ("_rels/.rels", cover.PACKAGE_RELS.encode("utf-8")),
        ("word/_rels/document.xml.rels", DOCUMENT_RELS.encode("utf-8")),
        ("word/_rels/header1.xml.rels", HEADER_RELS.encode("utf-8")),
        ("word/document.xml", document().encode("utf-8")),
        ("word/styles.xml", cover.STYLES.encode("utf-8")),
        ("word/header1.xml", header().encode("utf-8")),
        ("word/footer1.xml", footer().encode("utf-8")),
        ("word/media/logo.jpeg", cover.JPEG),
    ]
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    print(OUT)


if __name__ == "__main__":
    main()
