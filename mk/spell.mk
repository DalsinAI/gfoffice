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
