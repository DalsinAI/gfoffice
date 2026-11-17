#!/usr/bin/env python3
"""GFOffice's icons in the Soft 3D style: the GFOffice drawer, the drawers inside it and each program.

    python3 tools/icons/gfoffice_icons_art.py --gfopenup DIR --guest DIR [OUT]   icons/ (PNGs and .info files)
    python3 tools/icons/gfoffice_icons_art.py --gfopenup DIR --sheet OUT.png      the preview sheet

Drawn after AmiCradle's icon guide (amicradle docs/howto/12-draw-icons.md, Part 2): each icon is one object, drawn
flat on a 512 square from the fifteen PAL15 colours, then tilted, given its thickness, lit, edged and shadowed by
the Soft 3D kit (gfopenup tools/soft3d_drives_art.py), so it sits with the drives and the drawer, project, tool
and trashcan kinds. Chosen, the kit lifts it with the blue glow and one thing in the picture moves (gfopenup
ICONS.md, section 1). Two drawings each: about five features for 64 px, about three for 32 px. No text, no logos.

The drawers are the Soft 3D drawer kind (soft3d_kinds_art.draw_drawer) with a plate on the front saying what is
inside: the suite's four tiles, a book, three documents, a scroll, books on a shelf, a spanner, a picture.

Choices the guide leaves to Dale (Part 3), taken as the guide's default until Dale says otherwise:
- programs in Soft 3D, each its own object (no shared base);
- 64 x 64 in the .info files, at full size; the 32 px and 16 colour pictures are drawn and kept beside them;
- chosen: the lift and glow, and one thing moves;
- drawing code only, no hand-painted tops.

OWConvert is a Shell command and gets no icon, as Shell commands have none.

MIT, Copyright (c) 2026 AmiCradle Team.
"""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

d3 = kinds = None                       # the Soft 3D kit, from a gfopenup checkout (load_kit)

EDGE, SLATE, GREY, SILVER, PAPER = (22, 26, 36), (70, 76, 90), (130, 138, 152), (196, 202, 212), (246, 248, 251)
NAVY, BLUE, SKY = (40, 64, 120), (11, 94, 215), (74, 163, 255)
RUST, ORANGE, RED, GOLD = (214, 92, 36), (255, 122, 26), (200, 56, 52), (226, 182, 72)
GREEN, TEAL, MINT = (30, 120, 70), (13, 118, 110), (52, 170, 156)
INK = (120, 140, 176)                   # lines of writing, as on the project kind's page

# icon name -> (what it is, kind of icon, the picture)
DRAWERS = ["GFOffice", "Documentation", "samples", "Rexx", "library", "sdk", "Images"]
PROGRAMS = ["GFOpenWrite", "GFOpenCalc", "GFOpenPresentation", "GFOpenPressmaster", "GFOpenBase",
            "GFOpenProject", "GFOpenDiag", "GFOpenDraw"]
SAME_AS = {"GFOpenDrawOL": "GFOpenDraw"}        # GFOpenDraw's other front end: the same program, the same icon
LABELS = {"GFOffice": "GFOffice drawer", "Documentation": "Documentation", "samples": "samples", "Rexx": "Rexx",
          "library": "library", "sdk": "sdk", "Images": "Images", "GFOpenWrite": "Write", "GFOpenCalc": "Calc",
          "GFOpenPresentation": "Presentation", "GFOpenPressmaster": "Pressmaster", "GFOpenBase": "Base",
          "GFOpenProject": "Project", "GFOpenDiag": "Diag", "GFOpenDraw": "Draw"}


def load_kit(gfopenup: Path):
    global d3, kinds
    sys.path.insert(0, str(gfopenup / "tools"))
    import soft3d_drives_art as _d3                                             # noqa: E402
    import soft3d_kinds_art as _kinds                                           # noqa: E402
    d3, kinds = _d3, _kinds


def framed(f, b, r, col, top=None, edge=EDGE, w=6):
    """a light shape on its dark frame, 6 units wider (the guide's rule 6)"""
    f.rr([b[0] - w, b[1] - w, b[2] + w, b[3] + w], r + w, edge)
    f.rr(b, r, col, top)


