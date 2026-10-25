/*
 * libowf: the formula engine's maths and trigonometry functions beyond
 * the first ones (owf_calc_fn.c): rounding to multiples, whole-number
 * functions, sums of squares, number bases and Roman numerals, the inverse
 * hyperbolic functions, matrices and SUBTOTAL. Excel's names, argument
 * counts and errors (owf_calc_fn.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_fn.h"

#define TWO53 9007199254740992.0         /* past it, whole numbers are not all doubles */

/* x truncated toward zero, a hair from a whole number taken as it. */
static double trunc_(double x)
{
    return x < 0 ? -calc_approx_floor(-x) : calc_approx_floor(x);
}

/* ---- Rounding to a multiple ---- */

/* MROUND(number, multiple): to the nearest multiple, halves away from
 * zero; signs that differ are #NUM! (Excel 2010 on). */
CF(mround)
{
    double x, m;
    NUM(0, 0, x);
    NUM(1, 0, m);
    if (m == 0 || x == 0) {
        cv_num(r, 0);
        return;
    }
    if ((x > 0) != (m > 0)) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, calc_round_to(x / m, 0, 0) * m);
}

/* CEILING.MATH, FLOOR.MATH(number, [significance = 1], [mode]): the
 * significance's sign does not count; a negative number goes toward zero,
 * or away from it with a mode that is not 0. */
static void math_round(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int up)
{
    double x, s, mode, q;
    NUM(0, 0, x);
    NUM(1, 1, s);
    NUM(2, 0, mode);
    s = fabs(s);
    if (s == 0 || x == 0) {
        cv_num(r, 0);
        return;
    }
    q = fabs(x) / s;
    if (x > 0) {
        q = up ? calc_approx_ceil(q) : calc_approx_floor(q);
    } else {
        /* away from zero: CEILING with a mode, FLOOR without one */
        int away = up ? mode != 0 : mode == 0;
        q = -(away ? calc_approx_ceil(q) : calc_approx_floor(q));
    }
    cv_num(r, q * s);
}

CF(ceiling_math)
{
    math_round(ctx, args, argc, r, 1);
}

CF(floor_math)
{
    math_round(ctx, args, argc, r, 0);
}

/* CEILING.PRECISE and ISO.CEILING, FLOOR.PRECISE(number, [significance =
 * 1]): up (toward +infinity) or down to a multiple of |significance|. */
static void precise(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int up)
{
    double x, s;
    NUM(0, 0, x);
    NUM(1, 1, s);
    s = fabs(s);
    if (s == 0 || x == 0) {
        cv_num(r, 0);
        return;
    }
    cv_num(r, (up ? calc_approx_ceil(x / s) : calc_approx_floor(x / s)) * s);
}

CF(ceiling_precise)
{
    precise(ctx, args, argc, r, 1);
}

CF(floor_precise)
{
    precise(ctx, args, argc, r, 0);
}

CF(quotient)
{
    double a, b;
    NUM(0, 0, a);
    NUM(1, 0, b);
    if (b == 0) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    cv_num(r, trunc_(a / b));
}

/* ---- Whole numbers ---- */

/* A whole-number argument for the combinatorial functions: truncated. */
#define WHOLE(i, def, var)                                                                                   \
    do {                                                                                                     \
        NUM(i, def, var);                                                                                    \
        (var) = trunc_(var);                                                                                 \
    } while (0)

static double gcd2(double a, double b)
{
    while (b != 0) {
        double t = fmod(a, b);
        a = b;
        b = t;
    }
    return a;
}

/* GCD and LCM of the numbers given (truncated); a negative one, or one
 * past 2^53, is #NUM!. */
static void gcd_lcm(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int lcm)
{
    calc_numlist l;
    double acc = lcm ? 1 : 0;
    unsigned long i;
    COLLECT(l, 0, argc);
    for (i = 0; i < l.n; i++) {
        double x = trunc_(l.v[i]);
        if (x < 0 || x >= TWO53) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        if (!lcm)
            acc = gcd2(acc, x);
        else if (x == 0 || acc == 0)
            acc = 0;
        else {
            acc = acc / gcd2(acc, x) * x;
            if (acc >= TWO53) {
                cv_err(r, OWF_ERROR_NUM);
                return;
            }
        }
    }
    cv_num(r, acc);
}

