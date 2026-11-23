#!/bin/sh
# The GFOffice release package from a build's out/ (build-amiga.sh's OUT): the GFOffice drawer as
# GFOffice-<version>-AmigaOS3, with the programs and their icons, library/ (the libraries for LIBS: and the owf
# libraries, with their tests and what the tests read), sdk/, Images, Rexx, samples, Dictionaries, Documentation,
# the release notes and SHA256SUMS,
# then the .lha and its SHA-256 where there is an lha to pack with.
#
#   candidate/VERSION              one line: the version the package is named for (as 2.0.25-rc93)
#   candidate/RELEASE-NOTES.txt    goes in the package and beside the .lha
#
# usage: scripts/package.sh OUT [DEST]     DEST defaults to OUT/release
#
# The layout is RC92's (it was packed by a script outside the repository until 23 November 2026).
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
O=${1:?usage: package.sh OUT [DEST]}
DEST=${2:-$O/release}
[ -f "$HERE/candidate/VERSION" ] || { echo "package.sh: no candidate/VERSION" >&2; exit 1; }
VERSION=$(head -n 1 "$HERE/candidate/VERSION" | tr -d '[:space:]')
case "$VERSION" in
    [0-9]*.[0-9]*) ;;
    *) echo "package.sh: candidate/VERSION does not begin with a version (as 2.0.25-rc93)" >&2; exit 1 ;;
esac
case "$VERSION" in
    *[!0-9A-Za-z.-]*) echo "package.sh: the version may hold letters, digits, '.' and '-' only" >&2; exit 1 ;;
esac
NOTES=$HERE/candidate/RELEASE-NOTES.txt
[ -f "$NOTES" ] || { echo "package.sh: no candidate/RELEASE-NOTES.txt" >&2; exit 1; }
NAME=GFOffice-$VERSION-AmigaOS3
P=$DEST/$NAME
rm -rf "$P" "$DEST/$NAME.info" "$DEST/$NAME.lha" "$DEST/$NAME.lha.sha256"
mkdir -p "$P"
need() { [ -e "$1" ] || { echo "package.sh: the build has no ${1#"$O"/}" >&2; exit 1; }; }
for a in GFOpenWrite GFOpenCalc GFOpenPresentation GFOpenPressmaster GFOpenBase GFOpenDiag GFOpenProject \
         GFOpenDraw GFOpenDrawOL; do
    need "$O/$a"; need "$O/$a.info"
    cp "$O/$a" "$O/$a.info" "$P/"
done
need "$O/OWConvert"
cp "$O/OWConvert" "$P/"
mkdir -p "$P/library/tests"
# the three the programs open, which the installer's step copies to LIBS:, must be there
for l in gfopenlayout sqlite gfopendraw owf owfspell; do need "$O/library/$l.library"; done
cp "$O"/library/*.library "$P/library/"
for t in OWFLibTest OWFLibsTest OWFSpellTest; do
    need "$O/library/tests/$t"
    cp "$O/library/tests/$t" "$P/library/tests/"
done
# what the three tests read, beside them as build-library.sh lays it out: without these OWFSpellTest fails 5 of
# its checks and the library tests every check that reads a file (RC93's first package, in a lab)
for d in Dictionaries fixtures samples; do
    need "$O/library/tests/$d"
    cp -R "$O/library/tests/$d" "$P/library/tests/"
done
# the word list the programs' spelling reads, PROGDIR:Dictionaries/en_GB.words (docs/suite/owf-spell.md)
mkdir -p "$P/Dictionaries"
cp "$HERE/Dictionaries/en_GB.words" "$P/Dictionaries/"
for d in Images Rexx samples; do need "$O/$d"; cp -R "$O/$d" "$P/"; done
mkdir -p "$P/sdk/gfoffice"
cp -R "$HERE/libowf/library/sdk/FD" "$HERE/libowf/library/sdk/Include_H" "$HERE/libowf/library/sdk/Include_I" \
    "$P/sdk/gfoffice/"
need "$O/sdk/gfopendraw"
cp -R "$O/sdk/gfopendraw" "$P/sdk/"
D=$P/Documentation
mkdir -p "$D"
for f in "$HERE"/docs/parity/*.md; do
    b=$(basename "$f" .md)
    if [ "$b" = README ]; then cp "$f" "$D/Parity-README.md"; else cp "$f" "$D/Parity-$b.md"; fi
done
cp "$HERE/docs/openoffice-parity-plan.md" "$D/OpenOffice-Parity-Plan.md"
cp "$HERE/docs/suite/owf-libraries.md" "$D/OWF-Libraries.md"
cp "$HERE/docs/suite/layout-library.md" "$D/Layout-Library.md"
cp "$HERE/docs/FORMATS.md" "$D/Formats.md"
cp "$HERE/docs/CALC_ENGINE.md" "$D/Calc-Engine.md"
# drawer icons (docs/ICONS.md): each beside its drawer; GFOffice.info beside the GFOffice drawer, not in it
for d in samples library sdk Documentation Rexx Images; do cp "$HERE/icons/drawers/$d.info" "$P/"; done
cp "$HERE/icons/drawers/GFOffice.info" "$DEST/$NAME.info"
cp "$NOTES" "$P/RELEASE-NOTES.txt"
# the install step as a script: the four libraries the programs open, into LIBS: (Execute Install-Libraries)
cp "$HERE/scripts/Install-Libraries" "$P/Install-Libraries"
# what built it, when the build wrote it down (build-rc.sh)
[ -f "$O/BUILD-INFO.txt" ] && cp "$O/BUILD-INFO.txt" "$P/"
(cd "$P" && find . -type f ! -name SHA256SUMS | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
cp "$NOTES" "$DEST/RELEASE-NOTES.txt"
echo "package.sh: $NAME, $(find "$P" -type f | wc -l | tr -d ' ') files"
if command -v lha >/dev/null 2>&1; then
    (cd "$DEST" && lha -aq "$NAME.lha" "$NAME" "$NAME.info" >/dev/null)
    (cd "$DEST" && sha256sum "$NAME.lha" > "$NAME.lha.sha256")
    echo "package.sh: $DEST/$NAME.lha ($(wc -c < "$DEST/$NAME.lha") bytes)"
else
    # no lha here: the drawer is whole in DEST, and a .tar of it travels to a machine that has lha
    (cd "$DEST" && tar -cf "$NAME.tar" "$NAME" "$NAME.info")
    echo "package.sh: no lha on this machine; $DEST/$NAME.tar holds the drawer for packing elsewhere"
fi
