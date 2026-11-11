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

/* The rollback journal's mode for every project (Design-OpenBase.md 3.2: the
 * B0 probe's numbers are in section 11.3). */
#ifndef OB_JOURNAL_MODE
#define OB_JOURNAL_MODE "DELETE"
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
};

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
    snprintf(out, 16, "%04d-%02d-%02d", y, m, d);
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
    snprintf(out, 16, "%02d:%02d:%02d", h, m, sec);
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
            snprintf(err, errsize, "%.60s must be one of: %.180s", label, list);
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
    if (f->required && f->type != OB_COUNTER) {
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
        if ((c = strstr(m, "UNIQUE constraint failed: ")) != NULL && (c = strchr(c + 26, '.')) != NULL)
            return fail(p, "%s must be different in every row", t && (f = field_named(t, c + 1)) ? field_label(f) : c + 1);
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
    free(p);
    return NULL;
}

void ob_close(ob_project *p)
{
    if (!p) return;
    if (p->in_txn) sqlite3_exec(p->db, "ROLLBACK", NULL, NULL, NULL);
    sqlite3_close(p->db);
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
    p->tables_valid = 0;
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
    p->tables_valid = 0;
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

int ob_table_check(ob_project *p, const ob_table *t)
{
    int i, j, counters = 0;
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
        if (f->type == OB_LOOKUP) {
            ob_table other;
            if (!f->lookup[0]) return fail(p, "%s needs the table it looks up", f->name);
            if (!same(f->lookup, t->name)) {
                if (!p || !ob_table_get(p, f->lookup, &other))
                    return fail(p, "%s looks up %s, which isn't a table", f->name, f->lookup);
                for (j = 0; j < other.nfields && other.fields[j].type != OB_COUNTER; j++) ;
                if (j == other.nfields) return fail(p, "%s looks up %s, which has no Counter", f->name, f->lookup);
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
    return p ? ok(p) : 1;
}

/* One column's definition, appended to s. */
static void column_sql(sqlite3_str *s, const ob_field *f)
{
    sqlite3_str_appendf(s, "\"%w\" %s", f->name, sql_types[f->type]);
    if (f->type == OB_COUNTER) {
        sqlite3_str_appendall(s, " PRIMARY KEY");
        return;
    }
    if (f->required) sqlite3_str_appendall(s, " NOT NULL");
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

int ob_table_sql(const ob_table *t, const char *as_name, char *out, size_t size)
{
    sqlite3_str *s = sqlite3_str_new(NULL);
    char *sql;
    int i, len;
    sqlite3_str_appendf(s, "CREATE TABLE \"%w\" (", as_name ? as_name : t->name);
    for (i = 0; i < t->nfields; i++) {
        sqlite3_str_appendall(s, i ? ",\n  " : "\n  ");
        column_sql(s, &t->fields[i]);
    }
    sqlite3_str_appendall(s, "\n)");
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
    int i;
    sqlite3_str_appendf(s, "CREATE TABLE \"%w\" (", as_name ? as_name : t->name);
    for (i = 0; i < t->nfields; i++) {
        sqlite3_str_appendall(s, i ? ",\n  " : "\n  ");
        column_sql(s, &t->fields[i]);
    }
    sqlite3_str_appendall(s, "\n)");
    return sqlite3_str_finish(s);
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
    good = run(p, sql) && save_fields(p, t);
    sqlite3_free(sql);
    p->tables_valid = 0;
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

int ob_table_change(ob_project *p, const char *old_name, const ob_table *t)
{
    ob_table old;
    sqlite3_str *cols, *vals;
    char *sql = NULL, *cs = NULL, *vs = NULL;
    int i, good = 0, any = 0, rc;
    if (p->in_txn) return fail(p, "Save or undo the row changes first");
    if (!ob_table_get(p, old_name, &old)) return 0;
    if (!ob_table_check(p, t)) return 0;
    if (!same(old.name, t->name) && table_exists(p, t->name))
        return fail(p, "There is a table named %s already", t->name);
    for (i = 0; i < t->nfields; i++)
        if (t->fields[i].from[0] && ob_table_find_field(&old, t->fields[i].from) < 0)
            return fail(p, "%s has no field %s", old.name, t->fields[i].from);
    cols = sqlite3_str_new(NULL);
    vals = sqlite3_str_new(NULL);
    for (i = 0; i < t->nfields; i++) {
        const ob_field *f = &t->fields[i];
        int j = f->from[0] ? ob_table_find_field(&old, f->from) : -1;
        if (j < 0) continue;
        sqlite3_str_appendf(cols, "%s\"%w\"", any ? ", " : "", f->name);
        if (any) sqlite3_str_appendall(vals, ", ");
        convert_sql(vals, &old.fields[j], f);
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
        sql = sqlite3_mprintf("INSERT INTO oadb_change(%s) SELECT %s FROM \"%w\"", cs, vs, old.name);
        rc = sql ? sqlite3_exec(p->db, sql, NULL, NULL, NULL) : SQLITE_NOMEM;
        if (rc != SQLITE_OK) { change_fail(p, t, rc); goto undo; }
    }
    if (!runf(p, "DROP TABLE \"%w\"", old.name)) goto undo;
    if (!runf(p, "ALTER TABLE oadb_change RENAME TO \"%w\"", t->name)) goto undo;
    if (!runf(p, "DELETE FROM oadb_fields WHERE tbl = %Q", old.name) || !save_fields(p, t)) goto undo;
    if (!same(old.name, t->name)) {
        /* other tables' Lookups follow the rename (SQLite rewrote their REFERENCES) */
        if (!runf(p, "UPDATE oadb_fields SET lookup = %Q WHERE lookup = %Q", t->name, old.name)) goto undo;
    }
    {
        sqlite3_stmt *st = prep(p, "PRAGMA foreign_key_check");
        if (!st) goto undo;
        if (sqlite3_step(st) == SQLITE_ROW) {
            fail(p, "Changing %s would leave rows in %s looking up rows that aren't there", old.name,
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
    p->tables_valid = 0;
    return good ? ok(p) : 0;
}

int ob_table_rename(ob_project *p, const char *old_name, const char *new_name)
{
    char why[OB_ERR_MAX];
    ob_table t;
    int good;
    if (!ob_table_get(p, old_name, &t)) return 0;
    if (!name_ok(new_name, why, sizeof why, "table")) return fail(p, "%s", why);
    if (!same(t.name, new_name) && table_exists(p, new_name))
        return fail(p, "There is a table named %s already", new_name);
    if (!step_begin(p)) return 0;
    good = runf(p, "ALTER TABLE \"%w\" RENAME TO \"%w\"", t.name, new_name)
        && runf(p, "UPDATE oadb_fields SET tbl = %Q WHERE tbl = %Q", new_name, t.name)
        && runf(p, "UPDATE oadb_fields SET lookup = %Q WHERE lookup = %Q", new_name, t.name);
    p->tables_valid = 0;
    return step_end(p, good) && ok(p);
}

int ob_table_drop(ob_project *p, const char *name)
{
    ob_table t;
    sqlite3_stmt *st;
    int good;
    if (!ob_table_get(p, name, &t)) return 0;
    st = prep(p, "SELECT tbl FROM oadb_fields WHERE lookup = ?1 AND tbl <> ?1 LIMIT 1");
    if (!st) return 0;
    sqlite3_bind_text(st, 1, t.name, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW) {
        fail(p, "%s looks up %s: change or delete it first", (const char *)sqlite3_column_text(st, 0), t.name);
        sqlite3_finalize(st);
        return 0;
    }
    sqlite3_finalize(st);
    if (!step_begin(p)) return 0;
    good = runf(p, "DROP TABLE \"%w\"", t.name) && runf(p, "DELETE FROM oadb_fields WHERE tbl = %Q", t.name);
    p->tables_valid = 0;
    return step_end(p, good) && ok(p);
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

int ob_row_add(ob_project *p, const char *table, int n, const char *const *fields,
               const char *const *values, ob_int *key)
{
    ob_table t;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    int i, rc;
    if (!ob_table_get(p, table, &t)) return 0;
    if (n < 0 || n > t.nfields) return fail(p, "Too many fields");
    if (!names_unique(p, n, fields)) return 0;
    for (i = 0; i < n; i++)
        if (!field_named(&t, fields[i])) return fail(p, "%s has no field %s", t.name, fields[i]);
    /* a required field left out with no default: say so before SQLite does */
    for (i = 0; i < t.nfields; i++) {
        const ob_field *f = &t.fields[i];
        int j, given = 0;
        for (j = 0; j < n; j++) if (same(fields[j], f->name)) given = 1;
        if (!given && f->required && !f->has_default && f->type != OB_COUNTER)
            return fail(p, "%s must be filled in", field_label(f));
    }
    s = sqlite3_str_new(NULL);
    if (n == 0) {
        sqlite3_str_appendf(s, "INSERT INTO \"%w\" DEFAULT VALUES", t.name);
    } else {
        sqlite3_str_appendf(s, "INSERT INTO \"%w\"(", t.name);
        for (i = 0; i < n; i++) sqlite3_str_appendf(s, "%s\"%w\"", i ? ", " : "", fields[i]);
        sqlite3_str_appendall(s, ") VALUES(");
        for (i = 0; i < n; i++) sqlite3_str_appendf(s, "%s?%d", i ? ", " : "", i + 1);
        sqlite3_str_appendall(s, ")");
    }
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) return 0;
    if (!bind_fields(p, &t, st, 1, n, fields, values)) { sqlite3_finalize(st); return 0; }
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return sql_fail(p, &t, rc);
    if (key) *key = sqlite3_last_insert_rowid(p->db);
    return ok(p);
}

int ob_row_change(ob_project *p, const char *table, ob_int key, int n,
                  const char *const *fields, const char *const *values)
{
    ob_table t;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    int i, rc;
    if (!ob_table_get(p, table, &t)) return 0;
    if (n <= 0) return ok(p);
    if (n > t.nfields) return fail(p, "Too many fields");
    if (!names_unique(p, n, fields)) return 0;
    s = sqlite3_str_new(NULL);
    sqlite3_str_appendf(s, "UPDATE \"%w\" SET ", t.name);
    for (i = 0; i < n; i++) sqlite3_str_appendf(s, "%s\"%w\" = ?%d", i ? ", " : "", fields[i], i + 1);
    sqlite3_str_appendf(s, " WHERE rowid = ?%d", n + 1);
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) return 0;
    if (!bind_fields(p, &t, st, 1, n, fields, values)) { sqlite3_finalize(st); return 0; }
    sqlite3_bind_int64(st, n + 1, key);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return sql_fail(p, &t, rc);
    if (sqlite3_changes(p->db) == 0) return fail(p, "%s has no row %lld", t.name, key);
    return ok(p);
}

int ob_row_delete(ob_project *p, const char *table, ob_int key)
{
    ob_table t;
    char *sql;
    int rc;
    if (!ob_table_get(p, table, &t)) return 0;
    sql = sqlite3_mprintf("DELETE FROM \"%w\" WHERE rowid = %lld", t.name, key);
    rc = sql ? sqlite3_exec(p->db, sql, NULL, NULL, NULL) : SQLITE_NOMEM;
    sqlite3_free(sql);
    if (rc != SQLITE_OK) {
        if ((rc & 0xff) == SQLITE_CONSTRAINT) {
            /* a Lookup that refuses: name the table that still looks it up */
            sqlite3_stmt *st = prep(p, "SELECT tbl FROM oadb_fields WHERE lookup = ?1 AND on_delete = 0 LIMIT 1");
            if (st) {
                sqlite3_bind_text(st, 1, t.name, -1, SQLITE_STATIC);
                if (sqlite3_step(st) == SQLITE_ROW) {
                    fail(p, "%s row %lld still has rows in %s", t.name, key, (const char *)sqlite3_column_text(st, 0));
                    sqlite3_finalize(st);
                    return 0;
                }
                sqlite3_finalize(st);
            }
        }
        return sql_fail(p, &t, rc);
    }
    if (sqlite3_changes(p->db) == 0) return fail(p, "%s has no row %lld", t.name, key);
    return ok(p);
}

ob_int ob_row_total(ob_project *p, const char *table)
{
    ob_table t;
    char *sql;
    ob_int n;
    if (!ob_table_get(p, table, &t)) return -1;
    sql = sqlite3_mprintf("SELECT count(*) FROM \"%w\"", t.name);
    n = sql ? scalar(p, sql, -1) : -1;
    sqlite3_free(sql);
    return n;
}

int ob_rows(ob_project *p, const char *table, ob_int after, int max, ob_row_fn cb, void *user)
{
    ob_table t;
    sqlite3_str *s;
    sqlite3_stmt *st;
    char *sql;
    const char *vals[OB_MAX_FIELDS];
    char (*cells)[48];
    int i, n = 0, rc;
    if (!ob_table_get(p, table, &t)) return -1;
    if (max <= 0) return 0;
    cells = malloc(sizeof *cells * (size_t)t.nfields);
    if (!cells) { fail(p, "Not enough memory"); return -1; }
    s = sqlite3_str_new(NULL);
    sqlite3_str_appendall(s, "SELECT rowid");
    for (i = 0; i < t.nfields; i++) {
        if (t.fields[i].type == OB_OBJECT) sqlite3_str_appendf(s, ", length(\"%w\")", t.fields[i].name);
        else sqlite3_str_appendf(s, ", \"%w\"", t.fields[i].name);
    }
    sqlite3_str_appendf(s, " FROM \"%w\" WHERE rowid > ?1 ORDER BY rowid LIMIT ?2", t.name);
    sql = sqlite3_str_finish(s);
    st = sql ? prep(p, sql) : NULL;
    sqlite3_free(sql);
    if (!st) { free(cells); return -1; }
    sqlite3_bind_int64(st, 1, after);
    sqlite3_bind_int(st, 2, max);
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        for (i = 0; i < t.nfields; i++) {
            const ob_field *f = &t.fields[i];
            int c = i + 1;
            if (sqlite3_column_type(st, c) == SQLITE_NULL) { vals[i] = NULL; continue; }
            if (f->type == OB_CURRENCY && sqlite3_column_type(st, c) == SQLITE_INTEGER) {
                ob_currency_format(sqlite3_column_int64(st, c), cells[i], sizeof cells[i]);
                vals[i] = cells[i];
            } else if (f->type == OB_YESNO && sqlite3_column_type(st, c) == SQLITE_INTEGER) {
                vals[i] = sqlite3_column_int64(st, c) ? "Yes" : "No";
            } else if (f->type == OB_OBJECT) {
                snprintf(cells[i], sizeof cells[i], "(%lld bytes)", (ob_int)sqlite3_column_int64(st, c));
                vals[i] = cells[i];
            } else {
                vals[i] = (const char *)sqlite3_column_text(st, c);
            }
        }
        n++;
        if (cb && cb(user, sqlite3_column_int64(st, 0), t.nfields, vals)) { rc = SQLITE_DONE; break; }
    }
    sqlite3_finalize(st);
    free(cells);
    if (rc != SQLITE_DONE) { sql_fail(p, &t, rc); return -1; }
    ok(p);
    return n;
}
