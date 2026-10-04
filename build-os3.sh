#!/bin/sh
# OWConvert for AmigaOS 3.x on any 68000, with the os32 stove (bebbo's
# m68k-amigaos-gcc 6.5, libnix). No FPU needed: the filters use no floating
# point.
#   build-os3.sh [OUT_DIR]                    (default build/os3)
# ZLIB_SRC is a zlib 1.3 source folder: needed to open ODT and DOCX files,
# which are always compressed. Without it OWConvert still saves them
# (stored, which every reader accepts) but cannot open them.
# MIT, Copyright (c) 2026 Dalsin Limited.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$HERE/build/os3}
CFLAGS="-noixemul -m68000 -std=gnu99 -Wall -Werror -Os -fno-common"
mkdir -p "$OUT"

ZFLAGS=""
ZSRCS=""
if [ -n "${ZLIB_SRC:-}" ]; then
    mkdir -p "$OUT/zlib"
    # Chromium's zlib keeps its symbol renaming in zconf.h; turn it off.
    sed 's/^#if !defined(CHROMIUM_ZLIB_NO_CHROMECONF)/#if 0/' "$ZLIB_SRC/zconf.h" > "$OUT/zlib/zconf.h"
    cp "$ZLIB_SRC/zlib.h" "$OUT/zlib/"
    ZFLAGS="-DOWF_HAVE_ZLIB -DCHROMIUM_ZLIB_NO_CHROMECONF -I$OUT/zlib"
    # zlib is not ours: its own warnings are not errors here.
    for f in adler32.c crc32.c deflate.c trees.c zutil.c cpu_features.c inflate.c inffast.c inftrees.c; do
        if [ -f "$ZLIB_SRC/$f" ]; then
            "$CC" -noixemul -m68000 -Os -fno-common -DCHROMIUM_ZLIB_NO_CHROMECONF -I"$OUT/zlib" -I"$ZLIB_SRC" \
                -c "$ZLIB_SRC/$f" -o "$OUT/zlib/${f%.c}.o"
            ZSRCS="$ZSRCS $OUT/zlib/${f%.c}.o"
        fi
    done
fi

# shellcheck disable=SC2086
"$CC" $CFLAGS $ZFLAGS -I"$HERE/libowf/include" -I"$HERE/libowf/src" \
    "$HERE"/libowf/src/*.c "$HERE/tools/owconvert.c" $ZSRCS -o "$OUT/OWConvert"
echo "$OUT/OWConvert ($(wc -c < "$OUT/OWConvert") bytes)"
