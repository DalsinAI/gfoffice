# Hatched and see-through fills and shadows on slides (Presentation gap 1): ODP and PPTX read and saved,
# kept on the slide clipboard. The files are written by the test itself (tests/deck/test_fills.c).

$(OUT)/test-fills: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_fills.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_fills.c -lz -o $@

$(OUT)/test-fills-asan: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_fills.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_fills.c -lz -o $@

fills-test: $(OUT)/test-fills
	$(OUT)/test-fills

fills-check: $(OUT)/test-fills-asan
	$(OUT)/test-fills-asan

test: fills-test
check: fills-check

.PHONY: fills-test fills-check
