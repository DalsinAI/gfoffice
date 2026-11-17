# The time a zip is stamped with, fixed for tests that save twice (owf_zip_fix_time, SOURCE_DATE_EPOCH):
#   make zip-time-test / zip-time-check    tests/shared/test_zip_time.c
ZIP_TIME_SRC = $(OWF_SRC) libowf/src/owf_internal.h tests/shared/test_zip_time.c

$(OUT)/test-zip-time: $(ZIP_TIME_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/shared/test_zip_time.c \
		-lz -lm -o $@

$(OUT)/test-zip-time-asan: $(ZIP_TIME_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/shared/test_zip_time.c -lz -lm -o $@

zip-time-test: $(OUT)/test-zip-time
	$(OUT)/test-zip-time

zip-time-check: $(OUT)/test-zip-time-asan
	$(OUT)/test-zip-time-asan

test: zip-time-test
check: zip-time-check
.PHONY: zip-time-test zip-time-check
