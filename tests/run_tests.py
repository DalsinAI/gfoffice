#!/usr/bin/env python3
"""OpenWrite's filter tests: run_tests.py path/to/owconvert

Builds sample documents from the formats' specifications (IFF FTXT, ProWrite's
IFF WORD, ANSI and plain text), converts each to text, HTML, ODT and FTXT,
and checks the results. Then feeds every cut-short copy of each sample to the
converter, which must answer with an error or a document, never crash.

Real files from the original programs replace and add to these samples as the
format lab makes them (docs/FORMATS.md, section 4).
MIT, Copyright (c) 2026 Dalsin Limited.
"""
import io
import os
import struct
import subprocess
import sys
import tempfile
import xml.dom.minidom
import zipfile

OWCONVERT = os.path.abspath(sys.argv[1])
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name} {detail}")


def chunk(cid, data):
    out = cid.encode("ascii") + struct.pack(">I", len(data)) + data
    return out + (b"\0" if len(data) & 1 else b"")


def form(ftype, chunks):
    body = ftype.encode("ascii") + b"".join(chunks)
    return b"FORM" + struct.pack(">I", len(body)) + body


def ftxt_sample():
    fons_times = bytes([1, 0, 2, 2]) + b"times\0"
    fons_courier = bytes([2, 0, 1, 1]) + b"courier\0"
    chrs = (b"Hello \x9b1mbold\x9b22m and \x9b3mitalic\x9b23m, \x1b[4munderlined\x1b[24m.\n"
            b"Caf\xe9 in \x9b11mTimes\x9b10m and \x9b12mCourier\x9b10m.\n"
            b"Before\x0cAfter\tTab\n"
            b"\x9b31mRed\x9b39m plain\n")
    return form("FTXT", [chunk("FONS", fons_times), chunk("FONS", fons_courier), chunk("CHRS", chrs)])


def para(left_indent=0, left_margin=0, right_margin=0, spacing=0, justify=0, font=0, style=0, misc=0, colour=0):
    return struct.pack(">HHHBBBBBBI", left_indent, left_margin, right_margin, spacing, justify, font, style, misc, colour, 0)


def fscc(*changes):
    return b"".join(struct.pack(">HBBBBH", loc, font, style, misc, colour, 0) for loc, font, style, misc, colour in changes)


def prowrite_sample():
    chunks = [
        chunk("FONT", struct.pack(">BxH", 0, 12) + b"times\0"),
        chunk("FONT", struct.pack(">BxH", 1, 18) + b"helvetica\0"),
        chunk("COLR", bytes(range(8))),
        chunk("DOC ", struct.pack(">HBBI", 1, 0, 0, 0)),
        chunk("PARA", para(justify=1, font=1, style=2)),
        chunk("TEXT", b"Title"),
        chunk("PARA", para(left_indent=360, spacing=0x10)),
        chunk("TABS", struct.pack(">HBB", 1440, 2, 0)),
        # "Body bold red text, page " then the page number: bold from 5,
        # red (not bold) from 9, plain from 13.
        chunk("TEXT", b"Body bold red text, page \x80"),
        chunk("FSCC", fscc((5, 0, 2, 0, 0), (9, 0, 0, 0, 1), (13, 0, 0, 0, 0))),
        chunk("TEXT", b"a\tb"),
        chunk("PAGE", b""),
        chunk("TEXT", b"Next page"),
        chunk("HEAD", struct.pack(">BBI", 3, 0, 0)),
        chunk("PARA", para()),
        chunk("TEXT", b"Header \x80"),
        chunk("FOOT", struct.pack(">BBI", 3, 1, 0)),
        chunk("TEXT", b"Footer \x81"),
        chunk("PCTS", bytes([3, 0])),
        chunk("PINF", struct.pack(">HHHHHBBBB", 16, 8, 1, 100, 100, 0, 1, 0, 0)),
        chunk("BODY", bytes(48)),
    ]
    return form("WORD", chunks)


def wordworth_sample():
    """A Wordworth document built from docs/formats/wordworth.md."""
    def wpar(align=0, font=1, style=0):
        b = bytearray(36)
        b[18], b[21], b[22] = align, font, style
        return bytes(b)
    wdoc = bytearray(54)
    wdoc[4:8] = struct.pack(">I", 595440)
    wdoc[8:12] = struct.pack(">I", 843336)
    body = b"Body with bold and plain.\x0fSecond para\tafter tab\x0f"
    return form("WOWO", [
        chunk("WVRN", struct.pack(">II", 5, 0)),
        chunk("WFNT", bytes([1, 0xFA]) + struct.pack(">H", 12) + b"IF_CG Times\0"),
        chunk("WFNT", bytes([0xFF, 0xFA]) + struct.pack(">H", 14) + b"IF_Shannon Book\0"),
        chunk("WDOC", bytes(wdoc)),
        chunk("WPAR", wpar(align=1, style=2)), chunk("WTAB", b""), chunk("WTXT", b"Title\x0f"),
        chunk("WPAR", wpar()), chunk("WTAB", b""), chunk("WTXT", body),
        chunk("WFSC", struct.pack(">IBBBBBBBB", 10, 1, 2, 0, 0, 0, 0, 0, 0x90) + struct.pack(">IBBBBBBBB", 14, 1, 0, 0, 0, 0, 0, 0, 0x90)),
        chunk("WSPC", bytes(12)),
        chunk("WPAG", b""), chunk("WTXT", b"Page two\x0f"),
        chunk("WHED", bytes([3, 0, 0, 0, 0, 0])), chunk("WPAR", wpar()), chunk("WTXT", b"Head\x0f"),
        chunk("WFOT", bytes([3, 1, 0, 0, 0, 0])), chunk("WPAR", wpar()), chunk("WTXT", b"Foot\x0f"),
        form("ILBM", [chunk("BMHD", bytes(20))]),
    ])