def cut(f, b):
    """a hole through the object (its alpha), as the drives cut notches"""
    from PIL import ImageChops
    f.img.putalpha(ImageChops.subtract(f.img.getchannel("A"), f.mask("ell", b)))


def sheet_of_paper(f, b, r=14):
    f.rr(b, r, (214, 220, 230), (255, 255, 255))


# ---- the drawers -------------------------------------------------------------------------------------

PLATE = [92, 250, 420, 462]             # the plate on the drawer's front flap, over its handle


def em_suite(f, small, x0, y0, x1, y1):
    """GFOffice: the suite's four tiles, one colour a program family"""
    g = 14 if small else 12
    mx, my = (x0 + x1) / 2, (y0 + y1) / 2
    for (a, b, c, d), col in zip(((x0, y0, mx, my), (mx, y0, x1, my), (x0, my, mx, y1), (mx, my, x1, y1)),
                                 (BLUE, GREEN, ORANGE, RED)):
        f.rr([a + g, b + g, c - g, d - g], 12, col, d3.light(col, 0.35))


def em_book(f, small, x0, y0, x1, y1):
    """Documentation: an open book"""
    mx = (x0 + x1) / 2
    f.rr([x0, y0 + 4, x1, y1], 12, NAVY, d3.light(NAVY, 0.3))                    # the cover
    f.rr([x0 + 12, y0 - 6, mx - 4, y1 - 14], 8, PAPER, (255, 255, 255))         # the pages
    f.rr([mx + 4, y0 - 6, x1 - 12, y1 - 14], 8, PAPER, (255, 255, 255))
    rows = 2 if small else 3
    for i in range(rows):
        y = y0 + 22 + i * (y1 - y0 - 50) / rows
        h = 30 if small else 16
        f.rr([x0 + 34, y, mx - 26, y + h], 6, INK)
        f.rr([mx + 26, y, x1 - 34, y + h], 6, INK)


def em_documents(f, small, x0, y0, x1, y1):
    """samples: three documents fanned out, a word processor's, a sheet's and a slide's"""
    n, w = 3, (x1 - x0) / 3
    for i, col in enumerate((BLUE, GREEN, ORANGE)):
        a = x0 + i * w
        b = [a + 8, y0 - 8 + (i % 2) * 18, a + w - 8, y1 - 8 + (i % 2) * 8]
        framed(f, b, 8, PAPER, (255, 255, 255), w=5)
        f.rr([b[0] + 10, b[1] + 10, b[2] - 10, b[1] + (44 if small else 34)], 5, col)
        if not small:
            f.rr([b[0] + 14, b[1] + 60, b[2] - 22, b[1] + 74], 5, INK)
            f.rr([b[0] + 14, b[1] + 94, b[2] - 14, b[1] + 108], 5, INK)


def em_scroll(f, small, x0, y0, x1, y1):
    """Rexx: a script, a scroll with its rolled ends"""
    parch = d3.light(GOLD, 0.55)
    f.rr([x0 + 30, y0 + 10, x1 - 30, y1 - 10], 6, parch, (255, 255, 255))
    for x in (x0 + 4, x1 - 56):
        f.rr([x, y0 - 4, x + 52, y1 + 4], 26, d3.dark(GOLD, 0.1), d3.light(GOLD, 0.4))
    rows = 2 if small else 4
    for i in range(rows):
        y = y0 + 26 + i * (y1 - y0 - 40) / rows
        f.rr([x0 + 72 + (40 if i % 2 else 0), y, x1 - 80, y + (28 if small else 14)], 6, d3.dark(GOLD, 0.5))


