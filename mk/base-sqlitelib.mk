# GFOpenBase's SQLite either way (build.d/base.sh's SQLITE: linked in, or sqlite.library): host tests.
#   make base-sqlitelib-test    app/ob_sqlitelib.c built both ways (tests/base/test_base_sqlitelib.c), and
#                               build.d/base.sh's switch with a compiler that writes its arguments down
#                               (tests/base/test_base_build_switch.sh)
#   make base-sqlitelib-check   the same, the C under AddressSanitizer and UBSan
# MIT, Copyright (c) 2026 AmiCradle Team.

BASE_SQLITELIB_DEPS = app/ob_sqlitelib.c app/ob_sqlitelib.h tests/base/test_base_sqlitelib.c \
	tests/base/sqlitelib/sqlite3_link.h
BASE_SQLITELIB_SRC = app/ob_sqlitelib.c tests/base/test_base_sqlitelib.c
BASE_SQLITELIB_LIB = -DOB_SQLITE_LIBRARY -Itests/base/sqlitelib

$(OUT)/test-base-sqlitelib: $(BASE_SQLITELIB_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Iapp $(BASE_SQLITELIB_SRC) -o $@
	$(CC) $(WARN) $(CFLAGS) -Iapp $(BASE_SQLITELIB_LIB) $(BASE_SQLITELIB_SRC) -o $@-library

$(OUT)/test-base-sqlitelib-asan: $(BASE_SQLITELIB_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -Iapp $(BASE_SQLITELIB_SRC) -o $@
	$(CC) $(WARN) $(SANITIZE) -Iapp $(BASE_SQLITELIB_LIB) $(BASE_SQLITELIB_SRC) -o $@-library

base-sqlitelib-test: $(OUT)/test-base-sqlitelib
	$(OUT)/test-base-sqlitelib
	$(OUT)/test-base-sqlitelib-library
	tests/base/test_base_build_switch.sh
	tests/base/test_base_sqlite_fetch.sh

base-sqlitelib-check: $(OUT)/test-base-sqlitelib-asan
	$(OUT)/test-base-sqlitelib-asan
	$(OUT)/test-base-sqlitelib-asan-library
	tests/base/test_base_build_switch.sh
	tests/base/test_base_sqlite_fetch.sh

test: base-sqlitelib-test
check: base-sqlitelib-check
.PHONY: base-sqlitelib-test base-sqlitelib-check
