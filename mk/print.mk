# The shared core's print core (libowf/src/owf_print.c; docs/suite/owf-shared-core.md): bleed, crop marks, the
# bleed's colour and booklet order, with libowf's file length and the mail merge data file it refuses
# (libowf/pub/opp_merge.c).
PRINT_SRC = libowf/pub/opp_merge.c src/core/ow_merge.c tests/shared/test_owf_print.c
PRINT_DEPS = $(OWF_SRC) $(PRINT_SRC) libowf/include/owf_print.h libowf/include/owf_deck.h libowf/src/owf_internal.h \
	libowf/include/openpressmaster_merge.h libowf/include/ow_merge.h
PRINT_INC = -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src

$(OUT)/test-owf-print: $(PRINT_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) $(PRINT_INC) $(OWF_SRC) $(PRINT_SRC) -lz -o $@

$(OUT)/test-owf-print-asan: $(PRINT_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer $(PRINT_INC) \
		$(OWF_SRC) $(PRINT_SRC) -lz -o $@

print-core-test: $(OUT)/test-owf-print
	$(OUT)/test-owf-print

print-core-check: $(OUT)/test-owf-print-asan
	$(OUT)/test-owf-print-asan

test: print-core-test
check: print-core-check

.PHONY: print-core-test print-core-check
