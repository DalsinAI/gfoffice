/*
 * owf.library's maths: the <math.h> calls the formula engine (libowf/calc)
 * makes, and strtod, for a library that links no C library.
 *
 * Why its own: libnix's maths call mathieeedoubtrans.library and
 * mathieeedoubbas.library from the program's task, and those set FPCR in
 * the task that opens them (openamigagcc docs/libnix-fpcr.md); owf.library
 * runs in every caller's task. These are plain C on doubles (the
 * arithmetic is owf_softdouble.c's), so they need no FPU and no library,
 * keep no state, and are tested on the PC against its C library
 * (tests/library/test_owf_libm.c), alone and inside the engine's own tests.
 *
 * Written from the definitions and textbook methods, nothing ported:
 *   - sqrt, floor, ceil and fmod work on the bits and are exact;
 *   - log and exp: a table of log(1 + j/32) and of 2^(j/32) (both as two
 *     doubles), then short Taylor series, carried in double-double where
 *     it matters, so pow(x, y) = exp(y log x) keeps its accuracy for large
 *     y and gives exact answers exactly (10^15, 2^-3);
 *   - sin, cos and tan: the argument reduced by pi/2 against 192 bits of
 *     2/pi taken from a 1280-bit table where the exponent puts them
 *     (Payne and Hanek's method), right for every double, then Taylor
 *     series on |r| <= pi/4;
 *   - atan: atan(c) for c = i/16 from a table, then the series of
 *     atan((x - c)/(1 + xc)); asin, acos and atan2 go through it;
 *   - sinh, cosh and tanh from exp and an exp(x) - 1 on the same tables,
 *     in double-double.
 * The constants are worked out at 420 digits by
 * tests/library/gen_libm_tables.py, which `make test` runs to check them.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_libm.h"
#include "owf_internal.h"

#include <stddef.h>

typedef unsigned long long u64;
typedef unsigned int u32;

#define SIGN 0x8000000000000000ULL
#define FRAC 0x000FFFFFFFFFFFFFULL
#define HIDE 0x0010000000000000ULL
#define INF_BITS 0x7FF0000000000000ULL
#define ONE_BITS 0x3FF0000000000000ULL

/* ---- tables (gen_libm_tables.py) ---- */
#define LN2_HI 0x1.62e42fefa3800p-1   /* log(2) to 42 bits: k * LN2_HI is exact */
#define LN2_LO 0x1.ef35793c76730p-45
#define LN2_32_HI 0x1.62e42fefa0000p-6   /* log(2)/32 to 37 bits: N * LN2_32_HI is exact */
#define LN2_32_LO 0x1.cf79abc9e3b3ap-45
#define INV_LN2_32 0x1.71547652b82fep+5
#define PIO2_HI 0x1.921fb54442d18p+0
#define PIO2_LO 0x1.1a62633145c07p-54
#define PI_HI 0x1.921fb54442d18p+1
#define PI_LO 0x1.1a62633145c07p-53
#define INV_LN10_HI 0x1.bcb7b1526e50ep-2
#define INV_LN10_LO 0x1.95355baaafad3p-57

/* log(1 + j/32), j = -9..13, as hi and lo */
static const double logc[23][2] = {
    { -0x1.522ae0738a3d8p-2, 0x1.8f7e9b38a6979p-57 },
    { -0x1.269621134db92p-2, -0x1.e0efadd9db02bp-56 },
    { -0x1.f991c6cb3b379p-3, -0x1.f665066f980a2p-57 },
    { -0x1.a93ed3c8ad9e3p-3, -0x1.bcafa9de97203p-57 },
    { -0x1.5bf406b543db2p-3, 0x1.1f5b44c0df7e7p-61 },
    { -0x1.1178e8227e47cp-3, 0x1.0e63a5f01c691p-58 },
    { -0x1.9335e5d594989p-4, 0x1.478a85704ccb7p-58 },
    { -0x1.08598b59e3a07p-4, 0x1.dd7009902bf32p-58 },
    { -0x1.0415d89e74444p-5, -0x1.c05cf1d753622p-59 },
    { 0x0.0p+0, 0x0.0p+0 },
    { 0x1.f829b0e783300p-6, 0x1.33e3f04f1ef23p-60 },
    { 0x1.f0a30c01162a6p-5, 0x1.85f325c5bbacdp-59 },
    { 0x1.6f0d28ae56b4cp-4, -0x1.906d99184b992p-58 },
    { 0x1.e27076e2af2e6p-4, -0x1.61578001e0162p-60 },
    { 0x1.29552f81ff523p-3, 0x1.301771c407dbfp-57 },
    { 0x1.5ff3070a793d4p-3, -0x1.bc60efafc6f6ep-58 },
    { 0x1.9525a9cf456b4p-3, 0x1.d904c1d4e2e26p-57 },
    { 0x1.c8ff7c79a9a22p-3, -0x1.4f689f8434012p-57 },
    { 0x1.fb9186d5e3e2bp-3, -0x1.caaae64f21acbp-57 },
    { 0x1.1675cababa60ep-2, 0x1.ce63eab883717p-61 },
    { 0x1.2e8e2bae11d31p-2, -0x1.8f4cdb95ebdf9p-56 },
    { 0x1.4618bc21c5ec2p-2, 0x1.f42decdeccf1dp-56 },
    { 0x1.5d1bdbf5809cap-2, 0x1.4236383dc7fe1p-56 },
};

