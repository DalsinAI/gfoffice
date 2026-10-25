/*
 * libowf: the formula engine's text functions beyond the first ones
 * (owf_calc_fn.c), and its logic and information functions: TEXTJOIN,
 * CONCAT, UNICHAR, UNICODE, FIXED, DOLLAR, NUMBERVALUE; IFS, SWITCH;
 * ISFORMULA, ISREF, ISEVEN, ISODD, TYPE, ERROR.TYPE, CELL, SHEET, SHEETS.
 * Excel's names, argument counts and errors (owf_calc_fn.h).
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_calc_fn.h"

/* ---- Joining text ---- */

/* The text items of an argument, in order (a range by rows: its empty
 * cells are empty items). fn gets each; it returns 0 to go on or an error
 * to stop. */
typedef int (*item_fn)(void *user, const char *p, unsigned n);

static int each_item(calc_ctx *ctx, const calc_value *v, item_fn fn, void *user)
{
    calc_array *arr;
    unsigned long n, k;
    const char *p;
    unsigned len;
    int e, a;
    if (v->type == CV_REF) {
        for (a = 0; a < v->u.ref.n; a++) {
            calc_value one;
            one.type = CV_REF;
            one.u.ref.a = &v->u.ref.a[a];
            one.u.ref.n = 1;
            if ((e = calc_to_array(ctx, &one, &arr)) != 0)
                return e;
            n = (unsigned long)arr->rows * arr->cols;
            for (k = 0; k < n; k++) {
                if ((e = calc_to_text(ctx, &arr->v[k], &p, &len)) != 0 || (e = fn(user, p, len)) != 0)
                    return e;
            }
        }
        return 0;
    }
    if (v->type == CV_ARRAY) {
        n = (unsigned long)v->u.arr->rows * v->u.arr->cols;
        for (k = 0; k < n; k++)
            if ((e = calc_to_text(ctx, &v->u.arr->v[k], &p, &len)) != 0 || (e = fn(user, p, len)) != 0)
                return e;
        return 0;
    }
    if ((e = calc_to_text(ctx, v, &p, &len)) != 0)
        return e;
    return fn(user, p, len);
}

typedef struct {
    owf_buf out;
    const char **delim;          /* TEXTJOIN's delimiters, used in turn */
    unsigned *delim_len;
    unsigned long ndelim, next;
    int skip_empty, any;
    unsigned long chars;
} joiner;

static int join_item(void *user, const char *p, unsigned n)
{
    joiner *j = user;
    if (!n && j->skip_empty)
        return 0;
    if (j->any && j->ndelim) {
        unsigned long d = j->next++ % j->ndelim;
        owf_buf_put(&j->out, j->delim[d], j->delim_len[d]);
        j->chars += calc_utf8_count(j->delim[d], j->delim_len[d]);
    }
    owf_buf_put(&j->out, p, n);
    j->chars += calc_utf8_count(p, n);
    j->any = 1;
    return j->chars > CALC_MAX_TEXT ? OWF_ERROR_VALUE : 0;
}

/* A delimiter for TEXTJOIN's list. */
static int add_delim(void *user, const char *p, unsigned n)
{
    joiner *j = user;
    if (j->ndelim < 256) {
        j->delim[j->ndelim] = p;
        j->delim_len[j->ndelim++] = n;
    }
    return 0;
}

static void join(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int from, int skip_empty, joiner *j)
{
    int i, e;
    j->skip_empty = skip_empty;
    j->any = 0;
    j->chars = 0;
    j->next = 0;
    for (i = from; i < argc; i++)
        if ((e = each_item(ctx, &args[i], join_item, j)) != 0) {
            owf_buf_free(&j->out);
            cv_err(r, e);
            return;
        }
    calc_take_text(ctx, &j->out, r);
}

/* TEXTJOIN(delimiter, ignore_empty, text...): the delimiter may be a
 * range or array, its items used in turn. */
CF(textjoin)
{
    joiner j;
    int skip, e;
    memset(&j, 0, sizeof j);
    BOOL(1, 1, skip);
    j.delim = calc_arena_alloc(ctx->arena, 256 * sizeof *j.delim);
    j.delim_len = calc_arena_alloc(ctx->arena, 256 * sizeof *j.delim_len);
    if (!j.delim || !j.delim_len) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    if ((e = each_item(ctx, &args[0], add_delim, &j)) != 0) {
        cv_err(r, e);
        return;
    }
    owf_buf_init(&j.out);
    join(ctx, args, argc, r, 2, skip, &j);
}

