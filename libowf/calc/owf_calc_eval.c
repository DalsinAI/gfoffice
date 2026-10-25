/*
 * libowf: working formulas out. Values and Excel's conversions between
 * them, the operators, and the machine that runs a formula's bytecode: a
 * loop over the instructions with a stack of values, no recursion. Working
 * values (text made on the way, arrays) come from an arena that is emptied
 * after each formula, with a limit, so one formula's memory is bounded.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_int.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- The arena ---- */

struct calc_block {
    calc_block *next;
    size_t used, size;
    double data[1];              /* aligned for doubles */
};

#define BLOCK_MIN 4000

void *calc_arena_alloc(calc_arena *arena, size_t n)
{
    calc_block *b = arena->head;
    size_t want;
    n = (n + 7) & ~(size_t)7;
    if (b && b->size - b->used >= n) {
        void *p = (char *)b->data + b->used;
        b->used += n;
        return p;
    }
    if (arena->total + (long)n > CALC_ARENA_MAX)
        return NULL;
    want = n > BLOCK_MIN ? n : BLOCK_MIN;
    b = malloc(sizeof *b + want);
    if (!b)
        return NULL;
    b->size = want;
    b->used = n;
    arena->total += (long)want;
    /* a big block goes behind the current one, so its room is not lost */
    if (arena->head && n > BLOCK_MIN) {
        b->next = arena->head->next;
        arena->head->next = b;
        b->used = b->size;
    } else {
        b->next = arena->head;
        arena->head = b;
    }
    return b->data;
}

void calc_arena_reset(calc_arena *arena)
{
    calc_block *b = arena->head, *keep = NULL;
    while (b) {
        calc_block *next = b->next;
        if (!keep && b->size == BLOCK_MIN) {
            keep = b;
            keep->used = 0;
        } else {
            free(b);
        }
        b = next;
    }
    arena->head = keep;
    if (keep)
        keep->next = NULL;
    arena->total = keep ? BLOCK_MIN : 0;
}

void calc_arena_free(calc_arena *arena)
{
    calc_block *b = arena->head;
    while (b) {
        calc_block *next = b->next;
        free(b);
        b = next;
    }
    arena->head = NULL;
    arena->total = 0;
}

static void *ctx_alloc(calc_ctx *ctx, size_t n)
{
    void *p = calc_arena_alloc(ctx->arena, n);
    if (!p) {
        if (ctx->arena->total + (long)n > CALC_ARENA_MAX)
            ctx->too_big = 1;
        else
            ctx->failed = 1;
    }
    return p;
}

void calc_ctx_init(calc_ctx *ctx, owf_calc *calc, const owf_book *book, calc_arena *arena, int sheet,
                   unsigned row, unsigned col)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->calc = calc;
    ctx->book = book;
    ctx->arena = arena;
    ctx->sheet = sheet;
    ctx->row = row;
    ctx->col = col;
    ctx->dmy = OWF_CALC_DMY;
    ctx->used_rows = ctx->used_cols = -1;
}

/* ---- Values ---- */

void cv_num(calc_value *v, double n)
{
    if (n != n || n > 1.7976931348623157e308 || n < -1.7976931348623157e308) {
        cv_err(v, OWF_ERROR_NUM);
        return;
    }
    v->type = CV_NUM;
    v->u.num = n;
}

void cv_bool(calc_value *v, int b)
{
    v->type = CV_BOOL;
    v->u.num = b ? 1 : 0;
}

void cv_err(calc_value *v, int err)
{
    v->type = CV_ERR;
    v->u.err = err;
}

void cv_str(calc_value *v, const char *p, unsigned len)
{
    v->type = CV_STR;
    v->u.str.p = p;
    v->u.str.len = len;
}

void cv_empty(calc_value *v)
{
    v->type = CV_EMPTY;
}

void cv_str_copy(calc_ctx *ctx, calc_value *v, const char *p, size_t len)
{
    char *s;
    if (len > (size_t)CALC_MAX_TEXT * 4 || calc_utf8_count(p, (unsigned)len) > CALC_MAX_TEXT) {
        cv_err(v, OWF_ERROR_VALUE);
        return;
    }
    s = ctx_alloc(ctx, len + 1);
    if (!s) {
        cv_err(v, OWF_ERROR_NUM);
        return;
    }
    memcpy(s, p, len);
    s[len] = 0;
    cv_str(v, s, (unsigned)len);
}

