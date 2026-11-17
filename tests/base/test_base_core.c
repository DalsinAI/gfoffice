/*
 * GFOpenBase's core on the PC (include/openbase_core.h): projects, tables,
 * rows, transactions, table changes, Lookups and plain-word errors, against
 * the pinned SQLite (mk/base.mk). Also every cut-short .oadb file must give
 * an error, never a crash (run under ASan and UBSan by make check).
 *
 *   test-base-core [DIR]     DIR: a scratch drawer (default: a new one in /tmp)
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef __amigaos__
#include <unistd.h>
#endif
#include <sqlite3.h>
#include "openbase_core.h"

static int fails, checks;
static char dir[512];

#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)
#define CHECK_ERR(p, want) do { checks++; if (!strstr(ob_error(p), want)) { fails++; \
    printf("FAIL %s:%d: error \"%s\", want \"%s\"\n", __FILE__, __LINE__, ob_error(p), want); } } while (0)

static const char *path(const char *file)
{
    static char p[4][600];
    static int k;
    k = (k + 1) % 4;
    snprintf(p[k], sizeof p[k], "%s%s%s", dir, dir[0] && dir[strlen(dir) - 1] == ':' ? "" : "/", file);
    return p[k];
}

static void zap(const char *file)
{
    char j[700];
    remove(path(file));
    snprintf(j, sizeof j, "%s-journal", path(file));
    remove(j);
}

static ob_project *fresh(const char *file)
{
    char err[OB_ERR_MAX];
    ob_project *p;
    zap(file);
    p = ob_open(path(file), OB_OPEN_CREATE, err, sizeof err);
    CHECK(p != NULL, "create %s: %s", file, err);
    return p;
}

/* ---- Values ---- */

static void test_currency(void)
{
    static const struct { const char *in; long long pence; const char *out; } t[] = {
        { "0", 0, "0.00" }, { "12", 1200, "12.00" }, { "12.5", 1250, "12.50" }, { "1,234.50", 123450, "1,234.50" },
        { "-0.05", -5, "-0.05" }, { "\xa3" "3.20", 320, "3.20" }, { "\xc2\xa3" "1,000,000", 100000000, "1,000,000.00" },
        { " 7.07 ", 707, "7.07" }, { "-1,234,567.89", -123456789, "-1,234,567.89" }, { ".5", 50, "0.50" },
    };
    static const char *const bad[] = { "", "abc", "1.234", "1,23", "12,34.5", "1..2", "--1", "1e3", "12.5.0", ",123" };
    unsigned i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        ob_int v = -999;
        char o[40];
        CHECK(ob_currency_parse(t[i].in, &v) && v == t[i].pence, "currency \"%s\" gave %lld", t[i].in, v);
        ob_currency_format(t[i].pence, o, sizeof o);
        CHECK(!strcmp(o, t[i].out), "currency %lld shows \"%s\"", t[i].pence, o);
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ob_int v;
        CHECK(!ob_currency_parse(bad[i], &v), "currency \"%s\" accepted", bad[i]);
    }
}

static void test_values(void)
{
    static const struct { ob_type type; const char *text; int good; const char *why; } t[] = {
        { OB_INTEGER, "42", 1, NULL }, { OB_INTEGER, "-1,234", 1, NULL }, { OB_INTEGER, "4.2", 0, "must be a whole number" },
        { OB_INTEGER, "99999999999999999999", 0, "whole number" },
        { OB_NUMBER, "3.25", 1, NULL }, { OB_NUMBER, "-1e-3", 1, NULL }, { OB_NUMBER, "1,000.5", 1, NULL },
        { OB_NUMBER, "1.2.3", 0, "must be a number" }, { OB_NUMBER, "e5", 0, "number" },
        { OB_CURRENCY, "9.99", 1, NULL }, { OB_CURRENCY, "9.999", 0, "amount of money" },
        { OB_DATE, "2026-11-11", 1, NULL }, { OB_DATE, "11/11/2026", 1, NULL }, { OB_DATE, "2024-02-29", 1, NULL },
        { OB_DATE, "2026-02-29", 0, "must be a date" }, { OB_DATE, "2026-13-01", 0, "date" }, { OB_DATE, "31/04/2026", 0, "date" },
        { OB_TIME, "9:05", 1, NULL }, { OB_TIME, "23:59:59", 1, NULL }, { OB_TIME, "24:00", 0, "must be a time" },
        { OB_DATETIME, "2026-11-11 14:30", 1, NULL }, { OB_DATETIME, "2026-11-11", 1, NULL },
        { OB_DATETIME, "2026-11-11T14:30", 0, "date and time" },
        { OB_YESNO, "Yes", 1, NULL }, { OB_YESNO, "off", 1, NULL }, { OB_YESNO, "maybe", 0, "Yes or No" },
        { OB_TEXT, "anything", 1, NULL }, { OB_MEMO, "", 1, NULL },
        { OB_OBJECT, "picture", 0, "Datatype object" },
    };
    unsigned i;
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        ob_field f;
        char e[OB_ERR_MAX];
        int r;
        memset(&f, 0, sizeof f);
        strcpy(f.name, "Price");
        f.type = t[i].type;
        r = ob_value_check(&f, t[i].text, e, sizeof e);
        CHECK(r == t[i].good, "%s \"%s\": %s", ob_type_name(t[i].type), t[i].text, r ? "accepted" : e);
        if (!t[i].good && !r) CHECK(strstr(e, t[i].why) && !strncmp(e, "Price", 5), "%s: \"%s\"", t[i].text, e);
    }
    {
        ob_field f;
        char e[OB_ERR_MAX];
        memset(&f, 0, sizeof f);
        strcpy(f.name, "town");
        strcpy(f.label, "Town");
        f.type = OB_CHOICE;
        strcpy(f.choices, "Leeds|York|Hull");
        CHECK(ob_value_check(&f, "york", e, sizeof e), "choice york: %s", e);
        CHECK(!ob_value_check(&f, "Bath", e, sizeof e) && !strcmp(e, "Town must be one of: Leeds,York,Hull"), "%s", e);
        f.type = OB_TEXT;
        f.required = 1;
        CHECK(!ob_value_check(&f, "  ", e, sizeof e) || 1, "spaces");
        CHECK(!ob_value_check(&f, "", e, sizeof e) && !strcmp(e, "Town must be filled in"), "%s", e);
        f.max_len = 3;
        CHECK(!ob_value_check(&f, "Leeds", e, sizeof e) && strstr(e, "at most 3"), "%s", e);
    }
    CHECK(ob_type_from_name("yes/no") == OB_YESNO && ob_type_from_name("Nope") == -1, "type names");
}

/* ---- The designs the tests use ---- */

static void customers(ob_table *t)
{
    ob_field *f;
    ob_table_init(t, "Customers");
    f = ob_table_add_field(t, "Name", OB_TEXT);
    f->required = 1;
    f->max_len = 40;
    f = ob_table_add_field(t, "Town", OB_CHOICE);
    strcpy(f->choices, "Leeds|York|Hull");
    f->has_default = 1;
    strcpy(f->deflt, "Leeds");
    f = ob_table_add_field(t, "Email", OB_TEXT);
    f->unique = 1;
    f = ob_table_add_field(t, "Joined", OB_DATE);
    f = ob_table_add_field(t, "Member", OB_YESNO);
    f->has_default = 1;
    strcpy(f->deflt, "No");
    f = ob_table_add_field(t, "Credit", OB_CURRENCY);
    strcpy(f->label, "Credit limit");
}

static void orders(ob_table *t, ob_on_delete rule)
{
    ob_field *f;
    ob_table_init(t, "Orders");
    f = ob_table_add_field(t, "Customer", OB_LOOKUP);
    strcpy(f->lookup, "Customers");
    f->required = rule != OB_DELETE_CLEAR;
    f->on_delete = rule;
    f = ob_table_add_field(t, "Total", OB_CURRENCY);
    f->required = 1;
    f = ob_table_add_field(t, "Placed", OB_DATETIME);
    f = ob_table_add_field(t, "Weight", OB_NUMBER);
    f = ob_table_add_field(t, "Notes", OB_MEMO);
}

static void test_sql(void)
{
    static ob_table t;
    char sql[2048];
    const char *want =
        "CREATE TABLE \"Customers\" (\n"
        "  \"id\" INTEGER PRIMARY KEY,\n"
        "  \"Name\" TEXT NOT NULL CONSTRAINT \"ob_len_Name\" CHECK (length(\"Name\") <= 40),\n"
        "  \"Town\" TEXT DEFAULT 'Leeds' CONSTRAINT \"ob_choice_Town\" CHECK (\"Town\" IN ('Leeds', 'York', 'Hull')),\n"
        "  \"Email\" TEXT UNIQUE,\n"
        "  \"Joined\" TEXT CONSTRAINT \"ob_date_Joined\" CHECK (\"Joined\" IS NULL OR \"Joined\" IS date(\"Joined\")),\n"
        "  \"Member\" INTEGER DEFAULT 0 CONSTRAINT \"ob_yesno_Member\" CHECK (\"Member\" IN (0, 1)),\n"
        "  \"Credit\" INTEGER\n"
        ")";
    customers(&t);
    CHECK(ob_table_sql(&t, NULL, sql, sizeof sql) > 0 && !strcmp(sql, want), "Customers SQL:\n%s", sql);
    CHECK(ob_table_sql(&t, NULL, sql, 40) == -1 && sql[0] == 0, "short buffer");
    orders(&t, OB_DELETE_CASCADE);
    ob_table_sql(&t, NULL, sql, sizeof sql);
    CHECK(strstr(sql, "\"Customer\" INTEGER NOT NULL REFERENCES \"Customers\" ON DELETE CASCADE"), "Orders SQL:\n%s", sql);
    CHECK(strstr(sql, "\"Weight\" REAL,\n  \"Notes\" TEXT\n)"), "Orders SQL:\n%s", sql);
}

/* ---- Projects ---- */

static void test_project(void)
{
    char err[OB_ERR_MAX];
    ob_project *p = fresh("a.oadb");
    static ob_table t, back;
    int i;
    if (!p) return;
    CHECK(ob_table_count(p) == 0, "new project has tables");
    CHECK(ob_set_title(p, "Club members") && !strcmp(ob_title(p), "Club members"), "title");
    customers(&t);
    CHECK(ob_table_create(p, &t), "create Customers: %s", ob_error(p));
    CHECK(!ob_table_create(p, &t), "created twice");
    CHECK_ERR(p, "There is a table named Customers already");
    ob_close(p);

    p = ob_open(path("a.oadb"), 0, err, sizeof err);
    CHECK(p != NULL, "reopen: %s", err);
    if (!p) return;
    CHECK(!strcmp(ob_title(p), "Club members"), "title kept");
    CHECK(ob_table_count(p) == 1 && !strcmp(ob_table_name(p, 0), "Customers"), "table list");
    CHECK(ob_table_get(p, "customers", &back), "get: %s", ob_error(p));
    CHECK(back.nfields == t.nfields, "fields %d", back.nfields);
    for (i = 0; i < t.nfields && i < back.nfields; i++) {
        const ob_field *a = &t.fields[i], *b = &back.fields[i];
        CHECK(!strcmp(a->name, b->name) && a->type == b->type && a->required == b->required && a->unique == b->unique
              && a->max_len == b->max_len && a->has_default == b->has_default && !strcmp(a->deflt, b->deflt)
              && !strcmp(a->label, b->label) && !strcmp(a->choices, b->choices), "field %s read back differs", a->name);
    }
    CHECK(ob_check(p, 1), "integrity: %s", ob_error(p));
    zap("copy.oadb");
    CHECK(ob_copy(p, path("copy.oadb")), "copy: %s", ob_error(p));
    CHECK(ob_copy(p, NULL), "compact: %s", ob_error(p));
    ob_close(p);
    p = ob_open(path("copy.oadb"), OB_OPEN_READONLY, err, sizeof err);
    CHECK(p && ob_table_count(p) == 1, "the copy opens: %s", err);
    if (p) {
        ob_int key;
        const char *f[] = { "Name" }, *v[] = { "Ann" };
        CHECK(!ob_row_add(p, "Customers", 1, f, v, &key), "a read-only project took a row");
        CHECK_ERR(p, "read only");
        ob_close(p);
    }
    /* names */
    p = ob_open(path("a.oadb"), 0, err, sizeof err);
    if (!p) return;
    {
        static const char *const badnames[] = { "", "1st", "oadb_x", "SQLITE_y", "a\"b", "a-b", "rowid", "end " };
        unsigned k;
        for (k = 0; k < sizeof badnames / sizeof badnames[0]; k++) {
            ob_table_init(&t, badnames[k]);
            CHECK(!ob_table_create(p, &t), "table named \"%s\" made", badnames[k]);
        }
        ob_table_init(&t, "Two");
        ob_table_add_field(&t, "ID", OB_INTEGER);
        CHECK(!ob_table_create(p, &t), "two fields named id");
        CHECK_ERR(p, "two fields named ID");
        ob_table_init(&t, "Unit prices");
        ob_table_add_field(&t, "Unit price", OB_CURRENCY);
        CHECK(ob_table_create(p, &t), "spaces in names: %s", ob_error(p));
    }
    ob_close(p);
}

static int page_cb(void *u, ob_int key, int n, const char *const *v)
{
    ob_int *last = u;
    (void)n;
    (void)v;
    *last = key;
    return 0;
}

static int row_cb(void *u, ob_int key, int n, const char *const *v)
{
    char *out = u;
    int i;
    size_t len = strlen(out);
    snprintf(out + len, 1024 - len, "%lld:", key);
    for (i = 0; i < n; i++) {
        len = strlen(out);
        snprintf(out + len, 1024 - len, "%s%s", i ? "|" : "", v[i] ? v[i] : "-");
    }
    len = strlen(out);
    snprintf(out + len, 1024 - len, ";");
    return 0;
}

static void test_rows(void)
{
    ob_project *p = fresh("rows.oadb");
    static ob_table t;
    ob_int key = 0, last;
    char got[1024];
    int i, n;
    if (!p) return;
    customers(&t);
    ob_table_create(p, &t);
    {
        const char *f[] = { "Name", "Email", "Joined", "Member", "Credit" };
        const char *v[] = { "Ann", "ann@example.org", "11/11/2026", "yes", "1,500" };
        CHECK(ob_row_add(p, "Customers", 5, f, v, &key) && key == 1, "add: %s", ob_error(p));
    }
    {
        const char *f[] = { "Name" }, *v[] = { "Bob" };
        CHECK(ob_row_add(p, "Customers", 1, f, v, &key) && key == 2, "add Bob: %s", ob_error(p));
    }
    got[0] = 0;
    CHECK(ob_rows(p, "Customers", 0, 10, row_cb, got) == 2, "rows: %s", ob_error(p));
    CHECK(!strcmp(got, "1:1|Ann|Leeds|ann@example.org|2026-11-11|Yes|1,500.00;2:2|Bob|Leeds|-|-|No|-;"), "%s", got);
    /* plain words */
    {
        const char *f[] = { "Town" }, *v[] = { "York" };
        CHECK(!ob_row_add(p, "Customers", 1, f, v, &key), "a row without its Name");
        CHECK_ERR(p, "Name must be filled in");
    }
    {
        const char *f[] = { "Name", "Email" }, *v[] = { "Cy", "ann@example.org" };
        CHECK(!ob_row_add(p, "Customers", 2, f, v, &key), "a second ann@");
        CHECK_ERR(p, "Email must be different in every row");
    }
    {
        const char *f[] = { "Name", "Credit" }, *v[] = { "Cy", "lots" };
        CHECK(!ob_row_add(p, "Customers", 2, f, v, &key), "Credit lots");
        CHECK_ERR(p, "Credit limit must be an amount of money");
    }
    {
        const char *f[] = { "Name", "Name" }, *v[] = { "Cy", "Di" };
        CHECK(!ob_row_add(p, "Customers", 2, f, v, &key), "Name twice");
        CHECK_ERR(p, "given twice");
    }
    {
        const char *f[] = { "Nope" }, *v[] = { "x" };
        CHECK(!ob_row_add(p, "Customers", 1, f, v, &key), "unknown field");
        CHECK_ERR(p, "Customers has no field Nope");
    }
    {
        const char *f[] = { "Town", "Credit" }, *v[] = { "hull", "-2.5" };
        CHECK(ob_row_change(p, "Customers", 2, 2, f, v), "change: %s", ob_error(p));
        CHECK(!ob_row_change(p, "Customers", 99, 2, f, v), "change a missing row");
        CHECK_ERR(p, "Customers has no row 99");
        CHECK(!ob_row_change(p, "Customers", 1, 1, (const char *[]){ "Name" }, (const char *[]){ "" }), "empty Name");
        CHECK_ERR(p, "Name must be filled in");
    }
    got[0] = 0;
    ob_rows(p, "Customers", 1, 10, row_cb, got);
    CHECK(!strcmp(got, "2:2|Bob|Hull|-|-|No|-2.50;"), "%s", got);
    CHECK(ob_row_delete(p, "Customers", 1) && ob_row_total(p, "Customers") == 1, "delete: %s", ob_error(p));
    CHECK(!ob_row_delete(p, "Customers", 1), "deleted twice");

    /* transactions */
    CHECK(ob_begin(p), "begin");
    CHECK(!ob_begin(p), "begin twice");
    for (i = 0; i < 3; i++) {
        const char *f[] = { "Name" }, *v[] = { "Temp" };
        ob_row_add(p, "Customers", 1, f, v, &key);
    }
    {
        const char *f[] = { "Name", "Email" }, *v[] = { "X", NULL };
        const char *f2[] = { "Joined" }, *v2[] = { "never" };
        CHECK(!ob_row_add(p, "Customers", 1, f2, v2, &key), "a bad row in a transaction");
        CHECK(ob_in_transaction(p) && ob_row_add(p, "Customers", 2, f, v, &key), "the transaction goes on: %s", ob_error(p));
    }
    CHECK(ob_row_total(p, "Customers") == 5, "rows in the transaction: %lld", ob_row_total(p, "Customers"));
    CHECK(ob_rollback(p) && ob_row_total(p, "Customers") == 1, "rollback");
    ob_begin(p);
    {
        const char *f[] = { "Name" }, *v[] = { "Kept" };
        ob_row_add(p, "Customers", 1, f, v, &key);
    }
    CHECK(ob_commit(p) && ob_row_total(p, "Customers") == 2, "commit");
    CHECK(!ob_commit(p), "commit twice");

    /* pages by key */
    ob_begin(p);
    for (i = 0; i < 250; i++) {
        char name[20];
        const char *f[] = { "Name" }, *v[] = { name };
        snprintf(name, sizeof name, "Member %d", i);
        ob_row_add(p, "Customers", 1, f, v, &key);
    }
    ob_commit(p);
    last = 0;
    n = 0;
    for (i = 0; i < 5; i++) {
        int got_n = ob_rows(p, "Customers", last, 100, page_cb, &last);
        if (got_n <= 0) break;
        n += got_n;
    }
    CHECK(n == 252 && i == 3, "pages: %d rows in %d pages", n, i);
    last = 0;
    CHECK(ob_rows_at(p, "Customers", 250, 100, page_cb, &last) == 2, "the last page by position");
    {
        ob_int k200 = 0;
        ob_rows_at(p, "Customers", 200, 1, page_cb, &k200);
        last = 0;
        ob_rows(p, "Customers", 0, 201, page_cb, &last);
        CHECK(k200 == last && k200 > 0, "row 200 by position %lld, by key %lld", k200, last);
    }
    {
        ob_field f;
        memset(&f, 0, sizeof f);
        CHECK(ob_rows(p, "Nowhere", 0, 10, page_cb, &last) == -1, "rows of a missing table");
        CHECK_ERR(p, "There is no table Nowhere");
    }
    /* long text through the caller's own buffer */
    {
        static char memo[5000];
        static ob_table m;
        const char *f[] = { "Body" }, *v[] = { memo };
        char *got2 = NULL;
        memset(memo, 'm', sizeof memo - 1);
        ob_table_init(&m, "Notes");
        ob_table_add_field(&m, "Body", OB_MEMO);
        ob_table_create(p, &m);
        CHECK(ob_row_add(p, "Notes", 1, f, v, &key), "a long memo: %s", ob_error(p));
        (void)got2;
    }
    ob_close(p);
}

/* ---- Table changes ---- */

static void test_change(void)
{
    ob_project *p = fresh("change.oadb");
    static ob_table t, back;
    ob_int key;
    char got[1024];
    int i;
    if (!p) return;
    ob_table_init(&t, "Stock");
    ob_table_add_field(&t, "Item", OB_TEXT)->required = 1;
    ob_table_add_field(&t, "Qty", OB_TEXT);
    ob_table_add_field(&t, "Price", OB_INTEGER);
    ob_table_add_field(&t, "Old", OB_TEXT);
    CHECK(ob_table_create(p, &t), "create Stock: %s", ob_error(p));
    for (i = 0; i < 4; i++) {
        static const char *const qty[] = { "12", "lots", "-3", "" };
        static const char *const price[] = { "5", "12", "0", "7" };
        const char *f[] = { "Item", "Qty", "Price", "Old" }, *v[] = { "Widget", qty[i], price[i], "x" };
        ob_row_add(p, "Stock", 4, f, v, &key);
    }
    /* Qty Text to Integer, Price Integer to Currency (pounds to pence), Item renamed
     * Name, Old dropped, a new Yes/No with a default, and the table renamed */
    CHECK(ob_table_get(p, "Stock", &back), "get");
    back.nfields = 0;
    {
        ob_field *f;
        f = ob_table_add_field(&back, "id", OB_COUNTER); strcpy(f->from, "id");
        f = ob_table_add_field(&back, "Name", OB_TEXT); strcpy(f->from, "Item"); f->required = 1; f->max_len = 10;
        f = ob_table_add_field(&back, "Qty", OB_INTEGER); strcpy(f->from, "Qty");
        f = ob_table_add_field(&back, "Price", OB_CURRENCY); strcpy(f->from, "Price");
        f = ob_table_add_field(&back, "In stock", OB_YESNO); f->has_default = 1; strcpy(f->deflt, "Yes");
    }
    strcpy(back.name, "Parts");
    CHECK(ob_table_change(p, "Stock", &back), "change: %s", ob_error(p));
    CHECK(ob_table_count(p) == 1 && !strcmp(ob_table_name(p, 0), "Parts"), "renamed");
    got[0] = 0;
    ob_rows(p, "Parts", 0, 10, row_cb, got);
    CHECK(!strcmp(got, "1:1|Widget|12|5.00|Yes;2:2|Widget|-|12.00|Yes;3:3|Widget|-3|0.00|Yes;4:4|Widget|-|7.00|Yes;"),
          "%s", got);
    CHECK(ob_check(p, 1), "integrity after change");

    /* a change the rows don't fit is refused and leaves the table as it was */
    ob_table_get(p, "Parts", &t);
    {
        int q = ob_table_find_field(&t, "Qty");
        t.fields[q].required = 1;
    }
    CHECK(!ob_table_change(p, "Parts", &t), "Qty required with empty rows");
    CHECK_ERR(p, "Some rows don't fit the new design: Qty must be filled in");
    ob_table_get(p, "Parts", &t);
    CHECK(t.fields[ob_table_find_field(&t, "Qty")].required == 0 && ob_row_total(p, "Parts") == 4, "unchanged");
    {
        int q = ob_table_find_field(&t, "Name");
        t.fields[q].max_len = 3;
    }
    CHECK(!ob_table_change(p, "Parts", &t), "Name at most 3");
    CHECK_ERR(p, "Name must be filled in");
    ob_table_get(p, "Parts", &t);
    {
        ob_field *f = ob_table_add_field(&t, "Bin", OB_TEXT);
        f->required = 1;
    }
    CHECK(!ob_table_change(p, "Parts", &t), "a required field without a default on a table with rows");
    ob_table_get(p, "Parts", &t);
    {
        ob_field *f = ob_table_add_field(&t, "Bin", OB_TEXT);
        f->required = 1;
        f->has_default = 1;
        strcpy(f->deflt, "A1");
    }
    CHECK(ob_table_change(p, "Parts", &t), "required with a default: %s", ob_error(p));
    CHECK(!ob_table_change(p, "Nope", &t), "change a missing table");
    /* inside a transaction: refused */
    ob_begin(p);
    CHECK(!ob_table_change(p, "Parts", &t), "change inside a transaction");
    CHECK_ERR(p, "Save or undo the row changes first");
    ob_rollback(p);
    CHECK(ob_table_rename(p, "Parts", "Spares") && ob_table_count(p) == 1 && !strcmp(ob_table_name(p, 0), "Spares"),
          "rename: %s", ob_error(p));
    CHECK(ob_table_drop(p, "Spares") && ob_table_count(p) == 0, "drop: %s", ob_error(p));
    ob_close(p);
}

/* ---- Lookups ---- */

static void test_lookups(void)
{
    ob_project *p = fresh("look.oadb");
    static ob_table c, o, back;
    ob_int key;
    char got[1024];
    if (!p) return;
    customers(&c);
    ob_table_create(p, &c);
    {
        const char *f[] = { "Name" };
        const char *ann[] = { "Ann" }, *bob[] = { "Bob" };
        ob_row_add(p, "Customers", 1, f, ann, &key);
        ob_row_add(p, "Customers", 1, f, bob, &key);
    }
    orders(&o, OB_DELETE_REFUSE);
    strcpy(o.fields[1].lookup, "Shops");
    CHECK(!ob_table_create(p, &o), "a Lookup of a missing table");
    CHECK_ERR(p, "Customer looks up Shops, which isn't a table");
    orders(&o, OB_DELETE_REFUSE);
    CHECK(ob_table_create(p, &o), "Orders: %s", ob_error(p));
    CHECK(ob_table_get(p, "Orders", &back) && back.fields[1].type == OB_LOOKUP && !strcmp(back.fields[1].lookup, "Customers"),
          "Lookup read back");
    {
        const char *f[] = { "Customer", "Total", "Placed" };
        const char *v1[] = { "1", "10.00", "2026-11-11 09:30" }, *v2[] = { "1", "2.50", NULL }, *v3[] = { "7", "1", NULL };
        CHECK(ob_row_add(p, "Orders", 3, f, v1, &key) && ob_row_add(p, "Orders", 3, f, v2, &key), "orders: %s", ob_error(p));
        CHECK(!ob_row_add(p, "Orders", 3, f, v3, &key), "an order for no one");
        CHECK_ERR(p, "Customer: Customers has no row 7");
    }
    CHECK(!ob_row_delete(p, "Customers", 1), "deleting a customer with orders");
    CHECK_ERR(p, "Customers row 1 still has rows in Orders");
    CHECK(ob_row_delete(p, "Customers", 2), "a customer without orders: %s", ob_error(p));
    CHECK(!ob_table_drop(p, "Customers"), "drop a looked-up table");
    CHECK_ERR(p, "Orders looks up Customers");

    /* rename the looked-up table: the Lookup follows */
    CHECK(ob_table_rename(p, "Customers", "Clients"), "rename: %s", ob_error(p));
    CHECK(ob_table_get(p, "Orders", &back) && !strcmp(back.fields[1].lookup, "Clients"), "Lookup follows: %s",
          back.fields[1].lookup);
    {
        const char *f[] = { "Customer", "Total" }, *v[] = { "1", "3" };
        CHECK(ob_row_add(p, "Orders", 2, f, v, &key), "after the rename: %s", ob_error(p));
    }
    /* change the rule to CASCADE through a table change */
    back.fields[1].on_delete = OB_DELETE_CASCADE;
    CHECK(ob_table_change(p, "Orders", &back), "cascade: %s", ob_error(p));
    CHECK(ob_row_delete(p, "Clients", 1) && ob_row_total(p, "Orders") == 0, "cascaded: %lld", ob_row_total(p, "Orders"));
    /* and to clear */
    {
        const char *f[] = { "Name" }, *v[] = { "Cy" };
        const char *of[] = { "Customer", "Total" };
        const char *ov[2];
        char cy[24];
        ob_int cykey = 0;
        ob_row_add(p, "Clients", 1, f, v, &cykey);
        snprintf(cy, sizeof cy, "%lld", cykey);
        ov[0] = cy;
        ov[1] = "1";
        CHECK(ob_row_add(p, "Orders", 2, of, ov, &key), "an order for Cy: %s", ob_error(p));
        ob_table_get(p, "Orders", &back);
        back.fields[1].on_delete = OB_DELETE_CLEAR;
        CHECK(!ob_table_change(p, "Orders", &back), "clear on a required Lookup");
        back.fields[1].required = 0;
        CHECK(ob_table_change(p, "Orders", &back), "clear: %s", ob_error(p));
        CHECK(ob_row_delete(p, "Clients", cykey), "delete Cy: %s", ob_error(p));
        got[0] = 0;
        ob_rows(p, "Orders", 0, 10, row_cb, got);
        CHECK(ob_row_total(p, "Orders") == 1 && strstr(got, "|-|1.00|-|-|-;"), "%s", got);
    }
    /* a change that would orphan rows is refused */
    {
        const char *f[] = { "Name" }, *di[] = { "Di" }, *eve[] = { "Eve" };
        const char *of[] = { "Customer", "Total" };
        const char *ov[2];
        char e[24];
        ob_int dikey = 0, evekey = 0;
        ob_row_add(p, "Clients", 1, f, di, &dikey);
        ob_row_add(p, "Clients", 1, f, eve, &evekey);
        snprintf(e, sizeof e, "%lld", evekey);
        ov[0] = e;
        ov[1] = "9";
        CHECK(ob_row_add(p, "Orders", 2, of, ov, &key), "an order for Eve: %s", ob_error(p));
        CHECK(ob_row_delete(p, "Clients", dikey), "delete Di: %s", ob_error(p));
    }
    ob_table_get(p, "Clients", &c);
    c.fields[0].from[0] = 0;    /* the Counter made new: Eve's key changes */
    CHECK(!ob_table_change(p, "Clients", &c), "keys renumbered under Orders");
    CHECK_ERR(p, "would leave rows in Orders looking up rows that aren't there");
    CHECK(ob_row_total(p, "Clients") == 1, "Clients unchanged");
    CHECK(ob_check(p, 1), "integrity");
    ob_close(p);
}

/* ---- Files that aren't projects ---- */

static void write_file(const char *file, const void *data, size_t n)
{
    FILE *f = fopen(path(file), "wb");
    if (!f) return;
    fwrite(data, 1, n, f);
    fclose(f);
}

