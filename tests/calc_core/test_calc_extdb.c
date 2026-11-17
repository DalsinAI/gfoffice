/*
 * GFOpenCalc's database sources for External Data on the PC (include/opencalc_extdb.h): a GFOpenBase
 * project's table, saved queries, a view and queries typed in; a plain SQLite file; files that are not
 * databases; and a range filled from a project through GFOpenCalc's core. Run from the repository's top
 * (make calc-extdb-test, make calc-extdb-check).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "opencalc_core.h"
#include "opencalc_extdb.h"
#include "openbase_core.h"
#include "sqlite3.h"

static int failures, checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        checks++;                                                                    \
        if (!(cond)) {                                                               \
            failures++;                                                              \
            fprintf(stderr, "%s:%d: %s: failed: %s\n", __FILE__, __LINE__, __func__, #cond); \
        }                                                                            \
    } while (0)

#define CHECK_STR(got, want)                                                         \
    do {                                                                             \
        const char *g_ = (got), *w_ = (want);                                        \
        checks++;                                                                    \
        if (!g_ || strcmp(g_, w_)) {                                                 \
            failures++;                                                              \
            fprintf(stderr, "%s:%d: %s: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, __func__, g_ ? g_ : "(null)", w_); \
        }                                                                            \
    } while (0)

#define SHOP "build/host/calc-extdb-shop.oadb"
#define PLAIN "build/host/calc-extdb-plain.db"
#define NOTDB "build/host/calc-extdb-not.db"

static const char *cell(const oc_exttable *t, unsigned row, unsigned col)
{
    const oc_extvalue *v;
    static char buf[64];
    if (row >= t->rows || col >= t->cols)
        return "(outside)";
    v = &t->cells[(size_t)row * t->cols + col];
    if (v->kind == OC_EXTV_NUMBER) {
        snprintf(buf, sizeof buf, "#%g", v->number);
        return buf;
    }
    return v->kind == OC_EXTV_EMPTY ? "" : v->text ? v->text : "(null)";
}

static int kind(const oc_exttable *t, unsigned row, unsigned col)
{
    return row < t->rows && col < t->cols ? t->cells[(size_t)row * t->cols + col].kind : -1;
}

static int make_shop(void)
{
    static ob_table t;
    static ob_query q;
    char err[OB_ERR_MAX];
    ob_project *p;
    ob_field *f;
    static const char *const fields[] = { "Name", "Code", "Price", "Added", "In stock" };
    static const char *const widget[] = { "Widget", "0123", "2.50", "2026-11-16", "Yes" };
    static const char *const gadget[] = { "=Gadget", "", "1,234.50", "", "No" };
    remove(SHOP);
    if (!(p = ob_open(SHOP, OB_OPEN_CREATE, err, sizeof err)))
        return 0;
    ob_table_init(&t, "Products");
    f = ob_table_add_field(&t, "Name", OB_TEXT);
    f = f ? ob_table_add_field(&t, "Code", OB_TEXT) : NULL;
    f = f ? ob_table_add_field(&t, "Price", OB_CURRENCY) : NULL;
    f = f ? ob_table_add_field(&t, "Added", OB_DATE) : NULL;
    f = f ? ob_table_add_field(&t, "In stock", OB_YESNO) : NULL;
    f = f ? ob_table_add_field(&t, "Picture", OB_OBJECT) : NULL;
    if (!f || !ob_table_create(p, &t) || !ob_row_add(p, "Products", 5, fields, widget, NULL) ||
        !ob_row_add(p, "Products", 5, fields, gadget, NULL)) {
        fprintf(stderr, "make_shop: %s\n", ob_error(p));
        ob_close(p);
        return 0;
    }
    /* a query drawn in the designer: its Price is Currency */
    ob_query_init(&q, "Prices");
    ob_query_add_table(&q, "Products");
    ob_query_add_column(&q, "Products", "Name");
    ob_query_add_column(&q, "Products", "Price");
    if (!ob_query_save(p, &q)) {
        fprintf(stderr, "make_shop: %s\n", ob_error(p));
        ob_close(p);
        return 0;
    }
    /* SQL, one asking for a value, and a view */
    ob_query_init(&q, "Count");
    q.sql_only = 1;
    snprintf(q.sql, sizeof q.sql, "SELECT count(*) AS Products, 'x' || 'y' AS Text FROM Products");
    if (!ob_query_save(p, &q))
        fprintf(stderr, "make_shop: %s\n", ob_error(p));
    ob_query_init(&q, "Ask");
    q.sql_only = 1;
    snprintf(q.sql, sizeof q.sql, "SELECT Name FROM Products WHERE Name = :which");
    if (!ob_query_save(p, &q))
        fprintf(stderr, "make_shop: %s\n", ob_error(p));
    ob_query_init(&q, "Names");
    q.sql_only = 1;
    snprintf(q.sql, sizeof q.sql, "SELECT Name FROM Products ORDER BY Name");
    if (!ob_query_save_view(p, &q, "Name list"))
        fprintf(stderr, "make_shop view: %s\n", ob_error(p));
    ob_close(p);
    return 1;
}

