/*
 * GFOpenCalc's core: displayed-value, Standard and Top/Bottom AutoFilters.
 * More than one column can filter a table; rows must satisfy every column.
 * The criteria are the workbook model's ordinary XLSX filterColumn XML, so
 * XLSX, ODS, the native UI and future front ends share one representation.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"
#include "owf_scenario.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_FILTER_CHOICES 1000
#define OC_FILTER_ROWS 65536U

typedef struct {
    unsigned col;                 /* absolute sheet column */
    int kind;                     /* 0 comparison, 1 top/bottom */
    int op;                       /* OC_FILTER_* */
    char *value;                  /* displayed value, decoded UTF-8 */
    int has_second, op2, use_or;
    char *value2;
    unsigned rank;
    int bottom, percent, threshold_valid;
    double threshold;
} filter_rule;

static const char *shown_at(const oc_core *core, unsigned row, unsigned col, char *buf, size_t size);

static const owf_rowinfo *row_info(const owf_sheet *sheet, unsigned row)
{
    int lo = 0, hi = sheet->nrows;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sheet->rows[mid].row < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < sheet->nrows && sheet->rows[lo].row == row ? &sheet->rows[lo] : NULL;
}

static int same_text(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return !*a && !*b;
}

static int compare_ci(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
    }
    return *a ? 1 : *b ? -1 : 0;
}

static int number_text(const char *s, double *value)
{
    char *end;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) return 0;
    *value = strtod(s, &end);
    if (end == s) return 0;
    while (*end == ' ' || *end == '\t') end++;
    return !*end;
}

static const char *next_utf8(const char *s)
{
    if (!*s) return s;
    s++;
    while (((unsigned char)*s & 0xc0) == 0x80) s++;
    return s;
}

static int wild_literal(int a, int b)
{
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    return a == b;
}

static int wild_match(const char *pattern, const char *text)
{
    while (*pattern) {
        if (*pattern == '*') {
            while (*pattern == '*') pattern++;
            if (!*pattern) return 1;
            do {
                if (wild_match(pattern, text)) return 1;
                if (!*text) break;
                text = next_utf8(text);
            } while (1);
            return 0;
        }
        if (!*text)
            return 0;
        if (*pattern == '?') {
            pattern++;
            text = next_utf8(text);
            continue;
        }
        if (*pattern == '~' && (pattern[1] == '*' || pattern[1] == '?' || pattern[1] == '~'))
            pattern++;
        if (!wild_literal((unsigned char)*pattern, (unsigned char)*text))
            return 0;
        pattern++;
        text++;
    }
    return !*text;
}

static int compare_one(const char *shown, int op, const char *value)
{
    double a, b;
    int cmp;
    if (op == OC_FILTER_EQUAL || op == OC_FILTER_NOT_EQUAL) {
        int equal = wild_match(value, shown);
        return op == OC_FILTER_EQUAL ? equal : !equal;
    }
    if (number_text(shown, &a) && number_text(value, &b))
        cmp = a < b ? -1 : a > b ? 1 : 0;
    else
        cmp = compare_ci(shown, value);
    switch (op) {
    case OC_FILTER_LESS: return cmp < 0;
    case OC_FILTER_LESS_EQUAL: return cmp <= 0;
    case OC_FILTER_GREATER: return cmp > 0;
    case OC_FILTER_GREATER_EQUAL: return cmp >= 0;
    default: return 0;
    }
}

static int compare_value(const char *shown, const filter_rule *rule)
{
    int first, second;
    double a;
    if (rule->kind) {
        if (!rule->threshold_valid || !number_text(shown, &a))
            return 0;
        return rule->bottom ? a <= rule->threshold : a >= rule->threshold;
    }
    first = compare_one(shown, rule->op, rule->value);
    if (!rule->has_second)
        return first;
    second = compare_one(shown, rule->op2, rule->value2);
    return rule->use_or ? first || second : first && second;
}

static int compare_double(const void *aa, const void *bb)
{
    double a = *(const double *)aa, b = *(const double *)bb;
    return a < b ? -1 : a > b;
}

/* Top/bottom thresholds are column properties: as in spreadsheet AutoFilter,
 * each is ranked over the filter range and the resulting column conditions
 * are then ANDed. */
static int prepare_rules(const oc_core *core, filter_rule *rules, int n)
{
    const owf_sheet *sheet = oc_sheet(core);
    int i;
    for (i = 0; i < n; i++) {
        double *values;
        unsigned row, used = 0, take;
        char shown[256];
        if (!rules[i].kind)
            continue;
        rules[i].threshold_valid = 0;
        values = malloc((size_t)(sheet->autofilter.last_row - sheet->autofilter.first_row) * sizeof *values);
        if (!values)
            return OWF_ERR_MEMORY;
        for (row = sheet->autofilter.first_row + 1; row <= sheet->autofilter.last_row; row++) {
            double value;
            if (number_text(shown_at(core, row, rules[i].col, shown, sizeof shown), &value))
                values[used++] = value;
        }
        if (used) {
            qsort(values, used, sizeof *values, compare_double);
            take = rules[i].percent ? (used * rules[i].rank + 99) / 100 : rules[i].rank;
            if (!take) take = 1;
            if (take > used) take = used;
            rules[i].threshold = values[rules[i].bottom ? take - 1 : used - take];
            rules[i].threshold_valid = 1;
        }
        free(values);
    }
    return OWF_OK;
}

static const char *shown_at(const oc_core *core, unsigned row, unsigned col, char *buf, size_t size)
{
    const owf_cell *cell = oc_find(core, core->sheet, row, col);
    if (cell && cell->type == OWF_VALUE_TEXT)
        return cell->text ? cell->text : "";
    oc_core_cell_text(core, core->sheet, row, col, buf, size);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf;
}

