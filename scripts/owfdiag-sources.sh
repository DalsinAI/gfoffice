# owfdiag.library's sources besides libowf's archive (docs/suite/owf-libraries.md), sourced by build-library.sh and
# scripts/check-library-libc.sh with HERE set: the diagram model and its formats (libowf/diagram), GFOpenDiag's
# editing core and ARexx commands (src/diag, but its canvas and mouse, odg_view.c and odg_edit.c, which draw with
# GFOpenLayout in the program's window, and odg_spell.c, which calls owfspell.library) and the PNG and ILBM writers EXPORT uses (src/deck/ow_picture.c).
# MIT, Copyright (c) 2026 AmiCradle Team.
OWFDIAG_SRC="$(ls "$HERE"/libowf/diagram/*.c | tr '\n' ' ')"
for f in "$HERE"/src/diag/*.c; do
    case "$f" in
    */odg_view.c | */odg_edit.c | */odg_spell.c) ;;
    *) OWFDIAG_SRC="$OWFDIAG_SRC $f" ;;
    esac
done
OWFDIAG_SRC="$OWFDIAG_SRC $HERE/src/deck/ow_picture.c"
