# GFOpenDiag (amicradle-teams designs/GFOPENDIAG-20261113.md), step D1: the diagram model and the .drawio
# file (libowf/diagram); host tests plain, with no zlib (libowf's own inflate, stored blocks), and under
# ASan and UBSan.
DIAG_SRC = $(wildcard libowf/diagram/*.c)
DIAG_DEPS = $(DIAG_SRC) libowf/include/owf_diagram.h libowf/include/owf.h tests/diagram/test_diagram.c

$(OUT)/test-diagram: $(DIAG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		tests/diagram/test_diagram.c -lz -o $@

$(OUT)/test-diagram-nozlib: $(DIAG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -DOWF_TEST_NO_ZLIBRARY -Ilibowf/include -Ilibowf/src $(OWF_SRC) \
		$(DIAG_SRC) tests/diagram/test_diagram.c -lz -o $@

$(OUT)/test-diagram-asan: $(DIAG_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) tests/diagram/test_diagram.c -lz -o $@

diagram-test: $(OUT)/test-diagram $(OUT)/test-diagram-nozlib
	$(OUT)/test-diagram
	$(OUT)/test-diagram-nozlib

diagram-check: $(OUT)/test-diagram-asan
	$(OUT)/test-diagram-asan

test: diagram-test
check: diagram-check

.PHONY: diagram-test diagram-check