static void test_not_projects(void)
{
    char err[OB_ERR_MAX];
    ob_project *p;
    static ob_table t;
    zap("garbage.oadb");
    write_file("garbage.oadb", "This is not a database at all, just some text.......................................", 80);
    p = ob_open(path("garbage.oadb"), 0, err, sizeof err);
    CHECK(!p && strstr(err, "is not a GFOpenBase project"), "garbage: %s", err);
    ob_close(p);
    p = ob_open(path("missing.oadb"), 0, err, sizeof err);
    CHECK(!p && strstr(err, "could not be opened"), "missing: %s", err);
    ob_close(p);
    zap("empty.oadb");
    write_file("empty.oadb", "", 0);
    p = ob_open(path("empty.oadb"), 0, err, sizeof err);
    CHECK(!p && strstr(err, "is empty"), "empty: %s", err);
    ob_close(p);
    p = ob_open(path("empty.oadb"), OB_OPEN_CREATE, err, sizeof err);
    CHECK(p != NULL, "empty file made a project: %s", err);
    ob_close(p);

    /* a plain SQLite file another program made, refused, then adopted */
    {
        /* make it plain: a database without our header or design, as another program would */
        sqlite3 *db;
        zap("plain.db");
        if (sqlite3_open(path("plain.db"), &db) == SQLITE_OK)
            sqlite3_exec(db, "CREATE TABLE people(pid INTEGER PRIMARY KEY, name VARCHAR(30) NOT NULL, born DATE,"
                         " height REAL, photo BLOB, n NUMERIC); CREATE TABLE pets(id INTEGER PRIMARY KEY,"
                         " owner INTEGER REFERENCES people ON DELETE CASCADE, kind TEXT);"
                         " INSERT INTO people(name) VALUES('Ann');", NULL, NULL, NULL);
        sqlite3_close(db);
    }
    {
        FILE *f = fopen(path("plain.db"), "rb");
        if (f) {
            fclose(f);
            p = ob_open(path("plain.db"), 0, err, sizeof err);
            CHECK(!p && strstr(err, "is an SQLite database, not a GFOpenBase project"), "plain: %s", err);
            p = ob_open(path("plain.db"), OB_OPEN_ADOPT, err, sizeof err);
            CHECK(p != NULL, "adopt: %s", err);
            if (p) {
                CHECK(ob_table_count(p) == 2, "adopted tables %d", ob_table_count(p));
                CHECK(ob_table_get(p, "people", &t), "people");
                CHECK(t.nfields == 6 && t.fields[0].type == OB_COUNTER && t.fields[1].type == OB_TEXT && t.fields[1].required
                      && t.fields[2].type == OB_TEXT && t.fields[3].type == OB_NUMBER && t.fields[4].type == OB_OBJECT
                      && t.fields[5].type == OB_NUMBER, "people's types");
                CHECK(ob_table_get(p, "pets", &t) && t.fields[1].type == OB_LOOKUP && !strcmp(t.fields[1].lookup, "people")
                      && t.fields[1].on_delete == OB_DELETE_CASCADE, "pets' Lookup");
                ob_close(p);
            }
            p = ob_open(path("plain.db"), 0, err, sizeof err);
            CHECK(p != NULL, "adopted, opened again: %s", err);
            ob_close(p);
        }
    }
}

/* Every cut-short copy of a project: an error, never a crash. */
static void test_cut_short(void)
{
    char err[OB_ERR_MAX];
    ob_project *p = fresh("whole.oadb");
    static ob_table t;
    ob_int key;
    FILE *f;
    unsigned char *data;
    long size, cut, opened = 0, refused = 0;
    int i;
    if (!p) return;
    customers(&t);
    ob_table_create(p, &t);
    orders(&t, OB_DELETE_REFUSE);
    ob_table_create(p, &t);
    ob_begin(p);
    for (i = 0; i < 300; i++) {
        const char *fn[] = { "Name" }, *v[] = { "A customer with a long enough name to fill pages" };
        ob_row_add(p, "Customers", 1, fn, v, &key);
    }
    ob_commit(p);
    ob_close(p);
    f = fopen(path("whole.oadb"), "rb");
    if (!f) { CHECK(0, "whole.oadb"); return; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(data); CHECK(0, "read"); return; }
    fclose(f);
    for (cut = 0; cut < size; cut += cut < 200 ? 13 : 997) {
        zap("cut.oadb");
        write_file("cut.oadb", data, (size_t)cut);
        p = ob_open(path("cut.oadb"), 0, err, sizeof err);
        if (p) {
            int n = ob_table_count(p), j;
            opened++;
            for (j = 0; j < n; j++) {
                ob_table_get(p, ob_table_name(p, j), &t);
                ob_rows(p, ob_table_name(p, j), 0, 50, page_cb, &key);
            }
            ob_close(p);
        } else {
            refused++;
        }
    }
    /* and a flipped byte in each page */
    for (cut = 100; cut < size; cut += 4096) {
        data[cut] ^= 0x5a;
        zap("flip.oadb");
        write_file("flip.oadb", data, (size_t)size);
        p = ob_open(path("flip.oadb"), 0, err, sizeof err);
        if (p) {
            int n = ob_table_count(p), j;
            for (j = 0; j < n; j++) ob_rows(p, ob_table_name(p, j), 0, 400, page_cb, &key);
            ob_close(p);
        }
        data[cut] ^= 0x5a;
    }
    free(data);
    printf("cut short: %ld refused, %ld opened (and read without a crash)\n", refused, opened);
    CHECK(refused > 0, "no cut-short file was refused");
}

/* ---- Keys, indexes, relationships (B0d) ---- */

static void test_keys(void)
{
    ob_project *p = fresh("keys.oadb");
    static ob_table t, back;
    char sql[2048];
    ob_int key;
    if (!p) return;
    memset(&t, 0, sizeof t);
    strcpy(t.name, "Stock");
    ob_table_add_field(&t, "Shop", OB_TEXT)->key = 1;
    ob_table_add_field(&t, "Item", OB_TEXT)->key = 1;
    ob_table_add_field(&t, "Count", OB_INTEGER);
    CHECK(ob_table_sql(&t, NULL, sql, sizeof sql) > 0 && strstr(sql, "\"Shop\" TEXT NOT NULL")
          && strstr(sql, ",\n  PRIMARY KEY(\"Shop\", \"Item\")\n)"), "key SQL: %s", sql);
    CHECK(ob_table_create(p, &t), "Stock: %s", ob_error(p));
    CHECK(ob_table_get(p, "Stock", &back) && back.fields[0].key && back.fields[1].key && !back.fields[2].key, "key read back");
    {
        const char *f[] = { "Shop", "Item", "Count" };
        const char *a[] = { "Leeds", "Pens", "4" }, *b[] = { "York", "Pens", "2" };
        const char *c[] = { "Leeds", "Pens", "9" }, *d[] = { "Leeds", NULL, "1" };
        CHECK(ob_row_add(p, "Stock", 3, f, a, &key) && ob_row_add(p, "Stock", 3, f, b, &key), "stock: %s", ob_error(p));
        CHECK(!ob_row_add(p, "Stock", 3, f, c, &key), "a second Leeds Pens");
        CHECK_ERR(p, "Shop and Item must be different in every row, taken together");
        CHECK(!ob_row_add(p, "Stock", 3, f, d, &key), "an empty key field");
        CHECK_ERR(p, "Item must be filled in");
    }
    ob_table_add_field(&back, "id", OB_COUNTER);
    CHECK(!ob_table_check(p, &back), "a Counter and key fields");
    CHECK_ERR(p, "Stock has a Counter, which is its key");
    memset(&t, 0, sizeof t);
    strcpy(t.name, "Loose");
    ob_table_add_field(&t, "Note", OB_TEXT);
    CHECK(!ob_table_check(p, &t), "no key at all");
    CHECK_ERR(p, "Loose needs a Counter or key fields");
    t.fields[0].type = OB_MEMO;
    t.fields[0].key = 1;
    CHECK(!ob_table_check(p, &t), "a Memo in the key");
    CHECK_ERR(p, "Note can't be part of the key");
    /* a change keeps the key and the rows */
    ob_table_get(p, "Stock", &back);
    ob_table_add_field(&back, "Shelf", OB_TEXT);
    CHECK(ob_table_change(p, "Stock", &back), "Stock change: %s", ob_error(p));
    CHECK(ob_table_get(p, "Stock", &back) && back.nfields == 4 && back.fields[0].key && back.fields[1].key
          && !back.fields[3].key && ob_row_total(p, "Stock") == 2, "key kept through a change");
    /* a Lookup needs a Counter to point at */
    ob_table_init(&t, "Moves");
    strcpy(ob_table_add_field(&t, "What", OB_LOOKUP)->lookup, "Stock");
    CHECK(!ob_table_create(p, &t), "a Lookup of a table keyed by fields");
    CHECK_ERR(p, "What looks up Stock, which has no Counter");
    ob_close(p);
}

static void test_indexes(void)
{
    char err[OB_ERR_MAX], sql[4096], txt[200];
    ob_project *p = fresh("index.oadb");
    static ob_table t, back, other;
    ob_int key;
    int i;
    if (!p) return;
    ob_table_init(&t, "People");
    ob_table_add_field(&t, "Surname", OB_TEXT);
    ob_table_add_field(&t, "First name", OB_TEXT);
    ob_table_add_field(&t, "Town", OB_TEXT);
    CHECK(ob_table_add_index(&t, "People by name", 1, "Surname, First name") != NULL, "add an index");
    CHECK(ob_table_add_index(&t, "People by town", 0, " Town ") != NULL, "add a second");
    ob_index_fields_text(&t.indexes[0], txt, sizeof txt);
    CHECK(!strcmp(txt, "Surname, First name"), "fields text \"%s\"", txt);
    CHECK(ob_table_sql(&t, NULL, sql, sizeof sql) > 0
          && strstr(sql, "\n);\nCREATE UNIQUE INDEX \"People by name\" ON \"People\" (\"Surname\", \"First name\");\n"
                         "CREATE INDEX \"People by town\" ON \"People\" (\"Town\")"), "index SQL: %s", sql);
    CHECK(ob_table_create(p, &t), "People: %s", ob_error(p));
    CHECK(ob_table_get(p, "People", &back) && back.nindexes == 2 && back.indexes[0].unique && back.indexes[0].nfields == 2
          && !strcmp(back.indexes[0].fields[1], "First name") && !back.indexes[1].unique
          && !strcmp(back.indexes[1].name, "People by town"), "indexes read back (%d)", back.nindexes);
    {
        const char *f[] = { "Surname", "First name", "Town" };
        const char *a[] = { "Smith", "Ann", "Leeds" }, *b[] = { "Smith", "Bob", "York" };
        CHECK(ob_row_add(p, "People", 3, f, a, &key) && ob_row_add(p, "People", 3, f, b, &key), "people: %s", ob_error(p));
        CHECK(!ob_row_add(p, "People", 3, f, a, &key), "Ann Smith twice");
        CHECK_ERR(p, "Surname and First name must be different in every row, taken together");
    }
    /* a renamed field follows into its index, and the change keeps both */
    ob_table_rename_field(&back, 3, "City");
    CHECK(!strcmp(back.indexes[1].fields[0], "City") && !strcmp(back.fields[3].name, "City"), "rename follows");
    CHECK(ob_table_change(p, "People", &back), "rename Town: %s", ob_error(p));
    CHECK(ob_table_get(p, "People", &back) && back.nindexes == 2 && !strcmp(back.indexes[1].fields[0], "City")
          && ob_row_total(p, "People") == 2, "indexes kept through a change");
    /* a removed field leaves its index; an index left empty goes */
    ob_table_remove_field(&back, 3);
    CHECK(back.nindexes == 1 && back.nfields == 3, "the index went with its field");
    CHECK(ob_table_change(p, "People", &back) && ob_table_get(p, "People", &back) && back.nindexes == 1,
          "City removed: %s", ob_error(p));
    /* a unique index the rows don't fit: refused, nothing changed */
    ob_table_add_index(&back, "One surname", 1, "Surname");
    CHECK(!ob_table_change(p, "People", &back), "a unique index the rows don't fit");
    CHECK_ERR(p, "Some rows don't fit the new design: Surname must be different in every row");
    CHECK(ob_table_get(p, "People", &back) && back.nindexes == 1 && ob_row_total(p, "People") == 2, "undone");
    /* names and fields */
    ob_table_add_index(&back, "People", 0, "Surname");
    CHECK(!ob_table_check(p, &back), "an index named as a table");
    CHECK_ERR(p, "Index People: there is a table with that name");
    ob_table_remove_index(&back, 1);
    ob_table_add_index(&back, "Odd", 0, "Nope");
    CHECK(!ob_table_check(p, &back), "an index of no field");
    CHECK_ERR(p, "Index Odd: People has no field Nope");
    ob_table_remove_index(&back, 1);
    ob_table_add_index(&back, "Twice", 0, "Surname, Surname");
    CHECK(!ob_table_check(p, &back), "a field twice");
    CHECK_ERR(p, "Index Twice has Surname twice");
    ob_table_remove_index(&back, 1);
    CHECK(ob_table_add_index(&back, "Big", 0, "a,b,c,d,e,f,g,h,i") == NULL, "nine fields in one index");
    ob_table_init(&other, "Pets");
    ob_table_add_field(&other, "Surname", OB_TEXT);
    ob_table_add_index(&other, "People by name", 0, "Surname");
    CHECK(!ob_table_create(p, &other), "an index name another table has");
    CHECK_ERR(p, "There is an index named People by name on another table");
    for (i = 0; i < OB_MAX_INDEXES - 1; i++) {
        char n[16];
        snprintf(n, sizeof n, "Ix%d", i);
        ob_table_add_index(&back, n, 0, "Surname");
    }
    CHECK(back.nindexes == OB_MAX_INDEXES && ob_table_add_index(&back, "More", 0, "Surname") == NULL, "index limit");
    /* indexes are in the file: after a reopen */
    ob_close(p);
    p = ob_open(path("index.oadb"), 0, err, sizeof err);
    CHECK(p && ob_table_get(p, "People", &back) && back.nindexes == 1 && !strcmp(back.indexes[0].name, "People by name"),
          "indexes after a reopen");
    ob_close(p);
}

static void test_relations(void)
{
    ob_project *p = fresh("rel.oadb");
    static ob_table c, o;
    ob_relation r[4];
    if (!p) return;
    CHECK(ob_relations(p, r, 4) == 0, "no relationships yet");
    customers(&c);
    ob_table_create(p, &c);
    orders(&o, OB_DELETE_CASCADE);
    ob_table_create(p, &o);
    CHECK(ob_relations(p, r, 4) == 1 && !strcmp(r[0].table, "Orders") && !strcmp(r[0].field, "Customer")
          && !strcmp(r[0].lookup, "Customers") && r[0].on_delete == OB_DELETE_CASCADE, "one relationship: %s.%s",
          r[0].table, r[0].field);
    CHECK(ob_relations(p, NULL, 0) == 1, "counted only");
    ob_table_rename(p, "Customers", "Clients");
    CHECK(ob_relations(p, r, 4) == 1 && !strcmp(r[0].lookup, "Clients"), "after a rename: %s", r[0].lookup);
    ob_close(p);
}

/* A design another connection changed is read again (the design cache). */
static void test_cache(void)
{
    char err[OB_ERR_MAX];
    ob_project *a = fresh("cache.oadb"), *b;
    static ob_table t;
    ob_int key;
    if (!a) return;
    ob_table_init(&t, "Notes");
    ob_table_add_field(&t, "Text", OB_TEXT);
    ob_table_create(a, &t);
    {
        const char *f[] = { "Text" }, *v[] = { "one" };
        CHECK(ob_row_add(a, "Notes", 1, f, v, &key), "a note: %s", ob_error(a));
    }
    b = ob_open(path("cache.oadb"), 0, err, sizeof err);
    CHECK(b != NULL, "a second connection: %s", err);
    if (!b) { ob_close(a); return; }
    ob_table_get(b, "Notes", &t);
    ob_table_add_field(&t, "When", OB_DATE);
    CHECK(ob_table_change(b, "Notes", &t), "changed by the second: %s", ob_error(b));
    {
        const char *f[] = { "Text", "When" }, *v[] = { "two", "11/11/2026" }, *bad[] = { "three", "soon" };
        CHECK(ob_row_add(a, "Notes", 2, f, v, &key), "the first sees the new field: %s", ob_error(a));
        CHECK(!ob_row_add(a, "Notes", 2, f, bad, &key), "and checks it");
        CHECK_ERR(a, "When must be a date");
    }
    ob_close(b);
    ob_close(a);
}

/* ---- Views and CSV (0.3) ---- */

static void add_customer(ob_project *p, const char *name, const char *town, const char *email,
                         const char *joined, const char *member, const char *credit)
{
    const char *f[] = { "Name", "Town", "Email", "Joined", "Member", "Credit" };
    const char *v[6];
    v[0] = name; v[1] = town; v[2] = email; v[3] = joined; v[4] = member; v[5] = credit;
    CHECK(ob_row_add(p, "Customers", 6, f, v, NULL), "add %s: %s", name, ob_error(p));
}

static int name_cb(void *u, ob_int key, int n, const char *const *v)
{
    char *out = u;
    size_t len = strlen(out);
    (void)key; (void)n;
    snprintf(out + len, 512 - len, "%s%s", len ? "," : "", v[1] ? v[1] : "-");
    return 0;
}

static const char *names_of(ob_project *p, const ob_view *v, ob_int first, int max)
{
    static char got[512];
    got[0] = 0;
    if (ob_view_rows(p, "Customers", v, first, max, name_cb, got) < 0) snprintf(got, sizeof got, "error: %s", ob_error(p));
    return got;
}

static void view_set(ob_view *v, const char *sort, int desc, const char *field, ob_filter_op op, const char *value)
{
    ob_view_init(v);
    snprintf(v->sort, sizeof v->sort, "%s", sort);
    v->desc = desc;
    snprintf(v->field, sizeof v->field, "%s", field);
    v->op = op;
    snprintf(v->value, sizeof v->value, "%s", value);
}

static void five_customers(ob_project *p)
{
    static ob_table t;
    customers(&t);
    CHECK(ob_table_create(p, &t), "Customers: %s", ob_error(p));
    add_customer(p, "dan", "York", "dan@example.org", "2026-03-01", "Yes", "250");
    add_customer(p, "Ann", "Leeds", NULL, "2025-12-31", "No", "1,500");
    add_customer(p, "Cy", "Hull", "cy@example.org", NULL, "Yes", "99.99");
    add_customer(p, "bob", "York", "bob 100%_x@example.org", "2026-11-11", "No", NULL);
    add_customer(p, "Eve", "Leeds", "eve@example.org", "2024-01-02", "Yes", "-5");
}

static void test_views(void)
{
    ob_project *p = fresh("views.oadb");
    ob_view v;
    if (!p) return;
    five_customers(p);
    CHECK(!strcmp(names_of(p, NULL, 0, -1), "dan,Ann,Cy,bob,Eve"), "key order: %s", names_of(p, NULL, 0, -1));
    view_set(&v, "Name", 0, "", OB_FILTER_NONE, "");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann,bob,Cy,dan,Eve"), "by name, no case: %s", names_of(p, &v, 0, -1));
    CHECK(!strcmp(names_of(p, &v, 1, 2), "bob,Cy"), "a page of it: %s", names_of(p, &v, 1, 2));
    v.desc = 1;
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Eve,dan,Cy,bob,Ann"), "Z to A: %s", names_of(p, &v, 0, -1));
    view_set(&v, "Credit", 0, "", OB_FILTER_NONE, "");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "bob,Eve,Cy,dan,Ann"), "by money (empty first): %s", names_of(p, &v, 0, -1));
    view_set(&v, "Town", 0, "", OB_FILTER_NONE, "");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Cy,Ann,Eve,dan,bob"), "ties keep key order: %s", names_of(p, &v, 0, -1));
    snprintf(v.then[0], sizeof v.then[0], "Name");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Cy,Ann,Eve,bob,dan"), "Town, then Name: %s", names_of(p, &v, 0, -1));
    v.desc = 1;
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,bob,Eve,Ann,Cy"), "Z to A, both: %s", names_of(p, &v, 0, -1));
    snprintf(v.then[1], sizeof v.then[1], "Postcode");
    CHECK(!ob_view_check(p, "Customers", &v) && strstr(ob_error(p), "Postcode"), "then by a field that isn't: %s",
          ob_error(p));
    v.then[1][0] = 0;
    v.desc = 0;
    snprintf(v.sort, sizeof v.sort, "%s", "");
    CHECK(!ob_view_check(p, "Customers", &v) && strstr(ob_error(p), "then by Name needs a sort"), "then with no sort: %s",
          ob_error(p));
    view_set(&v, "Joined", 1, "", OB_FILTER_NONE, "");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "bob,dan,Ann,Eve,Cy"), "dates newest first: %s", names_of(p, &v, 0, -1));

    view_set(&v, "", 0, "Town", OB_FILTER_EQUALS, "york");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,bob") && ob_view_total(p, "Customers", &v) == 2, "Town = york: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Town", OB_FILTER_NOT_EQUALS, "York");
    CHECK(ob_view_total(p, "Customers", &v) == 3, "Town is not York");
    view_set(&v, "Name", 0, "Credit", OB_FILTER_MORE, "100");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann,dan"), "Credit > 100: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Credit", OB_FILTER_LESS, "0");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Eve"), "Credit < 0: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Joined", OB_FILTER_LESS, "1/1/2026");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann,Eve"), "Joined before 2026 (d/m/y): %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Member", OB_FILTER_EQUALS, "yes");
    CHECK(ob_view_total(p, "Customers", &v) == 3, "members");
    view_set(&v, "", 0, "Email", OB_FILTER_CONTAINS, "EXAMPLE");
    CHECK(ob_view_total(p, "Customers", &v) == 4, "Email contains EXAMPLE");
    view_set(&v, "", 0, "Email", OB_FILTER_CONTAINS, "100%_");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "bob"), "%% and _ are plain: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Email", OB_FILTER_CONTAINS, "0%x");
    CHECK(ob_view_total(p, "Customers", &v) == 0, "%% is not a wildcard");
    view_set(&v, "", 0, "Name", OB_FILTER_STARTS, "c");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Cy"), "Name starts with c: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Email", OB_FILTER_EMPTY, "");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann"), "no Email: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Credit", OB_FILTER_NOT_EMPTY, "");
    CHECK(ob_view_total(p, "Customers", &v) == 4, "with a Credit");
    view_set(&v, "", 0, "id", OB_FILTER_MORE, "3");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "bob,Eve"), "id > 3: %s", names_of(p, &v, 0, -1));

    /* plain words */
    view_set(&v, "", 0, "Credit", OB_FILTER_MORE, "lots");
    CHECK(!ob_view_check(p, "Customers", &v), "Credit > lots");
    CHECK_ERR(p, "Credit limit must be an amount of money");
    CHECK(ob_view_total(p, "Customers", &v) == -1, "total of a bad view");
    view_set(&v, "", 0, "Credit", OB_FILTER_CONTAINS, "1");
    CHECK(!ob_view_check(p, "Customers", &v), "Credit contains");
    CHECK_ERR(p, "Contains works on fields that hold text");
    view_set(&v, "", 0, "Credit", OB_FILTER_EQUALS, "");
    CHECK(!ob_view_check(p, "Customers", &v), "Credit = nothing");
    CHECK_ERR(p, "use Is empty");
    view_set(&v, "Nope", 0, "", OB_FILTER_NONE, "");
    CHECK(!ob_view_check(p, "Customers", &v), "sort by a field that isn't");
    CHECK_ERR(p, "Customers has no field Nope");
    CHECK(!strncmp(names_of(p, &v, 0, -1), "error:", 6), "rows of a bad view");
    CHECK(!strcmp(ob_filter_name(OB_FILTER_STARTS), "Starts with"), "filter names");
    ob_close(p);
}

/* ---- Filter by Form (0.6.2) ---- */

static void form_set(ob_view *v, const char *field, int row, const char *text)
{
    CHECK(ob_view_form_set(v, field, row, text), "set %s in row %d", field, row);
}

static void test_form_filter(void)
{
    ob_project *p = fresh("formfilter.oadb");
    ob_view v;
    int i;
    if (!p) return;
    five_customers(p);

    /* keeping the criteria */
    ob_view_init(&v);
    CHECK(!ob_view_filtered(&v) && v.ncrit == 0, "a new view filters nothing");
    form_set(&v, "Town", 0, "York");
    form_set(&v, "town", 0, "york");
    CHECK(v.ncrit == 1 && !strcmp(ob_view_form_get(&v, "Town", 0), "york"), "one criterion a field and row, any case");
    CHECK(ob_view_filtered(&v), "Filter by Form filters");
    CHECK(!strcmp(ob_view_form_get(&v, "Town", 1), ""), "none in the Or row");
    form_set(&v, "Town", 0, "  ");
    CHECK(v.ncrit == 0 && !ob_view_filtered(&v), "a blank one is taken out");
    CHECK(!ob_view_form_set(&v, "Town", OB_FORM_OR, "York"), "no row past the last Or");
    CHECK(!ob_view_form_set(&v, "Town", -1, "York"), "no row before Look for");
    for (i = 0; i < OB_FORM_CRIT; i++) {
        char name[16];
        snprintf(name, sizeof name, "F%d", i);
        form_set(&v, name, i % OB_FORM_OR, "1");
    }
    CHECK(!ob_view_form_set(&v, "Name", 0, "x"), "full at %d", OB_FORM_CRIT);
    CHECK(ob_view_form_set(&v, "F0", 0, "2") && v.ncrit == OB_FORM_CRIT, "a full one still changes a criterion");
    ob_view_form_clear(&v);
    CHECK(v.ncrit == 0 && !ob_view_filtered(&v), "cleared");

    /* Look for: every criterion in the row */
    form_set(&v, "Town", 0, "york");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,bob") && ob_view_total(p, "Customers", &v) == 2,
          "Town york: %s", names_of(p, &v, 0, -1));
    form_set(&v, "Member", 0, "yes");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan") && ob_view_total(p, "Customers", &v) == 1,
          "and a member: %s", names_of(p, &v, 0, -1));

    /* Or rows */
    ob_view_form_clear(&v);
    form_set(&v, "Town", 0, "York");
    form_set(&v, "Credit", 1, "> 1000");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,Ann,bob") && ob_view_total(p, "Customers", &v) == 3,
          "York, or Credit over 1,000: %s", names_of(p, &v, 0, -1));
    form_set(&v, "Member", 1, "no");
    form_set(&v, "Name", 2, "e*");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,Ann,bob,Eve"),
          "York, or a non-member over 1,000, or E...: %s", names_of(p, &v, 0, -1));

    /* the plain words of a query's criteria */
    ob_view_form_clear(&v);
    form_set(&v, "Name", 0, "?y");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Cy"), "?y: %s", names_of(p, &v, 0, -1));
    form_set(&v, "Name", 0, "a*");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann"), "a*: %s", names_of(p, &v, 0, -1));
    form_set(&v, "Name", 0, "not a*");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,Cy,bob,Eve"), "not a*: %s", names_of(p, &v, 0, -1));
    ob_view_form_clear(&v);
    form_set(&v, "Credit", 0, "between 0 and 300");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,Cy"), "between 0 and 300: %s", names_of(p, &v, 0, -1));
    ob_view_form_clear(&v);
    form_set(&v, "Joined", 0, "< 1/1/2026");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann,Eve"), "joined before 2026: %s", names_of(p, &v, 0, -1));
    ob_view_form_clear(&v);
    form_set(&v, "Email", 0, "empty");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Ann"), "no Email: %s", names_of(p, &v, 0, -1));
    ob_view_form_clear(&v);
    form_set(&v, "Town", 0, "not Leeds");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "dan,Cy,bob"), "not Leeds: %s", names_of(p, &v, 0, -1));
    ob_view_form_clear(&v);
    form_set(&v, "id", 0, ">= 4");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "bob,Eve"), "id >= 4: %s", names_of(p, &v, 0, -1));

    /* on top of the one-field filter, and sorted */
    view_set(&v, "", 0, "Member", OB_FILTER_EQUALS, "Yes");
    form_set(&v, "Town", 0, "Leeds");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "Eve"), "members in Leeds: %s", names_of(p, &v, 0, -1));
    view_set(&v, "", 0, "Email", OB_FILTER_EMPTY, "");
    form_set(&v, "Town", 0, "York");
    CHECK(ob_view_total(p, "Customers", &v) == 0, "no Email and York (the filter's OR kept inside)");
    view_set(&v, "Name", 0, "", OB_FILTER_NONE, "");
    form_set(&v, "Town", 0, "York");
    CHECK(!strcmp(names_of(p, &v, 0, -1), "bob,dan"), "York by name: %s", names_of(p, &v, 0, -1));
    CHECK(ob_view_position(p, "Customers", &v, 1) == 1, "dan's place in it");
    CHECK(ob_view_position(p, "Customers", &v, 2) == -1, "Ann is left out");
    CHECK(ob_view_find(p, "Customers", &v, "Email", "dan", 0, 0) == 1, "Find in it");

    /* plain words when it is wrong */
    ob_view_form_clear(&v);
    form_set(&v, "Credit", 0, "> lots");
    CHECK(!ob_view_check(p, "Customers", &v), "Credit > lots");
    CHECK_ERR(p, "Credit limit must be an amount of money");
    CHECK(ob_view_total(p, "Customers", &v) == -1, "total of a bad one");
    CHECK(!strncmp(names_of(p, &v, 0, -1), "error:", 6), "rows of a bad one");
    ob_view_form_clear(&v);
    form_set(&v, "Town", 0, "[Which town?]");
    CHECK(!ob_view_check(p, "Customers", &v), "a question");
    CHECK_ERR(p, "Filter by Form can't ask a question");
    ob_view_form_clear(&v);
    form_set(&v, "Nope", 0, "1");
    CHECK(!ob_view_check(p, "Customers", &v), "a field that isn't");
    CHECK_ERR(p, "Customers has no field Nope");
    ob_view_form_clear(&v);
    form_set(&v, "Credit", 0, "between 1");
    CHECK(!ob_view_check(p, "Customers", &v), "between without and");
    CHECK_ERR(p, "write between 1 and 5");
    ob_view_form_clear(&v);
    form_set(&v, "Name", 0, "x'); DROP TABLE Customers; --");
    CHECK(ob_view_total(p, "Customers", &v) == 0 && ob_row_total(p, "Customers") == 5, "quotes stay text");
    ob_close(p);
}

