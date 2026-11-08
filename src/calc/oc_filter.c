/*
 * GFOpenCalc's core: AutoFilter creation and value filtering. The file
 * model already keeps XLSX/ODS filter ranges and their XML; this turns
 * that preserved feature into an editable, undoable spreadsheet command.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OC_FILTER_CHOICES 1000
#define OC_FILTER_ROWS 65536U

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

static oc_undo *filter_step(oc_core *core)
{
    oc_undo *u = oc_undo_new(core, OC_UNDO_LAYOUT);
    if (u && oc_undo_save_layout(core, u, core->sheet) != OWF_OK) {
        oc_undo_free(u);
        u = NULL;
    }
    return u;
}

/* In the live core bit 0 is a row hidden by the user and bit 1 one hidden by
 * the current filter. File writers treat either as hidden. Exact imported
 * filters are recognised below so clearing one after a round trip can recover
 * the filter bit; OOXML itself does not distinguish a row hidden for both
 * reasons. */
static char *xml_value(const char *p, const char *end)
{
    char *out, *q;
    size_t cap;
    const char quote = p < end ? *p++ : 0;
    if (quote != '"' && quote != '\'')
        return NULL;
    cap = (size_t)(end - p) + 1;
    if (!(out = malloc(cap)))
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
    *q = 0;
    return out;
}

static int mark_filter_rows(oc_core *core, owf_sheet *sheet)
{
    const char *xml = sheet->autofilter_xml, *fc, *open, *end, *id, *fv, *at;
    char *value;
    unsigned long offset;
    unsigned col, row;
    int i;
    char shown[256];
    const char *display;
    if (!xml || !*xml)
        return 1;
    if (!(fc = strstr(xml, "<filterColumn")) || !(open = strchr(fc, '>')) ||
        !(end = strstr(open, "</filterColumn>")) || strstr(end + 15, "<filterColumn"))
        return 0;
    id = strstr(fc, "colId=");
    if (!id || id > open || (id[6] != '"' && id[6] != '\''))
        return 0;
    offset = strtoul(id + 7, NULL, 10);
    if (offset > sheet->autofilter.last_col - sheet->autofilter.first_col)
        return 0;
    fv = strstr(open, "<filter ");
    if (!fv || fv > end || !(at = strstr(fv, "val=")) || at > end || (at[4] != '"' && at[4] != '\''))
        return 0;
    {
        const char *next = strstr(fv + 1, "<filter ");
        if (next && next < end)
            return 0;
    }
    if (!(value = xml_value(at + 4, end)))
        return 0;
    col = sheet->autofilter.first_col + (unsigned)offset;
    for (i = 0; i < sheet->nrows; i++) {
        row = sheet->rows[i].row;
        if (!sheet->rows[i].hidden || row <= sheet->autofilter.first_row || row > sheet->autofilter.last_row)
            continue;
        display = shown_at(core, row, col, shown, sizeof shown);
        if (!same_text(display, value))
            sheet->rows[i].hidden = 2;
    }
    free(value);
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
    unsigned row;
    int rc = OWF_OK;
    char shown[256];
    if (items) *items = NULL;
    if (n) *n = 0;
    if (!core || !items || !n)
        return OWF_ERR_UNSUPPORTED;
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter || col < sheet->autofilter.first_col || col > sheet->autofilter.last_col ||
        sheet->autofilter.last_row - sheet->autofilter.first_row > OC_FILTER_ROWS)
        return OWF_ERR_UNSUPPORTED;
    for (row = sheet->autofilter.first_row + 1; row <= sheet->autofilter.last_row; row++) {
        const char *display = shown_at(core, row, col, shown, sizeof shown);
        rc = add_choice(items, n, display);
        if (rc != OWF_OK) {
            if (rc == OWF_ERR_UNSUPPORTED) {         /* a useful bounded list, even when there are more */
                rc = OWF_OK;
                break;
            }
            oc_choices_free(*items, *n);
            *items = NULL;
            *n = 0;
            return rc;
        }
    }
    qsort(*items, (size_t)*n, sizeof **items, compare_text);
    return rc;
}

static char *filter_xml(unsigned offset, const char *value)
{
    static const char before[] = "<filterColumn colId=\"%u\"><filters><filter val=\"";
    static const char after[] = "\"/></filters></filterColumn>";
    size_t cap = strlen(value) * 6 + sizeof before + sizeof after + 24, used;
    char *out = malloc(cap), *p;
    const unsigned char *s;
    if (!out)
        return NULL;
    used = (size_t)snprintf(out, cap, before, offset);
    p = out + used;
    for (s = (const unsigned char *)value; *s; s++) {
        const char *entity = NULL;
        if (*s == '&') entity = "&amp;";
        else if (*s == '<') entity = "&lt;";
        else if (*s == '>') entity = "&gt;";
        else if (*s == '"') entity = "&quot;";
        if (entity) {
            size_t k = strlen(entity);
            memcpy(p, entity, k);
            p += k;
        } else {
            *p++ = (char)*s;
        }
    }
    memcpy(p, after, sizeof after);
    return out;
}

int oc_core_filter_value(oc_core *core, unsigned col, const char *value)
{
    owf_sheet *sheet;
    oc_undo *u;
    char *xml = NULL;
    unsigned row, first, last, count;
    unsigned char *hidden = NULL;
    int rc = OWF_OK;
    char shown[256];
    if (!core)
        return OWF_ERR_UNSUPPORTED;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    sheet = oc_sheet(core);
    if (!sheet->has_autofilter || col < sheet->autofilter.first_col || col > sheet->autofilter.last_col)
        return OWF_ERR_UNSUPPORTED;
    first = sheet->autofilter.first_row + 1;
    last = sheet->autofilter.last_row;
    count = last >= first ? last - first + 1 : 0;
    if (count > OC_FILTER_ROWS)
        return OWF_ERR_UNSUPPORTED;
    if (value && !(xml = filter_xml(col - sheet->autofilter.first_col, value)))
        return OWF_ERR_MEMORY;
    if (value && count && !(hidden = malloc(count))) {
        free(xml);
        return OWF_ERR_MEMORY;
    }
    for (row = first; value && row <= last; row++) {
        const char *display = shown_at(core, row, col, shown, sizeof shown);
        hidden[row - first] = (unsigned char)!same_text(display, value);
    }
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
    for (row = first; value && row <= last; row++)
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
