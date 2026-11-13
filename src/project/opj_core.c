/*
 * opj_core.c: GFOpenProject's editing core (openproject_core.h): the plan being edited, one Undo step per
 * edit, scheduling after each, and the task sheet's cells. Each edit works on a copy of the plan; when it
 * succeeds the copy becomes the plan and the old plan becomes the Undo step, so an edit that fails (or
 * would make a loop of links) leaves the plan as it was.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "openproject_core.h"

#include <stdio.h>
#include <string.h>

#define MAX_LEVEL 100

struct opj_state {
    owf_project *plan;
    long bytes;
    opj_state *next;
};

static char *dup_text(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = owf_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static void free_states(opj_state *s)
{
    while (s) {
        opj_state *next = s->next;
        owf_project_free(s->plan);
        owf_free(s);
        s = next;
    }
}

opj_doc *opj_doc_new(owf_project *plan, owf_ptime start)
{
    opj_doc *doc = owf_calloc(1, sizeof *doc);
    if (!plan) plan = owf_project_new(start);
    if (!doc || !plan) {
        owf_free(doc);
        owf_project_free(plan);
        return NULL;
    }
    doc->plan = plan;
    doc->schedule_error = owf_project_schedule(plan);
    return doc;
}

void opj_doc_free(opj_doc *doc)
{
    if (!doc) return;
    free_states(doc->undo);
    free_states(doc->redo);
    owf_project_free(doc->plan);
    owf_free(doc);
}

void opj_doc_saved(opj_doc *doc)
{
    if (doc) doc->dirty = 0;
}

int opj_can_undo(const opj_doc *doc)
{
    return doc && doc->undo;
}

int opj_can_redo(const opj_doc *doc)
{
    return doc && doc->redo;
}

static void recount(opj_doc *doc);

/* Drops the oldest Undo steps past the budget (always keeping the newest). */
static void trim(opj_doc *doc)
{
    opj_state *s = doc->undo, *keep = NULL;
    long total = 0;
    for (; s; s = s->next) {
        total += s->bytes;
        if (keep && total > OPJ_UNDO_BUDGET) break;
        keep = s;
    }
    if (s && keep) {
        keep->next = NULL;
        free_states(s);
        doc->undo_lost = 1;
        recount(doc);
    }
}

/* The edited copy becomes the plan: OWF_OK, or OWF_ERR_FORMAT (it would make a loop) with work freed. */
static int commit(opj_doc *doc, owf_project *work)
{
    opj_state *s;
    int rc = owf_project_schedule(work);
    if (rc == OWF_ERR_FORMAT && doc->schedule_error != OWF_ERR_FORMAT) {
        owf_project_free(work);
        return OWF_ERR_FORMAT;
    }
    if (rc == OWF_ERR_MEMORY || !(s = owf_malloc(sizeof *s))) {
        owf_project_free(work);
        return OWF_ERR_MEMORY;
    }
    s->plan = doc->plan;
    s->bytes = (long)owf_project_bytes(doc->plan);
    s->next = doc->undo;
    doc->undo = s;
    doc->undo_bytes += s->bytes;
    free_states(doc->redo);
    doc->redo = NULL;
    doc->plan = work;
    doc->schedule_error = rc;
    doc->changes++;
    doc->dirty = 1;
    trim(doc);
    return OWF_OK;
}

static void recount(opj_doc *doc)
{
    opj_state *k;
    doc->undo_bytes = 0;
    for (k = doc->undo; k; k = k->next) doc->undo_bytes += k->bytes;
}

/* Undo (from undo to redo) or Redo: the saved plan and the current one change places. */
static int step(opj_doc *doc, opj_state **from, opj_state **to)
{
    opj_state *s = *from;
    owf_project *was = doc->plan;
    if (!s) return OWF_ERR_FORMAT;
    *from = s->next;
    doc->plan = s->plan;
    s->plan = was;
    s->bytes = (long)owf_project_bytes(was);
    s->next = *to;
    *to = s;
    doc->schedule_error = owf_project_schedule(doc->plan);
    doc->changes++;
    doc->dirty = 1;
    recount(doc);
    trim(doc);
    return OWF_OK;
}

