#!/bin/sh
# GFOpenWrite, GFOpenCalc and OWConvert for AmigaOS 3.x with the os32-gcc16 stove
# (GFOpenAmigaGCC 16). The compiler: OS32_GCC16, else STOVE, else
# ~/AmiCradle/stoves/os32-gcc16; this script stops when given a GCC older
# than 16 (the os32 stove's GCC 6.5 is retired: its peephole2 clears a word
# and the long after it with one clr.l).
#
#   build-os3.sh [OUT_DIR]              (default build/os3)
#   OWF=library build-os3.sh [OUT_DIR]  each on its library (build-library.sh, docs/suite/owf-libraries.md):
#                                       GFOpenWrite on owfwrite.library, GFOpenCalc on owfcalc.library, GFOpenDiag
#                                       on owfdiag.library, OWConvert on the first four
#
# Both are 68040 code, as every build. The GFOpenWrite UI uses GFOpenGadTools.
# No FPU is required.
#
# libowf, the file formats, is linked into each program (OWF=linked, the
# default) or opened as GFOffice's libraries (OWF=library: the libraries,
# OWFLibsTest and OWFLibTest are built too). Either way ODT and DOCX deflate and inflate
# through z.library, ours (amicradle-guest libraries/openzlib), opened
# when a document first needs it; there is no zlib source in the build.
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$HERE/specs/m68k-soft-m68040.specs"
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: GFOpenWrite needs GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
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
    # each program names its library: GFOpenWrite owfwrite.library (OWF_USE_WRITE_LIBRARY), GFOpenCalc
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

# GFOffice's one spell checker is owfspell.library (libowf/src/owf_spell.c, docs/suite/owf-spell.md): a program
# that spells is built with $SPELL and links $SPELL_SRC, which opens the library, and no program links a copy of
# owf_spell.c.
SPELL="-DOWF_USE_SPELL_LIBRARY -I$HERE/libowf/library/sdk/Include_H"
SPELL_SRC="$HERE/libowf/amiga/owf_spell_base.c"
OWFSRC_ALL=$OWFSRC
OWFSRC=""
for f in $OWFSRC_ALL; do
    case "$f" in
    */owf_spell.c) ;;
    *) OWFSRC="$OWFSRC $f" ;;
    esac
done

# GFOpenWrite reads documents only: the workbook code (owf_book.c, its charts and its
# formats) is OWConvert's and the spreadsheet's, and the deck code
# (owf_deck.c, ODP, the page PDF writer) OWConvert's and GFOpenPresentation's.
OWFSRC_DOCS=""
for f in $OWFSRC; do
    case "$f" in
    */owf_book.c | */owf_cfb.c | */owf_xlfunc.c | */in_xls.c | */out_csv.c | */owf_chart.c | */owf_chartbook.c | */owf_chartodf.c) ;;
    */owf_booknum.c | */owf_xlformula.c | */in_xlsx.c | */out_xlsx.c | */in_ods.c | */out_ods.c | */in_csv.c) ;;
    */owf_deck.c | */in_odp.c | */out_odp.c | */in_pptx.c | */in_ppt.c | */owf_pdf.c | */owf_pdffonts.c) ;;
    */owf_fill.c | */owf_picture.c) ;;   # the shared fills and picture writers: the drawing programs' (below)
    */owf_print.c) ;;   # the print core: it reads the page model (owf_deck.c), so the page programs' (below)
    */in_pub.c) ;;   # GFOpenPressmaster's .pub reader: it needs owf_cfb.c
    */owf_cfbw.c | */out_pub.c) ;;   # GFOpenPressmaster's .pub save and its compound file writer
    */owf_kind.c) ;;   # owf_file_kind: it reads the workbook and .pub detectors
    *) OWFSRC_DOCS="$OWFSRC_DOCS $f" ;;
    esac
done

