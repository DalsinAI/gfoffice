/*
 * GFOpenCalc's database sources for External Data (opencalc_core.h,
 * oc_extdata_io.query): a GFOpenBase project (.oadb), read through
 * GFOpenBase's core so values come as GFOpenBase shows them (Currency as
 * 1,234.50, Yes/No, dates), or any other SQLite file, read as SQLite gives it.
 * An item is a table's, view's or saved query's name, or a query typed in
 * (SELECT or WITH: one statement that only reads). Files are opened read only.
 * Built apart from src/calc (it needs SQLite): src/calc/extdb, tests in
 * tests/calc_core/test_calc_extdb.c and test_calc_extdb_lib.c (mk/calc-extdb.mk).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#ifndef OPENCALC_EXTDB_H
#define OPENCALC_EXTDB_H

#include "opencalc_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OC_EXTDB_ROWS 65536U            /* rows read from one item, at most (more: an error) */
/* The work one item's query may do: SQLite's steps, in thousands of thousands (more: stopped, an error), so a
 * query that would run without end (WITH RECURSIVE without LIMIT, a count over a huge cross join) cannot hang
 * GFOpenCalc as a book opens. About a minute's work on a 68040, a moment on a PC. */
#define OC_EXTDB_STEPS 50000000L

/* oc_extdata_io.query: user is not used. OWF_ERR_IO (the file cannot be opened or is not a database),
 * OWF_ERR_FORMAT (no such item, a query that does not run, one that asks for values or would change the
 * file, one that did too much work: OC_EXTDB_STEPS), OWF_ERR_UNSUPPORTED (too many rows or cells), OWF_ERR_MEMORY; the reason in err. */
int oc_extdb_query(void *user, const char *database, const char *item, int headers, oc_exttable *out, char *err,
                   size_t errsize);

/* What a database offers, for the window: its tables, views and saved queries, each kind in name order (not
 * GFOpenBase's or SQLite's own tables). How many there are (more than max: the first max given), -1 with the
 * reason in err. */
enum { OC_EXTDB_TABLE, OC_EXTDB_VIEW, OC_EXTDB_QUERY };
typedef struct {
    int kind;
    char name[128];
} oc_extdb_item;
int oc_extdb_items(const char *database, oc_extdb_item *items, int max, char *err, size_t errsize);

/* SQLite: linked in, or sqlite.library (OB_SQLITE_LIBRARY), which the two calls above open at the first
 * database read; until it opens they answer OWF_ERR_IO and -1 with "Reading a database needs sqlite.library
 * 1 or later in LIBS:" in err, and GFOpenCalc runs on without it. oc_extdb_close() lets the library go, on the
 * program's way out (safe when it never opened, or twice); a later read opens it again. */
void oc_extdb_close(void);

#ifdef __cplusplus
}
#endif

#endif
