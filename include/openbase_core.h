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

#define OB_CORE_VERSION "0.20.0"

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
/* Sets it (core 0.14, sheets #326), for GFOpenBase's parts outside the core (sheets): 0
 * with text as the reason ("" or NULL clears it). */
int ob_set_error(ob_project *p, const char *text);
/* A bound on the work each statement may do from now on (core 0.14, GFOpenCalc's External Data): SQLite's
 * progress handler is called every OB_STEP_OPS of its steps, and past limit calls the statement stops with an
 * error (a query that would run without end: WITH RECURSIVE without LIMIT). limit 0 takes the bound away; the
 * count starts again with each call. ob_step_limit_hit: 1 when the last error was the bound's. */
#define OB_STEP_OPS 10000
int ob_set_step_limit(ob_project *p, long limit);
int ob_step_limit_hit(const ob_project *p);

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

/* Drawn in the relationships view (core 0.11.0), each one table change
 * (ob_table_change: rows kept, refused with the reason if a row would look
 * up a row that isn't there):
 * ob_relation_add: table.field, an Integer or a Lookup, now looks up
 * lookup's key (a Counter), rule on delete. field NULL or "": a new Lookup
 * field, named lookup (or "lookup 2" when that is taken), its name in made
 * (when not NULL). A table's Counter or key can't look up.
 * ob_relation_set_rule: what deleting a looked-up row does to its rows.
 * ob_relation_remove: no longer a relationship; the field is an Integer
 * holding the same keys. */
int ob_relation_add(ob_project *p, const char *table, const char *field, const char *lookup,
                    ob_on_delete rule, char *made, size_t size);
int ob_relation_set_rule(ob_project *p, const char *table, const char *field, ob_on_delete rule);
int ob_relation_remove(ob_project *p, const char *table, const char *field);

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

/* ---- ARexx row commands (core 0.10.0, src/base/ob_rexx.c) ----
 * One of these, as GFOpenBase's port gets it, on table:
 *   ADDROW Field=Value ...        RESULT the new row's key
 *   CHANGEROW [KEY k] Field=Value ...   OK
 *   DELETEROW [KEY k]             OK, with no requester
 *   GETROW [KEY k] [FIELD f]      the field's value, or every value in
 *                                 field order with a tab between
 *   GETFIELDS                     the field names, each in quotes
 * A pair is Field=Value, "Field=Value" or Field="Value" (a value with
 * spaces in quotes); field names in any case; a field named Key or Field
 * goes in quotes. Values are the fields' own text, as ob_row_add takes
 * them. KEY left out is current (0: none). The answer, or the reason,
 * goes in out; the return is ARexx's code (0 OK, 5 no such row or none
 * chosen, 10 a bad command or value, 20 out of memory), or -1 when
 * command isn't one of these. *row (if not NULL) gets the row it was
 * about, 0 when none. */
int ob_rexx_rows(ob_project *p, const char *table, ob_int current, const char *command,
                 char *out, size_t size, ob_int *row);

/* ---- ARexx query commands (core 0.18.0, src/base/ob_rexx.c) ----
 * One of these on the project's saved queries:
 *   QUERIES                       the saved queries' names, each in quotes
 *   QUERYFIELDS name              a reading query's headings, each in quotes
 *   COUNTQUERY name [ANSWER a]... the rows a Select or Union gives, or the
 *                                 rows an Update, Append or Delete would
 *                                 change (counted, nothing kept)
 *   QUERYROW name ROW n [FIELD h] [ANSWER a]...
 *                                 row n (from 1) of a Select or Union: the
 *                                 column's value, or every value with a tab
 *                                 between
 *   RUNQUERY name [ANSWER a]...   runs an Update, Append or Delete, all or
 *                                 none, with no requester: RESULT the rows
 *                                 changed. It is one step on the Undo list
 *                                 when ob_query_run can make it one; when
 *                                 it can't (more than OB_UNDO_ROWS rows,
 *                                 triggers on the table, a delete that
 *                                 cascades or clears) the Undo list is
 *                                 emptied and RESULT is the count and
 *                                 NOUNDO, as "25000 NOUNDO"
 * A name with spaces goes in quotes. Each [question] the query asks gets an
 * ANSWER, in the order ob_query_params gives; too few or too many is
 * refused with the first question named. The answer, or the reason, goes in
 * out; the return is ARexx's code (0 OK, 5 no such row or no project, 10 a
 * bad command, name or value, 20 out of memory), or -1 when command isn't
 * one of these. *changed (if not NULL) is 1 when rows were changed. */
