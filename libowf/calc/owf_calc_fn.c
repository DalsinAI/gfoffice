/*
 * libowf: the formula engine's functions, with Excel's names, argument
 * counts and value rules: what a function does with text, booleans,
 * empty cells and errors in a reference, in an array and given directly.
 * The table at the end is in alphabetical order (the compiler finds a
 * name by halves); a function that is not in it gives #NAME?.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define NOERR (-1)                       /* OWF_ERROR_NULL is 0 */
#define PI_ 3.14159265358979323846

static int given(int argc, const calc_value *args, int i)
{
    return i < argc && args[i].type != CV_MISSING;
}

/* A number argument: absent, the default; left out (F(1,)), 0. */
static int num_arg(calc_ctx *ctx, calc_value *args, int argc, int i, double def, double *out)
{
    if (i >= argc) {
        *out = def;
        return 0;
    }
    return calc_to_number(ctx, &args[i], out);
}

#define NUM(i, def, var)                                                                                     \
    do {                                                                                                     \
        int e_ = num_arg(ctx, args, argc, (i), (def), &(var));                                               \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

#define TEXT(i, p, n)                                                                                        \
    do {                                                                                                     \
        int e_ = 0;                                                                                          \
        if ((i) < argc)                                                                                      \
            e_ = calc_to_text(ctx, &args[i], &(p), &(n));                                                    \
        else {                                                                                               \
            (p) = "";                                                                                        \
            (n) = 0;                                                                                         \
        }                                                                                                    \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

#define BOOL(i, def, var)                                                                                    \
    do {                                                                                                     \
        if ((i) >= argc)                                                                                     \
            (var) = (def);                                                                                   \
        else {                                                                                               \
            int e_ = calc_to_bool(ctx, &args[i], &(var));                                                    \
            if (e_) {                                                                                        \
                cv_err(r, e_);                                                                               \
                return;                                                                                      \
            }                                                                                                \
        }                                                                                                    \
    } while (0)

/* An argument's first error, if it is one. */
#define PASS_ERROR(i)                                                                                        \
    do {                                                                                                     \
        if ((i) < argc && args[i].type == CV_ERR) {                                                          \
            *r = args[i];                                                                                    \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

/* Text from a buffer, into the arena. */
static void take_text(calc_ctx *ctx, owf_buf *b, calc_value *r)
{
    if (b->failed)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_str_copy(ctx, r, b->length ? (const char *)b->data : "", b->length);
    owf_buf_free(b);
}

/* ---- Numbers in the arguments ---- */

/* fn gets each number; it returns NOERR to go on or an error to stop. */
typedef int (*num_fn)(void *user, double v);

typedef struct {
    num_fn fn;
    void *user;
    int err;
    int count_errors;            /* errors in references stop it (not for COUNT) */
} num_walk;

static int walk_cell(void *user, const owf_cell *c, int sheet)
{
    num_walk *w = user;
    (void)sheet;
    if (c->type == OWF_VALUE_NUMBER) {
        int e = w->fn(w->user, c->number);
        if (e != NOERR) {
            w->err = e;
            return 1;
        }
    } else if (c->type == OWF_VALUE_ERROR && w->count_errors) {
        w->err = c->error;
        return 1;
    }
    return 0;
}

/* The numbers in args[from...], as SUM takes them: in references and
 * arrays only numbers (text, booleans and empty cells are passed over);
 * given directly, numbers, booleans and text that reads as a number (other
 * text is #VALUE!), and a left-out argument is 0. An error anywhere stops
 * it. NOERR, or the error. */
static int each_number(calc_ctx *ctx, calc_value *args, int from, int argc, num_fn fn, void *user)
{
    int i, e;
    for (i = from; i < argc; i++) {
        calc_value *v = &args[i];
        double x;
        if (v->type == CV_REF) {
            num_walk w;
            int k;
            w.fn = fn;
            w.user = user;
            w.err = NOERR;
            w.count_errors = 1;
            for (k = 0; k < v->u.ref.n; k++)
                if (calc_area_each(ctx, &v->u.ref.a[k], walk_cell, &w))
                    return w.err;
        } else if (v->type == CV_ARRAY) {
            unsigned long n = (unsigned long)v->u.arr->rows * v->u.arr->cols, k;
            for (k = 0; k < n; k++) {
                const calc_value *el = &v->u.arr->v[k];
                if (el->type == CV_ERR)
                    return el->u.err;
                if (el->type == CV_NUM && (e = fn(user, el->u.num)) != NOERR)
                    return e;
            }
        } else {
            if ((e = calc_to_number(ctx, v, &x)) != 0)
                return e;
            if ((e = fn(user, x)) != NOERR)
                return e;
        }
    }
    return NOERR;
}

typedef struct {
    double sum, product, min, max, sumsq;
    long count;
} stats;

static int add_stat(void *user, double v)
{
    stats *s = user;
    s->sum = calc_approx_add(s->sum, v);
    s->product *= v;
    s->sumsq += v * v;
    if (!s->count || v < s->min)
        s->min = v;
    if (!s->count || v > s->max)
        s->max = v;
    s->count++;
    return NOERR;
}

static int gather(calc_ctx *ctx, calc_value *args, int from, int argc, stats *s)
{
    memset(s, 0, sizeof *s);
    s->product = 1;
    return each_number(ctx, args, from, argc, add_stat, s);
}

#define GATHER(s)                                                                                            \
    do {                                                                                                     \
        int e_ = gather(ctx, args, 0, argc, &(s));                                                           \
        if (e_ != NOERR) {                                                                                   \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

static void fn_sum(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    stats s;
    GATHER(s);
    cv_num(r, s.sum);
}

static void fn_sumsq(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    stats s;
    GATHER(s);
    cv_num(r, s.sumsq);
}

static void fn_product(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    stats s;
    GATHER(s);
    cv_num(r, s.count ? s.product : 0);
}

static void fn_average(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    stats s;
    GATHER(s);
    if (!s.count)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, s.sum / (double)s.count);
}

static void fn_min(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    stats s;
    GATHER(s);
    cv_num(r, s.count ? s.min : 0);
}

static void fn_max(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    stats s;
    GATHER(s);
    cv_num(r, s.count ? s.max : 0);
}

/* A list of numbers in the arena. */
typedef struct {
    calc_ctx *ctx;
    double *v;
    unsigned long n, cap;
} numlist;

static int add_num(void *user, double x)
{
    numlist *l = user;
    if (l->n == l->cap) {
        unsigned long want = l->cap ? l->cap * 2 : 64;
        double *p = calc_arena_alloc(l->ctx->arena, want * sizeof *p);
        if (!p) {
            l->ctx->too_big = 1;
            return OWF_ERROR_NUM;
        }
        if (l->n)
            memcpy(p, l->v, l->n * sizeof *p);
        l->v = p;
        l->cap = want;
    }
    l->v[l->n++] = x;
    return NOERR;
}

static int collect(calc_ctx *ctx, calc_value *args, int from, int argc, numlist *l)
{
    l->ctx = ctx;
    l->v = NULL;
    l->n = l->cap = 0;
    return each_number(ctx, args, from, argc, add_num, l);
}

/* Heapsort: no recursion, no more memory. */
static void sort_numbers(double *a, unsigned long n)
{
    unsigned long start, end, root, child;
    double t;
    if (n < 2)
        return;
    for (start = n / 2; start-- > 0;) {
        for (root = start; (child = 2 * root + 1) < n; root = child) {
            if (child + 1 < n && a[child] < a[child + 1])
                child++;
            if (a[root] >= a[child])
                break;
            t = a[root];
            a[root] = a[child];
            a[child] = t;
        }
    }
    for (end = n - 1; end > 0; end--) {
        t = a[0];
        a[0] = a[end];
        a[end] = t;
        for (root = 0; (child = 2 * root + 1) < end; root = child) {
            if (child + 1 < end && a[child] < a[child + 1])
                child++;
            if (a[root] >= a[child])
                break;
            t = a[root];
            a[root] = a[child];
            a[child] = t;
        }
    }
}

#define COLLECT(l, from)                                                                                     \
    do {                                                                                                     \
        int e_ = collect(ctx, args, (from), argc, &(l));                                                     \
        if (e_ != NOERR) {                                                                                   \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

static void fn_median(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    numlist l;
    COLLECT(l, 0);
    if (!l.n) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    sort_numbers(l.v, l.n);
    cv_num(r, l.n % 2 ? l.v[l.n / 2] : (l.v[l.n / 2 - 1] + l.v[l.n / 2]) / 2);
}

/* Variance: sample (STDEV, VAR) or population (STDEVP, VARP). */
static void variance(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int sample, int root)
{
    numlist l;
    double mean = 0, ss = 0;
    unsigned long i;
    COLLECT(l, 0);
    if (l.n < (sample ? 2u : 1u)) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    for (i = 0; i < l.n; i++)
        mean += l.v[i];
    mean /= (double)l.n;
    for (i = 0; i < l.n; i++)
        ss += (l.v[i] - mean) * (l.v[i] - mean);
    ss /= (double)(sample ? l.n - 1 : l.n);
    cv_num(r, root ? sqrt(ss) : ss);
}

static void fn_stdev(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    variance(ctx, args, argc, r, 1, 1);
}

static void fn_stdevp(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    variance(ctx, args, argc, r, 0, 1);
}

static void fn_var(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    variance(ctx, args, argc, r, 1, 0);
}

static void fn_varp(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    variance(ctx, args, argc, r, 0, 0);
}

static void kth(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int large)
{
    numlist l;
    double k;
    int e = collect(ctx, args, 0, 1, &l);
    if (e != NOERR) {
        cv_err(r, e);
        return;
    }
    NUM(1, 0, k);
    k = ceil(k);
    if (k < 1 || k > (double)l.n) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    sort_numbers(l.v, l.n);
    cv_num(r, large ? l.v[l.n - (unsigned long)k] : l.v[(unsigned long)k - 1]);
}

static void fn_large(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    kth(ctx, args, argc, r, 1);
}

static void fn_small(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    kth(ctx, args, argc, r, 0);
}

/* ---- Counting ---- */

typedef struct {
    long n;
    int all;                     /* COUNTA: every value; COUNT: numbers */
    int blank;                   /* COUNTBLANK: cells that are not blank */
} counter;

static int count_cell(void *user, const owf_cell *c, int sheet)
{
    counter *k = user;
    (void)sheet;
    if (k->blank)
        k->n += !(c->type == OWF_VALUE_EMPTY || (c->type == OWF_VALUE_TEXT && (!c->text || !*c->text)));
    else if (k->all ? c->type != OWF_VALUE_EMPTY : c->type == OWF_VALUE_NUMBER)
        k->n++;
    return 0;
}

static void count_values(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int all)
{
    counter k;
    int i, j;
    k.n = 0;
    k.all = all;
    k.blank = 0;
    for (i = 0; i < argc; i++) {
        calc_value *v = &args[i];
        double x;
        if (v->type == CV_REF) {
            for (j = 0; j < v->u.ref.n; j++)
                calc_area_each(ctx, &v->u.ref.a[j], count_cell, &k);
        } else if (v->type == CV_ARRAY) {
            unsigned long n = (unsigned long)v->u.arr->rows * v->u.arr->cols, e;
            for (e = 0; e < n; e++)
                if (all ? v->u.arr->v[e].type != CV_EMPTY : v->u.arr->v[e].type == CV_NUM)
                    k.n++;
        } else if (all) {
            k.n += v->type != CV_MISSING && v->type != CV_EMPTY;
        } else if (v->type == CV_NUM || v->type == CV_BOOL || (v->type == CV_STR && !calc_to_number(ctx, v, &x))) {
            k.n++;
        }
    }
    cv_num(r, (double)k.n);
}

static void fn_count(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    count_values(ctx, args, argc, r, 0);
}

static void fn_counta(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    count_values(ctx, args, argc, r, 1);
}

static double area_cells(const calc_area *a)
{
    return (double)(a->r1 - a->r0 + 1) * (double)(a->c1 - a->c0 + 1) * (double)(a->s1 - a->s0 + 1);
}

static void fn_countblank(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    counter k;
    double total = 0;
    int j;
    (void)argc;
    if (args[0].type != CV_REF) {
        cv_err(r, args[0].type == CV_ERR ? args[0].u.err : OWF_ERROR_VALUE);
        return;
    }
    k.n = 0;
    k.all = 0;
    k.blank = 1;
    for (j = 0; j < args[0].u.ref.n; j++) {
        total += area_cells(&args[0].u.ref.a[j]);
        calc_area_each(ctx, &args[0].u.ref.a[j], count_cell, &k);
    }
    cv_num(r, total - (double)k.n);
}

/* ---- Criteria: COUNTIF, SUMIF, AVERAGEIF and their -IFS ---- */

typedef struct {
    int op;                      /* OP_EQ ... OP_GE */
    int kind;                    /* CV_NUM, CV_STR, CV_BOOL, CV_ERR; CV_EMPTY: nothing after the operator */
    double num;
    int err;
    const char *s;
    unsigned len;
    int wild;
} crit;

static void make_crit(calc_ctx *ctx, const calc_value *v, crit *c)
{
    const char *s;
    unsigned n;
    int kind, dec;
    memset(c, 0, sizeof *c);
    c->op = OP_EQ;
    switch (v->type) {
    case CV_NUM:
        c->kind = CV_NUM;
        c->num = v->u.num;
        return;
    case CV_BOOL:
        c->kind = CV_BOOL;
        c->num = v->u.num;
        return;
    case CV_ERR:
        c->kind = CV_ERR;
        c->err = v->u.err;
        return;
    case CV_STR:
        break;
    default:                             /* an empty cell is 0 */
        c->kind = CV_NUM;
        return;
    }
    s = v->u.str.p;
    n = v->u.str.len;
    if (n >= 2 && s[0] == '<' && s[1] == '=') {
        c->op = OP_LE;
        s += 2;
    } else if (n >= 2 && s[0] == '>' && s[1] == '=') {
        c->op = OP_GE;
        s += 2;
    } else if (n >= 2 && s[0] == '<' && s[1] == '>') {
        c->op = OP_NE;
        s += 2;
    } else if (n >= 1 && (s[0] == '<' || s[0] == '>' || s[0] == '=')) {
        c->op = s[0] == '<' ? OP_LT : s[0] == '>' ? OP_GT : OP_EQ;
        s++;
    }
    n = v->u.str.len - (unsigned)(s - v->u.str.p);
    if (!n) {
        c->kind = CV_EMPTY;
        return;
    }
    if (calc_parse_number(s, n, ctx->dmy, ctx->book->date1904, &c->num, &kind, &dec, NULL, 0)) {
        c->kind = CV_NUM;
        return;
    }
    if (calc_text_equal_nocase(s, n, "TRUE", 4) || calc_text_equal_nocase(s, n, "FALSE", 5)) {
        c->kind = CV_BOOL;
        c->num = n == 4;
        return;
    }
    if (s[0] == '#') {
        static const int errors[] = { OWF_ERROR_NULL, OWF_ERROR_DIV0, OWF_ERROR_VALUE, OWF_ERROR_REF,
                                      OWF_ERROR_NAME, OWF_ERROR_NUM, OWF_ERROR_NA };
        size_t i;
        for (i = 0; i < sizeof errors / sizeof errors[0]; i++)
            if (calc_text_equal_nocase(s, n, owf_error_name(errors[i]), (unsigned)strlen(owf_error_name(errors[i])))) {
                c->kind = CV_ERR;
                c->err = errors[i];
                return;
            }
    }
    c->kind = CV_STR;
    c->s = s;
    c->len = n;
    c->wild = (c->op == OP_EQ || c->op == OP_NE) &&
              (memchr(s, '*', n) || memchr(s, '?', n) || memchr(s, '~', n));
}

static int test_op(int op, int cmp)
{
    switch (op) {
    case OP_EQ: return cmp == 0;
    case OP_NE: return cmp != 0;
    case OP_LT: return cmp < 0;
    case OP_LE: return cmp <= 0;
    case OP_GT: return cmp > 0;
    }
    return cmp >= 0;
}

static int crit_match(calc_ctx *ctx, const crit *c, const calc_value *v)
{
    int eq;
    switch (c->kind) {
    case CV_EMPTY:
        eq = v->type == CV_EMPTY || (v->type == CV_STR && !v->u.str.len);
        return c->op == OP_EQ ? eq : c->op == OP_NE ? v->type != CV_EMPTY : 0;
    case CV_NUM:
        if (v->type == CV_NUM)
            return test_op(c->op, calc_approx_equal(v->u.num, c->num) ? 0 : v->u.num < c->num ? -1 : 1);
        if (v->type == CV_STR && (c->op == OP_EQ || c->op == OP_NE)) {
            double x;
            int kind, dec;
            eq = calc_parse_number(v->u.str.p, v->u.str.len, ctx->dmy, ctx->book->date1904, &x, &kind, &dec,
                                   NULL, 0) && calc_approx_equal(x, c->num);
            return c->op == OP_EQ ? eq : !eq;
        }
        return c->op == OP_NE;
    case CV_STR:
        if (v->type != CV_STR)
            return c->op == OP_NE;
        if (c->op == OP_EQ || c->op == OP_NE) {
            eq = c->wild ? calc_wild_match(c->s, c->len, v->u.str.p, v->u.str.len)
                         : calc_text_equal_nocase(c->s, c->len, v->u.str.p, v->u.str.len);
            return c->op == OP_EQ ? eq : !eq;
        }
        return test_op(c->op, calc_text_compare_nocase(v->u.str.p, v->u.str.len, c->s, c->len));
    case CV_BOOL:
        if (v->type != CV_BOOL)
            return c->op == OP_NE;
        return test_op(c->op, v->u.num == c->num ? 0 : v->u.num < c->num ? -1 : 1);
    case CV_ERR:
        eq = v->type == CV_ERR && v->u.err == c->err;
        return c->op == OP_EQ ? eq : c->op == OP_NE ? !eq : 0;
    }
    return 0;
}

#define MAX_CRITERIA 127

typedef struct {
    calc_ctx *ctx;
    int n;
    const calc_area *areas[MAX_CRITERIA];
    crit crits[MAX_CRITERIA];
    const calc_area *target;     /* the cells summed; NULL to count */
    double sum, count;
    int driver;                  /* the criterion whose cells are walked */
} criteria;

/* (dr, dc) from the areas' corners: all criteria met? Then sum or count. */
static void criteria_at(criteria *c, unsigned dr, unsigned dc, const owf_cell *known)
{
    int k;
    calc_value v;
    for (k = 0; k < c->n; k++) {
        const calc_area *a = c->areas[k];
        if (k == c->driver && known)
            calc_value_of_cell(known, &v);
        else
            calc_cell_value(c->ctx, a->s0, a->r0 + dr, a->c0 + dc, &v);
        if (!crit_match(c->ctx, &c->crits[k], &v))
            return;
    }
    if (c->target) {
        calc_cell_value(c->ctx, c->target->s0, c->target->r0 + dr, c->target->c0 + dc, &v);
        if (v.type == CV_NUM) {
            c->sum = calc_approx_add(c->sum, v.u.num);
            c->count++;
        }
    } else {
        c->count++;
    }
}

static int criteria_cell(void *user, const owf_cell *cell, int sheet)
{
    criteria *c = user;
    const calc_area *a = c->areas[c->driver];
    (void)sheet;
    criteria_at(c, cell->row - a->r0, cell->col - a->c0, cell);
    return 0;
}

/* Works the criteria over their areas (all the same size). */
static int run_criteria(calc_ctx *ctx, criteria *c)
{
    int k;
    unsigned rows = c->areas[0]->r1 - c->areas[0]->r0, cols = c->areas[0]->c1 - c->areas[0]->c0;
    for (k = 0; k < c->n; k++) {
        const calc_area *a = c->areas[k];
        if (a->s0 != a->s1 || a->r1 - a->r0 != rows || a->c1 - a->c0 != cols)
            return OWF_ERROR_VALUE;
    }
    c->ctx = ctx;
    c->sum = c->count = 0;
    c->driver = -1;
    for (k = 0; k < c->n; k++) {
        calc_value empty;
        cv_empty(&empty);
        if (!crit_match(ctx, &c->crits[k], &empty)) {
            c->driver = k;
            break;
        }
    }
    if (c->driver >= 0) {
        /* an empty cell fails one criterion: only the cells there are can count */
        calc_area_each(ctx, c->areas[c->driver], criteria_cell, c);
    } else {
        /* every position, up to the last row and column the book uses */
        unsigned r1, c1, dr, dc, last_r = 0, last_c = 0;
        for (k = 0; k < c->n; k++) {
            calc_area_clip(ctx, c->areas[k], &r1, &c1);
            if (r1 - c->areas[k]->r0 > last_r)
                last_r = r1 - c->areas[k]->r0;
            if (c1 - c->areas[k]->c0 > last_c)
                last_c = c1 - c->areas[k]->c0;
        }
        if (c->target) {
            calc_area_clip(ctx, c->target, &r1, &c1);
            if (r1 - c->target->r0 > last_r)
                last_r = r1 - c->target->r0;
            if (c1 - c->target->c0 > last_c)
                last_c = c1 - c->target->c0;
        }
        if (last_r > rows)
            last_r = rows;
        if (last_c > cols)
            last_c = cols;
        for (dr = 0; dr <= last_r; dr++)
            for (dc = 0; dc <= last_c; dc++)
                criteria_at(c, dr, dc, NULL);
        if (!c->target)                  /* the rest are empty, and empty matches */
            c->count += ((double)rows + 1) * ((double)cols + 1) - ((double)last_r + 1) * ((double)last_c + 1);
    }
    return NOERR;
}

/* A range argument for the criteria functions. */
static int crit_area(const calc_value *v, const calc_area **a)
{
    if (v->type == CV_ERR)
        return v->u.err;
    if (v->type != CV_REF || v->u.ref.n != 1)
        return OWF_ERROR_VALUE;
    *a = v->u.ref.a;
    return NOERR;
}

static void if_family(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int what)
{
    criteria *c = calc_arena_alloc(ctx->arena, sizeof *c);
    calc_area sized;
    int e, k, first, pairs;
    if (!c) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    c->target = NULL;
    if (what == 3 || what == 4) {        /* SUMIFS, AVERAGEIFS: the target first, then pairs */
        if ((e = crit_area(&args[0], &c->target)) != NOERR) {
            cv_err(r, e);
            return;
        }
        first = 1;
    } else {
        first = 0;
    }
    if (what == 6 || what >= 3) {        /* -IFS: pairs */
        pairs = (argc - first) / 2;
        if ((argc - first) % 2 || pairs > MAX_CRITERIA) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
    } else {
        pairs = 1;
    }
    c->n = pairs;
    for (k = 0; k < pairs; k++) {
        calc_value cv;
        if ((e = crit_area(&args[first + 2 * k], &c->areas[k])) != NOERR) {
            if (args[first + 2 * k].type == CV_ARRAY && what == 0) {
                /* COUNTIF over an array */
                const calc_array *arr = args[first].u.arr;
                unsigned long i, n = (unsigned long)arr->rows * arr->cols, hits = 0;
                calc_scalar(ctx, &args[1], &cv);
                make_crit(ctx, &cv, &c->crits[0]);
                for (i = 0; i < n; i++)
                    hits += (unsigned long)crit_match(ctx, &c->crits[0], &arr->v[i]);
                cv_num(r, (double)hits);
                return;
            }
            cv_err(r, e);
            return;
        }
        calc_scalar(ctx, &args[first + 2 * k + 1], &cv);
        make_crit(ctx, &cv, &c->crits[k]);
    }
    if (what == 1 || what == 2) {        /* SUMIF, AVERAGEIF: the sum range, sized as the range */
        const calc_area *t = c->areas[0];
        if (given(argc, args, 2)) {
            if ((e = crit_area(&args[2], &t)) != NOERR) {
                cv_err(r, e);
                return;
            }
        }
        sized = *t;
        sized.r1 = sized.r0 + (c->areas[0]->r1 - c->areas[0]->r0);
        sized.c1 = sized.c0 + (c->areas[0]->c1 - c->areas[0]->c0);
        if (sized.r1 >= CALC_MAX_ROWS || sized.c1 >= CALC_MAX_COLS) {
            cv_err(r, OWF_ERROR_REF);
            return;
        }
        c->target = &sized;
    }
    if ((e = run_criteria(ctx, c)) != NOERR) {
        cv_err(r, e);
        return;
    }
    switch (what) {
    case 0:
    case 6: cv_num(r, c->count); break;
    case 1:
    case 3: cv_num(r, c->sum); break;
    default:
        if (!c->count)
            cv_err(r, OWF_ERROR_DIV0);
        else
            cv_num(r, c->sum / c->count);
        break;
    }
}

static void fn_countif(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    if_family(ctx, args, argc, r, 0);
}

static void fn_sumif(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    if_family(ctx, args, argc, r, 1);
}

static void fn_averageif(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    if_family(ctx, args, argc, r, 2);
}

static void fn_sumifs(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    if_family(ctx, args, argc, r, 3);
}

static void fn_averageifs(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    if_family(ctx, args, argc, r, 4);
}

static void fn_countifs(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    if_family(ctx, args, argc, r, 6);
}

static void fn_sumproduct(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    calc_array *first = NULL, *arr;
    double sum = 0;
    unsigned long n = 0, k;
    int i, e;
    double *prod;
    for (i = 0; i < argc; i++) {
        if (args[i].type == CV_ERR) {
            *r = args[i];
            return;
        }
        if ((e = calc_to_array(ctx, &args[i], &arr)) != 0) {
            cv_err(r, e);
            return;
        }
        if (!i) {
            first = arr;
            n = (unsigned long)arr->rows * arr->cols;
        } else if (arr->rows != first->rows || arr->cols != first->cols) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        args[i].type = CV_ARRAY;
        args[i].u.arr = arr;
    }
    prod = calc_arena_alloc(ctx->arena, (n ? n : 1) * sizeof *prod);
    if (!prod) {
        ctx->too_big = 1;
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (k = 0; k < n; k++)
        prod[k] = 1;
    for (i = 0; i < argc; i++) {
        arr = args[i].u.arr;
        for (k = 0; k < n; k++) {
            const calc_value *el = &arr->v[k];
            if (el->type == CV_ERR) {
                cv_err(r, el->u.err);
                return;
            }
            prod[k] *= el->type == CV_NUM ? el->u.num : 0;
        }
    }
    for (k = 0; k < n; k++)
        sum = calc_approx_add(sum, prod[k]);
    cv_num(r, sum);
}

/* ---- Maths ---- */

/* x to `digits` decimals (below 0: to tens, hundreds...): mode 0 half away
 * from zero (ROUND), 1 away from zero (ROUNDUP), 2 toward zero (ROUNDDOWN,
 * TRUNC). A hair either side of the boundary counts as on it, as Excel's
 * 15 digits see it: ROUND(2.675, 2) is 2.68. */
static double round_to(double x, double digits, int mode)
{
    double a = fabs(x), p, y, eps, rounded;
    int d;
    if (a == 0)
        return x;
    if (digits > 330)
        digits = 330;
    if (digits < -330)
        digits = -330;
    d = (int)digits;
    if (d >= 0) {
        if (d > 300)
            return x;
        p = pow(10, d);
        y = a * p;
        if (y >= 4503599627370496.0)
            return x;                    /* no fraction left to round */
    } else {
        p = pow(10, -d);
        y = a / p;
    }
    eps = y * (1.0 / 1125899906842624.0);   /* 2^-50 */
    if (mode == 0)
        rounded = floor(y + 0.5 + eps);
    else if (mode == 1)
        rounded = ceil(y - eps);
    else
        rounded = floor(y + eps);
    rounded = d >= 0 ? rounded / p : rounded * p;
    return x < 0 ? -rounded : rounded;
}

static void fn_round(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, d;
    NUM(0, 0, x);
    NUM(1, 0, d);
    cv_num(r, round_to(x, d, 0));
}

static void fn_roundup(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, d;
    NUM(0, 0, x);
    NUM(1, 0, d);
    cv_num(r, round_to(x, d, 1));
}

static void fn_rounddown(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, d;
    NUM(0, 0, x);
    NUM(1, 0, d);
    cv_num(r, round_to(x, d, 2));
}

static void fn_trunc(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, d;
    NUM(0, 0, x);
    NUM(1, 0, d);
    cv_num(r, round_to(x, d, 2));
}

static void fn_int(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    cv_num(r, calc_approx_floor(x));
}

static void fn_mod(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double a, b, m;
    NUM(0, 0, a);
    NUM(1, 0, b);
    if (b == 0) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    m = calc_approx_add(a, -calc_approx_floor(a / b) * b);
    if (m != 0 && (m < 0) != (b < 0))
        m += b;                          /* the sign of the divisor, always */
    cv_num(r, m);
}

static void fn_abs(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    cv_num(r, fabs(x));
}

static void fn_sign(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    cv_num(r, x > 0 ? 1 : x < 0 ? -1 : 0);
}

static void fn_sqrt(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    if (x < 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, sqrt(x));
}

static void fn_power(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double a, b;
    NUM(0, 0, a);
    NUM(1, 0, b);
    calc_power(a, b, r);
}

static void fn_exp(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    cv_num(r, exp(x));
}

static void fn_ln(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    if (x <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, log(x));
}

static void fn_log10(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    NUM(0, 0, x);
    if (x <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, log10(x));
}

static void fn_log(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, b;
    NUM(0, 0, x);
    NUM(1, 10, b);
    if (x <= 0 || b <= 0)
        cv_err(r, OWF_ERROR_NUM);
    else if (b == 1)
        cv_err(r, OWF_ERROR_DIV0);
    else if (b == 10)
        cv_num(r, log10(x));
    else
        cv_num(r, log(x) / log(b));
}

static void fn_pi(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)args;
    (void)argc;
    cv_num(r, PI_);
}

/* The functions of one number that only need C's. */
static void unary_math(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int which)
{
    double x, y = 0;
    NUM(0, 0, x);
    switch (which) {
    case 0: y = sin(x); break;
    case 1: y = cos(x); break;
    case 2: y = tan(x); break;
    case 3:
    case 4:
        if (x < -1 || x > 1) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        y = which == 3 ? asin(x) : acos(x);
        break;
    case 5: y = atan(x); break;
    case 6: y = sinh(x); break;
    case 7: y = cosh(x); break;
    case 8: y = tanh(x); break;
    case 9: y = x * 180 / PI_; break;
    case 10: y = x * PI_ / 180; break;
    }
    cv_num(r, y);
}

static void fn_sin(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 0); }
static void fn_cos(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 1); }
static void fn_tan(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 2); }
static void fn_asin(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 3); }
static void fn_acos(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 4); }
static void fn_atan(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 5); }
static void fn_sinh(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 6); }
static void fn_cosh(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 7); }
static void fn_tanh(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 8); }
static void fn_degrees(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 9); }
static void fn_radians(calc_ctx *c, calc_value *a, int n, calc_value *r) { unary_math(c, a, n, r, 10); }