static int compare_text(const void *aa, const void *bb)
{
    const unsigned char *a = (const unsigned char *)*(const char *const *)aa;
    const unsigned char *b = (const unsigned char *)*(const char *const *)bb;
    while (*a && *b) {
        int x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x - y;
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static int compare_rule(const void *aa, const void *bb)
{
    const filter_rule *a = aa, *b = bb;
    return a->col < b->col ? -1 : a->col > b->col;
}

static oc_undo *filter_step(oc_core *core)
{
    oc_undo *u = oc_undo_new(core, OC_UNDO_LAYOUT);
    if (u && oc_undo_save_layout(core, u, core->sheet) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    return u;
}

static void rules_free(filter_rule *rules, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        owf_free(rules[i].value);
        owf_free(rules[i].value2);
    }
    free(rules);
}

static char *xml_value(const char *p, const char *end)
{
    char *out, *q;
    size_t cap;
    const char quote = p < end ? *p++ : 0;
    if (quote != '"' && quote != '\'')
        return NULL;
    cap = (size_t)(end - p) + 1;
    if (!(out = owf_malloc(cap)))
        return NULL;
    q = out;
    while (p < end && *p != quote) {
        if (*p == '&') {
            if (end - p >= 5 && !memcmp(p, "&amp;", 5)) { *q++ = '&'; p += 5; continue; }
            if (end - p >= 4 && !memcmp(p, "&lt;", 4)) { *q++ = '<'; p += 4; continue; }
            if (end - p >= 4 && !memcmp(p, "&gt;", 4)) { *q++ = '>'; p += 4; continue; }
            if (end - p >= 6 && !memcmp(p, "&quot;", 6)) { *q++ = '"'; p += 6; continue; }
            if (end - p >= 6 && !memcmp(p, "&apos;", 6)) { *q++ = '\''; p += 6; continue; }
        }
        *q++ = *p++;
    }
    if (p >= end || *p != quote) {
        owf_free(out);
        return NULL;
    }
    *q = 0;
    return out;
}

static int custom_op(const char *tag, const char *close)
{
    static const char *const names[] = {
        "equal", "notEqual", "lessThan", "lessThanOrEqual", "greaterThan", "greaterThanOrEqual"
    };
    const char *at = strstr(tag, "operator=");
    char quote;
    size_t i;
    if (!at || at > close)
        return OC_FILTER_EQUAL;
    quote = at[9];
    if ((quote != '"' && quote != '\'') || at + 10 > close)
        return -1;
    at += 10;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        size_t n = strlen(names[i]);
        if (at + n < close && !memcmp(at, names[i], n) && at[n] == quote)
            return (int)i;
    }
    return -1;
}

static const char *xml_attr_at(const char *tag, const char *close, const char *name)
{
    size_t n = strlen(name);
    const char *p = tag;
    while ((p = strstr(p, name)) != NULL && p < close) {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\r' || p[-1] == '\n') &&
            p + n < close && p[n] == '=')
            return p + n + 1;
        p += n;
    }
    return NULL;
}

static int xml_unsigned_attr(const char *tag, const char *close, const char *name, unsigned *value)
{
    const char *p = xml_attr_at(tag, close, name);
    char *tail;
    unsigned long v;
    char quote;
    if (!p || ((quote = *p++) != '"' && quote != '\''))
        return 0;
    v = strtoul(p, &tail, 10);
    if (tail == p || tail >= close || *tail != quote || v > 1000000UL)
        return 0;
    *value = (unsigned)v;
    return 1;
}

static int xml_bool_attr(const char *tag, const char *close, const char *name, int fallback)
{
    const char *p = xml_attr_at(tag, close, name);
    char quote;
    if (!p)
        return fallback;
    if ((quote = *p++) != '"' && quote != '\'')
        return fallback;
    if ((*p == '1' && p + 1 < close && p[1] == quote) ||
        (p + 4 < close && !memcmp(p, "true", 4) && p[4] == quote))
        return 1;
    if ((*p == '0' && p + 1 < close && p[1] == quote) ||
        (p + 5 < close && !memcmp(p, "false", 5) && p[5] == quote))
        return 0;
    return fallback;
}

static int parse_custom_tag(const char *tag, const char *limit, int *op, char **value, const char **tag_end)
{
    const char *at;
    if (!tag || tag >= limit || !(*tag_end = strchr(tag, '>')) || *tag_end > limit ||
        (*op = custom_op(tag, *tag_end)) < 0 || !(at = xml_attr_at(tag, *tag_end, "val")))
        return 0;
    if (!(*value = xml_value(at, *tag_end)))
        return -1;
    return 1;
}

/* The editable subset: up to two comparisons per filterColumn. Other imported
 * conditions remain safely preserved until the user edits filtering. */
