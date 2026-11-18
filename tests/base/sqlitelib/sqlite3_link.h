/* gfopensqlite's <sqlite3_link.h> as tests/base/test_base_sqlitelib.c needs it on the PC: the two calls
 * app/ob_sqlitelib.c makes, which the test itself answers.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef SQLITE3_LINK_H
#define SQLITE3_LINK_H
#define SQLITE3_LINK_NEEDS "sqlite.library 1 or later in LIBS:"
int sqlite3_link_open(void);
void sqlite3_link_close(void);
#endif
