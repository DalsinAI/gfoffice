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
    rule kept as the file wrote them, priorities and stopIfTrue.
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
        damaged(work)
    if failures:
        print("sheet features: %d of %d checks failed" % (len(failures), checks))
        return 1
    print("sheet features: %d checks passed" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
