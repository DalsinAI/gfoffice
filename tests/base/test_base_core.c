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
    test_not_projects();
    test_cut_short();
    printf("test-base-core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
