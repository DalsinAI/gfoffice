/*
 * GFOpenCalc's database sources for External Data (include/opencalc_extdb.h).
 * A GFOpenBase project is read through GFOpenBase's core (ob_open read only,
 * ob_table_get and ob_rows_at for a table, ob_query_get and ob_query_rows for
 * a saved query, a view or a query typed in), so each value comes in its
 * field's own form; any other SQLite file is read with SQLite itself.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "opencalc_extdb.h"
#include "openbase_core.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

#define EXTDB_PAGE 512                   /* rows asked of GFOpenBase at once */
#define EXTDB_COLS 256                   /* a result's columns, at most */

/* The values read so far: rows of ncols, a heading row first when asked for. */
typedef struct {
    unsigned ncols, rows, cap;           /* cap: rows room is kept for */
    oc_extvalue *v;
    int kinds[EXTDB_COLS];               /* how each column's text is put in: OC_EXTV_TEXT, OC_EXTV_INPUT */
    int failed;                          /* OWF_ERR_MEMORY, OWF_ERR_UNSUPPORTED */
} extdb_rows;

static void say(char *err, size_t errsize, const char *fmt, const char *a)
{
    if (err && errsize)
        snprintf(err, errsize, fmt, a ? a : "");
}

static void rows_free(extdb_rows *r)
{
    unsigned long i;
    for (i = 0; i < (unsigned long)r->rows * r->ncols; i++)
        owf_free(r->v[i].text);
    owf_free(r->v);
    r->v = NULL;
    r->rows = r->cap = 0;
}

/* A new row, all empty; NULL (r->failed set) when there is no room or too many. */
static oc_extvalue *rows_add(extdb_rows *r)
{
    oc_extvalue *row;
    if (r->failed)
        return NULL;
    if (r->rows >= OC_EXTDB_ROWS || (unsigned long)(r->rows + 1) * r->ncols > (unsigned long)OC_MAX_RANGE_CELLS) {
        r->failed = OWF_ERR_UNSUPPORTED;
        return NULL;
    }
    if (r->rows == r->cap) {
        unsigned cap = r->cap ? r->cap * 2 : 64;
        oc_extvalue *more = owf_realloc(r->v, (size_t)cap * r->ncols * sizeof *more);
        if (!more) {
            r->failed = OWF_ERR_MEMORY;
            return NULL;
        }
        r->v = more;
        r->cap = cap;
    }
    row = &r->v[(size_t)r->rows * r->ncols];
    memset(row, 0, r->ncols * sizeof *row);
    r->rows++;
    return row;
}

static int put_text(extdb_rows *r, oc_extvalue *v, int kind, const char *text)
{
    size_t n;
    if (!text || !*text)
        return 1;
    n = strlen(text);
    if (!(v->text = owf_malloc(n + 1))) {
        r->failed = OWF_ERR_MEMORY;
        return 0;
    }
    memcpy(v->text, text, n + 1);
    v->kind = kind;
    return 1;
}

/* The values into a table (r is emptied). */
static int rows_table(extdb_rows *r, oc_exttable *out)
{
    out->rows = out->cols = 0;
    out->cells = NULL;
    if (r->failed) {
        int rc = r->failed;
        rows_free(r);
        return rc;
    }
    if (!r->rows || !r->ncols) {
        rows_free(r);
        return OWF_OK;
    }
    out->rows = r->rows;
    out->cols = r->ncols;
    out->cells = r->v;
    r->v = NULL;
    r->rows = r->cap = 0;
    return OWF_OK;
}

/* "2026-11-16", "2026-11-16 13:45:00", "13:45", "13:45:30": put in as typed (a date or time). */
static int iso_date_time(const char *s)
{
    int i;
    static const char *const shapes[] = { "dddd-dd-dd", "dddd-dd-dd dd:dd", "dddd-dd-dd dd:dd:dd", "dddd-dd-ddTdd:dd:dd",
                                          "dd:dd", "dd:dd:dd" };
    for (i = 0; i < (int)(sizeof shapes / sizeof shapes[0]); i++) {
        const char *p = shapes[i], *q = s;
        for (; *p && *q; p++, q++)
            if (*p == 'd' ? !isdigit((unsigned char)*q) : *p != *q)
                break;
        if (!*p && !*q)
            return 1;
    }
    return 0;
}

