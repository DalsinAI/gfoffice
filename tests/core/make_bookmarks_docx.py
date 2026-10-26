#!/usr/bin/env python3
"""Makes tests/core/fixtures/bookmarks.docx: make_bookmarks_docx.py [OUT]

Bookmarks where Word puts them, in the middle of the text, made by hand (no
Word document goes into the repository); it borrows make_cover_docx.py's
parts.
- a paragraph with "_Ref1" round a bold word between two plain runs, and an
  empty bookmark "Point" inside a run, between "para" and "graph";
- a heading with "_Toc5" round its text;
- a paragraph with a link to _Ref1 that has "Inner" inside it, round "link";
- a paragraph where "Span" begins after its first word and ends in the next
  paragraph;
- "Between", begun between two paragraphs, at the next one's start;
- "Accent" after a two-byte character ("Cafe" with an e acute, then "creme");
- a table whose second cell has two paragraphs and "Cell" round the second
  one's second word.
OpenWrite 2.0.20 and earlier kept every bookmark at its paragraph's start.
test_layout.c (make layout-test) and run_tests.py read it.
Run it again after changing it, and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "bookmarks.docx")
sys.argv = sys.argv[:1]                    # make_cover_docx reads its own arguments on import
sys.path.insert(0, HERE)
import make_cover_docx as cover            # noqa: E402

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>"""

DOCUMENT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>"""


def run(text, bold=False):
    return '<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r>' % ("<w:rPr><w:b/></w:rPr>" if bold else "", text)


def start(ident, name):
    return '<w:bookmarkStart w:id="%d" w:name="%s"/>' % (ident, name)


def end(ident):
    return '<w:bookmarkEnd w:id="%d"/>' % ident


def document():
    body = [
        '<w:p>%s%s%s%s%s%s%s%s</w:p>' % (run("The plan's "), start(1, "_Ref1"), run("first", True), end(1),
                                         run(" para"), start(2, "Point"), end(2), run("graph.")),
        '<w:p><w:pPr><w:pStyle w:val="Heading1"/></w:pPr>%s%s%s</w:p>' % (start(3, "_Toc5"), run("2 Goals"), end(3)),
        '<w:p>%s<w:hyperlink w:anchor="_Ref1">%s%s%s</w:hyperlink>%s</w:p>' % (
            run("See the "), start(4, "Inner"), run("link"), end(4), run(" above.")),
        '<w:p>%s%s%s</w:p>' % (run("Span "), start(5, "Span"), run("begins here")),
        '<w:p>%s%s</w:p>' % (run("and ends"), end(5)),
        start(6, "Between"),
        '<w:p>%s</w:p>' % run("After the mark between paragraphs."),
        '<w:p>%s%s%s</w:p>' % (run("Café"), start(7, "Accent"), run(" crème")),
        '<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/></w:tblPr><w:tblGrid><w:gridCol w:w="4500"/>'
        '<w:gridCol w:w="4500"/></w:tblGrid><w:tr><w:tc><w:p>%s</w:p></w:tc><w:tc><w:p>%s</w:p><w:p>%s%s%s%s</w:p>'
        '</w:tc></w:tr></w:tbl>' % (run("Cell one"), run("Cell two"), run("second "), start(8, "Cell"), run("line"),
                                    end(8)),
        '<w:p>%s</w:p>' % run("The end."),
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
    ]
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    print(OUT)


if __name__ == "__main__":
    main()
