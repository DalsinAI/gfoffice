#!/bin/sh
# OpenWrite and OWConvert for AmigaOS 3.x with the os32-gcc16 stove
# (OpenAmigaGCC 16). The compiler: OS32_GCC16, else STOVE, else
# ~/AmigaChrome/stoves/os32-gcc16; this script stops when given a GCC older
# than 16 (the os32 stove's GCC 6.5 is retired: its peephole2 clears a word
# and the long after it with one clr.l).
#
#   build-os3.sh [OUT_DIR]              (default build/os3)
#
# Both are 68040 code, as every build. The OpenWrite UI uses OpenGadTools.
# No FPU is required.
#
# ZLIB_SRC is a zlib source folder. DOCX/ODT input requires zlib.
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$HERE/specs/m68k-soft-m68040.specs"
STOVE=${OS32_GCC16:-${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
GCCV=$("$CC" -dumpversion 2>/dev/null || echo 0)
[ "${GCCV%%.*}" -ge 16 ] 2>/dev/null || { echo "$CC is GCC $GCCV: OpenWrite needs GCC 16 (the os32-gcc16 stove; set OS32_GCC16)"; exit 2; }
OUT=${1:-$HERE/build/os3}
CFLAGS_COMMON="-noixemul -std=gnu99 -Wall -Werror -fno-delete-null-pointer-checks -fno-common"
mkdir -p "$OUT"

ZFLAGS=""
ZOBJS=""
if [ -n "${ZLIB_SRC:-}" ]; then
    mkdir -p "$OUT/zlib"
    sed 's/^#if !defined(CHROMIUM_ZLIB_NO_CHROMECONF)/#if 0/' "$ZLIB_SRC/zconf.h" > "$OUT/zlib/zconf.h"
    cp "$ZLIB_SRC/zlib.h" "$OUT/zlib/"
    ZFLAGS="-DOWF_HAVE_ZLIB -DCHROMIUM_ZLIB_NO_CHROMECONF -I$OUT/zlib"
    for f in adler32.c crc32.c deflate.c trees.c zutil.c cpu_features.c inflate.c inffast.c inftrees.c compress.c uncompr.c; do
        # GCC 6.5 (the old os32 stove) miscompiled trees.c at -Os and -O2:
        # every deflate stream (saved DOCX/ODT, PDF pictures) came out
        # corrupt on the Amiga. It stays at -O1, which GCC 16 also gets right.
        OPT=-Os
        [ "$f" = trees.c ] && OPT=-O1
        if [ -f "$ZLIB_SRC/$f" ]; then
            "$CC" -noixemul -m68040 -specs=$SPECS $OPT -fno-delete-null-pointer-checks -fno-common -DCHROMIUM_ZLIB_NO_CHROMECONF \
                -I"$OUT/zlib" -I"$ZLIB_SRC" -c "$ZLIB_SRC/$f" -o "$OUT/zlib/${f%.c}.o"
            ZOBJS="$ZOBJS $OUT/zlib/${f%.c}.o"
        fi
    done
fi

# OWConvert: 68040, as every build.
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -Os $ZFLAGS \
    -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
    "$HERE"/libowf/src/*.c "$HERE/tools/owconvert.c" $ZOBJS \
    -o "$OUT/OWConvert"

# OpenWrite: the OpenGadTools shell (app/openwrite2.c) is OpenWrite since 2.0.1 (the user, 10 October
# 2026: "the rewrite is OpenWrite, not a second program"); the same core and filters.
# shellcheck disable=SC2086
APP_COMMON="$HERE/app/ow_autosave.c $HERE/app/ow_print.c $HERE/app/ow_spell.c $HERE/app/ow_stack.c"
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $ZFLAGS \
    -I"$HERE/include" -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
    -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    "$HERE"/libowf/src/*.c "$HERE"/src/core/*.c "$HERE/app/openwrite2.c" $APP_COMMON \
    "$HERE"/third_party/opengadtools/*.c $ZOBJS -lamiga \
    -o "$OUT/OpenWrite"

# The 1.x window (app/openwrite.c), as OpenWrite1, only when asked: OW1=yes build-os3.sh
if [ "${OW1:-}" = yes ]; then
"$CC" $CFLAGS_COMMON -m68040 -specs=$SPECS -O2 -Wno-pointer-sign $ZFLAGS \
    -I"$HERE/include" -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
    -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    "$HERE"/libowf/src/*.c "$HERE"/src/core/*.c "$HERE/app/openwrite.c" $APP_COMMON \
    "$HERE"/third_party/opengadtools/*.c $ZOBJS -lamiga \
    -o "$OUT/OpenWrite1"
echo "$OUT/OpenWrite1 ($(wc -c < "$OUT/OpenWrite1") bytes)"
fi

echo "$OUT/OWConvert ($(wc -c < "$OUT/OWConvert") bytes)"
echo "$OUT/OpenWrite ($(wc -c < "$OUT/OpenWrite") bytes)"
