/*
 * owf.library's double arithmetic: the helpers GCC calls for doubles in
 * soft-float code (-m68040 without -m68881), so the library's workbook and
 * formula code links with -nostdlib.
 *
 * In this toolchain libgcc has no double arithmetic: programs get __adddf3
 * and the rest from libnix's libm, which calls mathieeedoubbas.library in
 * the program's own task. A shared library can't do that: those libraries
 * set FPCR in the task that opens them (openamigagcc docs/libnix-fpcr.md),
 * and owf.library runs in its callers' tasks. These work on the bits, so
 * they need no FPU and no other library, give the same answers in every
 * task, and are tested bit for bit against the PC's doubles
 * (tests/library/test_owf_softdouble.c).
 *
 * Add, subtract, multiply, divide, negate and the compares are
 * amigachrome-guest's libraries/common/open_softdouble.c (main, blob
 * e5c2e17, last changed in 21d15ec), unchanged below its header: correctly
 * rounded, to nearest with ties to even, with subnormals, infinities and
 * NaNs as IEEE 754 has them. The conversions to and from int, unsigned and
 * their 64-bit forms are new here (Team 5, W118, 25 October 2026), so that
 * libgcc's conversion objects, which also define some of the compares, are
 * never linked beside these.
 *
 * Built with -DOSD_HOST_TEST the functions are named osd_* and work on the
 * bits, for the tests.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */

typedef unsigned long long osd_u64;
typedef unsigned int osd_u32;
typedef unsigned short osd_u16;

#define OSD_SIGN 0x8000000000000000ULL
#define OSD_FRAC 0x000FFFFFFFFFFFFFULL
#define OSD_HIDE 0x0010000000000000ULL          /* the implicit bit, 2^52 */
#define OSD_INF  0x7FF0000000000000ULL
#define OSD_QNAN 0x7FF8000000000000ULL
#define OSD_TOP  (1ULL << 55)                   /* leading bit of a working significand */

static int osd_isnan(osd_u64 a) { return (a & ~OSD_SIGN) > OSD_INF; }
static int osd_exp(osd_u64 a) { return (int)((a >> 52) & 0x7FF); }

/* a significand with its implicit bit (or none, subnormal: exponent 1) */
static osd_u64 osd_sig(osd_u64 a, int *e)
{
    int x = osd_exp(a);
    if (x) { *e = x; return (a & OSD_FRAC) | OSD_HIDE; }
    *e = 1;
    return a & OSD_FRAC;
}

/* shift right, the bits shifted out kept as a sticky bit 0 */
static osd_u64 osd_shr(osd_u64 m, int n)
{
    if (n <= 0) return m;
    if (n > 63) return m != 0;
    return (m >> n) | ((m & ((1ULL << n) - 1)) != 0);
}

/* m: leading bit at 55 for a normal number (below it only when e == 1),
 * three guard bits under the 53 kept; e the biased exponent. Rounds and packs. */
static osd_u64 osd_pack(osd_u64 sign, int e, osd_u64 m)
{
    osd_u32 low;
    if (e >= 0x7FF) return sign | OSD_INF;
    if (e < 1) { m = osd_shr(m, 1 - e); e = 1; }
    low = (osd_u32)(m & 7);
    m >>= 3;
    if (low > 4 || (low == 4 && (m & 1))) m++;
    /* (e - 1) << 52 plus m with its implicit bit is e << 52 plus the fraction;
     * a carry out of the significand moves the exponent up by one, and a
     * subnormal that rounds up to 2^52 becomes the smallest normal */
    return sign | (((osd_u64)(e - 1) << 52) + m);
}

static osd_u64 osd_nan(osd_u64 a, osd_u64 b)
{
    if (osd_isnan(a)) return a | 0x0008000000000000ULL;
    if (osd_isnan(b)) return b | 0x0008000000000000ULL;
    return OSD_QNAN;
}