static int parse_rules(const owf_sheet *sheet, filter_rule **out, int *count)
{
    const char *p = sheet->autofilter_xml;
    filter_rule *rules = NULL;
    int n = 0;
    *out = NULL;
    *count = 0;
    while (p && *p) {
        const char *fc = strstr(p, "<filterColumn"), *open, *end, *id, *digits, *tail, *fv, *at, *next, *tag_end, *top;
        const char *set, *set_end;
        char *number_end;
        unsigned long offset;
        char quote, *value = NULL, *value2 = NULL;
        int kind = 0, op = OC_FILTER_EQUAL, op2 = OC_FILTER_EQUAL, has_second = 0, use_or = 0;
        int bottom = 0, percent = 0;
        unsigned rank = 0;
        filter_rule *grown;
        int i;
        if (!fc)
            break;                                  /* sortState alone is not a value criterion */
        if (!(open = strchr(fc, '>')) || !(end = strstr(open, "</filterColumn>")))
            goto unsupported;
        id = strstr(fc, "colId=");
        if (!id || id > open || ((quote = id[6]) != '"' && quote != '\''))
            goto unsupported;
        digits = id + 7;
        offset = strtoul(digits, &number_end, 10);
        tail = number_end;
        if (tail == digits || tail > open || *tail != quote ||
            offset > sheet->autofilter.last_col - sheet->autofilter.first_col)
            goto unsupported;
        top = strstr(open, "<top10");
        if (top && top < end) {
            if (!(tag_end = strchr(top, '>')) || tag_end > end ||
                !xml_unsigned_attr(top, tag_end, "val", &rank) || !rank || rank > 500)
                goto unsupported;
            kind = 1;
            bottom = !xml_bool_attr(top, tag_end, "top", 1);
            percent = xml_bool_attr(top, tag_end, "percent", 0);
            if (percent && rank > 100)
                goto unsupported;
            if (!(value = oc_strdup(""))) {
                rules_free(rules, n);
                return OWF_ERR_MEMORY;
            }
        } else {
            fv = strstr(open, "<filter ");
            if (!fv || fv > end) {
                int parsed;
                set = strstr(open, "<customFilters");
                if (!set || set > end || !(set_end = strchr(set, '>')) || set_end > end)
                    goto unsupported;
                fv = strstr(open, "<customFilter ");
                parsed = parse_custom_tag(fv, end, &op, &value, &tag_end);
                if (parsed < 0) { rules_free(rules, n); return OWF_ERR_MEMORY; }
                if (!parsed)
                    goto unsupported;
                next = strstr(fv + 1, "<customFilter ");
                if (next && next < end) {
                    const char *third;
                    parsed = parse_custom_tag(next, end, &op2, &value2, &tag_end);
                    if (parsed < 0) { owf_free(value); rules_free(rules, n); return OWF_ERR_MEMORY; }
                    if (!parsed) { owf_free(value); goto unsupported; }
                    third = strstr(next + 1, "<customFilter ");
                    if (third && third < end) { owf_free(value); owf_free(value2); goto unsupported; }
                    has_second = 1;
                    use_or = !xml_bool_attr(set, set_end, "and", 0);
                }
            } else {
                tag_end = strchr(fv, '>');
                next = strstr(fv + 1, "<filter ");
                if (!tag_end || tag_end > end || (next && next < end) ||
                    !(at = strstr(fv, "val=")) || at > tag_end || (at[4] != '"' && at[4] != '\''))
                    goto unsupported;
                if (!(value = xml_value(at + 4, tag_end))) {
                    rules_free(rules, n);
                    return OWF_ERR_MEMORY;
                }
            }
        }
        for (i = 0; i < n; i++)
            if (rules[i].col == sheet->autofilter.first_col + (unsigned)offset)
                break;
        if (i < n) {
            owf_free(rules[i].value);
            owf_free(rules[i].value2);
            rules[i].kind = kind;
            rules[i].op = op;
            rules[i].value = value;
            rules[i].has_second = has_second;
            rules[i].op2 = op2;
            rules[i].value2 = value2;
            rules[i].use_or = use_or;
            rules[i].rank = rank;
            rules[i].bottom = bottom;
            rules[i].percent = percent;
        } else {
            grown = realloc(rules, (size_t)(n + 1) * sizeof *grown);
            if (!grown) {
                owf_free(value);
                owf_free(value2);
                rules_free(rules, n);
                return OWF_ERR_MEMORY;
            }
            rules = grown;
            rules[n].col = sheet->autofilter.first_col + (unsigned)offset;
            rules[n].kind = kind;
            rules[n].op = op;
            rules[n].value = value;
            rules[n].has_second = has_second;
            rules[n].op2 = op2;
            rules[n].value2 = value2;
            rules[n].use_or = use_or;
            rules[n].rank = rank;
            rules[n].bottom = bottom;
            rules[n].percent = percent;
            n++;
        }
        p = end + 15;
    }
    if (n > 1)
        qsort(rules, (size_t)n, sizeof *rules, compare_rule);
    *out = rules;
    *count = n;
    return OWF_OK;
unsupported:
    rules_free(rules, n);
    return OWF_ERR_UNSUPPORTED;
}

static int row_matches(const oc_core *core, unsigned row, const filter_rule *rules, int n, int except_col)
{
    int i;
    char shown[256];
    for (i = 0; i < n; i++)
        if ((int)rules[i].col != except_col && !compare_value(shown_at(core, row, rules[i].col, shown, sizeof shown), &rules[i]))
            return 0;
    return 1;
}

/* In the live core bit 0 is a row hidden by the user and bit 1 one hidden by
 * the current filter. File writers treat either as hidden. Parsing the saved
 * criteria recovers filter-hidden rows after an XLSX/ODS round trip. */
static int mark_filter_rows(oc_core *core, owf_sheet *sheet)
{
    filter_rule *rules = NULL;
    int n = 0, rc = parse_rules(sheet, &rules, &n), i;
    if (rc != OWF_OK)
        return 0;
    if (prepare_rules(core, rules, n) != OWF_OK) {
        rules_free(rules, n);
        return 0;
    }
    for (i = 0; i < sheet->nrows; i++) {
        unsigned row = sheet->rows[i].row;
        if (sheet->rows[i].hidden && row > sheet->autofilter.first_row && row <= sheet->autofilter.last_row &&
            !row_matches(core, row, rules, n, -1))
            sheet->rows[i].hidden = 2;
    }
    rules_free(rules, n);
    return 1;
}

static void show_filter_rows(owf_sheet *sheet, unsigned first, unsigned last, int exact)
{
    int i;
    for (i = 0; i < sheet->nrows; i++)
        if (sheet->rows[i].row >= first && sheet->rows[i].row <= last)
            sheet->rows[i].hidden = exact ? sheet->rows[i].hidden & 1 : 0;
}

int oc_core_has_autofilter(const oc_core *core)
{
    return core && oc_sheet(core)->has_autofilter;
}