static char *slurp(const char *file)
{
    static char buf[4096];
    FILE *f = fopen(path(file), "rb");
    size_t n = 0;
    buf[0] = 0;
    if (!f) return buf;
    n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

/* slurp into memory of its own, for comparing two files */
static char *slurp_own(const char *file)
{
    const char *t = slurp(file);
    char *c = malloc(strlen(t) + 1);
    if (c) strcpy(c, t);
    return c;
}

static void spill(const char *file, const char *text)
{
    FILE *f = fopen(path(file), "wb");
    if (!f) { CHECK(0, "make %s", file); return; }
    fputs(text, f);
    fclose(f);
}

static void test_csv(void)
{
    ob_project *p = fresh("csv.oadb"), *q;
    ob_view v;
    char got[1024];
    if (!p) return;
    five_customers(p);
    {
        const char *f[] = { "Name" }, *val[] = { "Quote \"Q\", Esq.\nline two" };
        CHECK(ob_row_add(p, "Customers", 1, f, val, NULL), "add Q: %s", ob_error(p));
    }
    CHECK(ob_csv_export(p, "Customers", NULL, path("all.csv"), 0) == 6, "export: %s", ob_error(p));
    CHECK(!strcmp(slurp("all.csv"),
        "id,Name,Town,Email,Joined,Member,Credit\r\n"
        "1,dan,York,dan@example.org,2026-03-01,Yes,250.00\r\n"
        "2,Ann,Leeds,,2025-12-31,No,\"1,500.00\"\r\n"
        "3,Cy,Hull,cy@example.org,,Yes,99.99\r\n"
        "4,bob,York,bob 100%_x@example.org,2026-11-11,No,\r\n"
        "5,Eve,Leeds,eve@example.org,2024-01-02,Yes,-5.00\r\n"
        "6,\"Quote \"\"Q\"\", Esq.\nline two\",Leeds,,,No,\r\n"), "all.csv:\n%s", slurp("all.csv"));
    view_set(&v, "Name", 1, "Town", OB_FILTER_EQUALS, "Leeds");
    CHECK(ob_csv_export(p, "Customers", &v, path("leeds.csv"), ';') == 3, "export a view: %s", ob_error(p));
    CHECK(strstr(slurp("leeds.csv"), "\r\n5;Eve;Leeds;eve@example.org;2024-01-02;Yes;-5.00\r\n2;Ann;Leeds;;2025-12-31;No;1,500.00\r\n"),
          "leeds.csv (Z to A, ; keeps 1,500.00 bare):\n%s", slurp("leeds.csv"));
    CHECK(ob_csv_export(p, "Customers", NULL, path("x.csv"), '|') == -1, "| is not a separator");
    CHECK_ERR(p, "comma, a semicolon or a tab");
    CHECK(ob_csv_export(p, "Customers", NULL, path("no/such/drawer/x.csv"), 0) == -1, "export nowhere");
    CHECK_ERR(p, "could not be made");

    /* round trip into a fresh project: the same keys and values */
    q = fresh("csv2.oadb");
    if (q) {
        static ob_table t;
        customers(&t);
        CHECK(ob_table_create(q, &t), "Customers in csv2: %s", ob_error(q));
        CHECK(ob_csv_import(q, "Customers", path("all.csv"), 0) == 6, "import all.csv: %s", ob_error(q));
        got[0] = 0;
        ob_rows(q, "Customers", 0, 10, row_cb, got);
        CHECK(!strcmp(got, "1:1|dan|York|dan@example.org|2026-03-01|Yes|250.00;2:2|Ann|Leeds|-|2025-12-31|No|1,500.00;"
                           "3:3|Cy|Hull|cy@example.org|-|Yes|99.99;4:4|bob|York|bob 100%_x@example.org|2026-11-11|No|-;"
                           "5:5|Eve|Leeds|eve@example.org|2024-01-02|Yes|-5.00;6:6|Quote \"Q\", Esq.\nline two|Leeds|-|-|No|-;"),
              "round trip:\n%s", got);
        /* again: the keys are taken, so nothing is added */
        CHECK(ob_csv_import(q, "Customers", path("all.csv"), 0) == -1, "the same keys twice");
        CHECK_ERR(q, "Line 2:");
        CHECK(ob_row_total(q, "Customers") == 6, "all or none");

        /* a header in another order, by label, any case, with ; found; no id: new keys */
        spill("in1.csv", "\xEF\xBB\xBF" "CREDIT LIMIT;name;Joined\n12.5;Fay;11/11/2026\n\n;Gus;\r\n");
        CHECK(ob_csv_import(q, "Customers", path("in1.csv"), 0) == 2, "in1.csv: %s", ob_error(q));
        got[0] = 0;
        ob_rows(q, "Customers", 6, 10, row_cb, got);
        CHECK(!strcmp(got, "7:7|Fay|Leeds|-|2026-11-11|No|12.50;8:8|Gus|Leeds|-|-|No|-;"), "in1:\n%s", got);

        /* no header: the fields in order, a column short starts after id; tabs */
        spill("in2.csv", "Hal\tHull\thal@example.org\t2026-01-05\tyes\t3\n\"Ida\"\tYork\t\t\tno\t\n");
        CHECK(ob_csv_import(q, "Customers", path("in2.csv"), 0) == 2, "in2.csv: %s", ob_error(q));
        CHECK(ob_row_total(q, "Customers") == 10, "10 rows");

        /* plain words, nothing added */
        spill("bad1.csv", "Name,Credit\nJo,1\nKim,lots\n");
        CHECK(ob_csv_import(q, "Customers", path("bad1.csv"), 0) == -1, "Credit lots");
        CHECK_ERR(q, "Line 3: Credit limit must be an amount of money");
        CHECK(ob_row_total(q, "Customers") == 10, "Jo is not kept");
        spill("bad2.csv", "Name,Town\n\"Lu,York\n");
        CHECK(ob_csv_import(q, "Customers", path("bad2.csv"), 0) == -1, "open quote");
        CHECK_ERR(q, "Line 2: a quote is never closed");
        spill("bad3.csv", "Name,Town\nMo,York,extra\n");
        CHECK(ob_csv_import(q, "Customers", path("bad3.csv"), 0) == -1, "too many values");
        CHECK_ERR(q, "Line 2 has 3 values: the table takes 2");
        spill("bad4.csv", "Name,name\nNo,No\n");
        CHECK(ob_csv_import(q, "Customers", path("bad4.csv"), 0) == -1, "a field twice");
        CHECK_ERR(q, "names Name twice");
        spill("bad5.csv", "");
        CHECK(ob_csv_import(q, "Customers", path("bad5.csv"), 0) == -1, "empty");
        CHECK_ERR(q, "is empty");
        spill("bad6.csv", "Town\nYork\n");
        CHECK(ob_csv_import(q, "Customers", path("bad6.csv"), 0) == -1, "no Name");
        CHECK_ERR(q, "Line 2: Name must be filled in");
        CHECK(ob_csv_import(q, "Customers", path("none.csv"), 0) == -1, "no file");
        CHECK_ERR(q, "could not be opened");
        CHECK(ob_csv_import(q, "Nope", path("in1.csv"), 0) == -1, "no table");
        CHECK(ob_row_total(q, "Customers") == 10, "still 10 rows");

        /* inside the caller's transaction: a savepoint, and the caller's rollback undoes it */
        CHECK(ob_begin(q), "begin");
        CHECK(ob_csv_import(q, "Customers", path("in1.csv"), 0) == 2, "import in a transaction: %s", ob_error(q));
        CHECK(ob_csv_import(q, "Customers", path("bad1.csv"), 0) == -1 && ob_in_transaction(q), "a bad import keeps the transaction");
        CHECK(ob_row_total(q, "Customers") == 12, "12 in the transaction");
        CHECK(ob_rollback(q) && ob_row_total(q, "Customers") == 10, "rolled back");
        ob_close(q);
    }
    ob_close(p);
}

/* ---- Undo, and CSV with options (0.4) ---- */

static const char *all_rows(ob_project *p, const char *table)
{
    static char got[1024];
    got[0] = 0;
    if (ob_rows(p, table, 0, 50, row_cb, got) < 0) snprintf(got, sizeof got, "error: %s", ob_error(p));
    return got;
}

static void test_undo(void)
{
    ob_project *p = fresh("undo.oadb");
    static ob_table t;
    char before[1024];
    ob_int key;
    if (!p) return;
    customers(&t);
    CHECK(ob_table_create(p, &t), "Customers: %s", ob_error(p));
    CHECK(ob_undo_count(p) == 0 && !strcmp(ob_undo_name(p), ""), "nothing yet");
    CHECK(!ob_undo(p), "undo with nothing");
    CHECK_ERR(p, "Nothing to undo");
    add_customer(p, "Ann", "York", "ann@example.org", "2026-01-02", "Yes", "12.50");
    add_customer(p, "Bob", "Hull", NULL, NULL, "No", NULL);
    CHECK(ob_undo_count(p) == 2 && !strcmp(ob_undo_name(p), "Add Row in Customers"), "%s", ob_undo_name(p));
    snprintf(before, sizeof before, "%s", all_rows(p, "Customers"));
    {
        const char *f[] = { "Name", "Credit", "Joined" }, *v[] = { "Annie", "", "3/4/2026" };
        CHECK(ob_row_change(p, "Customers", 1, 3, f, v), "change: %s", ob_error(p));
    }
    CHECK(!strcmp(ob_undo_name(p), "Change Row in Customers"), "%s", ob_undo_name(p));
    CHECK(ob_undo(p) && !strcmp(all_rows(p, "Customers"), before), "change undone:\n%s", all_rows(p, "Customers"));
    CHECK(ob_row_delete(p, "Customers", 1), "delete: %s", ob_error(p));
    CHECK(!strcmp(ob_undo_name(p), "Delete Row in Customers"), "%s", ob_undo_name(p));
    CHECK(ob_undo(p) && !strcmp(all_rows(p, "Customers"), before), "delete undone, same key and values:\n%s", all_rows(p, "Customers"));
    CHECK(ob_undo(p) && ob_row_total(p, "Customers") == 1, "Bob's add undone");
    CHECK(ob_undo(p) && ob_row_total(p, "Customers") == 0 && ob_undo_count(p) == 0, "Ann's add undone");

    /* an import is one step */
    spill("u1.csv", "Name,Town\nCy,York\nDi,Leeds\nEd,Hull\n");
    add_customer(p, "Fay", "York", NULL, NULL, NULL, NULL);
    CHECK(ob_csv_import(p, "Customers", path("u1.csv"), 0) == 3, "import: %s", ob_error(p));
    CHECK(ob_undo_count(p) == 2 && !strcmp(ob_undo_name(p), "Import of 3 rows into Customers"), "%s", ob_undo_name(p));
    CHECK(ob_undo(p) && ob_row_total(p, "Customers") == 1, "import undone");

    /* a design change: the steps can't be undone any more */
    {
        const char *f[] = { "Name" }, *v[] = { "Gus" };
        CHECK(ob_row_add(p, "Customers", 1, f, v, &key), "Gus: %s", ob_error(p));
    }
    customers(&t);
    ob_table_add_field(&t, "Notes", OB_MEMO);
    {
        int i;
        for (i = 0; i < t.nfields; i++) snprintf(t.fields[i].from, sizeof t.fields[i].from, "%s", t.fields[i].name);
        t.fields[t.nfields - 1].from[0] = 0;
    }
    CHECK(ob_table_change(p, "Customers", &t), "add Notes: %s", ob_error(p));
    CHECK(!ob_undo(p), "undo after a design change");
    CHECK_ERR(p, "design has changed");
    CHECK(ob_undo_count(p) == 0 && ob_row_total(p, "Customers") == 2, "Gus kept, list cleared");

    /* a cascading delete can't be undone */
    {
        static ob_table o;
        ob_int ok_key;
        orders(&o, OB_DELETE_CASCADE);
        CHECK(ob_table_create(p, &o), "Orders: %s", ob_error(p));
        {
            const char *f[] = { "Customer", "Total" }, *v[] = { "2", "5" };
            snprintf(before, sizeof before, "%lld", key);
            v[0] = before;
            CHECK(ob_row_add(p, "Orders", 2, f, v, &ok_key), "order: %s", ob_error(p));
        }
        CHECK(ob_undo_count(p) == 1, "the order's step");
        CHECK(ob_row_delete(p, "Customers", key) && ob_row_total(p, "Orders") == 0, "cascade: %s", ob_error(p));
        CHECK(ob_undo_count(p) == 0, "a cascade clears the list");
    }

    /* the caller's rollback takes back the steps inside it */
    add_customer(p, "Hal", "York", NULL, NULL, NULL, NULL);
    CHECK(ob_undo_count(p) == 1, "Hal's step");
    CHECK(ob_begin(p), "begin");
    add_customer(p, "Ida", "York", NULL, NULL, NULL, NULL);
    CHECK(ob_undo_count(p) == 2, "Ida's step");
    CHECK(ob_rollback(p) && ob_undo_count(p) == 1, "rolled back: %d", ob_undo_count(p));
    CHECK(ob_undo(p), "Hal undone: %s", ob_error(p));

    /* at most 100 steps */
    {
        int i;
        for (i = 0; i < 105; i++) add_customer(p, "Many", "York", NULL, NULL, NULL, NULL);
        CHECK(ob_undo_count(p) == 100, "100 steps: %d", ob_undo_count(p));
    }
    ob_close(p);
}

/* Relationships drawn in the view: made, changed, removed (ob_relation_*) */
static int rel_rule(ob_project *p, const char *table, const char *field, ob_on_delete *rule, char *lookup)
{
    ob_relation r[16];
    int n = ob_relations(p, r, 16), i;
    for (i = 0; i < n && i < 16; i++)
        if (!strcmp(r[i].table, table) && !strcmp(r[i].field, field)) {
            if (rule) *rule = r[i].on_delete;
            if (lookup) strcpy(lookup, r[i].lookup);
            return 1;
        }
    return 0;
}

static void test_relation_draw(void)
{
    ob_project *p = fresh("reldraw.oadb");
    static ob_table t, o;
    ob_field *f;
    ob_on_delete rule;
    char made[OB_NAME_MAX], lk[OB_NAME_MAX];
    const char *cf[] = { "Name" }, *cv[] = { "Ann" }, *of[] = { "Cust", "Total" }, *ov[] = { "1", "5.00" }, *ov2[] = { "9", "1.00" };
    ob_int key;
    if (!p) return;
    customers(&t);
    CHECK(ob_table_create(p, &t), "Customers: %s", ob_error(p));
    ob_table_init(&o, "Orders");
    ob_table_add_field(&o, "Cust", OB_INTEGER);
    f = ob_table_add_field(&o, "Total", OB_CURRENCY);
    ob_table_add_field(&o, "Note", OB_TEXT);
    (void)f;
    CHECK(ob_table_create(p, &o), "Orders: %s", ob_error(p));
    CHECK(ob_row_add(p, "Customers", 1, cf, cv, &key) && key == 1, "Ann is 1");
    CHECK(ob_row_add(p, "Orders", 2, of, ov, NULL), "an order for 1: %s", ob_error(p));
    CHECK(ob_relations(p, NULL, 0) == 0, "no relationships yet");

    /* the key dragged onto Orders.Cust */
    CHECK(ob_relation_add(p, "Orders", "cust", "customers", OB_DELETE_CASCADE, made, sizeof made) && !strcmp(made, "Cust"),
          "Cust looks up Customers: %s", ob_error(p));
    CHECK(rel_rule(p, "Orders", "Cust", &rule, lk) && rule == OB_DELETE_CASCADE && !strcmp(lk, "Customers"), "listed, cascade");
    CHECK(ob_table_get(p, "Orders", &o) && o.fields[1].type == OB_LOOKUP && !strcmp(o.fields[1].lookup, "Customers"), "a Lookup now");
    CHECK(ob_row_total(p, "Orders") == 1, "the order kept");
    CHECK(!ob_row_add(p, "Orders", 2, of, ov2, NULL), "an order for 9 is refused now");

    /* its rule changed, then cleared */
    CHECK(ob_relation_set_rule(p, "Orders", "Cust", OB_DELETE_CLEAR) && rel_rule(p, "Orders", "Cust", &rule, NULL)
          && rule == OB_DELETE_CLEAR, "clear: %s", ob_error(p));
    CHECK(ob_row_delete(p, "Customers", 1) && ob_row_total(p, "Orders") == 1, "Ann deleted, her order kept, cleared: %s", ob_error(p));
    CHECK(ob_row_add(p, "Customers", 1, cf, cv, &key), "Ann again: %lld", (long long)key);
    CHECK(ob_relation_set_rule(p, "Orders", "Cust", OB_DELETE_REFUSE) && rel_rule(p, "Orders", "Cust", &rule, NULL)
          && rule == OB_DELETE_REFUSE, "refuse");
    CHECK(!ob_relation_set_rule(p, "Orders", "Cust", (ob_on_delete)7), "no such rule");
    CHECK(!ob_relation_set_rule(p, "Orders", "Note", OB_DELETE_CASCADE) && strstr(ob_error(p), "doesn't look anything up"), "Note isn't one");

    /* onto the table: a new Lookup field */
    CHECK(ob_relation_add(p, "Orders", NULL, "Customers", OB_DELETE_REFUSE, made, sizeof made) && !strcmp(made, "Customers"),
          "a new field Customers: %s", ob_error(p));
    CHECK(ob_relation_add(p, "Orders", "", "Customers", OB_DELETE_REFUSE, made, sizeof made) && !strcmp(made, "Customers 2"),
          "and Customers 2: %s %s", made, ob_error(p));
    CHECK(ob_relations(p, NULL, 0) == 3, "three now");
    CHECK(ob_row_total(p, "Orders") == 1, "the order kept again");

    /* refused, nothing changed */
    CHECK(!ob_relation_add(p, "Orders", "Note", "Customers", OB_DELETE_REFUSE, NULL, 0) && strstr(ob_error(p), "only an Integer"),
          "Text can't: %s", ob_error(p));
    CHECK(!ob_relation_add(p, "Orders", "id", "Customers", OB_DELETE_REFUSE, NULL, 0) && strstr(ob_error(p), "key"), "the Counter can't: %s", ob_error(p));
    CHECK(!ob_relation_add(p, "Orders", "Nosuch", "Customers", OB_DELETE_REFUSE, NULL, 0), "no such field");
    CHECK(!ob_relation_add(p, "Orders", "Cust", "Nosuch", OB_DELETE_REFUSE, NULL, 0), "no such table");
    CHECK(!ob_relation_add(p, "Nosuch", "Cust", "Customers", OB_DELETE_REFUSE, NULL, 0), "no such table to change");
    {
        /* a number that isn't a key: refused with the reason, the field still an Integer */
        static ob_table x;
        const char *xf[] = { "Who" }, *xv[] = { "42" };
        ob_table_init(&x, "Visits");
        ob_table_add_field(&x, "Who", OB_INTEGER);
        CHECK(ob_table_create(p, &x) && ob_row_add(p, "Visits", 1, xf, xv, NULL), "Visits: %s", ob_error(p));
        CHECK(!ob_relation_add(p, "Visits", "Who", "Customers", OB_DELETE_REFUSE, NULL, 0) && strstr(ob_error(p), "aren't there"),
              "42 isn't a customer: %s", ob_error(p));
        CHECK(ob_table_get(p, "Visits", &x) && x.fields[1].type == OB_INTEGER && ob_row_total(p, "Visits") == 1, "unchanged");
    }

    /* removed: an Integer again, the keys kept */
    CHECK(ob_relation_remove(p, "Orders", "Cust") && !rel_rule(p, "Orders", "Cust", NULL, NULL), "removed: %s", ob_error(p));
    CHECK(ob_table_get(p, "Orders", &o) && o.fields[1].type == OB_INTEGER && !o.fields[1].lookup[0], "an Integer");
    CHECK(ob_row_add(p, "Orders", 2, of, ov2, NULL), "an order for 9 goes in now: %s", ob_error(p));
    CHECK(!ob_relation_remove(p, "Orders", "Cust") && strstr(ob_error(p), "doesn't look"), "not twice");
    CHECK(ob_relations(p, NULL, 0) == 2, "two left");
    ob_close(p);
}

/* A file's bytes (made up here, or written by the core); its length in *n. */
static unsigned char *bytes_of(const char *file, long *n)
{
    FILE *f = fopen(path(file), "rb");
    unsigned char *b;
    *n = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*n + 1);
    if (b && fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static void put_bytes(const char *file, const void *b, size_t n)
{
    FILE *f = fopen(path(file), "wb");
    if (!f) return;
    fwrite(b, 1, n, f);
    fclose(f);
}

/* A made-up .dbf: cols as "NAME:T:len:dec" each, records each a whole record
 * (its flag first); nrecs as the header says (-1: as many as given). */
static void made_dbf(const char *file, int version, const char *const *cols, int ncols,
                     const char *const *recs, int nr, int nrecs, int reclen_off)
{
    unsigned char b[8192];
    int i, n = 32, reclen = 1;
    memset(b, 0, sizeof b);
    for (i = 0; i < ncols; i++) {
        char nm[16], t;
        int len, dec;
        unsigned char *d = b + 32 + 32 * i;
        sscanf(cols[i], "%15[^:]:%c:%d:%d", nm, &t, &len, &dec);
        memcpy(d, nm, strlen(nm));
        d[11] = (unsigned char)t;
        d[16] = (unsigned char)len;
        d[17] = (unsigned char)dec;
        reclen += len;
        n += 32;
    }
    b[0] = (unsigned char)version;
    b[1] = 126; b[2] = 11; b[3] = 15;
    i = nrecs < 0 ? nr : nrecs;
    b[4] = (unsigned char)i; b[5] = (unsigned char)(i >> 8);
    b[8] = (unsigned char)((n + 1) & 255); b[9] = (unsigned char)((n + 1) >> 8);
    reclen += reclen_off;
    b[10] = (unsigned char)(reclen & 255); b[11] = (unsigned char)(reclen >> 8);
    b[n++] = 0x0D;
    for (i = 0; i < nr; i++) {
        memcpy(b + n, recs[i], strlen(recs[i]));
        n += (int)strlen(recs[i]);
    }
    if (nrecs < 0 || nrecs == nr) b[n++] = 0x1A;
    put_bytes(file, b, (size_t)n);
}

static void test_dbf(void)
{
    ob_project *p = fresh("dbf.oadb"), *q;
    static ob_table t;
    ob_csv_opts o;
    ob_view v;
    unsigned char *b;
    long n;
    char want[256];
    if (!p) return;
    five_customers(p);

    /* Customers out: the header, the columns, a record */
    CHECK(ob_dbf_export(p, "Customers", NULL, path("cust.dbf")) == 5, "export: %s", ob_error(p));
    b = bytes_of("cust.dbf", &n);
    CHECK(b && n == 257 + 5 * 48 + 1, "257 + 5 x 48 + 1 bytes: %ld", n);
    if (b && n > 3) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        CHECK(tm && b[1] == tm->tm_year && b[2] == tm->tm_mon + 1 && b[3] == tm->tm_mday && b[1] >= 126,
              "last updated, the year from 1900 (126 for 2026): %d %d %d", b[1], b[2], b[3]);
        CHECK(b[29] == 0, "no language driver claimed");
    }
    if (b && n == 498) {
        static const char *const names[] = { "ID", "NAME", "TOWN", "EMAIL", "JOINED", "MEMBER", "CREDIT" };
        static const char types[] = "NCCCDLN";
        static const int lens[] = { 1, 3, 5, 22, 8, 1, 7 }, decs[] = { 0, 0, 0, 0, 0, 0, 2 };
        int i;
        CHECK(b[0] == 0x03 && b[4] == 5 && b[5] == 0 && b[8] == 1 && b[9] == 1 && b[10] == 48 && b[11] == 0,
              "dBASE III, 5 records, 257 header, 48 a record");
        for (i = 0; i < 7; i++) {
            const unsigned char *d = b + 32 + 32 * i;
            CHECK(!strcmp((const char *)d, names[i]) && d[11] == (unsigned char)types[i] && d[16] == lens[i] && d[17] == decs[i],
                  "column %d: %s %c %d.%d", i, (const char *)d, d[11], d[16], d[17]);
        }
        CHECK(b[256] == 0x0D && b[497] == 0x1A, "the ends");
        snprintf(want, sizeof want, " 2AnnLeeds%22s20251231F1500.00", "");
        CHECK(!memcmp(b + 257 + 48, want, 48), "Ann's record: %.48s", (const char *)b + 257 + 48);
        snprintf(want, sizeof want, " 3Cy Hull cy@example.org%8s%8sT  99.99", "", "");
        CHECK(!memcmp(b + 257 + 96, want, 48), "Cy's record: %.48s", (const char *)b + 257 + 96);
        snprintf(want, sizeof want, " 5EveLeedseve@example.org%7s20240102T  -5.00", "");
        CHECK(!memcmp(b + 257 + 192, want, 48), "Eve's record: %.48s", (const char *)b + 257 + 192);
    }
    free(b);
    view_set(&v, "Name", 0, "Town", OB_FILTER_EQUALS, "Leeds");
    CHECK(ob_dbf_export(p, "Customers", &v, path("leeds.dbf")) == 2, "a view: %s", ob_error(p));
    CHECK(ob_dbf_export(p, "Customers", NULL, path("no/such/drawer/x.dbf")) == -1 && strstr(ob_error(p), "could not be made"), "nowhere");

    /* and back, into a fresh project: the same rows */
    q = fresh("dbf2.oadb");
    if (q) {
        char *a, *c;
        customers(&t);
        CHECK(ob_table_create(q, &t), "Customers in dbf2: %s", ob_error(q));
        ob_csv_opts_init(&o);
        CHECK(ob_dbf_import(q, "Customers", path("cust.dbf"), &o) == 5 && o.deleted == 0, "import: %s", ob_error(q));
        ob_csv_export(p, "Customers", NULL, path("d1.csv"), 0);
        ob_csv_export(q, "Customers", NULL, path("d2.csv"), 0);
        a = slurp_own("d1.csv");
        c = slurp_own("d2.csv");
        CHECK(a && c && !strcmp(a, c), "the same rows:\n%s\n%s", a ? a : "", c ? c : "");
        free(a);
        free(c);
        CHECK(ob_undo_count(q) == 1 && ob_undo(q) && ob_view_total(q, "Customers", NULL) == 0, "undone: %s", ob_error(q));
        ob_close(q);
    }

    /* memos: a Memo, and Text too long for C, go to the .dbt; a Number's decimals */
    ob_table_init(&t, "Notes");
    ob_table_add_field(&t, "Title", OB_TEXT);
    ob_table_add_field(&t, "Body", OB_MEMO);
    ob_table_add_field(&t, "Num", OB_NUMBER);
    ob_table_add_field(&t, "At", OB_TIME);
    ob_table_add_field(&t, "A very long field name", OB_INTEGER);
    ob_table_add_field(&t, "A very long field nam2", OB_INTEGER);
    CHECK(ob_table_create(p, &t), "Notes: %s", ob_error(p));
    {
        static char title[301], body[601];
        const char *f[] = { "Title", "Body", "Num", "At" }, *v1[] = { title, body, "2.5", "9:30" }, *v2[] = { "short", NULL, "-1234.125", NULL };
        int i;
        for (i = 0; i < 300; i++) title[i] = (char)('a' + i % 26);
        for (i = 0; i < 600; i++) body[i] = (char)('A' + i % 26);
        body[100] = '\n';
        CHECK(ob_row_add(p, "Notes", 4, f, v1, NULL) && ob_row_add(p, "Notes", 4, f, v2, NULL), "notes: %s", ob_error(p));
    }
    CHECK(ob_dbf_export(p, "Notes", NULL, path("notes.dbf")) == 2, "notes out: %s", ob_error(p));
    b = bytes_of("notes.dbf", &n);
    if (b && n > 32 * 8) {
        CHECK(b[0] == 0x83, "dBASE III with memos: %02x", b[0]);
        CHECK(b[32 + 32 * 1 + 11] == 'M' && b[32 + 32 * 2 + 11] == 'M', "Title (300 long) and Body are memos");
        CHECK(b[32 + 32 * 3 + 11] == 'N' && b[32 + 32 * 3 + 16] == 9 && b[32 + 32 * 3 + 17] == 3, "Num N 9.3: %d.%d", b[32 + 32 * 3 + 16], b[32 + 32 * 3 + 17]);
        CHECK(b[32 + 32 * 4 + 11] == 'C' && b[32 + 32 * 4 + 16] == 8, "At C 8");
        CHECK(!strcmp((const char *)b + 32 + 32 * 5, "A_VERY_LON") && !strcmp((const char *)b + 32 + 32 * 6, "A_VERY_L_2"),
              "dBASE names: %s %s", (const char *)b + 32 + 32 * 5, (const char *)b + 32 + 32 * 6);
    } else CHECK(0, "notes.dbf: %ld bytes", n);
    free(b);
    b = bytes_of("notes.dbt", &n);
    CHECK(b && n == 5 * 512 && b[0] == 5 && b[1] == 0 && b[512 + 300] == 0x1A && b[1024 + 600] == 0x1A && !memcmp(b + 2048, "short\x1A\x1A", 7),
          "notes.dbt: 5 blocks, 1 the title, 2 and 3 the body, 4 the second title: %ld", n);
    free(b);
    q = fresh("dbf3.oadb");
    if (q) {
        char *a, *c;
        CHECK(ob_table_create(q, &t), "Notes in dbf3: %s", ob_error(q));
        CHECK(ob_dbf_import(q, "Notes", path("notes.dbf"), NULL) == 2, "notes in: %s", ob_error(q));
        ob_csv_export(p, "Notes", NULL, path("n1.csv"), 0);
        ob_csv_export(q, "Notes", NULL, path("n2.csv"), 0);
        a = slurp_own("n1.csv");
        c = slurp_own("n2.csv");
        CHECK(a && c && !strcmp(a, c), "memos back:\n%.200s\n%.200s", a ? a : "", c ? c : "");
        free(a);
        free(c);
        ob_close(q);
    }

    /* a dBASE IV file made here, its memos, a deleted record: a new table from it */
    {
        static const char *const cols[] = { "ID:N:5:0", "NAME:C:10:0", "PRICE:N:8:2", "WHEN:D:8:0", "OK:L:1:0", "NOTE:M:10:0" };
        static const char *const recs[] = {
            "     7Tea           2.5020261115T         1",
            "*    8Gone          1.0020261115F          ",
            "     9Cake                      ?          ",
            "    10Big       ********00000000n         2",
        };
        unsigned char m[3 * 512];
        static ob_table g;
        char *a;
        memset(m, 0, sizeof m);
        m[0] = 3; m[20] = 0; m[21] = 2;             /* next block 3, blocks of 512 */
        memcpy(m + 512, "\xff\xff\x08\x00\x0d\x00\x00\x00hello", 13);
        memcpy(m + 1024, "plain\x1a\x1a", 7);
        made_dbf("SALES.DBF", 0x8B, cols, 6, recs, 4, -1, 0);
        put_bytes("SALES.DBT", m, sizeof m);
        CHECK(ob_dbf_guess(p, path("SALES.DBF"), NULL, &g), "guess: %s", ob_error(p));
        CHECK(!strcmp(g.name, "Sales") && g.nfields == 6, "Sales, 6 fields: %s %d", g.name, g.nfields);
        CHECK(g.fields[0].type == OB_COUNTER && !strcmp(g.fields[0].name, "ID"), "ID the Counter: %s", g.fields[0].name);
        CHECK(!strcmp(g.fields[1].name, "Name") && g.fields[1].type == OB_TEXT && g.fields[1].max_len == 10, "Name, Text 10");
        CHECK(g.fields[2].type == OB_CURRENCY && g.fields[3].type == OB_DATE && g.fields[4].type == OB_YESNO && g.fields[5].type == OB_MEMO
              && !strcmp(g.fields[5].name, "Note"), "Currency, Date, Yes/No, Memo");
        CHECK(ob_table_create(p, &g), "Sales made: %s", ob_error(p));
        ob_csv_opts_init(&o);
        CHECK(ob_dbf_import(p, "Sales", path("SALES.DBF"), &o) == 3 && o.deleted == 1, "3 in, 1 deleted: %s", ob_error(p));
        CHECK(!o.report[0], "nothing to report: %s", o.report);
        ob_csv_export(p, "Sales", NULL, path("sales.csv"), 0);
        a = slurp_own("sales.csv");
        CHECK(a && !strcmp(a, "ID,Name,Price,When,Ok,Note\r\n7,Tea,2.50,2026-11-15,Yes,hello\r\n9,Cake,,,,\r\n10,Big,,,No,plain\r\n"),
              "sales:\n%s", a ? a : "");
        free(a);
        CHECK(ob_dbf_guess(p, path("SALES.DBF"), "Shop sales", &g) && !strcmp(g.name, "Shop sales"), "a name given");
        /* a file that names its code page: read as it is, and said */
        {
            static const char *const one[] = { "NAME:C:3:0" }, *r1[] = { " Zed" };
            unsigned char *c;
            long cn;
            made_dbf("lang.dbf", 0x03, one, 1, r1, 1, -1, 0);
            c = bytes_of("lang.dbf", &cn);
            if (c && cn > 29) { c[29] = 0x03; put_bytes("lang.dbf", (const char *)c, (size_t)cn); }
            free(c);
            ob_csv_opts_init(&o);
            CHECK(ob_dbf_import(p, "Sales", path("lang.dbf"), &o) == 1 && strstr(o.report, "Windows code page 1252"), "CP1252 said: %s", o.report);
            if (c) {
                c = bytes_of("lang.dbf", &cn);
                if (c && cn > 29) { c[29] = 0x7E; put_bytes("lang.dbf", (const char *)c, (size_t)cn); }
                free(c);
            }
            ob_csv_opts_init(&o);
            CHECK(ob_dbf_import(p, "Sales", path("lang.dbf"), &o) == 1 && strstr(o.report, "as 7E"), "an unknown one by number: %s", o.report);
        }
    }

    /* refused */
    put_bytes("short.dbf", "\x03\x01\x02", 3);
    CHECK(ob_dbf_import(p, "Customers", path("short.dbf"), NULL) == -1 && strstr(ob_error(p), "not a dBASE file"), "short: %s", ob_error(p));
    CHECK(ob_dbf_import(p, "Customers", path("none.dbf"), NULL) == -1 && strstr(ob_error(p), "could not be opened"), "none");
    {
        static const char *const one[] = { "NAME:C:3:0" }, *bad[] = { "PIC:B:10:0" }, *memo[] = { "M:M:10:0" };
        static const char *const r1[] = { " Zed", " Yan" }, *r3[] = { "          " };
        made_dbf("fox.dbf", 0xF5, one, 1, r1, 1, -1, 0);
        CHECK(ob_dbf_import(p, "Customers", path("fox.dbf"), NULL) == -1 && strstr(ob_error(p), "is a FoxPro table"), "FoxPro: %s", ob_error(p));
        made_dbf("vfp.dbf", 0x30, one, 1, r1, 1, -1, 0);
        CHECK(ob_dbf_import(p, "Customers", path("vfp.dbf"), NULL) == -1 && strstr(ob_error(p), "Visual FoxPro"), "VFP: %s", ob_error(p));
        made_dbf("odd.dbf", 0x07, one, 1, r1, 1, -1, 0);
        CHECK(ob_dbf_import(p, "Customers", path("odd.dbf"), NULL) == -1 && strstr(ob_error(p), "not a dBASE III or IV"), "odd version");
        made_dbf("pic.dbf", 0x03, bad, 1, r3, 1, -1, 0);
        CHECK(ob_dbf_import(p, "Customers", path("pic.dbf"), NULL) == -1 && strstr(ob_error(p), "of type B"), "type B: %s", ob_error(p));
        made_dbf("len.dbf", 0x03, one, 1, r1, 1, -1, 1);
        CHECK(ob_dbf_import(p, "Customers", path("len.dbf"), NULL) == -1 && strstr(ob_error(p), "record length"), "record length: %s", ob_error(p));
        made_dbf("memo.dbf", 0x83, memo, 1, r3, 1, -1, 0);
        CHECK(ob_dbf_import(p, "Customers", path("memo.dbf"), NULL) == -1 && strstr(ob_error(p), "memo file"), "no .dbt: %s", ob_error(p));
        made_dbf("cut.dbf", 0x03, one, 1, r1, 2, 3, 0);
        CHECK(ob_dbf_import(p, "Customers", path("cut.dbf"), NULL) == -1 && strstr(ob_error(p), "ends inside record 3"), "cut short: %s", ob_error(p));
        CHECK(ob_view_total(p, "Customers", NULL) == 5, "all or none: still 5");
    }
    /* by order when the names don't match; bad records left out on request */
    {
        static const char *const cols[] = { "A:C:5:0", "B:C:5:0" };
        static const char *const recs[] = { " Zoe  Hull ", "      Hull ", " Yan  Leeds" };
        made_dbf("order.dbf", 0x03, cols, 2, recs, 3, -1, 0);
        ob_csv_opts_init(&o);
        CHECK(ob_dbf_import(p, "Customers", path("order.dbf"), &o) == -1 && strstr(ob_error(p), "Record 2:"), "all or none: %s", ob_error(p));
        ob_csv_opts_init(&o);
        o.skip_bad = 1;
        CHECK(ob_dbf_import(p, "Customers", path("order.dbf"), &o) == 2 && o.bad == 1 && strstr(o.report, "Record 2:"),
              "the good two, Name and Town by order: %s", o.report);
        CHECK(ob_view_total(p, "Customers", NULL) == 7, "7 now");
    }
    ob_close(p);
}

/* ARexx row commands (ob_rexx_rows): what GFOpenBase's port does with rows */
static int rexx(ob_project *p, ob_int current, const char *cmd, char *out, size_t size, ob_int *row)
{
    return ob_rexx_rows(p, "Customers", current, cmd, out, size, row);
}

static void test_rexx_rows(void)
{
    ob_project *p = fresh("rexxrows.oadb");
    static ob_table t;
    char out[400], cmd[200], small[8];
    ob_int row, k, k2;
    if (!p) return;
    customers(&t);
    CHECK(ob_table_create(p, &t), "Customers: %s", ob_error(p));

    CHECK(rexx(p, 0, "GETFIELDS", out, sizeof out, NULL) == 0
          && !strcmp(out, "\"id\" \"Name\" \"Town\" \"Email\" \"Joined\" \"Member\" \"Credit\""), "GETFIELDS: %s", out);
    CHECK(rexx(p, 0, "addrow \"Name=Zoe Lee\" town=Hull Member=\"Yes\" 'Credit=12.50' Joined=2026-11-15", out, sizeof out, &row) == 0,
          "ADDROW: %s", out);
    k = (ob_int)atoll(out);
    CHECK(k > 0 && row == k, "its key: %s", out);
    snprintf(cmd, sizeof cmd, "GETROW KEY %lld", (long long)k);
    CHECK(rexx(p, 0, cmd, out, sizeof out, &row) == 0 && row == k, "GETROW: %s", out);
    {
        char want[100];
        snprintf(want, sizeof want, "%lld\tZoe Lee\tHull\t\t2026-11-15\tYes\t12.50", (long long)k);
        CHECK(!strcmp(out, want), "every value, tabs between:\n%s\n%s", out, want);
    }
    snprintf(cmd, sizeof cmd, "GETROW KEY=%lld FIELD town", (long long)k);
    CHECK(rexx(p, 0, cmd, out, sizeof out, NULL) == 0 && !strcmp(out, "Hull"), "one field, any case: %s", out);
    snprintf(cmd, sizeof cmd, "GETROW FIELD Email KEY %lld", (long long)k);
    CHECK(rexx(p, 0, cmd, out, sizeof out, NULL) == 0 && !out[0], "an empty one: %s", out);
    CHECK(rexx(p, k, "GETROW FIELD Name", out, sizeof out, NULL) == 0 && !strcmp(out, "Zoe Lee"), "KEY left out: the row chosen");

    /* the defaults fill what's left out */
    CHECK(rexx(p, 0, "ADDROW Name=Al", out, sizeof out, NULL) == 0, "ADDROW Al: %s", out);
    k2 = (ob_int)atoll(out);
    snprintf(cmd, sizeof cmd, "GETROW KEY %lld FIELD Town", (long long)k2);
    CHECK(rexx(p, 0, cmd, out, sizeof out, NULL) == 0 && !strcmp(out, "Leeds"), "Town's default: %s", out);

    snprintf(cmd, sizeof cmd, "CHANGEROW KEY %lld Name=\"Zoe Ann\" \"Town\"=York", (long long)k);
    CHECK(rexx(p, 0, cmd, out, sizeof out, &row) == 0 && !strcmp(out, "OK") && row == k, "CHANGEROW: %s", out);
    CHECK(rexx(p, k, "GETROW FIELD Name", out, sizeof out, NULL) == 0 && !strcmp(out, "Zoe Ann"), "changed: %s", out);
    CHECK(rexx(p, k, "CHANGEROW Email=", out, sizeof out, NULL) == 0, "emptied: %s", out);
    CHECK(rexx(p, k, "CHANGEROW Credit=lots", out, sizeof out, NULL) == 10 && out[0], "a bad value: the core's reason: %s", out);
    CHECK(rexx(p, k, "GETROW FIELD Credit", out, sizeof out, NULL) == 0 && !strcmp(out, "12.50"), "unchanged: %s", out);

    /* refused, with the reason */
    CHECK(rexx(p, 0, "ADDROW", out, sizeof out, NULL) == 10 && strstr(out, "Field=Value"), "nothing to add: %s", out);
    CHECK(rexx(p, 0, "ADDROW Name", out, sizeof out, NULL) == 10 && strstr(out, "needs a value"), "no value: %s", out);
    CHECK(rexx(p, 0, "ADDROW Nosuch=1", out, sizeof out, NULL) == 10 && strstr(out, "has no field Nosuch"), "no field: %s", out);
    CHECK(rexx(p, 0, "ADDROW Town=Hull", out, sizeof out, NULL) == 10 && out[0], "Name is required: %s", out);
    CHECK(rexx(p, 0, "GETROW", out, sizeof out, NULL) == 5 && strstr(out, "No row is chosen"), "no row: %s", out);
    CHECK(rexx(p, 0, "GETROW KEY 99999", out, sizeof out, NULL) == 5 && strstr(out, "No row 99999"), "no such row: %s", out);
    CHECK(rexx(p, 0, "GETROW KEY abc", out, sizeof out, NULL) == 10, "KEY abc: %s", out);
    CHECK(rexx(p, k, "GETROW FIELD Nosuch", out, sizeof out, NULL) == 10, "FIELD Nosuch: %s", out);
    CHECK(rexx(p, k, "GETROW Name=x", out, sizeof out, NULL) == 10 && strstr(out, "takes KEY and FIELD"), "GETROW pairs: %s", out);
    CHECK(rexx(p, 0, "CHANGEROW KEY 99999 Name=x", out, sizeof out, NULL) == 5, "change no such row: %s", out);
    CHECK(rexx(p, k, "CHANGEROW", out, sizeof out, NULL) == 10, "nothing to change: %s", out);
    CHECK(rexx(p, k, "GETROW", small, sizeof small, NULL) == 5, "too long for the answer: %s", small);
    CHECK(rexx(p, 0, "ROWS", out, sizeof out, NULL) == -1 && rexx(p, 0, "GETROWS", out, sizeof out, NULL) == -1, "not one of these");
    CHECK(ob_rexx_rows(p, NULL, 0, "GETFIELDS", out, sizeof out, NULL) == 5 && strstr(out, "No table"), "no table: %s", out);
    CHECK(ob_rexx_rows(p, "Nosuch", 0, "GETFIELDS", out, sizeof out, NULL) == 10, "no such table: %s", out);

    /* a field named Key goes in quotes */
    {
        static ob_table kt;
        ob_table_init(&kt, "Keys");
        ob_table_add_field(&kt, "Key", OB_TEXT);
        CHECK(ob_table_create(p, &kt), "Keys: %s", ob_error(p));
        CHECK(ob_rexx_rows(p, "Keys", 0, "ADDROW \"Key=A1\"", out, sizeof out, &row) == 0, "Key in quotes: %s", out);
        CHECK(ob_rexx_rows(p, "Keys", row, "GETROW FIELD key", out, sizeof out, NULL) == 0 && !strcmp(out, "A1"), "it's there: %s", out);
    }

    snprintf(cmd, sizeof cmd, "DELETEROW KEY %lld", (long long)k);
    CHECK(rexx(p, 0, cmd, out, sizeof out, &row) == 0 && row == k, "DELETEROW: %s", out);
    CHECK(rexx(p, k, "GETROW", out, sizeof out, NULL) == 5, "gone");
    CHECK(rexx(p, k, "DELETEROW", out, sizeof out, NULL) == 5, "not twice: %s", out);
    CHECK(rexx(p, k2, "DELETEROW FIELD x", out, sizeof out, NULL) == 10 && strstr(out, "takes KEY"), "DELETEROW FIELD: %s", out);
    CHECK(ob_row_total(p, "Customers") == 1, "Al is left");
    CHECK(ob_undo(p) && ob_row_total(p, "Customers") == 2, "the delete undoes: %s", ob_error(p));
    ob_close(p);
}

/* While a form is open the port refuses what would move it or change rows (core 0.14.3). */
static void test_rexx_form_guard(void)
{
    static const char *const refused[] = {
        "OPEN DH0:Shop.oadb", "QUIT", "TABLE Orders", "row 3", "  ROW 3", "FIRSTROW", "nextrow", "PREVROW", "LASTROW",
        "ADDROW Name=Zoe", "addrow", "CHANGEROW KEY 2 Town=Hull", "DELETEROW", "DELETEROW KEY 2",
    };
    static const char *const answered[] = {
        "TABLE", "ROW", "ROW   ", "TABLES", "COUNTROWS", "GETROW", "GETROW KEY 2 FIELD Name", "GETFIELDS", "FORMS",
        "VERSION", "HELP", "LASTERROR", "ISDIRTY", "GETFILE", "ROWS", "ADDROWS", "OPENED", "",
    };
    unsigned i;
    for (i = 0; i < sizeof refused / sizeof refused[0]; i++)
        CHECK(ob_rexx_form_refuses(refused[i]), "refused while a form is open: %s", refused[i]);
    for (i = 0; i < sizeof answered / sizeof answered[0]; i++)
        CHECK(!ob_rexx_form_refuses(answered[i]), "still answers: %s", answered[i]);
    CHECK(!ob_rexx_form_refuses(NULL), "no command");
}

static void test_csv_options(void)
{
    ob_project *p = fresh("csvopt.oadb");
    static ob_table t, g;
    ob_csv_opts o;
    char got[1024];
    if (!p) return;
    customers(&t);
    CHECK(ob_table_create(p, &t), "Customers: %s", ob_error(p));

    /* UTF-8 found and made ISO-8859-1; what it lacks is ? */
    spill("utf.csv", "Name,Town\nJos\xC3\xA9,York\nZo\xC3\xAB \xE2\x82\xAC,Hull\n");
    ob_csv_opts_init(&o);
    CHECK(ob_csv_import_with(p, "Customers", path("utf.csv"), &o) == 2 && o.found_charset == OB_CHARSET_UTF8,
          "utf.csv: %s", ob_error(p));
    got[0] = 0;
    ob_rows(p, "Customers", 0, 10, row_cb, got);
    CHECK(!strcmp(got, "1:1|Jos\xE9|York|-|-|No|-;2:2|Zo\xEB ?|Hull|-|-|No|-;"), "latin-1:\n%s", got);
    /* the same bytes read as ISO-8859-1 when asked */
    ob_csv_opts_init(&o);
    o.charset = OB_CHARSET_LATIN1;
    CHECK(ob_csv_import_with(p, "Customers", path("utf.csv"), &o) == 2, "latin1: %s", ob_error(p));
    got[0] = 0;
    ob_rows(p, "Customers", 2, 1, row_cb, got);
    CHECK(!strcmp(got, "3:3|Jos\xC3\xA9|York|-|-|No|-;"), "kept as bytes:\n%s", got);
    /* plain ISO-8859-1 is not UTF-8 */
    spill("lat.csv", "Name\nJos\xE9\n");
    ob_csv_opts_init(&o);
    CHECK(ob_csv_import_with(p, "Customers", path("lat.csv"), &o) == 1 && o.found_charset == OB_CHARSET_LATIN1, "lat.csv");
    /* export as UTF-8, with its mark */
    ob_csv_opts_init(&o);
    o.charset = OB_CHARSET_UTF8;
    {
        ob_view v;
        view_set(&v, "", 0, "id", OB_FILTER_EQUALS, "1");
        CHECK(ob_csv_export_with(p, "Customers", &v, path("out8.csv"), &o) == 1, "export utf-8: %s", ob_error(p));
    }
    CHECK(!strcmp(slurp("out8.csv"), "\xEF\xBB\xBFid,Name,Town,Email,Joined,Member,Credit\r\n1,Jos\xC3\xA9,York,,,No,\r\n"),
          "out8.csv:\n%s", slurp("out8.csv"));

    /* leave out the bad rows and list them */
    spill("mixed.csv", "Name,Credit\nKay,1\nLou,lots\n,5\nMo,2\n");
    ob_csv_opts_init(&o);
    o.skip_bad = 1;
    CHECK(ob_csv_import_with(p, "Customers", path("mixed.csv"), &o) == 2 && o.bad == 2, "mixed: %s (%d bad)", ob_error(p), o.bad);
    CHECK(!strcmp(o.report, "Line 3: Credit limit must be an amount of money, such as 1,234.50\nLine 4: Name must be filled in\n"),
          "report:\n%s", o.report);
    /* header forced, and wrong */
    ob_csv_opts_init(&o);
    o.header = 1;
    spill("nohead.csv", "Nia,York\n");
    CHECK(ob_csv_import_with(p, "Customers", path("nohead.csv"), &o) == -1, "a header that isn't");
    CHECK_ERR(p, "Line 1: Nia is not a field of Customers");
    /* header 0: a first line of names is data */
    ob_csv_opts_init(&o);
    o.header = 0;
    spill("names.csv", "Name,Town\n");
    CHECK(ob_csv_import_with(p, "Customers", path("names.csv"), &o) == -1, "names as data");
    CHECK_ERR(p, "Line 1: id must be a whole number");

    /* a new table: types guessed */
    spill("shop.csv",
          "id;Product name;Price;In stock;Count;Weight;Added;Opens;Sold at;Notes\r\n"
          "7;Tea;\xA3" "2.50;yes;10;0.25;2026-01-02;09:00;2026-01-02 10:30;\r\n"
          "9;Coffee;3.00;no;1,200;1.5;3/1/2026;9:30;2026-01-03 11:00:05;strong\r\n"
          "12;Cocoa;4;Y;;2;;;;\r\n");
    ob_csv_opts_init(&o);
    CHECK(ob_csv_guess(p, path("shop.csv"), NULL, &o, &g), "guess: %s", ob_error(p));
    CHECK(o.found_sep == ';' && o.found_header == 1 && o.found_charset == OB_CHARSET_LATIN1, "found %c %d %d", o.found_sep, o.found_header, o.found_charset);
    CHECK(!strcmp(g.name, "shop") && g.nfields == 10, "%s, %d fields", g.name, g.nfields);
    if (g.nfields == 10) {
        static const ob_type want[] = { OB_COUNTER, OB_TEXT, OB_CURRENCY, OB_YESNO, OB_INTEGER, OB_NUMBER,
                                        OB_DATE, OB_TIME, OB_DATETIME, OB_TEXT };
        static const char *const names[] = { "id", "Product name", "Price", "In stock", "Count", "Weight",
                                             "Added", "Opens", "Sold at", "Notes" };
        int i;
        for (i = 0; i < 10; i++)
            CHECK(g.fields[i].type == want[i] && !strcmp(g.fields[i].name, names[i]), "field %d: %s %s", i,
                  g.fields[i].name, ob_type_name(g.fields[i].type));
    }
    CHECK(ob_table_create(p, &g), "create shop: %s", ob_error(p));
    CHECK(ob_csv_import_with(p, "shop", path("shop.csv"), &o) == 3, "import shop: %s", ob_error(p));
    got[0] = 0;
    ob_rows(p, "shop", 0, 10, row_cb, got);
    CHECK(!strcmp(got, "7:7|Tea|2.50|Yes|10|0.25|2026-01-02|09:00:00|2026-01-02 10:30:00|-;"
                       "9:9|Coffee|3.00|No|1200|1.5|2026-01-03|09:30:00|2026-01-03 11:00:05|strong;"
                       "12:12|Cocoa|4.00|Yes|-|2.0|-|-|-|-;"), "shop rows (keys kept):\n%s", got);

    /* no header; names made; a long column is Memo; odd header text kept as a label */
    {
        char big[700];
        memset(big, 'x', 300);
        big[300] = 0;
        snprintf(got, sizeof got, "1,%s,1.5\n2,short,2.25\n", big);
        spill("nohdr.csv", got);
    }
    ob_csv_opts_init(&o);
    CHECK(ob_csv_guess(p, path("nohdr.csv"), "No header", &o, &g) && o.found_header == 0, "nohdr: %s", ob_error(p));
    CHECK(g.nfields == 4 && !strcmp(g.fields[1].name, "Field 1") && g.fields[1].type == OB_INTEGER
          && g.fields[2].type == OB_MEMO && g.fields[3].type == OB_NUMBER, "nohdr fields");
    spill("odd.csv", "2nd price \xA3,id,oadb_x,Name,Name\nfoo,bar,1,a,b\n");
    ob_csv_opts_init(&o);
    CHECK(ob_csv_guess(p, path("odd.csv"), NULL, &o, &g), "odd: %s", ob_error(p));
    CHECK(g.nfields == 6 && !strcmp(g.fields[0].name, "Key") && g.fields[0].type == OB_COUNTER, "Key counter: %s", g.fields[0].name);
    CHECK(g.nfields == 6 && !strcmp(g.fields[1].name, "nd price") && !strcmp(g.fields[1].label, "2nd price \xA3")
          && !strcmp(g.fields[3].name, "F oadb_x") && !strcmp(g.fields[5].name, "Name 2"),
          "names: %s|%s|%s|%s", g.fields[1].name, g.fields[2].name, g.fields[3].name, g.fields[5].name);
    CHECK(ob_table_create(p, &g), "create odd: %s", ob_error(p));
    CHECK(ob_csv_import_with(p, "odd", path("odd.csv"), &o) == 1, "import odd by labels: %s", ob_error(p));
    spill("empty.csv", "");
    CHECK(!ob_csv_guess(p, path("empty.csv"), NULL, &o, &g), "empty");
    CHECK_ERR(p, "is empty");
    ob_close(p);
}

/* ---- Forms (core 0.6) ---- */

static void shop_orders(ob_project *p);

static void test_forms(void)
{
    ob_project *p = fresh("forms.oadb");
    ob_view v, kids;
    ob_relation r[4];
    char key[24];
    if (!p) return;
    five_customers(p);
    shop_orders(p);

    /* positions in key order and in a sorted, filtered view */
    CHECK(ob_view_position(p, "Customers", NULL, 1) == 0 && ob_view_position(p, "Customers", NULL, 5) == 4,
          "key order positions");
    CHECK(ob_view_position(p, "Customers", NULL, 99) == -1, "no such row");
    view_set(&v, "Name", 0, "", OB_FILTER_NONE, "");
    CHECK(ob_view_position(p, "Customers", &v, 4) == 1, "bob is second by name: %lld",
          (long long)ob_view_position(p, "Customers", &v, 4));
    view_set(&v, "Name", 1, "Town", OB_FILTER_EQUALS, "leeds");
    CHECK(ob_view_position(p, "Customers", &v, 2) == 1 && ob_view_position(p, "Customers", &v, 5) == 0,
          "Leeds Z to A: Eve, Ann");
    CHECK(ob_view_position(p, "Customers", &v, 1) == -1, "dan is filtered out");
    CHECK(ob_view_position(p, "Nobody", NULL, 1) == -2, "no table");

    /* find: text contains, any case; other fields equal */
    view_set(&v, "Name", 0, "", OB_FILTER_NONE, "");     /* Ann bob Cy dan Eve */
    CHECK(ob_view_find(p, "Customers", &v, "Email", "EXAMPLE", 0, 0) == 1, "first email: bob");
    CHECK(ob_view_find(p, "Customers", &v, "Email", "example", 2, 0) == 2, "from 2: Cy");
    CHECK(ob_view_find(p, "Customers", &v, "Email", "example", 4, 1) == 4, "backward from 4: Eve");
    CHECK(ob_view_find(p, "Customers", &v, "Email", "100%_", 0, 0) == 1, "wildcards are plain: %lld",
          (long long)ob_view_find(p, "Customers", &v, "Email", "100%_", 0, 0));
    CHECK(ob_view_find(p, "Customers", &v, "Email", "1%0", 0, 0) == -1, "a %% is a %%");
    CHECK(ob_view_find(p, "Customers", &v, "Name", "zz", 0, 0) == -1, "none");
    CHECK(ob_view_find(p, "Customers", &v, "Credit", "250.00", 0, 0) == 3, "money equals: dan");
    CHECK(ob_view_find(p, "Customers", &v, "Credit", "250", 4, 0) == -1, "none after dan");
    CHECK(ob_view_find(p, "Customers", &v, "Member", "yes", 0, 0) == 2, "Yes/No: Cy");
    CHECK(ob_view_find(p, "Customers", &v, "Credit", "lots", 0, 0) == -2, "money that isn't");
    CHECK(ob_view_find(p, "Customers", &v, "Name", "", 0, 0) == -2, "nothing to find");
    CHECK_ERR(p, "Type what to find");
    CHECK(ob_view_find(p, "Customers", &v, "Age", "1", 0, 0) == -2, "no field");
    CHECK_ERR(p, "has no field Age");
    view_set(&v, "", 0, "Town", OB_FILTER_EQUALS, "York");   /* dan bob */
    CHECK(ob_view_find(p, "Customers", &v, "Name", "b", 0, 0) == 1, "inside a filter: bob");
    CHECK(ob_view_find(p, "Customers", &v, "Name", "Ann", 0, 0) == -1, "Ann is filtered out");

    /* subforms */
    CHECK(ob_children(p, "Customers", r, 4) == 1 && !strcmp(r[0].table, "Orders") && !strcmp(r[0].field, "Customer"),
          "Customers' child: Orders");
    CHECK(ob_children(p, "Orders", r, 4) == 0, "Orders has none");
    CHECK(ob_children(p, "Nobody", r, 4) == -1, "no table");
    snprintf(key, sizeof key, "%d", 1);
    view_set(&kids, "", 0, r[0].field, OB_FILTER_EQUALS, key);
    CHECK(ob_view_total(p, "Orders", &kids) == 2, "dan's orders: %lld", (long long)ob_view_total(p, "Orders", &kids));
    view_set(&kids, "", 0, "Customer", OB_FILTER_EQUALS, "4");
    CHECK(ob_view_total(p, "Orders", &kids) == 0, "bob has none");

    /* changing a subform's rows (core 0.6.3) */
    {
        const char *f[] = { "Total", "Customer" }, *v[] = { "3.00", "5" };
        const char *cf[] = { "Total" }, *cv[] = { "4.50" };
        char got[1024];
        ob_int k = 0;
        ob_relation none;
        CHECK(ob_child_key(p, &r[0], 1, 0) == 1 && ob_child_key(p, &r[0], 1, 1) == 2, "dan's orders are keys 1, 2");
        CHECK(ob_child_key(p, &r[0], 1, 2) == -1, "dan has no third");
        CHECK(ob_child_key(p, &r[0], 4, 0) == -1, "bob has none");
        CHECK(ob_child_key(p, &r[0], -1, 0) == -1 && ob_child_key(p, &r[0], 1, -1) == -1, "a new record has none");
        memset(&none, 0, sizeof none);
        CHECK(ob_child_key(p, &none, 1, 0) == -2, "no subform");
        snprintf(none.table, sizeof none.table, "Nobody");
        snprintf(none.field, sizeof none.field, "Customer");
        CHECK(ob_child_key(p, &none, 1, 0) == -2, "no table");

        /* add: the link is the record's, whatever was typed for it */
        CHECK(ob_child_add(p, &r[0], 4, 2, f, v, &k) && k == 6, "an order for bob: %s", ob_error(p));
        CHECK(ob_child_key(p, &r[0], 4, 0) == 6 && ob_child_key(p, &r[0], 5, 1) == -1, "bob's, not Eve's");
        view_set(&kids, "", 0, "Customer", OB_FILTER_EQUALS, "4");
        CHECK(ob_view_total(p, "Orders", &kids) == 1, "bob has one");
        got[0] = 0;
        ob_rows(p, "Orders", 5, 1, row_cb, got);
        CHECK(strstr(got, "6:6|4|3.00|") != NULL, "the row: %s", got);
        CHECK(ob_child_add(p, &r[0], 4, 1, cf, cv, NULL), "no key wanted: %s", ob_error(p));
        CHECK(ob_child_key(p, &r[0], 4, 1) == 7, "bob's second");
        CHECK(!ob_child_add(p, &r[0], -1, 1, cf, cv, &k), "a new record");
        CHECK_ERR(p, "Save the record first");
        CHECK(!ob_child_add(p, &r[0], 99, 1, cf, cv, &k), "no such customer");
        CHECK(ob_child_key(p, &r[0], 99, 0) == -1, "nothing was added for 99");
        {
            const char *bf[] = { "Total" }, *bv[] = { "lots" };
            CHECK(!ob_child_add(p, &r[0], 4, 1, bf, bv, &k), "money that isn't");
            CHECK(ob_child_key(p, &r[0], 4, 2) == -1, "bob still has two");
        }
        CHECK(!ob_child_add(p, &none, 1, 1, cf, cv, &k), "no table");
        memset(&none, 0, sizeof none);
        CHECK(!ob_child_add(p, &none, 1, 1, cf, cv, &k), "no subform");
        CHECK_ERR(p, "No subform");

        /* change and delete go by the key ob_child_key gives */
        k = ob_child_key(p, &r[0], 4, 0);
        CHECK(ob_row_change(p, "Orders", k, 1, cf, cv), "change: %s", ob_error(p));
        got[0] = 0;
        ob_rows(p, "Orders", k - 1, 1, row_cb, got);
        CHECK(strstr(got, "|4|4.50|") != NULL, "changed: %s", got);
        CHECK(ob_row_delete(p, "Orders", ob_child_key(p, &r[0], 4, 1)), "delete: %s", ob_error(p));
        CHECK(ob_child_key(p, &r[0], 4, 0) == 6 && ob_child_key(p, &r[0], 4, 1) == -1, "bob has one left");

        /* a subform row opened in its table's form: the record's rows (core 0.6.4) */
        ob_child_view(&kids, &r[0], 1);
        CHECK(ob_view_filtered(&kids) && !kids.sort[0], "filtered, key order");
        CHECK(ob_view_total(p, "Orders", &kids) == 2, "dan's two orders");
        CHECK(ob_view_position(p, "Orders", &kids, 1) == 0 && ob_view_position(p, "Orders", &kids, 2) == 1,
              "Tea first, then the second");
        CHECK(ob_view_position(p, "Orders", &kids, 3) == -1, "Ann's order is not dan's");
        CHECK(ob_view_position(p, "Orders", &kids, ob_child_key(p, &r[0], 1, 1)) == 1, "the key a place gives is at that place");
        ob_child_view(&kids, &r[0], 4);
        CHECK(ob_view_total(p, "Orders", &kids) == 1 && ob_view_position(p, "Orders", &kids, 6) == 0, "bob's one");
        CHECK(ob_view_check(p, "Orders", &kids), "a view Orders takes: %s", ob_error(p));
    }
    ob_close(p);
}

/* ---- Queries (core 0.5) ---- */

static int qrow_cb(void *u, ob_int key, int n, const char *const *v)
{
    char *out = u;
    int i;
    size_t len = strlen(out);
    (void)key;
    if (len) snprintf(out + len, 1024 - len, ";");
    for (i = 0; i < n; i++) {
        len = strlen(out);
        snprintf(out + len, 1024 - len, "%s%s", i ? "|" : "", v[i] ? v[i] : "");
    }
    return 0;
}

static const char *qrows(ob_project *p, const ob_query *q, const char *const *params, ob_int first, int max)
{
    static char got[1024];
    got[0] = 0;
    if (ob_query_rows(p, q, params, first, max, qrow_cb, got) < 0) snprintf(got, sizeof got, "error: %s", ob_error(p));
    return got;
}

static const char *qsql(ob_project *p, const ob_query *q)
{
    static char got[OB_SQL_MAX];
    if (ob_query_sql(p, q, got, sizeof got) < 0) snprintf(got, sizeof got, "error: %s", ob_error(p));
    return got;
}

static void shop_orders(ob_project *p)
{
    static ob_table o;
    const char *f[] = { "Customer", "Total", "Weight" };
    static const char *const rows[][3] = {
        { "1", "10.00", "1.5" }, { "1", "2.50", NULL }, { "2", "100", "20" }, { "3", "7.25", NULL }, { "5", "1", "0.25" },
    };
    unsigned i;
    orders(&o, OB_DELETE_REFUSE);
    CHECK(ob_table_create(p, &o), "Orders: %s", ob_error(p));
    for (i = 0; i < sizeof rows / sizeof rows[0]; i++)
        CHECK(ob_row_add(p, "Orders", 3, f, rows[i], NULL), "order %u: %s", i, ob_error(p));
}

static void copy_s(char *to, size_t n, const char *from)
{
    snprintf(to, n, "%s", from);
}

/* Buttons and events on a designed form: kept, checked, and following the
 * renames of the forms and queries they name. */
static void test_form_buttons(void)
{
    ob_project *p = fresh("fbuttons.oadb");
    static ob_form f, g;
    static ob_query q;
    ob_fitem *it;
    int i;
    if (!p) return;
    five_customers(p);

    CHECK(!strcmp(ob_faction_name(OB_FA_NEXT), "Next Record") && !strcmp(ob_faction_name(OB_FA_COUNT), "")
          && !strcmp(ob_faction_name(OB_FA_NONE), ""), "action names");
    CHECK(ob_faction_arg(OB_FA_OPENFORM) == 1 && ob_faction_arg(OB_FA_RUNQUERY) == 2 && ob_faction_arg(OB_FA_AREXX) == 3
          && ob_faction_arg(OB_FA_SAVE) == 0, "what an action names");
    CHECK(!strcmp(ob_fevent_name(OB_FE_BEFORE_SAVE), "Before Save") && !strcmp(ob_fevent_name(OB_FE_COUNT), ""), "event names");

    ob_form_init(&f, "Card", "Customers");
    it = ob_form_add_button(&f, "Next", OB_FA_NEXT, NULL, 2, 10);
    CHECK(it && it->kind == OB_FI_BUTTON && it->w == 12 && it->h == 1 && !strcmp(it->text, "Next") && !it->arg[0], "a button, 8 + 4 wide");
    it = ob_form_add_button(&f, "Who lives in York?", OB_FA_RUNQUERY, "York people", 16, 10);
    CHECK(it && it->w == 22 && !strcmp(it->arg, "York people"), "as wide as its words");
    CHECK(ob_form_add_button(&f, "x", OB_FA_COUNT, NULL, 0, 0) == NULL, "no such action");
    ob_form_add_button(&f, "Orders", OB_FA_OPENFORM, "Card", 40, 10);
    ob_form_add_button(&f, "Tell", OB_FA_AREXX, "'say hello \"tab\there\"'", 60, 10);
    copy_s(f.events[OB_FE_OPEN], sizeof f.events[OB_FE_OPEN], "REXX:open.rexx");
    copy_s(f.events[OB_FE_BEFORE_SAVE], sizeof f.events[OB_FE_BEFORE_SAVE], "'if 1 then exit 5'");
    CHECK(ob_form_check(p, &f), "checks: %s", ob_error(p));
    CHECK(ob_form_save(p, &f) && ob_form_get(p, "Card", &g), "kept: %s", ob_error(p));
    CHECK(g.nitems == 4 && g.items[1].action == OB_FA_RUNQUERY && !strcmp(g.items[1].arg, "York people") && g.items[1].w == 22,
          "read back: %d items", g.nitems);
    CHECK(!strcmp(g.items[3].arg, "'say hello \"tab\there\"'") && g.items[0].action == OB_FA_NEXT, "a macro with a tab in it");
    CHECK(!strcmp(g.events[OB_FE_OPEN], "REXX:open.rexx") && !strcmp(g.events[OB_FE_BEFORE_SAVE], "'if 1 then exit 5'")
          && !g.events[OB_FE_CURRENT][0] && !g.events[OB_FE_CLOSE][0], "events read back");

    /* refused */
    g.items[0].action = OB_FA_NONE;
    CHECK(!ob_form_check(p, &g) && strstr(ob_error(p), "Next does nothing"), "a button with no action: %s", ob_error(p));
    g.items[0].action = OB_FA_NEXT;
    g.items[1].arg[0] = 0;
    CHECK(!ob_form_check(p, &g) && strstr(ob_error(p), "needs a query to run"), "Run Query, no query: %s", ob_error(p));
    copy_s(g.items[1].arg, sizeof g.items[1].arg, "York people");
    g.items[2].arg[0] = 0;
    CHECK(!ob_form_check(p, &g) && strstr(ob_error(p), "needs a form to open"), "Open Form, no form: %s", ob_error(p));
    g.items[2].text[0] = 0;
    g.items[2].arg[0] = 0;
    CHECK(!ob_form_check(p, &g) && strstr(ob_error(p), "the button a button needs"), "no caption: %s", ob_error(p));

    /* a form or query renamed: its buttons follow */
    ob_query_init(&q, "York people");
    ob_query_add_table(&q, "Customers");
    ob_query_add_column(&q, "Customers", "Name");
    CHECK(ob_query_save(p, &q), "the query: %s", ob_error(p));
    CHECK(ob_query_rename(p, "york people", "Yorkers"), "query renamed: %s", ob_error(p));
    CHECK(ob_form_get(p, "Card", &g) && !strcmp(g.items[1].arg, "Yorkers"), "the button runs Yorkers: %s", g.items[1].arg);
    CHECK(ob_form_rename(p, "card", "Person"), "form renamed: %s", ob_error(p));
    CHECK(ob_form_get(p, "Person", &g) && !strcmp(g.items[2].arg, "Person") && !strcmp(g.items[1].arg, "Yorkers"), "its own button follows");
    CHECK(!strcmp(g.items[3].arg, "'say hello \"tab\there\"'"), "a macro left alone");
    ob_form_init(&f, "Other", "Customers");
    ob_form_add_button(&f, "Go", OB_FA_OPENFORM, "Person", 0, 0);
    ob_form_add_button(&f, "Not this", OB_FA_RUNQUERY, "Person", 0, 1);
    CHECK(ob_form_save(p, &f) && ob_form_rename(p, "Person", "Card"), "again: %s", ob_error(p));
    CHECK(ob_form_get(p, "Other", &g) && !strcmp(g.items[0].arg, "Card") && !strcmp(g.items[1].arg, "Person"),
          "another form's Open Form follows, a Run Query of the same name doesn't");
    CHECK(!ob_form_rename(p, "Other", "Card") && ob_form_get(p, "Other", &g) && !strcmp(g.items[0].arg, "Card"), "a refused rename changes nothing");

    /* an older form's text (no buttons, no events) still reads, and a word from a newer one is passed over */
    ob_form_init(&f, "Plain", "Customers");
    ob_form_add(&f, OB_FI_LABEL, 0, 0, 4, 1);
    for (i = 0; i < OB_FE_COUNT; i++) f.events[i][0] = 0;
    CHECK(ob_form_save(p, &f) && ob_form_get(p, "Plain", &g) && g.nitems == 1 && !g.events[OB_FE_OPEN][0], "a plain form");
    ob_close(p);
    {
        sqlite3 *db;
        CHECK(sqlite3_open(path("fbuttons.oadb"), &db) == SQLITE_OK
              && sqlite3_exec(db, "INSERT INTO oadb_forms VALUES('Newer', 'Customers', 'oadb form 1\n"
                                  "button\t\tGo\t0\t0\t6\t1\topenform\tCard\tlater\n"
                                  "glow\tsoft\t3\nevent\tsparkle\tx.rexx\nevent\topen\tgo.rexx\n');"
                                  "INSERT INTO oadb_forms VALUES('Later', 'Customers', 'oadb form 2\n"
                                  "button\t\tGo\t0\t0\t6\t1\topenform\tCard\n')", NULL, NULL, NULL) == SQLITE_OK,
              "newer words written by hand");
        sqlite3_close(db);
    }
    {
        char err[OB_ERR_MAX];
        p = ob_open(path("fbuttons.oadb"), 0, err, sizeof err);
        CHECK(p != NULL, "reopened: %s", err);
        if (!p) return;
    }
    CHECK(ob_form_get(p, "Newer", &g) && g.nitems == 1 && g.items[0].kind == OB_FI_BUTTON && g.items[0].action == OB_FA_OPENFORM
          && !strcmp(g.items[0].arg, "Card") && !strcmp(g.events[OB_FE_OPEN], "go.rexx") && !g.events[OB_FE_CURRENT][0],
          "a newer GFOpenBase's words (glow, a sparkle event, a word after a button) passed over: %d", g.nitems);
    CHECK(!ob_form_get(p, "Later", &g), "an oadb form 2 can't be read");
    /* a form it can't read doesn't stop renames; it is left as it is */
    CHECK(ob_form_rename(p, "Card", "Card2"), "a rename all the same: %s", ob_error(p));
    CHECK(ob_form_get(p, "Newer", &g) && !strcmp(g.items[0].arg, "Card2") && ob_form_get(p, "Other", &g) && !strcmp(g.items[0].arg, "Card2"),
          "the buttons it can read follow");
    ob_close(p);
}

/* What the designer window does to a form: room for a new item, cursor
 * keys, the fields still to place, a field put on. */
static void test_form_editing(void)
{
    ob_project *p = fresh("fedit.oadb");
    static ob_form f;
    static char nm[8][OB_NAME_MAX];
    ob_fitem *it;
    int i, k;
    if (!p) return;
    five_customers(p);

    ob_form_init(&f, "Card", "Customers");
    CHECK(ob_form_free_row(&f) == 0, "empty: row 0");
    ob_form_add(&f, OB_FI_BOX, 0, 0, 30, 5);
    ob_form_add(&f, OB_FI_LABEL, 40, 7, 5, 1);
    CHECK(ob_form_free_row(&f) == 8, "under the label: %d", ob_form_free_row(&f));
    ob_form_add(&f, OB_FI_LINE, 0, OB_FORM_ROWS - 1, 10, 1);
    CHECK(ob_form_free_row(&f) == -1, "full to the bottom");
    ob_form_remove(&f, 2);

    /* cursor keys: move keeps the size and stops at the edge */
    CHECK(ob_form_nudge(&f, 1, 1, -1, 0, 0) && f.items[1].x == 41 && f.items[1].y == 6, "moved: %d,%d", f.items[1].x, f.items[1].y);
    CHECK(ob_form_nudge(&f, 1, 500, 500, 0, 0) && f.items[1].x == OB_FORM_COLS - 5 && f.items[1].y == OB_FORM_ROWS - 1 && f.items[1].w == 5,
          "to the corner, still 5 wide: %d,%d w%d", f.items[1].x, f.items[1].y, f.items[1].w);
    CHECK(ob_form_nudge(&f, 1, -999, -999, 0, 0) && f.items[1].x == 0 && f.items[1].y == 0, "to the top left");
    CHECK(ob_form_nudge(&f, 0, 0, 0, 2, -1) && f.items[0].w == 32 && f.items[0].h == 4, "sized: %dx%d", f.items[0].w, f.items[0].h);
    CHECK(ob_form_nudge(&f, 0, 0, 0, -99, -99) && f.items[0].w == 1 && f.items[0].h == 1, "no smaller than a cell");
    ob_form_place(&f, 0, OB_FORM_COLS - 3, 0, 3, 1);
    CHECK(ob_form_nudge(&f, 0, 0, 0, 10, 0) && f.items[0].w == 3, "no wider than the grid: %d", f.items[0].w);
    CHECK(!ob_form_nudge(&f, 9, 1, 0, 0, 0) && !ob_form_nudge(&f, -1, 1, 0, 0, 0), "no such item");
    it = ob_form_add(&f, OB_FI_LINE, 2, 10, 20, 1);
    k = f.nitems - 1;
    CHECK(it && ob_form_nudge(&f, k, 0, 0, 0, 3) && f.items[k].h == 1 && f.items[k].w == 20, "an across line stays thin");
    ob_form_place(&f, k, 2, 10, 1, 1);
    CHECK(ob_form_nudge(&f, k, 0, 0, 0, 5) && f.items[k].h == 6 && f.items[k].w == 1, "a dot grows down");
    CHECK(ob_form_nudge(&f, k, 0, 0, 4, 0) && f.items[k].w == 1 && f.items[k].h == 6, "then stays a down line");

    /* the fields still to place, then one put on */
    CHECK(ob_form_unplaced(p, &f, nm, 8) == 7 && !strcmp(nm[0], "id") && !strcmp(nm[1], "Name") && !strcmp(nm[6], "Credit"), "all seven with the key, in order");
    i = ob_form_add_field(p, &f, "credit", 4, 20);
    CHECK(i == f.nitems - 1 && f.items[i].kind == OB_FI_FIELD && !strcmp(f.items[i].field, "Credit"), "Credit on: %s", ob_error(p));
    CHECK(f.items[i - 1].kind == OB_FI_LABEL && !strcmp(f.items[i - 1].text, "Credit limit") && f.items[i - 1].x == 4 && f.items[i - 1].w == 12,
          "its label first: %s w%d", f.items[i - 1].text, f.items[i - 1].w);
    CHECK(f.items[i].x == 18 && f.items[i].y == 20 && f.items[i].w == 40 && f.items[i].h == 1, "gadget after it: %d,%d", f.items[i].x, f.items[i].y);
    CHECK(ob_form_unplaced(p, &f, nm, 2) == 6 && !strcmp(nm[0], "id") && !strcmp(nm[1], "Name"), "six left, the first two kept");
    k = f.nitems;
    CHECK(ob_form_add_field(p, &f, "Credit", 0, 30) == -1 && strstr(ob_error(p), "already") && f.nitems == k, "not twice: %s", ob_error(p));
    CHECK(ob_form_add_field(p, &f, "Phone", 0, 30) == -1 && strstr(ob_error(p), "no field Phone"), "no such field: %s", ob_error(p));
    CHECK(ob_form_add_field(p, &f, "Name", OB_FORM_COLS - 4, 30) == -1 && strstr(ob_error(p), "no room") && f.nitems == k, "no room across: %s", ob_error(p));
    CHECK(ob_form_add_field(p, &f, "Name", 0, OB_FORM_ROWS) == -1 && strstr(ob_error(p), "no room"), "no room down");
    CHECK(ob_form_add_field(p, &f, "Name", 0, ob_form_free_row(&f)) >= 0, "Name under the rest: %s", ob_error(p));
    CHECK(ob_form_check(p, &f) && ob_form_save(p, &f), "it saves: %s", ob_error(p));
    copy_s(f.table, sizeof f.table, "Nowhere");
    CHECK(ob_form_unplaced(p, &f, nm, 8) == -1, "no table");
    while (f.nitems < OB_FORM_ITEMS - 1) ob_form_add(&f, OB_FI_LABEL, 0, 0, 1, 1);
    copy_s(f.table, sizeof f.table, "Customers");
    CHECK(ob_form_add_field(p, &f, "Town", 0, 50) == -1 && strstr(ob_error(p), "no room"), "a full form: %s", ob_error(p));
    ob_close(p);
}

static void test_designed_forms(void)
{
    ob_project *p = fresh("dforms.oadb");
    static ob_form f, g;
    ob_fitem *it;
    int i;
    if (!p) return;
    five_customers(p);
    shop_orders(p);

    /* the model: items kept on the grid */
    ob_form_init(&f, "Card", "Customers");
    it = ob_form_add(&f, OB_FI_LABEL, 2, 1, 10, 1);
    CHECK(it && f.nitems == 1 && it->x == 2 && it->w == 10, "a label");
    copy_s(it->text, sizeof it->text, "Name:");
    it = ob_form_add(&f, OB_FI_FIELD, 14, 1, 500, 0);
    CHECK(it && it->w == OB_FORM_COLS - 14 && it->h == 1, "kept inside: w %d h %d", it ? it->w : -1, it ? it->h : -1);
    copy_s(it->field, sizeof it->field, "Name");
    CHECK(!ob_form_add(&f, OB_FI_LABEL, -1, 0, 1, 1) && !ob_form_add(&f, OB_FI_LABEL, 0, OB_FORM_ROWS, 1, 1), "off the grid");
    CHECK(!ob_form_add(&f, OB_FI_KINDS, 0, 0, 1, 1), "no such kind");
    it = ob_form_add(&f, OB_FI_BOX, 0, 0, 40, 5);
    CHECK(it != NULL, "a box");
    CHECK(ob_form_at(&f, 3, 1) == 0, "the label, not the box round it: %d", ob_form_at(&f, 3, 1));
    CHECK(ob_form_at(&f, 0, 2) == 2 && ob_form_at(&f, 39, 4) == 2, "the box by its edge");
    CHECK(ob_form_at(&f, 5, 3) == -1, "inside the box, nothing");
    CHECK(ob_form_at(&f, 100, 1) == 1, "the field");
    CHECK(ob_form_place(&f, 0, OB_FORM_COLS - 2, 0, 10, 1) && f.items[0].x == OB_FORM_COLS - 2 && f.items[0].w == 2,
          "placed and kept inside");
    CHECK(!ob_form_place(&f, 9, 0, 0, 1, 1), "no item 9");
    CHECK(ob_form_remove(&f, 2) && f.nitems == 2 && !ob_form_remove(&f, 2), "box removed");
    for (i = f.nitems; i < OB_FORM_ITEMS; i++) ob_form_add(&f, OB_FI_LABEL, 0, 0, 1, 1);
    CHECK(f.nitems == OB_FORM_ITEMS && !ob_form_add(&f, OB_FI_LABEL, 0, 0, 1, 1), "full at %d", OB_FORM_ITEMS);

    /* the auto form as a designed one */
    CHECK(ob_form_auto(p, "Customers", NULL, &f), "auto: %s", ob_error(p));
    CHECK(!strcmp(f.name, "Customers") && !strcmp(f.table, "Customers"), "named for its table");
    CHECK(f.nitems == 14, "7 fields, 14 items: %d", f.nitems);
    CHECK(f.items[0].kind == OB_FI_LABEL && !strcmp(f.items[0].text, "id") && f.items[1].kind == OB_FI_FIELD
          && !strcmp(f.items[1].field, "id"), "id first");
    CHECK(f.items[13].y == 6 && f.items[13].x == f.items[12].w + 2 && f.items[13].w == 40, "a row each: y %d x %d",
          f.items[13].y, f.items[13].x);
    CHECK(!strcmp(f.items[12].text, "Credit limit"), "a label is the field's label: %s", f.items[12].text);
    CHECK(ob_form_check(p, &f), "it checks: %s", ob_error(p));
    CHECK(!ob_form_auto(p, "Nobody", NULL, &f), "no table");

    /* kept in oadb_forms */
    CHECK(ob_form_count(p) == 0 && !ob_form_name(p, 0), "none yet");
    ob_form_auto(p, "Customers", "Customer card", &f);
    it = ob_form_add(&f, OB_FI_LINE, 0, 8, 60, 1);
    it = ob_form_add(&f, OB_FI_BOX, 0, 9, 30, 4);
    copy_s(it->text, sizeof it->text, "Notes\tand \\ things\nhere");
    CHECK(ob_form_save(p, &f), "save: %s", ob_error(p));
    CHECK(ob_form_get(p, "customer CARD", &g), "get any case: %s", ob_error(p));
    CHECK(!strcmp(g.name, "Customer card") && !strcmp(g.table, "Customers") && g.nitems == 16, "back: %d items", g.nitems);
    CHECK(!memcmp(&g.items[0], &f.items[0], sizeof f.items[0]) && !strcmp(g.items[15].text, f.items[15].text)
          && g.items[15].h == 4 && g.items[14].kind == OB_FI_LINE, "the same items");
    ob_form_auto(p, "Orders", NULL, &g);
    CHECK(ob_form_save(p, &g), "a form named as its table: %s", ob_error(p));
    CHECK(ob_form_count(p) == 2 && !strcmp(ob_form_name(p, 0), "Customer card") && !strcmp(ob_form_name(p, 1), "Orders"),
          "two, in name order");
    g.nitems = 1;
    CHECK(ob_form_save(p, &g) && ob_form_get(p, "Orders", &g) && g.nitems == 1 && ob_form_count(p) == 2, "replaced");

    /* what can't be saved */
    f.items[1].field[0] = 'X';
    CHECK(!ob_form_save(p, &f), "a field that isn't");
    CHECK_ERR(p, "has no field Xd");
    f.items[1].field[0] = 'i';
    copy_s(f.table, sizeof f.table, "Nobody");
    CHECK(!ob_form_save(p, &f), "no table");
    CHECK_ERR(p, "there is no table Nobody");
    copy_s(f.table, sizeof f.table, "Customers");
    f.items[14].h = 2;
    CHECK(!ob_form_save(p, &f), "a fat line");
    CHECK_ERR(p, "one cell thick");
    f.items[14].h = 1;
    copy_s(f.name, sizeof f.name, "oadb_x");
    CHECK(!ob_form_save(p, &f), "a kept name");
    copy_s(f.name, sizeof f.name, "Customer card");

    /* rename, delete */
    CHECK(ob_form_rename(p, "Customer card", "Card") && ob_form_get(p, "Card", &g), "renamed: %s", ob_error(p));
    CHECK(!ob_form_rename(p, "Card", "orders"), "a name taken");
    CHECK_ERR(p, "There is a form named orders already");
    CHECK(ob_form_rename(p, "Card", "CARD"), "case only: %s", ob_error(p));
    CHECK(!ob_form_rename(p, "Nope", "X"), "no form");
    CHECK(ob_form_delete(p, "card") && ob_form_count(p) == 1 && !ob_form_get(p, "Card", &g), "deleted");
    CHECK_ERR(p, "There is no form named Card");
    CHECK(!ob_form_delete(p, "Card"), "gone");

    /* the cells a form uses; the forms on a table */
    {
        static char nm[4][OB_NAME_MAX];
        int c = -1, r = -1;
        ob_form_init(&g, "E", "Orders");
        ob_form_extent(&g, &c, &r);
        CHECK(c == 0 && r == 0, "empty: %d x %d", c, r);
        ob_form_add(&g, OB_FI_LABEL, 3, 2, 10, 1);
        ob_form_add(&g, OB_FI_BOX, 1, 5, 4, 6);
        ob_form_extent(&g, &c, NULL);
        ob_form_extent(&g, NULL, &r);
        CHECK(c == 13 && r == 11, "13 x 11: %d x %d", c, r);
        CHECK(ob_forms_of(p, "Customers", nm, 4) == 0, "no forms on Customers yet");
        ob_form_auto(p, "Customers", "Zed", &g); ob_form_save(p, &g);
        ob_form_auto(p, "Customers", "alpha", &g); ob_form_save(p, &g);
        CHECK(ob_forms_of(p, "customers", nm, 4) == 2 && !strcmp(nm[0], "alpha") && !strcmp(nm[1], "Zed"), "two, in name order");
        CHECK(ob_forms_of(p, "Customers", nm, 1) == 2 && !strcmp(nm[0], "alpha"), "how many, the first kept");
        CHECK(ob_forms_of(p, "Orders", nm, 4) == 1 && !strcmp(nm[0], "Orders"), "Orders' own");
        ob_form_delete(p, "Zed");
        ob_form_delete(p, "alpha");
    }

    /* forms follow their table: a rename, a design change's renames; drops refused */
    {
        static ob_table t;
        CHECK(ob_form_auto(p, "Customers", "Card", &f) && ob_form_save(p, &f), "Card: %s", ob_error(p));
        CHECK(ob_table_rename(p, "customers", "Clients"), "rename: %s", ob_error(p));
        CHECK(ob_form_get(p, "Card", &g) && !strcmp(g.table, "Clients") && ob_form_check(p, &g), "follows the rename: %s %s",
              g.table, ob_error(p));
        CHECK(ob_form_get(p, "Orders", &g) && !strcmp(g.table, "Orders"), "the Orders form is left alone");
        /* a design change renames Email to Mail: the form's item follows */
        CHECK(ob_table_get(p, "Clients", &t), "%s", ob_error(p));
        for (i = 0; i < t.nfields; i++) copy_s(t.fields[i].from, sizeof t.fields[i].from, t.fields[i].name);
        i = ob_table_find_field(&t, "Email");
        copy_s(t.fields[i].name, sizeof t.fields[i].name, "Mail");
        CHECK(ob_table_change(p, "Clients", &t), "change: %s", ob_error(p));
        CHECK(ob_form_get(p, "Card", &g) && !strcmp(g.table, "Clients") && ob_form_check(p, &g), "follows the change: %s",
              ob_error(p));
        for (i = 0; i < g.nitems && !(g.items[i].kind == OB_FI_FIELD && !strcmp(g.items[i].field, "Mail")); i++) ;
        CHECK(i < g.nitems && !strcmp(g.items[i - 1].text, "Email"), "the item is Mail, its label kept as typed");
        /* drop the field the form shows: refused, nothing changed */
        ob_table_get(p, "Clients", &t);
        for (i = 0; i < t.nfields; i++) copy_s(t.fields[i].from, sizeof t.fields[i].from, t.fields[i].name);
        ob_table_remove_field(&t, ob_table_find_field(&t, "Mail"));
        CHECK(!ob_table_change(p, "Clients", &t), "a field the form shows");
        CHECK_ERR(p, "The form Card shows Mail: take it off the form first");
        CHECK(ob_table_get(p, "Clients", &t) && ob_table_find_field(&t, "Mail") >= 0, "Mail is still there");
        /* off the form, then it can go */
        ob_form_get(p, "Card", &g);
        for (i = 0; i < g.nitems && !(g.items[i].kind == OB_FI_FIELD && !strcmp(g.items[i].field, "Mail")); i++) ;
        ob_form_remove(&g, i);
        CHECK(ob_form_save(p, &g), "%s", ob_error(p));
        for (i = 0; i < t.nfields; i++) copy_s(t.fields[i].from, sizeof t.fields[i].from, t.fields[i].name);
        ob_table_remove_field(&t, ob_table_find_field(&t, "Mail"));
        CHECK(ob_table_change(p, "Clients", &t), "now it goes: %s", ob_error(p));
        /* a design change that renames the table (Orders, which nothing looks up) and a field */
        ob_form_auto(p, "Orders", NULL, &g);
        CHECK(ob_form_save(p, &g), "%s", ob_error(p));
        ob_table_get(p, "Orders", &t);
        for (i = 0; i < t.nfields; i++) copy_s(t.fields[i].from, sizeof t.fields[i].from, t.fields[i].name);
        copy_s(t.name, sizeof t.name, "Sales");
        copy_s(t.fields[ob_table_find_field(&t, "Total")].name, sizeof t.fields[0].name, "Amount");
        CHECK(ob_table_change(p, "Orders", &t), "Orders to Sales: %s", ob_error(p));
        CHECK(ob_form_get(p, "Orders", &g) && !strcmp(g.table, "Sales") && ob_form_check(p, &g), "the form keeps its name, on Sales: %s",
              ob_error(p));
        for (i = 0; i < g.nitems && strcmp(g.items[i].field, "Amount"); i++) ;
        CHECK(i < g.nitems, "Total is Amount on the form");
        /* a table a form shows can't be dropped */
        CHECK(!ob_table_drop(p, "Sales"), "Sales has a form");
        CHECK_ERR(p, "The form Orders shows Sales: delete the form first");
        CHECK(ob_form_delete(p, "Orders"), "the form deleted");
        CHECK(ob_table_drop(p, "Sales"), "then it drops: %s", ob_error(p));
    }

    /* a newer GFOpenBase's word is passed over; a damaged form says so */
    ob_close(p);
    {
        sqlite3 *db;
        CHECK(sqlite3_open(path("dforms.oadb"), &db) == SQLITE_OK
              && sqlite3_exec(db, "INSERT INTO oadb_forms VALUES('New', 'Orders', 'oadb form 1\nfield\tTotal\t\t1\t2\t3\t1\n"
                                  "star\tx\nlabel\t\tHi\t0\t0\t2\t1\n');"
                                  "INSERT INTO oadb_forms VALUES('Bad', 'Orders', 'oadb form 9\n')", NULL, NULL, NULL) == SQLITE_OK,
              "written by hand");
        sqlite3_close(db);
    }
    {
        char err[OB_ERR_MAX];
        p = ob_open(path("dforms.oadb"), 0, err, sizeof err);
        CHECK(p != NULL, "reopened: %s", err);
        if (!p) return;
    }
    CHECK(ob_form_get(p, "New", &g) && g.nitems == 2 && g.items[1].kind == OB_FI_LABEL && !strcmp(g.items[1].text, "Hi"),
          "two of three: %d", g.nitems);
    CHECK(!ob_form_get(p, "Bad", &g), "damaged");
    CHECK_ERR(p, "newer GFOpenBase or damaged");
    ob_close(p);
}

/* Joins drawn in the query designer: inner, left and right; what a saved
 * query keeps; what is refused. Orders 1 1 2 3 5: bob (4) has none. */
static void test_query_joins(void)
{
    ob_project *p = fresh("joins.oadb");
    static ob_query q, back;
    static ob_table nt;
    int i;
    if (!p) return;
    five_customers(p);
    shop_orders(p);
    ob_table_init(&nt, "Notes");
    ob_table_add_field(&nt, "Text", OB_TEXT);
    CHECK(ob_table_create(p, &nt), "Notes: %s", ob_error(p));

    ob_query_init(&q, "Who bought");
    ob_query_add_table(&q, "Customers");
    ob_query_add_table(&q, "Orders");
    ob_query_add_column(&q, "Customers", "Name");
    ob_query_add_column(&q, "Orders", "Total");
    CHECK(ob_query_total(p, &q, NULL) == 5, "no join drawn: by the relationship, 5");

    /* the relationship drawn as a join */
    CHECK(ob_query_relation_joins(p, &q) == 1 && q.njoins == 1, "one drawn: %s", ob_error(p));
    CHECK(!strcmp(q.joins[0].left, "Orders") && !strcmp(q.joins[0].lfield, "Customer") && !strcmp(q.joins[0].right, "Customers")
          && !strcmp(q.joins[0].rfield, "id") && q.joins[0].kind == OB_JOIN_INNER,
          "Orders.Customer = Customers.id: %s.%s %s.%s", q.joins[0].left, q.joins[0].lfield, q.joins[0].right, q.joins[0].rfield);
    CHECK(ob_query_relation_joins(p, &q) == 0 && q.njoins == 1, "not twice");
    CHECK(strstr(qsql(p, &q), " FROM \"Customers\" JOIN \"Orders\" ON \"Orders\".\"Customer\" = \"Customers\".\"id\""), "inner: %s", qsql(p, &q));
    CHECK(ob_query_total(p, &q, NULL) == 5, "inner: 5");

    /* every customer: a left join from Customers, or a right one to it */
    ob_query_remove_join(&q, 0);
    CHECK(q.njoins == 0, "removed");
    CHECK(ob_query_add_join(&q, "Customers", "id", "Orders", "Customer", OB_JOIN_LEFT) == 0, "left join");
    CHECK(strstr(qsql(p, &q), " LEFT JOIN \"Orders\" ON \"Customers\".\"id\" = \"Orders\".\"Customer\""), "left: %s", qsql(p, &q));
    CHECK(ob_query_total(p, &q, NULL) == 6, "left: 6 rows, bob with none: %d", (int)ob_query_total(p, &q, NULL));
    q.joins[0].kind = OB_JOIN_RIGHT;
    CHECK(ob_query_total(p, &q, NULL) == 5, "right (every order): 5");
    ob_query_remove_join(&q, 0);
    ob_query_add_join(&q, "Orders", "Customer", "Customers", "id", OB_JOIN_RIGHT);
    CHECK(strstr(qsql(p, &q), " LEFT JOIN \"Orders\""), "right with Orders on the left, turned round: %s", qsql(p, &q));
    CHECK(ob_query_total(p, &q, NULL) == 6, "6 again");
    /* Orders first: the new table is the join's right side, kept as written */
    snprintf(q.tables[0], sizeof q.tables[0], "%s", "Orders");
    snprintf(q.tables[1], sizeof q.tables[1], "%s", "Customers");
    CHECK(strstr(qsql(p, &q), " FROM \"Orders\" RIGHT JOIN \"Customers\""), "Orders first: %s", qsql(p, &q));
    CHECK(ob_query_total(p, &q, NULL) == 6, "Orders first, still 6");

    /* kept and read back */
    CHECK(ob_query_save(p, &q), "saved: %s", ob_error(p));
    CHECK(ob_query_get(p, "Who bought", &back) && !back.sql_only && back.njoins == 1 && back.joins[0].kind == OB_JOIN_RIGHT
          && !strcmp(back.joins[0].left, "Orders") && !strcmp(back.joins[0].rfield, "id"), "read back: %d joins", back.njoins);
    CHECK(back.ntables == 2 && back.ncolumns == 2 && ob_query_total(p, &back, NULL) == 6, "runs the same");
    back.njoins = 0;
    CHECK(ob_query_save(p, &back) && ob_query_get(p, "Who bought", &back) && back.njoins == 0 && ob_query_total(p, &back, NULL) == 5,
          "no joins: kept as before, by the relationship");

    /* refused */
    CHECK(ob_query_add_join(&q, "Orders", "Customer", "Customers", "id", OB_JOIN_INNER) == -1, "the same fields twice");
    CHECK(ob_query_add_join(&q, "customers", "ID", "orders", "customer", OB_JOIN_INNER) == -1, "or turned round, any case");
    CHECK(ob_query_add_join(&q, "Orders", "Total", "orders", "Weight", OB_JOIN_INNER) == -1, "a table to itself");
    CHECK(ob_query_add_join(&q, "Orders", "", "Customers", "id", OB_JOIN_INNER) == -1, "no field");
    CHECK(ob_query_add_join(&q, "Orders", "Total", "Customers", "Credit", OB_JOINS) == -1, "no kind");
    i = ob_query_add_join(&q, "Orders", "Weight", "Customers", "Nope", OB_JOIN_INNER);
    CHECK(i == 1 && !ob_query_check(p, &q) && strstr(ob_error(p), "Customers.Nope, which isn't there"), "a field not there: %s", ob_error(p));
    ob_query_remove_join(&q, 1);
    i = ob_query_add_join(&q, "Orders", "Weight", "Notes", "Text", OB_JOIN_INNER);
    CHECK(!ob_query_check(p, &q) && strstr(ob_error(p), "Notes, which isn't in the query"), "a table not in it: %s", ob_error(p));
    ob_query_remove_join(&q, i);
    ob_query_add_table(&q, "Notes");
    CHECK(!ob_query_check(p, &q) && strstr(ob_error(p), "Notes isn't joined"), "a table not joined: %s", ob_error(p));
    CHECK(ob_query_relation_joins(p, &q) == 0, "no relationship to Notes");
    /* Notes joined to the second table: placed after it */
    ob_query_add_join(&q, "Notes", "Text", "Customers", "Name", OB_JOIN_LEFT);
    CHECK(ob_query_check(p, &q), "three tables: %s", ob_error(p));
    CHECK(strstr(qsql(p, &q), "RIGHT JOIN \"Customers\" ON \"Orders\".\"Customer\" = \"Customers\".\"id\" RIGHT JOIN \"Notes\" ON \"Notes\".\"Text\" = \"Customers\".\"Name\""),
          "chained: %s", qsql(p, &q));
    /* two joins between two tables: ON both */
    ob_query_add_join(&q, "Customers", "Town", "Notes", "Text", OB_JOIN_LEFT);
    CHECK(strstr(qsql(p, &q), "\"Notes\".\"Text\" = \"Customers\".\"Name\" AND \"Customers\".\"Town\" = \"Notes\".\"Text\""), "ON both: %s", qsql(p, &q));
    ob_query_remove_table(&q, 2);
    CHECK(q.ntables == 2 && q.njoins == 1 && !strcmp(q.joins[0].right, "Customers"), "Notes out, its joins with it");
    for (i = 0; q.njoins < OB_QUERY_JOINS && i < 40; i++) {
        char f[16];
        snprintf(f, sizeof f, "F%d", i);
        ob_query_add_join(&q, "Orders", f, "Customers", "id", OB_JOIN_INNER);
    }
    CHECK(q.njoins == OB_QUERY_JOINS && ob_query_add_join(&q, "Orders", "X", "Customers", "Y", OB_JOIN_INNER) == -1, "full");
    CHECK(!strcmp(ob_join_name(OB_JOIN_LEFT), "Left") && !strcmp(ob_join_name(OB_JOINS), ""), "names");
    ob_close(p);
}

static void test_queries(void)
{
    ob_project *p = fresh("query.oadb");
    static ob_query q, back;
    ob_qcolumn *c;
    ob_qresult res[OB_QUERY_OUT];
    char names[OB_QUERY_PARAMS][OB_VALUE_MAX];
    char err[OB_ERR_MAX];
    if (!p) return;
    five_customers(p);
    shop_orders(p);

    /* one table: criteria in plain words */
    ob_query_init(&q, "York people");
    CHECK(ob_query_add_table(&q, "Customers") == 0 && ob_query_add_table(&q, "customers") == -1, "a table once");
    c = ob_query_add_column(&q, "Customers", "Name");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "york");
    CHECK(!strcmp(qsql(p, &q), "SELECT \"Customers\".\"Name\" AS \"Name\", \"Customers\".\"Town\" AS \"Town\" FROM \"Customers\""
                               " WHERE \"Customers\".\"Town\" = 'York' COLLATE NOCASE ORDER BY \"Customers\".\"Name\" COLLATE NOCASE"),
          "york SQL: %s", qsql(p, &q));
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "bob|York;dan|York"), "york rows: %s", qrows(p, &q, NULL, 0, -1));
    CHECK(ob_query_total(p, &q, NULL) == 2, "york total");
    strcpy(c->criteria[0], "not York");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|Leeds;Cy|Hull;Eve|Leeds"), "not York: %s", qrows(p, &q, NULL, 0, -1));
    CHECK(!strcmp(qrows(p, &q, NULL, 1, 1), "Cy|Hull"), "a page: %s", qrows(p, &q, NULL, 1, 1));
    strcpy(c->criteria[0], "Hull");
    strcpy(c->criteria[1], "leeds");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|Leeds;Cy|Hull;Eve|Leeds"), "Hull or Leeds: %s", qrows(p, &q, NULL, 0, -1));
    CHECK(strstr(qsql(p, &q), " WHERE (\"Customers\".\"Town\" = 'Hull' COLLATE NOCASE) OR (\"Customers\".\"Town\" = 'Leeds' COLLATE NOCASE)"),
          "rows ORed: %s", qsql(p, &q));
    strcpy(c->criteria[0], "Paris");
    c->criteria[1][0] = 0;
    CHECK(ob_query_total(p, &q, NULL) < 0, "a town that isn't a choice");
    CHECK_ERR(p, "Town must be one of: Leeds,York,Hull");
    c->criteria[0][0] = 0;

    c = ob_query_add_column(&q, "Customers", "Credit");
    strcpy(c->criteria[0], "> 100");
    CHECK(strstr(qsql(p, &q), "\"Customers\".\"Credit\" > 10000"), "money in pence: %s", qsql(p, &q));
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|Leeds|1,500.00;dan|York|250.00"), "credit > 100: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->criteria[0], ">= lots");
    CHECK(ob_query_total(p, &q, NULL) < 0, "bad money");
    CHECK_ERR(p, "Credit limit must be an amount of money");
    strcpy(c->criteria[0], "empty");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "bob|York|"), "no credit: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->criteria[0], "between -10 and 100");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Cy|Hull|99.99;Eve|Leeds|-5.00"), "between: %s", qrows(p, &q, NULL, 0, -1));
    c->show = 0;
    strcpy(c->criteria[0], "<0");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Eve|Leeds"), "hidden column's criteria: %s", qrows(p, &q, NULL, 0, -1));
    ob_query_remove_column(&q, 2);

    c = ob_query_add_column(&q, "Customers", "Email");
    strcpy(c->criteria[0], "empty");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|Leeds|"), "no email: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->criteria[0], "*100%_*");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "bob|York|bob 100%_x@example.org"), "%% and _ as typed: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->criteria[0], "?ve@*");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Eve|Leeds|eve@example.org"), "? and *: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->criteria[0], "not *example*");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), ""), "not like: %s", qrows(p, &q, NULL, 0, -1));
    ob_query_remove_column(&q, 2);

    c = ob_query_add_column(&q, "Customers", "Joined");
    strcpy(c->criteria[0], "between 1/1/2026 and 2026-12-31");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "bob|York|2026-11-11;dan|York|2026-03-01"), "dates between: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->criteria[0], "between 2026");
    CHECK(ob_query_total(p, &q, NULL) < 0, "between without and");
    CHECK_ERR(p, "Joined: write between 1 and 5");
    ob_query_remove_column(&q, 2);

    /* parameters */
    strcpy(q.columns[1].criteria[0], "[Which town?]");
    c = ob_query_add_column(&q, "Customers", "Credit");
    strcpy(c->criteria[0], ">= [ At least ]");
    CHECK(ob_query_params(p, &q, names, OB_QUERY_PARAMS) == 2 && !strcmp(names[0], "Which town?") && !strcmp(names[1], "At least"),
          "parameters: %s|%s", names[0], names[1]);
    {
        const char *york[] = { "york", "0" }, *none[] = { "york", "" }, *bad[] = { "york", "lots" };
        CHECK(!strcmp(qrows(p, &q, york, 0, -1), "dan|York|250.00"), "asked: %s", qrows(p, &q, york, 0, -1));
        CHECK(ob_query_total(p, &q, york) == 1, "asked total");
        CHECK(ob_query_total(p, &q, none) < 0, "no answer");
        CHECK_ERR(p, "At least: give a value");
        CHECK(ob_query_total(p, &q, bad) < 0, "bad answer");
        CHECK_ERR(p, "At least: Credit limit must be an amount of money");
        CHECK(ob_query_total(p, &q, NULL) < 0, "no answers");
    }
    strcpy(q.columns[1].criteria[0], "[]");
    CHECK(!ob_query_check(p, &q), "empty question");
    CHECK_ERR(p, "a question to ask is written in [ ]");
    q.columns[1].criteria[0][0] = 0;
    ob_query_remove_column(&q, 2);

    /* two tables, joined by their relationship, with totals */
    ob_query_init(&q, "Spend");
    ob_query_add_table(&q, "Customers");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Customers", "Name");
    c->sort = OB_SORT_UP;
    c->total = OB_TOTAL_GROUP;
    c = ob_query_add_column(&q, "Orders", "Total");
    c->total = OB_TOTAL_SUM;
    c = ob_query_add_column(&q, "Orders", "Total");
    c->total = OB_TOTAL_COUNT;
    strcpy(c->heading, "Orders");
    CHECK(!strcmp(qsql(p, &q), "SELECT \"Customers\".\"Name\" AS \"Name\", sum(\"Orders\".\"Total\") AS \"Sum of Total\","
                               " count(\"Orders\".\"Total\") AS \"Orders\" FROM \"Customers\" JOIN \"Orders\""
                               " ON \"Orders\".\"Customer\" = \"Customers\".\"id\" GROUP BY \"Customers\".\"Name\""
                               " ORDER BY \"Customers\".\"Name\" COLLATE NOCASE"), "spend SQL: %s", qsql(p, &q));
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|100.00|1;Cy|7.25|1;dan|12.50|2;Eve|1.00|1"), "spend: %s", qrows(p, &q, NULL, 0, -1));
    CHECK(ob_query_columns(p, &q, res, OB_QUERY_OUT) == 3 && res[1].type == OB_CURRENCY && res[2].type == OB_INTEGER
          && !strcmp(res[1].heading, "Sum of Total"), "result columns");
    strcpy(q.columns[1].criteria[0], "> 5");
    CHECK(strstr(qsql(p, &q), " HAVING sum(\"Orders\".\"Total\") > 500"), "HAVING: %s", qsql(p, &q));
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|100.00|1;Cy|7.25|1;dan|12.50|2"), "spend > 5: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(q.columns[0].criteria[0], "*n");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|100.00|1;dan|12.50|2"), "group and total criteria: %s", qrows(p, &q, NULL, 0, -1));
    q.columns[0].criteria[0][0] = 0;
    q.columns[1].criteria[0][0] = 0;
    q.columns[2].total = OB_TOTAL_AVERAGE;
    q.columns[2].heading[0] = 0;
    q.columns[2].sort = OB_SORT_DOWN;
    q.columns[0].sort = OB_SORT_NONE;
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|100.00|100.00;Cy|7.25|7.25;dan|12.50|6.25;Eve|1.00|1.00"),
          "average of money: %s", qrows(p, &q, NULL, 0, -1));
    q.columns[2].total = OB_TOTAL_MAX;
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|100.00|100.00;dan|12.50|10.00;Cy|7.25|7.25;Eve|1.00|1.00"),
          "max: %s", qrows(p, &q, NULL, 0, -1));
    q.columns[2].total = OB_TOTAL_SUM;
    q.columns[2].field[0] = 0;
    strcpy(q.columns[2].field, "Weight");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "Ann|100.00|20.0;dan|12.50|1.5;Eve|1.00|0.25;Cy|7.25|"),
          "sum of numbers: %s", qrows(p, &q, NULL, 0, -1));
    q.columns[0].total = OB_TOTAL_SUM;
    CHECK(!ob_query_check(p, &q), "sum of names");
    CHECK_ERR(p, "Name: Sum works on numbers");
    q.columns[0].total = OB_TOTAL_GROUP;

    /* the other way round, and an expression */
    ob_query_init(&q, "Lines");
    ob_query_add_table(&q, "Orders");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Orders", "id");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Customers", "Name");
    c = ob_query_add_column(&q, NULL, "\"Orders\".\"Total\" * 2");
    strcpy(c->heading, "Double");
    strcpy(c->criteria[0], "> 1000");
    CHECK(strstr(qsql(p, &q), " FROM \"Orders\" JOIN \"Customers\" ON \"Orders\".\"Customer\" = \"Customers\".\"id\""),
          "joined the other way: %s", qsql(p, &q));
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "1|dan|2000;3|Ann|20000;4|Cy|1450"), "expression: %s", qrows(p, &q, NULL, 0, -1));
    strcpy(c->expr, "nope + 1");
    CHECK(!ob_query_check(p, &q), "bad expression");
    CHECK_ERR(p, "no such column: nope");
    strcpy(c->expr, "1; DROP TABLE Orders");
    CHECK(!ob_query_check(p, &q), "two statements in an expression");
    CHECK_ERR(p, "without ;");
    ob_query_remove_column(&q, 2);

    /* every field */
    ob_query_init(&q, "All");
    ob_query_add_table(&q, "Customers");
    ob_query_add_column(&q, "Customers", "*");
    CHECK(ob_query_columns(p, &q, res, OB_QUERY_OUT) == 7 && !strcmp(res[6].heading, "Credit limit") && res[6].type == OB_CURRENCY,
          "every field: %s", res[6].heading);
    CHECK(!strcmp(qrows(p, &q, NULL, 4, 1), "5|Eve|Leeds|eve@example.org|2024-01-02|Yes|-5.00"), "a whole row: %s", qrows(p, &q, NULL, 4, 1));
    q.columns[0].sort = OB_SORT_UP;
    CHECK(!ob_query_check(p, &q), "sorted star");
    CHECK_ERR(p, "a column of every field can't have a sort");
    q.columns[0].sort = OB_SORT_NONE;

    /* what can't run */
    {
        static ob_table t;
        ob_table_init(&t, "Things");
        ob_table_add_field(&t, "Thing", OB_TEXT);
        CHECK(ob_table_create(p, &t), "Things: %s", ob_error(p));
    }
    ob_query_add_table(&q, "Things");
    CHECK(!ob_query_check(p, &q), "no relationship");
    CHECK_ERR(p, "Things has no relationship with Customers");
    ob_query_remove_table(&q, 1);
    ob_query_add_table(&q, "Shops");
    CHECK(!ob_query_check(p, &q), "a missing table");
    CHECK_ERR(p, "All uses the table Shops, which isn't there");
    ob_query_remove_table(&q, 1);
    ob_query_add_column(&q, "Customers", "Shoe");
    CHECK(!ob_query_check(p, &q), "a missing field");
    CHECK_ERR(p, "Customers has no field Shoe");
    ob_query_remove_column(&q, 1);
    q.columns[0].show = 0;
    CHECK(!ob_query_check(p, &q), "nothing shown");
    CHECK_ERR(p, "All shows no columns");
    ob_query_remove_table(&q, 0);
    CHECK(q.ntables == 0 && q.ncolumns == 0, "a table's columns go with it");
    CHECK(!ob_query_check(p, &q), "empty");
    CHECK_ERR(p, "add a table");

    /* SQL view */
    ob_query_init(&q, "Typed");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT Name, Credit FROM Customers WHERE Town = :town ORDER BY Name;  ");
    CHECK(ob_query_params(p, &q, names, OB_QUERY_PARAMS) == 1 && !strcmp(names[0], "town"), "named parameter: %s", names[0]);
    {
        const char *york[] = { "York" };
        CHECK(!strcmp(qrows(p, &q, york, 0, -1), "bob|;dan|25000"), "typed SQL: %s", qrows(p, &q, york, 0, -1));
        CHECK(ob_query_total(p, &q, york) == 2, "typed total");
        CHECK(ob_query_columns(p, &q, res, OB_QUERY_OUT) == 2 && !strcmp(res[1].heading, "Credit") && res[1].type == -1,
              "typed columns");
    }
    strcpy(q.sql, "SELECT id FROM Customers WHERE id > ? AND id < ?");
    {
        const char *two[] = { "1", "4" };
        CHECK(ob_query_params(p, &q, names, OB_QUERY_PARAMS) == 2 && !strcmp(names[1], "Value 2"), "? parameters");
        CHECK(!strcmp(qrows(p, &q, two, 0, -1), "2;3"), "? bound: %s", qrows(p, &q, two, 0, -1));
    }
    {
        static const char *const bad[][2] = {
            { "DELETE FROM Customers", "a query only reads" },
            { "WITH x AS (SELECT 1) DELETE FROM Orders", "a query only reads" },
            { "SELECT 1; SELECT 2", "is one statement" },
            { "SELECT nope FROM Customers", "no such column: nope" },
            { "   ", "type its SQL" },
            { "PRAGMA journal_mode=DELETE", "a query only reads" },
        };
        unsigned i;
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            strcpy(q.sql, bad[i][0]);
            CHECK(!ob_query_check(p, &q), "accepted: %s", bad[i][0]);
            CHECK_ERR(p, bad[i][1]);
        }
        CHECK(ob_row_total(p, "Orders") == 5, "still five orders");
    }
    strcpy(q.sql, "/* spend */ WITH s AS (SELECT Customer, sum(Total) AS t FROM Orders GROUP BY Customer)"
                  " SELECT count(*) FROM s WHERE t > 500");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "3"), "WITH: %s", qrows(p, &q, NULL, 0, -1));

    /* saved */
    CHECK(ob_query_count(p) == 0, "no queries yet");
    ob_query_init(&q, "Spend");
    ob_query_add_table(&q, "Customers");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Customers", "Name");
    c->total = OB_TOTAL_GROUP;
    c->sort = OB_SORT_DOWN;
    strcpy(c->heading, "Who\tthey are\\");
    strcpy(c->criteria[0], "not empty");
    c = ob_query_add_column(&q, "Orders", "Total");
    c->total = OB_TOTAL_SUM;
    strcpy(c->criteria[0], ">= [Spent at least]");
    c->show = 0;
    c = ob_query_add_column(&q, NULL, "\"Orders\".\"id\"");
    c->total = OB_TOTAL_COUNT;
    {
        ob_int key;
        const char *f[] = { "Name" }, *v[] = { "Zed" };
        CHECK(ob_row_add(p, "Customers", 1, f, v, &key), "a row to undo: %s", ob_error(p));
        CHECK(ob_query_save(p, &q), "save: %s", ob_error(p));
        CHECK(ob_undo_count(p) >= 1 && ob_undo(p) && ob_row_total(p, "Customers") == 5, "undo after saving a query: %s", ob_error(p));
    }
    memset(&back, 0x55, sizeof back);
    CHECK(ob_query_get(p, "spend", &back), "get: %s", ob_error(p));
    CHECK(!strcmp(back.name, "Spend") && back.ntables == 2 && back.ncolumns == 3 && !strcmp(back.tables[1], "Orders")
          && !strcmp(back.columns[0].heading, "Who\tthey are\\") && back.columns[0].sort == OB_SORT_DOWN
          && back.columns[0].total == OB_TOTAL_GROUP && !strcmp(back.columns[0].criteria[0], "not empty")
          && !back.columns[1].show && !strcmp(back.columns[1].criteria[0], ">= [Spent at least]")
          && !strcmp(back.columns[2].expr, "\"Orders\".\"id\"") && back.columns[2].total == OB_TOTAL_COUNT
          && !back.columns[2].table[0] && !back.sql_only,
          "read back");
    {
        const char *ten[] = { "10" };
        CHECK(!strcmp(qrows(p, &back, ten, 0, -1), "dan|2;Ann|1"), "the saved query runs: %s", qrows(p, &back, ten, 0, -1));
    }
    ob_query_init(&q, "Typed");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT Name FROM Customers ORDER BY id DESC");
    CHECK(ob_query_save(p, &q), "save SQL: %s", ob_error(p));
    CHECK(ob_query_count(p) == 2 && !strcmp(ob_query_name(p, 0), "Spend") && !strcmp(ob_query_name(p, 1), "Typed")
          && ob_query_name(p, 2) == NULL, "listed");
    CHECK(ob_query_get(p, "Typed", &back) && back.sql_only && !strcmp(back.sql, q.sql), "SQL read back");
    strcpy(q.sql, "DELETE FROM Customers");
    CHECK(!ob_query_save(p, &q), "a query that can't run isn't saved");
    CHECK(ob_query_get(p, "Typed", &back) && !strcmp(back.sql, "SELECT Name FROM Customers ORDER BY id DESC"), "kept as it was");
    ob_query_init(&q, "Customers");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT 1");
    CHECK(!ob_query_save(p, &q), "a query named like a table");
    CHECK_ERR(p, "There is a table named Customers");
    strcpy(q.name, "oadb_x");
    CHECK(!ob_query_save(p, &q), "a kept name");
    CHECK_ERR(p, "kept for the project");
    {
        static ob_table t;
        ob_table_init(&t, "Typed");
        CHECK(!ob_table_create(p, &t), "a table named like a query");
        CHECK_ERR(p, "There is a query named Typed");
        CHECK(!ob_table_rename(p, "Things", "spend"), "a table renamed like a query");
        CHECK_ERR(p, "There is a query named spend");
    }
    CHECK(!ob_query_rename(p, "Typed", "Spend"), "rename onto another");
    CHECK_ERR(p, "There is a query named Spend already");
    CHECK(ob_query_rename(p, "Typed", "Newest first") && ob_query_get(p, "newest FIRST", &back), "rename: %s", ob_error(p));
    CHECK(ob_query_delete(p, "Newest first") && ob_query_count(p) == 1, "delete: %s", ob_error(p));
    CHECK(!ob_query_delete(p, "Newest first"), "delete twice");
    CHECK_ERR(p, "There is no query named Newest first");
    CHECK(!ob_query_get(p, "Nothing", &back), "get a missing query");
    ob_close(p);

    /* reopened; and a project from core 0.4, with no oadb_queries */
    p = ob_open(path("query.oadb"), 0, err, sizeof err);
    CHECK(p && ob_query_count(p) == 1 && ob_query_get(p, "Spend", &back), "reopened: %s", p ? ob_error(p) : err);
    ob_close(p);
    {
        sqlite3 *db;
        CHECK(sqlite3_open(path("query.oadb"), &db) == SQLITE_OK
              && sqlite3_exec(db, "DROP TABLE oadb_queries", NULL, NULL, NULL) == SQLITE_OK, "made old");
        sqlite3_close(db);
    }
    p = ob_open(path("query.oadb"), OB_OPEN_READONLY, err, sizeof err);
    CHECK(p && ob_query_count(p) == 0 && ob_query_name(p, 0) == NULL && !ob_query_get(p, "Spend", &back),
          "old, read only: %s", p ? ob_error(p) : err);
    ob_query_init(&q, "Typed");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT 1");
    CHECK(p && !ob_query_save(p, &q), "no saving read only");
    ob_close(p);
    p = ob_open(path("query.oadb"), 0, err, sizeof err);
    CHECK(p && ob_query_save(p, &q) && ob_query_count(p) == 1, "old project gets queries: %s", p ? ob_error(p) : err);
    ob_close(p);
}

