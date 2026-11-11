# Stream W (GFOpenWrite review and references, docs/openoffice-parity-plan.md section 4): the Writer's own
# host tests. W1: tracked changes (tests/core/test_track.c). W2: comments (tests/core/test_comments.c).

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

test: track-test comments-test
check: track-check comments-check

.PHONY: track-test track-check comments-test comments-check
