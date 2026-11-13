/*
 * owf_project.c: GFOpenProject's plan model and its critical path scheduler (owf_project.h).
 * Written from the critical path method as published (forward and backward passes, total and free
 * slack) and MS Project's documented link, lag and constraint types; no other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"

#include <string.h>

static char *dup_text(const char *s)
{
    size_t n;
    char *p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    if ((p = owf_malloc(n)) != NULL) memcpy(p, s, n);
    return p;
}

owf_project *owf_project_new(owf_ptime start)
{
    owf_project *p = owf_calloc(1, sizeof *p);
    if (!p) return NULL;
    p->start = p->finish = start;
    p->minutes_per_day = 480;
    p->minutes_per_week = 2400;
    p->next_uid = 1;
    p->loop_task = -1;
    if (owf_project_add_calendar(p, "Standard") != 0) {
        owf_project_free(p);
        return NULL;
    }
    p->calendar = 0;
    return p;
}

void owf_project_free(owf_project *p)
{
    int i;
    if (!p) return;
    for (i = 0; i < p->ntasks; i++) {
        owf_free(p->tasks[i].name);
        owf_free(p->tasks[i].notes);
        owf_free(p->tasks[i].links);
    }
    for (i = 0; i < p->ncalendars; i++) owf_pcal_free(&p->calendars[i]);
    owf_free(p->tasks);
    owf_free(p->calendars);
    owf_free(p->title);
    owf_free(p);
}

int owf_project_add_task(owf_project *p, const char *name, long duration)
{
    owf_ptask *t;
    if (!p) return -1;
    if (p->ntasks == p->captasks) {
        int cap = p->captasks ? p->captasks * 2 : 16;
        owf_ptask *grown = owf_realloc(p->tasks, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        p->tasks = grown;
        p->captasks = cap;
    }
    t = &p->tasks[p->ntasks];
    memset(t, 0, sizeof *t);
    if (name && !(t->name = dup_text(name))) return -1;
    t->uid = p->next_uid++;
    t->level = 1;
    t->duration = duration < 0 ? 0 : duration;
    t->milestone = duration == 0;
    t->constraint = OWF_CON_ASAP;
    t->calendar = -1;
    t->parent = -1;
    return p->ntasks++;
}

int owf_project_add_calendar(owf_project *p, const char *name)
{
    owf_pcal *c;
    if (!p) return -1;
    if (p->ncalendars == p->capcalendars) {
        int cap = p->capcalendars ? p->capcalendars * 2 : 4;
        owf_pcal *grown = owf_realloc(p->calendars, (size_t)cap * sizeof *grown);
        if (!grown) return -1;
        p->calendars = grown;
        p->capcalendars = cap;
    }
    c = &p->calendars[p->ncalendars];
    memset(c, 0, sizeof *c);
    if (name && !(c->name = dup_text(name))) return -1;
    owf_pcal_standard(c);
    return p->ncalendars++;
}

int owf_project_link(owf_project *p, int pred, int succ, int type, long lag, int lag_percent)
{
    owf_ptask *s;
    int i;
    if (!p || pred < 0 || succ < 0 || pred >= p->ntasks || succ >= p->ntasks || pred == succ ||
        type < OWF_LINK_FS || type > OWF_LINK_SF)
        return OWF_ERR_FORMAT;
    s = &p->tasks[succ];
    for (i = 0; i < s->nlinks; i++)
        if (s->links[i].pred == pred) break;
    if (i == s->nlinks) {
        if (s->nlinks == s->caplinks) {
            int cap = s->caplinks ? s->caplinks * 2 : 4;
            owf_plink *grown = owf_realloc(s->links, (size_t)cap * sizeof *grown);
            if (!grown) return OWF_ERR_MEMORY;
            s->links = grown;
            s->caplinks = cap;
        }
        s->nlinks++;
    }
    s->links[i].pred = pred;
    s->links[i].type = type;
    s->links[i].lag = lag;
    s->links[i].lag_percent = !!lag_percent;
    return OWF_OK;
}

void owf_project_unlink(owf_project *p, int pred, int succ)
{
    owf_ptask *s;
    int i;
    if (!p || succ < 0 || succ >= p->ntasks) return;
    s = &p->tasks[succ];
    for (i = 0; i < s->nlinks; i++)
        if (s->links[i].pred == pred) {
            memmove(&s->links[i], &s->links[i + 1], (size_t)(s->nlinks - i - 1) * sizeof *s->links);
            s->nlinks--;
            return;
        }
}

/* ---- Scheduling ---- */

