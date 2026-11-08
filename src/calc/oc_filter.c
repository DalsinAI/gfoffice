/*
 * GFOpenCalc's core: AutoFilter creation, displayed-value and Standard Filters.
 * More than one column can filter a table; rows must satisfy every column.
 * The criteria are the workbook model's ordinary XLSX filterColumn XML, so
 * XLSX, ODS, the native UI and future front ends share one representation.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_FILTER_CHOICES 1000
#define OC_FILTER_ROWS 65536U

typedef struct {
    unsigned col;                 /* absolute sheet column */
    int op;                       /* OC_FILTER_* */
    char *value;                  /* displayed value, decoded UTF-8 */
} filter_rule;

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

static int compare_value(const char *shown, const filter_rule *rule)
{
    double a, b;
    int cmp;
    if (rule->op == OC_FILTER_EQUAL)
        return same_text(shown, rule->value);
    if (rule->op == OC_FILTER_NOT_EQUAL)
        return !same_text(shown, rule->value);
    if (number_text(shown, &a) && number_text(rule->value, &b))
        cmp = a < b ? -1 : a > b ? 1 : 0;
    else
        cmp = compare_ci(shown, rule->value);
    switch (rule->op) {
    case OC_FILTER_LESS: return cmp < 0;
    case OC_FILTER_LESS_EQUAL: return cmp <= 0;
    case OC_FILTER_GREATER: return cmp > 0;
    case OC_FILTER_GREATER_EQUAL: return cmp >= 0;
    default: return 0;
    }
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
    for (i = 0; i < n; i++)
        owf_free(rules[i].value);
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

/* The editable subset: one value comparison per filterColumn. Other imported
 * conditions remain safely preserved until the user edits filtering. */
static int parse_rules(const owf_sheet *sheet, filter_rule **out, int *count)
{
    const char *p = sheet->autofilter_xml;
    filter_rule *rules = NULL;
    int n = 0;
    *out = NULL;
    *count = 0;
    while (p && *p) {
        const char *fc = strstr(p, "<filterColumn"), *open, *end, *id, *digits, *tail, *fv, *at, *next, *tag_end;
        char *number_end;
        unsigned long offset;
        char quote, *value;
        int op = OC_FILTER_EQUAL;
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
        fv = strstr(open, "<filter ");
        if (!fv || fv > end) {
            fv = strstr(open, "<customFilter ");
            if (!fv || fv > end || !(tag_end = strchr(fv, '>')) || tag_end > end ||
                (op = custom_op(fv, tag_end)) < 0)
                goto unsupported;
            next = strstr(fv + 1, "<customFilter ");
        } else {
            tag_end = strchr(fv, '>');
            next = strstr(fv + 1, "<filter ");
        }
        if (!tag_end || tag_end > end || (next && next < end) ||
            !(at = strstr(fv, "val=")) || at > tag_end || (at[4] != '"' && at[4] != '\''))
            goto unsupported;
        if (!(value = xml_value(at + 4, tag_end))) {
            rules_free(rules, n);
            return OWF_ERR_MEMORY;
        }
        for (i = 0; i < n; i++)
            if (rules[i].col == sheet->autofilter.first_col + (unsigned)offset)
                break;
        if (i < n) {
            owf_free(rules[i].value);
            rules[i].op = op;
            rules[i].value = value;
        } else {
            grown = realloc(rules, (size_t)(n + 1) * sizeof *grown);
            if (!grown) {
                owf_free(value);
                rules_free(rules, n);
                return OWF_ERR_MEMORY;
            }
            rules = grown;
            rules[n].col = sheet->autofilter.first_col + (unsigned)offset;
            rules[n].op = op;
            rules[n].value = value;
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
 * exact criteria recovers filter-hidden rows after an XLSX/ODS round trip. */
static int mark_filter_rows(oc_core *core, owf_sheet *sheet)
{
    filter_rule *rules = NULL;
    int n = 0, rc = parse_rules(sheet, &rules, &n), i;
    if (rc != OWF_OK)
        return 0;
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
    static const char custom_after[] = "\"/></customFilters></filterColumn>";
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
        cap += sizeof custom_before + sizeof custom_after + 64 + escaped_size(rules[i].value);
    if (!(out = malloc(cap)))
        return NULL;
    p = out;
    for (i = 0; i < n; i++) {
        const char *after;
        int k;
        if (rules[i].op == OC_FILTER_EQUAL) {
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
        if (i + 1 < *n)
            memmove(&p[i], &p[i + 1], (size_t)(*n - i - 1) * sizeof *p);
        (*n)--;
        return OWF_OK;
    }
    if (!(copy = oc_strdup(value)))
        return OWF_ERR_MEMORY;
    if (i < *n) {
        owf_free(p[i].value);
        p[i].op = op;
        p[i].value = copy;
        return OWF_OK;
    }
    p = realloc(p, (size_t)(*n + 1) * sizeof *p);
    if (!p) {
        owf_free(copy);
        return OWF_ERR_MEMORY;
    }
    p[*n].col = col;
    p[*n].op = op;
    p[*n].value = copy;
    *rules = p;
    (*n)++;
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