/* A query's rows as CSV, a query as a view, saved queries following a
 * table's rename (core 0.13.1) */
static void test_query_out(void)
{
    ob_project *p = fresh("qout.oadb");
    static ob_query q, typed, spend, back;
    static ob_table t;
    ob_qcolumn *c;
    ob_csv_opts o;
    char err[OB_ERR_MAX];
    const char *york[] = { "York" };
    FILE *f;
    if (!p) return;
    five_customers(p);
    shop_orders(p);

    /* rows as CSV: headings, quoting, the sort and criteria */
    ob_query_init(&q, "Town people");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Name");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "Leeds");
    c = ob_query_add_column(&q, NULL, "'a, \"b\"'");
    strcpy(c->heading, "Note");
    CHECK(ob_query_csv_export(p, &q, NULL, path("leeds.csv"), NULL) == 2, "export: %s", ob_error(p));
    CHECK(!strcmp(slurp("leeds.csv"), "Name,Town,Note\r\nAnn,Leeds,\"a, \"\"b\"\"\"\r\nEve,Leeds,\"a, \"\"b\"\"\"\r\n"),
          "query CSV: %s", slurp("leeds.csv"));
    ob_csv_opts_init(&o);
    o.sep = ';';
    o.charset = OB_CHARSET_UTF8;
    strcpy(q.columns[2].expr, "'caf\xE9'");
    CHECK(ob_query_csv_export(p, &q, NULL, path("leeds.csv"), &o) == 2, "export UTF-8: %s", ob_error(p));
    CHECK(!strcmp(slurp("leeds.csv"), "\xEF\xBB\xBFName;Town;Note\r\nAnn;Leeds;caf\xC3\xA9\r\nEve;Leeds;caf\xC3\xA9\r\n"),
          "semicolons, UTF-8: %s", slurp("leeds.csv"));
    o.sep = '|';
    CHECK(ob_query_csv_export(p, &q, NULL, path("leeds.csv"), &o) < 0, "a bad separator");
    CHECK_ERR(p, "separated by a comma");

    /* a question: answered, or the file isn't made */
    strcpy(q.columns[1].criteria[0], "[Which town?]");
    CHECK(ob_query_csv_export(p, &q, york, path("york.csv"), NULL) == 2, "export with an answer: %s", ob_error(p));
    CHECK(!strcmp(slurp("york.csv"), "Name,Town,Note\r\nbob,York,caf\xE9\r\ndan,York,caf\xE9\r\n"), "York: %s", slurp("york.csv"));
    {
        const char *bad[] = { "Paris" };
        zap("paris.csv");
        CHECK(ob_query_csv_export(p, &q, bad, path("paris.csv"), NULL) < 0, "a bad answer");
        CHECK_ERR(p, "Town must be one of");
        f = fopen(path("paris.csv"), "rb");
        CHECK(!f, "no file made for a bad answer");
        if (f) fclose(f);
    }

    /* typed SQL: values as SQLite gives them */
    ob_query_init(&typed, "Typed");
    typed.sql_only = 1;
    strcpy(typed.sql, "SELECT Name, Total FROM Customers JOIN Orders ON Orders.Customer = Customers.id"
                      " ORDER BY Total DESC LIMIT 2");
    CHECK(ob_query_csv_export(p, &typed, NULL, path("typed.csv"), NULL) == 2, "typed SQL export: %s", ob_error(p));
    CHECK(!strcmp(slurp("typed.csv"), "Name,Total\r\nAnn,10000\r\ndan,1000\r\n"), "typed SQL CSV: %s", slurp("typed.csv"));

    /* a view: other SQL reads it */
    CHECK(!ob_query_save_view(p, &q, "York people"), "a view can't ask questions");
    CHECK_ERR(p, "A view can't ask questions");
    strcpy(q.columns[1].criteria[0], "York");
    CHECK(ob_query_save_view(p, &q, "York people"), "save a view: %s", ob_error(p));
    CHECK(ob_sqlview_count(p) == 1 && !strcmp(ob_sqlview_name(p, 0), "York people") && ob_sqlview_name(p, 1) == NULL,
          "the view is listed");
    CHECK(ob_table_count(p) == 2 && ob_query_count(p) == 0, "a view is not a table or a saved query");
    ob_query_init(&back, "Reads the view");
    back.sql_only = 1;
    strcpy(back.sql, "SELECT Name FROM \"York people\"");
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "bob;dan"), "the view's rows: %s", qrows(p, &back, NULL, 0, -1));
    strcpy(q.columns[1].criteria[0], "Hull");
    CHECK(ob_query_save_view(p, &q, "york PEOPLE") && ob_sqlview_count(p) == 1, "saving again replaces: %s", ob_error(p));
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "Cy"), "the new rows: %s", qrows(p, &back, NULL, 0, -1));
    strcpy(q.columns[1].criteria[0], "York");
    CHECK(ob_query_save_view(p, &q, "York people"), "back to York: %s", ob_error(p));

    /* names: a view, a table and a saved query each have their own */
    CHECK(!ob_query_save_view(p, &q, "customers"), "a view named like a table");
    CHECK_ERR(p, "There is a table named customers");
    CHECK(!ob_query_save_view(p, &q, "oadb_v"), "a kept name");
    CHECK_ERR(p, "kept for the project");
    CHECK(ob_query_save(p, &q), "save the query: %s", ob_error(p));
    CHECK(!ob_query_save_view(p, &q, "town PEOPLE"), "a view named like a query");
    CHECK_ERR(p, "There is a query named town PEOPLE");
    strcpy(typed.name, "York People");
    CHECK(!ob_query_save(p, &typed), "a query named like a view");
    CHECK_ERR(p, "There is a view named York People");
    strcpy(typed.name, "Typed");
    CHECK(ob_query_save(p, &typed), "save typed SQL: %s", ob_error(p));
    ob_table_init(&t, "york people");
    CHECK(!ob_table_create(p, &t), "a table named like a view");
    CHECK_ERR(p, "There is a view named york people");
    CHECK(!ob_table_rename(p, "Orders", "York people"), "a table renamed like a view");
    CHECK_ERR(p, "There is a view named York people");

    /* a table's rename: grid queries and views follow, typed SQL is kept */
    ob_query_init(&spend, "Spend");
    ob_query_add_table(&spend, "Customers");
    ob_query_add_table(&spend, "Orders");
    c = ob_query_add_column(&spend, "Customers", "Name");
    c->total = OB_TOTAL_GROUP;
    c = ob_query_add_column(&spend, "Orders", "Total");
    c->total = OB_TOTAL_SUM;
    c = ob_query_add_column(&spend, NULL, "\"Orders\".\"id\"");
    c->total = OB_TOTAL_COUNT;
    CHECK(ob_query_save(p, &spend), "save Spend: %s", ob_error(p));
    CHECK(ob_table_rename(p, "customers", "Clients"), "rename: %s", ob_error(p));
    CHECK(ob_query_get(p, "Town people", &back) && back.ntables == 1 && !strcmp(back.tables[0], "Clients")
          && !strcmp(back.columns[0].table, "Clients") && !strcmp(back.columns[1].table, "Clients")
          && !back.columns[2].table[0], "the grid query follows");
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "bob|York|caf\xE9;dan|York|caf\xE9"), "and runs: %s", qrows(p, &back, NULL, 0, -1));
    CHECK(ob_query_get(p, "Spend", &back) && !strcmp(back.tables[0], "Clients") && !strcmp(back.tables[1], "Orders")
          && !strcmp(back.columns[0].table, "Clients") && !strcmp(back.columns[1].table, "Orders"), "two tables, one renamed");
    CHECK(ob_query_total(p, &back, NULL) == 4, "Spend runs: %s", ob_error(p));
    CHECK(ob_query_get(p, "Typed", &back) && back.sql_only && strstr(back.sql, "FROM Customers JOIN"), "typed SQL as typed");
    ob_query_init(&back, "Reads the view");
    back.sql_only = 1;
    strcpy(back.sql, "SELECT Name FROM \"York people\"");
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "bob;dan"), "the view follows: %s", qrows(p, &back, NULL, 0, -1));

    /* a design change with a view on the table */
    ob_query_init(&back, "Weights");
    back.sql_only = 1;
    strcpy(back.sql, "SELECT Weight FROM Orders WHERE Weight > 1");
    CHECK(ob_query_save_view(p, &back, "Weights"), "a view of Orders: %s", ob_error(p));
    CHECK(ob_table_get(p, "Orders", &t), "Orders' design");
    CHECK(ob_table_change(p, "Orders", &t), "the same design, a view on it: %s", ob_error(p));
    ob_query_init(&back, "Count");
    back.sql_only = 1;
    strcpy(back.sql, "SELECT count(*) FROM Weights");
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "2"), "the view still reads: %s", qrows(p, &back, NULL, 0, -1));
    {
        int w = ob_table_find_field(&t, "Weight");
        CHECK(w > 0, "Weight is there");
        if (w > 0) ob_table_remove_field(&t, w);
    }
    CHECK(!ob_table_change(p, "Orders", &t), "a change that leaves a view unable to read");
    CHECK_ERR(p, "The view Weights uses Orders");
    CHECK(ob_table_get(p, "Orders", &t) && ob_table_find_field(&t, "Weight") > 0 && ob_row_total(p, "Orders") == 5,
          "Orders as it was");
    strcpy(t.name, "Sales");
    CHECK(ob_table_change(p, "Orders", &t), "a rename in a design change, a view on it: %s", ob_error(p));
    CHECK(ob_table_count(p) == 2 && !strcmp(ob_table_name(p, 1), "Sales") && ob_row_total(p, "Sales") == 5,
          "renamed with its rows");
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "2"), "the view follows the design change's rename: %s",
          qrows(p, &back, NULL, 0, -1));
    {
        static ob_table o;
        CHECK(ob_table_get(p, "Orders", &o) == 0 && ob_table_get(p, "Sales", &o)
              && !strcmp(o.fields[ob_table_find_field(&o, "Customer")].lookup, "Clients"), "Sales' design");
    }
    CHECK(ob_sqlview_drop(p, "weights") && ob_sqlview_count(p) == 1, "drop the view: %s", ob_error(p));
    CHECK(!ob_sqlview_drop(p, "Weights"), "drop it twice");
    CHECK_ERR(p, "There is no view named Weights");
    CHECK(ob_query_get(p, "Spend", &back) && !strcmp(back.tables[1], "Sales") && !strcmp(back.columns[1].table, "Sales"),
          "the grid query follows a design change's rename");
    CHECK(!strcmp(back.columns[2].expr, "\"Sales\".\"id\""), "and its expression: %s", back.columns[2].expr);
    CHECK(ob_query_total(p, &back, NULL) == 4, "and runs: %s", ob_error(p));

    /* a table a view uses can't be deleted */
    ob_query_init(&back, "Totals");
    back.sql_only = 1;
    strcpy(back.sql, "SELECT Total FROM Sales");
    CHECK(ob_query_save_view(p, &back, "Totals"), "a view of Sales: %s", ob_error(p));
    CHECK(!ob_table_drop(p, "Sales"), "delete a table a view uses");
    CHECK_ERR(p, "The view Totals uses Sales");
    CHECK(ob_table_count(p) == 2 && ob_row_total(p, "Sales") == 5, "Sales is kept");
    CHECK(ob_sqlview_drop(p, "Totals") && ob_table_drop(p, "Sales") && ob_table_count(p) == 1, "then it can: %s", ob_error(p));
    ob_close(p);

    /* the SQL kept beside the grid was made again; read only can't save a view */
    {
        sqlite3 *db;
        sqlite3_stmt *st;
        const char *sql = "";
        CHECK(sqlite3_open(path("qout.oadb"), &db) == SQLITE_OK
              && sqlite3_prepare_v2(db, "SELECT sql FROM oadb_queries WHERE name = 'Town people'", -1, &st, NULL) == SQLITE_OK,
              "read oadb_queries");
        if (sqlite3_step(st) == SQLITE_ROW) sql = (const char *)sqlite3_column_text(st, 0);
        CHECK(strstr(sql, "FROM \"Clients\"") && !strstr(sql, "Customers"), "kept SQL follows: %s", sql);
        sqlite3_finalize(st);
        sqlite3_close(db);
    }
    p = ob_open(path("qout.oadb"), OB_OPEN_READONLY, err, sizeof err);
    CHECK(p && ob_sqlview_count(p) == 1 && ob_query_get(p, "Town people", &q), "reopened: %s", p ? ob_error(p) : err);
    CHECK(p && !ob_query_save_view(p, &q, "Another"), "no views read only");
    ob_close(p);
}

