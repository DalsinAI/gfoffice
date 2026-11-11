# Stream P (docs/openoffice-parity-plan.md, section 4): GFOpenPresentation's authoring views.
# The slide sorter's core (src/deck/opr_sorter.c) and the one-step Undo groups it uses go through
# opr_edit.c, which needs GFOpenLayout, so these run when GFOpenLayout is there, as deck-edit-test does.
ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
$(OUT)/test-opr-sorter: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_sorter.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_sorter.c -lz -o $@

$(OUT)/test-opr-sorter-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_sorter.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_sorter.c -lz -o $@

$(OUT)/test-opr-notes: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_notes.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_notes.c -lz -o $@

$(OUT)/test-opr-notes-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_notes.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_notes.c -lz -o $@

present-test: $(OUT)/test-opr-sorter $(OUT)/test-opr-notes
	$(OUT)/test-opr-sorter
	$(OUT)/test-opr-notes

present-check: $(OUT)/test-opr-sorter-asan $(OUT)/test-opr-notes-asan
	$(OUT)/test-opr-sorter-asan
	$(OUT)/test-opr-notes-asan

test: present-test
check: present-check
endif

.PHONY: present-test present-check
