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

The old names stay as thin wrappers so no caller changes: `opr_png_encode`,
`opr_ilbm_encode` (`ow_picture.h`) and `opr_gradient_band`
(`openpresentation_core.h`). New code calls the `owf_` names.

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
