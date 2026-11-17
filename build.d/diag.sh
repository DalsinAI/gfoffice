# GFOpenDiag (amicradle-teams designs/GFOPENDIAG-20261113.md), sourced by build-os3.sh with its variables set (CC,
# CFLAGS_COMMON, SPECS, HERE, OUT, OWF, OWFFLAGS, OWFSRC_DOCS, OPENLAYOUT_SRC). Its window, canvas and mouse
# (app/opendiag.c, src/diag/odg_view.c, odg_edit.c) and GFOpenLayout's drawing (draw/oldraw.c with its Amiga target,
# amiga/oldraw_amiga.c: lines, polygons and ellipses need gfopenlayout 34472a1 or later, deps/openamigalayout.ref)
# are its own. The diagram model and files (libowf/diagram), the editing core and ARexx commands (the rest of
# src/diag) and the PNG and ILBM writers EXPORT uses (src/deck/ow_picture.c) are owfdiag.library's
# (scripts/owfdiag-sources.sh): linked in with libowf (OWF=linked), or opened from LIBS: (OWF=library,
# OWF_USE_DIAG_LIBRARY; docs/suite/owf-libraries.md section 9). Integer arithmetic only: no FPU, no maths library.
# MIT, Copyright (c) 2026 AmiCradle Team.
if [ "$OWF" = library ]; then
    DIAGSRC=""
    OWF_DIAG="-DOWF_USE_DIAG_LIBRARY"
else
    . "$HERE/scripts/owfdiag-sources.sh"
    DIAGSRC="$OWFSRC_DOCS $HERE/libowf/src/owf_pdf.c $HERE/libowf/src/owf_pdffonts.c $OWFDIAG_SRC"
    OWF_DIAG=""
fi
if [ ! -f "$OPENLAYOUT_SRC/draw/oldraw.c" ] || [ ! -f "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" ] ||
     ! grep -q oldraw_ellipse "$OPENLAYOUT_SRC/include/gfopenlayout_draw.h"; then
    echo "build.d/diag.sh: no GFOpenLayout drawing with lines and ellipses in $OPENLAYOUT_SRC: GFOpenDiag left out"
else
    # shellcheck disable=SC2086
    "$CC" $CFLAGS_COMMON -DNO_FPUAUTO -DNO_GFMALLOC -DOLDRAW_AMIGA -m68040 -specs=$SPECS -O2 -Wno-pointer-sign \
        $OWFFLAGS $OWF_DIAG -I"$HERE/include" -I"$HERE/src/deck" -I"$OPENLAYOUT_SRC/include" -I"$OPENLAYOUT_SRC/draw" \
        $DIAGSRC "$HERE/src/diag/odg_view.c" "$HERE/src/diag/odg_edit.c" \
        "$OPENLAYOUT_SRC/draw/oldraw.c" "$OPENLAYOUT_SRC/amiga/oldraw_amiga.c" "$HERE/app/opendiag.c" -lamiga \
        -o "$OUT/GFOpenDiag"
    echo "GFOpenDiag: $(wc -c < "$OUT/GFOpenDiag") bytes"
fi