CF(gcd)
{
    gcd_lcm(ctx, args, argc, r, 0);
}

CF(lcm)
{
    gcd_lcm(ctx, args, argc, r, 1);
}

CF(factdouble)
{
    double n, f = 1;
    WHOLE(0, 0, n);
    if (n < 0 || n > 400) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (; n > 1; n -= 2)
        f *= n;
    cv_num(r, f);
}

/* C(n, k) for 0 <= k <= n (whole numbers); HUGE_VAL past a double. */
static double choose(double n, double k)
{
    double m = k < n - k ? k : n - k, c = 1, i;
    for (i = 1; i <= m; i++) {
        c = c * (n - m + i) / i;
        if (c < 4503599627370496.0)
            c = floor(c + 0.5);          /* whole while a double holds it */
        if (c > 1.7976931348623157e308)
            return HUGE_VAL;
    }
    return c;
}

CF(combin)
{
    double n, k;
    WHOLE(0, 0, n);
    WHOLE(1, 0, k);
    if (n < 0 || k < 0 || n < k) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, choose(n, k));
}

/* COMBINA(n, k): with repetitions, C(n + k - 1, k). */
CF(combina)
{
    double n, k;
    WHOLE(0, 0, n);
    WHOLE(1, 0, k);
    if (n < 0 || k < 0 || (n == 0 && k > 0)) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, k == 0 ? 1 : choose(n + k - 1, k));
}

/* PERMUT(n, k): n! / (n - k)!. As Excel's documentation: n <= 0 is #NUM!. */
CF(permut)
{
    double n, k, p = 1, i;
    WHOLE(0, 0, n);
    WHOLE(1, 0, k);
    if (n <= 0 || k < 0 || n < k) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < k && p <= 1.7976931348623157e308; i++)
        p *= n - i;
    cv_num(r, p);
}

CF(permutationa)
{
    double n, k;
    WHOLE(0, 0, n);
    WHOLE(1, 0, k);
    if (n < 0 || k < 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, k == 0 ? 1 : pow(n, k));
}

/* MULTINOMIAL(n1, n2...): (n1 + n2 + ...)! / (n1! n2! ...), as a product
 * of binomials so that nothing overflows on the way. */
CF(multinomial)
{
    calc_numlist l;
    double sum = 0, m = 1;
    unsigned long i;
    COLLECT(l, 0, argc);
    for (i = 0; i < l.n; i++) {
        double x = trunc_(l.v[i]);
        if (x < 0) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        sum += x;
        m *= choose(sum, x);
    }
    cv_num(r, m);
}

/* ---- Sums of squares over two arrays ---- */

/* Two array arguments' elements in pairs where both are numbers; a
 * different count of elements is #N/A; an error in either stops it. */
static void sum_pairs(calc_ctx *ctx, calc_value *args, calc_value *r, int which)
{
    calc_array *a, *b;
    unsigned long n, k;
    double sum = 0;
    int e;
    if (args[0].type == CV_ERR || args[1].type == CV_ERR) {
        *r = args[args[0].type == CV_ERR ? 0 : 1];
        return;
    }
    if ((e = calc_to_array(ctx, &args[0], &a)) != 0 || (e = calc_to_array(ctx, &args[1], &b)) != 0) {
        cv_err(r, e);
        return;
    }
    n = (unsigned long)a->rows * a->cols;
    if (n != (unsigned long)b->rows * b->cols) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    for (k = 0; k < n; k++) {
        const calc_value *x = &a->v[k], *y = &b->v[k];
        if (x->type == CV_ERR || y->type == CV_ERR) {
            cv_err(r, x->type == CV_ERR ? x->u.err : y->u.err);
            return;
        }
        if (x->type != CV_NUM || y->type != CV_NUM)
            continue;
        switch (which) {
        case 0: sum += x->u.num * x->u.num - y->u.num * y->u.num; break;
        case 1: sum += x->u.num * x->u.num + y->u.num * y->u.num; break;
        default: sum += (x->u.num - y->u.num) * (x->u.num - y->u.num); break;
        }
    }
    cv_num(r, sum);
}