void calc_value_of_cell(const owf_cell *c, calc_value *out)
{
    if (!c) {
        cv_empty(out);
        return;
    }
    switch (c->type) {
    case OWF_VALUE_NUMBER:
        out->type = CV_NUM;
        out->u.num = c->number;
        break;
    case OWF_VALUE_TEXT:
        cv_str(out, c->text ? c->text : "", c->text ? (unsigned)strlen(c->text) : 0);
        break;
    case OWF_VALUE_BOOL:
        cv_bool(out, c->number != 0);
        break;
    case OWF_VALUE_ERROR:
        cv_err(out, c->error);
        break;
    default:
        cv_empty(out);
        break;
    }
}

void calc_cell_value(calc_ctx *ctx, int sheet, unsigned row, unsigned col, calc_value *out)
{
    if (sheet < 0 || sheet >= ctx->book->nsheets) {
        cv_err(out, OWF_ERROR_REF);
        return;
    }
    calc_value_of_cell(owf_sheet_find(&ctx->book->sheets[sheet], row, col), out);
}

/* ---- Areas ---- */

static int key_before(const owf_cell *c, unsigned row, unsigned col)
{
    return c->row < row || (c->row == row && c->col < col);
}

/* The first cell at or after (row, col), searching on from i: steps that
 * double, then halves, so a near one is found fast. */