int opj_undo(opj_doc *doc)
{
    return doc ? step(doc, &doc->undo, &doc->redo) : OWF_ERR_FORMAT;
}

int opj_redo(opj_doc *doc)
{
    return doc ? step(doc, &doc->redo, &doc->undo) : OWF_ERR_FORMAT;
}

/* A copy to edit, or NULL (memory, or a row out of range). */
static owf_project *begin(opj_doc *doc, int row, int *rc)
{
    owf_project *work;
    *rc = OWF_ERR_FORMAT;
    if (!doc || row < 0 || row >= doc->plan->ntasks) return NULL;
    if (!(work = owf_project_copy(doc->plan))) *rc = OWF_ERR_MEMORY;
    return work;
}

static int fail(owf_project *work, int rc)
{
    owf_project_free(work);
    return rc;
}

/* Levels kept to an outline: the first at 1, none more than one below the row above. */
static void tidy_levels(owf_project *p)
{
    int i;
    for (i = 0; i < p->ntasks; i++) {
        int top = i ? p->tasks[i - 1].level + 1 : 1;
        if (p->tasks[i].level < 1) p->tasks[i].level = 1;
        if (p->tasks[i].level > top) p->tasks[i].level = top;
        if (p->tasks[i].level > MAX_LEVEL) p->tasks[i].level = MAX_LEVEL;
    }
}

/* first..*end widened to hold every subtask of each row in it. */
static int subtree_end(const owf_project *p, int first, int count)
{
    int end = first + count, i;
    for (i = first; i < end; i++) {
        int j = i + 1;
        while (j < p->ntasks && p->tasks[j].level > p->tasks[i].level) j++;
        if (j > end) end = j;
    }
    return end;
}

int opj_insert_task(opj_doc *doc, int at, const char *name)
{
    owf_project *work;
    owf_ptask t;
    int i, j, n, rc;
    if (!doc || at < 0 || at > doc->plan->ntasks) return OWF_ERR_FORMAT;
    if (!(work = owf_project_copy(doc->plan))) return OWF_ERR_MEMORY;
    n = owf_project_add_task(work, name ? name : "", work->minutes_per_day);
    if (n < 0) return fail(work, OWF_ERR_MEMORY);
    t = work->tasks[n];
    t.estimated = 1;
    t.level = at < n ? work->tasks[at].level : at > 0 ? work->tasks[at - 1].level : 1;
    memmove(&work->tasks[at + 1], &work->tasks[at], (size_t)(n - at) * sizeof t);
    work->tasks[at] = t;
    for (i = 0; i < work->ntasks; i++)
        for (j = 0; j < work->tasks[i].nlinks; j++)
            if (i != at && work->tasks[i].links[j].pred >= at) work->tasks[i].links[j].pred++;
    for (i = 0; i < work->nassigns; i++)
        if (work->assigns[i].task >= at) work->assigns[i].task++;
    tidy_levels(work);
    rc = commit(doc, work);
    return rc;
}

int opj_delete_tasks(opj_doc *doc, int first, int count)
{
    owf_project *work;
    int *map, end, i, j, k, n, rc;
    if (!doc || count < 1 || first < 0 || first + count > doc->plan->ntasks) return OWF_ERR_FORMAT;
    if (!(work = owf_project_copy(doc->plan))) return OWF_ERR_MEMORY;
    end = subtree_end(work, first, count);
    if (!(map = owf_malloc((size_t)work->ntasks * sizeof *map))) return fail(work, OWF_ERR_MEMORY);
    for (i = 0, n = 0; i < work->ntasks; i++) map[i] = i >= first && i < end ? -1 : n++;
    for (i = first; i < end; i++) {
        owf_free(work->tasks[i].name);
        owf_free(work->tasks[i].notes);
        owf_free(work->tasks[i].links);
    }
    memmove(&work->tasks[first], &work->tasks[end], (size_t)(work->ntasks - end) * sizeof *work->tasks);
    work->ntasks = n;
    for (i = 0; i < n; i++) {
        owf_ptask *t = &work->tasks[i];
        for (j = 0, k = 0; j < t->nlinks; j++) {
            int to = t->links[j].pred >= 0 && t->links[j].pred < n + (end - first) ? map[t->links[j].pred] : -1;
            if (to < 0) continue;
            t->links[k] = t->links[j];
            t->links[k++].pred = to;
        }
        t->nlinks = k;
    }
    owf_project_remap_assignments(work, map, n + (end - first));
    owf_free(map);
    tidy_levels(work);
    rc = commit(doc, work);
    return rc;
}

