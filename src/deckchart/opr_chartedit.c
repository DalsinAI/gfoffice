/*
 * opr_chartedit: a slide's chart edited on GFOpenCalc's core (include/opr_chartedit.h, Presentation gap 2).
 *
 * Open lays the chart's data out on a one-sheet book, "Data" (names in row 1, categories or x values down
 * column A, a series a column from B), keeps a copy of the chart on that sheet pointing at those cells and
 * hands the book to an oc_core with the chart selected. Take reads the chart back from the sheet (its look as
 * GFOpenCalc left it) and rebuilds its series and points from the cells as they are now, with no refs.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opr_chartedit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATA_SHEET "Data"

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static void col_name(unsigned col, char *out)
{
    char tmp[8];
    int n = 0, i;
    col++;
    while (col && n < 7) {
        tmp[n++] = (char)('A' + (col - 1) % 26);
        col = (col - 1) / 26;
    }
    for (i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

/* "'Data'!$B$1", or "'Data'!$B$2:$B$4" when last > first (rows from 0). */
static char *ref(unsigned col, unsigned first, unsigned last)
{
    char c[8], buf[64];
    col_name(col, c);
    if (last > first)
        snprintf(buf, sizeof buf, "'" DATA_SHEET "'!$%s$%u:$%s$%u", c, first + 1, c, last + 1);
    else
        snprintf(buf, sizeof buf, "'" DATA_SHEET "'!$%s$%u", c, first + 1);
    return dup_str(buf);
}

static int put_text(owf_sheet *sh, unsigned row, unsigned col, const char *text)
{
    owf_cell *cell;
    if (!text || !*text)
        return OWF_OK;
    cell = owf_sheet_cell(sh, row, col, 1);
    if (!cell)
        return OWF_ERR_MEMORY;
    return owf_cell_set_text(cell, text, strlen(text));
}

static int put_number(owf_sheet *sh, unsigned row, unsigned col, double number)
{
    owf_cell *cell = owf_sheet_cell(sh, row, col, 1);
    if (!cell)
        return OWF_ERR_MEMORY;
    owf_cell_set_number(cell, number);
    return OWF_OK;
}

static void free_cells(owf_chart_series *s)
{
    free(s->name_cell); s->name_cell = NULL;
    free(s->values); s->values = NULL;
    free(s->categories); s->categories = NULL;
    free(s->x_values); s->x_values = NULL;
}

/* The data on the sheet and the copy's series pointing at it. */
static int lay_out(owf_sheet *sh, owf_chart *c)
{
    int scatter = c->kind == OWF_CHART_SCATTER, i, k, rows = 0, labelled = 0;
    for (i = 0; i < c->nseries; i++)
        if (c->series[i].npoints > rows)
            rows = c->series[i].npoints;
    /* column A: each row's category (the first series that has one), or a scatter's x */
    for (k = 0; k < rows; k++)
        for (i = 0; i < c->nseries; i++) {
            const owf_chart_point *p = k < c->series[i].npoints ? &c->series[i].points[k] : NULL;
            int rc = OWF_OK;
            if (!p)
                continue;
            if (scatter && p->has_x)
                rc = put_number(sh, (unsigned)k + 1, 0, p->x);
            else if (!scatter && p->label && *p->label)
                rc = put_text(sh, (unsigned)k + 1, 0, p->label);
            else
                continue;
            if (rc != OWF_OK)
                return rc;
            labelled = 1;
            break;
        }
    for (i = 0; i < c->nseries; i++) {
        owf_chart_series *s = &c->series[i];
        unsigned col = (unsigned)i + 1;
        if (put_text(sh, 0, col, s->name) != OWF_OK)
            return OWF_ERR_MEMORY;
        for (k = 0; k < s->npoints; k++)
            if (s->points[k].has_value && put_number(sh, (unsigned)k + 1, col, s->points[k].value) != OWF_OK)
                return OWF_ERR_MEMORY;
        free_cells(s);
        s->name_cell = ref(col, 0, 0);
        if (!s->name_cell)
            return OWF_ERR_MEMORY;
        if (rows > 0) {
            s->values = ref(col, 1, (unsigned)rows);
            if (!s->values)
                return OWF_ERR_MEMORY;
            if (labelled) {
                char **to = scatter ? &s->x_values : &s->categories;
                *to = ref(0, 1, (unsigned)rows);
                if (!*to)
                    return OWF_ERR_MEMORY;
            }
        }
    }
    /* beside the data, not over it */
    c->from_col = (unsigned)c->nseries + 2;
    c->from_row = 1;
    c->to_col = c->from_col + 8;
    c->to_row = 17;
    c->from_col_off = c->from_row_off = c->to_col_off = c->to_row_off = 0;
    c->sized = 0;
    c->width = c->height = 0;
    return OWF_OK;
}

