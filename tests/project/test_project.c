/*
 * GFOpenProject's plan model and scheduler on the PC (libowf/include/owf_project.h). Every expected
 * date below was worked out by hand on the Standard calendar (Monday to Friday, 08:00-12:00 and
 * 13:00-17:00), with the project starting on Monday 16 November 2026 at 08:00. A day is 480 minutes.
 * Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>
#include "owf_project.h"

static int fails, checks;

#define D 480L
#define AT(d, h, m) owf_ptime_make(2026, 11, d, h, m)

static void show(owf_ptime t, char *out)
{
    int y, mo, d, h, mi;
    owf_ptime_split(t, &y, &mo, &d, &h, &mi);
    sprintf(out, "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi);
}

#define CHECK(cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_T(got, want) do { owf_ptime g_ = (got), w_ = (want); checks++; if (g_ != w_) { char a_[32], b_[32]; \
    show(g_, a_); show(w_, b_); fails++; printf("FAIL %s:%d: %s is %s, want %s\n", __FILE__, __LINE__, #got, a_, b_); } } while (0)
#define CHECK_N(got, want) do { long g_ = (long)(got), w_ = (long)(want); checks++; if (g_ != w_) { fails++; \
    printf("FAIL %s:%d: %s is %ld, want %ld\n", __FILE__, __LINE__, #got, g_, w_); } } while (0)

static owf_project *plan(void)
{
    owf_project *p = owf_project_new(AT(16, 8, 0));
    CHECK(p != NULL);
    return p;
}

#define TASK(p, i) (&(p)->tasks[i])

static void test_dates(void)
{
    int y, mo, d, h, mi;
    CHECK_N(owf_ptime_make(2000, 1, 1, 0, 0), 0);
    CHECK_N(owf_ptime_weekday(0), 6);                               /* 1 Jan 2000: Saturday */
    CHECK_N(owf_ptime_weekday(AT(13, 12, 0)), 5);                   /* 13 Nov 2026: Friday */
    CHECK_N(owf_ptime_weekday(AT(16, 0, 0)), 1);                    /* Monday */
    CHECK_N(owf_ptime_weekday(owf_ptime_make(1999, 12, 31, 23, 59)), 5);
    CHECK_N(owf_ptime_make(2024, 3, 1, 0, 0) - owf_ptime_make(2024, 2, 28, 0, 0), 2 * 1440);  /* leap day */
    CHECK_N(owf_ptime_make(2100, 3, 1, 0, 0) - owf_ptime_make(2100, 2, 28, 0, 0), 1440);      /* not one */
    owf_ptime_split(owf_ptime_make(2024, 2, 29, 13, 45), &y, &mo, &d, &h, &mi);
    CHECK(y == 2024 && mo == 2 && d == 29 && h == 13 && mi == 45);
    owf_ptime_split(owf_ptime_make(1999, 12, 31, 23, 59), &y, &mo, &d, &h, &mi);
    CHECK(y == 1999 && mo == 12 && d == 31 && h == 23 && mi == 59);
}