int ob_rexx_queries(ob_project *p, const char *command, char *out, size_t size, int *changed);

/* ---- SQL typed from ARexx (core 0.19.0, src/base/ob_rexx.c) ----
 *   QUERY [ROW n] [COUNT] [ANSWER a]... sql
 *       one statement that reads (SELECT, WITH, VALUES): row n's values
 *       (row 1 without ROW) with a tab between, so the first value alone
 *       when it shows one column; COUNT gives the number of rows instead.
 *       5 with "No row n" past the end.
 *   EXEC [ANSWER a]... sql
 *       one UPDATE, INSERT or DELETE on one of the project's tables, as a
 *       typed query that changes rows (5.6): all or none, RESULT the rows
 *       changed, and NOUNDO after the count when it could not be made a
 *       step of Undo, as RUNQUERY answers.
 * The words before the SQL are taken first; the rest of the line is the
 * SQL, and one pair of quotes round all of it comes off. Values are bound,
 * never pasted: each ?, ?1 or :name in the SQL takes one ANSWER, in order,
 * so a quote in a value can't break the statement. Returns as
 * ob_rexx_queries does; -1 when command is neither. */
int ob_rexx_sql(ob_project *p, const char *command, char *out, size_t size, int *changed);

/* FINDROW Field=Value [BACK] [FIRST] (core 0.18.0): the next row of table,
 * in the sort and filter of view (NULL: key order), whose field holds the
 * value as ob_view_find finds it (text contains, any case; other fields
 * equal). It looks after the row at position current (BACK: before it);
 * FIRST, or current < 0, from the first row (BACK: the last). out gets the
 * row's number from 1 and *pos its position from 0; 5 with "No later row
 * has ..." when none, 10 for a bad command, field or value, -1 when command
 * isn't FINDROW. */
struct ob_view;
int ob_rexx_find(ob_project *p, const char *table, const struct ob_view *view, ob_int current, const char *command,
                 char *out, size_t size, ob_int *pos);

/* 1 when command must be refused while a form window is open (core 0.14.3):
 * OPEN, QUIT, TABLE t, ROW n, FIRSTROW NEXTROW PREVROW LASTROW, ADDROW,
 * CHANGEROW and DELETEROW; RUNQUERY and FINDROW too (core 0.18.0), and
 * EXEC (core 0.19.0). TABLE and ROW on their own, COUNTROWS, GETROW,
 * GETFIELDS and the rest only read, so they still answer. */
int ob_rexx_form_refuses(const char *command);

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

/* Filter by Form (core 0.6.2, design 6.3): what is typed into a form's
 * fields, in the plain words of a query's criteria (Leeds, > 10, Smith*,
 * between 1 and 5, empty, not York; no [questions]). The criteria in one
 * row ("Look for", then "Or" rows) must all hold; a row passes when any
 * row does. They are on top of the one-field filter above. */
#define OB_FORM_CRIT 16             /* criteria in all the rows together */
#define OB_FORM_OR 3                /* rows: Look for, Or, Or */

typedef struct ob_form_crit {
    char field[OB_NAME_MAX];
    int row;                        /* 0 Look for, 1 and 2 Or */
    char text[OB_VALUE_MAX];
} ob_form_crit;

/* About 5 KB: fine on a 64 KB stack, but not many at once. */
#define OB_VIEW_THEN 2           /* fields a view sorts by after its sort */

typedef struct ob_view {
    char sort[OB_NAME_MAX];
    int desc;
    char field[OB_NAME_MAX];
    ob_filter_op op;
    char value[OB_VALUE_MAX];
    int ncrit;
    ob_form_crit crit[OB_FORM_CRIT];
    char then[OB_VIEW_THEN][OB_NAME_MAX];   /* 0.14.4: then by these, in order ("" ends them), the same way
                                               up or down as sort; only with a sort */
} ob_view;

void ob_view_init(ob_view *v);                  /* key order, every row */
/* Filter by Form: sets field's criterion in row (0 Look for, 1 and 2 Or);
 * empty text takes it out. 0 when row is not one of them, or there is no
 * room for another (OB_FORM_CRIT). The text is checked when the view is. */
int ob_view_form_set(ob_view *v, const char *field, int row, const char *text);
/* field's criterion in row; "" when it has none. */
const char *ob_view_form_get(const ob_view *v, const char *field, int row);
void ob_view_form_clear(ob_view *v);
/* Whether v leaves rows out (its filter, or Filter by Form). */
int ob_view_filtered(const ob_view *v);
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
/* Changing a subform's rows (core 0.6.3). The key of the row at position
 * `pos` (from 0, key order) among r's rows for record `parent`: -1 when
 * there is none, -2 on error. */
