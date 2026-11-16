/*
 * opj_filter.c: GFOpenProject's filters and groups, MS Project's Project > Filtered For (All Tasks,
 * Critical, Milestones, Incomplete, Completed, Summary and Unstarted Tasks), the AutoFilter on a column, and
 * Project > Group By on a column. They work out which lines a view shows: the tasks a filter keeps, with
 * the summaries above them when asked, or, grouped, the tasks under a heading for each value of a column.
 * No Amiga headers.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openproject_core.h"

static const char *const filter_names[OPJ_NFILTERS] = {
    "All Tasks", "Critical", "Milestones", "Incomplete Tasks", "Completed Tasks", "Summary Tasks",
    "Unstarted Tasks", "AutoFilter"
};

const char *opj_filter_name(int kind)
{
    return kind >= 0 && kind < OPJ_NFILTERS ? filter_names[kind] : "";
}

int opj_filter_parse(const char *name)
{
    static const struct { const char *name; int kind; } more[] = {
        { "all", OPJ_FILTER_ALL }, { "incomplete", OPJ_FILTER_INCOMPLETE }, { "completed", OPJ_FILTER_COMPLETED },
        { "summary", OPJ_FILTER_SUMMARY }, { "summaries", OPJ_FILTER_SUMMARY }, { "unstarted", OPJ_FILTER_UNSTARTED },
        { "milestone", OPJ_FILTER_MILESTONES }, { "cell", OPJ_FILTER_CELL }
    };
    size_t i;
    if (!name) return -1;
    for (i = 0; i < OPJ_NFILTERS; i++)
        if (!opj_text_cmp(name, filter_names[i])) return (int)i;
    for (i = 0; i < sizeof more / sizeof more[0]; i++)
        if (!opj_text_cmp(name, more[i].name)) return more[i].kind;
    return -1;
}

/* Whether row is done and whether it has started: a summary's from its subtasks (all done; any started),
 * as MS Project rolls them up, since its own % Complete isn't kept up. */
static void progress(const owf_project *p, int row, int *done, int *started)
{
    int j;
    const owf_ptask *t = &p->tasks[row];
    *done = t->percent >= 100;
    *started = t->percent > 0 || t->has_actual_start;
    if (!t->summary) return;
    *done = 1;
    *started = 0;
    for (j = row + 1; j < p->ntasks && p->tasks[j].level > t->level; j++) {
        if (p->tasks[j].summary) continue;
        if (p->tasks[j].percent < 100) *done = 0;
        if (p->tasks[j].percent > 0 || p->tasks[j].has_actual_start) *started = 1;
    }
}

int opj_filter_passes(const opj_doc *doc, const opj_filter *filter, int row)
{
    int done, started;
    const owf_ptask *t;
    char cell[400];
    if (!doc || !doc->plan || row < 0 || row >= doc->plan->ntasks) return 0;
    if (!filter) return 1;
    t = &doc->plan->tasks[row];
    switch (filter->kind) {
    case OPJ_FILTER_ALL: return 1;
    case OPJ_FILTER_CRITICAL: return t->critical;
    case OPJ_FILTER_MILESTONES: return t->milestone && !t->summary;
    case OPJ_FILTER_INCOMPLETE: progress(doc->plan, row, &done, &started); return !done;
    case OPJ_FILTER_COMPLETED: progress(doc->plan, row, &done, &started); return done;
    case OPJ_FILTER_SUMMARY: return t->summary;
    case OPJ_FILTER_UNSTARTED: progress(doc->plan, row, &done, &started); return !started;
    case OPJ_FILTER_CELL:
        if (filter->column < 0 || filter->column >= OPJ_NCOLS) return 0;
        if (!filter->text[0]) return 1;                  /* the AutoFilter's "(All)" */
        opj_cell_text(doc, row, filter->column, cell, sizeof cell);
        return opj_text_matches(cell, filter->text, filter->how);
    default: return 0;
    }
}

/* ---- Lines ---- */

void opj_lines_init(opj_lines *lines)
{
    if (!lines) return;
    memset(lines, 0, sizeof *lines);
    lines->group = -1;
}

void opj_lines_free(opj_lines *lines)
{
    if (!lines) return;
    free(lines->task);
    free(lines->heading);
    free(lines->line_of);
    lines->task = lines->heading = lines->line_of = NULL;
    lines->n = lines->ntasks = 0;
}

int opj_lines_active(const opj_lines *lines)
{
    return lines && (lines->filter.kind != OPJ_FILTER_ALL || lines->group >= 0);
}

