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
        test_errors_and_empty(p);
        ob_close(p);
    }
    printf("%s: %d checks, %d failed\n", fails ? "FAILED" : "base reports", checks, fails);
    return fails ? 1 : 0;
}
