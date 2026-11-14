#!/usr/bin/env python3
"""tests/arexx/check_rexx_tokens.py: ARexx scripts checked for a string that runs straight into a B or X.

ARexx reads 'text'B as a binary string and 'text'X as a hex string, so a string abutted to a variable named b
or x ('SELECT' a','b) stops the script with Error 8, Unrecognized token, on the Amiga only; on the PC nothing
runs the scripts. Every tests/arexx/*.rexx (or the files named) is read with comments left out and each string
followed at once by B or X (not part of a longer name) must hold only binary or hex digits and blanks.
Prints one line for each bad token and the count; exits 1 when there is one.
check_rexx_tokens.py --self-test checks the scanner on its own examples first.
MIT, Copyright (c) 2026 AmiCradle Team.
"""
import glob
import os
import re
import sys

NAME = re.compile(r"[A-Za-z0-9_.!?$@#]")


def tokens(text):
    """(line, string body, suffix) for each string literal followed at once by B or X as a whole token."""
    out = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
        elif c in "'\"":
            j = i + 1
            body = []
            while j < n:
                if text[j] == c:
                    if j + 1 < n and text[j + 1] == c:   # doubled quote inside the string
                        body.append(c)
                        j += 2
                        continue
                    break
                if text[j] == "\n":
                    line += 1
                body.append(text[j])
                j += 1
            j += 1
            if j < n and text[j] in "bBxX" and not (j + 1 < n and NAME.match(text[j + 1])):
                out.append((line, "".join(body), text[j].upper()))
            i = j
        else:
            i += 1
    return out


def bad_tokens(text):
    bad = []
    for line, body, kind in tokens(text):
        digits = "01" if kind == "B" else "0123456789abcdefABCDEF"
        if not all(ch in digits or ch == " " for ch in body):
            bad.append((line, body, kind))
    return bad


def self_test():
    cases = [
        ("CALL want 'SELECT' a','b, 0", 1),
        ("say 'ff'x '0101'b", 0),
        ("say 'a' || ',' || b", 0),
        ("/* 'x'b in a comment */ say 1", 0),
        ("say ','bx", 0),
        ("say 'it''s'X", 1),
        ("say \"q\"B", 1),
    ]
    fails = 0
    for text, want in cases:
        got = len(bad_tokens(text))
        if got != want:
            print(f"SELF-TEST FAIL {text!r}: {got} bad, want {want}")
            fails += 1
    print(f"rexx tokens self-test: {len(cases)} checks, {fails} failed")
    return fails


def main(argv):
    fails = 0
    if argv and argv[0] == "--self-test":
        fails += self_test()
        argv = argv[1:]
    here = os.path.dirname(os.path.abspath(__file__))
    files = argv or sorted(glob.glob(os.path.join(here, "*.rexx")))
    bad = 0
    for f in files:
        with open(f, encoding="latin-1") as fh:
            for line, body, kind in bad_tokens(fh.read()):
                print(f"{os.path.relpath(f)}:{line}: '{body}'{kind} reads as a {'binary' if kind == 'B' else 'hex'} "
                      f"string (ARexx Error 8): use || to join")
                bad += 1
    print(f"rexx tokens: {len(files)} scripts, {bad} bad")
    return 1 if bad or fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
