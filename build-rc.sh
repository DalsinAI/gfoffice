#!/bin/sh
# A GFOffice release candidate in one kitchen job (stove_build, target ., script build-rc.sh): build-amiga.sh,
# then what built it in OUT/BUILD-INFO.txt, then scripts/package.sh, which leaves the package in OUT/release.
# The version is release/VERSION and the notes release/RELEASE-NOTES.txt. Publishing is not done here.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/build/os3}
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
export OUT STOVE
sh "$HERE/build-amiga.sh"
{
    echo "GFOffice $(head -n 1 "$HERE/release/VERSION" | tr -d '[:space:]')"
    echo "Source: $(git -C "$HERE" rev-parse HEAD 2>/dev/null || echo unknown)"
    echo "Stove: $(basename "$STOVE")"
    echo "Compiler: $("$STOVE/prefix/bin/m68k-amigaos-gcc" --version | head -n 1)"
    for r in openamigalayout gfopengadtools gfopendraw gfopensqlite; do
        [ -f "$HERE/deps/$r.ref" ] && echo "deps/$r.ref: $(head -n 1 "$HERE/deps/$r.ref")"
    done
} > "$OUT/BUILD-INFO.txt"
cat "$OUT/BUILD-INFO.txt"
sh "$HERE/scripts/package.sh" "$OUT"