def em_shelf(f, small, x0, y0, x1, y1):
    """library: books standing on a shelf"""
    cols = (RED, BLUE, GOLD, TEAL) if small else (RED, BLUE, GOLD, TEAL, NAVY)
    w = (x1 - x0 - 20) / len(cols)
    for i, col in enumerate(cols):
        a = x0 + 10 + i * w
        top = y0 + (0, 26, 8, 34, 14)[i]
        f.rr([a + 4, top, a + w - 4, y1 - 22], 8, col, d3.light(col, 0.35))
        if not small:
            f.rr([a + 14, top + 26, a + w - 14, top + 38], 4, d3.light(col, 0.6))
    f.rr([x0, y1 - 26, x1, y1], 6, SLATE)                                       # the shelf


def em_spanner(f, small, x0, y0, x1, y1):
    """sdk: a spanner, for building programs"""
    cx, cy, r = x0 + 70, (y0 + y1) / 2, 62 if small else 56
    f.line([(cx + 20, cy), (x1 - 30, cy)], 50 if small else 40, GREY)
    f.ell([x1 - 64, cy - 34, x1 - 4, cy + 34], GREY)
    f.ell([cx - r, cy - r, cx + r, cy + r], GREY, SILVER)
    f.rr([cx - r - 10, cy - 18, cx + 10, cy + 18], 4, PAPER)                     # the open jaw
    if not small:
        f.ell([x1 - 52, cy - 14, x1 - 24, cy + 14], PAPER)


def em_picture(f, small, x0, y0, x1, y1):
    """Images: a picture of a hill under the sun"""
    f.rr([x0 + 6, y0, x1 - 6, y1], 10, SKY, d3.light(SKY, 0.4))
    r = 30 if small else 24
    f.ell([x1 - 90 - r, y0 + 22, x1 - 90 + r, y0 + 22 + 2 * r], GOLD, d3.light(GOLD, 0.4))
    f.poly([(x0 + 6, y1), (x0 + 110, y0 + 60), (x0 + 200, y1 - 50), (x0 + 250, y0 + 90), (x1 - 6, y1)], GREEN)


EMBLEMS = {"GFOffice": em_suite, "Documentation": em_book, "samples": em_documents, "Rexx": em_scroll,
           "library": em_shelf, "sdk": em_spanner, "Images": em_picture}


def drawer(name):
    def draw(f, small, chosen=False):
        thick = kinds.draw_drawer(f, small, chosen)
        x0, y0, x1, y1 = PLATE
        framed(f, PLATE, 16, PAPER, (255, 255, 255))
        EMBLEMS[name](f, small, x0 + 22, y0 + 20, x1 - 22, y1 - 18)
        return thick
    return draw


# ---- the programs ------------------------------------------------------------------------------------

def pencil(f, tip, length, small):
    """a pencil lying from its point at tip up to the right"""
    ang = math.radians(-50)
    dx, dy = math.cos(ang), math.sin(ang)
    px, py = -dy, dx
    w = 34 if small else 28
    tx, ty = tip
    cone = 70
    b0 = (tx + dx * cone, ty + dy * cone)
    b1 = (tx + dx * length, ty + dy * length)
    f.poly([(tx, ty), (b0[0] + px * w, b0[1] + py * w), (b0[0] - px * w, b0[1] - py * w)], d3.light(GOLD, 0.6))
    f.poly([(tx, ty), (tx + dx * 26 + px * 10, ty + dy * 26 + py * 10),
            (tx + dx * 26 - px * 10, ty + dy * 26 - py * 10)], EDGE)
    f.line([b0, b1], 2 * w, ORANGE)
    f.line([(b0[0] + px * w * 0.4, b0[1] + py * w * 0.4), (b1[0] + px * w * 0.4, b1[1] + py * w * 0.4)],
           w * 0.5, d3.light(ORANGE, 0.35))
    e0 = (tx + dx * (length - 6), ty + dy * (length - 6))
    e1 = (tx + dx * (length + 44), ty + dy * (length + 44))
    f.line([e0, e1], 2 * w, RED)                                                 # the rubber