/* 2^(j/32), j = 0..31, as hi and lo */
static const double two_j32[32][2] = {
    { 0x1.0000000000000p+0, 0x0.0p+0 },
    { 0x1.059b0d3158574p+0, 0x1.d73e2a475b465p-55 },
    { 0x1.0b5586cf9890fp+0, 0x1.8a62e4adc610bp-54 },
    { 0x1.11301d0125b51p+0, -0x1.6c51039449b3ap-54 },
    { 0x1.172b83c7d517bp+0, -0x1.19041b9d78a76p-55 },
    { 0x1.1d4873168b9aap+0, 0x1.e016e00a2643cp-54 },
    { 0x1.2387a6e756238p+0, 0x1.9b07eb6c70573p-54 },
    { 0x1.29e9df51fdee1p+0, 0x1.612e8afad1255p-55 },
    { 0x1.306fe0a31b715p+0, 0x1.6f46ad23182e4p-55 },
    { 0x1.371a7373aa9cbp+0, -0x1.63aeabf42eae2p-54 },
    { 0x1.3dea64c123422p+0, 0x1.ada0911f09ebcp-55 },
    { 0x1.44e086061892dp+0, 0x1.89b7a04ef80d0p-59 },
    { 0x1.4bfdad5362a27p+0, 0x1.d4397afec42e2p-56 },
    { 0x1.5342b569d4f82p+0, -0x1.07abe1db13cadp-55 },
    { 0x1.5ab07dd485429p+0, 0x1.6324c054647adp-54 },
    { 0x1.6247eb03a5585p+0, -0x1.383c17e40b497p-54 },
    { 0x1.6a09e667f3bcdp+0, -0x1.bdd3413b26456p-54 },
    { 0x1.71f75e8ec5f74p+0, -0x1.16e4786887a99p-55 },
    { 0x1.7a11473eb0187p+0, -0x1.41577ee04992fp-55 },
    { 0x1.82589994cce13p+0, -0x1.d4c1dd41532d8p-54 },
    { 0x1.8ace5422aa0dbp+0, 0x1.6e9f156864b27p-54 },
    { 0x1.93737b0cdc5e5p+0, -0x1.75fc781b57ebcp-57 },
    { 0x1.9c49182a3f090p+0, 0x1.c7c46b071f2bep-56 },
    { 0x1.a5503b23e255dp+0, -0x1.d2f6edb8d41e1p-54 },
    { 0x1.ae89f995ad3adp+0, 0x1.7a1cd345dcc81p-54 },
    { 0x1.b7f76f2fb5e47p+0, -0x1.5584f7e54ac3bp-56 },
    { 0x1.c199bdd85529cp+0, 0x1.11065895048ddp-55 },
    { 0x1.cb720dcef9069p+0, 0x1.503cbd1e949dbp-56 },
    { 0x1.d5818dcfba487p+0, 0x1.2ed02d75b3707p-55 },
    { 0x1.dfc97337b9b5fp+0, -0x1.1a5cd4f184b5cp-54 },
    { 0x1.ea4afa2a490dap+0, -0x1.e9c23179c2893p-54 },
    { 0x1.f50765b6e4540p+0, 0x1.9d3e12dd8a18bp-54 },
};

/* atan(i/16), i = 0..16, as hi and lo */
static const double atanc[17][2] = {
    { 0x0.0p+0, 0x0.0p+0 },
    { 0x1.ff55bb72cfdeap-5, -0x1.c934d86d23f1dp-60 },
    { 0x1.fd5ba9aac2f6ep-4, -0x1.cd37686760c17p-59 },
    { 0x1.7b97b4bce5b02p-3, 0x1.347b0b4f881cap-58 },
    { 0x1.f5b75f92c80ddp-3, 0x1.8ab6e3cf7afbdp-57 },
    { 0x1.362773707ebccp-2, -0x1.963a544b672d8p-57 },
    { 0x1.6f61941e4def1p-2, -0x1.c63aae6f6e918p-56 },
    { 0x1.a64eec3cc23fdp-2, -0x1.24dec1b50b7ffp-56 },
    { 0x1.dac670561bb4fp-2, 0x1.a2b7f222f65e2p-56 },
    { 0x1.0657e94db30d0p-1, -0x1.d5b495f6349e6p-56 },
    { 0x1.1e00babdefeb4p-1, -0x1.928df287a668fp-58 },
    { 0x1.345f01cce37bbp-1, 0x1.1021137c71102p-55 },
    { 0x1.4978fa3269ee1p-1, 0x1.2419a87f2a458p-56 },
    { 0x1.5d58987169b18p-1, 0x1.0028e4bc5e7cap-57 },
    { 0x1.700a7c5784634p-1, -0x1.8c34d25aadef6p-56 },
    { 0x1.819d0b7158a4dp-1, -0x1.bf76229d3b917p-56 },
    { 0x1.921fb54442d18p-1, 0x1.1a62633145c07p-55 },
};