ob_int ob_child_key(ob_project *p, const ob_relation *r, ob_int parent, ob_int pos);
/* The view of r's rows for record `parent` (core 0.10.1): r->field Equals
 * parent, key order. A subform row opened in its own table's form is
 * shown in this view, so the form steps through that record's rows. */
void ob_child_view(ob_view *v, const ob_relation *r, ob_int parent);
/* Adds a row to r's table for record `parent`: its Lookup r->field holds
 * parent whatever the fields give for it; the rest as ob_row_add takes them.
 * 0 with the reason when parent is not a key (a new record not yet saved:
 * "Save the record first") or ob_row_add refuses the row. */
int ob_child_add(ob_project *p, const ob_relation *r, ob_int parent, int n, const char *const *fields,
                 const char *const *values, ob_int *key);

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
    /* out, ob_dbf_import */
    ob_int deleted;         /* records the file marks deleted, left out */
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

/* ---- Rows from a grid (core 0.14, sheets #326) ----
 * What CSV import and New Table from CSV do, for rows another part of the
 * program has already read: a spreadsheet's sheet (include/openbase_sheet.h)
 * or anything else laid out in rows and columns. The rows are asked for one
 * at a time, so nothing holds them all. row() puts up to max cells of row r
 * (from 0) into cells (ISO-8859-1 text; NULL or "" is empty) and returns how
 * many cells the row has (more than max when it has more than a table can
 * take), -1 when memory ran out; the cells need last only until the next
 * call. A row with nothing in it is left out, as a blank line is.
 *
 * Import and guess are ob_csv_import_with's and ob_csv_guess's, header,
 * skip_bad and the report included (o's sep and charset are not used; o may
 * be NULL), with "Row 4" for "Line 4": a row's number is first_row + r, so a
 * sheet whose data start in its third row says "Row 3". An import is one
 * step of Undo, as a CSV import is. name says what the rows are in messages
 * ("Sheet1 is empty"); guess names the new table name, else name. */
typedef int (*ob_grid_row_fn)(void *user, long row, const char **cells, int max);

typedef struct ob_grid {
    const char *name;
    long nrows;
    long first_row;
    ob_grid_row_fn row;
    void *user;
} ob_grid;

ob_int ob_grid_import(ob_project *p, const char *table, const ob_grid *grid, ob_csv_opts *o);
int ob_grid_guess(ob_project *p, const ob_grid *grid, const char *name, ob_csv_opts *o, ob_table *out);

/* ---- dBASE files (core 0.9, design 8) ----
 * The .dbf of dBASE III, which most database and spreadsheet programs read
 * and write; written from the published description of the format. Text is
 * kept as ISO-8859-1, as the Amiga keeps it.
 *
 * Export: every row of the view (NULL: key order) of table into path, a
 * column a field, named as dBASE names them (upper case, letters, digits
 * and _, 10 at most: "Credit limit" is CREDIT_LIM, a second one CREDIT_L_2).
 * Counter, Integer and Lookup are N with no decimals; Number is N with as
 * many decimals as its values need (9 at most); Currency N with 2; Date D;
 * Yes/No L; Text and Choice C as wide as their longest value (254 at most:
 * a longer one makes the column a memo); Memo M, its text in a .dbt beside
 * the .dbf (path with .dbt for .dbf), in 512-byte blocks; Time and Date and
 * time C of 8 and 19. Datatype objects are left out. Rows written, -1 on
 * error.
 *
 * Import: adds the file's records to table, all or none (o->skip_bad: the
 * good ones, the bad listed as "Record 4: ..." in o->report). Columns go to
 * the fields they name (by name, by the name dBASE would give the field, or
 * by label, any case) when each does; otherwise to the table's fields in
 * order, as CSV's. Records marked deleted are left out and counted in
 * o->deleted. dBASE III and IV files are read, their memos too (a .dbt
 * beside the file); FoxPro's are refused with that reason. Records added,
 * -1 with the reason. o may be NULL.
 *
 * Guess: a new table's design from a .dbf's columns: a numeric ID column
 * with no decimals becomes the Counter (so its keys are kept), else one is
 * added; C is Text of that width, N with no decimals Integer (Number past
 * 18 digits), N with 2 Currency, other N and F Number, D Date, L Yes/No, M
 * Memo. Names are as dBASE wrote them, first letter kept upper (NAME is
 * Name). Then ob_table_create and ob_dbf_import. */
