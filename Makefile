# OpenWrite's filters (libowf) and OWConvert, for this computer (the tests
# run here). For AmigaOS 3.x use build-os3.sh.
#   make            build/host/owconvert
#   make test       the tests (needs python3)
#   make check      the tests under AddressSanitizer and UBSan

CC ?= cc
CFLAGS ?= -O2
WARN = -std=c99 -Wall -Wextra -Wpedantic -Wno-overlength-strings
OWF_SRC = $(wildcard libowf/src/*.c)
OUT = build/host

all: $(OUT)/owconvert

$(OUT)/owconvert: $(OWF_SRC) tools/owconvert.c libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tools/owconvert.c -lz -o $@

$(OUT)/owconvert-asan: $(OWF_SRC) tools/owconvert.c libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tools/owconvert.c -lz -o $@

test: $(OUT)/owconvert $(OUT)/book-dump
	python3 tests/run_tests.py $(OUT)/owconvert
	python3 tests/book/test_books.py $(OUT)/book-dump $(OUT)/owconvert

check: $(OUT)/owconvert-asan $(OUT)/test-core-asan $(OUT)/book-dump-asan
	python3 tests/run_tests.py $(OUT)/owconvert-asan
	$(OUT)/test-core-asan
	python3 tests/book/test_books.py $(OUT)/book-dump-asan $(OUT)/owconvert-asan

clean:
	rm -rf build

.PHONY: all test check clean

CORE_SRC = src/core/editor.c

$(OUT)/test-core: $(OWF_SRC) $(CORE_SRC) tests/core/test_core.c include/openwrite_core.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) tests/core/test_core.c -lz -o $@

core-test: $(OUT)/test-core
	$(OUT)/test-core

.PHONY: core-test

$(OUT)/test-core-asan: $(OWF_SRC) $(CORE_SRC) tests/core/test_core.c include/openwrite_core.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) tests/core/test_core.c -lz -o $@

core-check: $(OUT)/test-core-asan
	$(OUT)/test-core-asan

.PHONY: core-check

$(OUT)/test-spell: $(OWF_SRC) $(CORE_SRC) app/ow_spell.c tests/core/test_spell.c include/openwrite_core.h app/ow_spell.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Iapp -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) app/ow_spell.c tests/core/test_spell.c -lz -o $@

spell-test: $(OUT)/test-spell
	$(OUT)/test-spell

$(OUT)/test-spell-asan: $(OWF_SRC) $(CORE_SRC) app/ow_spell.c tests/core/test_spell.c include/openwrite_core.h app/ow_spell.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Iapp -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) app/ow_spell.c tests/core/test_spell.c -lz -o $@

spell-check: $(OUT)/test-spell-asan
	$(OUT)/test-spell-asan

.PHONY: spell-test spell-check

# owf.library's own C library calls against the PC's
$(OUT)/test-owf-libc: libowf/library/owf_libc.c libowf/library/owf_libc.h tests/library/test_owf_libc.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Wno-format-truncation -Ilibowf/library libowf/library/owf_libc.c tests/library/test_owf_libc.c -o $@

libc-test: $(OUT)/test-owf-libc
	$(OUT)/test-owf-libc

.PHONY: libc-test

# OpenWrite's pages on OpenLayout (DalsinAI/openamigalayout 0.2 or later)
OPENLAYOUT_SRC ?= ../openamigalayout
LAYOUT_SRC = src/core/ow_layout.c $(OPENLAYOUT_SRC)/src/openlayout.c

$(OUT)/test-layout: $(OWF_SRC) $(LAYOUT_SRC) tests/core/test_layout.c include/ow_layout.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		$(OWF_SRC) $(LAYOUT_SRC) tests/core/test_layout.c -lz -o $@

$(OUT)/test-layout-asan: $(OWF_SRC) $(LAYOUT_SRC) tests/core/test_layout.c include/ow_layout.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		$(OWF_SRC) $(LAYOUT_SRC) tests/core/test_layout.c -lz -o $@

layout-test: $(OUT)/test-layout
	$(OUT)/test-layout samples/OpenWrite-Tour.odt samples/OpenWrite-Tour.docx samples/AmigaChrome-Feature-Tata.docx

layout-check: $(OUT)/test-layout-asan
	$(OUT)/test-layout-asan samples/OpenWrite-Tour.odt samples/OpenWrite-Tour.docx samples/AmigaChrome-Feature-Tata.docx

.PHONY: layout-test layout-check

# Workbooks (the spreadsheet's files): tests/book/test_books.py
$(OUT)/book-dump: $(OWF_SRC) tests/book/book_dump.c libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/book/book_dump.c -lz -o $@

$(OUT)/book-dump-asan: $(OWF_SRC) tests/book/book_dump.c libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/book/book_dump.c -lz -o $@

book-test: $(OUT)/book-dump $(OUT)/owconvert
	python3 tests/book/test_books.py $(OUT)/book-dump $(OUT)/owconvert

book-check: $(OUT)/book-dump-asan $(OUT)/owconvert-asan
	python3 tests/book/test_books.py $(OUT)/book-dump-asan $(OUT)/owconvert-asan

.PHONY: book-test book-check

# OpenCalc's core (src/calc) on the formula engine: tests/calc_core
CALC_SRC = $(wildcard src/calc/*.c)
CALC_DEPS = $(OWF_SRC) $(CALC_SRC) tests/calc_core/test_calc_core.c include/opencalc_core.h src/calc/oc_internal.h libowf/include/owf_calc.h

$(OUT)/test-calc-core: $(CALC_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -Isrc/calc \
		$(OWF_SRC) $(CALC_SRC) tests/calc_core/test_calc_core.c -lz -lm -o $@

$(OUT)/test-calc-core-asan: $(CALC_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src -Isrc/calc $(OWF_SRC) $(CALC_SRC) tests/calc_core/test_calc_core.c -lz -lm -o $@

calc-test: $(OUT)/test-calc-core
	$(OUT)/test-calc-core

calc-check: $(OUT)/test-calc-core-asan
	$(OUT)/test-calc-core-asan

test: calc-test
check: calc-check

.PHONY: calc-test calc-check
