/*
 * owf_presource.c: GFOpenProject's resources, assignments and baseline (owf_project.h). Version 1 keeps a
 * task's duration as typed (MS Project's fixed units, not effort driven): an assignment's work is the
 * duration times its units, its cost the work (or the quantity) times the resource's rate, and a resource
 * is over-allocated when more of it is assigned at one time than it has. Nothing is levelled. A baseline
 * keeps a task's dates, duration, work and cost as scheduled when it is set. Integer arithmetic only, cut
 * so that a 32-bit long never overflows.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"

#include <stdlib.h>
#include <string.h>

#define MAX_WORK 200000000L              /* minutes, as owf_pfile.c cuts */
#define MAX_COST 2000000000L             /* hundredths */

static char *dup_text(const char *s)
{
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    if ((p = owf_malloc(n)) != NULL) memcpy(p, s, n);
    return p;
}

/* a * b / c for a, b, c >= 0, cut at limit. */
static long mul_div(long a, long b, long c, long limit)
{
    long whole, part;
    if (a <= 0 || b <= 0 || c <= 0) return 0;
    whole = a / c;
    part = a % c;
    if (whole && b > limit / whole) return limit;
    whole *= b;
    if (part && b > (limit - whole) / part + 1) return limit;           /* part * b would pass the limit */
    whole += part * b / c;
    return whole > limit ? limit : whole;
}

