# Presentation gap 2 (docs/parity/impress.md): charts on slides, read and kept with their data.
#   make deck-chart-test    tests/deck/test_deck_chart.c: a chart from its part with no book, and a PPTX slide's
#   make deck-chart-check   the same under AddressSanitizer and UBSan
#   make opr-chart-test     tests/deck/test_opr_pptx_chart.c: a slide's chart saved as PPTX with its embedded
#   make opr-chart-check    workbook and read back (src/deck, so with GFOpenLayout: OPENLAYOUT_SRC)
# deck-chart is libowf only (no GFOpenLayout). All run from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

DECK_CHART_DEPS = $(OWF_SRC) libowf/include/owf.h libowf/include/owf_deck.h libowf/src/owf_internal.h \
	tests/deck/test_deck_chart.c tests/deck/chart_sales.h

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
.PHONY: deck-chart-test deck-chart-check opr-chart-test opr-chart-check

ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
OPR_CHART_DEPS = $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_pptx_chart.c \
	tests/deck/chart_sales.h

$(OUT)/test-opr-pptx-chart: $(OPR_CHART_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_pptx_chart.c -lz -lm -o $@

$(OUT)/test-opr-pptx-chart-asan: $(OPR_CHART_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_pptx_chart.c -lz -lm -o $@

opr-chart-test: $(OUT)/test-opr-pptx-chart
	$(OUT)/test-opr-pptx-chart

opr-chart-check: $(OUT)/test-opr-pptx-chart-asan
	$(OUT)/test-opr-pptx-chart-asan
else
opr-chart-test opr-chart-check:
	@echo "opr chart: no GFOpenLayout in $(OPENLAYOUT_SRC) (set OPENLAYOUT_SRC): left out"
endif
test: opr-chart-test
check: opr-chart-check
