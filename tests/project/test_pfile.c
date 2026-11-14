/*
 * GFOpenProject's files on the PC (owf_pfile.c): .gfproj, MSPDI and CSV, each written then read back,
 * and hand-written files read field by field. The MSPDI sample was written by hand from Microsoft's
 * published schema reference; every expected value below was worked out by hand on the Standard
 * calendar with the project starting on Monday 16 November 2026 at 08:00. Run under ASan and UBSan by
 * make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_project.h"
#include "owf_internal.h"

static int fails, checks;

#define D 480L
#define AT(d, h, m) owf_ptime_make(2026, 11, d, h, m)
#define DAYNO(y, m, d) (owf_ptime_make(y, m, d, 0, 0) / 1440)

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)
#define CHECK_S(got, want) do { const char *g_ = (got), *w_ = (want); checks++; if (!g_ || strcmp(g_, w_) != 0) { \
    fails++; printf("FAIL %s:%d: %s is \"%s\", want \"%s\"\n", __FILE__, __LINE__, #got, g_ ? g_ : "(null)", w_); } } while (0)
#define CHECK_HAS(text, part) do { checks++; if (!strstr((const char *)(text), (part))) { fails++; \
    printf("FAIL %s:%d: missing %s\n", __FILE__, __LINE__, (part)); } } while (0)

static const unsigned short short_day[2] = { 9 * 60, 15 * 60 };
static const unsigned short saturday[2] = { 10 * 60, 12 * 60 };

/* Six tasks on two calendars, every kind of link, a summary, a milestone, an elapsed and an estimated
 * duration, constraints, notes that need escaping. */
static owf_project *sample(void)
{
    owf_project *p = owf_project_new(AT(16, 8, 0));
    int c, d;
    owf_pcal *cal;
    CHECK(p != NULL);
    p->title = owf_malloc(8);
    strcpy(p->title, "Kitchen");
    c = owf_project_add_calendar(p, "Short");
    CHECK_N(c, 1);
    cal = &p->calendars[c];
    for (d = 1; d <= 5; d++) owf_pcal_set_week(cal, d, 1, short_day);
    owf_pcal_set_exception(cal, DAYNO(2026, 11, 26), 0, NULL);
    owf_pcal_set_exception(cal, DAYNO(2026, 11, 27), 0, NULL);
    owf_pcal_set_exception(cal, DAYNO(2026, 11, 28), 1, saturday);

    owf_project_add_task(p, "Design, review", 2 * D);
    p->tasks[0].notes = owf_malloc(32);
    strcpy(p->tasks[0].notes, "Ask \"Dale\" <first>");
    owf_project_add_task(p, "Build", 3 * D);
    p->tasks[1].percent = 25;
    owf_project_link(p, 0, 1, OWF_LINK_FS, 240, 0);
    owf_project_add_task(p, "Test", 1440);
    p->tasks[2].elapsed = 1;
    owf_project_link(p, 1, 2, OWF_LINK_SS, 50, 1);
    owf_project_add_task(p, "Ship", 0);
    owf_project_link(p, 2, 3, OWF_LINK_FF, 0, 0);
    owf_project_link(p, 0, 3, OWF_LINK_SF, -D, 0);
    p->tasks[3].constraint = OWF_CON_FNLT;
    p->tasks[3].constraint_date = AT(27, 17, 0);
    owf_project_add_task(p, "Docs", D);
    owf_project_add_task(p, "Guide", D);
    p->tasks[5].level = 2;
    p->tasks[5].estimated = 1;
    p->tasks[5].calendar = 1;
    p->tasks[5].constraint = OWF_CON_SNET;
    p->tasks[5].constraint_date = AT(18, 8, 0);
    d = owf_project_add_resource(p, "Dale");
    p->resources[d].rate = 2000;
    p->resources[d].initials = owf_malloc(2);
    strcpy(p->resources[d].initials, "D");
    p->resources[d].group = owf_malloc(16);
    strcpy(p->resources[d].group, "Build team");
    owf_project_add_resource(p, "Bob");
    owf_project_add_resource(p, "Steel");
    p->resources[2].type = OWF_RES_MATERIAL;
    p->resources[2].rate = 500;
    owf_project_assign(p, 0, 0, 100);
    owf_project_assign(p, 1, 0, 50);
    owf_project_assign(p, 1, 1, 100);
    owf_project_assign(p, 2, 2, 300);
    CHECK_N(owf_project_schedule(p), OWF_OK);
    return p;
}

static int same_day(const owf_pday *a, const owf_pday *b)
{
    int i;
    if (a->nspans != b->nspans) return 0;
    for (i = 0; i < a->nspans; i++)
        if (a->from[i] != b->from[i] || a->to[i] != b->to[i]) return 0;
    return 1;
}

static const char *text(const char *s)
{
    return s ? s : "";
}