static osd_u64 osd_add(osd_u64 a, osd_u64 b)
{
    int ea, eb, d;
    osd_u64 ma, mb, m, sign;
    if (osd_exp(a) == 0x7FF || osd_exp(b) == 0x7FF) {
        if (osd_isnan(a) || osd_isnan(b)) return osd_nan(a, b);
        if (osd_exp(a) == 0x7FF && osd_exp(b) == 0x7FF && ((a ^ b) & OSD_SIGN)) return OSD_QNAN;
        return osd_exp(a) == 0x7FF ? a : b;
    }
    if ((a & ~OSD_SIGN) < (b & ~OSD_SIGN)) { osd_u64 t = a; a = b; b = t; }
    sign = a & OSD_SIGN;
    ma = osd_sig(a, &ea) << 3;
    mb = osd_sig(b, &eb) << 3;
    d = ea - eb;
    mb = osd_shr(mb, d);
    if (!((a ^ b) & OSD_SIGN)) {
        m = ma + mb;
        if (m & (OSD_TOP << 1)) { m = osd_shr(m, 1); ea++; }
    } else {
        m = ma - mb;
        if (!m) return 0;                       /* x - x is +0 to nearest */
        while (!(m & OSD_TOP) && ea > 1) { m <<= 1; ea--; }
    }
    return osd_pack(sign, ea, m);
}

static osd_u64 osd_sub(osd_u64 a, osd_u64 b)
{
    return osd_add(a, b ^ OSD_SIGN);
}

/* 32 x 32 to 64 from the 68000's 16 x 16 multiplies, no helper called */
static osd_u64 osd_mul32(osd_u32 x, osd_u32 y)
{
    osd_u32 xl = (osd_u16)x, xh = x >> 16, yl = (osd_u16)y, yh = y >> 16;
    osd_u32 ll = (osd_u32)(osd_u16)xl * (osd_u32)(osd_u16)yl;    /* unsigned: no int overflow */
    osd_u32 lh = (osd_u32)(osd_u16)xl * (osd_u32)(osd_u16)yh;
    osd_u32 hl = (osd_u32)(osd_u16)xh * (osd_u32)(osd_u16)yl;
    osd_u32 hh = (osd_u32)(osd_u16)xh * (osd_u32)(osd_u16)yh;
    osd_u64 mid = (osd_u64)lh + hl;
    return ((osd_u64)hh << 32) + (mid << 16) + ll;
}

/* bring a subnormal's significand up to the implicit bit */
static osd_u64 osd_norm(osd_u64 m, int *e)
{
    while (!(m & OSD_HIDE)) { m <<= 1; (*e)--; }
    return m;
}

static osd_u64 osd_mul(osd_u64 a, osd_u64 b)
{
    int ea, eb, e;
    osd_u64 ma, mb, sign = (a ^ b) & OSD_SIGN;
    osd_u64 p0, p1, p2, p3, mid, lo, hi, m;
    osd_u32 a0, a1, b0, b1;
    if (osd_isnan(a) || osd_isnan(b)) return osd_nan(a, b);
    if (osd_exp(a) == 0x7FF || osd_exp(b) == 0x7FF) {
        if (!(a & ~OSD_SIGN) || !(b & ~OSD_SIGN)) return OSD_QNAN;     /* inf * 0 */
        return sign | OSD_INF;
    }
    if (!(a & ~OSD_SIGN) || !(b & ~OSD_SIGN)) return sign;
    ma = osd_norm(osd_sig(a, &ea), &ea);
    mb = osd_norm(osd_sig(b, &eb), &eb);
    /* the 106-bit product, hi:lo */
    a0 = (osd_u32)ma; a1 = (osd_u32)(ma >> 32);
    b0 = (osd_u32)mb; b1 = (osd_u32)(mb >> 32);
    p0 = osd_mul32(a0, b0);
    p1 = osd_mul32(a0, b1);
    p2 = osd_mul32(a1, b0);
    p3 = osd_mul32(a1, b1);
    mid = (p0 >> 32) + (osd_u32)p1 + (osd_u32)p2;
    lo = (mid << 32) | (osd_u32)p0;
    hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    /* product in [2^104, 2^106): hi holds bits 64 up, lead at 104 or 105 */
    e = ea + eb - 1023;
    if (hi & (1ULL << 41)) {                    /* bit 105 */
        m = (hi << 14) | (lo >> 50);
        m |= (lo & ((1ULL << 50) - 1)) != 0;
        e++;
    } else {
        m = (hi << 15) | (lo >> 49);
        m |= (lo & ((1ULL << 49) - 1)) != 0;
    }
    return osd_pack(sign, e, m);
}