static void fn_atan2(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, y;
    NUM(0, 0, x);
    NUM(1, 0, y);
    if (x == 0 && y == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else
        cv_num(r, atan2(y, x));
}

static void fn_rand(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)args;
    (void)argc;
    cv_num(r, calc_random(ctx));
}

static void fn_ceiling(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, s;
    NUM(0, 0, x);
    NUM(1, 0, s);
    if (x == 0 || s == 0)
        cv_num(r, 0);
    else if ((x > 0 && s < 0))
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, calc_approx_ceil(x / s) * s);
}

static void fn_floor(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, s;
    NUM(0, 0, x);
    NUM(1, 0, s);
    if (x == 0)
        cv_num(r, 0);
    else if (s == 0)
        cv_err(r, OWF_ERROR_DIV0);
    else if (x > 0 && s < 0)
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, calc_approx_floor(x / s) * s);
}

static void even_odd(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int odd)
{
    double x, a, k;
    NUM(0, 0, x);
    a = calc_approx_ceil(fabs(x));
    if (odd) {
        k = fmod(a, 2) == 1 ? a : a + 1;
    } else {
        k = fmod(a, 2) == 0 ? a : a + 1;
    }
    cv_num(r, x < 0 ? -k : k);
}

static void fn_even(calc_ctx *c, calc_value *a, int n, calc_value *r) { even_odd(c, a, n, r, 0); }
static void fn_odd(calc_ctx *c, calc_value *a, int n, calc_value *r) { even_odd(c, a, n, r, 1); }

