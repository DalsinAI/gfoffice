/*
 * GFOpenBase's portable core: a project (one SQLite file, .oadb) and the
 * things GFOpenBase's window does to it, as openwrite_core.h is GFOpenWrite's
 * (docs/gfoffice/Design-OpenBase.md, sections 3 and 4):
 *
 *   - projects: create and open a .oadb (PRAGMA application_id "OADB",
 *     user_version the format), its design tables (oadb_meta, oadb_fields),
 *     foreign keys on for every connection, quick_check on open;
 *   - tables: GFOpenBase's field types (4.1) written as SQL columns and
 *     rules, read back from the file; create, change (SQLite's
 *     copy-and-rename way, in one transaction), rename and drop;
 *   - rows: add, change and delete, each value checked against its field's
 *     type, in the project's transaction or one of their own;
 *   - a datasheet's rows a page at a time, by key;
 *   - errors in plain words ("Price must be a number", not "CHECK
 *     constraint failed").
 *
 * Nothing here knows the Amiga: it builds and is tested on the PC
 * (tests/base) against the same pinned SQLite. Plain C99. The only
 * floating point is SQLite's own, for Number fields.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENBASE_CORE_H
#define OPENBASE_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OB_CORE_VERSION "0.1.0"

/* The .oadb header: PRAGMA application_id ("OADB") and user_version. */
#define OB_APPLICATION_ID 0x4F414442L
#define OB_FORMAT_VERSION 1

#define OB_NAME_MAX 64          /* a table's or field's name, with its 0 */
#define OB_LABEL_MAX 80         /* a field's label */
#define OB_VALUE_MAX 256        /* a default, a choice list */
#define OB_ERR_MAX 256          /* ob_error()'s text */
#define OB_MAX_FIELDS 96        /* fields in one table (an auto form's 8 x 12) */
#define OB_TEXT_MAX_LEN 32767   /* the most a Text field's maximum length can be */

typedef long long ob_int;       /* SQLite's 64-bit integer */

typedef enum ob_type {
    OB_COUNTER,     /* INTEGER PRIMARY KEY, the rowid; every new table gets one, "id" */
    OB_TEXT,        /* TEXT, with an optional maximum length */
    OB_MEMO,        /* TEXT, any length */
    OB_INTEGER,     /* INTEGER */
    OB_NUMBER,      /* REAL */
    OB_CURRENCY,    /* INTEGER pence or cents, shown 1,234.50 */
    OB_DATE,        /* TEXT YYYY-MM-DD */
    OB_TIME,        /* TEXT HH:MM:SS */
    OB_DATETIME,    /* TEXT YYYY-MM-DD HH:MM:SS */
    OB_YESNO,       /* INTEGER 0 or 1 */
    OB_CHOICE,      /* TEXT, one of a list */
    OB_LOOKUP,      /* INTEGER, another table's key: a relationship */
    OB_OBJECT,      /* BLOB: a Datatype object */
    OB_TYPE_COUNT
} ob_type;

/* What happens to a Lookup's rows when the row they point at is deleted. */
typedef enum ob_on_delete {
    OB_DELETE_REFUSE,   /* ON DELETE RESTRICT: the delete is refused */
    OB_DELETE_CASCADE,  /* ON DELETE CASCADE: they are deleted too */
    OB_DELETE_CLEAR     /* ON DELETE SET NULL: their field is cleared */
} ob_on_delete;

typedef struct ob_field {
    char name[OB_NAME_MAX];
    ob_type type;
    int required;                   /* NOT NULL */
    int unique;                     /* UNIQUE */
    int max_len;                    /* Text: CHECK (length(x) <= n); 0 none */
    int has_default;
    char deflt[OB_VALUE_MAX];       /* in the field's own text form ("12.50", "Yes") */
    char label[OB_LABEL_MAX];       /* "" is the name */
    char choices[OB_VALUE_MAX];     /* Choice: the values, separated by '|' */
    char lookup[OB_NAME_MAX];       /* Lookup: the table whose key it holds */
    ob_on_delete on_delete;         /* Lookup */
    /* ob_table_change only: the field this one was ("" for a new field).
     * Read back as the field's own name. */
    char from[OB_NAME_MAX];
} ob_field;

typedef struct ob_table {
    char name[OB_NAME_MAX];
    int nfields;
    ob_field fields[OB_MAX_FIELDS];
} ob_table;

typedef struct ob_project ob_project;

/* ---- Projects ---- */

enum {
    OB_OPEN_CREATE = 1,     /* make the file if there is none */
    OB_OPEN_READONLY = 2,   /* SQLITE_OPEN_READONLY: a kiosk, a CD */
    OB_OPEN_NOLOCK = 4,     /* the "unix-none" VFS: the caller keeps its own lock (design 3.2) */
    OB_OPEN_ADOPT = 8       /* a plain SQLite file (no OADB header) is made a project */
};