def draw_write(f, small, chosen=False):
    """GFOpenWrite: a page of writing and a pencil; chosen, the pencil writes on and a new line appears"""
    sheet_of_paper(f, [70, 26, 392, 486])
    rows = (90, 170, 250) if small else (78, 134, 190, 246, 302)
    h = 34 if small else 18
    for i, y in enumerate(rows):
        f.rr([110, y, 352 - (70 if i == len(rows) - 1 else 0), y + h], 8, INK)
    y = rows[-1] + (80 if small else 56)
    end = 300 if chosen else 200
    f.rr([110, y, end, y + h], 8, BLUE)                                          # the line being written
    pencil(f, (end + 16, y + h - 4), 300 if small else 320, small)
    return 18


def draw_calc(f, small, chosen=False):
    """GFOpenCalc: a sheet, its green header and numbers in a grid; chosen, the cursor drops to the total"""
    x0, y0, x1, y1 = 40, 48, 472, 464
    sheet_of_paper(f, [x0, y0, x1, y1], 16)
    f.rr([x0, y0, x1, y0 + 74], 16, GREEN, d3.light(GREEN, 0.35))
    f.rr([x0, y0 + 40, x1, y0 + 74], 0, GREEN)
    cols, rows = (3, 3) if small else (3, 5)
    left = x0 + 54
    f.rr([x0, y0 + 74, left, y1], 0, (214, 220, 230))                          # the row numbers' column
    cw, rh = (x1 - left) / cols, (y1 - y0 - 74) / rows
    for c in range(1, cols):
        f.line([(left + c * cw, y0 + 74), (left + c * cw, y1 - 10)], 8 if not small else 12, SILVER)
    for r in range(1, rows):
        f.line([(x0 + 10, y0 + 74 + r * rh), (x1 - 10, y0 + 74 + r * rh)], 8 if not small else 12, SILVER)
    for r in range(rows - 1):
        for c in range(cols):
            if (r + c) % 3 == 2 and not small:
                continue
            yy = y0 + 74 + r * rh + rh / 2
            f.rr([left + c * cw + cw * (0.35 if c else 0.25), yy - (14 if small else 9), left + (c + 1) * cw - 20,
                  yy + (14 if small else 9)], 6, SLATE)
    r = rows - 1 if chosen else rows - 2                                          # the cursor
    c = cols - 1
    yy = y0 + 74 + r * rh
    f.rr([left + c * cw + 2, yy - 2, x1 - 8, yy + rh + 2], 10, BLUE)
    f.rr([left + c * cw + 16, yy + 12, x1 - 22, yy + rh - 12], 6, d3.light(SKY, 0.8))
    if chosen:
        f.rr([left + c * cw + cw * 0.2, yy + rh / 2 - (14 if small else 10), x1 - 26,
              yy + rh / 2 + (14 if small else 10)], 6, BLUE)                         # the total
    return 18


def wedge(cx, cy, r, a0, a1, n=24):
    pts = [(cx, cy)]
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def draw_present(f, small, chosen=False):
    """GFOpenPresentation: a slide on its stand with a pie chart; chosen, a slice of the pie comes out"""
    for x in (150, 362):                                                          # the stand's legs
        f.line([(x, 380), (x + (x - 256) * 0.5, 488)], 22, SLATE)
    x0, y0, x1, y1 = 34, 40, 478, 392
    f.rr([x0, y0, x1, y1], 18, EDGE, SLATE)                                       # the frame
    f.rr([x0 + 16, y0 + 16, x1 - 16, y1 - 16], 8, PAPER, (255, 255, 255))      # the slide
    f.rr([x0 + 16, y0 + 16, x1 - 16, y0 + 70], 8, ORANGE, d3.light(ORANGE, 0.3))
    f.rr([x0 + 16, y0 + 50, x1 - 16, y0 + 70], 0, ORANGE)
    cx, cy, r = 170, 238, (96 if small else 90)
    f.ell([cx - r - 6, cy - r - 6, cx + r + 6, cy + r + 6], EDGE)
    f.ell([cx - r, cy - r, cx + r, cy + r], BLUE, d3.light(BLUE, 0.3))
    f.poly(wedge(cx, cy, r, 180, 300), MINT)
    ox, oy = (30, -26) if chosen else (0, 0)
    sl = wedge(cx + ox, cy + oy, r, -60, 30)
    f.poly([(x + (6 if i == 0 else 0), y) for i, (x, y) in enumerate(sl)], EDGE)
    f.poly(wedge(cx + ox, cy + oy, r - 6, -56, 26), GOLD)
    rows = (180, 270) if small else (150, 200, 250, 300)
    for y in rows:
        f.ell([294, y, 318, y + 24] if not small else [292, y - 4, 324, y + 28], SLATE)
        f.rr([334, y + 2, 436 - (30 if y == rows[-1] else 0), y + (22 if not small else 24)], 8, INK)
    return 26


