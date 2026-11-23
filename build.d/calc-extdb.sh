# GFOpenCalc's database sources for External Data (include/opencalc_extdb.h): which SQLite they are built
# with, as GFOpenBase's (build.d/base.sh's SQLITE switch, the one setting for both):
#   SQLITE=library  (default) sqlite.library, through gfopensqlite's link library (OPENSQLITE_LIB: include/
#                   and the soft-float lib-soft/libsqlite3_link.a; build-amiga.sh fills it from the pinned
#                   release). GFOpenCalc opens the library at the first database read, not as it starts
#                   (src/calc/extdb/oc_extdb.c), and holds none of SQLite's code, which oc_extdb_verify checks.
#   SQLITE=static   gfopensqlite's single-threaded soft-float SQLite linked in (OPENSQLITE, out-soft-st/).
# Without the folder GFOpenCalc is built all the same and its External Data reads web pages only.
#
# build-os3.sh sources this before GFOpenCalc (it holds only functions, so the build.d loop sourcing it again
# does nothing):
#   oc_extdb_setup            sets OC_EXTDB (flags and sources for the compile line) and OC_EXTDB_LIB (the link)
#   oc_extdb_verify PROGRAM   on the library, stops the build if SQLite's code is in PROGRAM all the same
# Tests: tests/calc_core/test_calc_extdb_switch.sh (mk/calc-extdb.mk).
# MIT, Copyright (c) 2026 AmiCradle Team.

oc_extdb_setup() {
    OC_EXTDB=""
    OC_EXTDB_LIB=""
    OC_EXTDB_MODE=""
    case "${SQLITE:-library}" in
    library)
        oc_lib=${OPENSQLITE_LIB:-$HERE/../gfopensqlite/out-library}
        if [ -f "$oc_lib/lib-soft/libsqlite3_link.a" ] && [ -f "$oc_lib/include/sqlite3.h" ] &&
           [ -f "$oc_lib/include/sqlite3_link.h" ]; then
            OC_EXTDB="-DOC_EXTDATA_DATABASES -DOB_SQLITE_LIBRARY -I$oc_lib/include $HERE/src/calc/extdb/oc_extdb.c"
            OC_EXTDB="$OC_EXTDB $HERE/src/base/ob_core.c $HERE/app/ob_sqlitelib.c"
            OC_EXTDB_LIB="-L$oc_lib/lib-soft -lsqlite3_link"
            OC_EXTDB_MODE=library
            echo "build-os3.sh: GFOpenCalc's External Data reads databases through sqlite.library"
        else
            echo "build-os3.sh: no sqlite.library link library in $oc_lib: GFOpenCalc's External Data reads web pages only"
        fi ;;
    static)
        oc_st=${OPENSQLITE:-$HERE/../gfopensqlite/out-soft-st}
        if [ -f "$oc_st/lib/libsqlite3.a" ] && [ -f "$oc_st/include/sqlite3.h" ]; then
            OC_EXTDB="-DOC_EXTDATA_DATABASES -I$oc_st/include $HERE/src/calc/extdb/oc_extdb.c"
            OC_EXTDB="$OC_EXTDB $HERE/src/base/ob_core.c $HERE/app/ob_sqlitelib.c"
            OC_EXTDB_LIB="-L$oc_st/lib -lsqlite3"
            OC_EXTDB_MODE=static
            echo "build-os3.sh: GFOpenCalc's External Data reads databases with SQLite linked in"
        else
            echo "build-os3.sh: no gfopensqlite build in $oc_st: GFOpenCalc's External Data reads web pages only"
        fi ;;
    *)
        echo "build.d/calc-extdb.sh: SQLITE=$SQLITE is neither static nor library"
        return 1 ;;
    esac
    return 0
}

# On the library the program holds none of SQLite: this message is in every copy of the engine and nowhere
# else (as build.d/base.sh checks GFOpenBase).
oc_extdb_verify() {
    if [ "$OC_EXTDB_MODE" = library ] && grep -q "malformed database schema" "$1"; then
        echo "build.d/calc-extdb.sh: SQLITE=library, but $(basename "$1") has SQLite's code linked in"
        return 1
    fi
    return 0
}