static void fn_fact(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, f = 1;
    long i, n;
    NUM(0, 0, x);
    if (x < 0 || x > 170) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    n = (long)x;
    for (i = 2; i <= n; i++)
        f *= (double)i;
    cv_num(r, f);
}

/* ---- Logic ---- */

static void fn_true(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)args;
    (void)argc;
    cv_bool(r, 1);
}

static void fn_false(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)args;
    (void)argc;
    cv_bool(r, 0);
}

static void fn_na(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)args;
    (void)argc;
    cv_err(r, OWF_ERROR_NA);
}

/* IF takes its arguments as they are, so that IF(A1, B1:B3, C1:C3) is a
 * reference SUM can take; a condition that is an array (an array
 * formula) picks element by element. */
static void fn_if(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    calc_value cond = args[0], *pick;
    int b, e;
    calc_operand(ctx, &cond, ctx->actx);
    if (cond.type == CV_ARRAY) {
        unsigned rows = 1, cols = 1, i, j;
        calc_array *arr;
        calc_value then = argc > 1 ? args[1] : cond, other;
        if (argc > 2)
            other = args[2];
        else
            cv_bool(&other, 0);
        calc_operand(ctx, &then, 1);
        calc_operand(ctx, &other, 1);
        calc_dims(&cond, &rows, &cols);
        calc_dims(&then, &rows, &cols);
        calc_dims(&other, &rows, &cols);
        arr = calc_array_new(ctx, rows, cols);
        if (!arr) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        for (i = 0; i < rows; i++)
            for (j = 0; j < cols; j++) {
                calc_value c, *out = &arr->v[i * cols + j];
                calc_element(&cond, i, j, &c);
                if ((e = calc_to_bool(ctx, &c, &b)) != 0)
                    cv_err(out, e);
                else
                    calc_element(b ? &then : &other, i, j, out);
                if (out->type == CV_MISSING)
                    cv_num(out, 0);
            }
        r->type = CV_ARRAY;
        r->u.arr = arr;
        return;
    }
    if ((e = calc_to_bool(ctx, &cond, &b)) != 0) {
        cv_err(r, e);
        return;
    }
    if (!b && argc < 3) {
        cv_bool(r, 0);
        return;
    }
    pick = &args[b ? 1 : 2];
    if (pick->type == CV_MISSING)
        cv_num(r, 0);                    /* IF(A1,,1): a left-out value is 0 */
    else
        *r = *pick;
}

