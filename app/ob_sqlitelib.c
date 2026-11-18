/* ob_sqlitelib.h: sqlite.library opened for GFOpenBase before it has a window. The program has this
 * SQLiteBase of its own, so the link library's own start-up code (which opens the library before main()
 * and can only say what is wrong in its own words) is not linked.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "ob_sqlitelib.h"

#ifdef OB_SQLITE_LIBRARY

struct Library;
struct Library *SQLiteBase;

int ob_sqlite_open(void) { return sqlite3_link_open() != 0; }
void ob_sqlite_close(void) { sqlite3_link_close(); }
int ob_sqlite_is_library(void) { return 1; }

#else

int ob_sqlite_open(void) { return 1; }
void ob_sqlite_close(void) { }
int ob_sqlite_is_library(void) { return 0; }

#endif
