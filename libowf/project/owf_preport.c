/*
 * owf_preport.c: GFOpenProject's reports (owf_project.h), MS Project's Reports dialog: a plan's
 * summary, its critical tasks, its milestones, who does what, its overallocated resources and its cash
 * flow by week. Each is built as an owf_doc (headings, paragraphs and tables, a "Page n of m" footer),
 * as GFOpenBase's reports are, so libowf's own writers print and save it: PDF, ODT, DOCX, RTF, HTML and
 * the rest. Nothing here writes a file format of its own. Integer arithmetic only, cut so that a 32-bit
 * long never overflows.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"

#include <stdio.h>
#include <string.h>

#define MAX_WEEKS 520                    /* cash flow rows: ten years of weeks */

typedef struct {
    const owf_project *p;
    owf_doc *doc;
    int failed;
    int table, row, ncols;               /* the open table */
} rb;

static const char *const report_names[OWF_PREPORT_N] = {
    "Project Summary", "Critical Tasks", "Milestones", "Who Does What", "Overallocated Resources", "Cash Flow"
};

/* Short names ARexx takes too, squashed (lower case, no spaces). */
static const char *const short_names[OWF_PREPORT_N] = {
    "summary", "critical", "milestones", "whodoeswhat", "overallocated", "cashflow"
};

const char *owf_preport_name(int kind)
{
    return kind >= 0 && kind < OWF_PREPORT_N ? report_names[kind] : NULL;
}

static void squash(const char *s, char *out, size_t size)
{
    size_t k = 0;
    for (; *s && k + 1 < size; s++) {
        char c = *s;
        if (c == ' ' || c == '_' || c == '-') continue;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[k++] = c;
    }
    out[k] = 0;
}

int owf_preport_parse(const char *name)
{
    char s[40], full[40];
    int i;
    if (!name) return -1;
    squash(name, s, sizeof s);
    for (i = 0; i < OWF_PREPORT_N; i++) {
        squash(report_names[i], full, sizeof full);
        if (strcmp(s, full) == 0 || strcmp(s, short_names[i]) == 0) return i;
    }
    return -1;
}

/* ---- Text as the sheet shows it ---- */

