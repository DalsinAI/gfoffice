/*
 * GFOpenBase's reports on the PC (include/openbase_report.h, design 7):
 * a report made from a table or a saved query, grouped and totalled, built
 * as an owf_doc and saved through libowf as text, ODT and DOCX, against the
 * pinned SQLite (mk/base-report.mk). The project and its rows are made here.
 *
 *   test-base-report [DIR]   DIR: a scratch drawer (default: a new one in /tmp)
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "openbase_report.h"

static int fails, checks;
static char dir[512];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char *path(const char *file)
{
    static char p[4][600];
    static int k;
    k = (k + 1) % 4;
    snprintf(p[k], sizeof p[k], "%s/%s", dir, file);
    return p[k];
}

static char *slurp(const char *file, size_t *len)
{
    FILE *f = fopen(file, "rb");
    char *data;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n + 1);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) { free(data); data = NULL; }
    if (data) data[n] = 0;
    fclose(f);
    if (len) *len = (size_t)n;
    return data;
}

/* The document as plain text, through libowf's own text writer. */
static char *as_text(const owf_doc *doc, const char *name)
{
    owf_report *notes = owf_report_new();
    char *text = NULL;
    if (owf_export_file(doc, path(name), "text", notes) == OWF_OK)
        text = slurp(path(name), NULL);
    owf_report_free(notes);
    return text;
}

static ob_project *orders(void)
{
    static ob_table t;
    static const char *const fields[] = { "Customer", "Town", "Amount", "Qty" };
    static const char *const rows[][4] = {
        { "Ann", "Leeds", "10.50", "2" },
        { "Bob", "York", "3.25", "1" },
        { "Cy", "Leeds", "1,000.00", "5" },
        { "Di", "York", "", "4" },
    };
    char err[OB_ERR_MAX];
    ob_project *p;
    ob_int key;
    int i;
    remove(path("report.oadb"));
    p = ob_open(path("report.oadb"), OB_OPEN_CREATE, err, sizeof err);
    CHECK(p != NULL, "create: %s", err);
    if (!p) return NULL;
    ob_table_init(&t, "Orders");
    ob_table_add_field(&t, "Customer", OB_TEXT);
    ob_table_add_field(&t, "Town", OB_TEXT);
    ob_table_add_field(&t, "Amount", OB_CURRENCY);
    ob_table_add_field(&t, "Qty", OB_INTEGER);
    CHECK(ob_table_create(p, &t), "table: %s", ob_error(p));
    for (i = 0; i < 4; i++)
        CHECK(ob_row_add(p, "Orders", 4, fields, rows[i], &key), "row %d: %s", i, ob_error(p));
    return p;
}

static void test_grouped_table(ob_project *p)
{
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;
    int i, ids[8], nids = 0, j;
    const owf_story *st;

    ob_report_init(&r, "Sales");
    strcpy(r.title, "Sales by town");
    strcpy(r.table, "Orders");
    strcpy(r.group, "Town");
    ob_report_add_column(&r, "Customer", OB_TOTAL_COUNT);
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    ob_report_add_column(&r, "Qty", OB_TOTAL_AVERAGE);
    CHECK(ob_report_check(p, &r, err, sizeof err) == 0, "check: %s", err);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "grouped.txt");
    CHECK(text && !strcmp(text,
        "Sales by town\n"
        "Town: Leeds\n"
        "Customer\tAmount\tQty\n"
        "Ann\t10.50\t2\n"
        "Cy\t1,000.00\t5\n"
        "Count 2\tSum 1,010.50\tAverage 3.5\n"
        "Town: York\n"
        "Customer\tAmount\tQty\n"
        "Bob\t3.25\t1\n"
        "Di\t\t4\n"
        "Count 2\tSum 3.25\tAverage 2.5\n"
        "Report totals\n"
        "Customer\tAmount\tQty\n"
        "Count 4\tSum 1,013.75\tAverage 3\n"), "text:\n%s", text ? text : "(none)");
    free(text);

    st = &doc->body;
    CHECK(st->nparas > 0 && st->paras[0].fmt.heading == 1, "title is a heading");
    CHECK(st->nparas > 1 && st->paras[1].fmt.heading == 2, "group is a heading");
    for (i = 0; i < st->nparas; i++) {
        const owf_para *q = &st->paras[i];
        if (q->table_id < 0) continue;
        for (j = 0; j < nids && ids[j] != q->table_id; j++) ;
        if (j == nids && nids < 8) ids[nids++] = q->table_id;
        CHECK(q->table_cols == 3, "three columns, para %d", i);
        if (q->table_row == 0 && q->nruns)
            CHECK(q->runs[0].fmt.flags & OWF_BOLD, "headings bold, para %d", i);
    }
    CHECK(nids == 3, "a table for each group and the totals: %d", nids);
    CHECK(doc->title && !strcmp(doc->title, "Sales by town"), "document title");
    CHECK(doc->footer.nparas == 1, "a page footer");
    if (doc->footer.nparas == 1) {
        const owf_para *f = &doc->footer.paras[0];
        int page = 0, pages = 0;
        for (j = 0; j < f->nruns; j++) {
            if (f->runs[j].kind == OWF_RUN_FIELD && f->runs[j].field == OWF_FIELD_PAGE) page++;
            if (f->runs[j].kind == OWF_RUN_FIELD && f->runs[j].field == OWF_FIELD_PAGES) pages++;
        }
        CHECK(page == 1 && pages == 1, "Page # of ## in the footer");
        CHECK(f->fmt.align == OWF_ALIGN_CENTRE, "footer centred");
    }
    owf_doc_free(doc);
}

