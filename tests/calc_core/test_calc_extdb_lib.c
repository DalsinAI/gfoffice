/*
 * GFOpenCalc's database sources built for sqlite.library (OB_SQLITE_LIBRARY, build.d/calc-extdb.sh), on the
 * PC: the link library's two calls are answered here (tests/base/sqlitelib/sqlite3_link.h) and SQLite itself
 * is the host's copy. Checks that the library is opened at the first database read and not before, once
 * while it stays open, that a missing library is said in err with no file opened, that a later read finds a
 * library put in LIBS: meanwhile, and that oc_extdb_close() lets it go (and a read after opens it again).
 * Run from the repository's top (make calc-extdb-test, make calc-extdb-check).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include <stdio.h>
#include <string.h>

#include "opencalc_core.h"
#include "opencalc_extdb.h"
#include "ob_sqlitelib.h"
#include "sqlite3.h"

static int failures, checks;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        checks++;                                                                    \
        if (!(cond)) {                                                               \
            failures++;                                                              \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                            \
    } while (0)

#define PLAIN "build/host/calc-extdb-lib.db"

/* the link library: whether sqlite.library is in LIBS:, and how often it was opened and closed */
static int there, opens, closes, open_now;
struct Library;
extern struct Library *SQLiteBase;           /* the program's own (app/ob_sqlitelib.c): without it no link */
int sqlite3_link_open(void)
{
    opens++;
    if (there) open_now = 1;
    return there;
}
void sqlite3_link_close(void)
{
    closes++;
    open_now = 0;
}

int main(void)
{
    sqlite3 *db = NULL;
    oc_exttable t;
    oc_extdb_item items[4];
    char err[256];
    int n;

    CHECK(ob_sqlite_is_library() == 1);
    CHECK(SQLiteBase == 0);
    /* the test's own database, written with the host's SQLite */
    remove(PLAIN);
    CHECK(sqlite3_open(PLAIN, &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, "CREATE TABLE t(a INTEGER, b TEXT); INSERT INTO t VALUES(1, 'one'); "
                           "INSERT INTO t VALUES(2, 'two');", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    CHECK(opens == 0);                       /* nothing opens the library before a read (GFOpenCalc starting) */

    /* no sqlite.library: both calls say so, and nothing stays open */
    there = 0;
    err[0] = 0;
    CHECK(oc_extdb_query(NULL, PLAIN, "t", 1, &t, err, sizeof err) == OWF_ERR_IO);
    CHECK(strstr(err, "needs sqlite.library") != NULL);
    CHECK(t.rows == 0 && t.cols == 0 && t.cells == NULL);
    err[0] = 0;
    CHECK(oc_extdb_items(PLAIN, items, 4, err, sizeof err) == -1);
    CHECK(strstr(err, "needs sqlite.library") != NULL);
    CHECK(opens == 2 && closes == 0 && !open_now);
    oc_extdb_close();                        /* never opened: closes nothing */
    CHECK(closes == 0);

    /* the library put in LIBS: meanwhile: the next read opens it, once for as many reads as follow */
    there = 1;
    err[0] = 0;
    CHECK(oc_extdb_query(NULL, PLAIN, "t", 1, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 3 && t.cols == 2);
    oc_exttable_free(&t);
    n = oc_extdb_items(PLAIN, items, 4, err, sizeof err);
    CHECK(n == 1 && !strcmp(items[0].name, "t"));
    CHECK(oc_extdb_query(NULL, PLAIN, "SELECT b FROM t WHERE a = 2", 0, &t, err, sizeof err) == OWF_OK);
    CHECK(t.rows == 1 && t.cols == 1);
    oc_exttable_free(&t);
    CHECK(opens == 3 && open_now);

    /* the way out lets it go; twice is safe; a read after opens it again */
    oc_extdb_close();
    CHECK(closes == 1 && !open_now);
    oc_extdb_close();
    CHECK(closes == 1);
    CHECK(oc_extdb_query(NULL, PLAIN, "t", 0, &t, err, sizeof err) == OWF_OK);
    oc_exttable_free(&t);
    CHECK(opens == 4 && open_now);
    oc_extdb_close();
    CHECK(closes == 2);

    remove(PLAIN);
    printf("%s: %d checks, %d failed\n", failures ? "CALC EXTDB LIBRARY FAIL" : "CALC EXTDB LIBRARY PASS", checks,
           failures);
    return failures ? 1 : 0;
}
