#!/bin/sh
# build.d/calc-extdb.sh: which SQLite GFOpenCalc's External Data is built with (the SQLITE switch it shares with
# GFOpenBase), with made-up gfopensqlite folders:
#   SQLITE unset or library  OC_EXTDATA_DATABASES and OB_SQLITE_LIBRARY, the link library's include/ and
#                            lib-soft/, -lsqlite3_link, no -lsqlite3, and app/ob_sqlitelib.c compiled
#   SQLITE=static            the static build's include/ and -lsqlite3, no OB_SQLITE_LIBRARY
#   either, folder missing   no databases (web pages only), and the build goes on
#   SQLITE=library, SQLite's code in GFOpenCalc all the same: oc_extdb_verify stops the build
#   SQLITE=other             oc_extdb_setup stops the build
#   build-os3.sh sources it and calls both around GFOpenCalc
# usage: tests/calc_core/test_calc_extdb_switch.sh   (from anywhere)
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
checks=0; failed=0
check() { checks=$((checks + 1)); if ! eval "$2"; then failed=$((failed + 1)); echo "FAIL: $1"; fi; }

mkdir -p "$W/static/lib" "$W/static/include" "$W/library/lib-soft" "$W/library/include"
: > "$W/static/lib/libsqlite3.a"; : > "$W/static/include/sqlite3.h"
: > "$W/library/lib-soft/libsqlite3_link.a"; : > "$W/library/include/sqlite3.h"; : > "$W/library/include/sqlite3_link.h"

# oc_extdb_setup as build-os3.sh calls it; $1 SQLITE ("" unset), $2 OPENSQLITE, $3 OPENSQLITE_LIB. The words of
# OC_EXTDB and OC_EXTDB_LIB go to $W/words, a line each; what it said to $W/log.
run() {
    rm -f "$W/words"
    (
        unset SQLITE
        [ -n "$1" ] && SQLITE=$1
        OPENSQLITE=$2 OPENSQLITE_LIB=$3
        . "$HERE/build.d/calc-extdb.sh"
        oc_extdb_setup || exit 1
        for w in $OC_EXTDB $OC_EXTDB_LIB; do printf '%s\n' "$w"; done > "$W/words"
    ) > "$W/log" 2>&1
}
has() { grep -qxF -- "$1" "$W/words"; }

rc=0; run "" "$W/static" "$W/library" || rc=$?
check "default: on the library" '[ $rc = 0 ] && has "-DOB_SQLITE_LIBRARY" && has "-DOC_EXTDATA_DATABASES"'
check "default: the link library's include and soft-float lib" 'has "-I$W/library/include" && has "-L$W/library/lib-soft" && has "-lsqlite3_link"'
check "default: no static SQLite" '! has "-lsqlite3" && ! has "-I$W/static/include" && ! has "-L$W/static/lib"'
check "default: oc_extdb.c, GFOpenBase's core and the library opener" 'has "$HERE/src/calc/extdb/oc_extdb.c" && has "$HERE/src/base/ob_core.c" && has "$HERE/app/ob_sqlitelib.c"'
check "default: says which" 'grep -q "through sqlite.library" "$W/log"'
cp "$W/words" "$W/words-default"
rc=0; run "" "$W/nowhere" "$W/library" || rc=$?
check "default: needs no static build" '[ $rc = 0 ] && has "-lsqlite3_link"'

rc=0; run library "$W/static" "$W/library" || rc=$?
check "library: the same as the default" '[ $rc = 0 ] && cmp -s "$W/words" "$W/words-default"'

rc=0; run static "$W/static" "$W/library" || rc=$?
check "static: databases, linked in" '[ $rc = 0 ] && has "-DOC_EXTDATA_DATABASES" && ! has "-DOB_SQLITE_LIBRARY"'
check "static: the static build" 'has "-I$W/static/include" && has "-L$W/static/lib" && has "-lsqlite3" && ! has "-lsqlite3_link"'
check "static: the opener is compiled (it does nothing then)" 'has "$HERE/app/ob_sqlitelib.c"'

rc=0; run library "$W/static" "$W/nowhere" || rc=$?
check "library: no folder, web pages only, build goes on" '[ $rc = 0 ] && [ ! -s "$W/words" ] && grep -q "web pages only" "$W/log"'
rm "$W/library/include/sqlite3_link.h"
rc=0; run library "$W/static" "$W/library" || rc=$?
check "library: no sqlite3_link.h, web pages only" '[ $rc = 0 ] && [ ! -s "$W/words" ]'
: > "$W/library/include/sqlite3_link.h"
rc=0; run static "$W/nowhere" "$W/library" || rc=$?
check "static: no folder, web pages only, build goes on" '[ $rc = 0 ] && [ ! -s "$W/words" ] && grep -q "web pages only" "$W/log"'

rc=0; run dynamic "$W/static" "$W/library" || rc=$?
check "another word: the build stops" '[ $rc != 0 ] && grep -q "neither static nor library" "$W/log"'

# oc_extdb_verify: $1 SQLITE, $2 the program's text
verify() {
    printf '%s\n' "$2" > "$W/GFOpenCalc"
    (
        unset SQLITE
        SQLITE=$1 OPENSQLITE=$W/static OPENSQLITE_LIB=$W/library
        . "$HERE/build.d/calc-extdb.sh"
        oc_extdb_setup > /dev/null
        oc_extdb_verify "$W/GFOpenCalc"
    ) > "$W/log" 2>&1
}
rc=0; verify library "malformed database schema (%s)" || rc=$?
check "library: SQLite's code in GFOpenCalc stops the build" '[ $rc != 0 ] && grep -q "linked in" "$W/log"'
rc=0; verify library "SQLite format 3" || rc=$?
check "library: libowf's own copy of the file header's text does not" '[ $rc = 0 ]'
rc=0; verify static "malformed database schema (%s)" || rc=$?
check "static: SQLite's code in GFOpenCalc is as it should be" '[ $rc = 0 ]'

# sourcing it again (build-os3.sh's build.d loop) builds nothing and says nothing
out=$(OUT=$W/none sh -c '. "$1"' sh "$HERE/build.d/calc-extdb.sh" 2>&1)
check "sourced on its own: no output" '[ -z "$out" ]'
check "build-os3.sh sources it and calls both around GFOpenCalc" \
    'grep -q "build.d/calc-extdb.sh" "$HERE/build-os3.sh" && grep -q "^oc_extdb_setup" "$HERE/build-os3.sh" && grep -q "^oc_extdb_verify \"\$OUT/GFOpenCalc\"" "$HERE/build-os3.sh"'
check "build-os3.sh links GFOpenCalc with OC_EXTDB_LIB" 'grep -q "OC_EXTDB_LIB -lamiga" "$HERE/build-os3.sh"'

if [ $failed = 0 ]; then echo "CALC EXTDB SWITCH PASS: $checks checks"; else echo "CALC EXTDB SWITCH FAIL: $failed of $checks"; exit 1; fi