def draw_pressmaster(f, small, chosen=False):
    """GFOpenPressmaster: a newsletter page, its masthead, a picture and two columns; chosen, the picture's frame
    is picked up and moved, its handles showing"""
    x0, y0, x1, y1 = 72, 24, 440, 488
    sheet_of_paper(f, [x0, y0, x1, y1])
    f.rr([x0 + 30, y0 + 30, x1 - 30, y0 + 96], 10, NAVY, d3.light(NAVY, 0.3))  # the masthead
    f.rr([x0 + 30, y0 + 112, x1 - 30, y0 + 124], 4, RED)
    cols = ((x0 + 30, 240), (262, x1 - 30))
    rows = range(300, 470, 90 if small else 40)
    for a, b in cols:
        for i, y in enumerate(rows):
            f.rr([a, y, b - (30 if i == len(rows) - 1 else 0), y + (32 if small else 16)], 6, INK)
    dx, dy = (22, 16) if chosen else (0, 0)
    px0, py0, px1, py1 = x0 + 30 + dx, 158 + dy, 330 + dx, 270 + dy             # the picture's frame
    framed(f, [px0, py0, px1, py1], 6, SKY, d3.light(SKY, 0.4))
    f.poly([(px0, py1), (px0 + 70, py0 + 40), (px0 + 130, py1 - 30), (px0 + 170, py0 + 56), (px1, py1)], GREEN)
    f.ell([px1 - 56, py0 + 14, px1 - 20, py0 + 50], GOLD)
    if chosen:
        for hx, hy in ((px0, py0), (px1, py0), (px0, py1), (px1, py1)):
            f.rr([hx - 16, hy - 16, hx + 16, hy + 16], 4, EDGE)
            f.rr([hx - 10, hy - 10, hx + 10, hy + 10], 2, SKY)
    return 18


def draw_base(f, small, chosen=False):
    """GFOpenBase: a card file, its records on index cards; chosen, one record is pulled up"""
    tabs = ((ORANGE, 110), (GOLD, 216), (MINT, 322)) if not small else ((ORANGE, 100), (MINT, 300))
    for i, (col, tx) in enumerate(tabs):
        lift = 80 if chosen and i == 1 else 0
        y = 60 + i * 36 - lift
        f.rr([70, y + 40, 442, 340], 10, EDGE)
        f.rr([76, y + 46, 436, 340], 8, PAPER, (255, 255, 255))
        f.rr([tx, y, tx + 96, y + 60], 10, EDGE)                                 # the card's tab
        f.rr([tx + 6, y + 6, tx + 90, y + 56], 8, col, d3.light(col, 0.35))
        if not small and lift:
            f.rr([110, y + 80, 380, y + 96], 6, INK)
    body = TEAL
    f.rr([40, 210, 472, 476], 26, body, d3.light(body, 0.35))                   # the box
    framed(f, [156, 300, 356, 380], 10, PAPER, (255, 255, 255))                 # its label
    f.rr([186, 328, 326, 352] if not small else [176, 322, 336, 358], 8, SILVER)
    return 64


