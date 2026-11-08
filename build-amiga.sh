#!/bin/sh
# GFOffice for Cradle's stove_build (target .): Cradle checks out this repository alone and sets STOVE and OUT,
# so this fetches GFOpenLayout (DalsinAI/gfopenlayout, the pages) into $OUT/deps at the commit
# deps/openamigalayout.ref names (one line: a commit SHA, or a branch such as master), says which commit it
# was, then runs build-os3.sh (OWConvert, GFOpenWrite and GFOpenCalc with libowf linked in), build-library.sh
# (GFOffice's libraries, owf.library 2 and their tests, in $OUT/library), build-os3.sh again for the programs
# on those libraries (OWF=library, in $OUT/onlib: docs/suite/owf-libraries.md), and the separately versioned
# GFOpenDraw source pinned in deps/gfopendraw.ref. The sample documents go in $OUT/samples, for trying the
# applications in a lab. A local build can still run build-os3.sh itself with OS32_GCC16 and OPENLAYOUT_SRC set.
# MIT, Copyright (c) 2026 AmiCradle Team (Team 5, 25 October 2026).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-$HERE/build/os3}
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
DEPS=${DEPS:-$OUT/deps}
GIT_BASE=${AMIGACHROME_GIT_BASE:-https://github.com/}
mkdir -p "$DEPS"
# GFOpenLayout pinned (Team 5, 25 October 2026): a build of a GFOffice commit always takes the same GFOpenLayout,
# and a GFOffice pull request that needs a GFOpenLayout one names that one's head here, to be checked together.
REF=master
[ -f "$HERE/deps/openamigalayout.ref" ] && REF=$(head -n 1 "$HERE/deps/openamigalayout.ref" | tr -d '[:space:]')
[ -n "$REF" ] || REF=master
if [ ! -d "$DEPS/gfopenlayout/.git" ]; then
    git init -q "$DEPS/gfopenlayout" &&
        git -C "$DEPS/gfopenlayout" fetch -q --depth 1 "${GIT_BASE}DalsinAI/gfopenlayout" "$REF" &&
        git -C "$DEPS/gfopenlayout" checkout -q FETCH_HEAD ||
        { echo "build-amiga.sh: couldn't fetch DalsinAI/gfopenlayout at $REF" >&2; exit 1; }
fi
echo "build-amiga.sh: gfopenlayout at $(git -C "$DEPS/gfopenlayout" rev-parse --short HEAD) ($REF)"
OS32_GCC16=$STOVE OPENLAYOUT_SRC="$DEPS/gfopenlayout" sh "$HERE/build-os3.sh" "$OUT"
OS32_GCC16=$STOVE sh "$HERE/build-library.sh" "$OUT/library"
OS32_GCC16=$STOVE OPENLAYOUT_SRC="$DEPS/gfopenlayout" OWF=library OWF_BUILD_LIBRARIES=no \
    sh "$HERE/build-os3.sh" "$OUT/onlib"

# GFOpenDraw stays in its own repository because its plain-C indexed-paint engine is independently reusable.
# A suite build pins and builds that repository, then puts the two applications and its m68k SDK beside the
# other GFOffice deliverables. Its build-amiga.sh finds this job's pinned gfopenlayout and fetches
# GFOpenGadTools when the job did not check it out.
DRAW_REF=main
[ -f "$HERE/deps/gfopendraw.ref" ] && DRAW_REF=$(head -n 1 "$HERE/deps/gfopendraw.ref" | tr -d '[:space:]')
[ -n "$DRAW_REF" ] || DRAW_REF=main
if [ ! -d "$DEPS/gfopendraw/.git" ]; then
    git init -q "$DEPS/gfopendraw" &&
        git -C "$DEPS/gfopendraw" fetch -q --depth 1 "${GIT_BASE}DalsinAI/gfopendraw" "$DRAW_REF" &&
        git -C "$DEPS/gfopendraw" checkout -q FETCH_HEAD ||
        { echo "build-amiga.sh: couldn't fetch DalsinAI/gfopendraw at $DRAW_REF" >&2; exit 1; }
fi
echo "build-amiga.sh: gfopendraw at $(git -C "$DEPS/gfopendraw" rev-parse --short HEAD) ($DRAW_REF)"
OUT="$OUT/draw" STOVE="$STOVE" AMICRADLE_GIT_BASE="$GIT_BASE" \
    sh "$DEPS/gfopendraw/build-amiga.sh"
cp "$OUT/draw/GFOpenDraw" "$OUT/draw/GFOpenDrawOL" "$OUT/"
mkdir -p "$OUT/sdk/gfopendraw"
cp -a "$OUT/draw/sdk/Include_H" "$OUT/draw/sdk/Lib" "$OUT/sdk/gfopendraw/"
mkdir -p "$OUT/samples"
for f in "$HERE"/samples/*.docx "$HERE"/samples/*.odt "$HERE"/samples/*.txt \
         "$HERE"/tests/book/fixtures/features-openpyxl.xlsx "$HERE"/tests/book/fixtures/charts-openpyxl.xlsx \
         "$HERE"/tests/book/fixtures/validation-openpyxl.xlsx "$HERE"/tests/core/fixtures/*.docx \
         "$HERE"/tests/core/fixtures/*.odt; do
    [ -f "$f" ] && cp "$f" "$OUT/samples/"   # the workbooks (GFOpenCalc's charts, validation and the rest) and the
                                             # layout fixtures (covers, contents, text round frames), for a lab
done
# and presentations for GFOpenPresentation: the deck tests' fixtures, made here (tests/deck/make_*fixtures.py)
for f in pptx-talk.pptx pptx-plain.pptx pptx-text.pptx odfpy.odp libreoffice.odp; do
    [ -f "$HERE/tests/deck/fixtures/$f" ] && cp "$HERE/tests/deck/fixtures/$f" "$OUT/samples/"
done
rm -rf "$DEPS"   # the job's out/ keeps the programs, not the sources
