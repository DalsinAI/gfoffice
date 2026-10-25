#!/usr/bin/env python3
"""What a sheet holds beside its cells, read and kept on saving:
test_sheet_features.py BOOK_DUMP OWCONVERT

On fixtures/features-openpyxl.xlsx (make_feature_fixtures.py: a workbook
made up to have what the lab's yardstick has) and on the .xlsx OWConvert
writes from it: book_dump's lines must say the same of both, the file
written must be what openpyxl (when installed) and LibreOffice (when
installed; it is not needed) read back with the same, and ODS must say
what it leaves out.

  - conditional formatting: cellIs, expression and colour scale rules
    with their looks (the styles' dxfs), a data bar and a "contains text"
    rule kept as the file wrote them, priorities and stopIfTrue;
  - comments: their text and author, written with the boxes Excel shows
    them in (a VML drawing);
  - data validation: lists, numbers, dates, text lengths and formulas,
    their operators, messages and error styles.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import xml.dom.minidom

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures", "features-openpyxl.xlsx")
DUMP = os.path.abspath(sys.argv[1])
OWCONVERT = os.path.abspath(sys.argv[2])
SOFFICE = shutil.which("soffice") if os.environ.get("OWF_NO_SOFFICE") is None else None
failures = []
checks = 0


def check(name, condition, detail=""):
    global checks
    checks += 1
    if not condition:
        failures.append(name)
        print("FAIL %s %s" % (name, str(detail)[:2000]))


def run(args, timeout=300):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def dump(path):
    rc, out, err = run([DUMP, path])
    check("book_dump " + os.path.basename(path), rc == 0, out + err)
    return out


def lines(text, kind):
    return [l for l in text.splitlines() if l.startswith(kind + " ")]


def sheet_lines(text, sheet, kind):
    """book_dump's lines of one kind under sheet number `sheet`."""
    out, current = [], -1
    for l in text.splitlines():
        if l.startswith("sheet "):
            current = int(l.split()[1])
        elif current == sheet and l.startswith(kind + " "):
            out.append(l)
    return out


def convert(src, dst):
    rc, out, err = run([OWCONVERT, "--report", src, dst])
    check("owconvert %s -> %s" % (os.path.basename(src), os.path.basename(dst)), rc == 0, out + err)
    return out + err


def well_formed(path):
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            if name.endswith(".xml") or name.endswith(".rels"):
                try:
                    xml.dom.minidom.parseString(z.read(name))
                except Exception as e:  # noqa: BLE001 - any parse error is a failure
                    check("well-formed " + name, False, e)


# ---- Conditional formatting ----

