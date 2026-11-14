/*
 * owf_pmpx.c: reading MPX, MS Project's older text exchange format (owf_project.h), from the published
 * MPX 4.0 record layout and from what MS Project and other programs write. One record a line, its number
 * first, fields split by the file's delimiter (the character after "MPX" on the first line), quotes as
 * in CSV. The records read:
 *   10 currency (symbol, thousands and decimal separators), 11 defaults (hours a day and a week),
 *   12 dates (day order, separators, am/pm), 20/25/26 base calendars, their hours and exceptions,
 *   30 the project (title, calendar, start), 40/41 and 60/61 which fields resource and task records hold
 *   (by name and by number), 50 resources, 70 tasks, 71 task notes, 75 assignments.
 * MPX has no material or cost resources: every resource is read as a work resource. Text is Windows
 * code page 1252 ("ANSI") and is turned into UTF-8. MPXJ (LGPL) was run only as a black box on the
 * host to check files both ways; no MPXJ source was read.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 256

typedef struct {
    int sep;                             /* the list delimiter */
    char currency[8];
    char thousands, decimal;
    int date_order;                      /* 0 month day year, 1 day month year, 2 year month day */
    char date_sep, time_sep;
    char am[8], pm[8];
    int default_time;                    /* minutes after midnight */
    int task_fields[MAX_FIELDS], ntask_fields;
    int res_fields[MAX_FIELDS], nres_fields;
    int calendar;                        /* the base calendar records 25 and 26 add to; -1 none */
    int task;                            /* the last task read (for 71 and 75); -1 none */
    long *task_ids, *res_ids;            /* each task's and resource's ID field */
    int ctask_ids, cres_ids;
    char **preds;                        /* each task's Predecessors, to link after every task is in */
    int cpreds;
    int failed;
} mpx;

/* ---- Text ---- */

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int same(const char *a, const char *b)
{
    while (*a && *b && lower((unsigned char)*a) == lower((unsigned char)*b)) a++, b++;
    return !*a && !*b;
}

/* Windows-1252 into UTF-8, from owf_malloc. */
static char *to_utf8(const char *s, int *failed)
{
    static const unsigned short high[32] = {
        0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
        0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178,
    };
    size_t n = strlen(s), k = 0;
    char *out = owf_malloc(n * 3 + 1);
    if (!out) {
        *failed = 1;
        return NULL;
    }
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (c >= 0x80 && c < 0xA0) c = high[c - 0x80] ? high[c - 0x80] : '?';
        if (c < 0x80) out[k++] = (char)c;
        else if (c < 0x800) {
            out[k++] = (char)(0xC0 | c >> 6);
            out[k++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[k++] = (char)(0xE0 | c >> 12);
            out[k++] = (char)(0x80 | (c >> 6 & 0x3F));
            out[k++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[k] = 0;
    return out;
}

/* One line's fields, in place (the line is NUL-terminated); the count. */
static int split(char *line, int sep, char **field)
{
    int n = 0;
    char *in = line;
    for (;;) {
        char *out = in, *start = in;
        if (*in == '"') {
            in++;
            while (*in) {
                char c = *in++;
                if (c == '"') {
                    if (*in == '"') in++;
                    else break;
                }
                *out++ = c;
            }
            while (*in && *in != sep) in++;
        } else
            while (*in && *in != sep) *out++ = *in++;
        if (n < MAX_FIELDS) field[n++] = start;
        if (!*in) {
            *out = 0;
            return n;
        }
        in++;
        *out = 0;
    }
}

/* ---- Numbers, money, dates ---- */

/* A decimal in hundredths ("1", "0.5", "1,234.50" with the file's separators); 0 when there is none. */
static int hundredths(const mpx *m, const char *s, long *v)
{
    long whole = 0, part = 0;
    int digits = 0, places = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') neg = 1, s++;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            if (places) {
                if (places < 3) part = part * 10 + (*s - '0');
                places++;
            } else if (whole < 20000000L)
                whole = whole * 10 + (*s - '0');
            digits++;
        } else if (*s == m->decimal && !places)
            places = 1;
        else if (*s == m->thousands && !places)
            continue;
        else
            break;
    }
    if (!digits) return 0;
    if (places == 2) part *= 10;
    else if (places <= 1) part = 0;
    *v = (whole * 100 + part) * (neg ? -1 : 1);
    return 1;
}

