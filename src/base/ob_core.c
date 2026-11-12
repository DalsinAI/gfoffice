/*
 * GFOpenBase's portable core: projects, tables and rows (include/openbase_core.h).
 * Design: docs/gfoffice/Design-OpenBase.md, sections 3 and 4.
 *
 * The design of each table lives twice: as SQL (so SQLite enforces the rules
 * and any SQLite tool reads the data) and as rows of oadb_fields (what SQL
 * can't say: the GFOpenBase type, the label, the text form of a default).
 * ob_table_get reads both; a column with no oadb_fields row (a table made by
 * another program) gets a type from its declared SQL type.
 *
 * Every rule a field has is a named constraint ("ob_len_Name"), so SQLite's
 * "CHECK constraint failed: ob_len_Name" turns back into "Name must be at most
 * 20 characters".
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sqlite3.h>
#include "openbase_core.h"

#define OB_MAX_TABLES 256
#define OB_UNDO_MAX 100

struct undo_step;

/* The rollback journal's mode for every project (Design-OpenBase.md 3.2: the
 * B0 probe's numbers are in section 11.3). */
#ifndef OB_JOURNAL_MODE
#define OB_JOURNAL_MODE "TRUNCATE"
#endif

struct ob_project {
    sqlite3 *db;
    char err[OB_ERR_MAX];
    char title[OB_VALUE_MAX];
    int in_txn;
    int readonly;
    int ntables;
    int tables_valid;
    char tables[OB_MAX_TABLES][OB_NAME_MAX];
    /* the last table's design read by a row or table call (design()), on
     * the heap: an ob_table is too big for a 64 KB stack */
    ob_table *cache;
    int cache_ok;
    ob_int cache_version;
    /* undo: the steps, oldest first */
    struct undo_step *undo[OB_UNDO_MAX];
    int nundo;
    int undo_off;           /* > 0: row calls record no steps (an import records one) */
    int undo_at_begin;      /* nundo when ob_begin opened the transaction */
    char undo_name[OB_NAME_MAX + 48];
};

static void undo_drop_top(ob_project *p);

static const char *const type_names[OB_TYPE_COUNT] = {
    "Counter", "Text", "Memo", "Integer", "Number", "Currency", "Date", "Time",
    "Date and time", "Yes/No", "Choice", "Lookup", "Datatype object"
};

static const char *const sql_types[OB_TYPE_COUNT] = {
    "INTEGER", "TEXT", "TEXT", "INTEGER", "REAL", "INTEGER", "TEXT", "TEXT",
    "TEXT", "INTEGER", "TEXT", "INTEGER", "BLOB"
};

static const char *const delete_rules[3] = { "RESTRICT", "CASCADE", "SET NULL" };

/* ---- Small helpers ---- */

static void copy(char *out, size_t size, const char *in)
{
    size_t n;
    if (!size) return;
    n = in ? strlen(in) : 0;
    if (n >= size) n = size - 1;
    if (n) memcpy(out, in, n);
    out[n] = 0;
}

