#!/bin/sh
# GFOffice for Cradle's stove_build (target .): Cradle checks out this repository alone and sets STOVE and OUT,
# so this fetches OpenLayout (DalsinAI/openamigalayout, the pages) at main into $OUT/deps, says which commit it
# was, then runs build-os3.sh (OWConvert, OpenWrite and OpenCalc with libowf linked in) and build-library.sh
# (owf.library and OWFLibTest, in $OUT/library). The sample documents go in $OUT/samples, for trying OpenWrite
# in a lab. A local build can still run build-os3.sh itself with OS32_GCC16 and OPENLAYOUT_SRC set.
# MIT, Copyright (c) 2026 AmigaChrome Team (Team 5, 25 October 2026).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/build/os3}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
DEPS=${DEPS:-$OUT/deps}
GIT_BASE=${AMIGACHROME_GIT_BASE:-https://github.com/}
mkdir -p "$DEPS"
if [ ! -d "$DEPS/openamigalayout/.git" ]; then
    git clone -q --depth 1 "${GIT_BASE}DalsinAI/openamigalayout" "$DEPS/openamigalayout" ||
        { echo "build-amiga.sh: couldn't fetch DalsinAI/openamigalayout" >&2; exit 1; }
fi
echo "build-amiga.sh: openamigalayout at $(git -C "$DEPS/openamigalayout" rev-parse --short HEAD)"
OS32_GCC16=$STOVE OPENLAYOUT_SRC="$DEPS/openamigalayout" sh "$HERE/build-os3.sh" "$OUT"
OS32_GCC16=$STOVE sh "$HERE/build-library.sh" "$OUT/library"
mkdir -p "$OUT/samples"
for f in "$HERE"/samples/*.docx "$HERE"/samples/*.odt "$HERE"/samples/*.txt; do
    [ -f "$f" ] && cp "$f" "$OUT/samples/"
done
rm -rf "$DEPS"   # the job's out/ keeps the programs, not the sources