/* A plain number as SQLite writes one ("12", "-3.5", "1.5e+10"). */
static int plain_number(const char *s)
{
    char *end;
    if (!*s || isspace((unsigned char)*s))
        return 0;
    (void)strtod(s, &end);
    return !*end;
}

static int starts_word(const char *s, const char *word)
{
    size_t n = strlen(word), i;
    for (i = 0; i < n; i++)
        if (toupper((unsigned char)s[i]) != word[i])
            return 0;
    return !isalnum((unsigned char)s[n]) && s[n] != '_';
}

/* A query typed in, not a name. */
static int is_sql(const char *item)
{
    return starts_word(item, "SELECT") || starts_word(item, "WITH") || starts_word(item, "VALUES");
}

/* ---- GFOpenBase projects ---- */

static int base_row(void *user, ob_int key, int n, const char *const *values)
{
    extdb_rows *r = user;
    oc_extvalue *row;
    unsigned c;
    (void)key;
    if (!(row = rows_add(r)))
        return 1;
    for (c = 0; c < r->ncols && (int)c < n; c++) {
        const char *s = values[c];
        int kind = r->kinds[c];
        if (!s || !*s)
            continue;
        if (kind < 0)                    /* an expression's: as SQLite gives it */
            kind = plain_number(s) || iso_date_time(s) ? OC_EXTV_INPUT : OC_EXTV_TEXT;
        if (!put_text(r, &row[c], kind, s))
            return 1;
    }
    return 0;
}

/* How a field's values are put in: numbers, dates and times as typed, text kept as text. */
static int field_kind(int type)
{
    switch (type) {
    case OB_COUNTER: case OB_INTEGER: case OB_NUMBER: case OB_CURRENCY: case OB_DATE: case OB_TIME:
    case OB_DATETIME: case OB_LOOKUP:
        return OC_EXTV_INPUT;
    case -1:
        return -1;
    default:
        return OC_EXTV_TEXT;
    }
}

/* A row of headings: n names, the first at first, each stride bytes after the one before. */
static int add_headings(extdb_rows *r, const char *first, unsigned n, size_t stride)
{
    oc_extvalue *row = rows_add(r);
    unsigned c;
    if (!row)
        return 0;
    for (c = 0; c < n; c++)
        if (!put_text(r, &row[c], OC_EXTV_TEXT, first + c * stride))
            return 0;
    return 1;
}

/* A query's rows (q: a saved one or SQL). */
static int base_query(ob_project *p, ob_query *q, int headers, oc_exttable *out, char *err, size_t errsize)
{
    ob_qresult *cols = owf_calloc(EXTDB_COLS, sizeof *cols);
    char (*params)[OB_VALUE_MAX] = owf_malloc(sizeof *params);
    extdb_rows r;
    int n, i;
    memset(&r, 0, sizeof r);
    if (!cols || !params) {
        owf_free(cols);
        owf_free(params);
        return OWF_ERR_MEMORY;
    }
    if (!ob_query_check(p, q) || (n = ob_query_columns(p, q, cols, EXTDB_COLS)) < 0) {
        say(err, errsize, "%s", ob_error(p));
        owf_free(cols);
        owf_free(params);
        return OWF_ERR_FORMAT;
    }
    if (ob_query_params(p, q, params, 1) > 0) {
        say(err, errsize, "The query asks for a value (%s): External Data cannot give it one", params[0]);
        owf_free(cols);
        owf_free(params);
        return OWF_ERR_FORMAT;
    }
    owf_free(params);
    if (n > EXTDB_COLS)
        n = EXTDB_COLS;
    r.ncols = (unsigned)n;
    for (i = 0; i < n; i++)
        r.kinds[i] = field_kind(cols[i].type);
    if (headers && n && !add_headings(&r, cols[0].heading, (unsigned)n, sizeof *cols)) {
        owf_free(cols);
        return rows_table(&r, out);
    }
    owf_free(cols);
    if (n && ob_query_rows(p, q, NULL, 0, -1, base_row, &r) < 0 && !r.failed) {
        say(err, errsize, "%s", ob_error(p));
        rows_free(&r);
        return OWF_ERR_FORMAT;
    }
    if (r.failed == OWF_ERR_UNSUPPORTED)
        say(err, errsize, "%s gives too many rows for a sheet", q->name[0] ? q->name : "The query");
    return rows_table(&r, out);
}

