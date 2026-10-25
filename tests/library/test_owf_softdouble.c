/*
 * owf.library's double arithmetic (libowf/library/owf_softdouble.c, built
 * with -DOSD_HOST_TEST) against the PC's own IEEE doubles (round to nearest
 * even), bit for bit. Every NaN counts as the same answer.
 *
 *   test-owf-softdouble [count]
 *
 * The arithmetic and compare checks are amigachrome-guest's
 * tests/native/open_softdouble_test.c (main, blob f069118), unchanged; the
 * conversion checks are new here, with owf_softdouble.c's conversions.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

uint64_t osd_adddf3(uint64_t, uint64_t);
uint64_t osd_subdf3(uint64_t, uint64_t);
uint64_t osd_muldf3(uint64_t, uint64_t);
uint64_t osd_divdf3(uint64_t, uint64_t);
int osd_cmpdf(uint64_t, uint64_t);
uint64_t osd_mul32_test(uint32_t, uint32_t);

uint64_t osd_floatsidf(uint32_t);
uint64_t osd_floatunsidf(uint32_t);
uint64_t osd_floatdidf(uint64_t);
uint64_t osd_floatundidf(uint64_t);
uint32_t osd_fixdfsi(uint64_t);
uint32_t osd_fixunsdfsi(uint64_t);
uint64_t osd_fixdfdi(uint64_t);
uint64_t osd_fixunsdfdi(uint64_t);
static uint64_t s = 0x9E3779B97F4A7C15ULL;
static uint64_t rnd(void) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
static uint64_t b(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double f(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static int nan_(uint64_t u) { return (u & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL; }

static long fails;
static void check(const char *op, uint64_t x, uint64_t y, uint64_t got, uint64_t want)
{
    if (got == want || (nan_(got) && nan_(want))) return;
    if (fails++ < 20)
        printf("FAIL %s %016llx %016llx: got %016llx want %016llx (%a %s %a)\n", op,
               (unsigned long long)x, (unsigned long long)y, (unsigned long long)got,
               (unsigned long long)want, f(x), op, f(y));
}

static void one(uint64_t x, uint64_t y)
{
    double a = f(x), c = f(y);
    int want;
    check("+", x, y, osd_adddf3(x, y), b(a + c));
    check("-", x, y, osd_subdf3(x, y), b(a - c));
    check("*", x, y, osd_muldf3(x, y), b(a * c));
    check("/", x, y, osd_divdf3(x, y), b(a / c));
    want = isunordered(a, c) ? 2 : a < c ? -1 : a > c ? 1 : 0;
    if (osd_cmpdf(x, y) != want && fails++ < 20)
        printf("FAIL cmp %a %a: got %d want %d\n", a, c, osd_cmpdf(x, y), want);
}

/* a random double: any bits, or a narrow exponent, or a short significand */
static uint64_t pick(void)
{
    uint64_t r = rnd();
    switch (rnd() % 6) {
    case 0: return r;
    case 1: return (r & 0x800FFFFFFFFFFFFFULL) | ((uint64_t)(1023 + (int)(rnd() % 64) - 32) << 52);
    case 2: return r & 0x800FFFFFFFFFFFFFULL;                      /* subnormal */
    case 3: return (r & 0x8000000000000000ULL) | ((uint64_t)(rnd() % 0x7FF) << 52) | (rnd() & 0xFF);
    case 4: return (r & 0xFFF0000000000000ULL) | ((1ULL << (rnd() % 53)) - 1);
    default: return (r & 0x800FFFFFFFFFFFFFULL) | ((uint64_t)(0x7FE - rnd() % 4) << 52);
    }
}

/* ---- conversions (owf_softdouble.c's own) ---- */

static void conv_check(const char *what, uint64_t in, uint64_t got, uint64_t want)
{
    if (got != want && fails++ < 20)
        printf("FAIL %s %016llx: got %016llx want %016llx\n", what, (unsigned long long)in,
               (unsigned long long)got, (unsigned long long)want);
}

/* whole numbers to double: every value is defined */
static void from_ints(uint64_t r)
{
    uint32_t w = (uint32_t)r;
    conv_check("int->double", w, osd_floatsidf(w), b((double)(int32_t)w));
    conv_check("unsigned->double", w, osd_floatunsidf(w), b((double)w));
    conv_check("long long->double", r, osd_floatdidf(r), b((double)(int64_t)r));
    conv_check("unsigned long long->double", r, osd_floatundidf(r), b((double)r));
}

/* double to whole numbers, where C defines it; outside, ours stop at the
 * type's limits (and NaN gives 0) */