static void test_calendar(void)
{
    owf_pcal c;
    static const unsigned short morning[2] = { 8 * 60, 12 * 60 };
    memset(&c, 0, sizeof c);
    owf_pcal_standard(&c);
    CHECK_T(owf_pcal_add(&c, AT(16, 8, 0), D), AT(16, 17, 0));      /* one day: Monday 17:00 */
    CHECK_T(owf_pcal_add(&c, AT(16, 8, 0), D + 60), AT(17, 9, 0));  /* and an hour: Tuesday 09:00 */
    CHECK_T(owf_pcal_add(&c, AT(16, 12, 0), 60), AT(16, 14, 0));    /* lunch is not worked */
    CHECK_T(owf_pcal_add(&c, AT(20, 16, 0), 120), AT(23, 9, 0));    /* Friday 16:00 over the weekend */
    CHECK_T(owf_pcal_add(&c, AT(21, 10, 0), 60), AT(23, 9, 0));     /* from a Saturday */
    CHECK_T(owf_pcal_add(&c, AT(16, 17, 0), -D), AT(16, 8, 0));
    CHECK_T(owf_pcal_add(&c, AT(17, 9, 0), -120), AT(16, 16, 0));
    CHECK_T(owf_pcal_add(&c, AT(23, 9, 0), -120), AT(20, 16, 0));
    CHECK_T(owf_pcal_add(&c, AT(16, 17, 0), 0), AT(16, 17, 0));
    CHECK_N(owf_pcal_work(&c, AT(16, 8, 0), AT(17, 17, 0)), 2 * D);
    CHECK_N(owf_pcal_work(&c, AT(20, 16, 0), AT(23, 9, 0)), 120);
    CHECK_N(owf_pcal_work(&c, AT(23, 9, 0), AT(20, 16, 0)), -120);
    CHECK_N(owf_pcal_work(&c, AT(16, 11, 30), AT(16, 13, 30)), 60);
    CHECK_T(owf_pcal_next_start(&c, AT(16, 17, 0)), AT(17, 8, 0));
    CHECK_T(owf_pcal_next_start(&c, AT(21, 10, 0)), AT(23, 8, 0));
    CHECK_T(owf_pcal_next_start(&c, AT(16, 12, 30)), AT(16, 13, 0));
    CHECK_T(owf_pcal_next_start(&c, AT(16, 10, 0)), AT(16, 10, 0));
    CHECK_T(owf_pcal_prev_end(&c, AT(17, 8, 0)), AT(16, 17, 0));
    CHECK_T(owf_pcal_prev_end(&c, AT(16, 13, 0)), AT(16, 12, 0));
    CHECK_T(owf_pcal_prev_end(&c, AT(16, 17, 0)), AT(16, 17, 0));
    /* a holiday on Tuesday, a morning on Wednesday */
    CHECK(owf_pcal_set_exception(&c, AT(17, 0, 0) / 1440, 0, NULL) == OWF_OK);
    CHECK(owf_pcal_set_exception(&c, AT(18, 0, 0) / 1440, 1, morning) == OWF_OK);
    CHECK_T(owf_pcal_add(&c, AT(16, 8, 0), 2 * D), AT(19, 12, 0));  /* Mon 8h, Wed 4h, Thu 4h */
    CHECK_N(owf_pcal_work(&c, AT(16, 8, 0), AT(19, 8, 0)), D + 240);
    CHECK(c.nexceptions == 2);
    CHECK(owf_pcal_set_exception(&c, AT(17, 0, 0) / 1440, 1, morning) == OWF_OK && c.nexceptions == 2);
    {
        static const unsigned short bad[2] = { 12 * 60, 8 * 60 };
        CHECK(owf_pcal_set_exception(&c, 0, 1, bad) == OWF_ERR_FORMAT);
        CHECK(owf_pcal_set_week(&c, 7, 0, NULL) == OWF_ERR_FORMAT);
    }
    owf_pcal_free(&c);
    /* no working time at all: times stay where they are */
    memset(&c, 0, sizeof c);
    CHECK_T(owf_pcal_add(&c, AT(16, 8, 0), D), AT(16, 8, 0));
    owf_pcal_free(&c);
}

/* A 2d, then B 3d after it. */
static void test_finish_to_start(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", 2 * D), b = owf_project_add_task(p, "B", 3 * D);
    CHECK(owf_project_link(p, a, b, OWF_LINK_FS, 0, 0) == OWF_OK);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, a)->early_start, AT(16, 8, 0));
    CHECK_T(TASK(p, a)->early_finish, AT(17, 17, 0));
    CHECK_T(TASK(p, b)->early_start, AT(18, 8, 0));                /* not Tuesday 17:00 */
    CHECK_T(TASK(p, b)->early_finish, AT(20, 17, 0));
    CHECK_T(p->finish, AT(20, 17, 0));
    CHECK(TASK(p, a)->critical && TASK(p, b)->critical);
    CHECK_N(TASK(p, a)->total_slack, 0);
    CHECK_N(TASK(p, b)->free_slack, 0);
    /* the same link again replaces it; unlinking frees B */
    CHECK(owf_project_link(p, a, b, OWF_LINK_SS, 0, 0) == OWF_OK && TASK(p, b)->nlinks == 1);
    owf_project_unlink(p, a, b);
    CHECK(TASK(p, b)->nlinks == 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, b)->early_start, AT(16, 8, 0));
    CHECK_N(TASK(p, a)->total_slack, D);                            /* A 2d, B 3d: A can slip a day */
    CHECK(owf_project_link(p, a, a, OWF_LINK_FS, 0, 0) == OWF_ERR_FORMAT);
    CHECK(owf_project_link(p, a, 9, OWF_LINK_FS, 0, 0) == OWF_ERR_FORMAT);
    owf_project_free(p);
}

