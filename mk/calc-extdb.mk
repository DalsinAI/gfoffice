# GFOpenCalc's database sources for External Data (include/opencalc_extdb.h): host tests.
#   make calc-extdb-test    tests/calc_core/test_calc_extdb.c against the pinned SQLite, GFOpenBase's core and
#                           GFOpenCalc's core
#   make calc-extdb-check   the same under AddressSanitizer and UBSan
# Both run from make test and make check. The SQLite object and BASE_SRC come from mk/base.mk, and OPENCALC_SRC
# and CALC_SRC from the Makefile, read around this file: the prerequisites name the sources themselves.
# MIT, Copyright (c) 2026 AmiCradle Team.

CALC_EXTDB_SRC = $(wildcard src/calc/extdb/*.c)
CALC_EXTDB_DEPS = $(OWF_SRC) $(wildcard libowf/calc/*.c) $(wildcard src/calc/*.c) $(wildcard src/base/*.c) \
	$(CALC_EXTDB_SRC) include/opencalc_core.h include/opencalc_extdb.h include/openbase_core.h \
	tests/calc_core/test_calc_extdb.c
CALC_EXTDB_FLAGS = -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc -I$(SQLITE_SRC)
CALC_EXTDB_ALL = $(OWF_SRC) $(wildcard libowf/calc/*.c) $(wildcard src/calc/*.c) $(wildcard src/base/*.c) \
	$(CALC_EXTDB_SRC) tests/calc_core/test_calc_extdb.c

$(OUT)/test-calc-extdb: $(CALC_EXTDB_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(CALC_EXTDB_FLAGS) $(CALC_EXTDB_ALL) $(OUT)/sqlite3-base.o -lz -lm -o $@

$(OUT)/test-calc-extdb-asan: $(CALC_EXTDB_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(CALC_EXTDB_FLAGS) $(CALC_EXTDB_ALL) $(OUT)/sqlite3-base-asan.o -lz -lm -o $@

calc-extdb-test: $(OUT)/test-calc-extdb
	$(OUT)/test-calc-extdb

calc-extdb-check: $(OUT)/test-calc-extdb-asan
	$(OUT)/test-calc-extdb-asan

test: calc-extdb-test
check: calc-extdb-check
.PHONY: calc-extdb-test calc-extdb-check
