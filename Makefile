# GFOpenWrite's filters (libowf) and OWConvert, for this computer (the tests
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

# OWConvert: tools/owconvert.c and its parts, one per library on the Amiga (tools/owc.h), built together here
OWC_SRC = tools/owconvert.c $(wildcard tools/owc_*.c)

$(OUT)/owconvert: $(OWF_SRC) $(OWC_SRC) tools/owc.h libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(OWC_DECK) $(OWC_SRC) -lz -o $@

$(OUT)/owconvert-asan: $(OWF_SRC) $(OWC_SRC) tools/owc.h libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) $(OWC_DECK) $(OWC_SRC) -lz -o $@

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

CORE_SRC = src/core/editor.c src/core/ow_merge.c

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

MERGE_TABLE_DEPS = $(OWF_SRC) src/core/ow_merge.c tests/core/test_merge_table.c libowf/include/ow_merge.h

$(OUT)/test-merge-table: $(MERGE_TABLE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) src/core/ow_merge.c tests/core/test_merge_table.c -lz -o $@

$(OUT)/test-merge-table-asan: $(MERGE_TABLE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) src/core/ow_merge.c tests/core/test_merge_table.c -lz -o $@

merge-table-test: $(OUT)/test-merge-table
	$(OUT)/test-merge-table

merge-table-check: $(OUT)/test-merge-table-asan
	$(OUT)/test-merge-table-asan

test: merge-table-test
check: merge-table-check

.PHONY: merge-table-test merge-table-check

$(OUT)/test-spell: $(OWF_SRC) $(CORE_SRC) app/ow_spell.c src/core/ow_spell_dict.c tests/core/test_spell.c include/openwrite_core.h app/ow_spell.h include/ow_spell_dict.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Iapp -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) app/ow_spell.c src/core/ow_spell_dict.c tests/core/test_spell.c -lz -o $@

spell-test: $(OUT)/test-spell
	$(OUT)/test-spell

$(OUT)/test-spell-asan: $(OWF_SRC) $(CORE_SRC) app/ow_spell.c src/core/ow_spell_dict.c tests/core/test_spell.c include/openwrite_core.h app/ow_spell.h include/ow_spell_dict.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Iapp -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(CORE_SRC) app/ow_spell.c src/core/ow_spell_dict.c tests/core/test_spell.c -lz -o $@

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

# every C library call in libowf's archive is one the libraries' runtime gives (owf_runtime.c and friends), so
# OWF=library ./build-os3.sh links: caught here before the 68k link (in_md.c's sprintf, out_md.c's strpbrk)
library-libc-check:
	sh scripts/check-library-libc.sh

test: library-libc-check
check: library-libc-check

.PHONY: library-libc-check

# libowf's own inflate (libowf/src/owf_inflate.c), which reads zip packages
# without z.library. tests/inflate/test_inflate.c checks it against zlib's
# deflate, hand-made streams and damaged ones (--bench ZIP... times it
# against zlib). owconvert-nozlibrary is OWConvert with zlib's calls failing
# as z.library's do on an Amiga without it; test_inflate_samples.py reads
# the samples with it and with zlib, and compares.
INFLATE_SRC = libowf/src/owf_inflate.c libowf/src/owf_inflate.h

$(OUT)/test-inflate: $(INFLATE_SRC) tests/inflate/test_inflate.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Ilibowf/include -Ilibowf/src libowf/src/owf_inflate.c tests/inflate/test_inflate.c -lz -o $@

$(OUT)/test-inflate-asan: $(INFLATE_SRC) tests/inflate/test_inflate.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-Ilibowf/include -Ilibowf/src libowf/src/owf_inflate.c tests/inflate/test_inflate.c -lz -o $@

$(OUT)/owconvert-nozlibrary: $(OWF_SRC) $(OWC_SRC) tools/owc.h libowf/include/owf.h libowf/src/owf_internal.h libowf/src/owf_zlib.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -DOWF_TEST_NO_ZLIBRARY -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(OWC_SRC) -lz -o $@

$(OUT)/owconvert-nozlibrary-asan: $(OWF_SRC) $(OWC_SRC) tools/owc.h libowf/include/owf.h libowf/src/owf_internal.h libowf/src/owf_zlib.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -DOWF_TEST_NO_ZLIBRARY -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(OWC_SRC) -lz -o $@

