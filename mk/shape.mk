# GFOffice's one copy of shape geometry (libowf/src/owf_shape.c, docs/suite/owf-shared-core.md): its pen and
# primitives, and every diagram shape's outline against a golden sum taken before the geometry moved out of
# owf_dshape.c; plain and under ASan and UBSan.
SHAPE_DEPS = libowf/src/owf_shape.c libowf/include/owf_shape.h tests/shared/test_owf_shape.c $(DIAG_DEPS)

$(OUT)/test-owf-shape: $(SHAPE_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/shared/test_owf_shape.c -lz -o $@

$(OUT)/test-owf-shape-asan: $(SHAPE_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/shared/test_owf_shape.c -lz -o $@

shape-test: $(OUT)/test-owf-shape
	$(OUT)/test-owf-shape

shape-check: $(OUT)/test-owf-shape-asan
	$(OUT)/test-owf-shape-asan

test: shape-test
check: shape-check

.PHONY: shape-test shape-check
