# The shared core's fills and picture writers (libowf/src/owf_fill.c, owf_picture.c; docs/suite/owf-shared-core.md),
# with the old names that call them (src/deck/opr_gradient.c, ow_picture.c) and the PDF's gradients on them.
# OWF_TEST_NO_ZLIBRARY: PNG in stored blocks, so the test's golden sums don't hang on the zlib version.
SHARED_SRC = libowf/src/owf_fill.c libowf/src/owf_picture.c src/deck/opr_gradient.c src/deck/ow_picture.c
SHARED_DEPS = $(OWF_SRC) $(SHARED_SRC) libowf/include/owf_fill.h libowf/include/owf_picture.h include/ow_picture.h \
	include/openpresentation_core.h libowf/include/owf_pdf.h tests/shared/test_owf_shared.c
SHARED_INC = -DOWF_HAVE_ZLIB -DOWF_TEST_NO_ZLIBRARY -Iinclude -I$(OPENLAYOUT_SRC)/include -Ilibowf/include -Ilibowf/src

$(OUT)/test-owf-shared: $(SHARED_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) $(SHARED_INC) $(OWF_SRC) src/deck/opr_gradient.c src/deck/ow_picture.c \
		tests/shared/test_owf_shared.c -lz -o $@

$(OUT)/test-owf-shared-asan: $(SHARED_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer $(SHARED_INC) \
		$(OWF_SRC) src/deck/opr_gradient.c src/deck/ow_picture.c tests/shared/test_owf_shared.c -lz -o $@

shared-test: $(OUT)/test-owf-shared
	$(OUT)/test-owf-shared

shared-check: $(OUT)/test-owf-shared-asan
	$(OUT)/test-owf-shared-asan

test: shared-test
check: shared-check

.PHONY: shared-test shared-check