/* Every field a file keeps; calendars too when with_calendars. */
static void check_same(const owf_project *a, const owf_project *b, int with_calendars, int line)
{
    int i, j, before = fails;
    CHECK_N(b->ntasks, a->ntasks);
    CHECK_N(b->start, a->start);
    if (b->ntasks != a->ntasks) return;
    for (i = 0; i < a->ntasks; i++) {
        const owf_ptask *x = &a->tasks[i], *y = &b->tasks[i];
        CHECK_S(text(y->name), text(x->name));
        CHECK_S(text(y->notes), text(x->notes));
        CHECK_N(y->uid, x->uid);
        CHECK_N(y->level, x->level);
        CHECK_N(y->duration, x->duration);
        CHECK_N(y->elapsed, x->elapsed);
        CHECK_N(y->estimated, x->estimated);
        CHECK_N(y->milestone, x->milestone);
        CHECK_N(y->constraint, x->constraint);
        if (x->constraint >= OWF_CON_MSO) CHECK_N(y->constraint_date, x->constraint_date);
        CHECK_N(y->percent, x->percent);
        if (with_calendars) CHECK_N(y->calendar, x->calendar);
        CHECK_N(y->nlinks, x->nlinks);
        for (j = 0; j < x->nlinks && j < y->nlinks; j++) {
            CHECK_N(y->links[j].pred, x->links[j].pred);
            CHECK_N(y->links[j].type, x->links[j].type);
            CHECK_N(y->links[j].lag, x->links[j].lag);
            CHECK_N(y->links[j].lag_percent, x->links[j].lag_percent);
        }
    }
    CHECK_N(b->nresources, a->nresources);
    for (i = 0; i < a->nresources && i < b->nresources; i++) {
        CHECK_S(text(b->resources[i].name), text(a->resources[i].name));
        if (with_calendars) {
            CHECK_S(text(b->resources[i].initials), text(a->resources[i].initials));
            CHECK_S(text(b->resources[i].group), text(a->resources[i].group));
            CHECK_N(b->resources[i].uid, a->resources[i].uid);
            CHECK_N(b->resources[i].type, a->resources[i].type);
            CHECK_N(b->resources[i].max_units, a->resources[i].max_units);
            CHECK_N(b->resources[i].rate, a->resources[i].rate);
        }
    }
    CHECK_N(b->nassigns, a->nassigns);
    for (i = 0; i < a->nassigns && i < b->nassigns; i++) {
        CHECK_N(b->assigns[i].task, a->assigns[i].task);
        CHECK_N(b->assigns[i].resource, a->assigns[i].resource);
        CHECK_N(b->assigns[i].units, a->assigns[i].units);
        if (with_calendars) CHECK_N(b->assigns[i].uid, a->assigns[i].uid);
    }
    if (with_calendars) {
        CHECK_S(text(b->title), text(a->title));
        CHECK_N(b->calendar, a->calendar);
        CHECK_N(b->minutes_per_day, a->minutes_per_day);
        CHECK_N(b->ncalendars, a->ncalendars);
        for (i = 0; i < a->ncalendars && i < b->ncalendars; i++) {
            const owf_pcal *x = &a->calendars[i], *y = &b->calendars[i];
            CHECK_S(text(y->name), text(x->name));
            for (j = 0; j < 7; j++) CHECK(same_day(&x->week[j], &y->week[j]));
            CHECK_N(y->nexceptions, x->nexceptions);
            for (j = 0; j < x->nexceptions && j < y->nexceptions; j++) {
                CHECK_N(y->exceptions[j].day, x->exceptions[j].day);
                CHECK(same_day(&x->exceptions[j].hours, &y->exceptions[j].hours));
            }
        }
    }
    if (fails != before) printf("  (from the round trip at line %d)\n", line);
}

/* The dates scheduling works out, the same for both. */
static void check_same_schedule(owf_project *a, owf_project *b)
{
    int i;
    CHECK_N(owf_project_schedule(b), OWF_OK);
    CHECK_N(b->finish, a->finish);
    for (i = 0; i < a->ntasks && i < b->ntasks; i++) {
        CHECK_N(b->tasks[i].early_start, a->tasks[i].early_start);
        CHECK_N(b->tasks[i].early_finish, a->tasks[i].early_finish);
        CHECK_N(b->tasks[i].late_finish, a->tasks[i].late_finish);
        CHECK_N(b->tasks[i].total_slack, a->tasks[i].total_slack);
    }
}

static void test_durations(void)
{
    owf_project *p = owf_project_new(0);
    long m = 0;
    int el = 9, es = 9;
    char out[24];
    CHECK_N(owf_pduration_parse(p, "5d", &m, &el, &es), OWF_OK);
    CHECK(m == 5 * D && el == 0 && es == 0);
    CHECK_N(owf_pduration_parse(p, " 2.5h ", &m, &el, &es), OWF_OK);
    CHECK_N(m, 150);
    CHECK_N(owf_pduration_parse(p, "3ed", &m, &el, &es), OWF_OK);
    CHECK(m == 3 * 1440 && el == 1);
    CHECK_N(owf_pduration_parse(p, "1w?", &m, &el, &es), OWF_OK);
    CHECK(m == 2400 && el == 0 && es == 1);
    CHECK_N(owf_pduration_parse(p, "45", &m, &el, &es), OWF_OK);     /* days when there is no unit */
    CHECK_N(m, 45 * D);
    CHECK_N(owf_pduration_parse(p, "1.25 days", &m, &el, &es), OWF_OK);
    CHECK_N(m, 600);
    CHECK_N(owf_pduration_parse(p, "2 emons", &m, &el, &es), OWF_OK);  /* 30 days of 24 hours */
    CHECK(m == 2 * 43200L && el == 1);
    CHECK_N(owf_pduration_parse(p, "1 mo", &m, &el, &es), OWF_OK);     /* 20 working days */
    CHECK_N(m, 20 * D);
    CHECK_N(owf_pduration_parse(p, "-2 hrs", &m, &el, &es), OWF_OK);
    CHECK_N(m, -120);
    CHECK_N(owf_pduration_parse(p, "99999999999 days", &m, &el, &es), OWF_OK);
    CHECK_N(m, 200000000L);                                           /* cut, not wrapped */
    CHECK_N(owf_pduration_parse(p, "x", &m, &el, &es), OWF_ERR_FORMAT);
    CHECK_N(owf_pduration_parse(p, "5 dayz", &m, &el, &es), OWF_ERR_FORMAT);
    CHECK_N(owf_pduration_parse(p, "5d 3", &m, &el, &es), OWF_ERR_FORMAT);
    owf_pduration_text(p, 5 * D, 0, 0, out);
    CHECK_S(out, "5d");
    owf_pduration_text(p, 150, 0, 0, out);
    CHECK_S(out, "150m");
    owf_pduration_text(p, 3 * 1440, 1, 0, out);
    CHECK_S(out, "3ed");
    owf_pduration_text(p, 90, 1, 0, out);
    CHECK_S(out, "90em");
    owf_pduration_text(p, 120, 0, 1, out);
    CHECK_S(out, "2h?");
    owf_pduration_text(p, 0, 0, 0, out);
    CHECK_S(out, "0d");
    p->minutes_per_day = 450;                                          /* a 7.5 hour day */
    owf_pduration_text(p, 900, 0, 0, out);
    CHECK_S(out, "2d");
    owf_project_free(p);
}

