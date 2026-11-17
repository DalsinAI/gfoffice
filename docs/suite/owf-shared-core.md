# GFOffice's shared core: one copy of the drawing code

Version 0.1 | 17 November 2026 | Team 2 (Arnold), MIT

Dale (17 Nov): Diag, Pressmaster and Draw get libraries like Write, Calc and
Presentation, and the programs share code where it makes sense. Holly's plan
(gfoffice-rc/LIBRARY-SPLIT-PLAN-20261117.md) names the code two or more
programs had their own copies of. This page says where the one copy lives,
what it is called, and how each program and library reaches it.

## Where the one copy lives

In libowf (`libowf/src`, headers in `libowf/include`): the source every
GFOffice library and linked program is built from. Each library links what
its calls reach from `libowf.a` (owf-libraries.md, section 2, which measured
a separate core library against a copy in each and chose the copies). So
"in the core" means one source file, not one more library to open.

| Module | Header | Calls | Was |
| --- | --- | --- | --- |
| `owf_pdf.c`, `owf_pdffonts.c` | `owf_pdf.h` | the page PDF writer | already one copy; owfpresent.library exports it |
| `owf_picture.c` | `owf_picture.h` | `owf_picture_png`, `owf_picture_ilbm` (PNG's CRC is libowf's `owf_crc32`) | `src/deck/ow_picture.c` (Presentation, Diag) |
| `owf_fill.c` | `owf_fill.h` | `owf_fill_sin16k`, `owf_fill_blend`, `owf_fill_gradient_band` | `src/deck/opr_gradient.c` (Presentation, Pressmaster), sine and blends also in `owf_pdf.c` |
| `owf_print.c` | `owf_print.h` | the print core: `owf_print_bleed`, `owf_print_mark_gap`, `owf_print_margin`, `owf_print_crop_lines`, `owf_print_bleed_colour`, `owf_print_booklet_sides`, `owf_print_booklet_side` | `src/deck/opr_pdf.c` (Presentation's and Pressmaster's PDF) |

The old names stay as thin wrappers so no caller changes: `opr_png_encode`,
`opr_ilbm_encode` (`ow_picture.h`), `opr_gradient_band`, `opr_pdf_margin`
and `opr_booklet_side` (`openpresentation_core.h`). New code calls the
`owf_` names.

## The print core (Petersen, 17 November 2026)

Pressmaster's printing splits in two. What a printer's page needs worked
out is plain arithmetic on the page model, and is `owf_print.c`: the room
a bleed and crop marks take, the marks' eight lines, the colour that runs
into the bleed, and the order a folded booklet's pages print in. It draws
nothing, so a PDF, a screen preview and a printer driver all use the same
numbers. owfpub.library 3.10 exports it (7 calls).

Drawing a laid-out page into the PDF (`opr_export_pdf` in
`src/deck/opr_pdf.c`) stays with the programs for now: it walks a
GFOpenLayout display list, and no GFOffice library holds GFOpenLayout.
Moving it means either a layout library the page libraries open, or
GFOpenLayout linked into owfpub.library and owfpresent.library. That is
a suite decision, not part of this step.

With it came `owf_sys_file_size` (libowf's own, `owf_internal.h`): a
file's length without reading it. `opp_merge_load_file` asks it first, so
a data file over `OW_MERGE_MAX_BYTES` is refused before any of it is read.

## Rules for the shared modules

- Plain C99, integer only, no Amiga headers, no GFOpenLayout headers: a
  library or GFOpenDraw can build them alone. Limits that must match
  GFOpenLayout's are checked where both are seen (`opr_gradient.c` stops
  the build if `OL_MAX_SHAPE_POINTS` and `OWF_FILL_MAX_OUTLINE` differ).
- Memory is the caller's library's or program's `malloc`; a library that
  exports a writer documents that its result is freed with its `owf_free`.
- A new shared piece gets a golden test against the copy it replaces
  (`tests/shared/test_owf_shared.c`) in the PR that moves it.

## Next steps

1. **Hatching** (Arnold): the hatch lines on the deck-draw branch
   (`opr_hatch_lines`) go into `owf_fill.c` as `owf_fill_hatch_lines`
   before that branch opens, so Diag and Pressmaster draw the same hatches.
2. **Exports** (with each library's owner): owfpresent.library,
   owfpub.library and the new owfdiag.library add the `owf_picture_*` and
   `owf_fill_*` calls at the end of their SFDs when a program on that
   library needs them. Calls are added only where a program uses them.
3. **Shapes** (Kristine, with Arnold): one outline module for diagram
   shapes, deck shapes and .pub shapes (`owf_dshape.c` is the start);
   gradient bands and hatches take its outlines as `xy` point lists, so the
   fill calls need no change when it lands.
4. **GFOpenDraw** (Camille): Draw's own PNG and ILBM savers
   (`app/op_file.c` in gfopendraw) are the remaining copy. Two ways, for
   Camille to pick: gfopendraw.library builds `owf_picture.c` from
   gfoffice's libowf (pinned, as gfoffice pins gfopenlayout), or GFOpenDraw
   opens the library that exports `owf_picture_png`. The interface is the
   one above: 24-bit RGB rows with a stride, a malloc'd file back.
5. **Write's PDF** (`out_pdf.c`) is a second PDF writer for flowing
   documents. Folding it onto `owf_pdf.c` changes Write's output, so it is
   a separate, tested change, not part of the split.
