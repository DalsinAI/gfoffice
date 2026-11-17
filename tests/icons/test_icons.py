#!/usr/bin/env python3
"""GFOffice's icons (icons/, drawn by tools/icons/gfoffice_icons_art.py): every program and drawer has its icon
file and its pictures, and they keep to AmiCradle's icon guide (amicradle docs/howto/12-draw-icons.md).

Python's standard library only: the .info files are read here, not with the drawing kit.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ICONS = ROOT / "icons"
DRAWERS = ["GFOffice", "Documentation", "samples", "Rexx", "library", "sdk", "Images"]
PROGRAMS = ["GFOpenWrite", "GFOpenCalc", "GFOpenPresentation", "GFOpenPressmaster", "GFOpenBase",
            "GFOpenProject", "GFOpenDiag", "GFOpenDraw"]
WB_DRAWER, WB_TOOL = 2, 3
fails = 0


def check(ok, what):
    global fails
    if not ok:
        fails += 1
        print("FAIL", what)


def png(path):
    """(width, height, colour type, the pixels' bytes) of a PNG, 8 bits a channel, no interlace"""
    b = path.read_bytes()
    assert b[:8] == b"\x89PNG\r\n\x1a\n", path
    o, idat, ihdr = 8, b"", None
    while o < len(b):
        n, kind = struct.unpack(">I4s", b[o:o + 8])
        body = b[o + 8:o + 8 + n]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        o += 12 + n
    w, h, depth, ctype = ihdr[:4]
    return w, h, ctype, depth, zlib.decompress(idat)


def unfilter(w, h, bpp, raw):
    """the PNG's rows without their filters"""
    stride, out, prev, o = w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        ft, row = raw[o], bytearray(raw[o + 1:o + 1 + stride])
        o += 1 + stride
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0
            up, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                row[i] = (row[i] + a) & 255
            elif ft == 2:
                row[i] = (row[i] + up) & 255
            elif ft == 3:
                row[i] = (row[i] + (a + up) // 2) & 255
            elif ft == 4:
                p = a + up - c
                pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        out.append(bytes(row))
        prev = row
    return out


def rgba(path):
    w, h, ctype, depth, raw = png(path)
    check(depth == 8 and ctype == 6, f"{path.name}: 8 bit RGBA")
    return w, h, unfilter(w, h, 4, raw)


def info(path):
    """an icon file's type, its classic picture's size and its GlowIcon's (FACE) size and IMAG count"""
    b = path.read_bytes()
    magic, version = struct.unpack(">HH", b[:4])
    width, height = struct.unpack(">HH", b[12:16])
    wbtype = b[48]
    form = b.find(b"FORM")
    face = b.find(b"FACE", form)
    fw, fh = (b[face + 8] + 1, b[face + 9] + 1) if face > 0 else (0, 0)
    return dict(magic=magic, version=version, type=wbtype, glow=b[form + 8:form + 12] if form > 0 else b"",
                face=(fw, fh), imags=b.count(b"IMAG", form) if form > 0 else 0, classic=(width, height))


def main():
    pics = {}
    for name in DRAWERS + PROGRAMS:
        for n in (64, 32):
            frames = []
            for state in ("normal", "selected"):
                p = ICONS / "png" / f"{name}-{n}-{state}.png"
                check(p.exists(), f"{p.name} is there")
                if not p.exists():
                    continue
                w, h, rows = rgba(p)
                check((w, h) == (n, n), f"{p.name}: {n} x {n}")
                alphas = {r[i] for r in rows for i in range(3, len(r), 4)}
                check(alphas <= {0, 255}, f"{p.name}: every pixel solid or see-through")
                solid = sum(r[i] == 255 for r in rows for i in range(3, len(r), 4))
                check(solid > n * n // 4, f"{p.name}: fills its icon ({solid} solid pixels)")
                frames.append(b"".join(rows))
                cp = ICONS / "png" / f"{name}-32-chipset-{state}.png"
                if n == 32:
                    check(cp.exists(), f"{cp.name} is there")
                    if cp.exists():
                        _, _, crows = rgba(cp)
                        cols = {r[i:i + 3] for r in crows for i in range(0, len(r), 4) if r[i + 3]}
                        check(len(cols) <= 15, f"{cp.name}: at most 15 colours and see-through ({len(cols)})")
            if len(frames) == 2:
                check(frames[0] != frames[1], f"{name} {n} px: at rest and chosen differ")
                pics[(name, n)] = frames[0]
    for n in (64, 32):
        seen = {}
        for name in DRAWERS + PROGRAMS:
            pic = pics.get((name, n))
            check(pic not in seen, f"{name} {n} px is its own picture, not {seen.get(pic)}'s")
            seen[pic] = name
    for name, sub, wbtype in [(d, "drawers", WB_DRAWER) for d in DRAWERS] + \
                             [(p, "programs", WB_TOOL) for p in PROGRAMS + ["GFOpenDrawOL"]]:
        p = ICONS / sub / f"{name}.info"
        check(p.exists(), f"{sub}/{name}.info is there")
        if not p.exists():
            continue
        i = info(p)
        check(i["magic"] == 0xE310 and i["version"] == 1, f"{name}.info: a Workbench icon")
        check(i["type"] == wbtype, f"{name}.info: type {wbtype}, not {i['type']}")
        check(i["glow"] == b"ICON", f"{name}.info: a GlowIcon (FORM ICON)")
        check(i["face"] == (64, 64), f"{name}.info: 64 x 64 GlowIcon, not {i['face']}")
        check(i["imags"] == 2, f"{name}.info: two frames, not {i['imags']}")
        check(len(name) + 5 <= 30, f"{name}.info fits 30 characters")
    for p in sorted((ROOT / "app").glob("open*.c")):
        prog = {"openwrite": "GFOpenWrite", "opencalc": "GFOpenCalc", "openpresent": "GFOpenPresentation",
                "openbase": "GFOpenBase", "openproject": "GFOpenProject", "opendiag": "GFOpenDiag"}.get(p.stem)
        if prog:
            check(prog in PROGRAMS, f"app/{p.name}'s program has an icon")
    print(f"icons: {'FAIL' if fails else 'OK'} ({len(DRAWERS)} drawers, {len(PROGRAMS)} programs, {fails} failures)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
