/*
 * owf_pfile.c: GFOpenProject's files (owf_project.h): .gfproj, MSPDI and CSV. Integer arithmetic only.
 * MSPDI is written from Microsoft's published "Project XML Data Interchange Schema Reference" (element
 * names and order, the ConstraintType, link Type, DurationFormat and LagFormat codes, durations as
 * ISO 8601 "PT8H0M0S", lags and slack in tenths of a minute). A percent lag's LinkLag is the percent
 * itself, as MPXJ (run as a black box on the host) reads and writes it. CSV follows RFC 4180, with MS
 * Project's column names and its duration and predecessor text ("5 days", "3SS+1d") as its
 * documentation shows.
 * No other program's code.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"
#include "owf_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MSPDI_NS "http://schemas.microsoft.com/project"
#define MAX_MINUTES 200000000L           /* about 380 years: bigger numbers in a file are cut to this, so
                                          * that ten times it (tenths of a minute) fits a 32-bit long */
#define MAX_EXCEPTION_DAYS 3660          /* one exception's range of days, at most */
#define MAX_TEXT 256

/* ---- Small text helpers ---- */

static char *dup_n(const char *s, size_t n)
{
    char *p = owf_malloc(n + 1);
    if (p) {
        memcpy(p, s, n);
        p[n] = 0;
    }
    return p;
}

static int is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* A whole number at *s, moving past it; clamped to +-MAX_MINUTES*10. 0 when there are no digits. */
static int take_long(const char **s, long *v)
{
    const char *p = *s;
    int neg = 0, any = 0;
    long n = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    while (*p >= '0' && *p <= '9') {
        n = n <= MAX_MINUTES ? n * 10 + (*p - '0') : MAX_MINUTES * 10L;
        p++;
        any = 1;
    }
    if (!any) return 0;
    if (n > MAX_MINUTES * 10L) n = MAX_MINUTES * 10L;
    *v = neg ? -n : n;
    *s = p;
    return 1;
}

/* A number such as "12", "-3" or "1.25": its whole part (cut at MAX_MINUTES) and thousandths, both
 * with its sign. */
typedef struct {
    long whole, frac;
} num;

static int take_num(const char **s, num *v)
{
    const char *p = *s;
    long whole = 0, frac = 0, scale = 100;
    int neg = 0, any = 0;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    while (*p >= '0' && *p <= '9') {
        if (whole < MAX_MINUTES) whole = whole * 10 + (*p - '0');
        p++;
        any = 1;
    }
    if (*p == '.' || *p == ',') {
        const char *q = p + 1;
        if (*q >= '0' && *q <= '9') {
            p = q;
            while (*p >= '0' && *p <= '9') {
                frac += scale * (*p - '0');
                scale /= 10;
                p++;
                any = 1;
            }
        }
    }
    if (!any) return 0;
    if (whole > MAX_MINUTES) whole = MAX_MINUTES;
    v->whole = neg ? -whole : whole;
    v->frac = neg ? -frac : frac;
    *s = p;
    return 1;
}

static long clamp(long v)
{
    return v > MAX_MINUTES ? MAX_MINUTES : v < -MAX_MINUTES ? -MAX_MINUTES : v;
}

/* n units of unit minutes, rounded to the minute; clamped. */
static long to_minutes(num n, long unit)
{
    if (n.whole > MAX_MINUTES / unit) return MAX_MINUTES;
    if (n.whole < -(MAX_MINUTES / unit)) return -MAX_MINUTES;
    return clamp(n.whole * unit + (n.frac * unit + (n.frac < 0 ? -500 : 500)) / 1000);
}

static int same_word(const char *a, size_t n, const char *word)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!word[i] || owf_lower((unsigned char)a[i]) != owf_lower((unsigned char)word[i])) return 0;
    return word[n] == 0;
}

/* ---- Dates: "2026-11-16T08:00:00", "2026-11-16 08:00", "2026-11-16" ---- */

static int parse_time(const char *s, owf_ptime *t)
{
    long y, mo, d, h = 0, mi = 0;
    const char *p = s;
    while (is_space(*p)) p++;
    if (!take_long(&p, &y) || *p++ != '-' || !take_long(&p, &mo) || *p++ != '-' || !take_long(&p, &d)) return 0;
    if (*p == 'T' || *p == ' ') {
        const char *q = p + 1;
        if (take_long(&q, &h) && *q == ':' && (q++, take_long(&q, &mi))) p = q;
        else h = 0;
    }
    if (y < 1900 || y > 4000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 24 || mi < 0 || mi > 59) return 0;
    *t = owf_ptime_make((int)y, (int)mo, (int)d, (int)h, (int)mi);
    return 1;
}

