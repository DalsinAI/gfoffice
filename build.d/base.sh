# GFOpenBase (stream B, docs/openoffice-parity-plan.md section 4), sourced by build-os3.sh with its
# variables set (CC, CFLAGS_COMMON, SPECS, HERE, OUT). It links gfopensqlite's single-threaded soft-float
# SQLite (DalsinAI/gfopensqlite: FLOAT=soft THREADSAFE=0 ./build.sh, which fills out-soft-st/);
# OPENSQLITE names that folder, else a gfopensqlite beside this repository. Without it GFOpenBase is
# left out and the rest of the build goes on.
# SQLite's REAL work goes through fpuauto (the stove's default), so the FPU is used when there is one.
# MIT, Copyright (c) 2026 AmiCradle Team.
OPENSQLITE=${OPENSQLITE:-$HERE/../gfopensqlite/out-soft-st}
if [ -f "$OPENSQLITE/lib/libsqlite3.a" ] && [ -f "$OPENSQLITE/include/sqlite3.h" ]; then
    # Sheets (8, include/openbase_sheet.h) read and write ODS and XLSX through libowf's workbook code: with
    # libowf linked in, those files beside the documents' ($OB_BOOK: owf_book.c's format list names XLS and
    # CSV too); on the libraries, ob_sheet.c is built on its own for owfcalc.library, which it opens itself.
    if [ "$OWF" = library ]; then
        # shellcheck disable=SC2086
        "$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS -DOWF_USE_CALC_LIBRARY \
            -I"$HERE/include" -c "$HERE/src/base/sheet/ob_sheet.c" -o "$OUT/ob_sheet-calc.o"
        OB_SHEET="$OUT/ob_sheet-calc.o"
        OB_BOOK=""
    else
        OB_SHEET="$HERE/src/base/sheet/ob_sheet.c"
        OB_BOOK=""
        for f in owf_book owf_booknum owf_xlformula owf_xlfunc owf_chart owf_chartbook owf_cfb in_xls in_xlsx out_xlsx \
                 in_ods out_ods in_csv out_csv; do
            OB_BOOK="$OB_BOOK $HERE/libowf/src/$f.c"
        done
    fi
    # shellcheck disable=SC2086
    # Reports (4.3) write documents through libowf: GFOpenWrite's part of it ($OWFSRC_DOCS and $OWF_WRITE,
    # so OWF=library uses owfwrite.library as GFOpenWrite does). Their print preview lays the pages out as
    # GFOpenWrite does (src/core/ow_layout.c and GFOpenLayout, $OL_C).
    "$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_WRITE -I"$HERE/include" -I"$HERE/app" \
        -I"$OPENLAYOUT_SRC/include" -I"$OPENSQLITE/include" "$HERE"/src/base/*.c "$HERE"/src/base/report/*.c \
        "$HERE"/src/base/preview/*.c "$HERE/src/core/ow_layout.c" $OL_C $OB_SHEET $OWFSRC_DOCS $OB_BOOK \
        "$HERE/app/openbase.c" "$HERE/app/ob_reportwin.c" "$HERE/app/ob_previewwin.c" "$HERE/app/ow_stack.c" \
        -L"$OPENSQLITE/lib" -lsqlite3 -lm -lamiga -o "$OUT/GFOpenBase"
    echo "GFOpenBase: $(wc -c < "$OUT/GFOpenBase") bytes"
else
    echo "build.d/base.sh: no gfopensqlite build in $OPENSQLITE (FLOAT=soft THREADSAFE=0 ./build.sh there, or set OPENSQLITE): GFOpenBase left out"
fi
