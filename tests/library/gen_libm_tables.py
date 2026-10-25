#!/usr/bin/env python3
"""The constants of owf.library's maths (libowf/library/owf_libm.c), worked
out with Python's decimal module at 420 digits (2/pi's table needs 1280
right bits) and split into doubles.

    python3 tests/library/gen_libm_tables.py > /tmp/tables.c

prints the C tables owf_libm.c holds; `--check FILE` instead checks that
FILE (owf_libm.c) holds exactly these, as `make test` runs it.
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import sys
from decimal import Decimal as D, getcontext
from fractions import Fraction

getcontext().prec = 420
EPS = D(10) ** -415


def ln(x):
    """log(x) for x > 0: x = 2^k * m, then 2 atanh((m-1)/(m+1))."""
    x = D(x)
    k = 0
    while x > 2:
        x /= 2
        k += 1
    while x < 1:
        x *= 2
        k -= 1
    s = (x - 1) / (x + 1)
    s2, term, total, n = s * s, s, D(0), 1
    while True:
        add = term / n
        if abs(add) < EPS:
            break
        total += add
        term *= s2
        n += 2
    return 2 * total + k * LN2


def ln2():
    # 2 atanh(1/3)
    s, s2, term, total, n = D(1) / 3, D(1) / 9, D(1) / 3, D(0), 1
    while True:
        add = term / n
        if abs(add) < EPS:
            return 2 * total
        total += add
        term *= s2
        n += 2


def exp(x):
    x = D(x)
    total, term, n = D(1), D(1), 1
    while True:
        term = term * x / n
        if abs(term) < EPS:
            return total
        total += term
        n += 1


def atan(x):
    """atan(x) for 0 <= x <= 1: halve the angle until small, then the series."""
    x = D(x)
    halvings = 0
    while x > D("0.1"):
        x = x / (1 + (1 + x * x).sqrt())
        halvings += 1
    total, term, n, x2 = D(0), x, 1, x * x
    sign = 1
    while True:
        add = term / n
        if abs(add) < EPS:
            break
        total += sign * add
        sign = -sign
        term *= x2
        n += 2
    return total * (2 ** halvings)


def pi():
    return 16 * atan_small(D(1) / 5) - 4 * atan_small(D(1) / 239)


def atan_small(x):
    total, term, n, x2, sign = D(0), x, 1, x * x, 1
    while True:
        add = term / n
        if abs(add) < EPS:
            return total
        total += sign * add
        sign = -sign
        term *= x2
        n += 2


LN2 = ln2()
PI = pi()


def nearest_double(v):
    """The double nearest the Decimal v, as a Python float (exact rounding
    through Fraction)."""
    return float(Fraction(v))


def split(v, bits=53):
    """v as hi + lo: hi the nearest number of `bits` significant bits, lo
    the nearest double to the rest."""
    f = Fraction(v)
    if f == 0:
        return 0.0, 0.0
    e = 0
    a = abs(f)
    while a >= 1:
        a /= 2
        e += 1
    while a < Fraction(1, 2):
        a *= 2
        e -= 1
    scale = Fraction(2) ** (bits - e)
    hi = Fraction(round(f * scale)) / scale
    return float(hi), float(f - hi)


def c(x):
    return float.hex(x)


def tables():
    out = []
    hi, lo = split(LN2, 42)
    out.append("#define LN2_HI %s   /* log(2) to 42 bits: k * LN2_HI is exact */" % c(hi))
    out.append("#define LN2_LO %s" % c(lo))
    hi, lo = split(LN2 / 32, 37)
    out.append("#define LN2_32_HI %s   /* log(2)/32 to 37 bits: N * LN2_32_HI is exact */" % c(hi))
    out.append("#define LN2_32_LO %s" % c(lo))
    out.append("#define INV_LN2_32 %s" % c(nearest_double(32 / LN2)))
    hi, lo = split(PI / 2)
    out.append("#define PIO2_HI %s" % c(hi))
    out.append("#define PIO2_LO %s" % c(lo))
    hi, lo = split(PI)
    out.append("#define PI_HI %s" % c(hi))
    out.append("#define PI_LO %s" % c(lo))
    il10 = 1 / ln(10)
    hi, lo = split(il10)
    out.append("#define INV_LN10_HI %s" % c(hi))
    out.append("#define INV_LN10_LO %s" % c(lo))
    out.append("")
    out.append("/* log(1 + j/32), j = -9..13, as hi and lo */")
    out.append("static const double logc[23][2] = {")
    for j in range(-9, 14):
        hi, lo = split(ln(1 + D(j) / 32)) if j else (0.0, 0.0)
        out.append("    { %s, %s }," % (c(hi), c(lo)))
    out.append("};")
    out.append("")
    out.append("/* 2^(j/32), j = 0..31, as hi and lo */")
    out.append("static const double two_j32[32][2] = {")
    for j in range(32):
        hi, lo = split(exp(LN2 * j / 32))
        out.append("    { %s, %s }," % (c(hi), c(lo)))
    out.append("};")
    out.append("")
    out.append("/* atan(i/16), i = 0..16, as hi and lo */")
    out.append("static const double atanc[17][2] = {")
    for i in range(17):
        hi, lo = split(atan(D(i) / 16)) if i else (0.0, 0.0)
        out.append("    { %s, %s }," % (c(hi), c(lo)))
    out.append("};")
    out.append("")
    # the bits of 2/pi after the binary point, 32 to a word
    words = 40
    v = Fraction(2) / Fraction(PI)
    n = int(v * (Fraction(2) ** (32 * words)))
    out.append("/* 2/pi: its first %d bits after the binary point, 32 to a word */" % (32 * words))
    out.append("static const unsigned long two_over_pi[%d] = {" % words)
    ws = [(n >> (32 * (words - 1 - i))) & 0xFFFFFFFF for i in range(words)]
    for i in range(0, words, 6):
        out.append("    " + " ".join("0x%08XUL," % w for w in ws[i:i + 6]))
    out.append("};")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    text = tables()
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        src = open(sys.argv[2], encoding="utf-8").read()
        start = src.find("/* ---- tables (gen_libm_tables.py) ---- */\n")
        end = src.find("/* ---- end of tables ---- */")
        if start < 0 or end < 0:
            print("gen_libm_tables.py: no tables in %s" % sys.argv[2])
            sys.exit(1)
        held = src[start + len("/* ---- tables (gen_libm_tables.py) ---- */\n"):end]
        if held != text:
            print("gen_libm_tables.py: %s's tables differ from these: run it again" % sys.argv[2])
            sys.exit(1)
        print("gen_libm_tables.py: owf_libm.c's tables are right")
    else:
        sys.stdout.write(text)
