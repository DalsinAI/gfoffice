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
    test_csv();
    test_undo();
    test_csv_options();
    test_queries();
    test_forms();
    test_not_projects();
    test_cut_short();
    printf("test-base-core: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
