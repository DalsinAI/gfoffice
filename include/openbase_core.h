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
 *   - a datasheet's rows a page at a time, by key, or sorted and filtered
 *     (a view);
 *   - CSV out of a table (or a view of it) and into a table, or into a new
 *     table whose field types are guessed from the file;
 *   - undo of row changes;
 *   - queries: a designer's grid turned into SQL, or SQL typed in (read
 *     only), run a page at a time, saved in the project;
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

#define OB_CORE_VERSION "0.6.0"

/* The .oadb header: PRAGMA application_id ("OADB") and user_version. */
#define OB_APPLICATION_ID 0x4F414442L
#define OB_FORMAT_VERSION 1

#define OB_NAME_MAX 64          /* a table's or field's name, with its 0 */
#define OB_LABEL_MAX 80         /* a field's label */
#define OB_VALUE_MAX 256        /* a default, a choice list */
#define OB_ERR_MAX 256          /* ob_error()'s text */
#define OB_MAX_FIELDS 96        /* fields in one table (an auto form's 8 x 12) */
#define OB_TEXT_MAX_LEN 32767   /* the most a Text field's maximum length can be */
#define OB_MAX_INDEXES 16       /* indexes on one table */
#define OB_INDEX_FIELDS 8       /* fields in one index */

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
    int key;                        /* part of the table's key (PRIMARY KEY(...), in field
                                     * order): only in a table with no Counter */
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

/* An index (CREATE [UNIQUE] INDEX): its name is the project's, like a table's. */
typedef struct ob_index {
    char name[OB_NAME_MAX];
    int unique;
    int nfields;
    char fields[OB_INDEX_FIELDS][OB_NAME_MAX];
} ob_index;

/* About 90 KB: keep one static or on the heap, never on a 64 KB stack. */
typedef struct ob_table {
    char name[OB_NAME_MAX];
    int nfields;
    ob_field fields[OB_MAX_FIELDS];
    int nindexes;
    ob_index indexes[OB_MAX_INDEXES];
} ob_table;

/* A relationship: table.field (a Lookup) holds lookup's key. */
typedef struct ob_relation {
    char table[OB_NAME_MAX];
    char field[OB_NAME_MAX];
    char lookup[OB_NAME_MAX];
    ob_on_delete on_delete;
} ob_relation;

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
/* Removes or renames field i, and the same field in the table's indexes (an
 * index left with no fields goes too). */
void ob_table_remove_field(ob_table *t, int i);
void ob_table_rename_field(ob_table *t, int i, const char *name);

/* An index on fields, given as "Surname, First name"; added at the end. NULL
 * when the table has OB_MAX_INDEXES or a list has more than OB_INDEX_FIELDS
 * (names are checked by ob_table_check). */
ob_index *ob_table_add_index(ob_table *t, const char *name, int unique, const char *fields);
void ob_table_remove_index(ob_table *t, int i);
/* An index's fields as "Surname, First name". */
void ob_index_fields_text(const ob_index *x, char *out, size_t size);

/* Reads a table's design from the file. 0 when there is none. */
int ob_table_get(ob_project *p, const char *name, ob_table *out);

/* Checks a design without touching the file: names, types, rules, the key,
 * indexes, a Lookup's table. 0 with the reason in ob_error. A table has a
 * Counter or key fields. */
int ob_table_check(ob_project *p, const ob_table *t);

int ob_table_create(ob_project *p, const ob_table *t);
/* Changes table old_name to t: fields with from[] set keep their rows' values
 * (converted to the new type where SQLite can), new ones get their default.
 * t->name may differ from old_name (a rename). */
int ob_table_change(ob_project *p, const char *old_name, const ob_table *t);
int ob_table_rename(ob_project *p, const char *old_name, const char *new_name);
int ob_table_drop(ob_project *p, const char *name);

/* The CREATE TABLE statement for t, as ob_table_create writes it, then a
 * CREATE INDEX statement for each index, each after ";\n" (for the designer's
 * SQL view and the golden tests). Returns its length, or -1 when it does not
 * fit in size bytes. */
int ob_table_sql(const ob_table *t, const char *as_name, char *out, size_t size);

/* ---- Relationships ----
 * Every foreign key in the project (a Lookup, or one another program made),
 * in table and field order: up to max into out. Returns how many there are
 * (which may be more than max), -1 on error. */