static const char weekday_names[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static void date_text(owf_ptime t, char *out)
{
    int y, mo, d, h, mi;
    owf_ptime_split(t, &y, &mo, &d, &h, &mi);
    sprintf(out, "%s %02d/%02d/%02d", weekday_names[owf_ptime_weekday(t)], d, mo, y % 100);
}

/* Hundredths as "12", "12.5" or "12.25" (trim) or always two places ("12.00"). */
static void hundredths_text(long a, int trim, char *out)
{
    const char *sign = "";
    if (a < 0) sign = "-", a = -a;
    if (trim && a % 100 == 0) sprintf(out, "%s%ld", sign, a / 100);
    else if (trim && a % 10 == 0) sprintf(out, "%s%ld.%ld", sign, a / 100, a % 100 / 10);
    else sprintf(out, "%s%ld.%02ld", sign, a / 100, a % 100);
}

/* Minutes of work in hours, "16h", "2.5h". */
static void work_text(long minutes, char *out)
{
    if (minutes < 0) minutes = 0;
    hundredths_text(minutes / 60 * 100 + (minutes % 60 * 100 + 30) / 60, 1, out);
    strcat(out, "h");
}

static void money_text(long hundredths, char *out)
{
    hundredths_text(hundredths, 0, out);
}

/* An assignment's units as the Assign window types them: "50%" for work, "2.5" for material, the cost
 * for a cost resource. */
static void units_text(const owf_project *p, const owf_passign *a, char *out)
{
    int type = a->resource >= 0 && a->resource < p->nresources ? p->resources[a->resource].type : OWF_RES_WORK;
    if (type == OWF_RES_WORK) sprintf(out, "%ld%%", a->units);
    else if (type == OWF_RES_MATERIAL) hundredths_text(a->units, 1, out);
    else money_text(a->units, out);
}

static int is_summary(const owf_project *p, int task)
{
    return task + 1 < p->ntasks && p->tasks[task + 1].level > p->tasks[task].level;
}

static const owf_pcal *project_cal(const owf_project *p)
{
    return p->calendar >= 0 && p->calendar < p->ncalendars ? &p->calendars[p->calendar] : NULL;
}

/* ---- Building the document ---- */

static owf_para *add_para(rb *b, owf_story *story, int heading, owf_align align)
{
    owf_parafmt pf;
    owf_para *para;
    if (b->failed) return NULL;
    owf_parafmt_init(&pf);
    pf.heading = heading;
    pf.align = align;
    if (!(para = owf_story_add(story, &pf))) b->failed = 1;
    return para;
}

static void add_text(rb *b, owf_para *para, int bold, const char *utf8)
{
    owf_charfmt cf;
    if (!para || !utf8 || !*utf8 || b->failed) return;
    owf_charfmt_init(&cf);
    if (bold) cf.flags |= OWF_BOLD;
    if (owf_para_add_text(para, &cf, utf8, strlen(utf8))) b->failed = 1;
}

static void add_field(rb *b, owf_para *para, owf_field field)
{
    owf_charfmt cf;
    if (!para || b->failed) return;
    owf_charfmt_init(&cf);
    if (owf_para_add_special(para, &cf, OWF_RUN_FIELD, field)) b->failed = 1;
}

static void heading(rb *b, int level, const char *text)
{
    add_text(b, add_para(b, &b->doc->body, level, OWF_ALIGN_LEFT), 0, text);
}

static void line(rb *b, const char *text)
{
    add_text(b, add_para(b, &b->doc->body, 0, OWF_ALIGN_LEFT), 0, text);
}

/* A row of the open table: one cell for each column (NULL: empty). */
static void add_row(rb *b, const char *const *cells, int bold)
{
    int i;
    for (i = 0; i < b->ncols && !b->failed; i++) {
        owf_para *cell = add_para(b, &b->doc->body, 0, OWF_ALIGN_LEFT);
        if (!cell) return;
        cell->table_id = b->table;
        cell->table_row = b->row;
        cell->table_col = i;
        cell->table_cols = b->ncols;
        add_text(b, cell, bold, cells[i]);
    }
    b->row++;
}

/* A new table with a bold heading row. */
static void start_table(rb *b, const char *const *heads, int ncols)
{
    b->table++;
    b->row = 0;
    b->ncols = ncols;
    add_row(b, heads, 1);
}

/* A two-column row of a label and a value. */
static void pair(rb *b, const char *label, const char *value)
{
    const char *cells[2];
    cells[0] = label;
    cells[1] = value;
    add_row(b, cells, 0);
}

static void pair_n(rb *b, const char *label, long n)
{
    char num[24];
    sprintf(num, "%ld", n);
    pair(b, label, num);
}

/* The report's heading, the plan's title under it, the plan's title in the page header and "Page n of
 * m" in the footer. */
static void frame(rb *b, int kind)
{
    owf_para *f;
    heading(b, 1, report_names[kind]);
    if (b->p->title && *b->p->title) {
        line(b, b->p->title);
        add_text(b, add_para(b, &b->doc->header, 0, OWF_ALIGN_LEFT), 0, b->p->title);
    }
    if ((f = add_para(b, &b->doc->footer, 0, OWF_ALIGN_CENTRE))) {
        add_text(b, f, 0, "Page ");
        add_field(b, f, OWF_FIELD_PAGE);
        add_text(b, f, 0, " of ");
        add_field(b, f, OWF_FIELD_PAGES);
    }
}

/* ---- The reports ---- */

static void summary(rb *b)
{
    static const char *const heads[2] = { "", "" };
    const owf_project *p = b->p;
    const owf_pcal *cal = project_cal(p);
    char text[48];
    long work = 0, cost = 0, tasks = 0, milestones = 0, critical = 0, done = 0, started = 0, over = 0;
    int i;
    for (i = 0; i < p->ntasks; i++) {
        const owf_ptask *t = &p->tasks[i];
        if (is_summary(p, i)) continue;
        tasks++;
        work += t->work;
        cost += t->cost;
        if (t->milestone) milestones++;
        if (t->critical) critical++;
        if (t->percent >= 100) done++;
        else if (t->percent > 0 || t->has_actual_start) started++;
    }
    for (i = 0; i < p->nresources; i++)
        if (p->resources[i].overallocated) over++;
    heading(b, 2, "Dates");
    start_table(b, heads, 2);
    date_text(p->start, text);
    pair(b, "Start", text);
    date_text(p->finish, text);
    pair(b, "Finish", text);
    if (p->has_status_date) {
        date_text(p->status_date, text);
        pair(b, "Status date", text);
    }
    heading(b, 2, "Duration, work and cost");
    start_table(b, heads, 2);
    owf_pduration_text(p, cal ? owf_pcal_work(cal, p->start, p->finish) : p->finish - p->start, 0, 0, text);
    pair(b, "Duration", text);
    work_text(work, text);
    pair(b, "Work", text);
    money_text(cost, text);
    pair(b, "Cost", text);
    heading(b, 2, "Task status");
    start_table(b, heads, 2);
    pair_n(b, "Tasks", tasks);
    pair_n(b, "Not yet started", tasks - started - done);
    pair_n(b, "In progress", started);
    pair_n(b, "Completed", done);
    pair_n(b, "Milestones", milestones);
    pair_n(b, "Critical tasks", critical);
    heading(b, 2, "Resource status");
    start_table(b, heads, 2);
    pair_n(b, "Resources", p->nresources);
    pair_n(b, "Overallocated resources", over);
}

/* Tasks (not summaries) that pass keep, in a table; none says so. */
static void task_table(rb *b, int milestones)
{
    static const char *const task_heads[6] = { "ID", "Task Name", "Duration", "Start", "Finish", "Resource Names" };
    static const char *const ms_heads[5] = { "ID", "Task Name", "Finish", "Critical", "% Complete" };
    const owf_project *p = b->p;
    char id[16], dur[24], start[24], finish[24], pc[16], names[256];
    const char *cells[6];
    int i, n = 0;
    for (i = 0; i < p->ntasks && !b->failed; i++) {
        const owf_ptask *t = &p->tasks[i];
        if (is_summary(p, i) || (milestones ? !t->milestone : !t->critical)) continue;
        if (!n++) {
            if (milestones) start_table(b, ms_heads, 5);
            else start_table(b, task_heads, 6);
        }
        sprintf(id, "%d", i + 1);
        date_text(t->early_finish, finish);
        cells[0] = id;
        cells[1] = t->name;
        if (milestones) {
            sprintf(pc, "%d%%", t->percent);
            cells[2] = finish;
            cells[3] = t->critical ? "Yes" : "No";
            cells[4] = pc;
        } else {
            owf_pduration_text(p, t->duration, t->elapsed, t->estimated, dur);
            date_text(t->early_start, start);
            owf_presource_names_text(p, i, names, sizeof names);
            cells[2] = dur;
            cells[3] = start;
            cells[4] = finish;
            cells[5] = names;
        }
        add_row(b, cells, 0);
    }
    if (!n) line(b, milestones ? "The plan has no milestones." : "The plan has no critical tasks.");
}

/* One resource's assignments, a table under its name. */
static void assignments(rb *b, int resource)
{
    static const char *const heads[6] = { "ID", "Task Name", "Units", "Work", "Start", "Finish" };
    const owf_project *p = b->p;
    char id[16], units[24], work[24], start[24], finish[24];
    const char *cells[6];
    int i, n = 0;
    heading(b, 2, p->resources[resource].name);
    for (i = 0; i < p->nassigns && !b->failed; i++) {
        const owf_passign *a = &p->assigns[i];
        const owf_ptask *t;
        if (a->resource != resource || a->task < 0 || a->task >= p->ntasks) continue;
        t = &p->tasks[a->task];
        if (!n++) start_table(b, heads, 6);
        sprintf(id, "%d", a->task + 1);
        units_text(p, a, units);
        if (p->resources[resource].type == OWF_RES_WORK) work_text(a->work, work);
        else work[0] = 0;
        date_text(t->early_start, start);
        date_text(t->early_finish, finish);
        cells[0] = id;
        cells[1] = t->name;
        cells[2] = units;
        cells[3] = work;
        cells[4] = start;
        cells[5] = finish;
        add_row(b, cells, 0);
    }
    if (!n) line(b, "No assignments.");
}

static void who_does_what(rb *b)
{
    int r;
    if (!b->p->nresources) line(b, "The plan has no resources.");
    for (r = 0; r < b->p->nresources && !b->failed; r++) assignments(b, r);
}

static void overallocated(rb *b)
{
    static const char *const heads[4] = { "Resource Name", "Max Units", "Peak", "Work" };
    const owf_project *p = b->p;
    char max[24], peak[24], work[24];
    const char *cells[4];
    int r, n = 0;
    for (r = 0; r < p->nresources && !b->failed; r++) {
        const owf_presource *res = &p->resources[r];
        if (!res->overallocated) continue;
        if (!n++) start_table(b, heads, 4);
        sprintf(max, "%ld%%", res->max_units);
        sprintf(peak, "%ld%%", res->peak);
        work_text(res->work, work);
        cells[0] = res->name;
        cells[1] = max;
        cells[2] = peak;
        cells[3] = work;
        add_row(b, cells, 0);
    }
    if (!n) {
        line(b, "No resources are overallocated.");
        return;
    }
    for (r = 0; r < p->nresources && !b->failed; r++)
        if (p->resources[r].overallocated) assignments(b, r);
}

/* cost * done / total for 0 <= done <= total, with no product past a 32-bit long: total and done are
 * halved together until total fits in 46340 (the square root of 2^31), which keeps the share to within
 * a part in 46340. */
static long share(long cost, long done, long total)
{
    if (cost < 0) return -share(-cost, done, total);
    if (done <= 0) return 0;
    if (done >= total) return cost;
    while (total > 46340L) total >>= 1, done >>= 1;
    if (total <= 0) return cost;
    return cost / total * done + cost % total * done / total;
}

/* How much of task's cost is spent before t: in proportion to its working time (MS Project's prorated
 * accrual); a milestone's at its date. */
static long cost_before(const owf_project *p, int task, owf_ptime t)
{
    const owf_ptask *k = &p->tasks[task];
    const owf_pcal *cal;
    long done, total;
    if (t <= k->early_start) return 0;
    if (t >= k->early_finish) return k->cost;
    cal = owf_ptask_calendar(p, task);
    done = cal ? owf_pcal_work(cal, k->early_start, t) : t - k->early_start;
    total = cal ? owf_pcal_work(cal, k->early_start, k->early_finish) : k->early_finish - k->early_start;
    return share(k->cost, done, total);
}

static long plan_cost_before(const owf_project *p, owf_ptime t)
{
    long sum = 0;
    int i;
    for (i = 0; i < p->ntasks; i++)
        if (!is_summary(p, i) && p->tasks[i].cost) sum += cost_before(p, i, t);
    return sum;
}

static void cash_flow(rb *b)
{
    static const char *const heads[3] = { "Week Starting", "Cost", "Cumulative Cost" };
    const owf_project *p = b->p;
    char week[24], cost[24], cum[24];
    const char *cells[3];
    owf_ptime w, last = p->finish;
    long day, before = 0, total = 0, now;
    int i, n = 0;
    for (i = 0; i < p->ntasks; i++) {
        if (is_summary(p, i)) continue;
        total += p->tasks[i].cost;
        if (p->tasks[i].early_finish > last) last = p->tasks[i].early_finish;
    }
    /* weeks from the Monday on or before the plan's start */
    day = p->start / 1440;
    day -= (owf_ptime_weekday((owf_ptime)day * 1440) + 6) % 7;
    w = (owf_ptime)day * 1440;
    start_table(b, heads, 3);
    do {
        owf_ptime next = w + 7L * 1440;
        now = n + 1 == MAX_WEEKS ? total : plan_cost_before(p, next);
        date_text(w, week);
        money_text(now - before, cost);
        money_text(now, cum);
        cells[0] = week;
        cells[1] = cost;
        cells[2] = cum;
        add_row(b, cells, 0);
        before = now;
        w = next;
    } while (++n < MAX_WEEKS && w <= last && !b->failed);
    money_text(total, cost);
    cells[0] = "Total";
    cells[1] = cost;
    cells[2] = "";
    add_row(b, cells, 1);
    if (n == MAX_WEEKS && w <= last) line(b, "The last week holds the cost of every week after it.");
}

int owf_project_report(const owf_project *p, int kind, owf_doc **out)
{
    rb b;
    char title[256];
    *out = NULL;
    if (!p || kind < 0 || kind >= OWF_PREPORT_N) return OWF_ERR_FORMAT;
    memset(&b, 0, sizeof b);
    b.p = p;
    b.table = -1;
    if (!(b.doc = owf_doc_new())) return OWF_ERR_MEMORY;
    if (p->title && *p->title) snprintf(title, sizeof title, "%s: %s", p->title, report_names[kind]);
    else snprintf(title, sizeof title, "%s", report_names[kind]);
    if (owf_doc_set_title(b.doc, title)) b.failed = 1;
    frame(&b, kind);
    switch (kind) {
    case OWF_PREPORT_SUMMARY: summary(&b); break;
    case OWF_PREPORT_CRITICAL: task_table(&b, 0); break;
    case OWF_PREPORT_MILESTONES: task_table(&b, 1); break;
    case OWF_PREPORT_WHO_DOES_WHAT: who_does_what(&b); break;
    case OWF_PREPORT_OVERALLOCATED: overallocated(&b); break;
    default: cash_flow(&b); break;
    }
    if (b.failed) {
        owf_doc_free(b.doc);
        return OWF_ERR_MEMORY;
    }
    *out = b.doc;
    return OWF_OK;
}
