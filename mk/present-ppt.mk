# PowerPoint 97-2003 (.ppt) in (libowf/src/in_ppt.c), into the deck model.
#   make deck-ppt-test    tests/deck/test_ppt.c: a .ppt the test makes record by record, read into the model; cuts
#                         and damage through it read without harm
#   make deck-ppt-check   the same under AddressSanitizer and UBSan
# libowf only (no GFOpenLayout). Both run from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

DECK_PPT_DEPS = $(OWF_SRC) libowf/include/owf.h libowf/include/owf_deck.h libowf/src/owf_internal.h \
	libowf/src/owf_deck_int.h tests/deck/test_ppt.c

$(OUT)/test-ppt: $(DECK_PPT_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/test_ppt.c \
		-lz -lm -o $@

$(OUT)/test-ppt-asan: $(DECK_PPT_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/test_ppt.c \
		-lz -lm -o $@

deck-ppt-test: $(OUT)/test-ppt
	$(OUT)/test-ppt

deck-ppt-check: $(OUT)/test-ppt-asan
	$(OUT)/test-ppt-asan

test: deck-ppt-test
check: deck-ppt-check
.PHONY: deck-ppt-test deck-ppt-check
