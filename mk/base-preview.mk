# GFOpenBase's print preview (include/openbase_preview.h, Design-OpenBase.md section 7): host tests.
#   make base-preview-test    tests/base/test_base_preview.c against the pinned SQLite, libowf and GFOpenLayout
#   make base-preview-check   the same under AddressSanitizer and UBSan
# Both run from make test and make check. Without GFOpenLayout (OPENLAYOUT_SRC) they say so and pass.
# The SQLite object and BASE_SRC come from mk/base.mk, which is read after this file.
# MIT, Copyright (c) 2026 AmiCradle Team.

BASE_PREVIEW_SRC = $(wildcard src/base/preview/*.c) src/core/ow_layout.c $(OPENLAYOUT_ENGINE_SRC)
BASE_PREVIEW_DEPS = $(OWF_SRC) $(wildcard src/base/*.c src/base/report/*.c) $(BASE_PREVIEW_SRC) \
	include/openbase_core.h include/openbase_report.h include/openbase_preview.h include/ow_layout.h \
	libowf/include/owf.h tests/base/test_base_preview.c
BASE_PREVIEW_FLAGS = $(BASE_REPORT_FLAGS) -I$(OPENLAYOUT_SRC)/include

$(OUT)/test-base-preview: $(BASE_PREVIEW_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(BASE_PREVIEW_FLAGS) $(OWF_SRC) $(BASE_SRC) $(BASE_REPORT_SRC) $(BASE_PREVIEW_SRC) \
		tests/base/test_base_preview.c $(OUT)/sqlite3-base.o -lz -lm -o $@

$(OUT)/test-base-preview-asan: $(BASE_PREVIEW_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(BASE_PREVIEW_FLAGS) $(OWF_SRC) $(BASE_SRC) $(BASE_REPORT_SRC) $(BASE_PREVIEW_SRC) \
		tests/base/test_base_preview.c $(OUT)/sqlite3-base-asan.o -lz -lm -o $@

ifneq ($(OPENLAYOUT_ENGINE_SRC),)
base-preview-test: $(OUT)/test-base-preview
	$(OUT)/test-base-preview

base-preview-check: $(OUT)/test-base-preview-asan
	$(OUT)/test-base-preview-asan
else
base-preview-test base-preview-check:
	@echo "base preview: no GFOpenLayout in $(OPENLAYOUT_SRC) (set OPENLAYOUT_SRC): left out"
endif

test: base-preview-test
check: base-preview-check
.PHONY: base-preview-test base-preview-check
