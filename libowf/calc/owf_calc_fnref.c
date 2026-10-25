/*
 * libowf: the formula engine's lookup and reference functions beyond the
 * first ones (owf_calc_fn.c): XLOOKUP, XMATCH, LOOKUP, OFFSET, INDIRECT
 * (A1 and R1C1 text), ADDRESS, AREAS, TRANSPOSE, HYPERLINK, FORMULATEXT.
 * Excel's names, argument counts and errors (owf_calc_fn.h).
 *
 * OFFSET and INDIRECT make references as they are worked out: they are
 * volatile (as in Excel), and tell the engine what they reached
 * (calc_note_reach) so that a formula there is worked out first.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_fn.h"

static double trunc_(double x)
{
    return x < 0 ? -floor(-x) : floor(x);
}

static void give_ref(calc_ctx *ctx, calc_value *r, const calc_area *a)
{
    calc_area *out = calc_arena_alloc(ctx->arena, sizeof *out);
    if (!out) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    *out = *a;
    r->type = CV_REF;
    r->u.ref.a = out;
    r->u.ref.n = 1;
}

/* ---- XLOOKUP, XMATCH, LOOKUP ---- */

/* A lookup vector: one row or one column of a table. */
typedef struct {
    calc_table t;
    int by_row;
    unsigned n;                  /* elements there are (a range cut to what the book uses) */
    unsigned long length;        /* as the argument has it (a whole column is 1048576) */
} vec;

static int vec_of(calc_ctx *ctx, const calc_value *v, vec *out)
{
    int e = calc_table_of(ctx, v, &out->t);
    if (e)
        return e;
    if (out->t.a) {
        const calc_area *a = out->t.a;
        out->by_row = a->r0 == a->r1 && a->c1 > a->c0;
        if (!out->by_row && a->c0 != a->c1)
            return OWF_ERROR_VALUE;
        out->length = out->by_row ? (unsigned long)(a->c1 - a->c0) + 1 : (unsigned long)(a->r1 - a->r0) + 1;
    } else {
        out->by_row = out->t.rows == 1 && out->t.cols > 1;
        if (!out->by_row && out->t.cols != 1)
            return OWF_ERROR_VALUE;
        out->length = out->by_row ? out->t.cols : out->t.rows;
    }
    out->n = out->by_row ? out->t.cols : out->t.rows;
    return 0;
}

static void vec_get(const vec *v, unsigned long i, calc_value *out)
{
    calc_table_get(&v->t, v->by_row ? 0 : (unsigned)i, v->by_row ? (unsigned)i : 0, out);
}

/* Where key is in a vector: match 0 exact, -1 exact or the next smaller,
 * 1 exact or the next larger, 2 text with wildcards; search 1 first to
 * last, -1 last to first, 2 by halves in data sorted up, -2 sorted down.
 * Only values of key's kind match (a number never matches text). -1 for
 * none. */
