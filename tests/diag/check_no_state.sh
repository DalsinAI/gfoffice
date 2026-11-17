#!/bin/sh
# GFOpenDiag's model, files, editing core and ARexx commands keep no state between calls: no variable the code
# writes outside a call's own memory. They are to be owfdiag.library's (docs/suite/owf-libraries.md), and a
# library's code is shared by every task that opens it, so a static buffer one task writes is another's to clobber.
# Each file is compiled on the host and its writable data (.data, .bss and common: nm's b, B, d, D, c, C; tables
# that are const, pointers and all, are read-only and pass) listed. Run by make check. (The PNG and ILBM writers,
# src/deck/ow_picture.c, move to the shared core's libowf/src/owf_picture.c.)
#
#   check_no_state.sh [CC]
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${1:-${CC:-cc}}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
bad=0
for f in "$HERE"/libowf/diagram/*.c "$HERE"/src/diag/odg_core.c "$HERE"/src/diag/odg_clip.c \
         "$HERE"/src/diag/odg_order.c "$HERE"/src/diag/odg_align.c "$HERE"/src/diag/odg_mind.c \
         "$HERE"/src/diag/odg_style.c "$HERE"/src/diag/odg_stencil.c "$HERE"/src/diag/odg_rexx.c; do
    o="$TMP/$(basename "$f" .c).o"
    "$CC" -std=c99 -O1 -fno-pie -DOWF_HAVE_ZLIB -I"$HERE/include" -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
        -c "$f" -o "$o"
    found=$(nm "$o" | awk '$2 ~ /^[bBdDcC]$/ { print $3 }')
    if [ -n "$found" ]; then
        echo "FAIL ${f#"$HERE"/}: keeps state between calls in: $(echo $found)"
        bad=1
    fi
done
[ "$bad" = 0 ] && echo "diagram state: no file keeps state between calls"
exit "$bad"