def test_condfmt(work):
    src = dump(FIX)
    cf = sheet_lines(src, 0, "cf")
    check("cf: ten rules read", len(cf) == 10, "\n".join(cf))
    by_priority = {}
    for l in cf:
        m = re.search(r"priority=(\d+)", l)
        by_priority[int(m.group(1))] = l
    # cellIs greater than 800 on F7:F242: dark red text on light red
    l = by_priority.get(1, "")
    check("cf 1 cellIs greaterThan", "type=0 op=4" in l and "ranges=6,5-241,5" in l and 'formula "800"' in l, l)
    check("cf 1 look", "colour=9C0006 fill=FFC7CE" in l, l)
    l = by_priority.get(2, "")
    check("cf 2 between", "type=0 op=0" in l and 'formula "100" formula "200"' in l and "fill=C6EFCE" in l, l)
    l = by_priority.get(3, "")
    check("cf 3 expression", 'type=1' in l and 'formula "$D7=\\"No\\""' in l and "ranges=6,0-241,8" in l, l)
    check("cf 3 look: italic grey", "set=2 flags=2 colour=808080" in l, l)
    l = by_priority.get(4, "")
    check("cf 4 three-colour scale", "type=2" in l and 'point 0 "" F8696B point 4 "50" FFEB84 point 1 "" 63BE7B' in l, l)
    l = by_priority.get(5, "")
    check("cf 5 two-colour scale", "type=2" in l and 'point 0 "" FFFFFF point 1 "" 5A8AC6' in l, l)
    l = by_priority.get(6, "")
    check("cf 6 data bar kept", "type=3" in l and 'xml "<cfRule type=\\"dataBar\\"><dataBar>' in l and "dxfId" not in l, l)
    l = by_priority.get(7, "")
    check("cf 7 contains text kept, shown by its formula",
          "type=3" in l and 'formula "NOT(ISERROR(SEARCH(\\"Review\\",I7)))"' in l and "containsText" in l, l)
    check("cf 7 look", "set=1 flags=1 colour=9C0006 fill=FFC7CE" in l, l)
    l = by_priority.get(8, "")
    check("cf 8 stopIfTrue", "type=0 op=2" in l and "stop=1" in l and 'formula "\\"To confirm\\""' in l, l)
    rc, out, err = run([DUMP, FIX])
    check("cf: no 'not read yet' note", "Conditional formatting is not read" not in out, out)
    check("cf: kept-not-shown note", "kept but not shown" in out, out)

    # saved as .xlsx and read again: the same rules
    xlsx = os.path.join(work, "features.xlsx")
    convert(FIX, xlsx)
    well_formed(xlsx)
    back = dump(xlsx)
    check("cf: the same after saving", sheet_lines(back, 0, "cf") == cf,
          "\n".join(sheet_lines(back, 0, "cf")) + "\n---\n" + "\n".join(cf))
    with zipfile.ZipFile(xlsx) as z:
        styles = z.read("xl/styles.xml").decode()
        sheet = z.read("xl/worksheets/sheet1.xml").decode()
    m = re.search(r'<dxfs count="(\d+)"', styles)
    check("cf: one dxf a rule with a look", m and m.group(1) == "7", styles[-1500:])
    check("cf: conditionalFormatting after mergeCells", sheet.find("</mergeCells>") < sheet.find("<conditionalFormatting")
          < sheet.find("<pageMargins"), "")
    try:
        import openpyxl
    except ImportError:
        openpyxl = None
    if openpyxl:
        ws = openpyxl.load_workbook(xlsx)["Systems"]
        rules = sorted([(str(cfr.sqref), rule.type, rule.operator, tuple(rule.formula or ()), bool(rule.stopIfTrue))
                        for cfr in ws.conditional_formatting for rule in cfr.rules], key=str)
        ws0 = openpyxl.load_workbook(FIX)["Systems"]
        want = sorted([(str(cfr.sqref), rule.type, rule.operator, tuple(rule.formula or ()), bool(rule.stopIfTrue))
                       for cfr in ws0.conditional_formatting for rule in cfr.rules], key=str)
        check("cf: openpyxl reads the same rules back", rules == want, "%s\n---\n%s" % (rules, want))
    if SOFFICE:
        def lo_formats(path, outdir):
            """LibreOffice's ODS of a file: its conditional formats' parts, counted by kind."""
            profile = tempfile.mkdtemp(prefix="owf-lo-")
            try:
                os.makedirs(outdir, exist_ok=True)
                subprocess.run([SOFFICE, "-env:UserInstallation=file://" + profile, "--headless", "--convert-to", "ods",
                                "--outdir", outdir, path], check=True, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=300)
                name = os.path.splitext(os.path.basename(path))[0] + ".ods"
                with zipfile.ZipFile(os.path.join(outdir, name)) as z:
                    content = z.read("content.xml").decode()
            finally:
                shutil.rmtree(profile, ignore_errors=True)
            return {k: content.count("<calcext:" + k) for k in ("conditional-format ", "condition ", "color-scale ",
                                                                  "data-bar ", "color-scale-entry ")}
        try:
            mine = lo_formats(xlsx, os.path.join(work, "lo-mine"))
            theirs = lo_formats(FIX, os.path.join(work, "lo-fixture"))
            check("cf: LibreOffice reads the rules written as it reads openpyxl's", mine == theirs and
                  mine["conditional-format "] == 8 and mine["condition "] == 7 and mine["data-bar "] == 1, "%s\n%s" % (mine, theirs))
        except Exception as e:  # noqa: BLE001
            check("cf: LibreOffice converts the file written", False, e)

    # ODS: said to be left out
    report = convert(FIX, os.path.join(work, "features.ods"))
    check("cf: ODS says it leaves them out", "Conditional formatting is kept in Excel workbooks" in report, report)


# ---- Comments ----

