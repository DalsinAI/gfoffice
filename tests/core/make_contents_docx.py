#!/usr/bin/env python3
"""Makes tests/core/fixtures/contents.docx: make_contents_docx.py [OUT]

A Word contents page as Word writes one, made by hand (no Word document goes
into the repository): TOC1 and TOC2 styles whose right tab stop at the right
margin has a dot leader, entries in a TOC field with hyperlinks and PAGEREF
fields, an entry whose own stops add a left stop to the style's, one that
clears the style's stop for its own hyphen-led one, and a paragraph with
centred, decimal and underscore-led stops. Then (2.0.12) the headings the
entries link to, on a page of their own, each with its bookmark (Word's
_GoBack beside one, and a second bookmark), the last in a table's cell.
test_layout.c (make layout-test) reads it. Run it again after changing it,
and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "contents.docx")
W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>"""

PACKAGE_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
</Relationships>"""

DOCUMENT_RELS = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
</Relationships>"""

STYLES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="%s">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>
<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
<w:style w:type="paragraph" w:styleId="TOCHeading"><w:name w:val="TOC Heading"/><w:basedOn w:val="Normal"/>
<w:rPr><w:b/><w:sz w:val="32"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="TOC1"><w:name w:val="toc 1"/><w:basedOn w:val="Normal"/>
<w:pPr><w:tabs><w:tab w:val="right" w:leader="dot" w:pos="9016"/></w:tabs><w:spacing w:after="100"/></w:pPr></w:style>
<w:style w:type="paragraph" w:styleId="TOC2"><w:name w:val="toc 2"/><w:basedOn w:val="TOC1"/>
<w:pPr><w:ind w:left="220"/></w:pPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/>
<w:pPr><w:keepNext/><w:spacing w:before="240" w:after="0"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/>
<w:pPr><w:keepNext/><w:spacing w:before="40" w:after="0"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:sz w:val="26"/></w:rPr></w:style>
<w:style w:type="character" w:styleId="Hyperlink"><w:name w:val="Hyperlink"/><w:rPr><w:color w:val="0563C1"/><w:u w:val="single"/></w:rPr></w:style>
</w:styles>""" % W


def run(text):
    return '<w:r><w:t xml:space="preserve">%s</w:t></w:r>' % text


def entry(style, text, page, anchor, tabs="", begin=False, end=False):
    """One contents line: the entry's text, a tab and its page number, a hyperlink as Word makes them."""
    out = '<w:p><w:pPr><w:pStyle w:val="%s"/>%s</w:pPr>' % (style, tabs)
    if begin:
        out += ('<w:r><w:fldChar w:fldCharType="begin"/></w:r>'
                '<w:r><w:instrText xml:space="preserve"> TOC \\o "1-3" \\h \\z \\u </w:instrText></w:r>'
                '<w:r><w:fldChar w:fldCharType="separate"/></w:r>')
    out += '<w:hyperlink w:anchor="%s" w:history="1">' % anchor
    out += '<w:r><w:rPr><w:rStyle w:val="Hyperlink"/></w:rPr><w:t>%s</w:t></w:r>' % text
    out += '<w:r><w:tab/></w:r>'
    out += ('<w:r><w:fldChar w:fldCharType="begin"/></w:r>'
            '<w:r><w:instrText xml:space="preserve"> PAGEREF %s \\h </w:instrText></w:r>'
            '<w:r><w:fldChar w:fldCharType="separate"/></w:r>'
            '<w:r><w:t>%s</w:t></w:r>'
            '<w:r><w:fldChar w:fldCharType="end"/></w:r>') % (anchor, page)
    out += '</w:hyperlink>'
    if end:
        out += '<w:r><w:fldChar w:fldCharType="end"/></w:r>'
    return out + '</w:p>'


def document():
    body = '<w:p><w:pPr><w:pStyle w:val="TOCHeading"/></w:pPr>%s</w:p>' % run("Contents")
    body += entry("TOC1", "01 Executive summary", "3", "_Toc1", begin=True)
    body += entry("TOC2", "1.1 Purpose of this plan", "4", "_Toc2")
    # a left stop of its own as well as the style's right one
    body += entry("TOC1", "02 Scope", "12", "_Toc3", tabs='<w:tabs><w:tab w:val="left" w:pos="440"/></w:tabs>')
    # the style's stop cleared, a hyphen-led one of its own
    body += entry("TOC1", "03 Roles", "17", "_Toc4",
                  tabs='<w:tabs><w:tab w:val="clear" w:pos="9016"/>'
                       '<w:tab w:val="right" w:leader="hyphen" w:pos="8000"/></w:tabs>', end=True)
    # centred, decimal and an underscore leader
    body += ('<w:p><w:pPr><w:tabs><w:tab w:val="center" w:pos="4513"/>'
             '<w:tab w:val="decimal" w:leader="underscore" w:pos="8000"/></w:tabs></w:pPr>'
             '%s<w:r><w:tab/></w:r>%s<w:r><w:tab/></w:r>%s</w:p>') % (run("Item"), run("Centred"), run("1234.50"))
    # the headings the entries go to, each with its bookmark
    def heading(level, text, names, brk=False):
        marks = "".join('<w:bookmarkStart w:id="%d" w:name="%s"/>' % (i + 10 * level, n) for i, n in enumerate(names))
        ends = "".join('<w:bookmarkEnd w:id="%d"/>' % (i + 10 * level) for i in range(len(names)))
        return ('<w:p><w:pPr><w:pStyle w:val="Heading%d"/>%s</w:pPr>%s%s%s</w:p>'
                % (level, "<w:pageBreakBefore/>" if brk else "", marks, run(text), ends))
    body += heading(1, "01 Executive summary", ["_Toc1"], brk=True)
    body += heading(2, "1.1 Purpose of this plan", ["_Toc2"])
    body += heading(1, "02 Scope", ["_GoBack", "_Toc3", "_Ref9"])
    body += ('<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/></w:tblPr><w:tblGrid><w:gridCol w:w="9016"/></w:tblGrid>'
             '<w:tr><w:tc><w:tcPr><w:tcW w:w="9016" w:type="dxa"/></w:tcPr><w:p><w:bookmarkStart w:id="40" w:name="_Toc4"/>'
             '%s<w:bookmarkEnd w:id="40"/></w:p></w:tc></w:tr></w:tbl>' % run("03 Roles"))
    body += ('<w:sectPr><w:pgSz w:w="11906" w:h="16838"/>'
             '<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="708" w:footer="708" w:gutter="0"/>'
             '</w:sectPr>')
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<w:document xmlns:w="%s" xmlns:r="%s"><w:body>%s</w:body></w:document>') % (W, R, body)


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        for name, text in (("[Content_Types].xml", CONTENT_TYPES), ("_rels/.rels", PACKAGE_RELS),
                           ("word/_rels/document.xml.rels", DOCUMENT_RELS), ("word/styles.xml", STYLES),
                           ("word/document.xml", document())):
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, text.encode("utf-8"))
    print(OUT)


if __name__ == "__main__":
    main()
