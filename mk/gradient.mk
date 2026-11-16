# Gradient fills on slides (gfoffice gap 2.8): ODP and PPTX read and saved, OpenLayout's shape style,
# the PDF's bands. The files are written by the test itself (tests/deck/test_gradient.c).

$(OUT)/test-gradient: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_gradient.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_gradient.c -lz -o $@

$(OUT)/test-gradient-asan: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_gradient.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_gradient.c -lz -o $@

gradient-test: $(OUT)/test-gradient
	$(OUT)/test-gradient

gradient-check: $(OUT)/test-gradient-asan
	$(OUT)/test-gradient-asan

test: gradient-test
check: gradient-check

.PHONY: gradient-test gradient-check