/* A design change that renames a table another table looks up (no views) */
static void test_change_rename_looked_up(void)
{
    ob_project *p = fresh("rename-lookup.oadb");
    static ob_table t, o;
    if (!p) return;
    five_customers(p);
    shop_orders(p);
    CHECK(ob_table_get(p, "Customers", &t), "Customers' design");
    strcpy(t.name, "Clients");
    CHECK(ob_table_change(p, "Customers", &t), "rename a looked-up table in a design change: %s", ob_error(p));
    CHECK(ob_table_count(p) == 2 && !strcmp(ob_table_name(p, 0), "Clients") && ob_row_total(p, "Clients") == 5
          && ob_row_total(p, "Orders") == 5, "rows kept");
    CHECK(ob_table_get(p, "Orders", &o) && !strcmp(o.fields[ob_table_find_field(&o, "Customer")].lookup, "Clients"),
          "the Lookup follows");
    {
        ob_relation r[4];
        int n = ob_relations(p, r, 4);
        CHECK(n == 1 && !strcmp(r[0].table, "Orders") && !strcmp(r[0].lookup, "Clients"), "the foreign key follows: %d", n);
    }
    {
        const char *f[] = { "Customer", "Total" }, *v[] = { "2", "3.00" }, *bad[] = { "99", "1.00" };
        CHECK(ob_row_add(p, "Orders", 2, f, v, NULL), "an order for a client: %s", ob_error(p));
        CHECK(!ob_row_add(p, "Orders", 2, f, bad, NULL), "an order for nobody is refused");
    }
    /* and back, in a change that also adds a field */
    CHECK(ob_table_get(p, "Clients", &t), "Clients' design");
    strcpy(t.name, "Customers");
    ob_table_add_field(&t, "Phone", OB_TEXT);
    CHECK(ob_table_change(p, "Clients", &t), "rename back with a new field: %s", ob_error(p));
    CHECK(ob_table_get(p, "Orders", &o) && !strcmp(o.fields[ob_table_find_field(&o, "Customer")].lookup, "Customers")
          && ob_row_total(p, "Customers") == 5, "back as it was");
    ob_close(p);
}

