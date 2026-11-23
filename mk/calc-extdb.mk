# GFOpenCalc's database sources for External Data (include/opencalc_extdb.h): host tests.
#   make calc-extdb-test    tests/calc_core/test_calc_extdb.c against the pinned SQLite, GFOpenBase's core and
#                           GFOpenCalc's core
#                           (SQLite linked in), tests/calc_core/test_calc_extdb_lib.c built for sqlite.library
#                           (OB_SQLITE_LIBRARY, the link library's calls answered by the test), and
#                           build.d/calc-extdb.sh's switch (tests/calc_core/test_calc_extdb_switch.sh)
#   make calc-extdb-check   the same under AddressSanitizer and UBSan
# Both run from make test and make check. The SQLite object and BASE_SRC come from mk/base.mk, and OPENCALC_SRC
# and CALC_SRC from the Makefile, read around this file: the prerequisites name the sources themselves.
# MIT, Copyright (c) 2026 AmiCradle Team.

CALC_EXTDB_SRC = $(wildcard src/calc/extdb/*.c) app/ob_sqlitelib.c
CALC_EXTDB_DEPS = $(OWF_SRC) $(wildcard libowf/calc/*.c) $(wildcard src/calc/*.c) $(wildcard src/base/*.c) \
	$(CALC_EXTDB_SRC) include/opencalc_core.h include/opencalc_extdb.h include/openbase_core.h \
	app/ob_sqlitelib.h tests/calc_core/test_calc_extdb.c tests/calc_core/test_calc_extdb_lib.c \
	tests/base/sqlitelib/sqlite3_link.h
CALC_EXTDB_FLAGS = -DOWF_HAVE_ZLIB -Iinclude -Iapp -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc -I$(SQLITE_SRC)
CALC_EXTDB_BASE = $(OWF_SRC) $(wildcard libowf/calc/*.c) $(wildcard src/calc/*.c) $(wildcard src/base/*.c) \
	$(CALC_EXTDB_SRC)
CALC_EXTDB_ALL = $(CALC_EXTDB_BASE) tests/calc_core/test_calc_extdb.c
# on sqlite.library: the link library's two calls are the test's own (tests/base/sqlitelib), SQLite the host's
CALC_EXTDB_LIBFLAGS = -DOB_SQLITE_LIBRARY -Itests/base/sqlitelib
CALC_EXTDB_LIBALL = $(CALC_EXTDB_BASE) tests/calc_core/test_calc_extdb_lib.c

$(OUT)/test-calc-extdb: $(CALC_EXTDB_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(CALC_EXTDB_FLAGS) $(CALC_EXTDB_ALL) $(OUT)/sqlite3-base.o -lz -lm -o $@
	$(CC) $(WARN) $(CFLAGS) $(CALC_EXTDB_FLAGS) $(CALC_EXTDB_LIBFLAGS) $(CALC_EXTDB_LIBALL) $(OUT)/sqlite3-base.o \
		-lz -lm -o $@-library

$(OUT)/test-calc-extdb-asan: $(CALC_EXTDB_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(CALC_EXTDB_FLAGS) $(CALC_EXTDB_ALL) $(OUT)/sqlite3-base-asan.o -lz -lm -o $@
	$(CC) $(WARN) $(SANITIZE) $(CALC_EXTDB_FLAGS) $(CALC_EXTDB_LIBFLAGS) $(CALC_EXTDB_LIBALL) \
		$(OUT)/sqlite3-base-asan.o -lz -lm -o $@-library

calc-extdb-test: $(OUT)/test-calc-extdb
	$(OUT)/test-calc-extdb
	$(OUT)/test-calc-extdb-library
	tests/calc_core/test_calc_extdb_switch.sh

calc-extdb-check: $(OUT)/test-calc-extdb-asan
	$(OUT)/test-calc-extdb-asan
	$(OUT)/test-calc-extdb-asan-library
	tests/calc_core/test_calc_extdb_switch.sh

test: calc-extdb-test
check: calc-extdb-check
.PHONY: calc-extdb-test calc-extdb-check