static int base_table(ob_project *p, const ob_table *t, int headers, oc_exttable *out, char *err, size_t errsize)
{
    extdb_rows r;
    ob_int first = 0;
    int i, got;
    memset(&r, 0, sizeof r);
    r.ncols = (unsigned)(t->nfields < EXTDB_COLS ? t->nfields : EXTDB_COLS);
    for (i = 0; i < (int)r.ncols; i++)
        r.kinds[i] = t->fields[i].type == OB_OBJECT ? OC_EXTV_EMPTY : field_kind(t->fields[i].type);
    if (headers && r.ncols && !add_headings(&r, t->fields[0].name, r.ncols, sizeof t->fields[0]))
        return rows_table(&r, out);
    do {
        unsigned before = r.rows;
        got = ob_rows_at(p, t->name, first, EXTDB_PAGE, base_row, &r);
        if (got < 0 && !r.failed) {
            say(err, errsize, "%s", ob_error(p));
            rows_free(&r);
            return OWF_ERR_FORMAT;
        }
        first += (ob_int)(r.rows - before);
    } while (got == EXTDB_PAGE && !r.failed);
    /* a Datatype object is no cell's value */
    for (i = 0; i < (int)r.ncols && !r.failed; i++)
        if (r.kinds[i] == OC_EXTV_EMPTY) {
            unsigned k;
            for (k = headers ? 1u : 0u; k < r.rows; k++) {
                oc_extvalue *v = &r.v[(size_t)k * r.ncols + (unsigned)i];
                owf_free(v->text);
                v->text = NULL;
                v->kind = OC_EXTV_EMPTY;
            }
        }
    if (r.failed == OWF_ERR_UNSUPPORTED)
        say(err, errsize, "%s has too many rows for a sheet", t->name);
    return rows_table(&r, out);
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return !*a && !*b;
}

static int read_base(ob_project *p, const char *item, int headers, oc_exttable *out, char *err, size_t errsize)
{
    ob_query *q = owf_calloc(1, sizeof *q);
    ob_table *t = NULL;
    int rc = OWF_ERR_FORMAT, i;
    if (!q)
        return OWF_ERR_MEMORY;
    if (is_sql(item)) {
        if (strlen(item) >= sizeof q->sql) {
            say(err, errsize, "The query is too long%s", NULL);
            owf_free(q);
            return OWF_ERR_FORMAT;
        }
        ob_query_init(q, "");
        q->sql_only = 1;
        memcpy(q->sql, item, strlen(item) + 1);
        rc = base_query(p, q, headers, out, err, errsize);
    } else if (ob_query_get(p, item, q)) {
        rc = base_query(p, q, headers, out, err, errsize);
    } else if (!(t = owf_calloc(1, sizeof *t))) {
        rc = OWF_ERR_MEMORY;
    } else if (ob_table_get(p, item, t)) {
        rc = base_table(p, t, headers, out, err, errsize);
    } else {
        int n = ob_sqlview_count(p);
        for (i = 0; i < n; i++) {
            const char *v = ob_sqlview_name(p, i);
            if (v && same_name(v, item)) {
                char quoted[OB_NAME_MAX * 2 + 24];
                size_t at = 0, k;
                at = (size_t)snprintf(quoted, sizeof quoted, "SELECT * FROM \"");
                for (k = 0; v[k] && at + 4 < sizeof quoted; k++) {
                    if (v[k] == '"')
                        quoted[at++] = '"';
                    quoted[at++] = v[k];
                }
                quoted[at++] = '"';
                quoted[at] = 0;
                ob_query_init(q, "");
                q->sql_only = 1;
                memcpy(q->sql, quoted, at + 1);
                rc = base_query(p, q, headers, out, err, errsize);
                break;
            }
        }
        if (i == n)
            say(err, errsize, "There is no table or query called %s", item);
    }
    owf_free(t);
    owf_free(q);
    return rc;
}

/* ---- other SQLite files ---- */