oc_core *opr_chart_edit_open(const owf_chart *chart)
{
    owf_book *book;
    owf_sheet *sh;
    owf_chart copy;
    int rc;
    if (!chart)
        return NULL;
    book = owf_book_new();
    if (!book)
        return NULL;
    sh = owf_book_add_sheet(book, DATA_SHEET);
    memset(&copy, 0, sizeof copy);
    rc = sh ? owf_chart_copy(&copy, chart) : OWF_ERR_MEMORY;
    if (rc == OWF_OK)
        rc = lay_out(sh, &copy);
    if (rc == OWF_OK)
        rc = owf_sheet_keep_charts(book, 0, &copy, 1);
    owf_chart_clear(&copy);
    if (rc != OWF_OK) {
        owf_book_free(book);
        return NULL;
    }
    {
        oc_core *core = oc_core_new(book);   /* frees the book when it cannot */
        if (core)
            oc_core_select_chart(core, 0);
        return core;
    }
}

/* The cell's text, a number written as a file writes it, or NULL when empty. */
static char *cell_text(const owf_sheet *sh, unsigned row, unsigned col, int *memory)
{
    const owf_cell *cell = owf_sheet_find(sh, row, col);
    char buf[32];
    char *t;
    if (!cell)
        return NULL;
    if (cell->type == OWF_VALUE_TEXT && cell->text && *cell->text)
        t = dup_str(cell->text);
    else if (cell->type == OWF_VALUE_NUMBER) {
        owf_number_text(cell->number, buf);
        t = dup_str(buf);
    } else
        return NULL;
    if (!t)
        *memory = 1;
    return t;
}

static int cell_number(const owf_sheet *sh, unsigned row, unsigned col, double *out)
{
    const owf_cell *cell = owf_sheet_find(sh, row, col);
    if (!cell || (cell->type != OWF_VALUE_NUMBER && cell->type != OWF_VALUE_BOOL))
        return 0;
    *out = cell->number;
    return 1;
}

/* Frees series from..nseries-1 of c in place (the array kept), as owf_chart_clear frees a series. */
static void drop_series(owf_chart *c, int from)
{
    int i, k;
    for (i = from; i < c->nseries; i++) {
        owf_chart_series *s = &c->series[i];
        free_cells(s);
        for (k = 0; k < s->npoints; k++)
            free(s->points[k].label);
        free(s->points);
        free(s->name);
        free(s->label_format);
        free(s->format);
        free(s->error_plus_range);
        free(s->error_minus_range);
        memset(s, 0, sizeof *s);
    }
    if (from < c->nseries)
        c->nseries = from;
}

static int rebuild(const owf_sheet *sh, owf_chart *c)
{
    int scatter = c->kind == OWF_CHART_SCATTER, i, k, memory = 0, cols = 0, rows = 0, next = 0;
    for (i = 0; i < sh->ncells; i++) {
        const owf_cell *cell = &sh->cells[i];
        if (cell->type == OWF_VALUE_EMPTY || (cell->type == OWF_VALUE_TEXT && (!cell->text || !*cell->text)))
            continue;
        if (cell->col >= 1 && (int)cell->col > cols)
            cols = (int)cell->col;
        if (cell->row >= 1 && (int)cell->row > rows)
            rows = (int)cell->row;
    }
    for (i = 0; i < c->nseries; i++)
        if (c->series[i].index >= next)
            next = c->series[i].index + 1;
    if (cols < c->nseries)
        drop_series(c, cols);
    if (cols > c->nseries) {
        owf_chart_series *s = realloc(c->series, (size_t)cols * sizeof *s);
        if (!s)
            return OWF_ERR_MEMORY;
        c->series = s;
        for (i = c->nseries; i < cols; i++) {
            memset(&s[i], 0, sizeof s[i]);
            s[i].index = next++;
            s[i].line = 1;
        }
        c->nseries = cols;
    }
    for (i = 0; i < c->nseries; i++) {
        owf_chart_series *s = &c->series[i];
        unsigned col = (unsigned)i + 1;
        free_cells(s);
        free(s->name);
        s->name = cell_text(sh, 0, col, &memory);
        for (k = 0; k < s->npoints; k++)
            free(s->points[k].label);
        free(s->points);
        s->points = NULL;
        s->npoints = 0;
        if (rows > 0) {
            s->points = calloc((size_t)rows, sizeof *s->points);
            if (!s->points)
                return OWF_ERR_MEMORY;
            s->npoints = rows;
        }
        for (k = 0; k < rows; k++) {
            owf_chart_point *p = &s->points[k];
            p->has_value = cell_number(sh, (unsigned)k + 1, col, &p->value);
            if (scatter)
                p->has_x = cell_number(sh, (unsigned)k + 1, 0, &p->x);
            else
                p->label = cell_text(sh, (unsigned)k + 1, 0, &memory);
        }
        if (memory)
            return OWF_ERR_MEMORY;
    }
    c->from_col = c->from_row = c->to_col = c->to_row = 0;
    c->from_col_off = c->from_row_off = c->to_col_off = c->to_row_off = 0;
    c->sized = 0;
    c->width = c->height = 0;
    return OWF_OK;
}