static void fn_iferror(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)argc;
    *r = args[0].type == CV_ERR ? args[1] : args[0];
    if (r->type == CV_MISSING || r->type == CV_EMPTY)
        cv_num(r, 0);
}

static void fn_ifna(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)argc;
    *r = args[0].type == CV_ERR && args[0].u.err == OWF_ERROR_NA ? args[1] : args[0];
    if (r->type == CV_MISSING || r->type == CV_EMPTY)
        cv_num(r, 0);
}

typedef struct {
    int any, all, seen;
} logic;

static void logic_one(logic *l, int b)
{
    l->seen = 1;
    if (b)
        l->any = 1;
    else
        l->all = 0;
}

static int logic_cell(void *user, const owf_cell *c, int sheet)
{
    logic *l = user;
    (void)sheet;
    if (c->type == OWF_VALUE_NUMBER || c->type == OWF_VALUE_BOOL)
        logic_one(l, c->number != 0);
    else if (c->type == OWF_VALUE_ERROR)
        return c->error + 1;
    return 0;
}

static void and_or(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int is_or)
{
    logic l;
    int i, k, e, b;
    l.any = 0;
    l.all = 1;
    l.seen = 0;
    for (i = 0; i < argc; i++) {
        calc_value *v = &args[i];
        if (v->type == CV_REF) {
            for (k = 0; k < v->u.ref.n; k++)
                if ((e = calc_area_each(ctx, &v->u.ref.a[k], logic_cell, &l)) != 0) {
                    cv_err(r, e - 1);
                    return;
                }
        } else if (v->type == CV_ARRAY) {
            unsigned long n = (unsigned long)v->u.arr->rows * v->u.arr->cols, j;
            for (j = 0; j < n; j++) {
                const calc_value *el = &v->u.arr->v[j];
                if (el->type == CV_ERR) {
                    cv_err(r, el->u.err);
                    return;
                }
                if (el->type == CV_NUM || el->type == CV_BOOL)
                    logic_one(&l, el->u.num != 0);
            }
        } else if (v->type != CV_MISSING && v->type != CV_EMPTY) {
            if ((e = calc_to_bool(ctx, v, &b)) != 0) {
                cv_err(r, e);
                return;
            }
            logic_one(&l, b);
        }
    }
    if (!l.seen)
        cv_err(r, OWF_ERROR_VALUE);
    else
        cv_bool(r, is_or ? l.any : l.all);
}

static void fn_and(calc_ctx *c, calc_value *a, int n, calc_value *r) { and_or(c, a, n, r, 0); }
static void fn_or(calc_ctx *c, calc_value *a, int n, calc_value *r) { and_or(c, a, n, r, 1); }

static void fn_not(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    int b;
    BOOL(0, 0, b);
    cv_bool(r, !b);
}