/* Group heads a and b (task rows) in the group column's order: below 0 when a's group goes first. */
static int group_cmp(const opj_doc *doc, int column, int a, int b)
{
    long va, vb;
    int naa, nab;
    char ta[400], tb[400];
    if (opj_cell_value(doc, a, column, &va, &naa)) {
        opj_cell_value(doc, b, column, &vb, &nab);
        if (naa != nab) return naa ? 1 : -1;
        if (!naa && va != vb) return va < vb ? -1 : 1;
    }
    opj_cell_text(doc, a, column, ta, sizeof ta);
    opj_cell_text(doc, b, column, tb, sizeof tb);
    return opj_text_cmp(ta, tb);
}

int opj_lines_update(opj_lines *lines, const opj_doc *doc)
{
    const owf_project *p;
    int nt, i, j, n = 0, ng = 0;
    int *task, *heading, *line_of, *keep, *head = NULL, *group_of = NULL;
    char a[400], b[400];
    if (!lines || !doc || !doc->plan) return OWF_ERR_FORMAT;
    if (lines->filter.kind < 0 || lines->filter.kind >= OPJ_NFILTERS || lines->group >= OPJ_NCOLS ||
        (lines->filter.kind == OPJ_FILTER_CELL && (lines->filter.column < 0 || lines->filter.column >= OPJ_NCOLS)))
        return OWF_ERR_FORMAT;
    p = doc->plan;
    nt = p->ntasks;
    task = malloc(((size_t)nt * 2 + 1) * sizeof *task);
    heading = malloc(((size_t)nt * 2 + 1) * sizeof *heading);
    line_of = malloc(((size_t)nt + 1) * sizeof *line_of);
    keep = calloc((size_t)nt + 1, sizeof *keep);
    if (lines->group >= 0) {
        head = malloc(((size_t)nt + 1) * sizeof *head);
        group_of = malloc(((size_t)nt + 1) * sizeof *group_of);
    }
    if (!task || !heading || !line_of || !keep || (lines->group >= 0 && (!head || !group_of))) {
        free(task), free(heading), free(line_of), free(keep), free(head), free(group_of);
        return OWF_ERR_MEMORY;
    }
    for (i = 0; i < nt; i++) {
        line_of[i] = -1;
        keep[i] = opj_filter_passes(doc, &lines->filter, i);
    }
    if (lines->group < 0) {
        if (lines->filter.summaries)              /* the summaries above every task kept */
            for (i = 0; i < nt; i++) {
                int up = p->tasks[i].level, k;
                if (keep[i] != 1) continue;
                for (k = i - 1; k >= 0 && up > 1; k--)
                    if (p->tasks[k].level < up) {   /* a summary above i: done when one above is kept, */
                        if (keep[k]) break;          /* as rows before i have had theirs kept already */
                        up = p->tasks[k].level;
                        keep[k] = 2;
                    }
            }
        for (i = 0; i < nt; i++)
            if (keep[i]) {
                task[n] = i;
                heading[n] = -1;
                line_of[i] = n++;
            }
    } else {
        /* the groups: each kept task that isn't a summary joins the first group whose head's cell shows the
         * same text, or heads a new one */
        for (i = 0; i < nt; i++) {
            if (!keep[i] || p->tasks[i].summary) {
                group_of[i] = -1;
                continue;
            }
            opj_cell_text(doc, i, lines->group, a, sizeof a);
            for (j = 0; j < ng; j++) {
                opj_cell_text(doc, head[j], lines->group, b, sizeof b);
                if (!opj_text_cmp(a, b)) break;
            }
            if (j == ng) head[ng++] = i;
            group_of[i] = head[j];
        }
        for (i = 1; i < ng; i++) {                /* the heads in the column's order; ties keep theirs */
            int h = head[i], d;
            for (j = i; j > 0; j--) {
                d = group_cmp(doc, lines->group, head[j - 1], h);
                if (lines->group_descending ? d >= 0 : d <= 0) break;
                head[j] = head[j - 1];
            }
            head[j] = h;
        }
        for (j = 0; j < ng; j++) {
            task[n] = -1;
            heading[n++] = head[j];
            for (i = 0; i < nt; i++)
                if (group_of[i] == head[j]) {
                    task[n] = i;
                    heading[n] = head[j];
                    line_of[i] = n++;
                }
        }
    }
    free(keep);
    free(head);
    free(group_of);
    free(lines->task);
    free(lines->heading);
    free(lines->line_of);
    lines->task = task;
    lines->heading = heading;
    lines->line_of = line_of;
    lines->n = n;
    lines->ntasks = nt;
    return OWF_OK;
}

void opj_lines_heading_text(const opj_lines *lines, const opj_doc *doc, int line, char *out, size_t size)
{
    char value[256];
    if (!out || !size) return;
    out[0] = 0;
    if (!lines || !doc || line < 0 || line >= lines->n || lines->task[line] >= 0 || lines->group < 0) return;
    if (lines->heading[line] < 0 || lines->heading[line] >= doc->plan->ntasks) return;
    opj_cell_text(doc, lines->heading[line], lines->group, value, sizeof value);
    snprintf(out, size, "%s: %s", opj_column_title(lines->group), value);
}