/* 2/pi: its first 1280 bits after the binary point, 32 to a word */
static const unsigned long two_over_pi[40] = {
    0xA2F9836EUL, 0x4E441529UL, 0xFC2757D1UL, 0xF534DDC0UL, 0xDB629599UL, 0x3C439041UL,
    0xFE5163ABUL, 0xDEBBC561UL, 0xB7246E3AUL, 0x424DD2E0UL, 0x06492EEAUL, 0x09D1921CUL,
    0xFE1DEB1CUL, 0xB129A73EUL, 0xE88235F5UL, 0x2EBB4484UL, 0xE99C7026UL, 0xB45F7E41UL,
    0x3991D639UL, 0x835339F4UL, 0x9C845F8BUL, 0xBDF9283BUL, 0x1FF897FFUL, 0xDE05980FUL,
    0xEF2F118BUL, 0x5A0A6D1FUL, 0x6D367ECFUL, 0x27CB09B7UL, 0x4F463F66UL, 0x9E5FEA2DUL,
    0x7527BAC7UL, 0xEBE5F17BUL, 0x3D0739F7UL, 0x8A5292EAUL, 0x6BFB5FB1UL, 0x1F8D5D08UL,
    0x56033046UL, 0xFC7B6BABUL, 0xF0CFBC20UL, 0x9AF4361DUL,
};
/* ---- end of tables ---- */

#define SQRT2 0x1.6a09e667f3bcdp+0
#define PIO4_BITS 0x3FE921FB54442D18ULL      /* pi/4, rounded */

/* ---- bits ---------------------------------------------------------------- */

union dbits { double d; u64 u; };
static u64 bits(double x) { union dbits v; v.d = x; return v.u; }
static double from(u64 u) { union dbits v; v.u = u; return v.d; }
static int expo(u64 b) { return (int)((b >> 52) & 0x7FF); }
static int is_nan(u64 b) { return (b & ~SIGN) > INF_BITS; }
static double pow2(int k) { return from((u64)(k + 1023) << 52); }     /* -1022 <= k <= 1023 */
static double qnan(void) { return from(0x7FF8000000000000ULL); }
static double quiet(double x) { return from(bits(x) | 0x0008000000000000ULL); }
static double infinity(int negative) { return from(INF_BITS | (negative ? SIGN : 0)); }
static double with_sign(double x, int negative) { return from((bits(x) & ~SIGN) | (negative ? SIGN : 0)); }

/* a finite nonzero b's significand with bit 52 set: the value is
 * m * 2^(*e - 1075) */
static u64 sig53(u64 b, int *e)
{
    u64 m = b & FRAC;
    int x = expo(b);
    if (x) {
        *e = x;
        return m | HIDE;
    }
    *e = 1;
    while (!(m & HIDE)) {
        m <<= 1;
        (*e)--;
    }
    return m;
}

/* ---- double-double steps ---------------------------------------------------
 * a + b and a * b as a rounded double and the exact error (Knuth's and
 * Dekker's), so sums and products can be carried to about 106 bits. */

static double two_sum(double a, double b, double *err)
{
    double s = a + b, bb = s - a;
    *err = (a - (s - bb)) + (b - bb);
    return s;
}

static double fast_two_sum(double a, double b, double *err)     /* |a| >= |b|, or a == 0 */
{
    double s = a + b;
    *err = b - (s - a);
    return s;
}

static double two_prod(double a, double b, double *err)
{
    double p = a * b, t, ah, al, bh, bl;
    t = 134217729.0 * a;                         /* 2^27 + 1 */
    ah = t - (t - a);
    al = a - ah;
    t = 134217729.0 * b;
    bh = t - (t - b);
    bl = b - bh;
    *err = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    return p;
}

/* (ah + al) - (bh + bl) as a double-double */
static double dd_sub(double ah, double al, double bh, double bl, double *lo)
{
    double e, s = two_sum(ah, -bh, &e);
    return fast_two_sum(s, e + (al - bl), lo);
}

/* ---- exact ---------------------------------------------------------------- */

double owf_rt_fabs(double x)
{
    return from(bits(x) & ~SIGN);
}

/* towards minus infinity (up 0) or plus infinity (up 1) */
static double whole_part(double x, int up)
{
    u64 b = bits(x), mask;
    int e = expo(b), negative = (b & SIGN) != 0;
    if (e == 0x7FF)
        return is_nan(b) ? quiet(x) : x;
    if (e >= 1075)
        return x;                                /* already whole */
    if (e < 1023) {                              /* |x| < 1 */
        if (!(b & ~SIGN))
            return x;
        if (up)
            return negative ? from(SIGN) : 1.0;
        return negative ? -1.0 : 0.0;
    }
    mask = FRAC >> (e - 1023);
    if (!(b & mask))
        return x;
    b &= ~mask;
    if (negative != up)
        b += mask + 1;                           /* away from zero: a carry may reach the exponent */
    return from(b);
}

double owf_rt_floor(double x) { return whole_part(x, 0); }
double owf_rt_ceil(double x) { return whole_part(x, 1); }

double owf_rt_fmod(double x, double y)
{
    u64 bx = bits(x), by = bits(y), mx, my, sign = bx & SIGN;
    int ex, ey, n;
    if (is_nan(bx))
        return quiet(x);
    if (is_nan(by))
        return quiet(y);
    if (expo(bx) == 0x7FF || !(by & ~SIGN))
        return qnan();
    if (expo(by) == 0x7FF || (bx & ~SIGN) < (by & ~SIGN))
        return x;
    mx = sig53(bx, &ex);
    my = sig53(by, &ey);
    /* mx * 2^(ex - ey) mod my, a bit at a time */
    for (n = ex - ey; n > 0; n--) {
        if (mx >= my)
            mx -= my;
        mx <<= 1;
    }
    if (mx >= my)
        mx -= my;
    if (!mx)
        return from(sign);
    while (!(mx & HIDE)) {
        mx <<= 1;
        ey--;
    }
    if (ey >= 1)
        return from(sign | (((u64)(ey - 1) << 52) + mx));
    return from(sign | (mx >> (1 - ey)));        /* subnormal: the bits dropped are zeros */
}

