#!/usr/bin/env python3
"""Makes tests/core/fixtures/frames.odt: make_frames_odt.py [OUT]

Frames as LibreOffice Writer writes them (draw:frame, with graphic styles
that take most of their properties from the common "Graphics" and "Frame"
styles), made by hand (no document from elsewhere goes into the
repository); it borrows make_cover_docx.py's JPEG. On A4:
- a picture anchored to the page (between the paragraphs, as Writer puts
  one), 1 inch from the page's left and top edges, under the text;
- a paragraph holding a text box anchored to its character, centred in the
  paragraph's width, its height its text box's fo:min-height; it takes its
  wrap (parallel), border and padding from "Frame", and its own fill;
- a paragraph with a picture anchored to it at the margin's right, wrapped
  round its outline (style:wrap-contour), whose frame holds an SVG first and
  the JPEG after it, as Writer keeps a picture with a fallback;
- a paragraph with a picture as a character (in the line), which stays in
  the line;
- a header with a text box anchored to its paragraph, 2 inches from the
  page's left edge.
OpenWrite 2.0.19 and earlier left text boxes out and put every picture in
its line. test_layout.c (make layout-test) reads it.
Run it again after changing it, and commit the file.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fixtures", "frames.odt")
sys.argv = sys.argv[:1]                    # make_cover_docx reads its own arguments on import
sys.path.insert(0, HERE)
import make_cover_docx as cover            # noqa: E402

NS = ('xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" '
      'xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" '
      'xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" '
      'xmlns:draw="urn:oasis:names:tc:opendocument:xmlns:drawing:1.0" '
      'xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" '
      'xmlns:xlink="http://www.w3.org/1999/xlink" '
      'xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0" office:version="1.3"')

MANIFEST = """<?xml version="1.0" encoding="UTF-8"?>
<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0" manifest:version="1.3">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.text"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="styles.xml" manifest:media-type="text/xml"/>
<manifest:file-entry manifest:full-path="Pictures/side.jpg" manifest:media-type="image/jpeg"/>
<manifest:file-entry manifest:full-path="Pictures/side.svg" manifest:media-type="image/svg+xml"/>
</manifest:manifest>"""

SVG = ('<svg xmlns="http://www.w3.org/2000/svg" width="20" height="28"><rect width="20" height="28" '
       'fill="#1f3864"/></svg>')

STYLES = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-styles %s>
<office:styles>
<style:default-style style:family="graphic"><style:graphic-properties svg:stroke-color="#3465a4" draw:fill-color="#729fcf"/></style:default-style>
<style:style style:name="Standard" style:family="paragraph" style:class="text"/>
<style:style style:name="Graphics" style:family="graphic"><style:graphic-properties text:anchor-type="paragraph" svg:x="0in" svg:y="0in" style:wrap="dynamic" style:number-wrapped-paragraphs="no-limit" style:wrap-contour="false" style:vertical-pos="top" style:vertical-rel="paragraph" style:horizontal-pos="center" style:horizontal-rel="paragraph"/></style:style>
<style:style style:name="Frame" style:family="graphic"><style:graphic-properties text:anchor-type="paragraph" svg:x="0in" svg:y="0in" fo:margin-left="0.0791in" fo:margin-right="0.0791in" fo:margin-top="0.0791in" fo:margin-bottom="0.0791in" style:wrap="parallel" style:number-wrapped-paragraphs="no-limit" style:wrap-contour="false" style:vertical-pos="top" style:vertical-rel="paragraph-content" style:horizontal-pos="center" style:horizontal-rel="paragraph-content" fo:padding="0.0591in" fo:border="0.06pt solid #000000"/></style:style>
</office:styles>
<office:automatic-styles>
<style:style style:name="Mfr1" style:family="graphic" style:parent-style-name="Frame"><style:graphic-properties style:vertical-pos="from-top" style:vertical-rel="paragraph" style:horizontal-pos="from-left" style:horizontal-rel="page" draw:fill="solid" draw:fill-color="#c00000" fo:background-color="#c00000"/></style:style>
<style:page-layout style:name="pm1"><style:page-layout-properties fo:page-width="8.2681in" fo:page-height="11.6929in" fo:margin-top="1in" fo:margin-bottom="1in" fo:margin-left="1in" fo:margin-right="1in"/></style:page-layout>
</office:automatic-styles>
<office:master-styles><style:master-page style:name="Standard" style:page-layout-name="pm1"><style:header>
<text:p text:style-name="Standard"><draw:frame draw:style-name="Mfr1" draw:name="Header box" text:anchor-type="paragraph" svg:x="2in" svg:y="0in" svg:width="2in" draw:z-index="2"><draw:text-box fo:min-height="0.3in"><text:p text:style-name="Standard">Header box</text:p></draw:text-box></draw:frame>The header</text:p>
</style:header></style:master-page></office:master-styles>
</office:document-styles>""" % NS

