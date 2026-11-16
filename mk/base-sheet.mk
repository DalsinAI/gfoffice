# GFOpenBase sheets in and out (include/openbase_sheet.h, Design-OpenBase.md section 8): host tests.
#   make base-sheet-test    tests/base/test_base_sheet.c against the pinned SQLite and libowf
#   make base-sheet-check   the same under AddressSanitizer and UBSan
# Both run from make test and make check. The SQLite object and BASE_SRC come from mk/base.mk, which is
# read after this file: the prerequisites (expanded as they are read) name src/base/*.c themselves.
# MIT, Copyright (c) 2026 AmiCradle Team.

BASE_SHEET_SRC = $(wildcard src/base/sheet/*.c)
BASE_SHEET_DEPS = $(OWF_SRC) $(wildcard src/base/*.c) $(BASE_SHEET_SRC) include/openbase_core.h include/openbase_sheet.h \
	libowf/include/owf.h libowf/src/owf_internal.h tests/base/test_base_sheet.c
BASE_SHEET_FLAGS = -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(SQLITE_SRC)

$(OUT)/test-base-sheet: $(BASE_SHEET_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(BASE_SHEET_FLAGS) $(OWF_SRC) $(BASE_SRC) $(BASE_SHEET_SRC) \
		tests/base/test_base_sheet.c $(OUT)/sqlite3-base.o -lz -lm -o $@

$(OUT)/test-base-sheet-asan: $(BASE_SHEET_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(BASE_SHEET_FLAGS) $(OWF_SRC) $(BASE_SRC) $(BASE_SHEET_SRC) \
		tests/base/test_base_sheet.c $(OUT)/sqlite3-base-asan.o -lz -lm -o $@

base-sheet-test: $(OUT)/test-base-sheet
	$(OUT)/test-base-sheet

base-sheet-check: $(OUT)/test-base-sheet-asan
	$(OUT)/test-base-sheet-asan

test: base-sheet-test
check: base-sheet-check
.PHONY: base-sheet-test base-sheet-check
