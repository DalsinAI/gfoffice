# GFOpenDraw: suite integration and parity path

Dale chose GFOpenDraw as GFOffice's drawing application on 8 November 2026,
using the existing DPaint-inspired GFOpenPaint implementation rather than
starting again. Its repository is now `DalsinAI/gfopendraw`; GitHub redirects
the former name.

## Delivered base: 0.2.0 RC2

- Indexed pictures from 2 to 256 colours; palette mixing and spread; colour
  cycling; zoom, scrolling and full screen.
- Freehand, dotted freehand, line, airbrush, outlined/filled rectangle, circle
  and ellipse, flood fill, colour pick and picked-up brushes.
- Colour, matte, replace and cycle paint modes; brush flip/quarter-turn;
  bounded region undo/redo.
- Amiga datatype input, safe IFF ILBM output and PNG output through
  `gfopenpng.datatype`.
- GadTools/GFOpenGadTools front end (`GFOpenDraw`) and an OpenLayout UI front
  end (`GFOpenDrawOL`) over the same application and engine.
- ARexx port `GFOPENDRAW.1` with open/new/save/clear/undo/redo/zoom/query
  commands.
- Portable `libgfopendraw.a` with `gfopendraw.h`, normal and ASan/UBSan host
  tests. The Amiga build emits an m68k SDK archive; both front ends link it.

GFOffice pins commit `ca07afc25e793e4c4fdc5ae16979bebccd203eaa` in
`deps/gfopendraw.ref` so a suite build is reproducible.

## Do not duplicate OpenLayout

The paint engine owns raster pixels, palettes, brushes, raster tools and their
undo deltas. It should not grow a second vector layout system.

Apache OpenOffice Draw-style pages should use the GFOffice positioned
page/object model already shared by Publisher and Presentation, with
GFOpenLayout supplying normalized shape geometry, display operations, page
layout and hit testing. The reusable boundaries are therefore:

| Responsibility | Reuse |
| --- | --- |
| Raster image and paint tools | `libgfopendraw.a` |
| Vector page, objects, grouping and resources | libowf deck/page model, factored where Draw semantics differ |
| Shapes, crop, display geometry and hit testing | GFOpenLayout |
| Text in objects | GFOpenWrite story/paragraph model and layout bridge |
| Screen controls/theme | GFOpenLayout UI and GFOpenGadTools |
| Bitmap file interchange | Amiga/GFOpen datatypes |
| ODG/ODP/PDF package and output primitives | libowf |

A raster picture can later be an editable vector-page object whose pixel
content opens in the paint engine; that is composition, not copied tools.

## Gaps to Apache OpenOffice Draw

The current application is a useful paint editor, not yet an OpenOffice Draw
replacement. Major gaps are:

1. Multi-page vector drawing documents and page/layer navigation.
2. Editable text boxes, styled text and text-on-shape behaviour.
3. Connectors, glue points, polylines, polygons and Bezier curves.
4. Multi-selection, group/ungroup, distribute, snap, grid and guides.
5. Arbitrary rotation/scale/flip, shape combination and richer fills.
6. Layers, master pages, dimensions and technical-drawing aids.
7. ODG and SVG import/export; vector PDF fidelity and clipboard objects.
8. Embedded charts/tables and cross-suite object linking.

DPaint-side gaps remain too: text, polygon/curve, gradients, symmetry,
stencils, brush resize/bend, spare page, CRNG persistence, picture-selected
screen modes and remembered window geometry.

## Delivery order

1. **D1 — suite ingestion (delivered in RC2):** coherent identity, repository,
   binaries, ARexx, reusable host/m68k engine SDK and GFOffice build pin.
2. **D2 — vector page shell:** consume the shared page/object model and
   OpenLayout renderer for new/open/save, page strip, selection and basic
   text/shape/picture objects. No new shape renderer.
3. **D3 — drawing interaction:** multi-selection, grouping, connectors,
   polygons/curves, snap/grid/guides, transforms and layers as reusable core
   operations shared back to Publisher and Presentation where appropriate.
4. **D4 — interchange:** ODG first, then SVG; PDF through the shared display
   path; clipboard object exchange across GFOffice.
5. **D5 — advanced paint:** close the remaining DPaint-side tool and palette
   gaps inside `libgfopendraw`, independently reusable by image objects and
   GFOpenIconEdit.