static void to_ints(uint64_t x)
{
    double d = f(x);
    if (d != d) {
        conv_check("NaN->int", x, osd_fixdfsi(x), 0);
        conv_check("NaN->long long", x, osd_fixdfdi(x), 0);
        return;
    }
    if (d > -2147483649.0 && d < 2147483648.0)
        conv_check("double->int", x, osd_fixdfsi(x), (uint32_t)(int32_t)d);
    else
        conv_check("double->int (limit)", x, osd_fixdfsi(x), d < 0 ? 0x80000000U : 0x7FFFFFFFU);
    if (d > -1.0 && d < 4294967296.0)
        conv_check("double->unsigned", x, osd_fixunsdfsi(x), (uint32_t)d);
    else
        conv_check("double->unsigned (limit)", x, osd_fixunsdfsi(x), d < 0 ? 0 : 0xFFFFFFFFU);
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0)
        conv_check("double->long long", x, osd_fixdfdi(x), (uint64_t)(int64_t)d);
    else
        conv_check("double->long long (limit)", x, osd_fixdfdi(x),
                   d < 0 ? 0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFULL);
    if (d > -1.0 && d < 18446744073709551616.0)
        conv_check("double->unsigned long long", x, osd_fixunsdfdi(x), (uint64_t)d);
    else
        conv_check("double->unsigned long long (limit)", x, osd_fixunsdfdi(x), d < 0 ? 0 : ~0ULL);
}

static void conversions(long n)
{
    static const uint64_t ints[] = {
        0, 1, 0x7FFFFFFFULL, 0x80000000ULL, 0xFFFFFFFFULL, 0x1FFFFFFFFFFFFFULL, 0x20000000000001ULL,
        0x20000000000003ULL, 0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFC00ULL, 0xFFFFFFFFFFFFFBFFULL, 0x8000000000000400ULL, 0x8000000000000401ULL,
    };
    static const double reals[] = {
        0.0, -0.0, 0.5, -0.5, 0.999999, -0.999999, 1.5, -1.5, 2147483647.0, 2147483647.75, -2147483648.0,
        -2147483648.5, 2147483648.0, 4294967295.0, 4294967295.5, 4294967296.0, 9007199254740993.0,
        9223372036854774784.0, 9223372036854775808.0, -9223372036854775808.0, 18446744073709549568.0,
        18446744073709551616.0, 1e300, -1e300, 1.0 / 0.0, -1.0 / 0.0, 4.9e-324,
    };
    long i;
    unsigned j;
    for (j = 0; j < sizeof ints / sizeof ints[0]; j++) {
        from_ints(ints[j]);
        from_ints(0 - ints[j]);
    }
    for (j = 0; j < sizeof reals / sizeof reals[0]; j++)
        to_ints(b(reals[j]));
    to_ints(0x7FF8000000000000ULL);
    for (i = 0; i < n; i++) {
        uint64_t r = rnd();
        from_ints(r);
        from_ints(r >> (rnd() % 64));
        to_ints(pick());
        /* whole numbers and halves near the limits of each type */
        to_ints(b((double)(int64_t)(r >> (rnd() % 64)) + (double)(rnd() % 3) * 0.5));
    }
}

int main(int argc, char **argv)
{
    static const uint64_t sp[] = {
        0, 0x8000000000000000ULL, 0x7FF0000000000000ULL, 0xFFF0000000000000ULL,
        0x7FF8000000000000ULL, 0x7FF0000000000001ULL, 1, 0x800FFFFFFFFFFFFFULL,
        0x0010000000000000ULL, 0x000FFFFFFFFFFFFFULL, 0x7FEFFFFFFFFFFFFFULL,
        0x3FF0000000000000ULL, 0xBFF0000000000000ULL, 0x3FF0000000000001ULL,
        0x3CA0000000000000ULL, 0x3C90000000000000ULL, 0x4340000000000000ULL,
    };
    long n = argc > 1 ? atol(argv[1]) : 3000000, i;
    unsigned j, k;
    for (j = 0; j < sizeof sp / sizeof sp[0]; j++)
        for (k = 0; k < sizeof sp / sizeof sp[0]; k++) one(sp[j], sp[k]);
    for (i = 0; i < n; i++) {
        uint64_t x = pick(), y = pick();
        one(x, y);
        /* close neighbours: cancellation in + and - */
        one(x, (x ^ 0x8000000000000000ULL) + (rnd() % 5) - 2);
        one(x, x + (rnd() % 5) - 2);
    }
    for (i = 0; i < 1000000; i++) {
        uint32_t x = (uint32_t)rnd(), y = (uint32_t)rnd();
        if (osd_mul32_test(x, y) != (uint64_t)x * y && fails++ < 20) printf("FAIL mul32 %u %u\n", x, y);
    }
    conversions(n / 3 + 1);
    printf("%ld cases x 5 ops, conversions: %ld x 8, %ld failures\n", n * 3, (n / 3 + 1) * 4, fails);
    return fails != 0;
}
