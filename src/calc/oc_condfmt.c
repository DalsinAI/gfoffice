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

#include <stdio.h>
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
    if (nranges && !(dst->ranges = owf_malloc((size_t)nranges * sizeof *dst->ranges))) failed = 1;
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
        if (!(p = owf_realloc(*rules, (size_t)size * sizeof *p))) return 0;
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
    owf_free(rules);
}

static int cf_spec_ok(const oc_condfmt_spec *r)
{
    int i, two;
    if (r && r->type == OC_CF_DATA_BAR) {
        if (r->npoints != 0 && r->npoints != 2) return 0;
        for (i = 0; i < r->npoints; ++i) {
            if (r->points[i].kind < OWF_CFV_MIN || r->points[i].kind > OWF_CFV_PERCENTILE) return 0;
            if (r->points[i].kind >= OWF_CFV_NUMBER && (!r->points[i].value || !*r->points[i].value)) return 0;
        }
        return 1;
    }
    if (r && r->type == OC_CF_ICON_SET)
        return oc_icon_set_name(r->icon_set) != NULL;
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

/* A data bar's or icon set's cfRule, as Excel writes it (from owf_malloc), or NULL. */
static char *kept_xml(const oc_condfmt_spec *r)
{
    static const char *const kinds[5] = { "min", "max", "num", "percent", "percentile" };
    char buf[1024];
    size_t at = 0;
    int i, n;
#define PUT(...) do { \
        int w_ = snprintf(buf + at, sizeof buf - at, __VA_ARGS__); \
        if (w_ < 0 || (size_t)w_ >= sizeof buf - at) \
            return NULL; \
        at += (size_t)w_; \
    } while (0)
    if (r->type == OC_CF_DATA_BAR) {
        unsigned long rgb = OWF_SHADE_SET(r->fill) ? OWF_SHADE_RGB(r->fill) : 0x638EC6UL;
        PUT("<cfRule type=\"dataBar\"><dataBar%s>", r->hide_value ? " showValue=\"0\"" : "");
        for (i = 0; i < 2; ++i) {
            int kind = r->npoints == 2 ? r->points[i].kind : i ? OWF_CFV_MAX : OWF_CFV_MIN;
            if (kind >= OWF_CFV_NUMBER && r->npoints == 2)
                PUT("<cfvo type=\"%s\" val=\"%s\"/>", kinds[kind], r->points[i].value);
            else
                PUT("<cfvo type=\"%s\"/>", kinds[kind]);
        }
        PUT("<color rgb=\"FF%06lX\"/></dataBar></cfRule>", rgb & 0xFFFFFFUL);
    } else {
        const char *name = oc_icon_set_name(r->icon_set);
        n = name[0] - '0';
        PUT("<cfRule type=\"iconSet\"><iconSet iconSet=\"%s\"%s%s>", name,
            r->reverse ? " reverse=\"1\"" : "", r->hide_value ? " showValue=\"0\"" : "");
        for (i = 0; i < n; ++i)
            PUT("<cfvo type=\"percent\" val=\"%d\"/>", i * 100 / n);
        PUT("</iconSet></cfRule>");
    }
#undef PUT
    return oc_strdup(buf);
}

static int add_rule(oc_core *core, const oc_condfmt_spec *rule, const char *xml, const owf_range *targets, int n);

int oc_core_add_condfmt(oc_core *core, const oc_condfmt_spec *rule)
{
    owf_range targets[OC_MAX_RANGES];
    int n;
    if (!core || !cf_spec_ok(rule)) return OWF_ERR_FORMAT;
    if (oc_protected(core, 0)) return OC_PROTECTED;     /* as Excel: none on a protected sheet */
    if (core->edit_mode) oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!(n = cf_targets(core, targets))) return OWF_ERR_UNSUPPORTED;
    if (rule->type == OC_CF_DATA_BAR || rule->type == OC_CF_ICON_SET) {
        oc_condfmt_spec plain;
        char *xml = kept_xml(rule);
        int rc;
        if (!xml) return OWF_ERR_MEMORY;
        memset(&plain, 0, sizeof plain);
        plain.type = OWF_CF_KEPT;
        plain.stop = rule->stop;
        rc = add_rule(core, &plain, xml, targets, n);
        owf_free(xml);
        return rc;
    }
    return add_rule(core, rule, NULL, targets, n);
}

