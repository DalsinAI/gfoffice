# GFOpenDiag against draw.io and Dia

GFOpenDiag is the diagram editor of the GFOffice Suite, built to the design in
amicradle-teams `designs/GFOPENDIAG-20261113.md` (steps D1 to D6). This sheet
scores it against draw.io (diagrams.net) and against Dia, the same way the
9 November gap analysis scored the other apps
(docs/gfoffice/PARITY-GAPS-2026-11-09.md). The score is a practical,
workflow-weighted estimate, not a menu count. It asks whether an ordinary user
can finish the comparator's usual diagram workflow, with editing, Undo, save
and reload all working. A feature that only shows or only exports gets less
credit than one that edits and round-trips.

Only work merged on main is scored. Statuses are Done, Part and Gap, as in the
other sheets.

## Score

| Date | Main | Version | Against draw.io | Against Dia | What moved it |
| --- | --- | --- | ---: | ---: | --- |
| 16 Nov 2026 | a49df06 | GFOpenDiag 0.1.0 | **63%** | **69%** | #207, #208, #210 and #314 (Mermaid, DOT, GraphML and Excalidraw export); #316 (Cut, Copy, Paste, Duplicate), #317 (drawing order, Group, Ungroup), #318 (Align, Distribute) and #320 (their Edit and Arrange menus and ARexx commands) |
| 16 Nov 2026 | e646263 | GFOpenDiag 0.1.0 | **54%** | **59%** | First score. D1 and D2 (the model, `.drawio` read and write, SVG and PNG with the diagram inside), #189 (D3, the editing core), #192 (D4, the canvas), #195 (Style window), #196 (layers), #197 (shapes palette), #198, #199, #200 and #201 (D5: mind map, its keys, outlines, Import and Export Outline), #202 (PDF), #203 (ARexx fixes), #204 (PNG and IFF ILBM), #205 (EPS) |

The Dia score is higher because Dia has no pages, no mind maps and a smaller
shape library, so GFOpenDiag already meets it in those rows. Dia is still ahead
in connector kinds and its own file format.

## Feature rows

The weight is each area's share of an ordinary diagram workflow, out of 100.
The credit is how much of that area GFOpenDiag does against each comparator.

| Area | Weight | Status | draw.io credit | Dia credit | What GFOpenDiag does | What is missing |
| --- | ---: | --- | ---: | ---: | --- | --- |
| Canvas and selection | 12 | Part | 80% | 80% | Draw, move, resize and delete shapes; click, Shift-click and lasso selection; Select All and None; cursor-key nudge (Shift for ten); zoom in, zoom out, actual size, fit; grid shown; snap to grid | Rulers, guides, snap to other shapes' edges |
| Undo, save and reload | 10 | Done | 100% | 100% | Undo and Redo for every edit, one step per operation; `.drawio` is the native file, plain or compressed, and round-trips with diagrams.net; unsaved-change check on quit | |
| Shapes and stencils | 12 | Part | 25% | 35% | Box, rounded box, ellipse, rhombus and text tools; a flowchart palette of 8 shapes (process, decision, terminator, data, predefined process, preparation, triangle, circle); other draw.io shapes are kept and written back | The design's cylinder, cloud, document, actor and picture shapes; UML, network and ER sheets; the Amiga stencil set; user stencil files; shape search |
| Connectors | 12 | Part | 60% | 60% | Connector tool joins two shapes or leaves an end loose; ends stay glued and follow when shapes move; elbow (orthogonal) routing from the facing sides or named ports; arrowheads; connector labels | Straight and curved kinds to pick; dragging waypoints and bends; picking a port by mouse; jumps at crossings; routing round shapes |
| Text and labels | 6 | Part | 50% | 70% | Double-click or Return types a label; labels wrap and align as draw.io's; font size and colour; draw.io's HTML labels shown as text | Bold, italic and mixed runs in a label; rotated text; spelling |
| Style | 8 | Part | 55% | 70% | Style window for one or many cells: fill, line colour, line width, font colour, font size, dashed, rounded; one OK is one Undo step | Shadow, opacity, gradients, rotation and flip, line ends to choose, copy style |
| Arrange and clipboard | 10 | Part | 85% | 90% | Cut, Copy, Paste and Duplicate, keeping connectors (one to a shape left behind keeps its end loose), and between diagrams; Bring to Front, Send to Back, Bring Forward and Send Backward; Group and Ungroup, as draw.io writes groups; Align (sides, centres, middles) and Distribute across or down; each one Undo step, in the Edit and Arrange menus and by ARexx | Rotate; copying to other programs through the Amiga clipboard |
| Pages and layers | 6 | Part | 80% | 100% | Several pages, Add Page, Next and Previous Page; a Layers window to add, rename, show, hide and lock layers (nothing new lands on a hidden or locked layer) | Deleting, renaming and reordering pages in the window. Dia has no pages, so it is met there |
| Layout and mind maps | 6 | Part | 50% | 80% | Mind-map mode: new map, add child and siblings, fold, move up and down, delete a branch, lay out (one Undo step), keys as in the design, locked nodes kept in place | Tree, layered and organic layouts for ordinary diagrams |
| Export | 8 | Part | 90% | 80% | SVG and PNG with the diagram inside (both reopen as diagrams), PDF, EPS, IFF ILBM; Mermaid, Graphviz DOT, GraphML and Excalidraw; outlines as FreeMind `.mm`, OPML and Markdown | JPEG, HTML, VSDX; a page size and scale requester |
| Import and other formats | 6 | Part | 40% | 30% | `.drawio` and `.xml`, draw.io's own SVG and PNG, outlines from `.mm`, OPML and Markdown | VSDX, `.dia`, ODG, CSV; Mermaid, DOT, GraphML and Excalidraw in (they are export only) |
| Print | 2 | Gap | 0% | 0% | ARexx PRINT says to export and print the SVG | Printing from the window |
| Find | 2 | Gap | 0% | 0% | | Find and replace in labels |
| **Total** | **100** | | **63%** | **69%** | | |

The ARexx port (about 60 commands, tested by `tests/arexx/diag.rexx`) is not
counted: draw.io and Dia have no ARexx, and their plugin and Python scripting
is outside the everyday workflow scored here.

## Waiting to merge

Nothing for GFOpenDiag is waiting to merge. The menus from #320 still owe a lab
run, and the m68k build of #316 to #320 is owed until the os32-gcc16 stove is
back after the re-wire.

## The three biggest gaps

1. Shapes and stencils: only 8 flowchart shapes; no UML, network or Amiga sets and no pictures (9 points against draw.io).
2. Connectors: no straight or curved kind, no waypoint editing and no port picking by mouse (5 points).
3. Style: no shadow, opacity, gradients, rotation or line ends to choose (4 points against draw.io).
