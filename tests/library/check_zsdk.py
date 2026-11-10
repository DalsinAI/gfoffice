#!/usr/bin/env python3
"""libowf's z.library SDK (libowf/amiga) against z.library's own jump
table, on the PC (make sfd-check).

    check_zsdk.py [REPO] [--sfd FILE]

GFOffice's LV-426 save Guru (10 November 2026) came from here: z_lib.sfd
held a guessed table that was wrong from -102 on, and every SDK file made
from it agreed with it, so nothing showed the mistake until a save called
InflateSetDictionary in place of DeflateBound. This check compares the SDK
with the table z.library is built from, not with itself:

  - tests/library/fixtures/z.lvo is GFOpenZlib's z.lvo (DalsinAI/
    amicradle-guest libraries/openzlib/z.lvo, 53.13: the Aminet z.library's
    order and registers). Copy it again when z.library adds calls.
  - z_lib.sfd: every call at z.lvo's offset with z.lvo's registers, in
    order from the bias, with nothing skipped.
  - the files made from it (sdk/FD/z_lib.fd, sdk/Include_I/lvo/z_lib.i,
    sdk/Include_H/inline/z.h, sdk/Include_H/pragmas/z_pragmas.h): the
    same offset and registers for every call, so a stale copy of any one
    shows too.
  - every z.library call owf_zlib.h maps zlib's names to is in the table.

--sfd FILE checks another SFD in place of libowf/amiga/z_lib.sfd (and only
it): the test's own test, against the 53.12 SFD that caused LV-426.
Prints what differs; exit status 1 when anything does.
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import os
import re
import sys

args = [a for a in sys.argv[1:]]
sfd_only = None
if "--sfd" in args:
    i = args.index("--sfd")
    sfd_only = args[i + 1]
    del args[i:i + 2]
REPO = os.path.abspath(args[0]) if args else os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
AMIGA = os.path.join(REPO, "libowf", "amiga")
SDK = os.path.join(AMIGA, "sdk")
problems = []


def bad(where, what):
    problems.append(f"{where}: {what}")


def read(path):
    with open(path, encoding="latin-1") as f:
        return f.read()


def regs(text):
    return tuple(r.strip().lower() for r in text.split(",") if r.strip())


# z.library's table: {name: (lvo, registers)}
table = {}
for n, line in enumerate(read(os.path.join(REPO, "tests", "library", "fixtures", "z.lvo")).splitlines(), 1):
    line = line.split("#", 1)[0].strip()
    if not line:
        continue
    parts = line.split()
    table[parts[1]] = (int(parts[0]), regs(parts[2]) if len(parts) > 2 else ())
if len(table) < 40:
    bad("fixtures/z.lvo", f"only {len(table)} calls read")


def check_call(where, name, lvo, r):
    if name not in table:
        bad(where, f"{name} is not a z.library call")
    elif table[name][0] != lvo:
        bad(where, f"{name} at {lvo}, z.library has it at {table[name][0]}")
    elif r is not None and table[name][1] != r:
        bad(where, f"{name} in {','.join(r)}, z.library takes {','.join(table[name][1])}")


def check_sfd(path):
    where = os.path.relpath(path, REPO) if path.startswith(REPO) else path
    bias, lvo, seen = None, None, 0
    for line in read(path).splitlines():
        if line.startswith("==bias"):
            bias = int(line.split()[1])
            lvo = -bias
        elif line.startswith("==reserve"):
            lvo -= 6 * int(line.split()[1])
        elif line.startswith("==") or line.startswith("*") or not line.strip():
            continue
        else:
            m = re.match(r".*?(\w+)\s*\((.*)\)\s*\((.*)\)\s*$", line)
            if not m or lvo is None:
                bad(where, f"can't read {line!r}")
                continue
            check_call(where, m.group(1), lvo, regs(m.group(3)))
            lvo -= 6
            seen += 1
    if bias != 30:
        bad(where, f"bias {bias}, not 30")
    if not seen:
        bad(where, "no calls")
    return seen


if sfd_only:
    check_sfd(os.path.abspath(sfd_only))
else:
    count = check_sfd(os.path.join(AMIGA, "z_lib.sfd"))

    # sdk/FD/z_lib.fd
    lvo = None
    fd = os.path.join(SDK, "FD", "z_lib.fd")
    for line in read(fd).splitlines():
        if line.startswith("##bias"):
            lvo = -int(line.split()[1])
        elif line.startswith("*") or line.startswith("##") or not line.strip():
            continue
        else:
            m = re.match(r"(\w+)\((.*?)\)\((.*?)\)", line)
            if m and lvo is not None:
                check_call("sdk/FD/z_lib.fd", m.group(1), lvo, regs(m.group(3)))
                lvo -= 6

    # sdk/Include_I/lvo/z_lib.i
    found = 0
    for m in re.finditer(r"^_LVO(\w+)\s+EQU\s+(-\d+)", read(os.path.join(SDK, "Include_I", "lvo", "z_lib.i")), re.M):
        check_call("lvo/z_lib.i", m.group(1), int(m.group(2)), None)
        found += 1
    if found != count:
        bad("lvo/z_lib.i", f"{found} offsets, the SFD has {count} calls")

    # sdk/Include_H/inline/z.h: each call's "jsr -N(a6)" and its register variables
    text = read(os.path.join(SDK, "Include_H", "inline", "z.h"))
    found = 0
    for m in re.finditer(r"^__z_(\w+)\(void \*__base[^)]*\)\n\{\n(.*?)^\}", text, re.M | re.S):
        body = m.group(2)
        j = re.search(r'jsr\s+(-\d+)\(%/a6\)', body)
        if not j:
            bad("inline/z.h", f"{m.group(1)} has no jsr")
            continue
        # the arguments: register variables given a value, in the order they are passed (not a6, the base)
        r = tuple(re.findall(r'__asm\("(d[0-7]|a[0-5])"\) = \(', body))
        check_call("inline/z.h", m.group(1), int(j.group(1)), r)
        found += 1
    if found != count:
        bad("inline/z.h", f"{found} calls with jsr offsets, the SFD has {count}")

    # sdk/Include_H/pragmas/z_pragmas.h: "#pragma libcall ZBase Name hexoffset code"
    found = 0
    for m in re.finditer(r"^#pragma\s+libcall\s+\w+\s+(\w+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)", read(os.path.join(SDK, "Include_H", "pragmas", "z_pragmas.h")), re.M):
        code = m.group(3)
        r = tuple(("d%d" % int(c, 16)) if int(c, 16) < 8 else ("a%d" % (int(c, 16) - 8)) for c in code[:-2][::-1]) if len(code) > 2 else ()
        check_call("pragmas/z_pragmas.h", m.group(1), -int(m.group(2), 16), r)
        found += 1
    if found != count:
        bad("pragmas/z_pragmas.h", f"{found} pragmas, the SFD has {count} calls")

    # the calls owf_zlib.h maps zlib's names to
    used = set(re.findall(r"\(int\)(\w+)\(|\(uLong\)(\w+)\(", read(os.path.join(REPO, "libowf", "src", "owf_zlib.h"))))
    used = {a or b for a, b in used}
    if len(used) < 8:
        bad("owf_zlib.h", f"only {len(used)} z.library calls found: {sorted(used)}")
    for name in sorted(used):
        if name not in table:
            bad("owf_zlib.h", f"{name} is not a z.library call")

for p in problems:
    print("FAIL " + p)
if problems:
    print(f"{len(problems)} difference(s) from z.library's table")
    sys.exit(1)
print("z.library SDK: every call where z.library has it" + (" (" + sfd_only + ")" if sfd_only else ""))
