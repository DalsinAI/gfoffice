# GFOffice's one spell checker (libowf/src/owf_spell.c, owfspell.library: docs/suite/owf-spell.md), on its own:
# the word lists, the word rules, suggestions, the user's words and a list shared by two openers. It reads the
# shipped Dictionaries/en_GB.words too. Only owf_spell.c, owf_sys.c and owf_common.c's helpers: no document code.

SPELL_LIB_SRC = libowf/src/owf_spell.c libowf/src/owf_sys.c libowf/src/owf_common.c

$(OUT)/test-owf-spell: $(SPELL_LIB_SRC) libowf/include/owf_spell.h libowf/src/owf_internal.h tests/spell/test_owf_spell.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -Ilibowf/include -Ilibowf/src $(SPELL_LIB_SRC) tests/spell/test_owf_spell.c -o $@

$(OUT)/test-owf-spell-asan: $(SPELL_LIB_SRC) libowf/include/owf_spell.h libowf/src/owf_internal.h tests/spell/test_owf_spell.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-Ilibowf/include -Ilibowf/src $(SPELL_LIB_SRC) tests/spell/test_owf_spell.c -o $@

spell-lib-test: $(OUT)/test-owf-spell
	$(OUT)/test-owf-spell Dictionaries/en_GB.words

spell-lib-check: $(OUT)/test-owf-spell-asan
	$(OUT)/test-owf-spell-asan Dictionaries/en_GB.words

test: spell-lib-test
check: spell-lib-check

.PHONY: spell-lib-test spell-lib-check

# GFOpenCalc's spelling (src/calc/oc_spell.c) through the active sheet's text cells
$(OUT)/test-calc-spell: $(OPENCALC_DEPS) src/calc/oc_spell.c tests/calc_core/test_calc_spell.c
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc \
		$(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) tests/calc_core/test_calc_spell.c -lz -lm -o $@

$(OUT)/test-calc-spell-asan: $(OPENCALC_DEPS) src/calc/oc_spell.c tests/calc_core/test_calc_spell.c
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -Ilibowf/calc -Isrc/calc \
		$(OWF_SRC) $(CALC_SRC) $(OPENCALC_SRC) tests/calc_core/test_calc_spell.c -lz -lm -o $@

calc-spell-test: $(OUT)/test-calc-spell
	$(OUT)/test-calc-spell

calc-spell-check: $(OUT)/test-calc-spell-asan
	$(OUT)/test-calc-spell-asan

test: calc-spell-test
check: calc-spell-check

.PHONY: calc-spell-test calc-spell-check

# GFOpenDiag's spelling (src/diag/odg_spell.c) through a page's labels
DIAG_SPELL_DEPS = $(ODG_DEPS) src/diag/odg_spell.c include/opendiag_spell.h tests/diag/test_odg_spell.c

$(OUT)/test-odg-spell: $(DIAG_SPELL_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) $(CFLAGS) -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) \
		$(ODG_SRC) src/diag/odg_spell.c tests/diag/test_odg_spell.c -lz -o $@

$(OUT)/test-odg-spell-asan: $(DIAG_SPELL_DEPS) $(OWF_SRC)
	mkdir -p $(OUT)
	$(CC) $(WARN) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DOWF_HAVE_ZLIB \
		-Iinclude -Ilibowf/include -Ilibowf/src $(OWF_SRC) $(DIAG_SRC) $(ODG_SRC) src/diag/odg_spell.c \
		tests/diag/test_odg_spell.c -lz -o $@

diag-spell-test: $(OUT)/test-odg-spell
	$(OUT)/test-odg-spell

diag-spell-check: $(OUT)/test-odg-spell-asan
	$(OUT)/test-odg-spell-asan

test: diag-spell-test
check: diag-spell-check

.PHONY: diag-spell-test diag-spell-check
