# Presentation gap 2, part 4b (docs/parity/impress.md): the chart editor's result put on a slide as one undo
# step (opr_obj_set_chart in src/deck/opr_edit.c).
#   make setchart-test     tests/deck/test_opr_setchart.c
#   make setchart-check    the same under AddressSanitizer and UBSan
# The deck's editing code goes through GFOpenLayout, so these run when OPENLAYOUT_SRC has it; then they run
# from make test and make check.
# MIT, Copyright (c) 2026 AmiCradle Team.

ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
SETCHART_DEPS = $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_setchart.c \
	tests/deck/chart_sales.h

$(OUT)/test-opr-setchart: $(SETCHART_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_setchart.c -lz -lm -o $@

$(OUT)/test-opr-setchart-asan: $(SETCHART_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_setchart.c -lz -lm -o $@

setchart-test: $(OUT)/test-opr-setchart
	$(OUT)/test-opr-setchart

setchart-check: $(OUT)/test-opr-setchart-asan
	$(OUT)/test-opr-setchart-asan

test: setchart-test
check: setchart-check
.PHONY: setchart-test setchart-check
endif