/* Adds rule (with xml, a kept rule's cfRule) over the targets: one Undo step. */
static int add_rule(oc_core *core, const oc_condfmt_spec *rule, const char *xml, const owf_range *targets, int n)
{
    owf_sheet *sheet = oc_sheet(core);
    owf_condfmt made, *dst;
    oc_undo *undo;
    int i, priority = 0;
    memset(&made, 0, sizeof made);
    made.xml = (char *)xml;
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
    if (oc_protected(core, 0)) return OC_PROTECTED;
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
    owf_free(sheet->condfmts); sheet->condfmts = fresh; sheet->ncondfmts = nfresh; sheet->capcondfmts = cap;
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

/* Points (a colour scale's, a data bar's or an icon set's) as numbers, from the numbers in cf's ranges. 0 when
 * there are none. */
static int points_at(evaluator *ev, const owf_sheet *s, const owf_condfmt *cf, const owf_cfpoint *pts, int npts,
                     double *at)
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
    for (k = 0; k < npts; k++) {
        const owf_cfpoint *p = &pts[k];
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

static int scale_points(evaluator *ev, const owf_sheet *s, const owf_condfmt *cf, double *at)
{
    return points_at(ev, s, cf, cf->points, cf->npoints, at);
}

/* ---- Data bars and icon sets: rules the model keeps as the file's cfRule (OWF_CF_KEPT), read here ---- */

typedef struct {
    int kind;                    /* 0 neither, 1 data bar, 2 icon set */
    int n;                       /* points */
    owf_cfpoint pts[5];
    char vals[5][48];
    unsigned long colour;        /* a bar's, 0xRRGGBB */
    int min_len, max_len;        /* a bar's, percent of the cell */
    int family, reverse, hide;
} kept_rule;

/* An attribute's value in the tag starting at tag (up to its '>'): 1 when it is there. */
static int tag_attr(const char *tag, const char *name, char *out, size_t size)
{
    size_t n = strlen(name);
    const char *p = tag, *end = strchr(tag, '>');
    if (!end)
        return 0;
    while ((p = strstr(p, name)) != NULL && p < end) {
        if (p > tag && p[-1] == ' ' && p[n] == '=' && p[n + 1] == '"') {
            size_t k = 0;
            for (p += n + 2; *p && *p != '"' && k + 1 < size; ++p)
                out[k++] = *p;
            out[k] = 0;
            return 1;
        }
        p += n;
    }
    return 0;
}

static const struct { const char *name; int family; } icon_sets[] = {
    { "3Arrows", OC_ICONS_ARROWS }, { "3ArrowsGray", OC_ICONS_ARROWS_GREY }, { "3Flags", OC_ICONS_LIGHTS },
    { "3TrafficLights1", OC_ICONS_LIGHTS }, { "3TrafficLights2", OC_ICONS_LIGHTS }, { "3Signs", OC_ICONS_LIGHTS },
    { "3Symbols", OC_ICONS_LIGHTS }, { "3Symbols2", OC_ICONS_LIGHTS }, { "4Arrows", OC_ICONS_ARROWS },
    { "4ArrowsGray", OC_ICONS_ARROWS_GREY }, { "4RedToBlack", OC_ICONS_RED_BLACK }, { "4Rating", OC_ICONS_RATING },
    { "4TrafficLights", OC_ICONS_LIGHTS }, { "5Arrows", OC_ICONS_ARROWS }, { "5ArrowsGray", OC_ICONS_ARROWS_GREY },
    { "5Rating", OC_ICONS_RATING }, { "5Quarters", OC_ICONS_QUARTERS }
};

const char *oc_icon_set_name(const char *name)
{
    size_t i;
    for (i = 0; name && i < sizeof icon_sets / sizeof icon_sets[0]; ++i) {
        const char *a = name, *b = icon_sets[i].name;
        while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
            a++, b++;
        if (!*a && !*b)
            return icon_sets[i].name;
    }
    return NULL;
}

static int kept_parse(const owf_condfmt *cf, kept_rule *k)
{
    static const char *const kinds[6] = { "min", "max", "num", "percent", "percentile", "formula" };
    const char *tag, *p;
    char v[48];
    size_t i;
    memset(k, 0, sizeof *k);
    if (!cf->xml || cf->formula[0])
        return 0;
    if ((tag = strstr(cf->xml, "<dataBar")) != NULL) {
        k->kind = 1;
        k->colour = 0x638EC6UL;
        k->min_len = 10;
        k->max_len = 90;
        if (tag_attr(tag, "minLength", v, sizeof v)) k->min_len = atoi(v);
        if (tag_attr(tag, "maxLength", v, sizeof v)) k->max_len = atoi(v);
        if (k->min_len < 0 || k->min_len > 100) k->min_len = 10;
        if (k->max_len < k->min_len || k->max_len > 100) k->max_len = 90;
        if ((p = strstr(tag, "<color")) != NULL && tag_attr(p, "rgb", v, sizeof v) && strlen(v) >= 6)
            k->colour = strtoul(v + strlen(v) - 6, NULL, 16) & 0xFFFFFFUL;
    } else if ((tag = strstr(cf->xml, "<iconSet")) != NULL) {
        k->kind = 2;
        k->family = OC_ICONS_LIGHTS;
        if (!tag_attr(tag, "iconSet", v, sizeof v))
            strcpy(v, "3TrafficLights1");
        for (i = 0; i < sizeof icon_sets / sizeof icon_sets[0]; ++i)
            if (!strcmp(v, icon_sets[i].name))
                k->family = icon_sets[i].family;
        if (tag_attr(tag, "reverse", v, sizeof v)) k->reverse = !strcmp(v, "1") || !strcmp(v, "true");
    } else {
        return 0;
    }
    if (tag_attr(tag, "showValue", v, sizeof v)) k->hide = !strcmp(v, "0") || !strcmp(v, "false");
    for (p = strstr(tag, "<cfvo"); p && k->n < 5; p = strstr(p + 5, "<cfvo")) {
        owf_cfpoint *pt = &k->pts[k->n];
        int j;
        pt->kind = OWF_CFV_MIN;
        if (tag_attr(p, "type", v, sizeof v)) {
            for (j = 0; j < 6; ++j)
                if (!strcmp(v, kinds[j]))
                    pt->kind = j;
            if (!strcmp(v, "autoMax")) pt->kind = OWF_CFV_MAX;
        }
        if (pt->kind >= OWF_CFV_NUMBER && tag_attr(p, "val", k->vals[k->n], sizeof k->vals[k->n]))
            pt->value = k->vals[k->n];
        else if (pt->kind >= OWF_CFV_NUMBER)
            pt->kind = k->n ? OWF_CFV_MAX : OWF_CFV_MIN;
        k->n++;
    }
    if (k->kind == 1 && k->n != 2)
        return 0;
    return k->kind == 1 || k->n >= 3;
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
        double at[5];
        kept_rule kr;
        int r, kept = 0;
        if (!cf->nranges)
            continue;
        if (cf->type == OWF_CF_KEPT && !cf->formula[0]) {
            if (!kept_parse(cf, &kr) || !points_at(&ev, s, cf, kr.pts, kr.n, at))
                continue;
            kept = 1;
        }
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
                    if (kept) {
                        if (!cell || cell->type != OWF_VALUE_NUMBER)
                            continue;
                        if (kr.kind == 1 && !(l->parts & OC_LOOK_BAR)) {
                            double t = at[1] > at[0] ? (cell->number - at[0]) / (at[1] - at[0])
                                                     : cell->number >= at[1];
                            if (t < 0) t = 0;
                            if (t > 1) t = 1;
                            l->bar = (unsigned short)(10 * (kr.min_len + t * (kr.max_len - kr.min_len)) + 0.5);
                            l->bar_colour = kr.colour;
                            l->parts |= OC_LOOK_BAR;
                        } else if (kr.kind == 2 && !(l->parts & OC_LOOK_ICON)) {
                            int level = 0, j;
                            for (j = 1; j < kr.n; j++)
                                if (cell->number >= at[j])
                                    level = j;
                            l->icons = (unsigned char)kr.family;
                            l->nicons = (unsigned char)kr.n;
                            l->icon = (unsigned char)(kr.reverse ? kr.n - 1 - level : level);
                            l->parts |= OC_LOOK_ICON;
                        } else {
                            continue;
                        }
                        if (kr.hide)
                            l->parts |= OC_LOOK_NO_VALUE;
                        if (cf->stop)
                            l->stop = 1;
                        continue;
                    }
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
