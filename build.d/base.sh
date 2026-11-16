# GFOpenBase (stream B, docs/openoffice-parity-plan.md section 4), sourced by build-os3.sh with its
# variables set (CC, CFLAGS_COMMON, SPECS, HERE, OUT). It links gfopensqlite's single-threaded soft-float
# SQLite (DalsinAI/gfopensqlite: FLOAT=soft THREADSAFE=0 ./build.sh, which fills out-soft-st/);
# OPENSQLITE names that folder, else a gfopensqlite beside this repository. Without it GFOpenBase is
# left out and the rest of the build goes on.
# SQLite's REAL work goes through fpuauto (the stove's default), so the FPU is used when there is one.
# MIT, Copyright (c) 2026 AmiCradle Team.
OPENSQLITE=${OPENSQLITE:-$HERE/../gfopensqlite/out-soft-st}
if [ -f "$OPENSQLITE/lib/libsqlite3.a" ] && [ -f "$OPENSQLITE/include/sqlite3.h" ]; then
    # shellcheck disable=SC2086
    # Reports (4.3) write documents through libowf: GFOpenWrite's part of it ($OWFSRC_DOCS and $OWF_WRITE,
    # so OWF=library uses owfwrite.library as GFOpenWrite does).
    "$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $OWFFLAGS $OWF_WRITE -I"$HERE/include" -I"$HERE/app" \
        -I"$OPENSQLITE/include" "$HERE"/src/base/*.c "$HERE"/src/base/report/*.c $OWFSRC_DOCS \
        "$HERE/app/openbase.c" "$HERE/app/ob_reportwin.c" "$HERE/app/ow_stack.c" \
        -L"$OPENSQLITE/lib" -lsqlite3 -lm -lamiga -o "$OUT/GFOpenBase"
    echo "GFOpenBase: $(wc -c < "$OUT/GFOpenBase") bytes"
else
    echo "build.d/base.sh: no gfopensqlite build in $OPENSQLITE (FLOAT=soft THREADSAFE=0 ./build.sh there, or set OPENSQLITE): GFOpenBase left out"
fi
