#!/usr/bin/env python3
"""tests/docs/check_readme_versions.py: the README's and docs/STATUS.md's version of each program match its window's $VER.

Each app/*.c names its program and version in VERSION_TEXT ("GFOpenCalc 0.1.78 (14.11.2026)"). Where README.md
says "<Program> <version> is" (the line that introduces a program), the version must be the one in the source,
so the README can't be left behind when a version moves. A program the README doesn't introduce that way is
passed over. docs/STATUS.md is checked the same way, and each row of its table ("| <Program> | <version> |")
must carry the source's version; every program with a VERSION_TEXT must have a row there.
Prints one line for each mismatch and the count; exits 1 when there is one.
check_readme_versions.py --self-test checks the two readers on their own examples first.
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import glob
import os
import re
import sys

VERSION_TEXT = re.compile(r'#define\s+VERSION_TEXT\s+"(GFOpen[A-Za-z]+)\s+([0-9]+(?:\.[0-9]+)+)')
INTRO = re.compile(r'\b(GFOpen[A-Za-z]+)\s+([0-9]+(?:\.[0-9]+)+)\s+is\b')
ROW = re.compile(r'^\|\s*(GFOpen[A-Za-z]+)\s*\|\s*([0-9]+(?:\.[0-9]+)+)\s*\|')


def source_versions(texts):
    """{program: [versions]} from the VERSION_TEXT lines of the given source texts."""
    out = {}
    for text in texts:
        for prog, ver in VERSION_TEXT.findall(text):
            out.setdefault(prog, []).append(ver)
    return out


def mismatches(readme, versions):
    """(line, program, README version, source versions) for each introduction the sources don't agree with."""
    bad = []
    for n, line in enumerate(readme.splitlines(), 1):
        for prog, ver in INTRO.findall(line):
            have = versions.get(prog)
            if have and ver not in have:
                bad.append((n, prog, ver, have))
    return bad


def table_mismatches(status, versions):
    """(line, program, table version, source versions) for each stale STATUS row, and (0, program, None, source
    versions) for each program with no row."""
    bad = []
    seen = set()
    for n, line in enumerate(status.splitlines(), 1):
        m = ROW.match(line)
        if not m:
            continue
        prog, ver = m.groups()
        seen.add(prog)
        have = versions.get(prog)
        if have and ver not in have:
            bad.append((n, prog, ver, have))
    for prog in sorted(versions):
        if prog not in seen:
            bad.append((0, prog, None, versions[prog]))
    return bad


def self_test():
    src = ['#define VERSION_TEXT "GFOpenCalc 0.1.78 (14.11.2026)"',
           '#define VERSION_TEXT "GFOpenWrite 1.0.7 (25.10.2026)"\n',
           '#define VERSION_TEXT "GFOpenWrite 2.0.35 (12.11.2026)"']
    v = source_versions(src)
    assert v == {"GFOpenCalc": ["0.1.78"], "GFOpenWrite": ["1.0.7", "2.0.35"]}, v
    assert mismatches("GFOpenWrite 2.0.35 is a word processor.", v) == []
    assert mismatches("GFOpenWrite 1.0.7 is the old window.", v) == []
    assert mismatches("x\nGFOpenCalc 0.1.60 is out.", v) == [(2, "GFOpenCalc", "0.1.60", ["0.1.78"])]
    assert mismatches("GFOpenCalc 0.1.60 adds Data > Subtotals.", v) == []
    assert mismatches("GFOpenDraw 0.2.0 is separate.", v) == []
    table = "| Application | Version |\n| --- | --- |\n| GFOpenCalc | 0.1.78 | 80% |\n| GFOpenWrite | 2.0.35 | 85% |"
    assert table_mismatches(table, v) == []
    stale = table.replace("| GFOpenCalc | 0.1.78 |", "| GFOpenCalc | 0.1.60 |")
    assert table_mismatches(stale, v) == [(3, "GFOpenCalc", "0.1.60", ["0.1.78"])]
    missing = "| GFOpenWrite | 2.0.35 | 85% |"
    assert table_mismatches(missing, v) == [(0, "GFOpenCalc", None, ["0.1.78"])]
    print("check_readme_versions self-test: 9 checks passed")


def main(argv):
    if "--self-test" in argv:
        self_test()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    texts = []
    for path in sorted(glob.glob(os.path.join(root, "app", "*.c"))):
        with open(path, encoding="latin-1") as f:
            texts.append(f.read())
    with open(os.path.join(root, "README.md"), encoding="utf-8") as f:
        readme = f.read()
    versions = source_versions(texts)
    if not versions:
        print("check_readme_versions: no VERSION_TEXT found in app/*.c")
        return 1
    bad = [("README.md",) + b for b in mismatches(readme, versions)]
    with open(os.path.join(root, "docs", "STATUS.md"), encoding="utf-8") as f:
        status = f.read()
    bad += [("docs/STATUS.md",) + b for b in mismatches(status, versions)]
    bad += [("docs/STATUS.md",) + b for b in table_mismatches(status, versions)]
    for name, n, prog, ver, have in bad:
        if ver is None:
            print("%s: no row for %s (the source says %s)" % (name, prog, " or ".join(have)))
        else:
            print("%s:%d: %s %s, but the source says %s" % (name, n, prog, ver, " or ".join(have)))
    print("check_readme_versions: %d mismatch%s" % (len(bad), "" if len(bad) == 1 else "es"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
