/*
 * OpenCalc's core: data validation applied. A value typed into a cell is
 * checked against the cell's rule as Excel checks it (a whole number, a
 * decimal, a date or a time compared with the rule's bounds, a text's
 * length, one of a list, a formula that must be true), when the rule has
 * an error alert; a list's choices, for its dropdown.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "oc_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHOICES 1000

/* A rule's formula k worked out at a cell (moved from the rule's first cell, as Excel keeps it). 0 when it
 * can't be (a text result is the caller's to free). */
static int bound(oc_core *core, int sheet, const owf_validation *dv, int k, unsigned row, unsigned col, owf_cell *out)
{
    char *moved;
    int ok;
    memset(out, 0, sizeof *out);
    if (!dv->formula[k] || !dv->nranges)
        return 0;
    moved = owf_calc_move_formula(dv->formula[k], (int)((long)row - (long)dv->ranges[0].first_row),
                                  (int)((long)col - (long)dv->ranges[0].first_col));
    if (!moved)
        return 0;
    ok = owf_calc_evaluate(core->calc, sheet, row, col, moved, out) == OWF_OK;
    owf_free(moved);
    if (ok && out->type == OWF_VALUE_ERROR)
        ok = 0;
    return ok;
}

/* A value compared with the rule's bound(s) by its operator. */
static int compares(int op, double v, double a, double b)
{
    double lo = a < b ? a : b, hi = a < b ? b : a;
    switch (op) {
    case OWF_CF_BETWEEN: return v >= lo && v <= hi;
    case OWF_CF_NOT_BETWEEN: return v < lo || v > hi;
    case OWF_CF_EQUAL: return v == a;
    case OWF_CF_NOT_EQUAL: return v != a;
    case OWF_CF_GREATER: return v > a;
    case OWF_CF_LESS: return v < a;
    case OWF_CF_GREATER_EQUAL: return v >= a;
    case OWF_CF_LESS_EQUAL: return v <= a;
    }
    return 1;
}

/* A bound as a number: a number, or a text that reads as one; 0 when it is neither. */
static int number_of(const owf_cell *c, double *d)
{
    if (c->type == OWF_VALUE_NUMBER || c->type == OWF_VALUE_BOOL) {
        *d = c->number;
        return 1;
    }
    if (c->type == OWF_VALUE_EMPTY) {
        *d = 0;
        return 1;
    }
    return 0;
}

/* A value as the grid shows it, on one line (for lengths and lists). */
static void shown(const oc_core *core, const owf_cell *c, char *out, size_t size)
{
    const owf_cellstyle *st = oc_cell_style(core, c, c->row, c->col);
    const char *fmt = st && st->number_format > 0 && st->number_format < core->book->nnumfmts
                          ? core->book->numfmts[st->number_format] : NULL;
    unsigned long colour;
    if (c->type == OWF_VALUE_TEXT) {
        size_t n = c->text ? strlen(c->text) : 0;
        if (n >= size)
            n = size - 1;
        memcpy(out, c->text ? c->text : "", n);
        out[n] = 0;
        return;
    }
    owf_calc_format_value(fmt, c, core->book->date1904, 0, out, size, &colour);
    out[strcspn(out, "\r\n")] = 0;
}

static int same_text(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}