int oc_core_set_autofilter(oc_core *core, int enabled)
{
    owf_sheet *sheet;
    oc_sheetstate *state;
    owf_range range;
    oc_undo *u;
    unsigned rows, cols;
    if (!core)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    sheet = oc_sheet(core);
    if (!!enabled == !!sheet->has_autofilter)
        return OWF_OK;
    u = filter_step(core);
    if (!u)
        return OWF_ERR_MEMORY;
    if (!enabled) {
        int exact = mark_filter_rows(core, sheet);
        show_filter_rows(sheet, sheet->autofilter.first_row + 1, sheet->autofilter.last_row, exact);
        sheet->has_autofilter = 0;
        memset(&sheet->autofilter, 0, sizeof sheet->autofilter);
        free(sheet->autofilter_xml);
        sheet->autofilter_xml = NULL;
    } else {
        state = oc_state(core);
        if (state->sel.nranges != 1) {
            oc_undo_free(u);
            return OWF_ERR_UNSUPPORTED;
        }
        range = state->sel.ranges[0];
        if (range.first_row == range.last_row && range.first_col == range.last_col) {
            oc_used_range(sheet, &rows, &cols);
            if (rows < 2 || !cols) {
                oc_undo_free(u);
                return OWF_ERR_UNSUPPORTED;
            }
            range.first_row = range.first_col = 0;
            range.last_row = rows - 1;
            range.last_col = cols - 1;
        } else if (!oc_range_clip(core, &range, &range)) {
            oc_undo_free(u);
            return OWF_ERR_UNSUPPORTED;
        }
        if (range.first_row >= range.last_row || range.last_row - range.first_row > OC_FILTER_ROWS ||
            range.first_col > range.last_col) {
            oc_undo_free(u);
            return OWF_ERR_UNSUPPORTED;
        }
        sheet->has_autofilter = 1;
        sheet->autofilter = range;
        free(sheet->autofilter_xml);
        sheet->autofilter_xml = NULL;
    }
    oc_undo_push(core, u);
    oc_changed(core);
    return OWF_OK;
}

static int add_choice(char ***items, int *n, const char *text)
{
    char **p;
    char *copy;
    int i;
    for (i = 0; i < *n; i++)
        if (same_text((*items)[i], text))
            return OWF_OK;
    if (*n >= OC_FILTER_CHOICES)
        return OWF_ERR_UNSUPPORTED;
    copy = oc_strdup(text);
    p = realloc(*items, (size_t)(*n + 1) * sizeof *p);
    if (!copy || !p) {
        owf_free(copy);
        return OWF_ERR_MEMORY;
    }
    *items = p;
    p[(*n)++] = copy;
    return OWF_OK;
}

int oc_core_filter_choices(oc_core *core, unsigned col, char ***items, int *n)
{
    const owf_sheet *sheet;
    filter_rule *rules = NULL;
    unsigned row;
    int nrules = 0, parsed, rc = OWF_OK;
    char shown[256];
    if (items) *items = NULL;
    if (n) *n = 0;
    if (!core || !items || !n)
        return OWF_ERR_UNSUPPORTED;
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter || col < sheet->autofilter.first_col || col > sheet->autofilter.last_col ||
        sheet->autofilter.last_row - sheet->autofilter.first_row > OC_FILTER_ROWS)
        return OWF_ERR_UNSUPPORTED;
    parsed = parse_rules(sheet, &rules, &nrules);
    if (parsed == OWF_ERR_MEMORY)
        return parsed;
    if (parsed == OWF_OK && (rc = prepare_rules(core, rules, nrules)) != OWF_OK) {
        rules_free(rules, nrules);
        return rc;
    }
    for (row = sheet->autofilter.first_row + 1; row <= sheet->autofilter.last_row; row++) {
        const char *display;
        if (parsed == OWF_OK && !row_matches(core, row, rules, nrules, (int)col))
            continue;                                /* choices still possible under the other columns */
        display = shown_at(core, row, col, shown, sizeof shown);
        rc = add_choice(items, n, display);
        if (rc != OWF_OK) {
            if (rc == OWF_ERR_UNSUPPORTED) {         /* a useful bounded list, even when there are more */
                rc = OWF_OK;
                break;
            }
            oc_choices_free(*items, *n);
            *items = NULL;
            *n = 0;
            break;
        }
    }
    rules_free(rules, nrules);
    if (rc == OWF_OK && *n > 1)
        qsort(*items, (size_t)*n, sizeof **items, compare_text);
    return rc;
}

static size_t escaped_size(const char *s)
{
    size_t n = 0;
    for (; *s; s++)
        n += *s == '&' ? 5 : *s == '<' || *s == '>' ? 4 : *s == '"' ? 6 : 1;
    return n;
}

static char *put_escaped(char *p, const char *s)
{
    for (; *s; s++) {
        const char *entity = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '"' ? "&quot;" : NULL;
        if (entity) {
            size_t n = strlen(entity);
            memcpy(p, entity, n);
            p += n;
        } else {
            *p++ = *s;
        }
    }
    return p;
}