/* A rate, "$20.00/h", into hundredths an hour (per minute, day, week, month and year by the plan's day and
 * week). */
static int rate(const mpx *m, const owf_project *p, const char *s, long *v)
{
    size_t n = strlen(m->currency);
    const char *slash;
    long r, per = 60;                                     /* minutes the rate is for */
    while (*s == ' ') s++;
    if (n && strncmp(s, m->currency, n) == 0) s += n;
    if (!hundredths(m, s, &r)) return 0;
    if (r < 0) r = 0;
    if ((slash = strchr(s, '/')) != NULL) {
        char u = (char)lower((unsigned char)slash[1]);
        long day = p->minutes_per_day > 0 ? p->minutes_per_day : 480;
        long week = p->minutes_per_week > 0 ? p->minutes_per_week : 2400;
        if (u == 'm' && lower((unsigned char)slash[2]) == 'o') per = week * 4;     /* a month: 4 weeks */
        else if (u == 'm') per = 1;
        else if (u == 'd') per = day;
        else if (u == 'w') per = week;
        else if (u == 'y') per = week * 52;
    }
    if (per == 1) r = r > 20000000L / 60 ? 20000000L : r * 60;
    else r = r / per * 60 + r % per * 60 / per;           /* to an hour's, never past a 32-bit long */
    *v = r;
    return 1;
}

static int take_int(const char **s, long *v, int *digits)
{
    long n = 0;
    int k = 0;
    while (**s >= '0' && **s <= '9' && k < 9) n = n * 10 + (*(*s)++ - '0'), k++;
    if (!k) return 0;
    *v = n;
    if (digits) *digits = k;
    return 1;
}

/* A time, "08:00 am", "17:00" or "8:00 PM", in minutes after midnight; -1 when it isn't one. */
static int clock(const mpx *m, const char *s)
{
    long h, mi = 0;
    while (*s == ' ') s++;
    if (!take_int(&s, &h, NULL)) return -1;
    if (*s == m->time_sep || *s == ':') {
        s++;
        if (!take_int(&s, &mi, NULL)) return -1;
    }
    while (*s == ' ') s++;
    if (*s) {
        size_t na = strlen(m->am), np = strlen(m->pm);
        if (np && strncmp(s, m->pm, np) == 0) {           /* case as written, then any case */
            if (h < 12) h += 12;
        } else if (na && strncmp(s, m->am, na) == 0) {
            if (h == 12) h = 0;
        } else if (lower((unsigned char)*s) == 'p') {
            if (h < 12) h += 12;
        } else if (lower((unsigned char)*s) == 'a') {
            if (h == 12) h = 0;
        }
    }
    if (h > 24 || mi > 59 || (h == 24 && mi)) return -1;
    return (int)(h * 60 + mi);
}

/* A date and maybe a time in the file's order and separators; 0 when it isn't one ("NA" among them). */
static int date(const mpx *m, const char *s, int default_minute, owf_ptime *t)
{
    long a, b, c, y, mo, d;
    int da, dc, minute = default_minute;
    while (*s == ' ') s++;
    if (!take_int(&s, &a, &da) || (*s != m->date_sep && *s != '/' && *s != '-' && *s != '.')) return 0;
    s++;
    if (!take_int(&s, &b, NULL) || (*s != m->date_sep && *s != '/' && *s != '-' && *s != '.')) return 0;
    s++;
    if (!take_int(&s, &c, &dc)) return 0;
    if (m->date_order == 2 || da == 4) y = a, mo = b, d = c;
    else if (m->date_order == 1) d = a, mo = b, y = c;
    else mo = a, d = b, y = c;
    if (y < 100) y += y < 70 ? 2000 : 1900;
    while (*s == ' ') s++;
    if (*s) {
        int k = clock(m, s);
        if (k < 0) return 0;
        minute = k;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || y < 1900 || y > 4000) return 0;
    *t = owf_ptime_make((int)y, (int)mo, (int)d, minute / 60, minute % 60);
    return 1;
}

