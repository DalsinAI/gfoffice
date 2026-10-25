#!/usr/bin/env python3
"""The presentation tests: test_decks.py path/to/deck_dump path/to/owconvert

Reads the fixtures (fixtures/odp-hand, written by hand from ODF 1.2 and
zipped here; odfpy.odp; libreoffice.odp and .fodp, see make_fixtures.py)
and checks what deck_dump prints of them. Then:

- round trips: each fixture saved as ODP (and flat ODP) and read back to
  the same model, and saved again the same;
- every ODP written checked as ODF (odf_check.py, with odfpy's grammar),
  when odfpy is installed;
- the PDF OWConvert makes, when it was built with OpenLayout: a page a
  slide shown, the slides' text in it, and qpdf --check and pdftotext when
  they are installed;
- every cut-short copy of the fixtures, and damaged and hostile ones, read
  to an answer, never a crash (make deck-check runs this under
  AddressSanitizer and UBSan), within bounded time.

With DECK_LIBREOFFICE=1 and LibreOffice installed, LibreOffice opens the
ODP written from each fixture too.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import io
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
FIX = os.path.join(HERE, "fixtures")
DUMP = os.path.abspath(sys.argv[1])
OWCONVERT = os.path.abspath(sys.argv[2])
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


def model(text):
    """The dump without its notes and the reader's name: what two readings
    of the same deck must print alike."""
    lines = []
    for line in text.splitlines():
        if line.startswith(("note ", "save-note ")):
            continue
        line = re.sub(r"^deck format=\S+ ", "deck ", line)
        lines.append(line)
    return lines


def pack(src, dst):
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(src, "mimetype"), "mimetype", compress_type=zipfile.ZIP_STORED)
        for root, dirs, files in os.walk(src):
            dirs.sort()
            for f in sorted(files):
                path = os.path.join(root, f)
                name = os.path.relpath(path, src).replace(os.sep, "/")
                if name != "mimetype":
                    z.write(path, name)
    return dst


def have_odfpy():
    try:
        import odf  # noqa: F401
        return True
    except ImportError:
        return False


def odf_valid(path):
    r = subprocess.run([sys.executable, os.path.join(HERE, "odf_check.py"), path], stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=120)
    return r.returncode == 0, r.stdout.decode("utf-8", "replace")


def owc_pdf():
    rc, out, _ = run([OWCONVERT, "--formats"])
    return "a page a slide" in out


def pdf_pages(data):
    """Our writer's page count, from its page tree."""
    m = re.search(rb"/Type /Pages /Count (\d+)", data)
    return int(m.group(1)) if m else -1