def finalwriter_sample():
    """A Final Writer document built from docs/formats/finalwriter.md."""
    def attr(length, font=0, size=12, style=0, kind=0):
        b = bytearray(22)
        b[0:4] = struct.pack(">I", length)
        b[4:6] = struct.pack(">H", font)
        b[7], b[9], b[11] = size, style, kind
        b[16:20] = struct.pack(">I", 100)
        return bytes(b)
    txob = bytearray(178)
    txob[4:12] = b"SoftSans"
    txob[147] = 24
    txob[176:178] = struct.pack(">H", 10)
    rule = bytes(9) + b"\x01" + bytes(14)
    return form("SWRT", [
        chunk("FDTA", b"Symbol\0"), chunk("FDTA", b"SoftSans_Bold\0"), chunk("FDTA", b"SoftSans\0"),
        chunk("TXOB", bytes(txob) + b"Frame text"),
        chunk("TBDY", b"\0\0"),
        chunk("RULE", rule), chunk("ATTR", attr(5, font=1)), chunk("CHRS", b"Hello"),
        chunk("ATTR", attr(1, kind=1)), chunk("CHRS", b"\t"),
        chunk("ATTR", attr(5, size=10, style=1)), chunk("CHRS", b"world"),
        chunk("RULE", rule), chunk("ATTR", attr(0)), chunk("CHRS", b""),
        chunk("RULE", rule), chunk("ATTR", attr(11)), chunk("CHRS", "Caf\u00e9 \u00df ok.".encode("latin-1")),
        chunk("ATTR", attr(3, font=2)), chunk("CHRS", b"abg"),
        chunk("RMST", b""), chunk("RULE", rule), chunk("ATTR", attr(4)), chunk("CHRS", b"Page"),
    ])


SAMPLES = {
    "sample.ftxt": ftxt_sample(),
    "sample.pw": prowrite_sample(),
    "sample.ans": b"\x1b[1mBold\x1b[0m normal \x1b[32mgreen\x1b[0m\n",
    "sample.ww": wordworth_sample(),
    "sample.fw": finalwriter_sample(),
    "latin1.txt": b"Caf\xe9\r\nLine two\r\n",
    "utf8.txt": "﻿Café — UTF-8\n".encode("utf-8"),
}


def run(*args):
    return subprocess.run([OWCONVERT, *args], capture_output=True, timeout=60)


def convert(work, name, ext, extra=()):
    src = os.path.join(work, name)
    dst = os.path.join(work, name + "." + ext)
    r = run("--report", *extra, src, dst)
    check(f"{name}->{ext} exit", r.returncode == 0, (r.stdout + r.stderr).decode("utf-8", "replace"))
    try:
        with open(dst, "rb") as f:
            return f.read(), r.stdout.decode("utf-8", "replace")
    except OSError:
        return b"", ""


def check_odt(name, data):
    path = name + ".check.odt"
    with open(path, "wb") as f:
        f.write(data)
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile as e:
        check(f"{name} odt zip", False, str(e))
        return {}
    infos = z.infolist()
    check(f"{name} odt mimetype first", infos[0].filename == "mimetype", infos[0].filename)
    check(f"{name} odt mimetype stored", infos[0].compress_type == zipfile.ZIP_STORED)
    check(f"{name} odt mimetype", z.read("mimetype") == b"application/vnd.oasis.opendocument.text")
    check(f"{name} odt zip test", z.testzip() is None)
    parts = {}
    for part in ("content.xml", "styles.xml", "meta.xml", "META-INF/manifest.xml"):
        try:
            text = z.read(part).decode("utf-8")
            xml.dom.minidom.parseString(text)
            parts[part] = text
        except KeyError:
            check(f"{name} odt has {part}", False)
        except Exception as e:  # not well-formed
            check(f"{name} odt {part} well-formed", False, str(e))
    return parts