/* ---- Field lists ---- */

/* The field numbers of record 60's and 40's names, for a file with no 61 or 41. */
static int field_by_name(const char *name, int resource)
{
    static const struct { const char *name; int task, res; } names[] = {
        { "Name", 1, 1 }, { "WBS", 2, -1 }, { "Initials", -1, 2 }, { "Outline Level", 3, -1 }, { "Group", -1, 3 },
        { "Baseline Work", 21, -1 }, { "Baseline Cost", 31, -1 }, { "Duration", 40, -1 }, { "ID", 90, 40 },
        { "Baseline Duration", 41, -1 }, { "Max Units", -1, 41 }, { "Standard Rate", -1, 42 },
        { "% Complete", 44, -1 }, { "Start", 50, -1 }, { "Finish", 51, -1 }, { "Baseline Start", 56, -1 },
        { "Baseline Finish", 57, -1 }, { "Constraint Date", 68, -1 }, { "Predecessors", 70, -1 },
        { "Milestone", 81, -1 }, { "Constraint Type", 91, -1 }, { "Unique ID", 98, 49 },
    };
    size_t i;
    for (i = 0; i < sizeof names / sizeof *names; i++)
        if (same(name, names[i].name)) return resource ? names[i].res : names[i].task;
    return -1;
}

static void fields_by_number(int *out, int *nout, char **field, int n)
{
    int i;
    *nout = 0;
    for (i = 1; i < n && *nout < MAX_FIELDS; i++) out[(*nout)++] = atoi(field[i]);
}

static void fields_by_names(int *out, int *nout, char **field, int n, int resource)
{
    int i;
    *nout = 0;
    for (i = 1; i < n && *nout < MAX_FIELDS; i++) out[(*nout)++] = field_by_name(field[i], resource);
}

/* ---- Records ---- */

static int grow(void **array, int *cap, int need, size_t size)
{
    void *grown;
    int n;
    if (need < *cap) return 1;
    n = *cap ? *cap * 2 : 64;
    while (n <= need) n *= 2;
    if (!(grown = owf_realloc(*array, (size_t)n * size))) return 0;
    memset((char *)grown + (size_t)*cap * size, 0, (size_t)(n - *cap) * size);
    *array = grown;
    *cap = n;
    return 1;
}

static void calendar_record(mpx *m, owf_project *p, char **f, int n)
{
    int c = -1, i, day;
    char *name = n > 1 ? to_utf8(f[1], &m->failed) : NULL;
    if (!name) return;
    for (i = 0; i < p->ncalendars; i++)
        if (p->calendars[i].name && same(p->calendars[i].name, name)) c = i;
    if (c < 0 && (c = owf_project_add_calendar(p, name)) < 0) m->failed = 1;
    owf_free(name);
    m->calendar = c;
    if (c < 0) return;
    for (day = 0; day < 7; day++) {                      /* 0 not working, 1 working (Standard's hours) */
        if (2 + day >= n || !f[2 + day][0] || f[2 + day][0] == '2') continue;
        if (f[2 + day][0] == '0') owf_pcal_set_week(&p->calendars[c], day, 0, NULL);
        else {
            static const unsigned short standard[4] = { 480, 720, 780, 1020 };
            owf_pcal_set_week(&p->calendars[c], day, 2, standard);
        }
    }
}

