/*
 * GFOpenProject's reports (libowf/project/owf_preport.c) on the PC: each report built as an owf_doc and
 * read back through libowf's own text writer (tables as tab-separated rows), then saved through the
 * PDF and ODT writers. The plan starts on Monday 16 November 2026 at 08:00 on the Standard calendar;
 * every expected figure was worked out by hand. Run under ASan and UBSan by make check.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "owf_project.h"

static int fails, checks;

#define D 480L
#define AT(d, h, m) owf_ptime_make(2026, 11, d, h, m)

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* Build (summary): Design 2d (Dale), Code 5d after it (Dale, Ann at 50%), Done (a milestone after
 * Code); Docs 1d (Dale, 50% done) on its own, so Dale is on two tasks on Monday. Dale costs 10.00 an
 * hour, Ann 20.00. */
static owf_project *sample(void)
{
    owf_project *p = owf_project_new(AT(16, 8, 0));
    int build, design, code, done, docs, dale, ann;
    if (!p) return NULL;
    p->title = owf_malloc(8);
    if (p->title) strcpy(p->title, "Garden");
    build = owf_project_add_task(p, "Build", D);
    design = owf_project_add_task(p, "Design", 2 * D);
    code = owf_project_add_task(p, "Code", 5 * D);
    done = owf_project_add_task(p, "Done", 0);
    docs = owf_project_add_task(p, "Docs", D);
    (void)build;
    p->tasks[design].level = p->tasks[code].level = p->tasks[done].level = 2;
    p->tasks[done].milestone = 1;
    p->tasks[docs].percent = 50;
    owf_project_link(p, design, code, OWF_LINK_FS, 0, 0);
    owf_project_link(p, code, done, OWF_LINK_FS, 0, 0);
    dale = owf_project_add_resource(p, "Dale");
    ann = owf_project_add_resource(p, "Ann");
    p->resources[dale].rate = 1000;
    p->resources[ann].rate = 2000;
    owf_project_assign(p, design, dale, 100);
    owf_project_assign(p, code, dale, 100);
    owf_project_assign(p, code, ann, 50);
    owf_project_assign(p, docs, dale, 100);
    CHECK(owf_project_schedule(p) == OWF_OK, "schedule");
    return p;
}

/* A report as plain text, through libowf's text writer. */
static char *report_text(const owf_project *p, int kind)
{
    const owf_format *f = owf_format_named("text");
    owf_doc *doc = NULL;
    unsigned char *data = NULL;
    size_t len = 0;
    char *text = NULL;
    if (owf_project_report(p, kind, &doc) != OWF_OK || !doc) return NULL;
    if (f && f->export(doc, &data, &len, NULL) == OWF_OK && data) {
        text = malloc(len + 1);
        if (text) {
            memcpy(text, data, len);
            text[len] = 0;
        }
    }
    free(data);
    owf_doc_free(doc);
    return text;
}

static void expect(const owf_project *p, int kind, const char *want)
{
    char *text = report_text(p, kind);
    CHECK(text && !strcmp(text, want), "%s:\n%s\nwant:\n%s", owf_preport_name(kind), text ? text : "(none)", want);
    free(text);
}

static void test_names(void)
{
    int i;
    for (i = 0; i < OWF_PREPORT_N; i++) CHECK(owf_preport_parse(owf_preport_name(i)) == i, "name %d", i);
    CHECK(!strcmp(owf_preport_name(OWF_PREPORT_WHO_DOES_WHAT), "Who Does What"), "who");
    CHECK(owf_preport_parse("cash flow") == OWF_PREPORT_CASH_FLOW, "cash flow");
    CHECK(owf_preport_parse("CASHFLOW") == OWF_PREPORT_CASH_FLOW, "CASHFLOW");
    CHECK(owf_preport_parse("summary") == OWF_PREPORT_SUMMARY, "summary");
    CHECK(owf_preport_parse("Overallocated") == OWF_PREPORT_OVERALLOCATED, "over");
    CHECK(owf_preport_parse("critical") == OWF_PREPORT_CRITICAL, "critical");
    CHECK(owf_preport_parse("budget") == -1 && owf_preport_parse(NULL) == -1, "not a report");
    CHECK(!owf_preport_name(-1) && !owf_preport_name(OWF_PREPORT_N), "out of range");
}

