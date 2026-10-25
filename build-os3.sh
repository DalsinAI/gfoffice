#!/bin/sh
# OpenWrite, OpenCalc and OWConvert for AmigaOS 3.x with the os32-gcc16 stove
# (OpenAmigaGCC 16). The compiler: OS32_GCC16, else STOVE, else
# ~/AmigaChrome/stoves/os32-gcc16; this script stops when given a GCC older
# than 16 (the os32 stove's GCC 6.5 is retired: its peephole2 clears a word
# and the long after it with one clr.l).
#
#   build-os3.sh [OUT_DIR]              (default build/os3)
#   OWF=library build-os3.sh [OUT_DIR]  each on its library (build-library.sh, docs/suite/owf-libraries.md):
#                                       OpenWrite on owfwrite.library, OpenCalc on owfcalc.library, OWConvert
#                                       on all four
#
# Both are 68040 code, as every build. The OpenWrite UI uses OpenGadTools.
# No FPU is required.
#
# libowf, the file formats, is linked into each program (OWF=linked, the
# default) or opened as GFOffice's libraries (OWF=library: the libraries,
# OWFLibsTest and OWFLibTest are built too). Either way ODT and DOCX deflate and inflate
# through z.library, ours (amigachrome-guest libraries/openzlib), opened
# when a document first needs it; there is no zlib source in the build.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$HERE/specs/m68k-soft-m68040.specs"
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: OpenWrite needs GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
OUT=${1:-$HERE/build/os3}
# -fno-late-combine-instructions: GCC 16's late-combine pass (after reload) can put an address register
# that the same instruction post-increments into its other address too, as in "move.b (a2)+,(a0,a2.l)":
# the 68k takes the incremented value, GCC meant the old one, so the copy lands a byte late. owf.library's
# number text had it (25 Oct 2026: "6.5" read "6.w"); every GFOffice build turns the pass off until the
# stove's GCC is fixed.
CFLAGS_COMMON="-noixemul -std=gnu99 -Wall -Werror -fno-delete-null-pointer-checks -fno-common -fno-late-combine-instructions"
mkdir -p "$OUT"

OWF=${OWF:-linked}
case "$OWF" in
linked)
    OWFFLAGS="-DOWF_HAVE_ZLIB -DOWF_ZLIBRARY -I$HERE/libowf/include -I$HERE/libowf/src -I$HERE/libowf/amiga/sdk/Include_H"
    OWF_WRITE=""
    OWFSRC="$HERE/libowf/src/*.c $HERE/libowf/amiga/owf_zlib_amiga.c"
    # OWConvert's workbooks have doubles: libnix's soft double maths, through
    # mathieeedoubbas.library, which uses the FPU when there is one
    OWFLIBS="-lm"
    ;;
library)
    # OWF_BUILD_LIBRARIES=no: the libraries are built elsewhere (build-amiga.sh builds them once, in OUT/library)
    [ "${OWF_BUILD_LIBRARIES:-yes}" = no ] || "$HERE/build-library.sh" "$OUT/library"
    # each program names its library: OpenWrite owfwrite.library (OWF_USE_WRITE_LIBRARY), OpenCalc
    # owfcalc.library and OWConvert all four (below)
    OWFFLAGS="-I$HERE/libowf/include -I$HERE/libowf/library/sdk/Include_H"
    OWF_WRITE="-DOWF_USE_WRITE_LIBRARY"
    OWFSRC=""
    OWFLIBS=""
    ;;
*)
    echo "build-os3.sh: OWF is linked or library, not $OWF"
    exit 2
    ;;
esac

# OpenWrite reads documents only: the workbook code (owf_book.c, its charts and its
# formats) is OWConvert's and the spreadsheet's, and the deck code
# (owf_deck.c, ODP, the page PDF writer) OWConvert's and OpenPresentation's.
OWFSRC_DOCS=""
for f in $OWFSRC; do
    case "$f" in
    */owf_book.c | */owf_cfb.c | */owf_xlfunc.c | */in_xls.c | */out_csv.c | */owf_chart.c) ;;
    */owf_booknum.c | */owf_xlformula.c | */in_xlsx.c | */out_xlsx.c | */in_ods.c | */out_ods.c | */in_csv.c) ;;
    */owf_deck.c | */in_odp.c | */out_odp.c | */owf_pdf.c | */owf_pdffonts.c) ;;
    */in_pub.c) ;;   # OpenPublisher's .pub reader: it needs owf_cfb.c
    */owf_kind.c) ;;   # owf_file_kind: it reads the workbook and .pub detectors
    *) OWFSRC_DOCS="$OWFSRC_DOCS $f" ;;
    esac
done

# The pages are OpenLayout's (DalsinAI/openamigalayout 0.2 or later): OPENLAYOUT_SRC, else beside this
# repository.
OPENLAYOUT_SRC=${OPENLAYOUT_SRC:-$HERE/../openamigalayout}
[ -f "$OPENLAYOUT_SRC/src/openlayout.c" ] || { echo "OpenLayout not found: set OPENLAYOUT_SRC to an openamigalayout checkout"; exit 2; }