def check_docx(name, data):
    path = name + ".check.docx"
    with open(path, "wb") as f:
        f.write(data)
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile as e:
        check(f"{name} docx zip", False, str(e))
        return {}
    check(f"{name} docx zip test", z.testzip() is None)
    parts = {}
    for info in z.infolist():
        text = z.read(info.filename).decode("utf-8")
        try:
            xml.dom.minidom.parseString(text)
        except Exception as e:
            check(f"{name} docx {info.filename} well-formed", False, str(e))
        parts[info.filename] = text
    for part in ("[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/styles.xml",
                 "word/_rels/document.xml.rels", "docProps/core.xml", "docProps/app.xml"):
        check(f"{name} docx has {part}", part in parts)
    # Every relationship's target is in the package, and every part has a content type.
    import re
    types = parts.get("[Content_Types].xml", "")
    for rels, base in (("_rels/.rels", ""), ("word/_rels/document.xml.rels", "word/")):
        for target in re.findall(r'Target="([^"]+)"', parts.get(rels, "")):
            check(f"{name} docx target {target}", base + target in parts)
    for part in parts:
        if not part.endswith(".rels") and part != "[Content_Types].xml":
            check(f"{name} docx type for {part}", ('PartName="/' + part + '"') in types or part.endswith(".xml"))
    return parts


W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'


def docx_sample():
    """A Word document with the things Word writes and simple readers miss."""
    def p(inner, ppr=""):
        return f"<w:p>{('<w:pPr>' + ppr + '</w:pPr>') if ppr else ''}{inner}</w:p>"
    def r(text, rpr=""):
        return f"<w:r>{('<w:rPr>' + rpr + '</w:rPr>') if rpr else ''}<w:t xml:space=\"preserve\">{text}</w:t></w:r>"
    num = '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="1"/></w:numPr>'
    bul = '<w:numPr><w:ilvl w:val="0"/><w:numId w:val="2"/></w:numPr>'
    body = "".join([
        p(r("Heading here"), '<w:pStyle w:val="Heading1"/>'),
        p(r("First"), num), p(r("Second"), num), p(r("Third"), num),
        p(r("Apple"), bul),
        p(r("Page ") + '<w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText xml:space="preserve"> PAGE </w:instrText></w:r>'
          '<w:r><w:fldChar w:fldCharType="separate"/></w:r><w:r><w:t>5</w:t></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r>'
          + r(" and ") + '<w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText> HYPERLINK "http://example.com" </w:instrText></w:r>'
          '<w:r><w:fldChar w:fldCharType="separate"/></w:r>' + r("a link") + '<w:r><w:fldChar w:fldCharType="end"/></w:r>'),
        p('<w:ins w:id="1" w:author="Kim Example">' + r("Kept insert. ") + '</w:ins><w:del w:id="2" w:author="Kim Example"><w:r><w:delText>Gone. </w:delText></w:r></w:del>'
          + r("Note") + '<w:r><w:rPr><w:vertAlign w:val="superscript"/></w:rPr><w:footnoteReference w:id="1"/></w:r>'),
        p('<w:r><w:sym w:font="Symbol" w:char="F061"/></w:r>' + r(" is alpha, ") + r("bold", "<w:b/>") + r(" and ") + r("not", "<w:b w:val=\"0\"/>")),
        "<w:tbl><w:tr><w:tc>" + p(r("A1")) + "</w:tc><w:tc>" + p(r("B1")) + "</w:tc></w:tr><w:tr><w:tc>" + p(r("A2")) + "</w:tc><w:tc>" + p(r("B2")) + "</w:tc></w:tr></w:tbl>",
        p(r("Before break") + '<w:r><w:br w:type="page"/></w:r>' + r("After break")),
        p(r("Big red", '<w:color w:val="FF0000"/><w:sz w:val="32"/>')),
        '<w:sectPr><w:headerReference w:type="default" r:id="rIdH"/><w:pgSz w:w="12240" w:h="15840"/>'
        '<w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720" w:gutter="0"/>'
        '<w:pgNumType w:start="3"/><w:titlePg/></w:sectPr>',
    ])
    files = {
        "[Content_Types].xml": '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
            '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
            '<Default Extension="xml" ContentType="application/xml"/></Types>',
        "_rels/.rels": '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>',
        "word/_rels/document.xml.rels": '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rIdS" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'
            '<Relationship Id="rIdN" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/>'
            '<Relationship Id="rIdT" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme" Target="theme/theme1.xml"/>'
            '<Relationship Id="rIdF" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes" Target="footnotes.xml"/>'
            '<Relationship Id="rIdH" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/header" Target="header1.xml"/></Relationships>',
        "word/document.xml": f'<?xml version="1.0" encoding="UTF-8"?><w:document {W}><w:body>{body}</w:body></w:document>',
        "word/styles.xml": f'<?xml version="1.0"?><w:styles {W}><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:asciiTheme="minorHAnsi" w:hAnsiTheme="minorHAnsi"/>'
            '<w:sz w:val="22"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="160" w:line="259" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>'
            '<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>'
            '<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:pPr><w:keepNext/><w:outlineLvl w:val="0"/></w:pPr>'
            '<w:rPr><w:rFonts w:asciiTheme="majorHAnsi" w:hAnsiTheme="majorHAnsi"/><w:sz w:val="32"/></w:rPr></w:style></w:styles>',
        "word/numbering.xml": f'<?xml version="1.0"?><w:numbering {W}>'
            '<w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="decimal"/><w:lvlText w:val="%1."/><w:pPr><w:ind w:left="720" w:hanging="360"/></w:pPr></w:lvl></w:abstractNum>'
            '<w:abstractNum w:abstractNumId="1"><w:lvl w:ilvl="0"><w:numFmt w:val="bullet"/><w:lvlText w:val=""/></w:lvl></w:abstractNum>'
            '<w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num><w:num w:numId="2"><w:abstractNumId w:val="1"/></w:num></w:numbering>',
        "word/theme/theme1.xml": '<?xml version="1.0"?><a:theme xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" name="Office"><a:themeElements>'
            '<a:fontScheme name="Office"><a:majorFont><a:latin typeface="Calibri Light"/></a:majorFont><a:minorFont><a:latin typeface="Calibri"/></a:minorFont></a:fontScheme>'
            '</a:themeElements></a:theme>',
        "word/footnotes.xml": f'<?xml version="1.0"?><w:footnotes {W}><w:footnote w:type="separator" w:id="-1"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>'
            '<w:footnote w:id="1"><w:p><w:r><w:footnoteRef/></w:r><w:r><w:t xml:space="preserve"> The footnote text.</w:t></w:r></w:p></w:footnote></w:footnotes>',
        "word/header1.xml": f'<?xml version="1.0"?><w:hdr {W}><w:p><w:r><w:t>Running head</w:t></w:r></w:p></w:hdr>',
    }
    import io
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name, text in files.items():
            z.writestr(name, text.encode("utf-8"))
    return out.getvalue()


