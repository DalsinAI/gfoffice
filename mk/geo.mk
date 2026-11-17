# GFOffice's one copy of shape geometry (libowf/src/owf_geo.c): its pen and
# primitives, and every diagram shape's outline against a golden sum taken before the geometry moved out of
# owf_dshape.c; plain and under ASan and UBSan.
GEO_DEPS = libowf/src/owf_geo.c libowf/include/owf_geo.h tests/shared/test_owf_geo.c $(DIAG_DEPS)

$(OUT)/test-owf-geo: $(GEO_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/shared/test_owf_geo.c -lz -o $@

$(OUT)/test-owf-geo-asan: $(GEO_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/shared/test_owf_geo.c -lz -o $@

geo-test: $(OUT)/test-owf-geo
	$(OUT)/test-owf-geo

geo-check: $(OUT)/test-owf-geo-asan
	$(OUT)/test-owf-geo-asan

shape-test: geo-test
shape-check: geo-check
test: geo-test
check: geo-check

.PHONY: geo-test geo-check shape-test shape-check
