# GFOpenBase reports (include/openbase_report.h, Design-OpenBase.md section 7): host tests.
#   make base-report-test    tests/base/test_base_report.c against the pinned SQLite and libowf
#   make base-report-check   the same under AddressSanitizer and UBSan
# Both run from make test and make check. The SQLite object and BASE_SRC come from mk/base.mk.
# MIT, Copyright (c) 2026 AmiCradle Team.

BASE_REPORT_SRC = $(wildcard src/base/report/*.c)
BASE_REPORT_DEPS = $(OWF_SRC) $(BASE_SRC) $(BASE_REPORT_SRC) include/openbase_core.h include/openbase_report.h \
	libowf/include/owf.h libowf/src/owf_internal.h tests/base/test_base_report.c
BASE_REPORT_FLAGS = -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(SQLITE_SRC)

$(OUT)/test-base-report: $(BASE_REPORT_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(BASE_REPORT_FLAGS) $(OWF_SRC) $(BASE_SRC) $(BASE_REPORT_SRC) \
		tests/base/test_base_report.c $(OUT)/sqlite3-base.o -lz -lm -o $@

$(OUT)/test-base-report-asan: $(BASE_REPORT_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(BASE_REPORT_FLAGS) $(OWF_SRC) $(BASE_SRC) $(BASE_REPORT_SRC) \
		tests/base/test_base_report.c $(OUT)/sqlite3-base-asan.o -lz -lm -o $@

base-report-test: $(OUT)/test-base-report
	$(OUT)/test-base-report

base-report-check: $(OUT)/test-base-report-asan
	$(OUT)/test-base-report-asan

test: base-report-test
check: base-report-check
.PHONY: base-report-test base-report-check
