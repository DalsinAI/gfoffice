# Stream W (GFOpenWrite review and references, docs/openoffice-parity-plan.md section 4): the Writer's own
# host tests. W1: tracked changes (tests/core/test_track.c). W2: comments (tests/core/test_comments.c).
# W3: fields with a value (tests/core/test_fields.c).

TRACK_SRC = $(OWF_SRC) $(CORE_SRC) tests/core/test_track.c include/openwrite_core.h libowf/src/owf_track.h

$(OUT)/test-track: $(TRACK_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) \
		tests/core/test_track.c -lz -o $@

$(OUT)/test-track-asan: $(TRACK_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) tests/core/test_track.c -lz -o $@

track-test: $(OUT)/test-track
	$(OUT)/test-track

track-check: $(OUT)/test-track-asan
	$(OUT)/test-track-asan

COMMENTS_SRC = $(OWF_SRC) $(CORE_SRC) tests/core/test_comments.c include/openwrite_core.h libowf/src/owf_comment.h

$(OUT)/test-comments: $(COMMENTS_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) \
		tests/core/test_comments.c -lz -o $@

$(OUT)/test-comments-asan: $(COMMENTS_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) tests/core/test_comments.c -lz -o $@

comments-test: $(OUT)/test-comments
	$(OUT)/test-comments

comments-check: $(OUT)/test-comments-asan
	$(OUT)/test-comments-asan

FIELDS_SRC = $(OWF_SRC) $(CORE_SRC) tests/core/test_fields.c include/openwrite_core.h libowf/src/owf_fields.h

$(OUT)/test-fields: $(FIELDS_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) \
		tests/core/test_fields.c -lz -o $@

$(OUT)/test-fields-asan: $(FIELDS_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) tests/core/test_fields.c -lz -o $@

fields-test: $(OUT)/test-fields
	$(OUT)/test-fields

fields-check: $(OUT)/test-fields-asan
	$(OUT)/test-fields-asan

test: track-test comments-test fields-test
check: track-check comments-check fields-check

.PHONY: track-test track-check comments-test comments-check fields-test fields-check

# W4: footnotes and endnotes (core 1.5, libowf 1.8); with the layout, so GFOpenLayout's source too
NOTES_SRC = $(OWF_SRC) $(CORE_SRC) $(LAYOUT_SRC) tests/core/test_notes.c include/openwrite_core.h include/ow_layout.h \
	libowf/src/owf_note.h

$(OUT)/test-notes: $(NOTES_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		$(OWF_SRC) $(CORE_SRC) $(LAYOUT_SRC) tests/core/test_notes.c -lz -o $@

$(OUT)/test-notes-asan: $(NOTES_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		$(OWF_SRC) $(CORE_SRC) $(LAYOUT_SRC) tests/core/test_notes.c -lz -o $@

notes-test: $(OUT)/test-notes
	$(OUT)/test-notes

notes-check: $(OUT)/test-notes-asan
	$(OUT)/test-notes-asan

test: notes-test
check: notes-check
