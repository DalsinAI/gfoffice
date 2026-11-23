#!/bin/sh
# scripts/package.sh on a made-up build: the package's layout, its name from candidate/VERSION, the checksums,
# and that a build with a program or a LIBS: library missing is refused; then scripts/fetch-gfopensqlite.sh
# on a made-up release, with unzip and without it (Python's zipfile).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
PASS=0
FAIL=0
ok() { if [ "$2" = 0 ]; then PASS=$((PASS + 1)); else FAIL=$((FAIL + 1)); echo "FAIL $1"; fi; }
VERSION=$(head -n 1 "$HERE/candidate/VERSION" | tr -d '[:space:]')
NAME=GFOffice-$VERSION-AmigaOS3

mkout() {
    o=$1
    rm -rf "$o"
    mkdir -p "$o/library/tests" "$o/Images/Toolbar" "$o/Rexx" "$o/samples" "$o/sdk/gfopendraw/Include_H"
    for a in GFOpenWrite GFOpenCalc GFOpenPresentation GFOpenPressmaster GFOpenBase GFOpenDiag GFOpenProject \
             GFOpenDraw GFOpenDrawOL OWConvert; do
        echo "$a" > "$o/$a"; echo icon > "$o/$a.info"
    done
    for l in gfopenlayout sqlite gfopendraw owf owfwrite owfcalc owfpresent owfpub owfdiag owfspell; do
        echo "$l" > "$o/library/$l.library"
    done
    for t in OWFLibTest OWFLibsTest OWFSpellTest; do echo "$t" > "$o/library/tests/$t"; done
    mkdir -p "$o/library/tests/Dictionaries" "$o/library/tests/fixtures" "$o/library/tests/samples"
    echo w > "$o/library/tests/Dictionaries/en_GB.words"; echo f > "$o/library/tests/fixtures/book.xls"
    echo d > "$o/library/tests/samples/OpenWrite-Tour.docx"
    echo p > "$o/Images/Toolbar/new.ilbm"; echo r > "$o/Rexx/a.rexx"; echo s > "$o/samples/a.docx"
    echo h > "$o/sdk/gfopendraw/Include_H/x.h"
    echo "Compiler: test" > "$o/BUILD-INFO.txt"
}

case "$VERSION" in [0-9]*.[0-9]*.[0-9]*-rc[0-9]*|[0-9]*.[0-9]*.[0-9]*) r=0 ;; *) r=1 ;; esac
ok "candidate/VERSION is a version" $r
# in a checkout, both are files git has (release/ is in .gitignore: a version kept there never reached a build)
if git -C "$HERE" rev-parse --git-dir > /dev/null 2>&1; then
    git -C "$HERE" ls-files --error-unmatch candidate/VERSION candidate/RELEASE-NOTES.txt > /dev/null 2>&1
    ok "candidate/VERSION and the notes are in the repository" $?
fi
grep -q "$VERSION" "$HERE/candidate/RELEASE-NOTES.txt"; ok "the release notes name candidate/VERSION" $?

mkout "$W/out"
sh "$HERE/scripts/package.sh" "$W/out" "$W/rel" > "$W/log" 2>&1; ok "package.sh runs" $?
P=$W/rel/$NAME
for f in GFOpenWrite GFOpenWrite.info GFOpenDrawOL OWConvert library/gfopenlayout.library library/sqlite.library \
         library/gfopendraw.library library/owfspell.library library/tests/OWFSpellTest Images/Toolbar/new.ilbm \
         library/tests/Dictionaries/en_GB.words library/tests/fixtures/book.xls \
         library/tests/samples/OpenWrite-Tour.docx Dictionaries/en_GB.words \
         Rexx/a.rexx samples/a.docx sdk/gfopendraw/Include_H/x.h sdk/gfoffice/FD Documentation/Formats.md \
         Documentation/Layout-Library.md Documentation/Parity-README.md library.info Documentation.info \
         RELEASE-NOTES.txt BUILD-INFO.txt SHA256SUMS; do
    [ -e "$P/$f" ]; ok "the package has $f" $?