static char *rules_xml(const owf_sheet *sheet, filter_rule *rules, int n)
{
    static const char exact_before[] = "<filterColumn colId=\"%u\"><filters><filter val=\"";
    static const char exact_after[] = "\"/></filters></filterColumn>";
    static const char custom_before[] = "<filterColumn colId=\"%u\"><customFilters><customFilter operator=\"%s\" val=\"";
    static const char pair_before[] = "<filterColumn colId=\"%u\"><customFilters and=\"%d\"><customFilter operator=\"%s\" val=\"";
    static const char pair_between[] = "\"/><customFilter operator=\"%s\" val=\"";
    static const char custom_after[] = "\"/></customFilters></filterColumn>";
    static const char top_fmt[] = "<filterColumn colId=\"%u\"><top10 val=\"%u\" top=\"%d\" percent=\"%d\"/></filterColumn>";
    static const char *const names[] = {
        "equal", "notEqual", "lessThan", "lessThanOrEqual", "greaterThan", "greaterThanOrEqual"
    };
    size_t cap = 1;
    char *out, *p;
    int i;
    if (!n)
        return NULL;
    if (n > 1)
        qsort(rules, (size_t)n, sizeof *rules, compare_rule);
    for (i = 0; i < n; i++)
        cap += sizeof custom_before + sizeof pair_before + sizeof pair_between + sizeof custom_after +
               sizeof top_fmt + 128 + escaped_size(rules[i].value) +
               (rules[i].has_second ? escaped_size(rules[i].value2) : 0);
    if (!(out = malloc(cap)))
        return NULL;
    p = out;
    for (i = 0; i < n; i++) {
        const char *after;
        int k;
        if (rules[i].kind) {
            k = snprintf(p, cap - (size_t)(p - out), top_fmt,
                         rules[i].col - sheet->autofilter.first_col, rules[i].rank,
                         !rules[i].bottom, rules[i].percent);
            p += k;
            continue;
        } else if (rules[i].has_second) {
            k = snprintf(p, cap - (size_t)(p - out), pair_before,
                         rules[i].col - sheet->autofilter.first_col, !rules[i].use_or, names[rules[i].op]);
            p += k;
            p = put_escaped(p, rules[i].value);
            k = snprintf(p, cap - (size_t)(p - out), pair_between, names[rules[i].op2]);
            p += k;
            p = put_escaped(p, rules[i].value2);
            memcpy(p, custom_after, sizeof custom_after - 1);
            p += sizeof custom_after - 1;
            continue;
        } else if (rules[i].op == OC_FILTER_EQUAL) {
            k = snprintf(p, cap - (size_t)(p - out), exact_before, rules[i].col - sheet->autofilter.first_col);
            after = exact_after;
        } else {
            k = snprintf(p, cap - (size_t)(p - out), custom_before,
                         rules[i].col - sheet->autofilter.first_col, names[rules[i].op]);
            after = custom_after;
        }
        p += k;
        p = put_escaped(p, rules[i].value);
        memcpy(p, after, strlen(after));
        p += strlen(after);
    }
    *p = 0;
    return out;
}

static int update_rule(filter_rule **rules, int *n, unsigned col, int op, const char *value)
{
    filter_rule *p = *rules;
    char *copy = NULL;
    int i;
    for (i = 0; i < *n && p[i].col != col; i++) ;
    if (!value) {
        if (i == *n)
            return OWF_OK;
        owf_free(p[i].value);
        owf_free(p[i].value2);
        if (i + 1 < *n)
            memmove(&p[i], &p[i + 1], (size_t)(*n - i - 1) * sizeof *p);
        (*n)--;
        return OWF_OK;
    }
    if (!(copy = oc_strdup(value)))
        return OWF_ERR_MEMORY;
    if (i < *n) {
        owf_free(p[i].value);
        owf_free(p[i].value2);
        p[i].kind = 0;
        p[i].op = op;
        p[i].value = copy;
        p[i].has_second = p[i].use_or = 0;
        p[i].op2 = OC_FILTER_EQUAL;
        p[i].value2 = NULL;
        p[i].rank = 0;
        p[i].bottom = p[i].percent = p[i].threshold_valid = 0;
        return OWF_OK;
    }
    p = realloc(p, (size_t)(*n + 1) * sizeof *p);
    if (!p) {
        owf_free(copy);
        return OWF_ERR_MEMORY;
    }
    p[*n].col = col;
    p[*n].kind = 0;
    p[*n].op = op;
    p[*n].value = copy;
    p[*n].has_second = p[*n].use_or = 0;
    p[*n].op2 = OC_FILTER_EQUAL;
    p[*n].value2 = NULL;
    p[*n].rank = 0;
    p[*n].bottom = p[*n].percent = p[*n].threshold_valid = 0;
    *rules = p;
    (*n)++;
    return OWF_OK;
}

static int add_second_rule(filter_rule *rules, int n, unsigned col, int op, const char *value, int use_or)
{
    char *copy;
    int i;
    if (!(copy = oc_strdup(value)))
        return OWF_ERR_MEMORY;
    for (i = 0; i < n && rules[i].col != col; i++) ;
    if (i == n) {
        owf_free(copy);
        return OWF_ERR_UNSUPPORTED;
    }
    rules[i].has_second = 1;
    rules[i].op2 = op;
    rules[i].value2 = copy;
    rules[i].use_or = !!use_or;
    return OWF_OK;
}

static int update_top_rule(filter_rule **rules, int *n, unsigned col, unsigned rank, int bottom, int percent)
{
    filter_rule *p = *rules;
    char *empty;
    int i;
    for (i = 0; i < *n && p[i].col != col; i++) ;
    if (!(empty = oc_strdup("")))
        return OWF_ERR_MEMORY;
    if (i == *n) {
        p = realloc(p, (size_t)(*n + 1) * sizeof *p);
        if (!p) {
            owf_free(empty);
            return OWF_ERR_MEMORY;
        }
        *rules = p;
        (*n)++;
    } else {
        owf_free(p[i].value);
        owf_free(p[i].value2);
    }
    p[i].col = col;
    p[i].kind = 1;
    p[i].op = OC_FILTER_EQUAL;
    p[i].value = empty;
    p[i].has_second = p[i].use_or = 0;
    p[i].op2 = OC_FILTER_EQUAL;
    p[i].value2 = NULL;
    p[i].rank = rank;
    p[i].bottom = !!bottom;
    p[i].percent = !!percent;
    p[i].threshold_valid = 0;
    return OWF_OK;
}