def draw_project(f, small, chosen=False):
    """GFOpenProject: a plan on a clipboard, a Gantt chart's bars; chosen, the last task's bar runs on"""
    board = d3.dark(GOLD, 0.35)
    f.rr([66, 44, 446, 488], 24, board, d3.light(board, 0.25))
    f.rr([90, 92, 422, 466], 8, PAPER, (255, 255, 255))
    f.rr([186, 22, 326, 90], 16, EDGE)                                            # the clip
    f.rr([194, 30, 318, 82], 12, SILVER, (255, 255, 255))
    f.line([(120, 128), (120, 444)], 8, SILVER)
    bars = ((120, 260, BLUE), (210, 330, MINT), (260, 392, ORANGE)) if small else \
           ((120, 230, BLUE), (180, 300, MINT), (240, 330, BLUE), (280, 380, ORANGE), (330, 392, RED))
    n = len(bars)
    step = (430 - 130) / n
    for i, (a, b, col) in enumerate(bars):
        y = 134 + i * step
        if i == n - 1:
            b = b if chosen else a + (b - a) * 0.45
        f.rr([a - 4, y + 2, b + 4, y + step - 8], 10, EDGE)
        f.rr([a, y + 6, b, y + step - 12], 8, col, d3.light(col, 0.35))
    f.line([(300, 112), (300, 450)], 8 if not small else 12, RED)               # today
    return 24


def draw_diag(f, small, chosen=False):
    """GFOpenDiag: a flowchart, a start, a decision and two outcomes joined by connectors; chosen, one outcome
    is dragged and its connector follows"""
    sheet_of_paper(f, [40, 36, 472, 476])
    top = (176, 70, 336, 150) if not small else (166, 66, 346, 160)
    cx, cy, dr = 256, 262, (82 if not small else 92)
    lbox = (76, 360, 216, 440)
    rbox = (296, 360, 436, 440) if not chosen else (310, 316, 450, 396)
    w = 14 if not small else 20
    f.line([(256, top[3]), (256, cy - dr)], w, SLATE)
    f.line([(cx - dr, cy), ((lbox[0] + lbox[2]) / 2, cy), ((lbox[0] + lbox[2]) / 2, lbox[1])], w, SLATE)
    rx = (rbox[0] + rbox[2]) / 2
    f.line([(cx + dr, cy), (rx, cy)], w, SLATE)
    f.line([(rx, cy - w / 2), (rx, rbox[1])], w, SLATE)
    framed(f, list(top), 36, BLUE, d3.light(BLUE, 0.35))
    f.poly([(cx, cy - dr - 8), (cx + dr + 8, cy), (cx, cy + dr + 8), (cx - dr - 8, cy)], EDGE)
    f.poly([(cx, cy - dr), (cx + dr, cy), (cx, cy + dr), (cx - dr, cy)], GOLD)
    framed(f, list(lbox), 10, MINT, d3.light(MINT, 0.35))
    framed(f, list(rbox), 10, ORANGE, d3.light(ORANGE, 0.35))
    return 18


def draw_draw(f, small, chosen=False):
    """GFOpenDraw: a painter's palette and a brush; chosen, the brush moves to another colour and takes it up"""
    wood = d3.light(GOLD, 0.25)
    f.ell([30, 60, 482, 452], wood, d3.light(GOLD, 0.6))
    cut(f, [82, 236, 156, 304])                                                    # the thumb hole
    blobs = ((RED, 196, 112), (BLUE, 316, 116), (MINT, 400, 200), (PAPER, 388, 320), (ORANGE, 196, 360)) \
        if not small else ((RED, 200, 120), (BLUE, 340, 140), (ORANGE, 220, 350))
    r = 44 if not small else 56
    for col, x, y in blobs:
        f.ell([x - r - 6, y - r - 6, x + r + 6, y + r + 6], EDGE)
        f.ell([x - r, y - r, x + r, y + r], col, d3.light(col, 0.35))
    col, tx, ty = blobs[2] if chosen else blobs[-2]                                # the blob the brush is in
    ang = math.radians(48 if not chosen else 70)
    dx, dy = math.cos(ang), math.sin(ang)
    w = 34 if not small else 40
    f.line([(tx + dx * 40, ty + dy * 40), (tx + dx * 130, ty + dy * 130)], w + 16, EDGE)
    f.line([(tx + dx * 46, ty + dy * 46), (tx + dx * 124, ty + dy * 124)], w + 6, SILVER)      # the ferrule
    f.line([(tx + dx * 124, ty + dy * 124), (tx + dx * 330, ty + dy * 330)], w, NAVY)            # the handle
    f.line([(tx + dx * 124 - dy * w * 0.25, ty + dy * 124 + dx * w * 0.25),
            (tx + dx * 320 - dy * w * 0.25, ty + dy * 320 + dx * w * 0.25)], w * 0.35, d3.light(NAVY, 0.35))
    f.ell([tx - 34, ty - 34, tx + 34, ty + 34], EDGE)
    f.ell([tx - 28, ty - 28, tx + 28, ty + 28], d3.dark(col, 0.2), d3.light(col, 0.3))     # the tip in its paint
    return 20


