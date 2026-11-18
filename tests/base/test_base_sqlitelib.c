/* app/ob_sqlitelib.c both ways it is built (mk/base-sqlitelib.mk):
 *   with SQLite linked in, open answers 1, close does nothing, and the link library is never called;
 *   on the library (OB_SQLITE_LIBRARY), open and close are the link library's, the program has the
 *   SQLiteBase that keeps the link library's own start-up out, and a library that is missing answers 0.
 * The link library's two calls are answered here.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include "ob_sqlitelib.h"

static int checks, failed;
#define CHECK(x) do { checks++; if (!(x)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

static int opens, closes, there = 1;

#ifdef OB_SQLITE_LIBRARY
struct Library;
extern struct Library *SQLiteBase;      /* the program's own: without it this does not link */
int sqlite3_link_open(void) { opens++; return there; }
void sqlite3_link_close(void) { closes++; }
#endif

int main(void)
{
#ifdef OB_SQLITE_LIBRARY
    CHECK(ob_sqlite_is_library() == 1);
    CHECK(SQLiteBase == 0);
    CHECK(strstr(OB_SQLITE_NEEDS, "sqlite.library") != 0);
    CHECK(ob_sqlite_open() == 1);
    CHECK(opens == 1 && closes == 0);
    ob_sqlite_close();
    CHECK(closes == 1);
    /* missing or too old: 0, and the way out still closes (the link library's close is safe then) */
    there = 0;
    CHECK(ob_sqlite_open() == 0);
    ob_sqlite_close();
    ob_sqlite_close();
    CHECK(opens == 2 && closes == 3);
#else
    CHECK(ob_sqlite_is_library() == 0);
    CHECK(ob_sqlite_open() == 1);
    ob_sqlite_close();
    ob_sqlite_close();
    CHECK(ob_sqlite_open() == 1);
    CHECK(opens == 0 && closes == 0 && there == 1);
#endif
    printf("%s: %d checks, %d failed (%s)\n", failed ? "BASE SQLITELIB FAIL" : "BASE SQLITELIB PASS", checks, failed,
           ob_sqlite_is_library() ? "library" : "linked in");
    return failed ? 1 : 0;
}
