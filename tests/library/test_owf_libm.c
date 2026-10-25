/*
 * owf.library's maths and strtod (libowf/library/owf_libm.c) against the
 * PC's C library, which is within a unit in the last place (ulp) of the
 * true answers and nearly always correctly rounded.
 *
 *   test-owf-libm [count]        (default 200000 inputs a function)
 *
 * For each function: C99's special values (zeros, infinities, NaN, the
 * subnormals, the largest double) bit for bit, NaN as any NaN; then random
 * inputs over the ranges a spreadsheet meets and every double, with the
 * largest distance in ulps, which must stay within the function's limit;
 * then answers that must be exact (10^15, LOG10(1000), sqrt(2)^2...).
 * strtod: random decimal numbers of up to 40 digits, bit for bit with the
 * C library's, and where it stops.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_libm.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long failures, checks;

static uint64_t bits_of(double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

static double from_bits(uint64_t u)
{
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static uint64_t s = 0x2545F4914F6CDD1DULL;
static uint64_t rnd(void)
{
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

/* uniform in [a, b) */
static double uniform(double a, double b)
{
    return a + (b - a) * ((double)(rnd() >> 11) / 9007199254740992.0);
}

/* any finite double */
static double any_double(void)
{
    uint64_t u;
    do
        u = rnd();
    while (((u >> 52) & 0x7FF) == 0x7FF);
    return from_bits(u);
}

/* how many doubles apart; 0 for two NaNs, huge for a NaN and a number */
static uint64_t ulps(double a, double b)
{
    int64_t x, y;
    if (a != a || b != b)
        return (a != a && b != b) ? 0 : UINT64_MAX;
    x = (int64_t)bits_of(a);
    y = (int64_t)bits_of(b);
    if (x < 0)
        x = INT64_MIN - x;
    if (y < 0)
        y = INT64_MIN - y;
    return x > y ? (uint64_t)(x - y) : (uint64_t)(y - x);
}

static int same_bits(double a, double b)
{
    return (a != a && b != b) || bits_of(a) == bits_of(b);
}

typedef double (*fn1)(double);
typedef double (*fn2)(double, double);

struct stat {
    const char *name;
    uint64_t worst;
    double worst_at, worst_at2;
    long n, off;
};

static void note(struct stat *st, double got, double want, double x, double y)
{
    uint64_t u = ulps(got, want);
    st->n++;
    if (u)
        st->off++;
    if (u > st->worst) {
        st->worst = u;
        st->worst_at = x;
        st->worst_at2 = y;
    }
}

static void verdict(struct stat *st, uint64_t limit)
{
    checks++;
    if (st->worst > limit) {
        failures++;
        printf("FAIL %-6s worst %llu ulps (limit %llu) at %.17g %.17g\n", st->name, (unsigned long long)st->worst,
               (unsigned long long)limit, st->worst_at, st->worst_at2);
    } else {
        printf("ok   %-6s %ld inputs, %ld not the C library's last bit, worst %llu ulp\n", st->name, st->n,
               st->off, (unsigned long long)st->worst);
    }
}

static const double specials[] = {
    0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 1e-310, -1e-310, 4.9e-324, -4.9e-324, DBL_MIN, -DBL_MIN,
    DBL_MAX, -DBL_MAX, 1e300, -1e300, 710.0, -746.0, 709.7827128933840, 1e-20, 0.1, 22.0, 1e16, 9007199254740993.0,
    2.5, -2.5, 1.5, -1.5,
};
#define NSPECIAL (sizeof specials / sizeof specials[0])

static double special(unsigned i)
{
    if (i < NSPECIAL)
        return specials[i];
    if (i == NSPECIAL)
        return HUGE_VAL;
    if (i == NSPECIAL + 1)
        return -HUGE_VAL;
    return NAN;
}
#define NSPECIALS (NSPECIAL + 3)

static void special1(const char *name, fn1 ours, fn1 theirs)
{
    unsigned i;
    for (i = 0; i < NSPECIALS; i++) {
        double x = special(i), a = ours(x), b = theirs(x);
        checks++;
        /* the large or tiny values' last bit is the accuracy test's business */
        if (!same_bits(a, b) && !(a == a && b == b && ulps(a, b) <= 2 && a != 0 && b != 0 && !isinf(a) && !isinf(b))) {
            failures++;
            printf("FAIL %s(%a): %a, the C library %a\n", name, x, a, b);
        }
    }
}

