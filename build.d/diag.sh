# GFOpenDiag (amicradle-teams designs/GFOPENDIAG-20261113.md), sourced by build-os3.sh with its variables set (CC,
# CFLAGS_COMMON, SPECS, HERE, OUT, OWF, OWFFLAGS, OWFSRC_DOCS, OPENLAYOUT_SRC). The diagram model and files
# (libowf/diagram), the editing core, ARexx commands, canvas and mouse (src/diag) and GFOpenLayout's drawing
# (draw/oldraw.c with its Amiga target, amiga/oldraw_amiga.c: lines, polygons and ellipses need gfopenlayout
# 34472a1 or later, deps/openamigalayout.ref) are linked in; libowf too, so it is built with OWF=linked only
# (owf.library has no diagram code). Integer arithmetic only: no FPU, no maths library.
# MIT, Copyright (c) 2026 AmiCradle Team.
if [ "$OWF" != linked ]; then
    echo "build.d/diag.sh: GFOpenDiag is built with libowf linked in only (OWF=linked)"
elif [ ! -f "$OPENLAYOUT_SRC/draw/oldraw.c" ] || [ ! -f "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" ] ||
     ! grep -q oldraw_ellipse "$OPENLAYOUT_SRC/include/gfopenlayout_draw.h"; then
    echo "build.d/diag.sh: no GFOpenLayout drawing with lines and ellipses in $OPENLAYOUT_SRC: GFOpenDiag left out"
else
    # shellcheck disable=SC2086
    "$CC" $CFLAGS_COMMON -DNO_FPUAUTO -DNO_GFMALLOC -DOLDRAW_AMIGA -m68040 -specs=$SPECS -O2 -Wno-pointer-sign \
        $OWFFLAGS -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$OPENLAYOUT_SRC/draw" \
        $OWFSRC_DOCS "$HERE"/libowf/diagram/*.c "$HERE"/src/diag/*.c \
        "$OPENLAYOUT_SRC/draw/oldraw.c" "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" "$HERE/app/opendiag.c" -lamiga \
        -o "$OUT/GFOpenDiag"
    echo "GFOpenDiag: $(wc -c < "$OUT/GFOpenDiag") bytes"
fi