static void is_kind(calc_value *args, calc_value *r, int which)
{
    const calc_value *v = &args[0];
    int t = v->type == CV_MISSING ? CV_EMPTY : v->type, y = 0;
    switch (which) {
    case 0: y = t == CV_EMPTY; break;                              /* ISBLANK */
    case 1: y = t == CV_ERR; break;                                /* ISERROR */
    case 2: y = t == CV_ERR && v->u.err != OWF_ERROR_NA; break;    /* ISERR */
    case 3: y = t == CV_ERR && v->u.err == OWF_ERROR_NA; break;    /* ISNA */
    case 4: y = t == CV_NUM; break;                                /* ISNUMBER */
    case 5: y = t == CV_STR; break;                                /* ISTEXT */
    case 6: y = t == CV_BOOL; break;                               /* ISLOGICAL */
    case 7: y = t != CV_STR; break;                                /* ISNONTEXT */
    }
    cv_bool(r, y);
}

static void fn_isblank(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 0); }
static void fn_iserror(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 1); }
static void fn_iserr(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 2); }
static void fn_isna(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 3); }
static void fn_isnumber(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 4); }
static void fn_istext(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 5); }
static void fn_islogical(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 6); }
static void fn_isnontext(calc_ctx *c, calc_value *a, int n, calc_value *r) { (void)c; (void)n; is_kind(a, r, 7); }

static void fn_n(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)argc;
    if (args[0].type == CV_ERR)
        *r = args[0];
    else
        cv_num(r, args[0].type == CV_NUM || args[0].type == CV_BOOL ? args[0].u.num : 0);
}

static void fn_t(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)ctx;
    (void)argc;
    if (args[0].type == CV_ERR || args[0].type == CV_STR)
        *r = args[0];
    else
        cv_str(r, "", 0);
}

/* ---- Text ---- */

static void fn_len(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n;
    TEXT(0, p, n);
    cv_num(r, (double)calc_utf8_count(p, n));
}

/* A count of characters: n >= 0, else #VALUE!. */
static int char_count(calc_ctx *ctx, calc_value *args, int argc, int i, double def, unsigned *out)
{
    double x;
    int e = num_arg(ctx, args, argc, i, def, &x);
    if (e)
        return e;
    if (x < 0)
        return OWF_ERROR_VALUE;
    *out = x > CALC_MAX_TEXT ? CALC_MAX_TEXT : (unsigned)x;
    return 0;
}

