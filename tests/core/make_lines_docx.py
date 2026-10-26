#!/usr/bin/env python3
"""Makes tests/core/fixtures/line-rules.docx: make_lines_docx.py [OUT]

Line spacing as Word writes it (w:spacing w:line, w:lineRule), made by hand
(no Word document goes into the repository); it borrows make_cover_docx.py's
parts. On A4 with inch margins, in Trebuchet MS:
- a paragraph of 11 point text at 1.5 lines (lineRule auto, 360);
- a paragraph of 16 point text exactly 12 points a line (exact, 240): its
  lines closer than its text is tall;
- a paragraph of 11 point text at least 30 points a line (atLeast, 600);
- a paragraph of 11 point text at least 6 points a line, which its text is
  taller than.
OpenWrite 2.0.17 and earlier read exact and atLeast as single spacing.
test_layout.c (make layout-test) reads it: the lines that far apart, and
the spacing written back to DOCX and ODT and read again.
Run it again after changing it, and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "line-rules.docx")
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

WORDS = "Lines of a paragraph, as far apart as its line spacing says, whatever the size of its text. "


def para(line, rule, size, text):
    return ('<w:p><w:pPr><w:spacing w:after="0" w:line="%d" w:lineRule="%s"/></w:pPr><w:r><w:rPr><w:sz w:val="%d"/>'
            '</w:rPr><w:t xml:space="preserve">%s</w:t></w:r></w:p>' % (line, rule, size, text))


def document():
    body = [
        para(360, "auto", 22, "One and a half. " + WORDS * 3),
        para(240, "exact", 32, "Exactly twelve points. " + WORDS * 3),
        para(600, "atLeast", 22, "At least thirty points. " + WORDS * 3),
        para(120, "atLeast", 22, "At least six points. " + WORDS * 3),
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