/* correctly rounded: the square root of the significand a bit at a time */
double owf_rt_sqrt(double x)
{
    u64 b = bits(x), m, q = 0, r = 0, t;
    int e, i, round;
    if (!(b & ~SIGN))
        return x;                                /* +-0 */
    if (is_nan(b))
        return quiet(x);
    if (b & SIGN)
        return qnan();
    if (expo(b) == 0x7FF)
        return x;
    m = sig53(b, &e);
    e -= 1075;                                   /* x = m * 2^e */
    if (e & 1) {
        m <<= 1;
        e--;
    }
    /* q = floor(sqrt(m * 2^54)): 54 bits, from two bits of m * 2^54 at a time */
    for (i = 0; i < 54; i++) {
        int pos = 106 - 2 * i;
        r = (r << 2) | (pos >= 54 ? (m >> (pos - 54)) & 3 : 0);
        t = (q << 2) | 1;
        q <<= 1;
        if (r >= t) {
            r -= t;
            q |= 1;
        }
    }
    round = (int)(q & 1);
    q >>= 1;
    if (round && (r || (q & 1)))
        q++;
    /* sqrt(x) = q * 2^((e - 54) / 2 + 1); q's bit 52 is the implicit bit */
    return from(((u64)((e - 54) / 2 + 1 + 1075 - 1) << 52) + q);
}

/* ---- log and exp ---------------------------------------------------------- */

/* log(x) for finite x > 0, as hi + *lo, to about 2^-66 of it */
static double log_dd(double x, double *lo)
{
    u64 b = bits(x);
    int e, k, j;
    double m, c, d, f, fl, p, pe, q, qe, t, s1, e1, tail, hi, he, a, ae, s, se;
    u64 mb = sig53(b, &e);
    k = e - 1023;
    m = from((mb & FRAC) | ONE_BITS);            /* x = m * 2^k, m in [1, 2) */
    if (m > SQRT2) {
        m *= 0.5;
        k++;
    }
    /* m = c (1 + f), c = 1 + j/32 the nearest, |f| < 0.0222 */
    j = (int)((m - 1.0) * 32.0 + (m >= 1.0 ? 0.5 : -0.5));
    c = 1.0 + j * 0.03125;
    d = m - c;                                   /* exact */
    f = d / c;
    p = two_prod(f, c, &pe);
    fl = ((d - p) - pe) / c;                     /* f + fl is d/c to 106 bits */
    /* log(1 + f) = f - f^2/2 + f^3/3 - ..., to f^14 */
    q = two_prod(f, f, &qe);
    t = f * q * (1.0 / 3 - f * (1.0 / 4 - f * (1.0 / 5 - f * (1.0 / 6 - f * (1.0 / 7 - f * (1.0 / 8 -
        f * (1.0 / 9 - f * (1.0 / 10 - f * (1.0 / 11 - f * (1.0 / 12 - f * (1.0 / 13 - f * (1.0 / 14))))))))))));
    s1 = two_sum(f, -0.5 * q, &e1);
    tail = (((t - f * fl) - 0.5 * qe) + fl) + e1;
    hi = fast_two_sum(s1, tail, &he);
    /* + k log 2 + log c */
    a = two_sum(k * LN2_HI, logc[j + 9][0], &ae);
    ae += k * LN2_LO + logc[j + 9][1];
    s = two_sum(a, hi, &se);
    se += ae + he;
    return fast_two_sum(s, se, lo);
}

/* x * 2^k for y in [0.5, 4) */
static double scale(double y, int k)
{
    if (k > 1000)
        return y * pow2(1000) * pow2(k - 1000);  /* infinity past the largest double */
    if (k < -1021)
        return y * pow2(k + 100) * pow2(-100);   /* rounded once, into the subnormals */
    return y * pow2(k);
}

/* exp(x + xl), xl tiny beside x */
static double exp_dd(double x, double xl)
{
    double t, dn, r_hi, r_lo, r, p, th, tl;
    int n, j;
    if (x > 710.0)
        return infinity(0);
    if (x < -746.0)
        return 0.0;
    /* x = n log(2)/32 + r, |r| <= log(2)/64; exp(x) = 2^(n/32) exp(r) */
    t = x * INV_LN2_32;
    n = (int)(t + (t >= 0 ? 0.5 : -0.5));
    dn = n;
    r_hi = x - dn * LN2_32_HI;                   /* exact */
    r_lo = xl - dn * LN2_32_LO;
    r = r_hi + r_lo;
    /* exp(r) - 1 = r_hi + p */
    p = r_lo + r * r * (0.5 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720 + r * (1.0 / 5040))))));
    j = n & 31;
    th = two_j32[j][0];
    tl = two_j32[j][1];
    return scale(th + (tl + th * (r_hi + p)), (n - j) / 32);
}

double owf_rt_log(double x)
{
    u64 b = bits(x);
    double lo;
    if (is_nan(b))
        return quiet(x);
    if (!(b & ~SIGN))
        return infinity(1);
    if (b & SIGN)
        return qnan();
    if (expo(b) == 0x7FF)
        return x;
    return log_dd(x, &lo);
}