inflate-test: $(OUT)/test-inflate $(OUT)/owconvert $(OUT)/owconvert-nozlibrary
	$(OUT)/test-inflate
	python3 tests/inflate/test_inflate_samples.py $(OUT)/owconvert $(OUT)/owconvert-nozlibrary

inflate-check: $(OUT)/test-inflate-asan $(OUT)/owconvert-asan $(OUT)/owconvert-nozlibrary-asan
	$(OUT)/test-inflate-asan 100000
	python3 tests/inflate/test_inflate_samples.py $(OUT)/owconvert-asan $(OUT)/owconvert-nozlibrary-asan

test: inflate-test
check: inflate-check

# The compound file writer (libowf/src/owf_cfbw.c), the container for
# GFOpenPressmaster's .pub save; tests/cfb/test_cfbw.c reads what it writes
# back through owf_cfb.c.
$(OUT)/test-cfbw: $(OWF_SRC) libowf/src/owf_internal.h tests/cfb/test_cfbw.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/cfb/test_cfbw.c -lz -lm -o $@

$(OUT)/test-cfbw-asan: $(OWF_SRC) libowf/src/owf_internal.h tests/cfb/test_cfbw.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/cfb/test_cfbw.c -lz -lm -o $@

.PHONY: cfbw-test cfbw-check
cfbw-test: $(OUT)/test-cfbw
	$(OUT)/test-cfbw

cfbw-check: $(OUT)/test-cfbw-asan
	$(OUT)/test-cfbw-asan

test: cfbw-test
check: cfbw-check

.PHONY: inflate-test inflate-check

# GFOpenWrite's pages on GFOpenLayout (DalsinAI/gfopenlayout 0.2 or later)
OPENLAYOUT_SRC ?= ../gfopenlayout
OPENLAYOUT_ENGINE_SRC = $(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c) \
	$(wildcard $(OPENLAYOUT_SRC)/src/gfcss_parse.c) \
	$(wildcard $(OPENLAYOUT_SRC)/src/gfcss_values.c) \
	$(wildcard $(OPENLAYOUT_SRC)/src/gfcss_select.c) \
	$(wildcard $(OPENLAYOUT_SRC)/src/openlayout_css.c)
LAYOUT_SRC = src/core/ow_layout.c $(OPENLAYOUT_ENGINE_SRC)

$(OUT)/test-layout: $(OWF_SRC) $(LAYOUT_SRC) $(CORE_SRC) tests/core/test_layout.c include/ow_layout.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		$(OWF_SRC) $(LAYOUT_SRC) $(CORE_SRC) tests/core/test_layout.c -lz -o $@

$(OUT)/test-layout-asan: $(OWF_SRC) $(LAYOUT_SRC) $(CORE_SRC) tests/core/test_layout.c include/ow_layout.h
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		$(OWF_SRC) $(LAYOUT_SRC) $(CORE_SRC) tests/core/test_layout.c -lz -o $@

layout-test: $(OUT)/test-layout
	$(OUT)/test-layout samples/OpenWrite-Tour.odt samples/OpenWrite-Tour.docx samples/AmigaChrome-Feature-Tata.docx \
		tests/core/fixtures/contents.docx tests/core/fixtures/cover.docx \
		tests/core/fixtures/cover-wpg.docx tests/core/fixtures/wrap.docx tests/core/fixtures/header-frames.docx \
		tests/core/fixtures/inline-box.docx tests/core/fixtures/line-rules.docx \
		tests/core/fixtures/table-styles.docx tests/core/fixtures/frames.odt \
		tests/core/fixtures/bookmarks.docx tests/core/fixtures/columns.docx

layout-check: $(OUT)/test-layout-asan
	$(OUT)/test-layout-asan samples/OpenWrite-Tour.odt samples/OpenWrite-Tour.docx samples/AmigaChrome-Feature-Tata.docx \
		tests/core/fixtures/contents.docx tests/core/fixtures/cover.docx \
		tests/core/fixtures/cover-wpg.docx tests/core/fixtures/wrap.docx tests/core/fixtures/header-frames.docx \
		tests/core/fixtures/inline-box.docx tests/core/fixtures/line-rules.docx \
		tests/core/fixtures/table-styles.docx tests/core/fixtures/frames.odt \
		tests/core/fixtures/bookmarks.docx tests/core/fixtures/columns.docx

.PHONY: layout-test layout-check