int ob_relations(ob_project *p, ob_relation *out, int max);

/* ---- Rows ----
 * Values are text in the field's own form, as a datasheet cell or a form's
 * gadget holds them: "1,234.50" for Currency, "Yes"/"No" for Yes/No,
 * "2026-11-11" (or 11/11/2026) for a Date; NULL or "" is empty (NULL in the
 * file). Each is checked against its field (ob_value_check) and stored as
 * the field's SQL type. */

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
/* The same from the row at position `first` (0 is the first) in key order:
 * a datasheet scrolled to a place (LIMIT and OFFSET, design 5.4). */
int ob_rows_at(ob_project *p, const char *table, ob_int first, int max, ob_row_fn cb, void *user);

/* ---- Views: a datasheet sorted and filtered (design 5.4) ----
 * sort: a field to sort by ("" keeps key order), desc for Z to A; ties keep
 * key order. Text fields sort and compare without case.
 * filter: rows whose field `field` passes `op` against `value`, which is in
 * the field's own text form ("1,234.50", "Yes", 11/11/2026) and checked like
 * a row's value; Contains and Starts with are for fields that hold text
 * (Text, Memo, Choice, Date, Time, Date and time). */
typedef enum ob_filter_op {
    OB_FILTER_NONE,
    OB_FILTER_EQUALS,
    OB_FILTER_NOT_EQUALS,
    OB_FILTER_LESS,         /* less than */
    OB_FILTER_MORE,         /* more than */
    OB_FILTER_CONTAINS,
    OB_FILTER_STARTS,
    OB_FILTER_EMPTY,
    OB_FILTER_NOT_EMPTY,
    OB_FILTER_COUNT
} ob_filter_op;

typedef struct ob_view {
    char sort[OB_NAME_MAX];
    int desc;
    char field[OB_NAME_MAX];
    ob_filter_op op;
    char value[OB_VALUE_MAX];
} ob_view;

void ob_view_init(ob_view *v);                  /* key order, every row */
const char *ob_filter_name(ob_filter_op op);    /* "Contains"... */
/* Whether v is good for table (its fields exist, the value suits the field);
 * 0 with the reason in ob_error(). */
int ob_view_check(ob_project *p, const char *table, const ob_view *v);
/* The rows a view shows; -1 on error. */
ob_int ob_view_total(ob_project *p, const char *table, const ob_view *v);
/* ob_rows_at through a view (NULL is key order, every row). */
int ob_view_rows(ob_project *p, const char *table, const ob_view *v, ob_int first, int max,
                 ob_row_fn cb, void *user);

/* ---- Forms: one record at a time (core 0.6, design 6.3) ----
 * Positions count from 0 in the view's order (NULL: key order, every row),
 * as ob_view_rows takes them. */

/* Where the row with key `key` stands in view v: -1 when the view leaves it
 * out, -2 on error. */
ob_int ob_view_position(ob_project *p, const char *table, const ob_view *v, ob_int key);
/* Find: the first row at or after position `from` (backward: at or before)
 * whose field holds `text`. A field that holds text (Text, Memo, Choice,
 * Date, Time, Date and time) matches when it contains the text, any case;
 * any other field when it equals the value, given in the field's own form
 * ("1,250.50", "Yes"). Returns the row's position, -1 when none matches,
 * -2 with the reason in ob_error(). */
ob_int ob_view_find(ob_project *p, const char *table, const ob_view *v, const char *field, const char *text,
                    ob_int from, int backward);
/* Subforms: the Lookups in other tables that point at table (an order's
 * lines look up the order), up to max into out. A subform's rows are the
 * child table's rows whose Lookup holds the record's key: a view filtered on
 * r.field Equals the key. Returns how many there are, -1 on error. */
int ob_children(ob_project *p, const char *table, ob_relation *out, int max);

/* ---- CSV (design 8) ----
 * Export: a header of field names, then each row of the view (NULL: every
 * row in key order) with values in their own text form; quoted where they
 * hold the separator, a quote or a line end (RFC 4180), lines end CR LF.
 * Datatype objects are left out. sep is ',', ';' or '\t' (0 is ',').
 * Returns the rows written, -1 on error. */
ob_int ob_csv_export(ob_project *p, const char *table, const ob_view *v, const char *path, int sep);

