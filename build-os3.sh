#!/bin/sh
# OpenWrite and OWConvert for AmigaOS 3.x with the os32 stove.
#
#   build-os3.sh [OUT_DIR]              (default build/os3)
#
# OWConvert remains 68000-compatible. The OpenWrite UI targets the A1200
# baseline (68020) and uses OpenGadTools. No FPU is required.
#
# ZLIB_SRC is a zlib source folder. DOCX/ODT input requires zlib.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
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
        # The stove's GCC 6.5 miscompiles trees.c at -Os and -O2: every
        # deflate stream (saved DOCX/ODT, PDF pictures) came out corrupt on
        # the Amiga. -O1 is right (checked on the Amiga, round trips).
        OPT=-Os
        [ "$f" = trees.c ] && OPT=-O1
        if [ -f "$ZLIB_SRC/$f" ]; then
            "$CC" -noixemul -m68000 $OPT -fno-delete-null-pointer-checks -fno-common -DCHROMIUM_ZLIB_NO_CHROMECONF \
                -I"$OUT/zlib" -I"$ZLIB_SRC" -c "$ZLIB_SRC/$f" -o "$OUT/zlib/${f%.c}.o"
            ZOBJS="$ZOBJS $OUT/zlib/${f%.c}.o"
        fi
    done
fi

# OWConvert: any 68000.
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -m68000 -Os $ZFLAGS \
    -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
    "$HERE"/libowf/src/*.c "$HERE/tools/owconvert.c" $ZOBJS \
    -o "$OUT/OWConvert"

# OpenWrite: A1200/68020 native UI.
# shellcheck disable=SC2086
"$CC" $CFLAGS_COMMON -m68020 -O2 -Wno-pointer-sign $ZFLAGS \
    -I"$HERE/include" -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
    -I"$HERE/app" -I"$HERE/third_party/opengadtools" \
    "$HERE"/libowf/src/*.c "$HERE"/src/core/*.c "$HERE"/app/*.c \
    "$HERE"/third_party/opengadtools/*.c $ZOBJS -lamiga \
    -o "$OUT/OpenWrite"

echo "$OUT/OWConvert ($(wc -c < "$OUT/OWConvert") bytes)"
echo "$OUT/OpenWrite ($(wc -c < "$OUT/OpenWrite") bytes)"