CONTENT = """<?xml version="1.0" encoding="UTF-8"?>
<office:document-content %s>
<office:automatic-styles>
<style:style style:name="fr1" style:family="graphic" style:parent-style-name="Graphics"><style:graphic-properties style:wrap="run-through" style:run-through="background" style:vertical-pos="from-top" style:vertical-rel="page" style:horizontal-pos="from-left" style:horizontal-rel="page"/></style:style>
<style:style style:name="fr2" style:family="graphic" style:parent-style-name="Frame"><style:graphic-properties draw:fill="solid" draw:fill-color="#fff2cc" fo:background-color="#fff2cc"/></style:style>
<style:style style:name="fr3" style:family="graphic" style:parent-style-name="Graphics"><style:graphic-properties style:wrap="parallel" style:wrap-contour="true" style:horizontal-pos="right" style:horizontal-rel="page-content"/></style:style>
<style:style style:name="fr4" style:family="graphic" style:parent-style-name="Graphics"><style:graphic-properties style:vertical-pos="top" style:vertical-rel="baseline"/></style:style>
</office:automatic-styles>
<office:body><office:text>
<draw:frame draw:style-name="fr1" draw:name="Behind" text:anchor-type="page" text:anchor-page-number="1" svg:x="1in" svg:y="1in" svg:width="2in" svg:height="2.8in" draw:z-index="0"><draw:image xlink:href="Pictures/side.jpg" xlink:type="simple" xlink:show="embed" xlink:actuate="onLoad"/></draw:frame>
<text:p text:style-name="Standard">A paragraph with a text box <draw:frame draw:style-name="fr2" draw:name="Box" text:anchor-type="char" svg:width="3in" draw:z-index="1"><draw:text-box fo:min-height="0.75in"><text:p text:style-name="Standard">Text in the box</text:p><text:p text:style-name="Standard">and its second line</text:p></draw:text-box></draw:frame>anchored to its character, and more text after it.</text:p>
<text:p text:style-name="Standard"><draw:frame draw:style-name="fr3" draw:name="Side" text:anchor-type="paragraph" svg:y="0.1in" svg:width="1in" svg:height="1.4in" draw:z-index="3"><draw:image xlink:href="Pictures/side.svg" xlink:type="simple" xlink:show="embed" xlink:actuate="onLoad"/><draw:image xlink:href="Pictures/side.jpg" xlink:type="simple" xlink:show="embed" xlink:actuate="onLoad"/></draw:frame>A paragraph with a picture at the margin's right, the text wrapped round it.</text:p>
<text:p text:style-name="Standard">A picture in the line: <draw:frame draw:style-name="fr4" draw:name="Small" text:anchor-type="as-char" svg:width="0.3in" svg:height="0.4in" draw:z-index="4"><draw:image xlink:href="Pictures/side.jpg" xlink:type="simple" xlink:show="embed" xlink:actuate="onLoad"/></draw:frame> and after it.</text:p>
</office:text></office:body>
</office:document-content>""" % NS


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    parts = [
        ("mimetype", b"application/vnd.oasis.opendocument.text", zipfile.ZIP_STORED),
        ("META-INF/manifest.xml", MANIFEST.encode("utf-8"), zipfile.ZIP_DEFLATED),
        ("styles.xml", STYLES.encode("utf-8"), zipfile.ZIP_DEFLATED),
        ("content.xml", CONTENT.encode("utf-8"), zipfile.ZIP_DEFLATED),
        ("Pictures/side.jpg", cover.JPEG, zipfile.ZIP_STORED),
        ("Pictures/side.svg", SVG.encode("utf-8"), zipfile.ZIP_DEFLATED),
    ]
    with zipfile.ZipFile(OUT, "w") as z:
        for name, data, method in parts:
            info = zipfile.ZipInfo(name, date_time=(2026, 10, 25, 12, 0, 0))
            info.compress_type = method
            z.writestr(info, data)
    print(OUT)


if __name__ == "__main__":
    main()
