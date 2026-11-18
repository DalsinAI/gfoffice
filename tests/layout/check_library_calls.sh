#!/bin/sh
# The programs converted to gfopenlayout.library (build-os3.sh's OPENLAYOUT, docs/suite/layout-library.md) link
# with OPENLAYOUT=library only when every GFOpenLayout call their files make is one the library's link sources
# give (gfopenlayout library/link/gfopenlayout_link.c and gfopenlayout_link_core.c, library 24). This reads the
# calls from the text (a name and "("), so a call the library lacks shows on the host, before an m68k link would
# fail on it. It also holds each converted program to the rest of the conversion:
#   - its compile in build-os3.sh names $OL_PAGES and $OL_PAGES_FLAGS, never $OL_C (the source, whatever
#     OPENLAYOUT says);
#   - its main file asks ow_layout_open() as it starts, names OW_LAYOUT_NEEDS in a requester and ends with a
#     failure when that answers 0, and calls ow_layout_close() (app/ow_layoutlib.h): an older library in the ROM is said, not
#     crashed into;
#   - app/ow_layoutlib.c, built with the source compiled in, asks for nothing and answers 1.
# It prints build-os3.sh's default for OPENLAYOUT, so a log shows which way a release is built.
#   tests/layout/check_library_calls.sh [GFOpenLayout source] [CC]
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
LAYOUT=${1:-${OPENLAYOUT_SRC:-}}
CC=${2:-${CC:-cc}}
HERE=$(cd "$(dirname "$0")/../.." && pwd)
export LC_ALL=C
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0

# each converted program: its name in build-os3.sh, then the files of its own that call GFOpenLayout
programs="GFOpenWrite"
main_GFOpenWrite="$HERE/app/openwrite2.c"
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
        echo "check_library_calls.sh: $p names \$OL_C: a converted program takes GFOpenLayout as \$OL_PAGES"; fail=1
    elif ! grep -q 'OL_PAGES\b' "$T/$p.cc" || ! grep -q 'OL_PAGES_FLAGS\b' "$T/$p.cc"; then
        echo "check_library_calls.sh: $p's compile does not name \$OL_PAGES and \$OL_PAGES_FLAGS"; fail=1
    fi
    # the program asks for the library first and says so when it is missing or older
    eval "mainfile=\$main_$p"
    open_at=$(grep -n 'ow_layout_open()' "$mainfile" | head -n 1 | cut -d: -f1)
    if [ -z "$open_at" ] || ! grep -q 'ow_layout_close()' "$mainfile" || ! grep -q 'OW_LAYOUT_NEEDS' "$mainfile"; then
        echo "check_library_calls.sh: $p ($mainfile) does not ask ow_layout_open(), name OW_LAYOUT_NEEDS and call ow_layout_close()"
        fail=1
    elif ! awk -v at="$open_at" 'NR > at && NR <= at + 4 && /RETURN_FAIL|rc = 20|return 20/ { ok = 1 } END { exit !ok }' "$mainfile"; then
        echo "check_library_calls.sh: $p does not end with a failure when ow_layout_open() answers 0"; fail=1
    fi
done
# ow_layoutlib.c with the source compiled in: nothing to open
cat > "$T/t.c" <<'EOF'
#include "ow_layoutlib.h"
int main(void)
{
    int ok = ow_layout_open() == 1 && ow_layout_is_library() == 0;
    ow_layout_close();
    ow_layout_close();
    return ok && ow_layout_open() == 1 && OW_LAYOUT_LIBRARY_VERSION == 24 ? 0 : 1;
}
EOF
if ! "$CC" -std=gnu99 -Wall -Werror -I"$HERE/app" "$T/t.c" "$HERE/app/ow_layoutlib.c" -o "$T/t" || ! "$T/t"; then
    echo "check_library_calls.sh: app/ow_layoutlib.c with the source compiled in does not answer 1"; fail=1
fi
default=$(sed -n 's/^OPENLAYOUT=\${OPENLAYOUT:-\([a-z]*\)}$/\1/p' "$HERE/build-os3.sh")
case "$default" in
library|source) echo "check_library_calls.sh: build-os3.sh builds converted programs with OPENLAYOUT=$default by default" ;;
*) echo "check_library_calls.sh: build-os3.sh has no OPENLAYOUT default of library or source"; fail=1 ;;
esac

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