double owf_rt_log10(double x)
{
    u64 b = bits(x);
    double hi, lo, p, pe;
    if (is_nan(b) || !(b & ~SIGN) || (b & SIGN) || expo(b) == 0x7FF)
        return owf_rt_log(x);
    hi = log_dd(x, &lo);
    p = two_prod(hi, INV_LN10_HI, &pe);
    pe += hi * INV_LN10_LO + lo * INV_LN10_HI;
    return p + pe;
}

double owf_rt_exp(double x)
{
    if (is_nan(bits(x)))
        return quiet(x);
    return exp_dd(x, 0.0);
}

/* 1 when y is a whole number (finite); *odd its parity */
static int whole(u64 b, int *odd)
{
    int e = expo(b);
    *odd = 0;
    if (e == 0x7FF)
        return 0;
    if (!(b & ~SIGN))
        return 1;
    if (e < 1023)
        return 0;
    if (e >= 1075) {
        *odd = e == 1075 && (b & 1);
        return 1;
    }
    if (b & (FRAC >> (e - 1023)))
        return 0;
    *odd = (int)((((b & FRAC) | HIDE) >> (1075 - e)) & 1);
    return 1;
}

/* C99's pow, its special cases as Annex F has them */
double owf_rt_pow(double x, double y)
{
    u64 bx = bits(x), by = bits(y), ax = bx & ~SIGN;
    int odd, integer, negative = 0, xneg = (bx & SIGN) != 0, yneg = (by & SIGN) != 0;
    double l, ll, z, zl, r;
    if (!(by & ~SIGN) || bx == ONE_BITS)
        return 1.0;                              /* x^0, 1^y: even for NaN */
    if (is_nan(bx))
        return quiet(x);
    if (is_nan(by))
        return quiet(y);
    integer = whole(by, &odd);
    if (expo(by) == 0x7FF) {                     /* y infinite */
        if (ax == ONE_BITS)
            return 1.0;
        return (ax > ONE_BITS) != yneg ? infinity(0) : 0.0;
    }
    if (!ax)                                     /* x = +-0 */
        return yneg ? infinity(odd && xneg) : from(odd && xneg ? SIGN : 0);
    if (expo(bx) == 0x7FF) {                     /* x infinite */
        if (xneg)
            return yneg ? from(odd ? SIGN : 0) : infinity(odd);
        return yneg ? 0.0 : infinity(0);
    }
    if (xneg) {
        if (!integer)
            return qnan();
        negative = odd;
    }
    if (ax == ONE_BITS)
        return negative ? -1.0 : 1.0;            /* -1 to a whole power */
    if (expo(by) > 1023 + 62)                    /* |y| >= 2^63: far past the doubles either way */
        return (ax > ONE_BITS) != yneg ? infinity(0) : 0.0;
    l = log_dd(from(ax), &ll);
    z = two_prod(y, l, &zl);
    zl += y * ll;
    r = exp_dd(z, zl);
    return negative ? -r : r;
}

/* ---- sin, cos and tan --------------------------------------------------- */

/* 32 bits of 2/pi from bit i (bit 1 is the first after the point) */
static u32 two_over_pi_bits(int i)
{
    int pos = i - 1, w = pos >> 5, sh = pos & 31;
    u32 v = (u32)two_over_pi[w] << sh;
    if (sh)
        v |= (u32)(two_over_pi[w + 1] >> (32 - sh));
    return v;
}

/* cnt (<= 64) bits of the 256-bit p from bit pos up */
static u64 limb_bits(const u32 *p, int pos, int cnt)
{
    int li = pos >> 5, sh = pos & 31;
    u64 v = (u64)(li < 8 ? p[li] : 0) | ((u64)(li + 1 < 8 ? p[li + 1] : 0) << 32);
    v >>= sh;
    if (sh)
        v |= (u64)(li + 2 < 8 ? p[li + 2] : 0) << (64 - sh);
    if (cnt < 64)
        v &= (1ULL << cnt) - 1;
    return v;
}

/* x (finite, |x| >= pi/4) = n pi/2 + r, |r| <= pi/4 or a hair more, as
 * *r_hi + *r_lo; returns n mod 4. x = m 2^e; x 2/pi mod 4 is m times the
 * bits of 2/pi from 2^(1 - e) to 192 bits further (the ones before only
 * add multiples of 4), so the fraction has 190 bits or more, of which the
 * last 54 or so are not sure: more than the 61 a double ever cancels. */