static void special2(const char *name, fn2 ours, fn2 theirs)
{
    unsigned i, j;
    for (i = 0; i < NSPECIALS; i++)
        for (j = 0; j < NSPECIALS; j++) {
            double x = special(i), y = special(j), a = ours(x, y), b = theirs(x, y);
            checks++;
            if (!same_bits(a, b) && !(a == a && b == b && ulps(a, b) <= 2 && a != 0 && b != 0 && !isinf(a) && !isinf(b))) {
                failures++;
                printf("FAIL %s(%a, %a): %a, the C library %a\n", name, x, y, a, b);
            }
        }
}

/* the C library's, through function pointers (no builtins) */
static double c_fabs(double x) { return fabs(x); }
static double c_floor(double x) { return floor(x); }
static double c_ceil(double x) { return ceil(x); }
static double c_sqrt(double x) { return sqrt(x); }
static double c_exp(double x) { return exp(x); }
static double c_log(double x) { return log(x); }
static double c_log10(double x) { return log10(x); }
static double c_sin(double x) { return sin(x); }
static double c_cos(double x) { return cos(x); }
static double c_tan(double x) { return tan(x); }
static double c_asin(double x) { return asin(x); }
static double c_acos(double x) { return acos(x); }
static double c_atan(double x) { return atan(x); }
static double c_sinh(double x) { return sinh(x); }
static double c_cosh(double x) { return cosh(x); }
static double c_tanh(double x) { return tanh(x); }
static double c_fmod(double x, double y) { return fmod(x, y); }
static double c_pow(double x, double y) { return pow(x, y); }
static double c_atan2(double y, double x) { return atan2(y, x); }

static void exact(const char *what, double got, double want)
{
    checks++;
    if (!same_bits(got, want)) {
        failures++;
        printf("FAIL %s: %.17g, not %.17g\n", what, got, want);
    }
}