static int fail(ob_project *p, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
static int fail(ob_project *p, const char *fmt, ...)
{
    va_list ap;
    if (!p) return 0;
    va_start(ap, fmt);
    vsnprintf(p->err, sizeof p->err, fmt, ap);
    va_end(ap);
    return 0;
}

static int ok(ob_project *p)
{
    p->err[0] = 0;
    return 1;
}

static int same(const char *a, const char *b)
{
    return sqlite3_stricmp(a, b) == 0;
}

static int starts_nocase(const char *s, const char *prefix)
{
    return sqlite3_strnicmp(s, prefix, (int)strlen(prefix)) == 0;
}

const char *ob_type_name(ob_type t)
{
    return (unsigned)t < OB_TYPE_COUNT ? type_names[t] : "?";
}

int ob_type_from_name(const char *name)
{
    int i;
    for (i = 0; i < OB_TYPE_COUNT; i++)
        if (name && same(name, type_names[i])) return i;
    return -1;
}

static const char *field_label(const ob_field *f)
{
    return f->label[0] ? f->label : f->name;
}

/* ---- Values ---- */

static const char *skip_space(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Text with spaces trimmed from both ends, into out. */
static void trimmed(const char *in, char *out, size_t size)
{
    size_t n;
    in = skip_space(in ? in : "");
    copy(out, size, in);
    n = strlen(out);
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = 0;
}

/* An integer, with optional sign and thousands commas ("-1,234"). */
static int parse_int(const char *s, ob_int *out)
{
    unsigned long long v = 0;
    int neg = 0, digits = 0, group = -1;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    for (; *s; s++) {
        if (*s == ',') {
            if (!digits || (group >= 0 && group != 3)) return 0;
            group = 0;
            continue;
        }
        if (!isdigit((unsigned char)*s)) return 0;
        if (v > (9223372036854775807ULL - 9) / 10) return 0;
        v = v * 10 + (unsigned)(*s - '0');
        digits++;
        if (group >= 0) group++;
    }
    if (!digits || (group >= 0 && group != 3)) return 0;
    *out = neg ? -(ob_int)v : (ob_int)v;
    return 1;
}

int ob_currency_parse(const char *text, ob_int *pence)
{
    char t[64], whole[64];
    const char *s, *dot;
    ob_int w;
    int neg = 0, frac = 0, nf = 0;
    trimmed(text, t, sizeof t);
    s = t;
    if (*s == '-') { neg = 1; s++; }
    s = skip_space(s);
    if (*s == '\xa3' || *s == '$') s++;              /* a pound (ISO-8859-1) or dollar sign */
    else if ((unsigned char)s[0] == 0xc2 && (unsigned char)s[1] == 0xa3) s += 2;    /* UTF-8 pound */
    if (*s == '-' && !neg) { neg = 1; s++; }
    if (!isdigit((unsigned char)*s) && !(*s == '.' && isdigit((unsigned char)s[1]))) return 0;
    dot = strchr(s, '.');
    copy(whole, sizeof whole, s);
    if (dot) whole[dot - s] = 0;
    if (!whole[0]) copy(whole, sizeof whole, "0");
    if (whole[0] == '-' || whole[0] == '+' || !parse_int(whole, &w)) return 0;
    if (dot) {
        for (dot++; *dot; dot++) {
            if (!isdigit((unsigned char)*dot) || nf == 2) return 0;
            frac = frac * 10 + (*dot - '0');
            nf++;
        }
        if (nf == 1) frac *= 10;
    }
    if (w > (9223372036854775807LL - 99) / 100) return 0;
    *pence = (w * 100 + frac) * (neg ? -1 : 1);
    return 1;
}

void ob_currency_format(ob_int pence, char *out, size_t size)
{
    char digits[32], buf[48];
    unsigned long long v = pence < 0 ? 0ULL - (unsigned long long)pence : (unsigned long long)pence;
    int n, i, j = 0;
    snprintf(digits, sizeof digits, "%llu", v / 100);
    n = (int)strlen(digits);
    if (pence < 0) buf[j++] = '-';
    for (i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0) buf[j++] = ',';
        buf[j++] = digits[i];
    }
    snprintf(buf + j, sizeof buf - (size_t)j, ".%02u", (unsigned)(v % 100));
    copy(out, size, buf);
}

/* A decimal number as SQLite reads one: [sign] digits [. digits] [e [sign] digits],
 * with thousands commas allowed in the whole part (taken out into out). */
static int parse_number(const char *s, char *out, size_t size)
{
    char t[80];
    int j = 0, digits = 0;
    if (*s == '-' || *s == '+') t[j++] = *s++;
    for (; *s && j < (int)sizeof t - 1; s++) {
        if (isdigit((unsigned char)*s)) { t[j++] = *s; digits++; }
        else if (*s == ',' && digits) continue;
        else break;
    }
    if (*s == '.' && j < (int)sizeof t - 1) {
        t[j++] = *s++;
        while (isdigit((unsigned char)*s) && j < (int)sizeof t - 1) { t[j++] = *s++; digits++; }
    }
    if (!digits) return 0;
    if ((*s == 'e' || *s == 'E') && j < (int)sizeof t - 3) {
        int ed = 0;
        t[j++] = *s++;
        if (*s == '-' || *s == '+') t[j++] = *s++;
        while (isdigit((unsigned char)*s) && j < (int)sizeof t - 1) { t[j++] = *s++; ed++; }
        if (!ed) return 0;
    }
    if (*s) return 0;
    t[j] = 0;
    copy(out, size, t);
    return 1;
}

static int days_in(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

static int read_num(const char **s, int min, int max, int *out)
{
    int n = 0, v = 0;
    while (isdigit((unsigned char)**s) && n < max) { v = v * 10 + (**s - '0'); (*s)++; n++; }
    *out = v;
    return n >= min;
}

/* YYYY-MM-DD, or DD/MM/YYYY (as the UK writes a date), into YYYY-MM-DD. */
static int parse_date(const char **s, char out[16])
{
    int y, m, d;
    const char *t = *s;
    if (read_num(&t, 4, 4, &y) && *t == '-') {
        t++;
        if (!read_num(&t, 1, 2, &m) || *t++ != '-' || !read_num(&t, 1, 2, &d)) return 0;
    } else {
        t = *s;
        if (!read_num(&t, 1, 2, &d) || *t++ != '/' || !read_num(&t, 1, 2, &m) || *t++ != '/'
            || !read_num(&t, 4, 4, &y)) return 0;
    }
    if (y < 1 || m < 1 || m > 12 || d < 1 || d > days_in(y, m)) return 0;
    snprintf(out, 16, "%04u-%02u-%02u", (unsigned)y % 10000u, (unsigned)m % 100u, (unsigned)d % 100u);
    *s = t;
    return 1;
}

/* H:MM or H:MM:SS into HH:MM:SS. */
static int parse_time(const char **s, char out[16])
{
    int h, m, sec = 0;
    const char *t = *s;
    if (!read_num(&t, 1, 2, &h) || *t++ != ':' || !read_num(&t, 2, 2, &m)) return 0;
    if (*t == ':') {
        t++;
        if (!read_num(&t, 2, 2, &sec)) return 0;
    }
    if (h > 23 || m > 59 || sec > 59) return 0;
    snprintf(out, 16, "%02u:%02u:%02u", (unsigned)h % 100u, (unsigned)m % 100u, (unsigned)sec % 100u);
    *s = t;
    return 1;
}

static int parse_yesno(const char *s, int *v)
{
    static const char *const yes[] = { "yes", "y", "true", "on", "1", "x" };
    static const char *const no[] = { "no", "n", "false", "off", "0" };
    unsigned i;
    for (i = 0; i < sizeof yes / sizeof yes[0]; i++) if (same(s, yes[i])) { *v = 1; return 1; }
    for (i = 0; i < sizeof no / sizeof no[0]; i++) if (same(s, no[i])) { *v = 0; return 1; }
    return 0;
}

/* The choice in list ('|' separated) that s names, ignoring case, into out. */
static int find_choice(const char *list, const char *s, char *out, size_t size)
{
    const char *a = list;
    size_t n = strlen(s);
    while (*a) {
        const char *b = strchr(a, '|');
        size_t len = b ? (size_t)(b - a) : strlen(a);
        if (len == n && sqlite3_strnicmp(a, s, (int)n) == 0) {
            if (len >= size) return 0;
            memcpy(out, a, len);
            out[len] = 0;
            return 1;
        }
        if (!b) break;
        a = b + 1;
    }
    return 0;
}

/* The kinds a stored value can be, for binding. */
enum { V_NULL, V_INT, V_TEXT, V_REAL_TEXT };

typedef struct value {
    int kind;
    ob_int i;
    char text[512];
    const char *long_text;      /* Text and Memo beyond text[]: the caller's own */
} value;

/* Turns text in f's own form into what is stored. 0 with the reason in err. */
static int convert(const ob_field *f, const char *in, value *v, char *err, size_t errsize)
{
    char t[128];
    const char *s;
    const char *label = field_label(f);
    v->kind = V_NULL;
    v->long_text = NULL;
    if (f->type == OB_TEXT || f->type == OB_MEMO) {
        if (!in || !*in) goto empty;
        if (f->type == OB_TEXT && f->max_len > 0 && (int)strlen(in) > f->max_len) {
            snprintf(err, errsize, "%s must be at most %d characters", label, f->max_len);
            return 0;
        }
        v->kind = V_TEXT;
        if (strlen(in) < sizeof v->text) copy(v->text, sizeof v->text, in);
        else v->long_text = in;
        return 1;
    }
    trimmed(in, t, sizeof t);
    if (in && strlen(skip_space(in)) >= sizeof t) {
        snprintf(err, errsize, "%s is too long for a %s field", label, ob_type_name(f->type));
        return 0;
    }
    if (!t[0]) goto empty;
    s = t;
    switch (f->type) {
    case OB_COUNTER:
    case OB_INTEGER:
    case OB_LOOKUP:
        if (!parse_int(t, &v->i)) {
            snprintf(err, errsize, "%s must be a whole number", label);
            return 0;
        }
        v->kind = V_INT;
        return 1;
    case OB_NUMBER:
        if (!parse_number(t, v->text, sizeof v->text)) {
            snprintf(err, errsize, "%s must be a number", label);
            return 0;
        }
        v->kind = V_REAL_TEXT;
        return 1;
    case OB_CURRENCY:
        if (!ob_currency_parse(t, &v->i)) {
            snprintf(err, errsize, "%s must be an amount of money, such as 1,234.50", label);
            return 0;
        }
        v->kind = V_INT;
        return 1;
    case OB_DATE: {
        char d[16];
        if (!parse_date(&s, d) || *s) {
            snprintf(err, errsize, "%s must be a date, such as 2026-11-11 or 11/11/2026", label);
            return 0;
        }
        copy(v->text, sizeof v->text, d);
        v->kind = V_TEXT;
        return 1;
    }
    case OB_TIME: {
        char h[16];
        if (!parse_time(&s, h) || *s) {
            snprintf(err, errsize, "%s must be a time, such as 14:30 or 14:30:15", label);
            return 0;
        }
        copy(v->text, sizeof v->text, h);
        v->kind = V_TEXT;
        return 1;
    }
    case OB_DATETIME: {
        char d[16], h[16] = "00:00:00";
        if (!parse_date(&s, d) || (*s && (*s != ' ' || (s = skip_space(s), !parse_time(&s, h)))) || *s) {
            snprintf(err, errsize, "%s must be a date and time, such as 2026-11-11 14:30", label);
            return 0;
        }
        snprintf(v->text, sizeof v->text, "%s %s", d, h);
        v->kind = V_TEXT;
        return 1;
    }
    case OB_YESNO: {
        int b;
        if (!parse_yesno(t, &b)) {
            snprintf(err, errsize, "%s must be Yes or No", label);
            return 0;
        }
        v->i = b;
        v->kind = V_INT;
        return 1;
    }
    case OB_CHOICE:
        if (!find_choice(f->choices, t, v->text, sizeof v->text)) {
            char list[OB_VALUE_MAX];
            size_t i;
            copy(list, sizeof list, f->choices);
            for (i = 0; list[i]; i++) if (list[i] == '|') list[i] = ',';
            snprintf(err, errsize, "%.60s must be one of: %.150s", label, list);
            return 0;
        }
        v->kind = V_TEXT;
        return 1;
    case OB_OBJECT:
        snprintf(err, errsize, "%s holds a Datatype object: load a file into it", label);
        return 0;
    default:
        snprintf(err, errsize, "%s has an unknown type", label);
        return 0;
    }
empty:
    if ((f->required || f->key) && f->type != OB_COUNTER) {
        snprintf(err, errsize, "%s must be filled in", label);
        return 0;
    }
    return 1;
}

int ob_value_check(const ob_field *f, const char *text, char *err, size_t errsize)
{
    value v;
    char e[OB_ERR_MAX];
    if (!convert(f, text, &v, e, sizeof e)) {
        copy(err, errsize, e);
        return 0;
    }
    copy(err, errsize, "");
    return 1;
}

static int bind_value(sqlite3_stmt *st, int i, const value *v)
{
    switch (v->kind) {
    case V_INT: return sqlite3_bind_int64(st, i, v->i);
    case V_TEXT:
    case V_REAL_TEXT:   /* REAL affinity turns the text into a double: no floating point here */
        return sqlite3_bind_text(st, i, v->long_text ? v->long_text : v->text, -1, SQLITE_TRANSIENT);
    default: return sqlite3_bind_null(st, i);
    }
}

/* ---- Errors in plain words ---- */

static const ob_field *field_named(const ob_table *t, const char *name)
{
    int i = ob_table_find_field(t, name);
    return i >= 0 ? &t->fields[i] : NULL;
}

/* SQLite's message for rc on table t, in plain words, into p->err. */
static int sql_fail(ob_project *p, const ob_table *t, int rc)
{
    const char *m = sqlite3_errmsg(p->db);
    const char *c;
    const ob_field *f;
    int ext = sqlite3_extended_errcode(p->db);
    if ((rc & 0xff) == SQLITE_CONSTRAINT || (ext & 0xff) == SQLITE_CONSTRAINT) {
        if ((c = strstr(m, "CHECK constraint failed: ob_")) != NULL) {
            const char *rule = c + 27, *name = strchr(rule, '_');
            if (name && t && (f = field_named(t, name + 1)) != NULL) {
                if (!strncmp(rule, "len_", 4))
                    return fail(p, "%s must be at most %d characters", field_label(f), f->max_len);
                if (!strncmp(rule, "choice_", 7))
                    return fail(p, "%s must be one of its choices", field_label(f));
                return fail(p, "%s must be %s", field_label(f),
                            f->type == OB_YESNO ? "Yes or No" : f->type == OB_DATE ? "a date"
                            : f->type == OB_TIME ? "a time" : "a date and time");
            }
        }
        if ((c = strstr(m, "NOT NULL constraint failed: ")) != NULL && (c = strchr(c + 28, '.')) != NULL)
            return fail(p, "%s must be filled in", t && (f = field_named(t, c + 1)) ? field_label(f) : c + 1);
        if ((c = strstr(m, "UNIQUE constraint failed: ")) != NULL && strchr(c + 26, '.') != NULL) {
            /* "tbl.a" or "tbl.a, tbl.b": the fields by their labels */
            char list[OB_ERR_MAX], one[OB_NAME_MAX];
            const char *a = c + 26;
            int k = 0;
            list[0] = 0;
            while (a && *a) {
                const char *dot = strchr(a, '.'), *end = strstr(a, ", ");
                size_t len;
                if (!dot || (end && dot > end)) break;
                len = end ? (size_t)(end - dot - 1) : strlen(dot + 1);
                if (len >= sizeof one) len = sizeof one - 1;
                memcpy(one, dot + 1, len);
                one[len] = 0;
                if (k++) strncat(list, end ? ", " : " and ", sizeof list - strlen(list) - 1);
                strncat(list, t && (f = field_named(t, one)) ? field_label(f) : one, sizeof list - strlen(list) - 1);
                a = end ? end + 2 : NULL;
            }
            if (k > 1) return fail(p, "%s must be different in every row, taken together", list);
            return fail(p, "%s must be different in every row", list);
        }
        if (strstr(m, "FOREIGN KEY"))
            return fail(p, "That would leave a row looking up a row that isn't there");
    }
    if ((rc & 0xff) == SQLITE_READONLY) return fail(p, "The project is open read only");
    if ((rc & 0xff) == SQLITE_BUSY) return fail(p, "The project is in use by another program");
    if ((rc & 0xff) == SQLITE_FULL) return fail(p, "The disk is full");
    if ((rc & 0xff) == SQLITE_CORRUPT || (rc & 0xff) == SQLITE_NOTADB) return fail(p, "The project file is damaged");
    if ((rc & 0xff) == SQLITE_NOMEM) return fail(p, "Not enough memory");
    if ((rc & 0xff) == SQLITE_IOERR) return fail(p, "The disk could not be read or written");
    return fail(p, "%s", m);
}

/* The table list and the design cache are read again on next use. */
static void forget(ob_project *p)
{
    p->tables_valid = 0;
    p->cache_ok = 0;
}

/* ---- SQL helpers ---- */

static int run(ob_project *p, const char *sql)
{
    int rc = sqlite3_exec(p->db, sql, NULL, NULL, NULL);
    return rc == SQLITE_OK ? 1 : sql_fail(p, NULL, rc);
}

static int runf(ob_project *p, const char *fmt, ...)
{
    va_list ap;
    char *sql;
    int r;
    va_start(ap, fmt);
    sql = sqlite3_vmprintf(fmt, ap);
    va_end(ap);
    if (!sql) return fail(p, "Not enough memory");
    r = run(p, sql);
    sqlite3_free(sql);
    return r;
}

static sqlite3_stmt *prep(ob_project *p, const char *sql)
{
    sqlite3_stmt *st = NULL;
    int rc = sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) { sql_fail(p, NULL, rc); return NULL; }
    return st;
}

static ob_int scalar(ob_project *p, const char *sql, ob_int dflt)
{
    sqlite3_stmt *st = prep(p, sql);
    ob_int v = dflt;
    if (!st) return dflt;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

/* ---- Projects ---- */

static const char *const design_sql =
    "CREATE TABLE IF NOT EXISTS oadb_meta(key TEXT PRIMARY KEY, value);"
    "CREATE TABLE IF NOT EXISTS oadb_fields("
    " tbl TEXT NOT NULL COLLATE NOCASE, field TEXT NOT NULL COLLATE NOCASE, type TEXT NOT NULL,"
    " required INTEGER NOT NULL DEFAULT 0, uniq INTEGER NOT NULL DEFAULT 0,"
    " max_len INTEGER NOT NULL DEFAULT 0, has_default INTEGER NOT NULL DEFAULT 0, deflt TEXT,"
    " label TEXT, choices TEXT, lookup TEXT, on_delete INTEGER NOT NULL DEFAULT 0,"
    " PRIMARY KEY(tbl, field));";

static int make_design(ob_project *p)
{
    if (!run(p, "BEGIN")) return 0;
    if (!runf(p, "PRAGMA application_id=%ld; PRAGMA user_version=%d;", (long)OB_APPLICATION_ID, OB_FORMAT_VERSION)
        || !run(p, design_sql)
        || !run(p, "INSERT OR IGNORE INTO oadb_meta VALUES('format', 1)")
        || !run(p, "COMMIT")) {
        char keep[OB_ERR_MAX];
        copy(keep, sizeof keep, p->err);
        sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
        copy(p->err, sizeof p->err, keep);
        return 0;
    }
    return 1;
}

ob_project *ob_open(const char *path, int flags, char *err, size_t errsize)
{
    ob_project *p = calloc(1, sizeof *p);
    int oflags, rc;
    ob_int app, ver, objects;
    char q[16];
    if (!p) { copy(err, errsize, "Not enough memory"); return NULL; }
    p->readonly = (flags & OB_OPEN_READONLY) != 0;
    oflags = p->readonly ? SQLITE_OPEN_READONLY
                         : SQLITE_OPEN_READWRITE | ((flags & OB_OPEN_CREATE) ? SQLITE_OPEN_CREATE : 0);
    rc = sqlite3_open_v2(path, &p->db, oflags, (flags & OB_OPEN_NOLOCK) ? "unix-none" : NULL);
    if (rc != SQLITE_OK) {
        if ((rc & 0xff) == SQLITE_CANTOPEN) snprintf(p->err, sizeof p->err, "%s could not be opened", path);
        else sql_fail(p, NULL, rc);
        goto bad;
    }
    sqlite3_extended_result_codes(p->db, 1);
    app = scalar(p, "PRAGMA application_id", -1);
    if (app == -1) {
        /* not a database at all, or damaged */
        snprintf(p->err, sizeof p->err, "%s is not a GFOpenBase project", path);
        goto bad;
    }
    ver = scalar(p, "PRAGMA user_version", 0);
    objects = scalar(p, "SELECT count(*) FROM sqlite_schema", -1);
    if (objects < 0) {
        snprintf(p->err, sizeof p->err, "%s is not a GFOpenBase project", path);
        goto bad;
    }
    if (app == 0 && (objects == 0 || (flags & OB_OPEN_ADOPT))) {
        if (p->readonly) {
            snprintf(p->err, sizeof p->err, "%s is not a GFOpenBase project yet: open it to write once", path);
            goto bad;
        }
        if (objects == 0 && !(flags & OB_OPEN_CREATE)) {
            snprintf(p->err, sizeof p->err, "%s is empty", path);
            goto bad;
        }
        if (!make_design(p)) goto bad;
    } else if (app == 0) {
        snprintf(p->err, sizeof p->err, "%s is an SQLite database, not a GFOpenBase project", path);
        goto bad;
    } else if (app != OB_APPLICATION_ID) {
        snprintf(p->err, sizeof p->err, "%s is not a GFOpenBase project", path);
        goto bad;
    } else if (ver > OB_FORMAT_VERSION) {
        snprintf(p->err, sizeof p->err, "%s was made by a newer GFOpenBase", path);
        goto bad;
    }
    {
        sqlite3_stmt *st = prep(p, "PRAGMA quick_check");
        q[0] = 0;
        if (st && sqlite3_step(st) == SQLITE_ROW) copy(q, sizeof q, (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
        if (strcmp(q, "ok")) {
            snprintf(p->err, sizeof p->err, "%s is damaged (quick_check)", path);
            goto bad;
        }
    }
    if (scalar(p, "SELECT count(*) FROM sqlite_schema WHERE name = 'oadb_fields'", 0) != 1) {
        snprintf(p->err, sizeof p->err, "%s is not a whole GFOpenBase project (no oadb_fields)", path);
        goto bad;
    }
    run(p, "PRAGMA foreign_keys=ON");
    if (!p->readonly) run(p, "PRAGMA journal_mode=" OB_JOURNAL_MODE);
    copy(err, errsize, "");
    ok(p);
    return p;
bad:
    copy(err, errsize, p->err[0] ? p->err : "The project could not be opened");
    sqlite3_close(p->db);
    free(p->cache);
    free(p);
    return NULL;
}

void ob_close(ob_project *p)
{
    if (!p) return;
    if (p->in_txn) sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
    ob_undo_clear(p);
    sqlite3_close(p->db);
    free(p->cache);
    free(p);
}

const char *ob_error(const ob_project *p)
{
    return p ? p->err : "No project";
}

const char *ob_title(ob_project *p)
{
    sqlite3_stmt *st = prep(p, "SELECT value FROM oadb_meta WHERE key = 'title'");
    p->title[0] = 0;
    if (st && sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0))
        copy(p->title, sizeof p->title, (const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    ok(p);
    return p->title;
}

int ob_set_title(ob_project *p, const char *title)
{
    return runf(p, "INSERT OR REPLACE INTO oadb_meta VALUES('title', %Q)", title ? title : "") && ok(p);
}

int ob_check(ob_project *p, int full)
{
    sqlite3_stmt *st = prep(p, full ? "PRAGMA integrity_check" : "PRAGMA quick_check");
    int whole = 0;
    if (!st) return 0;
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0))
        whole = !strcmp((const char *)sqlite3_column_text(st, 0), "ok");
    sqlite3_finalize(st);
    return whole ? ok(p) : fail(p, "The project file is damaged");
}

int ob_copy(ob_project *p, const char *path)
{
    if (p->in_txn) return fail(p, "Save or undo the changes first");
    if (path) return runf(p, "VACUUM INTO %Q", path) && ok(p);
    return run(p, "VACUUM") && ok(p);
}

/* ---- Transactions ---- */

int ob_begin(ob_project *p)
{
    if (p->in_txn) return fail(p, "A change is already open");
    if (!run(p, "BEGIN IMMEDIATE")) return 0;
    p->in_txn = 1;
    p->undo_at_begin = p->nundo;
    return ok(p);
}

int ob_commit(ob_project *p)
{
    if (!p->in_txn) return fail(p, "No change is open");
    if (!run(p, "COMMIT")) return 0;
    p->in_txn = 0;
    return ok(p);
}

int ob_rollback(ob_project *p)
{
    if (!p->in_txn) return fail(p, "No change is open");
    sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
    p->in_txn = 0;
    forget(p);
    /* the steps the transaction recorded are gone with it; if it undid older
     * ones, those came back, and the list no longer says what the file holds */
    if (p->nundo < p->undo_at_begin) ob_undo_clear(p);
    else while (p->nundo > p->undo_at_begin) undo_drop_top(p);
    return ok(p);
}

int ob_in_transaction(const ob_project *p)
{
    return p->in_txn;
}

/* A change of several statements: inside the caller's transaction a
 * savepoint, else a transaction of its own. */
static int step_begin(ob_project *p)
{
    return run(p, p->in_txn ? "SAVEPOINT ob_step" : "BEGIN IMMEDIATE");
}

static int step_end(ob_project *p, int good)
{
    char keep[OB_ERR_MAX];
    if (good) {
        if (run(p, p->in_txn ? "RELEASE ob_step" : "COMMIT")) return 1;
    }
    copy(keep, sizeof keep, p->err);
    if (p->in_txn) {
        sqlite3_exec(p->db, "ROLLBACK TO ob_step", NULL, NULL, NULL);
        sqlite3_exec(p->db, "RELEASE ob_step", NULL, NULL, NULL);
    } else {
        sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
    }
    copy(p->err, sizeof p->err, keep);
    forget(p);
    return 0;
}

/* ---- Tables ---- */

static int load_tables(ob_project *p)
{
    sqlite3_stmt *st;
    if (p->tables_valid) return 1;
    st = prep(p, "SELECT name FROM sqlite_schema WHERE type = 'table' AND name NOT LIKE 'oadb\\_%' ESCAPE '\\'"
                 " AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name COLLATE NOCASE");
    p->ntables = 0;
    if (!st) return 0;
    while (sqlite3_step(st) == SQLITE_ROW && p->ntables < OB_MAX_TABLES)
        copy(p->tables[p->ntables++], OB_NAME_MAX, (const char *)sqlite3_column_text(st, 0));
    sqlite3_finalize(st);
    p->tables_valid = 1;
    return 1;
}

int ob_table_count(ob_project *p)
{
    return load_tables(p) ? p->ntables : 0;
}

const char *ob_table_name(ob_project *p, int i)
{
    if (!load_tables(p) || i < 0 || i >= p->ntables) return NULL;
    return p->tables[i];
}

static int table_exists(ob_project *p, const char *name)
{
    int i, n = ob_table_count(p);
    for (i = 0; i < n; i++) if (same(p->tables[i], name)) return 1;
    return 0;
}

/* Table name's design, from the cache when the schema hasn't changed since
 * it was read (another program's change bumps schema_version too). The
 * pointer is good until the next call that reads a design. */
static const ob_table *design(ob_project *p, const char *name)
{
    ob_int v = scalar(p, "PRAGMA schema_version", -1);
    if (p->cache_ok && v == p->cache_version && same(p->cache->name, name)) return p->cache;
    if (!p->cache && !(p->cache = malloc(sizeof *p->cache))) {
        fail(p, "Not enough memory");
        return NULL;
    }
    p->cache_ok = 0;
    if (!ob_table_get(p, name, p->cache)) return NULL;
    p->cache_ok = 1;
    p->cache_version = v;
    return p->cache;
}

void ob_table_init(ob_table *t, const char *name)
{
    memset(t, 0, sizeof *t);
    copy(t->name, sizeof t->name, name);
    ob_table_add_field(t, "id", OB_COUNTER);
}

ob_field *ob_table_add_field(ob_table *t, const char *name, ob_type type)
{
    ob_field *f;
    if (t->nfields >= OB_MAX_FIELDS) return NULL;
    f = &t->fields[t->nfields++];
    memset(f, 0, sizeof *f);
    copy(f->name, sizeof f->name, name);
    f->type = type;
    return f;
}

int ob_table_find_field(const ob_table *t, const char *name)
{
    int i;
    for (i = 0; i < t->nfields; i++) if (same(t->fields[i].name, name)) return i;
    return -1;
}

void ob_table_remove_index(ob_table *t, int i)
{
    if (i < 0 || i >= t->nindexes) return;
    memmove(&t->indexes[i], &t->indexes[i + 1], sizeof t->indexes[0] * (size_t)(t->nindexes - i - 1));
    t->nindexes--;
}

void ob_table_remove_field(ob_table *t, int i)
{
    int k, j, m;
    if (i < 0 || i >= t->nfields) return;
    for (k = 0; k < t->nindexes; k++) {
        ob_index *x = &t->indexes[k];
        for (j = m = 0; j < x->nfields; j++)
            if (!same(x->fields[j], t->fields[i].name)) {
                if (m != j) memcpy(x->fields[m], x->fields[j], OB_NAME_MAX);
                m++;
            }
        x->nfields = m;
        if (m == 0) ob_table_remove_index(t, k--);
    }
    memmove(&t->fields[i], &t->fields[i + 1], sizeof t->fields[0] * (size_t)(t->nfields - i - 1));
    t->nfields--;
}

void ob_table_rename_field(ob_table *t, int i, const char *name)
{
    char old[OB_NAME_MAX];
    int k, j;
    if (i < 0 || i >= t->nfields) return;
    copy(old, sizeof old, t->fields[i].name);
    for (k = 0; k < t->nindexes; k++)
        for (j = 0; j < t->indexes[k].nfields; j++)
            if (same(t->indexes[k].fields[j], old)) copy(t->indexes[k].fields[j], OB_NAME_MAX, name);
    copy(t->fields[i].name, sizeof t->fields[i].name, name);
}

ob_index *ob_table_add_index(ob_table *t, const char *name, int unique, const char *fields)
{
    ob_index *x;
    const char *a = fields ? fields : "";
    if (t->nindexes >= OB_MAX_INDEXES) return NULL;
    x = &t->indexes[t->nindexes];
    memset(x, 0, sizeof *x);
    copy(x->name, sizeof x->name, name);
    x->unique = unique != 0;
    for (;;) {
        const char *b = strchr(a, ',');
        size_t len = b ? (size_t)(b - a) : strlen(a);
        char one[OB_NAME_MAX * 2], name1[OB_NAME_MAX * 2];
        if (len >= sizeof one) len = sizeof one - 1;
        memcpy(one, a, len);
        one[len] = 0;
        trimmed(one, name1, sizeof name1);
        if (name1[0]) {
            if (x->nfields >= OB_INDEX_FIELDS) return NULL;
            copy(x->fields[x->nfields++], OB_NAME_MAX, name1);
        }
        if (!b) break;
        a = b + 1;
    }
    t->nindexes++;
    return x;
}

void ob_index_fields_text(const ob_index *x, char *out, size_t size)
{
    int j;
    size_t n = 0;
    if (!size) return;
    out[0] = 0;
    for (j = 0; j < x->nfields && n + 1 < size; j++) {
        snprintf(out + n, size - n, "%s%s", j ? ", " : "", x->fields[j]);
        n = strlen(out);
    }
}

/* Whether an index named name is on a table other than tbl (or old). */
static int index_elsewhere(ob_project *p, const char *name, const char *tbl, const char *old)
{
    sqlite3_stmt *st = prep(p, "SELECT tbl_name FROM sqlite_schema WHERE type = 'index' AND name = ?1 COLLATE NOCASE");
    int r = 0;
    if (!st) return 0;
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *n = (const char *)sqlite3_column_text(st, 0);
        if (n && !same(n, tbl) && !(old && same(n, old))) r = 1;
    }
    sqlite3_finalize(st);
    return r;
}

/* A name GFOpenBase gives a table or field: a letter, then letters, digits,
 * spaces and '_', at most OB_NAME_MAX - 1; not oadb_ or sqlite_ (theirs). */
static int name_ok(const char *s, char *why, size_t n, const char *what)
{
    size_t i, len = strlen(s);
    if (!len) { snprintf(why, n, "The %s needs a name", what); return 0; }
    if (len >= OB_NAME_MAX) { snprintf(why, n, "%s: a name is at most %d characters", s, OB_NAME_MAX - 1); return 0; }
    if (!isalpha((unsigned char)s[0])) { snprintf(why, n, "%s: a name starts with a letter", s); return 0; }
    for (i = 0; i < len; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != ' ' && s[i] != '_') {
            snprintf(why, n, "%s: a name is letters, digits, spaces and _", s);
            return 0;
        }
    if (s[len - 1] == ' ') { snprintf(why, n, "%s: a name can't end with a space", s); return 0; }
    if (starts_nocase(s, "oadb_") || starts_nocase(s, "sqlite_")) {
        snprintf(why, n, "%s: names starting oadb_ or sqlite_ are kept for the project", s);
        return 0;
    }
    if (same(s, "rowid") || same(s, "_rowid_") || same(s, "oid")) {
        snprintf(why, n, "%s is a name SQLite keeps for itself", s);
        return 0;
    }
    return 1;
}

/* old_name: the table t replaces (its indexes' names are free), or NULL. */
static int check_design(ob_project *p, const ob_table *t, const char *old_name)
{
    int i, j, k, counters = 0, keys = 0;
    char why[OB_ERR_MAX];
    if (!name_ok(t->name, why, sizeof why, "table")) return fail(p, "%s", why);
    if (t->nfields < 1) return fail(p, "%s needs at least one field", t->name);
    if (t->nfields > OB_MAX_FIELDS) return fail(p, "%s has more than %d fields", t->name, OB_MAX_FIELDS);
    for (i = 0; i < t->nfields; i++) {
        const ob_field *f = &t->fields[i];
        if (!name_ok(f->name, why, sizeof why, "field")) return fail(p, "%s", why);
        for (j = 0; j < i; j++)
            if (same(f->name, t->fields[j].name)) return fail(p, "%s has two fields named %s", t->name, f->name);
        if ((unsigned)f->type >= OB_TYPE_COUNT) return fail(p, "%s has an unknown type", f->name);
        if (f->type == OB_COUNTER && ++counters > 1) return fail(p, "%s can have only one Counter", t->name);
        if (f->type == OB_TEXT && (f->max_len < 0 || f->max_len > OB_TEXT_MAX_LEN))
            return fail(p, "%s: a maximum length is 1 to %d", f->name, OB_TEXT_MAX_LEN);
        if (f->type == OB_CHOICE) {
            const char *c = f->choices;
            if (!c[0]) return fail(p, "%s needs its choices", f->name);
            if (strchr(c, '\'') || c[0] == '|' || c[strlen(c) - 1] == '|' || strstr(c, "||"))
                return fail(p, "%s: choices are separated by |, without quotes or empty ones", f->name);
        }
        if (f->key) {
            keys++;
            if (f->type == OB_MEMO || f->type == OB_OBJECT) return fail(p, "%s can't be part of the key", f->name);
        }
        if (f->type == OB_LOOKUP) {
            const ob_table *other;
            if (!f->lookup[0]) return fail(p, "%s needs the table it looks up", f->name);
            if (!same(f->lookup, t->name)) {
                if (!p || (other = design(p, f->lookup)) == NULL)
                    return fail(p, "%s looks up %s, which isn't a table", f->name, f->lookup);
                for (j = 0; j < other->nfields && other->fields[j].type != OB_COUNTER; j++) ;
                if (j == other->nfields) return fail(p, "%s looks up %s, which has no Counter", f->name, f->lookup);
            } else {
                for (j = 0; j < t->nfields && t->fields[j].type != OB_COUNTER; j++) ;
                if (j == t->nfields) return fail(p, "%s looks up %s, which has no Counter", f->name, f->lookup);
            }
            if ((unsigned)f->on_delete > OB_DELETE_CLEAR) return fail(p, "%s has an unknown delete rule", f->name);
            if (f->on_delete == OB_DELETE_CLEAR && f->required)
                return fail(p, "%s is required, so it can't be cleared when its row goes", f->name);
        }
        if (f->has_default) {
            value v;
            if (f->type == OB_COUNTER || f->type == OB_OBJECT)
                return fail(p, "%s can't have a default", f->name);
            if (!convert(f, f->deflt, &v, why, sizeof why)) return fail(p, "Default: %s", why);
        }
    }
    if (counters && keys) return fail(p, "%s has a Counter, which is its key: take Key off its other fields", t->name);
    if (!counters && !keys) return fail(p, "%s needs a Counter or key fields", t->name);
    if (t->nindexes < 0 || t->nindexes > OB_MAX_INDEXES)
        return fail(p, "%s has more than %d indexes", t->name, OB_MAX_INDEXES);
    for (k = 0; k < t->nindexes; k++) {
        const ob_index *x = &t->indexes[k];
        if (!name_ok(x->name, why, sizeof why, "index")) return fail(p, "%s", why);
        for (j = 0; j < k; j++)
            if (same(x->name, t->indexes[j].name)) return fail(p, "%s has two indexes named %s", t->name, x->name);
        if (same(x->name, t->name) || (p && table_exists(p, x->name)))
            return fail(p, "Index %s: there is a table with that name", x->name);
        if (p && index_elsewhere(p, x->name, t->name, old_name))
            return fail(p, "There is an index named %s on another table", x->name);
        if (x->nfields < 1) return fail(p, "Index %s needs at least one field", x->name);
        if (x->nfields > OB_INDEX_FIELDS) return fail(p, "Index %s has more than %d fields", x->name, OB_INDEX_FIELDS);
        for (i = 0; i < x->nfields; i++) {
            int fi = ob_table_find_field(t, x->fields[i]);
            if (fi < 0) return fail(p, "Index %s: %s has no field %s", x->name, t->name, x->fields[i]);
            if (t->fields[fi].type == OB_OBJECT) return fail(p, "Index %s can't use %s, a Datatype object", x->name, x->fields[i]);
            for (j = 0; j < i; j++)
                if (same(x->fields[i], x->fields[j])) return fail(p, "Index %s has %s twice", x->name, x->fields[i]);
        }
    }
    return p ? ok(p) : 1;
}

int ob_table_check(ob_project *p, const ob_table *t)
{
    return check_design(p, t, NULL);
}

/* One column's definition, appended to s. */
static void column_sql(sqlite3_str *s, const ob_field *f)
{
    sqlite3_str_appendf(s, "\"%w\" %s", f->name, sql_types[f->type]);
    if (f->type == OB_COUNTER) {
        sqlite3_str_appendall(s, " PRIMARY KEY");
        return;
    }
    if (f->required || f->key) sqlite3_str_appendall(s, " NOT NULL");
    if (f->unique) sqlite3_str_appendall(s, " UNIQUE");
    if (f->has_default) {
        value v;
        char e[OB_ERR_MAX];
        if (convert(f, f->deflt, &v, e, sizeof e)) {
            if (v.kind == V_INT) sqlite3_str_appendf(s, " DEFAULT %lld", v.i);
            else if (v.kind == V_REAL_TEXT) sqlite3_str_appendf(s, " DEFAULT %s", v.text);
            else if (v.kind == V_TEXT) sqlite3_str_appendf(s, " DEFAULT %Q", v.long_text ? v.long_text : v.text);
        }
    }
    switch (f->type) {
    case OB_TEXT:
        if (f->max_len > 0)
            sqlite3_str_appendf(s, " CONSTRAINT \"ob_len_%w\" CHECK (length(\"%w\") <= %d)", f->name, f->name, f->max_len);
        break;
    case OB_DATE:
        sqlite3_str_appendf(s, " CONSTRAINT \"ob_date_%w\" CHECK (\"%w\" IS NULL OR \"%w\" IS date(\"%w\"))",
                            f->name, f->name, f->name, f->name);
        break;
    case OB_TIME:
        sqlite3_str_appendf(s, " CONSTRAINT \"ob_time_%w\" CHECK (\"%w\" IS NULL OR \"%w\" IS time(\"%w\"))",
                            f->name, f->name, f->name, f->name);
        break;
    case OB_DATETIME:
        sqlite3_str_appendf(s, " CONSTRAINT \"ob_datetime_%w\" CHECK (\"%w\" IS NULL OR \"%w\" IS datetime(\"%w\"))",
                            f->name, f->name, f->name, f->name);
        break;
    case OB_YESNO:
        sqlite3_str_appendf(s, " CONSTRAINT \"ob_yesno_%w\" CHECK (\"%w\" IN (0, 1))", f->name, f->name);
        break;
    case OB_CHOICE: {
        const char *a = f->choices;
        sqlite3_str_appendf(s, " CONSTRAINT \"ob_choice_%w\" CHECK (\"%w\" IN (", f->name, f->name);
        while (*a) {
            const char *b = strchr(a, '|');
            int len = b ? (int)(b - a) : (int)strlen(a);
            sqlite3_str_appendf(s, "%s'%.*q'", a == f->choices ? "" : ", ", len, a);
            if (!b) break;
            a = b + 1;
        }
        sqlite3_str_appendall(s, "))");
        break;
    }
    case OB_LOOKUP:
        sqlite3_str_appendf(s, " REFERENCES \"%w\" ON DELETE %s", f->lookup,
                            delete_rules[(unsigned)f->on_delete <= OB_DELETE_CLEAR ? f->on_delete : 0]);
        break;
    default:
        break;
    }
}

/* CREATE TABLE, with PRIMARY KEY(...) for a table keyed by its fields. */
static void create_sql(sqlite3_str *s, const ob_table *t, const char *as_name)
{
    int i, k = 0, counter = 0;
    sqlite3_str_appendf(s, "CREATE TABLE \"%w\" (", as_name ? as_name : t->name);
    for (i = 0; i < t->nfields; i++) {
        sqlite3_str_appendall(s, i ? ",\n  " : "\n  ");
        column_sql(s, &t->fields[i]);
        if (t->fields[i].type == OB_COUNTER) counter = 1;
    }
    if (!counter)
        for (i = 0; i < t->nfields; i++)
            if (t->fields[i].key)
                sqlite3_str_appendf(s, "%s\"%w\"", k++ ? ", " : ",\n  PRIMARY KEY(", t->fields[i].name);
    if (k) sqlite3_str_appendall(s, ")");
    sqlite3_str_appendall(s, "\n)");
}

static void index_sql(sqlite3_str *s, const ob_table *t, const ob_index *x)
{
    int j;
    sqlite3_str_appendf(s, "CREATE %sINDEX \"%w\" ON \"%w\" (", x->unique ? "UNIQUE " : "", x->name, t->name);
    for (j = 0; j < x->nfields; j++) sqlite3_str_appendf(s, "%s\"%w\"", j ? ", " : "", x->fields[j]);
    sqlite3_str_appendall(s, ")");
}

int ob_table_sql(const ob_table *t, const char *as_name, char *out, size_t size)
{
    sqlite3_str *s = sqlite3_str_new(NULL);
    char *sql;
    int i, len;
    create_sql(s, t, as_name);
    for (i = 0; i < t->nindexes; i++) {
        sqlite3_str_appendall(s, ";\n");
        index_sql(s, t, &t->indexes[i]);
    }
    len = sqlite3_str_length(s);
    sql = sqlite3_str_finish(s);
    if (!sql || (size_t)len >= size) {
        sqlite3_free(sql);
        if (size) out[0] = 0;
        return -1;
    }
    memcpy(out, sql, (size_t)len + 1);
    sqlite3_free(sql);
    return len;
}

/* The CREATE TABLE statement, from sqlite3_malloc: the caller frees it. */
static char *table_sql_alloc(const ob_table *t, const char *as_name)
{
    sqlite3_str *s = sqlite3_str_new(NULL);
    create_sql(s, t, as_name);
    return sqlite3_str_finish(s);
}

/* The table's indexes, made in the file; 0 with the reason in plain words. */
static int make_indexes(ob_project *p, const ob_table *t)
{
    int i, rc = SQLITE_OK;
    for (i = 0; i < t->nindexes && rc == SQLITE_OK; i++) {
        sqlite3_str *s = sqlite3_str_new(NULL);
        char *sql;
        index_sql(s, t, &t->indexes[i]);
        sql = sqlite3_str_finish(s);
        rc = sql ? sqlite3_exec(p->db, sql, NULL, NULL, NULL) : SQLITE_NOMEM;
        sqlite3_free(sql);
    }
    return rc == SQLITE_OK ? 1 : sql_fail(p, t, rc);
}

static int save_fields(ob_project *p, const ob_table *t)
{
    sqlite3_stmt *st;
    int i, rc = SQLITE_DONE;
    if (!runf(p, "DELETE FROM oadb_fields WHERE tbl = %Q", t->name)) return 0;
    st = prep(p, "INSERT INTO oadb_fields(tbl, field, type, required, uniq, max_len, has_default, deflt,"
                 " label, choices, lookup, on_delete) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12)");
    if (!st) return 0;
    for (i = 0; i < t->nfields && rc == SQLITE_DONE; i++) {
        const ob_field *f = &t->fields[i];
        sqlite3_bind_text(st, 1, t->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 2, f->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, ob_type_name(f->type), -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 4, f->required != 0);
        sqlite3_bind_int(st, 5, f->unique != 0);
        sqlite3_bind_int(st, 6, f->max_len);
        sqlite3_bind_int(st, 7, f->has_default != 0);
        sqlite3_bind_text(st, 8, f->deflt, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 9, f->label, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 10, f->choices, -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 11, f->lookup, -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 12, (int)f->on_delete);
        rc = sqlite3_step(st);
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? 1 : sql_fail(p, NULL, rc);
}

/* A type for a column no oadb_fields row describes, from its declared type
 * (SQLite's own affinity rules, section 3.1 of its datatype page). */
static ob_type type_from_sql(const char *decl, int pk_alone)
{
    char u[64];
    size_t i;
    copy(u, sizeof u, decl ? decl : "");
    for (i = 0; u[i]; i++) u[i] = (char)toupper((unsigned char)u[i]);
    if (strstr(u, "INT")) return pk_alone && !strcmp(u, "INTEGER") ? OB_COUNTER : OB_INTEGER;
    if (strstr(u, "CHAR") || strstr(u, "CLOB") || strstr(u, "TEXT")) return OB_TEXT;
    if (!u[0] || strstr(u, "BLOB")) return OB_OBJECT;
    if (strstr(u, "REAL") || strstr(u, "FLOA") || strstr(u, "DOUB")) return OB_NUMBER;
    if (strstr(u, "DATE")) return OB_TEXT;
    return OB_NUMBER;   /* NUMERIC affinity */
}

int ob_table_get(ob_project *p, const char *name, ob_table *out)
{
    sqlite3_stmt *st;
    char *sql;
    int pks = 0;
    memset(out, 0, sizeof *out);
    if (!table_exists(p, name)) return fail(p, "There is no table %s", name);
    {
        int i;
        for (i = 0; i < p->ntables; i++) if (same(p->tables[i], name)) copy(out->name, sizeof out->name, p->tables[i]);
    }
    sql = sqlite3_mprintf("SELECT count(*) FROM pragma_table_info(%Q) WHERE pk > 0", out->name);
    pks = (int)scalar(p, sql, 0);
    sqlite3_free(sql);
    sql = sqlite3_mprintf("SELECT c.name, c.type, c.\"notnull\", c.pk, f.type, f.required, f.uniq, f.max_len,"
                          " f.has_default, f.deflt, f.label, f.choices, f.lookup, f.on_delete"
                          " FROM pragma_table_info(%Q) c LEFT JOIN oadb_fields f ON f.tbl = %Q AND f.field = c.name"
                          " ORDER BY c.cid", out->name, out->name);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) return 0;
    while (sqlite3_step(st) == SQLITE_ROW && out->nfields < OB_MAX_FIELDS) {
        ob_field *f = &out->fields[out->nfields++];
        const char *tn = (const char *)sqlite3_column_text(st, 4);
        int t = tn ? ob_type_from_name(tn) : -1;
        copy(f->name, sizeof f->name, (const char *)sqlite3_column_text(st, 0));
        copy(f->from, sizeof f->from, f->name);
        if (t >= 0) {
            f->type = (ob_type)t;
            f->required = sqlite3_column_int(st, 5);
            f->unique = sqlite3_column_int(st, 6);
            f->max_len = sqlite3_column_int(st, 7);
            f->has_default = sqlite3_column_int(st, 8);
            copy(f->deflt, sizeof f->deflt, (const char *)sqlite3_column_text(st, 9));
            copy(f->label, sizeof f->label, (const char *)sqlite3_column_text(st, 10));
            copy(f->choices, sizeof f->choices, (const char *)sqlite3_column_text(st, 11));
            copy(f->lookup, sizeof f->lookup, (const char *)sqlite3_column_text(st, 12));
            f->on_delete = (ob_on_delete)sqlite3_column_int(st, 13);
        } else {
            f->type = type_from_sql((const char *)sqlite3_column_text(st, 1),
                                    pks == 1 && sqlite3_column_int(st, 3) == 1);
            f->required = sqlite3_column_int(st, 2);
        }
        f->key = f->type != OB_COUNTER && sqlite3_column_int(st, 3) > 0;
    }
    sqlite3_finalize(st);
    /* Foreign keys another program made become Lookups. */
    sql = sqlite3_mprintf("SELECT \"from\", \"table\", on_delete FROM pragma_foreign_key_list(%Q)", out->name);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    while (st && sqlite3_step(st) == SQLITE_ROW) {
        int i = ob_table_find_field(out, (const char *)sqlite3_column_text(st, 0));
        const char *rule = (const char *)sqlite3_column_text(st, 2);
        if (i < 0 || out->fields[i].type == OB_LOOKUP) continue;
        if (out->fields[i].type != OB_INTEGER) continue;
        out->fields[i].type = OB_LOOKUP;
        copy(out->fields[i].lookup, sizeof out->fields[i].lookup, (const char *)sqlite3_column_text(st, 1));
        out->fields[i].on_delete = rule && !strcmp(rule, "CASCADE") ? OB_DELETE_CASCADE
                                 : rule && !strcmp(rule, "SET NULL") ? OB_DELETE_CLEAR : OB_DELETE_REFUSE;
    }
    sqlite3_finalize(st);
    /* Indexes made by CREATE INDEX on whole fields (not UNIQUE fields' own,
     * the key's, partial ones or ones on expressions) */
    sql = sqlite3_mprintf("SELECT name, \"unique\" FROM pragma_index_list(%Q) WHERE origin = 'c' AND partial = 0"
                          " ORDER BY name COLLATE NOCASE", out->name);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    while (st && sqlite3_step(st) == SQLITE_ROW && out->nindexes < OB_MAX_INDEXES) {
        ob_index *x = &out->indexes[out->nindexes];
        sqlite3_stmt *ci;
        int whole = 1;
        memset(x, 0, sizeof *x);
        copy(x->name, sizeof x->name, (const char *)sqlite3_column_text(st, 0));
        x->unique = sqlite3_column_int(st, 1);
        sql = sqlite3_mprintf("SELECT name FROM pragma_index_info(%Q) ORDER BY seqno", x->name);
        ci = sql ? prep(p, sql) : NULL;
        sqlite3_free(sql);
        while (ci && sqlite3_step(ci) == SQLITE_ROW) {
            const char *n = (const char *)sqlite3_column_text(ci, 0);
            if (!n || x->nfields >= OB_INDEX_FIELDS) whole = 0;
            else copy(x->fields[x->nfields++], OB_NAME_MAX, n);
        }
        sqlite3_finalize(ci);
        if (whole && x->nfields > 0) out->nindexes++;
    }
    sqlite3_finalize(st);
    return ok(p);
}

int ob_table_create(ob_project *p, const ob_table *t)
{
    char *sql;
    int good;
    if (!ob_table_check(p, t)) return 0;
    if (table_exists(p, t->name)) return fail(p, "There is a table named %s already", t->name);
    if (!(sql = table_sql_alloc(t, NULL))) return fail(p, "Not enough memory");
    if (!step_begin(p)) { sqlite3_free(sql); return 0; }
    good = run(p, sql) && save_fields(p, t) && make_indexes(p, t);
    sqlite3_free(sql);
    forget(p);
    return step_end(p, good) && ok(p);
}

/* The expression that turns old field `of`'s value into new field f's type. */
static void convert_sql(sqlite3_str *s, const ob_field *of, const ob_field *f)
{
    ob_type a = of->type, b = f->type;
    const char *n = of->name;
    int int_like_a = a == OB_INTEGER || a == OB_COUNTER || a == OB_LOOKUP;
    if (a == b || (int_like_a && (b == OB_INTEGER || b == OB_COUNTER || b == OB_LOOKUP))
        || ((a == OB_TEXT || a == OB_MEMO) && (b == OB_TEXT || b == OB_MEMO)) || b == OB_OBJECT) {
        if (b == OB_TEXT && f->max_len > 0)
            sqlite3_str_appendf(s, "CASE WHEN length(\"%w\") <= %d THEN \"%w\" END", n, f->max_len, n);
        else if (b == OB_CHOICE)
            goto choice;
        else
            sqlite3_str_appendf(s, "\"%w\"", n);
        return;
    }
    switch (b) {
    case OB_TEXT:
    case OB_MEMO:
        if (a == OB_CURRENCY)
            sqlite3_str_appendf(s, "printf('%%s%%d.%%02d', CASE WHEN \"%w\" < 0 THEN '-' ELSE '' END,"
                                   " abs(\"%w\") / 100, abs(\"%w\") %% 100)", n, n, n);
        else if (a == OB_YESNO)
            sqlite3_str_appendf(s, "CASE \"%w\" WHEN 1 THEN 'Yes' WHEN 0 THEN 'No' END", n);
        else
            sqlite3_str_appendf(s, "CAST(\"%w\" AS TEXT)", n);
        return;
    case OB_INTEGER:
    case OB_COUNTER:
    case OB_LOOKUP:
        if (a == OB_CURRENCY) sqlite3_str_appendf(s, "\"%w\" / 100", n);
        else if (a == OB_NUMBER) sqlite3_str_appendf(s, "CAST(round(\"%w\") AS INTEGER)", n);
        else if (a == OB_YESNO) sqlite3_str_appendf(s, "\"%w\"", n);
        else sqlite3_str_appendf(s, "CASE WHEN trim(\"%w\") GLOB '[0-9]*' AND trim(\"%w\") NOT GLOB '*[^0-9]*'"
                                    " OR trim(\"%w\") GLOB '-[0-9]*' AND substr(trim(\"%w\"), 2) NOT GLOB '*[^0-9]*'"
                                    " THEN CAST(trim(\"%w\") AS INTEGER) END", n, n, n, n, n);
        return;
    case OB_NUMBER:
        if (a == OB_CURRENCY) sqlite3_str_appendf(s, "\"%w\" / 100.0", n);
        else if (int_like_a || a == OB_YESNO) sqlite3_str_appendf(s, "CAST(\"%w\" AS REAL)", n);
        else sqlite3_str_appendf(s, "CASE WHEN typeof(\"%w\") IN ('integer', 'real') OR (trim(\"%w\") <> ''"
                                    " AND CAST(trim(\"%w\") AS REAL) || '' <> '0.0') OR trim(\"%w\") IN ('0', '0.0')"
                                    " THEN CAST(trim(\"%w\") AS REAL) END", n, n, n, n, n);
        return;
    case OB_CURRENCY:
        if (int_like_a || a == OB_YESNO) sqlite3_str_appendf(s, "\"%w\" * 100", n);
        else if (a == OB_NUMBER) sqlite3_str_appendf(s, "CAST(round(\"%w\" * 100) AS INTEGER)", n);
        else sqlite3_str_appendf(s, "CASE WHEN typeof(\"%w\") IN ('integer', 'real') OR CAST(trim(\"%w\") AS REAL)"
                                    " || '' <> '0.0' OR trim(\"%w\") IN ('0', '0.0', '0.00')"
                                    " THEN CAST(round(CAST(trim(\"%w\") AS REAL) * 100) AS INTEGER) END", n, n, n, n);
        return;
    case OB_DATE:
        sqlite3_str_appendf(s, "CASE WHEN \"%w\" IS date(\"%w\") THEN \"%w\" END", n, n, n);
        return;
    case OB_TIME:
        sqlite3_str_appendf(s, "CASE WHEN \"%w\" IS time(\"%w\") THEN \"%w\" END", n, n, n);
        return;
    case OB_DATETIME:
        sqlite3_str_appendf(s, "CASE WHEN \"%w\" IS datetime(\"%w\") THEN \"%w\" WHEN \"%w\" IS date(\"%w\")"
                               " THEN \"%w\" || ' 00:00:00' END", n, n, n, n, n, n);
        return;
    case OB_YESNO:
        sqlite3_str_appendf(s, "CASE WHEN \"%w\" IS NULL OR \"%w\" = '' THEN NULL"
                               " WHEN lower(\"%w\") IN ('0', 'no', 'n', 'false', 'off') OR \"%w\" = 0 THEN 0 ELSE 1 END",
                            n, n, n, n);
        return;
    case OB_CHOICE:
        goto choice;
    default:
        sqlite3_str_appendf(s, "\"%w\"", n);
        return;
    }
choice: {
        const char *c = f->choices;
        sqlite3_str_appendf(s, "CASE");
        while (*c) {
            const char *e = strchr(c, '|');
            int len = e ? (int)(e - c) : (int)strlen(c);
            sqlite3_str_appendf(s, " WHEN lower(CAST(\"%w\" AS TEXT)) = lower('%.*q') THEN '%.*q'", n, len, c, len, c);
            if (!e) break;
            c = e + 1;
        }
        sqlite3_str_appendall(s, " END");
    }
}

/* Why a changed table's rows didn't fit, from the first rule a row breaks. */
static int change_fail(ob_project *p, const ob_table *t, int rc)
{
    sql_fail(p, t, rc);
    if (strstr(p->err, "must be")) {
        char keep[OB_ERR_MAX];
        copy(keep, sizeof keep, p->err);
        return fail(p, "Some rows don't fit the new design: %s", keep);
    }
    return 0;
}

static int change_table(ob_project *p, const char *old_name, const ob_table *t, ob_table *old)
{
    sqlite3_str *cols, *vals;
    char *sql = NULL, *cs = NULL, *vs = NULL;
    int i, good = 0, any = 0, rc;
    if (!ob_table_get(p, old_name, old)) return 0;
    if (!check_design(p, t, old->name)) return 0;
    if (!same(old->name, t->name) && table_exists(p, t->name))
        return fail(p, "There is a table named %s already", t->name);
    for (i = 0; i < t->nfields; i++)
        if (t->fields[i].from[0] && ob_table_find_field(old, t->fields[i].from) < 0)
            return fail(p, "%s has no field %s", old->name, t->fields[i].from);
    cols = sqlite3_str_new(NULL);
    vals = sqlite3_str_new(NULL);
    for (i = 0; i < t->nfields; i++) {
        const ob_field *f = &t->fields[i];
        int j = f->from[0] ? ob_table_find_field(old, f->from) : -1;
        if (j < 0) continue;
        sqlite3_str_appendf(cols, "%s\"%w\"", any ? ", " : "", f->name);
        if (any) sqlite3_str_appendall(vals, ", ");
        convert_sql(vals, &old->fields[j], f);
        any = 1;
    }
    cs = sqlite3_str_finish(cols);
    vs = sqlite3_str_finish(vals);
    /* SQLite's twelve steps (its ALTER TABLE page, "making other kinds of table
     * schema changes"), with foreign keys off for the copy and checked after */
    if (!run(p, "PRAGMA foreign_keys=OFF")) goto out;
    if (!run(p, "BEGIN IMMEDIATE")) goto keys;
    sql = table_sql_alloc(t, "oadb_change");
    if (!sql || !run(p, sql)) goto undo;
    sqlite3_free(sql);
    sql = NULL;
    if (any) {
        sql = sqlite3_mprintf("INSERT INTO oadb_change(%s) SELECT %s FROM \"%w\"", cs, vs, old->name);
        rc = sql ? sqlite3_exec(p->db, sql, NULL, NULL, NULL) : SQLITE_NOMEM;
        if (rc != SQLITE_OK) { change_fail(p, t, rc); goto undo; }
    }
    if (!runf(p, "DROP TABLE \"%w\"", old->name)) goto undo;
    if (!runf(p, "ALTER TABLE oadb_change RENAME TO \"%w\"", t->name)) goto undo;
    if (!runf(p, "DELETE FROM oadb_fields WHERE tbl = %Q", old->name) || !save_fields(p, t)) goto undo;
    if (!make_indexes(p, t)) {
        if (strstr(p->err, "must be")) {
            char keep[OB_ERR_MAX];
            copy(keep, sizeof keep, p->err);
            fail(p, "Some rows don't fit the new design: %s", keep);
        }
        goto undo;
    }
    if (!same(old->name, t->name)) {
        /* other tables' Lookups follow the rename (SQLite rewrote their REFERENCES) */
        if (!runf(p, "UPDATE oadb_fields SET lookup = %Q WHERE lookup = %Q", t->name, old->name)) goto undo;
    }
    {
        sqlite3_stmt *st = prep(p, "PRAGMA foreign_key_check");
        if (!st) goto undo;
        if (sqlite3_step(st) == SQLITE_ROW) {
            fail(p, "Changing %s would leave rows in %s looking up rows that aren't there", old->name,
                 (const char *)sqlite3_column_text(st, 0));
            sqlite3_finalize(st);
            goto undo;
        }
        sqlite3_finalize(st);
    }
    if (!run(p, "COMMIT")) goto undo;
    good = 1;
    goto keys;
undo:
    {
        char keep[OB_ERR_MAX];
        copy(keep, sizeof keep, p->err);
        sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
        copy(p->err, sizeof p->err, keep);
    }
keys:
    {
        char keep[OB_ERR_MAX];
        copy(keep, sizeof keep, p->err);
        sqlite3_exec(p->db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);
        copy(p->err, sizeof p->err, keep);
    }
out:
    sqlite3_free(sql);
    sqlite3_free(cs);
    sqlite3_free(vs);
    forget(p);
    return good ? ok(p) : 0;
}

int ob_table_change(ob_project *p, const char *old_name, const ob_table *t)
{
    ob_table *old;
    int good;
    if (p->in_txn) return fail(p, "Save or undo the row changes first");
    if (!(old = malloc(sizeof *old))) return fail(p, "Not enough memory");
    good = change_table(p, old_name, t, old);
    free(old);
    return good;
}

int ob_table_rename(ob_project *p, const char *old_name, const char *new_name)
{
    char why[OB_ERR_MAX];
    const ob_table *t;
    int good;
    if (!(t = design(p, old_name))) return 0;
    if (!name_ok(new_name, why, sizeof why, "table")) return fail(p, "%s", why);
    if (!same(t->name, new_name) && table_exists(p, new_name))
        return fail(p, "There is a table named %s already", new_name);
    if (!step_begin(p)) return 0;
    good = runf(p, "ALTER TABLE \"%w\" RENAME TO \"%w\"", t->name, new_name)
        && runf(p, "UPDATE oadb_fields SET tbl = %Q WHERE tbl = %Q", new_name, t->name)
        && runf(p, "UPDATE oadb_fields SET lookup = %Q WHERE lookup = %Q", new_name, t->name);
    forget(p);
    return step_end(p, good) && ok(p);
}

int ob_table_drop(ob_project *p, const char *name)
{
    const ob_table *t;
    sqlite3_stmt *st;
    int good;
    if (!(t = design(p, name))) return 0;
    st = prep(p, "SELECT tbl FROM oadb_fields WHERE lookup = ?1 AND tbl <> ?1 LIMIT 1");
    if (!st) return 0;
    sqlite3_bind_text(st, 1, t->name, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fail(p, "%s looks up %s: change or delete it first", (const char *)sqlite3_column_text(st, 0), t->name);
        sqlite3_finalize(st);
        return 0;
    }
    sqlite3_finalize(st);
    if (!step_begin(p)) return 0;
    good = runf(p, "DROP TABLE \"%w\"", t->name) && runf(p, "DELETE FROM oadb_fields WHERE tbl = %Q", t->name);
    forget(p);
    return step_end(p, good) && ok(p);
}

/* ---- Relationships ---- */

int ob_relations(ob_project *p, ob_relation *out, int max)
{
    sqlite3_stmt *st = prep(p, "SELECT m.name, k.\"from\", k.\"table\", k.on_delete"
                               " FROM sqlite_schema m, pragma_foreign_key_list(m.name) k"
                               " WHERE m.type = 'table' AND m.name NOT LIKE 'oadb\\_%' ESCAPE '\\'"
                               " AND m.name NOT LIKE 'sqlite\\_%' ESCAPE '\\'"
                               " ORDER BY m.name COLLATE NOCASE, k.\"from\" COLLATE NOCASE");
    int n = 0, rc;
    if (!st) return -1;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (out && n < max) {
            ob_relation *r = &out[n];
            const char *rule = (const char *)sqlite3_column_text(st, 3);
            copy(r->table, sizeof r->table, (const char *)sqlite3_column_text(st, 0));
            copy(r->field, sizeof r->field, (const char *)sqlite3_column_text(st, 1));
            copy(r->lookup, sizeof r->lookup, (const char *)sqlite3_column_text(st, 2));
            r->on_delete = rule && !strcmp(rule, "CASCADE") ? OB_DELETE_CASCADE
                         : rule && !strcmp(rule, "SET NULL") ? OB_DELETE_CLEAR : OB_DELETE_REFUSE;
        }
        n++;
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { sql_fail(p, NULL, rc); return -1; }
    ok(p);
    return n;
}

/* ---- Rows ---- */

/* Whether a Lookup's value names a row of its table; 0 with the reason. */
static int lookup_ok(ob_project *p, const ob_field *f, const value *v)
{
    char *sql;
    ob_int n;
    if (v->kind != V_INT) return 1;
    sql = sqlite3_mprintf("SELECT count(*) FROM \"%w\" WHERE rowid = %lld", f->lookup, v->i);
    n = sql ? scalar(p, sql, 0) : 0;
    sqlite3_free(sql);
    if (n) return 1;
    return fail(p, "%s: %s has no row %lld", field_label(f), f->lookup, v->i);
}

/* Checks and binds n named values into st from parameter `first`. */
static int bind_fields(ob_project *p, const ob_table *t, sqlite3_stmt *st, int first, int n,
                       const char *const *fields, const char *const *values)
{
    int i;
    for (i = 0; i < n; i++) {
        const ob_field *f = field_named(t, fields[i]);
        value v;
        char e[OB_ERR_MAX];
        if (!f) return fail(p, "%s has no field %s", t->name, fields[i]);
        if (!convert(f, values ? values[i] : NULL, &v, e, sizeof e)) return fail(p, "%s", e);
        if (f->type == OB_LOOKUP && !lookup_ok(p, f, &v)) return 0;
        bind_value(st, first + i, &v);
    }
    return 1;
}

static int names_unique(ob_project *p, int n, const char *const *fields)
{
    int i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < i; j++)
            if (same(fields[i], fields[j])) return fail(p, "%s is given twice", fields[i]);
    return 1;
}

/* ---- Undo ---- */

enum { U_ADD, U_CHANGE, U_DELETE, U_IMPORT };

typedef struct undo_step {
    int kind;
    char table[OB_NAME_MAX];
    ob_int key;                 /* the row (add, change, delete) */
    ob_int version;             /* PRAGMA schema_version when it was done */
    int ncols;                  /* change, delete: the row as it was */
    int has_counter;
    char (*cols)[OB_NAME_MAX];
    sqlite3_value **vals;
    ob_int *keys;               /* import: the rows it added */
    ob_int nkeys;
} undo_step;

static void undo_free(undo_step *u)
{
    int i;
    if (!u) return;
    if (u->vals) for (i = 0; i < u->ncols; i++) sqlite3_value_free(u->vals[i]);
    free(u->vals);
    free(u->cols);
    free(u->keys);
    free(u);
}

static void undo_drop_top(ob_project *p)
{
    if (p->nundo > 0) undo_free(p->undo[--p->nundo]);
}

void ob_undo_clear(ob_project *p)
{
    if (!p) return;
    while (p->nundo > 0) undo_drop_top(p);
    p->undo_at_begin = 0;
}

int ob_undo_count(ob_project *p)
{
    return p ? p->nundo : 0;
}

static undo_step *undo_new(ob_project *p, int kind, const char *table, ob_int key)
{
    undo_step *u = calloc(1, sizeof *u);
    if (!u) return NULL;
    u->kind = kind;
    copy(u->table, sizeof u->table, table);
    u->key = key;
    u->version = scalar(p, "PRAGMA schema_version", -1);
    return u;
}

/* Keeps u as the newest step (the oldest goes past OB_UNDO_MAX). */
static void undo_push(ob_project *p, undo_step *u)
{
    if (!u) return;
    if (p->nundo == OB_UNDO_MAX) {
        undo_free(p->undo[0]);
        memmove(&p->undo[0], &p->undo[1], sizeof p->undo[0] * (OB_UNDO_MAX - 1));
        p->nundo--;
        if (p->undo_at_begin > 0) p->undo_at_begin--;
    }
    p->undo[p->nundo++] = u;
}

/* The row as it is now, every column's stored value, into a new step. NULL
 * when it can't be kept (no memory, no such row): the change is then made
 * without a step, and the older steps are dropped, since they may no longer
 * fit what the file holds. */
static undo_step *undo_snap(ob_project *p, const ob_table *t, int kind, ob_int key)
{
    undo_step *u;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    int i;
    if (p->undo_off) return NULL;
    if (!(u = undo_new(p, kind, t->name, key))) return NULL;
    u->ncols = t->nfields;
    u->cols = malloc(sizeof *u->cols * (size_t)(t->nfields ? t->nfields : 1));
    u->vals = calloc((size_t)(t->nfields ? t->nfields : 1), sizeof *u->vals);
    if (!u->cols || !u->vals) { undo_free(u); return NULL; }
    s = sqlite3_str_new(NULL);
    sqlite3_str_appendall(s, "SELECT ");
    for (i = 0; i < t->nfields; i++) {
        copy(u->cols[i], sizeof u->cols[i], t->fields[i].name);
        if (t->fields[i].type == OB_COUNTER) u->has_counter = 1;
        sqlite3_str_appendf(s, "%s\"%w\"", i ? ", " : "", t->fields[i].name);
    }
    sqlite3_str_appendf(s, " FROM \"%w\" WHERE rowid = ?1", t->name);
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) { undo_free(u); return NULL; }
    sqlite3_bind_int64(st, 1, key);
    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); undo_free(u); return NULL; }
    for (i = 0; i < t->nfields; i++)
        if (!(u->vals[i] = sqlite3_value_dup(sqlite3_column_value(st, i)))) { sqlite3_finalize(st); undo_free(u); return NULL; }
    sqlite3_finalize(st);
    return u;
}