/* From,to pairs from field first on; their count. */
static int spans(const mpx *m, char **f, int n, int first, unsigned short *pairs)
{
    int k = 0, i;
    for (i = first; i + 1 < n && k < OWF_PCAL_SPANS; i += 2) {
        int a = clock(m, f[i]), b = clock(m, f[i + 1]);
        if (a < 0 || b < 0) break;
        if (b == 0) b = 1440;                             /* to midnight */
        if (b <= a) continue;
        pairs[2 * k] = (unsigned short)a;
        pairs[2 * k + 1] = (unsigned short)b;
        k++;
    }
    return k;
}

static void hours_record(mpx *m, owf_project *p, char **f, int n)
{
    unsigned short pairs[2 * OWF_PCAL_SPANS];
    long day;
    const char *s = n > 1 ? f[1] : "";
    if (m->calendar < 0 || !take_int(&s, &day, NULL) || day < 1 || day > 7) return;
    owf_pcal_set_week(&p->calendars[m->calendar], (int)day - 1, spans(m, f, n, 2, pairs), pairs);
}

static void exception_record(mpx *m, owf_project *p, char **f, int n)
{
    unsigned short pairs[2 * OWF_PCAL_SPANS];
    owf_ptime from, to;
    long d;
    int k = 0;
    if (m->calendar < 0 || n < 3 || !date(m, f[1], 0, &from) || !date(m, f[2], 0, &to) || to < from) return;
    if (n > 3 && f[3][0] == '1') {
        k = spans(m, f, n, 4, pairs);
        if (!k) {                                         /* working with no hours given: Standard's */
            static const unsigned short standard[4] = { 480, 720, 780, 1020 };
            memcpy(pairs, standard, sizeof standard);
            k = 2;
        }
    }
    for (d = from / 1440; d <= to / 1440 && d - from / 1440 < 3660; d++)
        if (owf_pcal_set_exception(&p->calendars[m->calendar], d, k, pairs) == OWF_ERR_MEMORY) m->failed = 1;
}

static void project_record(mpx *m, owf_project *p, char **f, int n)
{
    owf_ptime t;
    int i;
    if (n > 1 && f[1][0]) {
        owf_free(p->title);
        p->title = to_utf8(f[1], &m->failed);
    }
    if (n > 4 && f[4][0])
        for (i = 0; i < p->ncalendars; i++)
            if (p->calendars[i].name && same(p->calendars[i].name, f[4])) p->calendar = i;
    if (n > 5 && date(m, f[5], m->default_time, &t)) p->start = t;
}

static void resource_record(mpx *m, owf_project *p, char **f, int n)
{
    owf_presource *r;
    int at, i;
    long v;
    if ((at = owf_project_add_resource(p, NULL)) < 0 || !grow((void **)&m->res_ids, &m->cres_ids, at, sizeof *m->res_ids)) {
        m->failed = 1;
        return;
    }
    r = &p->resources[at];
    m->res_ids[at] = at + 1;
    for (i = 1; i < n && i - 1 < m->nres_fields; i++) {
        const char *s = f[i];
        if (!*s) continue;
        switch (m->res_fields[i - 1]) {
        case 1: owf_free(r->name); r->name = to_utf8(s, &m->failed); break;
        case 2: owf_free(r->initials); r->initials = to_utf8(s, &m->failed); break;
        case 3: owf_free(r->group); r->group = to_utf8(s, &m->failed); break;
        case 40: if (take_int(&s, &v, NULL)) m->res_ids[at] = v; break;
        case 41:
            if (hundredths(m, s, &v)) {
                if (strchr(s, '%')) v /= 100;             /* "100%" rather than "1" */
                r->max_units = v < 0 ? 0 : v;
            }
            break;
        case 42: if (rate(m, p, s, &v)) r->rate = v; break;
        case 49: if (take_int(&s, &v, NULL) && v > 0 && v < 0x7FFFFFFFL) r->uid = (int)v; break;
        default: break;
        }
    }
}

