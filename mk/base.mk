# GFOpenBase (stream B, docs/openoffice-parity-plan.md section 4): the core's host tests.
#   make base-test      tests/base against the pinned SQLite
#   make base-check     the same under AddressSanitizer and UBSan
# Both run from make test and make check. SQLite is the amalgamation gfopensqlite pins
# (DalsinAI/gfopensqlite SOURCES), fetched into build/sqlite and checked by SHA-256, never
# committed; SQLITE_SRC names an unpacked copy instead. Built with GFOpenBase's options
# (gfopensqlite build.sh THREADSAFE=0): one connection, one task, no WAL, no mmap.
# MIT, Copyright (c) 2026 AmiCradle Team.

BASE_SQLITE_URL = https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
BASE_SQLITE_SHA = 1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
SQLITE_SRC ?= build/sqlite/sqlite-amalgamation-3530400
BASE_SQLITE_OPTS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_WAL -DSQLITE_MAX_MMAP_SIZE=0 \
	-DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_DEPRECATED -DSQLITE_DQS=0
BASE_SRC = $(wildcard src/base/*.c)
BASE_DEPS = $(BASE_SRC) include/openbase_core.h tests/base/test_base_core.c
BASE_FLAGS = -Iinclude -I$(SQLITE_SRC)

build/sqlite/sqlite-amalgamation-3530400/sqlite3.c:
	mkdir -p build/sqlite
	curl -sSfL -o build/sqlite/sqlite.zip.part $(BASE_SQLITE_URL)
	echo "$(BASE_SQLITE_SHA)  build/sqlite/sqlite.zip.part" | sha256sum -c - >/dev/null
	mv build/sqlite/sqlite.zip.part build/sqlite/sqlite-amalgamation-3530400.zip
	cd build/sqlite && unzip -qo sqlite-amalgamation-3530400.zip && touch sqlite-amalgamation-3530400/sqlite3.c

$(OUT)/sqlite3-base.o: $(SQLITE_SRC)/sqlite3.c
	mkdir -p $(OUT)
	$(CC) -O1 $(BASE_SQLITE_OPTS) -c $< -o $@

$(OUT)/sqlite3-base-asan.o: $(SQLITE_SRC)/sqlite3.c
	mkdir -p $(OUT)
	$(CC) -O1 -g -fsanitize=address -fno-omit-frame-pointer $(BASE_SQLITE_OPTS) -c $< -o $@

$(OUT)/test-base-core: $(BASE_DEPS) $(OUT)/sqlite3-base.o
	$(CC) $(WARN) $(CFLAGS) $(BASE_FLAGS) $(BASE_SRC) tests/base/test_base_core.c $(OUT)/sqlite3-base.o -lm -o $@

$(OUT)/test-base-core-asan: $(BASE_DEPS) $(OUT)/sqlite3-base-asan.o
	$(CC) $(WARN) $(SANITIZE) $(BASE_FLAGS) $(BASE_SRC) tests/base/test_base_core.c $(OUT)/sqlite3-base-asan.o -lm -o $@

base-test: $(OUT)/test-base-core
	$(OUT)/test-base-core

base-check: $(OUT)/test-base-core-asan
	$(OUT)/test-base-core-asan

test: base-test
check: base-check
.PHONY: base-test base-check