static int read_sqlite(sqlite3 *db, const char *item, int headers, oc_exttable *out, char *err, size_t errsize)
{
    sqlite3_stmt *st = NULL;
    const char *tail = NULL;
    extdb_rows r;
    int rc, c, n;
    memset(&r, 0, sizeof r);
    if (is_sql(item)) {
        rc = sqlite3_prepare_v2(db, item, -1, &st, &tail);
    } else {
        char *sql = sqlite3_mprintf("SELECT * FROM \"%w\"", item);
        sqlite3_stmt *find = NULL;
        int found = 0;
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_schema WHERE type IN ('table', 'view') AND name = ?1 "
                                   "COLLATE NOCASE AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\'", -1, &find, NULL) == SQLITE_OK) {
            sqlite3_bind_text(find, 1, item, -1, SQLITE_STATIC);
            found = sqlite3_step(find) == SQLITE_ROW;
        }
        sqlite3_finalize(find);
        if (!found) {
            sqlite3_free(sql);
            say(err, errsize, "There is no table or view called %s", item);
            return OWF_ERR_FORMAT;
        }
        rc = sql ? sqlite3_prepare_v2(db, sql, -1, &st, NULL) : SQLITE_NOMEM;
        sqlite3_free(sql);
    }
    if (rc != SQLITE_OK || !st) {
        say(err, errsize, "%s", rc == SQLITE_OK ? "The query is empty" : sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return rc == SQLITE_NOMEM ? OWF_ERR_MEMORY : OWF_ERR_FORMAT;
    }
    while (tail && isspace((unsigned char)*tail))
        tail++;
    if (tail && *tail && *tail != ';') {
        say(err, errsize, "Give one query only%s", NULL);
        sqlite3_finalize(st);
        return OWF_ERR_FORMAT;
    }
    if (!sqlite3_stmt_readonly(st)) {
        say(err, errsize, "Only a query that reads can be run%s", NULL);
        sqlite3_finalize(st);
        return OWF_ERR_FORMAT;
    }
    if (sqlite3_bind_parameter_count(st)) {
        say(err, errsize, "The query asks for values: External Data cannot give them%s", NULL);
        sqlite3_finalize(st);
        return OWF_ERR_FORMAT;
    }
    n = sqlite3_column_count(st);
    r.ncols = (unsigned)(n < EXTDB_COLS ? n : EXTDB_COLS);
    if (headers && r.ncols) {
        oc_extvalue *row = rows_add(&r);
        for (c = 0; row && c < (int)r.ncols; c++)
            if (!put_text(&r, &row[c], OC_EXTV_TEXT, sqlite3_column_name(st, c)))
                break;
    }
    while (!r.failed && (rc = sqlite3_step(st)) == SQLITE_ROW) {
        oc_extvalue *row = rows_add(&r);
        for (c = 0; row && c < (int)r.ncols; c++) {
            switch (sqlite3_column_type(st, c)) {
            case SQLITE_INTEGER:
            case SQLITE_FLOAT:
                row[c].kind = OC_EXTV_NUMBER;
                row[c].number = sqlite3_column_double(st, c);
                break;
            case SQLITE_TEXT: {
                const char *s = (const char *)sqlite3_column_text(st, c);
                if (s && !put_text(&r, &row[c], iso_date_time(s) ? OC_EXTV_INPUT : OC_EXTV_TEXT, s))
                    c = (int)r.ncols;
                break;
            }
            default:                     /* NULL, a blob: no value */
                break;
            }
        }
    }
    if (!r.failed && rc != SQLITE_DONE) {
        say(err, errsize, "%s", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        rows_free(&r);
        return OWF_ERR_FORMAT;
    }
    sqlite3_finalize(st);
    if (r.failed == OWF_ERR_UNSUPPORTED)
        say(err, errsize, "%s gives too many rows for a sheet", item);
    return rows_table(&r, out);
}

/* A SQLite file opened read only, NULL (the reason in err) when it is not one. */
static sqlite3 *open_sqlite(const char *path, char *err, size_t errsize)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        say(err, errsize, "Could not open %s", path);
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_busy_timeout(db, 2000);
    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM sqlite_schema", -1, &st, NULL) != SQLITE_OK ||
        sqlite3_step(st) != SQLITE_ROW) {
        say(err, errsize, "%s is not a database", path);
        sqlite3_finalize(st);
        sqlite3_close(db);
        return NULL;
    }
    sqlite3_finalize(st);
    return db;
}

static void trim(const char *in, char *out, size_t size)
{
    size_t n;
    while (isspace((unsigned char)*in))
        in++;
    snprintf(out, size, "%s", in);
    n = strlen(out);
    while (n && (isspace((unsigned char)out[n - 1]) || out[n - 1] == ';'))
        out[--n] = 0;
}