static void test_project(void)
{
    oc_exttable t;
    oc_extdb_item items[8];
    char err[256];
    int n, i, saw_view = 0;

    CHECK(make_shop());
    CHECK(oc_extdb_query(NULL, SHOP, "Products", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 3 && t.cols == 7);
    CHECK_STR(cell(&t, 0, 0), "id");
    CHECK_STR(cell(&t, 0, 3), "Price");
    CHECK_STR(cell(&t, 0, 5), "In stock");
    CHECK_STR(cell(&t, 1, 1), "Widget");
    CHECK(kind(&t, 1, 1) == OC_EXTV_TEXT);
    CHECK_STR(cell(&t, 1, 2), "0123");
    CHECK(kind(&t, 1, 2) == OC_EXTV_TEXT);                  /* text stays text */
    CHECK_STR(cell(&t, 1, 3), "2.50");
    CHECK(kind(&t, 1, 3) == OC_EXTV_INPUT);                 /* Currency: put in as typed */
    CHECK(kind(&t, 1, 4) == OC_EXTV_INPUT);
    CHECK_STR(cell(&t, 1, 5), "Yes");
    CHECK_STR(cell(&t, 1, 6), "");
    CHECK_STR(cell(&t, 2, 1), "=Gadget");
    CHECK_STR(cell(&t, 2, 2), "");
    CHECK_STR(cell(&t, 2, 3), "1,234.50");
    CHECK(kind(&t, 1, 0) == OC_EXTV_INPUT);                 /* the Counter */
    oc_exttable_free(&t);

    CHECK(oc_extdb_query(NULL, SHOP, " products ;", 0, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 2 && !strcmp(cell(&t, 0, 1), "Widget"));
    oc_exttable_free(&t);

    CHECK(oc_extdb_query(NULL, SHOP, "Prices", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 3 && t.cols == 2);
    CHECK_STR(cell(&t, 0, 1), "Price");
    CHECK_STR(cell(&t, 2, 1), "1,234.50");
    CHECK(kind(&t, 2, 1) == OC_EXTV_INPUT);
    oc_exttable_free(&t);

    CHECK(oc_extdb_query(NULL, SHOP, "Count", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 2 && !strcmp(cell(&t, 1, 0), "2") && kind(&t, 1, 0) == OC_EXTV_INPUT);
    CHECK(kind(&t, 1, 1) == OC_EXTV_TEXT && !strcmp(cell(&t, 1, 1), "xy"));
    oc_exttable_free(&t);

    CHECK(oc_extdb_query(NULL, SHOP, "select Name from Products where Code = '0123'", 0, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 1 && t.cols == 1 && !strcmp(cell(&t, 0, 0), "Widget"));
    oc_exttable_free(&t);

    CHECK(oc_extdb_query(NULL, SHOP, "Name list", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 3 && !strcmp(cell(&t, 1, 0), "=Gadget"));
    oc_exttable_free(&t);

    CHECK(oc_extdb_query(NULL, SHOP, "Ask", 1, &t, err, sizeof err) == OWF_ERR_FORMAT && strstr(err, "value"));
    CHECK(oc_extdb_query(NULL, SHOP, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n) "
                         "SELECT count(*) FROM n", 0, &t, err, sizeof err) == OWF_ERR_FORMAT);
    CHECK(strstr(err, "too much work") != NULL);
    CHECK(oc_extdb_query(NULL, SHOP, "Count", 0, &t, err, sizeof err) == OWF_OK);   /* the next one runs */
    oc_exttable_free(&t);
    CHECK(!t.cells && !t.rows);
    CHECK(oc_extdb_query(NULL, SHOP, "Nothing", 1, &t, err, sizeof err) == OWF_ERR_FORMAT && strstr(err, "Nothing"));
    CHECK(oc_extdb_query(NULL, SHOP, "DELETE FROM Products", 1, &t, err, sizeof err) == OWF_ERR_FORMAT);
    CHECK(oc_extdb_query(NULL, SHOP, "SELECT nonsense FROM", 1, &t, err, sizeof err) == OWF_ERR_FORMAT && err[0]);
    CHECK(oc_extdb_query(NULL, SHOP, "   ", 1, &t, err, sizeof err) == OWF_ERR_FORMAT);
    CHECK(oc_extdb_query(NULL, SHOP, "Products", 1, &t, err, sizeof err) == OWF_OK && t.rows == 3);   /* nothing changed */
    oc_exttable_free(&t);

    n = oc_extdb_items(SHOP, items, 8, err, sizeof err);
    CHECK(n == 5);
    CHECK(items[0].kind == OC_EXTDB_TABLE && !strcmp(items[0].name, "Products"));
    for (i = 0; i < n && i < 8; i++)
        saw_view += items[i].kind == OC_EXTDB_VIEW && !strcmp(items[i].name, "Name list");
    CHECK(saw_view == 1);
    CHECK(items[2].kind == OC_EXTDB_QUERY && !strcmp(items[2].name, "Ask"));
    CHECK(items[4].kind == OC_EXTDB_QUERY && !strcmp(items[4].name, "Prices"));
    CHECK(oc_extdb_items(SHOP, NULL, 0, err, sizeof err) == 5);
    CHECK(oc_extdb_items(SHOP, items, 2, err, sizeof err) == 5);
}

static void test_plain(void)
{
    sqlite3 *db = NULL;
    oc_exttable t;
    oc_extdb_item items[4];
    char err[256];
    FILE *f;

    remove(PLAIN);
    CHECK(sqlite3_open(PLAIN, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE t(a INTEGER, b TEXT, c REAL, d TEXT, e BLOB);"
                           "INSERT INTO t VALUES(1, '0123', 2.5, '2026-11-16', x'00ff');"
                           "INSERT INTO t VALUES(NULL, 'x', NULL, '13:45', NULL);"
                           "CREATE VIEW v AS SELECT b FROM t;", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);

    CHECK(oc_extdb_query(NULL, PLAIN, "T", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 3 && t.cols == 5);
    CHECK_STR(cell(&t, 0, 0), "a");
    CHECK_STR(cell(&t, 1, 0), "#1");
    CHECK_STR(cell(&t, 1, 1), "0123");
    CHECK(kind(&t, 1, 1) == OC_EXTV_TEXT);
    CHECK_STR(cell(&t, 1, 2), "#2.5");
    CHECK(kind(&t, 1, 3) == OC_EXTV_INPUT && kind(&t, 2, 3) == OC_EXTV_INPUT);
    CHECK_STR(cell(&t, 1, 4), "");
    CHECK_STR(cell(&t, 2, 0), "");
    oc_exttable_free(&t);
    CHECK(oc_extdb_query(NULL, PLAIN, "v", 0, &t, err, sizeof err) == OWF_OK && t.rows == 2 && t.cols == 1);
    oc_exttable_free(&t);
    CHECK(oc_extdb_query(NULL, PLAIN, "WITH s AS (SELECT a FROM t) SELECT * FROM s;", 0, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 2);
    oc_exttable_free(&t);
    CHECK(oc_extdb_query(NULL, PLAIN, "SELECT a FROM t WHERE 1 = 0", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 1 && !strcmp(cell(&t, 0, 0), "a"));
    oc_exttable_free(&t);
    CHECK(oc_extdb_query(NULL, PLAIN, "SELECT a FROM t; DROP TABLE t", 0, &t, err, sizeof err) == OWF_ERR_FORMAT);
    CHECK(strstr(err, "one query") != NULL);
    CHECK(oc_extdb_query(NULL, PLAIN, "SELECT ?1", 0, &t, err, sizeof err) == OWF_ERR_FORMAT);
    /* a query that would run without end gives no rows to count: stopped by the work it does */
    CHECK(oc_extdb_query(NULL, PLAIN, "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n) "
                         "SELECT count(*) FROM n", 0, &t, err, sizeof err) == OWF_ERR_FORMAT);
    CHECK(strstr(err, "too much work") != NULL);
    CHECK(oc_extdb_query(NULL, PLAIN, "SELECT count(*) FROM t", 0, &t, err, sizeof err) == OWF_OK);
    oc_exttable_free(&t);
    CHECK(oc_extdb_query(NULL, PLAIN, "sqlite_schema", 0, &t, err, sizeof err) == OWF_ERR_FORMAT);
    CHECK(oc_extdb_query(NULL, PLAIN, "missing", 0, &t, err, sizeof err) == OWF_ERR_FORMAT && strstr(err, "missing"));
    CHECK(oc_extdb_items(PLAIN, items, 4, err, sizeof err) == 2);
    CHECK(items[0].kind == OC_EXTDB_TABLE && !strcmp(items[0].name, "t"));
    CHECK(items[1].kind == OC_EXTDB_VIEW && !strcmp(items[1].name, "v"));

    f = fopen(NOTDB, "wb");
    if (f) {
        fputs("this is not a database, only words, long enough to be read as a header by SQLite at all", f);
        fclose(f);
    }
    CHECK(oc_extdb_query(NULL, NOTDB, "t", 0, &t, err, sizeof err) == OWF_ERR_IO && err[0]);
    CHECK(oc_extdb_query(NULL, "build/host/calc-extdb-none.db", "t", 0, &t, err, sizeof err) == OWF_ERR_IO);
    CHECK(oc_extdb_items(NOTDB, items, 4, err, sizeof err) == -1);
    CHECK(oc_extdb_query(NULL, "", "t", 0, &t, err, sizeof err) == OWF_ERR_FORMAT);
    remove(NOTDB);
    remove(PLAIN);
}

/* Through GFOpenCalc's core: a range from the project's table, numbers as numbers. */
static void test_core(void)
{
    oc_core *core = oc_core_new(NULL);
    oc_extdata_spec spec;
    oc_extdata_io io;
    const owf_cell *c;
    char err[256];
    int index = -1;
    if (!core)
        return;
    memset(&io, 0, sizeof io);
    io.query = oc_extdb_query;
    oc_extdata_init(&spec);
    spec.kind = OC_EXTDATA_DATABASE;
    snprintf(spec.source, sizeof spec.source, "%s", SHOP);
    snprintf(spec.item, sizeof spec.item, "Prices");
    snprintf(spec.dest, sizeof spec.dest, "A1");
    CHECK(oc_core_extdata_add(core, &spec, &io, 0, &index, err, sizeof err) == OWF_OK && index == 0);
    c = owf_sheet_find(&oc_core_book(core)->sheets[0], 2, 1);
    CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 1234.5);
    c = owf_sheet_find(&oc_core_book(core)->sheets[0], 2, 0);
    CHECK(c && c->type == OWF_VALUE_TEXT && c->text && !strcmp(c->text, "=Gadget"));
    CHECK(oc_core_set_input(core, 0, 5, 0, "=SUM(B2:B3)") == OWF_OK);
    c = owf_sheet_find(&oc_core_book(core)->sheets[0], 5, 0);
    CHECK(c && c->type == OWF_VALUE_NUMBER && c->number == 1237.0);   /* 2.50 + 1,234.50 */
    snprintf(spec.item, sizeof spec.item, "Ask");
    CHECK(oc_core_extdata_change(core, 0, &spec, &io, 0, err, sizeof err) == OWF_ERR_FORMAT && strstr(err, "value"));
    oc_core_free(core);
}

int main(void)
{
    test_project();
    test_plain();
    test_core();
    remove(SHOP);
    if (failures) {
        fprintf(stderr, "calc external data (databases): %d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("calc external data (databases) tests passed (%d checks)\n", checks);
    return 0;
}