/* Whether deleting row key of table changes rows elsewhere (a Lookup that
 * cascades or clears, or another program's foreign key that does). */
static int delete_reaches(ob_project *p, const char *table, ob_int key)
{
    sqlite3_stmt *st = prep(p, "SELECT m.name, k.\"from\" FROM sqlite_schema m, pragma_foreign_key_list(m.name) k"
                               " WHERE m.type = 'table' AND k.\"table\" = ?1 COLLATE NOCASE"
                               " AND k.on_delete IN ('CASCADE', 'SET NULL', 'SET DEFAULT')");
    int reaches = 0;
    if (!st) return 1;
    sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
    while (!reaches && sqlite3_step(st) == SQLITE_ROW) {
        char *sql = sqlite3_mprintf("SELECT count(*) FROM \"%w\" WHERE \"%w\" = %lld",
                                    (const char *)sqlite3_column_text(st, 0), (const char *)sqlite3_column_text(st, 1), key);
        if (!sql || scalar(p, sql, 1) != 0) reaches = 1;
        sqlite3_free(sql);
    }
    sqlite3_finalize(st);
    return reaches;
}

const char *ob_undo_name(ob_project *p)
{
    const undo_step *u;
    static const char *const what[] = { "Add Row", "Change Row", "Delete Row", "Import" };
    if (!p || !p->nundo) return "";
    u = p->undo[p->nundo - 1];
    if (u->kind == U_IMPORT)
        snprintf(p->undo_name, sizeof p->undo_name, "Import of %lld rows into %s", u->nkeys, u->table);
    else
        snprintf(p->undo_name, sizeof p->undo_name, "%s in %s", what[u->kind], u->table);
    return p->undo_name;
}

