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

# Part 2: the merged cells and row heights drawn (opr_layout.c through GFOpenLayout 0.21) and kept right by
# the table edits (opr_edit.c). Needs GFOpenLayout, as deck-edit-test does.
#   make deck-table-draw-test    tests/deck/test_opr_table_draw.c
#   make deck-table-draw-check   the same under AddressSanitizer and UBSan
ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
$(OUT)/test-opr-table-draw: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_table_draw.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_table_draw.c -lz -lm -o $@

$(OUT)/test-opr-table-draw-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_table_draw.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_table_draw.c -lz -lm -o $@

deck-table-draw-test: $(OUT)/test-opr-table-draw
	$(OUT)/test-opr-table-draw

deck-table-draw-check: $(OUT)/test-opr-table-draw-asan
	$(OUT)/test-opr-table-draw-asan

test: deck-table-draw-test
check: deck-table-draw-check
.PHONY: deck-table-draw-test deck-table-draw-check
endif

# Part 3b: a slide table saved as PowerPoint as a real table (opr_pptx.c), read back the same.
#   make deck-table-pptx-test / deck-table-pptx-check    tests/deck/test_opr_table_pptx.c
ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
$(OUT)/test-opr-table-pptx: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_table_pptx.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_table_pptx.c -lz -lm -o $@

$(OUT)/test-opr-table-pptx-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_table_pptx.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_table_pptx.c -lz -lm -o $@

deck-table-pptx-test: $(OUT)/test-opr-table-pptx
	$(OUT)/test-opr-table-pptx

deck-table-pptx-check: $(OUT)/test-opr-table-pptx-asan
	$(OUT)/test-opr-table-pptx-asan

test: deck-table-pptx-test
check: deck-table-pptx-check
.PHONY: deck-table-pptx-test deck-table-pptx-check
endif
