# Presentation gap 2 (docs/parity/impress.md): charts on slides, read and kept with their data.
#   make deck-chart-test    tests/deck/test_deck_chart.c: a chart from its part with no book, and a PPTX slide's
#   make deck-chart-check   the same under AddressSanitizer and UBSan
# libowf only (no GFOpenLayout). Both run from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

DECK_CHART_DEPS = $(OWF_SRC) libowf/include/owf.h libowf/include/owf_deck.h libowf/src/owf_internal.h \
	tests/deck/test_deck_chart.c

$(OUT)/test-deck-chart: $(DECK_CHART_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/test_deck_chart.c \
		-lz -lm -o $@

$(OUT)/test-deck-chart-asan: $(DECK_CHART_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/test_deck_chart.c \
		-lz -lm -o $@

deck-chart-test: $(OUT)/test-deck-chart
	$(OUT)/test-deck-chart

deck-chart-check: $(OUT)/test-deck-chart-asan
	$(OUT)/test-deck-chart-asan

test: deck-chart-test
check: deck-chart-check
.PHONY: deck-chart-test deck-chart-check