typedef struct {
    owf_project *p;
    int *order;                          /* leaf tasks, predecessors first */
    int norder;
    int *alap_fixed;                     /* 1: an ALAP task held at alap_start */
    owf_ptime *alap_start;
} sched;

static const owf_pcal *cal_of(const owf_project *p, const owf_ptask *t)
{
    if (t->elapsed) return NULL;
    if (t->calendar >= 0 && t->calendar < p->ncalendars) return &p->calendars[t->calendar];
    return &p->calendars[p->calendar];
}

static const owf_pcal *project_cal(const owf_project *p)
{
    return &p->calendars[p->calendar];
}

/* Time on a calendar, or every minute counting (NULL: an elapsed task). */
static owf_ptime add(const owf_pcal *c, owf_ptime t, long minutes)
{
    return c ? owf_pcal_add(c, t, minutes) : t + minutes;
}

static long work(const owf_pcal *c, owf_ptime a, owf_ptime b)
{
    return c ? owf_pcal_work(c, a, b) : b - a;
}

static int is_ancestor(const owf_project *p, int a, int t)
{
    for (t = p->tasks[t].parent; t >= 0; t = p->tasks[t].parent)
        if (t == a) return 1;
    return 0;
}

/* Parents and summaries from the outline levels. */
static void outline(owf_project *p)
{
    int i, j;
    for (i = 0; i < p->ntasks; i++) {
        owf_ptask *t = &p->tasks[i];
        if (t->level < 1) t->level = 1;
        t->parent = -1;
        for (j = i - 1; j >= 0; j--)
            if (p->tasks[j].level < t->level) {
                t->parent = j;
                break;
            }
        t->summary = i + 1 < p->ntasks && p->tasks[i + 1].level > t->level;
    }
}

/* A summary's span (or a leaf's own dates) from its leaves: early when late is 0. */
static void span(const owf_project *p, int s, int late, owf_ptime *start, owf_ptime *finish)
{
    int i, any = 0;
    if (!p->tasks[s].summary) {
        *start = late ? p->tasks[s].late_start : p->tasks[s].early_start;
        *finish = late ? p->tasks[s].late_finish : p->tasks[s].early_finish;
        return;
    }
    for (i = s + 1; i < p->ntasks && p->tasks[i].level > p->tasks[s].level; i++) {
        const owf_ptask *t = &p->tasks[i];
        owf_ptime a, b;
        if (t->summary) continue;
        a = late ? t->late_start : t->early_start;
        b = late ? t->late_finish : t->early_finish;
        if (!any || a < *start) *start = a;
        if (!any || b > *finish) *finish = b;
        any = 1;
    }
    if (!any) *start = *finish = p->start;
}

static long lag_of(const owf_project *p, const owf_plink *l)
{
    owf_ptime a, b;
    if (!l->lag_percent) return l->lag;
    span(p, l->pred, 0, &a, &b);
    return work(project_cal(p), a, b) * l->lag / 100;
}

/* Calls f(from, to, data) for every leaf-to-leaf edge: from every leaf of a link's pred (the pred itself
 * when it is a leaf) to every leaf holding that link through itself or one of its summaries. */
