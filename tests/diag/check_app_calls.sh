#!/bin/sh
# GFOpenDiag on owfdiag.library (OWF=library, build.d/diag.sh) links only when every libowf or editing-core call its
# own files make is one of the library's (libowf/library/owfdiag_lib.sfd): the canvas and mouse
# (src/diag/odg_view.c, odg_edit.c), compiled for the host, by their undefined names (nm -u), and the window
# (app/opendiag.c, Amiga only), by the owf_ and odg_ calls its text makes. A call the library lacks shows here,
# before an m68k link would fail on it. odg_port_point is opendiag_proto.h's, on top of odg_port_xy.
#   tests/diag/check_app_calls.sh [CC] [GFOpenLayout source]
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
CC=${1:-cc}
LAYOUT=${2:-${OPENLAYOUT_SRC:-}}
HERE=$(cd "$(dirname "$0")/../.." && pwd)
export LC_ALL=C
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
# the library's calls, and what the program has of its own
sed -n 's/^[^=*].*[ *]\([a-z_][a-z0-9_]*\)(.*/\1/p' "$HERE/libowf/library/owfdiag_lib.sfd" > "$T/calls"
echo odg_port_point >> "$T/calls"
sort -u -o "$T/calls" "$T/calls"
: > "$T/used"
if [ -n "$LAYOUT" ] && [ -f "$LAYOUT/include/gfopenlayout_draw.h" ]; then
    for f in odg_view odg_edit; do
        "$CC" -std=gnu99 -c -I"$HERE/include" -I"$HERE/libowf/include" -I"$LAYOUT/include" -I"$LAYOUT/draw" \
            "$HERE/src/diag/$f.c" -o "$T/$f.o"
    done
    nm -g --defined-only "$T"/*.o | awk 'NF >= 3 { print $3 }' | sort -u > "$T/own"
    nm -u "$T"/*.o | awk '$1 == "U" { print $2 }' | grep -E '^(owf|odg)_' | sort -u | comm -23 - "$T/own" >> "$T/used"
else
    echo "check_app_calls.sh: no GFOpenLayout (OPENLAYOUT_SRC): the canvas and mouse are not checked"
fi
# app/opendiag.c: its calls (a name and "("), less the functions it and the canvas and mouse define
grep -oE '\b(owf|odg)_[a-z0-9_]+[[:space:]]*\(' "$HERE/app/opendiag.c" | sed 's/[[:space:](]*$//' | sort -u > "$T/app"
cat "$HERE/app/opendiag.c" "$HERE/src/diag/odg_view.c" "$HERE/src/diag/odg_edit.c" |
    grep -oE '^[a-z].*\b(owf|odg)_[a-z0-9_]+\(' | grep -oE '(owf|odg)_[a-z0-9_]+\($' | tr -d '(' |
    sort -u > "$T/appown"
# owf_open.h's owf_open and owf_close are the program's own
printf 'owf_close\nowf_open\n' >> "$T/appown"
sort -u -o "$T/appown" "$T/appown"
comm -23 "$T/app" "$T/appown" >> "$T/used"
sort -u -o "$T/used" "$T/used"
comm -23 "$T/used" "$T/calls" > "$T/missing"
if [ -s "$T/missing" ]; then
    echo "check_app_calls.sh: GFOpenDiag calls what owfdiag.library has no call for:"
    sed 's/^/  /' "$T/missing"
    exit 1
fi
echo "check_app_calls.sh: GFOpenDiag's $(wc -l < "$T/used" | tr -d ' ') calls into libowf and the editing core are all owfdiag.library's"