static void test_query_and_save(ob_project *p)
{
    static ob_query q;
    ob_qcolumn *c;
    ob_report r;
    owf_doc *doc = NULL, *back = NULL;
    owf_report *notes;
    char err[OB_ERR_MAX] = "";
    char *text, *data;
    size_t len;
    static const char want[] =
        "Ventes \xc3\xa0 Leeds\n"
        "Customer\tAmount\n"
        "Cy\t1,000.00\n"
        "Ann\t10.50\n"
        "\tMax 1,000.00\n";

    ob_query_init(&q, "LeedsOrders");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Orders", "Customer");
    c->sort = OB_SORT_DOWN;
    ob_query_add_column(&q, "Orders", "Amount");
    c = ob_query_add_column(&q, "Orders", "Town");
    c->show = 0;
    strcpy(c->criteria[0], "Leeds");
    CHECK(ob_query_save(p, &q), "save query: %s", ob_error(p));

    ob_report_init(&r, "Leeds");
    strcpy(r.title, "Ventes \xe0 Leeds");          /* the project's ISO-8859-1 */
    strcpy(r.query, "LeedsOrders");
    ob_report_add_column(&r, "Customer", OB_TOTAL_NONE);
    ob_report_add_column(&r, "Amount", OB_TOTAL_MAX);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "query.txt");
    CHECK(text && !strcmp(text, want), "text:\n%s", text ? text : "(none)");
    free(text);
    owf_doc_free(doc);

    /* saved through libowf as ODT and DOCX: real packages that read back the same */
    notes = owf_report_new();
    CHECK(ob_report_save(p, &r, NULL, path("leeds.odt"), NULL, notes, err, sizeof err) == 0, "odt: %s", err);
    CHECK(ob_report_save(p, &r, NULL, path("leeds.docx"), "docx", notes, err, sizeof err) == 0, "docx: %s", err);
    owf_report_free(notes);
    data = slurp(path("leeds.odt"), &len);
    CHECK(data && len > 100 && !memcmp(data, "PK", 2), "odt is a zip");
    free(data);
    data = slurp(path("leeds.docx"), &len);
    CHECK(data && len > 100 && !memcmp(data, "PK", 2), "docx is a zip");
    free(data);
    notes = owf_report_new();
    CHECK(owf_import_file(path("leeds.odt"), NULL, &back, notes, NULL) == OWF_OK && back, "odt reads back");
    if (back) {
        text = as_text(back, "leeds-odt.txt");
        CHECK(text && !strcmp(text, want), "odt text:\n%s", text ? text : "(none)");
        free(text);
        owf_doc_free(back);
        back = NULL;
    }
    CHECK(owf_import_file(path("leeds.docx"), NULL, &back, notes, NULL) == OWF_OK && back, "docx reads back");
    if (back) {
        text = as_text(back, "leeds-docx.txt");
        CHECK(text && !strcmp(text, want), "docx text:\n%s", text ? text : "(none)");
        free(text);
        owf_doc_free(back);
    }
    owf_report_free(notes);
}

/* Groups are made as the rows come: a query grouped on a column it isn't sorted by first would give
 * Leeds, York, Leeds, York, so the check refuses it; sorted by the group first, the groups are whole. */
static void test_query_groups(ob_project *p)
{
    static ob_query q;
    ob_qcolumn *c;
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;

    ob_query_init(&q, "ByCustomer");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Orders", "Customer");
    c->sort = OB_SORT_UP;
    ob_query_add_column(&q, "Orders", "Town");
    ob_query_add_column(&q, "Orders", "Qty");
    CHECK(ob_query_save(p, &q), "save query: %s", ob_error(p));
    ob_report_init(&r, "Towns");
    strcpy(r.query, "ByCustomer");
    strcpy(r.group, "Town");
    ob_report_add_column(&r, "Customer", OB_TOTAL_NONE);
    ob_report_add_column(&r, "Qty", OB_TOTAL_SUM);
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "sort the query ByCustomer by Town first"),
          "%s", err);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == -1 && !doc, "build refuses");

    ob_query_init(&q, "ByTown");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Orders", "Town");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Orders", "Customer");
    c->sort = OB_SORT_UP;
    ob_query_add_column(&q, "Orders", "Qty");
    CHECK(ob_query_save(p, &q), "save query: %s", ob_error(p));
    strcpy(r.query, "ByTown");
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "towns.txt");
    CHECK(text && !strcmp(text,
        "Towns\n"
        "Town: Leeds\n"
        "Customer\tQty\n"
        "Ann\t2\n"
        "Cy\t5\n"
        "\tSum 7\n"
        "Town: York\n"
        "Customer\tQty\n"
        "Bob\t1\n"
        "Di\t4\n"
        "\tSum 5\n"
        "Report totals\n"
        "Customer\tQty\n"
        "\tSum 12\n"), "text:\n%s", text ? text : "(none)");
    free(text);
    owf_doc_free(doc);
}