/* A 2d; B 1d a day after A; C 1d a day before A ends (a lead); E 1d 50% of A after it (one day). */
static void test_lags(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", 2 * D), b = owf_project_add_task(p, "B", D);
    int c = owf_project_add_task(p, "C", D), e = owf_project_add_task(p, "E", D);
    owf_project_link(p, a, b, OWF_LINK_FS, D, 0);
    owf_project_link(p, a, c, OWF_LINK_FS, -D, 0);
    owf_project_link(p, a, e, OWF_LINK_FS, 50, 1);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, b)->early_start, AT(19, 8, 0));                /* Wednesday is the lag */
    CHECK_T(TASK(p, b)->early_finish, AT(19, 17, 0));
    CHECK_T(TASK(p, c)->early_start, AT(17, 8, 0));
    CHECK_T(TASK(p, e)->early_start, AT(19, 8, 0));
    CHECK_T(p->finish, AT(19, 17, 0));
    CHECK(TASK(p, a)->critical && TASK(p, b)->critical && TASK(p, e)->critical && !TASK(p, c)->critical);
    CHECK_N(TASK(p, c)->total_slack, 2 * D);                        /* C may end on Thursday */
    owf_project_free(p);
}

/* A 4d (Mon-Thu); B 2d starts a day after A starts; C 1d finishes with A; F 1d finishes two days after
 * A starts (SF). */
static void test_link_types(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", 4 * D), b = owf_project_add_task(p, "B", 2 * D);
    int c = owf_project_add_task(p, "C", D), f = owf_project_add_task(p, "F", D);
    owf_project_link(p, a, b, OWF_LINK_SS, D, 0);
    owf_project_link(p, a, c, OWF_LINK_FF, 0, 0);
    owf_project_link(p, a, f, OWF_LINK_SF, 2 * D, 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, b)->early_start, AT(17, 8, 0));
    CHECK_T(TASK(p, b)->early_finish, AT(18, 17, 0));
    CHECK_T(TASK(p, c)->early_start, AT(19, 8, 0));
    CHECK_T(TASK(p, c)->early_finish, AT(19, 17, 0));
    CHECK_T(TASK(p, f)->early_finish, AT(17, 17, 0));              /* two days from Monday 08:00 */
    CHECK_T(TASK(p, f)->early_start, AT(17, 8, 0));
    CHECK_T(p->finish, AT(19, 17, 0));
    CHECK_N(TASK(p, b)->total_slack, D);                            /* B may finish on Thursday */
    CHECK(TASK(p, c)->critical && TASK(p, a)->critical);
    owf_project_free(p);
}

