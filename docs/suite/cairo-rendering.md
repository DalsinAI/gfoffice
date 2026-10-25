# Drawing GFOffice's pages through Cairo, Pixman and OpenGPU

An assessment for Dale. 25 October 2026, W139, Team 5. Nothing in GFOffice
changes here: the only code is a proof program, `tools/drawbench/`, which
measured the numbers below.

Dale, 14:41: "OpenBrowser is on it. They are wired into opengfx. Worth a look
for layouts..."

## The answer in short

- **What the OpenBrowser team built isn't Cairo inside OpenGfx.** It replaces
  Cairo and Pixman. In opengpu.library 0.18 (stream v1.6) Team 3 added the
  drawing a page needs:
  - anti-aliased paths, filled or stroked, with curves and dashes;
  - gradients;
  - pictures scaled with bilinear filtering;
  - blur;
  - Porter-Duff compositing and the blend modes.

  It's called through opengpu.library, which ships with OpenGfx's calls in it.
  Dale's rule for the browser (22 October) is "no cairo, no pixman": the
  browser still links Cairo today, and its own move to OpenGPU (patch 0076) is
  written but not built.
- **GFOffice should draw through OpenGPU, not link Cairo.** Cairo would add
  1.1 MB to each program and require an FPU, which GFOffice doesn't need
  today. It would also bring FreeType text that can't read OS 3.2's
  Intellifont fonts, and a PDF surface that our Cairo build doesn't include.
  OpenGPU is shared, uses integers only, and already sits in the AmigaChrome
  ROM (0.20). On AmigaChrome its vector commands run on the host, in about
  2 ms a batch, with the same pixels as the 68k code (measured, section 6).
- **Start with pictures and charts. Text stays on OpenFont and diskfont, and
  PDF stays on libowf's writer.**
- **The catch is real 68040s.** There, OpenGPU's 68k code runs every
  command. It draws anti-aliased paths 4 to 9 times slower than Cairo, and
  scales pictures 3 times slower (20 times through a path pattern). So:
  - charts are cached once they are drawn;
  - pictures are scaled once per zoom;
  - without a driver, programs fall back to today's drawing.

## 1. What exists

Checked on 25 October 2026 against:

| Repository | Commit | Note |
| --- | --- | --- |
| openamigabrowser | main `15f5f2d` | PR #104 at `1f74570` |
| openamigacairo | `20a18bb` | |
| openamigapixman | master `f29a8de` | |
| openamigartg | main `8a45a37` | |
| openamigagfx | main `6c8b6d9` | |
| openamigalayout | `1375bd7` | |
| gfoffice | `2238c05` | |

A lab copy of Instance-32 (t5-w139) ran the proof program.

"Wired into opengfx" covers four separate pieces.

### 1.1 Cairo and Pixman as libraries (openamigacairo)

- **Versions:** cairo 1.18.6 and pixman 0.46.4, with no source changes. Both
  are built as static link libraries for GCC programs.
- **Build:** `build.sh` with the os32-gcc16 stove: bebbo's amiga-gcc on GCC
  16.2, libnix and libpthread.
  - Flags: `-O2 -m68040 -m68881 -mcrt=nix20 -fno-delete-null-pointer-checks`.
  - Hard float, so it needs a 68040 with an FPU.
  - Pixman uses its portable C paths and a hand-written `pixman-config.h`.
  - Cairo is built without Meson, from a source list.