/* Budgie's two (#241): a table's text groups take "leeds" and "Leeds" as one, as ob_view's NOCASE sort
 * puts them together; and a Sum or Average past a 64-bit integer says so instead of wrapping. */
static void test_case_groups_and_overflow(ob_project *p)
{
    static ob_table t;
    static const char *const fields[] = { "Town", "Big" };
    static const char *const rows[][2] = {
        { "Leeds", "9000000000000000000" },
        { "york", "1" },
        { "leeds", "9000000000000000000" },
        { "York", "2" },
    };
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;
    ob_int key;
    int i;

    ob_table_init(&t, "Mixed");
    ob_table_add_field(&t, "Town", OB_TEXT);
    ob_table_add_field(&t, "Big", OB_INTEGER);
    CHECK(ob_table_create(p, &t), "table: %s", ob_error(p));
    for (i = 0; i < 4; i++)
        CHECK(ob_row_add(p, "Mixed", 2, fields, rows[i], &key), "row %d: %s", i, ob_error(p));
    ob_report_init(&r, "Mixed");
    strcpy(r.table, "Mixed");
    strcpy(r.group, "Town");
    ob_report_add_column(&r, "Big", OB_TOTAL_SUM);
    ob_report_add_column(&r, "Big", OB_TOTAL_AVERAGE);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "mixed.txt");
    CHECK(text && !strcmp(text,
        "Mixed\n"
        "Town: Leeds\n"
        "Big\tBig\n"
        "9000000000000000000\t9000000000000000000\n"
        "9000000000000000000\t9000000000000000000\n"
        "Sum too large\tAverage too large\n"
        "Town: york\n"
        "Big\tBig\n"
        "1\t1\n"
        "2\t2\n"
        "Sum 3\tAverage 1.5\n"
        "Report totals\n"
        "Big\tBig\n"
        "Sum too large\tAverage too large\n"), "text:\n%s", text ? text : "(none)");
    free(text);
    owf_doc_free(doc);
}

/* The text a report is kept as (oadb_reports.def): every field back as it was, awkward characters
 * included; a later version's file, a broken line and an unknown total refused; unknown keys skipped. */
static void test_text_form(void)
{
    ob_report r, back;
    char text[4096], err[OB_ERR_MAX] = "";
    int n;

    ob_report_init(&r, "Sales");
    strcpy(r.title, "Ventes \xe0 Leeds | York\\2026\nsecond line");
    strcpy(r.table, "Orders");
    strcpy(r.sort, "Customer");
    r.desc = 1;
    strcpy(r.group, "Town");
    ob_report_add_column(&r, "Customer", OB_TOTAL_COUNT);
    strcpy(ob_report_add_column(&r, "Amount", OB_TOTAL_SUM)->heading, "Total | \xa3");
    ob_report_add_column(&r, "Qty", OB_TOTAL_NONE);
    n = ob_report_to_text(&r, text, sizeof text);
    CHECK(n > 0 && (size_t)n == strlen(text), "to text: %d", n);
    CHECK(!strncmp(text, "OADBREPORT 1\n", 13), "first line:\n%s", text);
    CHECK(!strchr(text + 13, '\r') && strstr(text, "column=Amount|Total \\| \xa3|Sum\n"), "columns:\n%s", text);
    CHECK(ob_report_from_text(&back, "Sales", text, err, sizeof err) == 0, "from text: %s", err);
    CHECK(!memcmp(&r, &back, sizeof r), "the same report back");
    CHECK(ob_report_to_text(&r, text, 20) == -1, "too small for the buffer");

    CHECK(ob_report_from_text(&back, "X", "OADBREPORT 3\ntable=T\n", err, sizeof err) == -1 &&
          strstr(err, "a newer GFOpenBase"), "%s", err);
    CHECK(ob_report_from_text(&back, "X", "table=T\n", err, sizeof err) == -1 && strstr(err, "not a report"), "%s", err);
    CHECK(ob_report_from_text(&back, "X", "OADBREPORT 1\ncolumn=Qty|x|Median\n", err, sizeof err) == -1 &&
          strstr(err, "Median"), "%s", err);
    CHECK(ob_report_from_text(&back, "X", "OADBREPORT 1\nnonsense\n", err, sizeof err) == -1 &&
          strstr(err, "line 2"), "%s", err);
    CHECK(ob_report_from_text(&back, "X", "OADBREPORT 1\ntable=T\nlater=1\ncolumn=A||\n", err, sizeof err) == 0 &&
          !strcmp(back.name, "X") && !strcmp(back.table, "T") && back.ncolumns == 1 &&
          back.columns[0].total == OB_TOTAL_NONE && !back.columns[0].heading[0], "unknown keys skipped: %s", err);
}

