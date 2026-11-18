#!/bin/sh
# The programs that open gfopenlayout.library (build-os3.sh's OL_LIB, docs/suite/layout-library.md) link only when
# every GFOpenLayout call their files make is one the library's link sources give (gfopenlayout
# library/link/gfopenlayout_link.c and gfopenlayout_link_core.c, library 24). This reads the calls from the text
# (a name and "("), so a call the library lacks shows on the host, before an m68k link would fail on it. It also
# holds build-os3.sh to it: a converted program's compile names $OL_LIB and never $OL_C (the source compiled in).
#   tests/layout/check_library_calls.sh [GFOpenLayout source]
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
LAYOUT=${1:-${OPENLAYOUT_SRC:-}}
HERE=$(cd "$(dirname "$0")/../.." && pwd)
export LC_ALL=C
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0

# each converted program: its name in build-os3.sh, then the files of its own that call GFOpenLayout
programs="GFOpenWrite"
files_GFOpenWrite="$HERE/src/core/*.c $HERE/app/openwrite2.c $HERE/app/ow_autosave.c $HERE/app/ow_print.c
    $HERE/app/ow_spell.c $HERE/app/ow_stack.c $HERE/app/ow_fontwidths.c $HERE/app/ow_clipboard.c"

# build-os3.sh: the program's compile (from its "$CC" line to its -o line) takes the library's calls
for p in $programs; do
    awk -v want="-o \"\$OUT/$p\"" '
        /^"\$CC"/ { block = ""; inblock = 1 }
        inblock { block = block $0 "\n" }
        inblock && $0 ~ /-o "\$OUT\/[A-Za-z0-9]+"$/ {
            if (substr($0, length($0) - length(want) + 1) == want) { printf "%s", block; exit }
        }
        inblock && !/\\$/ { inblock = 0 }' "$HERE/build-os3.sh" > "$T/$p.cc"
    if [ ! -s "$T/$p.cc" ]; then
        echo "check_library_calls.sh: no compile of $p found in build-os3.sh"; fail=1
    elif grep -q 'OL_C\b' "$T/$p.cc"; then
        echo "check_library_calls.sh: $p compiles GFOpenLayout's source in (\$OL_C): it opens gfopenlayout.library"; fail=1
    elif ! grep -q 'OL_LIB\b' "$T/$p.cc"; then
        echo "check_library_calls.sh: $p does not link gfopenlayout.library's calls (\$OL_LIB)"; fail=1
    fi
done

if [ -z "$LAYOUT" ] || [ ! -f "$LAYOUT/library/link/gfopenlayout_link_core.c" ]; then
    echo "check_library_calls.sh: no GFOpenLayout with library 24's link sources (OPENLAYOUT_SRC): calls not checked"
    [ "$fail" = 0 ] && echo "LAYOUT-LIBRARY-CALLS PASS (build lines only)"
    exit "$fail"
fi
# what the link sources define: a line that starts a function, "type name(args)"
cat "$LAYOUT/library/link/gfopenlayout_link.c" "$LAYOUT/library/link/gfopenlayout_link_core.c" |
    grep -E '^[a-z].*[ *](ol|olui)_[a-z0-9_]+\(' | grep -v ';$' |
    sed -E 's/^.*[ *]((ol|olui)_[a-z0-9_]+)\(.*$/\1/' | sort -u > "$T/calls"
total=0
for p in $programs; do
    eval "files=\$files_$p"
    # shellcheck disable=SC2086
    cat $files | grep -oE '\b(ol|olui)_[a-z0-9_]+[[:space:]]*\(' | sed 's/[[:space:](]*$//' | sort -u > "$T/used"
    # the functions the program's own files define by such a name are its own
    # shellcheck disable=SC2086
    cat $files | grep -E '^[a-z].*[ *](ol|olui)_[a-z0-9_]+\(' | grep -v ';$' |
        sed -E 's/^.*[ *]((ol|olui)_[a-z0-9_]+)\(.*$/\1/' | sort -u > "$T/own"
    comm -23 "$T/used" "$T/own" > "$T/need"
    comm -23 "$T/need" "$T/calls" > "$T/missing"
    if [ -s "$T/missing" ]; then
        echo "check_library_calls.sh: $p calls what gfopenlayout.library's link sources do not give:"
        sed 's/^/    /' "$T/missing"
        fail=1
    fi
    n=$(wc -l < "$T/need")
    [ "$n" -gt 0 ] || { echo "check_library_calls.sh: $p makes no GFOpenLayout call: its file list is wrong"; fail=1; }
    total=$((total + n))
done
[ "$fail" = 0 ] && echo "LAYOUT-LIBRARY-CALLS PASS $total calls, all in gfopenlayout.library ($(wc -l < "$T/calls") given)"
exit "$fail"