CF(sumx2my2)
{
    (void)argc;
    sum_pairs(ctx, args, r, 0);
}

CF(sumx2py2)
{
    (void)argc;
    sum_pairs(ctx, args, r, 1);
}

CF(sumxmy2)
{
    (void)argc;
    sum_pairs(ctx, args, r, 2);
}

/* SERIESSUM(x, n, m, coefficients): a1 x^n + a2 x^(n+m) + ... */
CF(seriessum)
{
    double x, n, m, sum = 0;
    calc_array *a;
    unsigned long k, count;
    int e;
    NUM(0, 0, x);
    NUM(1, 0, n);
    NUM(2, 0, m);
    PASS_ERROR(3);
    if ((e = calc_to_array(ctx, &args[3], &a)) != 0) {
        cv_err(r, e);
        return;
    }
    count = (unsigned long)a->rows * a->cols;
    for (k = 0; k < count; k++) {
        const calc_value *c = &a->v[k];
        calc_value p;
        if (c->type == CV_ERR) {
            cv_err(r, c->u.err);
            return;
        }
        if (c->type != CV_NUM) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        calc_power(x, n + (double)k * m, &p);
        if (p.type == CV_ERR) {
            *r = p;
            return;
        }
        sum += c->u.num * p.u.num;
    }
    cv_num(r, sum);
}

/* ---- Number bases and Roman numerals ---- */

CF(base)
{
    double x, radix, len;
    char digits[64];
    int n = 0;
    owf_buf b;
    WHOLE(0, 0, x);
    WHOLE(1, 0, radix);
    WHOLE(2, 0, len);
    if (x < 0 || x >= TWO53 || radix < 2 || radix > 36 || len < 0 || len > 255) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    do {
        int d = (int)fmod(x, radix);
        digits[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        x = floor(x / radix);
    } while (x > 0);
    owf_buf_init(&b);
    while (len > n) {
        owf_buf_putc(&b, '0');
        len--;
    }
    while (n)
        owf_buf_putc(&b, digits[--n]);
    calc_take_text(ctx, &b, r);
}

CF(decimal)
{
    const char *p;
    unsigned n, i;
    double radix, v = 0;
    TEXT(0, p, n);
    WHOLE(1, 0, radix);
    if (radix < 2 || radix > 36 || n > 255) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < n; i++) {
        int c = (unsigned char)p[i], d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'Z')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z')
            d = c - 'a' + 10;
        else
            d = 99;
        if (d >= radix) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        v = v * radix + d;
    }
    if (v >= TWO53) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, v);
}

/* The numerals, largest first, with the form (0 to 4) from which each
 * subtractive pair is allowed: form 0 is the classic CM, CD, XC..., each
 * form after it lets a smaller numeral go before a larger one (499 is
 * CDXCIX, LDVLIV, XDIX, VDIV, ID), as Excel's documentation shows. */
static const struct {
    unsigned short value;
    char form;
    char text[3];
} numerals[] = {
    { 1000, 0, "M" }, { 999, 4, "IM" }, { 995, 3, "VM" }, { 990, 2, "XM" }, { 950, 1, "LM" }, { 900, 0, "CM" },
    { 500, 0, "D" }, { 499, 4, "ID" }, { 495, 3, "VD" }, { 490, 2, "XD" }, { 450, 1, "LD" }, { 400, 0, "CD" },
    { 100, 0, "C" }, { 99, 2, "IC" }, { 95, 1, "VC" }, { 90, 0, "XC" }, { 50, 0, "L" }, { 49, 2, "IL" },
    { 45, 1, "VL" }, { 40, 0, "XL" }, { 10, 0, "X" }, { 9, 0, "IX" }, { 5, 0, "V" }, { 4, 0, "IV" },
    { 1, 0, "I" },
};