int opr_chart_edit_take(oc_core *core, owf_chart *out)
{
    const owf_book *book = core ? oc_core_book(core) : NULL;
    owf_chart *charts = NULL;
    int n = 0, rc;
    if (!out)
        return OWF_ERR_FORMAT;
    memset(out, 0, sizeof *out);
    if (!book || book->nsheets < 1)
        return OWF_ERR_FORMAT;
    rc = owf_sheet_charts(book, 0, &charts, &n);
    if (rc != OWF_OK)
        return rc;
    if (n < 1) {
        owf_charts_free(charts, n);
        return OWF_ERR_FORMAT;
    }
    *out = charts[0];                         /* taken over: chart 0 left holding nothing */
    memset(&charts[0], 0, sizeof charts[0]);
    owf_charts_free(charts, n);
    rc = rebuild(&book->sheets[0], out);
    if (rc != OWF_OK)
        owf_chart_clear(out);
    return rc;
}

/* ---- The window's choices and keys ---- */

const char *const opr_chart_kind_labels[OPR_CHART_KINDS + 1] = { "Column", "Bar", "Line", "Area", "Pie", "Scatter",
                                                                  NULL };
const char *const opr_chart_grouping_labels[OPR_CHART_GROUPINGS + 1] = { "Side by side", "Stacked", "100% stacked",
                                                                          NULL };
const char *const opr_chart_legend_labels[OPR_CHART_LEGENDS + 1] = { "No legend", "Right", "Left", "Top", "Bottom",
                                                                      NULL };
static const int kinds[OPR_CHART_KINDS] = { OWF_CHART_COLUMN, OWF_CHART_BAR, OWF_CHART_LINE, OWF_CHART_AREA,
                                            OWF_CHART_PIE, OWF_CHART_SCATTER };
static const char legends[OPR_CHART_LEGENDS] = { 0, 'r', 'l', 't', 'b' };

int opr_chart_kind_choice(int kind)
{
    int i;
    for (i = 0; i < OPR_CHART_KINDS; i++)
        if (kinds[i] == kind)
            return i;
    return 0;
}

int opr_chart_choice_kind(int choice)
{
    return choice >= 0 && choice < OPR_CHART_KINDS ? kinds[choice] : OWF_CHART_COLUMN;
}

int opr_chart_legend_choice(int legend)
{
    int i;
    for (i = 1; i < OPR_CHART_LEGENDS; i++)
        if (legends[i] == legend)
            return i;
    return 0;
}

int opr_chart_choice_legend(int choice)
{
    return choice > 0 && choice < OPR_CHART_LEGENDS ? legends[choice] : 0;
}

int opr_chart_edit_char(oc_core *core, unsigned char c)
{
    char utf8[2];
    if (!core)
        return OWF_ERR_UNSUPPORTED;
    switch (c) {
    case 8: return oc_core_key(core, OC_KEY_BACKSPACE, 0);
    case 9: return oc_core_key(core, OC_KEY_TAB, 0);
    case 13: return oc_core_key(core, OC_KEY_RETURN, 0);
    case 27: return oc_core_key(core, OC_KEY_ESCAPE, 0);
    case 127: return oc_core_key(core, OC_KEY_DELETE, 0);
    }
    if (c < 32 || (c >= 128 && c < 160))
        return OWF_ERR_UNSUPPORTED;
    if (c < 128) {
        utf8[0] = (char)c;
        return oc_edit_type(core, utf8, 1);
    }
    utf8[0] = (char)(0xC0 | (c >> 6));
    utf8[1] = (char)(0x80 | (c & 0x3F));
    return oc_edit_type(core, utf8, 2);
}