static void accuracy(long n)
{
    struct stat st;
    long i;
    int k;

#define RUN1(NAME, OURS, THEIRS, GEN, LIMIT)                                   \
    memset(&st, 0, sizeof st);                                                 \
    st.name = NAME;                                                            \
    for (i = 0; i < n; i++) {                                                  \
        double x = (GEN);                                                      \
        note(&st, OURS(x), THEIRS(x), x, 0);                                   \
    }                                                                          \
    verdict(&st, LIMIT)
#define RUN2(NAME, OURS, THEIRS, GENX, GENY, LIMIT)                            \
    memset(&st, 0, sizeof st);                                                 \
    st.name = NAME;                                                            \
    for (i = 0; i < n; i++) {                                                  \
        double x = (GENX), y = (GENY);                                         \
        note(&st, OURS(x, y), THEIRS(x, y), x, y);                             \
    }                                                                          \
    verdict(&st, LIMIT)

    RUN1("fabs", owf_rt_fabs, c_fabs, any_double(), 0);
    RUN1("floor", owf_rt_floor, c_floor, rnd() & 1 ? any_double() : uniform(-1e6, 1e6), 0);
    RUN1("ceil", owf_rt_ceil, c_ceil, rnd() & 1 ? any_double() : uniform(-1e6, 1e6), 0);
    RUN1("sqrt", owf_rt_sqrt, c_sqrt, fabs(any_double()), 0);
    RUN2("fmod", owf_rt_fmod, c_fmod, rnd() & 1 ? any_double() : uniform(-1e5, 1e5),
         rnd() & 1 ? any_double() : uniform(-100, 100), 0);
    RUN1("exp", owf_rt_exp, c_exp, rnd() & 1 ? uniform(-745, 709.78) : uniform(-2, 2), 1);
    RUN1("log", owf_rt_log, c_log, rnd() & 1 ? fabs(any_double()) : uniform(0.5, 2), 1);
    /* the C library's log10 is up to 2 ulps off itself (glibc's manual); ours,
     * against exact values at its worst points here, within half an ulp */
    RUN1("log10", owf_rt_log10, c_log10, rnd() & 1 ? fabs(any_double()) : uniform(0.5, 2), 2);
    RUN2("pow", owf_rt_pow, c_pow, rnd() & 1 ? uniform(0, 100) : uniform(0.9, 1.1),
         rnd() & 1 ? uniform(-30, 30) : uniform(-3000, 3000), 1);
    RUN2("pow-", owf_rt_pow, c_pow, -uniform(0, 20), (double)((int)(rnd() % 200) - 100), 1);
    RUN1("sin", owf_rt_sin, c_sin, rnd() % 3 ? uniform(-10, 10) : rnd() & 1 ? uniform(-1e9, 1e9) : any_double(), 1);
    RUN1("cos", owf_rt_cos, c_cos, rnd() % 3 ? uniform(-10, 10) : rnd() & 1 ? uniform(-1e9, 1e9) : any_double(), 1);
    RUN1("tan", owf_rt_tan, c_tan, rnd() % 3 ? uniform(-10, 10) : rnd() & 1 ? uniform(-1e9, 1e9) : any_double(), 2);
    RUN1("asin", owf_rt_asin, c_asin, rnd() & 1 ? uniform(-1, 1) : 1 - fabs(uniform(-1e-6, 1e-6)), 1);
    RUN1("acos", owf_rt_acos, c_acos, rnd() & 1 ? uniform(-1, 1) : (rnd() & 1 ? 1 : -1) * (1 - fabs(uniform(-1e-6, 1e-6))), 1);
    RUN1("atan", owf_rt_atan, c_atan, rnd() & 1 ? uniform(-20, 20) : any_double(), 1);
    RUN2("atan2", owf_rt_atan2, c_atan2, rnd() & 1 ? uniform(-10, 10) : any_double(),
         rnd() & 1 ? uniform(-10, 10) : any_double(), 1);
    RUN1("sinh", owf_rt_sinh, c_sinh, rnd() & 1 ? uniform(-710, 710) : uniform(-1, 1), 2);
    RUN1("cosh", owf_rt_cosh, c_cosh, rnd() & 1 ? uniform(-710, 710) : uniform(-1, 1), 2);
    RUN1("tanh", owf_rt_tanh, c_tanh, rnd() & 1 ? uniform(-25, 25) : uniform(-1, 1), 2);

    /* near the multiples of pi/2, where the reduction cancels most */
    memset(&st, 0, sizeof st);
    st.name = "sin~";
    for (i = 0; i < n / 10; i++) {
        double x = (double)(rnd() % 2000000) * 1.5707963267948966;
        x = from_bits(bits_of(x) + (rnd() % 7) - 3);
        note(&st, owf_rt_sin(x), sin(x), x, 0);
        note(&st, owf_rt_cos(x), cos(x), x, 0);
    }
    verdict(&st, 1);
    /* the double nearest a multiple of pi/2 there is (6381956970095103 * 2^797) */
    exact("sin(6381956970095103 * 2^797)", owf_rt_sin(ldexp(6381956970095103.0, 797)), sin(ldexp(6381956970095103.0, 797)));

    /* what must come out exact */
    for (k = -22; k <= 22; k++) {
        char what[40], num[16];
        double p;
        snprintf(num, sizeof num, "1e%d", k);
        p = strtod(num, NULL);
        snprintf(what, sizeof what, "pow(10, %d)", k);
        exact(what, owf_rt_pow(10, k), p);
        snprintf(what, sizeof what, "log10(1e%d)", k);
        exact(what, owf_rt_log10(p), (double)k);
    }
    for (k = -1074; k <= 1023; k += 7) {
        char what[40];
        snprintf(what, sizeof what, "pow(2, %d)", k);
        exact(what, owf_rt_pow(2, k), ldexp(1, k));
        snprintf(what, sizeof what, "log(2^%d)", k);
        exact(what, owf_rt_log(ldexp(1, k)), log(ldexp(1, k)));
    }
    exact("pow(-2, 3)", owf_rt_pow(-2, 3), -8);
    exact("pow(1.5, 2)", owf_rt_pow(1.5, 2), 2.25);
    exact("pow(16, 0.25)", owf_rt_pow(16, 0.25), 2);
    exact("pow(1.1, 2)", owf_rt_pow(1.1, 2), 1.1 * 1.1);
    exact("exp(0)", owf_rt_exp(0), 1);
    exact("exp(1)", owf_rt_exp(1), 2.718281828459045);
    exact("log(1)", owf_rt_log(1), 0);
    exact("sqrt(2)", owf_rt_sqrt(2), 1.4142135623730951);
    exact("sqrt(1e-320)", owf_rt_sqrt(1e-320), sqrt(1e-320));
    exact("atan(1) * 4", owf_rt_atan(1) * 4, 3.141592653589793);
    exact("acos(-1)", owf_rt_acos(-1), 3.141592653589793);
    exact("asin(1)", owf_rt_asin(1), 1.5707963267948966);
    exact("fmod(5.5, 2)", owf_rt_fmod(5.5, 2), 1.5);
    exact("fmod(-5.5, 2)", owf_rt_fmod(-5.5, 2), -1.5);
    exact("fmod(1e300, 3)", owf_rt_fmod(1e300, 3), fmod(1e300, 3));
    exact("floor(-0.5)", owf_rt_floor(-0.5), -1);
    exact("ceil(-0.5)", owf_rt_ceil(-0.5), -0.0);
    exact("tanh(1)", owf_rt_tanh(1), tanh(1));
}

