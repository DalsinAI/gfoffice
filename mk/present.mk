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

$(OUT)/test-opr-outline: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_outline.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_outline.c -lz -o $@

$(OUT)/test-opr-outline-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_outline.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_outline.c -lz -o $@

$(OUT)/test-opr-pptx: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_pptx.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_pptx.c -lz -o $@

$(OUT)/test-opr-pptx-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_pptx.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_pptx.c -lz -o $@

$(OUT)/test-opr-multi: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_multi.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_multi.c -lz -o $@

$(OUT)/test-opr-multi-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_multi.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_multi.c -lz -o $@

$(OUT)/test-opr-anim: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_anim.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_anim.c -lz -o $@

$(OUT)/test-opr-anim-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_anim.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_anim.c -lz -o $@

$(OUT)/test-opr-master: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_master.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_master.c -lz -o $@

$(OUT)/test-opr-master-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_master.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_master.c -lz -o $@

$(OUT)/test-opr-clip: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_clip.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_clip.c -lz -o $@

$(OUT)/test-opr-picture: src/deck/opr_picture.c include/openpresentation_core.h tests/deck/test_opr_picture.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -I$(OPENLAYOUT_SRC)/include -Ilibowf/include -Ilibowf/src \
		src/deck/opr_picture.c tests/deck/test_opr_picture.c -lz -o $@

$(OUT)/test-opr-picture-asan: src/deck/opr_picture.c include/openpresentation_core.h tests/deck/test_opr_picture.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -DOWF_TEST_NO_ZLIBRARY -Iinclude -I$(OPENLAYOUT_SRC)/include -Ilibowf/include -Ilibowf/src \
		src/deck/opr_picture.c tests/deck/test_opr_picture.c -lz -o $@

$(OUT)/test-opr-clip-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_clip.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_clip.c -lz -o $@

present-test: $(OUT)/test-opr-sorter $(OUT)/test-opr-notes $(OUT)/test-opr-outline $(OUT)/test-opr-pptx $(OUT)/test-opr-multi $(OUT)/test-opr-anim $(OUT)/test-opr-master $(OUT)/test-opr-clip $(OUT)/test-opr-picture
	$(OUT)/test-opr-sorter
	$(OUT)/test-opr-notes
	$(OUT)/test-opr-outline
	$(OUT)/test-opr-pptx
	$(OUT)/test-opr-multi
	$(OUT)/test-opr-anim
	$(OUT)/test-opr-master
	$(OUT)/test-opr-clip
	cd $(OUT) && ./test-opr-picture

present-check: $(OUT)/test-opr-sorter-asan $(OUT)/test-opr-notes-asan $(OUT)/test-opr-outline-asan $(OUT)/test-opr-pptx-asan $(OUT)/test-opr-multi-asan $(OUT)/test-opr-anim-asan $(OUT)/test-opr-master-asan $(OUT)/test-opr-clip-asan $(OUT)/test-opr-picture-asan
	$(OUT)/test-opr-sorter-asan
	$(OUT)/test-opr-notes-asan
	$(OUT)/test-opr-outline-asan
	$(OUT)/test-opr-pptx-asan
	$(OUT)/test-opr-multi-asan
	$(OUT)/test-opr-anim-asan
	$(OUT)/test-opr-master-asan
	$(OUT)/test-opr-clip-asan
	cd $(OUT) && ./test-opr-picture-asan

test: present-test
check: present-check
endif

.PHONY: present-test present-check