static void test_errors_and_empty(ob_project *p)
{
    static ob_table t;
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX];
    char *text;

    ob_report_init(&r, "Bad");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "needs a table or a query"), "%s", err);
    strcpy(r.table, "Orders");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "no columns"), "%s", err);
    ob_report_add_column(&r, "Price", OB_TOTAL_NONE);
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Orders has no field Price"), "%s", err);
    strcpy(r.columns[0].field, "Customer");
    r.columns[0].total = OB_TOTAL_SUM;
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Sum needs a number: Customer is Text"), "%s", err);
    r.columns[0].total = OB_TOTAL_GROUP;
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Customer: a column's total is"), "%s", err);
    r.columns[0].total = OB_TOTAL_NONE;
    strcpy(r.group, "County");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "group by County"), "%s", err);
    r.group[0] = 0;
    strcpy(r.query, "LeedsOrders");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "a table or a query, not both"), "%s", err);
    r.table[0] = 0;
    strcpy(r.query, "Nothing");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "no query Nothing"), "%s", err);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == -1 && !doc, "build refuses");

    /* an empty table: the title and a line that says so, no table */
    ob_table_init(&t, "Empty");
    ob_table_add_field(&t, "Name", OB_TEXT);
    CHECK(ob_table_create(p, &t), "table: %s", ob_error(p));
    ob_report_init(&r, "Nobody");
    strcpy(r.table, "Empty");
    ob_report_add_column(&r, "Name", OB_TOTAL_COUNT);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "empty.txt");
    CHECK(text && !strcmp(text, "Nobody\nNo records.\n"), "text:\n%s", text ? text : "(none)");
    free(text);
    owf_doc_free(doc);
}

/* Reports kept in the project (oadb_reports): the core keeps each one's text under its name, the
 * report code turns it to and from an ob_report. */
/* A report on a query that asks for a value: the value given is the one the rows are made with, and
 * without one the build says what to give. */
static void test_query_params(ob_project *p)
{
    static ob_query q;
    ob_qcolumn *c;
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;
    const char *const york[] = { "York" };

    ob_query_init(&q, "OneTown");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Orders", "Customer");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Orders", "Town");
    c->show = 0;
    strcpy(c->criteria[0], "[Which town?]");
    ob_query_add_column(&q, "Orders", "Qty");
    CHECK(ob_query_save(p, &q), "save query: %s", ob_error(p));
    ob_report_init(&r, "Town");
    strcpy(r.query, "OneTown");
    ob_report_add_column(&r, "Customer", OB_TOTAL_COUNT);
    ob_report_add_column(&r, "Qty", OB_TOTAL_SUM);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == -1 && !doc && strstr(err, "Which town?"),
          "no value: %s", err);
    CHECK(ob_report_build(p, &r, york, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "onetown.txt");
    CHECK(text && !strcmp(text, "Town\nCustomer\tQty\nBob\t1\nDi\t4\nCount 2\tSum 5\n"), "text:\n%s",
          text ? text : "(none)");
    free(text);
    owf_doc_free(doc);
}

static void test_kept_reports(ob_project *p)
{
    static char text[OB_REPORT_TEXT_MAX];
    ob_report r, back;
    char err[OB_ERR_MAX] = "";

    CHECK(ob_report_count(p) == 0, "no reports yet: %d", ob_report_count(p));
    CHECK(!ob_report_get_text(p, "Sales", text, sizeof text) && strstr(ob_error(p), "no report named Sales"),
          "%s", ob_error(p));
    CHECK(ob_report_put_text(p, "Sales", "OADBREPORT 1\ntable=Orders\n"), "put: %s", ob_error(p));
    CHECK(ob_report_put_text(p, "Away", "OADBREPORT 1\ntable=Mixed\n"), "put: %s", ob_error(p));
    CHECK(ob_report_count(p) == 2, "two reports: %d", ob_report_count(p));
    CHECK(!strcmp(ob_report_name(p, 0), "Away") && !strcmp(ob_report_name(p, 1), "Sales"), "in name order");
    CHECK(ob_report_name(p, 2) == NULL, "past the end");
    CHECK(ob_report_get_text(p, "sales", text, sizeof text) && !strcmp(text, "OADBREPORT 1\ntable=Orders\n"),
          "get without case: %s", ob_error(p));
    CHECK(!ob_report_get_text(p, "Sales", text, 8), "too small for the buffer");
    CHECK(!ob_report_put_text(p, "Orders", "OADBREPORT 1\n") && strstr(ob_error(p), "a table named Orders"),
          "%s", ob_error(p));
    CHECK(!ob_report_put_text(p, "", "OADBREPORT 1\n"), "a report needs a name");
    CHECK(ob_report_rename(p, "Sales", "Sales 2026"), "rename: %s", ob_error(p));
    CHECK(!ob_report_rename(p, "Sales 2026", "Away") && strstr(ob_error(p), "Away"), "%s", ob_error(p));
    CHECK(ob_report_delete(p, "Away") && ob_report_count(p) == 1, "delete: %s", ob_error(p));
    CHECK(!ob_report_delete(p, "Away"), "deleted already");

    /* an ob_report kept and read back whole */
    ob_report_init(&r, "Towns");
    strcpy(r.title, "Sales by town");
    strcpy(r.table, "Orders");
    strcpy(r.group, "Town");
    ob_report_add_column(&r, "Customer", OB_TOTAL_COUNT);
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    CHECK(ob_report_store(p, &r, err, sizeof err) == 0, "store: %s", err);
    CHECK(ob_report_load(p, "towns", &back, err, sizeof err) == 0, "load: %s", err);
    CHECK(!strcmp(back.name, "Towns") && !memcmp(&r, &back, sizeof r), "the same report back: %s", back.name);
    CHECK(ob_report_load(p, "Nothing", &back, err, sizeof err) == -1 && strstr(err, "no report named Nothing"),
          "%s", err);
    ob_report_add_column(&r, "Price", OB_TOTAL_NONE);
    CHECK(ob_report_store(p, &r, err, sizeof err) == -1 && strstr(err, "Orders has no field Price"),
          "a report that can't be made isn't kept: %s", err);
}