/* Import: adds the file's rows to table, all or none (one transaction, or
 * a savepoint inside the caller's). The first line is a header when each of
 * its cells names a field (by name or label, any case): columns go to those
 * fields, in any order. Otherwise columns are the table's fields in order
 * (Datatype objects left out); a file one column short starts after the
 * Counter. A Counter value keeps that key; empty gets a new one. sep 0 finds
 * ',', ';' or tab from the first line. A UTF-8 byte order mark is skipped.
 * Returns the rows added, -1 with "Line 4: Price must be a number". */
ob_int ob_csv_import(ob_project *p, const char *table, const char *path, int sep);

/* ---- CSV with options (core 0.4, design 8) ---- */
typedef enum ob_charset {
    OB_CHARSET_AUTO,    /* import: UTF-8 when the file starts with its mark or its
                         * bytes above 127 are all UTF-8, else ISO-8859-1.
                         * Export: ISO-8859-1 */
    OB_CHARSET_LATIN1,  /* ISO-8859-1, as the Amiga keeps text */
    OB_CHARSET_UTF8     /* turned into ISO-8859-1 on import ('?' for what it lacks);
                         * export writes UTF-8 with its mark */
} ob_charset;

typedef struct ob_csv_opts {
    int sep;                /* ',', ';', '\t'; 0: found from the first line (import), ',' (export) */
    ob_charset charset;
    int header;             /* import: -1 found (each cell names a field), 1 the first line
                             * names fields, 0 it is data */
    int skip_bad;           /* import: add the good rows and list the bad ones in report,
                             * rather than adding none */
    /* out, import */
    int bad;                /* rows left out */
    char report[1024];      /* "Line 4: Price must be a number\n...", cut short with "..." */
    /* out, ob_csv_guess */
    int found_sep;
    ob_charset found_charset;
    int found_header;
} ob_csv_opts;

void ob_csv_opts_init(ob_csv_opts *o);
ob_int ob_csv_import_with(ob_project *p, const char *table, const char *path, ob_csv_opts *o);
ob_int ob_csv_export_with(ob_project *p, const char *table, const ob_view *v, const char *path,
                          const ob_csv_opts *o);

/* A new table's design from a CSV file: name (or the file's name), a Counter
 * "id" (a column called id holding whole numbers becomes it, so its keys are
 * kept), then a field for each column, named by the header (or Field 1,
 * Field 2...) and typed from the first 200 rows: Yes/No, Integer, Currency
 * (two decimals, or a pound or dollar sign), Number, Date, Time, Date and
 * time, else Text (Memo past 255 characters). The caller may change it, then
 * ob_table_create and ob_csv_import_with. o's found_* say what was found. */
int ob_csv_guess(ob_project *p, const char *path, const char *name, ob_csv_opts *o, ob_table *out);

/* ---- Undo (core 0.4) ----
 * Each row added, changed or deleted, and each CSV import, can be undone,
 * last first, up to 100 steps, while the tables' designs stay as they were.
 * A delete that cascaded to other rows can't be undone, and clears the list.
 * ob_rollback takes back the steps it undid itself. */
int ob_undo_count(ob_project *p);
const char *ob_undo_name(ob_project *p);        /* "Change Row in Customers"; "" none */
int ob_undo(ob_project *p);
void ob_undo_clear(ob_project *p);

/* ---- Queries (core 0.5, design 5) ----
 * A query is its tables, joined by their relationships (a Lookup between
 * each table and one before it), and its columns: a field (or "*", every
 * field of the table), or an SQL expression, each with a heading, shown or
 * not, a sort and a total; and up to OB_QUERY_OR rows of criteria, one per
 * column in each row, in plain words:
 *
 *   Leeds          = 'Leeds' (text without case)
 *   > 10, <= 11/11/2026, <> 5, not York
 *                  as written, the value in the field's own form
 *   Smith*, ?ill   LIKE 'Smith%', LIKE '_ill' (* and ? as wildcards)
 *   between 1 and 5, empty, not empty
 *   [Which town?]  a parameter: asked for when the query runs (also after
 *                  an operator: >= [From])
 *
 * Criteria in one row are joined by AND, the rows by OR. A total on any
 * column makes a grouping query: columns with no total (or Group) are
 * grouped, and criteria then go to HAVING when any is on a total.
 * A query can instead be SQL only (sql_only): one statement that reads.
 * Results are a datasheet's rows, a page at a time; values in their own
 * form where the column is a field or a total of one. */