static void each_edge(const owf_project *p, void (*f)(int from, int to, void *data), void *data)
{
    int n = p->ntasks, j, k, li, from;
    for (j = 0; j < n; j++) {
        if (p->tasks[j].summary) continue;
        for (k = j; k >= 0; k = p->tasks[k].parent)
            for (li = 0; li < p->tasks[k].nlinks; li++) {
                int pr = p->tasks[k].links[li].pred;
                if (!p->tasks[pr].summary) {
                    f(pr, j, data);
                    continue;
                }
                for (from = pr + 1; from < n && p->tasks[from].level > p->tasks[pr].level; from++)
                    if (!p->tasks[from].summary) f(from, j, data);
            }
    }
}

typedef struct {
    int *count;                          /* edges out of each task, then where each one's list starts */
    int *to;                             /* the edges' ends, grouped by start */
    int *fill;
} edges;

static void count_edge(int from, int to, void *data)
{
    (void)to;
    ((edges *)data)->count[from + 1]++;
}

static void put_edge(int from, int to, void *data)
{
    edges *e = data;
    e->to[e->count[from] + e->fill[from]++] = to;
}

/* The leaf tasks in an order where every predecessor (through summaries) comes first: 1; 0 when the
 * links make a loop, with p->loop_task set; -1 for memory. */
static int order_leaves(sched *s)
{
    owf_project *p = s->p;
    int n = p->ntasks, i, head = 0, tail = 0, ok = -1;
    edges e;
    int *indeg = owf_calloc((size_t)n + 1, sizeof *indeg), *queue = owf_calloc((size_t)n + 1, sizeof *queue);
    memset(&e, 0, sizeof e);
    e.count = owf_calloc((size_t)n + 2, sizeof *e.count);
    e.fill = owf_calloc((size_t)n + 1, sizeof *e.fill);
    if (!indeg || !queue || !e.count || !e.fill) goto out;
    each_edge(p, count_edge, &e);
    for (i = 0; i < n; i++) e.count[i + 1] += e.count[i];
    if (!(e.to = owf_calloc((size_t)e.count[n] + 1, sizeof *e.to))) goto out;
    each_edge(p, put_edge, &e);
    for (i = 0; i < e.count[n]; i++) indeg[e.to[i]]++;
    for (i = 0; i < n; i++)
        if (!p->tasks[i].summary && !indeg[i]) queue[tail++] = i;
    while (head < tail) {
        int x = queue[head++], k;
        s->order[s->norder++] = x;
        for (k = e.count[x]; k < e.count[x + 1]; k++)
            if (--indeg[e.to[k]] == 0) queue[tail++] = e.to[k];
    }
    ok = 1;
    for (i = 0; i < n; i++)
        if (!p->tasks[i].summary && indeg[i] > 0) {
            p->loop_task = i;
            ok = 0;
            break;
        }
out:
    owf_free(indeg);
    owf_free(queue);
    owf_free(e.count);
    owf_free(e.fill);
    owf_free(e.to);
    return ok;
}

/* The earliest start one link allows a task of duration dur on calendar c. */
static owf_ptime forward_bound(const owf_project *p, const owf_plink *l, const owf_pcal *c, long dur)
{
    const owf_pcal *pc = project_cal(p);
    owf_ptime ps, pf;
    long lag = lag_of(p, l);
    span(p, l->pred, 0, &ps, &pf);
    switch (l->type) {
    case OWF_LINK_SS: return add(pc, ps, lag);
    case OWF_LINK_FF: return add(c, add(pc, pf, lag), -dur);
    case OWF_LINK_SF: return add(c, add(pc, ps, lag), -dur);
    default: return add(pc, pf, lag);
    }
}

/* The latest finish one link (from task x to succ) allows x. */
static owf_ptime backward_bound(const owf_project *p, const owf_plink *l, int succ, const owf_pcal *c, long dur)
{
    const owf_pcal *pc = project_cal(p);
    owf_ptime ss, sf;
    long lag = lag_of(p, l);
    span(p, succ, 1, &ss, &sf);
    switch (l->type) {
    case OWF_LINK_SS: return add(c, add(pc, ss, -lag), dur);
    case OWF_LINK_FF: return add(pc, sf, -lag);
    case OWF_LINK_SF: return add(c, add(pc, sf, -lag), dur);
    default: return add(pc, ss, -lag);
    }
}