# GFOpenWrite's text widths for the layout (app/ow_fontwidths.c, 2.0.3): the face that draws the text when the
# document's doesn't open, and widths at the size asked or scaled to it (tests/core/test_fontwidths.c)
FONTW_SRC = app/ow_fontwidths.c app/ow_fontwidths.h tests/core/test_fontwidths.c

$(OUT)/test-fontwidths: $(FONTW_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Iapp app/ow_fontwidths.c tests/core/test_fontwidths.c -o $@

$(OUT)/test-fontwidths-asan: $(FONTW_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-Iapp app/ow_fontwidths.c tests/core/test_fontwidths.c -o $@

fontwidths-test: $(OUT)/test-fontwidths
	$(OUT)/test-fontwidths

fontwidths-check: $(OUT)/test-fontwidths-asan
	$(OUT)/test-fontwidths-asan

test: fontwidths-test
check: fontwidths-check

.PHONY: fontwidths-test fontwidths-check

# Saving (libowf/src/owf_sys.c): a save that fails, as on a full disk, leaves the file as it was
# (tests/core/test_save.c)
SAVE_SRC = libowf/src/owf_sys.c libowf/src/owf_internal.h libowf/include/owf.h tests/core/test_save.c

$(OUT)/test-save: $(SAVE_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Ilibowf/include -Ilibowf/src libowf/src/owf_sys.c tests/core/test_save.c -o $@

$(OUT)/test-save-asan: $(SAVE_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-Ilibowf/include -Ilibowf/src libowf/src/owf_sys.c tests/core/test_save.c -o $@

save-test: $(OUT)/test-save
	$(OUT)/test-save

save-check: $(OUT)/test-save-asan
	$(OUT)/test-save-asan

test: save-test
check: save-check

.PHONY: save-test save-check

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

# What a sheet holds beside its cells (conditional formatting...), read from
# fixtures/features-openpyxl.xlsx (tests/book/make_feature_fixtures.py) and
# kept on saving: tests/book/test_sheet_features.py
features-test: $(OUT)/book-dump $(OUT)/owconvert
	python3 tests/book/test_sheet_features.py $(OUT)/book-dump $(OUT)/owconvert

features-check: $(OUT)/book-dump-asan $(OUT)/owconvert-asan
	python3 tests/book/test_sheet_features.py $(OUT)/book-dump-asan $(OUT)/owconvert-asan

book-test: features-test
book-check: features-check

.PHONY: features-test features-check

# The formula engine (GFOpenCalc's): libowf/calc, linked only by what
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

# GFOpenCalc's core (src/calc) on the formula engine (libowf/calc, CALC_SRC
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

# The formula engine's second batch of functions: tests/calc/fn2_cases.h is
# made by tests/calc/fn2_expect.py (python3 tests/calc/fn2_expect.py >
# tests/calc/fn2_cases.h); the tests check it is what the script makes.
$(OUT)/test-calc $(OUT)/test-calc-asan: tests/calc/fn2_cases.h libowf/calc/owf_calc_fn.h

calc-expect:
	python3 tests/calc/fn2_expect.py --check tests/calc/fn2_cases.h

test: calc-expect
check: calc-expect

.PHONY: calc-expect

# GFOpenPresentation (GFOffice's slides): the deck model and ODP in libowf
# (owf_deck.h), the slides laid out by GFOpenLayout and drawn into the page
# PDF writer in src/deck. tests/deck/test_decks.py reads, writes and checks
# the fixtures (odfpy and pypdf when installed, for the tests only). With
# GFOpenLayout (OPENLAYOUT_SRC), OWConvert exports decks to PDF and the PDF
# and layout tests run too; without it they are skipped, and say so.
DECK_SRC = $(wildcard src/deck/*.c) src/core/ow_layout.c src/core/ow_spell_dict.c
DECK_DEPS = $(OWF_SRC) libowf/include/owf.h libowf/include/owf_deck.h libowf/include/owf_pdf.h libowf/src/owf_internal.h \
	libowf/src/owf_deck_int.h
ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
OWC_DECK = -DOWC_DECK_PDF -Iinclude -I$(OPENLAYOUT_SRC)/include $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC)
DECK_LAYOUT = -DOPR_WITH_LAYOUT -Iinclude -I$(OPENLAYOUT_SRC)/include $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC)
$(OUT)/owconvert $(OUT)/owconvert-asan: $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) include/openpresentation_core.h include/ow_layout.h
endif

$(OUT)/deck-dump: $(DECK_DEPS) tests/deck/deck_dump.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/deck_dump.c -lz -o $@

$(OUT)/deck-dump-asan: $(DECK_DEPS) tests/deck/deck_dump.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/deck/deck_dump.c -lz -o $@

$(OUT)/test-deck: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_deck.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_deck.c -lz -o $@

$(OUT)/test-deck-asan: $(DECK_DEPS) $(DECK_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/deck/test_deck.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_deck.c -lz -o $@

deck-test: $(OUT)/test-deck $(OUT)/deck-dump $(OUT)/owconvert
	$(OUT)/test-deck
	python3 tests/deck/test_decks.py $(OUT)/deck-dump $(OUT)/owconvert
	python3 tests/deck/test_pptx.py $(OUT)/deck-dump $(OUT)/owconvert

deck-check: $(OUT)/test-deck-asan $(OUT)/deck-dump-asan $(OUT)/owconvert-asan
	$(OUT)/test-deck-asan
	python3 tests/deck/test_decks.py $(OUT)/deck-dump-asan $(OUT)/owconvert-asan
	python3 tests/deck/test_pptx.py $(OUT)/deck-dump-asan $(OUT)/owconvert-asan

test: deck-test
check: deck-check

.PHONY: deck-test deck-check

# Typing in a slide's text (src/deck/opr_edit.c): the caret and the pointer go through GFOpenLayout's
# display list, so these run when GFOpenLayout is there.
ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
$(OUT)/test-opr-edit: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_edit.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_edit.c -lz -o $@

$(OUT)/test-opr-edit-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_edit.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_edit.c -lz -o $@

deck-edit-test: $(OUT)/test-opr-edit
	$(OUT)/test-opr-edit tests/deck/fixtures/pptx-talk.pptx

deck-edit-check: $(OUT)/test-opr-edit-asan
	$(OUT)/test-opr-edit-asan tests/deck/fixtures/pptx-talk.pptx

# Spelling through a deck (src/deck/opr_spell.c) with GFOpenWrite's dictionary.
$(OUT)/test-opr-spell: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h include/ow_spell_dict.h tests/deck/test_opr_spell.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) \
		tests/deck/test_opr_spell.c -lz -o $@

$(OUT)/test-opr-spell-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h include/ow_spell_dict.h tests/deck/test_opr_spell.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) tests/deck/test_opr_spell.c -lz -o $@

deck-spell-test: $(OUT)/test-opr-spell
	$(OUT)/test-opr-spell

deck-spell-check: $(OUT)/test-opr-spell-asan
	$(OUT)/test-opr-spell-asan

test: deck-spell-test
check: deck-spell-check

.PHONY: deck-spell-test deck-spell-check

$(OUT)/test-opr-show: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_show.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) \
		$(OWF_SRC) tests/deck/test_opr_show.c -lz -o $@

$(OUT)/test-opr-show-asan: $(DECK_DEPS) $(DECK_SRC) include/openpresentation_core.h tests/deck/test_opr_show.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) \
		$(OWF_SRC) tests/deck/test_opr_show.c -lz -o $@

deck-show-test: $(OUT)/test-opr-show
	$(OUT)/test-opr-show

deck-show-check: $(OUT)/test-opr-show-asan
	$(OUT)/test-opr-show-asan

test: deck-edit-test deck-show-test
check: deck-edit-check deck-show-check

.PHONY: deck-edit-test deck-edit-check deck-show-test deck-show-check
endif

# GFOpenPressmaster's .pub reader (libowf/src/in_pub.c): tests/pub/test_pub.py
# compares pub_dump's output with tests/pub/expected and reads cut-short and
# changed copies of every sample (more of them under ASan/UBSan). The samples
# are made by tools/pubgen/make_pub.py: no outside .pub files are kept.
PUB_SAMPLES = $(OUT)/pubgen
PUB_MADE = $(PUB_SAMPLES)/.made

$(PUB_MADE): tools/pubgen/make_pub.py
	python3 tools/pubgen/make_pub.py $(PUB_SAMPLES)
	touch $@

PUB_DEPS = $(OWF_SRC) tests/pub/pub_dump.c libowf/include/owf.h libowf/include/owf_pub.h libowf/src/owf_internal.h

$(OUT)/pub-dump: $(PUB_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/pub/pub_dump.c -lz -o $@

$(OUT)/pub-dump-asan: $(PUB_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/pub/pub_dump.c -lz -o $@

# pub_resave: reads a .pub and writes it again (out_pub.c), for test_pub_save.py
$(OUT)/pub-resave: $(PUB_DEPS) tests/pub/pub_resave.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/pub/pub_resave.c -lz -o $@

$(OUT)/pub-resave-asan: $(PUB_DEPS) tests/pub/pub_resave.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/pub/pub_resave.c -lz -o $@

# the format lab's stream lister (tools/publab: docs/gfoffice/PUB-FORMAT.md)
$(OUT)/pub-streams: $(OWF_SRC) tools/publab/pub_streams.c libowf/src/owf_internal.h
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Wno-format-truncation -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tools/publab/pub_streams.c -lz -o $@

pub-test: $(OUT)/pub-dump $(OUT)/pub-resave $(PUB_MADE)
	python3 tests/pub/test_pubgen.py
	PUB_SAMPLES=$(PUB_SAMPLES) python3 tests/pub/test_pub.py $(OUT)/pub-dump 40
	PUB_SAMPLES=$(PUB_SAMPLES) python3 tests/pub/test_pub_save.py $(OUT)/pub-resave $(OUT)/pub-dump
	python3 tests/pub/test_pub_long_list.py $(OUT)/pub-dump
	python3 tests/pub/test_pub_many_masters.py $(OUT)/pub-dump

pub-check: $(OUT)/pub-dump-asan $(OUT)/pub-resave-asan $(PUB_MADE)
	PUB_SAMPLES=$(PUB_SAMPLES) python3 tests/pub/test_pub.py $(OUT)/pub-dump-asan 150
	PUB_SAMPLES=$(PUB_SAMPLES) python3 tests/pub/test_pub_save.py $(OUT)/pub-resave-asan $(OUT)/pub-dump-asan
	python3 tests/pub/test_pub_long_list.py $(OUT)/pub-dump-asan
	python3 tests/pub/test_pub_many_masters.py $(OUT)/pub-dump-asan

test: pub-test
check: pub-check

# the publishing app's name, ARexx port and old-name alias (renamed from GFOpenPublisher, 16 Nov 2026)
pressmaster-name-check:
	python3 tests/pub/test_pressmaster_name.py

test: pressmaster-name-check
check: pressmaster-name-check

# GFOpenPressmaster's mail merge on the page model (libowf/pub/opp_merge.c)
PUB_MERGE_DEPS = $(OWF_SRC) libowf/pub/opp_merge.c src/core/ow_merge.c tests/pub/test_pub_merge.c \
	libowf/include/openpressmaster_merge.h libowf/include/ow_merge.h libowf/include/owf_deck.h

$(OUT)/test-publisher-merge: $(PUB_MERGE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) libowf/pub/opp_merge.c \
		src/core/ow_merge.c tests/pub/test_pub_merge.c -lz -o $@

$(OUT)/test-publisher-merge-asan: $(PUB_MERGE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) libowf/pub/opp_merge.c \
		src/core/ow_merge.c tests/pub/test_pub_merge.c -lz -o $@

pressmaster-merge-test: $(OUT)/test-publisher-merge
	$(OUT)/test-publisher-merge

pressmaster-merge-check: $(OUT)/test-publisher-merge-asan
	$(OUT)/test-publisher-merge-asan

test: pressmaster-merge-test
check: pressmaster-merge-check

.PHONY: pressmaster-merge-test pressmaster-merge-check

# GFOpenPressmaster's built-in publication templates (libowf/pub/opp_template.c)
PUB_TEMPLATE_DEPS = $(OWF_SRC) libowf/pub/opp_template.c libowf/pub/opp_merge.c src/core/ow_merge.c \
	tests/pub/test_pub_template.c libowf/include/openpressmaster_template.h libowf/include/openpressmaster_merge.h \
	libowf/include/ow_merge.h libowf/include/owf_deck.h

$(OUT)/test-publisher-template: $(PUB_TEMPLATE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) libowf/pub/opp_template.c \
		libowf/pub/opp_merge.c src/core/ow_merge.c tests/pub/test_pub_template.c -lz -o $@

$(OUT)/test-publisher-template-asan: $(PUB_TEMPLATE_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) libowf/pub/opp_template.c \
		libowf/pub/opp_merge.c src/core/ow_merge.c tests/pub/test_pub_template.c -lz -o $@

pressmaster-template-test: $(OUT)/test-publisher-template
	$(OUT)/test-publisher-template

pressmaster-template-check: $(OUT)/test-publisher-template-asan
	$(OUT)/test-publisher-template-asan

test: pressmaster-template-test
check: pressmaster-template-check

.PHONY: pressmaster-template-test pressmaster-template-check

ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
# merge fields shown by the page layout and kept by ODP and PPTX (tests/pub/test_pub_merge_show.c)
PUB_MERGE_SHOW_DEPS = $(PUB_MERGE_DEPS) $(DECK_DEPS) $(DECK_SRC) tests/pub/test_pub_merge_show.c include/openpresentation_core.h

$(OUT)/test-publisher-merge-show: $(PUB_MERGE_SHOW_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) libowf/pub/opp_merge.c \
		src/core/ow_merge.c tests/pub/test_pub_merge_show.c -lz -o $@

$(OUT)/test-publisher-merge-show-asan: $(PUB_MERGE_SHOW_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) libowf/pub/opp_merge.c src/core/ow_merge.c \
		tests/pub/test_pub_merge_show.c -lz -o $@

pressmaster-merge-show-test: $(OUT)/test-publisher-merge-show
	$(OUT)/test-publisher-merge-show

pressmaster-merge-show-check: $(OUT)/test-publisher-merge-show-asan
	$(OUT)/test-publisher-merge-show-asan

test: pressmaster-merge-show-test
check: pressmaster-merge-show-check

# GFOpenPressmaster's master pages (the shared Master view) and Insert > Page Number (src/pub/opp_pagenum.c)
PUB_MASTERS_SRC = src/pub/opp_pagenum.c libowf/pub/opp_merge.c libowf/pub/opp_template.c src/core/ow_merge.c
PUB_MASTERS_DEPS = $(PUB_MERGE_SHOW_DEPS) $(PUB_MASTERS_SRC) tests/pub/test_pub_masters.c \
	include/openpressmaster_pagenum.h libowf/include/openpressmaster_template.h

$(OUT)/test-pressmaster-masters: $(PUB_MASTERS_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) $(PUB_MASTERS_SRC) \
		tests/pub/test_pub_masters.c -lz -o $@

$(OUT)/test-pressmaster-masters-asan: $(PUB_MASTERS_DEPS)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Ilibowf/include -Ilibowf/src $(DECK_LAYOUT) $(OWF_SRC) $(PUB_MASTERS_SRC) tests/pub/test_pub_masters.c -lz -o $@

pressmaster-masters-test: $(OUT)/test-pressmaster-masters
	$(OUT)/test-pressmaster-masters

pressmaster-masters-check: $(OUT)/test-pressmaster-masters-asan
	$(OUT)/test-pressmaster-masters-asan

test: pressmaster-masters-test
check: pressmaster-masters-check

.PHONY: pressmaster-masters-test pressmaster-masters-check
pressmaster-merge-test: pressmaster-merge-show-test
pressmaster-merge-check: pressmaster-merge-show-check

.PHONY: pressmaster-merge-show-test pressmaster-merge-show-check
endif

ifneq ($(wildcard $(OPENLAYOUT_SRC)/src/openlayout.c),)
PUB_CORE_SRC = src/pub/opp_import.c src/pub/opp_export.c src/deck/opr_layout.c src/deck/opr_pdf.c src/core/ow_layout.c
PUB_CORE_DEPS = $(PUB_DEPS) $(PUB_CORE_SRC) include/openpressmaster_core.h include/openpresentation_core.h \
	libowf/include/owf_deck.h $(OPENLAYOUT_ENGINE_SRC)

$(OUT)/test-publisher-core: $(PUB_CORE_DEPS) tests/pub/test_pub_core.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		-DPUB_SAMPLES='"$(PUB_SAMPLES)"' $(OWF_SRC) $(PUB_CORE_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/pub/test_pub_core.c -lz -o $@

$(OUT)/test-publisher-core-asan: $(PUB_CORE_DEPS) tests/pub/test_pub_core.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(OPENLAYOUT_SRC)/include \
		-DPUB_SAMPLES='"$(PUB_SAMPLES)"' $(OWF_SRC) $(PUB_CORE_SRC) $(OPENLAYOUT_ENGINE_SRC) tests/pub/test_pub_core.c -lz -o $@

pressmaster-core-test: $(OUT)/test-publisher-core $(PUB_MADE)
	$(OUT)/test-publisher-core

pressmaster-core-check: $(OUT)/test-publisher-core-asan $(PUB_MADE)
	$(OUT)/test-publisher-core-asan

test: pressmaster-core-test
check: pressmaster-core-check

.PHONY: pressmaster-core-test pressmaster-core-check
endif

.PHONY: pub-test pub-check

# owf.library 2 (libowf/library): the library's own double arithmetic
# (owf_softdouble.c) bit for bit against the PC's doubles; its maths and
# strtod (owf_libm.c) against the PC's C library; the formula engine's own
# tests on those maths (libm_shim.c, no -lm); OWFLibTest's version-2 checks
# (owf_lib_checks.c) with libowf linked in and the same maths;
# owf_file_kind and the structures' sizes; the SFD against the headers and
# the glue (check_sfd.py); libowf's z.library SDK against z.library's own
# jump table (check_zsdk.py); and the maths' tables (gen_libm_tables.py).
LIBM_DEPS = libowf/library/owf_libm.c libowf/library/owf_libm.h
LIBM_ON = -fno-builtin -Ilibowf/library libowf/library/owf_libm.c tests/library/libm_shim.c
# Sample.pub: a .pub made by tools/pubgen/make_pub.py (gen-2007-runs.pub)
LIB_FIXTURES = tests/book/fixtures/book.xls tests/book/fixtures/book-lo.xlsx tests/book/fixtures/book-lo.ods
LIBCHECK_DEPS = $(OWF_SRC) $(CALC_SRC) $(LIBM_DEPS) tests/library/libm_shim.c tests/library/owf_lib_checks.c \
	tests/library/owf_lib_checks.h tests/library/test_owf_lib_checks.c libowf/include/owf.h libowf/include/owf_zip.h
SANITIZE = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

$(OUT)/test-owf-softdouble: libowf/library/owf_softdouble.c tests/library/test_owf_softdouble.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOSD_HOST_TEST libowf/library/owf_softdouble.c tests/library/test_owf_softdouble.c -lm -o $@

$(OUT)/test-owf-softdouble-asan: libowf/library/owf_softdouble.c tests/library/test_owf_softdouble.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOSD_HOST_TEST libowf/library/owf_softdouble.c tests/library/test_owf_softdouble.c -lm -o $@

$(OUT)/test-owf-libm: $(OWF_SRC) $(LIBM_DEPS) tests/library/test_owf_libm.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -fno-builtin -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/library $(OWF_SRC) \
		libowf/library/owf_libm.c tests/library/test_owf_libm.c -lz -lm -o $@

$(OUT)/test-owf-libm-asan: $(OWF_SRC) $(LIBM_DEPS) tests/library/test_owf_libm.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -fno-builtin -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/library $(OWF_SRC) \
		libowf/library/owf_libm.c tests/library/test_owf_libm.c -lz -lm -o $@

$(OUT)/test-calc-owflibm: $(OWF_SRC) $(CALC_SRC) $(LIBM_DEPS) tests/library/libm_shim.c tests/calc/test_calc.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/calc $(LIBM_ON) $(OWF_SRC) $(CALC_SRC) \
		tests/calc/test_calc.c -lz -o $@

$(OUT)/test-calc-owflibm-asan: $(OWF_SRC) $(CALC_SRC) $(LIBM_DEPS) tests/library/libm_shim.c tests/calc/test_calc.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/calc $(LIBM_ON) $(OWF_SRC) $(CALC_SRC) \
		tests/calc/test_calc.c -lz -o $@

$(OUT)/test-owf-lib-checks: $(LIBCHECK_DEPS) $(PUB_MADE)
	mkdir -p $(OUT)/libfixtures
	cp $(LIB_FIXTURES) $(OUT)/libfixtures/
	cp $(PUB_SAMPLES)/gen-2007-runs.pub $(OUT)/libfixtures/Sample.pub
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/calc $(LIBM_ON) $(OWF_SRC) $(CALC_SRC) \
		tests/library/owf_lib_checks.c tests/library/test_owf_lib_checks.c -lz -o $@

$(OUT)/test-owf-lib-checks-asan: $(LIBCHECK_DEPS) $(PUB_MADE)
	mkdir -p $(OUT)/libfixtures
	cp $(LIB_FIXTURES) $(OUT)/libfixtures/
	cp $(PUB_SAMPLES)/gen-2007-runs.pub $(OUT)/libfixtures/Sample.pub
	$(CC) $(WARN) $(SANITIZE) -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src -Ilibowf/calc $(LIBM_ON) $(OWF_SRC) $(CALC_SRC) \
		tests/library/owf_lib_checks.c tests/library/test_owf_lib_checks.c -lz -o $@

$(OUT)/test-owf-kind: $(OWF_SRC) tests/library/test_owf_kind.c libowf/include/owf.h libowf/include/owf_zip.h $(PUB_MADE)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DPUB_SAMPLES='"$(PUB_SAMPLES)"' -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/library/test_owf_kind.c -lz -o $@

$(OUT)/test-owf-kind-asan: $(OWF_SRC) tests/library/test_owf_kind.c libowf/include/owf.h libowf/include/owf_zip.h $(PUB_MADE)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(SANITIZE) -DPUB_SAMPLES='"$(PUB_SAMPLES)"' -DOWF_HAVE_ZLIB -Ilibowf/include -Ilibowf/src $(OWF_SRC) tests/library/test_owf_kind.c -lz -o $@

sfd-check:
	python3 tests/library/check_sfd.py
	python3 tests/library/check_zsdk.py
	python3 tests/library/gen_libm_tables.py --check libowf/library/owf_libm.c

library-test: sfd-check $(OUT)/test-owf-softdouble $(OUT)/test-owf-libm $(OUT)/test-calc-owflibm \
		$(OUT)/test-owf-lib-checks $(OUT)/test-owf-kind
	$(OUT)/test-owf-softdouble
	$(OUT)/test-owf-libm
	$(OUT)/test-calc-owflibm $(CALC_FIXTURES)
	$(OUT)/test-owf-lib-checks $(OUT)/libfixtures $(OUT)/
	$(OUT)/test-owf-kind

library-check: sfd-check $(OUT)/test-owf-softdouble-asan $(OUT)/test-owf-libm-asan $(OUT)/test-calc-owflibm-asan \
		$(OUT)/test-owf-lib-checks-asan $(OUT)/test-owf-kind-asan
	$(OUT)/test-owf-softdouble-asan 300000
	$(OUT)/test-owf-libm-asan 40000
	$(OUT)/test-calc-owflibm-asan $(CALC_FIXTURES)
	$(OUT)/test-owf-lib-checks-asan $(OUT)/libfixtures $(OUT)/
	$(OUT)/test-owf-kind-asan

libtest-host: $(OUT)/test-owf-lib-checks
	$(OUT)/test-owf-lib-checks $(OUT)/libfixtures $(OUT)/

test: library-test
check: library-check

.PHONY: sfd-check library-test library-check libtest-host

# The fuzz harnesses (tests/fuzz/README.md), under AddressSanitizer and UBSan
FUZZ_OUT = build/fuzz
FUZZ_FLAGS = $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB

fuzz: $(FUZZ_OUT)/fuzz_doc $(FUZZ_OUT)/fuzz_book

$(FUZZ_OUT)/fuzz_%: tests/fuzz/fuzz_%.c tests/fuzz/driver.c $(OWF_SRC) libowf/include/owf.h libowf/src/owf_internal.h
	mkdir -p $(FUZZ_OUT)
	$(CC) $(FUZZ_FLAGS) -Ilibowf/include -Ilibowf/src $(OWF_SRC) $< tests/fuzz/driver.c -lz -o $@

.PHONY: fuzz

# Workstream hooks (docs/openoffice-parity-plan.md, section 4): each stream's host test targets
# live in its own mk/<stream>.mk, so streams never edit this file at the same time.
-include $(sort $(wildcard mk/*.mk))

# Markdown in and out (libowf/src/in_md.c, out_md.c): files the test makes itself
markdown-test: $(OUT)/owconvert
	python3 tests/markdown/test_markdown.py $(OUT)/owconvert

markdown-check: $(OUT)/owconvert-asan
	python3 tests/markdown/test_markdown.py $(OUT)/owconvert-asan

test: markdown-test
check: markdown-check

.PHONY: markdown-test markdown-check

# the target names from before the app was renamed from GFOpenPublisher (16 Nov 2026), kept so old scripts still work
publisher-core-check: pressmaster-core-check
publisher-core-test: pressmaster-core-test
publisher-merge-check: pressmaster-merge-check
publisher-merge-test: pressmaster-merge-test
publisher-merge-show-check: pressmaster-merge-show-check
publisher-merge-show-test: pressmaster-merge-show-test
publisher-template-check: pressmaster-template-check
publisher-template-test: pressmaster-template-test
.PHONY: publisher-core-check publisher-core-test publisher-merge-check publisher-merge-test publisher-merge-show-check publisher-merge-show-test publisher-template-check publisher-template-test
