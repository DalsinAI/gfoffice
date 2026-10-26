#!/usr/bin/env python3
"""Makes tests/core/fixtures/table-styles.docx: make_tables_docx.py [OUT]

Table styles as Word writes them (w:style w:type="table", with pPr, rPr,
tcPr and w:tblStylePr), made by hand (no Word document goes into the
repository); it borrows make_cover_docx.py's parts. The document's
defaults space paragraphs 8 points after, at 1.08 lines, as Word 2013's do.
- a paragraph outside any table;
- a "Table Grid" table (no space after, single lines, as Word's own);
- a "Plan Table" table, based on Table Grid: centred, its header row bold,
  white on dark blue, its body rows banded light blue and white, its last
  row italic; one cell with its own spacing (6 points after) and one with a
  paragraph style that sets 4 points after;
- a table with no style, which takes the default table style (none of the
  above).
OpenWrite 2.0.18 and earlier left table styles out: the cells' paragraphs
were spaced as the body's, and the header row plain. test_layout.c (make
layout-test) reads it.
Run it again after changing it, and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "table-styles.docx")
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

# Word 2013's defaults and its Normal, Table Normal and Table Grid; Plan Table is ours, in the way Word's
# built-in "Grid Table 4" styles are written.
STYLES = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Trebuchet MS" w:hAnsi="Trebuchet MS"/><w:sz w:val="22"/></w:rPr></w:rPrDefault>
<w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>
<w:style w:type="paragraph" w:customStyle="1" w:styleId="CellNote"><w:name w:val="Cell Note"/><w:basedOn w:val="Normal"/>
<w:pPr><w:spacing w:after="80"/></w:pPr></w:style>
<w:style w:type="table" w:default="1" w:styleId="TableNormal"><w:name w:val="Normal Table"/><w:uiPriority w:val="99"/>
<w:semiHidden/><w:unhideWhenUsed/><w:tblPr><w:tblInd w:w="0" w:type="dxa"/><w:tblCellMar><w:top w:w="0" w:type="dxa"/>
<w:left w:w="108" w:type="dxa"/><w:bottom w:w="0" w:type="dxa"/><w:right w:w="108" w:type="dxa"/></w:tblCellMar></w:tblPr></w:style>
<w:style w:type="table" w:styleId="TableGrid"><w:name w:val="Table Grid"/><w:basedOn w:val="TableNormal"/><w:uiPriority w:val="39"/>
<w:pPr><w:spacing w:after="0" w:line="240" w:lineRule="auto"/></w:pPr><w:tblPr><w:tblBorders>
<w:top w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:left w:val="single" w:sz="4" w:space="0" w:color="auto"/>
<w:bottom w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:right w:val="single" w:sz="4" w:space="0" w:color="auto"/>
<w:insideH w:val="single" w:sz="4" w:space="0" w:color="auto"/><w:insideV w:val="single" w:sz="4" w:space="0" w:color="auto"/>
</w:tblBorders></w:tblPr></w:style>
<w:style w:type="table" w:customStyle="1" w:styleId="PlanTable"><w:name w:val="Plan Table"/><w:basedOn w:val="TableGrid"/>
<w:pPr><w:jc w:val="center"/></w:pPr><w:rPr><w:sz w:val="20"/></w:rPr><w:tblPr><w:tblStyleRowBandSize w:val="1"/>
<w:tblStyleColBandSize w:val="1"/></w:tblPr>
<w:tblStylePr w:type="firstRow"><w:rPr><w:b/><w:bCs/><w:color w:val="FFFFFF"/></w:rPr><w:tcPr>
<w:shd w:val="clear" w:color="auto" w:fill="1F3864"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="lastRow"><w:rPr><w:i/></w:rPr></w:tblStylePr>
<w:tblStylePr w:type="band1Horz"><w:tcPr><w:shd w:val="clear" w:color="auto" w:fill="D9E2F3"/></w:tcPr></w:tblStylePr>
<w:tblStylePr w:type="band1Vert"><w:tcPr><w:shd w:val="clear" w:color="auto" w:fill="FF0000"/></w:tcPr></w:tblStylePr>
</w:style>
</w:styles>"""


def cell(text, ppr="", width=3000):
    return ('<w:tc><w:tcPr><w:tcW w:w="%d" w:type="dxa"/></w:tcPr><w:p>%s<w:r><w:t xml:space="preserve">%s</w:t>'
            '</w:r></w:p></w:tc>' % (width, ("<w:pPr>%s</w:pPr>" % ppr) if ppr else "", text))


def row(*cells):
    return "<w:tr>%s</w:tr>" % "".join(cells)


def table(style, look, rows):
    tblpr = "<w:tblPr>%s<w:tblW w:w=\"0\" w:type=\"auto\"/>%s</w:tblPr>" % (
        ('<w:tblStyle w:val="%s"/>' % style) if style else "", look)
    grid = "<w:tblGrid>%s</w:tblGrid>" % ('<w:gridCol w:w="3000"/>' * 2)
    return "<w:tbl>%s%s%s</w:tbl>" % (tblpr, grid, "".join(rows))


def para(text):
    return '<w:p><w:r><w:t xml:space="preserve">%s</w:t></w:r></w:p>' % text


def document():
    # Word 2010 on writes both the attributes and Word 2007's w:val; banded rows on, banded columns off
    look = ('<w:tblLook w:val="04E0" w:firstRow="1" w:lastRow="1" w:firstColumn="0" w:lastColumn="0" '
            'w:noHBand="0" w:noVBand="1"/>')
    body = [
        para("A paragraph outside the tables."),
        table("TableGrid", '<w:tblLook w:val="04A0"/>', [row(cell("Grid A1"), cell("Grid B1")),
                                                         row(cell("Grid A2"), cell("Grid B2"))]),
        para("Between the tables."),
        table("PlanTable", look, [
            row(cell("Header A"), cell("Header B")),
            row(cell("Body A1"), cell("Own spacing", '<w:spacing w:after="120"/>')),
            row(cell("Body A2"), cell("Cell note", '<w:pStyle w:val="CellNote"/>')),
            row(cell("Body A3"), cell("Body B3")),
            row(cell("Total A"), cell("Total B")),
        ]),
        para("Between the tables again."),
        table(None, "", [row(cell("Plain A1"), cell("Plain B1"))]),
        para("After the tables."),
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
        ("word/styles.xml", STYLES.encode("utf-8")),
    ]
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        for name, data in parts:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, data)
    print(OUT)


if __name__ == "__main__":
    main()
