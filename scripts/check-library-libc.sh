#!/bin/sh
# The libraries link with no C library (build-library.sh): every C library
# call libowf's archive makes must be one the libraries' runtime gives
# (libowf/library/owf_runtime.c, owf_runtime_math.c, owf_sys_dos.c). This
# compiles the archive's sources for the host, as build-library.sh lists
# them, takes the names they call and do not define (nm -u), and fails on
# any the runtime does not define, before an OS3 link would. Host only:
# cc and nm.
#
#   scripts/check-library-libc.sh
#
# MIT, Copyright (c) 2026 AmiCradle Team.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
OWF="$HERE/libowf"
LIB="$OWF/library"
# the same order as sort gives, whatever the locale (comm needs it)
export LC_ALL=C
CC=${CC:-cc}
NM=${NM:-nm}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
FLAGS="-std=c99 -O1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -DOWF_LIBRARY_BUILD -I$OWF/include -I$OWF/src -I$LIB -I$HERE/include"
mkdir -p "$T/src" "$T/rt"
# owfdiag.library's too: the diagram model and formats, and GFOpenDiag's editing core (build-library.sh's DIAG list);
# libowf/pub and the merge table: owfpub.library 3.8 (build-library.sh)
. "$HERE/scripts/owfdiag-sources.sh"
for f in "$OWF"/src/*.c "$OWF"/calc/*.c $OWFDIAG_SRC "$OWF"/pub/*.c "$HERE/src/core/ow_merge.c"; do
    case "$f" in
    */owf_sys.c | */owf_kind.c) ;;
    *) "$CC" $FLAGS -c "$f" -o "$T/src/$(basename "$(dirname "$f")")_$(basename "$f" .c).o" 2>"$T/err" || {
           echo "check-library-libc: $f does not compile for the host:"; cat "$T/err"; exit 1; } ;;
    esac
done
# what the runtime defines: its own source's functions, by name (the files need Amiga headers, so they are read,
# not compiled)
for f in "$LIB/owf_runtime.c" "$LIB/owf_runtime_math.c" "$LIB/owf_sys_dos.c"; do
    sed -n 's/^[A-Za-z_][A-Za-z0-9_ \*]*[ \*]\([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p' "$f"
done | sort -u > "$T/given"
# what the archive's objects define between them
"$NM" -g --defined-only "$T"/src/*.o | awk 'NF >= 3 { print $3 }' | sort -u > "$T/defined"
# (glibc names C99 scanf and C23 strtol __isoc99_sscanf and __isoc23_strtol: the same calls)
"$NM" -u "$T"/src/*.o | awk '$1 == "U" { sub(/^__isoc(99|23)_/, "", $2); print $2 }' | sort -u > "$T/called"
# names a host compiler makes on its own, not calls in libowf's sources; the 68k's are in libgcc
cat > "$T/host" <<'EOT'
_GLOBAL_OFFSET_TABLE_
__stack_chk_fail
EOT
sort -u -o "$T/host" "$T/host"
comm -23 "$T/called" "$T/defined" | comm -23 - "$T/given" | comm -23 - "$T/host" > "$T/missing"
if [ -s "$T/missing" ]; then
    echo "check-library-libc: libowf calls C library names the libraries' runtime does not give"
    echo "(add them to libowf/library/owf_runtime.c, or use one it has):"
    while read -r name; do
        printf '  %s:' "$name"
        for o in "$T"/src/*.o; do "$NM" -u "$o" | awk '$1 == "U" { sub(/^__isoc(99|23)_/, "", $2); print $2 }' | grep -qx "$name" && printf ' %s' "$(basename "$o" .o)"; done
        echo
    done < "$T/missing"
    exit 1
fi
echo "check-library-libc: $(wc -l < "$T/called" | tr -d ' ') names called, all given by the archive or the runtime"