int opj_indent(opj_doc *doc, int first, int count, int by)
{
    owf_project *work;
    int end, i;
    if (!doc || count < 1 || first < 0 || first + count > doc->plan->ntasks || (by != 1 && by != -1))
        return OWF_ERR_FORMAT;
    if (by > 0 && (first == 0 || doc->plan->tasks[first].level > doc->plan->tasks[first - 1].level))
        return OWF_ERR_FORMAT;           /* already as deep as it can go */
    if (by < 0 && doc->plan->tasks[first].level <= 1) return OWF_ERR_FORMAT;
    if (!(work = owf_project_copy(doc->plan))) return OWF_ERR_MEMORY;
    end = subtree_end(work, first, count);
    for (i = first; i < end; i++) {
        if (by < 0 && work->tasks[i].level <= 1) return fail(work, OWF_ERR_FORMAT);
        if (by > 0 && work->tasks[i].level >= MAX_LEVEL) return fail(work, OWF_ERR_FORMAT);
        work->tasks[i].level += by;
    }
    tidy_levels(work);
    return commit(doc, work);
}

int opj_link_rows(opj_doc *doc, int first, int count)
{
    owf_project *work;
    int i, rc;
    if (!doc || count < 2 || first < 0 || first + count > doc->plan->ntasks) return OWF_ERR_FORMAT;
    if (!(work = owf_project_copy(doc->plan))) return OWF_ERR_MEMORY;
    for (i = first; i + 1 < first + count; i++)
        if ((rc = owf_project_link(work, i, i + 1, OWF_LINK_FS, 0, 0)) != OWF_OK) return fail(work, rc);
    return commit(doc, work);
}

int opj_unlink_rows(opj_doc *doc, int first, int count)
{
    owf_project *work;
    int i, j, k, last = first + count;
    if (!doc || count < 1 || first < 0 || last > doc->plan->ntasks) return OWF_ERR_FORMAT;
    if (!(work = owf_project_copy(doc->plan))) return OWF_ERR_MEMORY;
    for (i = 0; i < work->ntasks; i++) {
        owf_ptask *t = &work->tasks[i];
        for (j = 0, k = 0; j < t->nlinks; j++) {
            int pred = t->links[j].pred, gone;
            if (count == 1) gone = i == first || pred == first;
            else gone = i >= first && i < last && pred >= first && pred < last;
            if (!gone) t->links[k++] = t->links[j];
        }
        t->nlinks = k;
    }
    return commit(doc, work);
}

int opj_set_percent(opj_doc *doc, int row, int percent)
{
    int rc;
    owf_project *work = begin(doc, row, &rc);
    if (!work) return rc;
    if (percent < 0 || percent > 100) return fail(work, OWF_ERR_FORMAT);
    work->tasks[row].percent = percent;
    return commit(doc, work);
}

int opj_set_constraint(opj_doc *doc, int row, int constraint, owf_ptime date)
{
    int rc;
    owf_project *work = begin(doc, row, &rc);
    if (!work) return rc;
    if (constraint < OWF_CON_ASAP || constraint > OWF_CON_FNLT) return fail(work, OWF_ERR_FORMAT);
    work->tasks[row].constraint = constraint;
    work->tasks[row].constraint_date = constraint == OWF_CON_ASAP || constraint == OWF_CON_ALAP ? 0 : date;
    return commit(doc, work);
}