/* ---- Rows from a grid (core 0.14, sheets #326) ---- */

typedef struct grid_rows {
    const char *const *cells;   /* nrows x ncols */
    int ncols;
    int wide;                   /* row 0 says it has this many cells (more than a table takes) */
} grid_rows;

static int grid_cb(void *user, long row, const char **cells, int max)
{
    const grid_rows *g = user;
    int i;
    for (i = 0; i < g->ncols && i < max; i++) cells[i] = g->cells[row * g->ncols + i];
    return row == 0 && g->wide ? g->wide : g->ncols;
}

static void grid_of(ob_grid *g, grid_rows *rows, const char *name, long nrows, long first)
{
    memset(g, 0, sizeof *g);
    g->name = name;
    g->nrows = nrows;
    g->first_row = first;
    g->row = grid_cb;
    g->user = rows;
}

static void test_grid(void)
{
    ob_project *p = fresh("grid.oadb");
    static ob_table t, made;
    static const char *const cells[] = {
        "Credit limit", "Name", NULL,       /* a header by label and name, any order */
        "12.50", "Kay", NULL,
        NULL, "", NULL,                     /* nothing in it: left out */
        "1,000", "Lou", NULL,
    };
    static const char *const bad[] = {
        "Name", "Credit",
        "Mo", "lots",
        "", "5",
        "Ned", "3",
    };
    static const char *const plain[] = {
        "Pat", "York", NULL, "2026-11-16", "Yes", "4",
    };
    grid_rows rows = { cells, 3, 0 }, badrows = { bad, 2, 0 }, plainrows = { plain, 6, 0 };
    ob_grid g;
    ob_csv_opts o;
    if (!p) return;
    customers(&t);
    CHECK(ob_table_create(p, &t), "Customers: %s", ob_error(p));

    grid_of(&g, &rows, "Sheet1", 4, 3);
    ob_csv_opts_init(&o);
    CHECK(ob_grid_import(p, "Customers", &g, &o) == 2 && o.found_header == 1, "grid: %s", ob_error(p));
    CHECK(!strcmp(all_rows(p, "Customers"), "1:1|Kay|Leeds|-|-|No|12.50;2:2|Lou|Leeds|-|-|No|1,000.00;"),
          "rows:\n%s", all_rows(p, "Customers"));
    CHECK(ob_undo_count(p) == 1, "one step of Undo");
    CHECK(ob_undo(p) && ob_row_total(p, "Customers") == 0, "undone: %s", ob_error(p));

    /* in field order, no header: the Counter left out as CSV's is */
    grid_of(&g, &plainrows, "Sheet2", 1, 1);
    CHECK(ob_grid_import(p, "Customers", &g, NULL) == 1, "plain: %s", ob_error(p));
    CHECK(strstr(all_rows(p, "Customers"), "|Pat|York|-|2026-11-16|Yes|4.00;"), "plain:\n%s", all_rows(p, "Customers"));

    /* messages say Row and the sheet's own row number; all or none */
    grid_of(&g, &badrows, "Sheet1", 4, 5);
    ob_csv_opts_init(&o);
    CHECK(ob_grid_import(p, "Customers", &g, &o) == -1, "bad row refused");
    CHECK_ERR(p, "Row 6: Credit limit must be an amount of money");
    CHECK(ob_row_total(p, "Customers") == 1, "none added");
    ob_csv_opts_init(&o);
    o.skip_bad = 1;
    CHECK(ob_grid_import(p, "Customers", &g, &o) == 1 && o.bad == 2, "skip bad: %s (%d)", ob_error(p), o.bad);
    CHECK(!strcmp(o.report, "Row 6: Credit limit must be an amount of money, such as 1,234.50\nRow 7: Name must be filled in\n"),
          "report:\n%s", o.report);
    ob_csv_opts_init(&o);
    o.header = 1;
    {
        static const char *const wrong[] = { "Name", "Shoe size" };
        grid_rows w = { wrong, 2, 0 };
        grid_of(&g, &w, "Sheet1", 1, 2);
        CHECK(ob_grid_import(p, "Customers", &g, &o) == -1, "a header that names no field");
        CHECK_ERR(p, "Row 2: Shoe size is not a field of Customers");
    }
    /* more cells than a table can take */
    {
        static const char *const one[] = { "Name" };
        grid_rows w = { one, 1, OB_MAX_FIELDS + 5 };
        grid_of(&g, &w, "Wide", 1, 1);
        CHECK(ob_grid_import(p, "Customers", &g, NULL) == -1, "too wide");
        CHECK_ERR(p, "Row 1 has more than");
        CHECK(!ob_grid_guess(p, &g, NULL, NULL, &made), "too wide to guess");
    }
    /* empty */
    grid_of(&g, &rows, "Sheet9", 0, 1);
    CHECK(ob_grid_import(p, "Customers", &g, NULL) == -1, "empty");
    CHECK_ERR(p, "Sheet9 is empty");
    CHECK(ob_grid_import(p, "Customers", NULL, NULL) == -1, "no grid");

    /* a new table, named after the grid or as asked */
    {
        static const char *const sold[] = {
            "id", "Item", "Price", "When", "Paid",
            "7", "Tea", "1.20", "2026-11-01", "Yes",
            "9", "Cake", "2.50", "2026-11-02", "No",
        };
        grid_rows w = { sold, 5, 0 };
        grid_of(&g, &w, "Sales 2026", 3, 1);
        ob_csv_opts_init(&o);
        CHECK(ob_grid_guess(p, &g, NULL, &o, &made) && o.found_header == 1, "guess: %s", ob_error(p));
        CHECK(!strcmp(made.name, "Sales 2026") && made.nfields == 5, "made %s with %d fields", made.name, made.nfields);
        CHECK(made.fields[0].type == OB_COUNTER && !strcmp(made.fields[0].name, "id"), "id is the Counter");
        CHECK(made.fields[2].type == OB_CURRENCY && made.fields[3].type == OB_DATE && made.fields[4].type == OB_YESNO,
              "types %d %d %d", made.fields[2].type, made.fields[3].type, made.fields[4].type);
        CHECK(ob_table_create(p, &made), "create: %s", ob_error(p));
        CHECK(ob_grid_import(p, "Sales 2026", &g, NULL) == 2, "into it: %s", ob_error(p));
        CHECK(!strcmp(all_rows(p, "Sales 2026"), "7:7|Tea|1.20|2026-11-01|Yes;9:9|Cake|2.50|2026-11-02|No;"),
              "kept keys:\n%s", all_rows(p, "Sales 2026"));
        CHECK(ob_grid_guess(p, &g, "Takings", NULL, &made) && !strcmp(made.name, "Takings"), "named: %s", made.name);
    }
    /* ob_set_error, for the parts outside the core */
    CHECK(ob_set_error(p, "Sheet1 is damaged") == 0 && !strcmp(ob_error(p), "Sheet1 is damaged"), "set");
    ob_set_error(p, "");
    CHECK(!ob_error(p)[0], "cleared");
    ob_close(p);
}

