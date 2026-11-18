/* SQLite for GFOpenBase, either way it is built (build.d/base.sh's SQLITE): through sqlite.library
 * (OB_SQLITE_LIBRARY: gfopensqlite's link library, libsqlite3_link.a), or with SQLite linked in. The
 * program calls ob_sqlite_open() before it opens a project or a window, and when it answers 0 says so
 * (OB_SQLITE_NEEDS: a requester from Workbench, a line in the Shell) and ends with RETURN_FAIL; it calls
 * ob_sqlite_close() on its way out, after the last project is closed. With SQLite linked in the two do
 * nothing.
 *
 * On the library, ob_sqlite_close() also closes any connection the program left open (sqlite.library
 * keeps each program's own), so no file or lock outlives GFOpenBase. No sqlite3_ call may be made
 * before ob_sqlite_open() answers 1 or after ob_sqlite_close().
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OB_SQLITELIB_H
#define OB_SQLITELIB_H

#ifdef OB_SQLITE_LIBRARY
#include <sqlite3_link.h>
/* what the program's message names: "GFOpenBase needs " OB_SQLITE_NEEDS "." */
#define OB_SQLITE_NEEDS SQLITE3_LINK_NEEDS
#else
#define OB_SQLITE_NEEDS "SQLite"
#endif

/* 1 when SQLite's calls can be made: sqlite.library is open at the version the link library was built
 * for, or SQLite is linked in. 0 when the library is missing or older; nothing is left open then. */
int ob_sqlite_open(void);
/* Closes what ob_sqlite_open opened. Safe to call when it answered 0, or twice. */
void ob_sqlite_close(void);
/* 1 when this program was built to open the library, 0 with SQLite linked in. */
int ob_sqlite_is_library(void);

#endif
