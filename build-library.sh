#!/bin/sh
# GFOffice's libraries (docs/suite/owf-libraries.md): libowf split along the
# programs' lines into four shared libraries, and their tests, for AmigaOS
# 3.x on a 68040 (no FPU needed), with the os32-gcc16 stove (OpenAmigaGCC 16).
#
#   build-library.sh [OUT_DIR]          (default build/library)
#
#   OUT_DIR/owfwrite.library     documents, OpenWrite's: DOCX, ODT, the Amiga
#                                word processors' files, text, HTML, PDF out
#   OUT_DIR/owfcalc.library      workbooks, OpenCalc's: XLSX, ODS, XLS, CSV,
#                                charts and the formula engine
#   OUT_DIR/owfpub.library       Publisher files, OpenPublisher's
#   OUT_DIR/owfpresent.library   presentations, OpenPresentation's: the deck
#                                model, ODP and the page PDF writer
#   OUT_DIR/<name>.library.map   each one's linker map
#   OUT_DIR/owf.library          owf.library 2, the one library before the
#                                split, until OpenUp ships the four
#                                (OWF_LEGACY=no leaves it out)
#   OUT_DIR/tests/OWFLibsTest    opens the four from LIBS: and runs their
#                                calls, from two tasks at once
#   OUT_DIR/tests/OWFLibTest     the same for owf.library 2
#                                (tests/library), with their samples/ and
#                                fixtures/ beside them
#
# The libraries link with no C library: libowf's sources, its files over
# dos.library (libowf/library/owf_sys_dos.c), one Exec pool behind a
# semaphore for every document's memory (one per library: what a library
# allocates goes back to the same library's owf_free) and its own string,
# printf and sscanf calls (owf_runtime.c, owf_libc.c). Deflate and inflate
# are z.library's, ours (amigachrome-guest libraries/openzlib), opened when
# first needed; without it, packages inflate with libowf's own
# (owf_inflate.c). Doubles (workbooks, the formula engine, .pub) use the
# library's own soft double arithmetic (owf_softdouble.c: this toolchain's
# libgcc has none, and libnix's calls the mathieee libraries in the
# caller's task), and maths and strtod (owf_libm.c, named in
# owf_runtime_math.c).
#
# libowf is compiled once into an archive (OUT_DIR/obj/libowf.a); each
# library is its own file (libowf/library/<name>_library.c: its calls,
# made from <name>_lib.sfd by scripts/gen-lib-calls.py, and
# owf_libbase.h) linked against it, so it takes only the files its calls
# reach: a new libowf file joins whichever library uses it, with no list
# to edit here. owf_kind.c is built once per library, with the format
# tables that library has (OWF_KIND_NO_DOCUMENTS, OWF_KIND_NO_WORKBOOKS).
#
#   OS32_GCC16 or STOVE   the stove (default ~/AmigaChrome/stoves/os32-gcc16)
#   OPENSDK_SRC           an openamigasdk checkout: when set, the SDK headers
#                         in libowf/library/sdk and libowf/amiga/sdk are
#                         checked against their SFDs with its ndkgen.py, and
#                         the libraries' calls (<name>_calls.h) with
#                         scripts/gen-lib-calls.py
#   OWF_LEGACY            no: owf.library 2 and OWFLibTest are not built
#
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}}
BIN="$STOVE/prefix/bin"
CC="$BIN/m68k-amigaos-gcc"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: GFOffice's libraries need GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
OUT=${1:-$HERE/build/library}
SPECS="$HERE/specs/m68k-soft-m68040.specs"
OWF="$HERE/libowf"
LIB="$OWF/library"
LIBS="owfwrite owfcalc owfpub owfpresent"
mkdir -p "$OUT/tests"

if [ -n "${OPENSDK_SRC:-}" ]; then
    # the headers are made from the SFDs; a stale one fails the build
    for sfd in "$LIB/owf_lib.sfd" "$OWF/amiga/z_lib.sfd" "$LIB/owfwrite_lib.sfd" "$LIB/owfcalc_lib.sfd" \
               "$LIB/owfpub_lib.sfd" "$LIB/owfpresent_lib.sfd"; do
        python3 "$OPENSDK_SRC/OpenNDK/tools/ndkgen.py" --check --out "$(dirname "$sfd")/sdk" "$sfd" ||
            { echo "build-library.sh: $(dirname "$sfd")/sdk is stale: run scripts/gen-sdk.sh" >&2; exit 1; }
    done
    python3 "$HERE/scripts/gen-lib-calls.py" --check "$LIB/owfwrite_lib.sfd" "$LIB/owfcalc_lib.sfd" \
        "$LIB/owfpub_lib.sfd" "$LIB/owfpresent_lib.sfd" || exit 1