static int parse_constraint(const char *s)
{
    static const char *const names[8] = {
        "As Soon As Possible", "As Late As Possible", "Must Start On", "Must Finish On",
        "Start No Earlier Than", "Start No Later Than", "Finish No Earlier Than", "Finish No Later Than",
    };
    int i;
    for (i = 0; i < 8; i++)
        if (same(s, names[i])) return i;
    return -1;
}

static void task_record(mpx *m, owf_project *p, char **f, int n)
{
    owf_ptask *t;
    int at, i, has_baseline = 0;
    long v;
    owf_ptime when;
    if ((at = owf_project_add_task(p, NULL, 480)) < 0 ||
        !grow((void **)&m->task_ids, &m->ctask_ids, at, sizeof *m->task_ids) ||
        !grow((void **)&m->preds, &m->cpreds, at, sizeof *m->preds)) {
        m->failed = 1;
        return;
    }
    m->task = at;
    t = &p->tasks[at];
    t->milestone = 0;
    m->task_ids[at] = at + 1;
    for (i = 1; i < n && i - 1 < m->ntask_fields; i++) {
        const char *s = f[i];
        int el, es;
        if (!*s) continue;
        switch (m->task_fields[i - 1]) {
        case 1: owf_free(t->name); t->name = to_utf8(s, &m->failed); break;   /* a field named twice: the last */
        case 3: if (take_int(&s, &v, NULL)) t->level = v < 1 ? 1 : v > 100 ? 100 : (int)v; break;
        case 21: if (owf_pduration_parse(p, s, &v, &el, &es) == OWF_OK) t->baseline_work = v; break;
        case 31: {
            size_t k = strlen(m->currency);
            if (k && strncmp(s, m->currency, k) == 0) s += k;
            if (hundredths(m, s, &v)) t->baseline_cost = v < 0 ? 0 : v;
            break;
        }
        case 40:
            if (owf_pduration_parse(p, s, &v, &t->elapsed, &t->estimated) == OWF_OK) {
                t->duration = v < 0 ? 0 : v;
                if (!v) t->milestone = 1;
            }
            break;
        case 41: if (owf_pduration_parse(p, s, &v, &el, &es) == OWF_OK) t->baseline_duration = v < 0 ? 0 : v; break;
        case 44: if (take_int(&s, &v, NULL)) t->percent = v > 100 ? 100 : (int)v; break;
        case 50: if (date(m, s, m->default_time, &when)) t->early_start = when; break;
        case 51: if (date(m, s, 17 * 60, &when)) t->early_finish = when; break;
        case 56: if (date(m, s, m->default_time, &when)) t->baseline_start = when, has_baseline = 1; break;
        case 57: if (date(m, s, 17 * 60, &when)) t->baseline_finish = when, has_baseline = 1; break;
        case 68: if (date(m, s, m->default_time, &when)) t->constraint_date = when; break;
        case 70:
            owf_free(m->preds[at]);
            if (!(m->preds[at] = owf_malloc(strlen(s) + 1))) m->failed = 1;
            else strcpy(m->preds[at], s);
            break;
        case 81: t->milestone = lower((unsigned char)*s) == 'y' || *s == '1'; break;
        case 90: if (take_int(&s, &v, NULL)) m->task_ids[at] = v; break;
        case 91: if ((v = parse_constraint(s)) >= 0) t->constraint = (int)v; break;
        case 98: if (take_int(&s, &v, NULL) && v > 0 && v < 0x7FFFFFFFL) t->uid = (int)v; break;
        default: break;
        }
    }
    t->baselined = has_baseline;
    if (t->constraint != OWF_CON_ASAP && t->constraint != OWF_CON_ALAP && !t->constraint_date)
        t->constraint = OWF_CON_ASAP;
}

static int task_by_id(const mpx *m, const owf_project *p, long id)
{
    int i;
    for (i = 0; i < p->ntasks; i++)
        if (m->task_ids[i] == id) return i;
    return -1;
}

