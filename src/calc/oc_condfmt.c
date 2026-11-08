/*
 * GFOpenCalc's core: conditional formatting, as the grid shows it. For the
 * cells showing, the look each sheet rule (owf_condfmt) gives them: rules
 * in order of priority, the first that holds for a cell setting each part
 * of the look (the fill, the text's colour, bold, italic, underline,
 * strikeout) and the later ones only what is still unset, a rule with
 * stop the last one for its cells when it holds. A cellIs rule compares
 * the cell's value with its formulas' values, an expression rule (and a
 * kept one with a formula, such as "contains text") holds where its
 * formula is true, each formula worked out by the engine as if in the cell
 * (moved from the rule's first cell, as relative references move); a
 * colour scale colours each number by where it falls between its points.
 * The looks are kept until the book, the sheet or the cells showing
 * change, so that moving the cursor doesn't work them out again.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct { owf_range *v; int n, cap; } cf_ranges;

static int cf_range_add(cf_ranges *a, owf_range r)
{
    owf_range *p;
    if (a->n == a->cap) {
        int cap = a->cap ? a->cap * 2 : 4;
        if (!(p = realloc(a->v, (size_t)cap * sizeof *p))) return 0;
        a->v = p; a->cap = cap;
    }
    a->v[a->n++] = r;
    return 1;
}

static int cf_touches(const owf_range *a, const owf_range *b)
{
    return a->first_row <= b->last_row && a->last_row >= b->first_row &&
           a->first_col <= b->last_col && a->last_col >= b->first_col;
}

static int cf_subtract(cf_ranges *out, const owf_range *source, const owf_range *cut)
{
    owf_range x, p;
    if (!cf_touches(source, cut)) return cf_range_add(out, *source);
    x.first_row = source->first_row > cut->first_row ? source->first_row : cut->first_row;
    x.last_row = source->last_row < cut->last_row ? source->last_row : cut->last_row;
    x.first_col = source->first_col > cut->first_col ? source->first_col : cut->first_col;
    x.last_col = source->last_col < cut->last_col ? source->last_col : cut->last_col;
    if (source->first_row < x.first_row) { p = *source; p.last_row = x.first_row - 1; if (!cf_range_add(out, p)) return 0; }
    if (x.last_row < source->last_row) { p = *source; p.first_row = x.last_row + 1; if (!cf_range_add(out, p)) return 0; }
    if (source->first_col < x.first_col) { p = x; p.first_col = source->first_col; p.last_col = x.first_col - 1; if (!cf_range_add(out, p)) return 0; }
    if (x.last_col < source->last_col) { p = x; p.first_col = x.last_col + 1; p.last_col = source->last_col; if (!cf_range_add(out, p)) return 0; }
    return 1;
}

static char *cf_copy_text(const char *s, int *failed)
{
    char *p = NULL;
    if (s && !(p = oc_strdup(s))) *failed = 1;
    return p;
}

static int cf_copy(owf_condfmt *dst, const owf_condfmt *src, const owf_range *ranges, int nranges)
{
    int failed = 0, i;
    memset(dst, 0, sizeof *dst);
    *dst = *src;
    dst->ranges = NULL; dst->formula[0] = dst->formula[1] = dst->xml = NULL;
    for (i = 0; i < 3; ++i) dst->points[i].value = NULL;
    dst->nranges = nranges;
    if (nranges && !(dst->ranges = malloc((size_t)nranges * sizeof *dst->ranges))) failed = 1;
    if (!failed && nranges) memcpy(dst->ranges, ranges, (size_t)nranges * sizeof *ranges);
    dst->formula[0] = cf_copy_text(src->formula[0], &failed);
    dst->formula[1] = cf_copy_text(src->formula[1], &failed);
    dst->xml = cf_copy_text(src->xml, &failed);
    for (i = 0; i < 3; ++i) dst->points[i].value = cf_copy_text(src->points[i].value, &failed);
    if (failed) { owf_condfmt_free(dst); return 0; }
    return 1;
}

static int cf_add_copy(owf_condfmt **rules, int *n, int *cap, const owf_condfmt *src,
                       const owf_range *ranges, int nranges)
{
    owf_condfmt *p;
    if (!nranges) return 1;
    if (*n == *cap) {
        int size = *cap ? *cap * 2 : 4;
        if (!(p = realloc(*rules, (size_t)size * sizeof *p))) return 0;
        *rules = p; *cap = size;
    }
    if (!cf_copy(&(*rules)[*n], src, ranges, nranges)) return 0;
    (*n)++;
    return 1;
}

static void cf_rules_free(owf_condfmt *rules, int n)
{
    int i;
    for (i = 0; i < n; ++i) owf_condfmt_free(&rules[i]);
    free(rules);
}

static int cf_spec_ok(const oc_condfmt_spec *r)
{
    int i, two;
    if (!r || r->type < OWF_CF_CELL_IS || r->type > OWF_CF_COLOUR_SCALE) return 0;
    if (r->type == OWF_CF_COLOUR_SCALE) {
        if (r->npoints < 2 || r->npoints > 3) return 0;
        for (i = 0; i < r->npoints; ++i) {
            if (r->points[i].kind < OWF_CFV_MIN || r->points[i].kind > OWF_CFV_FORMULA ||
                !OWF_SHADE_SET(r->points[i].colour)) return 0;
            if (r->points[i].kind >= OWF_CFV_NUMBER && (!r->points[i].value || !*r->points[i].value)) return 0;
        }
        return 1;
    }
    if (!r->formula[0] || !*r->formula[0]) return 0;
    if (r->type == OWF_CF_EXPRESSION) return 1;
    if (r->op < OWF_CF_BETWEEN || r->op > OWF_CF_LESS_EQUAL) return 0;
    two = r->op == OWF_CF_BETWEEN || r->op == OWF_CF_NOT_BETWEEN;
    return !two || (r->formula[1] && *r->formula[1]);
}

static int cf_targets(oc_core *core, owf_range *targets)
{
    int i, n = 0;
    for (i = 0; i < oc_state(core)->sel.nranges; ++i)
        if (oc_range_clip(core, &oc_state(core)->sel.ranges[i], &targets[n])) n++;
    return n;
}

int oc_core_add_condfmt(oc_core *core, const oc_condfmt_spec *rule)
{
    owf_sheet *sheet;
    owf_condfmt made, *dst;
    owf_range targets[OC_MAX_RANGES];
    oc_undo *undo;
    int n, i, priority = 0;
    if (!core || !cf_spec_ok(rule)) return OWF_ERR_FORMAT;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!(n = cf_targets(core, targets))) return OWF_ERR_UNSUPPORTED;
    sheet = oc_sheet(core);
    memset(&made, 0, sizeof made);
    made.type = rule->type; made.op = rule->op; made.stop = !!rule->stop;
    made.formula[0] = (char *)rule->formula[0]; made.formula[1] = (char *)rule->formula[1];
    made.font_set = rule->font_set; made.font_flags = rule->font_flags;
    made.font_colour = rule->font_colour; made.fill = rule->fill; made.npoints = rule->npoints;
    for (i = 0; i < rule->npoints; ++i) {
        made.points[i].kind = rule->points[i].kind;
        made.points[i].value = (char *)rule->points[i].value;
        made.points[i].colour = rule->points[i].colour;
    }
    for (i = 0; i < sheet->ncondfmts; ++i) if (sheet->condfmts[i].priority > priority) priority = sheet->condfmts[i].priority;
    made.priority = priority + 1;
    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) { oc_undo_free(undo); undo = NULL; }
    if (!(dst = owf_sheet_add_condfmt(sheet)) || !cf_copy(dst, &made, targets, n)) {
        if (dst) sheet->ncondfmts--;
        oc_undo_free(undo);
        return OWF_ERR_MEMORY;
    }
    oc_undo_push(core, undo); oc_changed(core); oc_cf_forget(core);
    return OWF_OK;
}

int oc_core_clear_condfmt(oc_core *core)
{
    owf_sheet *sheet;
    owf_condfmt *fresh = NULL;
    owf_range targets[OC_MAX_RANGES];
    int ntargets, nfresh = 0, cap = 0, i, r, t, rc = OWF_OK;
    oc_undo *undo;
    if (!core) return OWF_ERR_FORMAT;
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!(ntargets = cf_targets(core, targets))) return OWF_ERR_UNSUPPORTED;
    sheet = oc_sheet(core);
    for (i = 0; i < sheet->ncondfmts; ++i) {
        cf_ranges remain = { 0 };
        for (r = 0; r < sheet->condfmts[i].nranges; ++r)
            if (!cf_range_add(&remain, sheet->condfmts[i].ranges[r])) { rc = OWF_ERR_MEMORY; break; }
        for (t = 0; rc == OWF_OK && t < ntargets; ++t) {
            cf_ranges next = { 0 };
            for (r = 0; r < remain.n; ++r) if (!cf_subtract(&next, &remain.v[r], &targets[t])) { rc = OWF_ERR_MEMORY; break; }
            free(remain.v); remain = next;
        }
        if (rc == OWF_OK && !cf_add_copy(&fresh, &nfresh, &cap, &sheet->condfmts[i], remain.v, remain.n)) rc = OWF_ERR_MEMORY;
        free(remain.v);
        if (rc != OWF_OK) break;
    }
    if (rc != OWF_OK) { cf_rules_free(fresh, nfresh); return rc; }
    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) { oc_undo_free(undo); undo = NULL; }
    for (i = 0; i < sheet->ncondfmts; ++i) owf_condfmt_free(&sheet->condfmts[i]);
    free(sheet->condfmts); sheet->condfmts = fresh; sheet->ncondfmts = nfresh; sheet->capcondfmts = cap;
    oc_undo_push(core, undo); oc_changed(core); oc_cf_forget(core);
    return OWF_OK;
}

/* A formula's value at a cell: 1 and *out (its text from owf_malloc), or 0 (no value: it won't parse, or
 * memory). The moved text of the last one is kept: a rule whose formula moves the same way along a row
 * ($D7) is worked out once a row. */
