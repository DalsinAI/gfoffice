# Stream W (GFOpenWrite review and references, docs/openoffice-parity-plan.md section 4): the Writer's own
# host tests. W1: tracked changes (tests/core/test_track.c).

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

test: track-test
check: track-check

.PHONY: track-test track-check