static osd_u64 osd_div(osd_u64 a, osd_u64 b)
{
    int ea, eb, e, i;
    osd_u64 ma, mb, q, r, sign = (a ^ b) & OSD_SIGN;
    if (osd_isnan(a) || osd_isnan(b)) return osd_nan(a, b);
    if (osd_exp(a) == 0x7FF) return osd_exp(b) == 0x7FF ? OSD_QNAN : sign | OSD_INF;
    if (osd_exp(b) == 0x7FF) return sign;
    if (!(b & ~OSD_SIGN)) return (a & ~OSD_SIGN) ? sign | OSD_INF : OSD_QNAN;
    if (!(a & ~OSD_SIGN)) return sign;
    ma = osd_norm(osd_sig(a, &ea), &ea);
    mb = osd_norm(osd_sig(b, &eb), &eb);
    e = ea - eb + 1023;
    if (ma < mb) { ma <<= 1; e--; }
    /* 56 quotient bits, the first a one, then the remainder as sticky */
    r = ma; q = 0;
    for (i = 0; i < 56; i++) {
        q <<= 1;
        if (r >= mb) { r -= mb; q |= 1; }
        r <<= 1;
    }
    q |= r != 0;
    return osd_pack(sign, e, q);
}

/* -1, 0, 1, or 2 when unordered */
static int osd_cmp(osd_u64 a, osd_u64 b)
{
    if (osd_isnan(a) || osd_isnan(b)) return 2;
    if (!((a | b) & ~OSD_SIGN)) return 0;      /* +0 and -0 */
    if ((a ^ b) & OSD_SIGN) return (a & OSD_SIGN) ? -1 : 1;
    if (a == b) return 0;
    if (a & OSD_SIGN) return a > b ? -1 : 1;
    return a < b ? -1 : 1;
}

/* ---- conversions (new in owf.library) ------------------------------------ */

typedef long long osd_s64;

/* a whole number m (sign apart) as a double, rounded to nearest even */
static osd_u64 osd_from_u64(osd_u64 sign, osd_u64 m)
{
    int p = 63;
    if (!m) return 0;
    while (!(m >> p)) p--;
    if (p <= 55) m <<= 55 - p;
    else m = osd_shr(m, p - 55);
    return osd_pack(sign, 1023 + p, m);
}

/* |a| truncated towards zero; *big set when it is 2^64 or more, or NaN */
static osd_u64 osd_to_u64(osd_u64 a, int *big)
{
    int e = osd_exp(a), shift;
    osd_u64 m;
    *big = 0;
    if (osd_isnan(a) || e >= 1023 + 64) { *big = 1; return 0; }
    if (e < 1023) return 0;
    m = (a & OSD_FRAC) | OSD_HIDE;
    shift = e - 1075;
    return shift >= 0 ? m << shift : m >> -shift;
}

static osd_u64 osd_floatsi(osd_u32 v)
{
    osd_u64 sign = (v & 0x80000000UL) ? OSD_SIGN : 0;
    return osd_from_u64(sign, sign ? (osd_u64)(0x100000000ULL - v) : v);
}

static osd_u64 osd_floatdi(osd_u64 v)
{
    osd_u64 sign = v & OSD_SIGN;
    return osd_from_u64(sign, sign ? 0 - v : v);
}

/* to int, unsigned and their 64-bit forms, towards zero; out of range (C
 * leaves it undefined) they stop at the type's limits, and NaN gives 0 */
static osd_u32 osd_fixsi(osd_u64 a)
{
    int big;
    osd_u64 m = osd_to_u64(a, &big);
    if (osd_isnan(a)) return 0;
    if (a & OSD_SIGN) return (big || m > 0x80000000ULL) ? 0x80000000UL : (osd_u32)(0 - m);
    return (big || m > 0x7FFFFFFFULL) ? 0x7FFFFFFFUL : (osd_u32)m;
}