def pdf_pages_any(path):
    """Any PDF's page count, by pypdf or pdfinfo; -1 when neither is there."""
    try:
        import pypdf
        return len(pypdf.PdfReader(path).pages)
    except ImportError:
        pass
    if shutil.which("pdfinfo"):
        r = subprocess.run(["pdfinfo", path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        m = re.search(rb"Pages:\s+(\d+)", r.stdout)
        if m:
            return int(m.group(1))
    return -1


def pdf_text(path):
    """The PDF's text, by pdftotext or pypdf when there is one, else None."""
    if shutil.which("pdftotext"):
        r = subprocess.run(["pdftotext", "-layout", path, "-"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        return r.stdout.decode("utf-8", "replace")
    try:
        import pypdf
        return "\n".join(p.extract_text() or "" for p in pypdf.PdfReader(path).pages)
    except ImportError:
        return None


# ---- What the fixtures hold ----

def lines_with(lines, word):
    return [x for x in lines if word in x]


def test_hand(work, hand):
    rc, out, err = dump(hand)
    check("hand: read", rc == 0, out[-500:] + err)
    lines = out.splitlines()
    head = lines_with(lines, "deck format=")
    check("hand: the deck", head and "format=odp size=15874,8929" in head[0] and 'title="OpenPresentation P1 fixture"' in head[0]
          and "slides=5 masters=1 layouts=2" in head[0], head)
    check("hand: the master's background", 'master 0 "Default" background=FDFDF6' in out)
    check("hand: the title level", "level title font=\"Liberation Sans\" size=800 flags=1 colour=1F3864 align=1" in out, out[:2000])
    check("hand: the subtitle level, serif italic", 'level subtitle font="Liberation Serif" size=560 flags=2' in out)
    check("hand: outline levels' sizes", 'level body1 font="Liberation Sans" size=640' in out and
          'level body2 font="Liberation Sans" size=560' in out and 'level body3 font="Liberation Sans" size=480' in out)
    check("hand: outline bullets and indents", "left=340 right=0 first=-340 before=283" in out and "bullet=25CF" in out
          and "bullet=2013" in out and 'bfont="OpenSymbol" bsize=45' in out)
    check("hand: layouts", 'layout 0 "Title Slide"' in out and 'layout 1 "Title, Content"' in out)
    check("hand: master art", 'name="Band"' in out and 'name="Logo"' in out and "image=image/png/146/" in out)
    slides = lines_with(lines, "slide ")
    check("hand: slide 1", 'slide 0 name="page1" master=0 layout=0' in out and
          'show=7 footer="Team 5 & friends" date="25 October 2026" transition=fade/0/2000/5000' in out, slides[:1])
    check("hand: slide 2", 'slide 1 name="page2" master=0 layout=1 background=FFFFFF' in out and
          "show=3" in slides[1] and "transition=push/1/500/-1" in slides[1], slides[1:2])
    check("hand: slide 3's gradient as a colour, its wipe", "background=FFCC00" in slides[2] and "transition=wipe/3/0/-1" in slides[2])
    check("hand: slide 3's animation kept", "timing=" in slides[2])
    check("hand: slide 4's dissolve", "transition=dissolve" in slides[3])
    check("hand: slide 5 hidden, a background picture, its transition kept", "hidden=1" in slides[4] and
          "image=image/png/80/" in slides[4] and 'tkeep= smil:type="clockWipe" smil:subtype="clockwiseTwelve"' in slides[4], slides[4:5])
    check("hand: notes", '"Welcome everyone."' in out and '"Say what P1 is."' in out and '"Lists keep their levels."' in out)
    check("hand: title text relative to its level", 'run 0 font=- size=0 flags=1 colour=auto "OpenPresentation"' in out)
    check("hand: subtitle text with its dash and ampersand", '"Phase P1 — the model & ODP"' in out)
    check("hand: list levels", re.search(r'para level=1 .*\n.*"A point with "', out) and
          re.search(r'para level=2 .*\n.*"A second level, "', out) and re.search(r'para level=3 .*\n.*"A third level"', out), "")
    check("hand: a span's own look", 'flags=1 colour=C00000 "bold red"' in out and 'flags=6 colour=auto "italic"' in out)
    check("hand: a link, and the space after it", '"DalsinAI" href="https://github.com/DalsinAI"' in out and '" for more"' in out)
    check("hand: a numbered list's own levels", 'level list1 font="Liberation Sans" size=360 flags=0 colour=000000 align=0 left=453 right=0 first=-453'
          in out and "bullet=80000031" in out and 'suffix="."' in out and "bullet=80000061" in out and 'suffix=")"' in out)
    check("hand: a text box's own font", 'run 0 font="Liberation Mono" size=320 flags=0 colour=auto "Open"' in out)
    check("hand: tabs, spaces and line breaks", 'run 1 font="Liberation Mono"' in out and '"tab   spaces"' in out and 'run 2 font="Liberation Mono"' in out)
    check("hand: shrink to fit and grow", "valign=0 autofit=1" in out and "autofit=2" in out)
    shapes = lines_with(lines, "kind=2 ")
    check("hand: shapes", any("shape=0" in x and 'name="Box"' in x and "id=1 " in x for x in shapes) and
          any("shape=1 radius=283" in x and 'alt="A rounded rectangle"' in x for x in shapes) and
          any("shape=2" in x for x in shapes) and any("shape=5" in x and "line=C00000/56/1/2" in x and "flags=8" in x for x in shapes)
          and any("shape=3" in x and "/3/0" in x for x in shapes) and any("shape=4" in x for x in shapes)
          and any("shape=6" in x for x in shapes) and any("shape=7 radius=0 points=0,1700 1133,0 2267,1700 1133,1133" in x for x in shapes),
          "\n".join(shapes))
    check("hand: a group and its objects", any("kind=5 " in x and 'name="Pair"' in x and "box=850,6803,2550,849" in x for x in lines)
          and len([x for x in lines if "parent=10 " in x]) == 2)
    check("hand: a rotated rectangle", any("rot=300" in x and 'name="Tilted"' in x for x in lines))
    kept = [x for x in lines if "kind=6 " in x]
    check("hand: four objects kept, one on the master", len(kept) == 4 and all("keep=0/" in x for x in kept)
          and any('name="Badge"' in x and "fill=FFC000" in x for x in kept), kept)
    check("hand: the chart's picture shown", any('name="Chart"' in x and "image=image/png/116/" in x for x in kept))
    check("hand: a cropped picture", "crop=188,124,188,124" in out and 'alt="A red picture"' in out)
    check("hand: a JPEG", "image=image/jpeg/910/" in out)
    check("hand: a table", "cols=3 widths=3401,1700,1700" in out and "cell=0,0/3 shade=D9E2F3" in out and "cell=2,2/3 shade=-" in out)
    check("hand: kept XML", "kept objects=4 timing=1 styles=3 definitions=2 attributes=1 settings=1 graphic=1 master-styles=1" in out,
          lines_with(lines, "kept "))
    check("hand: the chart's parts", 'part "Object 1/" application/vnd.oasis.opendocument.chart 0' in out and
          'part "Object 1/content.xml" text/xml' in out and 'part "ObjectReplacements/Object 1" image/png 116' in out)
    for note in ("Animations are kept, not played", "Rotated objects are drawn unrotated", "Custom shows are kept",
                 "Embedded charts and objects are kept"):
        check("hand: the report says " + note, note in out)
    return out


def test_odfpy(path):
    rc, out, err = dump(path)
    check("odfpy: read", rc == 0, out[-500:] + err)
    check("odfpy: 4:3, two slides", "size=14400,10800" in out and "slides=2 masters=1" in out, out[:300])
    check("odfpy: master name", 'master 0 "Plain"' in out)
    check("odfpy: title level", 'level title font="Liberation Sans" size=720 flags=1 colour=203040 align=1' in out)
    check("odfpy: text", '"Made with odfpy"' in out and '"bold green"' in out and '"One level in"' in out
          and '"example.org" href="https://example.org/"' in out and '"odfpy\'s notes"' in out)
    check("odfpy: levels", re.search(r'para level=2 .*\n.*"One level in"', out) is not None)
    check("odfpy: shapes, a picture and a table", "shape=0" in out and "shape=2" in out and "shape=5" in out
          and "image=image/png/146/" in out and "cols=3 widths=2267,2267,2267" in out)
    return out


def test_libreoffice(path, hand_out):
    rc, out, err = dump(path)
    check(os.path.basename(path) + ": read", rc == 0, out[-500:] + err)
    # LibreOffice keeps what we wrote by hand: the same slides, objects and text
    for want in ('"OpenPresentation"', '"Phase P1 — the model & ODP"', '"bold red"', '"A third level"',
                 '"DalsinAI" href="https://github.com/DalsinAI"', "transition=fade/0/2000/5000",
                 "transition=push/1/500/-1", "hidden=1", 'smil:type="clockWipe"', "cols=3", "crop=188,124,188,124",
                 'name="Pair"', "rot=300", 'footer="Team 5 & friends"', '"Welcome everyone."', "bullet=80000031"):
        check(os.path.basename(path) + ": has " + want, want in out)
    return out


# ---- Round trips ----

def round_trip(work, name, path, ext):
    first = os.path.join(work, f"{name}-1.{ext}")
    second = os.path.join(work, f"{name}-2.{ext}")
    rc, a, err = dump(path, first)
    check(f"{name}: saved as {ext}", rc == 0 and "save-error" not in a, a[-400:] + err)
    if rc:
        return None
    rc, b, err = dump(first, second)
    check(f"{name}: {ext} read back", rc == 0, b[-400:] + err)
    ma, mb = model(a), model(b)
    def flat(lines):
        return lines
    if ext == "fodp":
        # flat XML holds no package parts: the chart's are reported lost,
        # its picture with them
        def flat(lines):  # noqa: F811
            out = []
            for x in lines:
                if x.startswith("part "):
                    continue
                if " kind=6 " in x:
                    x = re.sub(r" image=\S+ crop=\S+", "", x)
                out.append(x)
            return out
        ma, mb = flat(ma), flat(mb)
        check(f"{name}: flat XML says the parts are lost", "part " not in a or "can't be kept in flat XML" in a, a[-300:])
    diff = [f"- {x}\n+ {y}" for x, y in zip(ma, mb) if x != y]
    check(f"{name}: the same deck through {ext}", ma == mb, (f"{len(ma)} and {len(mb)} lines; " + "\n".join(diff[:6])))
    rc, c, err = dump(second)
    check(f"{name}: {ext} saved again the same", rc == 0 and flat(model(c)) == mb, "")
    if ext == "odp" and have_odfpy():
        ok, why = odf_valid(first)
        check(f"{name}: written ODP is valid ODF", ok, why)
    return first


# ---- PDF ----

def test_pdf(work, name, path, slides, words, absent=()):
    pdf = os.path.join(work, name + ".pdf")
    rc, out, err = run([OWCONVERT, "--report", path, pdf])
    check(f"{name}: to PDF", rc == 0 and os.path.exists(pdf), out + err)
    if rc:
        return
    with open(pdf, "rb") as f:
        data = f.read()
    check(f"{name}: a PDF", data.startswith(b"%PDF-1.4") and data.rstrip().endswith(b"%%EOF"))
    check(f"{name}: a page a slide shown", pdf_pages(data) == slides, f"{pdf_pages(data)} pages, {slides} wanted")
    n = pdf_pages_any(pdf)
    check(f"{name}: pypdf or pdfinfo counts the same pages", n in (-1, slides), f"{n} pages")
    if shutil.which("qpdf"):
        r = subprocess.run(["qpdf", "--check", pdf], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        check(f"{name}: qpdf --check", r.returncode == 0, r.stdout.decode()[-500:])
    text = pdf_text(pdf)
    if text is not None:
        flat = " ".join(text.split())
        for w in words:
            check(f"{name}: PDF has {w!r}", w in flat, flat[:800])
        for w in absent:
            check(f"{name}: PDF leaves out {w!r}", w not in flat)
    else:
        print(f"{name}: no pdftotext or pypdf: the PDF's text not checked")


# ---- Damaged and hostile files ----

def cut_short(work, name, data, step, convert_pdf=False):
    path = os.path.join(work, "cut." + name.rsplit(".", 1)[-1])
    tried = 0
    for n in range(0, len(data), step):
        with open(path, "wb") as f:
            f.write(data[:n])
        rc, out, err = dump(path)
        tried += 1
        if rc not in (0, 10):
            check(f"{name} cut at {n}", False, f"exit {rc}: {(out + err)[-400:]}")
            break
        if convert_pdf and rc == 0:
            rc, out, err = run([OWCONVERT, path, os.path.join(work, "cut.pdf")])
            if rc not in (0, 10):
                check(f"{name} cut at {n} to PDF", False, f"exit {rc}: {(out + err)[-400:]}")
                break
    return tried


def rezip(src_zip, dst, change):
    """A package with content.xml changed, its zip still sound."""
    with zipfile.ZipFile(src_zip) as z, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as w:
        for info in z.infolist():
            data = z.read(info.filename)
            if info.filename == "content.xml":
                data = change(data)
            w.writestr(info, data, compress_type=info.compress_type)
    return dst


def hostile(work, hand, pdf):
    rng = random.Random(41)
    content = zipfile.ZipFile(hand).read("content.xml")
    cases = 0
    # bytes changed at random in the XML
    for k in range(60):
        def flip(d, k=k):
            b = bytearray(d)
            for _ in range(1 + k % 7):
                b[rng.randrange(len(b))] = rng.randrange(256)
            return bytes(b)
        p = rezip(hand, os.path.join(work, "flip.odp"), flip)
        rc, out, err = dump(p)
        cases += 1
        if rc not in (0, 10):
            check(f"damaged content.xml {k}", False, f"exit {rc}: {(out + err)[-400:]}")
            break
        if pdf and rc == 0 and k % 6 == 0:
            rc, out, err = run([OWCONVERT, p, os.path.join(work, "flip.pdf")])
            if rc not in (0, 10):
                check(f"damaged content.xml {k} to PDF", False, f"exit {rc}: {(out + err)[-400:]}")
    # nesting far deeper than any deck: groups, lists, spans
    deep = 20000
    body = (b"<draw:g>" * deep + b"<draw:rect svg:width=\"1cm\" svg:height=\"1cm\"/>" + b"</draw:g>" * deep +
            b"<draw:frame svg:width=\"5cm\" svg:height=\"5cm\"><draw:text-box>" + b"<text:list><text:list-item>" * deep +
            b"<text:p>deep</text:p>" + b"</text:list-item></text:list>" * deep + b"<text:p>" +
            b"<text:span>" * deep + b"x" + b"</text:span>" * deep + b"</text:p></draw:text-box></draw:frame>")

    def nest(d):
        return d.replace(b"<draw:page draw:name=\"page2\"", b"<draw:page draw:name=\"deep\" draw:master-page-name=\"Default\">" +
                         body + b"</draw:page><draw:page draw:name=\"page2\"", 1)
    p = rezip(hand, os.path.join(work, "deep.odp"), nest)
    start = time.time()
    rc, out, err = dump(p)
    cases += 1
    check("deep nesting: an answer", rc in (0, 10) and time.time() - start < 60, f"exit {rc} {(out + err)[-300:]}")
    if pdf and rc == 0:
        rc, out, err = run([OWCONVERT, p, os.path.join(work, "deep.pdf")])
        check("deep nesting to PDF: an answer", rc in (0, 10), f"exit {rc} {(out + err)[-300:]}")
    # more slides than a deck may hold: the rest left out, and said so
    many = b"".join(b'<draw:page draw:master-page-name="Default"/>' for _ in range(2100))

    def lots(d):
        return d.replace(b"<presentation:settings", many + b"<presentation:settings", 1)
    p = rezip(hand, os.path.join(work, "many.odp"), lots)
    rc, out, err = dump(p)
    cases += 1
    check("2100 slides: the first 2000", rc == 0 and "slides=2000" in out and "Slides beyond 2000 were left out" in out,
          out[:300] + err[-300:])
    # styles that are their own parents, and lists and pictures that are not there
    def loops(d):
        d = d.replace(b'style:name="gr1" style:family="graphic" style:parent-style-name="standard"',
                      b'style:name="gr1" style:family="graphic" style:parent-style-name="gr1"')
        d = d.replace(b'text:style-name="L1"', b'text:style-name="NoSuchList"')
        return d.replace(b"Pictures/photo.jpg", b"Pictures/missing.jpg")
    p = rezip(hand, os.path.join(work, "loops.odp"), loops)
    rc, out, err = dump(p)
    cases += 1
    check("style loops and missing parts: read", rc == 0 and "A picture that is not in the file was left out" in out, out[:300] + err)
    # numbers far past any slide: lengths, sizes, points, times
    def huge(d):
        k = [0]

        def length(m):
            k[0] += 1
            return m.group(1) + (b'="-99999999in"' if k[0] % 3 == 0 else b'="99999999.99in"')
        d = re.sub(rb'(svg:(?:x|y|width|height|x1|y1|x2|y2)|fo:(?:font-size|margin-left|margin-right|text-indent|'
                   rb'margin-top|margin-bottom|padding)|style:column-width|svg:stroke-width|draw:corner-radius)="[^"]*"',
                   length, d)
        d = re.sub(rb'draw:points="[^"]*"', b'draw:points="0,0 99999999999,-99999999999 -2147483648,2147483647"', d)
        d = re.sub(rb'svg:viewBox="[^"]*"', b'svg:viewBox="-2147483647 0 1 99999999999"', d)
        d = re.sub(rb'presentation:duration="[^"]*"', b'presentation:duration="PT99999999H99999999M99999999S"', d)
        d = re.sub(rb'text:bullet-relative-size="[^"]*"', b'text:bullet-relative-size="9999999%"', d)
        return d.replace(b'draw:transform="', b'draw:transform="translate(99999999in -99999999in) ', 1)
    p = rezip(hand, os.path.join(work, "huge.odp"), huge)
    rc, out, err = dump(p)
    cases += 1
    check("numbers past any slide: read", rc == 0, f"exit {rc} {(out + err)[-300:]}")
    if pdf and rc == 0:
        rc, out, err = run([OWCONVERT, p, os.path.join(work, "huge.pdf")])
        check("numbers past any slide to PDF", rc in (0, 10), f"exit {rc} {(out + err)[-300:]}")
        rc, out, err = run([OWCONVERT, p, os.path.join(work, "huge-again.odp")])
        check("numbers past any slide saved", rc in (0, 10), f"exit {rc} {(out + err)[-300:]}")
    # not a deck at all
    for what, data in (("empty", b""), ("text", b"hello"), ("a zip of nothing", b"PK\x05\x06" + b"\0" * 18)):
        p = os.path.join(work, "not.odp")
        with open(p, "wb") as f:
            f.write(data)
        rc, out, err = dump(p)
        cases += 1
        check(f"not a deck ({what}): refused", rc == 10 and "error " in out, out + err)
    return cases


def libreoffice_opens(work, odp, slides):
    soffice = shutil.which("soffice") or shutil.which("libreoffice")
    out = os.path.join(work, "lo")
    os.makedirs(out, exist_ok=True)
    subprocess.run([soffice, "--headless", "-env:UserInstallation=file://" + os.path.join(work, "lo-profile"),
                    "--convert-to", "pdf", "--outdir", out, odp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                   timeout=180)
    pdf = os.path.join(out, os.path.splitext(os.path.basename(odp))[0] + ".pdf")
    ok = os.path.exists(pdf)
    check("LibreOffice opens " + os.path.basename(odp), ok)
    if ok:
        n = pdf_pages_any(pdf)
        # LibreOffice's PDF leaves hidden slides out, as ours does
        check("LibreOffice shows its slides: " + os.path.basename(odp), n == slides, f"{n} pages")


def main():
    work = tempfile.mkdtemp(prefix="deck-tests-")
    hand = pack(os.path.join(FIX, "odp-hand"), os.path.join(work, "hand.odp"))
    if have_odfpy():
        ok, why = odf_valid(hand)
        check("the hand-written fixture is valid ODF", ok, why)
    else:
        print("no odfpy: written ODP not checked against ODF's grammar (pip install odfpy)")
    test_hand(work, hand)
    test_odfpy(os.path.join(FIX, "odfpy.odp"))
    test_libreoffice(os.path.join(FIX, "libreoffice.odp"), None)
    test_libreoffice(os.path.join(FIX, "libreoffice.fodp"), None)

    written = {}
    for name, path in (("hand", hand), ("odfpy", os.path.join(FIX, "odfpy.odp")),
                       ("libreoffice", os.path.join(FIX, "libreoffice.odp")),
                       ("libreoffice-flat", os.path.join(FIX, "libreoffice.fodp"))):
        written[name] = round_trip(work, name, path, "odp")
        round_trip(work, name, path, "fodp")
    # OWConvert: to ODP, and a presentation is not a document
    rc, out, err = run([OWCONVERT, hand, os.path.join(work, "owc.odp")])
    check("owconvert: odp to odp", rc == 0 and "5 slide(s)" in out, out + err)
    rc, out, err = run([OWCONVERT, hand, os.path.join(work, "owc.odt")])
    check("owconvert: a presentation is not saved as a document", rc == 10 and "a presentation converts to" in out, out + err)
    rc, out, err = run([OWCONVERT, "--formats"])
    check("owconvert: lists the presentation formats", "Presentations" in out and "odp" in out and "fodp" in out, out)

    pdf = owc_pdf()
    if pdf:
        test_pdf(work, "hand", hand, 4,
                 ["OpenPresentation", "Phase P1", "Team 5 & friends", "25 October 2026", "Bullets and numbers",
                  "A point with bold red", "A third level", "See DalsinAI for more", "1. Open", "a) as ODP", "Rectangle", "Ellipse", "Next", "Phase",
                  "PPTX", "Pictures, a table and a chart"], absent=["Backup (hidden)"])
        test_pdf(work, "odfpy", os.path.join(FIX, "odfpy.odp"), 2, ["Made with odfpy", "Plain and bold green",
                                                                       "One level in", "Box"])
        test_pdf(work, "libreoffice", os.path.join(FIX, "libreoffice.odp"), 4, ["OpenPresentation", "Bullets and numbers"])
        test_pdf(work, "libreoffice-flat", os.path.join(FIX, "libreoffice.fodp"), 4, ["OpenPresentation", "Shapes"])
        if written.get("hand"):
            test_pdf(work, "hand-saved", written["hand"], 4, ["OpenPresentation", "Rectangle"])
    else:
        print("owconvert was built without OpenLayout: the PDF tests were skipped (set OPENLAYOUT_SRC)")

    cases = hostile(work, hand, pdf)
    tried = 0
    for name, path in (("hand.odp", hand), ("odfpy.odp", os.path.join(FIX, "odfpy.odp")),
                       ("libreoffice.fodp", os.path.join(FIX, "libreoffice.fodp"))):
        with open(path, "rb") as f:
            data = f.read()
        tried += cut_short(work, name, data, max(1, len(data) // 400), convert_pdf=pdf and name == "hand.odp")
    print(f"{tried} cut-short and {cases} damaged files tried")

    if os.environ.get("DECK_LIBREOFFICE") == "1" and (shutil.which("soffice") or shutil.which("libreoffice")):
        for name, slides in (("hand", 4), ("odfpy", 2), ("libreoffice", 4)):
            if written.get(name):
                libreoffice_opens(work, written[name], slides)

    print(f"{'FAILED' if failures else 'passed'}: {len(failures)} failure(s); files in {work}")
    if not failures:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