int ob_undo(ob_project *p)
{
    undo_step *u;
    sqlite3_str *s;
    sqlite3_stmt *st = NULL;
    char *sql;
    int i, rc, good = 1;
    if (!p->nundo) return fail(p, "Nothing to undo");
    if (p->readonly) return fail(p, "The project is open read-only");
    u = p->undo[p->nundo - 1];
    if (scalar(p, "PRAGMA schema_version", -1) != u->version) {
        ob_undo_clear(p);
        return fail(p, "A table's design has changed since, so that can't be undone");
    }
    s = sqlite3_str_new(NULL);
    switch (u->kind) {
    case U_ADD:
    case U_IMPORT:
        sqlite3_str_appendf(s, "DELETE FROM \"%w\" WHERE rowid = ?1", u->table);
        break;
    case U_CHANGE:
        sqlite3_str_appendf(s, "UPDATE \"%w\" SET ", u->table);
        for (i = 0; i < u->ncols; i++) sqlite3_str_appendf(s, "%s\"%w\" = ?%d", i ? ", " : "", u->cols[i], i + 2);
        sqlite3_str_appendall(s, " WHERE rowid = ?1");
        break;
    default:
        sqlite3_str_appendf(s, "INSERT INTO \"%w\"(%s", u->table, u->has_counter ? "" : "rowid");
        for (i = 0; i < u->ncols; i++) sqlite3_str_appendf(s, "%s\"%w\"", i || !u->has_counter ? ", " : "", u->cols[i]);
        sqlite3_str_appendf(s, ") VALUES(%s", u->has_counter ? "" : "?1");
        for (i = 0; i < u->ncols; i++) sqlite3_str_appendf(s, "%s?%d", i || !u->has_counter ? ", " : "", i + 2);
        sqlite3_str_appendall(s, ")");
        break;
    }
    sql = sqlite3_str_finish(s);
    if (!sql || !step_begin(p)) { sqlite3_free(sql); return 0; }
    st = prep(p, sql);
    sqlite3_free(sql);
    if (!st) good = 0;
    if (good && u->kind == U_IMPORT) {
        ob_int k;
        for (k = 0; good && k < u->nkeys; k++) {
            sqlite3_reset(st);
            sqlite3_bind_int64(st, 1, u->keys[k]);
            if ((rc = sqlite3_step(st)) != SQLITE_DONE) good = sql_fail(p, design(p, u->table), rc);
        }
    } else if (good) {
        sqlite3_bind_int64(st, 1, u->key);
        for (i = 0; i < u->ncols; i++) sqlite3_bind_value(st, i + 2, u->vals[i]);
        if ((rc = sqlite3_step(st)) != SQLITE_DONE) good = sql_fail(p, design(p, u->table), rc);
    }
    sqlite3_finalize(st);
    if (!step_end(p, good)) return 0;
    p->nundo--;
    undo_free(u);
    return ok(p);
}

