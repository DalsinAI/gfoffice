#!/bin/sh
# GFOffice's programs take GFOpenLayout from gfopenlayout.library and nowhere else (Dale, 18 November 2026: "no
# switch, remove all the dead code and lock in just openlayout"; docs/suite/layout-library.md). They link only
# when every GFOpenLayout call their files make is one the library's link sources give (gfopenlayout
# library/link/gfopenlayout_link.c, gfopenlayout_link_core.c, gfopenlayout_link_draw.c). This reads the calls from
# the text (a name and "("), so a call the library lacks shows on the host, before an m68k link would fail on
# it. It also holds:
#   - each program's compile to $OL_PAGES and $OL_PAGES_FLAGS (pages) or $OL_DRAW and $OL_DRAW_FLAGS (drawing);
#   - each main file to asking ow_layout_open() as it starts, saying what it needs and ending with a failure
#     when that answers 0, and calling ow_layout_close() (app/ow_layoutlib.h): an older library is said, not
#     crashed into;
#   - the Amiga build (build-os3.sh, build.d/, build-amiga.sh) to naming none of GFOpenLayout's own sources and
#     having no switch back to them.
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
# says when the library is missing | the files of its own that call GFOpenLayout (globs from the repository) |
# pages (the document calls: $OL_PAGES, library 24) or draw (GFOpenLayout drawing: $OL_DRAW, library 25).
# OWConvert's compile is in parts ($OWC_PDF, $OWC_DECK), so its line names those two variables' lines instead.
cat > "$T/programs" <<'EOF'
GFOpenWrite|build-os3.sh|app/openwrite2.c|OW_LAYOUT_NEEDS|src/core/*.c app/openwrite2.c app/ow_autosave.c app/ow_print.c app/ow_spell.c app/ow_stack.c app/ow_fontwidths.c app/ow_clipboard.c|pages
GFOpenWrite1|build-os3.sh|app/openwrite.c|OW_LAYOUT_NEEDS|src/core/*.c app/openwrite.c|pages
GFOpenPresentation|build-os3.sh|app/openpresent.c|OW_LAYOUT_NEEDS|src/deck/*.c src/deckchart/*.c src/core/ow_layout.c app/openpresent.c app/opr_chartwin.c app/ow_clipboard.c|pages
GFOpenPressmaster|build-os3.sh|app/openpresent.c|OW_LAYOUT_NEEDS|src/deck/*.c src/pub/*.c libowf/pub/*.c src/core/ow_merge.c src/core/ow_layout.c app/openpresent.c app/ow_clipboard.c|pages
GFOpenBase|build.d/base.sh|app/openbase.c|OW_LAYOUT_NEEDS|src/base/*.c src/base/report/*.c src/base/preview/*.c src/core/ow_layout.c app/openbase.c app/ob_reportwin.c app/ob_previewwin.c|pages
OWConvert|build-os3.sh|tools/owc_deck.c|owc_missing("gfopenlayout.library"|src/deck/opr_layout.c src/deck/opr_pdf.c src/deck/opr_gradient.c src/core/ow_layout.c tools/owconvert.c tools/owc_*.c|pages
GFOpenDiag|build.d/diag.sh|app/opendiag.c|OW_LAYOUT_NEEDS|src/diag/odg_view.c src/diag/odg_edit.c app/opendiag.c|draw
GFOpenProject|build.d/project.sh|app/openproject.c|OW_LAYOUT_NEEDS|src/project/*.c app/openproject.c|draw
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
have_draw=0
if [ -n "$LAYOUT" ] && [ -f "$LAYOUT/library/link/gfopenlayout_link_draw.c" ]; then
    have_draw=1
    grep -E '^[a-z].*[ *]oldraw_[a-z0-9_]+\(' "$LAYOUT/library/link/gfopenlayout_link_draw.c" | grep -v ';$' |
        sed -E 's/^.*[ *](oldraw_[a-z0-9_]+)\(.*$/\1/' | sort -u > "$T/drawcalls"
else
    echo "check_library_calls.sh: no GFOpenLayout with library 25's drawing link source: drawing calls not checked"
fi

count=0
total=0
while IFS='|' read -r p script mainfile needs files kind; do
    count=$((count + 1))
    if [ "$kind" = draw ]; then var=OL_DRAW; pre='oldraw'; given="$T/drawcalls"; have=$have_draw
    else var=OL_PAGES; pre='(ol|olui)'; given="$T/calls"; have=$have_layout; fi
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
    elif grep -q 'draw/oldraw\.c\|amiga/oldraw_amiga\.c\|DOLDRAW_AMIGA' "$T/cc"; then
        say "$p names GFOpenLayout's drawing sources: a converted program takes them as \$OL_DRAW and \$OL_DRAW_FLAGS"
    elif ! grep -q "$var\\b" "$T/cc" || ! grep -q "${var}_FLAGS\\b" "$T/cc"; then
        say "$p's compile in $script does not name \$$var and \$${var}_FLAGS"
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
    [ "$have" = 1 ] || continue
    : > "$T/src"
    for g in $files; do
        # shellcheck disable=SC2086
        cat "$HERE"/$g >> "$T/src"
    done
    grep -oE "\\b${pre}_[a-z0-9_]+[[:space:]]*\\(" "$T/src" | sed 's/[[:space:](]*$//' | sort -u > "$T/used"
    # the functions the program's own files define by such a name are its own
    { grep -E "^[a-z].*[ *]${pre}_[a-z0-9_]+\\(" "$T/src" || :; } | grep -v ';$' |
        sed -E "s/^.*[ *](${pre}_[a-z0-9_]+)\\(.*\$/\\1/" | sort -u > "$T/own"
    comm -23 "$T/used" "$T/own" > "$T/need"
    comm -23 "$T/need" "$given" > "$T/missing"
    if [ -s "$T/missing" ]; then
        say "$p calls what gfopenlayout.library's link sources do not give:"
        sed 's/^/    /' "$T/missing"
    fi
    n=$(wc -l < "$T/need")
    [ "$n" -gt 0 ] || say "$p makes no GFOpenLayout call: its file list is wrong"
    echo "  $p: $n calls"
    total=$((total + n))
done < "$T/programs"

# No way back: the Amiga build compiles none of GFOpenLayout's own sources and has no switch for it
for f in "$HERE/build-os3.sh" "$HERE"/build.d/*.sh "$HERE/build-amiga.sh"; do
    grep -v '^[ \t]*#' "$f" > "$T/code"
    if grep -n 'OPENLAYOUT_SRC/\(src\|draw\|ui\|amiga\|core\)/\|\$OL_C\b\|OLDRAW_AMIGA' "$T/code" > "$T/hits"; then
        say "$(basename "$f") names GFOpenLayout's sources: the programs take gfopenlayout.library only"
        sed 's/^/    /' "$T/hits"
    fi
    if grep -n '\bOPENLAYOUT\b' "$T/code" | grep -v 'OPENLAYOUT_SRC' > "$T/hits"; then
        say "$(basename "$f") has an OPENLAYOUT switch again: there is one way, the library"
        sed 's/^/    /' "$T/hits"
    fi
done
grep -q 'OL_PAGES_FLAGS="-DOW_LAYOUT_LIBRARY"' "$HERE/build-os3.sh" || say "build-os3.sh does not build the page programs for the library"

# The package carries the library: build-amiga.sh builds gfopenlayout.library from the pinned GFOpenLayout, with
# the theme reader, into the libraries' drawer, and that library is no older than the programs ask for.
grep -q 'build-library.sh"$' "$HERE/build-amiga.sh" && grep -q 'OGT_ROOT="\$DEPS/gfopengadtools"' "$HERE/build-amiga.sh" &&
    grep -q 'cp "\$DEPS/gfopenlayout-lib/lib/gfopenlayout.library" "\$OUT/library/gfopenlayout.library"' "$HERE/build-amiga.sh" ||
    say "build-amiga.sh does not build gfopenlayout.library (with the theme reader) into \$OUT/library"
grep -q 'OGT_ROOT="\$DEPS/gfopengadtools" OPT=-Os ' "$HERE/build-amiga.sh" ||
    say "build-amiga.sh does not build gfopenlayout.library at -Os, as the ROM's is built"
for r in openamigalayout gfopengadtools; do
    head -n 1 "$HERE/deps/$r.ref" 2>/dev/null | grep -qE '^[0-9a-f]{40}$' || say "deps/$r.ref is not a commit: the shipped library must be a fixed one"
done
asks=$(sed -n 's/^#define OW_LAYOUT_LIBRARY_VERSION \([0-9]*\)$/\1/p' "$HERE/app/ow_layoutlib.h" | sort -n | tail -n 1)
[ -n "$asks" ] || say "app/ow_layoutlib.h names no OW_LAYOUT_LIBRARY_VERSION"
if [ "$have_layout" = 1 ] && [ -n "$asks" ]; then
    gives=$(sed -n 's/^#define LIB_VERSION \([0-9]*\)$/\1/p' "$LAYOUT/library/openlayout_lib.c")
    [ -n "$gives" ] && [ "$gives" -ge "$asks" ] || say "the GFOpenLayout given builds library ${gives:-?}: the programs ask for $asks"
fi

[ "$fail" = 0 ] || exit 1
if [ "$have_layout" = 1 ]; then
    echo "LAYOUT-LIBRARY-CALLS PASS $count programs, $total calls, all in gfopenlayout.library ($(wc -l < "$T/calls") given, $( [ "$have_draw" = 1 ] && wc -l < "$T/drawcalls" || echo 0) drawing)"
else
    echo "LAYOUT-LIBRARY-CALLS PASS $count programs (build lines and start-up only)"
fi