CF(roman)
{
    double x, form = 0;
    unsigned long v;
    size_t i;
    owf_buf b;
    WHOLE(0, 0, x);
    if (argc > 1 && args[1].type == CV_BOOL)
        form = args[1].u.num ? 0 : 4;   /* TRUE classic, FALSE simplified */
    else
        WHOLE(1, 0, form);
    if (x < 0 || x > 3999 || form < 0 || form > 4) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    v = (unsigned long)x;
    owf_buf_init(&b);
    for (i = 0; i < sizeof numerals / sizeof numerals[0]; i++)
        while (numerals[i].form <= form && v >= numerals[i].value) {
            owf_buf_puts(&b, numerals[i].text);
            v -= numerals[i].value;
        }
    calc_take_text(ctx, &b, r);
}

static int roman_value(int c)
{
    switch (c) {
    case 'I': case 'i': return 1;
    case 'V': case 'v': return 5;
    case 'X': case 'x': return 10;
    case 'L': case 'l': return 50;
    case 'C': case 'c': return 100;
    case 'D': case 'd': return 500;
    case 'M': case 'm': return 1000;
    }
    return 0;
}

/* ARABIC(text): spaces either side are passed over, "" is 0, a leading
 * minus makes it negative; each numeral smaller than the next is taken
 * off. */
CF(arabic)
{
    const char *p;
    unsigned n, i;
    double v = 0;
    int neg = 0;
    TEXT(0, p, n);
    if (n > 255) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    while (n && p[0] == ' ') {
        p++;
        n--;
    }
    while (n && p[n - 1] == ' ')
        n--;
    if (n && p[0] == '-') {
        neg = 1;
        p++;
        n--;
    }
    for (i = 0; i < n; i++) {
        int a = roman_value((unsigned char)p[i]), next = i + 1 < n ? roman_value((unsigned char)p[i + 1]) : 0;
        if (!a || (i + 1 < n && !next)) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        v += a < next ? -a : a;
    }
    cv_num(r, neg ? -v : v);
}

/* ---- More functions of one number ---- */

CF(randbetween)
{
    double lo, hi;
    NUM(0, 0, lo);
    NUM(1, 0, hi);
    lo = calc_approx_ceil(lo);
    hi = calc_approx_floor(hi);
    if (lo > hi) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, lo + floor(calc_random(ctx) * (hi - lo + 1)));
}

CF(sqrtpi)
{
    double x;
    NUM(0, 0, x);
    if (x < 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, sqrt(x * PI_));
}

/* The inverse hyperbolic functions, through log(1 + x) so that a small x
 * keeps its digits. */
CF(asinh)
{
    double x, a;
    NUM(0, 0, x);
    a = fabs(x);
    a = a > 1e8 ? log(a) + 0.69314718055994530942 : calc_log1p(a + a * a / (1 + sqrt(1 + a * a)));
    cv_num(r, x < 0 ? -a : a);
}

CF(acosh)
{
    double x;
    NUM(0, 0, x);
    if (x < 1)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, x > 1e8 ? log(x) + 0.69314718055994530942 : calc_log1p(x - 1 + sqrt((x - 1) * (x + 1))));
}

CF(atanh)
{
    double x;
    NUM(0, 0, x);
    if (x <= -1 || x >= 1)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, 0.5 * calc_log1p(2 * x / (1 - x)));
}

/* ---- Matrices ---- */

/* An array argument of numbers only (else #VALUE!), as a matrix. */
static int matrix(calc_ctx *ctx, const calc_value *v, calc_array **out)
{
    unsigned long n, k;
    int e;
    if (v->type == CV_ERR)
        return v->u.err;
    if ((e = calc_to_array(ctx, v, out)) != 0)
        return e;
    n = (unsigned long)(*out)->rows * (*out)->cols;
    for (k = 0; k < n; k++) {
        if ((*out)->v[k].type == CV_ERR)
            return (*out)->v[k].u.err;
        if ((*out)->v[k].type != CV_NUM)
            return OWF_ERROR_VALUE;
    }
    return 0;
}