static void test_mspdi_written(void)
{
    owf_project *p = sample(), *q = NULL;
    unsigned char *data;
    size_t length;
    CHECK_N(owf_project_write(p, OWF_PFILE_MSPDI, &data, &length), OWF_OK);
    CHECK_N(strlen((char *)data), length);
    CHECK_N(owf_project_detect(data, length), OWF_PFILE_MSPDI);
    CHECK_HAS(data, "<Project xmlns=\"http://schemas.microsoft.com/project\">");
    CHECK_HAS(data, "<Title>Kitchen</Title>");
    CHECK_HAS(data, "<StartDate>2026-11-16T08:00:00</StartDate>");
    CHECK_HAS(data, "<CalendarUID>1</CalendarUID><MinutesPerDay>480</MinutesPerDay><MinutesPerWeek>2400</MinutesPerWeek>");
    /* Short: Monday's one span; the 26th and 27th off as one run; the Saturday 28th working. */
    CHECK_HAS(data, "<Name>Short</Name><IsBaseCalendar>1</IsBaseCalendar><BaseCalendarUID>-1</BaseCalendarUID><WeekDays>"
                    "<WeekDay><DayType>1</DayType><DayWorking>0</DayWorking></WeekDay>"
                    "<WeekDay><DayType>2</DayType><DayWorking>1</DayWorking><WorkingTimes><WorkingTime>"
                    "<FromTime>09:00:00</FromTime><ToTime>15:00:00</ToTime></WorkingTime></WorkingTimes></WeekDay>");
    CHECK_HAS(data, "<WeekDay><DayType>0</DayType><DayWorking>0</DayWorking><TimePeriod><FromDate>2026-11-26T00:00:00"
                    "</FromDate><ToDate>2026-11-27T23:59:00</ToDate></TimePeriod></WeekDay>");
    CHECK_HAS(data, "<WeekDay><DayType>0</DayType><DayWorking>1</DayWorking><TimePeriod><FromDate>2026-11-28T00:00:00"
                    "</FromDate><ToDate>2026-11-28T23:59:00</ToDate></TimePeriod><WorkingTimes><WorkingTime><FromTime>"
                    "10:00:00</FromTime><ToTime>12:00:00</ToTime></WorkingTime></WorkingTimes></WeekDay>");
    /* Design: two days are 16 hours; the notes escaped. */
    CHECK_HAS(data, "<UID>1</UID><ID>1</ID><Name>Design, review</Name><OutlineLevel>1</OutlineLevel>"
                    "<Start>2026-11-16T08:00:00</Start><Finish>2026-11-17T17:00:00</Finish>"
                    "<Duration>PT16H0M0S</Duration><DurationFormat>7</DurationFormat>");
    CHECK_HAS(data, "<Notes>Ask &quot;Dale&quot; &lt;first&gt;</Notes>");
    /* Build: FS (1) with a 4 hour lag, 2400 tenths of a minute, shown in hours (5); it starts at 13:00
     * on Wednesday (Tuesday 17:00 plus 4 working hours is Wednesday 12:00, the lunch break). */
    CHECK_HAS(data, "<Name>Build</Name><OutlineLevel>1</OutlineLevel><Start>2026-11-18T13:00:00</Start>"
                    "<Finish>2026-11-23T12:00:00</Finish>");
    CHECK_HAS(data, "<PercentComplete>25</PercentComplete>");
    CHECK_HAS(data, "<PredecessorLink><PredecessorUID>1</PredecessorUID><Type>1</Type><CrossProject>0</CrossProject>"
                    "<LinkLag>2400</LinkLag><LagFormat>5</LagFormat></PredecessorLink>");
    /* Test: elapsed days (8); SS (3) at 50% (19). */
    CHECK_HAS(data, "<Duration>PT24H0M0S</Duration><DurationFormat>8</DurationFormat>");
    CHECK_HAS(data, "<PredecessorUID>2</PredecessorUID><Type>3</Type><CrossProject>0</CrossProject>"
                    "<LinkLag>50</LinkLag><LagFormat>19</LagFormat>");
    /* Ship: a milestone; FF (0) from Test, SF (2) a day early from Design; FNLT (7) with its date. */
    CHECK_HAS(data, "<Milestone>1</Milestone>");
    CHECK_HAS(data, "<PredecessorUID>3</PredecessorUID><Type>0</Type>");
    CHECK_HAS(data, "<PredecessorUID>1</PredecessorUID><Type>2</Type><CrossProject>0</CrossProject>"
                    "<LinkLag>-4800</LinkLag><LagFormat>7</LagFormat>");
    CHECK_HAS(data, "<ConstraintType>7</ConstraintType><CalendarUID>-1</CalendarUID>"
                    "<ConstraintDate>2026-11-27T17:00:00</ConstraintDate>");
    /* Docs is a summary over Guide; Guide is an estimated day (39) on Short, SNET (4). */
    CHECK_HAS(data, "<Name>Docs</Name>");
    CHECK_HAS(data, "<Summary>1</Summary>");
    CHECK_HAS(data, "<DurationFormat>39</DurationFormat><Estimated>1</Estimated>");
    CHECK_HAS(data, "<ConstraintType>4</ConstraintType><CalendarUID>2</CalendarUID>"
                    "<ConstraintDate>2026-11-18T08:00:00</ConstraintDate>");

    /* Resources (Type 1 work, 0 material) and assignments: Dale half time on Build's 3 days is 12 hours. */
    CHECK_HAS(data, "<Resources>\n<Resource><UID>1</UID><ID>1</ID><Name>Dale</Name><Type>1</Type><Initials>D</Initials>"
                    "<Group>Build team</Group><MaxUnits>1.00</MaxUnits><StandardRate>20.00</StandardRate>"
                    "<StandardRateFormat>2</StandardRateFormat></Resource>");
    CHECK_HAS(data, "<Resource><UID>3</UID><ID>3</ID><Name>Steel</Name><Type>0</Type><StandardRate>5.00</StandardRate>"
                    "<StandardRateFormat>8</StandardRateFormat></Resource>");
    CHECK_HAS(data, "<Assignment><UID>2</UID><TaskUID>2</TaskUID><ResourceUID>1</ResourceUID><Units>0.50</Units>"
                    "<Work>PT12H0M0S</Work></Assignment>");
    CHECK_HAS(data, "<TaskUID>3</TaskUID><ResourceUID>3</ResourceUID><Units>3.00</Units><Work>PT0H0M0S</Work>");
    CHECK_N(owf_project_read(data, length, &q), OWF_OK);
    if (q) {
        check_same(p, q, 1, __LINE__);
        check_same_schedule(p, q);
        CHECK_N(q->next_uid, 7);
        CHECK_N(q->next_resource_uid, 4);
        CHECK_N(q->next_assign_uid, 5);
        CHECK_N(q->resources[0].cost, p->resources[0].cost);
    }
    owf_project_free(q);
    owf_free(data);
    owf_project_free(p);
}