static size_t characters(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

void oc_choices_free(char **items, int n)
{
    int i;
    for (i = 0; items && i < n; i++)
        owf_free(items[i]);
    free(items);
}

static int add_choice(char ***items, int *n, const char *s, size_t len)
{
    char **p, *t;
    if (*n >= MAX_CHOICES)
        return 1;
    while (len && (*s == ' '))                    /* "Yes, No": "No" */
        s++, len--;
    while (len && s[len - 1] == ' ')
        len--;
    if (!(t = owf_malloc(len + 1)) || !(p = realloc(*items, (size_t)(*n + 1) * sizeof *p))) {
        owf_free(t);
        return 0;
    }
    memcpy(t, s, len);
    t[len] = 0;
    *items = p;
    p[(*n)++] = t;
    return 1;
}

int oc_list_choices(oc_core *core, int sheet, const owf_validation *dv, char ***items, int *n)
{
    const char *f = dv && dv->type == OWF_DV_LIST ? dv->formula[0] : NULL;
    const owf_book *b = core->book;
    owf_range r;
    int at, i;
    *items = NULL;
    *n = 0;
    if (!f)
        return OWF_OK;
    if (*f == '"') {                              /* "Yes,No": the words between the commas */
        const char *p = f + 1, *end = strrchr(p, '"');
        if (!end || end < p)
            end = p + strlen(p);
        while (p <= end) {
            const char *comma = memchr(p, ',', (size_t)(end - p));
            const char *stop = comma ? comma : end;
            if (!add_choice(items, n, p, (size_t)(stop - p)))
                goto memory;
            if (!comma)
                break;
            p = comma + 1;
        }
        return OWF_OK;
    }
    if (*f == '=')
        f++;
    /* a name: what it stands for (the sheet's own first, then the book's) */
    for (i = 0; i < b->nnames; i++)
        if (same_text(b->names[i].name, f) && (b->names[i].sheet == sheet || b->names[i].sheet < 0)) {
            f = b->names[i].formula;
            if (b->names[i].sheet == sheet)
                break;
        }
    if (!oc_sheet_range(b, f, sheet, &at, &r))
        return OWF_OK;                            /* a formula (INDIRECT, OFFSET...): no choices known */
    {
        unsigned row, col;
        char buf[256];
        long cells = (long)(r.last_row - r.first_row + 1) * (long)(r.last_col - r.first_col + 1);
        if (cells > MAX_CHOICES)
            r.last_row = r.first_row + (r.first_col == r.last_col ? MAX_CHOICES - 1 : 0);
        for (row = r.first_row; row <= r.last_row; row++)
            for (col = r.first_col; col <= r.last_col; col++) {
                const owf_cell *c = oc_find(core, at, row, col);
                buf[0] = 0;
                if (c && !oc_cell_empty(c))
                    shown(core, c, buf, sizeof buf);
                if (!add_choice(items, n, buf, strlen(buf)))
                    goto memory;
            }
        /* blank cells at its end are not choices */
        while (*n && !(*items)[*n - 1][0]) {
            owf_free((*items)[--*n]);
        }
    }
    return OWF_OK;
memory:
    oc_choices_free(*items, *n);
    *items = NULL;
    *n = 0;
    return OWF_ERR_MEMORY;
}

int oc_valid_value(oc_core *core, int sheet, unsigned row, unsigned col, const owf_validation *dv)
{
    const owf_cell *c = oc_find(core, sheet, row, col);
    owf_cell lo, hi;
    double v, a = 0, b = 0;
    int ok = 1, have;
    char buf[256];

    if (!dv || dv->type == OWF_DV_ANY)
        return 1;
    if (!c || oc_cell_empty(c))
        return dv->allow_blank || dv->type == OWF_DV_CUSTOM ? 1 : 0;
    if (c->type == OWF_VALUE_ERROR)
        return 0;
    switch (dv->type) {
    case OWF_DV_WHOLE:
    case OWF_DV_DECIMAL:
    case OWF_DV_DATE:
    case OWF_DV_TIME:
    case OWF_DV_TEXT_LENGTH:
        if (dv->type == OWF_DV_TEXT_LENGTH) {
            shown(core, c, buf, sizeof buf);
            v = (double)characters(buf);
        } else {
            if (c->type != OWF_VALUE_NUMBER)
                return 0;                         /* a word where a number goes */
            v = c->number;
            if (dv->type == OWF_DV_WHOLE && v != floor(v))
                return 0;
            if (dv->type == OWF_DV_TIME)          /* a time: the part of the day */
                v -= floor(v);
        }
        have = bound(core, sheet, dv, 0, row, col, &lo);
        if (!have || !number_of(&lo, &a)) {
            owf_free(lo.text);
            return 1;                             /* a bound that can't be worked out: let it be */
        }
        owf_free(lo.text);
        if (dv->op == OWF_CF_BETWEEN || dv->op == OWF_CF_NOT_BETWEEN) {
            have = bound(core, sheet, dv, 1, row, col, &hi);
            if (!have || !number_of(&hi, &b)) {
                owf_free(hi.text);
                return 1;
            }
            owf_free(hi.text);
        }
        if (dv->type == OWF_DV_TIME) {
            a -= floor(a);
            b -= floor(b);
        }
        ok = compares(dv->op, v, a, b);
        break;
    case OWF_DV_LIST: {
        char **items;
        int n, i;
        if (oc_list_choices(core, sheet, dv, &items, &n) != OWF_OK || !n) {
            oc_choices_free(items, n);
            return 1;                             /* no choices known: let it be */
        }
        shown(core, c, buf, sizeof buf);
        for (i = 0, ok = 0; i < n && !ok; i++)
            ok = same_text(items[i], buf) || (c->type == OWF_VALUE_TEXT && c->text && same_text(items[i], c->text));
        oc_choices_free(items, n);
        break;
    }
    case OWF_DV_CUSTOM:
        if (bound(core, sheet, dv, 0, row, col, &lo))
            ok = lo.type == OWF_VALUE_BOOL || lo.type == OWF_VALUE_NUMBER ? lo.number != 0 : 1;
        owf_free(lo.text);
        break;
    }
    return ok;
}

int oc_core_invalid(const oc_core *core, oc_alert *alert)
{
    const owf_validation *dv;
    if (!core->invalid || core->invalid_sheet < 0 || core->invalid_sheet >= core->book->nsheets)
        return 0;
    dv = owf_sheet_validation(&core->book->sheets[core->invalid_sheet], core->invalid_row, core->invalid_col);
    if (!dv)
        return 0;
    alert->style = dv->error_style;
    alert->move = core->invalid_move;
    alert->title = dv->error_title && *dv->error_title ? dv->error_title : "OpenCalc";
    alert->message = dv->error && *dv->error ? dv->error
                                             : "This value doesn't match the data validation restrictions defined for this cell.";
    return 1;
}

int oc_core_list_choices(oc_core *core, char ***items, int *n)
{
    const oc_sheetstate *st = oc_state_const(core);
    const owf_validation *dv = owf_sheet_validation(oc_sheet(core), st->sel.row, st->sel.col);
    return oc_list_choices(core, core->sheet, dv, items, n);
}