/* ---- Report design 2: group levels and bands (core 0.2.0) ---- */

static void sales_table(ob_project *p)
{
    static ob_table t;
    static const char *const fields[] = { "Region", "Town", "Customer", "Amount" };
    static const char *const rows[][4] = {
        { "North", "Leeds", "Ann", "10" },
        { "South", "Bath", "Di", "5" },
        { "North", "York", "Bob", "20" },
        { "North", "leeds", "Cy", "30" },
        { "South", "Bath", "Ed", "7" },
    };
    ob_int key;
    int i;
    ob_table_init(&t, "Sales");
    ob_table_add_field(&t, "Region", OB_TEXT);
    ob_table_add_field(&t, "Town", OB_TEXT);
    ob_table_add_field(&t, "Customer", OB_TEXT);
    ob_table_add_field(&t, "Amount", OB_INTEGER);
    CHECK(ob_table_create(p, &t), "Sales: %s", ob_error(p));
    for (i = 0; i < 5; i++)
        CHECK(ob_row_add(p, "Sales", 4, fields, rows[i], &key), "Sales row %d: %s", i, ob_error(p));
}

/* Two levels: a heading for each region and each town in it, the rows in a table per town with its
 * totals, the region's totals after its towns, then the report's. Towns compare without case. */
static void test_group_levels(ob_project *p)
{
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;
    int i, h2 = 0, h3 = 0;

    sales_table(p);
    ob_report_init(&r, "Regions");
    strcpy(r.title, "Sales by region");
    strcpy(r.table, "Sales");
    strcpy(r.group, "Region");
    strcpy(r.subgroup[0], "Town");
    ob_report_add_column(&r, "Customer", OB_TOTAL_COUNT);
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    CHECK(ob_report_check(p, &r, err, sizeof err) == 0, "check: %s", err);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "levels.txt");
    CHECK(text && !strcmp(text,
        "Sales by region\n"
        "Region: North\n"
        "Town: Leeds\n"
        "Customer\tAmount\n"
        "Ann\t10\n"
        "Cy\t30\n"
        "Count 2\tSum 40\n"
        "Town: York\n"
        "Customer\tAmount\n"
        "Bob\t20\n"
        "Count 1\tSum 20\n"
        "Totals for Region: North\n"
        "Customer\tAmount\n"
        "Count 3\tSum 60\n"
        "Region: South\n"
        "Town: Bath\n"
        "Customer\tAmount\n"
        "Di\t5\n"
        "Ed\t7\n"
        "Count 2\tSum 12\n"
        "Totals for Region: South\n"
        "Customer\tAmount\n"
        "Count 2\tSum 12\n"
        "Report totals\n"
        "Customer\tAmount\n"
        "Count 5\tSum 72\n"), "text:\n%s", text ? text : "(none)");
    free(text);
    for (i = 0; i < doc->body.nparas; i++) {
        if (doc->body.paras[i].fmt.heading == 2) h2++;
        if (doc->body.paras[i].fmt.heading == 3) h3++;
    }
    CHECK(h2 == 3 && h3 == 3, "regions (and the report totals) heading 2, towns heading 3: %d %d", h2, h3);
    owf_doc_free(doc);

    /* Z to A: every level */
    r.desc = 1;
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build desc: %s", err);
    if (doc) {
        text = as_text(doc, "levels-desc.txt");
        CHECK(text && strstr(text, "Region: South\nTown: Bath\n") &&
              strstr(text, "Region: North\nTown: York\n") && strstr(text, "Town: York\nCustomer\tAmount\nBob\t20\n") &&
              strstr(text, "Town: Leeds\nCustomer\tAmount\nAnn\t10\nCy\t30\n") &&
              strstr(text, "Region: South") < strstr(text, "Region: North"), "desc:\n%s", text ? text : "(none)");
        free(text);
        owf_doc_free(doc);
        doc = NULL;
    }
    r.desc = 0;

    /* a third level, and a level's totals left out */
    strcpy(r.subgroup[1], "Customer");
    r.no_totals = 4;                      /* bit 2: the customers' */
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build 3 levels: %s", err);
    if (doc) {
        int h4 = 0;
        text = as_text(doc, "levels3.txt");
        CHECK(text && strstr(text, "Town: Leeds\nCustomer: Ann\nCustomer\tAmount\nAnn\t10\nCustomer: Cy\n") &&
              strstr(text, "Customer: Cy\nCustomer\tAmount\nCy\t30\nTotals for Town: Leeds\nCustomer\tAmount\nCount 2\tSum 40\n"),
              "three levels:\n%s", text ? text : "(none)");
        free(text);
        for (i = 0; i < doc->body.nparas; i++)
            if (doc->body.paras[i].fmt.heading == 4) h4++;
        CHECK(h4 == 5, "customers heading 4: %d", h4);
        owf_doc_free(doc);
        doc = NULL;
    }

    /* the ways it can't be made */
    strcpy(r.subgroup[0], "");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Regions: a third group needs a second"),
          "gap in the levels: %s", err);
    strcpy(r.subgroup[0], "Postcode");
    strcpy(r.subgroup[1], "");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "group by Postcode: Sales has no field Postcode"),
          "a level that isn't a field: %s", err);
    strcpy(r.subgroup[0], "Town");
    strcpy(r.group, "");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Regions: a second group needs a first"),
          "a subgroup with no group: %s", err);
}