static int apply_rules(oc_core *core, owf_sheet *sheet, filter_rule *rules, int nrules)
{
    oc_undo *u;
    char *xml = NULL;
    unsigned row, first = sheet->autofilter.first_row + 1, last = sheet->autofilter.last_row;
    unsigned count = last >= first ? last - first + 1 : 0;
    unsigned char *hidden = NULL;
    int rc = OWF_OK;
    if (count > OC_FILTER_ROWS)
        return OWF_ERR_UNSUPPORTED;
    if (nrules && !(xml = rules_xml(sheet, rules, nrules)))
        return OWF_ERR_MEMORY;
    if (nrules && count && !(hidden = malloc(count))) {
        free(xml);
        return OWF_ERR_MEMORY;
    }
    if ((rc = prepare_rules(core, rules, nrules)) != OWF_OK) {
        free(hidden);
        free(xml);
        return rc;
    }
    for (row = first; nrules && row <= last; row++)
        hidden[row - first] = (unsigned char)!row_matches(core, row, rules, nrules, -1);
    u = filter_step(core);
    if (!u) {
        free(hidden);
        free(xml);
        return OWF_ERR_MEMORY;
    }
    {
        int exact = mark_filter_rows(core, sheet);
        show_filter_rows(sheet, first, last, exact);
    }
    free(sheet->autofilter_xml);
    sheet->autofilter_xml = xml;
    for (row = first; nrules && row <= last; row++)
        if (hidden[row - first]) {
            const owf_rowinfo *info = row_info(sheet, row);
            int height = info && info->height > 0 ? info->height : sheet->default_height;
            int manual = info ? info->hidden & 1 : 0;
            if (owf_sheet_set_row(sheet, row, height, manual | 2) < 0) {
                rc = OWF_ERR_MEMORY;
                break;
            }
        }
    free(hidden);
    oc_undo_push(core, u);
    oc_changed(core);
    if (rc != OWF_OK)
        oc_undo_take_back(core);
    return rc;
}

int oc_core_filter_value(oc_core *core, unsigned col, const char *value)
{
    return oc_core_filter_condition(core, col, OC_FILTER_EQUAL, value);
}

int oc_core_filter_condition(oc_core *core, unsigned col, int op, const char *value)
{
    owf_sheet *sheet;
    filter_rule *rules = NULL;
    int n = 0, rc;
    if (!core || op < OC_FILTER_EQUAL || op > OC_FILTER_GREATER_EQUAL)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter || col < sheet->autofilter.first_col || col > sheet->autofilter.last_col)
        return OWF_ERR_UNSUPPORTED;
    rc = parse_rules(sheet, &rules, &n);
    if (rc == OWF_ERR_MEMORY)
        return rc;
    if (rc == OWF_ERR_UNSUPPORTED) {                 /* edit starts a clean exact-value set */
        rules_free(rules, n);
        rules = NULL;
        n = 0;
    }
    rc = update_rule(&rules, &n, col, op, value);
    if (rc == OWF_OK)
        rc = apply_rules(core, sheet, rules, n);
    rules_free(rules, n);
    return rc;
}

int oc_core_filter_conditions(oc_core *core, unsigned col,
                              int op1, const char *value1, int op2, const char *value2, int use_or)
{
    owf_sheet *sheet;
    filter_rule *rules = NULL;
    int n = 0, rc;
    if (!core || !value1 || !value2 || op1 < OC_FILTER_EQUAL || op1 > OC_FILTER_GREATER_EQUAL ||
        op2 < OC_FILTER_EQUAL || op2 > OC_FILTER_GREATER_EQUAL)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter || col < sheet->autofilter.first_col || col > sheet->autofilter.last_col)
        return OWF_ERR_UNSUPPORTED;
    rc = parse_rules(sheet, &rules, &n);
    if (rc == OWF_ERR_MEMORY)
        return rc;
    if (rc == OWF_ERR_UNSUPPORTED) {
        rules_free(rules, n);
        rules = NULL;
        n = 0;
    }
    rc = update_rule(&rules, &n, col, op1, value1);
    if (rc == OWF_OK)
        rc = add_second_rule(rules, n, col, op2, value2, use_or);
    if (rc == OWF_OK)
        rc = apply_rules(core, sheet, rules, n);
    rules_free(rules, n);
    return rc;
}

int oc_core_filter_top(oc_core *core, unsigned col, unsigned count, int bottom, int percent)
{
    owf_sheet *sheet;
    filter_rule *rules = NULL;
    int n = 0, rc;
    if (!core || !count || count > (percent ? 100U : 500U))
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter || col < sheet->autofilter.first_col || col > sheet->autofilter.last_col)
        return OWF_ERR_UNSUPPORTED;
    rc = parse_rules(sheet, &rules, &n);
    if (rc == OWF_ERR_MEMORY)
        return rc;
    if (rc == OWF_ERR_UNSUPPORTED) {
        rules_free(rules, n);
        rules = NULL;
        n = 0;
    }
    rc = update_top_rule(&rules, &n, col, count, bottom, percent);
    if (rc == OWF_OK)
        rc = apply_rules(core, sheet, rules, n);
    rules_free(rules, n);
    return rc;
}

int oc_core_filter_clear(oc_core *core)
{
    owf_sheet *sheet;
    if (!core)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter)
        return OWF_ERR_UNSUPPORTED;
    if (!sheet->autofilter_xml)
        return OWF_OK;
    return apply_rules(core, sheet, NULL, 0);
}

/* ---- Advanced filter: a criteria range ---- */

typedef struct {
    unsigned col;                 /* the list's column it tests */
    int op;                       /* OC_FILTER_* */
    char *value;
} adv_test;

static const char *shown_on(const oc_core *core, int sheet, unsigned row, unsigned col, char *buf, size_t size)
{
    const owf_cell *cell = oc_find(core, sheet, row, col);
    if (cell && cell->type == OWF_VALUE_TEXT)
        return cell->text ? cell->text : "";
    oc_core_cell_text(core, sheet, row, col, buf, size);
    buf[strcspn(buf, "\r\n")] = 0;
    return buf;
}

