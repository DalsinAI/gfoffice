#!/usr/bin/env python3
"""The PPTX reader's tests: test_pptx.py path/to/deck_dump path/to/owconvert

Reads the PPTX fixtures (fixtures/pptx-talk.pptx and pptx-plain.pptx, made
by make_pptx_fixtures.py with python-pptx) and checks what deck_dump prints
of them. Then:

- each saved as ODP and read back: the same slides, objects and text, and
  the ODP valid ODF (odf_check.py) when odfpy is installed;
- OWConvert's PDF of each, when it was built with OpenLayout: a page a
  shown slide, and the slides' text in it (pdftotext);
- every cut-short copy, and damaged and hostile ones (bytes changed in the
  slides' XML, groups nested far too deep, numbers past any slide, parts
  missing, relationships that point nowhere or in a circle), read to an
  answer, never a crash (make deck-check runs this under AddressSanitizer
  and UBSan).
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
DUMP = os.path.abspath(sys.argv[1])
OWCONVERT = os.path.abspath(sys.argv[2])
TALK = os.path.join(FIX, "pptx-talk.pptx")
PLAIN = os.path.join(FIX, "pptx-plain.pptx")
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append(f"{name}: {detail}")
        print(f"FAIL {name} {str(detail)[:600]}")


def run(args, timeout=120):
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def dump(path, out=None):
    return run([DUMP, path] + ([out] if out else []))


def section(out, start):
    """The lines of one slide (from 'slide N ') up to the next."""
    lines, on = [], False
    for line in out.splitlines():
        if line.startswith("slide "):
            on = line.startswith(start)
        elif not line.startswith(" "):
            on = False
        if on:
            lines.append(line)
    return "\n".join(lines)


def test_talk():
    rc, out, err = dump(TALK)
    check("talk: read", rc == 0, out[-500:] + err)
    head = [x for x in out.splitlines() if x.startswith("deck ")]
    check("talk: the deck", head and "format=pptx size=19200,10800" in head[0] and
          'title="OpenPresentation PPTX fixture"' in head[0] and "slides=5 masters=1 layouts=4" in head[0], head)
    check("talk: the theme's fonts", 'base=font="Calibri" size=360' in out)
    check("talk: the master's title level", 'level title font="Calibri" size=880 flags=0 colour=000000 align=1' in out)
    check("talk: the master's body levels", 'level body1 font="Calibri" size=640 flags=0 colour=000000 align=0 left=540 right=0 first=-540 before=128'
          in out and "bullet=2022" in out and "bullet=2013" in out and 'bfont="Arial"' in out, out[:3000])
    check("talk: the master's footer fields", "ph=8/0" in out and "ph=10/0" in out and "field=2" in out and "field=0" in out)
    check("talk: layouts by name", 'layout 0 "Title Slide"' in out and 'layout 1 "Title and Content"' in out and
          'layout 2 "Title Only"' in out and 'layout 3 "Blank"' in out)
    s0 = section(out, "slide 0 ")
    check("slide 1: the title slide", "layout=0" in s0 and '"GFOffice on the Amiga"' in s0 and
          "box=1440,3355,16320,2315" in s0 and "valign=1 autofit=1" in s0, s0)
    check("slide 1: the subtitle's own look", 'colour=404040 "OpenPresentation reads PowerPoint"' in s0 and
          re.search(r"ph=2/0 .*\n\s+para level=0 align=1", s0) is not None, s0)
    s1 = section(out, "slide 1 ")
    check("slide 2: bullets on three levels", re.search(r'para level=1 .*\n.*"Titles and bullet lists"', s1) and
          re.search(r'para level=2 .*\n.*"A second level"', s1) and re.search(r'para level=3 .*\n.*"A third level"', s1), s1)
    check("slide 2: notes", '"Speaker notes for slide two."' in s1)
    check("slide 2: a push from below, fast", "transition=push/3/500/-1" in s1)
    s2 = section(out, "slide 2 ")
    check("slide 3: a fade after five seconds", "transition=fade/0/1000/5000" in s2, s2[:200])
    check("slide 3: a text box that grows", 'name="Note"' in s2 and "autofit=2" in s2 and "box=720,2160,5760,1152" in s2)
    check("slide 3: bold red", 'flags=1 colour=C00000 "bold red"' in s2 and '" words"' in s2)
    check("slide 3: a centred italic 24 point line", re.search(r'para level=0 align=1 .*\n.*size=480 flags=2 colour=auto "Second line, centred"', s2))
    check("slide 3: a filled rectangle with a 2 point line and white text", "fill=1F3864 line=000000/40/1/0 shape=0" in s2 and
          'colour=FFFFFF "Rectangle"' in s2)
    check("slide 3: the theme's accent for shapes without their own", "fill=4F81BD" in s2 and "shape=2 " in s2)
    check("slide 3: a rounded rectangle", "fill=FFC000" in s2 and "shape=1 radius=360" in s2)
    check("slide 3: triangle, diamond, arrow", "shape=3 " in s2 and "shape=4 " in s2 and "shape=6 " in s2)
    check("slide 3: a line with an arrowhead", "line=C00000/60/1/2 shape=5" in s2)
    s3 = section(out, "slide 3 ")
    check("slide 4: the picture", "kind=1 " in s3 and "image=image/png/" in s3 and 'alt="Four coloured quarters"' in s3, s3[:800])
    check("slide 4: the table", "cols=3 widths=5040,2880,2880" in s3 and "cell=0,0/3 shade=4F81BD" in s3 and
          'flags=1 colour=FFFFFF "Program"' in s3 and "cell=2,2/3 shade=E2EFDA" in s3 and '"PPTX ODP"' in s3, s3)
    s4 = section(out, "slide 4 ")
    check("slide 5: hidden", "hidden=1" in s4)
    check("slide 5: a group and its two shapes", "kind=5 " in s4 and 'name="Pair"' in s4 and
          len(re.findall(r"parent=0 ", s4)) == 2, s4)
    for note in ("Theme colours are read as fixed colours", "Table styles are drawn as PowerPoint's default one"):
        check("talk: the report says " + note, note in out)
    return out


def test_plain():
    rc, out, err = dump(PLAIN)
    check("plain: read", rc == 0, out[-500:] + err)
    check("plain: 4:3, two slides", "size=14400,10800" in out and "slides=2 masters=1 layouts=2" in out, out[:300])
    check("plain: text", '"Made with python-pptx"' in out and '"Its own template, 4:3"' in out and
          re.search(r'para level=1 .*\n.*"One"', out) and re.search(r'para level=1 .*\n.*"Two"', out))


def model(text):
    lines = []
    for line in text.splitlines():
        if line.startswith(("note ", "save-note ", "deck ", "  level ", "kept ")):
            continue
        line = re.sub(r"^slide (\d+) name=\S+", r"slide \1", line)
        line = re.sub(r' font=("[^"]*"|-) size=\d+', "", line)     # fonts are renamed, runs made relative
        line = re.sub(r" colour=(auto|[0-9A-F]{6})", "", line)
        line = re.sub(r" inset=\S+", "", line)
        line = re.sub(r" transition=fade/\d", " transition=fade", line)
        if " kind=5 " in line:
            line = re.sub(r" id=\d+", "", line)                          # ODP's groups have no id

        lines.append(line)
    return lines


def round_trip(work, name, path):
    odp = os.path.join(work, name + ".odp")
    rc, a, err = dump(path, odp)
    check(f"{name}: saved as ODP", rc == 0 and "save-error" not in a, a[-400:] + err)
    if rc:
        return None
    rc, b, err = dump(odp)
    check(f"{name}: its ODP read back", rc == 0, b[-400:] + err)
    ma, mb = model(a), model(b)
    diff = [f"- {x}\n+ {y}" for x, y in zip(ma, mb) if x != y]
    check(f"{name}: the same slides, objects and text through ODP", ma == mb,
          f"{len(ma)} and {len(mb)} lines; " + "\n".join(diff[:6]))
    try:
        import odf  # noqa: F401
        r = subprocess.run([sys.executable, os.path.join(HERE, "odf_check.py"), odp], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=120)
        check(f"{name}: its ODP is valid ODF", r.returncode == 0, r.stdout.decode("utf-8", "replace")[-800:])
    except ImportError:
        print("no odfpy: the ODP written is not checked against ODF's grammar")
    return odp


def test_pdf(work, name, path, pages, words):
    rc, out, _ = run([OWCONVERT, "--formats"])
    if "a page a slide" not in out:
        print("owconvert was built without OpenLayout: no PDF tests")
        return
    pdf = os.path.join(work, name + ".pdf")
    rc, out, err = run([OWCONVERT, path, pdf])
    check(f"{name}: to PDF", rc == 0 and os.path.exists(pdf), out + err)
    if rc:
        return
    data = open(pdf, "rb").read()
    m = re.search(rb"/Type /Pages /Count (\d+)", data)
    check(f"{name}: a page a shown slide", m and int(m.group(1)) == pages, m.group(1) if m else "no page tree")
    if shutil.which("pdftotext"):
        r = subprocess.run(["pdftotext", "-layout", pdf, "-"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        text = " ".join(r.stdout.decode("utf-8", "replace").split())
        for w in words:
            check(f"{name}: PDF has {w!r}", w in text, text[:600])
        check(f"{name}: PDF leaves the hidden slide out", "Backup (hidden)" not in text)


def rezip(src, dst, change, only=None):
    with zipfile.ZipFile(src) as z, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as w:
        for info in z.infolist():
            data = z.read(info.filename)
            if only is None or re.search(only, info.filename):
                data = change(info.filename, data)
            if data is not None:
                w.writestr(info, data)
    return dst


def answers(name, path, also_pdf=False):
    rc, out, err = dump(path)
    if rc not in (0, 10):
        check(name, False, f"exit {rc}: {(out + err)[-400:]}")
        return False
    if also_pdf and rc == 0:
        rc, out, err = run([OWCONVERT, path, path + ".pdf"])
        if rc not in (0, 10):
            check(name + " to PDF", False, f"exit {rc}: {(out + err)[-400:]}")
            return False
    return True


def hostile(work):
    rng = random.Random(138)
    cases = 0
    # bytes changed at random in the slides', layouts' and master's XML
    for k in range(80):
        def flip(fn, d, k=k):
            b = bytearray(d)
            for _ in range(1 + k % 9):
                b[rng.randrange(len(b))] = rng.randrange(256)
            return bytes(b)
        p = rezip(TALK, os.path.join(work, "flip.pptx"), flip, r"ppt/(slides|slideLayouts|slideMasters|theme)/[^/]*\.xml$")
        cases += 1
        if not answers(f"damaged XML {k}", p, also_pdf=k % 8 == 0):
            break
    # groups nested far deeper than any deck, and a paragraph of 20000 runs
    deep = 5000

    def nest(fn, d):
        if not fn.endswith("slide5.xml"):
            return d
        g = (b'<p:grpSp><p:nvGrpSpPr><p:cNvPr id="90" name="g"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr>'
             b'<a:xfrm><a:off x="1" y="1"/><a:ext cx="2" cy="2"/><a:chOff x="0" y="0"/><a:chExt cx="1" cy="1"/></a:xfrm></p:grpSpPr>')
        runs = b"<a:r><a:t>x</a:t></a:r>" * 20000
        box = (b'<p:sp><p:nvSpPr><p:cNvPr id="91" name="t"/><p:cNvSpPr txBox="1"/><p:nvPr/></p:nvSpPr><p:spPr/>'
               b'<p:txBody><a:bodyPr/><a:p>' + runs + b"</a:p></p:txBody></p:sp>")
        return d.replace(b"</p:spTree>", g * deep + box + b"</p:grpSp>" * deep + b"</p:spTree>", 1)
    p = rezip(TALK, os.path.join(work, "deep.pptx"), nest)
    cases += 1
    answers("deep groups", p, also_pdf=True)

    # numbers far past any slide
    def huge(fn, d):
        d = re.sub(rb'(x|y|cx|cy|w|marL|indent|sz|lIns|tIns|val|rot|advTm|dur)="-?\d+"',
                   lambda m: m.group(1) + (b'="-99999999999999999999"' if rng.random() < 0.3 else b'="99999999999999999999"'), d)
        return d
    p = rezip(TALK, os.path.join(work, "huge.pptx"), huge, r"\.xml$")
    cases += 1
    if answers("numbers past any slide", p, also_pdf=True):
        rc, out, err = dump(p, os.path.join(work, "huge.odp"))
        check("numbers past any slide: saved as ODP", rc in (0, 10), f"exit {rc} {(out + err)[-300:]}")
    # parts missing: the layouts, the master, the theme, the picture, the notes
    for gone in (r"slideLayouts/slideLayout\d+\.xml$", r"slideMasters/", r"theme/", r"media/", r"notesSlides/",
                 r"presentation\.xml\.rels$", r"_rels/\.rels$", r"slides/slide2\.xml$"):
        p = rezip(TALK, os.path.join(work, "gone.pptx"), lambda fn, d, gone=gone: None if re.search(gone, fn) else d)
        cases += 1
        answers("missing " + gone, p, also_pdf=True)
    # relationships that point nowhere, out of the package, or round in a circle
    def rels(fn, d):
        if fn.endswith("slide1.xml.rels"):
            return d.replace(b"../slideLayouts/slideLayout1.xml", b"../../../../etc/passwd")
        if fn.endswith("slideLayout2.xml.rels"):
            return d.replace(b"../slideMasters/slideMaster1.xml", b"slideLayout2.xml")
        if fn.endswith("slide4.xml.rels"):
            return d.replace(b"../media/image1.png", b"../slides/slide4.xml")
        return d
    p = rezip(TALK, os.path.join(work, "rels.pptx"), rels, r"\.rels$")
    cases += 1
    answers("odd relationships", p, also_pdf=True)
    # more slides than a deck holds
    def many(fn, d):
        if fn == "ppt/presentation.xml":
            ids = b"".join(b'<p:sldId id="%d" r:id="rId8"/>' % (300 + i) for i in range(2100))
            return re.sub(rb"<p:sldIdLst>.*?</p:sldIdLst>", b"<p:sldIdLst>" + ids + b"</p:sldIdLst>", d, flags=re.S)
        return d
    p = rezip(TALK, os.path.join(work, "many.pptx"), many)
    rc, out, err = dump(p)
    cases += 1
    check("2100 slides: the first 2000", rc == 0 and "slides=2000" in out and "Slides beyond 2000 were left out" in out,
          out[:300] + err[-300:])
    # not a presentation
    for what, data in (("empty", b""), ("text", b"hello"), ("a zip of nothing", b"PK\x05\x06" + b"\0" * 18)):
        p = os.path.join(work, "not.pptx")
        open(p, "wb").write(data)
        rc, out, err = dump(p)
        cases += 1
        check(f"not a deck ({what}): refused", rc == 10 and "error " in out, out + err)
    return cases


def cut_short(work, path):
    data = open(path, "rb").read()
    tried = 0
    p = os.path.join(work, "cut.pptx")
    for n in range(0, len(data), max(1, len(data) // 300)):
        open(p, "wb").write(data[:n])
        tried += 1
        if not answers(f"cut at {n}", p, also_pdf=n % 7 == 0):
            break
    return tried


def main():
    work = tempfile.mkdtemp(prefix="pptx-tests-")
    test_talk()
    test_plain()
    talk_odp = round_trip(work, "talk", TALK)
    round_trip(work, "plain", PLAIN)
    test_pdf(work, "talk", TALK, 4, ["GFOffice on the Amiga", "OpenPresentation reads PowerPoint", "What it reads",
                                       "Titles and bullet lists", "A third level", "A text box with bold red words",
                                       "Second line, centred", "Rectangle", "Ellipse", "Next", "OpenPresentation PPTX ODP ODP"])
    if talk_odp:
        test_pdf(work, "talk-odp", talk_odp, 4, ["GFOffice on the Amiga", "A third level", "Rectangle"])
    cases = hostile(work)
    tried = cut_short(work, TALK) + cut_short(work, PLAIN)
    print(f"{tried} cut-short and {cases} damaged PPTX files tried")
    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s); files in {work}")
    if not failures:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
