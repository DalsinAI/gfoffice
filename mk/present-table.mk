# Presentation gap 3 (docs/parity/impress.md): tables on slides, their rows' heights, merged cells and design.
#   make deck-table-test    tests/deck/test_deck_table.c: read from PPTX and ODP into the model, copied, freed
#   make deck-table-check   the same under AddressSanitizer and UBSan
# libowf only (no GFOpenLayout). Both run from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

DECK_TABLE_DEPS = $(OWF_SRC) libowf/include/owf.h libowf/include/owf_deck.h libowf/src/owf_internal.h \
	libowf/src/owf_deck_int.h tests/deck/test_deck_table.c

$(OUT)/test-deck-table: $(DECK_TABLE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/test_deck_table.c \
		-lz -lm -o $@

$(OUT)/test-deck-table-asan: $(DECK_TABLE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/test_deck_table.c \
		-lz -lm -o $@

deck-table-test: $(OUT)/test-deck-table
	$(OUT)/test-deck-table

deck-table-check: $(OUT)/test-deck-table-asan
	$(OUT)/test-deck-table-asan

test: deck-table-test
check: deck-table-check
.PHONY: deck-table-test deck-table-check