#define MATRIX(i, var)                                                                                       \
    do {                                                                                                     \
        int e_ = matrix(ctx, &args[i], &(var));                                                              \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

static void give_array(calc_value *r, calc_array *a)
{
    r->type = CV_ARRAY;
    r->u.arr = a;
}

CF(mmult)
{
    calc_array *a, *b, *c;
    unsigned i, j, k;
    (void)argc;
    MATRIX(0, a);
    MATRIX(1, b);
    if (a->cols != b->rows) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if ((c = calc_array_new(ctx, a->rows, b->cols)) == NULL) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < a->rows; i++)
        for (j = 0; j < b->cols; j++) {
            double s = 0;
            for (k = 0; k < a->cols; k++)
                s += a->v[i * a->cols + k].u.num * b->v[k * b->cols + j].u.num;
            cv_num(&c->v[i * c->cols + j], s);
        }
    give_array(r, c);
}

/* A square matrix's numbers in the arena, by row. */
static double *square(calc_ctx *ctx, const calc_array *a, unsigned extra)
{
    unsigned long n = (unsigned long)a->rows * (a->cols + extra), k, i, j;
    double *m = calc_arena_alloc(ctx->arena, (n ? n : 1) * sizeof *m);
    if (!m)
        return NULL;
    for (i = 0; i < a->rows; i++)
        for (j = 0; j < a->cols + extra; j++) {
            k = i * (a->cols + extra) + j;
            m[k] = j < a->cols ? a->v[i * a->cols + j].u.num : (j - a->cols == i ? 1 : 0);
        }
    return m;
}

/* Gaussian elimination with partial pivoting on an n x w matrix (w >= n),
 * by rows: the determinant, 0 when a pivot is 0. With w > n (an identity
 * beside it) it goes on to reduce to the inverse (Gauss-Jordan). */
static double eliminate(double *m, unsigned n, unsigned w)
{
    double det = 1;
    unsigned col, row, j;
    for (col = 0; col < n; col++) {
        unsigned best = col;
        double piv;
        for (row = col + 1; row < n; row++)
            if (fabs(m[row * w + col]) > fabs(m[best * w + col]))
                best = row;
        if (m[best * w + col] == 0)
            return 0;
        if (best != col) {
            for (j = 0; j < w; j++) {
                double t = m[col * w + j];
                m[col * w + j] = m[best * w + j];
                m[best * w + j] = t;
            }
            det = -det;
        }
        piv = m[col * w + col];
        det *= piv;
        for (row = 0; row < n; row++) {
            double f;
            if (row == col || (w == n && row < col))
                continue;
            f = m[row * w + col] / piv;
            if (f == 0)
                continue;
            for (j = col; j < w; j++)
                m[row * w + j] -= f * m[col * w + j];
        }
    }
    if (w > n)
        for (row = 0; row < n; row++) {
            double piv = m[row * w + row];
            for (j = 0; j < w; j++)
                m[row * w + j] /= piv;
        }
    return det;
}

CF(mdeterm)
{
    calc_array *a;
    double *m;
    (void)argc;
    MATRIX(0, a);
    if (a->rows != a->cols) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if ((m = square(ctx, a, 0)) == NULL) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, eliminate(m, a->rows, a->cols));
}