/* A grid query's rows come in its own order: grouped on two levels, it must sort by both first. */
static void test_query_levels(ob_project *p)
{
    static ob_query q;
    ob_qcolumn *c;
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;

    ob_query_init(&q, "SalesByTown");
    ob_query_add_table(&q, "Sales");
    c = ob_query_add_column(&q, "Sales", "Region");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Sales", "Customer");
    c->sort = OB_SORT_UP;
    ob_query_add_column(&q, "Sales", "Town");
    ob_query_add_column(&q, "Sales", "Amount");
    CHECK(ob_query_save(p, &q), "save: %s", ob_error(p));
    ob_report_init(&r, "Q");
    strcpy(r.query, "SalesByTown");
    strcpy(r.group, "Region");
    strcpy(r.subgroup[0], "Town");
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 &&
          strstr(err, "group by Town: sort the query SalesByTown by Region, then Town, first"), "%s", err);
    strcpy(r.subgroup[0], "Customer");
    CHECK(ob_report_check(p, &r, err, sizeof err) == 0, "sorted by both: %s", err);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (doc) {
        text = as_text(doc, "qlevels.txt");
        CHECK(text && strstr(text, "Region: North\nCustomer: Ann\nAmount\n10\nSum 10\nCustomer: Bob\n") &&
              strstr(text, "Totals for Region: South\nAmount\nSum 12\nReport totals\nAmount\nSum 72\n"),
              "query levels:\n%s", text ? text : "(none)");
        free(text);
        owf_doc_free(doc);
    }
}

static int has_field(const owf_story *st, owf_field field)
{
    int i, j;
    for (i = 0; i < st->nparas; i++)
        for (j = 0; j < st->paras[i].nruns; j++)
            if (st->paras[i].runs[j].kind == OWF_RUN_FIELD && st->paras[i].runs[j].field == field) return 1;
    return 0;
}

/* The bands: the report's header and footer, a page header and footer, and each level's group header and
 * footer, as text with [names] in it: [Title], [Date], [Page], [Pages], [Group], [Value], [Count] and the
 * source's fields; "[[" is a '['. */