int ob_row_add(ob_project *p, const char *table, int n, const char *const *fields,
               const char *const *values, ob_int *key)
{
    const ob_table *t;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    int i, rc;
    if (!(t = design(p, table))) return 0;
    if (n < 0 || n > t->nfields) return fail(p, "Too many fields");
    if (!names_unique(p, n, fields)) return 0;
    for (i = 0; i < n; i++)
        if (!field_named(t, fields[i])) return fail(p, "%s has no field %s", t->name, fields[i]);
    /* a required field left out with no default: say so before SQLite does */
    for (i = 0; i < t->nfields; i++) {
        const ob_field *f = &t->fields[i];
        int j, given = 0;
        for (j = 0; j < n; j++) if (same(fields[j], f->name)) given = 1;
        if (!given && (f->required || f->key) && !f->has_default && f->type != OB_COUNTER)
            return fail(p, "%s must be filled in", field_label(f));
    }
    s = sqlite3_str_new(NULL);
    if (n == 0) {
        sqlite3_str_appendf(s, "INSERT INTO \"%w\" DEFAULT VALUES", t->name);
    } else {
        sqlite3_str_appendf(s, "INSERT INTO \"%w\"(", t->name);
        for (i = 0; i < n; i++) sqlite3_str_appendf(s, "%s\"%w\"", i ? ", " : "", fields[i]);
        sqlite3_str_appendall(s, ") VALUES(");
        for (i = 0; i < n; i++) sqlite3_str_appendf(s, "%s?%d", i ? ", " : "", i + 1);
        sqlite3_str_appendall(s, ")");
    }
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) return 0;
    if (!bind_fields(p, t, st, 1, n, fields, values)) { sqlite3_finalize(st); return 0; }
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return sql_fail(p, t, rc);
    if (key) *key = sqlite3_last_insert_rowid(p->db);
    if (!p->undo_off) {
        undo_step *u = undo_new(p, U_ADD, t->name, sqlite3_last_insert_rowid(p->db));
        if (u) undo_push(p, u); else ob_undo_clear(p);
    }
    return ok(p);
}