#define OB_QUERY_TABLES 8
#define OB_QUERY_COLUMNS 16
#define OB_QUERY_OR 3
#define OB_QUERY_PARAMS 8
#define OB_QUERY_OUT 128        /* result columns, "*" spread out */
#define OB_EXPR_MAX 160
#define OB_SQL_MAX 4096

typedef enum ob_total {
    OB_TOTAL_NONE,
    OB_TOTAL_GROUP,
    OB_TOTAL_SUM,
    OB_TOTAL_COUNT,
    OB_TOTAL_AVERAGE,
    OB_TOTAL_MIN,
    OB_TOTAL_MAX,
    OB_TOTALS
} ob_total;

enum { OB_SORT_NONE, OB_SORT_UP, OB_SORT_DOWN };   /* A to Z, Z to A */

typedef struct ob_qcolumn {
    char table[OB_NAME_MAX];        /* "" for an expression */
    char field[OB_NAME_MAX];        /* a field, or "*" */
    char expr[OB_EXPR_MAX];         /* when table is "": SQL, such as Total * 2 */
    char heading[OB_NAME_MAX];      /* "" is the field's label (or the expression) */
    int show;
    int sort;                       /* OB_SORT_* */
    ob_total total;
    char criteria[OB_QUERY_OR][OB_VALUE_MAX];
} ob_qcolumn;

/* About 24 KB: keep one static or on the heap. */
typedef struct ob_query {
    char name[OB_NAME_MAX];
    int ntables;
    char tables[OB_QUERY_TABLES][OB_NAME_MAX];
    int ncolumns;
    ob_qcolumn columns[OB_QUERY_COLUMNS];
    int sql_only;
    char sql[OB_SQL_MAX];           /* sql_only: the statement */
} ob_query;

/* A result column: its heading, and the type its values are shown as
 * (-1: as SQLite gives them, for SQL and expressions). */
typedef struct ob_qresult {
    char heading[OB_NAME_MAX];
    int type;
} ob_qresult;

void ob_query_init(ob_query *q, const char *name);
/* Adds a table (once); its index, -1 when full or there already. */
int ob_query_add_table(ob_query *q, const char *table);
/* Removes table i and its columns. */
void ob_query_remove_table(ob_query *q, int i);
/* A column showing table.field ("*" for every field), or the expression
 * expr when table is NULL or ""; NULL when full. */
ob_qcolumn *ob_query_add_column(ob_query *q, const char *table, const char *field);
void ob_query_remove_column(ob_query *q, int i);
const char *ob_total_name(ob_total t);      /* "Sum"...; "" for none */

/* The SELECT the query runs, parameters as ?1, ?2...; its length, -1 with
 * the reason in ob_error (or "too long" when it does not fit). */
int ob_query_sql(ob_project *p, const ob_query *q, char *out, size_t size);
/* Whether q can run; 0 with the reason. */
int ob_query_check(ob_project *p, const ob_query *q);
/* The parameters to ask for, in order ("Which town?"; for SQL, :name's
 * name or "Value 1"): up to max; how many there are, -1 on error. */
int ob_query_params(ob_project *p, const ob_query *q, char (*names)[OB_VALUE_MAX], int max);
/* The result's columns: up to max; how many, -1 on error. */
int ob_query_columns(ob_project *p, const ob_query *q, ob_qresult *out, int max);
/* params: a value for each parameter, in the form of the field it is
 * compared with (NULL when there are none). The rows the query gives, -1
 * on error ("Which town?: must be a number"). */
ob_int ob_query_total(ob_project *p, const ob_query *q, const char *const *params);
/* Rows from position first (0 the first), up to max (-1 all): cb gets the
 * row's position as its key and the values (NULL when empty). */
int ob_query_rows(ob_project *p, const ob_query *q, const char *const *params, ob_int first, int max,
                  ob_row_fn cb, void *user);

/* Saved queries (oadb_queries), in name order. A query's name is a table's
 * kind of name, and no table has it. Saving a name that is there replaces it. */
int ob_query_count(ob_project *p);
const char *ob_query_name(ob_project *p, int i);
int ob_query_get(ob_project *p, const char *name, ob_query *out);
int ob_query_save(ob_project *p, const ob_query *q);
int ob_query_rename(ob_project *p, const char *old_name, const char *new_name);
int ob_query_delete(ob_project *p, const char *name);

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
