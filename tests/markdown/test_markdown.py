#!/usr/bin/env python3
"""GFOpenWrite's Markdown tests: test_markdown.py path/to/owconvert

Makes its own Markdown, plain text and RTF files from CommonMark's and
GitHub's table rules (no outside samples), converts them with owconvert and
checks what comes back: Markdown in (to text and HTML), Markdown out (from
RTF), Markdown round trips, a .txt file staying plain text, and every
cut-short copy of the sample answering with a document or an error, never a
crash.
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import io
import os
import subprocess
import sys
import tempfile
import zipfile

OWCONVERT = os.path.abspath(sys.argv[1])
failures = []


def check(name, condition, detail=""):
    if not condition:
        failures.append(name)
        print(f"FAIL {name} {detail}")


def run(*args):
    return subprocess.run([OWCONVERT, *args], capture_output=True, timeout=60)


def convert(work, name, ext):
    src = os.path.join(work, name)
    dst = os.path.join(work, name + "." + ext)
    r = run("--report", src, dst)
    check(f"{name}->{ext} exit", r.returncode == 0, (r.stdout + r.stderr).decode("utf-8", "replace"))
    try:
        with open(dst, "rb") as f:
            return f.read(), r.stdout.decode("utf-8", "replace")
    except OSError:
        return b"", ""


SAMPLE = "\n".join([
    "# Title",
    "",
    "Some **bold**, *italic*, __strong__, _em_, ~~struck~~ and `code` text.",
    "A second line of the same paragraph.",
    "",
    "Setext heading",
    "--------------",
    "",
    "## Links &amp; escapes",
    "",
    "See [the site](https://example.org/) or <https://example.net/>.",
    "Not \\*emphasis\\*, a \\# and 2 * 3 * 4.",
    "Line one  ",
    "line two\\",
    "line three",
    "",
    "- apple",
    "- pear",
    "  - small pear",
    "",
    "1. first",
    "2. second",
    "",
    "> Quoted text",
    "> goes on.",
    "",
    "```",
    "int x = 1;",
    "  if (x) *y = 2;",
    "```",
    "",
    "    indented code",
    "",
    "| Name | Count |",
    "| --- | ---: |",
    "| Ann | 3 |",
    "| Bob | 12 |",
    "",
    "***",
    "",
    "Café € end.",
    "",
])

SAMPLE_TEXT = "\n".join([
    "Title",
    "Some bold, italic, strong, em, struck and code text. A second line of the same paragraph.",
    "Setext heading",
    "Links & escapes",
    "See the site or https://example.net/. Not *emphasis*, a # and 2 * 3 * 4. Line one",
    "line two",
    "line three",
    "•\tapple",
    "•\tpear",
    "•\tsmall pear",
    "1.\tfirst",
    "2.\tsecond",
    "Quoted text goes on.",
    "int x = 1;",
    "  if (x) *y = 2;",
    "indented code",
    "Name\tCount",
    "Ann\t3",
    "Bob\t12",
    "",
    "Café € end.",
    "",
])

# What the Markdown writer gives for SAMPLE: the same document, in one way of writing it.
SAMPLE_MD = "\n".join([
    "# Title",
    "",
    "Some **bold**, *italic*, **strong**, *em*, ~~struck~~ and `code` text. A second line of the same paragraph.",
    "",
    "## Setext heading",
    "",
    "## Links & escapes",
    "",
    "See [the site](https://example.org/) or [https://example.net/](https://example.net/). Not \\*emphasis\\*, a "
    "# and 2 \\* 3 \\* 4. Line one\\",
    "line two\\",
    "line three",
    "",
    "- apple",
    "- pear",
    "  - small pear",
    "",
    "1. first",
    "2. second",
    "",
    "> Quoted text goes on.",
    "",
    "```",
    "int x = 1;",
    "  if (x) *y = 2;",
    "indented code",
    "```",
    "",
    "| Name | Count |",
    "| --- | ---: |",
    "| Ann | 3 |",
    "| Bob | 12 |",
    "",
    "---",
    "",
    "Café € end.",
    "",
])

RTF = (br"{\rtf1\ansi\deff0{\fonttbl{\f0 Times New Roman;}{\f1\fmodern Courier New;}}"
       br"\pard\outlinelevel0 Heading\par"
       br"\pard Plain \b bold\b0 , \i italic\i0 , \strike gone\strike0  and {\f1 mono} 1*2_3 [x].\par"
       br"\pard # not a heading\par"
       br"\pard 3. not a list\par"
       br"}")

RTF_MD = "\n".join([
    "# Heading",
    "",
    "Plain **bold**, *italic*, ~~gone~~ and `mono` 1\\*2\\_3 \\[x\\].",
    "",
    "\\# not a heading",
    "",
    "3\\. not a list",
    "",
])


def main():
    with tempfile.TemporaryDirectory() as work:
        def put(name, data):
            with open(os.path.join(work, name), "wb") as f:
                f.write(data)

        put("sample.md", SAMPLE.encode())
        put("sample.markdown", SAMPLE.encode())
        put("plain.txt", b"# Not a heading\n- not a list\n*not italic*\n")
        put("latin1.md", b"# Caf\xe9\n\n*na\xefve*\n")
        put("sample.rtf", RTF)

        text, report = convert(work, "sample.md", "txt")
        check("md text", text == SAMPLE_TEXT.encode(), repr(text))
        check("md report", "Read Markdown" in report, report)
        text2, _ = convert(work, "sample.markdown", "txt")
        check("markdown name", text2 == text, repr(text2))

        html, _ = convert(work, "sample.md", "html")
        h = html.decode("utf-8", "replace")
        for want in ("<h1", ">Title</h1>", "<h2", ">Setext heading</h2>", "<b>bold</b>", "<i>italic</i>",
                     "<b>strong</b>", "<i>em</i>", "<s>struck</s>", "Links &amp; escapes", "<table", ">Bob<", ">12<"):
            check("md html has " + want, want in h, h[:3000])
        check("md html code is monospace", "monospace" in h, h[:3000])
        check("md html quote indented", "margin-left" in h, h[:3000])

        odt, _ = convert(work, "sample.md", "odt")
        with zipfile.ZipFile(io.BytesIO(odt)) as z:
            content = z.read("content.xml").decode()
        for want in ('xlink:href="https://example.org/"', 'xlink:href="https://example.net/"'):
            check("md odt has link " + want, want in content, content[:2000])

        md, report = convert(work, "sample.md", "md")
        check("md out", md == SAMPLE_MD.encode(), "\n" + md.decode("utf-8", "replace"))
        check("md out report", "Saved as Markdown" in report, report)
        put("round.md", md)
        md2, _ = convert(work, "round.md", "md")
        check("md round trip stable", md2 == md, "\n" + md2.decode("utf-8", "replace"))

        out, report = convert(work, "sample.rtf", "md")
        check("rtf to md", out == RTF_MD.encode(), "\n" + out.decode("utf-8", "replace"))
        put("rtf.md", out)
        back, _ = convert(work, "rtf.md", "txt")
        check("rtf md back to text", back == "Heading\nPlain bold, italic, gone and mono 1*2_3 [x].\n"
              "# not a heading\n3. not a list\n".encode(), repr(back))

        plain, report = convert(work, "plain.txt", "txt")
        check("txt stays plain", plain == b"# Not a heading\n- not a list\n*not italic*\n", repr(plain))
        check("txt read as text", "Read as UTF-8 text" in report, report)

        latin, report = convert(work, "latin1.md", "txt")
        check("latin1 md", latin == "Café\nnaïve\n".encode(), repr(latin))
        check("latin1 md report", "ISO-8859-1" in report, report)

        data = SAMPLE.encode()
        for n in range(0, len(data), 3):
            put("cut.md", data[:n])
            r = run(os.path.join(work, "cut.md"), os.path.join(work, "cut.md.md"))
            check(f"cut {n} no crash", r.returncode >= 0 and r.returncode < 20,
                  (r.stdout + r.stderr).decode("utf-8", "replace"))

    if failures:
        print(f"{len(failures)} Markdown checks failed")
        return 1
    print("Markdown: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
