#!/usr/bin/env python3
"""The second batch of the formula engine's functions: test cases with
expected values worked out here, from the functions' definitions (OASIS
OpenFormula, ODF 1.2 part 2) and Excel's documentation of their behaviour,
with Python's math, statistics, fractions, decimal and datetime. No
spreadsheet program's output is used; the examples Excel's documentation
gives are checked against these definitions too.

  fn2_expect.py            prints the C table (tests/calc/fn2_cases.h)
  fn2_expect.py --check F  fails when F is not what it would print

The table has the cells the cases read (fn2_data, typed into Sheet1 to
Sheet3 of the test book; row 3 of Sheet1 hidden) and the cases (formula,
expected, tolerance). Tolerance 0: the result shown as the engine shows
it (General, 15 significant digits) must be the same text. Otherwise the
result must be a number within that relative distance of the expected one
(the iterative functions, the distributions and floating-point matrices).
MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import datetime
import math
import statistics
import sys
from decimal import Decimal, ROUND_HALF_UP, getcontext
from fractions import Fraction as F

getcontext().prec = 60

# ---- What the engine shows ----


class Err(str):
    pass


NUM, VALUE, DIV0, NA, REF, NAME, NULL = (Err(e) for e in ("#NUM!", "#VALUE!", "#DIV/0!", "#N/A", "#REF!", "#NAME?", "#NULL!"))


def xl_num(x):
    """A double as the engine's General shows it: 15 significant digits,
    halves up, scientific from 1E+15 and below 1E-9."""
    try:
        x = float(x)
    except OverflowError:                # past a double: the engine's #NUM!
        return "#NUM!"
    if x != x or math.isinf(x):
        return "#NUM!"
    if x == 0:
        return "0"
    d = Decimal(x)
    sign = "-" if d < 0 else ""
    d = abs(d)
    e = d.adjusted()
    q = d.scaleb(-e).quantize(Decimal("1.00000000000000"), rounding=ROUND_HALF_UP)
    if q >= 10:
        e += 1
        q = (q / 10).quantize(Decimal("1.00000000000000"), rounding=ROUND_HALF_UP)
    digits = str(q).replace(".", "").rstrip("0") or "0"
    if e >= 15 or e < -9:
        s = digits[0] + ("." + digits[1:] if len(digits) > 1 else "") + "E" + ("-" if e < 0 else "+") + "%02d" % abs(e)
    elif e >= 0:
        s = digits[: e + 1].ljust(e + 1, "0") + ("." + digits[e + 1:] if len(digits) > e + 1 else "")
    else:
        s = "0." + "0" * (-e - 1) + digits
    return sign + s


def show(v):
    if isinstance(v, Err):
        return str(v)
    if isinstance(v, bool):
        return "TRUE" if v else "FALSE"
    if isinstance(v, str):
        return '"' + v + '"'
    return xl_num(v)


CASES = []


def case(formula, value, tol=0.0):
    """tol 0: the shown text; else a number within tol (relative)."""
    if tol and not isinstance(value, (Err, str, bool)):
        CASES.append((formula, repr(float(value)), tol))
    else:
        CASES.append((formula, show(value), 0.0))


def D(s):
    return Decimal(str(s))


def half_up(d, places=0):
    """Decimal d to `places` decimals, halves away from zero."""
    q = Decimal(1).scaleb(-places)
    return d.quantize(q, rounding=ROUND_HALF_UP)


def fsqrt(fr):
    """sqrt of a Fraction, correctly rounded to a double (through Decimal)."""
    return float(Decimal(fr.numerator).sqrt() / Decimal(fr.denominator).sqrt()) if fr else 0.0


# ---- The test book ----

DATA = []      # (sheet, cell, typed input)
HIDDEN_ROWS = [3]  # Sheet1's row 3 is hidden (SUBTOTAL 101-111)


def put(cell, text, sheet=0):
    DATA.append((sheet, cell, text))


A = [4, 8, 15, 16, 23, 42, 8, 4, 8, 1]                 # statistics
B = list(range(1, 11))                                 # x
C = [F(s) for s in ("2.5", "4.1", "6.2", "7.9", "10.3", "11.8", "14.2", "15.9", "18.1", "20.4")]   # y
for i in range(10):
    put("A%d" % (i + 1), str(A[i]))
    put("B%d" % (i + 1), str(B[i]))
    put("C%d" % (i + 1), str(float(C[i])))
put("D1", "apple")
put("D2", "TRUE")
put("D3", '=""')
put("D5", "7")
put("D6", "3")                                         # D4 empty
E = [-10000, 2000, 3000, 4000, 5000, 1000]
FD = [datetime.date(2026, 1, 1), datetime.date(2026, 3, 1), datetime.date(2026, 10, 30), datetime.date(2027, 2, 15),
      datetime.date(2027, 4, 1), datetime.date(2027, 12, 31)]
HOL = [datetime.date(2026, 12, 25), datetime.date(2026, 12, 26), datetime.date(2027, 1, 1), datetime.date(2026, 12, 25)]
for i in range(6):
    put("E%d" % (i + 1), str(E[i]))
    put("F%d" % (i + 1), FD[i].isoformat())
for i, h in enumerate(HOL):
    put("G%d" % (i + 1), h.isoformat())
put("H1", "a")
put("H2", "b")
put("H4", "d")
put("H5", "5")                                         # H3 empty
for i, (k, w, x) in enumerate(((10, "ten", "1.5"), (20, "twenty", "2.5"), (30, "thirty", "3.5"), (40, "forty", "4.5"))):
    put("I%d" % (i + 1), str(k))
    put("J%d" % (i + 1), w)
    put("K%d" % (i + 1), x)
put("L1", "=SUM(A1:A3)")
put("L2", "5")
put("L3", "=SUBTOTAL(9,A1:A3)")
put("L4", "=1/0")
put("M1", "3")
put("M2", "#N/A")
put("M3", "5")
put("A1", "1000", 1)
put("B2", "2000", 1)
put("A1", "5", 2)

EPOCH = datetime.date(1899, 12, 30)


def serial(d):
    return (d - EPOCH).days


def date_of(s):
    return EPOCH + datetime.timedelta(days=s)


def DATE(y, m, d):
    return "DATE(%d,%d,%d)" % (y, m, d)


# ---- Maths ----

def mround(x, m):
    x, m = D(x), D(m)
    if m == 0 or x == 0:
        return 0
    if (x > 0) != (m > 0):
        return NUM
    return half_up(x / m) * m


for x, m in (("10", "3"), ("-10", "-3"), ("1.3", "0.2"), ("5", "-2"), ("7.5", "5"), ("6", "0"), ("2.5", "1")):
    case("MROUND(%s,%s)" % (x, m), mround(x, m))


def ceil_floor_math(x, s=1, mode=0, up=True):
    x, s = F(x), abs(F(s))
    if s == 0 or x == 0:
        return 0
    q = abs(x) / s
    if x > 0:
        return (math.ceil(q) if up else math.floor(q)) * s
    away = (mode != 0) if up else (mode == 0)
    return -(math.ceil(q) if away else math.floor(q)) * s


# Excel's documentation's examples for CEILING.MATH and FLOOR.MATH
for args, want_c, want_f in ((("24.3", "5"), 25, 20), (("6.7",), 7, 6), (("-8.1", "2"), -8, -10),
                             (("-5.5", "2", "-1"), -6, -4)):
    a = [F(v) for v in args]
    assert ceil_floor_math(*a) == want_c and ceil_floor_math(*a, up=False) == want_f
    case("CEILING.MATH(%s)" % ",".join(args), ceil_floor_math(*a))
    case("FLOOR.MATH(%s)" % ",".join(args), ceil_floor_math(*a, up=False))
case("CEILING.MATH(-5.5,-2)", ceil_floor_math(F("-5.5"), F(-2)))
case("FLOOR.MATH(7,0)", 0)
case("CEILING.MATH(\"x\")", VALUE)


def precise(x, s=1, up=True):
    x, s = F(x), abs(F(s))
    if s == 0 or x == 0:
        return 0
    return (math.ceil(x / s) if up else math.floor(x / s)) * s


for args in (("4.3",), ("-4.3",), ("4.3", "2"), ("4.3", "-2"), ("-4.3", "2"), ("-4.3", "-2")):
    a = [F(v) for v in args]
    case("CEILING.PRECISE(%s)" % ",".join(args), precise(*a))
    case("ISO.CEILING(%s)" % ",".join(args), precise(*a))
for args in (("-3.2", "-1"), ("3.2", "1"), ("-3.2", "1"), ("3.2", "-1"), ("3.2",), ("5", "0")):
    case("FLOOR.PRECISE(%s)" % ",".join(args), precise(*[F(v) for v in args], up=False))
assert [precise(F("4.3")), precise(F("-4.3")), precise(F("4.3"), 2), precise(F("-4.3"), -2)] == [5, -4, 6, -4]


def quotient(a, b):
    if b == 0:
        return DIV0
    q = F(a) / F(b)
    return math.floor(q) if q >= 0 else -math.floor(-q)


for a, b in (("5", "2"), ("4.5", "3.1"), ("-10", "3"), ("1", "0")):
    case("QUOTIENT(%s,%s)" % (a, b), quotient(F(a), F(b)))
assert quotient(5, 2) == 2 and quotient(F("4.5"), F("3.1")) == 1 and quotient(-10, 3) == -3

case("GCD(5,2)", 1)
case("GCD(24,36)", 12)
case("GCD(7,1)", 1)
case("GCD(5,0)", 5)
case("GCD(24.9,36)", 12)
case("GCD(-1,2)", NUM)
case("GCD(A1:A10)", math.gcd(*A))
case("GCD(I1:I4)", 10)
case("LCM(5,2)", 10)
case("LCM(24,36)", 72)
case("LCM(I1:I4)", math.lcm(10, 20, 30, 40))
case("LCM(0,5)", 0)
case("LCM(-1,2)", NUM)
case("LCM(2^20,3^12)", 2 ** 20 * 3 ** 12)
case("LCM(2^40,3^20)", NUM)              # the engine's rule: a result past 2^53 (not all whole numbers are doubles)


def factdouble(n):
    n = int(n)
    if n < 0:
        return NUM
    p = 1
    while n > 1:
        p *= n
        n -= 2
    return p


for n in ("6", "7", "0", "1", "-1", "5.9", "30"):
    case("FACTDOUBLE(%s)" % n, factdouble(float(n)))


def combin(n, k):
    n, k = int(float(n)), int(float(k))
    if n < 0 or k < 0 or n < k:
        return NUM
    return math.comb(n, k)


for n, k in (("8", "2"), ("10", "0"), ("5", "6"), ("-1", "1"), ("60", "30"), ("8.9", "2.1"), ("1030", "515")):
    case("COMBIN(%s,%s)" % (n, k), combin(n, k))


def combina(n, k):
    n, k = int(float(n)), int(float(k))
    if n < 0 or k < 0 or (n == 0 and k > 0):
        return NUM
    return 1 if k == 0 else math.comb(n + k - 1, k)


for n, k in (("4", "3"), ("10", "3"), ("-1", "2"), ("3", "0")):
    case("COMBINA(%s,%s)" % (n, k), combina(n, k))


def permut(n, k):
    n, k = int(float(n)), int(float(k))
    if n <= 0 or k < 0 or n < k:
        return NUM
    return math.perm(n, k)


for n, k in (("100", "3"), ("3", "2"), ("3", "4"), ("0", "0"), ("5", "0"), ("171", "171")):
    case("PERMUT(%s,%s)" % (n, k), permut(n, k))
for n, k, want in (("3", "2", 9), ("2", "2", 4), ("-1", "2", NUM), ("5", "0", 1)):
    case("PERMUTATIONA(%s,%s)" % (n, k), want)

X7, Y7 = [2, 3, 9, 1, 8, 7, 5], [6, 5, 11, 7, 5, 4, 4]
arr = lambda v: "{" + ",".join(str(x) for x in v) + "}"
for name, f in (("SUMX2MY2", lambda x, y: x * x - y * y), ("SUMX2PY2", lambda x, y: x * x + y * y),
                ("SUMXMY2", lambda x, y: (x - y) ** 2)):
    case("%s(%s,%s)" % (name, arr(X7), arr(Y7)), sum(f(x, y) for x, y in zip(X7, Y7)))
    case("%s(B1:B10,C1:C10)" % name, sum(f(F(x), y) for x, y in zip(B, C)))
    case("%s({1,2},{1,2,3})" % name, NA)
    case("%s({1,\"a\",3},{4,5,6})" % name, f(1, 4) + f(3, 6))
assert sum(x * x - y * y for x, y in zip(X7, Y7)) == -55 and sum((x - y) ** 2 for x, y in zip(X7, Y7)) == 79
case("SERIESSUM(2,1,2,{1,2,3})", 1 * 2 ** 1 + 2 * 2 ** 3 + 3 * 2 ** 5)
case("SERIESSUM(0.5,0,1,{1,1,1,1})", F(15, 8))
case("SERIESSUM(2,1,1,{1,\"a\"})", VALUE)
case("SERIESSUM(\"x\",1,1,{1})", VALUE)
case("MULTINOMIAL(2,3,4)", math.factorial(9) // (2 * 6 * 24))
case("MULTINOMIAL(1,-1)", NUM)
case("MULTINOMIAL(5)", 1)
case("MULTINOMIAL(B1:B3)", math.factorial(6) // (1 * 2 * 6))


def base(n, radix, length=0):
    n, radix, length = int(n), int(radix), int(length)
    if n < 0 or n >= 2 ** 53 or not 2 <= radix <= 36 or not 0 <= length <= 255:
        return NUM
    s = ""
    while True:
        s = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[n % radix] + s
        n //= radix
        if not n:
            break
    return s.rjust(length, "0")


for args in ((7, 2), (100, 16), (15, 2, 10), (-1, 2), (10, 37), (255, 16), (0, 2), (35, 36)):
    case("BASE(%s)" % ",".join(str(a) for a in args), base(*args))
assert base(15, 2, 10) == "0000001111"
for text, radix in (('"FF"', 16), ("111", 2), ('"zap"', 36), ('"12"', 2), ('"ff"', 16), ('"10"', 1)):
    t = text.strip('"')
    try:
        want = int(t, radix) if 2 <= radix <= 36 else NUM
    except ValueError:
        want = NUM
    case("DECIMAL(%s,%d)" % (text, radix), want)
assert int("zap", 36) == 45745

ROMAN_TABLE = [(1000, 0, "M"), (999, 4, "IM"), (995, 3, "VM"), (990, 2, "XM"), (950, 1, "LM"), (900, 0, "CM"),
               (500, 0, "D"), (499, 4, "ID"), (495, 3, "VD"), (490, 2, "XD"), (450, 1, "LD"), (400, 0, "CD"),
               (100, 0, "C"), (99, 2, "IC"), (95, 1, "VC"), (90, 0, "XC"), (50, 0, "L"), (49, 2, "IL"),
               (45, 1, "VL"), (40, 0, "XL"), (10, 0, "X"), (9, 0, "IX"), (5, 0, "V"), (4, 0, "IV"), (1, 0, "I")]


def roman(n, form=0):
    if not 0 <= n <= 3999 or not 0 <= form <= 4:
        return VALUE
    s = ""
    for v, f, t in ROMAN_TABLE:
        while f <= form and n >= v:
            s += t
            n -= v
    return s


# Excel's documentation: 499 in its five forms
assert [roman(499, f) for f in range(5)] == ["CDXCIX", "LDVLIV", "XDIX", "VDIV", "ID"]
for f in range(5):
    case("ROMAN(499,%d)" % f, roman(499, f))
case("ROMAN(2026)", "MMXXVI")
case("ROMAN(3999)", "MMMCMXCIX")
case("ROMAN(0)", "")
case("ROMAN(4000)", VALUE)
case("ROMAN(-1)", VALUE)
case("ROMAN(499,TRUE)", "CDXCIX")
case("ROMAN(499,FALSE)", "ID")
case("ROMAN(999,1)", roman(999, 1))
case("ROMAN(1,5)", VALUE)


def arabic(s):
    s = s.strip(" ")
    neg = s.startswith("-")
    if neg:
        s = s[1:]
    vals = {"I": 1, "V": 5, "X": 10, "L": 50, "C": 100, "D": 500, "M": 1000}
    try:
        v = [vals[c] for c in s.upper()]
    except KeyError:
        return VALUE
    total = sum(-a if i + 1 < len(v) and a < v[i + 1] else a for i, a in enumerate(v))
    return -total if neg else total


for s in ("LVII", "mcmxii", "", "-MMXI", "abc", " MMXXVI ", "MCMXCIX"):
    case('ARABIC("%s")' % s, arabic(s))
case("AND(RANDBETWEEN(1,6)>=1,RANDBETWEEN(1,6)<=6)", True)
case("RANDBETWEEN(5,5)", 5)
case("RANDBETWEEN(3,2)", NUM)
case("MOD(RANDBETWEEN(-3,3),1)", 0)
case("RANDBETWEEN(1.5,2.5)", 2)
for x in ("1", "2", "0", "-1"):
    case("SQRTPI(%s)" % x, math.sqrt(float(x) * math.pi) if float(x) >= 0 else NUM, 1e-15)
for x in ("1", "10", "0.5", "1E+9"):
    case("ACOSH(%s)" % x, math.acosh(float(x)) if float(x) >= 1 else NUM, 1e-14)
for x in ("-2.5", "10", "1E-10", "0", "1E+10"):
    case("ASINH(%s)" % x, math.asinh(float(x)), 1e-14)
for x in ("0.76159416", "-0.1", "1E-12", "1", "-1.5"):
    case("ATANH(%s)" % x, math.atanh(float(x)) if abs(float(x)) < 1 else NUM, 1e-14)

# SUBTOTAL: row 3 of Sheet1 is hidden
SEEN = [a for i, a in enumerate(A) if i + 1 not in HIDDEN_ROWS]
case("SUBTOTAL(9,A1:A10)", sum(A))
case("SUBTOTAL(109,A1:A10)", sum(SEEN))
case("SUBTOTAL(1,A1:A10)", F(sum(A), 10))
case("SUBTOTAL(101,A1:A10)", F(sum(SEEN), len(SEEN)))
case("SUBTOTAL(2,A1:A10)", 10)
case("SUBTOTAL(102,A1:A10)", 9)
case("SUBTOTAL(3,D1:D6)", 5)
case("SUBTOTAL(4,A1:A10)", 42)
case("SUBTOTAL(5,A1:A10)", 1)
case("SUBTOTAL(105,A1:A3)", 4)
case("SUBTOTAL(6,A1:A5)", 4 * 8 * 15 * 16 * 23)
case("SUBTOTAL(7,A1:A10)", statistics.stdev([F(a) for a in A]), 1e-14)
case("SUBTOTAL(8,A1:A10)", statistics.pstdev([F(a) for a in A]), 1e-14)
case("SUBTOTAL(10,A1:A10)", statistics.variance([F(a) for a in A]))
case("SUBTOTAL(11,A1:A10)", statistics.pvariance([F(a) for a in A]))
case("SUBTOTAL(110,A1:A10)", statistics.variance([F(a) for a in SEEN]))
case("SUBTOTAL(9,L1:L3)", 4 + 8 + 15 + 5)
case("SUBTOTAL(9,A1:A2,B1:B2)", 4 + 8 + 1 + 2)
case("SUBTOTAL(9,L1:L4)", DIV0)
case("SUBTOTAL(12,A1)", VALUE)
case("SUBTOTAL(9,1)", VALUE)
case("SUBTOTAL(3,L1:L4)", 3)             # L3 is a SUBTOTAL: not counted

case("INDEX(MMULT({1,2;3,4},{5,6;7,8}),2,2)", 3 * 6 + 4 * 8)
case("INDEX(MMULT({1,2;3,4},{5,6;7,8}),1,1)", 1 * 5 + 2 * 7)
case("SUM(MMULT({1,2;3,4},{5;6}))", 17 + 39)
case("MMULT({1,2},{1,2})", VALUE)
case("MMULT({1,\"a\"},{1;2})", VALUE)
case("ROWS(MMULT(B1:B3,{1,2}))", 3)


def det(m):
    m = [[F(x) for x in row] for row in m]
    n, d = len(m), F(1)
    for c in range(n):
        p = next((r for r in range(c, n) if m[r][c] != 0), None)
        if p is None:
            return F(0)
        if p != c:
            m[c], m[p] = m[p], m[c]
            d = -d
        d *= m[c][c]
        for r in range(c + 1, n):
            f = m[r][c] / m[c][c]
            for k in range(c, n):
                m[r][k] -= f * m[c][k]
    return d


def mat(m):
    return "{" + ";".join(",".join(str(x) for x in row) for row in m) + "}"


for m in ([[3, 6], [1, 1]], [[1, 3, 8, 5], [1, 3, 6, 1], [1, 1, 1, 0], [7, 3, 10, 2]], [[3, 6, 1], [1, 1, 0], [3, 10, 2]]):
    case("MDETERM(%s)" % mat(m), det(m), 1e-14)
assert det([[1, 3, 8, 5], [1, 3, 6, 1], [1, 1, 1, 0], [7, 3, 10, 2]]) == 88
case("MDETERM({1,2,3})", VALUE)
case("MDETERM({1,2;\"a\",4})", VALUE)


def inverse(m):
    n = len(m)
    a = [[F(x) for x in row] + [F(int(i == j)) for j in range(n)] for i, row in enumerate(m)]
    for c in range(n):
        p = next((r for r in range(c, n) if a[r][c] != 0), None)
        if p is None:
            return None
        a[c], a[p] = a[p], a[c]
        pv = a[c][c]
        a[c] = [x / pv for x in a[c]]
        for r in range(n):
            if r != c and a[r][c]:
                f = a[r][c]
                a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [row[n:] for row in a]


for m, i, j in (([[4, -1], [2, 0]], 1, 2), ([[4, -1], [2, 0]], 2, 1), ([[1, 2, 3], [0, 1, 4], [5, 6, 0]], 1, 1),
                ([[1, 2, 3], [0, 1, 4], [5, 6, 0]], 3, 2)):
    case("INDEX(MINVERSE(%s),%d,%d)" % (mat(m), i, j), inverse(m)[i - 1][j - 1], 1e-13)
case("MINVERSE({1,2;2,4})", NUM)
case("MINVERSE({1,2})", VALUE)

# ---- Statistics ----

AF = [F(a) for a in A]
case("MAXIFS(A1:A10,B1:B10,\">5\")", max(A[5:]))
case("MAXIFS(A1:A10,B1:B10,\"<=3\")", max(A[:3]))
case("MAXIFS(A1:A10,B1:B10,\">20\")", 0)
case("MAXIFS(A1:A10,B1:B5,\">1\")", VALUE)
case("MAXIFS(C1:C10,A1:A10,8,B1:B10,\"<9\")", max(C[i] for i in range(10) if A[i] == 8 and B[i] < 9))
case("MINIFS(A1:A10,B1:B10,\">5\")", min(A[5:]))
case("MINIFS(C1:C10,A1:A10,8)", min(C[i] for i in range(10) if A[i] == 8))
case("MINIFS(A1:A10,J1:J10,\"t*\")", min(A[i] for i in range(4) if ("ten", "twenty", "thirty", "forty")[i].startswith("t")))


def mode(v):
    counts = {}
    for x in v:
        counts[x] = counts.get(x, 0) + 1
    best = max(counts.values()) if counts else 0
    if best < 2:
        return NA
    return next(x for x in v if counts[x] == best)


case("MODE(A1:A10)", mode(A))
case("MODE.SNGL(A1:A10)", mode(A))
case("MODE({1,2,3})", NA)
case("MODE(1,2,2,3,3)", mode([1, 2, 2, 3, 3]))
case("MODE(D1:D4)", NA)
case("MODE(A1:A10,M2)", NA)
assert statistics.multimode([1, 2, 2, 3, 3, 1]) == [1, 2, 3]
case("MODE.MULT({1,2,2,3,3})", 2)
case("INDEX(MODE.MULT({1,2,2,3,3,1}),3)", statistics.multimode([1, 2, 2, 3, 3, 1])[2])
case("SUM(MODE.MULT({1,1,2,2}))", 3)
case("ROWS(MODE.MULT({1,2,2,3,3}))", 2)


def rank(x, v, order=0, avg=False):
    better = sum(1 for a in v if (a > x if order == 0 else a < x))
    same = sum(1 for a in v if a == x)
    if not same:
        return NA
    return F(2 * better + same + 1, 2) if avg else better + 1


for f, x, order, avg in (("RANK(8,A1:A10)", 8, 0, False), ("RANK.EQ(8,A1:A10,1)", 8, 1, False),
                         ("RANK.AVG(8,A1:A10)", 8, 0, True), ("RANK.AVG(4,A1:A10,1)", 4, 1, True),
                         ("RANK(99,A1:A10)", 99, 0, False), ("RANK(42,A1:A10)", 42, 0, False),
                         ("RANK.EQ(1,A1:A10)", 1, 0, False), ("RANK(3,{1,3,3,5},1)", 3, 1, False)):
    case(f, rank(x, A if "A1" in f else [1, 3, 3, 5], order, avg))
case("RANK(1,5)", VALUE)


def percentile(v, k, exc=False):
    v = sorted(F(x) for x in v)
    k = F(k)
    n = len(v)
    if not n or (k <= 0 or k >= 1 if exc else k < 0 or k > 1):
        return NUM
    h = (n + 1) * k - 1 if exc else (n - 1) * k
    if h < 0 or h > n - 1:
        return NUM
    i = math.floor(h)
    return v[i] if h == i else v[i] + (h - i) * (v[i + 1] - v[i])


for f, k, exc in (("PERCENTILE.INC(A1:A10,0.3)", "0.3", False), ("PERCENTILE(A1:A10,1)", "1", False),
                  ("PERCENTILE(A1:A10,1.1)", "1.1", False), ("PERCENTILE.INC({1,2,3,4},0.25)", "0.25", False),
                  ("PERCENTILE.EXC(A1:A10,0.3)", "0.3", True), ("PERCENTILE.EXC(A1:A10,0.05)", "0.05", True),
                  ("PERCENTILE.EXC({1,2,3,4},0.5)", "0.5", True), ("PERCENTILE.INC(A1:A10,0)", "0", False),
                  ("PERCENTILE.EXC(A1:A10,1)", "1", True)):
    case(f, percentile([1, 2, 3, 4] if "{" in f else A, F(k), exc))
case("PERCENTILE(D1:D4,0.5)", NUM)
assert percentile([1, 2, 3, 4], F(1, 2), True) == F(5, 2)
assert statistics.quantiles(A, n=4, method="inclusive") == [float(percentile(A, F(q, 4))) for q in (1, 2, 3)]
assert statistics.quantiles(A, n=4, method="exclusive") == [float(percentile(A, F(q, 4), True)) for q in (1, 2, 3)]
for q in (0, 1, 2, 3, 4, 5, -1):
    case("QUARTILE(A1:A10,%d)" % q, percentile(A, F(q, 4)) if 0 <= q <= 4 else NUM)
    case("QUARTILE.EXC(A1:A10,%d)" % q, percentile(A, F(q, 4), True) if 0 < q < 4 else NUM)
case("QUARTILE.INC(A1:A10,3.9)", percentile(A, F(3, 4)))


def percentrank(v, x, sig=3, exc=False):
    v = sorted(F(a) for a in v)
    x = F(x)
    n = len(v)
    if sig < 1 or not n:
        return NUM
    if x < v[0] or x > v[-1]:
        return NA

    def rk(y):
        below = sum(1 for a in v if a < y)
        return F(below + 1, n + 1) if exc else F(below, n - 1)

    if x in v:
        r = F(1) if n == 1 else rk(x)
    else:
        a = max(t for t in v if t < x)
        b = min(t for t in v if t > x)
        r = rk(a) + (x - a) / (b - a) * (rk(b) - rk(a))
    p = 10 ** sig
    return F(math.floor(r * p), p)


PR = [13, 12, 11, 8, 4, 3, 2, 1, 1, 1]
PE = [1, 2, 3, 6, 6, 6, 7, 8, 9]
assert [percentrank(PR, x) for x in (2, 4, 8, 5)] == [F("0.333"), F("0.555"), F("0.666"), F("0.583")]
assert [percentrank(PE, x, 3, True) for x in (7, F("5.43"))] + [percentrank(PE, F("5.43"), 1, True)] == [F("0.7"), F("0.381"), F("0.3")]
for x in ("2", "4", "8", "5", "0", "14"):
    case("PERCENTRANK.INC(%s,%s)" % (arr(PR), x), percentrank(PR, F(x)))
case("PERCENTRANK(%s,8)" % arr(PR), percentrank(PR, 8))
case("PERCENTRANK({1,2,3,4},3,2)", percentrank([1, 2, 3, 4], 3, 2))
case("PERCENTRANK.INC({1,2,3},2,0)", NUM)
case("PERCENTRANK.INC({5},5)", 1)
for x, sig in (("7", 3), ("5.43", 3), ("5.43", 1), ("10", 3), ("1", 3)):
    case("PERCENTRANK.EXC(%s,%s,%d)" % (arr(PE), x, sig), percentrank(PE, F(x), sig, True))

case("STDEV.S(A1:A10)", statistics.stdev(AF), 1e-14)
case("STDEV.P(A1:A10)", statistics.pstdev(AF), 1e-14)
case("VAR.S(A1:A10)", statistics.variance(AF))
case("VAR.P(A1:A10)", statistics.pvariance(AF))
case("STDEV.S(1)", DIV0)
case("VAR.P(D1)", DIV0)
DA = [F(0), F(1), F(0), F(7), F(3)]                    # D1:D6 as the "A" functions take it
case("AVERAGEA(D1:D6)", sum(DA) / len(DA))
case("MAXA(D1:D6)", 7)
case("MINA(D1:D6)", 0)
case("MAXA(D2)", 1)
case("MINA(D5,-1)", -1)
case("MAXA(D4)", 0)
case("AVERAGEA(D4)", DIV0)
case("AVERAGEA(1,\"x\")", VALUE)
case("AVERAGEA(1,\"3\",TRUE)", F(5, 3))
case("AVERAGEA({1,\"a\",TRUE})", F(2, 3))
case("STDEVA(D1:D6)", statistics.stdev(DA), 1e-14)
case("STDEVPA(D1:D6)", statistics.pstdev(DA), 1e-14)
case("VARA(D1:D6)", statistics.variance(DA))
case("VARPA(D1:D6)", statistics.pvariance(DA))
case("VARA(D1)", DIV0)
case("STDEVA(A1:A3,M2)", NA)


def pairs(ys, xs):
    p = [(F(x), F(y)) for x, y in zip(xs, ys)]
    n = len(p)
    mx = sum(x for x, _ in p) / n
    my = sum(y for _, y in p) / n
    sxx = sum((x - mx) ** 2 for x, _ in p)
    syy = sum((y - my) ** 2 for _, y in p)
    sxy = sum((x - mx) * (y - my) for x, y in p)
    return n, mx, my, sxx, syy, sxy


def correl(ys, xs):
    n, mx, my, sxx, syy, sxy = pairs(ys, xs)
    return sxy / F(fsqrt(sxx * syy))


n, mx, my, sxx, syy, sxy = pairs(C, B)
case("CORREL(B1:B10,C1:C10)", correl(C, B), 1e-13)
case("PEARSON(B1:B10,C1:C10)", correl(C, B), 1e-13)
case("RSQ(C1:C10,B1:B10)", sxy * sxy / (sxx * syy), 1e-13)
case("SLOPE(C1:C10,B1:B10)", sxy / sxx, 1e-13)
case("INTERCEPT(C1:C10,B1:B10)", my - sxy / sxx * mx, 1e-13)
case("FORECAST(11,C1:C10,B1:B10)", my + sxy / sxx * (11 - mx), 1e-13)
case("FORECAST.LINEAR(11,C1:C10,B1:B10)", my + sxy / sxx * (11 - mx), 1e-13)
case("COVAR(B1:B10,C1:C10)", sxy / n, 1e-13)
case("COVARIANCE.P(B1:B10,C1:C10)", sxy / n, 1e-13)
case("COVARIANCE.S(B1:B10,C1:C10)", sxy / (n - 1), 1e-13)
# Excel's documentation's examples
X5, Y5 = [3, 2, 4, 5, 6], [9, 7, 12, 15, 17]
case("CORREL(%s,%s)" % (arr(X5), arr(Y5)), correl(Y5, X5), 1e-13)
assert round(float(correl(Y5, X5)), 9) == 0.997054486
case("COVAR(%s,%s)" % (arr(X5), arr(Y5)), pairs(Y5, X5)[5] / 5)
n7 = pairs(X7, Y7)
case("SLOPE(%s,%s)" % (arr(X7), arr(Y7)), n7[5] / n7[3], 1e-13)
case("INTERCEPT(%s,%s)" % (arr(X7), arr(Y7)), n7[2] - n7[5] / n7[3] * n7[1], 1e-13)
case("RSQ(%s,%s)" % (arr(X7), arr(Y7)), n7[5] ** 2 / (n7[3] * n7[4]), 1e-13)
FY, FX = [6, 7, 9, 15, 21], [20, 28, 31, 38, 40]
nf = pairs(FY, FX)
case("FORECAST(30,%s,%s)" % (arr(FY), arr(FX)), nf[2] + nf[5] / nf[3] * (30 - nf[1]), 1e-13)
assert round(float(nf[2] + nf[5] / nf[3] * (30 - nf[1])), 6) == 10.607253
case("CORREL({1,2},{1,2,3})", NA)
case("SLOPE({1,2},{3,3})", DIV0)
case("CORREL(B1:B2,{1;1})", DIV0)
case("COVARIANCE.S({1},{2})", DIV0)
case("FORECAST(\"x\",{1,2},{3,4})", VALUE)
case("SLOPE({1,2,3},{1,\"a\",3})", 1)
case("CORREL(A1:A3,M1:M3)", NA)


def phi(z):
    return 0.5 * math.erfc(-z / math.sqrt(2))


def pdf(z):
    return math.exp(-z * z / 2) / math.sqrt(2 * math.pi)


case("STANDARDIZE(42,40,1.5)", F(2) / F("1.5"))
case("STANDARDIZE(1,2,0)", NUM)
for x, m, s in (("42", "40", "1.5"), ("-1", "0", "1"), ("100", "90", "5")):
    z = (float(x) - float(m)) / float(s)
    case("NORM.DIST(%s,%s,%s,TRUE)" % (x, m, s), phi(z), 1e-14)
    case("NORM.DIST(%s,%s,%s,FALSE)" % (x, m, s), pdf(z) / float(s), 1e-14)
    case("NORMDIST(%s,%s,%s,TRUE)" % (x, m, s), phi(z), 1e-14)
case("NORM.DIST(1,0,0,TRUE)", NUM)
assert round(phi((42 - 40) / 1.5), 7) == 0.9087888 and round(pdf(2 / 1.5) / 1.5, 5) == 0.10934
for p, m, s in (("0.908789", "40", "1.5"), ("0.025", "100", "15"), ("0.999", "0", "1")):
    case("NORM.INV(%s,%s,%s)" % (p, m, s), statistics.NormalDist(float(m), float(s)).inv_cdf(float(p)), 1e-13)
    case("NORMINV(%s,%s,%s)" % (p, m, s), statistics.NormalDist(float(m), float(s)).inv_cdf(float(p)), 1e-13)
case("NORM.INV(0,0,1)", NUM)
case("NORM.INV(1,0,1)", NUM)
case("NORM.INV(0.5,0,-1)", NUM)
for z in ("1.333333", "0", "-1.96", "-10", "5", "-38"):
    case("NORM.S.DIST(%s,TRUE)" % z, phi(float(z)), 1e-13)
    case("NORMSDIST(%s)" % z, phi(float(z)), 1e-13)
case("NORM.S.DIST(1.333333,FALSE)", pdf(1.333333), 1e-14)
for p in ("0.908789", "0.5", "0.975", "1E-10", "0.9999999", "0.02425", "1E-300"):
    want = statistics.NormalDist().inv_cdf(float(p))
    case("NORM.S.INV(%s)" % p, want, 1e-13 if p != "0.5" else 0)
    case("NORMSINV(%s)" % p, want, 1e-13 if p != "0.5" else 0)
case("NORM.S.INV(0)", NUM)
case("NORM.S.INV(1.2)", NUM)


def binom(k, n, p, cum):
    k, n, p = int(k), int(n), F(p)
    if k < 0 or n < 0 or k > n or p < 0 or p > 1:
        return NUM
    term = lambda j: math.comb(n, j) * p ** j * (1 - p) ** (n - j)
    return sum(term(j) for j in range(k + 1)) if cum else term(k)


assert binom(6, 10, F(1, 2), False) == F("0.205078125") and binom(6, 10, F(1, 2), True) == F("0.828125")
for k, n, p, cum in (("6", "10", "0.5", False), ("6", "10", "0.5", True), ("3", "20", "0.15", True),
                     ("3", "20", "0.15", False), ("0", "5", "0", False), ("5", "5", "1", True),
                     ("400", "1000", "0.4", False), ("390", "1000", "0.4", True), ("6.7", "10.2", "0.5", False)):
    want = binom(float(k), float(n), F(p), cum)
    case("BINOM.DIST(%s,%s,%s,%s)" % (k, n, p, "TRUE" if cum else "FALSE"), want, 1e-13)
    case("BINOMDIST(%s,%s,%s,%s)" % (k, n, p, "TRUE" if cum else "FALSE"), want, 1e-13)
case("BINOM.DIST(2400,5000,0.5,TRUE)", binom(2400, 5000, F(1, 2), True), 1e-9)
case("BINOM.DIST(11,10,0.5,FALSE)", NUM)
case("BINOM.DIST(1,10,1.5,FALSE)", NUM)
case("BINOM.DIST(-1,10,0.5,TRUE)", NUM)


def poisson(x, mean, cum):
    x, mean = int(x), D(mean)
    if x < 0 or mean < 0:
        return NUM
    if mean == 0:
        return 1 if cum or x == 0 else 0
    term = lambda k: (-mean).exp() * mean ** k / math.factorial(k)
    return sum(term(k) for k in range(x + 1)) if cum else term(x)


assert round(poisson(2, 5, True), 6) == D("0.124652") and round(poisson(2, 5, False), 6) == D("0.084224")
for x, m, cum in (("2", "5", True), ("2", "5", False), ("3", "0.5", False), ("0", "0", True), ("10", "2.5", True),
                  ("150", "120", True), ("150", "120", False), ("2.9", "5", False)):
    want = poisson(float(x), m, cum)
    case("POISSON.DIST(%s,%s,%s)" % (x, m, "TRUE" if cum else "FALSE"), want, 1e-13)
    case("POISSON(%s,%s,%s)" % (x, m, "TRUE" if cum else "FALSE"), want, 1e-13)
case("POISSON.DIST(1000,1000,TRUE)", poisson(1000, 1000, True), 1e-9)
case("POISSON.DIST(-1,5,TRUE)", NUM)
case("POISSON.DIST(1,-5,TRUE)", NUM)
for x, l in (("0.2", "10"), ("1", "0.5"), ("0", "3")):
    for cum in (True, False):
        want = -math.expm1(-float(l) * float(x)) if cum else float(l) * math.exp(-float(l) * float(x))
        case("EXPON.DIST(%s,%s,%s)" % (x, l, "TRUE" if cum else "FALSE"), want, 1e-14)
        case("EXPONDIST(%s,%s,%s)" % (x, l, "TRUE" if cum else "FALSE"), want, 1e-14)
case("EXPON.DIST(-1,1,TRUE)", NUM)
case("EXPON.DIST(1,0,TRUE)", NUM)


def t_two(t, v):
    """P(|T| < t), Abramowitz and Stegun 26.7.3 and 26.7.4 (whole v)."""
    th = math.atan(t / math.sqrt(v))
    c, s = math.cos(th), math.sin(th)
    if v % 2 == 1:
        acc, term = 0.0, c
        for k in range(1, (v - 1) // 2 + 1):
            if k > 1:
                term *= (2 * k - 2) / (2 * k - 1) * c * c
            acc += term
        return 2 / math.pi * (th + (s * acc if v > 1 else 0))
    acc, term = 1.0, 1.0
    for k in range(1, v // 2):
        term *= (2 * k - 1) / (2 * k) * c * c
        acc += term
    return s * acc


def t_cdf(t, v):
    return 0.5 + 0.5 * t_two(t, v)


def t_pdf(t, v):
    return math.exp(math.lgamma((v + 1) / 2) - math.lgamma(v / 2) - (v + 1) / 2 * math.log1p(t * t / v)) / math.sqrt(v * math.pi)


def t_inv(p, v):
    lo, hi = -1e6, 1e6
    for _ in range(300):
        mid = (lo + hi) / 2
        if t_cdf(mid, v) < p:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


assert round(t_cdf(60, 1), 8) == 0.99469533 and round(t_pdf(8, 3), 8) == 0.00073691
assert round(2 * (1 - t_cdf(1.959999998, 60)), 9) == 0.05464493
for x, v in (("60", 1), ("-1.5", 4), ("0.5", 2), ("2.1", 7), ("1", 30), ("2", 60)):
    case("T.DIST(%s,%d,TRUE)" % (x, v), t_cdf(float(x), v), 1e-12)
    case("T.DIST(%s,%d,FALSE)" % (x, v), t_pdf(float(x), v), 1e-12)
case("T.DIST(8,3,FALSE)", t_pdf(8, 3), 1e-12)
case("T.DIST(1,0,TRUE)", NUM)
case("T.DIST(1,2.9,TRUE)", t_cdf(1, 2), 1e-12)
for x, v in (("1.959999998", 60), ("1", 1), ("3", 5), ("0", 10)):
    case("T.DIST.2T(%s,%d)" % (x, v), 1 - t_two(float(x), v), 1e-12)
    case("TDIST(%s,%d,2)" % (x, v), 1 - t_two(float(x), v), 1e-12)
    case("TDIST(%s,%d,1)" % (x, v), (1 - t_two(float(x), v)) / 2, 1e-12)
    case("T.DIST.RT(%s,%d)" % (x, v), 1 - t_cdf(float(x), v), 1e-12)
case("T.DIST.RT(-1,5)", 1 - t_cdf(-1, 5), 1e-12)
case("T.DIST.2T(-1,5)", NUM)
case("TDIST(1,5,3)", NUM)
case("TDIST(-1,5,1)", NUM)
for p, v in (("0.75", 2), ("0.05", 10), ("0.995", 3), ("0.3", 1), ("0.9", 25), ("0.01", 4)):
    case("T.INV(%s,%d)" % (p, v), t_inv(float(p), v), 1e-11)
case("T.INV(0.5,3)", 0)
case("T.INV(0,5)", NUM)
case("T.INV(1,5)", NUM)
for p, v in (("0.546449", 60), ("0.05", 10), ("0.01", 3), ("0.5", 1), ("0.2", 2)):
    want = t_inv(1 - float(p) / 2, v)
    case("T.INV.2T(%s,%d)" % (p, v), want, 1e-11)
    case("TINV(%s,%d)" % (p, v), want, 1e-11)
case("T.INV.2T(1,5)", 0)
case("T.INV.2T(0,5)", NUM)
case("T.INV.2T(0.5,0)", NUM)


def geomean(v):
    v = [D(x) for x in v]
    if not v or any(x <= 0 for x in v):
        return NUM
    return (sum(x.ln() for x in v) / len(v)).exp()


G7 = [4, 5, 8, 7, 11, 4, 3]
case("GEOMEAN(%s)" % ",".join(map(str, G7)), geomean(G7), 1e-14)
assert abs(geomean(G7) - D("5.476986969")) < D("1e-9")
case("GEOMEAN(A1:A10)", geomean(A), 1e-14)
case("GEOMEAN(1,0)", NUM)
case("GEOMEAN(D1)", NUM)
case("HARMEAN(%s)" % ",".join(map(str, G7)), len(G7) / sum(F(1, x) for x in G7), 1e-14)
case("HARMEAN(A1:A10)", len(A) / sum(F(1, x) for x in A), 1e-14)
case("HARMEAN(1,-1)", NUM)
AV7 = [4, 5, 6, 7, 5, 4, 3]
m7 = F(sum(AV7), 7)
case("AVEDEV(%s)" % ",".join(map(str, AV7)), sum(abs(x - m7) for x in AV7) / 7)
case("AVEDEV(A1:A10)", sum(abs(x - F(sum(A), 10)) for x in A) / 10)
case("AVEDEV(D1)", NUM)
case("DEVSQ(%s)" % ",".join(map(str, G7)), sum((x - F(sum(G7), 7)) ** 2 for x in G7))
case("DEVSQ(C1:C10)", sum((x - sum(C) / 10) ** 2 for x in C))
case("DEVSQ(D1)", NUM)
TM = [4, 5, 6, 7, 2, 3, 4, 5, 1, 2, 3]


def trimmean(v, pc):
    v = sorted(v)
    pc = F(pc)
    if pc < 0 or pc >= 1 or not v:
        return NUM
    cut = math.floor(len(v) * pc / 2)
    rest = v[cut:len(v) - cut]
    return F(sum(rest), len(rest))


case("TRIMMEAN(%s,0.2)" % arr(TM), trimmean(TM, "0.2"))
case("TRIMMEAN(A1:A10,0.25)", trimmean(A, "0.25"))
case("TRIMMEAN(A1:A10,1)", NUM)
case("TRIMMEAN(A1:A10,-0.1)", NUM)
FQ, BINS = [79, 85, 78, 85, 50, 81, 95, 88, 97], [70, 79, 89]


def frequency(data, bins):
    out = [0] * (len(bins) + 1)
    sb = sorted(bins)
    for x in data:
        up = [b for b in sb if b >= x]
        out[bins.index(up[0]) if up else len(bins)] += 1
    return out


assert frequency(FQ, BINS) == [1, 2, 4, 2]
for i in range(4):
    case("INDEX(FREQUENCY(%s,%s),%d)" % (arr(FQ), arr(BINS), i + 1), frequency(FQ, BINS)[i])
case("SUM(FREQUENCY(A1:A10,{5,10}))", 10)
case("INDEX(FREQUENCY(A1:A10,{10,5}),1)", frequency(A, [10, 5])[0])
case("INDEX(FREQUENCY(A1:A10,{10,5}),2)", frequency(A, [10, 5])[1])
case("ROWS(FREQUENCY(A1:A10,{5,10,20}))", 4)
case("FREQUENCY(A1:A10,D1)", 10)

# ---- Text ----

case('TEXTJOIN("-",TRUE,H1:H5)', "a-b-d-5")
case('TEXTJOIN("-",FALSE,H1:H5)', "a-b--d-5")
case('TEXTJOIN({",",";"},TRUE,"a","b","c")', "a,b;c")
case('TEXTJOIN("",TRUE,1,TRUE,"x")', "1TRUEx")
case('TEXTJOIN("-",TRUE,NA())', NA)
case('TEXTJOIN(", ",TRUE,{"x","y";"z","w"})', "x, y, z, w")
case('TEXTJOIN(H1:H2,TRUE,1,2,3,4)', "1a2b3a4")
case('TEXTJOIN("-",TRUE,"",A1,"")', "4")
case('REPT("",1E+300)', "")             # the first batch's REPT: it used to go round a loop for each time
case('LEN(TEXTJOIN("",TRUE,REPT("a",20000),REPT("b",20000)))', VALUE)
case("CONCAT(H1:H5)", "abd5")
case('CONCAT("a",1,TRUE)', "a1TRUE")
case("CONCAT(B1:B3)", "123")
case('CONCAT({"x","y"},"z")', "xyz")
case("CONCAT(L4)", DIV0)
case("UNICHAR(66)", "B")
case("UNICHAR(8364)", "€")
case("UNICHAR(0)", VALUE)
case("UNICHAR(55296)", NA)
case("UNICHAR(1114112)", VALUE)
case("UNICHAR(128512.9)", "\U0001F600")
case('UNICODE("B")', 66)
case('UNICODE("€")', 8364)
case('UNICODE("")', VALUE)
case('UNICODE("\U0001F600x")', 0x1F600)


def fixed(x, d=2, commas=True):
    if d > 127:
        return VALUE
    v = half_up(D(x), d) if d >= 0 else half_up(D(x).scaleb(d)).scaleb(-d)
    s = "{:,.{}f}".format(abs(v), max(d, 0)) if commas else "{:.{}f}".format(abs(v), max(d, 0))
    return ("-" if v < 0 else "") + s


for args, want in ((("1234.567", 1), "1,234.6"), (("1234.567", -1), "1,230"), (("44.332",), "44.33"),
                   (("1234.5", 0), "1,235"), (("0.5", 0), "1"), (("-0.004", 2), "0.00")):
    got = fixed(*args)
    assert got == want or args[0] == "-0.004", (args, got, want)
    case("FIXED(%s)" % ",".join(str(a) for a in args), want)
case("FIXED(-1234.567,-1,TRUE)", "-1230")
case("FIXED(1234567.891,2,TRUE)", "1234567.89")
case("FIXED(1234567.891,2,FALSE)", "1,234,567.89")
case("FIXED(1,128)", VALUE)
case('FIXED("x")', VALUE)


def dollar(x, d=2):
    v = half_up(D(x), d) if d >= 0 else half_up(D(x).scaleb(d)).scaleb(-d)
    s = "${:,.{}f}".format(abs(v), max(d, 0))
    return "(" + s + ")" if v < 0 else s


for args, want in ((("1234.567", 2), "$1,234.57"), (("1234.567", -2), "$1,200"), (("-1234.567", -2), "($1,200)"),
                   (("-0.123", 4), "($0.1230)"), (("99.888",), "$99.89")):
    assert dollar(*args) == want
    case("DOLLAR(%s)" % ",".join(str(a) for a in args), want)
case("DOLLAR(1,128)", VALUE)
for text, args, want in (('"2.500,27"', ',",","."', F("2500.27")), ('"3.5%"', "", F("0.035")),
                         ('"1 234.5"', "", F("1234.5")), ('""', "", 0), ('"1.2.3"', "", VALUE),
                         ('"9%%"', "", F("0.0009")), ('"1,5"', ',",",","', VALUE), ('"12,34.5"', "", F("1234.5")),
                         ('"1.5,2"', "", VALUE), ('"abc"', "", VALUE), ('"-1E3"', "", -1000),
                         ('" 1 , 000 . 25 "', "", F("1000.25")), ('"5"', ',""', VALUE)):
    case("NUMBERVALUE(%s%s)" % (text, args), want)

# ---- Logic and information ----

case('IFS(A1>5,"big",A1>2,"mid",TRUE,"small")', "mid")
case("IFS(FALSE,1)", NA)
case("IFS(1/0,1,TRUE,2)", DIV0)
case("SUM(IFS(FALSE,A1:A2,TRUE,B1:B3))", 6)
case("IFS(TRUE,)", 0)
case('IFS("x",1)', VALUE)
case('SWITCH(2,1,"one",2,"two","other")', "two")
case('SWITCH(3,1,"one",2,"two","other")', "other")
case('SWITCH(3,1,"one",2,"two")', NA)
case('SWITCH("B","a",1,"b",2)', 2)
case("SWITCH(NA(),1,2)", NA)
case('SWITCH(WEEKDAY(46320),1,"Sunday",7,"Saturday")', "Sunday")
case("SUM(SWITCH(1,1,B1:B4))", 10)
case("XOR(3>0,2<9)", False)
case("XOR(3>12,4>6)", False)
case("XOR(TRUE,FALSE,TRUE,TRUE)", True)
case("XOR(D1:D2)", True)
case('XOR("x")', VALUE)
case("XOR(B1:B3)", True)
case("XOR({1,1})", False)
case("XOR(M2)", NA)
case("ISFORMULA(L1)", True)
case("ISFORMULA(L2)", False)
case("ISFORMULA(Z99)", False)
case("ISFORMULA(5)", VALUE)
case("ISREF(A1)", True)
case("ISREF(A1:B2)", True)
case("ISREF(5)", False)
case('ISREF("A1")', False)
case('ISREF(INDIRECT("A1"))', True)
for x, ev in (("-1", False), ("2.5", True), ("5", False), ("0", True), ("-4", True)):
    case("ISEVEN(%s)" % x, ev)
    case("ISODD(%s)" % x, not ev)
case("ISEVEN(TRUE)", VALUE)
case('ISEVEN("x")', VALUE)
case("ISEVEN(D4)", True)
case('ISODD("3")', True)
for f, want in (("TYPE(1)", 1), ('TYPE("a")', 2), ("TYPE(TRUE)", 4), ("TYPE(1/0)", 16), ("TYPE({1,2})", 64),
                ("TYPE(D4)", 1), ("TYPE(D1)", 2), ("TYPE(M2)", 16), ("TYPE(D2)", 4)):
    case(f, want)
for e, k in (("#NULL!", 1), ("#DIV/0!", 2), ("#VALUE!", 3), ("#REF!", 4), ("#NAME?", 5), ("#NUM!", 6), ("#N/A", 7)):
    case("ERROR.TYPE(%s)" % e, k)
case("ERROR.TYPE(1)", NA)
case("ERROR.TYPE(M2)", 7)
case('ERROR.TYPE("x")', NA)
case('CELL("address",B3)', "$B$3")
case('CELL("row",B3)', 3)
case('CELL("col",AB3)', 28)
case('CELL("contents",A2)', 8)
case('CELL("contents",D1)', "apple")
case('CELL("type",D4)', "b")
case('CELL("type",D1)', "l")
case('CELL("type",A1)', "v")
case('CELL("type",D3)', "v")
case('CELL("format",A1)', "G")
case('CELL("address",Sheet2!A1)', "Sheet2!$A$1")
case('CELL("address",\'My \'\'Q\'\' Sheet\'!C7)', "'My ''Q'' Sheet'!$C$7")
case('CELL("bogus",A1)', VALUE)
case('CELL("width",A1)', 8)
case('CELL("filename")', "")
case('CELL("row")', 100)
case('CELL("ADDRESS")', "$Z$100")
case('CELL("protect",A1)', 1)
case('CELL("prefix",A1)', "")
case('CELL("color",A1)', 0)
case('CELL("parentheses",A1)', 0)
case('CELL("row",5)', VALUE)
case("SHEET()", 1)
case("SHEET(Sheet2!A1)", 2)
case("SHEET(\"My 'Q' Sheet\")", 3)
case('SHEET("nope")', NA)
case("SHEETS()", 3)
case("SHEETS(Sheet1:Sheet2!A1)", 2)
case("SHEETS(A1)", 1)
case("SHEETS(5)", REF)

# ---- Dates and times ----


def add_months(d, n, end=False):
    y, m = divmod(d.year * 12 + d.month - 1 + n, 12)
    m += 1
    if not 1900 <= y <= 9999:
        return NUM
    last = [31, 29 if y % 4 == 0 and (y % 100 or y % 400 == 0) else 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]
    return serial(datetime.date(y, m, last if end else min(d.day, last)))


for d, n in ((datetime.date(2011, 1, 15), 1), (datetime.date(2011, 1, 15), -1), (datetime.date(2011, 1, 15), 2),
             (datetime.date(2026, 1, 31), 1), (datetime.date(2024, 1, 31), 1), (datetime.date(2026, 10, 25), 3),
             (datetime.date(2026, 8, 31), -18), (datetime.date(2026, 3, 15), 120)):
    case("EDATE(%s,%d)" % (DATE(d.year, d.month, d.day), n), add_months(d, n))
    case("EOMONTH(%s,%d)" % (DATE(d.year, d.month, d.day), n), add_months(d, n, True))
assert add_months(datetime.date(2011, 1, 15), 1) == 40589 and add_months(datetime.date(2011, 1, 1), -3, True) == 40482
case("EDATE(-1,1)", NUM)
case('EDATE("2026-10-25",1.9)', add_months(datetime.date(2026, 10, 25), 1))
case("EOMONTH(DATE(9999,12,1),1)", NUM)
case("EOMONTH(DATE(2024,2,10),0)", serial(datetime.date(2024, 2, 29)))

WEEKENDS = {1: {5, 6}, 2: {6, 0}, 3: {0, 1}, 4: {1, 2}, 5: {2, 3}, 6: {3, 4}, 7: {4, 5}, 11: {6}, 12: {0}, 13: {1},
            14: {2}, 15: {3}, 16: {4}, 17: {5}}         # Python's weekday(): Monday 0


def weekend(w):
    if isinstance(w, str):
        if len(w) != 7 or set(w) - {"0", "1"}:
            return VALUE
        return {i for i, c in enumerate(w) if c == "1"}
    return WEEKENDS.get(w, NUM)


def networkdays(a, b, w=1, hol=()):
    mask = weekend(w)
    if isinstance(mask, Err):
        return mask
    sign = 1
    if a > b:
        a, b, sign = b, a, -1
    hol = set(hol)
    n = 0
    d = a
    while d <= b:
        if d.weekday() not in mask and d not in hol:
            n += 1
        d += datetime.timedelta(days=1)
    return sign * n


def workday(start, days, w=1, hol=()):
    mask = weekend(w)
    if isinstance(mask, Err):
        return mask
    if len(mask) == 7:
        return VALUE
    hol = set(hol)
    d, step, left = start, (1 if days >= 0 else -1), abs(days)
    while left:
        d += datetime.timedelta(days=step)
        if d.weekday() not in mask and d not in hol:
            left -= 1
    return serial(d)


d0, d1 = datetime.date(2012, 10, 1), datetime.date(2013, 3, 1)
h3 = [datetime.date(2012, 11, 22), datetime.date(2012, 12, 4), datetime.date(2013, 1, 21)]
assert networkdays(d0, d1) == 110 and networkdays(d0, d1, 1, h3[:1]) == 109 and networkdays(d0, d1, 1, h3) == 107
case("NETWORKDAYS(DATE(2012,10,1),DATE(2013,3,1))", 110)
case("NETWORKDAYS(DATE(2012,10,1),DATE(2013,3,1),%d)" % serial(h3[0]), 109)
case("NETWORKDAYS(DATE(2012,10,1),DATE(2013,3,1),%s)" % arr(serial(h) for h in h3), 107)
case("NETWORKDAYS(DATE(2026,12,21),DATE(2027,1,8),G1:G4)", networkdays(datetime.date(2026, 12, 21), datetime.date(2027, 1, 8), 1, HOL))
case("NETWORKDAYS(DATE(2026,1,10),DATE(2026,1,1))", networkdays(datetime.date(2026, 1, 10), datetime.date(2026, 1, 1)))
case("NETWORKDAYS(DATE(2026,10,24),DATE(2026,10,25))", 0)
case("NETWORKDAYS(-1,5)", NUM)
case('NETWORKDAYS(1,5,"x")', VALUE)
for w, ws in ((11, "11"), ("0000011", '"0000011"'), (7, "7"), ("1111111", '"1111111"'), (8, "8"), ("000001", '"000001"'),
              ("0000021", '"0000021"'), (17, "17"), ("1010101", '"1010101"')):
    case("NETWORKDAYS.INTL(DATE(2026,10,1),DATE(2026,10,31),%s)" % ws,
         networkdays(datetime.date(2026, 10, 1), datetime.date(2026, 10, 31), w))
case("NETWORKDAYS.INTL(DATE(2026,12,1),DATE(2027,1,31),7,G1:G4)",
     networkdays(datetime.date(2026, 12, 1), datetime.date(2027, 1, 31), 7, HOL))
case("NETWORKDAYS.INTL(DATE(2026,12,1),DATE(2027,1,31),,G1:G4)",
     networkdays(datetime.date(2026, 12, 1), datetime.date(2027, 1, 31), 1, HOL))
case("NETWORKDAYS.INTL(DATE(2006,1,1),DATE(2006,1,31))", 22)
w151 = workday(datetime.date(2008, 10, 1), 151)
assert date_of(w151) == datetime.date(2009, 4, 30)
case("WORKDAY(DATE(2008,10,1),151)", w151)
wh = [datetime.date(2008, 11, 26), datetime.date(2008, 12, 4), datetime.date(2009, 1, 21)]
assert date_of(workday(datetime.date(2008, 10, 1), 151, 1, wh)) == datetime.date(2009, 5, 5)
case("WORKDAY(DATE(2008,10,1),151,%s)" % arr(serial(h) for h in wh), workday(datetime.date(2008, 10, 1), 151, 1, wh))
for start, n in ((datetime.date(2026, 12, 24), 3), (datetime.date(2026, 10, 30), -5), (datetime.date(2026, 1, 1), 1000),
                 (datetime.date(2027, 1, 5), -10)):
    case("WORKDAY(%s,%d,G1:G4)" % (DATE(start.year, start.month, start.day), n), workday(start, n, 1, HOL))
case("WORKDAY(DATE(2026,10,25),0)", serial(datetime.date(2026, 10, 25)))
case("WORKDAY(DATE(2026,1,1),-1000)", workday(datetime.date(2026, 1, 1), -1000))
case("WORKDAY(DATE(2026,10,23),1.9)", workday(datetime.date(2026, 10, 23), 1))
case("WORKDAY(-1,1)", NUM)
case("WORKDAY(DATE(9999,12,30),5)", NUM)
for n, w, ws in ((90, 11, "11"), (30, 17, "17"), (10, "0000011", '"0000011"'), (-7, 7, "7"), (5, "1111111", '"1111111"'),
                 (5, 0, "0"), (500, "0010100", '"0010100"')):
    case("WORKDAY.INTL(DATE(2012,1,1),%d,%s)" % (n, ws), workday(datetime.date(2012, 1, 1), n, w))
case("WORKDAY.INTL(DATE(2026,12,23),5,1,G1:G4)", workday(datetime.date(2026, 12, 23), 5, 1, HOL))


def weeknum(d, t=1):
    starts = {1: 6, 17: 6, 2: 0, 11: 0, 12: 1, 13: 2, 14: 3, 15: 4, 16: 5}   # Python weekday of the week's first day
    if t == 21:
        return d.isocalendar()[1]
    if t not in starts:
        return NUM
    jan1 = datetime.date(d.year, 1, 1)
    off = (jan1.weekday() - starts[t]) % 7
    return ((d - jan1).days + off) // 7 + 1


assert weeknum(datetime.date(2012, 3, 9)) == 10 and weeknum(datetime.date(2012, 3, 9), 2) == 11
for d, t in ((datetime.date(2012, 3, 9), 1), (datetime.date(2012, 3, 9), 2), (datetime.date(2026, 1, 1), 1),
             (datetime.date(2026, 12, 31), 21), (datetime.date(2026, 10, 25), 3), (datetime.date(2026, 10, 25), 16),
             (datetime.date(2026, 10, 25), 11), (datetime.date(2027, 1, 2), 1), (datetime.date(2026, 12, 31), 12)):
    case("WEEKNUM(%s,%d)" % (DATE(d.year, d.month, d.day), t), weeknum(d, t))
case("WEEKNUM(DATE(2026,10,25))", weeknum(datetime.date(2026, 10, 25)))
for d in (datetime.date(2012, 3, 9), datetime.date(2027, 1, 1), datetime.date(2026, 12, 28), datetime.date(2026, 10, 25),
          datetime.date(2021, 1, 3), datetime.date(2020, 12, 31)):
    case("ISOWEEKNUM(%s)" % DATE(d.year, d.month, d.day), d.isocalendar()[1])
case("ISOWEEKNUM(-1)", NUM)


def is_leap(y):
    return y % 4 == 0 and (y % 100 != 0 or y % 400 == 0)


def last_feb(d):
    return d.month == 2 and d.day == (29 if is_leap(d.year) else 28)


def yearfrac(a, b, basis=0):
    if not 0 <= basis <= 4:
        return NUM
    if a > b:
        a, b = b, a
    days = (b - a).days
    if basis == 0:
        d1, d2 = a.day, b.day
        if d1 == 31 and d2 == 31:
            d1 = d2 = 30
        elif d1 == 31:
            d1 = 30
        elif d1 == 30 and d2 == 31:
            d2 = 30
        elif a.month == 2 and b.month == 2 and last_feb(a) and last_feb(b):
            d1 = d2 = 30
        elif last_feb(a):
            d1 = 30
        return F((b.year - a.year) * 360 + (b.month - a.month) * 30 + d2 - d1, 360)
    if basis == 1:
        if a.year == b.year or (b.year == a.year + 1 and (a.month, a.day) >= (b.month, b.day)):
            if a.year == b.year:
                length = 366 if is_leap(a.year) else 365
            elif (is_leap(a.year) and a < datetime.date(a.year, 3, 1)) or \
                    (is_leap(b.year) and (b >= datetime.date(b.year, 3, 1) or (b.month, b.day) == (2, 29))):
                length = 366
            else:
                length = 365
            return F(days, length)
        total = (datetime.date(b.year + 1, 1, 1) - datetime.date(a.year, 1, 1)).days
        return F(days) / (F(total) / (b.year - a.year + 1))
    if basis == 2:
        return F(days, 360)
    if basis == 3:
        return F(days, 365)
    d1, d2 = min(a.day, 30), min(b.day, 30)
    return F((b.year - a.year) * 360 + (b.month - a.month) * 30 + d2 - d1, 360)


ya, yb = datetime.date(2012, 1, 1), datetime.date(2012, 7, 30)
assert round(float(yearfrac(ya, yb)), 8) == 0.58055556 and round(float(yearfrac(ya, yb, 1)), 8) == 0.57650273
assert round(float(yearfrac(ya, yb, 3)), 8) == 0.57808219
for a, b in ((ya, yb), (datetime.date(2011, 2, 28), datetime.date(2012, 2, 29)), (datetime.date(2010, 6, 15), datetime.date(2013, 3, 1)),
             (datetime.date(2026, 10, 31), datetime.date(2026, 1, 31)), (datetime.date(2024, 2, 29), datetime.date(2025, 2, 28)),
             (datetime.date(2023, 3, 1), datetime.date(2024, 2, 29)), (datetime.date(2026, 1, 30), datetime.date(2026, 3, 31))):
    for basis in range(5):
        case("YEARFRAC(%s,%s,%d)" % (DATE(a.year, a.month, a.day), DATE(b.year, b.month, b.day), basis), yearfrac(a, b, basis))
case("YEARFRAC(DATE(2012,1,1),DATE(2012,7,30))", yearfrac(ya, yb))
case("YEARFRAC(DATE(2012,1,1),DATE(2012,7,30),5)", NUM)


def datedif(a, b, unit):
    if a > b:
        return NUM
    unit = unit.upper()
    months = (b.year - a.year) * 12 + b.month - a.month - (b.day < a.day)
    if unit == "Y":
        return months // 12
    if unit == "M":
        return months
    if unit == "D":
        return (b - a).days
    if unit == "YM":
        return months % 12
    if unit == "MD":
        if b.day >= a.day:
            return b.day - a.day
        y, m = (b.year, b.month - 1) if b.month > 1 else (b.year - 1, 12)
        start = datetime.date(y, m, 1) + datetime.timedelta(days=a.day - 1)   # the day a.day of the month before, run over
        return (b - start).days
    if unit == "YD":
        y = b.year if (b.month, b.day) >= (a.month, a.day) else b.year - 1
        return (b - datetime.date(y, a.month, a.day)).days
    return NUM


for a, b, u in ((datetime.date(2001, 1, 1), datetime.date(2003, 1, 1), "Y"), (datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "D"),
                (datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "YD"), (datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "MD"),
                (datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "M"), (datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "YM"),
                (datetime.date(2026, 1, 31), datetime.date(2026, 3, 1), "MD"), (datetime.date(2020, 2, 29), datetime.date(2026, 10, 25), "y"),
                (datetime.date(2026, 10, 25), datetime.date(2026, 10, 24), "D"), (datetime.date(2026, 1, 1), datetime.date(2026, 2, 1), "X"),
                (datetime.date(2025, 11, 20), datetime.date(2026, 2, 10), "YD"), (datetime.date(2025, 11, 20), datetime.date(2026, 2, 10), "md")):
    case('DATEDIF(%s,%s,"%s")' % (DATE(a.year, a.month, a.day), DATE(b.year, b.month, b.day), u), datedif(a, b, u))
assert datedif(datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "YD") == 75
assert datedif(datetime.date(2001, 6, 1), datetime.date(2002, 8, 15), "MD") == 14
case('DAYS("15-MAR-2021","1-FEB-2021")', 42)
case("DAYS(DATE(2021,12,31),DATE(2021,1,1))", 364)
case("DAYS(-1,1)", NUM)
case('DAYS("x",1)', VALUE)


def days360(a, b, eu=False):
    d1, d2 = a.day, b.day
    if eu:
        d1, d2 = min(d1, 30), min(d2, 30)
    else:
        last = (datetime.date(a.year + a.month // 12, a.month % 12 + 1, 1) - datetime.timedelta(days=1)).day
        if d1 == last:
            d1 = 30
        if d2 == 31 and d1 >= 30:
            d2 = 30
    return (b.year - a.year) * 360 + (b.month - a.month) * 30 + d2 - d1


for a, b, eu in ((datetime.date(2011, 1, 1), datetime.date(2011, 12, 31), False), (datetime.date(2011, 1, 30), datetime.date(2011, 12, 31), False),
                 (datetime.date(2011, 1, 31), datetime.date(2011, 12, 31), True), (datetime.date(2026, 2, 28), datetime.date(2026, 3, 31), False),
                 (datetime.date(2026, 3, 31), datetime.date(2026, 2, 28), False), (datetime.date(2026, 1, 15), datetime.date(2026, 2, 28), False),
                 (datetime.date(2026, 4, 30), datetime.date(2026, 5, 31), True)):
    case("DAYS360(%s,%s%s)" % (DATE(a.year, a.month, a.day), DATE(b.year, b.month, b.day), ",TRUE" if eu else ""), days360(a, b, eu))
assert days360(datetime.date(2011, 1, 1), datetime.date(2011, 12, 31)) == 360
for text, want in (('"2:24 AM"', F(1, 10)), ('"22-Aug-2011 6:35 AM"', F(6 * 60 + 35, 1440)), ('"13:45:30"', F(13 * 3600 + 45 * 60 + 30, 86400)),
                   ("0.5", VALUE), ('"abc"', VALUE), ('"2026-10-25"', 0), ('"12"', VALUE)):
    case("TIMEVALUE(%s)" % text, want)
for text, want in (('"22/8/2011"', 40777), ('"2011-08-22"', 40777), ('"22-MAY-2011"', 40685), ("40777", VALUE),
                   ('"13:45"', VALUE), ('"2026-10-25 13:45"', 46320), ('"x"', VALUE)):
    case("DATEVALUE(%s)" % text, want)
assert serial(datetime.date(2011, 8, 22)) == 40777 and serial(datetime.date(2011, 5, 22)) == 40685

# ---- Lookup and reference ----

I, J, K = [10, 20, 30, 40], ["ten", "twenty", "thirty", "forty"], [F("1.5"), F("2.5"), F("3.5"), F("4.5")]
for f, want in (("XLOOKUP(30,I1:I4,J1:J4)", "thirty"), ("XLOOKUP(35,I1:I4,J1:J4)", NA),
                ('XLOOKUP(35,I1:I4,J1:J4,"none")', "none"), ("XLOOKUP(35,I1:I4,J1:J4,,-1)", "thirty"),
                ("XLOOKUP(35,I1:I4,J1:J4,,1)", "forty"), ('XLOOKUP("t*",J1:J4,I1:I4,,2)', 10),
                ('XLOOKUP("t*",J1:J4,I1:I4,,2,-1)', 30), ("SUM(XLOOKUP(20,I1:I4,K1:K4))", F("2.5")),
                ("SUM(XLOOKUP(20,I1:I4,J1:K4))", F("2.5")), ("XLOOKUP(20,I1:I4,J1:K4)", VALUE),
                ("XLOOKUP(20,I1:I4,J1:J3)", VALUE), ("XLOOKUP(20,I1:J4,K1:K4)", VALUE),
                ("XLOOKUP(25,I1:I4,J1:J4,,1,2)", "thirty"), ('XLOOKUP(30,{10,20,30},{"a","b","c"})', "c"),
                ("XLOOKUP(2,{1,2,3},{4;5;6})", VALUE), ("XLOOKUP(NA(),I1:I4,J1:J4)", NA),
                ("XLOOKUP(20,I1:I4,J1:J4,,3)", VALUE), ('XLOOKUP(25,{40,30,20,10},{"a","b","c","d"},,-1,-2)', "c"),
                ('XLOOKUP("TWENTY",J1:J4,K1:K4)', F("2.5")), ('XLOOKUP("20",I1:I4,J1:J4)', NA),
                ("XLOOKUP(8,A1:A10,B1:B10,,0,-1)", 9), ("XLOOKUP(8,A1:A10,B1:B10)", 2),
                ("INDEX(XLOOKUP(30,I1:I4,J1:K4),2)", F("3.5")), ('XLOOKUP(45,{40,30,20,10},{"a","b","c","d"},,1,-2)', NA),
                ('XLOOKUP(5,{40,30,20,10},{"a","b","c","d"},,1,-2)', "d")):
    case(f, want)
for f, want in (("XMATCH(30,I1:I4)", 3), ("XMATCH(35,I1:I4,1)", 4), ("XMATCH(35,I1:I4,-1)", 3), ("XMATCH(5,I1:I4,-1)", NA),
                ('XMATCH("TW*",J1:J4,2)', 2), ("XMATCH(8,A1:A10,0,-1)", 9), ("XMATCH(8,A1:A10)", 2),
                ("XMATCH(40,I1:I4,0,2)", 4), ("XMATCH(15,I1:I4,-1,2)", 1), ("XMATCH(15,I1:I4,1,2)", 2),
                ("XMATCH(45,I1:I4,1,2)", NA), ("XMATCH(1,A1:B2)", VALUE), ("XMATCH(30,I1:I4,0,3)", VALUE),
                ("XMATCH(3,{5,4,3,2,1},0,-2)", 3), ("XMATCH(5,A1:A10,1)", 2), ("XMATCH(5,A1:A10,-1)", 1)):
    case(f, want)
for f, want in (("LOOKUP(25,I1:I4,J1:J4)", "twenty"), ("LOOKUP(5,I1:I4,J1:J4)", NA), ("LOOKUP(40,I1:I4,K1:K4)", F("4.5")),
                ("LOOKUP(100,I1:I4,J1:J4)", "forty"), ("LOOKUP(25,I1:K4)", F("2.5")),
                ('LOOKUP("c",{"a","b","c","d";1,2,3,4})', 3), ('LOOKUP(25,I1:I4,{"w","x","y","z"})', "x"),
                ("LOOKUP(25,I1:I4,L1:L4)", 5), ("LOOKUP(NA(),I1:I4)", NA), ("LOOKUP(20,I1:I4,M2:M5)", 5), ("LOOKUP(30,I1:I4,M2:M5)", 0),
                ("LOOKUP(25,I1:J4,K1:K4)", NA)):
    case(f, want)
for f, want in (("SUM(OFFSET(A1,1,1,2,2))", 2 + 3 + C[1] + C[2]), ("OFFSET(A1,2,0)", 15), ("OFFSET(A1,-1,0)", REF),
                ("OFFSET(A1,0,0,0,1)", REF), ("SUM(OFFSET(C3,0,0,-2,1))", C[1] + C[2]), ("ROWS(OFFSET(A1,0,0,5,2))", 5),
                ("COLUMNS(OFFSET(A1,0,0,5,2))", 2), ("OFFSET(5,1,1)", VALUE), ("SUM(OFFSET(A1:B2,1,0))", 8 + 15 + 2 + 3),
                ("OFFSET(A1,1048576,0)", REF), ("SUM(OFFSET(B1,0,0,,))", 1), ("OFFSET(B2,-1,-1)", 4),
                ("SUM(OFFSET(A1:A2,0,1,3))", 1 + 2 + 3), ("OFFSET(Sheet2!A1,1,1)", 2000), ("OFFSET(D1,M2,0)", NA)):
    case(f, want)
for f, want in (('INDIRECT("A2")', 8), ('INDIRECT("a2")', 8), ('SUM(INDIRECT("B1:B10"))', 55), ('INDIRECT("Sheet2!A1")', 1000),
                ("INDIRECT(\"'My ''Q'' Sheet'!A1\")", 5), ('SUM(INDIRECT("A:A"))', sum(A)), ('INDIRECT("R2C1",FALSE)', 8),
                ('INDIRECT("R[-98]C[-24]",FALSE)', 2), ('SUM(INDIRECT("R1C2:R10C2",FALSE))', 55), ('SUM(INDIRECT("C2",FALSE))', 55),
                ('SUM(INDIRECT("R10",FALSE))', A[9] + B[9] + C[9]), ('INDIRECT("nonsense")', REF),
                ('INDIRECT("Nope!A1")', REF), ('INDIRECT("")', REF), ('INDIRECT("[Other.xls]Sheet1!A1")', REF),
                ('INDIRECT("Sheet2!R2C2",FALSE)', 2000), ('INDIRECT("$B$2")', 2), ('SUM(INDIRECT("Sheet1:Sheet2!A1"))', 1004),
                ('INDIRECT(" A2 ")', 8), ('INDIRECT("R0C1",FALSE)', REF), ('INDIRECT("A1:",TRUE)', REF), ("INDIRECT(M2)", NA),
                ('ROWS(INDIRECT("2:4"))', 3), ('INDIRECT("rc[-24]",FALSE)', 0)):
    case(f, want)
for f, want in (("ADDRESS(2,3)", "$C$2"), ("ADDRESS(2,3,2)", "C$2"), ("ADDRESS(2,3,2,FALSE)", "R2C[3]"),
                ('ADDRESS(2,3,1,FALSE,"[Book1]Sheet1")', "[Book1]Sheet1!R2C3"),
                ('ADDRESS(2,3,1,FALSE,"EXCEL SHEET")', "'EXCEL SHEET'!R2C3"), ("ADDRESS(1,16384)", "$XFD$1"),
                ("ADDRESS(0,1)", VALUE), ("ADDRESS(1,1,5)", VALUE), ("ADDRESS(2,3,4)", "C2"), ("ADDRESS(2,3,3)", "$C2"),
                ("ADDRESS(2,3,4,FALSE)", "R[2]C[3]"), ("ADDRESS(2,3,1,FALSE)", "R2C3"), ("ADDRESS(2,3,3,FALSE)", "R[2]C3"),
                ('ADDRESS(5,1,1,TRUE,"Sheet2")', "Sheet2!$A$5"), ("ADDRESS(1048577,1)", VALUE), ("ADDRESS(1,16385)", VALUE)):
    case(f, want)
case("AREAS(B2:D4)", 1)
case("AREAS((B2:D4,E5,F6:I9))", 3)
case("AREAS(B2:D4 B2)", 1)
case("AREAS(5)", VALUE)
case("INDEX(TRANSPOSE({1,2,3;4,5,6}),3,2)", 6)
case("ROWS(TRANSPOSE({1,2,3}))", 3)
case("SUM(TRANSPOSE(B1:B10))", 55)
case("COLUMNS(TRANSPOSE(B1:B10))", 10)
case("INDEX(TRANSPOSE(H1:H5),1,3)", 0)
case('HYPERLINK("http://example.org","Example")', "Example")
case('HYPERLINK("http://example.org")', "http://example.org")
case("HYPERLINK(NA())", NA)
case('HYPERLINK("x",42)', 42)
case("FORMULATEXT(L1)", "=SUM(A1:A3)")
case("FORMULATEXT(L2)", NA)
case("FORMULATEXT(5)", VALUE)
case("FORMULATEXT(D3)", '=""')

# ---- Money ----


def pmt(r, n, pv, fv=0, t=0):
    r = F(r)
    if r == 0:
        return -(F(pv) + fv) / n
    g = (1 + r) ** n
    return -(r * (fv + pv * g)) / ((1 + r * t) * (g - 1))


def fv(r, n, p, pv, t=0):
    r = F(r)
    if r == 0:
        return -(F(pv) + p * n)
    g = (1 + r) ** n
    return -(pv * g + p * (1 + r * t) * (g - 1) / r)


def ipmt(r, per, n, pv, fvv=0, t=0):
    if per < 1 or per > n:
        return NUM
    p = pmt(r, n, pv, fvv, t)
    if t and per == 1:
        return 0
    i = fv(r, per - 1, p, pv, t) * F(r)
    return i / (1 + F(r)) if t else i


def ppmt(r, per, n, pv, fvv=0, t=0):
    i = ipmt(r, per, n, pv, fvv, t)
    return i if isinstance(i, Err) else pmt(r, n, pv, fvv, t) - i


assert round(float(ipmt(F(1, 120), 1, 36, 8000)), 2) == -66.67 and round(float(ipmt(F(1, 10), 3, 3, 8000)), 2) == -292.45
assert round(float(ppmt(F(1, 120), 1, 24, 2000)), 2) == -75.62 and round(float(ppmt(F(8, 100), 10, 10, 200000)), 2) == -27598.05
for f, args in (("IPMT(0.1/12,1,3*12,8000)", (F(1, 120), 1, 36, 8000)), ("IPMT(0.1,3,3,8000)", (F(1, 10), 3, 3, 8000)),
                ("IPMT(0.1,1,3,8000,0,1)", (F(1, 10), 1, 3, 8000, 0, 1)), ("IPMT(0.1,2,3,8000,0,1)", (F(1, 10), 2, 3, 8000, 0, 1)),
                ("IPMT(0.05,5,10,-1000,500)", (F(1, 20), 5, 10, -1000, 500)), ("IPMT(0,2,10,1000)", (0, 2, 10, 1000)),
                ("IPMT(0.1,0,3,8000)", (F(1, 10), 0, 3, 8000)), ("IPMT(0.1,4,3,8000)", (F(1, 10), 4, 3, 8000))):
    case(f, ipmt(*args), 1e-13)
for f, args in (("PPMT(0.1/12,1,2*12,2000)", (F(1, 120), 1, 24, 2000)), ("PPMT(0.08,10,10,200000)", (F(8, 100), 10, 10, 200000)),
                ("PPMT(0.1,2,3,8000,0,1)", (F(1, 10), 2, 3, 8000, 0, 1)), ("PPMT(0.1,4,3,8000)", (F(1, 10), 4, 3, 8000))):
    case(f, ppmt(*args), 1e-13)


def cum(r, n, pv, a, b, t, principal):
    if r <= 0 or n <= 0 or pv <= 0 or a < 1 or b < 1 or a > b or b > n or t not in (0, 1):
        return NUM
    return sum((ppmt if principal else ipmt)(r, per, n, pv, 0, t) for per in range(a, b + 1))


R9 = F(9, 1200)
assert round(float(cum(R9, 360, 125000, 13, 24, 0, False)), 5) == -11135.23213
assert round(float(cum(R9, 360, 125000, 1, 1, 0, False)), 1) == -937.5
assert round(float(cum(R9, 360, 125000, 13, 24, 0, True)), 7) == -934.1071234
for a, b, t in ((13, 24, 0), (1, 1, 0), (1, 12, 1), (0, 5, 0), (5, 4, 0), (1, 1, 2), (350, 361, 0)):
    case("CUMIPMT(0.09/12,30*12,125000,%d,%d,%d)" % (a, b, t), cum(R9, 360, 125000, a, b, t, False), 1e-12)
    case("CUMPRINC(0.09/12,30*12,125000,%d,%d,%d)" % (a, b, t), cum(R9, 360, 125000, a, b, t, True), 1e-12)
case("CUMIPMT(0,360,125000,1,1,0)", NUM)


def nper(r, p, pv, fvv=0, t=0):
    r, p, pv, fvv = D(r), D(p), D(pv), D(fvv)
    if r == 0:
        return NUM if p == 0 else -(pv + fvv) / p
    a = p * (1 + r * t) - fvv * r
    b = p * (1 + r * t) + pv * r
    if b == 0 or a / b <= 0:
        return NUM
    return (a / b).ln() / (1 + r).ln()


assert round(nper("0.01", -100, -1000, 10000, 1), 7) == D("59.6738657")
assert round(nper("0.01", -100, -1000, 10000), 7) == D("60.0821229") and round(nper("0.01", -100, -1000), 8) == D("-9.57859404")
for f, args in (("NPER(0.12/12,-100,-1000,10000,1)", ("0.01", -100, -1000, 10000, 1)), ("NPER(0.12/12,-100,-1000,10000)", ("0.01", -100, -1000, 10000)),
                ("NPER(0.12/12,-100,-1000)", ("0.01", -100, -1000)), ("NPER(0,-100,1000)", (0, -100, 1000)),
                ("NPER(0.1,0,1000)", ("0.1", 0, 1000)), ("NPER(0.05,-200,1000)", ("0.05", -200, 1000)), ("NPER(0,0,1000)", (0, 0, 1000))):
    case(f, nper(*args), 1e-13)


def solve(f, lo, hi):
    """a root of f between lo and hi (f changes sign), by halves in Decimal"""
    flo = f(lo)
    for _ in range(250):
        mid = (lo + hi) / 2
        fm = f(mid)
        if (fm < 0) == (flo < 0):
            lo, flo = mid, fm
        else:
            hi = mid
    return (lo + hi) / 2


def rate(n, p, pv, fvv=0, t=0):
    n, p, pv, fvv = D(n), D(p), D(pv), D(fvv)
    f = lambda r: pv * (1 + r) ** n + p * (1 + r * t) * ((1 + r) ** n - 1) / r + fvv
    return solve(f, D("0.000000001"), D("0.9"))


assert round(rate(48, -200, 8000), 8) == D("0.00770147")
for f, args in (("RATE(4*12,-200,8000)", (48, -200, 8000)), ("RATE(10,-150,1000)", (10, -150, 1000)),
                ("RATE(360,-1000,150000)", (360, -1000, 150000)), ("RATE(5,0,-1000,1500)", (5, 0, -1000, 1500)),
                ("RATE(12,-100,1000,0,1)", (12, -100, 1000, 0, 1))):
    case(f, rate(*args), 1e-12)
case("RATE(0,-100,1000)", NUM)
case("RATE(12,100,1000)", NUM)


def npv_d(r, v):
    return sum(D(x) / (1 + r) ** i for i, x in enumerate(v))


def irr(v):
    return solve(lambda r: npv_d(r, v), D("-0.99"), D("10"))


IR = [-70000, 12000, 15000, 18000, 21000, 26000]
assert round(irr(IR[:5]), 3) == D("-0.021") and round(irr(IR), 3) == D("0.087") and round(irr(IR[:3]), 3) == D("-0.444")
case("IRR(%s)" % arr(IR[:5]), irr(IR[:5]), 1e-12)
case("IRR(%s)" % arr(IR), irr(IR), 1e-12)
case("IRR(%s,-0.1)" % arr(IR[:3]), irr(IR[:3]), 1e-12)
case("IRR(E1:E6)", irr(E), 1e-12)
case("IRR({1,2,3})", NUM)
case("IRR({-1,-2})", NUM)
case("IRR(M1:M3)", NA)


def xnpv(r, v, d):
    r = D(r)
    return sum(D(x) / (1 + r) ** (D(t - d[0]) / 365) for x, t in zip(v, d))


XV, XD = [-10000, 2750, 4250, 3250, 2750], [39448, 39508, 39751, 39859, 39904]
assert round(xnpv("0.09", XV, XD), 2) == D("2086.65")
case("XNPV(0.09,%s,%s)" % (arr(XV), arr(XD)), xnpv("0.09", XV, XD), 1e-13)
FS = [serial(d) for d in FD]
case("XNPV(0.09,E1:E6,F1:F6)", xnpv("0.09", E, FS), 1e-13)
case("XNPV(0.09,{1,2},{1})", NUM)
case("XNPV(0.09,{1,2},{10,5})", NUM)
case('XNPV(0.09,{1,"a"},{1,2})', VALUE)
case("XNPV(-1,{1,2},{1,2})", NUM)


def xirr(v, d):
    return solve(lambda r: xnpv(r, v, d), D("-0.99"), D("10"))


assert abs(xirr(XV, XD) - D("0.373362535")) < D("2e-9")   # Excel stops at its 0.000001% tolerance
case("XIRR(%s,%s)" % (arr(XV), arr(XD)), xirr(XV, XD), 1e-12)
case("XIRR(E1:E6,F1:F6)", xirr(E, FS), 1e-12)
case("XIRR({1,2},{1,2})", NUM)
case("XIRR({-1,2},{1,2,3})", NUM)


def mirr(v, fr, rr):
    fr, rr = D(fr), D(rr)
    n = len(v)
    pos = sum(D(x) * (1 + rr) ** (n - 1 - i) for i, x in enumerate(v) if x > 0)
    neg = sum(D(x) / (1 + fr) ** i for i, x in enumerate(v) if x < 0)
    if pos == 0 or neg == 0:
        return DIV0
    return (pos / -neg) ** (D(1) / (n - 1)) - 1


MV = [-120000, 39000, 30000, 21000, 37000, 46000]
assert round(mirr(MV, "0.1", "0.12"), 6) == D("0.126094") and round(mirr(MV[:4], "0.1", "0.12"), 9) == D("-0.048044655")
assert round(mirr(MV, "0.1", "0.14"), 4) == D("0.1348")
for v, fr, rr in ((MV, "0.1", "0.12"), (MV[:4], "0.1", "0.12"), (MV, "0.1", "0.14")):
    case("MIRR(%s,%s,%s)" % (arr(v), fr, rr), mirr(v, fr, rr), 1e-13)
case("MIRR(E1:E6,0.08,0.1)", mirr(E, "0.08", "0.1"), 1e-13)
case("MIRR({1,2},0.1,0.1)", DIV0)
case("FVSCHEDULE(1,{0.09,0.11,0.1})", F("1.09") * F("1.11") * F("1.1"))
case('FVSCHEDULE(100,{0.1,"a"})', VALUE)
case("FVSCHEDULE(100,Z1:Z2)", 100)
case("FVSCHEDULE(100,M1:M3)", NA)


def effect(n, p):
    p = int(p)
    if D(n) <= 0 or p < 1:
        return NUM
    return (1 + D(n) / p) ** p - 1


def nominal(e, p):
    p = int(p)
    if D(e) <= 0 or p < 1:
        return NUM
    return p * ((1 + D(e)) ** (D(1) / p) - 1)


assert round(effect("0.0525", 4), 9) == D("0.053542667") and round(nominal("0.053543", 4), 8) == D("0.05250032")
for n, p in (("0.0525", "4"), ("0.1", "12"), ("0.05", "4.9"), ("0", "4"), ("0.1", "0.5")):
    case("EFFECT(%s,%s)" % (n, p), effect(n, float(p)), 1e-13)
for e, p in (("0.053543", "4"), ("0.1", "12"), ("-0.1", "4"), ("0.08", "365")):
    case("NOMINAL(%s,%s)" % (e, p), nominal(e, float(p)), 1e-13)
case("SLN(30000,7500,10)", 2250)
case("SLN(1,1,0)", DIV0)
for per in (1, 10, 11, 0, 3.5):
    case("SYD(30000,7500,10,%s)" % per, F(22500) * (10 - F(per) + 1) * 2 / (10 * 11) if 0 < per <= 10 else NUM)
assert round(float(F(22500) * 10 * 2 / 110), 2) == 4090.91


def db(cost, salvage, life, period, month=12):
    if cost <= 0 or salvage < 0 or life <= 0 or period < 1 or not 1 <= month <= 12 or period > (life + 1 if month < 12 else life):
        return NUM
    rate = F(half_up(1 - (D(salvage) / D(cost)) ** (D(1) / D(life)), 3))
    total, dep = F(0), F(0)
    for p in range(1, period + 1):
        if p == 1:
            dep = cost * rate * month / 12
        elif p > life:
            dep = (cost - total) * rate * (12 - month) / 12
        else:
            dep = (cost - total) * rate
        total += dep
    return dep


assert [round(float(db(1000000, 100000, 6, p, 7)), 2) for p in range(1, 8)] == [186083.33, 259639.42, 176814.44, 120410.64, 81999.64, 55841.76, 15845.10]
for p in range(1, 9):
    case("DB(1000000,100000,6,%d,7)" % p, db(1000000, 100000, 6, p, 7), 1e-13)
case("DB(10000,1000,5,5)", db(10000, 1000, 5, 5), 1e-13)
case("DB(10000,1000,5,6)", NUM)
case("DB(10000,1000,5,1,13)", NUM)


def ddb(cost, salvage, life, period, factor=2):
    cost, salvage, life, factor = F(cost), F(salvage), F(life), F(factor)
    if cost < 0 or salvage < 0 or life <= 0 or period <= 0 or factor <= 0 or period > life:
        return NUM
    rate = min(factor / life, F(1))
    book, dep = cost, F(0)
    for _ in range(int(period)):
        dep = max(min(book * rate, book - salvage), F(0))
        book -= dep
    return dep


assert [round(float(ddb(2400, 300, *a)), 2) for a in ((3650, 1), (120, 1, 2), (10, 1, 2), (10, 2, F("1.5")), (10, 10))] == [1.32, 40, 480, 306, 22.12]
for a, s in (((3650, 1), "3650,1"), ((120, 1, 2), "120,1,2"), ((10, 1, 2), "10,1,2"), ((10, 2, F("1.5")), "10,2,1.5"),
             ((10, 10), "10,10"), ((10, 11), "10,11"), ((10, 1, 0), "10,1,0"), ((5, 3, 3), "5,3,3")):
    case("DDB(2400,300,%s)" % s, ddb(2400, 300, *a), 1e-13)

# ---- The C table ----


def c_string(s):
    out = '"'
    for ch in s.encode("utf-8"):
        c = chr(ch)
        if c in '"\\':
            out += "\\" + c
        elif 32 <= ch < 127:
            out += c
        else:
            out += "\\%03o" % ch
    return out + '"'


def table():
    lines = ["/* Made by fn2_expect.py: do not edit (python3 tests/calc/fn2_expect.py > tests/calc/fn2_cases.h). */",
             "", "static const struct {", "    int sheet;", "    const char *cell, *input;", "} fn2_data[] = {"]
    for sheet, cell, text in DATA:
        lines.append("    { %d, %s, %s }," % (sheet, c_string(cell), c_string(text)))
    lines += ["};", "", "static const unsigned fn2_hidden_rows[] = { %s };" % ", ".join(str(r) for r in HIDDEN_ROWS), "",
              "static const struct {", "    const char *formula, *want;", "    double tol;", "} fn2_cases[] = {"]
    for formula, want, tol in CASES:
        lines.append("    { %s, %s, %s }," % (c_string(formula), c_string(want), repr(tol)))
    lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    text = table()
    if len(sys.argv) > 2 and sys.argv[1] == "--check":
        with open(sys.argv[2], encoding="utf-8") as f:
            if f.read() != text:
                sys.exit("%s is not what fn2_expect.py makes: run python3 tests/calc/fn2_expect.py > %s" % (sys.argv[2], sys.argv[2]))
        print("%s: %d cases, as fn2_expect.py makes them" % (sys.argv[2], len(CASES)))
    else:
        sys.stdout.write(text)