int ob_row_change(ob_project *p, const char *table, ob_int key, int n,
                  const char *const *fields, const char *const *values)
{
    const ob_table *t;
    undo_step *u;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    int i, rc;
    if (!(t = design(p, table))) return 0;
    if (n <= 0) return ok(p);
    if (n > t->nfields) return fail(p, "Too many fields");
    if (!names_unique(p, n, fields)) return 0;
    s = sqlite3_str_new(NULL);
    sqlite3_str_appendf(s, "UPDATE \"%w\" SET ", t->name);
    for (i = 0; i < n; i++) sqlite3_str_appendf(s, "%s\"%w\" = ?%d", i ? ", " : "", fields[i], i + 1);
    sqlite3_str_appendf(s, " WHERE rowid = ?%d", n + 1);
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) return 0;
    if (!bind_fields(p, t, st, 1, n, fields, values)) { sqlite3_finalize(st); return 0; }
    sqlite3_bind_int64(st, n + 1, key);
    u = undo_snap(p, t, U_CHANGE, key);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { undo_free(u); return sql_fail(p, t, rc); }
    if (sqlite3_changes(p->db) == 0) { undo_free(u); return fail(p, "%s has no row %lld", t->name, key); }
    if (u) undo_push(p, u);
    else if (!p->undo_off) ob_undo_clear(p);
    return ok(p);
}

int ob_row_delete(ob_project *p, const char *table, ob_int key)
{
    const ob_table *t;
    undo_step *u;
    char *sql;
    int rc;
    if (!(t = design(p, table))) return 0;
    u = NULL;
    if (!p->undo_off) {
        if (delete_reaches(p, t->name, key)) ob_undo_clear(p);     /* other rows go or change too */
        else u = undo_snap(p, t, U_DELETE, key);
        if (!(t = design(p, table))) { undo_free(u); return 0; }
    }
    sql = sqlite3_mprintf("DELETE FROM \"%w\" WHERE rowid = %lld", t->name, key);
    rc = sql ? sqlite3_exec(p->db, sql, NULL, NULL, NULL) : SQLITE_NOMEM;
    sqlite3_free(sql);
    if (rc != SQLITE_OK) {
        undo_free(u);
        if ((rc & 0xff) == SQLITE_CONSTRAINT) {
            /* a Lookup that refuses: name the table that still looks it up */
            sqlite3_stmt *st = prep(p, "SELECT tbl FROM oadb_fields WHERE lookup = ?1 AND on_delete = 0 LIMIT 1");
            if (st) {
                sqlite3_bind_text(st, 1, t->name, -1, SQLITE_STATIC);
                if (sqlite3_step(st) == SQLITE_ROW) {
                    fail(p, "%s row %lld still has rows in %s", t->name, key, (const char *)sqlite3_column_text(st, 0));
                    sqlite3_finalize(st);
                    return 0;
                }
                sqlite3_finalize(st);
            }
        }
        return sql_fail(p, t, rc);
    }
    if (sqlite3_changes(p->db) == 0) { undo_free(u); return fail(p, "%s has no row %lld", t->name, key); }
    if (u) undo_push(p, u);
    else if (!p->undo_off) ob_undo_clear(p);
    return ok(p);
}

ob_int ob_row_total(ob_project *p, const char *table)
{
    const ob_table *t;
    char *sql;
    ob_int n;
    if (!(t = design(p, table))) return -1;
    sql = sqlite3_mprintf("SELECT count(*) FROM \"%w\"", t->name);
    n = sql ? scalar(p, sql, -1) : -1;
    sqlite3_free(sql);
    return n;
}

/* ---- Views ---- */

static const char *const filter_names[OB_FILTER_COUNT] = {
    "Every row", "Equals", "Is not", "Less than", "More than", "Contains", "Starts with",
    "Is empty", "Is not empty"
};

const char *ob_filter_name(ob_filter_op op)
{
    return (unsigned)op < OB_FILTER_COUNT ? filter_names[op] : "?";
}

void ob_view_init(ob_view *v)
{
    memset(v, 0, sizeof *v);
    v->op = OB_FILTER_NONE;
}

/* Fields whose stored value is text (so LIKE and NOCASE mean something). */
static int holds_text(ob_type t)
{
    return t == OB_TEXT || t == OB_MEMO || t == OB_CHOICE || t == OB_DATE || t == OB_TIME || t == OB_DATETIME;
}

/* What a view adds to a SELECT ... FROM table: its WHERE and ORDER BY,
 * appended to s, the filter's value into *bind (?3; kind V_NULL when none).
 * 0 with the reason. */
static int view_sql(ob_project *p, const ob_table *t, const ob_view *v, sqlite3_str *s, value *bind)
{
    const ob_field *f;
    bind->kind = V_NULL;
    bind->long_text = NULL;
    if (v && v->op != OB_FILTER_NONE) {
        const char *nc;
        if ((unsigned)v->op >= OB_FILTER_COUNT) return fail(p, "Unknown filter");
        if (!(f = field_named(t, v->field))) return fail(p, "%s has no field %s", t->name, v->field);
        if (f->type == OB_OBJECT && v->op != OB_FILTER_EMPTY && v->op != OB_FILTER_NOT_EMPTY)
            return fail(p, "%s holds Datatype objects: filter it by Is empty or Is not empty", field_label(f));
        nc = holds_text(f->type) ? " COLLATE NOCASE" : "";
        switch (v->op) {
        case OB_FILTER_EMPTY:
            sqlite3_str_appendf(s, " WHERE \"%w\" IS NULL", f->name);
            if (holds_text(f->type)) sqlite3_str_appendf(s, " OR \"%w\" = ''", f->name);
            break;
        case OB_FILTER_NOT_EMPTY:
            sqlite3_str_appendf(s, " WHERE \"%w\" IS NOT NULL", f->name);
            if (holds_text(f->type)) sqlite3_str_appendf(s, " AND \"%w\" <> ''", f->name);
            break;
        case OB_FILTER_CONTAINS:
        case OB_FILTER_STARTS: {
            size_t i, o = 0;
            if (!holds_text(f->type))
                return fail(p, "%s: %s works on fields that hold text", field_label(f), ob_filter_name(v->op));
            if (v->op == OB_FILTER_CONTAINS) bind->text[o++] = '%';
            for (i = 0; v->value[i] && o + 3 < sizeof bind->text; i++) {
                char c = v->value[i];
                if (c == '%' || c == '_' || c == '\\') bind->text[o++] = '\\';
                bind->text[o++] = c;
            }
            bind->text[o++] = '%';
            bind->text[o] = 0;
            bind->kind = V_TEXT;
            sqlite3_str_appendf(s, " WHERE \"%w\" LIKE ?3 ESCAPE '\\'", f->name);
            break;
        }
        default: {
            char e[OB_ERR_MAX];
            static const char *const ops[] = { "", "=", "<>", "<", ">" };
            ob_field loose = *f;
            loose.required = loose.key = 0;     /* "" here means empty, which has its own filter */
            loose.max_len = 0;
            if (f->type == OB_COUNTER) loose.type = OB_INTEGER;
            if (!convert(&loose, v->value, bind, e, sizeof e)) return fail(p, "%s", e);
            if (bind->kind == V_NULL)
                return fail(p, "%s: give a value to compare with, or use Is empty", field_label(f));
            sqlite3_str_appendf(s, " WHERE \"%w\" %s ?3%s", f->name, ops[v->op], nc);
            break;
        }
        }
    }
    if (v && v->sort[0]) {
        if (!(f = field_named(t, v->sort))) return fail(p, "%s has no field %s", t->name, v->sort);
        sqlite3_str_appendf(s, " ORDER BY \"%w\"%s%s, rowid", f->name,
                            holds_text(f->type) ? " COLLATE NOCASE" : "", v->desc ? " DESC" : "");
    } else {
        sqlite3_str_appendall(s, " ORDER BY rowid");
    }
    return 1;
}

int ob_view_check(ob_project *p, const char *table, const ob_view *v)
{
    const ob_table *t;
    sqlite3_str *s;
    value b;
    int good;
    if (!(t = design(p, table))) return 0;
    s = sqlite3_str_new(NULL);
    good = view_sql(p, t, v, s, &b);
    sqlite3_free(sqlite3_str_finish(s));
    return good ? ok(p) : 0;
}

