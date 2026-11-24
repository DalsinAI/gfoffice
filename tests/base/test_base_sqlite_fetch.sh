#!/bin/sh
# scripts/fetch-gfopensqlite.sh over a stand-in release in a folder (RELEASE_BASE=file://...):
#   the release the ref names is fetched and laid out as build.d/base.sh's OPENSQLITE_LIB, with
#   libs/sqlite.library for the package, and build.d/base.sh then builds GFOpenBase from that folder;
#   a .zip whose SHA-256 is not the ref's, a release that is not there, a .zip with a file missing and a
#   ref with no sum each stop it, and none of them leaves a folder a build would take;
#   the vault: a release on a stand-in kitchen shelf (SHELF=..., or with no SHELF the one beside the job's
#   stove) is taken from there with no RELEASE_BASE to reach, a shelf without that version falls back to
#   RELEASE_BASE, and a shelf .zip whose SHA-256 is not the ref's stops it;
#   deps/gfopensqlite.ref itself is a tag and a SHA-256.
# usage: tests/base/test_base_sqlite_fetch.sh   (from the repository's top)
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "${W:?}"' EXIT
checks=0; failed=0
check() { checks=$((checks + 1)); if ! eval "$2"; then failed=$((failed + 1)); echo "FAIL: $1"; fi; }

