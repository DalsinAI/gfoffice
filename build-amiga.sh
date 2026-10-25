#!/bin/sh
# GFOffice for Cradle's stove_build (target .): Cradle checks out this repository alone and sets STOVE and OUT,
# so this fetches OpenLayout (DalsinAI/openamigalayout, the pages) into $OUT/deps at the commit
# deps/openamigalayout.ref names (one line: a commit SHA, or a branch such as master), says which commit it
# was, then runs build-os3.sh (OWConvert, OpenWrite and OpenCalc with libowf linked in), build-library.sh
# (GFOffice's libraries, owf.library 2 and their tests, in $OUT/library) and build-os3.sh again for the programs
# on those libraries (OWF=library, in $OUT/onlib: docs/suite/owf-libraries.md). The sample documents go in
# $OUT/samples, for trying OpenWrite in a lab. A local build can still run build-os3.sh itself with OS32_GCC16
# and OPENLAYOUT_SRC set.
# MIT, Copyright (c) 2026 AmigaChrome Team (Team 5, 25 October 2026).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/build/os3}
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
DEPS=${DEPS:-$OUT/deps}
GIT_BASE=${AMIGACHROME_GIT_BASE:-https://github.com/}
mkdir -p "$DEPS"
# OpenLayout pinned (Team 5, 25 October 2026): a build of a GFOffice commit always takes the same OpenLayout,
# and a GFOffice pull request that needs an OpenLayout one names that one's head here, to be checked together.
REF=master
[ -f "$HERE/deps/openamigalayout.ref" ] && REF=$(head -n 1 "$HERE/deps/openamigalayout.ref" | tr -d '[:space:]')
[ -n "$REF" ] || REF=master
if [ ! -d "$DEPS/openamigalayout/.git" ]; then
    git init -q "$DEPS/openamigalayout" &&
        git -C "$DEPS/openamigalayout" fetch -q --depth 1 "${GIT_BASE}DalsinAI/openamigalayout" "$REF" &&
        git -C "$DEPS/openamigalayout" checkout -q FETCH_HEAD ||
        { echo "build-amiga.sh: couldn't fetch DalsinAI/openamigalayout at $REF" >&2; exit 1; }
fi
echo "build-amiga.sh: openamigalayout at $(git -C "$DEPS/openamigalayout" rev-parse --short HEAD) ($REF)"
OS32_GCC16=$STOVE OPENLAYOUT_SRC="$DEPS/openamigalayout" sh "$HERE/build-os3.sh" "$OUT"
OS32_GCC16=$STOVE sh "$HERE/build-library.sh" "$OUT/library"
OS32_GCC16=$STOVE OPENLAYOUT_SRC="$DEPS/openamigalayout" OWF=library OWF_BUILD_LIBRARIES=no \
    sh "$HERE/build-os3.sh" "$OUT/onlib"
mkdir -p "$OUT/samples"
for f in "$HERE"/samples/*.docx "$HERE"/samples/*.odt "$HERE"/samples/*.txt \
         "$HERE"/tests/book/fixtures/features-openpyxl.xlsx "$HERE"/tests/book/fixtures/charts-openpyxl.xlsx \
         "$HERE"/tests/core/fixtures/*.docx; do
    [ -f "$f" ] && cp "$f" "$OUT/samples/"   # the workbooks (OpenCalc's charts and the rest) and the layout
                                             # fixtures (covers, contents, text round frames), for a lab
done
# and presentations for OpenPresentation: the deck tests' fixtures, made here (tests/deck/make_*fixtures.py)
for f in pptx-talk.pptx pptx-plain.pptx odfpy.odp libreoffice.odp; do
    [ -f "$HERE/tests/deck/fixtures/$f" ] && cp "$HERE/tests/deck/fixtures/$f" "$OUT/samples/"
done
rm -rf "$DEPS"   # the job's out/ keeps the programs, not the sources