int oc_extdb_query(void *user, const char *database, const char *item, int headers, oc_exttable *out, char *err,
                   size_t errsize)
{
    char why[OB_ERR_MAX], *what;
    ob_project *p;
    sqlite3 *db;
    int rc;
    size_t len;
    (void)user;
    if (err && errsize)
        err[0] = 0;
    if (!out)
        return OWF_ERR_UNSUPPORTED;
    out->rows = out->cols = 0;
    out->cells = NULL;
    if (!database || !*database || !item) {
        say(err, errsize, "No database was given%s", NULL);
        return OWF_ERR_FORMAT;
    }
    len = strlen(item) + 1;
    if (!(what = owf_malloc(len)))
        return OWF_ERR_MEMORY;
    trim(item, what, len);
    if (!*what) {
        owf_free(what);
        say(err, errsize, "No table or query was given%s", NULL);
        return OWF_ERR_FORMAT;
    }
    why[0] = 0;
    if ((p = ob_open(database, OB_OPEN_READONLY, why, sizeof why)) != NULL) {
        rc = read_base(p, what, headers, out, err, errsize);
        ob_close(p);
    } else if ((db = open_sqlite(database, err, errsize)) != NULL) {
        rc = read_sqlite(db, what, headers, out, err, errsize);
        sqlite3_close(db);
    } else {
        if (why[0] && strstr(why, "not a GFOpenBase project") == NULL)
            say(err, errsize, "%s", why);
        rc = OWF_ERR_IO;
    }
    owf_free(what);
    return rc;
}

static int item_order(const void *a, const void *b)
{
    const oc_extdb_item *x = a, *y = b;
    const char *p = x->name, *q = y->name;
    if (x->kind != y->kind)
        return x->kind < y->kind ? -1 : 1;
    for (; *p && *q; p++, q++)
        if (tolower((unsigned char)*p) != tolower((unsigned char)*q))
            return tolower((unsigned char)*p) - tolower((unsigned char)*q);
    return (unsigned char)*p - (unsigned char)*q;
}

static void add_item(oc_extdb_item *items, int max, int *n, int kind, const char *name)
{
    if (*n < max) {
        items[*n].kind = kind;
        snprintf(items[*n].name, sizeof items[*n].name, "%s", name);
    }
    (*n)++;
}

int oc_extdb_items(const char *database, oc_extdb_item *items, int max, char *err, size_t errsize)
{
    char why[OB_ERR_MAX];
    ob_project *p;
    sqlite3 *db;
    int n = 0, i, count;
    if (err && errsize)
        err[0] = 0;
    if (!items)
        max = 0;
    why[0] = 0;
    if (!database || !*database) {
        say(err, errsize, "No database was given%s", NULL);
        return -1;
    }
    if ((p = ob_open(database, OB_OPEN_READONLY, why, sizeof why)) != NULL) {
        count = ob_table_count(p);
        for (i = 0; i < count; i++)
            add_item(items, max, &n, OC_EXTDB_TABLE, ob_table_name(p, i));
        count = ob_sqlview_count(p);
        for (i = 0; i < count; i++)
            add_item(items, max, &n, OC_EXTDB_VIEW, ob_sqlview_name(p, i));
        count = ob_query_count(p);
        for (i = 0; i < count; i++)
            add_item(items, max, &n, OC_EXTDB_QUERY, ob_query_name(p, i));
        ob_close(p);
    } else if ((db = open_sqlite(database, err, errsize)) != NULL) {
        sqlite3_stmt *st = NULL;
        if (sqlite3_prepare_v2(db, "SELECT type, name FROM sqlite_schema WHERE type IN ('table', 'view') AND "
                                   "name NOT LIKE 'sqlite\\_%' ESCAPE '\\'", -1, &st, NULL) == SQLITE_OK)
            while (sqlite3_step(st) == SQLITE_ROW)
                add_item(items, max, &n, !strcmp((const char *)sqlite3_column_text(st, 0), "view") ? OC_EXTDB_VIEW
                                                                                                : OC_EXTDB_TABLE,
                         (const char *)sqlite3_column_text(st, 1));
        sqlite3_finalize(st);
        sqlite3_close(db);
    } else {
        if (why[0] && strstr(why, "not a GFOpenBase project") == NULL)
            say(err, errsize, "%s", why);
        return -1;
    }
    if (items)
        qsort(items, (size_t)(n < max ? n : max), sizeof *items, item_order);
    return n;
}