# a release: $1 its tag, $2 "nosoft" to leave the soft-float link library out
release() {
    P=$W/pack-$1
    G=$P/GFOpenSQLite
    mkdir -p "$G/Libs" "$G/C" "$G/SDK/lib/soft-float" "$G/SDK/Include/libraries" "$G/SDK/Include/inline" "$G/SDK/Include/proto"
    echo "the library" > "$G/Libs/sqlite.library"
    echo test > "$G/C/SQLiteLibTest"
    echo hard > "$G/SDK/lib/libsqlite3_link.a"
    [ "$2" = nosoft ] || echo soft > "$G/SDK/lib/soft-float/libsqlite3_link.a"
    echo h > "$G/SDK/Include/sqlite3.h"; echo link > "$G/SDK/Include/sqlite3_link.h"
    for d in libraries inline proto; do echo "$d" > "$G/SDK/Include/$d/sqlite.h"; done
    mkdir -p "$W/rel/$1"
    (cd "$P" && zip -qr "$W/rel/$1/GFOpenSQLite-${1#v}.zip" GFOpenSQLite)
    sha256sum "$W/rel/$1/GFOpenSQLite-${1#v}.zip" | cut -d' ' -f1
}
# SHELF points at an empty folder, so these fetches never find a kitchen's real shelf
fetch() { REF=$W/ref SHELF=$W/noshelf RELEASE_BASE=file://$W/rel sh "$HERE/scripts/fetch-gfopensqlite.sh" "$W/dest$N" > "$W/log" 2>&1; }
nothing() { [ ! -e "$W/dest$N/lib-soft/libsqlite3_link.a" ] && [ ! -e "$W/dest$N/libs/sqlite.library" ]; }

N=1
SUM=$(release v9.9.9-1 "")
echo "v9.9.9-1 $SUM" > "$W/ref"
rc=0; fetch || rc=$?
D=$W/dest1
check "fetches" '[ $rc = 0 ] && grep -q "v9.9.9-1" "$W/log"'
check "the soft-float link library" '[ "$(cat "$D/lib-soft/libsqlite3_link.a")" = soft ]'
check "the headers, with their drawers" '[ "$(cat "$D/include/sqlite3.h")" = h ] && [ "$(cat "$D/include/sqlite3_link.h")" = link ] && [ "$(cat "$D/include/proto/sqlite.h")" = proto ] && [ "$(cat "$D/include/inline/sqlite.h")" = inline ] && [ "$(cat "$D/include/libraries/sqlite.h")" = libraries ]'
check "the library, for the package" '[ "$(cat "$D/libs/sqlite.library")" = "the library" ]'

# build.d/base.sh takes that folder as it is (a compiler that only writes its arguments down)
cat > "$W/cc" <<'CC'
#!/bin/sh
printf '%s\n' "$@" > "$ARGS"
out=""
while [ $# -gt 0 ]; do [ "$1" = -o ] && out=$2; shift; done
[ -n "$out" ] && echo program > "$out"
exit 0
CC
chmod +x "$W/cc"; mkdir -p "$W/out"
rc=0
(
    OPENSQLITE=$W/nowhere OPENSQLITE_LIB=$D
    ARGS=$W/args; export ARGS
    CC=$W/cc CFLAGS_COMMON="" SPECS=specs OUT=$W/out OWF=linked OWFFLAGS="" OWF_WRITE="" SPELL="" SPELL_SRC=""
    OPENLAYOUT_SRC=$W/ol OL_C="" OL_PAGES="" OL_PAGES_FLAGS="" OWFSRC_DOCS=""
    . "$HERE/build.d/base.sh"
) > "$W/buildlog" 2>&1 || rc=$?
check "build.d/base.sh builds GFOpenBase from it" '[ $rc = 0 ] && [ -f "$W/out/GFOpenBase" ] && grep -qxF -- "-L$D/lib-soft" "$W/args" && grep -qxF -- "-lsqlite3_link" "$W/args"'

echo changed > "$D/libs/sqlite.library"
rc=0; fetch || rc=$?
check "a second fetch puts the release's files back" '[ $rc = 0 ] && [ "$(cat "$D/libs/sqlite.library")" = "the library" ]'

# each refusal into a folder of its own, which must stay without the files a build takes
N=2
echo "v9.9.9-1 0000000000000000000000000000000000000000000000000000000000000000" > "$W/ref"
rc=0; fetch || rc=$?
check "another SHA-256 stops it" '[ $rc != 0 ] && grep -q "SHA-256 differs" "$W/log" && nothing'
N=3
echo "v9.9.9-2 $SUM" > "$W/ref"
rc=0; fetch || rc=$?
check "a release that is not there stops it" '[ $rc != 0 ] && grep -q "fetch" "$W/log" && nothing'
N=4
SUM3=$(release v9.9.9-3 nosoft)
echo "v9.9.9-3 $SUM3" > "$W/ref"
rc=0; fetch || rc=$?
check "a release with no soft-float link library stops it" '[ $rc != 0 ] && grep -q "has no SDK/lib/soft-float/libsqlite3_link.a" "$W/log" && nothing'
N=5
echo "v9.9.9-1" > "$W/ref"
rc=0; fetch || rc=$?
check "a ref with no sum stops it" '[ $rc != 0 ] && grep -q "no SHA-256" "$W/log" && nothing'
N=6
echo "main $SUM" > "$W/ref"
rc=0; fetch || rc=$?
check "a ref that names no release stops it" '[ $rc != 0 ] && grep -q "release tag" "$W/log" && nothing'
N=7
echo "v9.9.9-1/../x $SUM" > "$W/ref"
rc=0; fetch || rc=$?
check "a tag with a path in it stops it" '[ $rc != 0 ] && nothing'

# the vault: a stand-in kitchen shelf, laid out as the kitchen keeps an ingredient (<version>/files/...)
SH=$W/shelf
mkdir -p "$SH/9.9.9-1/files"
cp "$W/rel/v9.9.9-1/GFOpenSQLite-9.9.9-1.zip" "$SH/9.9.9-1/files/"
vault() { REF=$W/ref SHELF=$SH RELEASE_BASE=$1 sh "$HERE/scripts/fetch-gfopensqlite.sh" "$W/dest$N" > "$W/log" 2>&1; }
N=8
echo "v9.9.9-1 $SUM" > "$W/ref"
rc=0; vault file://$W/nowhere || rc=$?
check "the release is taken from the shelf, with no RELEASE_BASE to reach" '[ $rc = 0 ] && grep -q "from the shelf" "$W/log" && [ "$(cat "$W/dest8/libs/sqlite.library")" = "the library" ] && [ "$(cat "$W/dest8/lib-soft/libsqlite3_link.a")" = soft ]'
N=9
SUM4=$(release v9.9.9-4 "")
echo "v9.9.9-4 $SUM4" > "$W/ref"
rc=0; vault file://$W/rel || rc=$?
check "a version the shelf hasn't comes from RELEASE_BASE" '[ $rc = 0 ] && grep -q "from file://$W/rel" "$W/log" && [ "$(cat "$W/dest9/libs/sqlite.library")" = "the library" ]'
N=10
echo "not the release" > "$SH/9.9.9-1/files/GFOpenSQLite-9.9.9-1.zip"
echo "v9.9.9-1 $SUM" > "$W/ref"
rc=0; vault file://$W/rel || rc=$?
check "a shelf .zip that is not the ref's stops it" '[ $rc != 0 ] && grep -q "from the shelf.*SHA-256 differs" "$W/log" && nothing'
N=11
echo "v9.9.9-2 $SUM" > "$W/ref"
rc=0; vault file://$W/nowhere || rc=$?
check "on neither the shelf nor RELEASE_BASE stops it" '[ $rc != 0 ] && grep -q "is not on the shelf" "$W/log" && nothing'
N=12
# no SHELF: the kitchen's shelf is found beside the job's stove (KITCHEN_ROOT/stoves/<stove>, KITCHEN_ROOT/shelf)
K=$W/kitchen
mkdir -p "$K/stoves/os32" "$K/shelf/amicradle/gfopensqlite/9.9.9-1/files"
cp "$W/rel/v9.9.9-1/GFOpenSQLite-9.9.9-1.zip" "$K/shelf/amicradle/gfopensqlite/9.9.9-1/files/"
echo "v9.9.9-1 $SUM" > "$W/ref"
rc=0; (unset SHELF; REF=$W/ref STOVE=$K/stoves/os32 RELEASE_BASE=file://$W/nowhere sh "$HERE/scripts/fetch-gfopensqlite.sh" "$W/dest$N" > "$W/log" 2>&1) || rc=$?
check "with no SHELF, the shelf beside the job's stove" '[ $rc = 0 ] && grep -qF "from the shelf ($K/shelf/amicradle/gfopensqlite/9.9.9-1)" "$W/log" && [ "$(cat "$W/dest12/libs/sqlite.library")" = "the library" ]'

read -r T S REST < "$HERE/deps/gfopensqlite.ref" || true
check "deps/gfopensqlite.ref: a tag and a SHA-256, nothing more" 'case "$T" in v[0-9]*) [ ${#S} = 64 ] && [ -z "$REST" ] && [ "$(wc -l < "$HERE/deps/gfopensqlite.ref")" = 1 ] ;; *) false ;; esac'
check "build-amiga.sh fetches it and ships the library" 'grep -q "scripts/fetch-gfopensqlite.sh" "$HERE/build-amiga.sh" && grep -q "libs/sqlite.library\" \"\$OUT/library/sqlite.library\"" "$HERE/build-amiga.sh"'

if [ $failed = 0 ]; then echo "BASE SQLITE FETCH PASS: $checks checks"; else echo "BASE SQLITE FETCH FAIL: $failed of $checks"; exit 1; fi