def test_comments(work):
    src = dump(FIX)
    cms = sheet_lines(src, 0, "comment")
    want = ['comment 6,0 author "Team 5" text "Main identity service.\\nOwner to confirm."',
            'comment 8,2 author "Team 5" text "Hosted by the supplier"',
            'comment 11,5 author "Finance" text "Cost per year, before VAT"']
    check("comments read", cms == want, "\n".join(cms))
    rc, out, err = run([DUMP, FIX])
    check("comments: no 'not read yet' note", "Comments on cells are not read" not in out, out)
    xlsx = os.path.join(work, "comments.xlsx")
    convert(FIX, xlsx)
    well_formed(xlsx)
    check("comments: the same after saving", sheet_lines(dump(xlsx), 0, "comment") == want, "")
    with zipfile.ZipFile(xlsx) as z:
        names = z.namelist()
        types = z.read("[Content_Types].xml").decode()
        rels = z.read("xl/worksheets/_rels/sheet1.xml.rels").decode() if "xl/worksheets/_rels/sheet1.xml.rels" in names else ""
        sheet = z.read("xl/worksheets/sheet1.xml").decode()
        vml = z.read("xl/drawings/vmlDrawing1.vml").decode() if "xl/drawings/vmlDrawing1.vml" in names else ""
    check("comments: the parts", "xl/comments1.xml" in names and "/xl/comments1.xml" in types and 'Extension="vml"' in types, names)
    check("comments: the sheet's relationships", "comments1.xml" in rels and "vmlDrawing1.vml" in rels, rels)
    check("comments: the legacy drawing, last", sheet.rstrip().endswith('<legacyDrawing r:id="rId2"/></worksheet>'), sheet[-200:])
    check("comments: a note shape each", vml.count('ObjectType="Note"') == 3 and "<x:Row>6</x:Row><x:Column>0</x:Column>" in vml, vml[:500])
    try:
        import openpyxl
    except ImportError:
        openpyxl = None
    if openpyxl:
        ws = openpyxl.load_workbook(xlsx)["Systems"]
        got = [(c, ws[c].comment.text if ws[c].comment else None, ws[c].comment.author if ws[c].comment else None)
               for c in ("A7", "C9", "F12", "B7")]
        check("comments: openpyxl reads them back", got == [("A7", "Main identity service.\nOwner to confirm.", "Team 5"),
                                                           ("C9", "Hosted by the supplier", "Team 5"),
                                                           ("F12", "Cost per year, before VAT", "Finance"), ("B7", None, None)], got)
    if SOFFICE:
        profile = tempfile.mkdtemp(prefix="owf-lo-")
        try:
            outdir = os.path.join(work, "lo-comments")
            os.makedirs(outdir, exist_ok=True)
            subprocess.run([SOFFICE, "-env:UserInstallation=file://" + profile, "--headless", "--convert-to", "ods",
                            "--outdir", outdir, xlsx], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=300)
            with zipfile.ZipFile(os.path.join(outdir, "comments.ods")) as z:
                content = z.read("content.xml").decode()
            check("comments: LibreOffice reads the three", content.count("<office:annotation") == 3 and
                  "Cost per year, before VAT" in content, content.count("<office:annotation"))
        except Exception as e:  # noqa: BLE001
            check("comments: LibreOffice converts the file written", False, e)
        finally:
            shutil.rmtree(profile, ignore_errors=True)
    report = convert(FIX, os.path.join(work, "comments.ods"))
    check("comments: ODS says it leaves them out", "Comments on cells are kept in Excel workbooks" in report, report)


# ---- Data validation ----

VALIDATIONS = [
    'validation type=3 op=0 blank=1 nodropdown=0 prompt=0 error=0 style=0 ranges=6,3-241,3 formula "\\"Yes,No\\"" '
    'prompt "Hosted" "Hosted by us?" message "Not a choice" "Yes or No"',
    'validation type=2 op=0 blank=0 nodropdown=0 prompt=0 error=0 style=0 ranges=6,5-241,5 formula "0" formula "100000"',
    'validation type=4 op=4 blank=0 nodropdown=0 prompt=0 error=0 style=0 ranges=6,6-241,6 formula "DATE(2020,1,1)"',
    'validation type=6 op=7 blank=0 nodropdown=0 prompt=0 error=1 style=1 ranges=6,8-241,8 formula "200"',
    'validation type=3 op=0 blank=0 nodropdown=0 prompt=0 error=0 style=0 ranges=6,4-241,4 formula "Lists!$A$1:$A$5"',
    'validation type=1 op=0 blank=0 nodropdown=0 prompt=0 error=0 style=0 ranges=6,7-19,7;29,7-39,7 formula "0" formula "10"',
    'validation type=7 op=0 blank=0 nodropdown=0 prompt=0 error=0 style=0 ranges=6,0-241,0 formula "LEN(A7)>0"',
]