ob_int ob_view_total(ob_project *p, const char *table, const ob_view *v)
{
    const ob_table *t;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    value b;
    ob_int n = -1;
    if (!v || v->op == OB_FILTER_NONE) return ob_row_total(p, table);
    if (!(t = design(p, table))) return -1;
    s = sqlite3_str_new(NULL);
    sqlite3_str_appendf(s, "SELECT count(*) FROM \"%w\"", t->name);
    if (!view_sql(p, t, v, s, &b)) { sqlite3_free(sqlite3_str_finish(s)); return -1; }
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) return -1;
    if (b.kind != V_NULL) bind_value(st, 3, &b);
    if (sqlite3_step(st) == SQLITE_ROW) { n = sqlite3_column_int64(st, 0); ok(p); }
    else sql_fail(p, t, sqlite3_errcode(p->db));
    sqlite3_finalize(st);
    return n;
}

static int rows_from(ob_project *p, const char *table, const ob_view *v, ob_int after, ob_int first,
                     int max, ob_row_fn cb, void *user);

int ob_rows(ob_project *p, const char *table, ob_int after, int max, ob_row_fn cb, void *user)
{
    return rows_from(p, table, NULL, after, -1, max, cb, user);
}

int ob_rows_at(ob_project *p, const char *table, ob_int first, int max, ob_row_fn cb, void *user)
{
    return rows_from(p, table, NULL, 0, first < 0 ? 0 : first, max, cb, user);
}

int ob_view_rows(ob_project *p, const char *table, const ob_view *v, ob_int first, int max,
                 ob_row_fn cb, void *user)
{
    return rows_from(p, table, v, 0, first < 0 ? 0 : first, max, cb, user);
}

/* first < 0: the rows after key `after` (key order); else from position
 * first of the view v (NULL: key order). max < 0: every row. */
static int rows_from(ob_project *p, const char *table, const ob_view *v, ob_int after, ob_int first,
                     int max, ob_row_fn cb, void *user)
{
    const ob_table *t;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    const char *vals[OB_MAX_FIELDS];
    char (*cells)[48];
    unsigned char types[OB_MAX_FIELDS];
    value b;
    int i, nf, n = 0, rc;
    if (!(t = design(p, table))) return -1;
    if (max == 0) return 0;
    /* cb may read another table's design, so keep what the loop needs */
    nf = t->nfields;
    for (i = 0; i < nf; i++) types[i] = (unsigned char)t->fields[i].type;
    cells = malloc(sizeof *cells * (size_t)(t->nfields ? t->nfields : 1));
    if (!cells) { fail(p, "Not enough memory"); return -1; }
    s = sqlite3_str_new(NULL);
    sqlite3_str_appendall(s, "SELECT rowid");
    for (i = 0; i < t->nfields; i++) {
        if (t->fields[i].type == OB_OBJECT) sqlite3_str_appendf(s, ", length(\"%w\")", t->fields[i].name);
        else sqlite3_str_appendf(s, ", \"%w\"", t->fields[i].name);
    }
    sqlite3_str_appendf(s, " FROM \"%w\"", t->name);
    b.kind = V_NULL;
    if (first < 0) {
        sqlite3_str_appendall(s, " WHERE rowid > ?1 ORDER BY rowid LIMIT ?2");
    } else {
        if (!view_sql(p, t, v, s, &b)) { sqlite3_free(sqlite3_str_finish(s)); free(cells); return -1; }
        sqlite3_str_appendall(s, " LIMIT ?2 OFFSET ?1");
    }
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) { free(cells); return -1; }
    sqlite3_bind_int64(st, 1, first < 0 ? after : first);
    sqlite3_bind_int(st, 2, max < 0 ? -1 : max);
    if (b.kind != V_NULL) bind_value(st, 3, &b);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        for (i = 0; i < nf; i++) {
            int c = i + 1;
            if (sqlite3_column_type(st, c) == SQLITE_NULL) { vals[i] = NULL; continue; }
            if (types[i] == OB_CURRENCY && sqlite3_column_type(st, c) == SQLITE_INTEGER) {
                ob_currency_format(sqlite3_column_int64(st, c), cells[i], sizeof cells[i]);
                vals[i] = cells[i];
            } else if (types[i] == OB_YESNO && sqlite3_column_type(st, c) == SQLITE_INTEGER) {
                vals[i] = sqlite3_column_int64(st, c) ? "Yes" : "No";
            } else if (types[i] == OB_OBJECT) {
                snprintf(cells[i], sizeof cells[i], "(%lld bytes)", (ob_int)sqlite3_column_int64(st, c));
                vals[i] = cells[i];
            } else {
                vals[i] = (const char *)sqlite3_column_text(st, c);
            }
        }
        n++;
        if (cb && cb(user, sqlite3_column_int64(st, 0), nf, vals)) { rc = SQLITE_DONE; break; }
    }
    sqlite3_finalize(st);
    free(cells);
    if (rc != SQLITE_DONE) { sql_fail(p, NULL, rc); return -1; }
    ok(p);
    return n;
}

/* ---- CSV ---- */

void ob_csv_opts_init(ob_csv_opts *o)
{
    memset(o, 0, sizeof *o);
    o->header = -1;
    o->charset = OB_CHARSET_AUTO;
}

typedef struct csv_out {
    FILE *f;
    int sep;
    int utf8;
    int nf;
    unsigned char types[OB_MAX_FIELDS];
    int bad;
} csv_out;

static void out_byte(csv_out *o, int c)
{
    c &= 0xff;
    if (o->utf8 && c >= 0x80) {
        putc(0xc0 | (c >> 6), o->f);
        putc(0x80 | (c & 0x3f), o->f);
    } else {
        putc(c, o->f);
    }
}

static void csv_cell(csv_out *o, const char *v, int first)
{
    const char *c;
    int quote = 0;
    if (!first) putc(o->sep, o->f);
    if (!v) return;
    for (c = v; *c; c++)
        if (*c == o->sep || *c == '"' || *c == '\n' || *c == '\r') quote = 1;
    /* a value with spaces at its ends keeps them */
    if (*v == ' ' || (*v && v[strlen(v) - 1] == ' ')) quote = 1;
    if (quote) putc('"', o->f);
    for (c = v; *c; c++) {
        if (*c == '"') putc('"', o->f);
        out_byte(o, (unsigned char)*c);
    }
    if (quote) putc('"', o->f);
}

static int csv_row(void *user, ob_int key, int n, const char *const *values)
{
    csv_out *o = user;
    int i, first = 1;
    (void)key;
    for (i = 0; i < n && i < o->nf; i++) {
        if (o->types[i] == OB_OBJECT) continue;
        csv_cell(o, values[i], first);
        first = 0;
    }
    fputs("\r\n", o->f);
    if (ferror(o->f)) { o->bad = 1; return 1; }
    return 0;
}

static int sep_ok(int sep)
{
    return sep == ',' || sep == ';' || sep == '\t';
}

ob_int ob_csv_export_with(ob_project *p, const char *table, const ob_view *v, const char *path,
                          const ob_csv_opts *opts)
{
    const ob_table *t;
    csv_out *o;
    int i, first = 1, n, shut, sep = opts && opts->sep ? opts->sep : ',';
    if (!(t = design(p, table))) return -1;
    if (!sep_ok(sep)) { fail(p, "CSV values are separated by a comma, a semicolon or a tab"); return -1; }
    if (v && !ob_view_check(p, table, v)) return -1;
    if (!(t = design(p, table))) return -1;
    if (!(o = calloc(1, sizeof *o))) { fail(p, "Not enough memory"); return -1; }
    if (!(o->f = fopen(path, "wb"))) { free(o); fail(p, "%s could not be made", path); return -1; }
    o->sep = sep;
    o->utf8 = opts && opts->charset == OB_CHARSET_UTF8;
    o->nf = t->nfields;
    if (o->utf8) fputs("\xEF\xBB\xBF", o->f);
    for (i = 0; i < t->nfields; i++) {
        o->types[i] = (unsigned char)t->fields[i].type;
        if (t->fields[i].type == OB_OBJECT) continue;
        csv_cell(o, t->fields[i].name, first);
        first = 0;
    }
    fputs("\r\n", o->f);
    n = rows_from(p, table, v, 0, 0, -1, csv_row, o);
    shut = fclose(o->f);
    if (n >= 0 && (o->bad || shut != 0)) { fail(p, "%s could not be written: is the disk full?", path); n = -1; }
    free(o);
    return n;
}

ob_int ob_csv_export(ob_project *p, const char *table, const ob_view *v, const char *path, int sep)
{
    ob_csv_opts o;
    ob_csv_opts_init(&o);
    o.sep = sep;
    return ob_csv_export_with(p, table, v, path, &o);
}

/* A CSV reader: one record at a time, its cells in one buffer. */
typedef struct csv_in {
    FILE *f;
    int sep;
    int utf8;
    char *buf;
    size_t len, cap;
    int ncells;
    size_t cell[OB_MAX_FIELDS + 1];
    int too_many;
    long line;      /* the line the record starts on */
    long next_line;
    int peek;
} csv_in;

static int csv_get(csv_in *r)
{
    int c;
    if (r->peek != EOF - 1) { c = r->peek; r->peek = EOF - 1; return c; }
    return getc(r->f);
}

static int csv_put(csv_in *r, int c)
{
    if (r->len + 1 >= r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 1024;
        char *b = realloc(r->buf, cap);
        if (!b) return 0;
        r->buf = b;
        r->cap = cap;
    }
    r->buf[r->len++] = (char)c;
    return 1;
}

static int csv_end_cell(csv_in *r)
{
    if (!csv_put(r, 0)) return 0;
    if (r->ncells < OB_MAX_FIELDS) r->cell[r->ncells] = r->len;
    else r->too_many = 1;
    r->ncells++;
    return 1;
}

/* UTF-8 text made ISO-8859-1 in place: what it lacks becomes '?'. */
static void utf8_to_latin1(char *s)
{
    unsigned char *in = (unsigned char *)s, *out = (unsigned char *)s;
    while (*in) {
        unsigned c = *in;
        int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        if (c < 0x80) { *out++ = *in++; continue; }
        if (c < 0xc0) { *out++ = '?'; in++; continue; }
        {
            unsigned v = c & (0x3f >> more);
            int k;
            in++;
            for (k = 0; k < more && (*in & 0xc0) == 0x80; k++) v = (v << 6) | (*in++ & 0x3f);
            *out++ = k == more && v <= 0xff ? (unsigned char)v : '?';
        }
    }
    *out = 0;
}

/* Reads one record: 1, 0 at the end of the file, -1 out of memory, -2 a
 * quote that never ends. */
static int csv_record(csv_in *r)
{
    int c, quoted = 0, any = 0, rc = 1, i;
    r->len = 0;
    r->ncells = 0;
    r->too_many = 0;
    r->cell[0] = 0;
    r->line = r->next_line;
    for (;;) {
        c = csv_get(r);
        if (c == EOF) {
            if (quoted) return -2;
            if (!any) return 0;
            if (!csv_end_cell(r)) return -1;
            break;
        }
        any = 1;
        if (quoted) {
            if (c == '"') {
                int d = csv_get(r);
                if (d == '"') { if (!csv_put(r, '"')) return -1; continue; }
                r->peek = d;
                quoted = 0;
                continue;
            }
            if (c == '\n') r->next_line++;
            if (c == '\r') {        /* CR LF inside a value is a line end: keep LF */
                int d = csv_get(r);
                if (d != '\n') r->peek = d;
                r->next_line++;
                c = '\n';
            }
            if (!csv_put(r, c)) return -1;
            continue;
        }
        if (c == '"' && (r->len == (r->ncells ? r->cell[r->ncells - 1] : 0))) { quoted = 1; continue; }
        if (c == r->sep) { if (!csv_end_cell(r)) return -1; continue; }
        if (c == '\r' || c == '\n') {
            if (c == '\r') { int d = csv_get(r); if (d != '\n') r->peek = d; }
            r->next_line++;
            if (!csv_end_cell(r)) return -1;
            break;
        }
        if (!csv_put(r, c)) return -1;
    }
    if (r->utf8)
        for (i = 0; i < r->ncells && i < OB_MAX_FIELDS; i++) utf8_to_latin1(r->buf + (i ? r->cell[i - 1] : 0));
    return rc;
}

static const char *csv_text(const csv_in *r, int i)
{
    return r->buf + (i ? r->cell[i - 1] : 0);
}

/* The separator the first line uses most, outside quotes: ',' when none. */
static int csv_guess_sep(FILE *f)
{
    long at = ftell(f);
    int c, quoted = 0, comma = 0, semi = 0, tab = 0;
    while ((c = getc(f)) != EOF) {
        if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '\n' || c == '\r')) break;
        else if (!quoted) { comma += c == ','; semi += c == ';'; tab += c == '\t'; }
    }
    fseek(f, at, SEEK_SET);
    if (tab > comma && tab >= semi) return '\t';
    if (semi > comma) return ';';
    return ',';
}

/* Whether the bytes above 127 from here on are all UTF-8, and there is one. */
static int csv_looks_utf8(FILE *f)
{
    long at = ftell(f);
    int c, need = 0, high = 0, good = 1;
    while (good && (c = getc(f)) != EOF) {
        if (need) {
            if ((c & 0xc0) != 0x80) good = 0;
            need--;
            continue;
        }
        if (c < 0x80) continue;
        high = 1;
        if (c >= 0xc2 && c <= 0xdf) need = 1;
        else if (c >= 0xe0 && c <= 0xef) need = 2;
        else if (c >= 0xf0 && c <= 0xf4) need = 3;
        else good = 0;
    }
    if (need) good = 0;
    fseek(f, at, SEEK_SET);
    return good && high;
}

/* Opens path for reading CSV the way o says: the mark skipped, the
 * character set and the separator found. NULL with the reason. */
static csv_in *csv_open(ob_project *p, const char *path, ob_csv_opts *o)
{
    csv_in *r = calloc(1, sizeof *r);
    unsigned char bom[3];
    int marked;
    if (!r) { fail(p, "Not enough memory"); return NULL; }
    if (o->sep && !sep_ok(o->sep)) { free(r); fail(p, "CSV values are separated by a comma, a semicolon or a tab"); return NULL; }
    if (!(r->f = fopen(path, "rb"))) { free(r); fail(p, "%s could not be opened", path); return NULL; }
    marked = fread(bom, 1, 3, r->f) == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF;
    if (!marked) rewind(r->f);
    r->utf8 = o->charset == OB_CHARSET_UTF8 || (o->charset == OB_CHARSET_AUTO && (marked || csv_looks_utf8(r->f)));
    r->sep = o->sep ? o->sep : csv_guess_sep(r->f);
    r->peek = EOF - 1;
    r->next_line = 1;
    o->found_sep = r->sep;
    o->found_charset = r->utf8 ? OB_CHARSET_UTF8 : OB_CHARSET_LATIN1;
    return r;
}

static void csv_close(csv_in *r)
{
    if (!r) return;
    if (r->f) fclose(r->f);
    free(r->buf);
    free(r);
}

