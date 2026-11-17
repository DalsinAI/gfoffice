# Picture fills and tiled picture backgrounds (Presentation gap 1, part 2): ODP and PPTX read and saved,
# the slide clipboard, the layout and the PDF (tests/deck/test_picture_fills.c).

$(OUT)/test-picture-fills: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_picture_fills.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_picture_fills.c -lz -o $@

$(OUT)/test-picture-fills-asan: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_picture_fills.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_picture_fills.c -lz -o $@

picture-fills-test: $(OUT)/test-picture-fills
	$(OUT)/test-picture-fills

picture-fills-check: $(OUT)/test-picture-fills-asan
	$(OUT)/test-picture-fills-asan

test: picture-fills-test
check: picture-fills-check

.PHONY: picture-fills-test picture-fills-check