static int seek(const owf_sheet *s, int i, unsigned row, unsigned col)
{
    int step = 1, lo, hi;
    if (i >= s->ncells || !key_before(&s->cells[i], row, col))
        return i;
    while (i + step < s->ncells && key_before(&s->cells[i + step], row, col))
        step *= 2;
    lo = i + step / 2 + 1;
    hi = i + step < s->ncells ? i + step : s->ncells;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (key_before(&s->cells[mid], row, col))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int calc_area_each(calc_ctx *ctx, const calc_area *a, calc_cell_fn fn, void *user)
{
    int sheet;
    for (sheet = a->s0; sheet <= a->s1; sheet++) {
        const owf_sheet *s;
        int i;
        if (sheet < 0 || sheet >= ctx->book->nsheets)
            continue;
        s = &ctx->book->sheets[sheet];
        i = seek(s, 0, a->r0, a->c0);
        while (i < s->ncells) {
            const owf_cell *c = &s->cells[i];
            int stop;
            if (c->row > a->r1)
                break;
            if (c->col < a->c0) {
                i = seek(s, i, c->row, a->c0);
                continue;
            }
            if (c->col > a->c1) {
                if (c->row == a->r1)
                    break;
                i = seek(s, i, c->row + 1, a->c0);
                continue;
            }
            if (c->type != OWF_VALUE_EMPTY && (stop = fn(user, c, sheet)) != 0)
                return stop;
            i++;
        }
    }
    return 0;
}

/* The book's last row and column used, plus 1 (the engine keeps them
 * between formulas). */
static void used_extent(calc_ctx *ctx)
{
    long *cache = calc_used_cache(ctx->calc);
    int k, i;
    long rows = 0, cols = 0;
    if (ctx->used_rows >= 0)
        return;
    if (cache && cache[0] >= 0) {
        ctx->used_rows = cache[0];
        ctx->used_cols = cache[1];
        return;
    }
    for (k = 0; k < ctx->book->nsheets; k++) {
        const owf_sheet *s = &ctx->book->sheets[k];
        if (s->ncells && (long)s->cells[s->ncells - 1].row + 1 > rows)
            rows = (long)s->cells[s->ncells - 1].row + 1;
        for (i = 0; i < s->ncells; i++)
            if ((long)s->cells[i].col + 1 > cols)
                cols = (long)s->cells[i].col + 1;
    }
    ctx->used_rows = rows;
    ctx->used_cols = cols;
    if (cache) {
        cache[0] = rows;
        cache[1] = cols;
    }
}

void calc_area_clip(calc_ctx *ctx, const calc_area *a, unsigned *r1, unsigned *c1)
{
    *r1 = a->r1;
    *c1 = a->c1;
    if (a->r1 >= CALC_MAX_ROWS - 1 || a->c1 >= CALC_MAX_COLS - 1) {
        used_extent(ctx);
        if (a->r1 >= CALC_MAX_ROWS - 1 && ctx->used_rows - 1 < (long)a->r1)
            *r1 = ctx->used_rows - 1 > (long)a->r0 ? (unsigned)(ctx->used_rows - 1) : a->r0;
        if (a->c1 >= CALC_MAX_COLS - 1 && ctx->used_cols - 1 < (long)a->c1)
            *c1 = ctx->used_cols - 1 > (long)a->c0 ? (unsigned)(ctx->used_cols - 1) : a->c0;
    }
}

/* ---- Plain values from references and arrays ---- */

void calc_scalar(calc_ctx *ctx, const calc_value *in, calc_value *out)
{
    const calc_area *a;
    switch (in->type) {
    case CV_REF:
        a = in->u.ref.a;
        if (in->u.ref.n != 1 || a->s0 != a->s1)
            cv_err(out, OWF_ERROR_VALUE);
        else if (a->r0 == a->r1 && a->c0 == a->c1)
            calc_cell_value(ctx, a->s0, a->r0, a->c0, out);
        else if (a->c0 == a->c1 && ctx->row >= a->r0 && ctx->row <= a->r1)
            calc_cell_value(ctx, a->s0, ctx->row, a->c0, out);
        else if (a->r0 == a->r1 && ctx->col >= a->c0 && ctx->col <= a->c1)
            calc_cell_value(ctx, a->s0, a->r0, ctx->col, out);
        else
            cv_err(out, OWF_ERROR_VALUE);
        break;
    case CV_ARRAY:
        *out = in->u.arr->v[0];
        break;
    case CV_MISSING:
        cv_empty(out);
        break;
    default:
        *out = *in;
        break;
    }
}

calc_array *calc_array_new(calc_ctx *ctx, unsigned rows, unsigned cols)
{
    calc_array *arr;
    unsigned long n = (unsigned long)rows * cols, i;
    if (!rows || !cols || n > (unsigned long)(CALC_ARENA_MAX / sizeof(calc_value))) {
        ctx->too_big = 1;
        return NULL;
    }
    arr = ctx_alloc(ctx, sizeof *arr + (size_t)(n - 1) * sizeof(calc_value));
    if (!arr)
        return NULL;
    arr->rows = rows;
    arr->cols = cols;
    for (i = 0; i < n; i++)
        arr->v[i].type = CV_EMPTY;
    return arr;
}

typedef struct {
    calc_array *arr;
    unsigned r0, c0, r1, c1;
} fill_job;

static int fill_cell(void *user, const owf_cell *c, int sheet)
{
    fill_job *j = user;
    (void)sheet;
    if (c->row <= j->r1 && c->col <= j->c1)
        calc_value_of_cell(c, &j->arr->v[(c->row - j->r0) * j->arr->cols + (c->col - j->c0)]);
    return 0;
}

int calc_to_array(calc_ctx *ctx, const calc_value *in, calc_array **out)
{
    calc_array *arr;
    if (in->type == CV_ARRAY) {
        *out = in->u.arr;
        return 0;
    }
    if (in->type == CV_REF) {
        const calc_area *a = in->u.ref.a;
        fill_job j;
        calc_area clipped;
        if (in->u.ref.n != 1 || a->s0 != a->s1)
            return OWF_ERROR_VALUE;
        calc_area_clip(ctx, a, &j.r1, &j.c1);
        arr = calc_array_new(ctx, j.r1 - a->r0 + 1, j.c1 - a->c0 + 1);
        if (!arr)
            return OWF_ERROR_NUM;
        j.arr = arr;
        j.r0 = a->r0;
        j.c0 = a->c0;
        clipped = *a;
        clipped.r1 = j.r1;
        clipped.c1 = j.c1;
        calc_area_each(ctx, &clipped, fill_cell, &j);
        *out = arr;
        return 0;
    }
    arr = calc_array_new(ctx, 1, 1);
    if (!arr)
        return OWF_ERROR_NUM;
    if (in->type != CV_MISSING)
        arr->v[0] = *in;
    *out = arr;
    return 0;
}

/* ---- Conversions ---- */

int calc_approx_equal(double a, double b)
{
    double d;
    if (a == b)
        return 1;
    d = a - b;
    if (d < 0)
        d = -d;
    return d < (a < 0 ? -a : a) * (1.0 / (16777216.0 * 16777216.0));
}

double calc_approx_add(double a, double b)
{
    if (((a < 0 && b > 0) || (a > 0 && b < 0)) && calc_approx_equal(a, -b))
        return 0;
    return a + b;
}

double calc_approx_floor(double x)
{
    double f = floor(x);
    return calc_approx_equal(x, f + 1) ? f + 1 : f;
}

double calc_approx_ceil(double x)
{
    double c = ceil(x);
    return calc_approx_equal(x, c - 1) ? c - 1 : c;
}

int calc_to_number(calc_ctx *ctx, const calc_value *v, double *out)
{
    calc_value s;
    int kind, decimals;
    switch (v->type) {
    case CV_NUM:
    case CV_BOOL:
        *out = v->u.num;
        return 0;
    case CV_EMPTY:
    case CV_MISSING:
        *out = 0;
        return 0;
    case CV_ERR:
        return v->u.err;
    case CV_STR:
        if (!calc_parse_number(v->u.str.p, v->u.str.len, ctx->dmy, ctx->book->date1904, out, &kind, &decimals,
                               NULL, 0))
            return OWF_ERROR_VALUE;
        return 0;
    }
    calc_scalar(ctx, v, &s);
    if (s.type == CV_REF || s.type == CV_ARRAY)
        return OWF_ERROR_VALUE;
    return calc_to_number(ctx, &s, out);
}

int calc_to_bool(calc_ctx *ctx, const calc_value *v, int *out)
{
    calc_value s;
    switch (v->type) {
    case CV_NUM:
    case CV_BOOL:
        *out = v->u.num != 0;
        return 0;
    case CV_EMPTY:
    case CV_MISSING:
        *out = 0;
        return 0;
    case CV_ERR:
        return v->u.err;
    case CV_STR:
        if (calc_text_equal_nocase(v->u.str.p, v->u.str.len, "TRUE", 4))
            *out = 1;
        else if (calc_text_equal_nocase(v->u.str.p, v->u.str.len, "FALSE", 5))
            *out = 0;
        else
            return OWF_ERROR_VALUE;
        return 0;
    }
    calc_scalar(ctx, v, &s);
    if (s.type == CV_REF || s.type == CV_ARRAY)
        return OWF_ERROR_VALUE;
    return calc_to_bool(ctx, &s, out);
}

int calc_to_text(calc_ctx *ctx, const calc_value *v, const char **p, unsigned *len)
{
    calc_value s;
    char num[40];
    char *t;
    size_t n;
    switch (v->type) {
    case CV_STR:
        *p = v->u.str.p;
        *len = v->u.str.len;
        return 0;
    case CV_EMPTY:
    case CV_MISSING:
        *p = "";
        *len = 0;
        return 0;
    case CV_BOOL:
        *p = v->u.num ? "TRUE" : "FALSE";
        *len = v->u.num ? 4 : 5;
        return 0;
    case CV_ERR:
        return v->u.err;
    case CV_NUM:
        calc_number_text(v->u.num, num);
        n = strlen(num);
        t = ctx_alloc(ctx, n + 1);
        if (!t)
            return OWF_ERROR_NUM;
        memcpy(t, num, n + 1);
        *p = t;
        *len = (unsigned)n;
        return 0;
    }
    calc_scalar(ctx, v, &s);
    if (s.type == CV_REF || s.type == CV_ARRAY)
        return OWF_ERROR_VALUE;
    return calc_to_text(ctx, &s, p, len);
}

static int type_rank(int type)
{
    return type == CV_NUM ? 0 : type == CV_STR ? 1 : 2;
}

int calc_compare(const calc_value *a, const calc_value *b, int *err)
{
    calc_value x = *a, y = *b;
    *err = 0;
    if (x.type == CV_MISSING)
        x.type = CV_EMPTY;
    if (y.type == CV_MISSING)
        y.type = CV_EMPTY;
    if (x.type == CV_ERR) {
        *err = x.u.err;
        return 0;
    }
    if (y.type == CV_ERR) {
        *err = y.u.err;
        return 0;
    }
    /* an empty cell is 0, "" or FALSE, as the other side is */
    if (x.type == CV_EMPTY && y.type == CV_EMPTY)
        return 0;
    if (x.type == CV_EMPTY) {
        if (y.type == CV_STR)
            cv_str(&x, "", 0);
        else if (y.type == CV_BOOL)
            cv_bool(&x, 0);
        else
            cv_num(&x, 0);
    }
    if (y.type == CV_EMPTY) {
        if (x.type == CV_STR)
            cv_str(&y, "", 0);
        else if (x.type == CV_BOOL)
            cv_bool(&y, 0);
        else
            cv_num(&y, 0);
    }
    if (type_rank(x.type) != type_rank(y.type))
        return type_rank(x.type) < type_rank(y.type) ? -1 : 1;
    if (x.type == CV_STR)
        return calc_text_compare_nocase(x.u.str.p, x.u.str.len, y.u.str.p, y.u.str.len);
    if (calc_approx_equal(x.u.num, y.u.num))
        return 0;
    return x.u.num < y.u.num ? -1 : 1;
}

/* ---- Text ---- */

unsigned calc_utf8_count(const char *p, unsigned len)
{
    unsigned i, n = 0;
    for (i = 0; i < len; i++)
        if (((unsigned char)p[i] & 0xC0) != 0x80)
            n++;
    return n;
}

unsigned calc_utf8_offset(const char *p, unsigned len, unsigned n)
{
    unsigned i;
    for (i = 0; i < len; i++)
        if (((unsigned char)p[i] & 0xC0) != 0x80) {
            if (!n)
                return i;
            n--;
        }
    return len;
}

/* The pairs of letters in Latin Extended-A, Greek and Cyrillic, and Latin-1's. */
unsigned long calc_lower(unsigned long c)
{
    if (c < 0x80)
        return c >= 'A' && c <= 'Z' ? c + 32 : c;
    if ((c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3AB && c != 0x3A2) ||
        (c >= 0x410 && c <= 0x42F))
        return c + 32;
    if (c >= 0x400 && c <= 0x40F)
        return c + 80;
    if (c >= 0x100 && c <= 0x17F) {
        if (c == 0x178)
            return 0xFF;
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
            return (c & 1) ? c + 1 : c;
        if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F)
            return c;
        return (c & 1) ? c : c + 1;
    }
    return c;
}