/* CONCAT(text...): CONCATENATE that takes ranges and arrays too. */
CF(concat)
{
    joiner j;
    memset(&j, 0, sizeof j);
    owf_buf_init(&j.out);
    join(ctx, args, argc, r, 0, 1, &j);
}

/* ---- Characters ---- */

CF(unichar)
{
    double x;
    owf_buf b;
    NUM(0, 0, x);
    x = floor(x);
    if (x < 1 || x > 0x10FFFF) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (x >= 0xD800 && x <= 0xDFFF) {    /* half a surrogate pair */
        cv_err(r, OWF_ERROR_NA);
        return;
    }
    owf_buf_init(&b);
    calc_put_utf8(&b, (unsigned long)x);
    calc_take_text(ctx, &b, r);
}

CF(unicode)
{
    const char *p;
    unsigned n;
    TEXT(0, p, n);
    if (!n) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    cv_num(r, (double)owf_utf8_next((const unsigned char **)&p, (const unsigned char *)p + n));
}

/* ---- Numbers as text ---- */

/* A number rounded to `decimals` and shown through a format made for it. */
static void as_text(calc_ctx *ctx, calc_value *r, double x, int decimals, const char *before, int commas,
                    int brackets)
{
    owf_buf f, out;
    calc_value v;
    int i, sect;
    owf_buf_init(&f);
    for (sect = 0; sect < (brackets ? 2 : 1); sect++) {
        if (sect)
            owf_buf_puts(&f, ";(");
        owf_buf_puts(&f, before);
        owf_buf_puts(&f, commas ? "#,##0" : "0");
        if (decimals > 0) {
            owf_buf_putc(&f, '.');
            for (i = 0; i < decimals; i++)
                owf_buf_putc(&f, '0');
        }
        if (sect)
            owf_buf_putc(&f, ')');
    }
    owf_buf_putc(&f, 0);
    if (f.failed) {
        owf_buf_free(&f);
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    cv_num(&v, calc_round_to(x, decimals, 0));
    if (v.type == CV_ERR) {
        owf_buf_free(&f);
        *r = v;
        return;
    }
    owf_buf_init(&out);
    calc_format((const char *)f.data, &v, ctx->book->date1904, 0, &out, NULL);
    owf_buf_free(&f);
    calc_take_text(ctx, &out, r);
}

/* FIXED(number, [decimals = 2], [no_commas]): rounded (decimals below 0
 * round to tens, hundreds...), with thousands separators unless told
 * not to; over 127 decimals is #VALUE!. */
CF(fixed)
{
    double x, d;
    int nocommas;
    NUM(0, 0, x);
    NUM(1, 2, d);
    BOOL(2, 0, nocommas);
    d = d < 0 ? -floor(-d) : floor(d);
    if (d > 127) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (d < -308)
        d = -308;
    as_text(ctx, r, x, (int)d, "", !nocommas, 0);
}

/* DOLLAR(number, [decimals = 2]): as Excel's documentation shows it (in
 * the United States), "$1,234.57" and "($1,234.57)". */
CF(dollar)
{
    double x, d;
    NUM(0, 0, x);
    NUM(1, 2, d);
    d = d < 0 ? -floor(-d) : floor(d);
    if (d > 127) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    if (d < -308)
        d = -308;
    as_text(ctx, r, x, (int)d, "$", 1, 1);
}

/* The first character of a separator argument (default def); "" is
 * #VALUE!. */
static int separator(calc_ctx *ctx, calc_value *args, int argc, int i, const char *def, const char **p, unsigned *n)
{
    int e;
    if (i >= argc) {
        *p = def;
        *n = 1;
        return 0;
    }
    if ((e = calc_to_text(ctx, &args[i], p, n)) != 0)
        return e;
    if (!*n)
        return OWF_ERROR_VALUE;
    *n = calc_utf8_offset(*p, *n, 1);
    return 0;
}

/* NUMBERVALUE(text, [decimal_separator = "."], [group_separator = ","]):
 * spaces anywhere are passed over; group separators before the decimal
 * one go; per cent signs at the end divide by 100 each. */
CF(numbervalue)
{
    const char *p, *dp, *gp;
    unsigned n, dn, gn, i;
    char *s;
    size_t len = 0, at = 0;
    int seen_dec = 0, pct = 0, k, digits = 0;
    double x;
    TEXT(0, p, n);
    {
        int e = separator(ctx, args, argc, 1, ".", &dp, &dn);
        if (!e)
            e = separator(ctx, args, argc, 2, ",", &gp, &gn);
        if (e) {
            cv_err(r, e);
            return;
        }
    }
    if (dn == gn && !memcmp(dp, gp, dn)) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    s = calc_arena_alloc(ctx->arena, (size_t)n + 1);
    if (!s) {
        cv_err(r, OWF_ERROR_NUM);
        return;
    }
    for (i = 0; i < n;) {
        if (p[i] == ' ' || p[i] == '\t' || p[i] == '\n' || p[i] == '\r') {
            i++;
        } else if (i + dn <= n && !memcmp(p + i, dp, dn)) {
            if (seen_dec) {
                cv_err(r, OWF_ERROR_VALUE);
                return;
            }
            seen_dec = 1;
            s[len++] = '.';
            i += dn;
        } else if (i + gn <= n && !memcmp(p + i, gp, gn)) {
            if (seen_dec) {
                cv_err(r, OWF_ERROR_VALUE);
                return;
            }
            i += gn;
        } else {
            s[len++] = p[i++];
        }
    }
    while (len && s[len - 1] == '%') {
        pct++;
        len--;
    }
    if (!len && !pct) {
        cv_num(r, 0);
        return;
    }
    /* what is left: [sign] digits [. digits] [E [sign] digits] */
    if (at < len && (s[at] == '+' || s[at] == '-'))
        at++;
    for (; at < len && s[at] >= '0' && s[at] <= '9'; at++)
        digits++;
    if (at < len && s[at] == '.')
        for (at++; at < len && s[at] >= '0' && s[at] <= '9'; at++)
            digits++;
    if (digits && at < len && (s[at] == 'E' || s[at] == 'e')) {
        size_t e0 = ++at;
        if (at < len && (s[at] == '+' || s[at] == '-'))
            at++;
        while (at < len && s[at] >= '0' && s[at] <= '9')
            at++;
        if (at == e0 || s[at - 1] < '0' || s[at - 1] > '9')
            digits = 0;
    }
    if (!digits || at != len || !calc_parse_number(s, len, ctx->dmy, ctx->book->date1904, &x, &k, &k, NULL, 0)) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    while (pct--)
        x /= 100;
    cv_num(r, x);
}

/* ---- Choosing ---- */

/* IFS(condition, value, ...): the value of the first condition that is
 * TRUE; none is #N/A. */
CF(ifs)
{
    int i, b, e;
    for (i = 0; i + 1 < argc; i += 2) {
        if ((e = calc_to_bool(ctx, &args[i], &b)) != 0) {
            cv_err(r, e);
            return;
        }
        if (b) {
            *r = args[i + 1];
            if (r->type == CV_MISSING)
                cv_num(r, 0);
            return;
        }
    }
    cv_err(r, OWF_ERROR_NA);
}

/* SWITCH(expression, value, result, ..., [default]): the result of the
 * first value equal to the expression (as = compares them), else the
 * default, else #N/A. */
CF(switch)
{
    calc_value x = args[0];
    int i, e;
    if (x.type == CV_ERR) {
        *r = x;
        return;
    }
    for (i = 1; i + 1 < argc; i += 2) {
        calc_value v = args[i];
        calc_scalar(ctx, &v, &v);
        if (calc_compare(&x, &v, &e) == 0 && !e) {
            *r = args[i + 1];
            if (r->type == CV_MISSING)
                cv_num(r, 0);
            return;
        }
        if (e && v.type == CV_ERR) {
            *r = v;
            return;
        }
    }
    if (i < argc) {
        *r = args[i];
        if (r->type == CV_MISSING)
            cv_num(r, 0);
        return;
    }
    cv_err(r, OWF_ERROR_NA);
}

/* ---- Information ---- */

/* The cell at a reference's corner (NULL for none); *sheet gets its sheet. */
static const owf_cell *corner(calc_ctx *ctx, const calc_value *v, int *sheet, unsigned *row, unsigned *col)
{
    const calc_area *a = v->u.ref.a;
    *sheet = a->s0;
    *row = a->r0;
    *col = a->c0;
    if (a->s0 < 0 || a->s0 >= ctx->book->nsheets)
        return NULL;
    return owf_sheet_find(&ctx->book->sheets[a->s0], a->r0, a->c0);
}

#define NEED_REF(i)                                                                                          \
    do {                                                                                                     \
        PASS_ERROR(i);                                                                                       \
        if (args[i].type != CV_REF) {                                                                        \
            cv_err(r, OWF_ERROR_VALUE);                                                                      \
            return;                                                                                          \
        }                                                                                                    \
    } while (0)

CF(isformula)
{
    int sheet;
    unsigned row, col;
    const owf_cell *c;
    (void)argc;
    NEED_REF(0);
    c = corner(ctx, &args[0], &sheet, &row, &col);
    cv_bool(r, c && c->formula);
}

CF(isref)
{
    (void)ctx;
    (void)argc;
    cv_bool(r, args[0].type == CV_REF);
}

/* ISEVEN, ISODD(number): truncated; a boolean is #VALUE!. */
static void even_odd(calc_ctx *ctx, calc_value *args, int argc, calc_value *r, int odd)
{
    double x;
    if (args[0].type == CV_BOOL) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    NUM(0, 0, x);
    x = floor(fabs(x));
    cv_bool(r, (fmod(x, 2) != 0) == odd);
}

CF(iseven)
{
    even_odd(ctx, args, argc, r, 0);
}

CF(isodd)
{
    even_odd(ctx, args, argc, r, 1);
}

/* TYPE(value): 1 number (and empty), 2 text, 4 boolean, 16 error, 64
 * array (or a range of more than one cell). */
CF(type)
{
    calc_value v = args[0];
    (void)argc;
    if (v.type == CV_REF) {
        const calc_area *a = v.u.ref.a;
        if (v.u.ref.n > 1 || a->s0 != a->s1 || a->r0 != a->r1 || a->c0 != a->c1) {
            cv_num(r, 64);
            return;
        }
        calc_scalar(ctx, &args[0], &v);
    }
    switch (v.type) {
    case CV_STR: cv_num(r, 2); break;
    case CV_BOOL: cv_num(r, 4); break;
    case CV_ERR: cv_num(r, 16); break;
    case CV_ARRAY: cv_num(r, 64); break;
    default: cv_num(r, 1); break;
    }
}

/* ERROR.TYPE(value): 1 #NULL!, 2 #DIV/0!, 3 #VALUE!, 4 #REF!, 5 #NAME?,
 * 6 #NUM!, 7 #N/A; not an error, #N/A. */
CF(error_type)
{
    static const int codes[] = { OWF_ERROR_NULL, OWF_ERROR_DIV0, OWF_ERROR_VALUE, OWF_ERROR_REF,
                                 OWF_ERROR_NAME, OWF_ERROR_NUM, OWF_ERROR_NA };
    int i;
    (void)ctx;
    (void)argc;
    if (args[0].type == CV_ERR)
        for (i = 0; i < 7; i++)
            if (codes[i] == args[0].u.err) {
                cv_num(r, i + 1);
                return;
            }
    cv_err(r, OWF_ERROR_NA);
}

/* CELL's "format" code for a number format: G, F2, ,2, C2, P2, S2, D1 to
 * D9, with "-" for negatives in colour and "()" for brackets. */
static void format_code(const char *f, char *out)
{
    const char *p;
    int date = 0, mmm = 0, d = 0, y = 0, h = 0, s = 0, ampm = 0, pct = 0, sci = 0, cur = 0, comma = 0;
    int dec = -1, digits_after = 0, section = 0, colour2 = 0, bracket = 0, frac = 0;
    if (!f || !*f || owf_same_word(f, "General")) {
        strcpy(out, "G");
        return;
    }
    for (p = f; *p; p++) {
        char c = *p;
        if (c == '"') {                  /* a literal */
            for (p++; *p && *p != '"'; p++)
                ;
            if (!*p)
                break;
            continue;
        }
        if (c == '\\' || c == '_' || c == '*') {
            if (p[1])
                p++;
            continue;
        }
        if (c == '[') {
            const char *e = strchr(p, ']');
            if (!e)
                break;
            if (p[1] == '$')
                cur |= !section;
            else if (section == 1 && !(p[1] == '<' || p[1] == '>' || p[1] == '=' || p[1] == 'h' || p[1] == 'H' ||
                                        p[1] == 'm' || p[1] == 'M' || p[1] == 's' || p[1] == 'S'))
                colour2 = 1;
            p = e;
            continue;
        }
        if (c == ';') {
            section++;
            continue;
        }
        if (section)
            continue;
        switch (c) {
        case 'd': case 'D': d = date = 1; break;
        case 'y': case 'Y': y = date = 1; break;
        case 'm': case 'M':
            if (!strncmp(p, "mmm", 3) || !strncmp(p, "MMM", 3))
                mmm = 1;
            date = 1;
            while (p[1] == c)
                p++;
            break;
        case 'h': case 'H': h = 1; break;
        case 's': case 'S': s = 1; break;
        case 'A': case 'a':
            if (p[1] == 'M' || p[1] == 'm' || p[1] == '/')
                ampm = 1;
            break;
        case '%': pct = 1; break;
        case 'E': case 'e': sci = 1; break;
        case '$': cur = 1; break;
        case ',': comma = 1; break;
        case '(': bracket = 1; break;
        case '/': frac = 1; break;
        case '.': dec = 0; break;
        case '0': case '#': case '?':
            if (dec >= 0 && !sci)
                digits_after++;
            break;
        default:
            if ((unsigned char)c == 0xC2 || (unsigned char)c == 0xE2)   /* £ ¥ €, in UTF-8 */
                cur = 1;
            break;
        }
    }
    if (h && !d && !y)
        date = 0;                        /* m after h is minutes */
    if (date && (d || y)) {
        if (mmm)
            strcpy(out, d && y ? "D1" : d ? "D2" : "D3");
        else
            strcpy(out, y ? "D4" : "D5");
    } else if (h || s) {
        strcpy(out, ampm ? (s ? "D6" : "D7") : (s ? "D8" : "D9"));
    } else if (frac) {
        strcpy(out, "G");
    } else {
        int k = 0;
        if (digits_after > 15)
            digits_after = 15;
        out[k++] = pct ? 'P' : sci ? 'S' : cur ? 'C' : comma ? ',' : 'F';
        if (digits_after >= 10)
            out[k++] = (char)('0' + digits_after / 10);
        out[k++] = (char)('0' + digits_after % 10);
        out[k] = 0;
    }
    if (colour2)
        strcat(out, "-");
    if (bracket)
        strcat(out, "()");
}

/* A column's width in characters, as CELL("width") gives it: Excel's
 * standard font is 7 pixels a digit with 5 of padding; 15 twips a pixel. */
static double column_chars(const owf_sheet *s, unsigned col)
{
    int i, twips = s->default_width > 0 ? s->default_width : 960;
    double px, ch;
    for (i = 0; i < s->ncols; i++)
        if (col >= s->cols[i].first && col <= s->cols[i].last) {
            twips = s->cols[i].hidden ? 0 : s->cols[i].width;
            break;
        }
    px = twips / 15.0;
    ch = (px - 5) / 7;
    return ch < 0 ? 0 : floor(ch);
}

/* CELL(info_type, [reference]): "address", "col", "row", "contents",
 * "type", "format", "prefix", "width", "protect", "parentheses", "color",
 * "filename" of the reference's corner cell (without one, the formula's
 * own). The book's file name is not known here: "filename" is "" (as Excel
 * gives for a book not saved), and an address on another sheet has the
 * sheet but no book. */
CF(cell)
{
    const char *p;
    unsigned n, row, col;
    int sheet;
    const owf_cell *c;
    char code[16], what[16];
    owf_buf b;
    size_t i;
    TEXT(0, p, n);
    if (n >= sizeof what) {
        cv_err(r, OWF_ERROR_VALUE);
        return;
    }
    for (i = 0; i < n; i++)
        what[i] = (char)owf_lower((unsigned char)p[i]);
    what[n] = 0;
    if (calc_given(argc, args, 1)) {
        NEED_REF(1);
        c = corner(ctx, &args[1], &sheet, &row, &col);
    } else {
        sheet = ctx->sheet;
        row = ctx->row;
        col = ctx->col;
        c = sheet >= 0 && sheet < ctx->book->nsheets ? owf_sheet_find(&ctx->book->sheets[sheet], row, col) : NULL;
    }
    if (!strcmp(what, "address")) {
        char cellname[20];
        owf_buf_init(&b);
        if (sheet != ctx->sheet && sheet >= 0 && sheet < ctx->book->nsheets)
            calc_put_sheet_prefix(&b, ctx->book->sheets[sheet].name, NULL);
        owf_calc_cell_name(row, col, cellname + 1);
        cellname[0] = '$';
        for (i = 1; cellname[i] >= 'A' && cellname[i] <= 'Z'; i++)
            ;
        owf_buf_put(&b, cellname, i);
        owf_buf_putc(&b, '$');
        owf_buf_puts(&b, cellname + i);
        calc_take_text(ctx, &b, r);
    } else if (!strcmp(what, "col")) {
        cv_num(r, (double)col + 1);
    } else if (!strcmp(what, "row")) {
        cv_num(r, (double)row + 1);
    } else if (!strcmp(what, "contents")) {
        calc_value_of_cell(c, r);
        if (r->type == CV_EMPTY)
            cv_num(r, 0);
    } else if (!strcmp(what, "type")) {
        cv_str(r, !c || c->type == OWF_VALUE_EMPTY ? "b" : c->type == OWF_VALUE_TEXT && !c->formula ? "l" : "v", 1);
    } else if (!strcmp(what, "format") || !strcmp(what, "parentheses") || !strcmp(what, "color")) {
        const char *f = NULL;
        if (c && c->style > 0 && c->style < ctx->book->nstyles) {
            int k = ctx->book->styles[c->style].number_format;
            if (k > 0 && k < ctx->book->nnumfmts)
                f = ctx->book->numfmts[k];
        }
        format_code(f, code);
        if (what[0] == 'f')
            cv_str_copy(ctx, r, code, strlen(code));
        else if (what[0] == 'p')
            cv_num(r, strstr(code, "()") != NULL);
        else
            cv_num(r, strchr(code, '-') != NULL);
    } else if (!strcmp(what, "prefix")) {
        const char *pre = "";
        if (c && c->type == OWF_VALUE_TEXT && c->style >= 0 && c->style < ctx->book->nstyles)
            switch (ctx->book->styles[c->style].halign) {
            case OWF_HALIGN_LEFT: pre = "'"; break;
            case OWF_HALIGN_RIGHT: pre = "\""; break;
            case OWF_HALIGN_CENTRE: pre = "^"; break;
            case OWF_HALIGN_FILL: pre = "\\"; break;
            }
        cv_str(r, pre, (unsigned)strlen(pre));
    } else if (!strcmp(what, "width")) {
        cv_num(r, sheet >= 0 && sheet < ctx->book->nsheets ? column_chars(&ctx->book->sheets[sheet], col) : 0);
    } else if (!strcmp(what, "protect")) {
        cv_num(r, 1);                    /* cells are locked unless a style says not; the book keeps no such flag */
    } else if (!strcmp(what, "filename")) {
        cv_str(r, "", 0);
    } else {
        cv_err(r, OWF_ERROR_VALUE);
    }
}

/* SHEET([value]): the sheet's number (from 1) of a reference, of a
 * sheet's name, or of the formula's own sheet. */
CF(sheet)
{
    if (!calc_given(argc, args, 0)) {
        cv_num(r, (double)ctx->sheet + 1);
        return;
    }
    PASS_ERROR(0);
    if (args[0].type == CV_REF) {
        cv_num(r, (double)args[0].u.ref.a->s0 + 1);
        return;
    }
    if (args[0].type == CV_STR) {
        char name[CALC_SHEET_NAME];
        int k;
        if (args[0].u.str.len < sizeof name) {
            memcpy(name, args[0].u.str.p, args[0].u.str.len);
            name[args[0].u.str.len] = 0;
            if ((k = calc_find_sheet(ctx->book, name)) >= 0) {
                cv_num(r, (double)k + 1);
                return;
            }
        }
    }
    cv_err(r, OWF_ERROR_NA);
}

/* SHEETS([reference]): how many sheets a (3-D) reference spans, or the
 * book has. */
CF(sheets)
{
    int k, n = 0;
    if (!calc_given(argc, args, 0)) {
        cv_num(r, ctx->book->nsheets);
        return;
    }
    PASS_ERROR(0);
    if (args[0].type != CV_REF) {
        cv_err(r, OWF_ERROR_REF);
        return;
    }
    for (k = 0; k < args[0].u.ref.n; k++) {
        int m = args[0].u.ref.a[k].s1 - args[0].u.ref.a[k].s0 + 1;
        if (m > n)
            n = m;
    }
    cv_num(r, n);
}