static void test_bands(ob_project *p)
{
    ob_report r;
    owf_doc *doc = NULL;
    char err[OB_ERR_MAX] = "";
    char *text;
    int i, breaks = 0, first_break = -1, h2 = 0;

    ob_report_init(&r, "Sales");
    strcpy(r.title, "Sales by town");
    strcpy(r.table, "Orders");
    strcpy(r.group, "Town");
    ob_report_add_column(&r, "Customer", OB_TOTAL_COUNT);
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    strcpy(r.head, "All orders ([Title]) [[draft]");
    strcpy(r.group_head[0], "[Value] ([Group]), first [Customer]");
    strcpy(r.group_foot[0], "[Count] orders in [Value], last [Customer]");
    strcpy(r.foot, "[Count] orders in all");
    strcpy(r.page_head, "[Title], printed [Date]");
    strcpy(r.page_foot, "Page [Page]");
    r.new_page = 1;
    CHECK(ob_report_check(p, &r, err, sizeof err) == 0, "check: %s", err);
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (!doc) return;
    text = as_text(doc, "bands.txt");
    CHECK(text && !strncmp(text,
        "All orders (Sales by town) [draft]\n"
        "Leeds (Town), first Ann\n"
        "Customer\tAmount\n"
        "Ann\t10.50\n"
        "Cy\t1,000.00\n"
        "Count 2\tSum 1,010.50\n"
        "2 orders in Leeds, last Cy\n"
        "York (Town), first Bob\n", 140) && strstr(text,
        "Count 2\tSum 3.25\n"
        "2 orders in York, last Di\n"
        "Report totals\n"
        "Customer\tAmount\n"
        "Count 4\tSum 1,013.75\n"
        "4 orders in all\n"), "text:\n%s", text ? text : "(none)");
    free(text);
    CHECK(doc->body.nparas > 0 && doc->body.paras[0].fmt.heading == 1, "the report header is heading 1");
    CHECK(doc->title && !strcmp(doc->title, "Sales by town"), "document title is still the title: %s",
          doc->title ? doc->title : "(none)");
    for (i = 0; i < doc->body.nparas; i++) {
        const owf_para *q = &doc->body.paras[i];
        if (q->fmt.heading == 2 && h2++ < 2) {
            if (q->fmt.page_break_before) { breaks++; if (first_break < 0) first_break = h2; }
        }
    }
    CHECK(breaks == 1 && first_break == 2, "a new page before each town but the first: %d %d", breaks, first_break);
    CHECK(doc->header.nparas == 1 && has_field(&doc->header, OWF_FIELD_DATE), "page header with the date");
    CHECK(doc->footer.nparas == 1 && has_field(&doc->footer, OWF_FIELD_PAGE) && !has_field(&doc->footer, OWF_FIELD_PAGES),
          "page footer: Page n");
    owf_doc_free(doc);
    doc = NULL;

    /* a level's totals left out; the report's stay */
    r.no_totals = 1;
    r.new_page = 0;
    CHECK(ob_report_build(p, &r, NULL, &doc, err, sizeof err) == 0 && doc, "build: %s", err);
    if (doc) {
        text = as_text(doc, "nototals.txt");
        CHECK(text && strstr(text, "Cy\t1,000.00\n2 orders in Leeds, last Cy\n") &&
              strstr(text, "Report totals\nCustomer\tAmount\nCount 4\tSum 1,013.75\n"), "no group totals:\n%s",
              text ? text : "(none)");
        free(text);
        owf_doc_free(doc);
        doc = NULL;
    }

    /* names that aren't */
    strcpy(r.group_head[0], "[Price]");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Sales: group header: Orders has no field Price"),
          "%s", err);
    strcpy(r.group_head[0], "");
    strcpy(r.page_foot, "[Customer]");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Sales: page footer: [Customer]: a page shows no row"),
          "%s", err);
    strcpy(r.page_foot, "");
    strcpy(r.head, "[Count]");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Sales: report header: [Count]"), "%s", err);
    strcpy(r.head, "[Title");
    CHECK(ob_report_check(p, &r, err, sizeof err) == -1 && strstr(err, "Sales: report header: no ] after [Title"),
          "%s", err);
}

/* The new settings are kept, as report text version 2; a report that uses none of them is still version 1,
 * so an older GFOpenBase reads it. */
static void test_text_form_bands(void)
{
    ob_report r, back;
    char text[8192], err[OB_ERR_MAX] = "";

    ob_report_init(&r, "Levels");
    strcpy(r.table, "Sales");
    strcpy(r.group, "Region");
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    CHECK(ob_report_to_text(&r, text, sizeof text) > 0 && !strncmp(text, "OADBREPORT 1\n", 13), "still 1:\n%s", text);
    strcpy(r.subgroup[0], "Town");
    strcpy(r.subgroup[1], "Customer");
    strcpy(r.head, "[Title] | head\\");
    strcpy(r.foot, "two\nlines");
    strcpy(r.page_head, "ph");
    strcpy(r.page_foot, "pf");
    strcpy(r.group_head[0], "g0 | x");
    strcpy(r.group_head[2], "g2");
    strcpy(r.group_foot[1], "f1");
    r.no_totals = 5;
    r.new_page = 1;
    CHECK(ob_report_to_text(&r, text, sizeof text) > 0 && !strncmp(text, "OADBREPORT 2\n", 13), "2:\n%s", text);
    CHECK(strstr(text, "grouphead=1|g0 \\| x\n") && strstr(text, "groupfoot=2|f1\n") && strstr(text, "subgroup=Town\n"),
          "settings:\n%s", text);
    CHECK(ob_report_from_text(&back, "Levels", text, err, sizeof err) == 0, "from text: %s", err);
    CHECK(!memcmp(&r, &back, sizeof r), "the same report back");
    CHECK(ob_report_from_text(&back, "X", "OADBREPORT 2\ntable=T\ngrouphead=4|x\n", err, sizeof err) == -1 &&
          strstr(err, "line 3"), "a level past 3: %s", err);
    CHECK(ob_report_from_text(&back, "X", "OADBREPORT 2\ntable=T\nsubgroup=A\nsubgroup=B\nsubgroup=C\n", err, sizeof err) == -1 &&
          strstr(err, "line 5"), "a fourth level: %s", err);
}