unsigned long calc_upper(unsigned long c)
{
    if (c < 0x80)
        return c >= 'a' && c <= 'z' ? c - 32 : c;
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) ||
        (c >= 0x430 && c <= 0x44F))
        return c - 32;
    if (c == 0xFF)
        return 0x178;
    if (c >= 0x450 && c <= 0x45F)
        return c - 80;
    if (c >= 0x100 && c <= 0x17F) {
        if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
            return (c & 1) ? c : c - 1;
        if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F)
            return c;
        return (c & 1) ? c - 1 : c;
    }
    return c;
}

unsigned long calc_fold(unsigned long c)
{
    return calc_lower(c);
}

int calc_is_letter(unsigned long c)
{
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    return calc_lower(c) != c || calc_upper(c) != c || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7);
}

static unsigned long next_char(const char **p, const char *end)
{
    return owf_utf8_next((const unsigned char **)p, (const unsigned char *)end);
}

int calc_text_compare_nocase(const char *a, unsigned alen, const char *b, unsigned blen)
{
    const char *ae = a + alen, *be = b + blen;
    while (a < ae && b < be) {
        unsigned long x = calc_fold(next_char(&a, ae)), y = calc_fold(next_char(&b, be));
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (a < ae)
        return 1;
    if (b < be)
        return -1;
    return 0;
}

int calc_text_equal_nocase(const char *a, unsigned alen, const char *b, unsigned blen)
{
    return calc_text_compare_nocase(a, alen, b, blen) == 0;
}

/* Wildcards without recursion: on a mismatch, go back to the last * and
 * let it take one more character. prefix: the pattern need only match the
 * start of s; *matched gets how many bytes of s it took. */
static int wild(const char *pat, const char *pe, const char *s, const char *se, int prefix, const char **matched)
{
    const char *star_p = NULL, *star_s = NULL;
    while (1) {
        if (pat < pe && *pat == '*') {
            while (pat < pe && *pat == '*')
                pat++;
            if (pat == pe) {
                if (matched)
                    *matched = prefix ? s : se;
                return 1;
            }
            star_p = pat;
            star_s = s;
            continue;
        }
        if (pat == pe) {
            if (s == se || prefix) {
                if (matched)
                    *matched = s;
                return 1;
            }
        } else if (s < se) {
            const char *p2 = pat, *s2 = s;
            unsigned long pc, sc;
            int any = 0;
            if (*p2 == '~' && p2 + 1 < pe && (p2[1] == '*' || p2[1] == '?' || p2[1] == '~'))
                p2++;
            else if (*p2 == '?')
                any = 1;
            pc = next_char(&p2, pe);
            sc = next_char(&s2, se);
            if (any || calc_fold(pc) == calc_fold(sc)) {
                pat = p2;
                s = s2;
                continue;
            }
        }
        if (!star_p || star_s >= se)
            return 0;
        next_char(&star_s, se);
        pat = star_p;
        s = star_s;
    }
}

int calc_wild_match(const char *pat, unsigned plen, const char *s, unsigned slen)
{
    return wild(pat, pat + plen, s, s + slen, 0, NULL);
}

long calc_wild_search(const char *pat, unsigned plen, const char *s, unsigned slen, unsigned start)
{
    const char *p = s + start, *end = s + slen;
    while (1) {
        if (wild(pat, pat + plen, p, end, 1, NULL))
            return (long)(p - s);
        if (p >= end)
            return -1;
        next_char(&p, end);
    }
}

void calc_put_utf8(owf_buf *b, unsigned long c)
{
    owf_buf_put_utf8(b, c);
}

/* ---- Operators ---- */

static int is_multi_ref(const calc_value *v)
{
    const calc_area *a;
    if (v->type != CV_REF)
        return 0;
    a = v->u.ref.a;
    return v->u.ref.n != 1 || a->s0 != a->s1 || a->r0 != a->r1 || a->c0 != a->c1;
}

/* An operand of an operator or a value argument: a plain value, or an
 * array (a reference's cells, when worked as arrays). */
void calc_operand(calc_ctx *ctx, calc_value *v, int actx)
{
    calc_value s;
    if (v->type == CV_REF) {
        if (actx && is_multi_ref(v)) {
            calc_array *arr;
            int e = calc_to_array(ctx, v, &arr);
            if (e)
                cv_err(v, e);
            else {
                v->type = CV_ARRAY;
                v->u.arr = arr;
            }
            return;
        }
        calc_scalar(ctx, v, &s);
        *v = s;
    }
}

/* Element (i, j) of an array as Excel stretches one: a single row or
 * column (or one value) repeats; past the end of a bigger one, #N/A. */
void calc_element(const calc_value *v, unsigned i, unsigned j, calc_value *out)
{
    const calc_array *a;
    if (v->type != CV_ARRAY) {
        *out = *v;
        if (out->type == CV_MISSING)
            out->type = CV_EMPTY;
        return;
    }
    a = v->u.arr;
    if (a->rows == 1)
        i = 0;
    if (a->cols == 1)
        j = 0;
    if (i >= a->rows || j >= a->cols)
        cv_err(out, OWF_ERROR_NA);
    else
        *out = a->v[i * a->cols + j];
}

void calc_dims(const calc_value *v, unsigned *rows, unsigned *cols)
{
    if (v->type == CV_ARRAY) {
        if (v->u.arr->rows > *rows)
            *rows = v->u.arr->rows;
        if (v->u.arr->cols > *cols)
            *cols = v->u.arr->cols;
    }
}

void calc_power(double a, double b, calc_value *r)
{
    if (a == 0 && b == 0) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if (a == 0 && b < 0) {
        cv_err(r, OWF_ERROR_DIV0);
        return;
    }
    if (a < 0 && b != floor(b)) {       /* (-8)^(1/3) too, as Excel */
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(r, pow(a, b));
}

static void scalar_binary(calc_ctx *ctx, int op, const calc_value *a, const calc_value *b, calc_value *r)
{
    double x, y;
    int e, cmp;
    if (op == OP_CAT) {
        const char *p, *q;
        unsigned n, m;
        char *s;
        if ((e = calc_to_text(ctx, a, &p, &n)) != 0 || (e = calc_to_text(ctx, b, &q, &m)) != 0) {
            cv_err(r, e);
            return;
        }
        if (calc_utf8_count(p, n) + calc_utf8_count(q, m) > CALC_MAX_TEXT) {
            cv_err(r, OWF_ERROR_VALUE);
            return;
        }
        s = ctx_alloc(ctx, (size_t)n + m + 1);
        if (!s) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        memcpy(s, p, n);
        memcpy(s + n, q, m);
        s[n + m] = 0;
        cv_str(r, s, n + m);
        return;
    }
    if (op >= OP_EQ && op <= OP_GE) {
        cmp = calc_compare(a, b, &e);
        if (e) {
            cv_err(r, e);
            return;
        }
        switch (op) {
        case OP_EQ: cv_bool(r, cmp == 0); break;
        case OP_NE: cv_bool(r, cmp != 0); break;
        case OP_LT: cv_bool(r, cmp < 0); break;
        case OP_LE: cv_bool(r, cmp <= 0); break;
        case OP_GT: cv_bool(r, cmp > 0); break;
        default: cv_bool(r, cmp >= 0); break;
        }
        return;
    }
    if ((e = calc_to_number(ctx, a, &x)) != 0 || (e = calc_to_number(ctx, b, &y)) != 0) {
        cv_err(r, e);
        return;
    }
    switch (op) {
    case OP_ADD: cv_num(r, calc_approx_add(x, y)); break;
    case OP_SUB: cv_num(r, calc_approx_add(x, -y)); break;
    case OP_MUL: cv_num(r, x * y); break;
    case OP_DIV:
        if (y == 0)
            cv_err(r, OWF_ERROR_DIV0);
        else
            cv_num(r, x / y);
        break;
    default: calc_power(x, y, r); break;
    }
}

static void scalar_unary(calc_ctx *ctx, int op, const calc_value *a, calc_value *r)
{
    double x;
    int e;
    if (op == OP_PLUS) {
        *r = *a;
        if (r->type == CV_MISSING)
            r->type = CV_EMPTY;
        return;
    }
    if ((e = calc_to_number(ctx, a, &x)) != 0) {
        cv_err(r, e);
        return;
    }
    cv_num(r, op == OP_NEG ? -x : x / 100);
}

/* The reference operators: range (the box around both), union (both),
 * intersection (the cells in both: #NULL! for none). */
static void ref_op(calc_ctx *ctx, int op, const calc_value *a, const calc_value *b, calc_value *r)
{
    calc_area *out;
    const calc_area *x, *y;
    if (a->type == CV_ERR) {
        *r = *a;
        return;
    }
    if (b->type == CV_ERR) {
        *r = *b;
        return;
    }
    if (a->type != CV_REF || b->type != CV_REF) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (op == OP_UNION) {
        int n = a->u.ref.n + b->u.ref.n;
        out = ctx_alloc(ctx, (size_t)n * sizeof *out);
        if (!out) {
            cv_err(r, OWF_ERROR_NUM);
            return;
        }
        memcpy(out, a->u.ref.a, (size_t)a->u.ref.n * sizeof *out);
        memcpy(out + a->u.ref.n, b->u.ref.a, (size_t)b->u.ref.n * sizeof *out);
        r->type = CV_REF;
        r->u.ref.a = out;
        r->u.ref.n = n;
        return;
    }
    x = a->u.ref.a;
    y = b->u.ref.a;
    if (a->u.ref.n != 1 || b->u.ref.n != 1 || x->s0 != y->s0 || x->s1 != y->s1) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    out = ctx_alloc(ctx, sizeof *out);
    if (!out) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    *out = *x;
    if (op == OP_RANGE) {
        out->r0 = x->r0 < y->r0 ? x->r0 : y->r0;
        out->c0 = x->c0 < y->c0 ? x->c0 : y->c0;
        out->r1 = x->r1 > y->r1 ? x->r1 : y->r1;
        out->c1 = x->c1 > y->c1 ? x->c1 : y->c1;
    } else {
        out->r0 = x->r0 > y->r0 ? x->r0 : y->r0;
        out->c0 = x->c0 > y->c0 ? x->c0 : y->c0;
        out->r1 = x->r1 < y->r1 ? x->r1 : y->r1;
        out->c1 = x->c1 < y->c1 ? x->c1 : y->c1;
        if (out->r0 > out->r1 || out->c0 > out->c1) {
            cv_err(r, OWF_ERROR_NULL);
            return;
        }
    }
    r->type = CV_REF;
    r->u.ref.a = out;
    r->u.ref.n = 1;
}

/* An operator over plain values or, element by element, over arrays. */
static void operate(calc_ctx *ctx, int op, int actx, calc_value *a, calc_value *b, calc_value *r)
{
    unsigned rows = 1, cols = 1, i, j;
    calc_array *arr;
    calc_operand(ctx, a, actx);
    if (b)
        calc_operand(ctx, b, actx);
    if (a->type != CV_ARRAY && (!b || b->type != CV_ARRAY)) {
        if (b)
            scalar_binary(ctx, op, a, b, r);
        else
            scalar_unary(ctx, op, a, r);
        return;
    }
    calc_dims(a, &rows, &cols);
    if (b)
        calc_dims(b, &rows, &cols);
    arr = calc_array_new(ctx, rows, cols);
    if (!arr) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++) {
            calc_value x, y;
            calc_element(a, i, j, &x);
            if (b) {
                calc_element(b, i, j, &y);
                scalar_binary(ctx, op, &x, &y, &arr->v[i * cols + j]);
            } else {
                scalar_unary(ctx, op, &x, &arr->v[i * cols + j]);
            }
        }
    r->type = CV_ARRAY;
    r->u.arr = arr;
}

/* ---- Calling a function ---- */

static void plain(calc_ctx *ctx, calc_value *v)
{
    calc_value s;
    if (v->type == CV_REF || v->type == CV_ARRAY) {
        calc_scalar(ctx, v, &s);
        *v = s;
    }
    if (v->type == CV_MISSING)
        v->type = CV_EMPTY;
}

static void call(calc_ctx *ctx, int id, int actx, calc_value *args, int argc, calc_value *r)
{
    const calc_function *f = calc_function_at(id);
    unsigned rows = 1, cols = 1, i, j;
    int k, lift = 0;
    calc_value *one;
    calc_array *arr;

    for (k = 0; k < argc; k++) {
        if (calc_function_arg_class(f, k) != 'V')
            continue;
        calc_operand(ctx, &args[k], actx);
        if (args[k].type == CV_ARRAY) {
            lift = 1;
            calc_dims(&args[k], &rows, &cols);
        }
    }
    ctx->actx = actx;
    if (!lift) {
        f->fn(ctx, args, argc, r);
        return;
    }
    /* a value argument is an array: the function for each element */
    arr = calc_array_new(ctx, rows, cols);
    one = ctx_alloc(ctx, (size_t)(argc ? argc : 1) * sizeof *one);
    if (!arr || !one) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++) {
            calc_value *out = &arr->v[i * cols + j];
            for (k = 0; k < argc; k++) {
                if (calc_function_arg_class(f, k) == 'V')
                    calc_element(&args[k], i, j, &one[k]);
                else
                    one[k] = args[k];
            }
            ctx->actx = actx;
            f->fn(ctx, one, argc, out);
            plain(ctx, out);
        }
    r->type = CV_ARRAY;
    r->u.arr = arr;
}