int owf_project_add_resource(owf_project *p, const char *name)
{
    owf_presource *r;
    if (!p) return -1;
    if (p->nresources == p->capresources) {
        int cap = p->capresources ? p->capresources * 2 : 8;
        owf_presource *grown = owf_realloc(p->resources, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        p->resources = grown;
        p->capresources = cap;
    }
    r = &p->resources[p->nresources];
    memset(r, 0, sizeof *r);
    if (name && !(r->name = dup_text(name))) return -1;
    if (p->next_resource_uid < 1) p->next_resource_uid = 1;
    r->uid = p->next_resource_uid++;
    r->type = OWF_RES_WORK;
    r->max_units = 100;
    r->calendar = -1;
    return p->nresources++;
}

void owf_project_remove_resource(owf_project *p, int resource)
{
    int i, k;
    owf_presource *r;
    if (!p || resource < 0 || resource >= p->nresources) return;
    r = &p->resources[resource];
    owf_free(r->name);
    owf_free(r->initials);
    owf_free(r->group);
    memmove(r, r + 1, (size_t)(p->nresources - resource - 1) * sizeof *r);
    p->nresources--;
    for (i = 0, k = 0; i < p->nassigns; i++) {
        owf_passign a = p->assigns[i];
        if (a.resource == resource) continue;
        if (a.resource > resource) a.resource--;
        p->assigns[k++] = a;
    }
    p->nassigns = k;
}

int owf_project_assign(owf_project *p, int task, int resource, long units)
{
    owf_passign *a;
    int i;
    if (!p || task < 0 || task >= p->ntasks || resource < 0 || resource >= p->nresources || units < 0) return -1;
    if (units > 10000000L) units = 10000000L;
    for (i = 0; i < p->nassigns; i++)
        if (p->assigns[i].task == task && p->assigns[i].resource == resource) {
            p->assigns[i].units = units;
            return i;
        }
    if (p->nassigns == p->capassigns) {
        int cap = p->capassigns ? p->capassigns * 2 : 16;
        owf_passign *grown = owf_realloc(p->assigns, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        p->assigns = grown;
        p->capassigns = cap;
    }
    a = &p->assigns[p->nassigns];
    memset(a, 0, sizeof *a);
    a->task = task;
    a->resource = resource;
    a->units = units;
    if (p->next_assign_uid < 1) p->next_assign_uid = 1;
    a->uid = p->next_assign_uid++;
    return p->nassigns++;
}

void owf_project_unassign(owf_project *p, int task, int resource)
{
    int i;
    if (!p) return;
    for (i = 0; i < p->nassigns; i++)
        if (p->assigns[i].task == task && p->assigns[i].resource == resource) {
            memmove(&p->assigns[i], &p->assigns[i + 1], (size_t)(p->nassigns - i - 1) * sizeof *p->assigns);
            p->nassigns--;
            return;
        }
}

void owf_project_remap_assignments(owf_project *p, const int *map, int nold)
{
    int i, k;
    if (!p || !map) return;
    for (i = 0, k = 0; i < p->nassigns; i++) {
        owf_passign a = p->assigns[i];
        int to = a.task >= 0 && a.task < nold ? map[a.task] : -1;
        if (to < 0) continue;
        a.task = to;
        p->assigns[k++] = a;
    }
    p->nassigns = k;
}

static const owf_pcal *task_cal(const owf_project *p, const owf_ptask *t)
{
    if (t->elapsed) return NULL;
    if (t->calendar >= 0 && t->calendar < p->ncalendars) return &p->calendars[t->calendar];
    return p->ncalendars ? &p->calendars[p->calendar] : NULL;
}

/* A task's working minutes from a to b, cut to its dates. */
static long task_minutes(const owf_project *p, const owf_ptask *t, owf_ptime a, owf_ptime b)
{
    const owf_pcal *c;
    if (a < t->early_start) a = t->early_start;
    if (b > t->early_finish) b = t->early_finish;
    if (b <= a) return 0;
    c = task_cal(p, t);
    return c ? owf_pcal_work(c, a, b) : b - a;
}

long owf_passign_work(const owf_project *p, int assign, owf_ptime a, owf_ptime b)
{
    const owf_passign *as;
    if (!p || assign < 0 || assign >= p->nassigns) return 0;
    as = &p->assigns[assign];
    if (as->task < 0 || as->task >= p->ntasks || as->resource < 0 || as->resource >= p->nresources ||
        p->resources[as->resource].type != OWF_RES_WORK)
        return 0;
    return mul_div(task_minutes(p, &p->tasks[as->task], a, b), as->units, 100, MAX_WORK);
}

long owf_presource_work(const owf_project *p, int resource, owf_ptime a, owf_ptime b)
{
    long total = 0;
    int i;
    if (!p) return 0;
    for (i = 0; i < p->nassigns; i++)
        if (p->assigns[i].resource == resource) {
            total += owf_passign_work(p, i, a, b);
            if (total > MAX_WORK) total = MAX_WORK;
        }
    return total;
}

typedef struct {
    owf_ptime at;
    long units;                          /* + at a start, - at a finish */
} event;

static int cmp_event(const void *x, const void *y)
{
    const event *a = x, *b = y;
    if (a->at != b->at) return a->at < b->at ? -1 : 1;
    return a->units < b->units ? -1 : a->units > b->units;   /* finishes before starts at the same time */
}

/* Work and cost onto a task and every summary above it. */
static void add_up(owf_project *p, int task, long work, long cost)
{
    int guard = 0;
    while (task >= 0 && task < p->ntasks && guard++ <= p->ntasks) {
        owf_ptask *t = &p->tasks[task];
        t->work = t->work + work > MAX_WORK ? MAX_WORK : t->work + work;
        t->cost = t->cost + cost > MAX_COST ? MAX_COST : t->cost + cost;
        task = t->parent;
    }
}

void owf_project_resources_update(owf_project *p)
{
    event *ev;
    int i, r;
    if (!p) return;
    for (r = 0; r < p->nresources; r++) {
        owf_presource *res = &p->resources[r];
        res->work = res->cost = res->peak = 0;
        res->overallocated = 0;
    }
    for (i = 0; i < p->ntasks; i++) p->tasks[i].work = p->tasks[i].cost = 0;
    for (i = 0; i < p->nassigns; i++) {
        owf_passign *a = &p->assigns[i];
        owf_presource *res;
        a->work = a->cost = 0;
        if (a->task < 0 || a->task >= p->ntasks || a->resource < 0 || a->resource >= p->nresources) continue;
        res = &p->resources[a->resource];
        switch (res->type) {
        case OWF_RES_WORK:
            a->work = mul_div(p->tasks[a->task].duration, a->units, 100, MAX_WORK);
            a->cost = mul_div(a->work, res->rate, 60, MAX_COST);
            break;
        case OWF_RES_MATERIAL:
            a->cost = mul_div(a->units, res->rate, 100, MAX_COST);
            break;
        default:
            a->cost = a->units;
            break;
        }
        res->work = res->work + a->work > MAX_WORK ? MAX_WORK : res->work + a->work;
        res->cost = res->cost + a->cost > MAX_COST ? MAX_COST : res->cost + a->cost;
        add_up(p, a->task, a->work, a->cost);
    }
    /* The most of each work resource assigned at once: a sweep over its assignments' starts and finishes. */
    if (!p->nassigns || !(ev = owf_malloc((size_t)p->nassigns * 2 * sizeof *ev))) return;
    for (r = 0; r < p->nresources; r++) {
        owf_presource *res = &p->resources[r];
        int n = 0;
        long now = 0;
        if (res->type != OWF_RES_WORK) continue;
        for (i = 0; i < p->nassigns; i++) {
            const owf_passign *a = &p->assigns[i];
            const owf_ptask *t;
            if (a->resource != r || a->task < 0 || a->task >= p->ntasks || !a->units) continue;
            t = &p->tasks[a->task];
            if (t->early_finish <= t->early_start) continue;          /* a milestone takes no time */
            ev[n].at = t->early_start;
            ev[n++].units = a->units;
            ev[n].at = t->early_finish;
            ev[n++].units = -a->units;
        }
        qsort(ev, (size_t)n, sizeof *ev, cmp_event);
        for (i = 0; i < n; i++) {
            now += ev[i].units;
            if (now > res->peak) res->peak = now;
        }
        res->overallocated = res->peak > res->max_units;
    }
    owf_free(ev);
}

/* ---- The baseline ---- */

static int task_range(const owf_project *p, int *first, int *count)
{
    if (!p) return 0;
    if (*count < 0) *first = 0, *count = p->ntasks;
    if (*first < 0 || *count < 1 || *first > p->ntasks - *count) return 0;
    return 1;
}

void owf_project_set_baseline(owf_project *p, int first, int count)
{
    int i;
    if (!task_range(p, &first, &count)) return;
    for (i = first; i < first + count; i++) {
        owf_ptask *t = &p->tasks[i];
        t->baselined = 1;
        t->baseline_start = t->early_start;
        t->baseline_finish = t->early_finish;
        t->baseline_duration = t->summary ? task_minutes(p, t, t->early_start, t->early_finish) : t->duration;
        t->baseline_work = t->work;
        t->baseline_cost = t->cost;
    }
}

void owf_project_clear_baseline(owf_project *p, int first, int count)
{
    int i;
    if (!task_range(p, &first, &count)) return;
    for (i = first; i < first + count; i++) {
        owf_ptask *t = &p->tasks[i];
        t->baselined = 0;
        t->baseline_start = t->baseline_finish = 0;
        t->baseline_duration = t->baseline_work = t->baseline_cost = 0;
    }
}

/* Working minutes from a to b on t's calendar, below 0 when b is before a. */
static long signed_work(const owf_project *p, const owf_ptask *t, owf_ptime a, owf_ptime b)
{
    const owf_pcal *c = task_cal(p, t);
    if (a == b) return 0;
    if (b < a) return -(c ? owf_pcal_work(c, b, a) : a - b);
    return c ? owf_pcal_work(c, a, b) : b - a;
}

long owf_ptask_variance(const owf_project *p, int task, int which)
{
    const owf_ptask *t;
    if (!p || task < 0 || task >= p->ntasks || !p->tasks[task].baselined) return 0;
    t = &p->tasks[task];
    switch (which) {
    case OWF_VAR_START: return signed_work(p, t, t->baseline_start, t->early_start);
    case OWF_VAR_FINISH: return signed_work(p, t, t->baseline_finish, t->early_finish);
    case OWF_VAR_DURATION:
        return (t->summary ? task_minutes(p, t, t->early_start, t->early_finish) : t->duration) - t->baseline_duration;
    case OWF_VAR_WORK: return t->work - t->baseline_work;
    case OWF_VAR_COST: return t->cost - t->baseline_cost;
    default: return 0;
    }
}