static void report_line(ob_csv_opts *o, long line, const char *why)
{
    size_t n = strlen(o->report);
    o->bad++;
    if (n + 4 >= sizeof o->report) return;
    if (snprintf(o->report + n, sizeof o->report - n, "Line %ld: %s\n", line, why) >= (int)(sizeof o->report - n))
        copy(o->report + sizeof o->report - 5, 5, "...\n");
}

ob_int ob_csv_import_with(ob_project *p, const char *table, const char *path, ob_csv_opts *o)
{
    const ob_table *t;
    csv_in *r = NULL;
    char (*names)[OB_NAME_MAX] = NULL;
    const char *fields[OB_MAX_FIELDS], *vals[OB_MAX_FIELDS];
    int map[OB_MAX_FIELDS];
    int nf, ncols = 0, i, j, rc, header = 1, good = 0, began = 0;
    ob_int added = 0, nkeys = 0, capkeys = 0, *keys = NULL;
    char tname[OB_NAME_MAX];
    ob_csv_opts dflt;

    if (!o) { ob_csv_opts_init(&dflt); o = &dflt; }
    o->bad = 0;
    o->report[0] = 0;
    if (!(t = design(p, table))) return -1;
    if (p->readonly) { fail(p, "The project is open read-only"); return -1; }
    /* keep what the loop needs: ob_row_add reads designs */
    copy(tname, sizeof tname, t->name);
    nf = t->nfields;
    names = malloc(sizeof *names * (size_t)(nf ? nf : 1));
    if (!names) { fail(p, "Not enough memory"); goto out; }
    for (i = 0; i < nf; i++) copy(names[i], sizeof names[i], t->fields[i].name);
    if (!(r = csv_open(p, path, o))) goto out;
    if (!(t = design(p, tname))) goto out;

    rc = csv_record(r);
    if (rc == 0) { fail(p, "%s is empty", path); goto out; }
    if (rc < 0) goto bad_read;
    if (r->too_many) { fail(p, "Line 1 has more than %d values", OB_MAX_FIELDS); goto out; }
    /* a header: every cell names a field */
    header = o->header != 0;
    for (i = 0; i < r->ncells && header; i++) {
        const char *h = csv_text(r, i);
        int found = -1, taken = -1, k;
        while (*h == ' ') h++;
        /* by name first, then by label; a field another column took is passed over */
        for (k = 0; k < 2 && found < 0; k++)
            for (j = 0; j < nf && found < 0; j++) {
                const ob_field *f = &t->fields[j];
                int m, used = 0;
                if (!(k ? f->label[0] && same(h, f->label) : same(h, f->name))) continue;
                for (m = 0; m < i; m++) if (map[m] == j) used = 1;
                if (used) taken = j; else found = j;
            }
        if (found < 0 && taken >= 0) { fail(p, "Line 1 names %s twice", names[taken]); goto out; }
        if (found < 0) {
            if (o->header == 1) { fail(p, "Line 1: %s is not a field of %s", h, tname); goto out; }
            header = 0;
        } else {
            map[i] = found;
        }
    }
    o->found_header = header;
    if (header) {
        ncols = r->ncells;
        for (i = 0; i < ncols; i++)
            if (t->fields[map[i]].type == OB_OBJECT) { fail(p, "%s holds Datatype objects, which CSV can't carry", names[map[i]]); goto out; }
    } else {
        int skip_counter;
        for (i = 0; i < nf; i++) if (t->fields[i].type != OB_OBJECT) ncols++;
        skip_counter = r->ncells == ncols - 1 && nf && t->fields[0].type == OB_COUNTER;
        ncols = 0;
        for (i = 0; i < nf; i++) {
            if (t->fields[i].type == OB_OBJECT) continue;
            if (skip_counter && t->fields[i].type == OB_COUNTER) { skip_counter = 0; continue; }
            map[ncols++] = i;
        }
    }

    if (!step_begin(p)) goto out;
    began = 1;
    p->undo_off++;
    if (header) rc = csv_record(r);
    for (; rc == 1; rc = csv_record(r)) {
        int n = 0;
        ob_int key;
        if (r->ncells == 1 && !csv_text(r, 0)[0]) continue;     /* a blank line */
        if (r->too_many || r->ncells > ncols) {
            char why[96];
            snprintf(why, sizeof why, "it has %d values: the table takes %d", r->ncells, ncols);
            if (o->skip_bad) { report_line(o, r->line, why); continue; }
            fail(p, "Line %ld has %d values: the table takes %d", r->line, r->ncells, ncols);
            goto out_undo;
        }
        for (i = 0; i < ncols; i++) {
            const char *c = i < r->ncells ? csv_text(r, i) : "";
            vals[n] = *c ? c : NULL;
            fields[n] = names[map[i]];
            n++;
        }
        if (!ob_row_add(p, tname, n, fields, vals, &key)) {
            char e[OB_ERR_MAX];
            copy(e, sizeof e, p->err);
            if (o->skip_bad) { report_line(o, r->line, e); continue; }
            fail(p, "Line %ld: %s", r->line, e);
            goto out_undo;
        }
        if (nkeys == capkeys) {
            ob_int cap = capkeys ? capkeys * 2 : 64, *k = realloc(keys, sizeof *keys * (size_t)cap);
            if (!k) { fail(p, "Not enough memory"); goto out_undo; }
            keys = k;
            capkeys = cap;
        }
        keys[nkeys++] = key;
        added++;
    }
    if (rc < 0) { p->undo_off--; goto bad_read; }
    if (ferror(r->f)) { fail(p, "%s could not be read", path); goto out_undo; }
    good = 1;
out_undo:
    p->undo_off--;
    goto out;
bad_read:
    if (rc == -2) fail(p, "Line %ld: a quote is never closed", r->line);
    else fail(p, "Not enough memory");
out:
    if (began) good = step_end(p, good);
    csv_close(r);
    free(names);
    if (!good) { free(keys); return -1; }
    if (added) {
        undo_step *u = undo_new(p, U_IMPORT, tname, 0);
        if (u) { u->keys = keys; u->nkeys = nkeys; keys = NULL; undo_push(p, u); }
        else ob_undo_clear(p);
    }
    free(keys);
    ok(p);
    return added;
}

ob_int ob_csv_import(ob_project *p, const char *table, const char *path, int sep)
{
    ob_csv_opts o;
    ob_csv_opts_init(&o);
    o.sep = sep;
    return ob_csv_import_with(p, table, path, &o);
}

/* ---- A table from a CSV file ---- */

#define GUESS_ROWS 200

typedef struct col_guess {
    int filled;
    int yesno, yesno_word, whole, money, money_hint, money_odd, number, date, time, datetime;
    int longest;
} col_guess;

static void guess_start(col_guess *g)
{
    memset(g, 0, sizeof *g);
    g->yesno = g->whole = g->money = g->number = g->date = g->time = g->datetime = 1;
}

static void guess_cell(col_guess *g, const char *cell)
{
    char t[128], tmp[80];
    const char *s;
    ob_int v;
    int b, len = (int)strlen(cell);
    if (len > g->longest) g->longest = len;
    trimmed(cell, t, sizeof t);
    if (!t[0]) return;
    g->filled++;
    if (len >= (int)sizeof t) { g->yesno = g->whole = g->money = g->number = g->date = g->time = g->datetime = 0; return; }
    if (!parse_yesno(t, &b)) g->yesno = 0;
    else if (!isdigit((unsigned char)t[0])) g->yesno_word = 1;
    if (!parse_int(t, &v)) g->whole = 0;
    if (!ob_currency_parse(t, &v)) g->money = 0;
    else {
        const char *dot = strrchr(t, '.');
        if (strchr(t, '$') || strchr(t, '\xa3')) g->money_hint = 1;
        if (dot) { if (strlen(dot + 1) == 2) g->money_hint = 1; else g->money_odd = 1; }
    }
    if (!parse_number(t, tmp, sizeof tmp)) g->number = 0;
    s = t; if (!parse_date(&s, tmp) || *s) g->date = 0;
    s = t; if (!parse_time(&s, tmp) || *s) g->time = 0;
    s = t;
    if (!parse_date(&s, tmp) || *s != ' ' || (s = skip_space(s), !parse_time(&s, tmp)) || *s) g->datetime = 0;
}

static ob_type guess_type(const col_guess *g)
{
    if (!g->filled) return OB_TEXT;
    if (g->yesno && g->yesno_word) return OB_YESNO;
    if (g->whole && !g->money_hint) return OB_INTEGER;
    if (g->money && g->money_hint && !g->money_odd) return OB_CURRENCY;
    if (g->number) return OB_NUMBER;
    if (g->date) return OB_DATE;
    if (g->time) return OB_TIME;
    if (g->datetime) return OB_DATETIME;
    return g->longest > 255 ? OB_MEMO : OB_TEXT;
}

/* A name GFOpenBase takes from text: letters, digits, spaces and _ (others
 * dropped), starting with a letter, at most OB_NAME_MAX - 1. */
static void name_from(const char *in, char *out, size_t size)
{
    size_t o = 0;
    for (; *in && o + 1 < size; in++) {
        unsigned char c = (unsigned char)*in;
        if (!o && !isalpha(c)) continue;
        if (isalnum(c) || c == '_') out[o++] = (char)c;
        else if ((c == ' ' || c == '-' || c == '.' || c == '/') && o && out[o - 1] != ' ') out[o++] = ' ';
    }
    while (o && out[o - 1] == ' ') o--;
    out[o] = 0;
    if (starts_nocase(out, "oadb_") || starts_nocase(out, "sqlite_") || same(out, "rowid") || same(out, "oid")
        || same(out, "_rowid_")) {
        char kept[OB_NAME_MAX];
        copy(kept, sizeof kept, out);
        snprintf(out, size, "F %.60s", kept);
    }
}

static int name_taken(const ob_table *t, const char *name)
{
    return ob_table_find_field(t, name) >= 0;
}

static void unique_name(const ob_table *t, char *name, size_t size)
{
    char base[OB_NAME_MAX];
    int k = 2;
    copy(base, sizeof base, name);
    while (name_taken(t, name) && k < 1000) snprintf(name, size, "%.50s %d", base, k++);
}

int ob_csv_guess(ob_project *p, const char *path, const char *name, ob_csv_opts *o, ob_table *out)
{
    csv_in *r;
    col_guess *g = NULL;
    char (*first)[OB_LABEL_MAX] = NULL;
    int rc, rows = 0, ncols = 0, i, header, id_col = -1;
    ob_type types[OB_MAX_FIELDS];
    char tname[OB_NAME_MAX];
    ob_csv_opts dflt;
    if (!o) { ob_csv_opts_init(&dflt); o = &dflt; }
    if (!(r = csv_open(p, path, o))) return 0;
    rc = csv_record(r);
    if (rc == 0) { csv_close(r); return fail(p, "%s is empty", path); }
    if (rc < 0) { csv_close(r); return fail(p, rc == -2 ? "Line 1: a quote is never closed" : "Not enough memory"); }
    if (r->too_many) { csv_close(r); return fail(p, "Line 1 has more than %d values: a table has at most %d fields", OB_MAX_FIELDS, OB_MAX_FIELDS - 1); }
    ncols = r->ncells;
    if (ncols >= OB_MAX_FIELDS) { csv_close(r); return fail(p, "A table has at most %d fields besides its Counter", OB_MAX_FIELDS - 1); }
    g = calloc((size_t)ncols, sizeof *g);
    first = calloc((size_t)ncols, sizeof *first);
    if (!g || !first) { free(g); free(first); csv_close(r); return fail(p, "Not enough memory"); }
    for (i = 0; i < ncols; i++) { copy(first[i], sizeof first[i], csv_text(r, i)); guess_start(&g[i]); }
    while (rows < GUESS_ROWS && (rc = csv_record(r)) == 1) {
        if (r->ncells == 1 && !csv_text(r, 0)[0]) continue;
        for (i = 0; i < ncols && i < r->ncells; i++) guess_cell(&g[i], csv_text(r, i));
        rows++;
    }
    csv_close(r);
    if (rc < 0) { free(g); free(first); return fail(p, rc == -2 ? "A quote is never closed" : "Not enough memory"); }
    for (i = 0; i < ncols; i++) types[i] = guess_type(&g[i]);
    /* the first line: names, unless each cell fits its column's type and one
     * column isn't text */
    header = o->header;
    if (header < 0) {
        int typed = 0, fits = 1;
        for (i = 0; i < ncols; i++) {
            col_guess one;
            if (types[i] == OB_TEXT || types[i] == OB_MEMO || !first[i][0]) continue;
            typed = 1;
            guess_start(&one);
            guess_cell(&one, first[i]);
            if (guess_type(&one) != types[i] && !(types[i] == OB_NUMBER && one.number)
                && !(types[i] == OB_CURRENCY && one.money)) fits = 0;
        }
        header = !(rows && typed && fits);
    }
    if (!header)
        for (i = 0; i < ncols; i++) {
            guess_cell(&g[i], first[i]);
            types[i] = guess_type(&g[i]);
        }
    o->found_header = header;

    if (name && *name) name_from(name, tname, sizeof tname);
    else {
        const char *leafname = path, *c;
        char base[OB_NAME_MAX];
        for (c = path; *c; c++) if (*c == '/' || *c == ':') leafname = c + 1;
        copy(base, sizeof base, leafname);
        if (strrchr(base, '.')) *strrchr(base, '.') = 0;
        name_from(base, tname, sizeof tname);
    }
    if (!tname[0]) copy(tname, sizeof tname, "Imported");
    ob_table_init(out, tname);
    /* a column called id that holds whole numbers is the Counter */
    if (header)
        for (i = 0; i < ncols && id_col < 0; i++)
            if (same(first[i], "id") && types[i] == OB_INTEGER) id_col = i;
    {
        const char *cname = id_col >= 0 ? first[id_col] : "id";
        int clash = 0;
        if (id_col < 0 && header) for (i = 0; i < ncols; i++) if (same(first[i], "id")) clash = 1;
        if (clash) cname = "Key";
        /* ob_table_init gave it a Counter "id": name it */
        if (out->nfields && out->fields[0].type == OB_COUNTER) copy(out->fields[0].name, sizeof out->fields[0].name, cname);
        else ob_table_add_field(out, cname, OB_COUNTER);
    }
    for (i = 0; i < ncols; i++) {
        char fname[OB_NAME_MAX];
        ob_field *f;
        if (i == id_col) continue;
        if (header) name_from(first[i], fname, sizeof fname);
        else fname[0] = 0;
        if (!fname[0]) snprintf(fname, sizeof fname, "Field %d", i + 1);
        unique_name(out, fname, sizeof fname);
        if (!(f = ob_table_add_field(out, fname, types[i]))) break;
        /* the header as it was, so the import finds the column by it */
        if (header && strcmp(fname, first[i])) copy(f->label, sizeof f->label, first[i]);
    }
    free(g);
    free(first);
    return ok(p);
}