static long xfind(const vec *v, const calc_value *key, int match, int search)
{
    calc_value x, best_v;
    long best = -1, k, i;
    int e, c;
    if (search == 2 || search == -2) {
        long lo = 0, hi = (long)v->n - 1, at;
        while (lo <= hi) {
            long mid = lo + (hi - lo) / 2;
            vec_get(v, (unsigned long)mid, &x);
            c = calc_compare(&x, key, &e);
            if (search < 0)
                c = -c;
            if (c == 0 && x.type == key->type)
                return mid;
            if (c <= 0)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        if (match != -1 && match != 1)
            return -1;
        /* lo is where key would go: before it smaller (sorted up) or larger */
        at = (search > 0) == (match == -1) ? lo - 1 : lo;
        if (at < 0 || at >= (long)v->n)
            return -1;
        vec_get(v, (unsigned long)at, &x);
        return x.type == key->type ? at : -1;
    }
    for (k = 0; k < (long)v->n; k++) {
        i = search > 0 ? k : (long)v->n - 1 - k;
        vec_get(v, (unsigned long)i, &x);
        if (x.type != key->type)
            continue;
        if (match == 2 && key->type == CV_STR) {
            if (calc_wild_match(key->u.str.p, key->u.str.len, x.u.str.p, x.u.str.len))
                return i;
            continue;
        }
        c = calc_compare(&x, key, &e);
        if (c == 0)
            return i;
        if ((match == -1 && c < 0) || (match == 1 && c > 0)) {
            if (best < 0 || (match == -1 ? calc_compare(&x, &best_v, &e) > 0 : calc_compare(&x, &best_v, &e) < 0)) {
                best = i;
                best_v = x;
            }
        }
    }
    return best;
}

/* The key, and match and search modes, from XLOOKUP's or XMATCH's
 * arguments. */
static int x_args(calc_ctx *ctx, calc_value *args, int argc, int im, calc_value *key, int *match, int *search)
{
    double m = 0, s = 1;
    int e;
    *key = args[0];
    if (key->type == CV_ERR)
        return key->u.err;
    if (key->type == CV_EMPTY || key->type == CV_MISSING)
        cv_num(key, 0);
    if ((e = calc_num_arg(ctx, args, argc, im, 0, &m)) != 0 || (e = calc_num_arg(ctx, args, argc, im + 1, 1, &s)) != 0)
        return e;
    if (argc > im + 1 && args[im + 1].type == CV_MISSING)
        s = 1;
    m = trunc_(m);
    s = trunc_(s);
    if (!(m == 0 || m == -1 || m == 1 || m == 2) || !(s == 1 || s == -1 || s == 2 || s == -2))
        return OWF_ERROR_VALUE;
    *match = (int)m;
    *search = (int)s;
    return 0;
}

/* XLOOKUP(value, lookup_array, return_array, [if_not_found], [match_mode],
 * [search_mode]): return_array's row (or column) where value is in
 * lookup_array; a reference when it is one. */
CF(xlookup)
{
    calc_value key;
    vec v;
    int e, match, search;
    long at;
    if ((e = x_args(ctx, args, argc, 4, &key, &match, &search)) != 0 || (e = vec_of(ctx, &args[1], &v)) != 0) {
        cv_err(r, e);
        return;
    }
    PASS_ERROR(2);
    if (args[2].type == CV_REF) {
        const calc_area *ra = args[2].u.ref.a;
        unsigned long len = v.by_row ? (unsigned long)(ra->c1 - ra->c0) + 1 : (unsigned long)(ra->r1 - ra->r0) + 1;
        if (args[2].u.ref.n != 1 || len != v.length) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        if ((at = xfind(&v, &key, match, search)) >= 0) {
            calc_area out = *ra;
            if (v.by_row)
                out.c0 = out.c1 = ra->c0 + (unsigned)at;
            else
                out.r0 = out.r1 = ra->r0 + (unsigned)at;
            give_ref(ctx, r, &out);
            return;
        }
    } else {
        calc_array *ret, *out;
        unsigned k, n;
        if ((e = calc_to_array(ctx, &args[2], &ret)) != 0) {
            cv_err(r, e);
            return;
        }
        if ((v.by_row ? ret->cols : ret->rows) != v.length) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        if ((at = xfind(&v, &key, match, search)) >= 0) {
            n = v.by_row ? ret->rows : ret->cols;
            if (n == 1) {
                *r = ret->v[v.by_row ? (unsigned)at : (unsigned)at * ret->cols];
                return;
            }
            out = v.by_row ? calc_array_new(ctx, n, 1) : calc_array_new(ctx, 1, n);
            if (!out) {
                cv_err(r, OWF_ERROR_NUM);
                return;
            }
            for (k = 0; k < n; k++)
                out->v[k] = v.by_row ? ret->v[k * ret->cols + (unsigned)at] : ret->v[(unsigned)at * ret->cols + k];
            r->type = CV_ARRAY;
            r->u.arr = out;
            return;
        }
    }
    if (calc_given(argc, args, 3))
        *r = args[3];
    else
        cv_err(r, OWF_ERROR_NA);
}

/* XMATCH(value, lookup_array, [match_mode], [search_mode]): value's
 * place (from 1). */
CF(xmatch)
{
    calc_value key;
    vec v;
    int e, match, search;
    long at;
    if ((e = x_args(ctx, args, argc, 2, &key, &match, &search)) != 0 || (e = vec_of(ctx, &args[1], &v)) != 0) {
        cv_err(r, e);
        return;
    }
    at = xfind(&v, &key, match, search);
    if (at < 0)
        cv_err(r, OWF_ERROR_NA);
    else
        cv_num(r, (double)at + 1);
}

/* LOOKUP(value, lookup_vector, [result_vector]) and LOOKUP(value, array):
 * the largest value not above value in data sorted up (by halves, as
 * VLOOKUP's approximate match). The array form searches the first row
 * of an array wider than it is tall, else the first column, and gives
 * the last row's or column's value. */
CF(lookup)
{
    calc_value key = args[0];
    calc_table t;
    int e, by_row;
    long at;
    if (key.type == CV_ERR) {
        *r = key;
        return;
    }
    if (key.type == CV_EMPTY || key.type == CV_MISSING)
        cv_num(&key, 0);
    if ((e = calc_table_of(ctx, &args[1], &t)) != 0) {
        cv_err(r, e);
        return;
    }
    if (argc < 3) {
        by_row = t.cols > t.rows;
        if ((at = calc_lookup(&t, &key, by_row, 0, 1)) < 0)
            cv_err(r, OWF_ERROR_NA);
        else
            calc_table_get(&t, by_row ? t.rows - 1 : (unsigned)at, by_row ? (unsigned)at : t.cols - 1, r);
        return;
    }
    if (t.rows != 1 && t.cols != 1) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    by_row = t.rows == 1 && t.cols > 1;
    if ((at = calc_lookup(&t, &key, by_row, 0, 1)) < 0) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    PASS_ERROR(2);
    if (args[2].type == CV_REF) {        /* along it, even past its end, as Excel */
        const calc_area *ra = args[2].u.ref.a;
        int across = ra->r0 == ra->r1 && ra->c1 > ra->c0;
        unsigned long row = ra->r0 + (across ? 0 : (unsigned long)at), col = ra->c0 + (across ? (unsigned long)at : 0);
        if (row >= CALC_MAX_ROWS || col >= CALC_MAX_COLS)
            cv_err(r, OWF_ERROR_NA);
        else
            calc_cell_value(ctx, ra->s0, (unsigned)row, (unsigned)col, r);
        return;
    }
    {
        calc_array *res;
        if ((e = calc_to_array(ctx, &args[2], &res)) != 0) {
            cv_err(r, e);
            return;
        }
        if ((unsigned long)at >= (unsigned long)res->rows * res->cols)
            cv_err(r, OWF_ERROR_NA);
        else
            *r = res->v[at];
    }
}

/* ---- References made from numbers and text ---- */

/* OFFSET(reference, rows, cols, [height], [width]): a reference moved and
 * sized (a negative height or width reaches up or left); off the sheet,
 * or a height or width of 0, is #REF!. */
CF(offset)
{
    const calc_area *a;
    calc_area out;
    double rows, cols, h, w, r0, c0, r1, c1;
    PASS_ERROR(0);
    if (args[0].type != CV_REF || args[0].u.ref.n != 1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    a = args[0].u.ref.a;
    NUM(1, 0, rows);
    NUM(2, 0, cols);
    h = (double)(a->r1 - a->r0) + 1;
    w = (double)(a->c1 - a->c0) + 1;
    if (calc_given(argc, args, 3))
        NUM(3, 0, h);
    if (calc_given(argc, args, 4))
        NUM(4, 0, w);
    rows = trunc_(rows);
    cols = trunc_(cols);
    h = trunc_(h);
    w = trunc_(w);
    if (h == 0 || w == 0) {
        cv_err(r, OWF_ERROR_REF);
        return;
    }
    r0 = a->r0 + rows;
    c0 = a->c0 + cols;
    r1 = h > 0 ? r0 + h - 1 : r0;
    if (h < 0)
        r0 += h + 1;
    c1 = w > 0 ? c0 + w - 1 : c0;
    if (w < 0)
        c0 += w + 1;
    if (r0 < 0 || c0 < 0 || r1 > CALC_MAX_ROWS - 1 || c1 > CALC_MAX_COLS - 1) {
        cv_err(r, OWF_ERROR_REF);
        return;
    }
    out = *a;
    out.r0 = (unsigned)r0;
    out.r1 = (unsigned)r1;
    out.c0 = (unsigned)c0;
    out.c1 = (unsigned)c1;
    calc_note_reach(ctx, &out);
    give_ref(ctx, r, &out);
}

/* "Sheet1", "'My sheet'" (quotes doubled inside): the sheet's index, or -1
 * (another book's "[Book]Sheet" too). */
static int sheet_of_text(calc_ctx *ctx, const char *p, unsigned n)
{
    char *name = calc_arena_alloc(ctx->arena, (size_t)n + 1);
    unsigned i, k = 0;
    if (!name || !n)
        return -1;
    if (p[0] == '\'') {
        if (n < 2 || p[n - 1] != '\'')
            return -1;
        for (i = 1; i + 1 < n; i++) {
            name[k++] = p[i];
            if (p[i] == '\'' && i + 2 < n && p[i + 1] == '\'')
                i++;
        }
    } else {
        memcpy(name, p, n);
        k = n;
    }
    name[k] = 0;
    if (memchr(name, '[', k))
        return -1;
    return calc_find_sheet(ctx->book, name);
}

/* One R1C1 part: R, R2, R[-1], C, C3, C[2] (case aside). *has: the letter
 * was there; *v: the row or column (from 0). 0 when it is not one. */
static int r1c1_piece(const char **p, const char *e, int letter, unsigned here, unsigned max, int *has, unsigned *v)
{
    const char *q = *p;
    double x = 0;
    int neg = 0, digits = 0;
    *has = 0;
    if (q >= e || owf_upper((unsigned char)*q) != letter)
        return 1;
    q++;
    *has = 1;
    if (q < e && *q == '[') {
        q++;
        if (q < e && (*q == '-' || *q == '+'))
            neg = *q++ == '-';
        while (q < e && *q >= '0' && *q <= '9' && digits < 8) {
            x = x * 10 + (*q++ - '0');
            digits++;
        }
        if (!digits || q >= e || *q++ != ']')
            return 0;
        x = (double)here + (neg ? -x : x);
    } else if (q < e && *q >= '0' && *q <= '9') {
        while (q < e && *q >= '0' && *q <= '9' && digits < 8) {
            x = x * 10 + (*q++ - '0');
            digits++;
        }
        if (x < 1)
            return 0;
        x -= 1;
    } else {
        x = here;
    }
    if (x < 0 || x >= max)
        return 0;
    *v = (unsigned)x;
    *p = q;
    return 1;
}

/* An R1C1 reference's text (without a sheet) as an area: R1C1, R[1]C[-1],
 * R2 (a row), C3 (a column), and two of a kind joined by ':'. */
static int r1c1_area(calc_ctx *ctx, const char *p, const char *e, calc_area *a)
{
    int hr[2], hc[2], k;
    unsigned rr[2] = { 0, 0 }, cc[2] = { 0, 0 };
    for (k = 0; k < 2; k++) {
        if (!r1c1_piece(&p, e, 'R', ctx->row, CALC_MAX_ROWS, &hr[k], &rr[k]) ||
            !r1c1_piece(&p, e, 'C', ctx->col, CALC_MAX_COLS, &hc[k], &cc[k]) || (!hr[k] && !hc[k]))
            return 0;
        if (k == 0) {
            if (p == e) {
                hr[1] = hr[0];
                hc[1] = hc[0];
                rr[1] = rr[0];
                cc[1] = cc[0];
                break;
            }
            if (*p++ != ':')
                return 0;
        }
    }
    if (p != e || hr[0] != hr[1] || hc[0] != hc[1])
        return 0;
    a->r0 = hr[0] ? (rr[0] < rr[1] ? rr[0] : rr[1]) : 0;
    a->r1 = hr[0] ? (rr[0] < rr[1] ? rr[1] : rr[0]) : CALC_MAX_ROWS - 1;
    a->c0 = hc[0] ? (cc[0] < cc[1] ? cc[0] : cc[1]) : 0;
    a->c1 = hc[0] ? (cc[0] < cc[1] ? cc[1] : cc[0]) : CALC_MAX_COLS - 1;
    return 1;
}

/* A reference's text as an area: A1 ("A1", "$B$2:C3", "A:A", "2:5",
 * "Sheet2!A1", "'My sheet'!A1", "Sheet1:Sheet3!A1", a defined name that
 * stands for one) or R1C1. 0 when it is not one. */
static int area_of_text(calc_ctx *ctx, const char *text, unsigned n, int a1, int home, int depth, calc_area *a)
{
    char *buf;
    const char *p;
    calc_token t, end;
    int i;
    if (depth > CALC_MAX_NAME_DEPTH || !n || n > 2000)
        return 0;
    buf = calc_arena_alloc(ctx->arena, (size_t)n + 1);
    if (!buf)
        return 0;
    memcpy(buf, text, n);
    buf[n] = 0;
    a->s0 = a->s1 = home;
    if (!a1) {
        /* the sheet is what comes before the last '!' outside quotes */
        unsigned bang = n, k;
        int quoted = 0;
        for (k = 0; k < n; k++) {
            if (buf[k] == '\'')
                quoted = !quoted;
            else if (buf[k] == '!' && !quoted)
                bang = k;
        }
        if (bang < n) {
            int s = sheet_of_text(ctx, buf, bang);
            if (s < 0)
                return 0;
            a->s0 = a->s1 = s;
            return r1c1_area(ctx, buf + bang + 1, buf + n, a);
        }
        return r1c1_area(ctx, buf, buf + n, a);
    }
    p = buf;
    calc_next_token(&p, &t);
    calc_next_token(&p, &end);
    if (end.type != TK_END)
        return 0;
    if (t.type == TK_REF) {
        if (t.kind == RK_ERR)
            return 0;
        if (t.sheet) {
            calc_sheetref *sr = calc_arena_alloc(ctx->arena, sizeof *sr);
            if (!sr || !calc_split_sheet(&t, sr) || sr->has_book)
                return 0;
            a->s0 = calc_find_sheet(ctx->book, sr->first);
            a->s1 = calc_find_sheet(ctx->book, sr->last);
            if (a->s0 < 0 || a->s1 < 0)
                return 0;
            if (a->s1 < a->s0) {
                int k = a->s0;
                a->s0 = a->s1;
                a->s1 = k;
            }
        }
        a->r0 = t.r0;
        a->c0 = t.c0;
        a->r1 = t.r1;
        a->c1 = t.c1;
        return 1;
    }
    if (t.type == TK_NAME && !t.sheet) {   /* a defined name: the sheet's own, then the book's */
        const owf_book *b = ctx->book;
        for (i = 0; i < 2 * b->nnames; i++) {
            const owf_defname *d = &b->names[i % b->nnames];
            if ((i < b->nnames ? d->sheet == ctx->sheet : d->sheet < 0) && d->formula &&
                calc_text_equal_nocase(d->name, (unsigned)strlen(d->name), buf, n)) {
                const char *f = d->formula[0] == '=' ? d->formula + 1 : d->formula;
                return area_of_text(ctx, f, (unsigned)strlen(f), 1, d->sheet >= 0 ? d->sheet : home, depth + 1, a);
            }
        }
    }
    return 0;
}

/* INDIRECT(ref_text, [a1 = TRUE]): the reference text stands for; text
 * that is not one (or another book's) is #REF!. */
CF(indirect)
{
    const char *p;
    unsigned n;
    int a1;
    calc_area a;
    TEXT(0, p, n);
    BOOL(1, 1, a1);
    while (n && p[0] == ' ') {
        p++;
        n--;
    }
    while (n && p[n - 1] == ' ')
        n--;
    if (!area_of_text(ctx, p, n, a1, ctx->sheet, 0, &a)) {
        cv_err(r, ctx->too_big ? OWF_ERROR_NUM : OWF_ERROR_REF);
        return;
    }
    calc_note_reach(ctx, &a);
    give_ref(ctx, r, &a);
}

/* ADDRESS(row, column, [abs = 1], [a1 = TRUE], [sheet_text]): 1 "$C$2",
 * 2 "C$2", 3 "$C2", 4 "C2"; R1C1 "R2C3", "R2C[3]", "R[2]C3", "R[2]C[3]". */
CF(address)
{
    double row, col, kind;
    int a1;
    char name[20];
    owf_buf b;
    unsigned k;
    NUM(0, 0, row);
    NUM(1, 0, col);
    NUM(2, 1, kind);
    BOOL(3, 1, a1);
    row = trunc_(row);
    col = trunc_(col);
    kind = trunc_(kind);
    if (row < 1 || row > CALC_MAX_ROWS || col < 1 || col > CALC_MAX_COLS || kind < 1 || kind > 4) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    owf_buf_init(&b);
    if (calc_given(argc, args, 4)) {
        const char *s;
        unsigned sn;
        TEXT(4, s, sn);
        if (sn && s[0] == '[') {         /* "[Book1]Sheet1" as it is */
            owf_buf_put(&b, s, sn);
            owf_buf_putc(&b, '!');
        } else if (sn) {
            char *z = calc_arena_alloc(ctx->arena, (size_t)sn + 1);
            if (!z) {
                cv_err(r, OWF_ERROR_NUM);
                owf_buf_free(&b);
                return;
            }
            memcpy(z, s, sn);
            z[sn] = 0;
            calc_put_sheet_prefix(&b, z, NULL);
        } else {
            owf_buf_putc(&b, '!');
        }
    }
    if (a1) {
        int abs_row = kind == 1 || kind == 2, abs_col = kind == 1 || kind == 3;
        owf_calc_cell_name((unsigned)row - 1, (unsigned)col - 1, name);
        for (k = 0; name[k] >= 'A' && name[k] <= 'Z'; k++)
            ;
        if (abs_col)
            owf_buf_putc(&b, '$');
        owf_buf_put(&b, name, k);
        if (abs_row)
            owf_buf_putc(&b, '$');
        owf_buf_puts(&b, name + k);
    } else {
        int rel_row = kind == 3 || kind == 4, rel_col = kind == 2 || kind == 4;
        owf_calc_cell_name((unsigned)row - 1, 0, name);
        owf_buf_puts(&b, rel_row ? "R[" : "R");
        owf_buf_puts(&b, name + 1);
        owf_buf_puts(&b, rel_row ? "]C" : "C");
        owf_calc_cell_name((unsigned)col - 1, 0, name);
        if (rel_col)
            owf_buf_putc(&b, '[');
        owf_buf_puts(&b, name + 1);
        if (rel_col)
            owf_buf_putc(&b, ']');
    }
    calc_take_text(ctx, &b, r);
}

CF(areas)
{
    (void)ctx;
    (void)argc;
    PASS_ERROR(0);
    if (args[0].type != CV_REF)
        cv_err(r, OWF_ERROR_VALUE);
    else
        cv_num(r, args[0].u.ref.n);
}

CF(transpose)
{
    calc_array *a, *t;
    unsigned i, j;
    int e;
    (void)argc;
    PASS_ERROR(0);
    if ((e = calc_to_array(ctx, &args[0], &a)) != 0) {
        cv_err(r, e);
        return;
    }
    if ((t = calc_array_new(ctx, a->cols, a->rows)) == NULL) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < a->rows; i++)
        for (j = 0; j < a->cols; j++) {
            t->v[j * a->rows + i] = a->v[i * a->cols + j];
            if (t->v[j * a->rows + i].type == CV_EMPTY)
                cv_num(&t->v[j * a->rows + i], 0);
        }
    r->type = CV_ARRAY;
    r->u.arr = t;
}

/* HYPERLINK(link, [friendly_name]): what the cell shows (a link to follow
 * is the program's business). */
CF(hyperlink)
{
    (void)ctx;
    *r = calc_given(argc, args, 1) ? args[1] : args[0];
    if (r->type == CV_EMPTY || r->type == CV_MISSING)
        cv_num(r, 0);
}

/* FORMULATEXT(reference): the formula of the reference's corner cell, with
 * its "="; a cell without one is #N/A. */
CF(formulatext)
{
    const calc_area *a;
    const owf_cell *c;
    char *s;
    size_t n;
    (void)argc;
    PASS_ERROR(0);
    if (args[0].type != CV_REF) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    a = args[0].u.ref.a;
    c = a->s0 >= 0 && a->s0 < ctx->book->nsheets ? owf_sheet_find(&ctx->book->sheets[a->s0], a->r0, a->c0) : NULL;
    if (!c || !c->formula) {
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    n = strlen(c->formula);
    s = calc_arena_alloc(ctx->arena, n + 2);
    if (!s) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    s[0] = '=';
    memcpy(s + 1, c->formula, n + 1);
    cv_str_copy(ctx, r, s, n + 1);
}
