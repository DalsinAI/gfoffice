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
    int done, started, match;
    const owf_ptask *t;
    char *cell;
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
        if (!(cell = opj_cell_text_dup(doc, row, filter->column))) return 0;
        match = opj_text_matches(cell, filter->text, filter->how);
        owf_free(cell);
        return match;
    default: return 0;
    }
}

/* ---- Column names, and Sort's keys as typed ---- */

static void squash(const char *s, char *out, size_t size)
{
    size_t k = 0;
    for (; *s && k < size - 1; s++) {
        int c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[k++] = (char)c;
    }
    out[k] = 0;
}

int opj_column_parse(const char *name, int resource_sheet)
{
    static const struct { const char *alias; int column; } tasks[] = {
        { "name", OPJ_COL_NAME }, { "percent", OPJ_COL_PERCENT }, { "complete", OPJ_COL_PERCENT },
        { "resources", OPJ_COL_RESOURCES }, { "startvariance", OPJ_COL_START_VARIANCE },
        { "finishvariance", OPJ_COL_FINISH_VARIANCE },
        { "constraint", OPJ_COL_CONSTRAINT_TYPE }, { "actualstart", OPJ_COL_ACTUAL_START },
        { "actualfinish", OPJ_COL_ACTUAL_FINISH }, { "actualduration", OPJ_COL_ACTUAL_DURATION },
        { "remaining", OPJ_COL_REMAINING_DURATION }, { "remainingduration", OPJ_COL_REMAINING_DURATION }, { "earned", OPJ_COL_BCWP }, { "planned", OPJ_COL_BCWS },
        { "leveldelay", OPJ_COL_LEVELING_DELAY }, { "delay", OPJ_COL_LEVELING_DELAY },
        { "overallocation", OPJ_COL_OVERALLOCATED }, { "over", OPJ_COL_OVERALLOCATED },
    }, res[] = {
        { "maxunits", OPJ_RCOL_MAX }, { "max", OPJ_RCOL_MAX }, { "rate", OPJ_RCOL_RATE },
        { "stdrate", OPJ_RCOL_RATE }, { "name", OPJ_RCOL_NAME }, { "calendar", OPJ_RCOL_CALENDAR },
    };
    char want[40], have[40];
    int i, n = resource_sheet ? OPJ_NRCOLS : OPJ_NCOLS;
    squash(name, want, sizeof want);
    if (!want[0]) return -1;
    for (i = 0; i < n; i++) {
        squash(resource_sheet ? opj_resource_column_title(i) : opj_column_title(i), have, sizeof have);
        if (strcmp(have, want) == 0) return i;
    }
    if (resource_sheet) {
        for (i = 0; i < (int)(sizeof res / sizeof *res); i++)
            if (strcmp(res[i].alias, want) == 0) return res[i].column;
    } else
        for (i = 0; i < (int)(sizeof tasks / sizeof *tasks); i++)
            if (strcmp(tasks[i].alias, want) == 0) return tasks[i].column;
    return -1;
}