typedef struct {
    oc_core *core;
    int sheet;
    char *last_text;
    owf_cell last_value;
    int last_ok;
} evaluator;

static void eval_reset(evaluator *ev)
{
    owf_free(ev->last_text);
    owf_free(ev->last_value.text);
    ev->last_text = NULL;
    memset(&ev->last_value, 0, sizeof ev->last_value);
    ev->last_ok = 0;
}

static const owf_cell *eval_at(evaluator *ev, const char *formula, long drow, long dcol, unsigned row, unsigned col)
{
    char *moved = owf_calc_move_formula(formula, (int)drow, (int)dcol);
    if (!moved)
        return NULL;
    if (ev->last_text && !strcmp(ev->last_text, moved)) {
        owf_free(moved);
        return ev->last_ok ? &ev->last_value : NULL;
    }
    eval_reset(ev);
    ev->last_text = moved;
    ev->last_ok = owf_calc_evaluate(ev->core->calc, ev->sheet, row, col, moved, &ev->last_value) == OWF_OK;
    return ev->last_ok ? &ev->last_value : NULL;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* Two values compared as Excel compares them: numbers < text < booleans, text without case; an empty cell
 * is 0 to a number and "" to text. -2 when they can't be (an error). */
static int compare(const owf_cell *a, const owf_cell *b)
{
    owf_value_type ta = a ? a->type : OWF_VALUE_EMPTY, tb = b ? b->type : OWF_VALUE_EMPTY;
    double x, y;
    if (ta == OWF_VALUE_ERROR || tb == OWF_VALUE_ERROR)
        return -2;
    if (ta == OWF_VALUE_EMPTY)
        ta = tb == OWF_VALUE_TEXT ? OWF_VALUE_TEXT : OWF_VALUE_NUMBER;
    if (tb == OWF_VALUE_EMPTY)
        tb = ta == OWF_VALUE_TEXT ? OWF_VALUE_TEXT : OWF_VALUE_NUMBER;
    if (ta != tb) {
        int ra = ta == OWF_VALUE_NUMBER ? 0 : ta == OWF_VALUE_TEXT ? 1 : 2, rb = tb == OWF_VALUE_NUMBER ? 0 : tb == OWF_VALUE_TEXT ? 1 : 2;
        return ra < rb ? -1 : 1;
    }
    if (ta == OWF_VALUE_TEXT) {
        const char *p = a && a->type == OWF_VALUE_TEXT && a->text ? a->text : "";
        const char *q = b && b->type == OWF_VALUE_TEXT && b->text ? b->text : "";
        for (; *p && lower((unsigned char)*p) == lower((unsigned char)*q); p++, q++)
            ;
        return lower((unsigned char)*p) < lower((unsigned char)*q) ? -1 : lower((unsigned char)*p) > lower((unsigned char)*q);
    }
    x = a && a->type != OWF_VALUE_EMPTY ? a->number : 0;
    y = b && b->type != OWF_VALUE_EMPTY ? b->number : 0;
    return x < y ? -1 : x > y;
}

static int truthy(const owf_cell *v)
{
    if (!v)
        return 0;
    if (v->type == OWF_VALUE_NUMBER || v->type == OWF_VALUE_BOOL)
        return v->number != 0;
    return 0;
}

/* Whether a cellIs or formula rule holds at (row, col). */
static int holds(evaluator *ev, const owf_condfmt *cf, const owf_cell *cell, unsigned row, unsigned col)
{
    long drow = (long)row - (long)cf->ranges[0].first_row, dcol = (long)col - (long)cf->ranges[0].first_col;
    const owf_cell *v;
    owf_cell first;
    int c1, c2;
    if (cf->type != OWF_CF_CELL_IS)
        return cf->formula[0] && truthy(eval_at(ev, cf->formula[0], drow, dcol, row, col));
    if (!cf->formula[0])
        return 0;
    if (!(v = eval_at(ev, cf->formula[0], drow, dcol, row, col)))
        return 0;
    if ((c1 = compare(cell, v)) == -2)
        return 0;
    switch (cf->op) {
    case OWF_CF_EQUAL: return c1 == 0;
    case OWF_CF_NOT_EQUAL: return c1 != 0;
    case OWF_CF_GREATER: return c1 > 0;
    case OWF_CF_LESS: return c1 < 0;
    case OWF_CF_GREATER_EQUAL: return c1 >= 0;
    case OWF_CF_LESS_EQUAL: return c1 <= 0;
    }
    /* between, not between: the two bounds either way round */
    if (!cf->formula[1])
        return 0;
    first = *v;
    first.text = NULL;
    if (v->type == OWF_VALUE_TEXT && v->text && !(first.text = oc_strdup(v->text)))
        return 0;
    if (!(v = eval_at(ev, cf->formula[1], drow, dcol, row, col))) {
        owf_free(first.text);
        return 0;
    }
    c2 = compare(cell, v);
    if (compare(&first, v) > 0) {                        /* the bounds the other way round */
        int t = c1;
        c1 = c2;
        c2 = t;
    }
    owf_free(first.text);
    if (c1 == -2 || c2 == -2)
        return 0;
    return cf->op == OWF_CF_NOT_BETWEEN ? (c1 < 0 || c2 > 0) : (c1 >= 0 && c2 <= 0);
}

static int in_ranges_one(const owf_condfmt *cf, int r, unsigned row, unsigned col)
{
    return row >= cf->ranges[r].first_row && row <= cf->ranges[r].last_row && col >= cf->ranges[r].first_col &&
           col <= cf->ranges[r].last_col;
}

static int in_ranges(const owf_condfmt *cf, unsigned row, unsigned col)
{
    int r;
    for (r = 0; r < cf->nranges; r++)
        if (row >= cf->ranges[r].first_row && row <= cf->ranges[r].last_row && col >= cf->ranges[r].first_col &&
            col <= cf->ranges[r].last_col)
            return 1;
    return 0;
}

static int by_value(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* A colour scale's points as numbers, from the numbers in its ranges. 0 when it has none to colour. */
static int scale_points(evaluator *ev, const owf_sheet *s, const owf_condfmt *cf, double *at)
{
    double lo = 0, hi = 0, *all = NULL;
    long n = 0, cap = 0;
    int i, k;
    for (i = 0; i < s->ncells; i++) {
        const owf_cell *c = &s->cells[i];
        if (c->type != OWF_VALUE_NUMBER || !in_ranges(cf, c->row, c->col))
            continue;
        if (!n || c->number < lo)
            lo = c->number;
        if (!n || c->number > hi)
            hi = c->number;
        if (n == cap) {
            long want = cap ? cap * 2 : 64;
            double *p = realloc(all, (size_t)want * sizeof *p);
            if (!p) {
                free(all);
                return 0;
            }
            all = p;
            cap = want;
        }
        all[n++] = c->number;
    }
    if (!n) {
        free(all);
        return 0;
    }
    qsort(all, (size_t)n, sizeof *all, by_value);
    for (k = 0; k < cf->npoints; k++) {
        const owf_cfpoint *p = &cf->points[k];
        double v = 0;
        const owf_cell *r;
        switch (p->kind) {
        case OWF_CFV_MAX: at[k] = hi; continue;
        case OWF_CFV_MIN: at[k] = lo; continue;
        case OWF_CFV_FORMULA:
            r = p->value ? eval_at(ev, p->value, 0, 0, cf->ranges[0].first_row, cf->ranges[0].first_col) : NULL;
            at[k] = r && r->type == OWF_VALUE_NUMBER ? r->number : k ? hi : lo;
            continue;
        default:
            break;
        }
        if (p->value)
            v = strtod(p->value, NULL);
        if (p->kind == OWF_CFV_PERCENT) {
            at[k] = lo + (hi - lo) * v / 100;
        } else if (p->kind == OWF_CFV_PERCENTILE) {      /* as PERCENTILE: between the two nearest */
            double pos = (double)(n - 1) * (v < 0 ? 0 : v > 100 ? 100 : v) / 100;
            long lo_i = (long)pos;
            at[k] = lo_i + 1 < n ? all[lo_i] + (all[lo_i + 1] - all[lo_i]) * (pos - (double)lo_i) : all[n - 1];
        } else {
            at[k] = v;
        }
    }
    free(all);
    return 1;
}

static unsigned long mix(unsigned long a, unsigned long b, double t)
{
    unsigned long out = 0;
    int k;
    for (k = 16; k >= 0; k -= 8) {
        double x = (double)((a >> k) & 255), y = (double)((b >> k) & 255);
        out |= (unsigned long)(x + (y - x) * t + 0.5) << k;
    }
    return out;
}

/* A number's colour on the scale: the first point's below it, the last's above, between two the mix. */
static unsigned long scale_colour(const owf_condfmt *cf, const double *at, double v)
{
    int k, n = cf->npoints;
    if (v <= at[0])
        return OWF_SHADE_RGB(cf->points[0].colour);
    for (k = 1; k < n; k++)
        if (v <= at[k]) {
            double span = at[k] - at[k - 1];
            return mix(OWF_SHADE_RGB(cf->points[k - 1].colour), OWF_SHADE_RGB(cf->points[k].colour),
                       span > 0 ? (v - at[k - 1]) / span : 1);
        }
    return OWF_SHADE_RGB(cf->points[n - 1].colour);
}

static void give(oc_look *l, const owf_condfmt *cf)
{
    unsigned flags = cf->font_set & ~l->set;
    l->flags = (l->flags & ~flags) | (cf->font_flags & flags);
    l->set |= flags;
    if (OWF_SHADE_SET(cf->font_colour) && !(l->parts & OC_LOOK_COLOUR)) {
        l->colour = OWF_SHADE_RGB(cf->font_colour);
        l->parts |= OC_LOOK_COLOUR;
    }
    if (OWF_SHADE_SET(cf->fill) && !(l->parts & OC_LOOK_FILL)) {
        l->fill = OWF_SHADE_RGB(cf->fill);
        l->parts |= OC_LOOK_FILL;
    }
}

/* The rows (or columns) showing that a span crosses: their places in l, from *a to *b (b < a: none). */
static void span_in(const oc_line *l, int n, unsigned first, unsigned last, int *a, int *b)
{
    int i;
    *a = 0;
    *b = -1;
    for (i = 0; i < n && l[i].index < first; i++)
        ;
    *a = i;
    for (; i < n && l[i].index <= last; i++)
        *b = i;
}

static unsigned long lines_hash(const oc_line *l, int n)
{
    unsigned long h = 5381;
    int i;
    for (i = 0; i < n; i++)
        h = h * 33 + l[i].index;
    return h;
}

const oc_look *oc_cf_looks(oc_core *core, const oc_line *rl, int nr, const oc_line *cl, int nc)
{
    const owf_sheet *s = oc_sheet(core);
    unsigned long hash = (lines_hash(rl, nr) * 31 + lines_hash(cl, nc)) * 31 + (unsigned long)s->ncondfmts;
    oc_look *looks;
    evaluator ev;
    int *order, n = 0, i, k, ri, rj;
    if (!s->ncondfmts || !nr || !nc || !core->calc)
        return NULL;
    if (core->cf_looks && core->cf_stamp == core->changes && core->cf_sheet == core->sheet && core->cf_nr == nr &&
        core->cf_nc == nc && core->cf_hash == hash)
        return core->cf_looks;                           /* nothing has changed */
    free(core->cf_looks);
    core->cf_looks = NULL;
    looks = calloc((size_t)nr * (size_t)nc, sizeof *looks);
    order = malloc((size_t)s->ncondfmts * sizeof *order);
    if (!looks || !order) {
        free(looks);
        free(order);
        return NULL;
    }
    /* by priority (1 first), those of equal priority in the file's order */
    for (i = 0; i < s->ncondfmts; i++) {
        int p = s->condfmts[i].priority;
        for (k = n; k > 0 && s->condfmts[order[k - 1]].priority > p; k--)
            order[k] = order[k - 1];
        order[k] = i;
        n++;
    }
    memset(&ev, 0, sizeof ev);
    ev.core = core;
    ev.sheet = core->sheet;
    for (k = 0; k < n; k++) {
        const owf_condfmt *cf = &s->condfmts[order[k]];
        double at[3];
        int r;
        if (!cf->nranges || (cf->type == OWF_CF_KEPT && !cf->formula[0]))
            continue;
        if (cf->type == OWF_CF_COLOUR_SCALE && (cf->npoints < 2 || !scale_points(&ev, s, cf, at)))
            continue;
        eval_reset(&ev);
        for (r = 0; r < cf->nranges; r++) {
            int a, b, c, e;
            span_in(rl, nr, cf->ranges[r].first_row, cf->ranges[r].last_row, &a, &b);
            span_in(cl, nc, cf->ranges[r].first_col, cf->ranges[r].last_col, &c, &e);
            for (ri = a; ri <= b; ri++)
                for (rj = c; rj <= e; rj++) {
                    oc_look *l = &looks[(size_t)ri * (size_t)nc + (size_t)rj];
                    unsigned row = rl[ri].index, col = cl[rj].index;
                    const owf_cell *cell;
                    int prev;
                    if (l->stop)
                        continue;
                    for (prev = 0; prev < r && !in_ranges_one(cf, prev, row, col); prev++)
                        ;
                    if (prev < r)
                        continue;                    /* a cell two of its ranges share: once */
                    cell = owf_sheet_find(s, row, col);
                    if (cf->type == OWF_CF_COLOUR_SCALE) {
                        if (cell && cell->type == OWF_VALUE_NUMBER && !(l->parts & OC_LOOK_FILL)) {
                            l->fill = scale_colour(cf, at, cell->number);
                            l->parts |= OC_LOOK_FILL;
                            if (cf->stop)
                                l->stop = 1;
                        }
                        continue;
                    }
                    if (!holds(&ev, cf, cell, row, col))
                        continue;
                    give(l, cf);
                    if (cf->stop)
                        l->stop = 1;
                }
        }
    }
    eval_reset(&ev);
    free(order);
    core->cf_looks = looks;
    core->cf_stamp = core->changes;
    core->cf_sheet = core->sheet;
    core->cf_nr = nr;
    core->cf_nc = nc;
    core->cf_hash = hash;
    return looks;
}

void oc_cf_forget(oc_core *core)
{
    free(core->cf_looks);
    core->cf_looks = NULL;
}