static int rem_pio2(double x, double *r_hi, double *r_lo)
{
    u64 b = bits(x), m = (b & FRAC) | HIDE, f1, f2, hi53, lo53, t;
    u32 w[6], p[8], m0 = (u32)m, m1 = (u32)(m >> 32);
    int e = expo(b) - 1075, i0 = e - 1 > 1 ? e - 1 : 1, s, i, n, lz = 0, minus = 0;
    double dh, dl, ph, pe, rh, rl;
    for (i = 0; i < 6; i++)
        w[5 - i] = two_over_pi_bits(i0 + 32 * i);
    for (i = 0; i < 8; i++)
        p[i] = 0;
    for (i = 0; i < 6; i++) {                    /* p = m * w */
        t = (u64)m0 * w[i] + p[i];
        p[i] = (u32)t;
        t = (t >> 32) + (u64)m1 * w[i] + p[i + 1];
        p[i + 1] = (u32)t;
        p[i + 2] += (u32)(t >> 32);
    }
    s = i0 + 191 - e;                            /* bits after the point */
    n = (int)limb_bits(p, s, 2);
    f1 = limb_bits(p, s - 64, 64);
    f2 = limb_bits(p, s - 128, 64);
    if (f1 >> 63) {                              /* a half or more: to the next n, from below */
        n++;
        f2 = 0 - f2;
        f1 = ~f1 + (f2 == 0);
        minus = 1;
    }
    if (!f1) {
        f1 = f2;
        f2 = 0;
        lz = 64;
    }
    while (f1 && !(f1 >> 63)) {
        f1 = (f1 << 1) | (f2 >> 63);
        f2 <<= 1;
        lz++;
    }
    hi53 = f1 >> 11;
    lo53 = ((f1 & 0x7FF) << 42) | (f2 >> 22);
    dh = (double)hi53 * pow2(-53 - lz);
    dl = (double)lo53 * pow2(-106 - lz);
    ph = two_prod(dh, PIO2_HI, &pe);
    pe += dh * PIO2_LO + dl * PIO2_HI;
    rh = fast_two_sum(ph, pe, &rl);
    if (minus != ((b & SIGN) != 0)) {
        rh = -rh;
        rl = -rl;
    }
    if (b & SIGN)
        n = -n;
    *r_hi = rh;
    *r_lo = rl;
    return n & 3;
}

/* the Taylor series on |x| <= pi/4, x + xl */
static double sin_k(double x, double xl)
{
    double z = x * x;
    double s = z * x * (-1.0 / 6 + z * (1.0 / 120 + z * (-1.0 / 5040 + z * (1.0 / 362880 + z * (-1.0 / 39916800 +
               z * (1.0 / 6227020800.0 + z * (-1.0 / 1307674368000.0 + z * (1.0 / 355687428096000.0))))))));
    return x + (s + xl * (1.0 - 0.5 * z));
}

static double cos_k(double x, double xl)
{
    double ze, z = two_prod(x, x, &ze), hz = 0.5 * z, w = 1.0 - hz, err = (1.0 - w) - hz;
    double poly = z * z * (1.0 / 24 + z * (-1.0 / 720 + z * (1.0 / 40320 + z * (-1.0 / 3628800 + z * (1.0 / 479001600.0 +
                  z * (-1.0 / 87178291200.0 + z * (1.0 / 20922789888000.0 + z * (-1.0 / 6402373705728000.0))))))));
    return w + (((err - 0.5 * ze) + poly) - x * xl);
}

/* which: 0 sin, 1 cos, 2 tan */
static double trig(double x, int which)
{
    u64 b = bits(x), a = b & ~SIGN;
    double r = x, rl = 0.0, s, c;
    int n = 0;
    if (expo(b) == 0x7FF)
        return is_nan(b) ? quiet(x) : qnan();
    if (a < 0x3E40000000000000ULL)               /* |x| < 2^-27 */
        return which == 1 ? 1.0 : x;
    if (a > PIO4_BITS)
        n = rem_pio2(x, &r, &rl);
    switch (which) {
    case 0:
        s = (n & 1) ? cos_k(r, rl) : sin_k(r, rl);
        return (n & 2) ? -s : s;
    case 1:
        c = (n & 1) ? sin_k(r, rl) : cos_k(r, rl);
        return (n == 1 || n == 2) ? -c : c;
    default:
        s = sin_k(r, rl);
        c = cos_k(r, rl);
        return (n & 1) ? -c / s : s / c;
    }
}

double owf_rt_sin(double x) { return trig(x, 0); }
double owf_rt_cos(double x) { return trig(x, 1); }
double owf_rt_tan(double x) { return trig(x, 2); }

/* ---- atan and its kin ---------------------------------------------------- */

/* atan(a + al) for 0 <= a <= 1, as hi + *lo: atan(c) + atan(t), c = i/16
 * the nearest, t = (a - c)/(1 + ac), |t| <= 1/32 */
static double atan_core(double a, double al, double *lo)
{
    int i = (int)(a * 16.0 + 0.5);
    double c = i * 0.0625, t, tl, d, de, p, pe, z, s, h, he;
    if (!i) {
        t = a;
        tl = al;
    } else {
        p = two_prod(a, c, &pe);
        d = two_sum(1.0, p, &de);
        de += pe + al * c;                       /* 1 + ac = d + de */
        t = (a - c) / d;                         /* a - c is exact */
        p = two_prod(t, d, &pe);
        tl = ((((a - c) - p) - pe) + al - t * de) / d;
    }
    z = t * t;
    s = t * z * (-1.0 / 3 + z * (1.0 / 5 + z * (-1.0 / 7 + z * (1.0 / 9 + z * (-1.0 / 11 + z * (1.0 / 13))))));
    h = two_sum(atanc[i][0], t, &he);
    he += atanc[i][1] + (tl - tl * z) + s;
    return fast_two_sum(h, he, lo);
}

/* pi/2 - (h + l) and pi - (h + l) as double-doubles */
static double from_pio2(double h, double l, double *lo) { return dd_sub(PIO2_HI, PIO2_LO, h, l, lo); }
static double from_pi(double h, double l, double *lo) { return dd_sub(PI_HI, PI_LO, h, l, lo); }

/* n/d, 0 <= n <= d, as q + *ql */
static double quotient(double n, double nl, double d, double dl, double *ql)
{
    double pe, q = n / d, p = two_prod(q, d, &pe);
    *ql = ((((n - p) - pe) + nl) - q * dl) / d;
    return q;
}