fi

INC="-I$OWF/include -I$OWF/src -I$OWF/calc -I$LIB -I$OWF/amiga/sdk/Include_H"
DEFS="-DOWF_LIBRARY_BUILD -DOWF_HAVE_ZLIB -DOWF_ZLIBRARY"
# -nostdlib: nothing of a C library; -fno-tree-loop-distribute-patterns: no memset or memcpy calls made out of
# the runtime's own loops; -fno-toplevel-reorder: each library's start() stays its first code.
# -fno-late-combine-instructions: see build-os3.sh (GCC 16's late-combine made "move.b (a2)+,(a0,a2.l)" in
# owf_number_text and "move.b (a0)+,(a0,d2.l)" in owf_booknum.c, each a byte off on the 68k).
LIBFLAGS="-m68040 -specs=$SPECS -mcrt=nix20 -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common
    -fno-late-combine-instructions -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin
    -fno-tree-loop-distribute-patterns -Wall -Werror -Wno-pointer-sign -nostartfiles -nostdlib"

# ---- libowf, once, into an archive ------------------------------------------------------------------------------
# every file of libowf/src and of the formula engine (new ones arrive as new files) but owf_sys.c (the C library's;
# owf_sys_dos.c instead) and owf_kind.c (built per library, below), and the libraries' runtime
OBJ="$OUT/obj"
rm -rf "$OBJ"
mkdir -p "$OBJ"
SRC=""
for f in "$OWF"/src/*.c "$OWF"/calc/*.c; do
    case "$f" in
    */owf_sys.c | */owf_kind.c) ;;
    *) SRC="$SRC $f" ;;
    esac