/* Opens (or with OB_OPEN_CREATE makes) the project at path. NULL on failure,
 * with the reason in err (plain words). */
ob_project *ob_open(const char *path, int flags, char *err, size_t errsize);
void ob_close(ob_project *p);

/* The last error, in plain words; "" after a call that succeeded. */
const char *ob_error(const ob_project *p);

/* The project's title (oadb_meta "title"); "" when it has none. */
const char *ob_title(ob_project *p);
int ob_set_title(ob_project *p, const char *title);

/* PRAGMA integrity_check: 1 when the file is whole. */
int ob_check(ob_project *p, int full);

/* VACUUM INTO path (Save a Copy), or VACUUM when path is NULL (Compact). */
int ob_copy(ob_project *p, const char *path);

/* ---- Transactions ----
 * Row and table calls made between ob_begin and ob_commit or ob_rollback are
 * one change; outside them each call is its own transaction. */
int ob_begin(ob_project *p);
int ob_commit(ob_project *p);
int ob_rollback(ob_project *p);
int ob_in_transaction(const ob_project *p);

/* ---- Tables ---- */

/* The project's own tables (not oadb_* or sqlite_*), in name order. */
int ob_table_count(ob_project *p);
const char *ob_table_name(ob_project *p, int i);

/* A new table with one Counter field "id", for the designer to fill in. */
void ob_table_init(ob_table *t, const char *name);
/* A field of the type with its rules empty; added at the end. NULL when full. */
ob_field *ob_table_add_field(ob_table *t, const char *name, ob_type type);
int ob_table_find_field(const ob_table *t, const char *name);

/* Reads a table's design from the file. 0 when there is none. */
int ob_table_get(ob_project *p, const char *name, ob_table *out);

/* Checks a design without touching the file: names, types, rules, a
 * Lookup's table. 0 with the reason in ob_error. */
int ob_table_check(ob_project *p, const ob_table *t);

int ob_table_create(ob_project *p, const ob_table *t);
/* Changes table old_name to t: fields with from[] set keep their rows' values
 * (converted to the new type where SQLite can), new ones get their default.
 * t->name may differ from old_name (a rename). */
int ob_table_change(ob_project *p, const char *old_name, const ob_table *t);
int ob_table_rename(ob_project *p, const char *old_name, const char *new_name);
int ob_table_drop(ob_project *p, const char *name);

/* The CREATE TABLE statement for t, as ob_table_create writes it (for the
 * designer's SQL view and the golden tests). Returns its length, or -1 when it
 * does not fit in size bytes. */
int ob_table_sql(const ob_table *t, const char *as_name, char *out, size_t size);

/* ---- Rows ----
 * Values are text in the field's own form, as a datasheet cell or a form's
 * gadget holds them: "1,234.50" for Currency, "Yes"/"No" for Yes/No,
 * "2026-11-11" for a Date; NULL (or "" for a field that isn't Text or Memo)
 * is empty. Each is checked against its field (ob_value_check) and stored
 * as the field's SQL type. */

/* Adds a row; *key gets its Counter (rowid). Fields left out get their
 * default. */
int ob_row_add(ob_project *p, const char *table, int n, const char *const *fields,
               const char *const *values, ob_int *key);
int ob_row_change(ob_project *p, const char *table, ob_int key, int n,
                  const char *const *fields, const char *const *values);
int ob_row_delete(ob_project *p, const char *table, ob_int key);
ob_int ob_row_total(ob_project *p, const char *table);

/* A datasheet page: up to max rows after key `after` (0 for the first page),
 * in key order. cb gets each row's key and its values in the table's field
 * order, in their own text form (NULL when empty); a non-zero return stops.
 * Returns the number of rows given, -1 on error. */
typedef int (*ob_row_fn)(void *user, ob_int key, int n, const char *const *values);
int ob_rows(ob_project *p, const char *table, ob_int after, int max, ob_row_fn cb, void *user);

/* ---- Values ---- */

/* Whether text is a valid value for f; 0 with the reason in err ("Price must
 * be a number"). Empty text is checked against required. */
int ob_value_check(const ob_field *f, const char *text, char *err, size_t errsize);

/* Currency: "1,234.50", "-0.05", "12" in, pence out; 0 when it isn't money. */
int ob_currency_parse(const char *text, ob_int *pence);
void ob_currency_format(ob_int pence, char *out, size_t size);

const char *ob_type_name(ob_type t);          /* "Text", "Currency"... */
int ob_type_from_name(const char *name);      /* -1 when unknown */

#ifdef __cplusplus
}
#endif

#endif