static void forward(sched *s)
{
    owf_project *p = s->p;
    int oi, k, i;
    p->finish = p->start;
    for (oi = 0; oi < s->norder; oi++) {
        int x = s->order[oi];
        owf_ptask *t = &p->tasks[x];
        const owf_pcal *c = cal_of(p, t);
        long dur = t->duration;
        owf_ptime es = p->start;
        for (k = x; k >= 0; k = p->tasks[k].parent) {
            const owf_ptask *h = &p->tasks[k];
            for (i = 0; i < h->nlinks; i++) {
                owf_ptime b = forward_bound(p, &h->links[i], c, dur);
                if (b > es) es = b;
            }
            if (k != x && (h->constraint == OWF_CON_SNET || h->constraint == OWF_CON_MSO) && h->constraint_date > es)
                es = h->constraint_date;
        }
        switch (t->constraint) {
        case OWF_CON_SNET: if (t->constraint_date > es) es = t->constraint_date; break;
        case OWF_CON_FNET: { owf_ptime b = add(c, t->constraint_date, -dur); if (b > es) es = b; } break;
        case OWF_CON_MSO: es = t->constraint_date; break;
        case OWF_CON_MFO: es = add(c, t->constraint_date, -dur); break;
        case OWF_CON_SNLT: if (t->constraint_date < es) es = t->constraint_date; break;
        case OWF_CON_FNLT: { owf_ptime b = add(c, t->constraint_date, -dur); if (b < es) es = b; } break;
        default: break;
        }
        if (s->alap_fixed[x]) es = s->alap_start[x];
        if (dur > 0 && c) es = owf_pcal_next_start(c, es);
        t->early_start = es;
        t->early_finish = dur > 0 ? add(c, es, dur) : es;
        if (t->early_finish > p->finish) p->finish = t->early_finish;
    }
}

/* Steps through every link that holds back task x as a predecessor: the links whose pred is x or one of
 * x's summaries. Start with *sj 0 and *sl -1; 1 with the link at p->tasks[*sj].links[*sl], 0 at the end. */
static int next_successor(const owf_project *p, int x, int *sj, int *sl)
{
    for (; *sj < p->ntasks; (*sj)++, *sl = -1)
        while (++*sl < p->tasks[*sj].nlinks) {
            int pr = p->tasks[*sj].links[*sl].pred;
            if (pr == x || is_ancestor(p, pr, x)) return 1;
        }
    return 0;
}

static void backward(sched *s)
{
    owf_project *p = s->p;
    int oi;
    for (oi = s->norder - 1; oi >= 0; oi--) {
        int x = s->order[oi];
        owf_ptask *t = &p->tasks[x];
        const owf_pcal *c = cal_of(p, t);
        long dur = t->duration;
        owf_ptime lf = p->finish;
        int sj = 0, sl = -1;
        while (next_successor(p, x, &sj, &sl)) {
            /* a successor that is a summary waits through its leaves, all done before x */
            owf_ptime b = backward_bound(p, &p->tasks[sj].links[sl], sj, c, dur);
            if (b < lf) lf = b;
        }
        switch (t->constraint) {
        case OWF_CON_FNLT: if (t->constraint_date < lf) lf = t->constraint_date; break;
        case OWF_CON_SNLT: { owf_ptime b = add(c, t->constraint_date, dur); if (b < lf) lf = b; } break;
        case OWF_CON_MSO: lf = add(c, t->constraint_date, dur); break;
        case OWF_CON_MFO: lf = t->constraint_date; break;
        default: break;
        }
        if (dur > 0 && c) lf = owf_pcal_prev_end(c, lf);
        t->late_finish = lf;
        t->late_start = dur > 0 ? add(c, lf, -dur) : lf;
    }
}