done
SRC="$SRC $LIB/owf_sys_dos.c $LIB/owf_runtime.c $LIB/owf_runtime_math.c $LIB/owf_libc.c $LIB/owf_libm.c"
SRC="$SRC $LIB/owf_softdouble.c $OWF/amiga/owf_zlib_amiga.c"
# shellcheck disable=SC2086
(cd "$OBJ" && "$CC" $LIBFLAGS $DEFS $INC -c $SRC)
"$BIN/m68k-amigaos-ar" rcs "$OBJ/libowf.a" "$OBJ"/*.o

# ---- the four libraries -----------------------------------------------------------------------------------------
for name in $LIBS; do
    case "$name" in
    owfwrite) KIND="-DOWF_KIND_NO_WORKBOOKS" ;;
    owfcalc) KIND="-DOWF_KIND_NO_DOCUMENTS" ;;
    *) KIND="-DOWF_KIND_NO_DOCUMENTS -DOWF_KIND_NO_WORKBOOKS" ;;
    esac
    # shellcheck disable=SC2086
    "$CC" $LIBFLAGS $DEFS $KIND $INC -c "$OWF/src/owf_kind.c" -o "$OBJ/owf_kind_$name.o"
    # the library's own file first: its start() is the first code
    # shellcheck disable=SC2086
    "$CC" $LIBFLAGS $DEFS $INC "$LIB/${name}_library.c" "$OBJ/owf_kind_$name.o" "$OBJ/libowf.a" -lgcc \
        -Wl,-Map,"$OUT/$name.library.map" -o "$OUT/$name.library"
    echo "$OUT/$name.library ($(wc -c < "$OUT/$name.library") bytes)"
    UNDEF=$("$BIN/m68k-amigaos-nm" -u "$OUT/$name.library" || true)
    if [ -n "$UNDEF" ]; then
        echo "build-library.sh: $name.library has undefined symbols:" >&2
        echo "$UNDEF" >&2
        exit 1
    fi
done

# ---- owf.library 2, the one library before the split -------------------------------------------------------------
# Built as it was (every libowf file but the deck's) until OpenUp ships the four; OWF_LEGACY=no leaves it out.
if [ "${OWF_LEGACY:-yes}" != no ]; then
    LSRC=""
    for f in "$OWF"/src/*.c; do
        case "$f" in
        */owf_sys.c) ;;   # owf_sys_dos.c instead
        */owf_deck.c | */in_odp.c | */out_odp.c | */in_pptx.c | */owf_pdf.c | */owf_pdffonts.c) ;;   # owfpresent.library's
        *) LSRC="$LSRC $f" ;;
        esac
    done
    for f in "$OWF"/calc/*.c; do
        LSRC="$LSRC $f"
    done
    # shellcheck disable=SC2086
    "$CC" $LIBFLAGS $DEFS $INC \
        "$LIB/owf_library.c" $LSRC "$LIB/owf_sys_dos.c" "$LIB/owf_runtime.c" "$LIB/owf_runtime_math.c" "$LIB/owf_libc.c" \
        "$LIB/owf_libm.c" "$LIB/owf_softdouble.c" "$OWF/amiga/owf_zlib_amiga.c" \
        -lgcc -Wl,-Map,"$OUT/owf.library.map" -o "$OUT/owf.library"
    echo "$OUT/owf.library ($(wc -c < "$OUT/owf.library") bytes)"
    UNDEF=$("$BIN/m68k-amigaos-nm" -u "$OUT/owf.library" || true)
    if [ -n "$UNDEF" ]; then
        echo "build-library.sh: owf.library has undefined symbols:" >&2
        echo "$UNDEF" >&2
        exit 1
    fi
fi

# ---- the tests: ordinary programs that open the libraries from LIBS: ------------------------------------------------
# with their samples and fixtures beside them (PROGDIR:samples, PROGDIR:fixtures).
# -fno-builtin-strcat: GCC 16 expanded owf_lib_checks.c's strcat into a local array as
# "&want + strlen(want)" and its vregs pass left that address's virtual-stack-vars in place, so
# combine dropped the frame and the copy went to an absolute address (25 Oct 2026).
TESTFLAGS="-m68040 -specs=$SPECS -noixemul -O2 -std=gnu99 -fno-delete-null-pointer-checks -fno-common
    -fno-late-combine-instructions -fno-builtin-strcat -Wall -Werror -I$OWF/include -I$LIB/sdk/Include_H
    -I$HERE/tests/library"
T="$OBJ/tests"
mkdir -p "$T"
# OWFLibsTest: one file per library (each file's calls go to one library: owf_proto.h), owf_lib_checks.c's
# workbook and .pub checks built for owfcalc.library and owfpub.library
# shellcheck disable=SC2086
"$CC" $TESTFLAGS -c "$HERE/tests/library/owf_libs_test.c" -o "$T/owf_libs_test.o"
for name in write calc pub present; do
    up=$(echo "$name" | tr a-z A-Z)
    # shellcheck disable=SC2086
    "$CC" $TESTFLAGS -DOWF_USE_${up}_LIBRARY -c "$HERE/tests/library/owf_libs_$name.c" -o "$T/owf_libs_$name.o"
done
# shellcheck disable=SC2086
"$CC" $TESTFLAGS -DOWF_USE_CALC_LIBRARY -DOWF_LIB_CHECKS_PART=1 -c "$HERE/tests/library/owf_lib_checks.c" \
    -o "$T/owf_lib_checks_calc.o"
# shellcheck disable=SC2086
"$CC" $TESTFLAGS -DOWF_USE_PUB_LIBRARY -DOWF_LIB_CHECKS_PART=3 -c "$HERE/tests/library/owf_lib_checks.c" \
    -o "$T/owf_lib_checks_pub.o"
# shellcheck disable=SC2086
"$CC" $TESTFLAGS "$T"/owf_libs_*.o "$T/owf_lib_checks_calc.o" "$T/owf_lib_checks_pub.o" -o "$OUT/tests/OWFLibsTest"
echo "$OUT/tests/OWFLibsTest ($(wc -c < "$OUT/tests/OWFLibsTest") bytes)"
if [ "${OWF_LEGACY:-yes}" != no ]; then
    # shellcheck disable=SC2086
    "$CC" $TESTFLAGS -DOWF_USE_LIBRARY \
        "$HERE/tests/library/owf_lib_test.c" "$HERE/tests/library/owf_lib_checks.c" -o "$OUT/tests/OWFLibTest"
    echo "$OUT/tests/OWFLibTest ($(wc -c < "$OUT/tests/OWFLibTest") bytes)"
fi
mkdir -p "$OUT/tests/samples" "$OUT/tests/fixtures"
cp "$HERE/samples/OpenWrite-Tour.docx" "$HERE/samples/OpenWrite-Tour.odt" "$OUT/tests/samples/"
cp "$HERE/tests/book/fixtures/book.xls" "$HERE/tests/book/fixtures/book-lo.xlsx" \
    "$HERE/tests/book/fixtures/book-lo.ods" "$HERE/tests/pub/samples/Sample.pub" \
    "$HERE/tests/deck/fixtures/libreoffice.odp" "$OUT/tests/fixtures/"
for name in $LIBS; do
    sha256sum "$OUT/$name.library"
done
[ "${OWF_LEGACY:-yes}" = no ] || sha256sum "$OUT/owf.library" "$OUT/tests/OWFLibTest"
sha256sum "$OUT/tests/OWFLibsTest"