# OWConvert: 68040, as every build, with presentations' PDFs through OpenPresentation's core (src/deck) on
# OpenLayout. Its parts are one file per library (tools/owc.h, docs/suite/owf-libraries.md section 5): with
# libowf linked in they are built together; on the libraries each is built for its own (owc_doc.c for
# owfwrite.library, owc_book.c owfcalc, owc_pub.c owfpub, owc_deck.c and the deck's PDF owfpresent), so a
# conversion opens only the libraries it needs.
OWC_DECK="-DOWC_DECK_PDF -I$HERE/include -I$OPENLAYOUT_SRC/include"
OWC_PDF="$HERE/src/deck/opr_layout.c $HERE/src/deck/opr_pdf.c $HERE/src/core/ow_layout.c $OPENLAYOUT_SRC/src/openlayout.c"
OWCFLAGS="$CFLAGS_COMMON -m68040 -specs=$SPECS -Os $OWFFLAGS -I$HERE/tools"
if [ "$OWF" = library ]; then
    OWC_OBJ="$OUT/obj-owconvert"
    rm -rf "$OWC_OBJ"
    mkdir -p "$OWC_OBJ"
    # shellcheck disable=SC2086
    (cd "$OWC_OBJ" &&
        "$CC" $OWCFLAGS -DOWF_USE_WRITE_LIBRARY -c "$HERE/tools/owc_doc.c" &&
        "$CC" $OWCFLAGS -DOWF_USE_CALC_LIBRARY -c "$HERE/tools/owc_book.c" &&
        "$CC" $OWCFLAGS -DOWF_USE_PUB_LIBRARY -c "$HERE/tools/owc_pub.c" &&
        "$CC" $OWCFLAGS -DOWF_USE_PRESENT_LIBRARY $OWC_DECK -c "$HERE/tools/owc_deck.c" $OWC_PDF)
    # shellcheck disable=SC2086
    "$CC" $OWCFLAGS "$HERE/tools/owconvert.c" "$OWC_OBJ"/*.o -lm -o "$OUT/OWConvert"
else
    # shellcheck disable=SC2086
    "$CC" $OWCFLAGS $OWC_DECK $OWFSRC $OWC_PDF "$HERE/tools/owconvert.c" "$HERE"/tools/owc_*.c $OWFLIBS \
        -o "$OUT/OWConvert"
fi

# OpenWrite: the OpenGadTools shell (app/openwrite2.c) is OpenWrite since 2.0.1 (the user, 10 October
# 2026: "the rewrite is OpenWrite, not a second program"); the same core and filters.
# shellcheck disable=SC2086
APP_COMMON="$HERE/app/ow_autosave.c $HERE/app/ow_print.c $HERE/app/ow_spell.c $HERE/app/ow_stack.c $HERE/app/ow_fontwidths.c"
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_WRITE \
    -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    $OWFSRC_DOCS "$HERE"/src/core/*.c "$OPENLAYOUT_SRC/src/openlayout.c" "$HERE/app/openwrite2.c" $APP_COMMON \
    "$HERE"/third_party/opengadtools/*.c -lamiga \
    -o "$OUT/OpenWrite"

# The 1.x window (app/openwrite.c), as OpenWrite1, only when asked: OW1=yes build-os3.sh
if [ "${OW1:-}" = yes ] && [ "$OWF" = library ]; then
    echo "build-os3.sh: the 1.x window is built with libowf linked in only (OWF=linked)"
elif [ "${OW1:-}" = yes ]; then
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS \
    -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    $OWFSRC_DOCS "$HERE"/src/core/*.c "$OPENLAYOUT_SRC/src/openlayout.c" "$HERE/app/openwrite.c" $APP_COMMON \
    "$HERE"/third_party/opengadtools/*.c -lamiga \
    -o "$OUT/OpenWrite1"
echo "$OUT/OpenWrite1 ($(wc -c < "$OUT/OpenWrite1") bytes)"
fi

# OpenCalc, the spreadsheet of the GFOffice Suite: its core (src/calc) and OpenWrite 2's shell pieces, with
# libowf linked in (OWF=linked: every workbook format and the formula engine, libowf/calc) or on
# owfcalc.library (OWF=library, OWF_USE_CALC_LIBRARY). Its own drawing's doubles (the charts) take libnix's
# maths (-lm) either way.
if [ "$OWF" = library ]; then
    OCSRC=""
    OWF_CALC="-DOWF_USE_CALC_LIBRARY"
else
    OCSRC="$OWFSRC $HERE/libowf/calc/*.c"
    OWF_CALC=""
fi
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_CALC \
    -I"$HERE/include" -I"$HERE/app" -I"$HERE/src/calc" -I"$HERE/libowf/calc" -I"$HERE/third_party/opengadtools" \
    $OCSRC "$HERE"/src/calc/*.c "$HERE/app/opencalc.c" "$HERE/app/ow_autosave.c" "$HERE/app/ow_stack.c" "$HERE/app/ow_fontwidths.c" \
    "$HERE"/third_party/opengadtools/*.c -lamiga -lm \
    -o "$OUT/OpenCalc"
echo "$OUT/OpenCalc ($(wc -c < "$OUT/OpenCalc") bytes)"

# The toolbars' pictures: the Chrome icon set's strips beside the programs, where they look first
# (PROGDIR:Images/Toolbar/; opengadtools lib/ogt_iconset.c). Made by opengadtools' tools/iconset.
mkdir -p "$OUT/Images/Toolbar"
cp "$HERE"/Images/Toolbar/*.ilbm "$OUT/Images/Toolbar/"

echo "$OUT/OWConvert ($(wc -c < "$OUT/OWConvert") bytes)"
echo "$OUT/OpenWrite ($(wc -c < "$OUT/OpenWrite") bytes)"