done
[ -f "$W/rel/$NAME.info" ]; ok "the drawer's icon is beside the drawer" $?
[ ! -e "$P/OWConvert.info" ]; ok "OWConvert has no icon" $?
(cd "$P" && sha256sum -c SHA256SUMS > /dev/null 2>&1); ok "SHA256SUMS checks" $?
n=$(find "$P" -type f ! -name SHA256SUMS | wc -l | tr -d ' ')
[ "$n" = "$(wc -l < "$P/SHA256SUMS" | tr -d ' ')" ]; ok "SHA256SUMS lists every file" $?
if command -v lha > /dev/null 2>&1; then
    [ -s "$W/rel/$NAME.lha" ] && (cd "$W/rel" && sha256sum -c "$NAME.lha.sha256" > /dev/null 2>&1)
    ok "the .lha and its sum" $?
else
    [ -s "$W/rel/$NAME.tar" ]; ok "without lha, the .tar of the drawer" $?
fi

mkout "$W/out"; rm "$W/out/GFOpenBase"
sh "$HERE/scripts/package.sh" "$W/out" "$W/rel2" > "$W/log" 2>&1; [ $? != 0 ] && grep -q "no GFOpenBase" "$W/log"
ok "a build without GFOpenBase is refused" $?
mkout "$W/out"; rm -r "$W/out/library/tests/fixtures"
sh "$HERE/scripts/package.sh" "$W/out" "$W/rel4" > "$W/log" 2>&1; [ $? != 0 ] && grep -q "tests/fixtures" "$W/log"
ok "a build without the tests' fixtures is refused" $?
grep -q "owfspell.library LIBS:" "$HERE/candidate/RELEASE-NOTES.txt"; ok "the notes say owfspell.library goes in LIBS:" $?
mkout "$W/out"; rm "$W/out/library/sqlite.library"
sh "$HERE/scripts/package.sh" "$W/out" "$W/rel3" > "$W/log" 2>&1; [ $? != 0 ] && grep -q "sqlite.library" "$W/log"
ok "a build without sqlite.library is refused" $?

# fetch-gfopensqlite.sh: a made-up release behind file://, taken with unzip (when there is one) and without
S=$W/src/GFOpenSQLite
mkdir -p "$S/Libs" "$S/SDK/lib/soft-float" "$S/SDK/Include/libraries" "$S/SDK/Include/inline" "$S/SDK/Include/proto"
echo lib > "$S/Libs/sqlite.library"; echo a > "$S/SDK/lib/soft-float/libsqlite3_link.a"
for h in sqlite3.h sqlite3_link.h libraries/sqlite.h inline/sqlite.h proto/sqlite.h; do echo h > "$S/SDK/Include/$h"; done
mkdir -p "$W/base/v9.9.9"
(cd "$W/src" && python3 -m zipfile -c "$W/base/v9.9.9/GFOpenSQLite-9.9.9.zip" GFOpenSQLite) > /dev/null 2>&1
echo "v9.9.9 $(sha256sum < "$W/base/v9.9.9/GFOpenSQLite-9.9.9.zip" | cut -d' ' -f1)" > "$W/ref"
if command -v unzip > /dev/null 2>&1; then
    REF=$W/ref RELEASE_BASE=file://$W/base sh "$HERE/scripts/fetch-gfopensqlite.sh" "$W/d1" > "$W/log" 2>&1 &&
        [ -f "$W/d1/libs/sqlite.library" ]
    ok "fetch-gfopensqlite with unzip" $?
fi
UNZIP=$W/no-such-unzip REF=$W/ref RELEASE_BASE=file://$W/base sh "$HERE/scripts/fetch-gfopensqlite.sh" "$W/d2" > "$W/log" 2>&1 &&
    [ -f "$W/d2/libs/sqlite.library" ] && [ -f "$W/d2/lib-soft/libsqlite3_link.a" ] && [ -f "$W/d2/include/proto/sqlite.h" ]
ok "fetch-gfopensqlite without unzip" $?

echo "test_package: $PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