static void test_constraints(void)
{
    owf_project *p = plan();
    int snet = owf_project_add_task(p, "SNET", D), mso = owf_project_add_task(p, "MSO", D);
    int fnet = owf_project_add_task(p, "FNET", D), mfo = owf_project_add_task(p, "MFO", D);
    TASK(p, snet)->constraint = OWF_CON_SNET; TASK(p, snet)->constraint_date = AT(18, 8, 0);
    TASK(p, mso)->constraint = OWF_CON_MSO; TASK(p, mso)->constraint_date = AT(19, 8, 0);
    TASK(p, fnet)->constraint = OWF_CON_FNET; TASK(p, fnet)->constraint_date = AT(18, 17, 0);
    TASK(p, mfo)->constraint = OWF_CON_MFO; TASK(p, mfo)->constraint_date = AT(20, 17, 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, snet)->early_start, AT(18, 8, 0));
    CHECK_T(TASK(p, mso)->early_start, AT(19, 8, 0));
    CHECK_T(TASK(p, fnet)->early_start, AT(18, 8, 0));
    CHECK_T(TASK(p, mfo)->early_start, AT(20, 8, 0));
    CHECK_T(p->finish, AT(20, 17, 0));
    CHECK_N(TASK(p, mfo)->total_slack, 0);
    CHECK_N(TASK(p, mso)->total_slack, 0);                          /* fixed dates have no slack */
    CHECK_N(TASK(p, snet)->total_slack, 2 * D);
    owf_project_free(p);

    /* SNLT wins over its link: B must start by Tuesday though A (3d) ends on Wednesday, which leaves A
     * two days of negative slack */
    p = plan();
    {
        int a = owf_project_add_task(p, "A", 3 * D), b = owf_project_add_task(p, "B", D);
        int f = owf_project_add_task(p, "F", 3 * D);
        owf_project_link(p, a, b, OWF_LINK_FS, 0, 0);
        TASK(p, b)->constraint = OWF_CON_SNLT; TASK(p, b)->constraint_date = AT(17, 8, 0);
        TASK(p, f)->constraint = OWF_CON_FNLT; TASK(p, f)->constraint_date = AT(17, 17, 0);
        CHECK(owf_project_schedule(p) == OWF_OK);
        CHECK_T(TASK(p, b)->early_start, AT(17, 8, 0));
        CHECK_N(TASK(p, a)->total_slack, -2 * D);
        CHECK(TASK(p, a)->critical);
        CHECK_T(TASK(p, f)->early_start, AT(13, 8, 0));             /* finishing by Tuesday: from Friday */
        CHECK_T(TASK(p, f)->early_finish, AT(17, 17, 0));
    }
    owf_project_free(p);
}

/* A 3d and B 1d lead to C 1d; B as late as possible goes to Wednesday. */
static void test_alap(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", 3 * D), b = owf_project_add_task(p, "B", D), c = owf_project_add_task(p, "C", D);
    owf_project_link(p, a, c, OWF_LINK_FS, 0, 0);
    owf_project_link(p, b, c, OWF_LINK_FS, 0, 0);
    TASK(p, b)->constraint = OWF_CON_ALAP;
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, b)->early_start, AT(18, 8, 0));
    CHECK_T(TASK(p, b)->early_finish, AT(18, 17, 0));
    CHECK_T(TASK(p, c)->early_start, AT(19, 8, 0));
    CHECK_N(TASK(p, b)->free_slack, 0);
    CHECK_N(TASK(p, b)->total_slack, 0);
    owf_project_free(p);
}