static void special_values(void)
{
    special1("fabs", owf_rt_fabs, c_fabs);
    special1("floor", owf_rt_floor, c_floor);
    special1("ceil", owf_rt_ceil, c_ceil);
    special1("sqrt", owf_rt_sqrt, c_sqrt);
    special1("exp", owf_rt_exp, c_exp);
    special1("log", owf_rt_log, c_log);
    special1("log10", owf_rt_log10, c_log10);
    special1("sin", owf_rt_sin, c_sin);
    special1("cos", owf_rt_cos, c_cos);
    special1("tan", owf_rt_tan, c_tan);
    special1("asin", owf_rt_asin, c_asin);
    special1("acos", owf_rt_acos, c_acos);
    special1("atan", owf_rt_atan, c_atan);
    special1("sinh", owf_rt_sinh, c_sinh);
    special1("cosh", owf_rt_cosh, c_cosh);
    special1("tanh", owf_rt_tanh, c_tanh);
    special2("fmod", owf_rt_fmod, c_fmod);
    special2("pow", owf_rt_pow, c_pow);
    special2("atan2", owf_rt_atan2, c_atan2);
}

static void strtod_one(const char *text)
{
    char *e1, *e2;
    double a = owf_rt_strtod(text, &e1), b = strtod(text, &e2);
    checks++;
    if (!same_bits(a, b) || e1 != e2) {
        failures++;
        if (failures < 30)
            printf("FAIL strtod(\"%s\"): %a stopping at %d, the C library %a at %d\n", text, a, (int)(e1 - text), b,
                   (int)(e2 - text));
    }
}

static void strtod_tests(long n)
{
    static const char *const fixed[] = {
        "0", "-0", "1", "+1.5", "  12.5e3x", ".5", "5.", ".", "-.", "e5", "1e", "1e+", "1e-5", "1E400", "-1E400",
        "1e-400", "2.2250738585072011e-308", "2.2250738585072014e-308", "4.9406564584124654e-324",
        "2.4703282292062327e-324", "2.4703282292062328e-324", "1.7976931348623157e308", "1.7976931348623159e308",
        "0.1", "0.30000000000000004", "9007199254740993", "123456789012345678901234567890", "inf", "-Infinity",
        "INFx", "nan", "-NaN", "nan(123)", "\t\n 42", "1,234", "12abc", "", "-", "0x10",
    };
    unsigned i;
    long k;
    for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        const char *t = fixed[i];
        if (!strcmp(t, "0x10")) {              /* no hex here: 0, stopping at the x */
            char *e;
            double v = owf_rt_strtod(t, &e);
            checks++;
            if (v != 0 || e != t + 1) {
                failures++;
                printf("FAIL strtod(\"0x10\")\n");
            }
            continue;
        }
        if (strstr(t, "nan") || strstr(t, "NaN")) {
            char *e1, *e2;
            double a = owf_rt_strtod(t, &e1), b = strtod(t, &e2);
            checks++;
            if (a == a || b == b || e1 != e2 || signbit(a) != signbit(b)) {
                failures++;
                printf("FAIL strtod(\"%s\")\n", t);
            }
            continue;
        }
        strtod_one(t);
    }
    for (k = 0; k < n; k++) {
        char buf[80];
        int len = 0, digits = 1 + (int)(rnd() % 40), point = (int)(rnd() % (digits + 1)), j;
        if (rnd() & 1)
            buf[len++] = '-';
        for (j = 0; j < digits; j++) {
            if (j == point)
                buf[len++] = '.';
            buf[len++] = (char)('0' + rnd() % 10);
        }
        if (rnd() % 3)
            len += snprintf(buf + len, sizeof buf - len, "e%d", (int)(rnd() % 700) - 350);
        buf[len] = 0;
        strtod_one(buf);
    }
    /* every double, printed short and long, reads back as itself */
    for (k = 0; k < n; k++) {
        char buf[40];
        double d = any_double();
        snprintf(buf, sizeof buf, "%.17g", d);
        strtod_one(buf);
        snprintf(buf, sizeof buf, "%.15g", d);
        strtod_one(buf);
    }
}

int main(int argc, char **argv)
{
    long n = argc > 1 ? atol(argv[1]) : 200000;
    special_values();
    accuracy(n);
    strtod_tests(n / 4 + 1);
    printf("test-owf-libm: %ld checks, %ld failed\n", checks, failures);
    return failures != 0;
}
