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
    "$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign -I"$HERE/include" -I"$HERE/app" \
        -I"$OPENSQLITE/include" "$HERE"/src/base/*.c "$HERE/app/openbase.c" "$HERE/app/ow_stack.c" \
        -L"$OPENSQLITE/lib" -lsqlite3 -lm -lamiga -o "$OUT/GFOpenBase"
    echo "GFOpenBase: $(wc -c < "$OUT/GFOpenBase") bytes"
else
    echo "build.d/base.sh: no gfopensqlite build in $OPENSQLITE (FLOAT=soft THREADSAFE=0 ./build.sh there, or set OPENSQLITE): GFOpenBase left out"
fi