static int replace_text(char **field, const char *text)
{
    char *s = NULL;
    if (text && *text && !(s = dup_text(text))) return OWF_ERR_MEMORY;
    owf_free(*field);
    *field = s;
    return OWF_OK;
}

int opj_set_notes(opj_doc *doc, int row, const char *notes)
{
    int rc;
    owf_project *work = begin(doc, row, &rc);
    if (!work) return rc;
    if ((rc = replace_text(&work->tasks[row].notes, notes)) != OWF_OK) return fail(work, rc);
    return commit(doc, work);
}

/* ---- Dates as the sheet shows them ---- */

static const char weekday_names[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

void opj_date_text(owf_ptime t, int with_time, char *out)
{
    int y, mo, d, h, mi;
    owf_ptime_split(t, &y, &mo, &d, &h, &mi);
    if (with_time) sprintf(out, "%s %02d/%02d/%02d %02d:%02d", weekday_names[owf_ptime_weekday(t)], d, mo, y % 100, h, mi);
    else sprintf(out, "%s %02d/%02d/%02d", weekday_names[owf_ptime_weekday(t)], d, mo, y % 100);
}

static int take_int(const char **s, long *v, int *digits)
{
    const char *p = *s;
    long n = 0;
    int k = 0;
    while (*p >= '0' && *p <= '9' && k < 9) n = n * 10 + (*p++ - '0'), k++;
    if (!k || (*p >= '0' && *p <= '9')) return 0;
    *v = n;
    if (digits) *digits = k;
    *s = p;
    return 1;
}

int opj_date_parse(const char *s, int default_minute, owf_ptime *t)
{
    long a, b, c, y, mo, d, h, mi;
    int da;
    char sep;
    if (!s) return 0;
    while (*s == ' ') s++;
    while ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) s++;    /* a weekday's name */
    while (*s == ' ' || *s == ',') s++;
    if (!take_int(&s, &a, &da)) return 0;
    sep = *s;
    if (sep != '/' && sep != '-' && sep != '.') return 0;
    s++;
    if (!take_int(&s, &b, NULL) || *s != sep) return 0;
    s++;
    if (!take_int(&s, &c, NULL)) return 0;
    if (da == 4) y = a, mo = b, d = c;   /* 2026-11-16 */
    else d = a, mo = b, y = c < 100 ? 2000 + c : c;
    h = default_minute / 60;
    mi = default_minute % 60;
    while (*s == ' ' || *s == 'T') s++;
    if (*s) {
        if (!take_int(&s, &h, NULL) || *s++ != ':' || !take_int(&s, &mi, NULL)) return 0;
        if (*s == ':') {                 /* seconds, dropped */
            long sec;
            s++;
            if (!take_int(&s, &sec, NULL)) return 0;
        }
        while (*s == ' ') s++;
        if (*s) return 0;
    }
    if (y < 1900 || y > 4000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 24 || mi < 0 || mi > 59 ||
        (h == 24 && mi))
        return 0;
    *t = owf_ptime_make((int)y, (int)mo, (int)d, (int)h, (int)mi);
    return 1;
}

/* ---- The task sheet ---- */

static const char *const column_titles[OPJ_NCOLS] = {
    "ID", "Task Name", "Duration", "Start", "Finish", "Predecessors", "% Complete", "WBS",
};

const char *opj_column_title(int column)
{
    return column >= 0 && column < OPJ_NCOLS ? column_titles[column] : "";
}

void opj_outline_number(const opj_doc *doc, int row, char *out, size_t size)
{
    int count[MAX_LEVEL + 1], i, level = 0;
    size_t at = 0;
    if (!out || !size) return;
    out[0] = 0;
    if (!doc || row < 0 || row >= doc->plan->ntasks) return;
    memset(count, 0, sizeof count);
    for (i = 0; i <= row; i++) {
        level = doc->plan->tasks[i].level;
        if (level < 1) level = 1;
        if (level > MAX_LEVEL) level = MAX_LEVEL;
        count[level]++;
        memset(&count[level + 1], 0, (size_t)(MAX_LEVEL - level) * sizeof *count);
    }
    for (i = 1; i <= level && at + 12 < size; i++) at += (size_t)sprintf(out + at, i > 1 ? ".%d" : "%d", count[i]);
}