ob_int ob_dbf_export(ob_project *p, const char *table, const ob_view *v, const char *path);
ob_int ob_dbf_import(ob_project *p, const char *table, const char *path, ob_csv_opts *o);
int ob_dbf_guess(ob_project *p, const char *path, const char *name, ob_table *out);

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
#define OB_QUERY_JOINS 12

/* A join drawn between two of the query's tables (core 0.8): left.lfield =
 * right.rfield. Inner: the rows that match on both sides; left: every row of
 * the left table, with the right table's where one matches; right: the
 * other way round. */
typedef enum ob_join {
    OB_JOIN_INNER,
    OB_JOIN_LEFT,
    OB_JOIN_RIGHT,
    OB_JOINS
} ob_join;

typedef struct ob_qjoin {
    char left[OB_NAME_MAX];
    char lfield[OB_NAME_MAX];
    char right[OB_NAME_MAX];
    char rfield[OB_NAME_MAX];
    ob_join kind;
} ob_qjoin;

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
    char to[OB_EXPR_MAX];           /* core 0.15: Update To (a value) or Append To (a field); "" none */
    int xtab;                       /* core 0.20, OB_QK_CROSSTAB: OB_XT_* */
} ob_qcolumn;

/* A column's part in a Crosstab query (core 0.20). */
enum { OB_XT_NONE, OB_XT_ROW, OB_XT_COLUMN, OB_XT_VALUE, OB_XTS };

/* What a query does (core 0.15): reads rows, or changes them. */
typedef enum ob_qkind {
    OB_QK_SELECT,                   /* reads: its rows are shown */
    OB_QK_UPDATE,                   /* changes fields of the target's rows */
    OB_QK_APPEND,                   /* adds the rows it reads to the target */
    OB_QK_DELETE,                   /* deletes the target's rows */
    OB_QK_UNION,                    /* core 0.16, reads: the rows of several tables and queries, one after another */
    OB_QK_MAKE,                     /* core 0.20: makes a new table of the rows it reads */
    OB_QK_CROSSTAB,                 /* core 0.20, reads: a value totalled for each row heading and column heading */
    OB_QKINDS
} ob_qkind;