def openpyxl_validations(path):
    import openpyxl
    ws = openpyxl.load_workbook(path)["Systems"]
    return sorted((str(v.sqref), v.type, v.operator or "between", v.formula1, v.formula2, bool(v.allow_blank),
                   v.promptTitle, v.prompt, v.errorTitle, v.error, v.errorStyle or "stop", bool(v.showErrorMessage))
                  for v in ws.data_validations.dataValidation)


def test_validation(work):
    src = dump(FIX)
    got = sheet_lines(src, 0, "validation")
    check("validation: the seven read", got == VALIDATIONS, "\n".join(got))
    rc, out, err = run([DUMP, FIX])
    check("validation: no 'not read yet' note", "Data validation (lists to pick from, limits) is not read" not in out, out)
    xlsx = os.path.join(work, "validation.xlsx")
    convert(FIX, xlsx)
    well_formed(xlsx)
    check("validation: the same after saving", sheet_lines(dump(xlsx), 0, "validation") == VALIDATIONS, "")
    with zipfile.ZipFile(xlsx) as z:
        sheet = z.read("xl/worksheets/sheet1.xml").decode()
    check("validation: after the conditional formats, before the margins",
          sheet.rfind("</conditionalFormatting>") < sheet.find('<dataValidations count="7">') < sheet.find("<pageMargins"), "")
    try:
        import openpyxl  # noqa: F401
        have = True
    except ImportError:
        have = False
    if have:
        a, b = openpyxl_validations(xlsx), openpyxl_validations(FIX)
        check("validation: openpyxl reads them back", a == b, "%s\n---\n%s" % (a, b))
    if SOFFICE:
        profile = tempfile.mkdtemp(prefix="owf-lo-")
        try:
            outdir = os.path.join(work, "lo-validation")
            os.makedirs(outdir, exist_ok=True)
            subprocess.run([SOFFICE, "-env:UserInstallation=file://" + profile, "--headless", "--convert-to", "ods",
                            "--outdir", outdir, xlsx], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=300)
            with zipfile.ZipFile(os.path.join(outdir, "validation.ods")) as z:
                content = z.read("content.xml").decode()
            n = content.count("<table:content-validation ")
            check("validation: LibreOffice reads the seven", n == 7 and "Hosted by us?" in content, n)
        except Exception as e:  # noqa: BLE001
            check("validation: LibreOffice converts the file written", False, e)
        finally:
            shutil.rmtree(profile, ignore_errors=True)
    report = convert(FIX, os.path.join(work, "validation.ods"))
    check("validation: ODS says it leaves them out", "Data validation is kept in Excel workbooks" in report, report)


def damaged(work):
    """The first sheet's XML cut short and changed in what follows its cells (the conditional formats and
    the rest): book_dump answers with a book or an error, never a crash."""
    with zipfile.ZipFile(FIX) as z:
        parts = {n: z.read(n) for n in z.namelist()}
    sheet = parts["xl/worksheets/sheet1.xml"]
    start = sheet.find(b"</sheetData>")
    rnd = __import__("random").Random(25102026)
    bad = 0
    for k in range(60):
        cut = start + (len(sheet) - start) * k // 60
        variants = [sheet[:cut]]
        if cut < len(sheet):
            changed = bytearray(sheet)
            changed[cut] = rnd.choice(b'<>"=/ x')
            variants.append(bytes(changed))
        for v in variants:
            path = os.path.join(work, "damaged.xlsx")
            with zipfile.ZipFile(path, "w") as z:
                for n, data in parts.items():
                    z.writestr(n, v if n == "xl/worksheets/sheet1.xml" else data)
            rc, out, err = run([DUMP, path])
            if rc not in (0, 10):
                bad += 1
                check("damaged sheet read safely (cut at %d)" % cut, False, err[-800:])
    check("damaged sheets read safely", bad == 0, bad)


def main():
    with tempfile.TemporaryDirectory() as work:
        test_condfmt(work)
        test_comments(work)
        test_validation(work)
        damaged(work)
    if failures:
        print("sheet features: %d of %d checks failed" % (len(failures), checks))
        return 1
    print("sheet features: %d checks passed" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