static void put_time(owf_buf *b, owf_ptime t, int csv)
{
    int y, mo, d, h, mi;
    owf_ptime_split(t, &y, &mo, &d, &h, &mi);
    if (csv) owf_buf_printf(b, "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi);
    else owf_buf_printf(b, "%04d-%02d-%02dT%02d:%02d:00", y, mo, d, h, mi);
}

/* "08:00:00" as minutes; "00:00:00" as an end is midnight, 1440. */
static int parse_clock(const char *s, int end)
{
    long h, m = 0;
    const char *p = s;
    while (is_space(*p)) p++;
    if (!take_long(&p, &h) || h < 0 || h > 24) return -1;
    if (*p == ':' && (p++, !take_long(&p, &m))) return -1;
    if (m < 0 || m > 59) return -1;
    h = h * 60 + m;
    if (end && h == 0) h = 1440;
    return h > 1440 ? -1 : (int)h;
}

/* ---- Durations ---- */

/* ISO 8601: "PT8H0M0S", "P1DT2H", "-PT30M". */
static int parse_iso_duration(const char *s, long *minutes)
{
    const char *p = s;
    long total = 0, n;
    int neg = 0, in_time = 0, any = 0;
    while (is_space(*p)) p++;
    if (*p == '-') neg = 1, p++;
    if (*p++ != 'P') return 0;
    for (;;) {
        num milli;
        if (*p == 'T') {
            in_time = 1;
            p++;
            continue;
        }
        if (!take_num(&p, &milli)) break;
        n = 0;
        switch (*p) {
        case 'Y': n = to_minutes(milli, 525600L); break;
        case 'M': n = in_time ? to_minutes(milli, 1) : to_minutes(milli, 43200L); break;
        case 'W': n = to_minutes(milli, 10080L); break;
        case 'D': n = to_minutes(milli, 1440L); break;
        case 'H': n = to_minutes(milli, 60); break;
        case 'S': n = milli.whole / 60; break;
        default: return 0;
        }
        total = clamp(total + n);
        p++;
        any = 1;
    }
    if (!any) return 0;
    *minutes = neg ? -total : total;
    return 1;
}

static void put_iso_duration(owf_buf *b, long minutes)
{
    if (minutes < 0) {
        owf_buf_putc(b, '-');
        minutes = -minutes;
    }
    owf_buf_printf(b, "PT%ldH%ldM0S", minutes / 60, minutes % 60);
}

typedef struct {
    const char *name;
    int unit;                            /* 0 m, 1 h, 2 d, 3 w, 4 mo */
} unit_name;

static const unit_name unit_names[] = {
    { "m", 0 }, { "min", 0 }, { "mins", 0 }, { "minute", 0 }, { "minutes", 0 },
    { "h", 1 }, { "hr", 1 }, { "hrs", 1 }, { "hour", 1 }, { "hours", 1 },
    { "d", 2 }, { "dy", 2 }, { "dys", 2 }, { "day", 2 }, { "days", 2 },
    { "w", 3 }, { "wk", 3 }, { "wks", 3 }, { "week", 3 }, { "weeks", 3 },
    { "mo", 4 }, { "mon", 4 }, { "mons", 4 }, { "month", 4 }, { "months", 4 },
};

static long unit_minutes(const owf_project *p, int unit, int elapsed)
{
    static const long el[5] = { 1, 60, 1440L, 10080L, 43200L };
    long per_day = p && p->minutes_per_day > 0 ? p->minutes_per_day : 480;
    long per_week = p && p->minutes_per_week > 0 ? p->minutes_per_week : 2400;
    if (elapsed) return el[unit];
    switch (unit) {
    case 0: return 1;
    case 1: return 60;
    case 2: return per_day;
    case 3: return per_week;
    default: return per_day * 20;        /* MS Project's default 20 days a month */
    }
}

/* A number then a unit at *s (spaces allowed between); moves past them. */
static int take_duration(const owf_project *p, const char **s, long *minutes, int *elapsed, int *estimated)
{
    const char *q = *s, *w;
    num milli;
    int el = 0, unit = 2;
    size_t n, i;
    if (!take_num(&q, &milli)) return 0;
    while (*q == ' ') q++;
    w = q;
    while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z')) q++;
    n = (size_t)(q - w);
    if (n) {
        const char *u = w;
        size_t un = n;
        if (owf_lower((unsigned char)*u) == 'e' && n > 1) el = 1, u++, un--;
        for (i = 0; i < sizeof unit_names / sizeof *unit_names; i++)
            if (same_word(u, un, unit_names[i].name)) break;
        if (i == sizeof unit_names / sizeof *unit_names) return 0;
        unit = unit_names[i].unit;
    }
    if (estimated) *estimated = 0;
    while (*q == ' ') q++;
    if (*q == '?') {
        if (estimated) *estimated = 1;
        q++;
    }
    *minutes = to_minutes(milli, unit_minutes(p, unit, el));
    if (elapsed) *elapsed = el;
    *s = q;
    return 1;
}

int owf_pduration_parse(const owf_project *p, const char *s, long *minutes, int *elapsed, int *estimated)
{
    const char *q = s;
    long m;
    int el, es;
    if (!s) return OWF_ERR_FORMAT;
    while (is_space(*q)) q++;
    if (!take_duration(p, &q, &m, &el, &es)) return OWF_ERR_FORMAT;
    while (is_space(*q)) q++;
    if (*q) return OWF_ERR_FORMAT;
    *minutes = m;
    if (elapsed) *elapsed = el;
    if (estimated) *estimated = es;
    return OWF_OK;
}

void owf_pduration_text(const owf_project *p, long minutes, int elapsed, int estimated, char *out)
{
    static const char letter[3][2] = { "m", "h", "d" };
    int unit;
    long size = 1;
    for (unit = 2; unit > 0; unit--) {   /* days, hours or minutes, as MS Project shows them */
        size = unit_minutes(p, unit, elapsed);
        if (minutes % size == 0) break;
    }
    if (unit == 0) size = 1;
    sprintf(out, "%ld%s%s%s", minutes / size, elapsed ? "e" : "", letter[unit], estimated ? "?" : "");
}

/* ---- Detecting ---- */

static const unsigned char *skip_bom_space(const unsigned char *d, const unsigned char *end)
{
    if (end - d >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) d += 3;
    while (d < end && is_space(*d)) d++;
    return d;
}

int owf_project_detect(const unsigned char *data, size_t length)
{
    owf_unzip zip;
    const unsigned char *d;
    if (!data || !length) return -1;
    if (owf_unzip_open(&zip, data, length)) return owf_unzip_has(&zip, "content.xml") ? OWF_PFILE_GFPROJ : -1;
    d = skip_bom_space(data, data + length);
    if (d < data + length && *d == '<') return OWF_PFILE_MSPDI;
    if (length >= 4 && memcmp(data, "MPX", 3) == 0 && (data[3] == ',' || data[3] == ';' || data[3] == '\t'))
        return OWF_PFILE_MPX;
    return OWF_PFILE_CSV;
}

/* ---- Reading XML (MSPDI and .gfproj's content.xml) ---- */

typedef struct {
    long uid;
    int index;
} uid_map;

static int cmp_uid(const void *a, const void *b)
{
    const uid_map *x = a, *y = b;
    return x->uid < y->uid ? -1 : x->uid > y->uid;
}

static int find_uid(const uid_map *map, int n, long uid)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (map[mid].uid == uid) return map[mid].index;
        if (map[mid].uid < uid) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

/* An element's text, all of it, into out (cut at size - 1). */
static const char *text_of(const owf_xml_node *n, char *out, size_t size)
{
    const owf_xml_node *c;
    size_t at = 0;
    out[0] = 0;
    if (!n) return out;
    for (c = n->first; c; c = c->next) {
        size_t k;
        if (c->type != OWF_XML_TEXT) continue;
        k = c->length < size - 1 - at ? c->length : size - 1 - at;
        memcpy(out + at, c->name, k);
        at += k;
    }
    out[at] = 0;
    return out;
}

/* An element's text as a new string; NULL with *failed set when memory ran out. */
static char *text_dup(const owf_xml_node *n, int *failed)
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

typedef struct {
    const char *ns;
    char buf[MAX_TEXT];
} xr;

static const char *child_text(xr *r, const owf_xml_node *n, const char *name)
{
    owf_xml_node *c = owf_xml_child(n, r->ns, name);
    return c ? text_of(c, r->buf, sizeof r->buf) : NULL;
}

static int child_long(xr *r, const owf_xml_node *n, const char *name, long *v)
{
    const char *s = child_text(r, n, name);
    while (s && is_space(*s)) s++;
    return s && take_long(&s, v);
}

static int child_flag(xr *r, const owf_xml_node *n, const char *name)
{
    const char *s = child_text(r, n, name);
    while (s && is_space(*s)) s++;
    return s && (*s == '1' || owf_lower((unsigned char)*s) == 't');
}

/* A decimal ("1.5", "25.00") in hundredths. */
static int child_hundredths(xr *r, const owf_xml_node *n, const char *name, long *v)
{
    const char *s = child_text(r, n, name);
    num x;
    while (s && is_space(*s)) s++;
    if (!s || !take_num(&s, &x)) return 0;
    if (x.whole > 20000000L) x.whole = 20000000L;
    if (x.whole < -20000000L) x.whole = -20000000L;
    *v = x.whole * 100 + x.frac / 10;
    return 1;
}

/* A WeekDay's or an Exception's spans; the count, or -1 when there are none written or they are bad. */
static int read_spans(xr *r, const owf_xml_node *day, unsigned short *pairs)
{
    owf_xml_node *times = owf_xml_child(day, r->ns, "WorkingTimes"), *w;
    int n = 0, i, j;
    if (!times) return 0;
    for (w = owf_xml_child(times, r->ns, "WorkingTime"); w; w = owf_xml_next(w, r->ns, "WorkingTime")) {
        const char *s;
        int from, to;
        if (n == OWF_PCAL_SPANS) return -1;
        s = child_text(r, w, "FromTime");
        from = s ? parse_clock(s, 0) : -1;
        s = child_text(r, w, "ToTime");
        to = s ? parse_clock(s, 1) : -1;
        if (from < 0 || to < 0) return -1;
        pairs[2 * n] = (unsigned short)from;
        pairs[2 * n + 1] = (unsigned short)to;
        n++;
    }
    for (i = 1; i < n; i++)                  /* in order of start */
        for (j = i; j > 0 && pairs[2 * j] < pairs[2 * j - 2]; j--) {
            unsigned short f = pairs[2 * j], t = pairs[2 * j + 1];
            pairs[2 * j] = pairs[2 * j - 2];
            pairs[2 * j + 1] = pairs[2 * j - 1];
            pairs[2 * j - 2] = f;
            pairs[2 * j - 1] = t;
        }
    return n;
}

static int set_exception_range(xr *r, owf_pcal *cal, const owf_xml_node *day, const owf_xml_node *period)
{
    unsigned short pairs[2 * OWF_PCAL_SPANS];
    owf_ptime from, to;
    const char *s;
    long d, last;
    int n = 0, rc;
    s = child_text(r, period, "FromDate");
    if (!s || !parse_time(s, &from)) return OWF_OK;      /* an exception we can't place: left out */
    s = child_text(r, period, "ToDate");
    if (!s || !parse_time(s, &to)) to = from;
    if (child_flag(r, day, "DayWorking") && (n = read_spans(r, day, pairs)) < 0) return OWF_OK;
    d = from / 1440;
    last = to / 1440;
    if (to % 1440 == 0 && last > d) last--;  /* a range ending at 00:00 ends the day before */
    if (last - d >= MAX_EXCEPTION_DAYS) last = d + MAX_EXCEPTION_DAYS - 1;
    for (; d <= last; d++)
        if ((rc = owf_pcal_set_exception(cal, d, n, pairs)) == OWF_ERR_MEMORY) return rc;
    return OWF_OK;
}

static int read_calendar(xr *r, owf_pcal *cal, const owf_xml_node *c)
{
    owf_xml_node *days = owf_xml_child(c, r->ns, "WeekDays"), *wd, *ex;
    int rc;
    for (wd = owf_xml_child(days, r->ns, "WeekDay"); wd; wd = owf_xml_next(wd, r->ns, "WeekDay")) {
        long type;
        if (!child_long(r, wd, "DayType", &type)) continue;
        if (type >= 1 && type <= 7) {
            unsigned short pairs[2 * OWF_PCAL_SPANS];
            int n = child_flag(r, wd, "DayWorking") ? read_spans(r, wd, pairs) : 0;
            if (n >= 0) owf_pcal_set_week(cal, (int)type - 1, n, pairs);
        } else if (type == 0) {
            owf_xml_node *period = owf_xml_child(wd, r->ns, "TimePeriod");
            if (period && (rc = set_exception_range(r, cal, wd, period)) != OWF_OK) return rc;
        }
    }
    ex = owf_xml_child(c, r->ns, "Exceptions");
    for (ex = owf_xml_child(ex, r->ns, "Exception"); ex; ex = owf_xml_next(ex, r->ns, "Exception")) {
        owf_xml_node *period = owf_xml_child(ex, r->ns, "TimePeriod");
        if (period && (rc = set_exception_range(r, cal, ex, period)) != OWF_OK) return rc;
    }
    return OWF_OK;
}

/* MSPDI's link Type: 0 FF, 1 FS, 2 SF, 3 SS. */
static const int link_from_mspdi[4] = { OWF_LINK_FF, OWF_LINK_FS, OWF_LINK_SF, OWF_LINK_SS };
static const int link_to_mspdi[4] = { 1, 3, 0, 2 };

/* DurationFormat codes that count every minute ("em", "eh", "ed", "ew", "emo", "e%" and their "?"s). */
static int format_elapsed(long f)
{
    return f == 4 || f == 6 || f == 8 || f == 10 || f == 12 || f == 20 ||
           f == 36 || f == 38 || f == 40 || f == 42 || f == 44 || f == 52;
}

/* MSPDI's resource Type: 0 material, 1 work, 2 cost. */
static const int res_from_mspdi[3] = { OWF_RES_MATERIAL, OWF_RES_WORK, OWF_RES_COST };
static const int res_to_mspdi[3] = { 1, 0, 2 };

/* Resources (UID 0 is MS Project's own "unassigned" one: left out) and their assignments. */
static int read_resources(xr *r, const owf_xml_node *root, owf_project *p, const uid_map *cals, int ncals,
                          const uid_map *tasks, int ntasks, int *failed)
{
    owf_xml_node *n = owf_xml_child(owf_xml_child(root, r->ns, "Resources"), r->ns, "Resource"), *t;
    uid_map *res = NULL;
    int nres = 0, top_res = 0, top_assign = 0, rc = OWF_OK;
    long v;
    for (t = n; t; t = owf_xml_next(t, r->ns, "Resource")) nres++;
    if (nres && !(res = owf_malloc((size_t)nres * sizeof *res))) return OWF_ERR_MEMORY;
    nres = 0;
    for (t = n; t; t = owf_xml_next(t, r->ns, "Resource")) {
        owf_xml_node *x;
        owf_presource *k;
        long uid = 0;
        int at;
        if (!child_long(r, t, "UID", &uid) || uid <= 0 || uid >= 0x7FFFFFFFL || child_flag(r, t, "IsNull")) continue;
        if ((at = owf_project_add_resource(p, NULL)) < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        k = &p->resources[at];
        k->uid = (int)uid;
        if (uid > top_res) top_res = (int)uid;
        if ((x = owf_xml_child(t, r->ns, "Name")) != NULL) k->name = text_dup(x, failed);
        if ((x = owf_xml_child(t, r->ns, "Initials")) != NULL) k->initials = text_dup(x, failed);
        if ((x = owf_xml_child(t, r->ns, "Group")) != NULL) k->group = text_dup(x, failed);
        if (child_long(r, t, "Type", &v) && v >= 0 && v <= 2) k->type = res_from_mspdi[v];
        if (child_flag(r, t, "IsCostResource")) k->type = OWF_RES_COST;
        if (child_hundredths(r, t, "MaxUnits", &v) && v >= 0) k->max_units = v;
        if (child_hundredths(r, t, "StandardRate", &v) && v >= 0) k->rate = v;
        if (child_long(r, t, "CalendarUID", &v)) k->calendar = find_uid(cals, ncals, v);
        res[nres].uid = uid;
        res[nres].index = at;
        nres++;
    }
    if (nres) qsort(res, (size_t)nres, sizeof *res, cmp_uid);
    n = owf_xml_child(owf_xml_child(root, r->ns, "Assignments"), r->ns, "Assignment");
    for (t = n; t && rc == OWF_OK; t = owf_xml_next(t, r->ns, "Assignment")) {
        long tuid, ruid, units = 100, uid = 0;
        int task, resource, at;
        if (!child_long(r, t, "TaskUID", &tuid) || !child_long(r, t, "ResourceUID", &ruid) ||
            (task = find_uid(tasks, ntasks, tuid)) < 0 || (resource = find_uid(res, nres, ruid)) < 0)
            continue;
        child_hundredths(r, t, "Units", &units);
        if (p->resources[resource].type == OWF_RES_COST) {                    /* the amount is the Cost, in cents */
            long cost;
            if (child_long(r, t, "Cost", &cost)) units = cost;            /* owf_project_assign cuts it */
        }
        if (units < 0) units = 0;
        if ((at = owf_project_assign(p, task, resource, units)) < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        if (child_long(r, t, "UID", &uid) && uid > 0 && uid < 0x7FFFFFFFL) {
            p->assigns[at].uid = (int)uid;
            if (uid > top_assign) top_assign = (int)uid;
        }
    }
    if (top_res >= p->next_resource_uid) p->next_resource_uid = top_res + 1;
    {
        int i;
        for (i = 0; i < p->nassigns; i++)
            if (p->assigns[i].uid > top_assign) top_assign = p->assigns[i].uid;
        p->next_assign_uid = top_assign + 1;
    }
    owf_free(res);
    return rc;
}

/* A task's Baseline 0 (a Baseline with no Number is taken as 0); later baselines (1 to 10) are left out. */
static void read_baseline(xr *r, const owf_xml_node *task, owf_ptask *k)
{
    owf_xml_node *bl;
    for (bl = owf_xml_child(task, r->ns, "Baseline"); bl; bl = owf_xml_next(bl, r->ns, "Baseline")) {
        long number = 0, v;
        owf_ptime when;
        child_long(r, bl, "Number", &number);
        if (number != 0) continue;
        k->baselined = 1;
        if (child_text(r, bl, "Start") && parse_time(r->buf, &when)) k->baseline_start = when;
        if (child_text(r, bl, "Finish") && parse_time(r->buf, &when)) k->baseline_finish = when;
        if (child_text(r, bl, "Duration") && parse_iso_duration(r->buf, &v) && v >= 0) k->baseline_duration = v;
        if (child_text(r, bl, "Work") && parse_iso_duration(r->buf, &v) && v >= 0) k->baseline_work = v;
        if (child_long(r, bl, "Cost", &v)) k->baseline_cost = v < 0 ? 0 : v > 2000000000L ? 2000000000L : v;
        return;
    }
}

static int read_xml(const char *text, size_t length, owf_project **out)
{
    owf_xml *xml;
    owf_xml_node *root, *n, *t;
    owf_project *p;
    uid_map *cals = NULL, *tasks = NULL;
    int ncals = 0, ntasks = 0, error = 0, rc = OWF_OK, failed = 0;
    xr r;
    long v;
    owf_ptime start = 0;

    if (!(xml = owf_xml_parse(text, length, &error))) return error == OWF_ERR_MEMORY ? OWF_ERR_MEMORY : OWF_ERR_CORRUPT;
    root = owf_xml_root(xml);
    if (!root || strcmp(root->name, "Project") != 0 ||
        (strcmp(root->ns, MSPDI_NS) != 0 && strcmp(root->ns, OWF_PFILE_NS) != 0 && root->ns[0])) {
        owf_xml_free(xml);
        return OWF_ERR_FORMAT;
    }
    r.ns = root->ns;
    if (child_text(&r, root, "StartDate")) parse_time(r.buf, &start);
    if (!(p = owf_project_new(start))) {
        owf_xml_free(xml);
        return OWF_ERR_MEMORY;
    }
    if ((n = owf_xml_child(root, r.ns, "Title")) != NULL || (n = owf_xml_child(root, r.ns, "Name")) != NULL)
        p->title = text_dup(n, &failed);
    if (child_long(&r, root, "MinutesPerDay", &v) && v > 0 && v <= 1440) p->minutes_per_day = v;
    if (child_long(&r, root, "MinutesPerWeek", &v) && v > 0 && v <= 10080) p->minutes_per_week = v;

    /* Calendars: the base ones (resource calendars wait for resources). */
    n = owf_xml_child(owf_xml_child(root, r.ns, "Calendars"), r.ns, "Calendar");
    for (t = n; t; t = owf_xml_next(t, r.ns, "Calendar")) ncals++;
    if (ncals && !(cals = owf_malloc((size_t)ncals * sizeof *cals))) rc = OWF_ERR_MEMORY;
    ncals = 0;
    for (t = n; t && rc == OWF_OK; t = owf_xml_next(t, r.ns, "Calendar")) {
        owf_xml_node *name = owf_xml_child(t, r.ns, "Name");
        char *s;
        int at;
        if (owf_xml_child(t, r.ns, "IsBaseCalendar") && !child_flag(&r, t, "IsBaseCalendar")) continue;
        if (!child_long(&r, t, "UID", &v)) continue;
        if (ncals == 0) {                    /* the file's calendars replace Standard */
            owf_pcal_free(&p->calendars[0]);
            p->ncalendars = 0;
        }
        if ((at = owf_project_add_calendar(p, NULL)) < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        if (name && (s = text_dup(name, &failed)) != NULL) p->calendars[at].name = s;
        cals[ncals].uid = v;
        cals[ncals].index = at;
        ncals++;
        rc = read_calendar(&r, &p->calendars[at], t);
    }
    if (ncals) qsort(cals, (size_t)ncals, sizeof *cals, cmp_uid);
    if (child_long(&r, root, "CalendarUID", &v) && (v = find_uid(cals, ncals, v)) >= 0) p->calendar = (int)v;

    /* Tasks, then their links once every UID is known. */
    n = owf_xml_child(owf_xml_child(root, r.ns, "Tasks"), r.ns, "Task");
    for (t = n; t; t = owf_xml_next(t, r.ns, "Task")) ntasks++;
    if (rc == OWF_OK && ntasks && !(tasks = owf_malloc((size_t)ntasks * sizeof *tasks))) rc = OWF_ERR_MEMORY;
    ntasks = 0;
    for (t = n; t && rc == OWF_OK; t = owf_xml_next(t, r.ns, "Task")) {
        owf_xml_node *name = owf_xml_child(t, r.ns, "Name"), *notes = owf_xml_child(t, r.ns, "Notes");
        owf_ptask *k;
        long uid = 0, level = 1, f = 0;
        int at;
        child_long(&r, t, "UID", &uid);
        child_long(&r, t, "OutlineLevel", &level);
        if (level < 1 || child_flag(&r, t, "IsNull")) continue;   /* the project's own summary; blank rows */
        if ((at = owf_project_add_task(p, NULL, 0)) < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        k = &p->tasks[at];
        if (name) k->name = text_dup(name, &failed);
        if (notes) k->notes = text_dup(notes, &failed);
        if (uid > 0 && uid < 0x7FFFFFFFL) k->uid = (int)uid;
        k->level = level > 100 ? 100 : (int)level;
        if (child_text(&r, t, "Duration") && parse_iso_duration(r.buf, &v) && v > 0) k->duration = v;
        child_long(&r, t, "DurationFormat", &f);
        k->elapsed = format_elapsed(f);
        k->estimated = child_flag(&r, t, "Estimated") || (f >= 35 && f <= 53);
        k->milestone = child_flag(&r, t, "Milestone");
        if (child_long(&r, t, "ConstraintType", &v) && v >= OWF_CON_ASAP && v <= OWF_CON_FNLT) k->constraint = (int)v;
        if (child_text(&r, t, "ConstraintDate") && !parse_time(r.buf, &k->constraint_date)) k->constraint_date = 0;
        if (k->constraint != OWF_CON_ASAP && k->constraint != OWF_CON_ALAP && !owf_xml_child(t, r.ns, "ConstraintDate"))
            k->constraint = OWF_CON_ASAP;    /* a dated constraint with no date */
        if (child_long(&r, t, "PercentComplete", &v)) k->percent = v < 0 ? 0 : v > 100 ? 100 : (int)v;
        if (child_long(&r, t, "CalendarUID", &v)) k->calendar = find_uid(cals, ncals, v);
        if (child_text(&r, t, "Start")) parse_time(r.buf, &k->early_start);
        if (child_text(&r, t, "Finish")) parse_time(r.buf, &k->early_finish);
        read_baseline(&r, t, k);
        tasks[ntasks].uid = k->uid;
        tasks[ntasks].index = at;
        ntasks++;
    }
    if (ntasks) qsort(tasks, (size_t)ntasks, sizeof *tasks, cmp_uid);
    for (t = n; t && rc == OWF_OK; t = owf_xml_next(t, r.ns, "Task")) {
        owf_xml_node *l;
        long uid = 0, level = 1;
        int succ;
        child_long(&r, t, "UID", &uid);
        child_long(&r, t, "OutlineLevel", &level);
        if (level < 1 || child_flag(&r, t, "IsNull") || (succ = find_uid(tasks, ntasks, uid)) < 0) continue;
        for (l = owf_xml_child(t, r.ns, "PredecessorLink"); l && rc == OWF_OK; l = owf_xml_next(l, r.ns, "PredecessorLink")) {
            long puid, type = 1, lag = 0, format = 7;
            int pred, percent;
            if (!child_long(&r, l, "PredecessorUID", &puid) || (pred = find_uid(tasks, ntasks, puid)) < 0) continue;
            child_long(&r, l, "Type", &type);
            child_long(&r, l, "LinkLag", &lag);
            child_long(&r, l, "LagFormat", &format);
            if (type < 0 || type > 3) type = 1;
            percent = format == 19 || format == 20 || format == 51 || format == 52;
            rc = owf_project_link(p, pred, succ, link_from_mspdi[type], percent ? lag : lag / 10, percent);
            if (rc == OWF_ERR_FORMAT) rc = OWF_OK;   /* a link to itself: left out */
        }
    }
    if (p->ntasks) {
        int i, top = 0;
        for (i = 0; i < p->ntasks; i++)
            if (p->tasks[i].uid >= top) top = p->tasks[i].uid;
        p->next_uid = top + 1;
    }
    if (rc == OWF_OK) rc = read_resources(&r, root, p, cals, ncals, tasks, ntasks, &failed);
    owf_free(cals);
    owf_free(tasks);
    owf_xml_free(xml);
    if (rc == OWF_OK && failed) rc = OWF_ERR_MEMORY;
    if (rc != OWF_OK) {
        owf_project_free(p);
        return rc;
    }
    *out = p;
    return OWF_OK;
}

/* ---- Writing XML ---- */

static void put_text(owf_buf *b, const char *tag, const char *s)
{
    owf_buf_printf(b, "<%s>", tag);
    owf_buf_puts_xml(b, s ? s : "");
    owf_buf_printf(b, "</%s>", tag);
}

static void put_long(owf_buf *b, const char *tag, long v)
{
    owf_buf_printf(b, "<%s>%ld</%s>", tag, v, tag);
}

static void put_hundredths(owf_buf *b, const char *tag, long v)
{
    owf_buf_printf(b, "<%s>%s%ld.%02ld</%s>", tag, v < 0 ? "-" : "", (v < 0 ? -v : v) / 100, (v < 0 ? -v : v) % 100, tag);
}

static void put_date(owf_buf *b, const char *tag, owf_ptime t)
{
    owf_buf_printf(b, "<%s>", tag);
    put_time(b, t, 0);
    owf_buf_printf(b, "</%s>", tag);
}

static void put_spans(owf_buf *b, const owf_pday *h)
{
    int i;
    if (!h->nspans) return;
    owf_buf_puts(b, "<WorkingTimes>");
    for (i = 0; i < h->nspans; i++)
        owf_buf_printf(b, "<WorkingTime><FromTime>%02u:%02u:00</FromTime><ToTime>%02u:%02u:00</ToTime></WorkingTime>",
                       h->from[i] / 60, h->from[i] % 60, (h->to[i] / 60) % 24, h->to[i] % 60);
    owf_buf_puts(b, "</WorkingTimes>");
}

static int same_hours(const owf_pday *a, const owf_pday *b)
{
    int i;
    if (a->nspans != b->nspans) return 0;
    for (i = 0; i < a->nspans; i++)
        if (a->from[i] != b->from[i] || a->to[i] != b->to[i]) return 0;
    return 1;
}

static void put_calendar(owf_buf *b, const owf_pcal *c, int uid)
{
    int d, i;
    owf_buf_puts(b, "<Calendar>");
    put_long(b, "UID", uid);
    put_text(b, "Name", c->name ? c->name : "");
    put_long(b, "IsBaseCalendar", 1);
    put_long(b, "BaseCalendarUID", -1);
    owf_buf_puts(b, "<WeekDays>");
    for (d = 0; d < 7; d++) {
        owf_buf_puts(b, "<WeekDay>");
        put_long(b, "DayType", d + 1);
        put_long(b, "DayWorking", c->week[d].nspans ? 1 : 0);
        put_spans(b, &c->week[d]);
        owf_buf_puts(b, "</WeekDay>");
    }
    for (i = 0; i < c->nexceptions; ) {      /* runs of days with the same hours, one WeekDay each */
        int j = i + 1;
        while (j < c->nexceptions && c->exceptions[j].day == c->exceptions[j - 1].day + 1 &&
               same_hours(&c->exceptions[j].hours, &c->exceptions[i].hours))
            j++;
        owf_buf_puts(b, "<WeekDay>");
        put_long(b, "DayType", 0);
        put_long(b, "DayWorking", c->exceptions[i].hours.nspans ? 1 : 0);
        owf_buf_puts(b, "<TimePeriod>");
        put_date(b, "FromDate", c->exceptions[i].day * 1440L);
        put_date(b, "ToDate", c->exceptions[j - 1].day * 1440L + 1439);
        owf_buf_puts(b, "</TimePeriod>");
        put_spans(b, &c->exceptions[i].hours);
        owf_buf_puts(b, "</WeekDay>");
        i = j;
    }
    owf_buf_puts(b, "</WeekDays></Calendar>\n");
}

/* DurationFormat: 3 m, 5 h, 7 d (4, 6, 8 elapsed); 39 for an estimated day. */
static int duration_format(const owf_project *p, long minutes, int elapsed)
{
    if (minutes % unit_minutes(p, 2, elapsed) == 0) return elapsed ? 8 : 7;
    if (minutes % 60 == 0) return elapsed ? 6 : 5;
    return elapsed ? 4 : 3;
}

static void write_xml(const owf_project *p, owf_buf *b, const char *ns)
{
    int i, j;
    owf_buf_puts(b, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    owf_buf_printf(b, "<Project xmlns=\"%s\">\n", ns);
    put_long(b, "SaveVersion", 14);
    put_text(b, "Name", p->title);
    put_text(b, "Title", p->title);
    put_long(b, "ScheduleFromStart", 1);
    put_date(b, "StartDate", p->start);
    put_date(b, "FinishDate", p->finish);
    put_long(b, "CalendarUID", p->calendar + 1);
    put_long(b, "MinutesPerDay", p->minutes_per_day);
    put_long(b, "MinutesPerWeek", p->minutes_per_week);
    owf_buf_puts(b, "\n<Calendars>\n");
    for (i = 0; i < p->ncalendars; i++) put_calendar(b, &p->calendars[i], i + 1);
    owf_buf_puts(b, "</Calendars>\n<Tasks>\n");
    for (i = 0; i < p->ntasks; i++) {
        const owf_ptask *t = &p->tasks[i];
        owf_buf_puts(b, "<Task>");
        put_long(b, "UID", t->uid);
        put_long(b, "ID", i + 1);
        put_text(b, "Name", t->name);
        put_long(b, "OutlineLevel", t->level);
        put_date(b, "Start", t->early_start);
        put_date(b, "Finish", t->early_finish);
        owf_buf_puts(b, "<Duration>");
        put_iso_duration(b, t->duration);
        owf_buf_puts(b, "</Duration>");
        put_long(b, "DurationFormat", duration_format(p, t->duration, t->elapsed) + (t->estimated ? 32 : 0));
        if (t->work) {
            owf_buf_puts(b, "<Work>");
            put_iso_duration(b, t->work);
            owf_buf_puts(b, "</Work>");
        }
        put_long(b, "Estimated", t->estimated ? 1 : 0);
        put_long(b, "Milestone", t->milestone ? 1 : 0);
        put_long(b, "Summary", t->summary ? 1 : 0);
        put_long(b, "Critical", t->critical ? 1 : 0);
        put_long(b, "PercentComplete", t->percent);
        if (t->cost) put_long(b, "Cost", t->cost);                      /* in cents */
        put_date(b, "EarlyStart", t->early_start);
        put_date(b, "EarlyFinish", t->early_finish);
        put_date(b, "LateStart", t->late_start);
        put_date(b, "LateFinish", t->late_finish);
        put_long(b, "FreeSlack", clamp(t->free_slack) * 10);
        put_long(b, "TotalSlack", clamp(t->total_slack) * 10);
        put_long(b, "ConstraintType", t->constraint);
        put_long(b, "CalendarUID", t->calendar < 0 ? -1 : t->calendar + 1);
        if (t->constraint != OWF_CON_ASAP && t->constraint != OWF_CON_ALAP) put_date(b, "ConstraintDate", t->constraint_date);
        if (t->notes && t->notes[0]) put_text(b, "Notes", t->notes);
        for (j = 0; j < t->nlinks; j++) {
            const owf_plink *l = &t->links[j];
            int format = l->lag_percent ? 19 : duration_format(p, l->lag < 0 ? -l->lag : l->lag, 0);
            if (l->pred < 0 || l->pred >= p->ntasks) continue;
            owf_buf_puts(b, "<PredecessorLink>");
            put_long(b, "PredecessorUID", p->tasks[l->pred].uid);
            put_long(b, "Type", link_to_mspdi[l->type & 3]);
            put_long(b, "CrossProject", 0);
            put_long(b, "LinkLag", l->lag_percent ? clamp(l->lag) : clamp(l->lag) * 10);   /* a percent as it is */
            put_long(b, "LagFormat", format);
            owf_buf_puts(b, "</PredecessorLink>");
        }
        if (t->baselined) {                                              /* Baseline 0, MS Project's "Baseline" */
            owf_buf_puts(b, "<Baseline>");
            put_long(b, "Number", 0);
            put_date(b, "Start", t->baseline_start);
            put_date(b, "Finish", t->baseline_finish);
            owf_buf_puts(b, "<Duration>");
            put_iso_duration(b, t->baseline_duration);
            owf_buf_puts(b, "</Duration>");
            put_long(b, "DurationFormat", duration_format(p, t->baseline_duration, t->elapsed));
            owf_buf_puts(b, "<Work>");
            put_iso_duration(b, t->baseline_work);
            owf_buf_puts(b, "</Work>");
            put_long(b, "Cost", t->baseline_cost);
            owf_buf_puts(b, "</Baseline>");
        }
        owf_buf_puts(b, "</Task>\n");
    }
    owf_buf_puts(b, "</Tasks>\n<Resources>\n");
    for (i = 0; i < p->nresources; i++) {
        const owf_presource *r = &p->resources[i];
        owf_buf_puts(b, "<Resource>");
        put_long(b, "UID", r->uid);
        put_long(b, "ID", i + 1);
        put_text(b, "Name", r->name);
        put_long(b, "Type", res_to_mspdi[r->type >= 0 && r->type <= 2 ? r->type : 0]);
        if (r->initials && r->initials[0]) put_text(b, "Initials", r->initials);
        if (r->group && r->group[0]) put_text(b, "Group", r->group);
        if (r->type == OWF_RES_WORK) put_hundredths(b, "MaxUnits", r->max_units);
        put_hundredths(b, "StandardRate", r->rate);
        if (r->type != OWF_RES_COST) put_long(b, "StandardRateFormat", r->type == OWF_RES_WORK ? 2 : 8);   /* per hour, per unit */
        if (r->calendar >= 0 && r->calendar < p->ncalendars) put_long(b, "CalendarUID", r->calendar + 1);
        if (r->type == OWF_RES_COST) put_long(b, "IsCostResource", 1);           /* how Project marks one */
        owf_buf_puts(b, "</Resource>\n");
    }
    owf_buf_puts(b, "</Resources>\n<Assignments>\n");
    for (i = 0; i < p->nassigns; i++) {
        const owf_passign *a = &p->assigns[i];
        if (a->task < 0 || a->task >= p->ntasks || a->resource < 0 || a->resource >= p->nresources) continue;
        owf_buf_puts(b, "<Assignment>");
        put_long(b, "UID", a->uid);
        put_long(b, "TaskUID", p->tasks[a->task].uid);
        put_long(b, "ResourceUID", p->resources[a->resource].uid);
        if (p->resources[a->resource].type == OWF_RES_COST) put_long(b, "Cost", a->units);   /* in cents */
        put_hundredths(b, "Units", a->units);
        owf_buf_puts(b, "<Work>");
        put_iso_duration(b, a->work);
        owf_buf_puts(b, "</Work></Assignment>\n");
    }
    owf_buf_puts(b, "</Assignments>\n</Project>\n");
}

/* ---- .gfproj ---- */

static int read_gfproj(const unsigned char *data, size_t length, owf_project **out)
{
    owf_unzip zip;
    unsigned char *mime = NULL, *content = NULL;
    size_t mlen, clen;
    int rc;
    if (!owf_unzip_open(&zip, data, length)) return OWF_ERR_FORMAT;
    if (owf_unzip_has(&zip, "mimetype")) {
        if ((rc = owf_unzip_read(&zip, "mimetype", &mime, &mlen)) != OWF_OK) return rc;
        rc = mlen == strlen(OWF_PFILE_MIME) && memcmp(mime, OWF_PFILE_MIME, mlen) == 0;
        owf_free(mime);
        if (!rc) return OWF_ERR_FORMAT;
    }
    if ((rc = owf_unzip_read(&zip, "content.xml", &content, &clen)) != OWF_OK) return rc;
    rc = read_xml((const char *)content, clen, out);
    owf_free(content);
    return rc;
}

static int write_gfproj(const owf_project *p, owf_buf *b)
{
    owf_buf content, meta;
    owf_zip *zip;
    int rc;
    owf_buf_init(&content);
    owf_buf_init(&meta);
    write_xml(p, &content, OWF_PFILE_NS);
    owf_buf_puts(&meta, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    owf_buf_printf(&meta, "<Meta xmlns=\"%s\"><Generator>GFOpenProject</Generator><FormatVersion>1</FormatVersion>", OWF_PFILE_NS);
    put_text(&meta, "Title", p->title);
    owf_buf_puts(&meta, "</Meta>\n");
    if (content.failed || meta.failed || !(zip = owf_zip_new(b))) {
        rc = OWF_ERR_MEMORY;
    } else {
        rc = owf_zip_add(zip, "mimetype", OWF_PFILE_MIME, strlen(OWF_PFILE_MIME), 0);
        if (rc == OWF_OK) rc = owf_zip_add(zip, "content.xml", content.data, content.length, 1);
        if (rc == OWF_OK) rc = owf_zip_add(zip, "meta.xml", meta.data, meta.length, 1);
        if (owf_zip_finish(zip) != OWF_OK && rc == OWF_OK) rc = OWF_ERR_MEMORY;
    }
    owf_buf_free(&content);
    owf_buf_free(&meta);
    return rc;
}

/* ---- CSV ---- */

static const char *const csv_constraints[8][2] = {
    { "As Soon As Possible", "ASAP" }, { "As Late As Possible", "ALAP" },
    { "Must Start On", "MSO" }, { "Must Finish On", "MFO" },
    { "Start No Earlier Than", "SNET" }, { "Start No Later Than", "SNLT" },
    { "Finish No Earlier Than", "FNET" }, { "Finish No Later Than", "FNLT" },
};
static const char link_names[4][3] = { "FS", "SS", "FF", "SF" };

enum { COL_ID, COL_UID, COL_NAME, COL_LEVEL, COL_DURATION, COL_START, COL_FINISH, COL_PREDECESSORS,
       COL_CONSTRAINT, COL_CONSTRAINT_DATE, COL_PERCENT, COL_MILESTONE, COL_NOTES, COL_RESOURCES, NCOLS };

static const char *const csv_columns[NCOLS] = {
    "ID", "Unique ID", "Name", "Outline Level", "Duration", "Start", "Finish", "Predecessors",
    "Constraint Type", "Constraint Date", "% Complete", "Milestone", "Notes", "Resource Names",
};

static void csv_field(owf_buf *b, const char *s, int sep)
{
    const char *q;
    size_t n = s ? strlen(s) : 0;
    int quote = n && (s[0] == ' ' || s[n - 1] == ' ');
    for (q = s; q && *q && !quote; q++)
        if (*q == sep || *q == '"' || *q == '\r' || *q == '\n') quote = 1;
    if (!quote) {
        if (n) owf_buf_put(b, s, n);
        return;
    }
    owf_buf_putc(b, '"');
    for (q = s; *q; q++) {
        if (*q == '"') owf_buf_putc(b, '"');
        owf_buf_putc(b, *q);
    }
    owf_buf_putc(b, '"');
}

static void put_lag(owf_buf *b, const owf_project *p, const owf_plink *l)
{
    char text[24];
    if (!l->lag) return;
    owf_buf_putc(b, l->lag < 0 ? '-' : '+');
    if (l->lag_percent) {
        owf_buf_printf(b, "%ld%%", l->lag < 0 ? -l->lag : l->lag);
        return;
    }
    owf_pduration_text(p, l->lag < 0 ? -l->lag : l->lag, 0, 0, text);
    owf_buf_puts(b, text);
}

static void put_predecessors(owf_buf *b, const owf_project *p, const owf_ptask *t)
{
    int j, any = 0;
    for (j = 0; j < t->nlinks; j++) {
        const owf_plink *l = &t->links[j];
        if (l->pred < 0 || l->pred >= p->ntasks) continue;
        if (any++) owf_buf_putc(b, ',');
        owf_buf_printf(b, "%d", l->pred + 1);
        if (l->type != OWF_LINK_FS || l->lag) owf_buf_puts(b, link_names[l->type & 3]);
        put_lag(b, p, l);
    }
}

/* "Dale,Bob[50%]": a task's assignments as MS Project types them (units shown when not 100%). */
static void put_resource_names(owf_buf *b, const owf_project *p, int task)
{
    int i, any = 0;
    for (i = 0; i < p->nassigns; i++) {
        const owf_passign *a = &p->assigns[i];
        if (a->task != task || a->resource < 0 || a->resource >= p->nresources) continue;
        if (any++) owf_buf_putc(b, ',');
        owf_buf_puts(b, p->resources[a->resource].name ? p->resources[a->resource].name : "");
        if (a->units != 100) {
            if (p->resources[a->resource].type == OWF_RES_WORK) owf_buf_printf(b, "[%ld%%]", a->units);
            else owf_buf_printf(b, "[%ld.%02ld]", a->units / 100, a->units % 100);
        }
    }
}

void owf_presource_names_text(const owf_project *p, int task, char *out, size_t size)
{
    owf_buf b;
    size_t n;
    if (!out || !size) return;
    out[0] = 0;
    if (!p || task < 0 || task >= p->ntasks) return;
    owf_buf_init(&b);
    put_resource_names(&b, p, task);
    if (!b.failed && b.length) {
        n = b.length < size - 1 ? b.length : size - 1;
        memcpy(out, b.data, n);
        out[n] = 0;
    }
    owf_buf_free(&b);
}

static int find_resource(const owf_project *p, const char *name, size_t n)
{
    int i;
    for (i = 0; i < p->nresources; i++) {
        const char *r = p->resources[i].name ? p->resources[i].name : "";
        if (strlen(r) == n && memcmp(r, name, n) == 0) return i;
    }
    for (i = 0; i < p->nresources; i++)
        if (same_word(name, n, p->resources[i].name ? p->resources[i].name : "")) return i;
    return -1;
}

int owf_presource_names_parse(owf_project *p, int task, const char *text, int create)
{
    const char *s = text;
    int i, k;
    if (!p || task < 0 || task >= p->ntasks || !text) return OWF_ERR_FORMAT;
    for (i = 0, k = 0; i < p->nassigns; i++)        /* the task's assignments are replaced */
        if (p->assigns[i].task != task) p->assigns[k++] = p->assigns[i];
    p->nassigns = k;
    while (*s) {
        const char *name, *end;
        long units = 100;
        int res;
        while (*s == ' ' || *s == ',' || *s == ';') s++;
        if (!*s) break;
        name = s;
        while (*s && *s != ',' && *s != ';' && *s != '[') s++;
        end = s;
        while (end > name && end[-1] == ' ') end--;
        if (*s == '[') {
            num x;
            s++;
            while (*s == ' ') s++;
            if (!take_num(&s, &x) || x.whole < 0) return OWF_ERR_FORMAT;
            if (x.whole > 10000000L) x.whole = 10000000L;
            while (*s == ' ') s++;
            if (*s == '%') units = x.whole, s++;
            else units = x.whole > 100000L ? 10000000L : x.whole * 100 + x.frac / 10;
            while (*s == ' ') s++;
            if (*s++ != ']') return OWF_ERR_FORMAT;
        }
        if (end == name) return OWF_ERR_FORMAT;
        if ((res = find_resource(p, name, (size_t)(end - name))) < 0) {
            char *copy;
            if (!create) return OWF_ERR_FORMAT;
            if (!(copy = dup_n(name, (size_t)(end - name)))) return OWF_ERR_MEMORY;
            res = owf_project_add_resource(p, copy);
            owf_free(copy);
            if (res < 0) return OWF_ERR_MEMORY;
        }
        if (owf_project_assign(p, task, res, units) < 0) return OWF_ERR_MEMORY;
    }
    return OWF_OK;
}

static void write_csv(const owf_project *p, owf_buf *b)
{
    owf_buf cell;
    int i, c;
    for (c = 0; c < NCOLS; c++) {
        if (c) owf_buf_putc(b, ',');
        owf_buf_puts(b, csv_columns[c]);
    }
    owf_buf_puts(b, "\r\n");
    owf_buf_init(&cell);
    for (i = 0; i < p->ntasks; i++) {
        const owf_ptask *t = &p->tasks[i];
        char text[24];
        owf_pduration_text(p, t->duration, t->elapsed, t->estimated, text);
        owf_buf_printf(b, "%d,%d,", i + 1, t->uid);
        csv_field(b, t->name, ',');
        owf_buf_printf(b, ",%d,%s,", t->level, text);
        put_time(b, t->early_start, 1);
        owf_buf_putc(b, ',');
        put_time(b, t->early_finish, 1);
        owf_buf_putc(b, ',');
        cell.length = 0;
        put_predecessors(&cell, p, t);
        owf_buf_putc(&cell, 0);
        csv_field(b, cell.failed ? "" : (const char *)cell.data, ',');
        owf_buf_printf(b, ",%s,", csv_constraints[t->constraint & 7][0]);
        if (t->constraint != OWF_CON_ASAP && t->constraint != OWF_CON_ALAP) put_time(b, t->constraint_date, 1);
        owf_buf_printf(b, ",%d%%,%s,", t->percent, t->milestone ? "Yes" : "No");
        csv_field(b, t->notes, ',');
        owf_buf_putc(b, ',');
        cell.length = 0;
        put_resource_names(&cell, p, i);
        owf_buf_putc(&cell, 0);
        csv_field(b, cell.failed ? "" : (const char *)cell.data, ',');
        owf_buf_puts(b, "\r\n");
        if (cell.failed) b->failed = 1;
    }
    owf_buf_free(&cell);
}

/* One row's fields, in place: each field is NUL-terminated inside the row's copy. */
typedef struct {
    char *text;                          /* the whole file, copied, written over as fields are cut */
    size_t length, at;
    int sep;
} csv_reader;

/* The next row's fields into field[] (at most max); the count, or -1 at the end of the file. */
static int csv_row(csv_reader *r, char **field, int max)
{
    int n = 0;
    if (r->at >= r->length) return -1;
    for (;;) {
        char *out = r->text + r->at, *start = out;
        int end_row = 0;
        if (r->at < r->length && r->text[r->at] == '"') {
            r->at++;
            while (r->at < r->length) {
                char c = r->text[r->at++];
                if (c == '"') {
                    if (r->at < r->length && r->text[r->at] == '"') r->at++;
                    else break;
                }
                *out++ = c;
            }
            while (r->at < r->length && r->text[r->at] != r->sep && r->text[r->at] != '\n' && r->text[r->at] != '\r')
                r->at++;                     /* text after the closing quote is dropped */
        } else {
            while (r->at < r->length && r->text[r->at] != r->sep && r->text[r->at] != '\n' && r->text[r->at] != '\r')
                *out++ = r->text[r->at++];
        }
        if (r->at >= r->length) end_row = 1;
        else if (r->text[r->at] == r->sep) r->at++;
        else {
            if (r->text[r->at] == '\r' && r->at + 1 < r->length && r->text[r->at + 1] == '\n') r->at++;
            r->at++;
            end_row = 1;
        }
        *out = 0;                            /* out never passes the separator or newline it stopped on */
        if (n < max) field[n++] = start;
        if (end_row) return n;
    }
}

static int find_column(const char *name)
{
    int c;
    const char *s = name;
    size_t n;
    while (*s == ' ') s++;
    n = strlen(s);
    while (n && s[n - 1] == ' ') n--;
    for (c = 0; c < NCOLS; c++)
        if (same_word(s, n, csv_columns[c])) return c;
    if (same_word(s, n, "Task Name")) return COL_NAME;
    if (same_word(s, n, "UID")) return COL_UID;
    if (same_word(s, n, "Percent Complete")) return COL_PERCENT;
    return -1;
}

static int parse_constraint(const char *s)
{
    int i;
    size_t n;
    while (*s == ' ') s++;
    n = strlen(s);
    while (n && s[n - 1] == ' ') n--;
    for (i = 0; i < 8; i++)
        if (same_word(s, n, csv_constraints[i][0]) || same_word(s, n, csv_constraints[i][1])) return i;
    return -1;
}

/* "2,3SS+1d,4FF-2h,5SF+50%" onto task succ; ids maps the ID column to tasks (NULL: IDs are row numbers).
 * strict: anything that isn't a predecessor, or names no task or succ itself, is OWF_ERR_FORMAT, and with
 * apply 0 nothing is linked (a check first). Not strict: those are skipped. */
static int parse_predecessors(owf_project *p, int succ, const char *s, const uid_map *ids, int nids, int strict,
                              int apply)
{
    while (*s) {
        long id, lag = 0;
        int type = OWF_LINK_FS, percent = 0, pred, i, rc;
        while (*s == ' ' || *s == ',' || *s == ';') s++;
        if (!*s) break;
        if (!take_long(&s, &id)) {
            if (strict) return OWF_ERR_FORMAT;
            while (*s && *s != ',' && *s != ';') s++;    /* not a predecessor we read */
            continue;
        }
        for (i = 0; i < 4; i++)
            if (owf_lower((unsigned char)s[0]) == owf_lower((unsigned char)link_names[i][0]) &&
                owf_lower((unsigned char)s[1]) == owf_lower((unsigned char)link_names[i][1])) {
                type = i;
                s += 2;
                break;
            }
        while (*s == ' ') s++;
        if (*s == '+' || *s == '-') {
            int neg = *s++ == '-';
            const char *q;
            num milli;
            while (*s == ' ') s++;
            q = s;
            if (take_num(&q, &milli) && *q == '%') {
                lag = milli.whole;
                percent = 1;
                s = q + 1;
            } else if (!take_duration(p, &s, &lag, NULL, NULL)) {
                if (strict) return OWF_ERR_FORMAT;
                lag = 0;
            }
            if (neg) lag = -lag;
        }
        while (*s == ' ') s++;
        if (strict && *s && *s != ',' && *s != ';') return OWF_ERR_FORMAT;
        while (*s && *s != ',' && *s != ';') s++;
        pred = ids ? find_uid(ids, nids, id) : id >= 1 && id <= p->ntasks ? (int)id - 1 : -1;
        if (pred < 0 || pred == succ) {
            if (strict) return OWF_ERR_FORMAT;
            continue;
        }
        if (apply && (rc = owf_project_link(p, pred, succ, type, lag, percent)) == OWF_ERR_MEMORY) return rc;
    }
    return OWF_OK;
}

int owf_ppredecessors_parse(owf_project *p, int succ, const char *text)
{
    int rc;
    if (!p || succ < 0 || succ >= p->ntasks || !text) return OWF_ERR_FORMAT;
    if ((rc = parse_predecessors(p, succ, text, NULL, 0, 1, 0)) != OWF_OK) return rc;
    p->tasks[succ].nlinks = 0;
    return parse_predecessors(p, succ, text, NULL, 0, 1, 1);
}

void owf_ppredecessors_text(const owf_project *p, int row, char *out, size_t size)
{
    owf_buf b;
    size_t n;
    if (!out || !size) return;
    out[0] = 0;
    if (!p || row < 0 || row >= p->ntasks) return;
    owf_buf_init(&b);
    put_predecessors(&b, p, &p->tasks[row]);
    if (!b.failed && b.length) {
        n = b.length < size - 1 ? b.length : size - 1;
        memcpy(out, b.data, n);
        out[n] = 0;
    }
    owf_buf_free(&b);
}

static int read_csv(const unsigned char *data, size_t length, owf_project **out)
{
    csv_reader r;
    char *field[64];
    int column[64], nhead, i, n, rc = OWF_OK, nids = 0, cap = 0, have_start = 0, row = 0;
    uid_map *ids = NULL;
    char **preds = NULL, **names = NULL;
    owf_project *p;
    owf_ptime earliest = 0;
    const unsigned char *d = data;
    size_t commas = 0, semis = 0, tabs = 0;

    if (length >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) d += 3, length -= 3;
    for (i = 0; (size_t)i < length && d[i] != '\n'; i++)     /* the header's separator */
        commas += d[i] == ',', semis += d[i] == ';', tabs += d[i] == '\t';
    if (!(r.text = dup_n((const char *)d, length))) return OWF_ERR_MEMORY;
    r.length = length;
    r.at = 0;
    r.sep = tabs > commas && tabs > semis ? '\t' : semis > commas ? ';' : ',';
    nhead = csv_row(&r, field, 64);
    for (i = 0; i < nhead; i++) column[i] = find_column(field[i]);
    for (i = 0; i < nhead && column[i] != COL_NAME; i++) ;
    if (nhead <= 0 || i == nhead) {
        owf_free(r.text);
        return OWF_ERR_FORMAT;
    }
    if (!(p = owf_project_new(0))) {
        owf_free(r.text);
        return OWF_ERR_MEMORY;
    }
    while (rc == OWF_OK && (n = csv_row(&r, field, 64)) >= 0) {
        owf_ptask *t;
        int at, blank = 1;
        long v;
        for (i = 0; i < n && i < nhead; i++)
            if (field[i][0]) blank = 0;
        if (blank) continue;
        row++;
        if (nids == cap) {
            int grown_cap = cap ? cap * 2 : 64;
            uid_map *gi = owf_realloc(ids, (size_t)grown_cap * sizeof *gi);
            char **gp;
            if (gi) ids = gi;
            gp = gi ? owf_realloc(preds, (size_t)grown_cap * sizeof *gp) : NULL;
            if (gp) preds = gp;
            gp = gp ? owf_realloc(names, (size_t)grown_cap * sizeof *gp) : NULL;
            if (!gp) {
                rc = OWF_ERR_MEMORY;
                break;
            }
            names = gp;
            memset(preds + cap, 0, (size_t)(grown_cap - cap) * sizeof *preds);
            memset(names + cap, 0, (size_t)(grown_cap - cap) * sizeof *names);
            cap = grown_cap;
        }
        if ((at = owf_project_add_task(p, NULL, 480)) < 0) {
            rc = OWF_ERR_MEMORY;
            break;
        }
        t = &p->tasks[at];
        t->milestone = 0;
        ids[nids].uid = row;
        ids[nids].index = at;
        for (i = 0; i < n && i < nhead; i++) {
            const char *s = field[i];
            owf_ptime when;
            switch (column[i]) {
            case COL_ID:
                if (take_long(&s, &v)) ids[nids].uid = v;
                break;
            case COL_UID:
                if (take_long(&s, &v) && v > 0 && v < 0x7FFFFFFFL) t->uid = (int)v;
                break;
            case COL_NAME:
                if (!(t->name = dup_n(s, strlen(s)))) rc = OWF_ERR_MEMORY;
                break;
            case COL_LEVEL:
                if (take_long(&s, &v)) t->level = v < 1 ? 1 : v > 100 ? 100 : (int)v;
                break;
            case COL_DURATION:
                if (owf_pduration_parse(p, s, &v, &t->elapsed, &t->estimated) == OWF_OK) {
                    t->duration = v < 0 ? 0 : v;
                    if (!v) t->milestone = 1;
                }
                break;
            case COL_START:
                if (parse_time(s, &when)) {
                    t->early_start = when;
                    if (!have_start || when < earliest) earliest = when;
                    have_start = 1;
                }
                break;
            case COL_FINISH:
                if (parse_time(s, &when)) t->early_finish = when;
                break;
            case COL_PREDECESSORS:
                if (*s && !(preds[at] = dup_n(s, strlen(s)))) rc = OWF_ERR_MEMORY;
                break;
            case COL_CONSTRAINT:
                if ((v = parse_constraint(s)) >= 0) t->constraint = (int)v;
                break;
            case COL_CONSTRAINT_DATE:
                if (parse_time(s, &when)) t->constraint_date = when;
                break;
            case COL_PERCENT:
                while (*s == ' ') s++;
                if (take_long(&s, &v)) t->percent = v < 0 ? 0 : v > 100 ? 100 : (int)v;
                break;
            case COL_MILESTONE:
                while (*s == ' ') s++;
                t->milestone = owf_lower((unsigned char)*s) == 'y' || *s == '1' || owf_lower((unsigned char)*s) == 't';
                break;
            case COL_NOTES:
                if (*s && !(t->notes = dup_n(s, strlen(s)))) rc = OWF_ERR_MEMORY;
                break;
            case COL_RESOURCES:
                if (*s && !(names[at] = dup_n(s, strlen(s)))) rc = OWF_ERR_MEMORY;
                break;
            default:
                break;
            }
        }
        nids++;
    }
    if (rc == OWF_OK) {
        int top = 0;
        if (nids) qsort(ids, (size_t)nids, sizeof *ids, cmp_uid);
        for (i = 0; i < p->ntasks && rc == OWF_OK; i++) {
            const owf_ptask *t = &p->tasks[i];
            if ((t->constraint != OWF_CON_ASAP && t->constraint != OWF_CON_ALAP) && !t->constraint_date)
                p->tasks[i].constraint = OWF_CON_ASAP;
            if (preds[i]) rc = parse_predecessors(p, i, preds[i], ids, nids, 0, 1);
            if (rc == OWF_OK && names[i] && owf_presource_names_parse(p, i, names[i], 1) == OWF_ERR_MEMORY)
                rc = OWF_ERR_MEMORY;
            if (t->uid > top) top = t->uid;
        }
        p->next_uid = top + 1;
        p->start = p->finish = have_start ? earliest : 0;
    }
    for (i = 0; i < nids; i++) {
        owf_free(preds[i]);
        owf_free(names[i]);
    }
    owf_free(preds);
    owf_free(names);
    owf_free(ids);
    owf_free(r.text);
    if (rc != OWF_OK) {
        owf_project_free(p);
        return rc;
    }
    *out = p;
    return OWF_OK;
}

/* ---- Either way ---- */

int owf_project_read(const unsigned char *data, size_t length, owf_project **out)
{
    if (!out) return OWF_ERR_FORMAT;
    *out = NULL;
    switch (owf_project_detect(data, length)) {
    case OWF_PFILE_GFPROJ: return read_gfproj(data, length, out);
    case OWF_PFILE_MSPDI: return read_xml((const char *)data, length, out);
    case OWF_PFILE_CSV: return read_csv(data, length, out);
    case OWF_PFILE_MPX: return owf_pmpx_read(data, length, out);
    default: return OWF_ERR_FORMAT;
    }
}

int owf_project_write(const owf_project *p, int format, unsigned char **data, size_t *length)
{
    owf_buf b;
    int rc = OWF_OK;
    if (!p || !data || !length) return OWF_ERR_FORMAT;
    *data = NULL;
    *length = 0;
    owf_buf_init(&b);
    switch (format) {
    case OWF_PFILE_GFPROJ: rc = write_gfproj(p, &b); break;
    case OWF_PFILE_MSPDI: write_xml(p, &b, MSPDI_NS); break;
    case OWF_PFILE_CSV: write_csv(p, &b); break;
    default: rc = OWF_ERR_UNSUPPORTED; break;
    }
    if (rc != OWF_OK) {
        owf_buf_free(&b);
        return rc;
    }
    return owf_buf_take(&b, data, length);
}
