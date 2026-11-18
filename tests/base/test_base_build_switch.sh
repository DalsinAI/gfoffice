#!/bin/sh
# build.d/base.sh's SQLITE switch, with a compiler that only writes down its arguments:
#   SQLITE unset or static   today's line: -I and -L of the static build, -lsqlite3, no OB_SQLITE_LIBRARY
#   SQLITE=library           -DOB_SQLITE_LIBRARY, the link library's include/ and lib-soft/, -lsqlite3_link,
#                            and no -lsqlite3
#   either, folder missing   GFOpenBase is left out and the build goes on
#   SQLITE=library, SQLite's code in the program all the same: the build stops
#   SQLITE=other             the build stops
# usage: tests/base/test_base_build_switch.sh   (from the repository's top)
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
checks=0; failed=0
check() { checks=$((checks + 1)); if ! eval "$2"; then failed=$((failed + 1)); echo "FAIL: $1"; fi; }

# the compiler: its arguments to $W/args, and the file after -o holding $FAKE_TEXT
cat > "$W/cc" <<'CC'
#!/bin/sh
printf '%s\n' "$@" > "$ARGS"
out=""
while [ $# -gt 0 ]; do [ "$1" = -o ] && out=$2; shift; done
[ -n "$out" ] && printf '%s\n' "${FAKE_TEXT:-a program}" > "$out"
exit 0
CC
chmod +x "$W/cc"
mkdir -p "$W/static/lib" "$W/static/include" "$W/library/lib-soft" "$W/library/include" "$W/out"
: > "$W/static/lib/libsqlite3.a"; : > "$W/static/include/sqlite3.h"
: > "$W/library/lib-soft/libsqlite3_link.a"; : > "$W/library/include/sqlite3.h"; : > "$W/library/include/sqlite3_link.h"

# build.d/base.sh as build-os3.sh sources it; $1 SQLITE ("" unset), $2 OPENSQLITE, $3 OPENSQLITE_LIB
run() {
    rm -f "$W/args" "$W/out/GFOpenBase"
    (
        [ -n "$1" ] && SQLITE=$1
        OPENSQLITE=$2 OPENSQLITE_LIB=$3
        ARGS=$W/args; export ARGS
        CC=$W/cc CFLAGS_COMMON="" SPECS=specs OUT=$W/out OWF=linked OWFFLAGS="" OWF_WRITE="" SPELL="" SPELL_SRC=""
        OPENLAYOUT_SRC=$W/ol OL_PAGES="" OL_PAGES_FLAGS="" OWFSRC_DOCS=""
        . "$HERE/build.d/base.sh"
    ) > "$W/log" 2>&1
}
has() { grep -qxF -- "$1" "$W/args"; }

rc=0; run "" "$W/static" "$W/library" || rc=$?
check "default: builds" '[ $rc = 0 ] && [ -f "$W/out/GFOpenBase" ]'
check "default: static include" 'has "-I$W/static/include"'
check "default: -lsqlite3 from the static folder" 'has "-L$W/static/lib" && has "-lsqlite3"'
check "default: not the library" '! has "-DOB_SQLITE_LIBRARY" && ! has "-lsqlite3_link"'
check "default: the helper is compiled" 'has "$HERE/app/ob_sqlitelib.c"'
check "default: says which" 'grep -q "SQLITE=static" "$W/log"'
cp "$W/args" "$W/args-default"

rc=0; run static "$W/static" "$W/library" || rc=$?
check "static: the same line as the default" '[ $rc = 0 ] && cmp -s "$W/args" "$W/args-default"'

rc=0; run library "$W/static" "$W/library" || rc=$?
check "library: builds" '[ $rc = 0 ] && [ -f "$W/out/GFOpenBase" ]'
check "library: OB_SQLITE_LIBRARY and the link library include" 'has "-DOB_SQLITE_LIBRARY" && has "-I$W/library/include"'
check "library: -lsqlite3_link, soft float" 'has "-L$W/library/lib-soft" && has "-lsqlite3_link"'
check "library: no static SQLite" '! has "-lsqlite3" && ! has "-I$W/static/include" && ! has "-L$W/static/lib"'
check "library: says which" 'grep -q "SQLITE=library" "$W/log"'

rc=0; FAKE_TEXT="SQLite format 3" run library "$W/static" "$W/library" || rc=$?
check "library: SQLite's code in the program stops the build" '[ $rc != 0 ] && grep -q "linked in" "$W/log"'
rc=0; FAKE_TEXT="SQLite format 3" run static "$W/static" "$W/library" || rc=$?
check "static: SQLite's code in the program is as it should be" '[ $rc = 0 ]'

rc=0; run static "$W/nowhere" "$W/library" || rc=$?
check "static: no folder, left out, build goes on" '[ $rc = 0 ] && [ ! -f "$W/out/GFOpenBase" ] && grep -q "left out" "$W/log"'
rc=0; run library "$W/static" "$W/nowhere" || rc=$?
check "library: no folder, left out, build goes on" '[ $rc = 0 ] && [ ! -f "$W/out/GFOpenBase" ] && grep -q "left out" "$W/log"'
check "library: no folder, names build-library.sh" 'grep -q "build-library.sh" "$W/log"'
rm "$W/library/include/sqlite3_link.h"
rc=0; run library "$W/static" "$W/library" || rc=$?
check "library: no sqlite3_link.h, left out" '[ $rc = 0 ] && [ ! -f "$W/out/GFOpenBase" ]'

rc=0; run dynamic "$W/static" "$W/library" || rc=$?
check "another word: the build stops" '[ $rc != 0 ] && grep -q "neither static nor library" "$W/log"'

if [ $failed = 0 ]; then echo "BASE BUILD SWITCH PASS: $checks checks"; else echo "BASE BUILD SWITCH FAIL: $failed of $checks"; exit 1; fi