static void slack(owf_project *p)
{
    int i, j;
    for (i = 0; i < p->ntasks; i++) {
        owf_ptask *t = &p->tasks[i];
        const owf_pcal *c = cal_of(p, t);
        long ts, fs;
        int any = 0;
        if (t->summary) continue;
        ts = work(c, t->early_start, t->late_start);
        fs = work(c, t->early_finish, t->late_finish);
        if (fs < ts) ts = fs;
        fs = 0;
        int sj = 0, sl = -1;
        while (next_successor(p, i, &sj, &sl)) {
            const owf_plink *link = &p->tasks[sj].links[sl];
            owf_ptime ss, sf, from, to;
            long lag = lag_of(p, link), f;
            span(p, sj, 0, &ss, &sf);
            switch (link->type) {
            case OWF_LINK_SS: from = add(project_cal(p), t->early_start, lag); to = ss; break;
            case OWF_LINK_FF: from = add(project_cal(p), t->early_finish, lag); to = sf; break;
            case OWF_LINK_SF: from = add(project_cal(p), t->early_start, lag); to = sf; break;
            default: from = add(project_cal(p), t->early_finish, lag); to = ss; break;
            }
            f = work(c, from, to);
            if (!any || f < fs) fs = f;
            any = 1;
        }
        if (!any) fs = work(c, t->early_finish, p->finish);
        if (fs > ts) fs = ts;
        t->total_slack = ts;
        t->free_slack = fs;
        t->critical = ts <= 0;
    }
    /* summaries: their span, the least slack of their leaves, critical with any of them */
    for (i = p->ntasks - 1; i >= 0; i--) {
        owf_ptask *t = &p->tasks[i];
        int any = 0;
        if (!t->summary) continue;
        span(p, i, 0, &t->early_start, &t->early_finish);
        span(p, i, 1, &t->late_start, &t->late_finish);
        t->critical = 0;
        for (j = i + 1; j < p->ntasks && p->tasks[j].level > t->level; j++) {
            const owf_ptask *c = &p->tasks[j];
            if (c->summary) continue;
            if (!any || c->total_slack < t->total_slack) t->total_slack = c->total_slack;
            if (!any || c->free_slack < t->free_slack) t->free_slack = c->free_slack;
            t->critical |= c->critical;
            any = 1;
        }
        t->duration = work(project_cal(p), t->early_start, t->early_finish);
    }
}

int owf_project_schedule(owf_project *p)
{
    sched s;
    int i, ok, alap = 0;
    if (!p) return OWF_ERR_FORMAT;
    p->loop_task = -1;
    outline(p);
    memset(&s, 0, sizeof s);
    s.p = p;
    s.order = owf_calloc((size_t)p->ntasks + 1, sizeof *s.order);
    s.alap_fixed = owf_calloc((size_t)p->ntasks + 1, sizeof *s.alap_fixed);
    s.alap_start = owf_calloc((size_t)p->ntasks + 1, sizeof *s.alap_start);
    if (!s.order || !s.alap_fixed || !s.alap_start) {
        ok = -1;
        goto done;
    }
    if ((ok = order_leaves(&s)) != 1) goto done;
    forward(&s);
    backward(&s);
    for (i = 0; i < p->ntasks; i++)
        if (!p->tasks[i].summary && p->tasks[i].constraint == OWF_CON_ALAP) {
            s.alap_fixed[i] = 1;
            s.alap_start[i] = p->tasks[i].late_start;
            alap = 1;
        }
    if (alap) {
        forward(&s);
        backward(&s);
    }
    slack(p);
done:
    owf_free(s.order);
    owf_free(s.alap_fixed);
    owf_free(s.alap_start);
    return ok == 1 ? OWF_OK : ok == 0 ? OWF_ERR_FORMAT : OWF_ERR_MEMORY;
}