/* One criteria cell as a test: "<10", ">=2026", "<>East", "=East" (just
 * that), "East" (begins with it, as Excel reads it), "10" (equal). 0 for an
 * empty cell, -1 for memory. */
static int adv_parse(const char *text, unsigned col, adv_test *t)
{
    static const struct { const char *s; int op; } ops[] = {
        { "<=", OC_FILTER_LESS_EQUAL }, { ">=", OC_FILTER_GREATER_EQUAL }, { "<>", OC_FILTER_NOT_EQUAL },
        { "<", OC_FILTER_LESS }, { ">", OC_FILTER_GREATER }, { "=", OC_FILTER_EQUAL }
    };
    unsigned i;
    double number;
    size_t n;
    if (!*text)
        return 0;
    t->col = col;
    for (i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        n = strlen(ops[i].s);
        if (!strncmp(text, ops[i].s, n)) {
            t->op = ops[i].op;
            t->value = oc_strdup(text + n);
            return t->value ? 1 : -1;
        }
    }
    t->op = OC_FILTER_EQUAL;
    if (number_text(text, &number)) {
        t->value = oc_strdup(text);
        return t->value ? 1 : -1;
    }
    n = strlen(text);
    t->value = owf_malloc(n + 2);
    if (!t->value)
        return -1;
    memcpy(t->value, text, n);
    t->value[n] = '*';
    t->value[n + 1] = 0;
    return 1;
}

static int adv_row_matches(const oc_core *core, int sheet, unsigned row, const adv_test *tests,
                           const unsigned *row_start, unsigned nrows)
{
    unsigned r, k;
    char shown[256];
    if (!nrows)
        return 1;
    for (r = 0; r < nrows; r++) {                        /* criteria rows: any of them */
        int all = 1;
        for (k = row_start[r]; k < row_start[r + 1] && all; k++)   /* a row's cells: all of them */
            all = compare_one(shown_on(core, sheet, row, tests[k].col, shown, sizeof shown), tests[k].op,
                              tests[k].value);
        if (all)
            return 1;
    }
    return 0;
}

/* A row's shown values joined, for Unique records only. */
static char *adv_row_key(const oc_core *core, int sheet, unsigned row, const owf_range *list)
{
    char shown[256], *key = NULL;
    size_t at = 0, cap = 0;
    unsigned c;
    for (c = list->first_col; c <= list->last_col; c++) {
        const char *s = shown_on(core, sheet, row, c, shown, sizeof shown);
        size_t n = strlen(s);
        if (at + n + 2 > cap) {
            char *more;
            cap = (at + n + 2) * 2;
            more = realloc(key, cap);
            if (!more) {
                free(key);
                return NULL;
            }
            key = more;
        }
        memcpy(key + at, s, n);
        at += n;
        key[at++] = '\t';
        key[at] = 0;
    }
    if (key)
        for (at = 0; key[at]; at++)
            if (key[at] >= 'A' && key[at] <= 'Z')
                key[at] = (char)(key[at] + ('a' - 'A'));
    return key;
}

static int adv_key_seen(char **keys, unsigned n, const char *key)
{
    unsigned i;
    for (i = 0; i < n; i++)
        if (!strcmp(keys[i], key))
            return 1;
    return 0;
}

