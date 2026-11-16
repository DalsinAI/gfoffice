# Presentation gap 2, part 4 (docs/parity/impress.md): a slide's chart edited on GFOpenCalc's chart engine and
# a data sheet (src/deckchart, include/opr_chartedit.h).
#   make chartedit-test     tests/deckchart/test_chartedit.c
#   make chartedit-check    the same under AddressSanitizer and UBSan
# libowf, its formula engine and GFOpenCalc's core (no GFOpenLayout). Both run from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

CHARTEDIT_SRC = $(wildcard src/deckchart/*.c)
CHARTEDIT_DEPS = $(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) $(CHARTEDIT_SRC) include/opr_chartedit.h \
	include/opencalc_core.h tests/deckchart/test_chartedit.c tests/deck/chart_sales.h
CHARTEDIT_FLAGS = -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc

$(OUT)/test-chartedit: $(CHARTEDIT_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) $(CHARTEDIT_FLAGS) $(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) $(CHARTEDIT_SRC) \
		tests/deckchart/test_chartedit.c -lz -lm -o $@

$(OUT)/test-chartedit-asan: $(CHARTEDIT_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) $(CHARTEDIT_FLAGS) $(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) $(CHARTEDIT_SRC) \
		tests/deckchart/test_chartedit.c -lz -lm -o $@

chartedit-test: $(OUT)/test-chartedit
	$(OUT)/test-chartedit

chartedit-check: $(OUT)/test-chartedit-asan
	$(OUT)/test-chartedit-asan

test: chartedit-test
check: chartedit-check
.PHONY: chartedit-test chartedit-check
