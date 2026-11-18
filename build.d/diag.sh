# GFOpenDiag (amicradle-teams designs/GFOPENDIAG-20261113.md), sourced by build-os3.sh with its variables set (CC,
# CFLAGS_COMMON, SPECS, HERE, OUT, OWF, OWFFLAGS, OWFSRC_DOCS, OPENLAYOUT_SRC, OL_DRAW, OL_DRAW_FLAGS). Its window,
# canvas and mouse (app/opendiag.c, src/diag/odg_view.c, odg_edit.c) are its own. GFOpenLayout's drawing is
# $OL_DRAW (build-os3.sh, docs/suite/layout-library.md): gfopenlayout.library 25's calls. The diagram model and files (libowf/diagram), the editing core and ARexx commands (the rest of
# src/diag) and the PNG and ILBM writers EXPORT uses (src/deck/ow_picture.c, libowf's owf_picture.c by its old
# names) are owfdiag.library's
# (scripts/owfdiag-sources.sh): linked in with libowf (OWF=linked), or opened from LIBS: (OWF=library,
# OWF_USE_DIAG_LIBRARY; docs/suite/owf-libraries.md section 9). Integer arithmetic only: no FPU, no maths library.
# MIT, Copyright (c) 2026 AmiCradle Team.
if [ "$OWF" = library ]; then
    DIAGSRC=""
    OWF_DIAG="-DOWF_USE_DIAG_LIBRARY"
else
    . "$HERE/scripts/owfdiag-sources.sh"
    # owf_fill.c and owf_picture.c: the shared fills and picture writers (owf_fill.h, owf_picture.h), which
    # owf_pdf.c and ow_picture.c call; OWFSRC_DOCS leaves them out
    DIAGSRC="$OWFSRC_DOCS $HERE/libowf/src/owf_pdf.c $HERE/libowf/src/owf_pdffonts.c $HERE/libowf/src/owf_fill.c"
    DIAGSRC="$DIAGSRC $HERE/libowf/src/owf_picture.c $OWFDIAG_SRC"
    OWF_DIAG=""
fi
if ! grep -q oldraw_ellipse "$OPENLAYOUT_SRC/include/gfopenlayout_draw.h"; then
    echo "build.d/diag.sh: no GFOpenLayout drawing with lines and ellipses in $OPENLAYOUT_SRC: GFOpenDiag left out"
else
    # shellcheck disable=SC2086
    "$CC" $CFLAGS_COMMON -DNO_FPUAUTO -DNO_GFMALLOC $OL_DRAW_FLAGS -m68040 -specs=$SPECS -O2 -Wno-pointer-sign \
        $OWFFLAGS $OWF_DIAG $SPELL -I"$HERE/include" -I"$HERE/src/deck" -I"$OPENLAYOUT_SRC/include" -I"$HERE/app" \
        $DIAGSRC "$HERE/src/diag/odg_view.c" "$HERE/src/diag/odg_edit.c" "$HERE/src/diag/odg_spell.c" $SPELL_SRC \
        $OL_DRAW "$HERE/app/opendiag.c" -lamiga \
        -o "$OUT/GFOpenDiag"
    echo "GFOpenDiag: $(wc -c < "$OUT/GFOpenDiag") bytes"
fi