/* About 27 KB: keep one static or on the heap. */
typedef struct ob_query {
    char name[OB_NAME_MAX];
    int ntables;
    char tables[OB_QUERY_TABLES][OB_NAME_MAX];
    int ncolumns;
    ob_qcolumn columns[OB_QUERY_COLUMNS];
    int sql_only;
    char sql[OB_SQL_MAX];           /* sql_only: the statement */
    int njoins;                     /* 0: tables joined by their relationships */
    ob_qjoin joins[OB_QUERY_JOINS];
    ob_qkind kind;                  /* core 0.15; OB_QK_SELECT (0) by ob_query_init */
    char target[OB_NAME_MAX];       /* the table an action query changes; "" the first table; a Make Table's new table */
    int union_all;                  /* core 0.16, OB_QK_UNION: 1 keeps rows that are the same (UNION ALL) */
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
/* Removes table i, its columns and its joins. */
void ob_query_remove_table(ob_query *q, int i);
/* Draws a join, left.lfield = right.rfield; its index, -1 when full, when
 * the two are one table, or when those two fields are joined already.
 * With no join drawn a query joins its tables by their relationships, as
 * before; once one is drawn only the drawn ones are used, and each table
 * must be joined to the others. Several joins between two tables are all
 * kept (ON a AND b), the first one's kind used. */
int ob_query_add_join(ob_query *q, const char *left, const char *lfield, const char *right, const char *rfield,
                      ob_join kind);
void ob_query_remove_join(ob_query *q, int i);
const char *ob_join_name(ob_join k);        /* "Inner", "Left", "Right" */
/* Draws an inner join for each relationship between two of the query's
 * tables that aren't joined yet (the Lookup to the table's key), as a
 * designer does when a table is added; how many were drawn, -1 on error. */
int ob_query_relation_joins(ob_project *p, ob_query *q);
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
 * kind of name, and no table has it. Saving a name that is there replaces it.
 * A query with joins drawn is kept as "oadb query 2" (an older GFOpenBase
 * opens it as its SQL, which has the joins). */
int ob_query_count(ob_project *p);
const char *ob_query_name(ob_project *p, int i);
int ob_query_get(ob_project *p, const char *name, ob_query *out);
int ob_query_save(ob_project *p, const ob_query *q);
int ob_query_rename(ob_project *p, const char *old_name, const char *new_name);
int ob_query_delete(ob_project *p, const char *name);

/* ---- Queries that change rows (core 0.15, design 5.6) ----
 * An Update, Append or Delete query is made in the grid as a query that
 * reads, and changes the rows that query would show:
 *
 *   Update  the target (one of its tables; "" the first) gets each column of
 *           its own with something in "to" set to it, in every row the
 *           criteria pick: a value in the field's own form (Leeds, 12.50,
 *           1/12/2026), empty, a [question], or =SQL (= "Products"."Price"
 *           * 1.1). Its Counter can't be changed; no totals.
 *   Append  the rows it reads (totals allowed) are added to the target (any
 *           table): each column with a field's name in "to" goes to that
 *           field; columns with "to" empty only pick the rows.
 *   Delete  the target's rows the criteria pick are deleted, as from its
 *           datasheet: a Lookup elsewhere refuses, cascades or clears.
 *
 * The SQL of one is also accepted typed (sql_only, kind set): one UPDATE,
 * INSERT or DELETE statement on one of the project's tables, without
 * RETURNING; it may read any table. Anything else is refused.
 *
 * ob_query_sql gives an action's statement; ob_query_rows, ob_query_total,
 * CSV and views refuse it. ob_query_preview counts the rows it would add,
 * change or delete and keeps nothing (a rule it would break is the error);
 * ob_query_run does it, all or none, and puts it on the Undo list as one
 * step ("Update Query Raise prices: 3 rows in Products"), unless more than
 * OB_UNDO_ROWS rows change, the target has triggers, or a delete would reach
 * rows of other tables (a cascade or clear): then it runs, the Undo list is
 * emptied and *undoable is 0. Both take the [questions]' answers as
 * ob_query_rows does. A saved action query is kept as "oadb query 3" (an
 * older GFOpenBase refuses to run it, as SQL that changes rows). */
#define OB_UNDO_ROWS 20000
const char *ob_qkind_name(ob_qkind k);      /* "Select", "Update", "Append", "Delete", "Union", "Make Table", "Crosstab" */
ob_int ob_query_preview(ob_project *p, const ob_query *q, const char *const *params);
ob_int ob_query_run(ob_project *p, const ob_query *q, const char *const *params, int *undoable);

/* ---- The query window's words (core 0.17) ----
 * ob_query_set_kind changes what q does and clears what the new kind has no
 * use for: every column's "to" (an Update To is a value, an Append To a
 * field), the target when the new kind reads, union_all unless it is a
 * Union, and a Union's drawn joins (its tables are its parts). The same
 * kind again changes nothing.
 * ob_query_ask counts as ob_query_preview does, keeps nothing, and puts in
 * out the sentence a window asks before Run: "This will change 3 rows in
 * Products.", "This will add 1 row to Archive.", "This will delete 2 rows
 * from Orders.", with " It can't be undone." after it when ob_query_run
 * would empty the Undo list, or "Raise prices changes no rows." for none.
 * The count; -1 with the reason in ob_error and out empty. */
void ob_query_set_kind(ob_query *q, ob_qkind kind);
ob_int ob_query_ask(ob_project *p, const ob_query *q, const char *const *params, char *out, size_t size);

/* ---- Union queries (core 0.16, design 5.7) ----
 * A Union query (kind OB_QK_UNION) reads the rows of two or more parts, one
 * part after another, as one result. Its parts are its tables (q->tables, in
 * order, up to OB_QUERY_TABLES): each the name of a table (all its fields,
 * as Table.* shows them) or of a saved Select query. The same part may be
 * there twice (add it by hand: ob_query_add_table adds a name once).
 *
 *   - Every part shows the same number of columns. The headings are the
 *     first part's. Column by column the parts' types agree: the same type,
 *     or both text (Text, Memo...), or both whole numbers, or both numbers.
 *     A column that is an expression (or typed SQL's) in one part is shown
 *     as the other parts' field is, its values taken as that field stores
 *     them (Currency in pence); one that is an expression in every part is
 *     shown as SQLite gives it. Anything else is refused in plain words
 *     ("column 2 is Currency in A and Text in B").
 *   - union_all 0 leaves out rows that are the same as one before (UNION);
 *     1 keeps every row (UNION ALL).
 *   - Its columns (q->columns) only sort: each names a result heading (as a
 *     column with no table: ob_query_add_column(q, NULL, "Name")) with sort
 *     up or down; text sorts without case. No columns: no order is promised.
 *   - A part can't ask a [question], change rows, or be a Union itself (add
 *     that union's parts instead), so parts never go round in a circle.
 *
 * A Union reads: ob_query_rows, ob_query_total, ob_query_columns, CSV and
 * views take it as they take a Select query; ob_query_preview and
 * ob_query_run refuse it. Saved, its grid is "oadb query 4" (an older
 * GFOpenBase opens it as its SQL, a plain read). Renaming a table or a
 * saved query renames it in the unions that read it. SQL typed as SQL may
 * say UNION itself, as a Select query; sql_only with this kind is refused. */

/* ---- Make Table queries (core 0.20, design 5.9) ----
 * A Make Table query (kind OB_QK_MAKE) reads rows as a Select query does
 * (made in the grid, or typed as one SELECT) and makes a new table of them,
 * named in q->target. The new table has a Counter, "id" (or "Row", "Row id"
 * when a column is called that), then a field for each column shown, named
 * by its heading, of the type the column is shown as: a Counter or a Lookup
 * becomes an Integer, a Choice a Text; an expression's type is taken from
 * its values (whole numbers an Integer, numbers a Number, else a Text). A
 * column of Datatype objects is refused, as Append refuses it.
 *
 *   - The name must be free: no table, saved query or view has it. Running
 *     the query again once the table is made is refused ("Archive is a
 *     table already: delete it, or make the query's table another name").
 *   - ob_query_preview makes the table and its rows and takes them back,
 *     counting the rows; ob_query_run makes them, all or none. A new table
 *     changes the project's design, so it is not on the Undo list: the list
 *     is emptied and *undoable is 0. ob_query_ask says "This will make the
 *     table Archive with 3 rows. It can't be undone." (or "with no rows":
 *     an empty table is made too).
 *   - Rows, totals, CSV and views refuse it, as an action query. Saved, its
 *     grid is "oadb query 5" (an older GFOpenBase opens it as its SQL, the
 *     plain Select). Renaming a table renames it in the query's grid.
 *
 * ---- Crosstab queries (core 0.20, design 5.10) ----
 * A Crosstab query (kind OB_QK_CROSSTAB) is made in the grid. Each column
 * has a part (xtab):
 *
 *   OB_XT_ROW     a row heading (one to OB_XT_ROWS): a row for each value,
 *                 sorted A to Z (Z to A when the column sorts down);
 *   OB_XT_COLUMN  the column heading (exactly one): a column for each value
 *                 it has in the rows the criteria pick, in order (text
 *                 without case), headed by the value as the datasheet
 *                 shows it, "(empty)" for none;
 *   OB_XT_VALUE   the value (exactly one), with a total (Sum, Count,
 *                 Average, Min or Max) worked out in each cell; empty
 *                 where no row has that pair;
 *   OB_XT_NONE    only picks rows, by its criteria.
 *
 * Every criterion picks rows before they are totalled. A Crosstab can't
 * ask a [question] (its columns are found when it is made ready), has no
 * drawn total besides the value's, and shows at most OB_QUERY_OUT columns.
 * It reads: ob_query_rows, ob_query_total, ob_query_columns and CSV take it
 * as a Select query; a view refuses it (its columns come from the rows), so
 * does a Union. Saved, its grid is "oadb query 5" with each column's part. */
#define OB_XT_ROWS 4

/* ---- A query's rows as CSV, a query as a view (core 0.13.1) ----
 * CSV: a header of the result's headings, then every row the query gives,
 * as ob_csv_export_with writes a table's (o: separator and charset; NULL is
 * a comma and ISO-8859-1). Datatype object columns are left out. The answers
 * (params) are checked before the file is made. Rows written, -1 on error.
 *
 * A view: the query's SELECT kept in the project as an SQL VIEW, so other
 * SQLite programs (and typed SQL) can read it. Its name is a table's kind of
 * name and no table or saved query has it; saving a name that is there
 * replaces that view. A query that asks [questions] can't be a view.
 * Renaming a table rewrites the views that name it (SQLite does), also when
 * a design change renames it; a design change or a delete that would leave a
 * view unable to read is refused.
 * Saving or deleting a view is not on the Undo list.
 *
 * Renaming a table (ob_table_rename, or a design change that renames it) also
 * renames it in saved queries made in the grid: their tables, their columns'
 * tables, and "Orders". in an expression (as the designer writes a field).
 * SQL typed as SQL is kept as it was typed. */
ob_int ob_query_csv_export(ob_project *p, const ob_query *q, const char *const *params, const char *path,
                           const ob_csv_opts *o);
int ob_query_save_view(ob_project *p, const ob_query *q, const char *name);
/* The project's views, in name order; a name is good until the next call. */
int ob_sqlview_count(ob_project *p);
const char *ob_sqlview_name(ob_project *p, int i);
int ob_sqlview_drop(ob_project *p, const char *name);

/* ---- Designed forms (core 0.7, design 6.2) ----
 * A form places items on a grid counted in the font's character width and
 * line height, not pixels, so it survives a change of font: x 0 to
 * OB_FORM_COLS - 1 across, y 0 to OB_FORM_ROWS - 1 down. Kept as text in
 * oadb_forms. A form's name is its own: it may be its table's name. */
#define OB_FORM_ITEMS 200       /* items on one form (an auto form of 96 fields: 192) */
#define OB_FORM_COLS 160
#define OB_FORM_ROWS 100

typedef enum ob_fitem_kind {
    OB_FI_FIELD,    /* a field's gadget; field names it */
    OB_FI_LABEL,    /* text */
    OB_FI_LINE,     /* across (h 1) or down (w 1) */
    OB_FI_BOX,      /* a frame round other items; text is its title, may be "" */
    OB_FI_BUTTON,   /* text on it; action and arg say what it does (core 0.7.3) */
    OB_FI_KINDS
} ob_fitem_kind;

/* What a button does (core 0.7.3). The record ones act on the form's
 * record as its record bar does; Open Form opens the form arg names (at the
 * rows of its table), Run Query runs the saved query arg names, ARexx runs
 * arg as an ARexx macro (a file, or a line of ARexx in quotes). */
#define OB_ARG_MAX 128
typedef enum ob_faction {
    OB_FA_NONE,
    OB_FA_FIRST, OB_FA_PREVIOUS, OB_FA_NEXT, OB_FA_LAST,
    OB_FA_NEW, OB_FA_SAVE, OB_FA_DELETE, OB_FA_FIND, OB_FA_CLOSE,
    OB_FA_OPENFORM, OB_FA_RUNQUERY, OB_FA_AREXX,
    OB_FA_COUNT
} ob_faction;

/* A form's events: an ARexx macro run when the form opens, when it moves
 * to a record, before and after a record is saved, and when it closes. ""
 * is none. Before Save's macro can refuse the save by giving a return code
 * of 5 or more. */
typedef enum ob_fevent {
    OB_FE_OPEN, OB_FE_CURRENT, OB_FE_BEFORE_SAVE, OB_FE_AFTER_SAVE, OB_FE_CLOSE,
    OB_FE_COUNT
} ob_fevent;

typedef struct ob_fitem {
    ob_fitem_kind kind;
    char field[OB_NAME_MAX];
    char text[OB_LABEL_MAX];
    int x, y, w, h;
    ob_faction action;              /* a button's */
    char arg[OB_ARG_MAX];           /* its form, query or macro */
} ob_fitem;

typedef struct ob_form {
    char name[OB_NAME_MAX];
    char table[OB_NAME_MAX];
    int nitems;
    ob_fitem items[OB_FORM_ITEMS];
    char events[OB_FE_COUNT][OB_ARG_MAX];
} ob_form;

/* "First Record"...; "" for none or out of range. */
const char *ob_faction_name(ob_faction a);
/* What a button's arg names: 0 nothing, 1 a form, 2 a query, 3 a macro. */
int ob_faction_arg(ob_faction a);
const char *ob_fevent_name(ob_fevent e);     /* "On Open"... */
/* Adds a button with text doing action (arg its form, query or macro);
 * NULL as ob_form_add. */
ob_fitem *ob_form_add_button(ob_form *f, const char *text, ob_faction action, const char *arg, int x, int y);

void ob_form_init(ob_form *f, const char *name, const char *table);
/* Adds an item, kept inside the grid (w and h at least 1); NULL when the
 * form is full or x, y is off the grid. */
ob_fitem *ob_form_add(ob_form *f, ob_fitem_kind kind, int x, int y, int w, int h);
/* Removes item i; the ones after it move up. 0 when there is none. */
int ob_form_remove(ob_form *f, int i);
/* The item at cell x, y: the last added that covers it (a box only by its
 * edge, so what is inside it can be chosen), -1 when none. */
int ob_form_at(const ob_form *f, int x, int y);
/* Moves item i to x, y and sizes it w, h, all kept inside the grid. 0 when
 * there is no item i. */
int ob_form_place(ob_form *f, int i, int x, int y, int w, int h);
/* The auto form of a table as a designed one: a label and its field a row,
 * labels right-aligned to the longest (at most 24), fields 40 wide (a Memo
 * 4 lines). */
int ob_form_auto(ob_project *p, const char *table, const char *name, ob_form *out);
/* Whether the form can be shown: its table is there, each field item names
 * one of its fields, a line is 1 thick, a button does something and has the
 * form, query or macro it needs named (one that is gone since shows as a
 * button that says so when pressed). 0 with the reason. */
int ob_form_check(ob_project *p, const ob_form *f);
/* The cells the items use: cols across (the furthest x + w), rows down. */
void ob_form_extent(const ob_form *f, int *cols, int *rows);
/* The first row under every item, for a new one; -1 when that is off the
 * grid. */
int ob_form_free_row(const ob_form *f);
/* Moves item i by dx, dy and sizes it by dw, dh (cursor keys in the
 * designer): it keeps its size while it moves, stops at the grid's edge,
 * and a line stays one cell thick. 0 when there is no item i. */
int ob_form_nudge(ob_form *f, int i, int dx, int dy, int dw, int dh);
/* The fields of the form's table it doesn't show yet, in the table's
 * order, up to max; how many there are, -1 on error. */
int ob_form_unplaced(ob_project *p, const ob_form *f, char (*names)[OB_NAME_MAX], int max);
/* Puts field on the form at x, y as the auto form would: its label, then
 * its gadget 40 wide (a Memo 4 lines). The field item's index; -1 with the
 * reason when the table has no such field, it is on the form already, or
 * there is no room. */
int ob_form_add_field(ob_project *p, ob_form *f, const char *field, int x, int y);
/* The names of the forms on table, in name order, up to max; how many
 * there are, -1 on error. */
int ob_forms_of(ob_project *p, const char *table, char (*names)[OB_NAME_MAX], int max);
/* Forms kept in the project (oadb_forms), in name order. Saving a name that
 * is there replaces it. A table's rename, on its own or in a design change,
 * renames it in its forms, and a design change's field renames (from[])
 * rename their items. A table or field a form shows can't be dropped
 * ("The form Card shows Email: take it off the form first"). Renaming a
 * form or a saved query renames it in the buttons that open or run it. A
 * button's caption, action and macro, and a form's events, are kept as
 * "button" and "event" lines an older GFOpenBase passes over. */
int ob_form_count(ob_project *p);
const char *ob_form_name(ob_project *p, int i);
int ob_form_get(ob_project *p, const char *name, ob_form *out);
int ob_form_save(ob_project *p, const ob_form *f);
int ob_form_rename(ob_project *p, const char *old_name, const char *new_name);
int ob_form_delete(ob_project *p, const char *name);

/* ---- Values ---- */

/* Whether text is a valid value for f; 0 with the reason in err ("Price must
 * be a number"). Empty text is checked against required. */
int ob_value_check(const ob_field *f, const char *text, char *err, size_t errsize);

/* Currency: "1,234.50", "-0.05", "12" in, pence out; 0 when it isn't money. */
int ob_currency_parse(const char *text, ob_int *pence);
void ob_currency_format(ob_int pence, char *out, size_t size);

const char *ob_type_name(ob_type t);          /* "Text", "Currency"... */
int ob_type_from_name(const char *name);      /* -1 when unknown */

/* ---- Reports kept in the project (oadb_reports) ----
 * Each report's text under its name; include/openbase_report.h writes and reads the text
 * (ob_report_store, ob_report_load). A report's name is a query's kind of name, and no table has it.
 * 1 when they work, 0 with the reason in ob_error. */

#define OB_REPORT_TEXT_MAX 8192        /* the most a report's text can be, with its 0 */

int ob_report_count(ob_project *p);
/* In name order; NULL past the end. The text lasts until the next call. */
const char *ob_report_name(ob_project *p, int i);
/* The text kept as name (any case) into out; 0 when there is none or it doesn't fit in size. */
int ob_report_get_text(ob_project *p, const char *name, char *out, size_t size);
/* Keeps text as name, replacing what is kept there (the name keeps the case it was first given). */
int ob_report_put_text(ob_project *p, const char *name, const char *text);
int ob_report_rename(ob_project *p, const char *old_name, const char *new_name);
int ob_report_delete(ob_project *p, const char *name);

#ifdef __cplusplus
}
#endif

#endif