static osd_u32 osd_fixunssi(osd_u64 a)
{
    int big;
    osd_u64 m = osd_to_u64(a, &big);
    if (osd_isnan(a) || (a & OSD_SIGN)) return 0;
    return (big || m > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (osd_u32)m;
}

static osd_u64 osd_fixdi(osd_u64 a)
{
    int big;
    osd_u64 m = osd_to_u64(a, &big);
    if (osd_isnan(a)) return 0;
    if (a & OSD_SIGN) return (big || m > OSD_SIGN) ? OSD_SIGN : 0 - m;
    return (big || m > OSD_SIGN - 1) ? OSD_SIGN - 1 : m;
}

static osd_u64 osd_fixunsdi(osd_u64 a)
{
    int big;
    osd_u64 m = osd_to_u64(a, &big);
    if (osd_isnan(a) || (a & OSD_SIGN)) return 0;
    return big ? ~0ULL : m;
}

#ifdef OSD_HOST_TEST
osd_u64 osd_adddf3(osd_u64 a, osd_u64 b) { return osd_add(a, b); }
osd_u64 osd_subdf3(osd_u64 a, osd_u64 b) { return osd_sub(a, b); }
osd_u64 osd_muldf3(osd_u64 a, osd_u64 b) { return osd_mul(a, b); }
osd_u64 osd_divdf3(osd_u64 a, osd_u64 b) { return osd_div(a, b); }
int osd_cmpdf(osd_u64 a, osd_u64 b) { return osd_cmp(a, b); }
osd_u64 osd_mul32_test(osd_u32 x, osd_u32 y) { return osd_mul32(x, y); }
osd_u64 osd_floatsidf(osd_u32 v) { return osd_floatsi(v); }
osd_u64 osd_floatunsidf(osd_u32 v) { return osd_from_u64(0, v); }
osd_u64 osd_floatdidf(osd_u64 v) { return osd_floatdi(v); }
osd_u64 osd_floatundidf(osd_u64 v) { return osd_from_u64(0, v); }
osd_u32 osd_fixdfsi(osd_u64 a) { return osd_fixsi(a); }
osd_u32 osd_fixunsdfsi(osd_u64 a) { return osd_fixunssi(a); }
osd_u64 osd_fixdfdi(osd_u64 a) { return osd_fixdi(a); }
osd_u64 osd_fixunsdfdi(osd_u64 a) { return osd_fixunsdi(a); }
#else
/* gcc's names and libgcc's answers for the compares: the sign of the answer
 * against zero is the relation, and unordered makes the relation false */
union osd_d { double d; osd_u64 u; };
static osd_u64 osd_b(double d) { union osd_d v; v.d = d; return v.u; }
static double osd_f(osd_u64 u) { union osd_d v; v.u = u; return v.d; }

double __adddf3(double a, double b);
double __subdf3(double a, double b);
double __muldf3(double a, double b);
double __divdf3(double a, double b);
double __negdf2(double a);
int __eqdf2(double a, double b);
int __nedf2(double a, double b);
int __ltdf2(double a, double b);
int __ledf2(double a, double b);
int __gtdf2(double a, double b);
int __gedf2(double a, double b);
int __unorddf2(double a, double b);
int __cmpdf2(double a, double b);
double __floatsidf(int v);
double __floatunsidf(unsigned v);
double __floatdidf(long long v);
double __floatundidf(unsigned long long v);
int __fixdfsi(double a);
unsigned __fixunsdfsi(double a);
long long __fixdfdi(double a);
unsigned long long __fixunsdfdi(double a);

double __adddf3(double a, double b) { return osd_f(osd_add(osd_b(a), osd_b(b))); }
double __subdf3(double a, double b) { return osd_f(osd_sub(osd_b(a), osd_b(b))); }
double __muldf3(double a, double b) { return osd_f(osd_mul(osd_b(a), osd_b(b))); }
double __divdf3(double a, double b) { return osd_f(osd_div(osd_b(a), osd_b(b))); }
double __negdf2(double a) { return osd_f(osd_b(a) ^ OSD_SIGN); }
int __eqdf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? 1 : c; }
int __nedf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? 1 : c; }
int __ltdf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? 1 : c; }
int __ledf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? 1 : c; }
int __gtdf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? -1 : c; }
int __gedf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? -1 : c; }
int __unorddf2(double a, double b) { return osd_cmp(osd_b(a), osd_b(b)) == 2; }
/* new here: the three-way compare some libgcc builds call, as __ledf2 */
int __cmpdf2(double a, double b) { int c = osd_cmp(osd_b(a), osd_b(b)); return c == 2 ? 1 : c; }
double __floatsidf(int v) { return osd_f(osd_floatsi((osd_u32)v)); }
double __floatunsidf(unsigned v) { return osd_f(osd_from_u64(0, v)); }
double __floatdidf(long long v) { return osd_f(osd_floatdi((osd_u64)v)); }
double __floatundidf(unsigned long long v) { return osd_f(osd_from_u64(0, v)); }
int __fixdfsi(double a) { return (int)osd_fixsi(osd_b(a)); }
unsigned __fixunsdfsi(double a) { return osd_fixunssi(osd_b(a)); }
long long __fixdfdi(double a) { return (long long)osd_fixdi(osd_b(a)); }
unsigned long long __fixunsdfdi(double a) { return osd_fixunsdi(osd_b(a)); }
#endif