# GFOpenLayout is gfopenlayout.library, and nothing else (Dale, 18 November 2026: "no switch, remove all the dead
# code and lock in just openlayout"; docs/suite/layout-library.md). No program compiles GFOpenLayout's source in:
# each links the library's link sources from a gfopenlayout checkout (OPENLAYOUT_SRC, else beside this
# repository; build-amiga.sh fetches the one deps/openamigalayout.ref pins), which make the same C calls through
# the library, and app/ow_layoutlib.c, which opens it in main() and lets the program say so when it is missing.
#   $OL_PAGES, $OL_PAGES_FLAGS   the page programs: the document calls, gfopenlayout.library 24 or later
#   $OL_DRAW, $OL_DRAW_FLAGS     GFOpenDiag and GFOpenProject: GFOpenLayout drawing, gfopenlayout.library 25
# tests/layout/check_library_calls.sh holds every program to these and fails if a build line names GFOpenLayout's
# sources again.
OPENLAYOUT_SRC=${OPENLAYOUT_SRC:-$HERE/../gfopenlayout}
OL_LINK_DIR="$OPENLAYOUT_SRC/library/link"
for f in gfopenlayout_link.c gfopenlayout_link_core.c gfopenlayout_link_draw.c; do
    [ -f "$OL_LINK_DIR/$f" ] || {
        echo "GFOpenLayout with library 25's link sources not found (library/link/$f): set OPENLAYOUT_SRC to a gfopenlayout checkout"; exit 2; }
done
OL_PAGES="$OL_LINK_DIR/gfopenlayout_link.c $OL_LINK_DIR/gfopenlayout_link_core.c $HERE/app/ow_layoutlib.c"
OL_PAGES_FLAGS="-DOW_LAYOUT_LIBRARY"
OL_DRAW="$OL_LINK_DIR/gfopenlayout_link_draw.c $HERE/app/ow_layoutlib.c"
OL_DRAW_FLAGS="-DOW_LAYOUT_LIBRARY -DOW_LAYOUT_DRAW"