static void test_summary(const owf_project *p)
{
    expect(p, OWF_PREPORT_SUMMARY,
           "Project Summary\n"
           "Garden\n"
           "Dates\n"
           "\t\n"
           "Start\tMon 16/11/26\n"
           "Finish\tTue 24/11/26\n"
           "Duration, work and cost\n"
           "\t\n"
           "Duration\t7d\n"
           "Work\t84h\n"
           "Cost\t1040.00\n"
           "Task status\n"
           "\t\n"
           "Tasks\t4\n"
           "Not yet started\t3\n"
           "In progress\t1\n"
           "Completed\t0\n"
           "Milestones\t1\n"
           "Critical tasks\t3\n"
           "Resource status\n"
           "\t\n"
           "Resources\t2\n"
           "Overallocated resources\t1\n");
}

static void test_tasks(const owf_project *p)
{
    expect(p, OWF_PREPORT_CRITICAL,
           "Critical Tasks\n"
           "Garden\n"
           "ID\tTask Name\tDuration\tStart\tFinish\tResource Names\n"
           "2\tDesign\t2d\tMon 16/11/26\tTue 17/11/26\tDale\n"
           "3\tCode\t5d\tWed 18/11/26\tTue 24/11/26\tDale,Ann[50%]\n"
           "4\tDone\t0d\tTue 24/11/26\tTue 24/11/26\t\n");
    expect(p, OWF_PREPORT_MILESTONES,
           "Milestones\n"
           "Garden\n"
           "ID\tTask Name\tFinish\tCritical\t% Complete\n"
           "4\tDone\tTue 24/11/26\tYes\t0%\n");
}

static void test_resources(const owf_project *p)
{
    static const char dale[] =
        "Dale\n"
        "ID\tTask Name\tUnits\tWork\tStart\tFinish\n"
        "2\tDesign\t100%\t16h\tMon 16/11/26\tTue 17/11/26\n"
        "3\tCode\t100%\t40h\tWed 18/11/26\tTue 24/11/26\n"
        "5\tDocs\t100%\t8h\tMon 16/11/26\tMon 16/11/26\n";
    char want[1024];
    snprintf(want, sizeof want, "Who Does What\nGarden\n%sAnn\n"
             "ID\tTask Name\tUnits\tWork\tStart\tFinish\n"
             "3\tCode\t50%%\t20h\tWed 18/11/26\tTue 24/11/26\n", dale);
    expect(p, OWF_PREPORT_WHO_DOES_WHAT, want);
    snprintf(want, sizeof want, "Overallocated Resources\nGarden\n"
             "Resource Name\tMax Units\tPeak\tWork\n"
             "Dale\t100%%\t200%%\t64h\n%s", dale);
    expect(p, OWF_PREPORT_OVERALLOCATED, want);
}

/* Design (160.00) and Docs (80.00) in the first week, Code's 800.00 spread over its five days: three
 * in the first week (480.00), two in the second (320.00). */
static void test_cash_flow(const owf_project *p)
{
    expect(p, OWF_PREPORT_CASH_FLOW,
           "Cash Flow\n"
           "Garden\n"
           "Week Starting\tCost\tCumulative Cost\n"
           "Mon 16/11/26\t720.00\t720.00\n"
           "Mon 23/11/26\t320.00\t1040.00\n"
           "Total\t1040.00\t\n");
}

/* A long dear task: the weeks add up to its cost exactly, and no week's share overflows. */
static void test_cash_flow_long(void)
{
    owf_project *p = owf_project_new(AT(18, 8, 0));         /* a Wednesday: the first week starts Monday */
    char *text, *s;
    long sum = 0, whole, part, rows = 0;
    int t, r;
    if (!p) return;
    t = owf_project_add_task(p, "Dig", 400 * D);
    r = owf_project_add_resource(p, "Digger");
    p->resources[r].rate = 4000000;                          /* 40,000.00 an hour: the cost is cut at 20,000,000.00 */
    owf_project_assign(p, t, r, 300);
    CHECK(owf_project_schedule(p) == OWF_OK, "schedule");
    CHECK(p->tasks[t].cost > 1000000000L, "cost %ld", p->tasks[t].cost);
    text = report_text(p, OWF_PREPORT_CASH_FLOW);
    CHECK(text && !strncmp(text, "Cash Flow\nWeek Starting\tCost\tCumulative Cost\nMon 16/11/26\t", 56),
          "head:\n%.120s", text ? text : "(none)");
    for (s = text; s && (s = strchr(s, '\n')) != NULL; s++) {
        if (s[1] < 'A' || s[1] == 'T' || s[1] == 'W' || s[1] == 'C') continue;
        if (sscanf(strchr(s, '\t') + 1, "%ld.%ld", &whole, &part) == 2) sum += whole * 100 + part, rows++;
    }
    CHECK(sum == p->tasks[t].cost, "weeks add up to %ld, want %ld", sum, p->tasks[t].cost);
    CHECK(rows == 81, "%ld weeks", rows);                    /* 400 working days: 80 weeks and a Wednesday on */
    CHECK(text && strstr(text, "Mon 16/11/26\t") && strstr(text, "Mon 16/11/26\t150000.00\t150000.00\n"),
          "the first week (Wed-Fri) is three days: %.200s", text ? text : "");
    free(text);
    owf_project_free(p);
}