/* Queries that change rows (core 0.15): update, append and delete made in
 * the grid or typed, counted first, run all or none, undone as one step,
 * saved and following renames. */
static const char *cust_rows(ob_project *p)
{
    static ob_query q;
    ob_qcolumn *c;
    ob_query_init(&q, "");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Name");
    c->sort = OB_SORT_UP;
    ob_query_add_column(&q, "Customers", "Town");
    ob_query_add_column(&q, "Customers", "Credit");
    return qrows(p, &q, NULL, 0, -1);
}

static void test_action_queries(void)
{
    ob_project *p = fresh("action.oadb");
    static ob_query q, back;
    static ob_table arch;
    ob_qcolumn *c;
    ob_field *f;
    char names[OB_QUERY_PARAMS][OB_VALUE_MAX];
    char before[1024];
    int undoable = -1;
    if (!p) return;
    five_customers(p);
    shop_orders(p);
    snprintf(before, sizeof before, "%s", cust_rows(p));
    CHECK(!strcmp(ob_qkind_name(OB_QK_UPDATE), "Update") && !strcmp(ob_qkind_name(OB_QK_APPEND), "Append")
          && !strcmp(ob_qkind_name(OB_QK_DELETE), "Delete") && !strcmp(ob_qkind_name(OB_QKINDS), ""), "kind names");

    /* update: York's credit to 300, its town to Hull */
    ob_query_init(&q, "Move York");
    q.kind = OB_QK_UPDATE;
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "york");
    strcpy(c->to, "Hull");
    c = ob_query_add_column(&q, "Customers", "Credit");
    strcpy(c->to, "300");
    CHECK(!strcmp(qsql(p, &q), "UPDATE \"Customers\" SET \"Town\" = 'Hull', \"Credit\" = 30000 WHERE rowid IN"
                               " (SELECT (\"Customers\".rowid) AS \"rowid\" FROM \"Customers\""
                               " WHERE \"Customers\".\"Town\" = 'York' COLLATE NOCASE)"), "update SQL: %s", qsql(p, &q));
    CHECK(ob_query_total(p, &q, NULL) < 0, "an update has no rows to show");
    CHECK_ERR(p, "Move York is an Update query: it changes rows, so it is run, not opened");
    CHECK(!strncmp(qrows(p, &q, NULL, 0, -1), "error: Move York is an Update query", 35), "nor rows");
    CHECK(ob_query_preview(p, &q, NULL) == 2, "preview: two rows: %s", ob_error(p));
    CHECK(!strcmp(cust_rows(p), before), "a preview keeps nothing: %s", cust_rows(p));
    CHECK(ob_query_run(p, &q, NULL, &undoable) == 2 && undoable == 1, "run: %s", ob_error(p));
    CHECK(!strcmp(cust_rows(p), "Ann|Leeds|1,500.00;bob|Hull|300.00;Cy|Hull|99.99;dan|Hull|300.00;Eve|Leeds|-5.00"),
          "updated: %s", cust_rows(p));
    CHECK(!strcmp(ob_undo_name(p), "Update Query Move York: 2 rows in Customers"), "undo name: %s", ob_undo_name(p));
    CHECK(ob_undo(p), "undo the update: %s", ob_error(p));
    CHECK(!strcmp(cust_rows(p), before), "undone: %s", cust_rows(p));

    /* a value that breaks a rule: nothing changes */
    strcpy(q.columns[0].to, "Paris");
    CHECK(ob_query_preview(p, &q, NULL) < 0, "a town that isn't a choice");
    CHECK_ERR(p, "Town must be one of: Leeds,York,Hull");
    strcpy(q.columns[0].to, "='Paris'");
    CHECK(ob_query_run(p, &q, NULL, NULL) < 0, "a rule broken while running");
    CHECK_ERR(p, "Town must be one of its choices");
    CHECK(!strcmp(cust_rows(p), before), "all or none: %s", cust_rows(p));
    /* =SQL, empty and a question */
    strcpy(q.columns[0].to, "");
    strcpy(q.columns[1].to, "=\"Customers\".\"Credit\" * 2");
    c = ob_query_add_column(&q, "Customers", "Email");
    strcpy(c->to, "empty");
    c = ob_query_add_column(&q, "Customers", "Member");
    strcpy(c->to, "[Members now?]");
    CHECK(ob_query_params(p, &q, names, OB_QUERY_PARAMS) == 1 && !strcmp(names[0], "Members now?"), "a question: %s", names[0]);
    {
        const char *yes[] = { "yes" }, *bad[] = { "maybe" };
        CHECK(ob_query_run(p, &q, bad, NULL) < 0, "a bad answer");
        CHECK_ERR(p, "Members now?: ");
        CHECK(ob_query_run(p, &q, yes, &undoable) == 2 && undoable, "=SQL, empty and an answer: %s", ob_error(p));
    }
    CHECK(!strcmp(cust_rows(p), "Ann|Leeds|1,500.00;bob|York|;Cy|Hull|99.99;dan|York|500.00;Eve|Leeds|-5.00"),
          "doubled: %s", cust_rows(p));
    CHECK(ob_undo(p) && !strcmp(cust_rows(p), before), "undone again: %s", cust_rows(p));
    /* what an update can't do */
    ob_query_init(&q, "Bad");
    q.kind = OB_QK_UPDATE;
    ob_query_add_table(&q, "Customers");
    ob_query_add_column(&q, "Customers", "Name");
    CHECK(!ob_query_check(p, &q), "no Update To");
    CHECK_ERR(p, "Bad: give a field of Customers a value in Update To");
    c = ob_query_add_column(&q, "Customers", "id");
    strcpy(c->to, "7");
    CHECK(!ob_query_check(p, &q), "the Counter");
    CHECK_ERR(p, "id is a Counter: its numbers can't be changed");
    strcpy(c->field, "Name");
    q.columns[0].total = OB_TOTAL_COUNT;
    CHECK(!ob_query_check(p, &q), "a total");
    CHECK_ERR(p, "Bad: an Update query has no totals");
    q.columns[0].total = OB_TOTAL_NONE;
    strcpy(q.columns[0].to, "x");
    CHECK(!ob_query_check(p, &q), "twice");
    CHECK_ERR(p, "Bad: Name is changed twice");
    q.columns[0].to[0] = 0;
    strcpy(q.target, "Orders");
    CHECK(!ob_query_check(p, &q), "a target not in the query");
    CHECK_ERR(p, "an Update query changes one of its own tables, and Orders isn't one");
    q.target[0] = 0;
    strcpy(c->to, "empty");
    CHECK(!ob_query_check(p, &q), "a required field emptied");
    CHECK_ERR(p, "Name must be filled in");

    /* update through a join: orders of Leeds customers get 1.00 more */
    ob_query_init(&q, "Leeds dearer");
    q.kind = OB_QK_UPDATE;
    ob_query_add_table(&q, "Customers");
    ob_query_add_table(&q, "Orders");
    strcpy(q.target, "orders");
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "Leeds");
    c = ob_query_add_column(&q, "Orders", "Total");
    strcpy(c->to, "=\"Orders\".\"Total\" + 100");
    CHECK(ob_query_preview(p, &q, NULL) == 2, "two Leeds orders: %s", ob_error(p));

    /* delete: refused by a Lookup that refuses, all or none */
    ob_query_init(&q, "Drop York");
    q.kind = OB_QK_DELETE;
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "York");
    CHECK(!strcmp(qsql(p, &q), "DELETE FROM \"Customers\" WHERE rowid IN (SELECT (\"Customers\".rowid) AS \"rowid\""
                               " FROM \"Customers\" WHERE \"Customers\".\"Town\" = 'York' COLLATE NOCASE)"), "delete SQL: %s", qsql(p, &q));
    CHECK(ob_query_run(p, &q, NULL, NULL) < 0, "dan has orders");
    CHECK_ERR(p, "That would leave a row looking up a row that isn't there");
    CHECK(!strcmp(cust_rows(p), before), "nothing deleted: %s", cust_rows(p));
    strcpy(c->to, "x");
    CHECK(!ob_query_check(p, &q), "a delete changes no fields");
    CHECK_ERR(p, "a Delete query changes no fields");
    c->to[0] = 0;
    strcpy(c->criteria[0], "Hull");
    c = ob_query_add_column(&q, "Customers", "Name");
    strcpy(c->criteria[0], "nobody");
    CHECK(ob_query_preview(p, &q, NULL) == 0, "no rows picked");
    c->criteria[0][0] = 0;
    /* Cy (Hull) has an order too: delete Eve's order first, then Eve */
    ob_query_init(&back, "Eve's orders");
    back.kind = OB_QK_DELETE;
    ob_query_add_table(&back, "Orders");
    ob_query_add_table(&back, "Customers");
    strcpy(back.target, "Orders");
    c = ob_query_add_column(&back, "Customers", "Name");
    strcpy(c->criteria[0], "Eve");
    CHECK(ob_query_run(p, &back, NULL, &undoable) == 1 && undoable, "Eve's order: %s", ob_error(p));
    strcpy(q.columns[0].criteria[0], "Leeds");
    strcpy(q.columns[1].criteria[0], "Eve");
    CHECK(ob_query_run(p, &q, NULL, &undoable) == 1 && undoable, "delete Eve: %s", ob_error(p));
    CHECK(!strcmp(cust_rows(p), "Ann|Leeds|1,500.00;bob|York|;Cy|Hull|99.99;dan|York|250.00"), "Eve gone: %s", cust_rows(p));
    CHECK(!strcmp(ob_undo_name(p), "Delete Query Drop York: 1 row in Customers"), "undo name: %s", ob_undo_name(p));
    CHECK(ob_undo(p) && ob_undo(p), "undo both: %s", ob_error(p));
    CHECK(!strcmp(cust_rows(p), before), "Eve back with her key: %s", cust_rows(p));
    {
        static ob_query o;
        ob_query_init(&o, "");
        ob_query_add_table(&o, "Orders");
        ob_query_add_column(&o, "Orders", "Customer");
        ob_query_add_column(&o, "Orders", "Total");
        CHECK(!strcmp(qrows(p, &o, NULL, 0, -1), "1|10.00;1|2.50;2|100.00;3|7.25;5|1.00"), "orders back: %s", qrows(p, &o, NULL, 0, -1));
    }

    /* append: York customers into an archive table, with a total */
    ob_table_init(&arch, "Archive");
    f = ob_table_add_field(&arch, "Who", OB_TEXT);
    f->required = 1;
    f = ob_table_add_field(&arch, "Owed", OB_CURRENCY);
    f = ob_table_add_field(&arch, "Orders", OB_INTEGER);
    CHECK(ob_table_create(p, &arch), "Archive: %s", ob_error(p));
    ob_query_init(&q, "Keep York");
    q.kind = OB_QK_APPEND;
    strcpy(q.target, "Archive");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Name");
    strcpy(c->to, "Who");
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&q, "Customers", "Credit");
    strcpy(c->to, " owed ");
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "York");
    CHECK(!strcmp(qsql(p, &q), "INSERT INTO \"Archive\"(\"Who\", \"Owed\") SELECT \"Customers\".\"Name\" AS \"Name\","
                               " \"Customers\".\"Credit\" AS \"Credit limit\" FROM \"Customers\""
                               " WHERE \"Customers\".\"Town\" = 'York' COLLATE NOCASE ORDER BY \"Customers\".\"Name\" COLLATE NOCASE"),
          "append SQL: %s", qsql(p, &q));
    CHECK(ob_query_preview(p, &q, NULL) == 2 && ob_row_total(p, "Archive") == 0, "append preview: %s", ob_error(p));
    CHECK(ob_query_run(p, &q, NULL, &undoable) == 2 && undoable && ob_row_total(p, "Archive") == 2, "append: %s", ob_error(p));
    CHECK(!strcmp(ob_undo_name(p), "Append Query Keep York: 2 rows in Archive"), "undo name: %s", ob_undo_name(p));
    CHECK(ob_undo(p) && ob_row_total(p, "Archive") == 0, "append undone: %s", ob_error(p));
    /* append with totals: each customer's order count */
    ob_query_init(&q, "Count orders");
    q.kind = OB_QK_APPEND;
    strcpy(q.target, "Archive");
    ob_query_add_table(&q, "Customers");
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Customers", "Name");
    c->total = OB_TOTAL_GROUP;
    strcpy(c->to, "Who");
    c = ob_query_add_column(&q, "Orders", "Total");
    c->total = OB_TOTAL_COUNT;
    strcpy(c->to, "Orders");
    CHECK(ob_query_run(p, &q, NULL, NULL) == 4 && ob_row_total(p, "Archive") == 4, "grouped append: %s", ob_error(p));
    strcpy(q.columns[1].to, "Who");
    CHECK(!ob_query_check(p, &q), "two columns to one field");
    CHECK_ERR(p, "Count orders: two columns go to Who");
    strcpy(q.columns[1].to, "Postcode");
    CHECK(!ob_query_check(p, &q), "no such field");
    CHECK_ERR(p, "Archive has no field Postcode to append to");
    q.columns[0].to[0] = q.columns[1].to[0] = 0;
    CHECK(!ob_query_check(p, &q), "nothing appended");
    CHECK_ERR(p, "give a column the field of Archive it goes to in Append To");
    strcpy(q.columns[1].to, "Orders");
    q.columns[0].to[0] = 0;
    CHECK(!ob_query_check(p, &q), "a required field left out");
    CHECK_ERR(p, "Who must be filled in: give it a column in Append To");

    /* saved, read back, and following a rename of the target */
    ob_query_init(&q, "Keep York");
    q.kind = OB_QK_APPEND;
    strcpy(q.target, "Archive");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Name");
    strcpy(c->to, "Who");
    CHECK(ob_query_save(p, &q), "save an append: %s", ob_error(p));
    CHECK(ob_query_get(p, "Keep York", &back) && back.kind == OB_QK_APPEND && !strcmp(back.target, "Archive")
          && !strcmp(back.columns[0].to, "Who"), "read back: %d %s", back.kind, back.target);
    CHECK(ob_query_save_view(p, &back, "Kept") == 0, "an append is no view");
    CHECK_ERR(p, "Keep York is an Append query");
    CHECK(ob_table_rename(p, "Archive", "Old customers"), "rename: %s", ob_error(p));
    CHECK(ob_query_get(p, "Keep York", &back) && !strcmp(back.target, "Old customers"), "target follows: %s", back.target);
    CHECK(strstr(qsql(p, &back), "INSERT INTO \"Old customers\"(\"Who\")") != NULL, "SQL follows: %s", qsql(p, &back));

    /* typed SQL */
    ob_query_init(&q, "Typed");
    q.kind = OB_QK_UPDATE;
    q.sql_only = 1;
    strcpy(q.sql, "UPDATE Customers SET Credit = Credit + :Extra WHERE Town = 'Leeds';");
    CHECK(ob_query_params(p, &q, names, OB_QUERY_PARAMS) == 1 && !strcmp(names[0], "Extra"), "typed question: %s", names[0]);
    {
        const char *extra[] = { "100" };
        CHECK(ob_query_run(p, &q, extra, &undoable) == 2 && undoable, "typed update: %s", ob_error(p));
        CHECK(ob_undo(p) && !strcmp(cust_rows(p), before), "typed undone: %s", cust_rows(p));
    }
    CHECK(ob_query_save(p, &q) && ob_query_get(p, "Typed", &back) && back.sql_only && back.kind == OB_QK_UPDATE
          && strstr(back.sql, ":Extra"), "typed saved: %d %d %s", back.sql_only, back.kind, back.sql);
    strcpy(q.sql, "DELETE FROM Customers");
    CHECK(!ob_query_check(p, &q), "the wrong kind");
    CHECK_ERR(p, "Typed: an Update query's SQL is UPDATE");
    strcpy(q.sql, "SELECT * FROM Customers");
    CHECK(!ob_query_check(p, &q), "a typed read");
    CHECK_ERR(p, "a query that reads is a Select query");
    strcpy(q.sql, "UPDATE oadb_queries SET sql = ''");
    CHECK(!ob_query_check(p, &q), "the project's own tables");
    CHECK_ERR(p, "oadb_queries isn't one of the project's tables");
    strcpy(q.sql, "DROP TABLE Customers");
    CHECK(!ob_query_check(p, &q), "no DDL");
    CHECK_ERR(p, "it may only add, change or delete rows of one table");
    strcpy(q.sql, "UPDATE Customers SET Credit = 1; DELETE FROM Orders");
    CHECK(!ob_query_check(p, &q), "two statements");
    CHECK_ERR(p, "is one statement");
    strcpy(q.sql, "UPDATE Customers SET Credit = 1 RETURNING Name");
    CHECK(!ob_query_check(p, &q), "RETURNING");
    CHECK_ERR(p, "has no RETURNING");
    q.kind = OB_QK_DELETE;
    strcpy(q.sql, "DELETE FROM Orders WHERE Customer = (SELECT id FROM Customers WHERE Name = 'Cy')");
    CHECK(ob_query_run(p, &q, NULL, &undoable) == 1 && undoable, "typed delete: %s", ob_error(p));
    CHECK(ob_undo(p), "typed delete undone: %s", ob_error(p));
    q.kind = OB_QK_APPEND;
    strcpy(q.sql, "INSERT OR REPLACE INTO \"Old customers\"(id, Who) VALUES(1, 'Dan')");
    CHECK(ob_query_run(p, &q, NULL, &undoable) == 1 && !undoable && ob_undo_count(p) == 0, "REPLACE: no undo: %s", ob_error(p));

    /* a cascade: runs, with no undo */
    {
        static ob_table o2;
        orders(&o2, OB_DELETE_CASCADE);
        strcpy(o2.name, "Lines");
        CHECK(ob_table_create(p, &o2), "Lines: %s", ob_error(p));
        CHECK(ob_row_add(p, "Lines", 2, (const char *[]){ "Customer", "Total" }, (const char *[]){ "4", "1" }, NULL),
              "a line: %s", ob_error(p));
        ob_query_init(&q, "Drop bob");
        q.kind = OB_QK_DELETE;
        ob_query_add_table(&q, "Customers");
        c = ob_query_add_column(&q, "Customers", "Name");
        strcpy(c->criteria[0], "bob");
        CHECK(ob_query_run(p, &q, NULL, &undoable) == 1 && !undoable && ob_undo_count(p) == 0 && ob_row_total(p, "Lines") == 0,
              "cascade: %s", ob_error(p));
        q.sql_only = 1;
        strcpy(q.sql, "DELETE FROM Customers WHERE Name = 'nobody'");
        CHECK(ob_query_check(p, &q), "typed delete with a cascade is one table: %s", ob_error(p));
    }
    ob_close(p);
}

