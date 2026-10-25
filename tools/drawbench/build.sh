#!/bin/sh
# drawbench, the proof program of docs/suite/cairo-rendering.md: built with the
# os32-gcc16 stove against openamigacairo's CAIRO_FONTS=user build and
# openamigartg's OpenGPU core (linked in, so its batches run on the 68k).
#
#   OS32_GCC16    stove root (default ~/AmigaChrome/stoves/os32-gcc16)
#   CAIRO         openamigacairo's PREFIX, with include/cairo and lib (required)
#   OPENAMIGARTG  an openamigartg checkout (required)
#   OUT           where the programs go (default tools/drawbench/build)
#
# MIT, Copyright (c) 2026 AmigaChrome Team.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
S=${OS32_GCC16:-"$HOME/AmigaChrome/stoves/os32-gcc16"}/prefix
R=${OPENAMIGARTG:?set OPENAMIGARTG to an openamigartg checkout}
C=${CAIRO:?set CAIRO to the PREFIX openamigacairo built into}
OUT=${OUT:-"$HERE/build"}
CC=$S/bin/m68k-amigaos-gcc
CF="-O2 -m68040 -m68881 -mcrt=nix20 -fno-delete-null-pointer-checks -Wall"
INC="-I$R/library/opengpu -I$R/include -I$C/include/cairo"
mkdir -p "$OUT"
$CC $CF $INC -c "$R/library/opengpu/ogpu_core.c" -o "$OUT/ogpu_core.o"
$CC $CF $INC -c "$R/library/opengpu/ogpu_build.c" -o "$OUT/ogpu_build.o"
$CC $CF $INC "$HERE/drawbench.c" "$OUT/ogpu_core.o" "$OUT/ogpu_build.o" -L"$C/lib" \
    -L"$S/m68k-amigaos/port-deps/lib" -lcairo -lpixman-1 -lpng -lz -lpthread -latomic -lm -o "$OUT/drawbench"
$CC $CF -DNO_CAIRO $INC "$HERE/drawbench.c" "$OUT/ogpu_core.o" "$OUT/ogpu_build.o" -lm -o "$OUT/drawbench-nocairo"
ls -l "$OUT/drawbench" "$OUT/drawbench-nocairo"