double owf_rt_atan(double x)
{
    u64 b = bits(x), a = b & ~SIGN;
    double h, l, u, ul;
    if (is_nan(b))
        return quiet(x);
    if (a < 0x3E40000000000000ULL)               /* |x| < 2^-27 */
        return x;
    if (a <= ONE_BITS) {
        h = atan_core(from(a), 0.0, &l);
    } else if (a >= 0x43C0000000000000ULL) {     /* |x| >= 2^61, infinity too */
        h = PIO2_HI;
    } else {
        u = quotient(1.0, 0.0, from(a), 0.0, &ul);
        h = atan_core(u, ul, &l);
        h = from_pio2(h, l, &l);
        h += l;
    }
    return (b & SIGN) ? -h : h;
}

double owf_rt_atan2(double y, double x)
{
    u64 by = bits(y), bx = bits(x), ay = by & ~SIGN, ax = bx & ~SIGN;
    int yneg = (by & SIGN) != 0, xneg = (bx & SIGN) != 0, ey, ex;
    double n, d, q, ql, h, l, fy, fx;
    if (is_nan(bx))
        return quiet(x);
    if (is_nan(by))
        return quiet(y);
    if (!ay)
        return with_sign(xneg ? PI_HI : 0.0, yneg);
    if (!ax)
        return with_sign(PIO2_HI, yneg);
    if (expo(by) == 0x7FF) {
        if (expo(bx) != 0x7FF)
            return with_sign(PIO2_HI, yneg);
        if (!xneg)
            return with_sign(PIO2_HI * 0.5, yneg);
        h = dd_sub(PI_HI, PI_LO, PIO2_HI * 0.5, PIO2_LO * 0.5, &l);
        return with_sign(h + l, yneg);
    }
    if (expo(bx) == 0x7FF)
        return with_sign(xneg ? PI_HI : 0.0, yneg);
    /* both finite and nonzero: scale them alike so no product overflows */
    fy = from(ay);
    fx = from(ax);
    ey = expo(ay);
    ex = expo(ax);
    if ((ey > ex ? ey : ex) >= 1023 + 995) {     /* a split would overflow; what goes subnormal gives q = 0 anyway */
        fy *= pow2(-100);
        fx *= pow2(-100);
    } else if ((ey > ex ? ey : ex) < 1023 - 500) {  /* exact */
        fy *= pow2(600);
        fx *= pow2(600);
    }
    if (bits(fy) <= bits(fx)) {
        n = fy;
        d = fx;
    } else {
        n = fx;
        d = fy;
    }
    q = quotient(n, 0.0, d, 0.0, &ql);
    if (bits(q) < 0x0370000000000000ULL) {       /* q < 2^-968: atan(q) is q */
        h = q;
        l = 0.0;
    } else {
        h = atan_core(q, ql, &l);
    }
    if (bits(fy) > bits(fx))
        h = from_pio2(h, l, &l);
    if (xneg)
        h = from_pi(h, l, &l);
    return with_sign(h + l, yneg);
}

/* sqrt(1 - a^2) for 0 <= a <= 1, as hi + *lo */
static double cos_of_sin(double a, double *lo)
{
    double w, we, p, pe, s, c, b, be;
    if (a < 0.5) {
        p = two_prod(a, a, &pe);
        w = fast_two_sum(1.0, -p, &we);
        we -= pe;
    } else {
        c = 1.0 - a;                             /* exact */
        b = fast_two_sum(1.0, a, &be);
        w = two_prod(c, b, &we);
        we += c * be;
    }
    if (!(bits(w) & ~SIGN)) {
        *lo = 0.0;
        return 0.0;
    }
    s = owf_rt_sqrt(w);
    p = two_prod(s, s, &pe);
    *lo = (((w - p) - pe) + we) / (2.0 * s);
    return s;
}

/* asin (which 0) or acos (1) */
static double arc(double x, int which)
{
    u64 b = bits(x), a = b & ~SIGN;
    double fa = from(a), s, sl, q, ql, h, l;
    if (is_nan(b))
        return quiet(x);
    if (a > ONE_BITS)
        return qnan();
    if (!which && a < 0x3E40000000000000ULL)
        return x;
    s = cos_of_sin(fa, &sl);
    /* asin(a) = atan2(a, s), acos(a) = atan2(s, a) */
    if (which == 0 ? bits(fa) <= bits(s) : bits(s) <= bits(fa)) {
        q = which == 0 ? quotient(fa, 0.0, s, sl, &ql) : quotient(s, sl, fa, 0.0, &ql);
        h = atan_core(q, ql, &l);
    } else {
        q = which == 0 ? quotient(s, sl, fa, 0.0, &ql) : quotient(fa, 0.0, s, sl, &ql);
        h = atan_core(q, ql, &l);
        h = from_pio2(h, l, &l);
    }
    if (which == 0)
        return with_sign(h + l, (b & SIGN) != 0);
    if (b & SIGN)
        h = from_pi(h, l, &l);
    return h + l;
}

double owf_rt_asin(double x) { return arc(x, 0); }
double owf_rt_acos(double x) { return arc(x, 1); }

/* ---- sinh, cosh, tanh --------------------------------------------------- */

/* exp(x) - 1 as hi + *lo, for |x| <= 50, without losing the small ones:
 * 2^(n/32) exp(r) - 1 with every part exact but exp(r)'s last terms */