static void test_gfproj(void)
{
    owf_project *p = sample(), *q = NULL;
    unsigned char *data, *part;
    size_t length, plen;
    owf_unzip zip;
    CHECK_N(owf_project_write(p, OWF_PFILE_GFPROJ, &data, &length), OWF_OK);
    CHECK_N(owf_project_detect(data, length), OWF_PFILE_GFPROJ);
    CHECK(length > 58 && memcmp(data + 30, "mimetype" OWF_PFILE_MIME, 8 + strlen(OWF_PFILE_MIME)) == 0);  /* first, stored */
    CHECK(owf_unzip_open(&zip, data, length));
    CHECK_N(owf_unzip_read(&zip, "content.xml", &part, &plen), OWF_OK);
    CHECK_HAS(part, "<Project xmlns=\"" OWF_PFILE_NS "\">");
    owf_free(part);
    CHECK_N(owf_unzip_read(&zip, "meta.xml", &part, &plen), OWF_OK);
    CHECK_HAS(part, "<Generator>GFOpenProject</Generator><FormatVersion>1</FormatVersion><Title>Kitchen</Title>");
    owf_free(part);
    CHECK_N(owf_project_read(data, length, &q), OWF_OK);
    if (q) {
        check_same(p, q, 1, __LINE__);
        check_same_schedule(p, q);
    }
    owf_project_free(q);
    owf_free(data);
    owf_project_free(p);
}

static void test_csv_written(void)
{
    owf_project *p = sample(), *q = NULL;
    unsigned char *data;
    size_t length;
    int i;
    CHECK_N(owf_project_write(p, OWF_PFILE_CSV, &data, &length), OWF_OK);
    CHECK_N(owf_project_detect(data, length), OWF_PFILE_CSV);
    CHECK_HAS(data, "ID,Unique ID,Name,Outline Level,Duration,Start,Finish,Predecessors,Constraint Type,"
                    "Constraint Date,% Complete,Milestone,Notes,Resource Names\r\n");
    CHECK_HAS(data, "\r\n1,1,\"Design, review\",1,2d,2026-11-16 08:00,2026-11-17 17:00,,As Soon As Possible,,0%,No,"
                    "\"Ask \"\"Dale\"\" <first>\",Dale\r\n");
    CHECK_HAS(data, "\r\n2,2,Build,1,3d,2026-11-18 13:00,2026-11-23 12:00,1FS+4h,As Soon As Possible,,25%,No,,\"Dale[50%],Bob\"\r\n");
    CHECK_HAS(data, ",Steel[3.00]\r\n");
    CHECK_HAS(data, ",1ed,");
    CHECK_HAS(data, ",2SS+50%,");
    CHECK_HAS(data, ",\"3FF,1SF-1d\",Finish No Later Than,2026-11-27 17:00,0%,Yes,");
    CHECK_HAS(data, "\r\n6,6,Guide,2,1d?,");
    CHECK_N(owf_project_read(data, length, &q), OWF_OK);
    if (q) {
        check_same(p, q, 0, __LINE__);     /* a CSV keeps no calendars */
        for (i = 0; i < q->ntasks; i++) q->tasks[i].calendar = p->tasks[i].calendar = -1;
        CHECK_N(owf_project_schedule(p), OWF_OK);
        check_same_schedule(p, q);
    }
    owf_project_free(q);
    owf_free(data);
    owf_project_free(p);
}