FODT = """<?xml version="1.0" encoding="UTF-8"?>
<office:document xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0"
 xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0"
 xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:svg="urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"
 xmlns:dc="http://purl.org/dc/elements/1.1/" office:version="1.3" office:mimetype="application/vnd.oasis.opendocument.text">
 <office:meta><dc:title>Flat &amp; tested</dc:title></office:meta>
 <office:font-face-decls><style:font-face style:name="Liberation Sans" svg:font-family="'Liberation Sans'" style:font-family-generic="swiss"/></office:font-face-decls>
 <office:styles>
  <style:default-style style:family="paragraph"><style:text-properties fo:font-size="11pt"/></style:default-style>
  <style:style style:name="Standard" style:family="paragraph"/>
  <style:style style:name="Base" style:family="paragraph" style:parent-style-name="Standard"><style:paragraph-properties fo:text-align="center"/></style:style>
  <style:style style:name="Child" style:family="paragraph" style:parent-style-name="Base"><style:text-properties fo:font-weight="bold" style:font-name="Liberation Sans"/></style:style>
  <text:list-style style:name="Num"><text:list-level-style-number text:level="1" style:num-format="a" style:num-suffix=")"/><text:list-level-style-number text:level="2" style:num-format="1" text:display-levels="2" style:num-suffix="."/></text:list-style>
 </office:styles>
 <office:automatic-styles>
  <style:style style:name="T1" style:family="text"><style:text-properties fo:font-style="italic" fo:color="#008000"/></style:style>
  <style:page-layout style:name="pm1"><style:page-layout-properties fo:page-width="21cm" fo:page-height="29.7cm" fo:margin-left="2cm" fo:margin-right="2cm" fo:margin-top="1in" fo:margin-bottom="1in"/></style:page-layout>
 </office:automatic-styles>
 <office:master-styles><style:master-page style:name="Standard" style:page-layout-name="pm1">
  <style:header><text:p>Head <text:page-number>1</text:page-number></text:p></style:header><style:header-first/></style:master-page></office:master-styles>
 <office:body><office:text>
  <text:tracked-changes><text:changed-region text:id="c1"><text:deletion><text:p>Deleted text</text:p></text:deletion></text:changed-region></text:tracked-changes>
  <text:h text:outline-level="2">A heading</text:h>
  <text:p text:style-name="Child">Centred   and    bold<text:s text:c="3"/>spaced</text:p>
  <text:p>Some <text:span text:style-name="T1">green italic</text:span> text<text:note text:note-class="footnote"><text:note-citation>1</text:note-citation><text:note-body><text:p>A note.</text:p></text:note-body></text:note>.<office:annotation><text:p>A comment</text:p></office:annotation></text:p>
  <text:list text:style-name="Num"><text:list-item><text:p>one</text:p><text:list><text:list-item><text:p>inner</text:p></text:list-item></text:list></text:list-item><text:list-item><text:p>two</text:p></text:list-item></text:list>
  <table:table><table:table-row><table:table-cell><text:p>c1</text:p></table:table-cell><table:table-cell><text:p>c2</text:p></table:table-cell></table:table-row></table:table>
 </office:text></office:body>
</office:document>
"""


def png_rgba(w=4, h=3):
    """A small PNG with an alpha channel (half see-through)."""
    import struct, zlib
    raw = b"".join(b"\x00" + b"".join(bytes((200, 32, 43, 128 if (x + y) % 2 else 255)) for x in range(w)) for y in range(h))
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    return b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b"")