/* ---- The bands as a designer sees them (0.2.1): each band's text by its kind and level, and the names
 * it may have ---- */

static void test_band_api(ob_project *p)
{
    static ob_report r;
    char err[OB_ERR_MAX] = "", text[OB_BAND_MAX];
    size_t size = 0;
    int b, L;

    ob_report_init(&r, "Api");
    CHECK(ob_report_band(&r, OB_BAND_HEAD, 0, &size) == r.head && size == sizeof r.head, "head");
    CHECK(ob_report_band(&r, OB_BAND_FOOT, 0, &size) == r.foot, "foot");
    CHECK(ob_report_band(&r, OB_BAND_PAGE_HEAD, 0, &size) == r.page_head, "page head");
    CHECK(ob_report_band(&r, OB_BAND_PAGE_FOOT, 0, &size) == r.page_foot, "page foot");
    CHECK(ob_report_band(&r, OB_BAND_GROUP_HEAD, 2, &size) == r.group_head[2] && size == OB_BAND_MAX, "group head 3");
    CHECK(ob_report_band(&r, OB_BAND_GROUP_FOOT, 1, NULL) == r.group_foot[1], "group foot 2, no size wanted");
    CHECK(ob_report_band(&r, OB_BAND_GROUP_HEAD, OB_REPORT_LEVELS, &size) == NULL, "no fourth level");
    CHECK(ob_report_band(&r, OB_BAND_GROUP_FOOT, -1, &size) == NULL, "no level -1");
    CHECK(ob_report_band(&r, OB_BANDS, 0, &size) == NULL, "no band past the last");
    CHECK(!strcmp(ob_report_band_title(OB_BAND_PAGE_FOOT), "page footer") &&
          !strcmp(ob_report_band_title(OB_BAND_GROUP_HEAD), "group header") && !strcmp(ob_report_band_title(OB_BANDS), ""),
          "titles");
    CHECK(!strcmp(ob_report_band_names(OB_BAND_HEAD), "[Title] [Date]"), "%s", ob_report_band_names(OB_BAND_HEAD));
    CHECK(!strcmp(ob_report_band_names(OB_BAND_FOOT), "[Title] [Date] [Count]"), "%s", ob_report_band_names(OB_BAND_FOOT));
    CHECK(!strcmp(ob_report_band_names(OB_BAND_PAGE_HEAD), "[Title] [Date] [Page] [Pages]"), "%s",
          ob_report_band_names(OB_BAND_PAGE_HEAD));
    CHECK(!strcmp(ob_report_band_names(OB_BAND_GROUP_HEAD), "[Title] [Date] [Group] [Value] [a field]"), "%s",
          ob_report_band_names(OB_BAND_GROUP_HEAD));
    CHECK(!strcmp(ob_report_band_names(OB_BAND_GROUP_FOOT), "[Title] [Date] [Group] [Value] [Count] [a field]"), "%s",
          ob_report_band_names(OB_BAND_GROUP_FOOT));

    /* what each band says it may name, it may: every band made of its own names (a real field for
     * "[a field]") passes the check, at every level */
    strcpy(r.table, "Sales");
    strcpy(r.group, "Region");
    strcpy(r.subgroup[0], "Town");
    strcpy(r.subgroup[1], "Customer");
    ob_report_add_column(&r, "Amount", OB_TOTAL_SUM);
    for (b = 0; b < OB_BANDS; b++)
        for (L = 0; L < OB_REPORT_LEVELS; L++) {
            char *t = ob_report_band(&r, (ob_band)b, L, &size);
            const char *n = ob_report_band_names((ob_band)b), *f = strstr(n, "[a field]");
            if (!t) continue;
            snprintf(text, sizeof text, "%.*s%s", f ? (int)(f - n) : (int)strlen(n), n, f ? "[Customer]" : "");
            snprintf(t, size, "%s", text);
        }
    CHECK(ob_report_check(p, &r, err, sizeof err) == 0, "every band of its own names: %s", err);
}

int main(int argc, char **argv)
{
    ob_project *p;
    if (argc > 1) {
        snprintf(dir, sizeof dir, "%s", argv[1]);
    } else {
        char t[] = "/tmp/ob-report-XXXXXX";
        if (!mkdtemp(t)) { perror("mkdtemp"); return 1; }
        snprintf(dir, sizeof dir, "%s", t);
    }
    p = orders();
    if (p) {
        test_grouped_table(p);
        test_query_and_save(p);
        test_query_groups(p);
        test_case_groups_and_overflow(p);
        test_text_form();
        test_kept_reports(p);
        test_query_params(p);
        test_errors_and_empty(p);
        test_group_levels(p);
        test_query_levels(p);
        test_bands(p);
        test_text_form_bands();
        test_band_api(p);
        ob_close(p);
    }
    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "base reports", checks, fails);
    return fails ? 1 : 0;
}
