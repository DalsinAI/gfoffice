# GFOpenProject (amicradle-teams designs/GFOPENPROJECT-20261113.md), sourced by build-os3.sh with its
# variables set (CC, CFLAGS_COMMON, SPECS, HERE, OUT, OWF, OWFFLAGS, OWFSRC, OPENLAYOUT_SRC). The plan's core
# (libowf/project, src/project) and GFOpenLayout's drawing (draw/oldraw.c with its Amiga target,
# amiga/oldraw_amiga.c) are linked in; libowf too, so it is built with OWF=linked only (owf.library has no
# plan code). Integer arithmetic only: no FPU, no maths library.
# MIT, Copyright (c) 2026 AmiCradle Team.
if [ "$OWF" != linked ]; then
    echo "build.d/project.sh: GFOpenProject is built with libowf linked in only (OWF=linked)"
elif [ ! -f "$OPENLAYOUT_SRC/draw/oldraw.c" ] || [ ! -f "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" ]; then
    echo "build.d/project.sh: no GFOpenLayout drawing in $OPENLAYOUT_SRC (draw/oldraw.c): GFOpenProject left out"
else
    # shellcheck disable=SC2086
    "$CC" $CFLAGS_COMMON -DNO_FPUAUTO -DNO_GFMALLOC -DOLDRAW_AMIGA -m68040 -specs=$SPECS -O2 -Wno-pointer-sign \
        $OWFFLAGS -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$OPENLAYOUT_SRC/draw" \
        $OWFSRC_DOCS "$HERE"/libowf/project/*.c "$HERE"/src/project/*.c \
        "$OPENLAYOUT_SRC/draw/oldraw.c" "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" "$HERE/app/openproject.c" -lamiga \
        -o "$OUT/GFOpenProject"
    echo "GFOpenProject: $(wc -c < "$OUT/GFOpenProject") bytes"
fi
