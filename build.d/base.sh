# GFOpenBase (stream B, docs/openoffice-parity-plan.md section 4), sourced by build-os3.sh with its
# variables set (CC, CFLAGS_COMMON, SPECS, HERE, OUT). SQLite comes one of two ways (SQLITE):
#   SQLITE=static   (default) gfopensqlite's single-threaded soft-float SQLite linked in
#                   (DalsinAI/gfopensqlite: FLOAT=soft THREADSAFE=0 ./build.sh, which fills out-soft-st/);
#                   OPENSQLITE names that folder, else a gfopensqlite beside this repository.
#   SQLITE=library  sqlite.library at run time, through gfopensqlite's link library
#                   (./build-library.sh there, which fills out-library/: include/ and the soft-float
#                   lib-soft/libsqlite3_link.a); OPENSQLITE_LIB names that folder. GFOpenBase opens the
#                   library itself before its window (app/ob_sqlitelib.c) and holds none of SQLite's code,
#                   which is checked below.
# Without the folder GFOpenBase is left out and the rest of the build goes on.
# SQLite's REAL work goes through fpuauto (the stove's default, and the library's own), so the FPU is used
# when there is one.
# MIT, Copyright (c) 2026 AmiCradle Team.
SQLITE=${SQLITE:-static}
OB_SQL_OK=""
case "$SQLITE" in
static)
    OPENSQLITE=${OPENSQLITE:-$HERE/../gfopensqlite/out-soft-st}
    OB_SQL_WANT="no gfopensqlite build in $OPENSQLITE (FLOAT=soft THREADSAFE=0 ./build.sh there, or set OPENSQLITE)"
    if [ -f "$OPENSQLITE/lib/libsqlite3.a" ] && [ -f "$OPENSQLITE/include/sqlite3.h" ]; then
        OB_SQL_OK=1
        OB_SQL_INC="-I$OPENSQLITE/include"
        OB_SQL_LINK="-L$OPENSQLITE/lib -lsqlite3"
    fi ;;
library)
    OPENSQLITE_LIB=${OPENSQLITE_LIB:-$HERE/../gfopensqlite/out-library}
    OB_SQL_WANT="no sqlite.library link library in $OPENSQLITE_LIB (./build-library.sh in gfopensqlite, or set OPENSQLITE_LIB)"
    if [ -f "$OPENSQLITE_LIB/lib-soft/libsqlite3_link.a" ] && [ -f "$OPENSQLITE_LIB/include/sqlite3.h" ] &&
       [ -f "$OPENSQLITE_LIB/include/sqlite3_link.h" ]; then
        OB_SQL_OK=1
        OB_SQL_INC="-DOB_SQLITE_LIBRARY -I$OPENSQLITE_LIB/include"
        OB_SQL_LINK="-L$OPENSQLITE_LIB/lib-soft -lsqlite3_link"
    fi ;;
*)
    echo "build.d/base.sh: SQLITE=$SQLITE is neither static nor library"
    exit 1 ;;
esac
if [ -n "$OB_SQL_OK" ]; then
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
    # GFOpenWrite does (src/core/ow_layout.c and GFOpenLayout, $OL_PAGES: build-os3.sh's OPENLAYOUT).
    # Spelling (src/base/spell) is GFOffice's one spell checker, owfspell.library ($SPELL, $SPELL_SRC).
    "$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_WRITE $SPELL $OL_PAGES_FLAGS -I"$HERE/include" -I"$HERE/app" \
        -I"$OPENLAYOUT_SRC/include" $OB_SQL_INC "$HERE"/src/base/*.c "$HERE"/src/base/report/*.c \
        "$HERE/src/base/spell/ob_spell.c" $SPELL_SRC \
        "$HERE"/src/base/preview/*.c "$HERE/src/core/ow_layout.c" $OL_PAGES $OB_SHEET $OWFSRC_DOCS $OB_BOOK \
        "$HERE/app/openbase.c" "$HERE/app/ob_reportwin.c" "$HERE/app/ob_previewwin.c" "$HERE/app/ow_stack.c" \
        "$HERE/app/ob_sqlitelib.c" $OB_SQL_LINK -lm -lamiga -o "$OUT/GFOpenBase"
    echo "GFOpenBase: $(wc -c < "$OUT/GFOpenBase") bytes (SQLITE=$SQLITE)"
    # on the library the program holds none of SQLite: its file header's text is in every copy of the engine
    if [ "$SQLITE" = library ] && grep -q "SQLite format 3" "$OUT/GFOpenBase"; then
        echo "build.d/base.sh: SQLITE=library, but GFOpenBase has SQLite's code linked in"
        exit 1
    fi
else
    echo "build.d/base.sh: $OB_SQL_WANT: GFOpenBase left out"
fi
