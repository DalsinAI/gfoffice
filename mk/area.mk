# The Area window's core (Presentation gap 1): transparency, hatching and shadow values for objects and
# slide backgrounds, each one undo step (tests/deck/test_area.c).

$(OUT)/test-area: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_area.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_area.c -lz -o $@

$(OUT)/test-area-asan: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_area.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_area.c -lz -o $@

area-test: $(OUT)/test-area
	$(OUT)/test-area

area-check: $(OUT)/test-area-asan
	$(OUT)/test-area-asan

test: area-test
check: area-check

.PHONY: area-test area-check
