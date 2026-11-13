/*
 * owf_pcal.c: GFOpenProject's times and working calendars (owf_project.h). Integer arithmetic only.
 * Dates: the proleptic Gregorian calendar, counted in days from 1 Jan 2000 (a Saturday).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "owf_project.h"

#include <string.h>

#define DAY 1440L
#define FAR_DAYS 400000L                 /* about 1100 years: no working time beyond this is "none" */

static long floor_div(long a, long b)
{
    long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* Days from 1 Jan 2000 to y-m-d. */
static long days_from_civil(long y, int m, int d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = floor_div(y, 400);
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 730425L;  /* 730425: 1 Mar 0000 to 1 Jan 2000 */
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp, year;
    z += 730425L;
    era = floor_div(z, 146097L);
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(year + (*m <= 2));
}

owf_ptime owf_ptime_make(int year, int month, int day, int hour, int minute)
{
    return days_from_civil(year, month, day) * DAY + hour * 60L + minute;
}

void owf_ptime_split(owf_ptime t, int *year, int *month, int *day, int *hour, int *minute)
{
    long d = floor_div(t, DAY), m = t - d * DAY;
    int y, mo, dd;
    civil_from_days(d, &y, &mo, &dd);
    if (year) *year = y;
    if (month) *month = mo;
    if (day) *day = dd;
    if (hour) *hour = (int)(m / 60);
    if (minute) *minute = (int)(m % 60);
}

int owf_ptime_weekday(owf_ptime t)
{
    long d = floor_div(t, DAY);
    return (int)((d % 7 + 7 + 6) % 7);   /* 1 Jan 2000 was a Saturday (6) */
}

/* ---- Calendars ---- */

static int set_day(owf_pday *day, int nspans, const unsigned short *pairs)
{
    int i;
    if (nspans < 0 || nspans > OWF_PCAL_SPANS) return OWF_ERR_FORMAT;
    for (i = 0; i < nspans; i++) {
        unsigned from = pairs[2 * i], to = pairs[2 * i + 1];
        if (from >= to || to > DAY || (i && from < pairs[2 * i - 1])) return OWF_ERR_FORMAT;
    }
    memset(day, 0, sizeof *day);
    day->nspans = (unsigned char)nspans;
    for (i = 0; i < nspans; i++) {
        day->from[i] = pairs[2 * i];
        day->to[i] = pairs[2 * i + 1];
    }
    return OWF_OK;
}

void owf_pcal_standard(owf_pcal *cal)
{
    static const unsigned short hours[4] = { 8 * 60, 12 * 60, 13 * 60, 17 * 60 };
    int d;
    for (d = 0; d < 7; d++) {
        memset(&cal->week[d], 0, sizeof cal->week[d]);
        if (d >= 1 && d <= 5) set_day(&cal->week[d], 2, hours);
    }
}

int owf_pcal_set_week(owf_pcal *cal, int weekday, int nspans, const unsigned short *pairs)
{
    if (!cal || weekday < 0 || weekday > 6) return OWF_ERR_FORMAT;
    return set_day(&cal->week[weekday], nspans, pairs);
}

static int find_exception(const owf_pcal *cal, long day)
{
    int lo = 0, hi = cal->nexceptions - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (cal->exceptions[mid].day == day) return mid;
        if (cal->exceptions[mid].day < day) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1 - lo;                      /* where it would go */
}

int owf_pcal_set_exception(owf_pcal *cal, long day, int nspans, const unsigned short *pairs)
{
    owf_pday hours;
    int at, rc;
    if (!cal) return OWF_ERR_FORMAT;
    if ((rc = set_day(&hours, nspans, pairs)) != OWF_OK) return rc;
    at = find_exception(cal, day);
    if (at < 0) {
        at = -1 - at;
        if (cal->nexceptions == cal->capexceptions) {
            int cap = cal->capexceptions ? cal->capexceptions * 2 : 8;
            owf_pexception *grown = owf_realloc(cal->exceptions, (size_t)cap * sizeof *grown);
            if (!grown) return OWF_ERR_MEMORY;
            cal->exceptions = grown;
            cal->capexceptions = cap;
        }
        memmove(&cal->exceptions[at + 1], &cal->exceptions[at], (size_t)(cal->nexceptions - at) * sizeof *cal->exceptions);
        cal->nexceptions++;
        cal->exceptions[at].day = day;
    }
    cal->exceptions[at].hours = hours;
    return OWF_OK;
}

void owf_pcal_free(owf_pcal *cal)
{
    if (!cal) return;
    owf_free(cal->name);
    owf_free(cal->exceptions);
    cal->name = NULL;
    cal->exceptions = NULL;
    cal->nexceptions = cal->capexceptions = 0;
}

const owf_pday *owf_pcal_day(const owf_pcal *cal, long day)
{
    int at = find_exception(cal, day);
    if (at >= 0) return &cal->exceptions[at].hours;
    return &cal->week[(int)((day % 7 + 7 + 6) % 7)];
}

/* Whether the calendar works at all; and the last exception day, past which only the week counts. */
static int works(const owf_pcal *cal)
{
    int i;
    for (i = 0; i < 7; i++) if (cal->week[i].nspans) return 1;
    for (i = 0; i < cal->nexceptions; i++) if (cal->exceptions[i].hours.nspans) return 1;
    return 0;
}

owf_ptime owf_pcal_add(const owf_pcal *cal, owf_ptime t, long minutes)
{
    long day = floor_div(t, DAY), m = t - day * DAY, steps;
    int i;
    if (!minutes || !works(cal)) return t;
    if (minutes > 0) {
        for (steps = 0; steps < FAR_DAYS; steps++, day++, m = 0) {
            const owf_pday *h = owf_pcal_day(cal, day);
            for (i = 0; i < h->nspans; i++) {
                long start, avail;
                if (m >= h->to[i]) continue;
                start = m > h->from[i] ? m : h->from[i];
                avail = h->to[i] - start;
                if (minutes <= avail) return day * DAY + start + minutes;
                minutes -= avail;
                m = h->to[i];
            }
        }
    } else {
        minutes = -minutes;
        for (steps = 0; steps < FAR_DAYS; steps++, day--, m = DAY) {
            const owf_pday *h = owf_pcal_day(cal, day);
            for (i = h->nspans - 1; i >= 0; i--) {
                long end, avail;
                if (m <= h->from[i]) continue;
                end = m < h->to[i] ? m : h->to[i];
                avail = end - h->from[i];
                if (minutes <= avail) return day * DAY + end - minutes;
                minutes -= avail;
                m = h->from[i];
            }
        }
    }
    return t;
}

long owf_pcal_work(const owf_pcal *cal, owf_ptime a, owf_ptime b)
{
    long day, last, total = 0;
    int i;
    if (a == b) return 0;
    if (b < a) return -owf_pcal_work(cal, b, a);
    last = floor_div(b, DAY);
    for (day = floor_div(a, DAY); day <= last; day++) {
        const owf_pday *h = owf_pcal_day(cal, day);
        long lo = day == floor_div(a, DAY) ? a - day * DAY : 0, hi = day == last ? b - day * DAY : DAY;
        for (i = 0; i < h->nspans; i++) {
            long from = h->from[i] > lo ? h->from[i] : lo, to = h->to[i] < hi ? h->to[i] : hi;
            if (to > from) total += to - from;
        }
    }
    return total;
}

owf_ptime owf_pcal_next_start(const owf_pcal *cal, owf_ptime t)
{
    long day = floor_div(t, DAY), m = t - day * DAY, steps;
    int i;
    if (!works(cal)) return t;
    for (steps = 0; steps < FAR_DAYS; steps++, day++, m = 0) {
        const owf_pday *h = owf_pcal_day(cal, day);
        for (i = 0; i < h->nspans; i++) {
            if (m < h->from[i]) return day * DAY + h->from[i];
            if (m < h->to[i]) return day * DAY + m;
        }
    }
    return t;
}

owf_ptime owf_pcal_prev_end(const owf_pcal *cal, owf_ptime t)
{
    long day = floor_div(t, DAY), m = t - day * DAY, steps;
    int i;
    if (!works(cal)) return t;
    for (steps = 0; steps < FAR_DAYS; steps++, day--, m = DAY) {
        const owf_pday *h = owf_pcal_day(cal, day);
        for (i = h->nspans - 1; i >= 0; i--) {
            if (m > h->to[i]) return day * DAY + h->to[i];
            if (m > h->from[i]) return day * DAY + m;
        }
    }
    return t;
}