void opj_cell_text(const opj_doc *doc, int row, int column, char *out, size_t size)
{
    char text[32];
    const owf_ptask *t;
    const char *s = text;
    size_t n;
    if (!out || !size) return;
    out[0] = 0;
    if (!doc || row < 0 || row >= doc->plan->ntasks) return;
    t = &doc->plan->tasks[row];
    text[0] = 0;
    switch (column) {
    case OPJ_COL_ID: sprintf(text, "%d", row + 1); break;
    case OPJ_COL_NAME: s = t->name ? t->name : ""; break;
    case OPJ_COL_DURATION: {
        int estimated = t->estimated, i;
        if (t->summary) {                /* a summary's is estimated when one of its subtasks' is */
            estimated = 0;
            for (i = row + 1; i < doc->plan->ntasks && doc->plan->tasks[i].level > t->level; i++)
                if (!doc->plan->tasks[i].summary && doc->plan->tasks[i].estimated) estimated = 1;
        }
        owf_pduration_text(doc->plan, t->duration, t->elapsed, estimated, text);
        break;
    }
    case OPJ_COL_START: opj_date_text(t->early_start, 0, text); break;
    case OPJ_COL_FINISH: opj_date_text(t->early_finish, 0, text); break;
    case OPJ_COL_PREDECESSORS: owf_ppredecessors_text(doc->plan, row, out, size); return;
    case OPJ_COL_PERCENT: sprintf(text, "%d%%", t->percent); break;
    case OPJ_COL_WBS: opj_outline_number(doc, row, out, size); return;
    default: return;
    }
    n = strlen(s);
    if (n > size - 1) n = size - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

/* The first working minute and the last of the project calendar's Monday: a typed date's time. */
static int day_edge(const owf_project *p, int end)
{
    const owf_pday *h = &p->calendars[p->calendar].week[1];
    if (!h->nspans) return end ? 17 * 60 : 8 * 60;
    return end ? h->to[h->nspans - 1] : h->from[0];
}

int opj_set_cell(opj_doc *doc, int row, int column, const char *text)
{
    owf_project *work;
    owf_ptask *t;
    owf_ptime when;
    long v;
    int rc, el, es;
    if (!(work = begin(doc, row, &rc))) return rc;
    if (!text) text = "";
    t = &work->tasks[row];
    switch (column) {
    case OPJ_COL_NAME:
        if ((rc = replace_text(&t->name, text)) != OWF_OK) return fail(work, rc);
        if (!t->name && !(t->name = dup_text(""))) return fail(work, OWF_ERR_MEMORY);
        break;
    case OPJ_COL_DURATION:
        if (t->summary || owf_pduration_parse(work, text, &v, &el, &es) != OWF_OK || v < 0)
            return fail(work, OWF_ERR_FORMAT);
        t->duration = v;
        t->elapsed = el;
        t->estimated = es;
        t->milestone = v == 0;
        break;
    case OPJ_COL_START:
    case OPJ_COL_FINISH:
        if (t->summary || !opj_date_parse(text, day_edge(work, column == OPJ_COL_FINISH), &when))
            return fail(work, OWF_ERR_FORMAT);
        t->constraint = column == OPJ_COL_START ? OWF_CON_SNET : OWF_CON_FNET;
        t->constraint_date = when;
        break;
    case OPJ_COL_PREDECESSORS:
        if ((rc = owf_ppredecessors_parse(work, row, text)) != OWF_OK) return fail(work, rc);
        break;
    case OPJ_COL_PERCENT: {
        const char *s = text;
        while (*s == ' ') s++;
        if (!take_int(&s, &v, NULL)) return fail(work, OWF_ERR_FORMAT);
        while (*s == ' ' || *s == '%') s++;
        if (*s || v > 100) return fail(work, OWF_ERR_FORMAT);
        t->percent = (int)v;
        break;
    }
    default:
        return fail(work, OWF_ERR_FORMAT);
    }
    return commit(doc, work);
}