def docx_shading_sample():
    """Shading, borders, a shaded cell, a transparent picture and a footer in a table row (7 Oct 2026)."""
    W = 'xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"'
    pic = ('<w:r><w:drawing><wp:inline><wp:extent cx="914400" cy="685800"/><wp:docPr id="1" name="Logo"/><a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">'
           '<pic:pic><pic:blipFill><a:blip r:embed="rIdImg"/></pic:blipFill><pic:spPr><a:xfrm><a:ext cx="914400" cy="685800"/></a:xfrm></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>')
    body = ('<w:p><w:pPr><w:shd w:val="clear" w:color="auto" w:fill="F2F2F2"/></w:pPr><w:r><w:t>Shaded box</w:t></w:r></w:p>'
            '<w:p><w:pPr><w:pBdr><w:bottom w:val="single" w:sz="6" w:space="1" w:color="888888"/></w:pBdr></w:pPr><w:r><w:t>Ruled heading</w:t></w:r></w:p>'
            '<w:tbl><w:tblPr/><w:tr><w:tc><w:tcPr><w:shd w:val="clear" w:color="auto" w:fill="D6D6D6"/></w:tcPr><w:p><w:r><w:t>Head</w:t></w:r></w:p></w:tc>'
            '<w:tc><w:p><w:r><w:t>Plain</w:t></w:r></w:p></w:tc></w:tr></w:tbl>'
            f'<w:p>{pic}</w:p>'
            '<w:sectPr><w:footerReference w:type="default" r:id="rIdFoot"/><w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440"/></w:sectPr>')
    foot = (f'<w:ftr {W}><w:tbl><w:tblPr/><w:tr><w:tc><w:p><w:r><w:t>Left title</w:t></w:r></w:p></w:tc>'
            '<w:tc><w:p><w:r><w:t>Middle notice</w:t></w:r></w:p></w:tc>'
            '<w:tc><w:p><w:pPr><w:jc w:val="right"/></w:pPr><w:r><w:t xml:space="preserve">Page </w:t></w:r><w:fldSimple w:instr=" PAGE "><w:r><w:t>1</w:t></w:r></w:fldSimple></w:p></w:tc></w:tr></w:tbl></w:ftr>')
    doc = f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document {W}><w:body>{body}</w:body></w:document>'
    ct = ('<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/>'
          '<Default Extension="png" ContentType="image/png"/>'
          '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
          '<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>')
    drels = ('<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
             '<Relationship Id="rIdImg" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/logo.png"/>'
             '<Relationship Id="rIdFoot" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/></Relationships>')
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ct); z.writestr("_rels/.rels", rels); z.writestr("word/document.xml", doc)
        z.writestr("word/_rels/document.xml.rels", drels); z.writestr("word/footer1.xml", foot); z.writestr("word/media/logo.png", png_rgba())
    return buf.getvalue()