int opj_sort_parse(const char *text, opj_sort_key *keys, int *nkeys)
{
    char item[80], *word;
    const char *s = text, *comma;
    int n = 0;
    if (nkeys) *nkeys = 0;
    if (!text || !keys || !nkeys) return OWF_ERR_FORMAT;
    while (*s) {
        size_t len, k;
        int desc = 0;
        comma = strchr(s, ',');
        len = comma ? (size_t)(comma - s) : strlen(s);
        if (len >= sizeof item) return OWF_ERR_FORMAT;
        memcpy(item, s, len);
        item[len] = 0;
        s += len + (comma ? 1 : 0);
        for (k = len; k > 0 && item[k - 1] == ' '; k--) item[k - 1] = 0;   /* a trailing direction word */
        word = strrchr(item, ' ');
        if (word && (!opj_text_cmp(word + 1, "desc") || !opj_text_cmp(word + 1, "descending") ||
                     !opj_text_cmp(word + 1, "down"))) {
            desc = 1;
            *word = 0;
        } else if (word && (!opj_text_cmp(word + 1, "asc") || !opj_text_cmp(word + 1, "ascending") ||
                            !opj_text_cmp(word + 1, "up"))) {
            *word = 0;
        }
        for (word = item; *word == ' '; word++)
            ;
        if (!*word && !comma && !*s && n) break;         /* a trailing comma */
        if (n == 3 || (keys[n].column = opj_column_parse(word, 0)) < 0) return OWF_ERR_FORMAT;
        keys[n++].descending = desc;
    }
    if (!n) return OWF_ERR_FORMAT;
    *nkeys = n;
    return OWF_OK;
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
static int group_cmp(const opj_doc *doc, int column, char *const *text, int a, int b)
{
    long va, vb;
    int naa, nab;
    if (opj_cell_value(doc, a, column, &va, &naa)) {
        opj_cell_value(doc, b, column, &vb, &nab);
        if (naa != nab) return naa ? 1 : -1;
        if (!naa && va != vb) return va < vb ? -1 : 1;
    }
    return opj_text_cmp(text[a], text[b]);
}

static void free_texts(char **text, int n)
{
    int i;
    if (!text) return;
    for (i = 0; i < n; i++) owf_free(text[i]);
    free(text);
}

/* What a grouping sort compares by. */
typedef struct {
    const opj_doc *doc;
    int column, descending;
    char **text;                         /* each kept task's cell, whole */
    const int *head;                     /* each group's first task */
} group_ctx;

/* Tasks a and b by their cells' text, case ignored. */
static int by_text(const group_ctx *c, int a, int b)
{
    return opj_text_cmp(c->text[a], c->text[b]);
}

/* Groups a and b in the column's order (descending: the other way), ties by their heads' rows. */
static int by_group(const group_ctx *c, int a, int b)
{
    int d = group_cmp(c->doc, c->column, c->text, c->head[a], c->head[b]);
    if (c->descending) d = -d;
    return d ? d : c->head[a] - c->head[b];
}

/* a[0..n-1] sorted by cmp, stably (equal ones keep their order); tmp as long as a. */
static void merge_sort(int *a, int *tmp, int n, int (*cmp)(const group_ctx *, int, int), const group_ctx *c)
{
    int width, i;
    for (width = 1; width < n; width *= 2) {
        for (i = 0; i < n; i += 2 * width) {
            int lo = i, mid = i + width < n ? i + width : n, hi = i + 2 * width < n ? i + 2 * width : n;
            int x = lo, y = mid, k = lo;
            while (x < mid && y < hi) tmp[k++] = cmp(c, a[y], a[x]) < 0 ? a[y++] : a[x++];
            while (x < mid) tmp[k++] = a[x++];
            while (y < hi) tmp[k++] = a[y++];
        }
        memcpy(a, tmp, (size_t)n * sizeof *a);
    }
}

int opj_lines_update(opj_lines *lines, const opj_doc *doc)
{
    const owf_project *p;
    int nt, i, j, n = 0, ng = 0;
    int *task, *heading, *line_of, *keep, *head = NULL, *group_of = NULL;
    char **text = NULL;                  /* grouped: each kept task's cell in the group's column, whole */
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
        text = calloc((size_t)nt + 1, sizeof *text);
    }
    if (!task || !heading || !line_of || !keep || (lines->group >= 0 && (!head || !group_of || !text))) {
        free(task), free(heading), free(line_of), free(keep), free(head), free(group_of), free(text);
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
        /* the groups (Budgie's #403 nit: n log n, not n^2 on a plan of distinct values): the kept tasks that
         * aren't summaries sorted by their cell's text, stably, so each run of one text is a group in sheet
         * order headed by its first; then the groups sorted by the column's order, ties by their heads */
        int m = 0, r, g, *tmp = malloc(((size_t)nt + 1) * sizeof *tmp), *start = malloc(((size_t)nt + 2) * sizeof *start);
        int *members = group_of;
        group_ctx ctx;
        if (!tmp || !start) {
            free(tmp), free(start), free_texts(text, nt);
            free(task), free(heading), free(line_of), free(keep), free(head), free(group_of);
            return OWF_ERR_MEMORY;
        }
        for (i = 0; i < nt; i++) {
            if (!keep[i] || p->tasks[i].summary) continue;
            if (!(text[i] = opj_cell_text_dup(doc, i, lines->group))) {
                free(tmp), free(start), free_texts(text, nt);
                free(task), free(heading), free(line_of), free(keep), free(head), free(group_of);
                return OWF_ERR_MEMORY;
            }
            members[m++] = i;
        }
        ctx.doc = doc;
        ctx.column = lines->group;
        ctx.text = text;
        ctx.head = head;
        ctx.descending = lines->group_descending;
        merge_sort(members, tmp, m, by_text, &ctx);
        for (r = 0; r < m; r++)
            if (!r || opj_text_cmp(text[members[r]], text[members[r - 1]])) {
                head[ng] = members[r];
                start[ng++] = r;
            }
        start[ng] = m;
        {
            int *order = malloc(((size_t)ng + 1) * sizeof *order), *spare = malloc(((size_t)ng + 1) * sizeof *spare);
            if (!order || !spare) {
                free(order), free(spare), free(tmp), free(start), free_texts(text, nt);
                free(task), free(heading), free(line_of), free(keep), free(head), free(group_of);
                return OWF_ERR_MEMORY;
            }
            for (g = 0; g < ng; g++) order[g] = g;
            merge_sort(order, spare, ng, by_group, &ctx);
            for (j = 0; j < ng; j++) {
                g = order[j];
                task[n] = -1;
                heading[n++] = head[g];
                for (r = start[g]; r < start[g + 1]; r++) {
                    task[n] = members[r];
                    heading[n] = head[g];
                    line_of[members[r]] = n++;
                }
            }
            free(order);
            free(spare);
        }
        free(tmp);
        free(start);
    }
    free(keep);
    free(head);
    free(group_of);
    free_texts(text, nt);
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
