#!/usr/bin/env python3
"""tests/docs/check_write_version.py: GFOpenWrite's about box and $VER give VERSION_TEXT, and it is not behind.

app/openwrite2.c tags each change with the version it shipped in ("/* 2.0.44 */", "2.0.38: ..."). The about box
(about()) and the $VER string must both be built from VERSION_TEXT, and VERSION_TEXT's version must be at least
the highest 2.0.N tag in the source, so a feature can't ship while the about box still names an older release.
Prints one line for each problem and the count; exits 1 when there is one.
check_write_version.py --self-test checks the readers on their own examples first.
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import re
import sys

VERSION_TEXT = re.compile(r'#define\s+VERSION_TEXT\s+"GFOpenWrite\s+(2\.0\.[0-9]+)\s+\([0-9]{1,2}\.[0-9]{1,2}\.[0-9]{4}\)"')
TAG = re.compile(r'\b2\.0\.([0-9]+)\b')
ABOUT = re.compile(r'static void about\(void\)\s*\{(.*?)\n\}', re.S)
VER = re.compile(r'"\$VER: "\s*VERSION_TEXT\b')


def parts(ver):
    return tuple(int(x) for x in ver.split("."))


def problems(src):
    """Problems with the source text's version line, about box and $VER, as strings."""
    out = []
    m = VERSION_TEXT.search(src)
    if not m:
        return ["no VERSION_TEXT of the form \"GFOpenWrite 2.0.N (d.m.yyyy)\""]
    have = m.group(1)
    body = src[:m.start()] + src[m.end():]
    tags = [int(n) for n in TAG.findall(body)]
    if tags and max(tags) > parts(have)[2]:
        out.append("VERSION_TEXT says %s but the source has changes tagged 2.0.%d" % (have, max(tags)))
    a = ABOUT.search(src)
    if not a or "VERSION_TEXT" not in a.group(1):
        out.append("about() does not show VERSION_TEXT")
    if not VER.search(src):
        out.append("$VER is not built from VERSION_TEXT")
    return out


def self_test():
    good = ('#define VERSION_TEXT "GFOpenWrite 2.0.45 (16.11.2026)"\n'
            'static const char ver[] = "$VER: " VERSION_TEXT " MIT";\n'
            '/* 2.0.44 */\nstatic void about(void)\n{\n    tell("GFOpenWrite", VERSION_TEXT "\\n");\n}\n')
    assert problems(good) == [], problems(good)
    behind = good.replace("2.0.45", "2.0.37")
    assert problems(behind) == ["VERSION_TEXT says 2.0.37 but the source has changes tagged 2.0.44"], problems(behind)
    same = good.replace("2.0.45", "2.0.44")
    assert problems(same) == [], problems(same)
    hard = good.replace('VERSION_TEXT "\\n"', '"GFOpenWrite 2.0.37\\n"')
    assert problems(hard) == ["about() does not show VERSION_TEXT"], problems(hard)
    nover = good.replace('"$VER: " VERSION_TEXT', '"$VER: GFOpenWrite 2.0.37"')
    assert problems(nover) == ["$VER is not built from VERSION_TEXT"], problems(nover)
    assert problems("int x;") == ["no VERSION_TEXT of the form \"GFOpenWrite 2.0.N (d.m.yyyy)\""]
    print("check_write_version self-test: 6 checks passed")


def main(argv):
    if "--self-test" in argv:
        self_test()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    with open(os.path.join(root, "app", "openwrite2.c"), encoding="latin-1") as f:
        src = f.read()
    bad = problems(src)
    for p in bad:
        print("app/openwrite2.c: " + p)
    print("check_write_version: %d problem%s" % (len(bad), "" if len(bad) == 1 else "s"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
