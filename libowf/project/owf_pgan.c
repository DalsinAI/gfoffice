/*
 * owf_pgan.c: reading GanttProject's .gan files (owf_project.h), from the format's published description
 * and sample files and from how other programs read them; no GanttProject code was read. A .gan is XML:
 *   <project name=...> holds <calendars> (a <default-week> with 1 for each day off, and <date year month
 *   date> holidays, month from 1), <tasks> (nested <task id name start="YYYY-MM-DD" duration=days
 *   complete=percent meeting=true|false>, with <notes> and <depend id=successor type difference=days>,
 *   type 1 start-start, 2 finish-start, 3 finish-finish, 4 start-finish), <resources> (<resource id name>
 *   with <rate name="standard" value=a day's>) and <allocations> (task-id, resource-id, load percent).
 * GanttProject has no constraints: a task keeps the start it was given unless a link moves it, so a task
 * with no predecessor that starts after the project is given Start No Earlier Than its start.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"
#include "owf_internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    long id;
    int index;
} id_map;

typedef struct {
    owf_project *p;
    id_map *tasks;
    int ntasks, cap;
    int failed;                          /* 1 memory */
} gan;

static const char *attr(const owf_xml_node *n, const char *name)
{
    const char *s = owf_xml_attr(n, "", name);
    return s ? s : "";
}

static char *dup_text(const char *s, int *failed)
{
    size_t n = strlen(s) + 1;
    char *p = owf_malloc(n);
    if (!p) *failed = 1;
    else memcpy(p, s, n);
    return p;
}

static char *text_of(const owf_xml_node *n, int *failed)
{
    const owf_xml_node *c;
    size_t total = 0;
    char *s;
    for (c = n->first; c; c = c->next)
        if (c->type == OWF_XML_TEXT) total += c->length;
    if (!(s = owf_malloc(total + 1))) {
        *failed = 1;
        return NULL;
    }
    total = 0;
    for (c = n->first; c; c = c->next)
        if (c->type == OWF_XML_TEXT) {
            memcpy(s + total, c->name, c->length);
            total += c->length;
        }
    s[total] = 0;
    return s;
}

/* A whole number, or a decimal in hundredths (scale 100); 0 when there is none. */
static int number(const char *s, int scale, long *v)
{
    long whole = 0, part = 0;
    int digits = 0, places = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') neg = 1, s++;
    for (; *s >= '0' && *s <= '9'; s++, digits++)
        if (whole < 20000000L) whole = whole * 10 + (*s - '0');
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++)
            if (places < 2) part = part * 10 + (*s - '0'), places++;
    if (!digits) return 0;
    if (scale == 100) {
        if (places == 1) part *= 10;
        whole = whole * 100 + part;
    }
    *v = neg ? -whole : whole;
    return 1;
}

