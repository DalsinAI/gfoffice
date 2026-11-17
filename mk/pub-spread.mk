# GFOpenPressmaster: facing pages, two to a spread (src/pub/opp_spread.c, spreads step 2).
#   make pressmaster-spread-test    tests/pub/test_pub_spread.c
#   make pressmaster-spread-check   the same under AddressSanitizer and UBSan
# libowf only (no GFOpenLayout). Both run from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

PUB_SPREAD_DEPS = $(OWF_SRC) libowf/include/owf.h libowf/include/owf_deck.h include/openpressmaster_spread.h \
	src/pub/opp_spread.c tests/pub/test_pub_spread.c

$(OUT)/test-pressmaster-spread: $(PUB_SPREAD_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) src/pub/opp_spread.c \
		tests/pub/test_pub_spread.c -lz -lm -o $@

$(OUT)/test-pressmaster-spread-asan: $(PUB_SPREAD_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) src/pub/opp_spread.c \
		tests/pub/test_pub_spread.c -lz -lm -o $@

pressmaster-spread-test: $(OUT)/test-pressmaster-spread
	$(OUT)/test-pressmaster-spread

pressmaster-spread-check: $(OUT)/test-pressmaster-spread-asan
	$(OUT)/test-pressmaster-spread-asan

test: pressmaster-spread-test
check: pressmaster-spread-check
.PHONY: pressmaster-spread-test pressmaster-spread-check