static void test_empty_and_errors(void)
{
    owf_project *p = owf_project_new(AT(16, 8, 0));
    owf_doc *doc = (owf_doc *)1;
    if (!p) return;
    owf_project_add_task(p, "Only", D);
    CHECK(owf_project_schedule(p) == OWF_OK, "schedule");
    expect(p, OWF_PREPORT_MILESTONES, "Milestones\nThe plan has no milestones.\n");
    expect(p, OWF_PREPORT_WHO_DOES_WHAT, "Who Does What\nThe plan has no resources.\n");
    expect(p, OWF_PREPORT_OVERALLOCATED, "Overallocated Resources\nNo resources are overallocated.\n");
    p->tasks[0].critical = 0;
    expect(p, OWF_PREPORT_CRITICAL, "Critical Tasks\nThe plan has no critical tasks.\n");
    CHECK(owf_project_report(p, OWF_PREPORT_N, &doc) == OWF_ERR_FORMAT && !doc, "kind out of range");
    CHECK(owf_project_report(p, -1, &doc) == OWF_ERR_FORMAT, "kind -1");
    CHECK(owf_project_report(NULL, 0, &doc) == OWF_ERR_FORMAT, "no plan");
    owf_project_free(p);
}

/* Material and cost resources: units as the Assign window types them, no work. */
static void test_units(void)
{
    owf_project *p = owf_project_new(AT(16, 8, 0));
    int t, wood, fee;
    if (!p) return;
    t = owf_project_add_task(p, "Fence", D);
    wood = owf_project_add_resource(p, "Wood");
    fee = owf_project_add_resource(p, "Fee");
    p->resources[wood].type = OWF_RES_MATERIAL;
    p->resources[fee].type = OWF_RES_COST;
    owf_project_assign(p, t, wood, 250);
    owf_project_assign(p, t, fee, 1250);
    CHECK(owf_project_schedule(p) == OWF_OK, "schedule");
    expect(p, OWF_PREPORT_WHO_DOES_WHAT,
           "Who Does What\n"
           "Wood\n"
           "ID\tTask Name\tUnits\tWork\tStart\tFinish\n"
           "1\tFence\t2.5\t\tMon 16/11/26\tMon 16/11/26\n"
           "Fee\n"
           "ID\tTask Name\tUnits\tWork\tStart\tFinish\n"
           "1\tFence\t12.50\t\tMon 16/11/26\tMon 16/11/26\n");
    owf_project_free(p);
}

/* Through libowf's own writers: a PDF with the report's text and its page footer, and an ODT package. */
static void test_writers(const owf_project *p)
{
    const owf_format *pdf = owf_format_named("pdf"), *odt = owf_format_named("odt");
    owf_doc *doc = NULL;
    unsigned char *data = NULL;
    size_t len = 0;
    CHECK(owf_project_report(p, OWF_PREPORT_WHO_DOES_WHAT, &doc) == OWF_OK && doc, "build");
    if (!doc) return;
    CHECK(doc->title && !strcmp(doc->title, "Garden: Who Does What"), "title %s", doc->title ? doc->title : "");
    CHECK(doc->footer.nparas == 1 && doc->header.nparas == 1, "header and footer");
    CHECK(pdf && pdf->export(doc, &data, &len, NULL) == OWF_OK && data && len > 200 && !memcmp(data, "%PDF-", 5),
          "pdf");
    free(data);
    data = NULL;
    CHECK(odt && odt->export(doc, &data, &len, NULL) == OWF_OK && data && len > 200 && !memcmp(data, "PK", 2), "odt");
    free(data);
    owf_doc_free(doc);
}

int main(void)
{
    owf_project *p = sample();
    test_names();
    if (p) {
        test_summary(p);
        test_tasks(p);
        test_resources(p);
        test_cash_flow(p);
        test_writers(p);
        owf_project_free(p);
    }
    test_cash_flow_long();
    test_empty_and_errors();
    test_units();
    if (fails) {
        printf("report tests: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("report tests passed (%d checks)\n", checks);
    return 0;
}
