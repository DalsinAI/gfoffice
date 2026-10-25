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

test: $(OUT)/owconvert $(OUT)/book-dump $(OUT)/test-calc
	python3 tests/run_tests.py $(OUT)/owconvert
	python3 tests/book/test_books.py $(OUT)/book-dump $(OUT)/owconvert
	$(OUT)/test-calc $(CALC_FIXTURES)

check: $(OUT)/owconvert-asan $(OUT)/test-core-asan $(OUT)/book-dump-asan $(OUT)/test-calc-asan
	python3 tests/run_tests.py $(OUT)/owconvert-asan
	$(OUT)/test-core-asan
	python3 tests/book/test_books.py $(OUT)/book-dump-asan $(OUT)/owconvert-asan
	$(OUT)/test-calc-asan $(CALC_FIXTURES)

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

# GFOffice's workbook files: XLSX, ODS and CSV (tests/book/test_xml_books.py),
# and the formula and number helpers they share (tests/book/test_bookfmt.c)
$(OUT)/test-bookfmt: $(OWF_SRC) tests/book/test_bookfmt.c libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/book/test_bookfmt.c -lz -o $@

$(OUT)/test-bookfmt-asan: $(OWF_SRC) tests/book/test_bookfmt.c libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/book/test_bookfmt.c -lz -o $@

xml-book-test: $(OUT)/test-bookfmt $(OUT)/book-dump $(OUT)/owconvert
	$(OUT)/test-bookfmt
	python3 tests/book/test_xml_books.py $(OUT)/book-dump $(OUT)/owconvert $(OUT)/test-bookfmt

xml-book-check: $(OUT)/test-bookfmt-asan $(OUT)/book-dump-asan $(OUT)/owconvert-asan
	$(OUT)/test-bookfmt-asan
	python3 tests/book/test_xml_books.py $(OUT)/book-dump-asan $(OUT)/owconvert-asan $(OUT)/test-bookfmt-asan

book-test: xml-book-test
book-check: xml-book-check
test: xml-book-test
check: xml-book-check

.PHONY: xml-book-test xml-book-check

# The formula engine (OpenCalc's): libowf/calc, linked only by what
# calculates (it needs -lm). tests/calc/test_calc.c checks it, and works out
# the fixtures' formulas again against the results saved in them
# (tests/calc/make_calc_fixtures.py makes calc.xls with LibreOffice).
CALC_SRC = $(wildcard libowf/calc/*.c)
CALC_FIXTURES = tests/book/fixtures/book.xls tests/calc/fixtures/calc.xls

$(OUT)/test-calc: $(OWF_SRC) $(CALC_SRC) tests/calc/test_calc.c libowf/include/owf.h libowf/include/owf_calc.h libowf/calc/owf_calc_int.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/calc $(OWF_SRC) $(CALC_SRC) tests/calc/test_calc.c -lz -lm -o $@

$(OUT)/test-calc-asan: $(OWF_SRC) $(CALC_SRC) tests/calc/test_calc.c libowf/include/owf.h libowf/include/owf_calc.h libowf/calc/owf_calc_int.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src -Ilibowf/calc $(OWF_SRC) $(CALC_SRC) tests/calc/test_calc.c -lz -lm -o $@

calc-test: $(OUT)/test-calc
	$(OUT)/test-calc $(CALC_FIXTURES)

calc-check: $(OUT)/test-calc-asan
	$(OUT)/test-calc-asan $(CALC_FIXTURES)

# OpenCalc's core (src/calc) on the formula engine (libowf/calc, CALC_SRC
# above): tests/calc_core
OPENCALC_SRC = $(wildcard src/calc/*.c)
OPENCALC_DEPS = $(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) tests/calc_core/test_calc_core.c include/opencalc_core.h \
	src/calc/oc_internal.h libowf/include/owf_calc.h

$(OUT)/test-opencalc-core: $(OPENCALC_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc \
		$(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) tests/calc_core/test_calc_core.c -lz -lm -o $@

$(OUT)/test-opencalc-core-asan: $(OPENCALC_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc \
		$(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) tests/calc_core/test_calc_core.c -lz -lm -o $@

opencalc-test: $(OUT)/test-opencalc-core
	$(OUT)/test-opencalc-core

opencalc-check: $(OUT)/test-opencalc-core-asan
	$(OUT)/test-opencalc-core-asan

test: opencalc-test
check: opencalc-check

.PHONY: opencalc-test opencalc-check

.PHONY: calc-test calc-check

# OpenPublisher's .pub reader (libowf/src/in_pub.c): tests/pub/test_pub.py
# compares pub_dump's output with tests/pub/expected and reads cut-short and
# changed copies of every sample (more of them under ASan/UBSan).
PUB_DEPS = $(OWF_SRC) tests/pub/pub_dump.c libowf/include/owf.h libowf/include/owf_pub.h libowf/src/owf_internal.h

$(OUT)/pub-dump: $(PUB_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/pub/pub_dump.c -lz -o $@

$(OUT)/pub-dump-asan: $(PUB_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/pub/pub_dump.c -lz -o $@

# the format lab's stream lister (tools/publab: docs/gfoffice/PUB-FORMAT.md)
$(OUT)/pub-streams: $(OWF_SRC) tools/publab/pub_streams.c libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Wno-format-truncation -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tools/publab/pub_streams.c -lz -o $@

pub-test: $(OUT)/pub-dump
	python3 tests/pub/test_pub.py $(OUT)/pub-dump 40

pub-check: $(OUT)/pub-dump-asan
	python3 tests/pub/test_pub.py $(OUT)/pub-dump-asan 150

test: pub-test
check: pub-check

.PHONY: pub-test pub-check