int oc_core_advanced_filter(oc_core *core, const char *list_text, const char *criteria_text, const char *copy_to,
                            int unique, unsigned *kept)
{
    owf_range list, crit, dest;
    int list_sheet, crit_sheet, dest_sheet = -1, rc = OWF_ERR_MEMORY;
    adv_test *tests = NULL;
    unsigned *row_start = NULL, ntests = 0, ncrit_rows = 0, row, c, out = 0, nkeys = 0;
    unsigned *head_col = NULL;
    char **keys = NULL, shown[256], shown2[256];
    owf_book *work = NULL;
    owf_calc *calc = NULL;
    if (kept)
        *kept = 0;
    if (!core || !list_text || !criteria_text)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    if (!oc_sheet_range(core->book, list_text, core->sheet, &list_sheet, &list) ||
        !oc_sheet_range(core->book, criteria_text, core->sheet, &crit_sheet, &crit) ||
        list.first_row >= list.last_row)
        return OWF_ERR_FORMAT;
    if (copy_to && *copy_to) {
        if (!oc_sheet_range(core->book, copy_to, core->sheet, &dest_sheet, &dest))
            return OWF_ERR_FORMAT;
        if (dest_sheet == list_sheet && dest.first_row <= list.last_row && dest.first_col <= list.last_col &&
            dest.first_col + (list.last_col - list.first_col) >= list.first_col)
            return OWF_ERR_FORMAT;                       /* it would write over the list */
    } else if (list_sheet != core->sheet) {
        return OWF_ERR_FORMAT;                           /* in place: on the sheet showing */
    }
    if ((unsigned long)(list.last_row - list.first_row) * (list.last_col - list.first_col + 1u) > OC_MAX_RANGE_CELLS ||
        (unsigned long)(crit.last_row - crit.first_row + 1u) * (crit.last_col - crit.first_col + 1u) > 4096u)
        return OWF_ERR_UNSUPPORTED;
    /* each criteria heading is one of the list's */
    head_col = malloc((crit.last_col - crit.first_col + 1u) * sizeof *head_col);
    tests = calloc((size_t)(crit.last_row - crit.first_row + 1u) * (crit.last_col - crit.first_col + 1u), sizeof *tests);
    row_start = malloc((crit.last_row - crit.first_row + 2u) * sizeof *row_start);
    if (!head_col || !tests || !row_start)
        goto done;
    for (c = crit.first_col; c <= crit.last_col; c++) {
        unsigned lc;
        const char *h = shown_on(core, crit_sheet, crit.first_row, c, shown, sizeof shown);
        head_col[c - crit.first_col] = ~0u;
        if (!*h)
            continue;                                    /* a column with no heading tests nothing */
        for (lc = list.first_col; lc <= list.last_col; lc++)
            if (!compare_ci(h, shown_on(core, list_sheet, list.first_row, lc, shown2, sizeof shown2))) {
                head_col[c - crit.first_col] = lc;
                break;
            }
        if (head_col[c - crit.first_col] == ~0u) {
            rc = OWF_ERR_FORMAT;
            goto done;
        }
    }
    for (row = crit.first_row + 1u; row <= crit.last_row; row++) {
        unsigned first = ntests;
        for (c = crit.first_col; c <= crit.last_col; c++) {
            int got;
            if (head_col[c - crit.first_col] == ~0u)
                continue;
            got = adv_parse(shown_on(core, crit_sheet, row, c, shown, sizeof shown), head_col[c - crit.first_col],
                            &tests[ntests]);
            if (got < 0)
                goto done;
            if (got)
                ntests++;
        }
        if (ntests == first) {                           /* an empty criteria row: every row matches */
            ncrit_rows = 0;
            break;
        }
        row_start[ncrit_rows++] = first;
    }
    row_start[ncrit_rows] = ntests;
    if (unique && !(keys = calloc(list.last_row - list.first_row, sizeof *keys)))
        goto done;
    if (dest_sheet < 0) {
        /* in place: the rows that do not match are hidden; Show Rows brings them back */
        owf_sheet *s = &core->book->sheets[list_sheet];
        oc_undo *u = oc_undo_new(core, OC_UNDO_LAYOUT);
        if (u && oc_undo_save_layout(core, u, list_sheet) != OWF_OK) {
            oc_undo_free(u);
            u = NULL;
        }
        oc_undo_push(core, u);
        rc = OWF_OK;
        for (row = list.first_row + 1u; row <= list.last_row && rc == OWF_OK; row++) {
            int keep = adv_row_matches(core, list_sheet, row, tests, row_start, ncrit_rows);
            const owf_rowinfo *ri = row_info(s, row);
            if (keep && keys) {
                char *key = adv_row_key(core, list_sheet, row, &list);
                if (!key) { rc = OWF_ERR_MEMORY; break; }
                if (adv_key_seen(keys, nkeys, key)) { free(key); keep = 0; }
                else keys[nkeys++] = key;
            }
            if (keep)
                out++;
            if (owf_sheet_set_row(s, row, ri && ri->height > 0 ? ri->height : s->default_height, keep ? 0 : 1) < 0)
                rc = OWF_ERR_MEMORY;
        }
        oc_changed(core);
        core->used_valid = 0;
        if (kept)
            *kept = out;
        goto done;
    }
    /* copied: the heading row and the rows that match, as values in their styles */
    work = oc_book_copy(core->book);
    if (!work || !(calc = owf_calc_new(work)))
        goto done;
    rc = OWF_OK;
    for (row = list.first_row; row <= list.last_row && rc == OWF_OK; row++) {
        int keep = row == list.first_row || adv_row_matches(core, list_sheet, row, tests, row_start, ncrit_rows);
        if (keep && keys && row > list.first_row) {
            char *key = adv_row_key(core, list_sheet, row, &list);
            if (!key) { rc = OWF_ERR_MEMORY; break; }
            if (adv_key_seen(keys, nkeys, key)) { free(key); keep = 0; }
            else keys[nkeys++] = key;
        }
        if (!keep)
            continue;
        if (dest.first_row + out >= OC_MAX_ROWS || dest.first_col + (list.last_col - list.first_col) >= OC_MAX_COLS) {
            rc = OWF_ERR_FORMAT;
            break;
        }
        for (c = list.first_col; c <= list.last_col && rc == OWF_OK; c++) {
            const owf_cell *from = oc_find(core, list_sheet, row, c);
            unsigned trow = dest.first_row + out, tcol = dest.first_col + (c - list.first_col);
            char *input;
            if (from && from->formula) {
                owf_cell value = *from;
                value.formula = NULL;
                input = owf_scenario_input(&value);
            } else {
                input = owf_scenario_input(from);
            }
            if (!input) { rc = OWF_ERR_MEMORY; break; }
            rc = owf_calc_enter(calc, dest_sheet, trow, tcol, input);
            owf_free(input);
            if (rc == OWF_OK && from && from->style) {
                owf_cell *to = owf_sheet_cell(&work->sheets[dest_sheet], trow, tcol, 1);
                if (to) to->style = from->style;
                else rc = OWF_ERR_MEMORY;
            }
        }
        out++;
    }
    if (rc == OWF_OK)
        rc = owf_calc_recalc(calc);
    if (rc != OWF_OK)
        goto done;
    {
        owf_book *old_book = core->book;
        owf_calc *old_calc = core->calc;
        oc_undo *undo = oc_undo_new(core, OC_UNDO_BOOK);
        if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
            oc_undo_free(undo);
            undo = NULL;
        }
        oc_cf_forget(core);
        oc_charts_forget(core);
        core->book = work;
        core->calc = calc;
        work = NULL;
        calc = NULL;
        owf_calc_free(old_calc);
        owf_book_free(old_book);
        oc_undo_push(core, undo);
        oc_changed(core);
        core->used_valid = 0;
        core->unchecked = 0;
        if (core->clip.valid)
            core->clip.cut = 0;
    }
    if (kept)
        *kept = out ? out - 1u : 0u;
done:
    owf_calc_free(calc);
    owf_book_free(work);
    if (tests)
        for (c = 0; c < ntests; c++)
            owf_free(tests[c].value);
    free(tests);
    free(row_start);
    free(head_col);
    if (keys)
        for (c = 0; c < nkeys; c++)
            free(keys[c]);
    free(keys);
    return rc;
}
