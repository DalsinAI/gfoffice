# libowf's WMF player (libowf/src/owf_metafile.c) and the page PDF writer playing WMF pictures: host tests.
# The metafiles are made by the test itself (tests/core/test_metafile.c).

METAFILE_SRC = $(OWF_SRC) tests/core/test_metafile.c libowf/src/owf_metafile.h libowf/include/owf_pdf.h

$(OUT)/test-metafile: $(METAFILE_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/core/test_metafile.c -lz -o $@

$(OUT)/test-metafile-asan: $(METAFILE_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/core/test_metafile.c -lz -o $@

metafile-test: $(OUT)/test-metafile
	$(OUT)/test-metafile

metafile-check: $(OUT)/test-metafile-asan
	$(OUT)/test-metafile-asan

test: metafile-test
check: metafile-check

.PHONY: metafile-test metafile-check
