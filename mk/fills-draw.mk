# Hatched and see-through fills and shadows drawn (Presentation gap 1, part 3): the shape style, the hatch
# lines cut to an outline, the PDF (tests/deck/test_fills_draw.c).

$(OUT)/test-fills-draw: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_fills_draw.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_fills_draw.c -lz -o $@

$(OUT)/test-fills-draw-asan: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_fills_draw.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_fills_draw.c -lz -o $@

fills-draw-test: $(OUT)/test-fills-draw
	$(OUT)/test-fills-draw

fills-draw-check: $(OUT)/test-fills-draw-asan
	$(OUT)/test-fills-draw-asan

test: fills-draw-test
check: fills-draw-check

.PHONY: fills-draw-test fills-draw-check
