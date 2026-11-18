# GFOpenProject (amicradle-teams designs/GFOPENPROJECT-20261113.md), sourced by build-os3.sh with its
# variables set (CC, CFLAGS_COMMON, SPECS, HERE, OUT, OWF, OWFFLAGS, OWFSRC, OPENLAYOUT_SRC, OL_DRAW,
# OL_DRAW_FLAGS). The plan's core (libowf/project, src/project) is linked in. GFOpenLayout's drawing is $OL_DRAW
# (build-os3.sh's OPENLAYOUT, docs/suite/layout-library.md): gfopenlayout.library 25's calls, or draw/oldraw.c
# with its Amiga target compiled in. libowf is linked in too, so it is built with OWF=linked only (owf.library has no
# plan code). Integer arithmetic only: no FPU, no maths library.
# MIT, Copyright (c) 2026 AmiCradle Team.
if [ "$OWF" != linked ]; then
    echo "build.d/project.sh: GFOpenProject is built with libowf linked in only (OWF=linked)"
elif [ ! -f "$OPENLAYOUT_SRC/draw/oldraw.c" ] || [ ! -f "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" ]; then
    echo "build.d/project.sh: no GFOpenLayout drawing in $OPENLAYOUT_SRC (draw/oldraw.c): GFOpenProject left out"
else
    # shellcheck disable=SC2086
    "$CC" $CFLAGS_COMMON -DNO_FPUAUTO -DNO_GFMALLOC $OL_DRAW_FLAGS -m68040 -specs=$SPECS -O2 -Wno-pointer-sign \
        $OWFFLAGS $SPELL -I"$HERE/include" -I"$OPENLAYOUT_SRC/include" -I"$OPENLAYOUT_SRC/draw" -I"$HERE/app" \
        $OWFSRC_DOCS "$HERE/libowf/src/owf_pdf.c" "$HERE/libowf/src/owf_pdffonts.c" "$HERE/libowf/src/owf_fill.c" \
        "$HERE"/libowf/project/*.c "$HERE"/src/project/*.c $SPELL_SRC \
        $OL_DRAW "$HERE/app/openproject.c" "$HERE/app/ow_print.c" -lamiga \
        -o "$OUT/GFOpenProject"
    echo "GFOpenProject: $(wc -c < "$OUT/GFOpenProject") bytes"
fi