CF(minverse)
{
    calc_array *a, *c;
    double *m;
    unsigned i, j, n;
    (void)argc;
    MATRIX(0, a);
    if (a->rows != a->cols) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    n = a->rows;
    if ((m = square(ctx, a, n)) == NULL || (c = calc_array_new(ctx, n, n)) == NULL) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (eliminate(m, n, 2 * n) == 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            cv_num(&c->v[i * n + j], m[i * 2 * n + n + j]);
    give_array(r, c);
}

/* ---- SUBTOTAL ---- */

typedef struct {
    calc_numlist nums;
    long nonempty;               /* COUNTA */
    int err;
    int hidden;                  /* 101 to 111: hidden rows are passed over */
    const owf_sheet *sheet;
} subtotal_job;

static int row_hidden(const owf_sheet *s, unsigned row)
{
    int lo = 0, hi = s->nrows;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (s->rows[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < s->nrows && s->rows[lo].row == row && s->rows[lo].hidden;
}

/* Another SUBTOTAL (or AGGREGATE) in the range is not counted twice. */
static int is_subtotal(const owf_cell *c)
{
    return c->formula && (strstr(c->formula, "SUBTOTAL(") || strstr(c->formula, "AGGREGATE(") ||
                          strstr(c->formula, "subtotal(") || strstr(c->formula, "aggregate("));
}

static int subtotal_cell(void *user, const owf_cell *c, int sheet)
{
    subtotal_job *j = user;
    (void)sheet;
    if (is_subtotal(c) || (j->hidden && row_hidden(j->sheet, c->row)))
        return 0;
    j->nonempty++;
    if (c->type == OWF_VALUE_NUMBER)
        return calc_numlist_add(&j->nums, c->number) != NOERR;
    if (c->type == OWF_VALUE_ERROR && !j->err)
        j->err = c->error + 1;
    return 0;
}

/* SUBTOTAL(function, ref...): 1 AVERAGE, 2 COUNT, 3 COUNTA, 4 MAX, 5 MIN,
 * 6 PRODUCT, 7 STDEV, 8 STDEVP, 9 SUM, 10 VAR, 11 VARP of the references'
 * cells but those holding a SUBTOTAL; 101 to 111 the same without the
 * hidden rows. (Excel's 1 to 11 also pass over rows a filter hides; the
 * book keeps no filters.) */
CF(subtotal)
{
    subtotal_job j;
    double f, sum = 0, mean, ss = 0, prod = 1, lo = 0, hi = 0;
    unsigned long i;
    int k, a, which;
    NUM(0, 0, f);
    f = trunc_(f);
    which = (int)(f > 100 ? f - 100 : f);
    if (f < 1 || f > 111 || (f > 11 && f < 101) || which < 1 || which > 11) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    memset(&j, 0, sizeof j);
    j.nums.ctx = ctx;
    j.hidden = f > 100;
    for (k = 1; k < argc; k++) {
        PASS_ERROR(k);
        if (args[k].type != CV_REF) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        for (a = 0; a < args[k].u.ref.n; a++) {
            const calc_area *ar = &args[k].u.ref.a[a];
            if (ar->s0 != ar->s1 || ar->s0 < 0 || ar->s0 >= ctx->book->nsheets) {
                cv_err(r, OWF_ERROR_VALUE);
                return;
            }
            j.sheet = &ctx->book->sheets[ar->s0];
            if (calc_area_each(ctx, ar, subtotal_cell, &j)) {   /* the working memory is full */
                cv_err(r, OWF_ERROR_NUM);
                return;
            }
        }
    }
    if (which == 2 || which == 3) {
        cv_num(r, which == 2 ? (double)j.nums.n : (double)j.nonempty);
        return;
    }
    if (j.err) {
        cv_err(r, j.err - 1);
        return;
    }
    for (i = 0; i < j.nums.n; i++) {
        double x = j.nums.v[i];
        sum = calc_approx_add(sum, x);
        prod *= x;
        if (!i || x < lo)
            lo = x;
        if (!i || x > hi)
            hi = x;
    }
    switch (which) {
    case 1:
        if (!j.nums.n)
            cv_err(r, OWF_ERROR_DIV0);
        else
            cv_num(r, sum / (double)j.nums.n);
        return;
    case 4: cv_num(r, hi); return;
    case 5: cv_num(r, lo); return;
    case 6: cv_num(r, j.nums.n ? prod : 0); return;
    case 9: cv_num(r, sum); return;
    }
    /* 7, 8, 10, 11: STDEV, STDEVP, VAR, VARP */
    if (j.nums.n < (which == 7 || which == 10 ? 2u : 1u)) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    mean = sum / (double)j.nums.n;
    for (i = 0; i < j.nums.n; i++)
        ss += (j.nums.v[i] - mean) * (j.nums.v[i] - mean);
    ss /= (double)(which == 7 || which == 10 ? j.nums.n - 1 : j.nums.n);
    cv_num(r, which <= 8 ? sqrt(ss) : ss);
}