static const char mspdi_sample[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<Project xmlns=\"http://schemas.microsoft.com/project\">\n"
    " <Name>Sample.xml</Name><Title>Kitchen &amp; hall</Title>\n"
    " <StartDate>2026-11-16T08:00:00</StartDate>\n"
    " <CalendarUID>3</CalendarUID>\n"
    " <MinutesPerDay>450</MinutesPerDay><MinutesPerWeek>2250</MinutesPerWeek>\n"
    " <Calendars>\n"
    "  <Calendar><UID>3</UID><Name>Office</Name><IsBaseCalendar>1</IsBaseCalendar><BaseCalendarUID>-1</BaseCalendarUID>\n"
    "   <WeekDays>\n"
    "    <WeekDay><DayType>1</DayType><DayWorking>0</DayWorking></WeekDay>\n"
    "    <WeekDay><DayType>2</DayType><DayWorking>1</DayWorking><WorkingTimes>\n"
    "      <WorkingTime><FromTime>13:00:00</FromTime><ToTime>17:30:00</ToTime></WorkingTime>\n"
    "      <WorkingTime><FromTime>08:30:00</FromTime><ToTime>12:00:00</ToTime></WorkingTime></WorkingTimes></WeekDay>\n"
    "    <WeekDay><DayType>0</DayType><DayWorking>0</DayWorking><TimePeriod><FromDate>2026-12-24T00:00:00</FromDate>"
    "<ToDate>2026-12-26T23:59:00</ToDate></TimePeriod></WeekDay>\n"
    "   </WeekDays>\n"
    "   <Exceptions><Exception><TimePeriod><FromDate>2026-12-31T00:00:00</FromDate><ToDate>2026-12-31T23:59:00</ToDate>"
    "</TimePeriod><Name>NYE</Name><DayWorking>1</DayWorking><WorkingTimes><WorkingTime><FromTime>08:30:00</FromTime>"
    "<ToTime>12:00:00</ToTime></WorkingTime></WorkingTimes></Exception></Exceptions>\n"
    "  </Calendar>\n"
    "  <Calendar><UID>9</UID><Name>Bob</Name><IsBaseCalendar>0</IsBaseCalendar><BaseCalendarUID>3</BaseCalendarUID></Calendar>\n"
    "  <Calendar><UID>4</UID><Name>Night &amp; day</Name><IsBaseCalendar>1</IsBaseCalendar><WeekDays><WeekDay><DayType>3"
    "</DayType><DayWorking>1</DayWorking><WorkingTimes><WorkingTime><FromTime>20:00:00</FromTime><ToTime>00:00:00</ToTime>"
    "</WorkingTime></WorkingTimes></WeekDay></WeekDays></Calendar>\n"
    " </Calendars>\n"
    " <Tasks>\n"
    "  <Task><UID>0</UID><ID>0</ID><Name>Kitchen</Name><OutlineLevel>0</OutlineLevel><Summary>1</Summary></Task>\n"
    "  <Task><UID>10</UID><ID>1</ID><Name>Strip out</Name><OutlineLevel>1</OutlineLevel><Duration>PT15H0M0S</Duration>"
    "<DurationFormat>7</DurationFormat><ConstraintType>0</ConstraintType><CalendarUID>-1</CalendarUID>"
    "<Notes>Skip &lt;old&gt; units</Notes></Task>\n"
    "  <Task><UID>11</UID><ID>2</ID><IsNull>1</IsNull></Task>\n"
    "  <Task><UID>12</UID><ID>3</ID><Name>Fit</Name><OutlineLevel>1</OutlineLevel><Duration>PT48H0M0S</Duration>"
    "<DurationFormat>40</DurationFormat><PercentComplete>30</PercentComplete><ConstraintType>4</ConstraintType>"
    "<CalendarUID>4</CalendarUID><ConstraintDate>2026-11-20T08:30:00</ConstraintDate>\n"
    "    <PredecessorLink><PredecessorUID>10</PredecessorUID><Type>1</Type><LinkLag>4500</LinkLag><LagFormat>7</LagFormat>"
    "</PredecessorLink>\n"
    "    <PredecessorLink><PredecessorUID>99</PredecessorUID><Type>1</Type></PredecessorLink>\n"
    "    <PredecessorLink><PredecessorUID>12</PredecessorUID><Type>1</Type></PredecessorLink></Task>\n"
    "  <Task><UID>13</UID><ID>4</ID><Name>Sign off</Name><OutlineLevel>1</OutlineLevel><Duration>PT0H0M0S</Duration>"
    "<Milestone>1</Milestone><ConstraintType>6</ConstraintType>\n"
    "    <PredecessorLink><PredecessorUID>12</PredecessorUID><Type>0</Type><LinkLag>-6000</LinkLag><LagFormat>5</LagFormat>"
    "</PredecessorLink>\n"
    "    <PredecessorLink><PredecessorUID>10</PredecessorUID><Type>3</Type><LinkLag>25</LinkLag><LagFormat>19</LagFormat>"
    "</PredecessorLink></Task>\n"
    " </Tasks>\n"
    " <Resources>\n"
    "  <Resource><UID>0</UID><ID>0</ID><Type>1</Type></Resource>\n"
    "  <Resource><UID>5</UID><ID>1</ID><Name>Ann</Name><Type>1</Type><MaxUnits>0.5</MaxUnits><StandardRate>30"
    "</StandardRate><StandardRateFormat>2</StandardRateFormat><CalendarUID>3</CalendarUID></Resource>\n"
    "  <Resource><UID>6</UID><ID>2</ID><Name>Permit</Name><Type>2</Type></Resource>\n"
    "  <Resource><UID>8</UID><ID>3</ID><Name>Fees</Name><Type>0</Type><IsCostResource>1</IsCostResource></Resource>\n"
    " </Resources>\n"
    " <Assignments>\n"
    "  <Assignment><UID>7</UID><TaskUID>12</TaskUID><ResourceUID>5</ResourceUID><Units>1.5</Units></Assignment>\n"
    "  <Assignment><UID>8</UID><TaskUID>12</TaskUID><ResourceUID>99</ResourceUID><Units>1</Units></Assignment>\n"
    "  <Assignment><UID>9</UID><TaskUID>12</TaskUID><ResourceUID>0</ResourceUID><Units>1</Units></Assignment>\n"
    "  <Assignment><UID>10</UID><TaskUID>77</TaskUID><ResourceUID>5</ResourceUID><Units>1</Units></Assignment>\n"
    "  <Assignment><UID>11</UID><TaskUID>10</TaskUID><ResourceUID>6</ResourceUID><Units>250</Units></Assignment>\n"
    "  <Assignment><UID>12</UID><TaskUID>12</TaskUID><ResourceUID>8</ResourceUID><Cost>123450</Cost><Units>1</Units>"
    "</Assignment>\n"
    " </Assignments>\n"
    "</Project>\n";

static void test_mspdi_read(void)
{
    owf_project *p = NULL;
    const owf_pcal *c;
    const owf_ptask *t;
    CHECK_N(owf_project_read((const unsigned char *)mspdi_sample, sizeof mspdi_sample - 1, &p), OWF_OK);
    if (!p) return;
    CHECK_S(p->title, "Kitchen & hall");
    CHECK_N(p->start, AT(16, 8, 0));
    CHECK_N(p->minutes_per_day, 450);
    CHECK_N(p->minutes_per_week, 2250);
    /* Office and Night & day; Bob, a resource's calendar, waits for resources. */
    CHECK_N(p->ncalendars, 2);
    CHECK_N(p->calendar, 0);
    c = &p->calendars[0];
    CHECK_S(c->name, "Office");
    CHECK_N(c->week[0].nspans, 0);                                   /* Sunday off */
    CHECK_N(c->week[1].nspans, 2);                                   /* Monday, put in order */
    CHECK(c->week[1].from[0] == 510 && c->week[1].to[0] == 720 && c->week[1].from[1] == 780 && c->week[1].to[1] == 1050);
    CHECK(c->week[2].nspans == 2 && c->week[2].from[0] == 480);      /* not in the file: Standard's */
    CHECK_N(c->nexceptions, 4);                                      /* 24, 25, 26 and 31 December */
    if (c->nexceptions == 4) {
        CHECK_N(c->exceptions[0].day, DAYNO(2026, 12, 24));
        CHECK_N(c->exceptions[2].day, DAYNO(2026, 12, 26));
        CHECK_N(c->exceptions[2].hours.nspans, 0);
        CHECK_N(c->exceptions[3].day, DAYNO(2026, 12, 31));
        CHECK(c->exceptions[3].hours.nspans == 1 && c->exceptions[3].hours.from[0] == 510 &&
              c->exceptions[3].hours.to[0] == 720);
    }
    c = &p->calendars[1];
    CHECK_S(c->name, "Night & day");
    CHECK(c->week[2].nspans == 1 && c->week[2].from[0] == 1200 && c->week[2].to[0] == 1440);  /* to midnight */
    /* The project's own summary (UID 0) and the blank row are left out. */
    CHECK_N(p->ntasks, 3);
    if (p->ntasks != 3) {
        owf_project_free(p);
        return;
    }
    t = &p->tasks[0];
    CHECK_S(t->name, "Strip out");
    CHECK_S(t->notes, "Skip <old> units");
    CHECK(t->uid == 10 && t->duration == 900 && !t->elapsed && !t->estimated && t->calendar == -1 && !t->milestone);
    t = &p->tasks[1];
    CHECK_S(t->name, "Fit");
    CHECK(t->uid == 12 && t->duration == 2880 && t->elapsed && t->estimated && t->percent == 30);
    CHECK(t->constraint == OWF_CON_SNET && t->constraint_date == AT(20, 8, 30) && t->calendar == 1);
    CHECK_N(t->nlinks, 1);                                           /* UID 99 is not there; 12 is itself */
    CHECK(t->links[0].pred == 0 && t->links[0].type == OWF_LINK_FS && t->links[0].lag == 450 && !t->links[0].lag_percent);
    t = &p->tasks[2];
    CHECK(t->uid == 13 && t->level == 1 && t->milestone && t->duration == 0);
    CHECK_N(t->constraint, OWF_CON_ASAP);                            /* FNET with no date */
    CHECK_N(t->nlinks, 2);
    CHECK(t->links[0].pred == 1 && t->links[0].type == OWF_LINK_FF && t->links[0].lag == -600);
    CHECK(t->links[1].pred == 0 && t->links[1].type == OWF_LINK_SS && t->links[1].lag == 25 && t->links[1].lag_percent);
    CHECK_N(p->next_uid, 14);
    /* Ann (half time, 30.00 an hour, on Office), Permit (a cost) and Fees (a cost, marked the way Project
     * marks one, its amount the assignment's Cost in cents); UID 0 and the assignments to resources or tasks
     * not there are left out. */
    CHECK_N(p->nresources, 3);
    CHECK_S(p->resources[0].name, "Ann");
    CHECK(p->resources[0].uid == 5 && p->resources[0].type == OWF_RES_WORK && p->resources[0].max_units == 50);
    CHECK(p->resources[0].rate == 3000 && p->resources[0].calendar == 0);
    CHECK(p->resources[1].type == OWF_RES_COST && p->resources[1].max_units == 100);
    CHECK(p->resources[2].type == OWF_RES_COST && p->resources[2].uid == 8);
    CHECK_N(p->nassigns, 3);
    CHECK(p->assigns[2].task == 1 && p->assigns[2].resource == 2 && p->assigns[2].units == 123450);
    CHECK(p->assigns[0].task == 1 && p->assigns[0].resource == 0 && p->assigns[0].units == 150 && p->assigns[0].uid == 7);
    CHECK(p->assigns[1].task == 0 && p->assigns[1].resource == 1 && p->assigns[1].units == 25000);
    CHECK_N(p->next_resource_uid, 9);
    CHECK_N(p->next_assign_uid, 13);
    CHECK_N(owf_project_schedule(p), OWF_OK);
    CHECK(p->resources[0].overallocated);                            /* 150% of a half-time person */
    CHECK_N(p->resources[1].cost, 25000);
    CHECK_N(p->resources[2].cost, 123450);
    CHECK_N(p->assigns[0].work, 2880 * 3 / 2);                       /* Fit's 48 elapsed hours, half again */
    owf_project_free(p);
}

static const char csv_sample[] =
    "\xEF\xBB\xBF" "ID;Task Name;Duration;Start;Predecessors;Notes;Something else;Resource Names\r\n"
    "1;Plan;5 days;2026-11-16 08:00;;;x;Ann\r\n"
    "\r\n"
    "2;\"Build; test\";1.5 wks?;2026-11-23 08:00;1SS+2 days;\"line one\nline \"\"two\"\"\";;ann [ 25 % ], Bob\r\n"
    "3;Done;0 days;;2FF-4 hrs,1;;;Steel[2.5]\r\n"
    "5;Wait;3 edays;;3FS+50%,7,junk;";

static void test_csv_read(void)
{
    owf_project *p = NULL;
    const owf_ptask *t;
    CHECK_N(owf_project_detect((const unsigned char *)csv_sample, sizeof csv_sample - 1), OWF_PFILE_CSV);
    CHECK_N(owf_project_read((const unsigned char *)csv_sample, sizeof csv_sample - 1, &p), OWF_OK);
    if (!p) return;
    CHECK_N(p->start, AT(16, 8, 0));                                 /* the earliest Start */
    CHECK_N(p->ntasks, 4);
    if (p->ntasks != 4) {
        owf_project_free(p);
        return;
    }
    t = &p->tasks[0];
    CHECK_S(t->name, "Plan");
    CHECK(t->duration == 5 * D && !t->milestone && t->nlinks == 0 && t->notes == NULL);
    t = &p->tasks[1];
    CHECK_S(t->name, "Build; test");
    CHECK_S(t->notes, "line one\nline \"two\"");
    CHECK(t->duration == 3600 && t->estimated && !t->elapsed);
    CHECK(t->nlinks == 1 && t->links[0].pred == 0 && t->links[0].type == OWF_LINK_SS && t->links[0].lag == 2 * D);
    t = &p->tasks[2];
    CHECK(t->milestone && t->duration == 0 && t->nlinks == 2);
    CHECK(t->links[0].pred == 1 && t->links[0].type == OWF_LINK_FF && t->links[0].lag == -240);
    CHECK(t->links[1].pred == 0 && t->links[1].type == OWF_LINK_FS && t->links[1].lag == 0);
    t = &p->tasks[3];
    CHECK(t->duration == 3 * 1440 && t->elapsed);
    CHECK_N(t->nlinks, 1);                                           /* 7 is not there; "junk" is not one */
    CHECK(t->links[0].pred == 2 && t->links[0].lag == 50 && t->links[0].lag_percent);
    /* Resource Names: made as they are first named, matched again whatever their case. */
    CHECK_N(p->nresources, 3);
    CHECK_S(p->resources[0].name, "Ann");
    CHECK_S(p->resources[1].name, "Bob");
    CHECK_S(p->resources[2].name, "Steel");
    CHECK_N(p->nassigns, 4);
    CHECK(p->assigns[0].task == 0 && p->assigns[0].resource == 0 && p->assigns[0].units == 100);
    CHECK(p->assigns[1].task == 1 && p->assigns[1].resource == 0 && p->assigns[1].units == 25);
    CHECK(p->assigns[2].task == 1 && p->assigns[2].resource == 1 && p->assigns[2].units == 100);
    CHECK(p->assigns[3].task == 2 && p->assigns[3].resource == 2 && p->assigns[3].units == 250);
    {
        char names[64];
        owf_presource_names_text(p, 1, names, sizeof names);
        CHECK_S(names, "Ann[25%],Bob");
        CHECK_N(owf_presource_names_parse(p, 1, "Bob, Zed", 0), OWF_ERR_FORMAT);  /* Zed isn't there */
        CHECK_N(owf_presource_names_parse(p, 1, "Bob[", 1), OWF_ERR_FORMAT);
        CHECK_N(owf_presource_names_parse(p, 1, "[50%]", 1), OWF_ERR_FORMAT);
        CHECK_N(owf_presource_names_parse(p, 1, "Bob[50%],Zed", 1), OWF_OK);
        owf_presource_names_text(p, 1, names, sizeof names);
        CHECK_S(names, "Bob[50%],Zed");
        CHECK_N(p->nresources, 4);
        owf_presource_names_text(p, 1, names, 6);                    /* cut to fit */
        CHECK_S(names, "Bob[5");
    }
    CHECK_N(owf_project_schedule(p), OWF_OK);
    owf_project_free(p);
}

static void test_damaged(void)
{
    owf_project *p = NULL;
    owf_buf b;
    owf_zip *zip;
    unsigned char *data;
    size_t length;
    static const char cut[] = "<Project xmlns=\"http://schemas.microsoft.com/project\"><Tasks><Task>";
    static const char other[] = "<Workbook xmlns=\"urn:x\"/>";
    static const char big[] = "<Project><Tasks><Task><UID>1</UID><OutlineLevel>1</OutlineLevel>"
                              "<Duration>PT99999999999H0M0S</Duration><PredecessorLink><PredecessorUID>1"
                              "</PredecessorUID><LinkLag>99999999999999</LinkLag></PredecessorLink></Task></Tasks></Project>";
    static const char no_name[] = "a,b\r\n1,2\r\n";

    CHECK_N(owf_project_read((const unsigned char *)cut, sizeof cut - 1, &p), OWF_ERR_CORRUPT);
    CHECK(p == NULL);
    CHECK_N(owf_project_read((const unsigned char *)other, sizeof other - 1, &p), OWF_ERR_FORMAT);
    CHECK_N(owf_project_read((const unsigned char *)no_name, sizeof no_name - 1, &p), OWF_ERR_FORMAT);
    CHECK_N(owf_project_read(NULL, 0, &p), OWF_ERR_FORMAT);
    /* No namespace is read too; numbers too big are cut, not wrapped. */
    CHECK_N(owf_project_read((const unsigned char *)big, sizeof big - 1, &p), OWF_OK);
    if (p) {
        CHECK_N(p->ntasks, 1);
        CHECK_N(p->tasks[0].duration, 200000000L);
        CHECK_N(p->tasks[0].nlinks, 0);                              /* to itself */
        owf_project_free(p);
        p = NULL;
    }
    /* A zip with no content.xml, and one that says it is something else. */
    owf_buf_init(&b);
    zip = owf_zip_new(&b);
    owf_zip_add(zip, "mimetype", "text/plain", 10, 0);
    owf_zip_add(zip, "other.xml", "<x/>", 4, 1);
    CHECK_N(owf_zip_finish(zip), OWF_OK);
    CHECK_N(owf_project_detect(b.data, b.length), -1);
    CHECK_N(owf_project_read(b.data, b.length, &p), OWF_ERR_FORMAT);
    owf_buf_free(&b);
    owf_buf_init(&b);
    zip = owf_zip_new(&b);
    owf_zip_add(zip, "mimetype", "text/plain", 10, 0);
    owf_zip_add(zip, "content.xml", mspdi_sample, sizeof mspdi_sample - 1, 1);
    CHECK_N(owf_zip_finish(zip), OWF_OK);
    CHECK_N(owf_project_read(b.data, b.length, &p), OWF_ERR_FORMAT);
    owf_buf_free(&b);
    /* An unknown format to write. */
    p = owf_project_new(0);
    CHECK_N(owf_project_write(p, 9, &data, &length), OWF_ERR_UNSUPPORTED);
    owf_project_free(p);
}

/* A cost resource goes out as Project marks one, its amount as the assignment's Cost in cents, and comes back. */
static void test_cost_resource(void)
{
    owf_project *p = owf_project_new(AT(16, 8, 0)), *q = NULL;
    unsigned char *data;
    size_t length;
    int r;
    if (!p) return;
    owf_project_add_task(p, "Plan", 480);
    r = owf_project_add_resource(p, "Fees");
    p->resources[r].type = OWF_RES_COST;
    CHECK_N(owf_project_assign(p, 0, r, 123450), 0);
    CHECK_N(owf_project_schedule(p), OWF_OK);
    CHECK_N(owf_project_write(p, OWF_PFILE_MSPDI, &data, &length), OWF_OK);
    CHECK_HAS(data, "<Name>Fees</Name><Type>2</Type><StandardRate>0.00</StandardRate><IsCostResource>1</IsCostResource>"
                    "</Resource>");
    CHECK_HAS(data, "<ResourceUID>1</ResourceUID><Cost>123450</Cost><Units>1234.50</Units>");
    CHECK_N(owf_project_read(data, length, &q), OWF_OK);
    if (q) {
        CHECK(q->nresources == 1 && q->resources[0].type == OWF_RES_COST);
        CHECK(q->nassigns == 1 && q->assigns[0].units == 123450);
        CHECK_N(owf_project_schedule(q), OWF_OK);
        CHECK_N(q->resources[0].cost, 123450);
    }
    owf_project_free(q);
    owf_free(data);
    owf_project_free(p);
}

/* A baseline goes out as Baseline 0 and comes back; task Work and Cost are written for other programs. */
static void test_baseline(void)
{
    static const char hand[] =
        "<?xml version=\"1.0\"?>\n<Project xmlns=\"http://schemas.microsoft.com/project\">"
        "<StartDate>2026-11-16T08:00:00</StartDate><Tasks>"
        "<Task><UID>1</UID><ID>1</ID><Name>Dig</Name><OutlineLevel>1</OutlineLevel><Duration>PT8H0M0S</Duration>"
        "<Baseline><Number>1</Number><Start>2026-11-01T08:00:00</Start></Baseline>"
        "<Baseline><Start>2026-11-16T08:00:00</Start><Finish>2026-11-16T17:00:00</Finish>"
        "<Duration>PT8H0M0S</Duration><Work>PT4H0M0S</Work><Cost>12345</Cost></Baseline></Task>"
        "<Task><UID>2</UID><ID>2</ID><Name>Fill</Name><OutlineLevel>1</OutlineLevel><Duration>PT8H0M0S</Duration>"
        "<Baseline><Number>2</Number><Start>2026-11-01T08:00:00</Start></Baseline></Task>"
        "</Tasks></Project>\n";
    owf_project *p = sample(), *q = NULL;
    unsigned char *data;
    size_t length;
    int i;
    owf_project_set_baseline(p, 0, -1);
    p->tasks[0].duration = 3 * D;                                    /* Design slips a day */
    CHECK_N(owf_project_schedule(p), OWF_OK);
    CHECK_N(owf_project_write(p, OWF_PFILE_MSPDI, &data, &length), OWF_OK);
    CHECK_HAS(data, "<DurationFormat>7</DurationFormat><Work>PT24H0M0S</Work><Estimated>");
    CHECK_HAS(data, "<PercentComplete>0</PercentComplete><Cost>48000</Cost><EarlyStart>");
    CHECK_HAS(data, "<Baseline><Number>0</Number><Start>2026-11-16T08:00:00</Start><Finish>2026-11-17T17:00:00</Finish>"
                    "<Duration>PT16H0M0S</Duration><DurationFormat>7</DurationFormat><Work>PT16H0M0S</Work>"
                    "<Cost>32000</Cost></Baseline></Task>");
    CHECK_N(owf_project_read(data, length, &q), OWF_OK);
    if (q) {
        CHECK_N(owf_project_schedule(q), OWF_OK);
        CHECK_N(q->ntasks, p->ntasks);
        for (i = 0; i < p->ntasks && i < q->ntasks; i++) {
            const owf_ptask *a = &p->tasks[i], *b = &q->tasks[i];
            CHECK(b->baselined && b->baseline_start == a->baseline_start && b->baseline_finish == a->baseline_finish);
            CHECK(b->baseline_duration == a->baseline_duration && b->baseline_work == a->baseline_work);
            CHECK(b->baseline_cost == a->baseline_cost && b->work == a->work && b->cost == a->cost);
            CHECK_N(owf_ptask_variance(q, i, OWF_VAR_FINISH), owf_ptask_variance(p, i, OWF_VAR_FINISH));
        }
        CHECK_N(owf_ptask_variance(q, 0, OWF_VAR_FINISH), D);
    }
    owf_project_free(q);
    q = NULL;
    owf_free(data);
    /* By hand: a Baseline with no Number is Baseline 0; Baselines 1 to 10 are left out. */
    CHECK_N(owf_project_read((const unsigned char *)hand, sizeof hand - 1, &q), OWF_OK);
    if (q && q->ntasks == 2) {
        CHECK(q->tasks[0].baselined && q->tasks[0].baseline_start == AT(16, 8, 0));
        CHECK(q->tasks[0].baseline_finish == AT(16, 17, 0) && q->tasks[0].baseline_duration == D);
        CHECK(q->tasks[0].baseline_work == 240 && q->tasks[0].baseline_cost == 12345);
        CHECK(!q->tasks[1].baselined);
    } else
        CHECK(q && q->ntasks == 2);
    owf_project_free(q);
    owf_project_free(p);
}

int main(void)
{
    test_durations();
    test_mspdi_written();
    test_gfproj();
    test_csv_written();
    test_mspdi_read();
    test_csv_read();
    test_cost_resource();
    test_baseline();
    test_damaged();
    if (fails) {
        printf("project file tests: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("project file tests passed (%d checks)\n", checks);
    return 0;
}