# OWConvert: 68040, as every build, with presentations' PDFs through GFOpenPresentation's core (src/deck) on
# GFOpenLayout. Its parts are one file per library (tools/owc.h, docs/suite/owf-libraries.md section 5): with
# libowf linked in they are built together; on the libraries each is built for its own (owc_doc.c for
# owfwrite.library, owc_book.c owfcalc, owc_pub.c owfpub, owc_deck.c and the deck's PDF owfpresent), so a
# conversion opens only the libraries it needs.
OWC_DECK="-DOWC_DECK_PDF $OL_PAGES_FLAGS -I$HERE/include -I$HERE/app -I$OPENLAYOUT_SRC/include"
OWC_PDF="$HERE/src/deck/opr_layout.c $HERE/src/deck/opr_pdf.c $HERE/src/deck/opr_gradient.c $HERE/src/core/ow_layout.c $OL_PAGES"
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
        "$CC" $OWCFLAGS -DOWF_USE_PRESENT_LIBRARY $OWC_DECK -c "$HERE/tools/owc_deck.c" $OWC_PDF \
            "$HERE/libowf/src/owf_fill.c" "$HERE/libowf/src/owf_print.c")   # opr_gradient.c's fills and opr_pdf.c's
                                                    # print core: the drawing programs', not owfpresent.library's
    # shellcheck disable=SC2086
    "$CC" $OWCFLAGS "$HERE/tools/owconvert.c" "$OWC_OBJ"/*.o -lm -o "$OUT/OWConvert"
else
    # shellcheck disable=SC2086
    "$CC" $OWCFLAGS $OWC_DECK $OWFSRC $OWC_PDF "$HERE/tools/owconvert.c" "$HERE"/tools/owc_*.c $OWFLIBS \
        -o "$OUT/OWConvert"
fi

# GFOpenWrite: the GFOpenGadTools shell (app/openwrite2.c) is GFOpenWrite since 2.0.1 (the user, 10 October
# 2026: "the rewrite is GFOpenWrite, not a second program"); the same core and filters.
# shellcheck disable=SC2086
APP_COMMON="$HERE/app/ow_autosave.c $HERE/app/ow_print.c $HERE/app/ow_spell.c $HERE/app/ow_stack.c $HERE/app/ow_fontwidths.c $HERE/app/ow_clipboard.c"
# GFOpenWrite has no floating-point work. The soft-68040 flavour is a complete
# libnix archive, including its allocator; the current stove's default
# libgfmalloc would define the same calls. Select the flavour's allocator and
# do not inject the otherwise unused fpuauto maths. Calc and the converter keep
# their explicit -lm paths.
"$CC" $CFLAGS_COMMON -DNO_FPUAUTO -DNO_GFMALLOC -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_WRITE $SPELL \
    $OL_PAGES_FLAGS \
    -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    $OWFSRC_DOCS "$HERE"/src/core/*.c $OL_PAGES "$HERE/app/openwrite2.c" $APP_COMMON $SPELL_SRC \
    "$HERE"/third_party/opengadtools/*.c -lamiga \
    -o "$OUT/GFOpenWrite"

# The 1.x window (app/openwrite.c), as GFOpenWrite1, only when asked: OW1=yes build-os3.sh
if [ "${OW1:-}" = yes ] && [ "$OWF" = library ]; then
    echo "build-os3.sh: the 1.x window is built with libowf linked in only (OWF=linked)"
elif [ "${OW1:-}" = yes ]; then
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $SPELL $OL_PAGES_FLAGS \
    -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    $OWFSRC_DOCS "$HERE"/src/core/*.c $OL_PAGES "$HERE/app/openwrite.c" $APP_COMMON $SPELL_SRC \
    "$HERE"/third_party/opengadtools/*.c -lamiga \
    -o "$OUT/GFOpenWrite1"
echo "$OUT/GFOpenWrite1 ($(wc -c < "$OUT/GFOpenWrite1") bytes)"
fi

# GFOpenCalc, the spreadsheet of the GFOffice Suite: its core (src/calc) and GFOpenWrite 2's shell pieces, with
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
# External Data's databases (include/opencalc_extdb.h) need GFOpenBase's core and gfopensqlite's soft-float
# SQLite, found as build.d/base.sh finds it (OPENSQLITE); without it GFOpenCalc's External Data reads web pages
# only and says so for a database.
OC_SQLITE=${OPENSQLITE:-$HERE/../gfopensqlite/out-soft-st}
if [ -f "$OC_SQLITE/lib/libsqlite3.a" ] && [ -f "$OC_SQLITE/include/sqlite3.h" ]; then
    OC_EXTDB="-DOC_EXTDATA_DATABASES -I$OC_SQLITE/include $HERE/src/calc/extdb/oc_extdb.c $HERE/src/base/ob_core.c"
    OC_EXTDB_LIB="-L$OC_SQLITE/lib -lsqlite3"
else
    OC_EXTDB=""
    OC_EXTDB_LIB=""
    echo "build-os3.sh: no gfopensqlite build in $OC_SQLITE: GFOpenCalc's External Data reads web pages only"
fi
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_CALC $SPELL \
    -I"$HERE/include" -I"$HERE/app" -I"$HERE/src/calc" -I"$HERE/libowf/calc" -I"$HERE/third_party/opengadtools" \
    $OCSRC "$HERE"/src/calc/*.c $OC_EXTDB "$HERE/app/opencalc.c" "$HERE/app/ow_autosave.c" "$HERE/app/ow_stack.c" $SPELL_SRC \
    "$HERE/app/ow_fontwidths.c" "$HERE"/third_party/opengadtools/*.c $OC_EXTDB_LIB -lamiga -lm \
    -o "$OUT/GFOpenCalc"
echo "$OUT/GFOpenCalc ($(wc -c < "$OUT/GFOpenCalc") bytes)"

# GFOpenPresentation, the GFOffice Suite's slides (P2 and P3's first typing): the documents' libowf files with
# the deck's (the model, ODP, PPTX in, the page PDF writer), its core (src/deck) on GFOpenLayout and GFOpenWrite's
# paragraph layout, with libowf linked in (owf.library has no deck calls yet).
if [ "$OWF" = library ]; then
    echo "build-os3.sh: GFOpenPresentation is built with libowf linked in only (OWF=linked): owf.library has no deck calls yet"
else
OWFSRC_DECK="$OWFSRC_DOCS"
# owf_chart.c: a slide's charts are read and written as chart parts (Presentation gap 2); owf_booknum.c reads
# and writes their numbers
# owf_fill.c and owf_picture.c: the fills and picture writers every drawing program shares (owf_fill.h, owf_picture.h)
# owf_print.c: the print core (owf_print.h), opr_pdf.c's bleed, crop marks and booklet order
for f in owf_deck.c in_odp.c out_odp.c in_pptx.c in_ppt.c owf_pdf.c owf_pdffonts.c owf_fill.c owf_picture.c owf_print.c \
    owf_chart.c owf_chartodf.c owf_booknum.c; do
    OWFSRC_DECK="$OWFSRC_DECK $HERE/libowf/src/$f"
done
# The chart editor (Chart Data..., OPR_CHART_EDITOR; Presentation gap 2) is GFOpenCalc's core on a data sheet:
# src/calc, the formula engine (libowf/calc) and the workbook code they need, with libnix's maths (-lm). About
# 1 MB more; GFOpenPressmaster's build below leaves it out.
OWFSRC_CHARTED=""
for f in owf_book.c owf_cfb.c owf_xlfunc.c in_xls.c out_csv.c owf_chartbook.c owf_xlformula.c in_xlsx.c out_xlsx.c \
    in_ods.c out_ods.c in_csv.c owf_kind.c; do
    OWFSRC_CHARTED="$OWFSRC_CHARTED $HERE/libowf/src/$f"
done
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -DOW_CLIPBOARD_OBJECTS -DOPR_CHART_EDITOR -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $SPELL \
    $OL_PAGES_FLAGS \
    -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" -I"$HERE/src/calc" -I"$HERE/libowf/calc" \
    -I"$HERE/third_party/opengadtools" \
    $OWFSRC_DECK $OWFSRC_CHARTED "$HERE"/libowf/calc/*.c "$HERE"/src/calc/*.c "$HERE"/src/deckchart/*.c \
    "$HERE"/src/deck/*.c $SPELL_SRC "$HERE/src/core/ow_layout.c" $OL_PAGES \
    "$HERE/app/openpresent.c" "$HERE/app/opr_chartwin.c" "$HERE/app/ow_stack.c" "$HERE/app/ow_clipboard.c" \
    "$HERE"/third_party/opengadtools/*.c -lamiga -lm \
    -o "$OUT/GFOpenPresentation"
echo "$OUT/GFOpenPresentation ($(wc -c < "$OUT/GFOpenPresentation") bytes)"

# GFOpenPressmaster P1a: the clean-room .pub reader adapted to the same positioned-page model and
# GFOpenLayout/PDF/screen renderer as Presentation.  This first build is linked: owfpub.library's
# ABI still exposes the P0 model only, and the deck bridge is intentionally kept in the portable core.
OWFSRC_PUB="$OWFSRC_DECK $HERE/libowf/src/owf_cfb.c $HERE/libowf/src/in_pub.c"
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -DOPENPRESSMASTER -DOW_CLIPBOARD_OBJECTS -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $SPELL \
    $OL_PAGES_FLAGS \
    -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    $OWFSRC_PUB "$HERE"/src/deck/*.c $SPELL_SRC "$HERE/src/pub/opp_import.c" "$HERE/libowf/pub/opp_merge.c" "$HERE/src/pub/opp_pagenum.c" "$HERE/src/pub/opp_spread.c" "$HERE/libowf/pub/opp_template.c" "$HERE/src/core/ow_merge.c" "$HERE/src/core/ow_layout.c" $OL_PAGES \
    "$HERE/app/openpresent.c" "$HERE/app/ow_stack.c" "$HERE/app/ow_clipboard.c" "$HERE"/third_party/opengadtools/*.c -lamiga \
    -o "$OUT/GFOpenPressmaster"
echo "$OUT/GFOpenPressmaster ($(wc -c < "$OUT/GFOpenPressmaster") bytes)"
fi

# Workstream hooks (docs/openoffice-parity-plan.md, section 4): a stream that needs a new program or
# its own link line adds build.d/<stream>.sh, sourced here with this script's variables set.
for f in "$HERE"/build.d/*.sh; do
    [ -f "$f" ] || continue
    . "$f"
done

# The toolbars' pictures: the Chrome icon set's strips beside the programs, where they look first
# (PROGDIR:Images/Toolbar/; gfopengadtools lib/ogt_iconset.c). Made by gfopengadtools' tools/iconset.
mkdir -p "$OUT/Images/Toolbar"
cp "$HERE"/Images/Toolbar/*.ilbm "$OUT/Images/Toolbar/"
# the ARexx examples (docs/gfoffice/Design-Suite-Shared.md: ARexx)
mkdir -p "$OUT/Rexx"
cp "$HERE"/Rexx/*.rexx "$OUT/Rexx/"

echo "$OUT/OWConvert ($(wc -c < "$OUT/OWConvert") bytes)"
echo "$OUT/GFOpenWrite ($(wc -c < "$OUT/GFOpenWrite") bytes)"