PROGRAM_DRAW = {"GFOpenWrite": draw_write, "GFOpenCalc": draw_calc, "GFOpenPresentation": draw_present,
                "GFOpenPressmaster": draw_pressmaster, "GFOpenBase": draw_base, "GFOpenProject": draw_project,
                "GFOpenDiag": draw_diag, "GFOpenDraw": draw_draw}


def drawing(name):
    name = SAME_AS.get(name, name)
    return drawer(name) if name in EMBLEMS else PROGRAM_DRAW[name]


# ---- rendering, as soft3d_kinds_art renders a kind ---------------------------------------------------

def render(name, S, chosen, small):
    from PIL import Image
    f = d3.Flat(S)
    thick = drawing(name)(f, small, chosen)
    im = d3.solid(f.img, thick, S, chosen)
    if chosen:                                                                     # lifted 2 px at 64
        up = Image.new("RGBA", (S, S))
        up.alpha_composite(im, (0, 0), (0, int(16 * S / 512)))
        im = up
    return im


_CACHE = {}


def frames(name, size, chosen):
    """NAME at rest or chosen, size x size, hard alpha: both frames cut to one square, as the kit's frames()"""
    from PIL import ImageChops
    big = size >= 48
    key = (SAME_AS.get(name, name), big)
    if key not in _CACHE:
        S = 512 if big else 256
        pair = [render(name, S, c, not big) for c in (False, True)]
        m = ImageChops.lighter(pair[0].getchannel("A"), pair[1].getchannel("A")).point(lambda v: 255 if v >= 60 else 0)
        x0, y0, x1, y1 = m.getbbox()
        side = int(max(x1 - x0, y1 - y0) * 1.06)
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        _CACHE[key] = [p.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side))
                       for p in pair]
    im = d3.hard(_CACHE[key][1 if chosen else 0], size)
    return im if big else d3.edged(im)


# ---- the .info files ---------------------------------------------------------------------------------

def info_bytes(name, guest: Path):
    """NAME's icon file: an OS 3.5 GlowIcon, two 64 px frames of up to 255 colours each (median cut, no dither,
    colour 0 see-through under 110 alpha) and the 4 colour classic picture for OS 3.1, as gfopenup's
    openicons_part.aos_icon writes the Soft 3D drives"""
    import numpy as np
    sys.path.insert(0, str(guest / "icons" / "openicons"))
    from openicons import info as infofmt                                         # noqa: E402
    from openicons.render import planar_from                                      # noqa: E402
    images, planar = [], []
    for chosen in (False, True):
        fr = frames(name, 64, chosen)
        q = fr.convert("RGB").quantize(colors=255, method=0, dither=0)
        idx = np.array(q, np.int32) + 1
        idx[np.array(fr)[..., 3] < 110] = 0
        pal = q.getpalette()[:255 * 3]
        pal = [(0, 0, 0)] + [tuple(pal[i:i + 3]) for i in range(0, len(pal), 3)]
        pal = pal[:int(idx.max()) + 1]
        images.append((idx.reshape(-1).tolist(), pal))
        planar.append(planar_from(idx, pal))
    is_drawer = SAME_AS.get(name, name) in EMBLEMS
    icon = dict(type="drawer" if is_drawer else "tool", width=64, height=64, planar=planar, planar_depth=2,
                colour=dict(images=images, transparent=0, frameless=True), default_tool=None, tooltypes=[],
                stack=0 if is_drawer else 65536)
    if is_drawer:
        icon["drawer"] = dict(left=80, top=60, width=520, height=300, flags=0, viewmodes=0)
    data = infofmt.write_info(icon)
    errs = infofmt.validate(icon, data)
    if errs:
        raise infofmt.InfoError(f"{name}: " + "; ".join(errs))
    return data


