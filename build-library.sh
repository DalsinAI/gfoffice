#!/bin/sh
# owf.library (libowf as a resident library) and OWFLibTest, for AmigaOS 3.x
# on a 68040 (no FPU needed), with the os32-gcc16 stove (OpenAmigaGCC 16).
#
#   build-library.sh [OUT_DIR]          (default build/library)
#
#   OUT_DIR/owf.library          for LIBS:
#   OUT_DIR/owf.library.map      the linker's map
#   OUT_DIR/tests/OWFLibTest     opens LIBS:owf.library and runs its calls,
#                                from two tasks at once (tests/library)
#
# The library links with no C library: libowf's sources, its files over
# dos.library (libowf/library/owf_sys_dos.c), one Exec pool behind a
# semaphore for every document's memory and its own string and printf
# calls (owf_runtime.c, owf_libc.c). Deflate and inflate are z.library's,
# ours (amigachrome-guest libraries/openzlib), opened when first needed.
#
#   OS32_GCC16 or STOVE   the stove (default ~/AmigaChrome/stoves/os32-gcc16)
#   OPENSDK_SRC           an openamigasdk checkout: when set, the SDK headers
#                         in libowf/library/sdk and libowf/amiga/sdk are
#                         checked against their SFDs with its ndkgen.py
#
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}}
BIN="$STOVE/prefix/bin"
CC="$BIN/m68k-amigaos-gcc"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: owf.library needs GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
OUT=${1:-$HERE/build/library}
SPECS="$HERE/specs/m68k-soft-m68040.specs"
OWF="$HERE/libowf"
LIB="$OWF/library"
mkdir -p "$OUT/tests"

if [ -n "${OPENSDK_SRC:-}" ]; then
    # the headers are made from the SFDs; a stale one fails the build
    for sfd in "$LIB/owf_lib.sfd" "$OWF/amiga/z_lib.sfd"; do
        python3 "$OPENSDK_SRC/OpenNDK/tools/ndkgen.py" --check --out "$(dirname "$sfd")/sdk" "$sfd" ||
            { echo "build-library.sh: $(dirname "$sfd")/sdk is stale: run scripts/gen-sdk.sh" >&2; exit 1; }
    done
fi

INC="-I$OWF/include -I$OWF/src -I$LIB -I$OWF/amiga/sdk/Include_H"
DEFS="-DOWF_LIBRARY_BUILD -DOWF_HAVE_ZLIB -DOWF_ZLIBRARY"
SRC=""
# The workbook code (owf_book.c and its formats) joins the library with the
# library's workbook calls and their double maths, next.
for f in "$OWF"/src/*.c; do
    case "$f" in
    */owf_sys.c | */owf_book.c | */owf_cfb.c | */owf_xlfunc.c | */in_xls.c | */out_csv.c) ;;
    *) SRC="$SRC $f" ;;
    esac
done

# owf_library.c first: its start() is the first code. -nostdlib: nothing of a
# C library; -fno-tree-loop-distribute-patterns: no memset or memcpy calls
# made out of the runtime's own loops.
# shellcheck disable=SC2086
"$CC" -m68040 -specs="$SPECS" -mcrt=nix20 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common \
    -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -fno-tree-loop-distribute-patterns \
    -Wall -Werror -Wno-pointer-sign -nostartfiles -nostdlib $DEFS $INC \
    "$LIB/owf_library.c" $SRC "$LIB/owf_sys_dos.c" "$LIB/owf_runtime.c" "$LIB/owf_libc.c" \
    "$OWF/amiga/owf_zlib_amiga.c" \
    -lgcc -Wl,-Map,"$OUT/owf.library.map" -o "$OUT/owf.library"
echo "$OUT/owf.library ($(wc -c < "$OUT/owf.library") bytes)"
UNDEF=$("$BIN/m68k-amigaos-nm" -u "$OUT/owf.library" || true)
if [ -n "$UNDEF" ]; then
    echo "build-library.sh: owf.library has undefined symbols:" >&2
    echo "$UNDEF" >&2
    exit 1
fi

# OWFLibTest: an ordinary program that opens the library from LIBS:
"$CC" -m68040 -specs="$SPECS" -noixemul -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common \
    -Wall -Werror -I"$OWF/include" -I"$LIB/sdk/Include_H" \
    "$HERE/tests/library/owf_lib_test.c" -o "$OUT/tests/OWFLibTest"
echo "$OUT/tests/OWFLibTest ($(wc -c < "$OUT/tests/OWFLibTest") bytes)"
sha256sum "$OUT/owf.library" "$OUT/tests/OWFLibTest"