static void assignment_record(mpx *m, owf_project *p, char **f, int n)
{
    long id, units = 100, v;
    const char *s = n > 1 ? f[1] : "";
    int r;
    if (m->task < 0 || !take_int(&s, &id, NULL)) return;
    for (r = 0; r < p->nresources; r++)
        if (m->res_ids[r] == id) break;
    if (r == p->nresources) return;
    if (n > 2 && hundredths(m, f[2], &v)) units = v < 0 ? 0 : v;
    if (owf_project_assign(p, m->task, r, units) < 0) m->failed = 1;
}

/* Where one predecessor ends: a ';', the file's delimiter, or a ',' when the file's decimal point isn't one. */
static int item_end(const mpx *m, char c)
{
    return !c || c == ';' || c == m->sep || (c == ',' && m->decimal != ',');
}

/* "2,3SS+1d,4FF-2h,5SF+50%" by the ID field, onto task succ; anything not understood is skipped. */
static void link_task(mpx *m, owf_project *p, int succ, const char *s)
{
    while (*s) {
        long id, lag = 0;
        int type = OWF_LINK_FS, percent = 0, pred, i, el, es;
        static const char names[4][3] = { "FS", "SS", "FF", "SF" };
        static const int types[4] = { OWF_LINK_FS, OWF_LINK_SS, OWF_LINK_FF, OWF_LINK_SF };
        while (*s == ' ' || (*s && item_end(m, *s))) s++;
        if (!*s) break;
        if (!take_int(&s, &id, NULL)) {
            while (!item_end(m, *s)) s++;
            continue;
        }
        for (i = 0; i < 4; i++)
            if (lower((unsigned char)s[0]) == lower(names[i][0]) && lower((unsigned char)s[1]) == lower(names[i][1])) {
                type = types[i];
                s += 2;
                break;
            }
        while (*s == ' ') s++;
        if (*s == '+' || *s == '-') {
            int neg = *s++ == '-';
            char text[32];
            size_t k = 0;
            while (!item_end(m, *s) && k < sizeof text - 1) text[k++] = *s++;
            text[k] = 0;
            if (k && text[k - 1] == '%') {
                const char *q = text;
                long pc;
                if (take_int(&q, &pc, NULL)) lag = pc, percent = 1;
            } else if (owf_pduration_parse(p, text, &lag, &el, &es) != OWF_OK)
                lag = 0;
            if (neg) lag = -lag;
        }
        while (!item_end(m, *s)) s++;
        if ((pred = task_by_id(m, p, id)) < 0 || pred == succ) continue;
        if (owf_project_link(p, pred, succ, type, lag, percent) == OWF_ERR_MEMORY) m->failed = 1;
    }
}