/* "2026-11-16" at minute; 0 when it isn't one. */
static int iso_date(const char *s, int minute, owf_ptime *t)
{
    long y, m, d;
    char *end;
    y = strtol(s, &end, 10);
    if (*end != '-') return 0;
    m = strtol(end + 1, &end, 10);
    if (*end != '-') return 0;
    d = strtol(end + 1, &end, 10);
    if (y < 1900 || y > 4000 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    *t = owf_ptime_make((int)y, (int)m, (int)d, minute / 60, minute % 60);
    return 1;
}

static int find_task(const gan *g, long id)
{
    int i;
    for (i = 0; i < g->ntasks; i++)
        if (g->tasks[i].id == id) return g->tasks[i].index;
    return -1;
}

static void read_calendars(gan *g, const owf_xml_node *cals)
{
    static const char *const days[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
    static const unsigned short standard[4] = { 480, 720, 780, 1020 };
    owf_pcal *c = &g->p->calendars[g->p->calendar];
    const owf_xml_node *week = owf_xml_child(owf_xml_child(cals, "", "day-types"), "", "default-week"), *d;
    int i;
    if (week)
        for (i = 0; i < 7; i++) {
            const char *v = owf_xml_attr(week, "", days[i]);
            if (v) owf_pcal_set_week(c, i, *v == '1' ? 0 : 2, standard);
        }
    for (d = owf_xml_child(cals, "", "date"); d; d = owf_xml_next(d, "", "date")) {
        long y, m, day;
        const char *type = owf_xml_attr(d, "", "type");
        if (!number(attr(d, "year"), 1, &y) || !number(attr(d, "month"), 1, &m) || !number(attr(d, "date"), 1, &day) ||
            y < 1900 || y > 4000 || m < 1 || m > 12 || day < 1 || day > 31)
            continue;
        if (type && strcmp(type, "WORKING_DAY") == 0) continue;     /* only days off are kept */
        if (owf_pcal_set_exception(c, owf_ptime_make((int)y, (int)m, (int)day, 0, 0) / 1440, 0, NULL) == OWF_ERR_MEMORY)
            g->failed = 1;
    }
}

/* Tasks under parent at level, in file order; links wait for every task. */
static void read_tasks(gan *g, const owf_xml_node *parent, int level)
{
    const owf_xml_node *n;
    for (n = owf_xml_child(parent, "", "task"); n && !g->failed; n = owf_xml_next(n, "", "task")) {
        owf_ptask *t;
        const owf_xml_node *notes;
        long v;
        int at;
        if ((at = owf_project_add_task(g->p, NULL, 0)) < 0) {
            g->failed = 1;
            return;
        }
        if (g->ntasks == g->cap) {
            int cap = g->cap ? g->cap * 2 : 64;
            id_map *grown = owf_realloc(g->tasks, (size_t)cap * sizeof *grown);
            if (!grown) {
                g->failed = 1;
                return;
            }
            g->tasks = grown;
            g->cap = cap;
        }
        g->tasks[g->ntasks].id = number(attr(n, "id"), 1, &v) ? v : -1 - g->ntasks;
        g->tasks[g->ntasks++].index = at;
        t = &g->p->tasks[at];
        t->name = dup_text(attr(n, "name"), &g->failed);
        t->level = level > 100 ? 100 : level;
        if (number(attr(n, "duration"), 1, &v) && v > 0) t->duration = v > 100000L ? 100000L * g->p->minutes_per_day
                                                                                    : v * g->p->minutes_per_day;
        t->milestone = t->duration == 0 || strcmp(attr(n, "meeting"), "true") == 0;
        if (t->milestone) t->duration = 0;
        if (number(attr(n, "complete"), 1, &v)) t->percent = v < 0 ? 0 : v > 100 ? 100 : (int)v;
        iso_date(attr(n, "start"), 480, &t->early_start);
        if ((notes = owf_xml_child(n, "", "notes")) != NULL) {
            t->notes = text_of(notes, &g->failed);
            if (t->notes && !t->notes[0]) {
                owf_free(t->notes);
                t->notes = NULL;
            }
        }
        read_tasks(g, n, level + 1);
    }
}

/* Each task's <depend>s, now that every task has its index. */
static void read_links(gan *g, const owf_xml_node *parent)
{
    static const int types[5] = { OWF_LINK_FS, OWF_LINK_SS, OWF_LINK_FS, OWF_LINK_FF, OWF_LINK_SF };
    const owf_xml_node *n, *d;
    long v;
    for (n = owf_xml_child(parent, "", "task"); n && !g->failed; n = owf_xml_next(n, "", "task")) {
        int pred = number(attr(n, "id"), 1, &v) ? find_task(g, v) : -1;
        for (d = owf_xml_child(n, "", "depend"); d && pred >= 0; d = owf_xml_next(d, "", "depend")) {
            long type = 2, lag = 0;
            int succ = number(attr(d, "id"), 1, &v) ? find_task(g, v) : -1;
            if (succ < 0 || succ == pred) continue;
            number(attr(d, "type"), 1, &type);
            number(attr(d, "difference"), 1, &lag);
            if (type < 1 || type > 4) type = 2;
            if (lag > 100000L) lag = 100000L;
            if (lag < -100000L) lag = -100000L;
            if (owf_project_link(g->p, pred, succ, types[type], lag * g->p->minutes_per_day, 0) == OWF_ERR_MEMORY)
                g->failed = 1;
        }
        read_links(g, n);
    }
}

static void read_resources(gan *g, const owf_xml_node *root, id_map **res, int *nres)
{
    const owf_xml_node *n, *rt;
    long v;
    int count = 0;
    for (n = owf_xml_child(owf_xml_child(root, "", "resources"), "", "resource"); n; n = owf_xml_next(n, "", "resource"))
        count++;
    if (!count) return;
    if (!(*res = owf_malloc((size_t)count * sizeof **res))) {
        g->failed = 1;
        return;
    }
    for (n = owf_xml_child(owf_xml_child(root, "", "resources"), "", "resource"); n && !g->failed;
         n = owf_xml_next(n, "", "resource")) {
        int at = owf_project_add_resource(g->p, attr(n, "name"));
        if (at < 0) {
            g->failed = 1;
            return;
        }
        (*res)[*nres].id = number(attr(n, "id"), 1, &v) ? v : -1 - *nres;
        (*res)[(*nres)++].index = at;
        for (rt = owf_xml_child(n, "", "rate"); rt; rt = owf_xml_next(rt, "", "rate"))
            if (strcmp(attr(rt, "name"), "standard") == 0 && number(attr(rt, "value"), 100, &v) && v > 0) {
                long day = g->p->minutes_per_day > 0 ? g->p->minutes_per_day : 480;
                g->p->resources[at].rate = v / day * 60 + v % day * 60 / day;     /* a day's to an hour's */
            }
    }
}

int owf_pgan_read(const unsigned char *data, size_t length, owf_project **out)
{
    owf_xml *xml;
    owf_xml_node *root, *n;
    gan g;
    id_map *res = NULL;
    int error = 0, nres = 0, i, top;
    owf_ptime start = 0;
    if (!out) return OWF_ERR_FORMAT;
    *out = NULL;
    if (!(xml = owf_xml_parse((const char *)data, length, &error))) return error == OWF_ERR_MEMORY ? OWF_ERR_MEMORY
                                                                                                     : OWF_ERR_CORRUPT;
    root = owf_xml_root(xml);
    if (!root || strcmp(root->name, "project") != 0 || root->ns[0]) {
        owf_xml_free(xml);
        return OWF_ERR_FORMAT;
    }
    memset(&g, 0, sizeof g);
    if (!(g.p = owf_project_new(0))) {
        owf_xml_free(xml);
        return OWF_ERR_MEMORY;
    }
    if (*attr(root, "name")) g.p->title = dup_text(attr(root, "name"), &g.failed);
    read_calendars(&g, owf_xml_child(root, "", "calendars"));
    n = owf_xml_child(root, "", "tasks");
    if (n) {
        read_tasks(&g, n, 1);
        if (!g.failed) read_links(&g, n);
    }
    if (!g.failed) read_resources(&g, root, &res, &nres);
    for (n = owf_xml_child(owf_xml_child(root, "", "allocations"), "", "allocation"); n && !g.failed;
         n = owf_xml_next(n, "", "allocation")) {
        long tid, rid, load = 10000;
        int task, r = -1;
        if (!number(attr(n, "task-id"), 1, &tid) || !number(attr(n, "resource-id"), 1, &rid) ||
            (task = find_task(&g, tid)) < 0)
            continue;
        for (i = 0; i < nres; i++)
            if (res[i].id == rid) r = res[i].index;
        if (r < 0) continue;
        number(attr(n, "load"), 100, &load);
        if (owf_project_assign(g.p, task, r, load < 0 ? 0 : load / 100) < 0) g.failed = 1;
    }
    /* The project starts with its earliest task; a task with no predecessor keeps its own start. */
    for (i = 0; i < g.p->ntasks; i++)
        if (g.p->tasks[i].early_start && (!start || g.p->tasks[i].early_start < start)) start = g.p->tasks[i].early_start;
    g.p->start = start;
    for (i = 0; i < g.p->ntasks; i++) {
        owf_ptask *t = &g.p->tasks[i];
        int summary = i + 1 < g.p->ntasks && g.p->tasks[i + 1].level > t->level;
        if (!t->nlinks && !summary && t->early_start > start) {
            t->constraint = OWF_CON_SNET;
            t->constraint_date = t->early_start;
        }
    }
    for (i = 0, top = 0; i < g.p->ntasks; i++)
        if (g.p->tasks[i].uid > top) top = g.p->tasks[i].uid;
    g.p->next_uid = top + 1;
    owf_free(res);
    owf_free(g.tasks);
    owf_xml_free(xml);
    if (g.failed) {
        owf_project_free(g.p);
        return OWF_ERR_MEMORY;
    }
    *out = g.p;
    return OWF_OK;
}