/* ---- Union queries (core 0.16) ---- */

/* A saved query of Customers' Name and second (a field), the rows whose Town is town ("" all). */
static void save_pair(ob_project *p, const char *name, const char *second, const char *town)
{
    static ob_query q;
    ob_qcolumn *c;
    ob_query_init(&q, name);
    ob_query_add_table(&q, "Customers");
    ob_query_add_column(&q, "Customers", "Name");
    ob_query_add_column(&q, "Customers", second);
    if (town[0]) {
        c = ob_query_add_column(&q, "Customers", "Town");
        c->show = 0;
        strcpy(c->criteria[0], town);
    }
    CHECK(ob_query_save(p, &q), "save %s: %s", name, ob_error(p));
}

static void union_of(ob_query *u, const char *name, const char *a, const char *b, int all)
{
    ob_query_init(u, name);
    u->kind = OB_QK_UNION;
    u->union_all = all;
    strcpy(u->tables[0], a);
    strcpy(u->tables[1], b);
    u->ntables = 2;
}

static void test_union_queries(void)
{
    ob_project *p = fresh("union.oadb");
    static ob_query u, back, q;
    static ob_table t;
    ob_qcolumn *c;
    ob_qresult res[OB_QUERY_OUT];
    int undoable = -1, n;
    if (!p) return;
    five_customers(p);
    shop_orders(p);
    CHECK(!strcmp(ob_qkind_name(OB_QK_UNION), "Union") && OB_QK_UNION == 4 && OB_QKINDS == 5, "the kind's name and number");
    save_pair(p, "York", "Town", "york");
    save_pair(p, "Leeds", "Town", "leeds");
    save_pair(p, "Credits", "Credit", "");

    /* two saved queries, sorted by a heading: text without case */
    union_of(&u, "York and Leeds", "York", "Leeds", 0);
    c = ob_query_add_column(&u, NULL, "Name");
    c->sort = OB_SORT_UP;
    CHECK(ob_query_check(p, &u), "a union of two queries: %s", ob_error(p));
    CHECK(!strcmp(qrows(p, &u, NULL, 0, -1), "Ann|Leeds;bob|York;dan|York;Eve|Leeds"), "its rows: %s", qrows(p, &u, NULL, 0, -1));
    CHECK(ob_query_total(p, &u, NULL) == 4, "its total: %lld", (long long)ob_query_total(p, &u, NULL));
    CHECK(!strcmp(qrows(p, &u, NULL, 1, 2), "bob|York;dan|York"), "a page of it: %s", qrows(p, &u, NULL, 1, 2));
    CHECK(!strncmp(qsql(p, &u), "SELECT * FROM (SELECT \"Customers\".\"Name\" AS \"Name\"", 50)
          && strstr(qsql(p, &u), ") UNION SELECT * FROM (SELECT ") && !strstr(qsql(p, &u), "UNION ALL")
          && strstr(qsql(p, &u), ") ORDER BY 1 COLLATE NOCASE") && !strstr(qsql(p, &u), "DESC"), "its SQL: %s", qsql(p, &u));
    n = ob_query_columns(p, &u, res, OB_QUERY_OUT);
    CHECK(n == 2 && !strcmp(res[0].heading, "Name") && !strcmp(res[1].heading, "Town") && res[0].type == OB_TEXT
          && res[1].type == OB_CHOICE, "its columns are the first part's: %d", n);
    CHECK(ob_query_params(p, &u, NULL, 0) == 0, "it asks nothing");
    c->sort = OB_SORT_DOWN;
    CHECK(!strcmp(qrows(p, &u, NULL, 0, -1), "Eve|Leeds;dan|York;bob|York;Ann|Leeds"), "Z to A: %s", qrows(p, &u, NULL, 0, -1));
    strcpy(c->expr, "town");                                /* a heading without case; then by name */
    c->sort = OB_SORT_UP;
    c = ob_query_add_column(&u, NULL, "Name");
    c->sort = OB_SORT_DOWN;
    CHECK(!strcmp(qrows(p, &u, NULL, 0, -1), "Eve|Leeds;Ann|Leeds;dan|York;bob|York"), "two sorts: %s", qrows(p, &u, NULL, 0, -1));
    CHECK(strstr(qsql(p, &u), " ORDER BY 2 COLLATE NOCASE, 1 COLLATE NOCASE DESC"), "two sorts' SQL: %s", qsql(p, &u));

    /* UNION leaves out rows that are the same; UNION ALL keeps them */
    union_of(&u, "York twice", "York", "york", 0);
    CHECK(ob_query_total(p, &u, NULL) == 2, "the same part twice, UNION: %lld (%s)", (long long)ob_query_total(p, &u, NULL), ob_error(p));
    u.union_all = 1;
    CHECK(ob_query_total(p, &u, NULL) == 4 && strstr(qsql(p, &u), ") UNION ALL SELECT * FROM ("), "UNION ALL: %lld",
          (long long)ob_query_total(p, &u, NULL));
    CHECK(!strcmp(qrows(p, &u, NULL, 0, -1), "bob|York;dan|York;bob|York;dan|York")
          || ob_query_total(p, &u, NULL) == 4, "UNION ALL's rows: %s", qrows(p, &u, NULL, 0, -1));

    /* tables as parts: every field, as Table.* shows them; three parts */
    union_of(&u, "Customers thrice", "Customers", "Customers", 1);
    strcpy(u.tables[2], "customers");
    u.ntables = 3;
    CHECK(ob_query_total(p, &u, NULL) == 15, "a table three times: %lld (%s)", (long long)ob_query_total(p, &u, NULL), ob_error(p));
    n = ob_query_columns(p, &u, res, OB_QUERY_OUT);
    CHECK(n == 7 && res[6].type == OB_CURRENCY && res[5].type == OB_YESNO, "a table's fields and their types: %d", n);
    u.union_all = 0;
    c = ob_query_add_column(&u, NULL, "Name");
    c->sort = OB_SORT_UP;
    CHECK(ob_query_total(p, &u, NULL) == 5, "the same rows once: %lld", (long long)ob_query_total(p, &u, NULL));
    CHECK(strstr(qrows(p, &u, NULL, 0, 1), "|Ann|Leeds||2025-12-31|No|1,500.00") != NULL
          || strstr(qrows(p, &u, NULL, 0, 1), "|Ann|Leeds|") != NULL, "a table's row, money as money: %s", qrows(p, &u, NULL, 0, 1));
    CHECK(strstr(qrows(p, &u, NULL, 0, 1), "1,500.00") != NULL && strstr(qrows(p, &u, NULL, 0, 1), "|No|") != NULL,
          "Currency and Yes/No shown in their own form: %s", qrows(p, &u, NULL, 0, 1));

    /* a table with a query: the column counts must agree */
    union_of(&u, "Mixed", "York", "Customers", 0);
    CHECK(!ob_query_check(p, &u), "two columns with seven");
    CHECK_ERR(p, "York shows 2 columns and Customers shows 7");
    /* kind for kind */
    union_of(&u, "Mixed", "York", "Credits", 0);
    CHECK(!ob_query_check(p, &u), "Text with Currency");
    CHECK_ERR(p, "Mixed: column 2 is ");
    CHECK_ERR(p, " in York and Currency in Credits: a union's parts line up kind for kind");
    /* typed SQL's column is shown as the other part's field is, its values as that field stores them */
    ob_query_init(&q, "Typed");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT 'Zed', 99");
    CHECK(ob_query_save(p, &q), "typed SQL saved: %s", ob_error(p));
    union_of(&u, "With typed", "Credits", "Typed", 1);
    c = ob_query_add_column(&u, NULL, "Name");
    c->sort = OB_SORT_DOWN;
    n = ob_query_columns(p, &u, res, OB_QUERY_OUT);
    CHECK(n == 2 && res[0].type == OB_TEXT && res[1].type == OB_CURRENCY && !strcmp(res[1].heading, "Credit limit"),
          "typed SQL takes the field's types: %d %d %d %s (%s)", n, res[0].type, res[1].type, res[1].heading, ob_error(p));
    CHECK(!strcmp(qrows(p, &u, NULL, 0, 2), "Zed|0.99;Eve|-5.00"), "typed SQL's row first, as money: %s", qrows(p, &u, NULL, 0, 2));
    CHECK(ob_query_total(p, &u, NULL) == 6, "six rows: %lld", (long long)ob_query_total(p, &u, NULL));
    union_of(&u, "With typed", "Typed", "Credits", 1);      /* the other way round: its headings are the typed SQL's */
    n = ob_query_columns(p, &u, res, OB_QUERY_OUT);
    CHECK(n == 2 && res[0].type == OB_TEXT && res[1].type == OB_CURRENCY && !strcmp(res[0].heading, "'Zed'"),
          "typed SQL first: %d %d %d %s (%s)", n, res[0].type, res[1].type, res[0].heading, ob_error(p));
    ob_query_init(&q, "Typed two");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT 'Amy' AS Who, 1.5 AS Much");
    CHECK(ob_query_save(p, &q), "more typed SQL saved: %s", ob_error(p));
    union_of(&u, "All typed", "Typed", "Typed two", 0);
    c = ob_query_add_column(&u, NULL, "'Zed'");
    c->sort = OB_SORT_UP;
    n = ob_query_columns(p, &u, res, OB_QUERY_OUT);
    CHECK(n == 2 && res[0].type == -1 && res[1].type == -1, "typed SQL in every part: as SQLite gives it: %d (%s)", n, ob_error(p));
    CHECK(!strcmp(qrows(p, &u, NULL, 0, -1), "Amy|1.5;Zed|99"), "its rows: %s", qrows(p, &u, NULL, 0, -1));

    /* what a union can't be made of */
    ob_query_init(&u, "One");
    u.kind = OB_QK_UNION;
    CHECK(!ob_query_check(p, &u), "no parts");
    CHECK_ERR(p, "two or more tables or queries");
    ob_query_add_table(&u, "York");
    CHECK(!ob_query_check(p, &u), "one part");
    CHECK_ERR(p, "two or more tables or queries: add another");
    union_of(&u, "Missing", "York", "Nowhere", 0);
    CHECK(!ob_query_check(p, &u), "a part that isn't there");
    CHECK_ERR(p, "Missing: there is no table or query named Nowhere");
    CHECK(ob_query_total(p, &u, NULL) == -1 && ob_query_sql(p, &u, (char *)res, sizeof res) == -1
          && ob_query_columns(p, &u, res, OB_QUERY_OUT) == -1, "nothing runs then");
    union_of(&u, "Typed union", "York", "Leeds", 0);
    u.sql_only = 1;
    strcpy(u.sql, "SELECT 1 UNION SELECT 2");
    CHECK(!ob_query_check(p, &u), "typed SQL of kind Union");
    CHECK_ERR(p, "SQL typed as SQL says UNION itself");
    u.kind = OB_QK_SELECT;
    CHECK(ob_query_total(p, &u, NULL) == 2, "typed UNION as a Select query: %s", ob_error(p));
    union_of(&u, "Sorted wrong", "York", "Leeds", 0);
    c = ob_query_add_column(&u, NULL, "Email");
    c->sort = OB_SORT_UP;
    CHECK(!ob_query_check(p, &u), "a sort by a column that isn't shown");
    CHECK_ERR(p, "Sorted wrong has no column Email to sort by");
    c->sort = OB_SORT_NONE;
    CHECK(ob_query_check(p, &u), "a column with no sort is no matter: %s", ob_error(p));
    /* a part that asks a question */
    ob_query_init(&q, "Ask");
    ob_query_add_table(&q, "Customers");
    ob_query_add_column(&q, "Customers", "Name");
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "[Which town?]");
    CHECK(ob_query_save(p, &q), "a query that asks: %s", ob_error(p));
    union_of(&u, "Asking", "York", "Ask", 0);
    CHECK(!ob_query_check(p, &u), "a part that asks");
    CHECK_ERR(p, "Asking: Ask asks a question: a union's parts ask none");
    /* a part that changes rows */
    ob_query_init(&q, "Wipe York");
    q.kind = OB_QK_DELETE;
    ob_query_add_table(&q, "Orders");
    c = ob_query_add_column(&q, "Orders", "Total");
    strcpy(c->criteria[0], "> 1000");
    CHECK(ob_query_save(p, &q), "an action query: %s", ob_error(p));
    union_of(&u, "Acting", "York", "Wipe York", 0);
    CHECK(!ob_query_check(p, &u), "a part that changes rows");
    CHECK_ERR(p, "Wipe York is a Delete query: it changes rows, so a union can't read it");

    /* a union reads: it is opened, not run */
    union_of(&u, "Both towns", "York", "Leeds", 1);
    c = ob_query_add_column(&u, NULL, "Name");
    c->sort = OB_SORT_DOWN;
    CHECK(ob_query_preview(p, &u, NULL) == -1, "no preview of a union");
    CHECK_ERR(p, "Both towns reads rows: open it to see them");
    CHECK(ob_query_run(p, &u, NULL, &undoable) == -1, "a union is not run");

    /* saved and read back; a union is no part of a union */
    CHECK(ob_query_save(p, &u), "saved: %s", ob_error(p));
    memset(&back, 0x55, sizeof back);
    CHECK(ob_query_get(p, "both TOWNS", &back) && back.kind == OB_QK_UNION && back.union_all == 1 && back.ntables == 2
          && !strcmp(back.tables[0], "York") && !strcmp(back.tables[1], "Leeds") && !back.sql_only && back.ncolumns == 1
          && back.columns[0].sort == OB_SORT_DOWN && !strcmp(back.columns[0].expr, "Name") && !back.columns[0].table[0],
          "read back: %s", ob_error(p));
    CHECK(!strcmp(qrows(p, &back, NULL, 0, -1), "Eve|Leeds;dan|York;bob|York;Ann|Leeds"), "read back, its rows: %s",
          qrows(p, &back, NULL, 0, -1));
    u.union_all = 0;
    CHECK(ob_query_save(p, &u) && ob_query_get(p, "Both towns", &back) && back.union_all == 0 && back.kind == OB_QK_UNION,
          "saved again as UNION: %s", ob_error(p));
    {
        ob_query_init(&q, "Raw");
        q.sql_only = 1;
        strcpy(q.sql, "SELECT grid, sql FROM oadb_queries WHERE name = 'Both towns'");
        CHECK(!strncmp(qrows(p, &q, NULL, 0, -1), "oadb query 4\nkind\t4\t\ntable\tYork\ntable\tLeeds\ncolumn\t", 51)
              && strstr(qrows(p, &q, NULL, 0, -1), "|SELECT * FROM (SELECT "), "kept as oadb query 4 with its SQL: %s",
              qrows(p, &q, NULL, 0, -1));
    }
    union_of(&u, "Nested", "Both towns", "York", 0);
    CHECK(!ob_query_check(p, &u), "a union of a union");
    CHECK_ERR(p, "Nested: Both towns is a union itself: add its tables and queries instead");
    union_of(&u, "Both towns", "Both towns", "York", 0);
    CHECK(!ob_query_save(p, &u), "a union of itself");
    CHECK_ERR(p, "is a union itself");

    /* a view and CSV take it as they take a Select query */
    CHECK(ob_query_get(p, "Both towns", &u) && ob_query_save_view(p, &u, "Towns view"), "as a view: %s", ob_error(p));
    ob_query_init(&q, "Raw");
    q.sql_only = 1;
    strcpy(q.sql, "SELECT count(*) FROM \"Towns view\"");
    CHECK(!strcmp(qrows(p, &q, NULL, 0, -1), "4"), "the view's rows: %s", qrows(p, &q, NULL, 0, -1));
    CHECK(ob_sqlview_drop(p, "Towns view"), "view dropped: %s", ob_error(p));
    CHECK(ob_query_csv_export(p, &u, NULL, path("union.csv"), NULL) == 4, "as CSV: %s", ob_error(p));
    CHECK(!strncmp(slurp("union.csv"), "Name,Town\r\nEve,Leeds\r\ndan,York\r\n", 32), "the CSV: %s", slurp("union.csv"));

    /* a saved query's rename follows into the unions that read it */
    CHECK(ob_query_rename(p, "York", "York folk"), "a part renamed: %s", ob_error(p));
    CHECK(ob_query_get(p, "Both towns", &back) && !strcmp(back.tables[0], "York folk") && !strcmp(back.tables[1], "Leeds"),
          "the union follows: %s", back.tables[0]);
    CHECK(ob_query_total(p, &back, NULL) == 4, "and still reads: %s", ob_error(p));
    /* a table's rename too */
    union_of(&u, "People twice", "Customers", "Customers", 1);
    CHECK(ob_query_save(p, &u), "a union of tables saved: %s", ob_error(p));
    CHECK(ob_table_rename(p, "Customers", "Clients"), "the table renamed: %s", ob_error(p));
    CHECK(ob_query_get(p, "People twice", &back) && !strcmp(back.tables[0], "Clients") && !strcmp(back.tables[1], "Clients")
          && back.kind == OB_QK_UNION && back.union_all == 1, "the union of tables follows: %s", back.tables[0]);
    CHECK(ob_query_total(p, &back, NULL) == 10, "ten rows: %lld (%s)", (long long)ob_query_total(p, &back, NULL), ob_error(p));
    CHECK(ob_query_get(p, "Both towns", &back) && ob_query_total(p, &back, NULL) == 4, "the union of queries reads the renamed table: %s",
          ob_error(p));
    CHECK(ob_table_get(p, "Clients", &t), "Clients is there");

    /* a part deleted: the union says so */
    CHECK(ob_query_delete(p, "Leeds"), "a part deleted: %s", ob_error(p));
    CHECK(ob_query_get(p, "Both towns", &back) && !ob_query_check(p, &back), "the union can't read then");
    CHECK_ERR(p, "Both towns: there is no table or query named Leeds");
    ob_close(p);
}

/* ---- The query window's words (core 0.17) ---- */

static void test_query_window(void)
{
    ob_project *p = fresh("qwindow.oadb");
    static ob_query q;
    static ob_table arch, o2;
    ob_qcolumn *c;
    ob_field *f;
    char said[200], before[1024];
    int steps;
    if (!p) return;
    five_customers(p);
    shop_orders(p);
    snprintf(before, sizeof before, "%s", cust_rows(p));
    steps = ob_undo_count(p);

    /* the kind chosen in the window */
    ob_query_init(&q, "Move York");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Town");
    strcpy(c->criteria[0], "york");
    ob_query_set_kind(&q, OB_QK_UPDATE);
    CHECK(q.kind == OB_QK_UPDATE && q.ncolumns == 1 && !strcmp(c->criteria[0], "york"), "an update, its grid kept");
    strcpy(c->to, "Hull");
    strcpy(q.target, "Customers");
    ob_query_set_kind(&q, OB_QK_UPDATE);
    CHECK(!strcmp(c->to, "Hull") && !strcmp(q.target, "Customers"), "the same kind again changes nothing");

    /* the sentence before Run */
    strcpy(said, "x");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 2 && !strcmp(said, "This will change 2 rows in Customers."),
          "two rows: %s / %s", said, ob_error(p));
    CHECK(!strcmp(cust_rows(p), before) && ob_undo_count(p) == steps, "asking keeps nothing: %s", cust_rows(p));
    strcpy(c->criteria[0], "hull");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 1 && !strcmp(said, "This will change 1 row in Customers."),
          "one row: %s", said);
    CHECK(ob_query_ask(p, &q, NULL, NULL, 0) == 1, "only the count");
    strcpy(c->criteria[0], "york");
    strcpy(c->to, "Paris");
    strcpy(said, "x");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) < 0 && !said[0], "a rule broken: nothing said");
    CHECK_ERR(p, "Town must be one of: Leeds,York,Hull");
    strcpy(c->to, "[Where to?]");
    {
        const char *hull[] = { "Hull" };
        CHECK(ob_query_ask(p, &q, hull, said, sizeof said) == 2 && !strcmp(said, "This will change 2 rows in Customers."),
              "with an answer: %s / %s", said, ob_error(p));
    }
    strcpy(c->to, "Hull");
    q.columns[0].criteria[0][0] = 0;
    c = ob_query_add_column(&q, "Customers", "Name");
    strcpy(c->criteria[0], "nobody");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 0 && !strcmp(said, "Move York changes no rows."), "none: %s", said);
    q.name[0] = 0;
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 0 && !strcmp(said, "The query changes no rows."), "no name: %s", said);
    strcpy(q.name, "Move York");

    /* to a delete: Update To goes, the target stays */
    ob_query_set_kind(&q, OB_QK_DELETE);
    CHECK(q.kind == OB_QK_DELETE && !q.columns[0].to[0] && !strcmp(q.target, "Customers") && q.ncolumns == 2, "a delete");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 0 && !strcmp(said, "Move York deletes no rows."), "deletes none: %s", said);
    strcpy(q.columns[1].criteria[0], "bob");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 1 && !strcmp(said, "This will delete 1 row from Customers."),
          "delete one: %s / %s", said, ob_error(p));
    strcpy(q.columns[1].criteria[0], "dan");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) < 0 && !said[0], "dan has orders");
    CHECK_ERR(p, "That would leave a row looking up a row that isn't there");
    CHECK(!strcmp(cust_rows(p), before), "still nothing kept: %s", cust_rows(p));

    /* to an append: into another table */
    ob_table_init(&arch, "Archive");
    f = ob_table_add_field(&arch, "Who", OB_TEXT);
    f->required = 1;
    CHECK(ob_table_create(p, &arch), "Archive: %s", ob_error(p));
    ob_query_set_kind(&q, OB_QK_APPEND);
    strcpy(q.target, "Archive");
    q.columns[1].criteria[0][0] = 0;
    strcpy(q.columns[1].to, "Who");
    strcpy(q.columns[0].criteria[0], "york");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 2 && !strcmp(said, "This will add 2 rows to Archive."),
          "append: %s / %s", said, ob_error(p));
    CHECK(ob_row_total(p, "Archive") == 0, "none added by asking");
    strcpy(q.columns[1].criteria[0], "nobody");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 0 && !strcmp(said, "Move York adds no rows."), "adds none: %s", said);

    /* to a query that reads: no target, no Append To; asking is refused */
    ob_query_set_kind(&q, OB_QK_SELECT);
    CHECK(q.kind == OB_QK_SELECT && !q.target[0] && !q.columns[1].to[0], "a select again");
    strcpy(said, "x");
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) < 0 && !said[0], "a select isn't run");
    CHECK_ERR(p, "Move York reads rows: open it to see them");
    q.njoins = 1;
    q.union_all = 1;
    ob_query_set_kind(&q, OB_QK_UNION);
    CHECK(q.kind == OB_QK_UNION && q.njoins == 0 && q.union_all == 1, "a union has no joins");
    ob_query_set_kind(&q, (ob_qkind)99);
    CHECK(q.kind == OB_QK_SELECT && q.union_all == 0, "a kind that isn't one is a select");

    /* a delete that cascades can't be undone, and the sentence says so */
    orders(&o2, OB_DELETE_CASCADE);
    strcpy(o2.name, "Lines");
    CHECK(ob_table_create(p, &o2), "Lines: %s", ob_error(p));
    ob_query_init(&q, "Drop bob");
    ob_query_add_table(&q, "Customers");
    c = ob_query_add_column(&q, "Customers", "Name");
    strcpy(c->criteria[0], "bob");
    ob_query_set_kind(&q, OB_QK_DELETE);
    CHECK(ob_query_ask(p, &q, NULL, said, sizeof said) == 1
          && !strcmp(said, "This will delete 1 row from Customers. It can't be undone."), "cascade: %s / %s", said, ob_error(p));
    CHECK(!strcmp(cust_rows(p), before), "and still nothing kept: %s", cust_rows(p));
    ob_close(p);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(dir, sizeof dir, "%s", argv[1]);
    } else {
#ifdef __amigaos__
        printf("usage: test-base-core DIR (a scratch drawer, such as RAM:Test or DH1:Test)\n");
        return 2;
#else
        char t[] = "/tmp/obtestXXXXXX";
        if (!mkdtemp(t)) { perror("mkdtemp"); return 2; }
        snprintf(dir, sizeof dir, "%s", t);
#endif
    }
    {
        size_t n = strlen(dir);
        if (n && (dir[n - 1] == '/' || dir[n - 1] == ':')) dir[n - 1] = dir[n - 1] == ':' ? ':' : 0;
    }
    test_currency();
    test_values();
    test_sql();
    test_project();
    test_rows();
    test_change();
    test_lookups();
    test_keys();
    test_indexes();
    test_relations();
    test_cache();
    test_views();
    test_form_filter();
    test_csv();
    test_undo();
    test_csv_options();
    test_grid();
    test_action_queries();
    test_union_queries();
    test_query_window();
    test_relation_draw();
    test_dbf();
    test_rexx_rows();
    test_rexx_form_guard();
    test_queries();
    test_query_out();
    test_change_rename_looked_up();
    test_designed_forms();
    test_form_editing();
    test_form_buttons();
    test_query_joins();
    test_forms();
    test_not_projects();
    test_cut_short();
    printf("test-base-core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
