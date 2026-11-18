#!/bin/sh
# The programs converted to gfopenlayout.library (build-os3.sh's OPENLAYOUT, docs/suite/layout-library.md) link
# with OPENLAYOUT=library only when every GFOpenLayout call their files make is one the library's link sources
# give (gfopenlayout library/link/gfopenlayout_link.c and gfopenlayout_link_core.c, library 24). This reads the
# calls from the text (a name and "("), so a call the library lacks shows on the host, before an m68k link would
# fail on it. It also holds each converted program to the rest of the conversion:
#   - its compile names $OL_PAGES and $OL_PAGES_FLAGS, never $OL_C (the source, whatever OPENLAYOUT says);
#   - its main file asks ow_layout_open() as it starts, says what it needs and ends with a failure when that
#     answers 0, and calls ow_layout_close() (app/ow_layoutlib.h): an older library in the ROM is said, not
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
say() { echo "check_library_calls.sh: $*"; fail=1; }

# Each converted program, a line: its name | the script with its compile | its main file | what its main file
# says when the library is missing | the files of its own that call GFOpenLayout (globs from the repository).
# OWConvert's compile is in parts ($OWC_PDF, $OWC_DECK), so its line names those two variables' lines instead.
cat > "$T/programs" <<'EOF'
GFOpenWrite|build-os3.sh|app/openwrite2.c|OW_LAYOUT_NEEDS|src/core/*.c app/openwrite2.c app/ow_autosave.c app/ow_print.c app/ow_spell.c app/ow_stack.c app/ow_fontwidths.c app/ow_clipboard.c
GFOpenWrite1|build-os3.sh|app/openwrite.c|OW_LAYOUT_NEEDS|src/core/*.c app/openwrite.c
GFOpenPresentation|build-os3.sh|app/openpresent.c|OW_LAYOUT_NEEDS|src/deck/*.c src/deckchart/*.c src/core/ow_layout.c app/openpresent.c app/opr_chartwin.c app/ow_clipboard.c
GFOpenPressmaster|build-os3.sh|app/openpresent.c|OW_LAYOUT_NEEDS|src/deck/*.c src/pub/*.c libowf/pub/*.c src/core/ow_merge.c src/core/ow_layout.c app/openpresent.c app/ow_clipboard.c
GFOpenBase|build.d/base.sh|app/openbase.c|OW_LAYOUT_NEEDS|src/base/*.c src/base/report/*.c src/base/preview/*.c src/core/ow_layout.c app/openbase.c app/ob_reportwin.c app/ob_previewwin.c
OWConvert|build-os3.sh|tools/owc_deck.c|owc_missing("gfopenlayout.library"|src/deck/opr_layout.c src/deck/opr_pdf.c src/deck/opr_gradient.c src/core/ow_layout.c tools/owconvert.c tools/owc_*.c
EOF

have_layout=0
if [ -n "$LAYOUT" ] && [ -f "$LAYOUT/library/link/gfopenlayout_link_core.c" ]; then
    have_layout=1
    # what the link sources define: a line that starts a function, "type name(args)"
    cat "$LAYOUT/library/link/gfopenlayout_link.c" "$LAYOUT/library/link/gfopenlayout_link_core.c" |
        grep -E '^[a-z].*[ *](ol|olui)_[a-z0-9_]+\(' | grep -v ';$' |
        sed -E 's/^.*[ *]((ol|olui)_[a-z0-9_]+)\(.*$/\1/' | sort -u > "$T/calls"
else
    echo "check_library_calls.sh: no GFOpenLayout with library 24's link sources (OPENLAYOUT_SRC): calls not checked"
fi

count=0
total=0
while IFS='|' read -r p script mainfile needs files; do
    count=$((count + 1))
    # ---- the compile ----
    if [ "$p" = OWConvert ]; then
        grep -E '^OWC_(PDF|DECK)=' "$HERE/$script" > "$T/cc" || :
    else
        awk -v want="-o \"\$OUT/$p\"" '
            /^[ \t]*"\$CC"/ { block = ""; inblock = 1 }
            inblock { block = block $0 "\n" }
            inblock && length($0) >= length(want) && substr($0, length($0) - length(want) + 1) == want {
                printf "%s", block; exit }
            inblock && !/\\$/ { inblock = 0 }' "$HERE/$script" > "$T/cc"
    fi
    if [ ! -s "$T/cc" ]; then
        say "no compile of $p found in $script"
    elif grep -q 'OL_C\b' "$T/cc"; then
        say "$p names \$OL_C: a converted program takes GFOpenLayout as \$OL_PAGES"
    elif ! grep -q 'OL_PAGES\b' "$T/cc" || ! grep -q 'OL_PAGES_FLAGS\b' "$T/cc"; then
        say "$p's compile in $script does not name \$OL_PAGES and \$OL_PAGES_FLAGS"
    fi
    # ---- the start-up ----
    open_at=$(grep -n 'ow_layout_open()' "$HERE/$mainfile" | head -n 1 | cut -d: -f1)
    if [ -z "$open_at" ] || ! grep -q 'ow_layout_close()' "$HERE/$mainfile"; then
        say "$p ($mainfile) does not ask ow_layout_open() and call ow_layout_close()"
    elif ! awk -v at="$open_at" -v needs="$needs" '
            NR > at && NR <= at + 4 && index($0, needs) { said = 1 }
            NR > at && NR <= at + 4 && /rc = 20|RETURN_FAIL|OWF_ERR_UNSUPPORTED/ { failed = 1 }
            END { exit !(said && failed) }' "$HERE/$mainfile"; then
        say "$p ($mainfile) does not say what it needs and end with a failure when ow_layout_open() answers 0"
    fi
    # ---- the calls ----
    [ "$have_layout" = 1 ] || continue
    : > "$T/src"
    for g in $files; do
        # shellcheck disable=SC2086
        cat "$HERE"/$g >> "$T/src"
    done
    grep -oE '\b(ol|olui)_[a-z0-9_]+[[:space:]]*\(' "$T/src" | sed 's/[[:space:](]*$//' | sort -u > "$T/used"
    # the functions the program's own files define by such a name are its own
    grep -E '^[a-z].*[ *](ol|olui)_[a-z0-9_]+\(' "$T/src" | grep -v ';$' |
        sed -E 's/^.*[ *]((ol|olui)_[a-z0-9_]+)\(.*$/\1/' | sort -u > "$T/own"
    comm -23 "$T/used" "$T/own" > "$T/need"
    comm -23 "$T/need" "$T/calls" > "$T/missing"
    if [ -s "$T/missing" ]; then
        say "$p calls what gfopenlayout.library's link sources do not give:"
        sed 's/^/    /' "$T/missing"
    fi
    n=$(wc -l < "$T/need")
    [ "$n" -gt 0 ] || say "$p makes no GFOpenLayout call: its file list is wrong"
    echo "  $p: $n calls"
    total=$((total + n))
done < "$T/programs"

# a program not listed here still names $OL_C: say which, so the list and the steps stay in step
left=$(grep -l '\$OL_C\b' "$HERE/build-os3.sh" "$HERE"/build.d/*.sh 2>/dev/null |
    while read -r f; do grep -v '^[ \t]*#' "$f" | grep -v '^OL_C=\|OL_C="\$OL_C \|OL_PAGES="\$OL_C ' | grep -c '\$OL_C\b' || :; done |
    awk '{ s += $1 } END { print s + 0 }')
[ "$left" = 0 ] || say "$left compile line(s) still name \$OL_C: convert them and list the program here"

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
    say "app/ow_layoutlib.c with the source compiled in does not answer 1"
fi
default=$(sed -n 's/^OPENLAYOUT=\${OPENLAYOUT:-\([a-z]*\)}$/\1/p' "$HERE/build-os3.sh")
case "$default" in
library|source) echo "check_library_calls.sh: build-os3.sh builds converted programs with OPENLAYOUT=$default by default" ;;
*) say "build-os3.sh has no OPENLAYOUT default of library or source" ;;
esac

[ "$fail" = 0 ] || exit 1
if [ "$have_layout" = 1 ]; then
    echo "LAYOUT-LIBRARY-CALLS PASS $count programs, $total calls, all in gfopenlayout.library ($(wc -l < "$T/calls") given)"
else
    echo "LAYOUT-LIBRARY-CALLS PASS $count programs (build lines and start-up only)"
fi