int owf_pmpx_read(const unsigned char *data, size_t length, owf_project **out)
{
    mpx m;
    owf_project *p;
    char *text, *line, *next, **field;
    int i, top, saw_task_fields = 0;
    if (!out) return OWF_ERR_FORMAT;
    *out = NULL;
    if (!data || length < 4 || memcmp(data, "MPX", 3) != 0) return OWF_ERR_FORMAT;
    memset(&m, 0, sizeof m);
    m.sep = data[3];
    strcpy(m.currency, "$");
    m.thousands = ',';
    m.decimal = '.';
    m.date_sep = '/';
    m.time_sep = ':';
    strcpy(m.am, "am");
    strcpy(m.pm, "pm");
    m.default_time = 480;
    m.calendar = m.task = -1;
    if (!(text = owf_malloc(length + 1))) return OWF_ERR_MEMORY;
    memcpy(text, data, length);
    text[length] = 0;
    if (!(field = owf_malloc(MAX_FIELDS * sizeof *field)) || !(p = owf_project_new(0))) {
        owf_free(field);
        owf_free(text);
        return OWF_ERR_MEMORY;
    }
    for (line = text; line && !m.failed; line = next) {
        int n;
        long kind;
        const char *s = line;
        next = strpbrk(line, "\r\n");
        if (next) {
            if (*next == '\r' && next[1] == '\n') *next++ = 0;
            *next++ = 0;
        }
        if (!take_int(&s, &kind, NULL) || *s != m.sep) continue;
        n = split(line, m.sep, field);
        switch (kind) {
        case 10:
            if (n > 1) {
                size_t k = strlen(field[1]);
                if (k < sizeof m.currency) memcpy(m.currency, field[1], k + 1);
            }
            if (n > 4 && field[4][0]) m.thousands = field[4][0];
            if (n > 5 && field[5][0]) m.decimal = field[5][0];
            break;
        case 11: {
            long v;
            if (n > 4 && hundredths(&m, field[4], &v) && v > 0 && v <= 2400) p->minutes_per_day = v * 60 / 100;
            if (n > 5 && hundredths(&m, field[5], &v) && v > 0 && v <= 16800) p->minutes_per_week = v * 60 / 100;
            break;
        }
        case 12:
            if (n > 1) m.date_order = atoi(field[1]);
            if (n > 3 && field[3][0]) m.default_time = atoi(field[3]) % 1440;
            if (n > 4 && field[4][0]) m.date_sep = field[4][0];
            if (n > 5 && field[5][0]) m.time_sep = field[5][0];
            if (n > 6 && strlen(field[6]) < sizeof m.am) strcpy(m.am, field[6]);
            if (n > 7 && strlen(field[7]) < sizeof m.pm) strcpy(m.pm, field[7]);
            break;
        case 20: calendar_record(&m, p, field, n); break;
        case 25: hours_record(&m, p, field, n); break;
        case 26: exception_record(&m, p, field, n); break;
        case 30: project_record(&m, p, field, n); break;
        case 40: if (!m.nres_fields) fields_by_names(m.res_fields, &m.nres_fields, field, n, 1); break;
        case 41: fields_by_number(m.res_fields, &m.nres_fields, field, n); break;
        case 50: m.calendar = -1; resource_record(&m, p, field, n); break;
        case 60:
            if (!saw_task_fields) fields_by_names(m.task_fields, &m.ntask_fields, field, n, 0);
            break;
        case 61:
            fields_by_number(m.task_fields, &m.ntask_fields, field, n);
            saw_task_fields = 1;
            break;
        case 70: m.calendar = -1; task_record(&m, p, field, n); break;
        case 71:
            if (m.task >= 0 && n > 1 && !p->tasks[m.task].notes) p->tasks[m.task].notes = to_utf8(field[1], &m.failed);
            break;
        case 75: assignment_record(&m, p, field, n); break;
        default: break;
        }
    }
    if (!m.failed && m.ntask_fields == 0 && p->ntasks == 0 && p->nresources == 0 && m.calendar < 0 &&
        p->ncalendars <= 1 && !p->title)
        m.failed = 2;                                     /* nothing an MPX file holds */
    for (i = 0; i < p->ntasks && !m.failed; i++)
        if (m.preds[i]) link_task(&m, p, i, m.preds[i]);
    if (!p->start)                                        /* no project record: the earliest Start */
        for (i = 0; i < p->ntasks; i++)
            if (p->tasks[i].early_start && (!p->start || p->tasks[i].early_start < p->start))
                p->start = p->tasks[i].early_start;
    for (i = 0, top = 0; i < p->ntasks; i++)
        if (p->tasks[i].uid > top) top = p->tasks[i].uid;
    p->next_uid = top + 1;
    for (i = 0, top = 0; i < p->nresources; i++)
        if (p->resources[i].uid > top) top = p->resources[i].uid;
    p->next_resource_uid = top + 1;
    for (i = 0; i < m.cpreds; i++) owf_free(m.preds[i]);
    owf_free(m.preds);
    owf_free(m.task_ids);
    owf_free(m.res_ids);
    owf_free(field);
    owf_free(text);
    if (m.failed) {
        owf_project_free(p);
        return m.failed == 2 ? OWF_ERR_FORMAT : OWF_ERR_MEMORY;
    }
    *out = p;
    return OWF_OK;
}
