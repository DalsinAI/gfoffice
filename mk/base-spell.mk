# GFOpenBase's spelling through a form's own words (src/base/spell/ob_spell.c, openbase_spell.h): host tests.
#   make base-spell-test    tests/base/test_base_spell.c against the pinned SQLite and libowf
#   make base-spell-check   the same under AddressSanitizer and UBSan
# The SQLite object and BASE_SRC come from mk/base.mk, read after this file: the prerequisites name
# src/base/*.c themselves (as mk/base-report.mk does).
# MIT, Copyright (c) 2026 AmiCradle Team.

BASE_SPELL_DEPS = $(OWF_SRC) $(wildcard src/base/*.c) src/base/spell/ob_spell.c include/openbase_core.h \
	include/openbase_spell.h libowf/include/owf_spell.h tests/base/test_base_spell.c
BASE_SPELL_FLAGS = -DOWF_HAVE_ZLIB -Iinclude -Ilibowf/include -Ilibowf/src -I$(SQLITE_SRC)

$(OUT)/test-base-spell: $(BASE_SPELL_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(BASE_SPELL_FLAGS) $(OWF_SRC) $(BASE_SRC) src/base/spell/ob_spell.c \
		tests/base/test_base_spell.c $(OUT)/sqlite3-base.o -lz -lm -o $@

$(OUT)/test-base-spell-asan: $(BASE_SPELL_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(BASE_SPELL_FLAGS) $(OWF_SRC) $(BASE_SRC) src/base/spell/ob_spell.c \
		tests/base/test_base_spell.c $(OUT)/sqlite3-base-asan.o -lz -lm -o $@

base-spell-test: $(OUT)/test-base-spell
	$(OUT)/test-base-spell

base-spell-check: $(OUT)/test-base-spell-asan
	$(OUT)/test-base-spell-asan

test: base-spell-test
check: base-spell-check
.PHONY: base-spell-test base-spell-check