/* ---- The machine ---- */

static unsigned get_u16(const unsigned char *p)
{
    unsigned short v;
    memcpy(&v, p, 2);
    return v;
}

static unsigned long get_u32(const unsigned char *p)
{
    unsigned int v;
    memcpy(&v, p, 4);
    return v;
}

void calc_run(calc_ctx *ctx, const calc_prog *prog, calc_value *result)
{
    const unsigned char *p = prog->code, *end = p + prog->len;
    calc_value *stack = ctx_alloc(ctx, (size_t)(prog->stack ? prog->stack : 1) * sizeof *stack), v;
    unsigned sp = 0;

    if (!stack) {
        cv_err(result, OWF_ERROR_NUM);
        return;
    }
    while (p < end) {
        int op = OP_CODE(*p), actx = (*p & OP_ARRAYCTX) != 0;
        calc_area *a;
        p++;
        switch (op) {
        case OP_NUM:
            memcpy(&stack[sp].u.num, p, sizeof(double));
            stack[sp++].type = CV_NUM;
            p += sizeof(double);
            break;
        case OP_STR:
            cv_str(&stack[sp++], (const char *)p + 2, get_u16(p));
            p += 2 + get_u16(p);
            break;
        case OP_BOOL:
            cv_bool(&stack[sp++], *p++);
            break;
        case OP_ERR:
            cv_err(&stack[sp++], *p++);
            break;
        case OP_MISSING:
            stack[sp++].type = CV_MISSING;
            break;
        case OP_EXTERN:
            cv_err(&stack[sp++], OWF_ERROR_REF);
            break;
        case OP_REF:
        case OP_AREA:
            a = ctx_alloc(ctx, sizeof *a);
            if (!a) {
                cv_err(&stack[sp++], OWF_ERROR_NUM);
                p += op == OP_REF ? 8 : 16;
                break;
            }
            if (op == OP_REF) {
                a->s0 = a->s1 = (int)get_u16(p);
                a->r0 = a->r1 = (unsigned)get_u32(p + 2);
                a->c0 = a->c1 = get_u16(p + 6);
                p += 8;
            } else {
                a->s0 = (int)get_u16(p);
                a->s1 = (int)get_u16(p + 2);
                a->r0 = (unsigned)get_u32(p + 4);
                a->c0 = get_u16(p + 8);
                a->r1 = (unsigned)get_u32(p + 10);
                a->c1 = get_u16(p + 14);
                p += 16;
            }
            stack[sp].type = CV_REF;
            stack[sp].u.ref.a = a;
            stack[sp++].u.ref.n = 1;
            break;
        case OP_ARRAY: {
            unsigned rows = get_u16(p), cols = get_u16(p + 2), n = rows * cols, k;
            calc_array *arr = calc_array_new(ctx, rows, cols);
            p += 4;
            sp -= n;
            if (!arr) {
                cv_err(&stack[sp++], OWF_ERROR_NUM);
                break;
            }
            for (k = 0; k < n; k++)
                arr->v[k] = stack[sp + k];
            stack[sp].type = CV_ARRAY;
            stack[sp++].u.arr = arr;
            break;
        }
        case OP_FUNC: {
            int id = (int)get_u16(p), argc = p[2];
            p += 3;
            sp -= (unsigned)argc;
            call(ctx, id, actx, stack + sp, argc, &v);
            stack[sp++] = v;
            break;
        }
        case OP_UNKNOWN:
            sp -= *p++;
            cv_err(&stack[sp++], OWF_ERROR_NAME);
            break;
        case OP_RANGE:
        case OP_UNION:
        case OP_ISECT:
            sp--;
            ref_op(ctx, op, &stack[sp - 1], &stack[sp], &v);
            stack[sp - 1] = v;
            break;
        case OP_NEG:
        case OP_PLUS:
        case OP_PCT:
            operate(ctx, op, actx, &stack[sp - 1], NULL, &v);
            stack[sp - 1] = v;
            break;
        default:
            sp--;
            operate(ctx, op, actx, &stack[sp - 1], &stack[sp], &v);
            stack[sp - 1] = v;
            break;
        }
    }
    v = stack[0];
    if (ctx->array && (v.type == CV_REF || v.type == CV_ARRAY)) {
        /* an array formula's cell: its element of the result */
        calc_array *arr;
        int e = calc_to_array(ctx, &v, &arr);
        if (e)
            cv_err(result, e);
        else {
            calc_value whole;
            whole.type = CV_ARRAY;
            whole.u.arr = arr;
            calc_element(&whole, ctx->adr, ctx->adc, result);
        }
    } else {
        calc_scalar(ctx, &v, result);
    }
    if (ctx->too_big || ctx->failed)
        cv_err(result, OWF_ERROR_NUM);
}