def write_all(out: Path, guest: Path):
    png = out / "png"
    png.mkdir(parents=True, exist_ok=True)
    names = DRAWERS + PROGRAMS
    for name in names:
        for chosen, state in ((False, "normal"), (True, "selected")):
            for n in (64, 32):
                im = frames(name, n, chosen)
                im.save(png / f"{name}-{n}-{state}.png", optimize=True)
                if n == 32:
                    d3.chipset(im).save(png / f"{name}-32-chipset-{state}.png", optimize=True)
    for sub in ("drawers", "programs"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    (out / "drawers" / "GFOffice.info").write_bytes(info_bytes("GFOffice", guest))
    for name in DRAWERS[1:]:
        (out / "drawers" / f"{name}.info").write_bytes(info_bytes(name, guest))
    for name in PROGRAMS + list(SAME_AS):
        (out / "programs" / f"{name}.info").write_bytes(info_bytes(name, guest))
    print(f"{len(names)} icons in {out}")


# ---- the preview sheet -------------------------------------------------------------------------------

def sheet(out: Path):
    from PIL import Image, ImageDraw
    rows = [("64 px", 64, False, False, 1), ("64 px chosen", 64, True, False, 1), ("32 px", 32, False, False, 1),
            ("32 px chosen", 32, True, False, 1), ("32 px, 16 colours", 32, False, True, 1),
            ("64 px at 2x", 64, False, False, 2), ("64 px chosen at 2x", 64, True, False, 2),
            ("32 px at 3x", 32, False, False, 3)]
    groups = [DRAWERS, PROGRAMS]
    cell, left, top = 144, 170, 40
    hs = [max(n * z, 40) + 16 for _, n, _, _, z in rows]
    block = top + sum(hs)
    width = left + cell * max(len(g) for g in groups) + 20
    im = Image.new("RGBA", (width, 2 * len(groups) * (block + 16)), (255, 255, 255, 255))
    dr = ImageDraw.Draw(im)
    f, fb = d3.font(13), d3.font(15)
    y0 = 0
    for g in groups:
        for bg, ink in (((232, 234, 238), (30, 34, 44)), ((43, 47, 54), (230, 232, 236))):
            dr.rectangle([0, y0, im.width, y0 + block], fill=bg)
            for i, name in enumerate(g):
                x = left + i * cell + cell // 2
                lab = LABELS[name]
                dr.text((x - dr.textlength(lab, font=fb) / 2, y0 + 12), lab, font=fb, fill=ink)
            y = y0 + top
            for (lab, n, chosen, chip, z), h in zip(rows, hs):
                dr.text((16, y + h // 2 - 8), lab, font=f, fill=ink)
                for i, name in enumerate(g):
                    p = frames(name, n, chosen)
                    if chip:
                        p = d3.chipset(p)
                    p = d3.blow(p, z)
                    im.alpha_composite(p, (left + i * cell + (cell - p.width) // 2, y + (h - p.height) // 2))
                y += h
            y0 += block + 16
    im.convert("RGB").save(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", type=Path, nargs="?", default=Path(__file__).resolve().parents[2] / "icons")
    ap.add_argument("--gfopenup", type=Path, required=True, help="a gfopenup checkout (the Soft 3D kit)")
    ap.add_argument("--guest", type=Path, help="an amicradle-guest checkout (icons/openicons), for the .info files")
    ap.add_argument("--sheet", action="store_true")
    a = ap.parse_args(argv)
    load_kit(a.gfopenup)
    if a.sheet:
        sheet(a.out)
        return 0
    if not a.guest:
        ap.error("--guest is needed to write the .info files")
    write_all(a.out, a.guest)
    return 0


if __name__ == "__main__":
    sys.exit(main())