#define COUNT_ARG(i, def, var)                                                                               \
    do {                                                                                                     \
        int e_ = char_count(ctx, args, argc, (i), (def), &(var));                                            \
        if (e_) {                                                                                            \
            cv_err(r, e_);                                                                                   \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

static void fn_left(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n, k;
    TEXT(0, p, n);
    COUNT_ARG(1, 1, k);
    cv_str(r, p, calc_utf8_offset(p, n, k));
}

static void fn_right(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n, k, chars, at;
    TEXT(0, p, n);
    COUNT_ARG(1, 1, k);
    chars = calc_utf8_count(p, n);
    at = k >= chars ? 0 : calc_utf8_offset(p, n, chars - k);
    cv_str(r, p + at, n - at);
}

static void fn_mid(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n, k, from, to;
    double start;
    TEXT(0, p, n);
    NUM(1, 1, start);
    COUNT_ARG(2, 0, k);
    if (start < 1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (start > CALC_MAX_TEXT + 1)
        start = CALC_MAX_TEXT + 1;
    from = calc_utf8_offset(p, n, (unsigned)start - 1);
    to = calc_utf8_offset(p + from, n - from, k);
    cv_str(r, p + from, to);
}

static void map_case(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int mode)
{
    const char *p, *end;
    unsigned n;
    owf_buf b;
    int after_letter = 0;
    TEXT(0, p, n);
    owf_buf_init(&b);
    end = p + n;
    while (p < end) {
        unsigned long c = owf_utf8_next((const unsigned char **)&p, (const unsigned char *)end);
        if (mode == 0)
            c = calc_upper(c);
        else if (mode == 1)
            c = calc_lower(c);
        else {                           /* PROPER: a capital after anything but a letter */
            int letter = calc_is_letter(c);
            c = letter && !after_letter ? calc_upper(c) : calc_lower(c);
            after_letter = letter;
        }
        calc_put_utf8(&b, c);
    }
    take_text(ctx, &b, r);
}

static void fn_upper(calc_ctx *c, calc_value *a, int n, calc_value *r) { map_case(c, a, n, r, 0); }
static void fn_lower(calc_ctx *c, calc_value *a, int n, calc_value *r) { map_case(c, a, n, r, 1); }
static void fn_proper(calc_ctx *c, calc_value *a, int n, calc_value *r) { map_case(c, a, n, r, 2); }

static void fn_trim(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n, i;
    owf_buf b;
    int space = 0;
    TEXT(0, p, n);
    owf_buf_init(&b);
    for (i = 0; i < n; i++) {
        if (p[i] == ' ') {
            space = 1;
            continue;
        }
        if (space && b.length)
            owf_buf_putc(&b, ' ');
        space = 0;
        owf_buf_putc(&b, p[i]);
    }
    take_text(ctx, &b, r);
}

static void fn_clean(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n, i;
    owf_buf b;
    TEXT(0, p, n);
    owf_buf_init(&b);
    for (i = 0; i < n; i++)
        if ((unsigned char)p[i] >= 32)
            owf_buf_putc(&b, p[i]);
    take_text(ctx, &b, r);
}

static void fn_concatenate(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    owf_buf b;
    int i;
    owf_buf_init(&b);
    for (i = 0; i < argc; i++) {
        const char *p;
        unsigned n;
        int e = calc_to_text(ctx, &args[i], &p, &n);
        if (e) {
            owf_buf_free(&b);
            cv_err(r, e);
            return;
        }
        owf_buf_put(&b, p, n);
    }
    take_text(ctx, &b, r);
}

static void fn_rept(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n;
    double k, i;
    owf_buf b;
    TEXT(0, p, n);
    NUM(1, 0, k);
    k = floor(k);
    if (k < 0 || (double)calc_utf8_count(p, n) * k > CALC_MAX_TEXT) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    owf_buf_init(&b);
    for (i = 0; i < k; i++)
        owf_buf_put(&b, p, n);
    take_text(ctx, &b, r);
}

static void fn_exact(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p, *q;
    unsigned n, m;
    TEXT(0, p, n);
    TEXT(1, q, m);
    cv_bool(r, n == m && !memcmp(p, q, n));
}

static void find_search(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int search)
{
    const char *what, *in;
    unsigned wn, inn, chars, from;
    double start;
    long at = -1;
    TEXT(0, what, wn);
    TEXT(1, in, inn);
    NUM(2, 1, start);
    chars = calc_utf8_count(in, inn);
    if (start < 1 || start > (double)chars + 1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    from = calc_utf8_offset(in, inn, (unsigned)start - 1);
    if (search) {
        at = calc_wild_search(what, wn, in, inn, from);
    } else if (!wn) {
        at = from;
    } else {
        unsigned i;
        for (i = from; i + wn <= inn; i++)
            if (!memcmp(in + i, what, wn)) {
                at = (long)i;
                break;
            }
    }
    if (at < 0)
        cv_err(r, OWF_ERROR_VALUE);
    else
        cv_num(r, (double)calc_utf8_count(in, (unsigned)at) + 1);
}

static void fn_find(calc_ctx *c, calc_value *a, int n, calc_value *r) { find_search(c, a, n, r, 0); }
static void fn_search(calc_ctx *c, calc_value *a, int n, calc_value *r) { find_search(c, a, n, r, 1); }

static void fn_substitute(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *text, *old, *by;
    unsigned tn, on, bn, i, done = 0;
    double which = 0;
    long seen = 0;
    owf_buf b;
    TEXT(0, text, tn);
    TEXT(1, old, on);
    TEXT(2, by, bn);
    if (given(argc, args, 3)) {
        NUM(3, 0, which);
        if (which < 1) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        which = floor(which);
    }
    if (!on) {
        *r = args[0];
        cv_str(r, text, tn);
        return;
    }
    owf_buf_init(&b);
    for (i = 0; i + on <= tn;) {
        if (!memcmp(text + i, old, on) && !(which && done)) {
            seen++;
            if (!which || (double)seen == which) {
                owf_buf_put(&b, by, bn);
                i += on;
                done = which ? 1 : 0;
                continue;
            }
        }
        owf_buf_putc(&b, text[i]);
        i++;
    }
    owf_buf_put(&b, text + i, tn - i);
    take_text(ctx, &b, r);
}

static void fn_replace(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *text, *by;
    unsigned tn, bn, k, from, to;
    double start;
    owf_buf b;
    TEXT(0, text, tn);
    NUM(1, 1, start);
    COUNT_ARG(2, 0, k);
    TEXT(3, by, bn);
    if (start < 1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (start > CALC_MAX_TEXT + 1)
        start = CALC_MAX_TEXT + 1;
    from = calc_utf8_offset(text, tn, (unsigned)start - 1);
    to = from + calc_utf8_offset(text + from, tn - from, k);
    owf_buf_init(&b);
    owf_buf_put(&b, text, from);
    owf_buf_put(&b, by, bn);
    owf_buf_put(&b, text + to, tn - to);
    take_text(ctx, &b, r);
}

/* Windows-1252's 0x80-0x9F, as Excel's CHAR and CODE have them. */
static const unsigned short cp1252[32] = {
    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D,
    0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
    0x0153, 0x9D, 0x017E, 0x0178
};

static void fn_char(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    unsigned long c;
    owf_buf b;
    NUM(0, 0, x);
    x = floor(x);
    if (x < 1 || x > 255) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    c = (unsigned long)x;
    if (c >= 0x80 && c < 0xA0)
        c = cp1252[c - 0x80];
    owf_buf_init(&b);
    calc_put_utf8(&b, c);
    take_text(ctx, &b, r);
}

static void fn_code(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *p;
    unsigned n, i;
    unsigned long c;
    TEXT(0, p, n);
    if (!n) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    c = owf_utf8_next((const unsigned char **)&p, (const unsigned char *)p + n);
    if (c >= 0x100 || (c >= 0x80 && c < 0xA0)) {
        unsigned long code = '?';
        for (i = 0; i < 32; i++)
            if (cp1252[i] == c)
                code = 0x80 + i;
        c = code;
    }
    cv_num(r, (double)c);
}

static void fn_text(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    const char *fmt;
    unsigned fn_len_;
    calc_value v = args[0];
    owf_buf b;
    char *f;
    double x;
    TEXT(1, fmt, fn_len_);
    if (v.type == CV_ERR) {
        *r = v;
        return;
    }
    if (v.type == CV_STR && !calc_to_number(ctx, &v, &x))
        cv_num(&v, x);
    if (v.type == CV_EMPTY || v.type == CV_MISSING)
        cv_num(&v, 0);
    f = calc_arena_alloc(ctx->arena, (size_t)fn_len_ + 1);
    if (!f) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    memcpy(f, fmt, fn_len_);
    f[fn_len_] = 0;
    owf_buf_init(&b);
    calc_format(f, &v, ctx->book->date1904, 0, &b, NULL);
    take_text(ctx, &b, r);
}

static void fn_value(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x;
    (void)argc;
    if (args[0].type == CV_BOOL) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    NUM(0, 0, x);
    cv_num(r, x);
}

/* ---- Lookup ---- */

/* A table: a reference's one area, or an array. */
typedef struct {
    calc_ctx *ctx;
    const calc_area *a;
    const calc_array *arr;
    unsigned rows, cols;
} table;

static int table_of(calc_ctx *ctx, const calc_value *v, table *t)
{
    t->ctx = ctx;
    t->a = NULL;
    t->arr = NULL;
    if (v->type == CV_ERR)
        return v->u.err;
    if (v->type == CV_REF) {
        unsigned r1, c1;
        if (v->u.ref.n != 1 || v->u.ref.a->s0 != v->u.ref.a->s1)
            return OWF_ERROR_VALUE;
        t->a = v->u.ref.a;
        calc_area_clip(ctx, t->a, &r1, &c1);
        t->rows = r1 - t->a->r0 + 1;
        t->cols = c1 - t->a->c0 + 1;
        return 0;
    }
    if (v->type == CV_ARRAY) {
        t->arr = v->u.arr;
        t->rows = t->arr->rows;
        t->cols = t->arr->cols;
        return 0;
    }
    {
        calc_array *arr;
        int e = calc_to_array(ctx, v, &arr);
        if (e)
            return e;
        t->arr = arr;
        t->rows = t->cols = 1;
    }
    return 0;
}

static void table_get(const table *t, unsigned i, unsigned j, calc_value *out)
{
    if (t->a)
        calc_cell_value(t->ctx, t->a->s0, t->a->r0 + i, t->a->c0 + j, out);
    else if (i < t->arr->rows && j < t->arr->cols)
        *out = t->arr->v[i * t->arr->cols + j];
    else
        cv_empty(out);
}

static int same_type(const calc_value *a, const calc_value *b)
{
    return a->type == b->type;
}

static int has_wild(const calc_value *v)
{
    return v->type == CV_STR && (memchr(v->u.str.p, '*', v->u.str.len) || memchr(v->u.str.p, '?', v->u.str.len) ||
                                 memchr(v->u.str.p, '~', v->u.str.len));
}

/* Where key is along a row (by_row) or column of a table: exact (with
 * wildcards for text), the last at or below (sorted up: binary search, as
 * Excel), or the last at or above (sorted down). -1 for none. */
static long lookup(const table *t, const calc_value *key, int by_row, unsigned fixed, int mode)
{
    unsigned n = by_row ? t->cols : t->rows, i;
    calc_value v;
    int err, cmp;
    if (mode == 0) {
        int wild = has_wild(key);
        for (i = 0; i < n; i++) {
            table_get(t, by_row ? fixed : i, by_row ? i : fixed, &v);
            if (!same_type(&v, key))
                continue;
            if (wild ? calc_wild_match(key->u.str.p, key->u.str.len, v.u.str.p, v.u.str.len)
                     : calc_compare(&v, key, &err) == 0)
                return (long)i;
        }
        return -1;
    }
    if (mode < 0) {
        long found = -1;
        for (i = 0; i < n; i++) {
            table_get(t, by_row ? fixed : i, by_row ? i : fixed, &v);
            if (!same_type(&v, key))
                continue;
            cmp = calc_compare(&v, key, &err);
            if (cmp < 0)
                break;
            found = (long)i;
        }
        return found;
    }
    {
        long lo = 0, hi = (long)n - 1, found = -1;
        while (lo <= hi) {
            long mid = lo + (hi - lo) / 2, k;
            /* past empty cells and other kinds to one that compares */
            for (k = mid; k >= lo; k--) {
                table_get(t, by_row ? fixed : (unsigned)k, by_row ? (unsigned)k : fixed, &v);
                if (same_type(&v, key))
                    break;
            }
            if (k < lo) {
                lo = mid + 1;
                continue;
            }
            cmp = calc_compare(&v, key, &err);
            if (cmp <= 0) {
                found = k;
                lo = mid + 1;
            } else {
                hi = k - 1;
            }
        }
        return found;
    }
}

static void vh_lookup(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int horizontal)
{
    table t;
    calc_value key = args[0];
    double index;
    int approx, e;
    long at;
    if (key.type == CV_ERR) {
        *r = key;
        return;
    }
    if (key.type == CV_EMPTY || key.type == CV_MISSING)
        cv_num(&key, 0);
    if ((e = table_of(ctx, &args[1], &t)) != 0) {
        cv_err(r, e);
        return;
    }
    NUM(2, 0, index);
    BOOL(3, 1, approx);
    index = floor(index);
    if (index < 1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (index > (double)(horizontal ? t.rows : t.cols)) {
        cv_err(r, OWF_ERROR_REF);
        return;
    }
    at = lookup(&t, &key, horizontal, 0, approx ? 1 : 0);
    if (at < 0) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    if (horizontal)
        table_get(&t, (unsigned)index - 1, (unsigned)at, r);
    else
        table_get(&t, (unsigned)at, (unsigned)index - 1, r);
}

static void fn_vlookup(calc_ctx *c, calc_value *a, int n, calc_value *r) { vh_lookup(c, a, n, r, 0); }
static void fn_hlookup(calc_ctx *c, calc_value *a, int n, calc_value *r) { vh_lookup(c, a, n, r, 1); }

static void fn_match(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    table t;
    calc_value key = args[0];
    double type;
    int e, mode;
    long at;
    if (key.type == CV_ERR) {
        *r = key;
        return;
    }
    if (key.type == CV_EMPTY || key.type == CV_MISSING)
        cv_num(&key, 0);
    if ((e = table_of(ctx, &args[1], &t)) != 0) {
        cv_err(r, e);
        return;
    }
    NUM(2, 1, type);
    mode = type > 0 ? 1 : type < 0 ? -1 : 0;
    if (t.rows != 1 && t.cols != 1) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    at = lookup(&t, &key, t.rows == 1 && t.cols != 1, 0, mode);
    if (at < 0)
        cv_err(r, OWF_ERROR_NA);
    else
        cv_num(r, (double)at + 1);
}

/* INDEX(range, row, col, area): a reference (to a cell, or a whole row or
 * column with 0), or an array's element. */
static void fn_index(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double row = 0, col = 0, area = 1;
    unsigned rows, cols;
    const calc_value *v = &args[0];
    PASS_ERROR(0);
    NUM(1, 0, row);
    NUM(2, 0, col);
    NUM(3, 1, area);
    row = floor(row);
    col = floor(col);
    if (row < 0 || col < 0) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (v->type == CV_REF) {
        const calc_area *a;
        calc_area *out;
        if (area < 1 || area > v->u.ref.n) {
            cv_err(r, OWF_ERROR_REF);
            return;
        }
        a = &v->u.ref.a[(int)area - 1];
        rows = a->r1 - a->r0 + 1;
        cols = a->c1 - a->c0 + 1;
        if (argc == 2 && rows == 1) {    /* INDEX(A1:E1, 3): along the row */
            col = row;
            row = 0;
        }
        if (row > rows || col > cols || (argc == 2 && rows > 1 && cols > 1 && !row)) {
            cv_err(r, OWF_ERROR_REF);
            return;
        }
        out = calc_arena_alloc(ctx->arena, sizeof *out);
        if (!out) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        *out = *a;
        if (row) {
            out->r0 = a->r0 + (unsigned)row - 1;
            out->r1 = out->r0;
        }
        if (col) {
            out->c0 = a->c0 + (unsigned)col - 1;
            out->c1 = out->c0;
        }
        r->type = CV_REF;
        r->u.ref.a = out;
        r->u.ref.n = 1;
        return;
    }
    if (v->type == CV_ARRAY) {
        const calc_array *arr = v->u.arr;
        if (argc == 2 && arr->rows == 1) {
            col = row;
            row = 0;
        }
        if (row > arr->rows || col > arr->cols) {
            cv_err(r, OWF_ERROR_REF);
            return;
        }
        if (row && col) {
            *r = arr->v[((unsigned)row - 1) * arr->cols + (unsigned)col - 1];
        } else if (row || col) {
            unsigned n = row ? arr->cols : arr->rows, k;
            calc_array *out = row ? calc_array_new(ctx, 1, n) : calc_array_new(ctx, n, 1);
            if (!out) {
                cv_err(r, OWF_ERROR_NUM);
                return;
            }
            for (k = 0; k < n; k++)
                out->v[k] = row ? arr->v[((unsigned)row - 1) * arr->cols + k] : arr->v[k * arr->cols + (unsigned)col - 1];
            r->type = CV_ARRAY;
            r->u.arr = out;
        } else {
            *r = *v;
        }
        return;
    }
    if (row > 1 || col > 1) {
        cv_err(r, OWF_ERROR_REF);
        return;
    }
    *r = *v;
}

static void fn_choose(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double i;
    NUM(0, 0, i);
    i = floor(i);
    if (i < 1 || i >= argc) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    *r = args[(int)i];
    if (r->type == CV_MISSING)
        cv_num(r, 0);
}

/* ROW and COLUMN: of a reference's first cell (as an array in an array
 * formula); of the formula's own cell without one. */
static void row_col(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int column)
{
    const calc_area *a;
    unsigned n, k;
    calc_array *arr;
    if (!argc || args[0].type == CV_MISSING) {
        cv_num(r, (double)(column ? ctx->col : ctx->row) + 1);
        return;
    }
    PASS_ERROR(0);
    if (args[0].type != CV_REF || args[0].u.ref.n != 1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    a = args[0].u.ref.a;
    n = column ? a->c1 - a->c0 + 1 : a->r1 - a->r0 + 1;
    if (!ctx->actx || n == 1) {
        cv_num(r, (double)(column ? a->c0 : a->r0) + 1);
        return;
    }
    arr = column ? calc_array_new(ctx, 1, n) : calc_array_new(ctx, n, 1);
    if (!arr) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (k = 0; k < n; k++)
        cv_num(&arr->v[k], (double)(column ? a->c0 : a->r0) + k + 1);
    r->type = CV_ARRAY;
    r->u.arr = arr;
}

static void fn_row(calc_ctx *c, calc_value *a, int n, calc_value *r) { row_col(c, a, n, r, 0); }
static void fn_column(calc_ctx *c, calc_value *a, int n, calc_value *r) { row_col(c, a, n, r, 1); }

static void rows_cols(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int column)
{
    (void)ctx;
    (void)argc;
    PASS_ERROR(0);
    if (args[0].type == CV_REF) {
        const calc_area *a = args[0].u.ref.a;
        if (args[0].u.ref.n != 1) {
            cv_err(r, OWF_ERROR_REF);
            return;
        }
        cv_num(r, column ? (double)(a->c1 - a->c0 + 1) : (double)(a->r1 - a->r0 + 1));
    } else if (args[0].type == CV_ARRAY) {
        cv_num(r, column ? (double)args[0].u.arr->cols : (double)args[0].u.arr->rows);
    } else {
        cv_num(r, 1);
    }
}

static void fn_rows(calc_ctx *c, calc_value *a, int n, calc_value *r) { rows_cols(c, a, n, r, 0); }
static void fn_columns(calc_ctx *c, calc_value *a, int n, calc_value *r) { rows_cols(c, a, n, r, 1); }

/* ---- Dates and times ---- */

static void fn_date(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double y, m, d, serial;
    NUM(0, 0, y);
    NUM(1, 0, m);
    NUM(2, 0, d);
    if (fabs(y) > 1e7 || fabs(m) > 1e7 || fabs(d) > 1e8 ||
        !calc_date_serial((long)floor(y), (long)floor(m), (long)floor(d), ctx->book->date1904, &serial))
        cv_err(r, OWF_ERROR_NUM);
    else
        cv_num(r, serial);
}

/* A serial date's day number, or an error. */
static int serial_arg(calc_ctx *ctx, calc_value *args, int argc, double *out)
{
    double x;
    int e = num_arg(ctx, args, argc, 0, 0, &x);
    if (e)
        return e;
    if (x < 0 || x >= 2958466)
        return OWF_ERROR_NUM;
    *out = x;
    return 0;
}

static void date_part(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int which)
{
    double x;
    int y, m, d, e = serial_arg(ctx, args, argc, &x);
    if (e) {
        cv_err(r, e);
        return;
    }
    calc_serial_date((long)floor(x), ctx->book->date1904, &y, &m, &d);
    cv_num(r, which == 0 ? y : which == 1 ? m : d);
}

static void fn_year(calc_ctx *c, calc_value *a, int n, calc_value *r) { date_part(c, a, n, r, 0); }
static void fn_month(calc_ctx *c, calc_value *a, int n, calc_value *r) { date_part(c, a, n, r, 1); }
static void fn_day(calc_ctx *c, calc_value *a, int n, calc_value *r) { date_part(c, a, n, r, 2); }

static void time_part(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int which)
{
    double x, secs;
    int e = serial_arg(ctx, args, argc, &x);
    if (e) {
        cv_err(r, e);
        return;
    }
    secs = floor((x - floor(x)) * 86400 + 0.5);
    if (secs >= 86400)
        secs = 0;
    cv_num(r, which == 0 ? floor(secs / 3600) : which == 1 ? fmod(floor(secs / 60), 60) : fmod(secs, 60));
}

static void fn_hour(calc_ctx *c, calc_value *a, int n, calc_value *r) { time_part(c, a, n, r, 0); }
static void fn_minute(calc_ctx *c, calc_value *a, int n, calc_value *r) { time_part(c, a, n, r, 1); }
static void fn_second(calc_ctx *c, calc_value *a, int n, calc_value *r) { time_part(c, a, n, r, 2); }

static void fn_time(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double h, m, s, t;
    NUM(0, 0, h);
    NUM(1, 0, m);
    NUM(2, 0, s);
    t = floor(h) * 3600 + floor(m) * 60 + floor(s);
    if (t < 0 || h > 32767 || m > 32767 || s > 32767) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, fmod(t, 86400) / 86400);
}

static void fn_today(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)args;
    (void)argc;
    cv_num(r, floor(calc_now(ctx)));
}

static void fn_now(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    (void)args;
    (void)argc;
    cv_num(r, calc_now(ctx));
}

static void fn_weekday(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double x, type;
    long serial, sunday0;
    int e = serial_arg(ctx, args, argc, &x), first;
    if (e) {
        cv_err(r, e);
        return;
    }
    NUM(1, 1, type);
    serial = (long)floor(x) + (ctx->book->date1904 ? 1462 : 0);
    sunday0 = (serial + 6) % 7;          /* 0 Sunday ... 6 Saturday */
    switch ((int)type) {
    case 1: case 17: first = 0; break;    /* Sunday is 1 */
    case 2: case 11: first = 1; break;    /* Monday is 1 */
    case 3: cv_num(r, (double)((sunday0 + 6) % 7)); return;   /* Monday is 0 */
    case 12: first = 2; break;
    case 13: first = 3; break;
    case 14: first = 4; break;
    case 15: first = 5; break;
    case 16: first = 6; break;
    default:
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, (double)((sunday0 - first + 7) % 7 + 1));
}

/* ---- Money ---- */

/* (1 + rate)^n and (1 + rate)^n - 1 without losing the digits a rate
 * near 0 has: through log(1 + x) and exp(x) - 1 in Kahan's forms, which
 * need only log and exp. */
static double log1p_(double x)
{
    double u = 1 + x;
    return u == 1 ? x : log(u) * x / (u - 1);
}

static double expm1_(double x)
{
    double u = exp(x);
    if (u == 1)
        return x;
    if (u - 1 == -1)
        return -1;
    return (u - 1) * x / log(u);
}

static void growth(double rate, double n, double *g, double *g1)
{
    double l = n * log1p_(rate);
    *g = exp(l);
    *g1 = expm1_(l);
}

static void fn_pmt(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double rate, n, pv, fv, type, g, g1;
    NUM(0, 0, rate);
    NUM(1, 0, n);
    NUM(2, 0, pv);
    NUM(3, 0, fv);
    NUM(4, 0, type);
    if (n == 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    type = type != 0;
    if (rate == 0) {
        cv_num(r, -(pv + fv) / n);
        return;
    }
    growth(rate, n, &g, &g1);
    cv_num(r, -(rate * (fv + pv * g)) / ((1 + rate * type) * g1));
}

static void fn_fv(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double rate, n, pmt, pv, type, g, g1;
    NUM(0, 0, rate);
    NUM(1, 0, n);
    NUM(2, 0, pmt);
    NUM(3, 0, pv);
    NUM(4, 0, type);
    type = type != 0;
    if (rate == 0) {
        cv_num(r, -(pv + pmt * n));
        return;
    }
    growth(rate, n, &g, &g1);
    cv_num(r, -(pv * g + pmt * (1 + rate * type) * g1 / rate));
}

static void fn_pv(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    double rate, n, pmt, fv, type, g, g1;
    NUM(0, 0, rate);
    NUM(1, 0, n);
    NUM(2, 0, pmt);
    NUM(3, 0, fv);
    NUM(4, 0, type);
    type = type != 0;
    if (rate == 0) {
        cv_num(r, -(fv + pmt * n));
        return;
    }
    growth(rate, n, &g, &g1);
    cv_num(r, -(fv + pmt * (1 + rate * type) * g1 / rate) / g);
}

typedef struct {
    double rate, sum, factor;
} npv_state;

static int npv_add(void *user, double v)
{
    npv_state *s = user;
    s->factor *= 1 + s->rate;
    s->sum += v / s->factor;
    return NOERR;
}

static void fn_npv(calc_ctx *ctx, calc_value *args, int argc, calc_value *r)
{
    npv_state s;
    int e;
    NUM(0, 0, s.rate);
    if (s.rate == -1) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    s.sum = 0;
    s.factor = 1;
    if ((e = each_number(ctx, args, 1, argc, npv_add, &s)) != NOERR)
        cv_err(r, e);
    else
        cv_num(r, s.sum);
}

/* ---- The table, in alphabetical order ---- */

#define M 255

static const calc_function functions[] = {
    { "ABS", 1, 1, "V", 0, fn_abs },
    { "ACOS", 1, 1, "V", 0, fn_acos },
    { "AND", 1, M, "R", 0, fn_and },
    { "ASIN", 1, 1, "V", 0, fn_asin },
    { "ATAN", 1, 1, "V", 0, fn_atan },
    { "ATAN2", 2, 2, "V", 0, fn_atan2 },
    { "AVERAGE", 1, M, "R", 0, fn_average },
    { "AVERAGEIF", 2, 3, "RVR", 0, fn_averageif },
    { "AVERAGEIFS", 3, M, "R*RV", 0, fn_averageifs },
    { "CEILING", 2, 2, "V", 0, fn_ceiling },
    { "CHAR", 1, 1, "V", 0, fn_char },
    { "CHOOSE", 2, M, "VR", 0, fn_choose },
    { "CLEAN", 1, 1, "V", 0, fn_clean },
    { "CODE", 1, 1, "V", 0, fn_code },
    { "COLUMN", 0, 1, "R", FN_ADDRESS, fn_column },
    { "COLUMNS", 1, 1, "R", FN_ADDRESS, fn_columns },
    { "CONCATENATE", 1, M, "V", 0, fn_concatenate },
    { "COS", 1, 1, "V", 0, fn_cos },
    { "COSH", 1, 1, "V", 0, fn_cosh },
    { "COUNT", 1, M, "R", 0, fn_count },
    { "COUNTA", 1, M, "R", 0, fn_counta },
    { "COUNTBLANK", 1, 1, "R", 0, fn_countblank },
    { "COUNTIF", 2, 2, "RV", 0, fn_countif },
    { "COUNTIFS", 2, M, "*RV", 0, fn_countifs },
    { "DATE", 3, 3, "V", 0, fn_date },
    { "DAY", 1, 1, "V", 0, fn_day },
    { "DEGREES", 1, 1, "V", 0, fn_degrees },
    { "EVEN", 1, 1, "V", 0, fn_even },
    { "EXACT", 2, 2, "V", 0, fn_exact },
    { "EXP", 1, 1, "V", 0, fn_exp },
    { "FACT", 1, 1, "V", 0, fn_fact },
    { "FALSE", 0, 0, "V", 0, fn_false },
    { "FIND", 2, 3, "V", 0, fn_find },
    { "FLOOR", 2, 2, "V", 0, fn_floor },
    { "FV", 3, 5, "V", 0, fn_fv },
    { "HLOOKUP", 3, 4, "VRV", 0, fn_hlookup },
    { "HOUR", 1, 1, "V", 0, fn_hour },
    { "IF", 2, 3, "R", 0, fn_if },
    { "IFERROR", 2, 2, "V", 0, fn_iferror },
    { "IFNA", 2, 2, "V", 0, fn_ifna },
    { "INDEX", 2, 4, "RV", 0, fn_index },
    { "INT", 1, 1, "V", 0, fn_int },
    { "ISBLANK", 1, 1, "V", 0, fn_isblank },
    { "ISERR", 1, 1, "V", 0, fn_iserr },
    { "ISERROR", 1, 1, "V", 0, fn_iserror },
    { "ISLOGICAL", 1, 1, "V", 0, fn_islogical },
    { "ISNA", 1, 1, "V", 0, fn_isna },
    { "ISNONTEXT", 1, 1, "V", 0, fn_isnontext },
    { "ISNUMBER", 1, 1, "V", 0, fn_isnumber },
    { "ISTEXT", 1, 1, "V", 0, fn_istext },
    { "LARGE", 2, 2, "RV", 0, fn_large },
    { "LEFT", 1, 2, "V", 0, fn_left },
    { "LEN", 1, 1, "V", 0, fn_len },
    { "LN", 1, 1, "V", 0, fn_ln },
    { "LOG", 1, 2, "V", 0, fn_log },
    { "LOG10", 1, 1, "V", 0, fn_log10 },
    { "LOWER", 1, 1, "V", 0, fn_lower },
    { "MATCH", 2, 3, "VRV", 0, fn_match },
    { "MAX", 1, M, "R", 0, fn_max },
    { "MEDIAN", 1, M, "R", 0, fn_median },
    { "MID", 3, 3, "V", 0, fn_mid },
    { "MIN", 1, M, "R", 0, fn_min },
    { "MINUTE", 1, 1, "V", 0, fn_minute },
    { "MOD", 2, 2, "V", 0, fn_mod },
    { "MONTH", 1, 1, "V", 0, fn_month },
    { "N", 1, 1, "V", 0, fn_n },
    { "NA", 0, 0, "V", 0, fn_na },
    { "NOT", 1, 1, "V", 0, fn_not },
    { "NOW", 0, 0, "V", FN_VOLATILE, fn_now },
    { "NPV", 2, M, "VR", 0, fn_npv },
    { "ODD", 1, 1, "V", 0, fn_odd },
    { "OR", 1, M, "R", 0, fn_or },
    { "PI", 0, 0, "V", 0, fn_pi },
    { "PMT", 3, 5, "V", 0, fn_pmt },
    { "POWER", 2, 2, "V", 0, fn_power },
    { "PRODUCT", 1, M, "R", 0, fn_product },
    { "PROPER", 1, 1, "V", 0, fn_proper },
    { "PV", 3, 5, "V", 0, fn_pv },
    { "RADIANS", 1, 1, "V", 0, fn_radians },
    { "RAND", 0, 0, "V", FN_VOLATILE, fn_rand },
    { "REPLACE", 4, 4, "V", 0, fn_replace },
    { "REPT", 2, 2, "V", 0, fn_rept },
    { "RIGHT", 1, 2, "V", 0, fn_right },
    { "ROUND", 2, 2, "V", 0, fn_round },
    { "ROUNDDOWN", 2, 2, "V", 0, fn_rounddown },
    { "ROUNDUP", 2, 2, "V", 0, fn_roundup },
    { "ROW", 0, 1, "R", FN_ADDRESS, fn_row },
    { "ROWS", 1, 1, "R", FN_ADDRESS, fn_rows },
    { "SEARCH", 2, 3, "V", 0, fn_search },
    { "SECOND", 1, 1, "V", 0, fn_second },
    { "SIGN", 1, 1, "V", 0, fn_sign },
    { "SIN", 1, 1, "V", 0, fn_sin },
    { "SINH", 1, 1, "V", 0, fn_sinh },
    { "SMALL", 2, 2, "RV", 0, fn_small },
    { "SQRT", 1, 1, "V", 0, fn_sqrt },
    { "STDEV", 1, M, "R", 0, fn_stdev },
    { "STDEVP", 1, M, "R", 0, fn_stdevp },
    { "SUBSTITUTE", 3, 4, "V", 0, fn_substitute },
    { "SUM", 1, M, "R", 0, fn_sum },
    { "SUMIF", 2, 3, "RVR", 0, fn_sumif },
    { "SUMIFS", 3, M, "R*RV", 0, fn_sumifs },
    { "SUMPRODUCT", 1, M, "A", 0, fn_sumproduct },
    { "SUMSQ", 1, M, "R", 0, fn_sumsq },
    { "T", 1, 1, "V", 0, fn_t },
    { "TAN", 1, 1, "V", 0, fn_tan },
    { "TANH", 1, 1, "V", 0, fn_tanh },
    { "TEXT", 2, 2, "V", 0, fn_text },
    { "TIME", 3, 3, "V", 0, fn_time },
    { "TODAY", 0, 0, "V", FN_VOLATILE, fn_today },
    { "TRIM", 1, 1, "V", 0, fn_trim },
    { "TRUE", 0, 0, "V", 0, fn_true },
    { "TRUNC", 1, 2, "V", 0, fn_trunc },
    { "UPPER", 1, 1, "V", 0, fn_upper },
    { "VALUE", 1, 1, "V", 0, fn_value },
    { "VAR", 1, M, "R", 0, fn_var },
    { "VARP", 1, M, "R", 0, fn_varp },
    { "VLOOKUP", 3, 4, "VRV", 0, fn_vlookup },
    { "WEEKDAY", 1, 2, "V", 0, fn_weekday },
    { "YEAR", 1, 1, "V", 0, fn_year },
};

#define NFUNCTIONS ((int)(sizeof functions / sizeof functions[0]))

static int name_cmp(const char *name, size_t length, const char *entry)
{
    size_t i;
    for (i = 0; i < length && entry[i]; i++) {
        int a = owf_upper((unsigned char)name[i]), b = (unsigned char)entry[i];
        if (a != b)
            return a < b ? -1 : 1;
    }
    if (i < length)
        return 1;
    return entry[i] ? -1 : 0;
}

int calc_function_find(const char *name, size_t length)
{
    int lo = 0, hi = NFUNCTIONS - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2, c = name_cmp(name, length, functions[mid].name);
        if (!c)
            return mid;
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return -1;
}

const calc_function *calc_function_at(int id)
{
    return id >= 0 && id < NFUNCTIONS ? &functions[id] : NULL;
}

int calc_function_count(void)
{
    return NFUNCTIONS;
}

int calc_function_arg_class(const calc_function *f, int i)
{
    const char *a = f->args, *star = strchr(a, '*');
    size_t n;
    if (star) {
        size_t before = (size_t)(star - a), cycle = strlen(star + 1);
        if ((size_t)i < before)
            return a[i];
        return cycle ? star[1 + ((size_t)i - before) % cycle] : 'V';
    }
    n = strlen(a);
    return (size_t)i < n ? a[i] : a[n - 1];
}