static double expm1_dd(double x, double *lo)
{
    double t, r_hi, r_lo, r, p, th, tl, u, ue, tail, a, ae, s, se;
    int n, j;
    if ((bits(x) & ~SIGN) < 0x3C90000000000000ULL) {   /* |x| < 2^-54 */
        *lo = 0.0;
        return x;
    }
    t = x * INV_LN2_32;
    n = (int)(t + (t >= 0 ? 0.5 : -0.5));
    r_hi = x - n * LN2_32_HI;                    /* exact */
    r_lo = -(n * LN2_32_LO);
    r = r_hi + r_lo;
    p = r_lo + r * r * (0.5 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720 + r * (1.0 / 5040))))));
    if (!n)
        return fast_two_sum(r_hi, p, lo);        /* exp(r) - 1 = r_hi + p */
    j = n & 31;
    th = two_j32[j][0] * pow2((n - j) / 32);
    tl = two_j32[j][1] * pow2((n - j) / 32);
    u = two_prod(th, r_hi, &ue);
    tail = ue + tl + th * p + tl * (r_hi + p);
    a = two_sum(th, -1.0, &ae);
    s = two_sum(a, u, &se);
    return fast_two_sum(s, (ae + se) + tail, lo);
}

double owf_rt_sinh(double x)
{
    u64 b = bits(x), a = b & ~SIGN;
    double fa = from(a), e, el, d, dl, q, ql, h, hl, r;
    if (is_nan(b))
        return quiet(x);
    if (a < 0x3E30000000000000ULL)               /* |x| < 2^-28 */
        return x;
    if (fa < 22.0) {                             /* (E + E/(E + 1)) / 2, E = exp(|x|) - 1 */
        e = expm1_dd(fa, &el);
        d = two_sum(e, 1.0, &dl);
        dl += el;
        q = quotient(e, el, d, dl, &ql);
        h = two_sum(e, q, &hl);
        r = 0.5 * (h + (hl + el + ql));
    } else if (fa < 709.0) {
        r = 0.5 * owf_rt_exp(fa);
    } else {
        e = owf_rt_exp(0.5 * fa);
        r = (0.5 * e) * e;
    }
    return (b & SIGN) ? -r : r;
}

double owf_rt_cosh(double x)
{
    u64 b = bits(x), a = b & ~SIGN;
    double fa = from(a), e, el;
    if (is_nan(b))
        return quiet(x);
    if (a < 0x3E40000000000000ULL)               /* |x| < 2^-27 */
        return 1.0;
    if (fa < 0.34657359027997264) {              /* log(2)/2 */
        e = expm1_dd(fa, &el);
        return 1.0 + (e * e) / (2.0 * (1.0 + e));
    }
    if (fa < 22.0) {
        e = owf_rt_exp(fa);
        return 0.5 * e + 0.5 / e;
    }
    if (fa < 709.0)
        return 0.5 * owf_rt_exp(fa);
    e = owf_rt_exp(0.5 * fa);
    return (0.5 * e) * e;
}

double owf_rt_tanh(double x)
{
    u64 b = bits(x), a = b & ~SIGN;
    double fa = from(a), e, el, d, dl, q, ql;
    if (is_nan(b))
        return quiet(x);
    if (a < 0x3E30000000000000ULL)               /* |x| < 2^-28 */
        return x;
    if (fa < 22.0) {                             /* E/(E + 2), E = exp(2|x|) - 1 */
        e = expm1_dd(2.0 * fa, &el);
        d = two_sum(e, 2.0, &dl);
        dl += el;
        q = quotient(e, el, d, dl, &ql);
        q += ql;
    } else {
        q = 1.0;
    }
    return (b & SIGN) ? -q : q;
}

/* ---- strtod --------------------------------------------------------------- */

static int starts_word(const char *p, const char *word)
{
    for (; *word; p++, word++)
        if ((*p | 0x20) != *word)
            return 0;
    return 1;
}

double owf_rt_strtod(const char *s, char **end)
{
    const char *p = s, *num, *q;
    int negative = 0, digits = 0;
    double v;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    num = q = p;
    if (*q == '+' || *q == '-')
        negative = *q++ == '-';
    if (starts_word(q, "inf")) {
        q += starts_word(q, "infinity") ? 8 : 3;
        if (end)
            *end = (char *)q;
        return infinity(negative);
    }
    if (starts_word(q, "nan")) {
        q += 3;
        if (*q == '(') {
            const char *c = q + 1;
            while ((*c >= '0' && *c <= '9') || ((*c | 0x20) >= 'a' && (*c | 0x20) <= 'z') || *c == '_')
                c++;
            if (*c == ')')
                q = c + 1;
        }
        if (end)
            *end = (char *)q;
        return with_sign(qnan(), negative);
    }
    while (*q >= '0' && *q <= '9') {
        q++;
        digits++;
    }
    if (*q == '.')
        for (q++; *q >= '0' && *q <= '9'; q++)
            digits++;
    if (!digits) {
        if (end)
            *end = (char *)s;
        return 0.0;
    }
    if (*q == 'e' || *q == 'E') {
        const char *e = q + 1;
        if (*e == '+' || *e == '-')
            e++;
        if (*e >= '0' && *e <= '9') {
            while (*e >= '0' && *e <= '9')
                e++;
            q = e;
        }
    }
    if (end)
        *end = (char *)q;
    if (owf_number_parse(num, (size_t)(q - num), &v))
        return v;
    return infinity(negative);                   /* beyond a double */
}