def main():
    base = os.environ.get("OWF_TEST_DIR") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "tests")
    os.makedirs(base, exist_ok=True)
    work = tempfile.mkdtemp(prefix="owf-", dir=base)
    for name, data in SAMPLES.items():
        with open(os.path.join(work, name), "wb") as f:
            f.write(data)
    os.chdir(work)

    # IFF FTXT
    text, _ = convert(work, "sample.ftxt", "txt")
    check("ftxt text", text == "Hello bold and italic, underlined.\nCafé in Times and Courier.\nBefore\n\fAfter\tTab\nRed plain\n".encode(),
          repr(text))
    html, _ = convert(work, "sample.ftxt", "html")
    h = html.decode()
    for want in ("<b>bold</b>", "<i>italic</i>", "<u>underlined</u>", "Café", "'Liberation Serif', 'times', serif",
                 "'Liberation Mono', 'courier', monospace", "color: #cc0000", "class=\"ow-page-break\"", "<span class=\"ow-tab\">\t</span>"):
        check("ftxt html has " + want, want in h)
    odt, _ = convert(work, "sample.ftxt", "odt")
    parts = check_odt("ftxt", odt)
    c = parts.get("content.xml", "")
    for want in ("fo:font-weight=\"bold\"", "fo:font-style=\"italic\"", "style:text-underline-style=\"solid\"",
                 "fo:break-before=\"page\"", "<text:tab/>", "fo:color=\"#cc0000\"", "Café"):
        check("ftxt odt has " + want, want in c)

    # ProWrite (IFF WORD)
    text, report = convert(work, "sample.pw", "txt")
    check("prowrite text", text == b"Title\nBody bold red text, page #\na\tb\n\fNext page\n", repr(text))
    check("prowrite report pictures", "1 picture(s) are not brought across yet" in report, report)
    html, _ = convert(work, "sample.pw", "html")
    h = html.decode()
    for want in ("text-align: center", "<b>Title</b>", "font-size: 18pt", "'Liberation Sans', 'helvetica', sans-serif",
                 "<b>bold</b>", "color: #cc0000\"> red</span>", "text-indent: 36pt", "line-height: 200%", "data-ow-tabs=\"2880:right\"",
                 "data-field=\"page\"", "<div class=\"ow-header\" data-first-page=\"0\">", "Header ",
                 "<div class=\"ow-footer\" data-first-page=\"1\">", "data-field=\"date\""):
        check("prowrite html has " + want, want in h)
    odt, report = convert(work, "sample.pw", "odt")
    parts = check_odt("prowrite", odt)
    c = parts.get("content.xml", "")
    s = parts.get("styles.xml", "")
    for want in ("Title", "<text:page-number text:select-page=\"current\">", "style:type=\"right\"", "style:position=\"144pt\"",
                 "fo:line-height=\"200%\"", "fo:text-indent=\"36pt\"", "fo:break-before=\"page\"", "style:font-name=\"Liberation Sans\"",
                 "fo:font-size=\"18pt\"", "fo:color=\"#cc0000\""):
        check("prowrite odt content has " + want, want in c)
    for want in ("<style:header>", "Header ", "<style:footer>", "<text:date/>", "fo:page-width="):
        check("prowrite odt styles has " + want, want in s)
    check("prowrite odt header not on first page", "<style:header-first/>" in s, s[-400:])

    docx, report = convert(work, "sample.pw", "docx")
    parts = check_docx("prowrite", docx)
    d = parts.get("word/document.xml", "")
    for want in ("<w:jc w:val=\"center\"/>", "<w:b/>", "<w:sz w:val=\"36\"/>", "w:ascii=\"Liberation Sans\"",
                 "<w:color w:val=\"CC0000\"/>", "<w:t xml:space=\"preserve\"> red</w:t>", "<w:tab w:val=\"right\" w:pos=\"2880\"/>",
                 "w:line=\"480\" w:lineRule=\"auto\"", "<w:ind w:firstLine=\"720\"/>", "<w:pageBreakBefore/>",
                 "<w:fldSimple w:instr=\" PAGE \">", "<w:headerReference w:type=\"default\" r:id=\"rId2\"/>",
                 "<w:footerReference w:type=\"first\" r:id=\"rId3\"/>", "<w:titlePg/>", "<w:pgSz w:w=\"11906\" w:h=\"16838\"/>"):
        check("prowrite docx document has " + want, want in d)
    check("prowrite docx no first header", "<w:headerReference w:type=\"first\"" not in d, d[-600:])
    check("prowrite docx header", "Header " in parts.get("word/header1.xml", ""))
    check("prowrite docx footer date", "<w:fldSimple w:instr=\" DATE \">" in parts.get("word/footer1.xml", ""))
    check("prowrite docx rels", "header1.xml" in parts.get("word/_rels/document.xml.rels", ""))
    check("prowrite docx types", "wordprocessingml.header+xml" in parts.get("[Content_Types].xml", ""))
    # Elements in w:pPr and w:rPr must be in the schema's order.
    order_ppr = ["pStyle", "pageBreakBefore", "tabs", "spacing", "ind", "jc"]
    order_rpr = ["rFonts", "b", "i", "strike", "color", "sz", "u", "vertAlign"]
    import re
    for block, order in (("pPr", order_ppr), ("rPr", order_rpr)):
        for m in re.finditer(r"<w:%s>(.*?)</w:%s>" % (block, block), d):
            names = re.findall(r"<w:(\w+)", m.group(1))
            names = [n for n in names if n in order]
            check(f"docx {block} order", names == sorted(names, key=order.index), m.group(0))
    docx, _ = convert(work, "sample.ftxt", "docx")
    parts = check_docx("ftxt", docx)
    check("ftxt docx text", "Caf\u00e9" in parts.get("word/document.xml", ""))
    check("ftxt docx no header part", "word/header1.xml" not in parts)

    # ProWrite -> FTXT -> text keeps the words
    ftxt, _ = convert(work, "sample.pw", "ftxt")
    with open(os.path.join(work, "round.ftxt"), "wb") as f:
        f.write(ftxt)
    text, _ = convert(work, "round.ftxt", "txt")
    check("prowrite->ftxt->text", text == b"Title\nBody bold red text, page \na\tb\n\fNext page\n", repr(text))

    # ANSI and plain text
    html, _ = convert(work, "sample.ans", "html")
    check("ansi bold", "<b>Bold</b>" in html.decode(), html)
    check("ansi green", "color: #008800" in html.decode(), html)
    text, report = convert(work, "latin1.txt", "txt")
    check("latin1 text", text == "Café\nLine two\n".encode(), repr(text))
    check("latin1 report", "ISO-8859-1" in report, report)
    with open(os.path.join(work, "pages.txt"), "wb") as f:
        f.write(b"One\n\fTwo\fThree\n")
    text, _ = convert(work, "pages.txt", "txt")
    check("text page breaks", text == b"One\n\fTwo\n\fThree\n", repr(text))
    text, _ = convert(work, "utf8.txt", "asc", ("--format", "amiga-text"))
    check("utf8 to amiga text", text == b"Caf\xe9 ? UTF-8\n", repr(text))

    # Wordworth and Final Writer, as their notes describe them.
    text, report = convert(work, "sample.ww", "txt")
    check("wordworth text", text == b"Title\nBody with bold and plain.\nSecond para\tafter tab\n\fPage two\n", repr(text))
    check("wordworth report pictures", "1 picture(s) or drawing(s)" in report, report)
    html, _ = convert(work, "sample.ww", "html")
    h = html.decode()
    for want in ("text-align: center", "<b>Title</b>", "Body with <b>bold</b> and plain.", "'Liberation Serif', 'CG Times', serif",
                 "@page { size: 595.4pt 843.3pt;", "<div class=\"ow-header\" data-first-page=\"0\">\n<p>Head</p>",
                 "<div class=\"ow-footer\" data-first-page=\"1\">\n<p>Foot</p>", "class=\"ow-page-break\""):
        check("wordworth html has " + want, want in h)
    text, report = convert(work, "sample.fw", "txt")
    check("finalwriter text", text.decode() == "Frame text\nHello\tworld\n\nCaf\u00e9 \u00df ok.\u03b1\u03b2\u03b3\n", repr(text.decode()))
    check("finalwriter report frames", "1 text frame(s) became paragraphs" in report, report)
    html, _ = convert(work, "sample.fw", "html")
    h = html.decode()
    for want in ("font-size: 24pt", "<b>Hello</b>", "<u>world</u>", "font-size: 10pt", "<div class=\"ow-header\"", "<p>Page</p>"):
        check("finalwriter html has " + want, want in h)

    # Readers: our own ODT and DOCX come back with the same text.
    direct, _ = convert(work, "sample.pw", "txt")
    for fmt in ("odt", "docx"):
        with open(os.path.join(work, "sample.pw." + fmt), "rb") as f:
            data = f.read()
        with open(os.path.join(work, "round." + fmt), "wb") as f:
            f.write(data)
        text, _ = convert(work, "round." + fmt, "txt")
        check(f"{fmt} round trip text", text == direct, repr(text))
        html, _ = convert(work, "round." + fmt, "html")
        h = html.decode()
        for want in ("<b>Title</b>", "font-size: 18pt", "data-ow-tabs=\"2880:right\"", "line-height: 200%",
                     "class=\"ow-page-break\"", "data-field=\"page\"", "color: #cc0000"):
            check(f"{fmt} round trip html has {want}", want in h)
        check(f"{fmt} round trip header not on first page", "<div class=\"ow-header\" data-first-page=\"0\">" in h, h[:600])

    # A Word document with Word's own habits.
    with open(os.path.join(work, "word.docx"), "wb") as f:
        f.write(docx_sample())
    text, report = convert(work, "word.docx", "txt")
    want = ("Heading here\n1.\tFirst\n2.\tSecond\n3.\tThird\n\u2022\tApple\nPage # and a link\n"
            "Kept insert. Note1\n\u03b1 is alpha, bold and not\nA1\tB1\nA2\tB2\nBefore break\n\fAfter break\nBig red\nNotes\n1\tThe footnote text.\n")
    check("docx text", text.decode() == want, repr(text.decode()))
    check("docx report lists", "Lists are kept as text" in report, report)
    check("docx report tables", "Tables are kept as rows" in report, report)
    html, _ = convert(work, "word.docx", "html")
    h = html.decode()
    for want in ("<h1", "'Carlito', 'Calibri'", "<b>bold</b>", "color: #ff0000", "font-size: 16pt",
                 "<div class=\"ow-header\" data-first-page=\"0\">", "Running head", "font-size: 11pt; }"):
        check("docx html has " + want, want in h)
    check("docx not bold stays plain", "<b>not</b>" not in h)
    check("docx deletion dropped", "Gone" not in h)

    # A flat ODT with styles through parents, lists, notes and tracked changes.
    with open(os.path.join(work, "flat.fodt"), "w", encoding="utf-8") as f:
        f.write(FODT)
    text, report = convert(work, "flat.fodt", "txt")
    want = "A heading\nCentred and bold   spaced\nSome green italic text1.\na)\tone\na.1.\tinner\nb)\ttwo\nc1\tc2\nNotes\n1\tA note.\n"
    check("fodt text", text.decode() == want, repr(text.decode()))
    check("fodt report comments", "Comments were left out" in report, report)
    html, _ = convert(work, "flat.fodt", "html")
    h = html.decode()
    for want in ("<h2>A heading</h2>", "text-align: center", "'Liberation Sans', sans-serif", "<b>Centred and bold   spaced</b>",
                 "color: #008000\"><i>green italic</i>", "<title>Flat &amp; tested</title>", "@page { size: 595.3pt 841.9pt; margin: 72pt 56.7pt 72pt 56.7pt; }",
                 "<div class=\"ow-header\" data-first-page=\"0\">"):
        check("fodt html has " + want, want in h)
    check("fodt deleted text dropped", "Deleted text" not in h)

    # Word 97 and protected documents are recognised and refused clearly.
    ole = bytearray(1024)
    ole[0:8] = bytes([0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1])
    ole[600:600 + 24] = "WordDocument".encode("utf-16-le")
    with open(os.path.join(work, "old.doc"), "wb") as f:
        f.write(bytes(ole))
    r = run(os.path.join(work, "old.doc"), os.path.join(work, "old.txt"))
    check("word 97 refused", r.returncode == 10 and b"Word 97-2003" in r.stdout, r.stdout)

    # Damaged XML: the flat ODT with bytes changed at random, a fixed seed.
    import random
    rng = random.Random(4102026)
    base = FODT.encode("utf-8")
    for i in range(300):
        data = bytearray(base)
        for _ in range(rng.randint(1, 8)):
            pos = rng.randrange(len(data))
            data[pos] = rng.choice(b"<>/&;\"'=:x \x00\xff") if rng.random() < 0.7 else rng.randrange(256)
        with open(os.path.join(work, "fuzz.fodt"), "wb") as f:
            f.write(bytes(data))
        r = run(os.path.join(work, "fuzz.fodt"), os.path.join(work, "fuzz.html"))
        if r.returncode not in (0, 10):
            check(f"fodt fuzz {i}", False, f"exit {r.returncode}: {(r.stdout + r.stderr)[-400:]!r}")
            break

    # Real-world documents, when a folder of them is given (not in the repository).
    # Folders of them, separated by ":"; every file is tried, Amiga files
    # often have no extension. Each must convert or be refused, never crash.
    corpus = os.environ.get("OWF_CORPUS")
    if corpus:
        n = 0
        for folder in corpus.split(":"):
            for root, _, files in os.walk(folder):
                for name in sorted(files):
                    if name.lower().endswith((".lha", ".info", ".txt", ".md", ".sig")) or name in ("SHA256SUMS",):
                        continue
                    for ext in ("txt", "odt", "docx"):
                        r = run(os.path.join(root, name), os.path.join(work, "corpus." + ext))
                        n += 1
                        if r.returncode not in (0, 10):
                            check(f"corpus {name} -> {ext}", False, f"exit {r.returncode}: {(r.stdout + r.stderr)[-400:]!r}")
        print(f"{n} conversions of real-world documents")

    # Unknown output and broken input
    r = run(os.path.join(work, "sample.pw"), os.path.join(work, "out.xyz"))
    check("unknown extension refused", r.returncode == 10, r.stdout)
    with open(os.path.join(work, "binary.bin"), "wb") as f:
        f.write(bytes(range(256)))
    r = run(os.path.join(work, "binary.bin"), os.path.join(work, "out.txt"))
    check("binary refused", r.returncode == 10, r.stdout)

    # Every cut-short sample: an answer, never a crash.
    cuts = 0
    cut_samples = dict(SAMPLES)
    cut_samples["word.docx"] = docx_sample()
    with open(os.path.join(work, "sample.pw.odt"), "rb") as f:
        cut_samples["sample.odt"] = f.read()
    for name, data in cut_samples.items():
        step = 1 if len(data) < 1500 else 5
        for n in range(0, len(data), step):
            path = os.path.join(work, "cut.bin")
            with open(path, "wb") as f:
                f.write(data[:n])
            r = run(path, os.path.join(work, "cut.html"))
            cuts += 1
            if r.returncode not in (0, 10):
                check(f"{name} cut at {n}", False, f"exit {r.returncode}: {(r.stdout + r.stderr)[-400:]!r}")
                break

    # Shading, borders, a shaded cell, a transparent picture, a footer in a table row (7 Oct 2026)
    with open(os.path.join(work, "shade.docx"), "wb") as f:
        f.write(docx_shading_sample())
    odt, _ = convert(work, "shade.docx", "odt")
    with zipfile.ZipFile(io.BytesIO(odt)) as z:
        c = z.read("content.xml").decode()
    check("shading odt paragraph background", 'fo:background-color="#f2f2f2"' in c, c[:800])
    check("shading odt paragraph border", 'fo:border-bottom="0.5pt solid #888888"' in c)
    check("shading odt cell style", 'style:name="CellD6D6D6"' in c and 'table:style-name="CellD6D6D6"' in c)
    with open(os.path.join(work, "shade-round.odt"), "wb") as f:
        f.write(odt)
    back, _ = convert(work, "shade-round.odt", "docx")
    with zipfile.ZipFile(io.BytesIO(back)) as z:
        d = z.read("word/document.xml").decode()
    check("shading odt to docx paragraph", 'w:fill="F2F2F2"' in d, d[:900])
    check("shading odt to docx border", '<w:bottom w:val="single"' in d and 'w:color="888888"' in d)
    check("shading odt to docx cell", '<w:tcPr><w:tcW w:w="0" w:type="auto"/><w:shd w:val="clear" w:color="auto" w:fill="D6D6D6"/>' in d)
    html, _ = convert(work, "shade.docx", "html")
    h = html.decode()
    for want in ("background-color: #f2f2f2", "border-bottom: 0.5pt solid #888888", "<table class=\"ow-table\">",
                 "<td style=\"background-color: #d6d6d6\">", "<img src=\"data:image/png;base64,"):
        check("shading html has " + want, want in h)
    pdf, _ = convert(work, "shade.docx", "pdf")
    check("pdf picture drawn", b"/Subtype /Image" in pdf and b"/Im1 Do" in pdf)
    check("pdf picture keeps its transparency", b"/SMask" in pdf)
    for want in (b"(Left title)", b"(Middle notice)", b"(Page )"):
        check("pdf footer has " + want.decode(), want in pdf)
    check("pdf text in one object a line", pdf.count(b"BT ") < 12, str(pdf.count(b"BT ")))
    print(f"{cuts} cut-short files tried")

    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s); files in {work}")
    sys.exit(1 if failures else 0)


main()