/* A 1d; then C 1d and E 1d in a row, beside B 4d; D 1d after B and E. */
static void test_critical_path(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", D), b = owf_project_add_task(p, "B", 4 * D);
    int c = owf_project_add_task(p, "C", D), e = owf_project_add_task(p, "E", D), d = owf_project_add_task(p, "D", D);
    owf_project_link(p, a, b, OWF_LINK_FS, 0, 0);
    owf_project_link(p, a, c, OWF_LINK_FS, 0, 0);
    owf_project_link(p, c, e, OWF_LINK_FS, 0, 0);
    owf_project_link(p, b, d, OWF_LINK_FS, 0, 0);
    owf_project_link(p, e, d, OWF_LINK_FS, 0, 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_T(TASK(p, b)->early_finish, AT(20, 17, 0));
    CHECK_T(TASK(p, d)->early_start, AT(23, 8, 0));
    CHECK_T(p->finish, AT(23, 17, 0));
    CHECK(TASK(p, a)->critical && TASK(p, b)->critical && TASK(p, d)->critical);
    CHECK(!TASK(p, c)->critical && !TASK(p, e)->critical);
    CHECK_T(TASK(p, c)->late_start, AT(19, 8, 0));
    CHECK_T(TASK(p, e)->late_finish, AT(20, 17, 0));
    CHECK_N(TASK(p, c)->total_slack, 2 * D);
    CHECK_N(TASK(p, c)->free_slack, 0);                             /* E follows at once */
    CHECK_N(TASK(p, e)->total_slack, 2 * D);
    CHECK_N(TASK(p, e)->free_slack, 2 * D);
    CHECK_N(TASK(p, d)->free_slack, 0);
    owf_project_free(p);
}

/* Y 1d; summary S (S1 2d, then S2 1d) after Y; X 1d after S. */
static void test_summary(void)
{
    owf_project *p = plan();
    int y = owf_project_add_task(p, "Y", D), s = owf_project_add_task(p, "S", 0);
    int s1 = owf_project_add_task(p, "S1", 2 * D), s2 = owf_project_add_task(p, "S2", D);
    int x = owf_project_add_task(p, "X", D);
    TASK(p, s1)->level = TASK(p, s2)->level = 2;
    owf_project_link(p, y, s, OWF_LINK_FS, 0, 0);
    owf_project_link(p, s1, s2, OWF_LINK_FS, 0, 0);
    owf_project_link(p, s, x, OWF_LINK_FS, 0, 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK(TASK(p, s)->summary && !TASK(p, s1)->summary && TASK(p, s1)->parent == s && TASK(p, x)->parent == -1);
    CHECK_T(TASK(p, s1)->early_start, AT(17, 8, 0));               /* held back by S's predecessor */
    CHECK_T(TASK(p, s2)->early_start, AT(19, 8, 0));
    CHECK_T(TASK(p, s)->early_start, AT(17, 8, 0));
    CHECK_T(TASK(p, s)->early_finish, AT(19, 17, 0));
    CHECK_T(TASK(p, x)->early_start, AT(20, 8, 0));                /* waits for S to finish */
    CHECK(TASK(p, s)->critical && TASK(p, x)->critical);
    CHECK_N(TASK(p, s)->duration, 3 * D);
    /* a summary linked to its own subtask is a loop */
    owf_project_link(p, s, s1, OWF_LINK_FS, 0, 0);
    CHECK(owf_project_schedule(p) == OWF_ERR_FORMAT);
    owf_project_free(p);
}

static void test_milestone_and_elapsed(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", 2 * D), m = owf_project_add_task(p, "Done", 0);
    int e = owf_project_add_task(p, "Cure", 2 * 1440);
    owf_project_link(p, a, m, OWF_LINK_FS, 0, 0);
    TASK(p, e)->elapsed = 1;
    TASK(p, e)->constraint = OWF_CON_SNET;
    TASK(p, e)->constraint_date = AT(20, 8, 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK(TASK(p, m)->milestone);
    CHECK_T(TASK(p, m)->early_start, AT(17, 17, 0));               /* where A ends */
    CHECK_T(TASK(p, m)->early_finish, AT(17, 17, 0));
    CHECK_T(TASK(p, e)->early_finish, AT(22, 8, 0));               /* two whole days, over the weekend */
    CHECK_T(p->finish, AT(22, 8, 0));
    owf_project_free(p);
}

static void test_loop_and_holiday(void)
{
    owf_project *p = plan();
    int a = owf_project_add_task(p, "A", D), b = owf_project_add_task(p, "B", D), c = owf_project_add_task(p, "C", 2 * D);
    owf_project_link(p, a, b, OWF_LINK_FS, 0, 0);
    owf_project_link(p, b, a, OWF_LINK_SS, 0, 0);
    CHECK(owf_project_schedule(p) == OWF_ERR_FORMAT);
    CHECK(p->loop_task == a || p->loop_task == b);
    owf_project_unlink(p, b, a);
    /* a holiday on Tuesday in the project's calendar: C (2d) ends on Wednesday */
    CHECK(owf_pcal_set_exception(&p->calendars[p->calendar], AT(17, 0, 0) / 1440, 0, NULL) == OWF_OK);
    CHECK(owf_project_schedule(p) == OWF_OK && p->loop_task == -1);
    CHECK_T(TASK(p, b)->early_start, AT(18, 8, 0));
    CHECK_T(TASK(p, c)->early_finish, AT(18, 17, 0));
    /* C on its own calendar that works Saturdays too */
    {
        static const unsigned short day[4] = { 8 * 60, 12 * 60, 13 * 60, 17 * 60 };
        int cal = owf_project_add_calendar(p, "Six days");
        CHECK(cal == 1 && owf_pcal_set_week(&p->calendars[cal], 6, 2, day) == OWF_OK);
        TASK(p, c)->calendar = cal;
        TASK(p, c)->constraint = OWF_CON_SNET;
        TASK(p, c)->constraint_date = AT(20, 8, 0);
        CHECK(owf_project_schedule(p) == OWF_OK);
        CHECK_T(TASK(p, c)->early_finish, AT(21, 17, 0));          /* Friday and Saturday */
    }
    owf_project_free(p);
}

/* Resources: A (2d) and C (2d) both from Monday 16th, B (3d) after A. Dale on A and C full time is two people's
 * work at once on Monday and Tuesday: over-allocated. Bob half time on B. Steel, a material, 3 units on B. */
static void test_resources(void)
{
    owf_project *p = plan(), *q;
    int a = owf_project_add_task(p, "A", 2 * D), b = owf_project_add_task(p, "B", 3 * D), c = owf_project_add_task(p, "C", 2 * D);
    int dale = owf_project_add_resource(p, "Dale"), bob = owf_project_add_resource(p, "Bob");
    int steel = owf_project_add_resource(p, "Steel");
    owf_project_link(p, a, b, OWF_LINK_FS, 0, 0);
    CHECK(dale == 0 && bob == 1 && steel == 2);
    CHECK_N(p->resources[bob].uid, 2);
    p->resources[dale].rate = 2000;                                 /* 20.00 an hour */
    p->resources[steel].type = OWF_RES_MATERIAL;
    p->resources[steel].rate = 500;                                 /* 5.00 a unit */
    CHECK_N(owf_project_assign(p, a, dale, 100), 0);
    CHECK_N(owf_project_assign(p, c, dale, 100), 1);
    CHECK_N(owf_project_assign(p, b, bob, 50), 2);
    CHECK_N(owf_project_assign(p, b, steel, 300), 3);
    CHECK_N(owf_project_assign(p, b, bob, 50), 2);                 /* the same pair: the same assignment */
    CHECK_N(owf_project_assign(p, 9, bob, 50), -1);
    CHECK_N(owf_project_assign(p, a, 9, 50), -1);
    CHECK_N(owf_project_assign(p, a, bob, -1), -1);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_N(p->assigns[0].work, 2 * D);
    CHECK_N(p->assigns[2].work, 3 * D / 2);                         /* half of three days: 12 hours */
    CHECK_N(p->assigns[3].work, 0);
    CHECK_N(p->resources[dale].work, 4 * D);                        /* 32 hours */
    CHECK_N(p->resources[dale].cost, 64000);                        /* 32 hours at 20.00: 640.00 */
    CHECK_N(p->resources[dale].peak, 200);
    CHECK(p->resources[dale].overallocated);
    CHECK_N(p->resources[bob].peak, 50);
    CHECK(!p->resources[bob].overallocated);
    CHECK_N(p->resources[steel].cost, 1500);                        /* 3 at 5.00 */
    CHECK(!p->resources[steel].overallocated);
    /* Work over time: Monday is A's day and C's; Bob's Wednesday morning is half of four hours. */
    CHECK_N(owf_presource_work(p, dale, AT(16, 0, 0), AT(17, 0, 0)), 2 * D);
    CHECK_N(owf_presource_work(p, bob, AT(18, 8, 0), AT(18, 12, 0)), 120);
    CHECK_N(owf_presource_work(p, bob, AT(16, 0, 0), AT(30, 0, 0)), 3 * D / 2);
    CHECK_N(owf_passign_work(p, 1, AT(17, 13, 0), AT(30, 0, 0)), 240);
    CHECK_N(owf_presource_work(p, steel, AT(16, 0, 0), AT(30, 0, 0)), 0);
    /* C a day later: Dale no longer works two tasks at once. */
    TASK(p, c)->constraint = OWF_CON_SNET;
    TASK(p, c)->constraint_date = AT(18, 8, 0);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_N(p->resources[dale].peak, 100);
    CHECK(!p->resources[dale].overallocated);
    /* A copy holds them too. */
    q = owf_project_copy(p);
    CHECK(q && q->nresources == 3 && q->nassigns == 4 && strcmp(q->resources[2].name, "Steel") == 0);
    CHECK(q && q->resources[0].name != p->resources[0].name);
    owf_project_free(q);
    /* Removing Bob takes his assignment; Steel moves down. */
    owf_project_remove_resource(p, bob);
    CHECK_N(p->nresources, 2);
    CHECK_N(p->nassigns, 3);
    CHECK_N(p->assigns[2].resource, 1);
    owf_project_unassign(p, c, dale);
    CHECK_N(p->nassigns, 2);
    /* Tasks moving: A deleted, B and C down a row. */
    {
        int map[3] = { -1, 0, 1 };
        owf_project_remap_assignments(p, map, 3);
        CHECK_N(p->nassigns, 1);
        CHECK(p->assigns[0].task == 0 && p->assigns[0].resource == 1);
    }
    owf_project_free(p);
}

/* A baseline, then A takes a day longer: A finishes a day late, B starts and finishes a day late, and the
 * summary's work and cost grow by A's extra day of Dale at 20.00 an hour. */
static void test_baseline(void)
{
    owf_project *p = plan();
    int ph = owf_project_add_task(p, "Phase", 0), a = owf_project_add_task(p, "A", 2 * D);
    int b = owf_project_add_task(p, "B", D), dale = owf_project_add_resource(p, "Dale");
    TASK(p, a)->level = TASK(p, b)->level = 2;
    owf_project_link(p, a, b, OWF_LINK_FS, 0, 0);
    p->resources[dale].rate = 2000;
    owf_project_assign(p, a, dale, 100);
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_N(TASK(p, a)->work, 2 * D);
    CHECK_N(TASK(p, a)->cost, 32000);
    CHECK_N(TASK(p, ph)->work, 2 * D);                              /* the summary adds up its subtasks */
    CHECK_N(TASK(p, b)->work, 0);
    CHECK_N(owf_ptask_variance(p, a, OWF_VAR_FINISH), 0);           /* no baseline yet */
    owf_project_set_baseline(p, 0, -1);
    CHECK(TASK(p, a)->baselined && TASK(p, ph)->baselined);
    CHECK_N(TASK(p, a)->baseline_start, AT(16, 8, 0));
    CHECK_N(TASK(p, a)->baseline_finish, AT(17, 17, 0));
    CHECK_N(TASK(p, ph)->baseline_duration, 3 * D);                 /* a summary's: its span's working time */
    CHECK_N(TASK(p, a)->baseline_cost, 32000);
    TASK(p, a)->duration = 3 * D;
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_N(owf_ptask_variance(p, a, OWF_VAR_START), 0);
    CHECK_N(owf_ptask_variance(p, a, OWF_VAR_FINISH), D);
    CHECK_N(owf_ptask_variance(p, a, OWF_VAR_DURATION), D);
    CHECK_N(owf_ptask_variance(p, a, OWF_VAR_WORK), D);
    CHECK_N(owf_ptask_variance(p, a, OWF_VAR_COST), 16000);
    CHECK_N(owf_ptask_variance(p, b, OWF_VAR_START), D);            /* Wed 08:00 to Thu 08:00 */
    CHECK_N(owf_ptask_variance(p, b, OWF_VAR_FINISH), D);
    CHECK_N(owf_ptask_variance(p, ph, OWF_VAR_DURATION), D);
    CHECK_N(owf_ptask_variance(p, ph, OWF_VAR_COST), 16000);
    CHECK_N(owf_ptask_variance(p, a, 99), 0);
    /* Earlier than the baseline is below 0: A back to 1 day. */
    TASK(p, a)->duration = D;
    CHECK(owf_project_schedule(p) == OWF_OK);
    CHECK_N(owf_ptask_variance(p, b, OWF_VAR_START), -D);
    /* Clearing one row; a range out of the plan does nothing. */
    owf_project_clear_baseline(p, b, 1);
    CHECK(!TASK(p, b)->baselined && TASK(p, a)->baselined);
    CHECK_N(owf_ptask_variance(p, b, OWF_VAR_START), 0);
    owf_project_set_baseline(p, 2, 5);
    CHECK(!TASK(p, b)->baselined);
    owf_project_free(p);
}

int main(void)
{
    test_dates();
    test_calendar();
    test_finish_to_start();
    test_lags();
    test_link_types();
    test_constraints();
    test_alap();
    test_critical_path();
    test_summary();
    test_milestone_and_elapsed();
    test_loop_and_holiday();
    test_resources();
    test_baseline();
    if (fails) {
        printf("project tests: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("project tests passed (%d checks)\n", checks);
    return 0;
}