- **Two builds:**
  - `CAIRO_FONTS=ft` uses FreeType, fontconfig, Expat, libpng and zlib.
  - `CAIRO_FONTS=user` (#8, 23 October) has no FreeType. OpenBrowser uses this
    one, and draws OpenFont's outlines through Cairo user fonts.
- **Features built:** image surfaces, PNG writing, recording surfaces and
  user fonts. **The PDF surface is not built:** `cairo-pdf-surface.c` isn't
  in the source list, and `CAIRO_HAS_PDF_SURFACE` isn't set.
- **Known trap:** OS 3.2's ROM `mathieeesingbas.library` leaves the FPU in
  single precision. Every task that uses Cairo must set FPCR to 0, or
  nothing is drawn.
- **State:** merged. The smoke test ran on Instance-24 on 4 October. Today
  OpenBrowser's engine is the only user.

### 1.2 pixman.library 2.0 (openamigapixman)

- **What it is:** 25 of Pixman's calls as an Amiga library of 16 KB. It
  contains no Pixman: each call becomes an OpenGPU batch.
- **State:** merged (#6), and measured on the AC090 with PixBench.
- **Users:** none. Cairo links upstream Pixman statically, and Dale's
  22 October decision says nothing in the browser uses pixman.library. It's a
  dead end for GFOffice.

### 1.3 opengpu.library: where Cairo's job went (openamigartg)

opengpu.library is the one home for pixel work (Dale, 22 October). OpenGfx's
calls (`OGFX_*`) live in it too.

**The calls:**
- `OGPU_Query(op, format)` says whether an operation is done fully, partly or
  not at all for a format, and by which back end.
- `OGPU_Submit(stream, words, &fence)` sends a batch, and `OGPU_Wait(fence)`
  waits for it.
- `OGPU_BackEndName`, `OGPU_ModuleOpen` and `OGPU_ModuleClose`.

A program writes its batch with the builders in `opengpu/build.h`
(`ogpu_build.c`, 5.6 KB of code linked into the program). Building a batch
makes no library calls.

**Surfaces** are memory, set by `SURFACE`: an address, bytes per row, a size
and a format.
- Formats: ARGB32, XRGB32, BGRA32, RGBA32, ABGR32, RGB565 and RGB555 (both
  byte orders), A8, CLUT8 (pens), and INDEX8 with a `PALETTE`.
- A screen's or window's bitmap can be a surface once its address is known.
  OpenLayers' composited mode does this internally.

**Commands:**

| Stream | Commands |
| --- | --- |
| up to v1.5 | `FILL`, `COPY`, `PIXELS`, `MASK` (A8 coverage), `COMPOSITE` (scaled, nearest or bilinear, straight alpha), `COMPOSITE_AFFINE`, `COMPOSITE_PD` (premultiplied, Pixman's Porter-Duff operators and rounding) |
| v1.6 (0.18, #109, merged 23 October) | `PATH_FILL` (moves, lines, quadratic and cubic curves in 24.8, under a 16.16 matrix, non-zero or even-odd, exact-area coverage), `PATH_STROKE` (widths, caps, joins, miter limit, dashes), linear, radial and conic gradients and pictures under a matrix as paint, an A8 clip mask, `BLUR`, and the 15 W3C blend modes |
| v1.7 (0.19) | What SDL 3 needs. Nothing GFOffice uses |

**Back ends:**
- The **68k CPU core** (`ogpu_core.c`) runs on any 68040, LC040 included,
  with integers only. It is about 129 KB of code.
- **ACRTG.gpu** on AmigaChrome runs batches on the host. The same C core runs
  there, and also on the GPU through Vulkan.
- A **PiStorm** driver is named in the design.

A driver gets only the batches it draws correctly. The rest run on the 68k.

**Palette screens:** paths and every blending command refuse CLUT8 and
INDEX8 targets. On an 8-bit screen a program draws into an ARGB32 buffer and
turns it into pens itself.

**State:**
- 0.18 was tested on Linux only: 118 checks against references in doubles,
  with the qemu-m68k and Vulkan runs skipped.
- Instance-32's ROM has opengpu.library 0.20. Its ACRTG.gpu answers FULL for
  `PATH_FILL` on ARGB32.
- In this assessment's lab, every vector batch drawn by opengpu.library, and
  so by the host, matched the 68k core's pixels byte for byte (section 6).
- As far as I can find, that is the first check of v1.6 on an Amiga.
- No real 68040 has run it yet.

### 1.4 OpenGfx itself (openamigagfx)

- **graphics.library 47.100** is clean room, with the OS 3.2 API, planar and
  chunky. It is in the ROM.
- **layers.library 47.107** has a composited mode on 16 and 32-bit RTG
  screens with OpenGPU. Each window draws into its own surface, with opacity
  and shadows. AGA and other palette screens keep classic layers.
- **What it doesn't have:** OpenBrowser's PLAN.md (22 October) says OpenGfx
  has no anti-aliased paths, gradients, alpha compositing or glyph runs. It
  keeps the graphics.library API and planar bitmaps, and hands RTG pixel work
  to OpenGPU.

So "opengfx" exposes nothing of Cairo or Pixman. The new drawing is
opengpu.library's.

### 1.5 How OpenBrowser uses it

**Today (main):**
- WebKit draws with WebCore's Cairo GraphicsContext, on the static
  Cairo 1.18.6 and Pixman 0.46.4.
- Text goes through Cairo user fonts on OpenFont (patch 0090).
- Cairo, Pixman and libpng are 0.94 MiB of the engine's link map.

**Patch 0076 (PR #104, open since 23 October, written, not built):**
- `GraphicsContextOpenGPU` draws solid fills, clears, rectangle clips and
  pictures through `FILL`, `COMPOSITE_PD`, and a `PATH_FILL` painted with the
  picture for scaled or turned pictures. Everything else goes to Cairo, on
  the same pixels, and is counted.
- There is one batch per paint. `OB_PAINT=cairo` switches back.
- Its stream writer, `OpenGPUPaintAmiga.c`, is plain C, about 6 KB, and
  passes 268 host checks.

**Still to come (STEP5-6-WEBCORE.md):** paths and strokes (6.3), glyph runs
(6.4), layers, shadows and blend modes (6.5), then Cairo and Pixman out of
the link (6.6).

### 1.6 State at a glance

| Piece | Merged | On an Amiga |
| --- | --- | --- |
| openamigacairo (static Cairo and Pixman) | yes | smoke test, Instance-24, 4 October; inside the browser engine |
| pixman.library 2.0 | yes | PixBench on the AC090, 17 October; no users |
| opengpu.library 0.18 vectors (v1.6) | yes, 23 October | in the ROM as 0.20; first run by this assessment's lab, 25 October, on the AC090 only |
| OpenBrowser patch 0076 (GraphicsContextOpenGPU) | no (PR #104) | not built |
| OpenBrowser steps 6.3 to 6.6 | no | not written |

## 2. What GFOffice would gain

What GFOffice draws today:
- OpenWrite 2.0.13 draws OpenLayout's display list with graphics.library and
  CyberGraphX. Text uses 1-bit TextFonts from openfont.library or diskfont.
  Rules are boxes or `Draw`.
- Pictures are box-averaged once, down to about a megapixel, when they are
  cached. They are then point-sampled on every repaint (`draw_picture`).
- OpenCalc 0.1.19 draws charts with fills, 1-pixel lines and text. W131's
  branch adds pies, doughnuts and scatters through a small drawer
  (`src/calc/oc_gfx.h`: line, even-odd polygon, text).

| Gain | Through OpenGPU | Matters most for |
| --- | --- | --- |
| Smooth picture scaling | `COMPOSITE` with `BILINEAR` for opaque pictures, which OpenWrite already makes opaque when it caches them. A `PATH_FILL` with a picture as paint for turned ones. Zooming out below about 70% also needs a box reduction first: at 50% bilinear still breaks thin lines up (section 6) | The 32-page plan's full-page cover. It is the largest thing on screen, it's redrawn on every scroll of page 1 and in the navigator, and point sampling breaks up its fine lines and edges at any zoom but 100% |
| Anti-aliased lines | `PATH_STROKE` with widths, round joins and caps, and dashes | Line series, axes at an angle, borders, arrows |
| Curves | `PATH_FILL` with cubic curves (a pie slice is 1 to 4 of them) | Pies and doughnuts (W131's), rounded rectangles, ellipses, OpenPresentation's shapes |
| Gradients | Linear, radial and conic paint. Within 1 level of Cairo's | Slide backgrounds and shape fills, Excel's gradient-filled bars |
| Shadows | `BLUR` | Slide shapes and pictures |
| Alpha | `COMPOSITE_PD` | PNG logos on coloured shading. Today the paper is assumed white |
| PDF and print through Cairo's PDF surface | **Not a gain.** It isn't built, and text through user fonts would become Type 3 glyphs. libowf's writer already draws text in base-14 fonts that stays searchable, rectangles, rounded rectangles, ellipses, polygon paths, dashes, pictures and links, and PDF viewers anti-alias it themselves | Nothing. Pies in PDF need one more call in `owf_pdf` (a path with curves) |

For the 8-sheet workbook, the chart's columns and gridlines are rectangles
on whole pixels. Anti-aliasing leaves them exactly as they are. Its gain is
in the series lines, sloping or round shapes, and pies. The cover picture of
the Word plan gains the most of anything in the two yardsticks.

## 3. Costs

### 3.1 Size and memory

| What | Bytes |
| --- | --- |
| A program linked with Cairo, Pixman, libpng and zlib (CAIRO_FONTS=user), stripped | +1,094,920 (drawbench 1,263,544 against 168,624 without Cairo) |
| `libcairo.a` / `libpixman-1.a` code | 502,224 / 397,868 |
| The `ft` build would add FreeType, fontconfig and Expat | +0.95 MiB (OpenBrowser's link map) |
| opengpu.library 0.18 (shared; in AmigaChrome's ROM as 0.20) | 173,176, of which v1.6 is 20,812 |
| What a GFOffice program links for OpenGPU | about 5.6 KB (the batch builder) |

OpenWrite and OpenCalc are separate programs, so Cairo would cost each of
them 1.1 MB. The readme aims at 8 MB of Fast RAM for small documents.

At run time:
- After all the drawing runs the program held 16 KB more than before them.
  Cairo's peak wasn't measured.
- PNG writing left 6 to 8 MB in libnix's pools. GFOffice wouldn't write PNG
  through Cairo.
- OpenGPU's paths need work memory from the program: at least 32 bytes per
  pixel of the target's width, which is 26 KB at 800 pixels. 1 MB and 32 MB
  timed the same within noise (section 6).
- An ARGB32 buffer is 1.5 MB for an 800 × 480 chart, and 3.6 MB for a page
  at 100%.

### 3.2 Speed on a 68040

The figures are from the AC090 (Instance-32's 68040 JIT) and are in
section 6. These ratios hold on any 68040, since all of it is 68k code:

| Scene (AC090, best of 5) | Today | OpenGPU 68k core | Cairo | OpenGPU on AmigaChrome (host) |
| --- | --- | --- | --- | --- |
| Pie, 6 slices, white borders | not drawn (W131: polygons) | 52-78 ms | 6-8 ms | 2 ms |
| 4 line series of 24 points, 3 px, round joins | 0.1 ms (1 px) | 70-89 ms | 16-22 ms | 2 ms |
| 800 × 480 three-stop gradient | none | 128-184 ms | 209-305 ms | 2 ms |
| Full page picture at 100% (794 × 1123) | 3.7-4 ms (point) | 76 ms (`COMPOSITE` bilinear), 491-726 ms (path pattern) | 23-34 ms (bilinear) | 2 ms |
| The same at 50% (397 × 561) | 1-1.2 ms | 20 ms, 122-196 ms | 8-11 ms bilinear, 131-148 ms GOOD | 2 ms |

- **On AmigaChrome** the vector commands go to the host. Every batch took
  about 2.0 ms whatever was in it, which is the hand-over and fence wait,
  not the drawing. That's 4 to 150 times faster than Cairo on the AC090.
  One batch per paint is the rule, as the browser's patch 0076 has it.
- **Without a driver** (a real 68040, or no OpenRTG):
  - OpenGPU's core is 4 to 9 times slower than Cairo for anti-aliased
    paths, and 3 times slower for a bilinear `COMPOSITE`.
  - It is 15 to 20 times slower for a picture painted through a path.
  - It is faster than Cairo for gradients.
  - Today's drawing is 7 to 700 times cheaper than either.
- **On a real 68040 (an estimate):** no real 68040 timing exists. SysInfo's
  register loop puts the AC090 at about 300 times an A4000's 25 MHz 68040
  (about 6,000 against 19 MIPS). Pixel loops that touch memory gain less
  from the JIT. So read 1 ms here as roughly 0.05 to 0.3 s on that machine:
  - the pie takes 3 to 17 s through OpenGPU's core, and half a second to
    two seconds through Cairo;
  - a smoothly scaled cover takes seconds through either;
  - charts and scaled pictures must be drawn once and cached, not redrawn
    on every scroll.

### 3.3 Palette (8-bit) screens

- OpenGPU won't draw paths or blends into CLUT8 or INDEX8.
- Anti-aliasing on a palette screen would mean drawing ARGB and mapping
  every edge shade to a pen. With OpenGadTools' shared pens that costs pens
  and looks blotchy.
- Today OpenWrite shows a picture on a screen of 8 bits or fewer as a
  labelled frame, and OpenCalc draws charts in pens.
- **Proposal:** palette screens keep today's drawing exactly, and
  anti-aliasing is for 15, 16, 24 and 32-bit screens. OpenLayers does the
  same: it composites only 16 and 32-bit RTG screens.
- Dithered pictures on AGA would be a separate piece of work (question 3).

### 3.4 Fonts

**Cairo's text** would be one of two things:
- FreeType through cairo-ft. FreeType reads TrueType, OpenType and Type 1,
  but not Agfa Intellifont, so OS 3.2's CGTimes and CGTriumvirate, which
  OpenWrite uses through diskfont today, would be gone. FreeType is also
  excluded from the browser by its decisions 2 and 3.
- Or user fonts on OpenFont, as the browser does.

**Either way the text would look different:**
- grey anti-aliased glyphs instead of crisp 1-bit ones;
- different hinting and weight;
- fractional advances.

**And the drawing must match the layout.** OpenWrite lays text out with the
widths of the fonts it draws with (`ow_fontwidths`), and places glyphs one
by one when a font runs wide. A drawing font that measures differently moves
words against the layout.

So text stays on OpenFont and diskfont TextFonts. Anti-aliased text, if Dale
wants it, should come later as OpenFont A8 glyphs drawn with OpenGPU's
`MASK`. That is the browser's step 6.4, and it needs OpenWrite's measuring
moved to the same outlines.

### 3.5 Other costs

**Requirements:**
- Cairo needs a 68040 with an FPU, and FPCR set in every task.
- GFOffice needs no FPU today. libowf even carries its own soft doubles so
  as not to touch FPCR.
- OpenGPU uses integers only.

**opengpu.library must be present:**
- It is in the AmigaChrome ROM.
- On other Amigas it would have to be shipped or found (question 6).
- Programs ask `OGPU_Query` and fall back when it's missing or answers NONE.

**Pixels differ a little from Cairo's** (section 6):
- gradients within 1 level, and scaled pictures within 4;
- pie edges within 22, on 317 pixels;
- lines on 2% of pixels, where a stroke overlaps itself at sharp turns,
  which is OpenGPU's documented quarter-pixel winding. It isn't visible at
  normal size.

**Two drawing paths to keep:** the classic path for palette screens and
"lite" machines, and the anti-aliased one.

## 4. Recommendation

**Adopt OpenGPU's vector drawing, behind GFOffice's own small drawers. Don't
link Cairo or Pixman.**

The order:

1. **Pictures in OpenWrite.**
   - `draw_picture` becomes one OpenGPU `COMPOSITE` with `BILINEAR` per
     picture and paint, into a buffer of the picture's visible part, then
     `WritePixelArray` as now.
   - Where memory is short, use bands of rows. In each band the picture is
     placed at its full size, reaching above and below the band (points are
     signed), so the bands meet without seams.
   - Below about 70% zoom, box-average the cached picture to the next power
     of two first. OpenWrite's `image_read_scaled` already does this kind of
     reduction.
   - When `OGPU_Query` names the CPU back end, keep point sampling while
     scrolling. Draw and cache the smooth version per zoom once the view
     settles, if memory allows.
   - This is the largest visible gain, and it touches one function.
2. **Charts in OpenCalc, through W131's `oc_gfx` drawer.**
   - An OpenGPU drawer: a polygon becomes an even-odd `PATH_FILL`, and a line
     becomes a `PATH_STROKE`.
   - Before that, widen the interface: sub-pixel coordinates (24.8 or
     twips), and arcs as arcs rather than polygons of whole pixels.
     Otherwise most of the anti-aliasing is thrown away.
   - Draw a chart once into an ARGB32 image per size and zoom, and redraw it
     only when its data changes.
3. **OpenPresentation's slides.** Shapes, gradients and shadows through the
   same drawer, once OpenLayout has the shape op that
   Design-OpenPresentation.md 5.2 asks for (`OL_OP_SHAPE`, with curves).
4. **Text: no change.** Later, if Dale wants it, OpenFont A8 glyph runs
   through OpenGPU, after the browser's step 6.4.
5. **PDF and print: no change.** libowf's writer gains a curved path for
   pies.

**Risks:**

| Risk | Answer |
| --- | --- |
| v1.6 is two days old. It is tested on Linux, and here on the AC090 through ACRTG.gpu, never on real hardware | Gate everything on `OGPU_Query`. Keep today's drawing as the fallback, and a setting to force it |
| The 68k core is slow, and much slower than Cairo for scaled pictures painted through a path | Use `COMPOSITE` for opaque pictures, and cache. Ask for 68k fast paths (below) |
| About 2 ms per batch on AmigaChrome | One batch per paint, not per shape. Size the bands so a page is a few batches |
| Memory for ARGB buffers (1.5 to 3.6 MB) against the 8 MB aim | Draw in bands. Cache only on machines with room |
| Palette screens | Classic drawing there, unchanged |

**What has to land in other teams' repositories first.** That is their work,
asked of them, not done in a fork.

- **openamigartg (OpenGPU, Teams 3 and 4):**
  - an Amiga-side test of v1.6, at least qemu-m68k in CI and a run on a real
    68040 or a PiStorm;
  - 68k fast paths in `ogpu_core.c`: an axis-aligned bilinear scale with
    premultiplied colour, so pictures need neither `COMPOSITE`'s straight
    colour nor the path pattern, which is 15 to 20 times slower than Cairo;
    and span fills for simple paths;
  - a word on whether the 2 ms per batch on ACRTG.gpu is a polling interval
    that could shrink.
- **OpenRTG and OpenLayers (Team 4):** a way to point an OpenGPU batch at a
  window's own surface, clipped by its layer. Then GFOffice could draw
  straight into the window instead of a band plus `WritePixelArray`.
  openamigapixman's gap 12 asks the same for screens.
- **openamigabrowser (Team 3):** nothing is required. If `OpenGPUPaintAmiga.c`
  stays a plain C file, GFOffice could share it rather than write its own
  fills and pictures.
- **openamigalayout:** `OL_OP_SHAPE` with curves, for slides. This one is
  ours to write.
- **openamigacairo:** nothing, unless Dale wants Cairo after all (question 1).

## 5. Questions for Dale

1. Does "no cairo, no pixman" hold for GFOffice as it does for the browser,
   so that GFOffice draws through OpenGPU? Or do you want Cairo for something
   in particular?
2. On real 68040s without a driver, should anti-aliased charts and smooth
   pictures be on (cached, seconds per chart)? Or AmigaChrome only, with
   today's drawing as the fallback, like OpenGadTools' lite mode?
3. Palette and AGA screens: keep today's drawing there (no anti-aliasing,
   pictures as labelled frames)? Or is dithered pictures on AGA wanted?
4. May Team 5 ask Teams 3 and 4 for the 68k fast paths and for an OpenGPU
   target for a window? Or should Team 5 write them in openamigartg, under
   their review?
5. Text: crisp 1-bit as now, or anti-aliased later through OpenFont and
   OpenGPU, once the browser's glyph runs exist?
6. On Amigas that aren't AmigaChrome, is opengpu.library a requirement
   (shipped by OpenUp), or optional, probed at run time?

## 6. Measured: drawbench

`tools/drawbench/drawbench.c`. On 25 October 2026 it ran in lab t5-w139, a
copy of Instance-32:
- AmigaOS 3.2 with the open ROM: graphics.library 47.100, layers.library
  47.107, opengpu.library 0.20, ACRTG.gpu 1.3;
- AC090 68040 JIT, E-clock timing;
- three runs, of 5 and 7 rounds, with the host busy with other labs. Ranges
  are the bests of the three runs.

It draws into ARGB32 memory, white before each round and not timed:

| Scene | What it draws |
| --- | --- |
| pie | Six slices of a 200 px radius from 12 o'clock, each filled, then a 2 px white border with round joins. Cairo: `cairo_arc`. OpenGPU: the same arcs as cubics |
| lines | 9 gridlines as 1 px fills, and 4 series of 24 points stroked 3 px with round joins and caps. "Today" is 1 px Bresenham lines |
| gradient | A three-stop linear gradient over 800 × 480 |
| page100 and page50 | A synthetic 860 × 1216 picture (fine diagonal lines and a checker) scaled to 794 × 1123 and 397 × 561. Today's way is OpenWrite's `draw_picture` loop. OpenGPU: `COMPOSITE` with `BILINEAR`, and a `PATH_FILL` with the picture as a padded bilinear pattern. Cairo: `BILINEAR` and `GOOD` |

Every result was written as PNG and compared:

| Comparison | Largest difference | Pixels over 8 |
| --- | --- | --- |
| opengpu.library (host) against the 68k core | identical, every scene | 0 |
| Cairo against the core, gradient | 1 | 0 |
| Cairo against the core, pictures | 4 | 0 |
| Cairo against the core, pie | 22 | 317 of 384,000 |
| Cairo against the core, lines | 182 | 7,987 of 384,000, where strokes cross themselves |
| Cairo `GOOD` against bilinear at 50% | 75 | 31% |

At 50%, `GOOD` is Cairo's box filter, which bilinear lacks.

One run, best and median of 5, in µs (work memory 1 MB):

```
pie       ogpu-core-68k             55867     62827
pie       cairo                      7402      7707
pie       opengpu.library            2074      2099
lines     today-1px                   101       107
lines     ogpu-core-68k             70185     74283
lines     cairo                     15604     17073
lines     opengpu.library            2029      2043
gradient  ogpu-core-68k            128307    136810
gradient  cairo                    209466    224021
gradient  opengpu.library            2002      2005
page100   today-point                3748      3879
page100   ogpu-core-68k-composite   76470     80198
page100   ogpu-core-68k-bilinear   491447    519879
page100   cairo-bilinear            25903     27991
page100   cairo-good                25934     36337
page100   opengpu.library            2003      2006
page50    today-point                1102      1317
page50    ogpu-core-68k-composite   20109     23733
page50    ogpu-core-68k-bilinear   126502    127450
page50    cairo-bilinear             7676      8268
page50    cairo-good               130975    137333
page50    opengpu.library            2000      2002
```

**What it doesn't measure:**
- text;
- the cost of `WritePixelArray` to a window;
- palette screens;
- Cairo's peak memory;
- a real 68040.

## 7. Reproducing

```sh
# openamigacairo at 20a18bb, tarballs from its SOURCES
CAIRO_FONTS=user DEPS_PREFIX=$STOVE/prefix/m68k-amigaos/port-deps \
  PREFIX=$PWD/out-user ./build.sh
# openamigartg at 8a45a37
CAIRO=<that PREFIX> OPENAMIGARTG=<checkout> tools/drawbench/build.sh
```

On the Amiga, `drawbench [ROUNDS] [png] [work=MB]`. With `png`, each result
is also written to `RAM:db-<scene>-<how>.png`.

On Instance-32's copy the program found opengpu.library 0.20, and its
`PATH_FILL` answered FULL from back end 1, "ACRTG.gpu on the Cradle".
